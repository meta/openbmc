#include <nlohmann/json.hpp>
#include <org/freedesktop/DBus/Error/event.hpp>
#include <redfish-binding/Sensor_Sensor.hpp>
#include <redfish_client/core/sensor.hpp>
#include <xyz/openbmc_project/Association/Definitions/client.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Critical/client.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/HardShutdown/client.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Warning/client.hpp>
#include <xyz/openbmc_project/Sensor/Value/client.hpp>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace redfish_client::core;
namespace DBusError = sdbusplus::error::org::freedesktop::d_bus::Error;

class SensorTest : public ::testing::Test
{
  protected:
    static constexpr auto kBusName = "xyz.openbmc_project.test.NotARealService";

    static constexpr auto kSensorNamespace = "some";
    static constexpr auto kSensorId = "Metric";
    static constexpr auto kMetricPath =
        "/xyz/openbmc_project/sensors/some/Metric";

    static constexpr auto kAssociationPath =
        "/xyz/openbmc_project/inventory/system/NotARealBoard/XXX";

    SensorTest() : manager(ctx, Sensor::rootPath) {}

    ~SensorTest() noexcept override {}

    void SetUp() override
    {
        ctx.spawn(
            [](sdbusplus::async::context& ctx) -> sdbusplus::async::task<> {
                ctx.request_name(kBusName);
                co_return;
            }(ctx));
    }

    void TearDown() override
    {
        ctx.spawn(
            [](sdbusplus::async::context& ctx) -> sdbusplus::async::task<> {
                ctx.request_stop();
                co_return;
            }(ctx));
        ctx.run();
        ASSERT_TRUE(testBodyExecuted);
    }

    // Create (and publish) a Sensor from raw reading fields, exercising the
    // D-Bus object directly. The object path is composed as rootPath/ns/id.
    // Returns nullptr when the unit is absent or unrecognized (mirroring
    // Sensor::create).
    std::unique_ptr<Sensor> createSensor(
        const std::string& ns, const std::string& id,
        const std::string& associationPath, double reading,
        const std::string& unit, std::optional<double> minValue = std::nullopt,
        std::optional<double> maxValue = std::nullopt,
        std::optional<double> hardShutdownHigh = std::nullopt)
    {
        nlohmann::json json;
        json["Reading"] = reading;
        json["ReadingUnits"] = unit;
        if (minValue.has_value())
        {
            json["ReadingRangeMin"] = minValue.value();
        }
        if (maxValue.has_value())
        {
            json["ReadingRangeMax"] = maxValue.value();
        }
        auto parsed = redfish_binding::Sensor::parseSensor(json.dump());
        ThresholdOverride override;
        if (hardShutdownHigh.has_value())
        {
            override.hardShutdownHigh = {ThresholdOverride::Mode::Value,
                                         *hardShutdownHigh};
        }
        return Sensor::create(ctx, ns, id, associationPath, parsed, override);
    }

    auto getSensorClient()
    {
        using ValueClient =
            sdbusplus::client::xyz::openbmc_project::sensor::Value<>;
        return ValueClient(ctx).service(kBusName).path(kMetricPath);
    }

    auto getAssociationClient()
    {
        using AssociationClient =
            sdbusplus::client::xyz::openbmc_project::association::Definitions<>;
        return AssociationClient(ctx).service(kBusName).path(kMetricPath);
    }

    auto getWarningClient()
    {
        using WarningClient = sdbusplus::client::xyz::openbmc_project::sensor::
            threshold::Warning<>;
        return WarningClient(ctx).service(kBusName).path(kMetricPath);
    }

    auto getCriticalClient()
    {
        using CriticalClient = sdbusplus::client::xyz::openbmc_project::sensor::
            threshold::Critical<>;
        return CriticalClient(ctx).service(kBusName).path(kMetricPath);
    }

    auto getHardShutdownClient()
    {
        using HardShutdownClient = sdbusplus::client::xyz::openbmc_project::
            sensor::threshold::HardShutdown<>;
        return HardShutdownClient(ctx).service(kBusName).path(kMetricPath);
    }

    // Threshold limits shared by every threshold test below.
    static constexpr auto kThresholdUnit = "Cel";
    static constexpr double kWarningHigh = 80.0;
    static constexpr double kWarningLow = -10.0;
    static constexpr double kCriticalHigh = 90.0;
    static constexpr double kCriticalLow = -20.0;
    static constexpr double kFatalHigh = 100.0;
    static constexpr double kFatalLow = -30.0;

