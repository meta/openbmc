#include <redfish_client/core/sensor.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace redfish_client::core
{

namespace
{

std::optional<double> toMaybeDouble(redfish_binding::Property<double>& property)
{
    if (property.hasValue())
    {
        return property.value();
    }
    return std::nullopt;
}

// Never alarms for an unset (NaN) limit or a missing (NaN) reading.
constexpr bool evaluateAlarm(ThresholdDirection direction, double current,
                             double limit)
{
    return direction == ThresholdDirection::High ? current >= limit
                                                 : current <= limit;
}

// Adds one of the target's Redfish thresholds, if it reports a reading for it.
void addTargetThreshold(
    std::vector<Threshold>& thresholds, ThresholdLevel level,
    ThresholdDirection direction,
    redfish_binding::Property<redfish_binding::Sensor::Threshold>& entry)
{
    if (!entry.hasValue())
    {
        return;
    }
    auto reading = toMaybeDouble(entry.value().getReading());
    if (!reading.has_value())
    {
        return;
    }
    thresholds.push_back({level, direction, *reading});
}

// Applies one configured limit on top of what the target reported.
void applyThresholdOverride(std::vector<Threshold>& thresholds,
                            ThresholdLevel level, ThresholdDirection direction,
                            const ThresholdOverride::Limit& limit)
{
    auto it = std::ranges::find_if(thresholds, [&](const Threshold& t) {
        return t.level == level && t.direction == direction;
    });
    switch (limit.mode)
    {
        case ThresholdOverride::Mode::Inherit:
            break;
        case ThresholdOverride::Mode::Disabled:
            if (it != thresholds.end())
            {
                thresholds.erase(it);
            }
            break;
        case ThresholdOverride::Mode::Value:
            if (it != thresholds.end())
            {
                it->value = limit.value;
            }
            else
            {
                thresholds.push_back({level, direction, limit.value});
            }
            break;
    }
}

} // namespace

auto Sensor::toMaybeMetadata(const std::string& unitsStr)
    -> std::optional<Metadata>
{
    using Unit = SensorValueIntf::Unit;
    static const std::unordered_map<std::string_view, Metadata> table = {
        {"%", {Unit::Percent, 0.0, 100.0}},
        {"Cel", {Unit::DegreesC, -128.0, 127.0}},
        {"J", {Unit::Joules, 0.0, 100000.0}},
        {"Pa", {Unit::Pascals, 30000.0, 120000.0}},
        {"V", {Unit::Volts, 0.0, 255.0}},
        {"W", {Unit::Watts, 0.0, 3000.0}},
        {"A", {Unit::Amperes, 0.0, 255.0}},
        {"DegreesC", {Unit::DegreesC, -128.0, 127.0}},
        {"Volts", {Unit::Volts, 0.0, 255.0}},
        {"Watts", {Unit::Watts, 0.0, 3000.0}},
        {"Amperes", {Unit::Amperes, 0.0, 255.0}},
    };

    if (auto it = table.find(unitsStr); it != table.end())
    {
        return it->second;
    }
    return std::nullopt;
}

std::optional<SensorValueIntf::Unit> Sensor::toMaybeUnit(
    const std::string& unitsStr)
{
    return toMaybeMetadata(unitsStr).transform([](const Metadata& metadata) {
        return metadata.unit;
    });
}

std::unique_ptr<Sensor> Sensor::create(
    sdbusplus::async::context& ctx, const std::string& ns,
    const std::string& id, const std::string& associationPath,
    redfish_binding::Sensor::Sensor& parsed,
    const ThresholdOverride& thresholds)
{
    // Without a unit there is nothing meaningful to put on the bus.
    if (!parsed.getReadingUnits().hasValue())
    {
        return nullptr;
    }
    std::string unit = parsed.getReadingUnits().value();

    // An unmappable unit is skipped rather than published under a guessed one.
    auto maybeMetadata = toMaybeMetadata(unit);
    if (!maybeMetadata.has_value())
    {
        return nullptr;
    }
    Metadata metadata = maybeMetadata.value();

    if (auto& minProp = parsed.getReadingRangeMin(); minProp.hasValue())
    {
        metadata.minValue = minProp.value();
    }
    if (auto& maxProp = parsed.getReadingRangeMax(); maxProp.hasValue())
    {
        metadata.maxValue = maxProp.value();
    }

    using enum ThresholdLevel;
    using enum ThresholdDirection;

    auto& out = metadata.thresholds;
    if (auto& target = parsed.getThresholds(); target.hasValue())
    {
        auto& t = target.value();
        addTargetThreshold(out, Warning, High, t.getUpperCaution());
        addTargetThreshold(out, Warning, Low, t.getLowerCaution());
        addTargetThreshold(out, Critical, High, t.getUpperCritical());
        addTargetThreshold(out, Critical, Low, t.getLowerCritical());
        addTargetThreshold(out, HardShutdown, High, t.getUpperFatal());
        addTargetThreshold(out, HardShutdown, Low, t.getLowerFatal());
    }

    applyThresholdOverride(out, Warning, High, thresholds.warningHigh);
    applyThresholdOverride(out, Warning, Low, thresholds.warningLow);
    applyThresholdOverride(out, Critical, High, thresholds.criticalHigh);
    applyThresholdOverride(out, Critical, Low, thresholds.criticalLow);
    applyThresholdOverride(out, HardShutdown, High,
                           thresholds.hardShutdownHigh);
    applyThresholdOverride(out, HardShutdown, Low, thresholds.hardShutdownLow);

    auto objectPath = std::string(rootPath) + "/" + ns + "/" + id;
    return std::unique_ptr<Sensor>(
        new Sensor(ctx, objectPath, associationPath,
                   toMaybeDouble(parsed.getReading())
                       .value_or(std::numeric_limits<double>::quiet_NaN()),
                   metadata));
}

void Sensor::setThresholdLimit(sdbusplus::async::context& ctx,
                               const std::string& objectPath,
                               const Threshold& threshold)
{
    // Called only while constructing, before the interfaces are announced.
    constexpr bool emitSignal = false;
    const bool high = threshold.direction == ThresholdDirection::High;
    switch (threshold.level)
    {
        case ThresholdLevel::Warning:
            if (!warningIntf)
            {
                warningIntf =
                    std::make_unique<WarningInterface>(ctx, objectPath.c_str());
            }
            high ? warningIntf->warning_high<emitSignal>(threshold.value)
                 : warningIntf->warning_low<emitSignal>(threshold.value);
            break;
        case ThresholdLevel::Critical:
            if (!criticalIntf)
            {
                criticalIntf = std::make_unique<CriticalInterface>(
                    ctx, objectPath.c_str());
            }
            high ? criticalIntf->critical_high<emitSignal>(threshold.value)
                 : criticalIntf->critical_low<emitSignal>(threshold.value);
            break;
        case ThresholdLevel::HardShutdown:
            if (!hardShutdownIntf)
            {
                hardShutdownIntf = std::make_unique<HardShutdownInterface>(
                    ctx, objectPath.c_str());
            }
            high ? hardShutdownIntf->hard_shutdown_high<emitSignal>(
                       threshold.value)
                 : hardShutdownIntf->hard_shutdown_low<emitSignal>(
                       threshold.value);
            break;
    }
}

// Only the alarm state and its signals are published here.
template <bool EmitSignal>
bool Sensor::checkThresholds(double current)
{
    using enum ThresholdLevel;
    using enum ThresholdDirection;

    bool transitioned = false;
    for (auto level : {Warning, Critical, HardShutdown})
    {
        for (auto direction : {High, Low})
        {
            const bool alarm = thresholdAlarm(level, direction);
            const bool newAlarm = evaluateAlarm(
                direction, current, thresholdLimit(level, direction));
            if (newAlarm != alarm)
            {
                setThresholdAlarm<EmitSignal>(level, direction, newAlarm,
                                              current);
                transitioned = true;
            }
        }
    }
    return transitioned;
}

// NaN for a limit whose level has no interface.
double Sensor::thresholdLimit(ThresholdLevel level,
                              ThresholdDirection direction) const
{
    constexpr double unset = std::numeric_limits<double>::quiet_NaN();
    const bool high = direction == ThresholdDirection::High;
    switch (level)
    {
        case ThresholdLevel::Warning:
            if (!warningIntf)
            {
                return unset;
            }
            return high ? warningIntf->warning_high()
                        : warningIntf->warning_low();
        case ThresholdLevel::Critical:
            if (!criticalIntf)
            {
                return unset;
            }
            return high ? criticalIntf->critical_high()
                        : criticalIntf->critical_low();
        case ThresholdLevel::HardShutdown:
            if (!hardShutdownIntf)
            {
                return unset;
            }
            return high ? hardShutdownIntf->hard_shutdown_high()
                        : hardShutdownIntf->hard_shutdown_low();
    }
    return unset;
}

bool Sensor::thresholdAlarm(ThresholdLevel level,
                            ThresholdDirection direction) const
{
    const bool high = direction == ThresholdDirection::High;
    switch (level)
    {
        case ThresholdLevel::Warning:
            return warningIntf && (high ? warningIntf->warning_alarm_high()
                                        : warningIntf->warning_alarm_low());
        case ThresholdLevel::Critical:
            return criticalIntf && (high ? criticalIntf->critical_alarm_high()
                                         : criticalIntf->critical_alarm_low());
        case ThresholdLevel::HardShutdown:
            return hardShutdownIntf &&
                   (high ? hardShutdownIntf->hard_shutdown_alarm_high()
                         : hardShutdownIntf->hard_shutdown_alarm_low());
    }
    return false;
}

// Only called on an alarm change, so never for a level without an interface.
template <bool EmitSignal>
void Sensor::setThresholdAlarm(ThresholdLevel level,
                               ThresholdDirection direction, bool alarm,
                               double current)
{
    const bool high = direction == ThresholdDirection::High;
    switch (level)
    {
        case ThresholdLevel::Warning:
        {
            auto& intf = *warningIntf;
            high ? intf.warning_alarm_high<EmitSignal>(alarm)
                 : intf.warning_alarm_low<EmitSignal>(alarm);
            if constexpr (EmitSignal)
            {
                if (high)
                {
                    alarm ? intf.warningHighAlarmAsserted(current)
                          : intf.warningHighAlarmDeasserted(current);
                }
                else
                {
                    alarm ? intf.warningLowAlarmAsserted(current)
                          : intf.warningLowAlarmDeasserted(current);
                }
            }
            break;
        }
        case ThresholdLevel::Critical:
        {
            auto& intf = *criticalIntf;
            high ? intf.critical_alarm_high<EmitSignal>(alarm)
                 : intf.critical_alarm_low<EmitSignal>(alarm);
            if constexpr (EmitSignal)
            {
                if (high)
                {
                    alarm ? intf.criticalHighAlarmAsserted(current)
                          : intf.criticalHighAlarmDeasserted(current);
                }
                else
                {
                    alarm ? intf.criticalLowAlarmAsserted(current)
                          : intf.criticalLowAlarmDeasserted(current);
                }
            }
            break;
        }
        case ThresholdLevel::HardShutdown:
        {
            auto& intf = *hardShutdownIntf;
            high ? intf.hard_shutdown_alarm_high<EmitSignal>(alarm)
                 : intf.hard_shutdown_alarm_low<EmitSignal>(alarm);
            if constexpr (EmitSignal)
            {
                if (high)
                {
                    alarm ? intf.hardShutdownHighAlarmAsserted(current)
                          : intf.hardShutdownHighAlarmDeasserted(current);
                }
                else
                {
                    alarm ? intf.hardShutdownLowAlarmAsserted(current)
                          : intf.hardShutdownLowAlarmDeasserted(current);
                }
            }
            break;
        }
    }
}

Sensor::Sensor(sdbusplus::async::context& ctx, const std::string& objectPath,
               const std::string& associationPath, double reading,
               const Metadata& metadata) :
    SensorInterfaces(ctx, objectPath.c_str())
{
    // Populate everything before announcing: emit_added() publishes the object
    // as a unit, so a half-built sensor is never visible to consumers.
    constexpr bool emitSignal = false;

    if (!associationPath.empty())
    {
        std::vector<std::tuple<std::string, std::string, std::string>>
            associations;
        associations.emplace_back("chassis", "all_sensors", associationPath);
        this->associations<emitSignal>(associations);
    }

    this->unit<emitSignal>(metadata.unit);
    this->min_value<emitSignal>(metadata.minValue);
    this->max_value<emitSignal>(metadata.maxValue);

    for (const auto& threshold : metadata.thresholds)
    {
        setThresholdLimit(ctx, objectPath, threshold);
    }

    this->value<emitSignal>(reading);
    if (!std::isnan(reading))
    {
        lastNotifiedValue = reading;
    }
    checkThresholds<emitSignal>(reading);

    Definitions::emit_added();
    Value::emit_added();
    if (warningIntf)
    {
        warningIntf->emit_added();
    }
    if (criticalIntf)
    {
        criticalIntf->emit_added();
    }
    if (hardShutdownIntf)
    {
        hardShutdownIntf->emit_added();
    }
}

auto Sensor::updateValue(double current) -> sdbusplus::async::task<>
{
    std::lock_guard<std::mutex> guard(this->lock);

    if (!checkThresholds(current) && shouldSkipSignal(current))
    {
        this->value<false>(current);
    }
    else
    {
        lastNotifiedValue = current;
        this->value<true>(current);
    }
    co_return;
}

auto Sensor::shouldSkipSignal(double current) -> bool
{
    // Skip unless the reading moved by at least 1%; sub-percent jitter is not
    // worth waking every bus consumer. A reading appearing or disappearing
    // (a NaN transition) always signals.
    if (std::isnan(lastNotifiedValue) && std::isnan(current))
    {
        return true;
    }
    if (std::isnan(lastNotifiedValue) && !(std::isnan(current)))
    {
        return false;
    }
    if ((!std::isnan(lastNotifiedValue)) && std::isnan(current))
    {
        return false;
    }
    assert(!std::isnan(lastNotifiedValue));
    assert(!std::isnan(current));
    if (std::abs(lastNotifiedValue) < 1e-6)
    {
        // Baseline is ~0, so a percentage change is undefined; treat as no
        // change.
        return true;
    }
    auto changed =
        std::abs((current - lastNotifiedValue) / lastNotifiedValue * 100.0);
    if (changed >= 1.0)
    {
        return false;
    }
    return true;
}

} // namespace redfish_client::core
