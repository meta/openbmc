#include "helper.hpp"

#include <org/freedesktop/DBus/Error/event.hpp>
#include <redfish_client/core/config.hpp>
#include <redfish_client/core/sensor.hpp>
#include <redfish_client/core/sensor_handler.hpp>
#include <xyz/openbmc_project/Sensor/Value/client.hpp>

#include <format>
#include <optional>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace redfish_client::core;
namespace DBusError = sdbusplus::error::org::freedesktop::d_bus::Error;

class SensorHandlerTest : public ::testing::Test
{
  protected:
    static constexpr auto kServiceName =
        "xyz.openbmc_project.test.SensorHandler";

    static constexpr auto kTemp0Url = "/redfish/v1/Chassis/Host0/Sensors/Temp0";
    static constexpr auto kTemp1Url = "/redfish/v1/Chassis/Host0/Sensors/Temp1";
    static constexpr auto kPressure0Url =
        "/redfish/v1/Chassis/Host0/Sensors/Pressure0";
    static constexpr auto kReportUrl =
        "/redfish/v1/TelemetryService/MetricReports/Sensors";

    static constexpr auto kTemp0Path =
        "/xyz/openbmc_project/sensors/temperature/Host0_Temp0";
    static constexpr auto kTemp1Path =
        "/xyz/openbmc_project/sensors/temperature/Host0_Temp1";
    static constexpr auto kPressure0Path =
        "/xyz/openbmc_project/sensors/pressure/Host0_Pressure0";

    static constexpr auto kTemp0Response =
        R"({"Reading": 100.0, "ReadingUnits": "Cel"})";
    static constexpr auto kTemp1Response =
        R"({"Reading": 200.0, "ReadingUnits": "Cel"})";

    // A metric report carrying only a bare value for Temp0 (MetricValue is a
    // string, as Redfish sends it).
    static constexpr auto kReportResponse = R"({
  "MetricValues": [
    {
      "MetricProperty": "/redfish/v1/Chassis/Host0/Sensors/Temp0",
      "MetricValue": "150"
    }
  ]
})";

    SensorHandlerTest() : manager(ctx, Sensor::rootPath) {}

    ~SensorHandlerTest() noexcept override {}

    static std::string generateResponse(
        const SimpleTestHttpServer::ReceivedHttpRequest& request)
    {
        if (request.path.ends_with("/Temp0"))
        {
            return kTemp0Response;
        }
        if (request.path.ends_with("/Temp1"))
        {
            return kTemp1Response;
        }
        if (request.path.ends_with("/Sensors"))
        {
            return kReportResponse;
        }
        // Anything else (e.g. Pressure0) returns unparseable content.
        return "[]";
    }

    static SensorMapper createMapper(const std::string& fromUrl,
                                     const std::string& ns,
                                     const std::string& id)
    {
        return SensorMapper{fromUrl, ns, id};
    }

    // Build a SensorConfig pointing at the fake HTTP server on the given port.
    SensorConfig createConfig(
        std::vector<SensorMapper> mappers,
        std::optional<std::vector<std::string>> metricReportUrls = std::nullopt)
    {
        SensorConfig config;
        config.associationPath = "/xyz/openbmc_project/inventory/system/host0";
        config.mappers = std::move(mappers);
        config.intervalMilliseconds = 10;
        config.maxRetries = 1;
        config.retryIntervalMilliseconds = 1;
        config.metricReportUrls = std::move(metricReportUrls);
        return config;
    }

    auto getSensorClient(const char* path)
    {
        using ValueClient =
            sdbusplus::client::xyz::openbmc_project::sensor::Value<>;
        return ValueClient(ctx).service(kServiceName).path(path);
    }

    sdbusplus::async::context ctx;
    sdbusplus::server::manager_t manager;
};

TEST_F(SensorHandlerTest, IndividualPolling)
{
    SimpleTestHttpServer server(generateResponse, {});
    auto config = createConfig(
        {createMapper(kTemp0Url, "temperature", "Host0_Temp0"),
         createMapper(kTemp1Url, "temperature", "Host0_Temp1"),
         createMapper(kPressure0Url, "pressure", "Host0_Pressure0")});
    SensorHandler handler(std::format("localhost:{}", server.getPort()),
                          config);

    ctx.request_name(kServiceName);
    ctx.spawn([&]() -> sdbusplus::async::task<> {
        co_await handler.load(ctx);

        EXPECT_EQ(100.0, co_await getSensorClient(kTemp0Path).value());
        EXPECT_EQ(200.0, co_await getSensorClient(kTemp1Path).value());

        // Pressure0 returns unparseable content, so nothing is published.
        EXPECT_THROW(co_await getSensorClient(kPressure0Path).value(),
                     DBusError::UnknownObject);

        ctx.request_stop();
        co_return;
    }());
    ctx.run();
}

TEST_F(SensorHandlerTest, MetricReportBootstrapsThenUpdates)
{
    SimpleTestHttpServer server(generateResponse, {});
    auto config =
        createConfig({createMapper(kTemp0Url, "temperature", "Host0_Temp0")},
                     std::vector<std::string>{kReportUrl});
    SensorHandler handler(std::format("localhost:{}", server.getPort()),
                          config);

    ctx.request_name(kServiceName);
    ctx.spawn([&]() -> sdbusplus::async::task<> {
        // First pass: the sensor is seen in the report but not yet created, so
        // it is bootstrapped with a full read of the sensor endpoint (100).
        co_await handler.load(ctx);
        EXPECT_EQ(100.0, co_await getSensorClient(kTemp0Path).value());

        // Second pass: the sensor already exists, so the report drives it with
        // its bare metric value (150) instead of another full read.
        co_await handler.load(ctx);
        EXPECT_EQ(150.0, co_await getSensorClient(kTemp0Path).value());

        ctx.request_stop();
        co_return;
    }());
    ctx.run();
}