    // Builds a Redfish sensor reading JSON with all six threshold directions
    // populated at the kXxx limits above.
    static nlohmann::json sensorJsonWithThresholds(double reading)
    {
        nlohmann::json json;
        json["Reading"] = reading;
        json["ReadingUnits"] = kThresholdUnit;
        json["Thresholds"]["UpperCaution"] = {{"Reading", kWarningHigh}};
        json["Thresholds"]["LowerCaution"] = {{"Reading", kWarningLow}};
        json["Thresholds"]["UpperCritical"] = {{"Reading", kCriticalHigh}};
        json["Thresholds"]["LowerCritical"] = {{"Reading", kCriticalLow}};
        json["Thresholds"]["UpperFatal"] = {{"Reading", kFatalHigh}};
        json["Thresholds"]["LowerFatal"] = {{"Reading", kFatalLow}};
        return json;
    }

    // Tests that deal with lambdas can silently pass if the code that triggers
    // the lambda is accidentally deleted. Guard against that with this flag.
    bool testBodyExecuted = false;

    sdbusplus::async::context ctx;
    sdbusplus::server::manager_t manager;
};

TEST_F(SensorTest, FailWithBadJson)
{
    std::string badJson = "[";
    EXPECT_THROW((void)redfish_binding::Sensor::parseSensor(badJson),
                 std::exception);
    testBodyExecuted = true;
}

TEST_F(SensorTest, SensorRootPath)
{
    EXPECT_STREQ("/xyz/openbmc_project/sensors", Sensor::rootPath);
    testBodyExecuted = true;
}

TEST_F(SensorTest, ToMaybeUnit)
{
    auto testSuccessHelper =
        [](SensorValueIntf::Unit expected, const std::string& units) {
            auto maybeActual = Sensor::toMaybeUnit(units);
            EXPECT_TRUE(maybeActual.has_value());
            EXPECT_EQ(expected, maybeActual.value());
        };

    testSuccessHelper(SensorValueIntf::Unit::Percent, "%");
    testSuccessHelper(SensorValueIntf::Unit::DegreesC, "Cel");
    testSuccessHelper(SensorValueIntf::Unit::Joules, "J");
    testSuccessHelper(SensorValueIntf::Unit::Pascals, "Pa");
    testSuccessHelper(SensorValueIntf::Unit::Volts, "V");
    testSuccessHelper(SensorValueIntf::Unit::Watts, "W");

    auto testFailureHelper = [](const std::string& units) {
        auto maybeActual = Sensor::toMaybeUnit(units);
        EXPECT_FALSE(maybeActual.has_value());
    };

    testFailureHelper("unknown_unit");
    testBodyExecuted = true;
}

TEST_F(SensorTest, ToMaybeMetadata)
{
    // A recognized unit yields its enum and the per-unit default range.
    auto pct = Sensor::toMaybeMetadata("%");
    ASSERT_TRUE(pct.has_value());
    EXPECT_EQ(SensorValueIntf::Unit::Percent, pct->unit);
    EXPECT_EQ(0.0, pct->minValue);
    EXPECT_EQ(100.0, pct->maxValue);

    auto cel = Sensor::toMaybeMetadata("Cel");
    ASSERT_TRUE(cel.has_value());
    EXPECT_EQ(SensorValueIntf::Unit::DegreesC, cel->unit);
    EXPECT_EQ(-128.0, cel->minValue);
    EXPECT_EQ(127.0, cel->maxValue);

    // An unrecognized unit yields nullopt.
    EXPECT_FALSE(Sensor::toMaybeMetadata("unknown_unit").has_value());
    testBodyExecuted = true;
}

