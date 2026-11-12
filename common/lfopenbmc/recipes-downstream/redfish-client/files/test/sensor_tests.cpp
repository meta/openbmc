#include <nlohmann/json.hpp>
#include <org/freedesktop/DBus/Error/event.hpp>
#include <redfish-binding/Sensor_Sensor.hpp>
#include <redfish_client/core/sensor.hpp>
#include <xyz/openbmc_project/Association/Definitions/client.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/HardShutdown/client.hpp>
#include <xyz/openbmc_project/Sensor/Value/client.hpp>

#include <cmath>
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
        return Sensor::create(ctx, ns, id, associationPath, hardShutdownHigh,
                              parsed);
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

    auto getHardShutdownClient()
    {
        using HardShutdownClient = sdbusplus::client::xyz::openbmc_project::
            sensor::threshold::HardShutdown<>;
        return HardShutdownClient(ctx).service(kBusName).path(kMetricPath);
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
                                           kAssociationPath, std::nullopt, parsed);
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
                                           kAssociationPath, std::nullopt, parsed);
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
