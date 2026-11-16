#pragma once

#include <redfish-binding/Sensor_Sensor.hpp>
#include <redfish_client/core/config.hpp>
#include <sdbusplus/async.hpp>
#include <xyz/openbmc_project/Association/Definitions/aserver.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Critical/aserver.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/HardShutdown/aserver.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Warning/aserver.hpp>
#include <xyz/openbmc_project/Sensor/Value/aserver.hpp>
#include <xyz/openbmc_project/Sensor/Value/common.hpp>

#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace redfish_client::core
{

using SensorValueIntf = sdbusplus::common::xyz::openbmc_project::sensor::Value;

class Sensor;

class WarningTag;
using WarningInterface = sdbusplus::async::server_t<
    WarningTag,
    sdbusplus::aserver::xyz::openbmc_project::sensor::threshold::Warning>;

class CriticalTag;
using CriticalInterface = sdbusplus::async::server_t<
    CriticalTag,
    sdbusplus::aserver::xyz::openbmc_project::sensor::threshold::Critical>;

class HardShutdownTag;
using HardShutdownInterface = sdbusplus::async::server_t<
    HardShutdownTag,
    sdbusplus::aserver::xyz::openbmc_project::sensor::threshold::HardShutdown>;

enum class ThresholdLevel
{
    Warning,
    Critical,
    HardShutdown,
};

enum class ThresholdDirection
{
    High,
    Low,
};

struct Threshold
{
    ThresholdLevel level;
    ThresholdDirection direction;
    double value;
};

// A sensor also implements Association.Definitions, not just Value, so it can be
// linked back to the chassis that owns it.
using SensorInterfaces = sdbusplus::async::server_t<
    Sensor,
    sdbusplus::aserver::xyz::openbmc_project::association::Definitions,
    sdbusplus::aserver::xyz::openbmc_project::sensor::Value>;

// The live D-Bus object for one Redfish sensor. Created lazily, only once a
// reading with a known unit has arrived, so sensors that never yield a valid
// reading stay off the bus. Unit and range are fixed at construction because
// Redfish never changes them mid-life; only the value moves afterwards.
class Sensor : public SensorInterfaces
{
  public:
    Sensor() = delete;
    Sensor(const Sensor&) = delete;
    Sensor(Sensor&&) = delete;
    Sensor& operator=(const Sensor&) = delete;
    Sensor& operator=(Sensor&&) = delete;
    ~Sensor() = default;

    // Sourced from the interface definition so it stays in step with the
    // namespace the objects actually live under.
    static constexpr auto rootPath = SensorValueIntf::namespace_path::value;

    // Initial values for a new sensor. Unit and range are fixed after
    // creation; threshold limits can be changed later over D-Bus.
    struct Metadata
    {
        SensorValueIntf::Unit unit;
        double minValue;
        double maxValue;
        std::vector<Threshold> thresholds{};
    };

    // A sensor whose unit is absent or unrecognized cannot be represented
    // faithfully, so it is skipped (nullptr) rather than published under a
    // guessed unit. The object path is composed as rootPath/ns/id.
    // Thresholds are taken from the target as reported, then the configured
    // overrides are applied on top of them.
    static std::unique_ptr<Sensor> create(
        sdbusplus::async::context& ctx, const std::string& ns,
        const std::string& id, const std::string& associationPath,
        redfish_binding::Sensor::Sensor& parsed,
        const ThresholdOverride& thresholds = {});

    // Stays silent when the value barely moved, so continuous polling does not
    // flood the bus with PropertiesChanged signals.
    auto updateValue(double current) -> sdbusplus::async::task<>;

    // Redfish reports units as free-form strings, so only a known set maps onto
    // D-Bus; an unknown unit yields nullopt. The range is a fallback default for
    // sensors that report none of their own.
    static std::optional<Metadata> toMaybeMetadata(const std::string& unitsStr);

    // The unit alone, for when the range is not needed.
    static std::optional<SensorValueIntf::Unit> toMaybeUnit(
        const std::string& unitsStr);

  private:
    Sensor(sdbusplus::async::context& ctx, const std::string& objectPath,
           const std::string& associationPath, double reading,
           const Metadata& metadata);

    auto shouldSkipSignal(double current) -> bool;

    void setThresholdLimit(sdbusplus::async::context& ctx,
                           const std::string& objectPath,
                           const Threshold& threshold);

    template <bool EmitSignal = true>
    bool checkThresholds(double current);
    double thresholdLimit(ThresholdLevel level,
                          ThresholdDirection direction) const;
    bool thresholdAlarm(ThresholdLevel level,
                        ThresholdDirection direction) const;
    template <bool EmitSignal>
    void setThresholdAlarm(ThresholdLevel level, ThresholdDirection direction,
                           bool alarm, double current);

    std::mutex lock;

    std::unique_ptr<WarningInterface> warningIntf;
    std::unique_ptr<CriticalInterface> criticalIntf;
    std::unique_ptr<HardShutdownInterface> hardShutdownIntf;

    double lastNotifiedValue = std::numeric_limits<double>::quiet_NaN();
};

} // namespace redfish_client::core