TEST_F(SensorTest, NoOpBeforeCreation)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        auto sensorClient = getSensorClient();

        // No Sensor has been created, so nothing is published on the bus.
        EXPECT_THROW(co_await sensorClient.value(), DBusError::UnknownObject);

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, EmptyAssociationPath)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        std::string emptyAssociation;
        auto sensorServer = createSensor(kSensorNamespace, kSensorId,
                                         emptyAssociation, 10.0, "%");
        EXPECT_NE(nullptr, sensorServer);
        auto associationClient = getAssociationClient();

        auto associations = co_await associationClient.associations();
        EXPECT_EQ(0, associations.size());

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, CreateAndUpdate)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        auto sensorServer =
            createSensor(kSensorNamespace, kSensorId, kAssociationPath, 25.0,
                         "%", 0.0, 100.0);
        auto sensorClient = getSensorClient();
        auto associationClient = getAssociationClient();

        EXPECT_EQ(25.0, co_await sensorClient.value());
        EXPECT_EQ(SensorValueIntf::Unit::Percent, co_await sensorClient.unit());
        EXPECT_EQ(0.0, co_await sensorClient.min_value());
        EXPECT_EQ(100.0, co_await sensorClient.max_value());

        auto associations = co_await associationClient.associations();
        EXPECT_EQ(1, associations.size());
        EXPECT_EQ("chassis", std::get<0>(associations[0]));
        EXPECT_EQ("all_sensors", std::get<1>(associations[0]));
        EXPECT_EQ(kAssociationPath, std::get<2>(associations[0]));

        // A subsequent value update changes only the value.
        co_await sensorServer->updateValue(42.0);
        EXPECT_EQ(42.0, co_await sensorClient.value());

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, ValueUpdateKeepsUnitAndRange)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        auto sensorServer =
            createSensor(kSensorNamespace, kSensorId, kAssociationPath, 25.0,
                         "Cel", -10, 10);
        auto sensorClient = getSensorClient();

        EXPECT_EQ(25.0, co_await sensorClient.value());
        EXPECT_EQ(SensorValueIntf::Unit::DegreesC,
                  co_await sensorClient.unit());
        EXPECT_EQ(-10, co_await sensorClient.min_value());
        EXPECT_EQ(10, co_await sensorClient.max_value());

        // The unit and range are fixed at construction; only the value changes.
        co_await sensorServer->updateValue(35.0);
        EXPECT_EQ(35.0, co_await sensorClient.value());
        EXPECT_EQ(SensorValueIntf::Unit::DegreesC,
                  co_await sensorClient.unit());
        EXPECT_EQ(-10, co_await sensorClient.min_value());
        EXPECT_EQ(10, co_await sensorClient.max_value());

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, HardShutdownAlarmTracksPolledValue)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        auto sensorServer = createSensor(kSensorNamespace, kSensorId,
                                         kAssociationPath, 119.0, "Cel",
                                         std::nullopt, std::nullopt, 120.0);
        EXPECT_NE(nullptr, sensorServer);
        auto hardShutdownClient = getHardShutdownClient();

        EXPECT_EQ(120.0, co_await hardShutdownClient.hard_shutdown_high());
        EXPECT_FALSE(
            co_await hardShutdownClient.hard_shutdown_alarm_high());

        co_await sensorServer->updateValue(121.0);
        EXPECT_TRUE(co_await hardShutdownClient.hard_shutdown_alarm_high());

        co_await sensorServer->updateValue(119.0);
        EXPECT_FALSE(
            co_await hardShutdownClient.hard_shutdown_alarm_high());

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, MaxMinOmitted)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        // When the Redfish sensor omits the range, per-unit defaults are
        // applied (J -> 0..100000).
        auto sensorServer = createSensor(kSensorNamespace, kSensorId,
                                         kAssociationPath, 25.0, "J");
        auto sensorClient = getSensorClient();

        EXPECT_EQ(25.0, co_await sensorClient.value());
        EXPECT_EQ(SensorValueIntf::Unit::Joules, co_await sensorClient.unit());
        EXPECT_EQ(0.0, co_await sensorClient.min_value());
        EXPECT_EQ(100000.0, co_await sensorClient.max_value());

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, UnrecognizedUnitIsNotCreated)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        // A unit we do not recognize produces no D-Bus object.
        auto sensorServer =
            createSensor(kSensorNamespace, kSensorId, kAssociationPath, 3.0,
                         "This is not a real unit");
        EXPECT_EQ(nullptr, sensorServer);

        // Nothing is published on the bus.
        auto sensorClient = getSensorClient();
        EXPECT_THROW(co_await sensorClient.value(), DBusError::UnknownObject);

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, MissingUnitsIsNotCreated)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        // A Redfish sensor with no ReadingUnits produces no D-Bus object.
        nlohmann::json json;
        json["Reading"] = 5.0;
        auto parsed = redfish_binding::Sensor::parseSensor(json.dump());
        auto sensorServer = Sensor::create(ctx, kSensorNamespace, kSensorId,
                                           kAssociationPath, parsed);
        EXPECT_EQ(nullptr, sensorServer);

        // Nothing is published on the bus.
        auto sensorClient = getSensorClient();
        EXPECT_THROW(co_await sensorClient.value(), DBusError::UnknownObject);

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, MissingReadingBecomesNan)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        // No Reading field -> value is published as NaN.
        nlohmann::json json;
        json["ReadingUnits"] = "Cel";
        auto parsed = redfish_binding::Sensor::parseSensor(json.dump());
        auto sensorServer = Sensor::create(ctx, kSensorNamespace, kSensorId,
                                           kAssociationPath, parsed);
        EXPECT_NE(nullptr, sensorServer);

        auto sensorClient = getSensorClient();
        EXPECT_TRUE(std::isnan(co_await sensorClient.value()));
        // Cel gets per-unit default range.
        EXPECT_EQ(-128.0, co_await sensorClient.min_value());
        EXPECT_EQ(127.0, co_await sensorClient.max_value());

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, NoThresholdsNoInterfaces)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        // No Thresholds field at all -> none of the three threshold
        // interfaces are published, even though Value still is.
        nlohmann::json json;
        json["Reading"] = 25.0;
        json["ReadingUnits"] = "Cel";
        auto parsed = redfish_binding::Sensor::parseSensor(json.dump());
        auto sensorServer = Sensor::create(ctx, kSensorNamespace, kSensorId,
                                           kAssociationPath, parsed);
        EXPECT_NE(nullptr, sensorServer);

        auto sensorClient = getSensorClient();
        EXPECT_EQ(25.0, co_await sensorClient.value());

        EXPECT_THROW(co_await getWarningClient().warning_high(),
                     DBusError::UnknownProperty);
        EXPECT_THROW(co_await getCriticalClient().critical_high(),
                     DBusError::UnknownProperty);
        EXPECT_THROW(co_await getHardShutdownClient().hard_shutdown_high(),
                     DBusError::UnknownProperty);

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, ThresholdsPublishedWithinRange)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        // A reading well within every threshold: interfaces exist, limits
        // match what was parsed, and no alarm is asserted.
        auto json = sensorJsonWithThresholds(25.0);
        auto parsed = redfish_binding::Sensor::parseSensor(json.dump());
        auto sensorServer = Sensor::create(ctx, kSensorNamespace, kSensorId,
                                           kAssociationPath, parsed);
        EXPECT_NE(nullptr, sensorServer);

        auto warningClient = getWarningClient();
        EXPECT_EQ(kWarningHigh, co_await warningClient.warning_high());
        EXPECT_EQ(kWarningLow, co_await warningClient.warning_low());
        EXPECT_FALSE(co_await warningClient.warning_alarm_high());
        EXPECT_FALSE(co_await warningClient.warning_alarm_low());

        auto criticalClient = getCriticalClient();
        EXPECT_EQ(kCriticalHigh, co_await criticalClient.critical_high());
        EXPECT_EQ(kCriticalLow, co_await criticalClient.critical_low());
        EXPECT_FALSE(co_await criticalClient.critical_alarm_high());
        EXPECT_FALSE(co_await criticalClient.critical_alarm_low());

        auto hardShutdownClient = getHardShutdownClient();
        EXPECT_EQ(kFatalHigh, co_await hardShutdownClient.hard_shutdown_high());
        EXPECT_EQ(kFatalLow, co_await hardShutdownClient.hard_shutdown_low());
        EXPECT_FALSE(co_await hardShutdownClient.hard_shutdown_alarm_high());
        EXPECT_FALSE(co_await hardShutdownClient.hard_shutdown_alarm_low());

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, AlarmAssertedAtCreation)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        // Alarm must assert on the very first reading if it's already past
        // the threshold, not only after a later updateValue().
        auto json = sensorJsonWithThresholds(95.0);
        auto parsed = redfish_binding::Sensor::parseSensor(json.dump());
        auto sensorServer = Sensor::create(ctx, kSensorNamespace, kSensorId,
                                           kAssociationPath, parsed);
        EXPECT_NE(nullptr, sensorServer);

        auto warningClient = getWarningClient();
        EXPECT_TRUE(co_await warningClient.warning_alarm_high());

        auto criticalClient = getCriticalClient();
        EXPECT_TRUE(co_await criticalClient.critical_alarm_high());

        auto hardShutdownClient = getHardShutdownClient();
        EXPECT_FALSE(co_await hardShutdownClient.hard_shutdown_alarm_high());

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, UpdateValueTransitionsAlarm)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        // Starts within range, then crosses warningHigh; the alarm should
        // flip on updateValue() the same way it does at creation.
        auto json = sensorJsonWithThresholds(25.0);
        auto parsed = redfish_binding::Sensor::parseSensor(json.dump());
        auto sensorServer = Sensor::create(ctx, kSensorNamespace, kSensorId,
                                           kAssociationPath, parsed);
        EXPECT_NE(nullptr, sensorServer);

        auto warningClient = getWarningClient();
        EXPECT_FALSE(co_await warningClient.warning_alarm_high());

        co_await sensorServer->updateValue(85.0);
        EXPECT_TRUE(co_await warningClient.warning_alarm_high());

        // Recovering clears the alarm again.
        co_await sensorServer->updateValue(25.0);
        EXPECT_FALSE(co_await warningClient.warning_alarm_high());

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, ConfigOverridesTarget)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        // Config overrides warningHigh to a stricter limit and disables
        // hardShutdownHigh outright.
        auto json = sensorJsonWithThresholds(25.0);
        auto parsed = redfish_binding::Sensor::parseSensor(json.dump());

        ThresholdOverride override;
        override.warningHigh.mode = ThresholdOverride::Mode::Value;
        override.warningHigh.value = 50.0;
        override.hardShutdownHigh.mode = ThresholdOverride::Mode::Disabled;

        auto sensorServer = Sensor::create(ctx, kSensorNamespace, kSensorId,
                                           kAssociationPath, parsed, override);
        EXPECT_NE(nullptr, sensorServer);

        // Override wins: 50.0, not the target's 80.0.
        auto warningClient = getWarningClient();
        EXPECT_EQ(50.0, co_await warningClient.warning_high());

        // Disabled: never alarms even far past the target's original 100.0.
        co_await sensorServer->updateValue(150.0);
        auto hardShutdownClient = getHardShutdownClient();
        EXPECT_FALSE(co_await hardShutdownClient.hard_shutdown_alarm_high());

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, LimitsWrittenOverDBusTakeEffect)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        // The target reports only UpperCaution, so Warning is published with
        // WarningLow unset.
        nlohmann::json json;
        json["Reading"] = 25.0;
        json["ReadingUnits"] = kThresholdUnit;
        json["Thresholds"]["UpperCaution"] = {{"Reading", kWarningHigh}};
        auto parsed = redfish_binding::Sensor::parseSensor(json.dump());
        auto sensorServer = Sensor::create(ctx, kSensorNamespace, kSensorId,
                                           kAssociationPath, parsed);
        EXPECT_NE(nullptr, sensorServer);
        auto warningClient = getWarningClient();

        // A limit the target never reported, written over D-Bus.
        co_await warningClient.warning_low(0.0);
        co_await sensorServer->updateValue(-1.0);
        EXPECT_TRUE(co_await warningClient.warning_alarm_low());

        // Removing a limit (NaN) clears its asserted alarm.
        co_await warningClient.warning_low(
            std::numeric_limits<double>::quiet_NaN());
        co_await sensorServer->updateValue(-2.0);
        EXPECT_FALSE(co_await warningClient.warning_alarm_low());

        // An alarm cleared over D-Bus re-asserts on the next reading while
        // still past the limit, even if the reading did not change.
        co_await sensorServer->updateValue(kWarningHigh + 1.0);
        co_await warningClient.warning_alarm_high(false);
        co_await sensorServer->updateValue(kWarningHigh + 1.0);
        EXPECT_TRUE(co_await warningClient.warning_alarm_high());

        testBodyExecuted = true;
        co_return;
    }());
}

TEST_F(SensorTest, NanReadingClearsAlarm)
{
    ctx.spawn([this]() -> sdbusplus::async::task<> {
        // A reading that drops out (e.g. the host is off) clears the alarms,
        // and a later real reading past the limit asserts them again.
        auto json = sensorJsonWithThresholds(kCriticalHigh + 1.0);
        auto parsed = redfish_binding::Sensor::parseSensor(json.dump());
        auto sensorServer = Sensor::create(ctx, kSensorNamespace, kSensorId,
                                           kAssociationPath, parsed);
        EXPECT_NE(nullptr, sensorServer);
        auto warningClient = getWarningClient();
        auto criticalClient = getCriticalClient();
        EXPECT_TRUE(co_await warningClient.warning_alarm_high());
        EXPECT_TRUE(co_await criticalClient.critical_alarm_high());

        co_await sensorServer->updateValue(
            std::numeric_limits<double>::quiet_NaN());
        EXPECT_FALSE(co_await warningClient.warning_alarm_high());
        EXPECT_FALSE(co_await criticalClient.critical_alarm_high());

        co_await sensorServer->updateValue(kCriticalHigh + 1.0);
        EXPECT_TRUE(co_await criticalClient.critical_alarm_high());

        testBodyExecuted = true;
        co_return;
    }());
}
