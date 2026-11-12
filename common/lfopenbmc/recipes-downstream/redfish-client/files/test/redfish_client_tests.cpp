#include "helper.hpp"

#include <redfish_client/core/redfish_client.hpp>
#include <redfish_client/core/update_service_handler.hpp>
#include <xyz/openbmc_project/Sensor/Value/client.hpp>
#include <xyz/openbmc_project/Software/Version/client.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace redfish_client::core;

namespace
{

constexpr auto kServiceName = "xyz.openbmc_project.test.RedfishClient";

// A config that drives every handler RedfishClient wires up: one sensor and
// one firmware entry. The per-handler behavior is covered in the focused
// sensor_handler_tests / update_service_handler_tests; this test only checks
// that RedfishClient actually spawns each handler and its output reaches dbus.
// A long poll interval so each handler runs a single load() (publishing its
// objects) and then sleeps -- no second poll to race with request_stop().
constexpr auto kTestConfigFormat = R"(
  {{
    "host": "localhost:{}",
    "compatible": "com.meta.Hardware.BMC.TEST",
    "sensorConfig": {{
      "associationPath": "/xyz/openbmc_project/inventory/system/host0",
      "intervalMilliseconds": 5000,
      "maxRetries": 1,
      "retryIntervalMilliseconds": 1,
      "mappers": [
        {{
          "fromUrl": "/redfish/v1/Chassis/Host0/Sensors/Temp0",
          "toNamespace": "temperature",
          "toId": "Host0_Temp0"
        }}
      ]
    }},
    "logServiceConfig": {{
      "intervalMilliseconds": 5000,
      "urls": ["/redfish/v1/Systems/System0/LogServices/EventLog/Entries"]
    }},
    "updateServiceConfig": {{
      "intervalMilliseconds": 5000,
      "firmwareMappers": [
        {{
          "fromId": "FW0",
          "toId": "Host0_FW0"
        }}
      ],
      "softwareMappers": []
    }}
  }}
)";

constexpr auto kSensorResponse = R"({"Reading": 100.0, "ReadingUnits": "Cel"})";

// An empty log collection is enough to prove the log handler polled without
// needing a logging service to receive committed entries.
constexpr auto kLogEntriesResponse = R"(
{
  "@odata.type": "#LogEntryCollection.LogEntryCollection",
  "Members@odata.count": 0,
  "Members": []
}
)";

constexpr auto kFirmwareInventoryResponse = R"(
{
  "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory",
  "@odata.type": "#SoftwareInventoryCollection.SoftwareInventoryCollection",
  "Members": [
    {
      "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/FW0",
      "@odata.type": "#SoftwareInventory.v1_4_0.SoftwareInventory",
      "Id": "FW0",
      "Version": "FW0-V1"
    }
  ],
  "Members@odata.count": 1,
  "Name": "Software Inventory Collection"
}
)";

std::string generateResponse(
    const SimpleTestHttpServer::ReceivedHttpRequest& request)
{
    if (request.path.ends_with("/Temp0"))
    {
        return kSensorResponse;
    }
    if (request.path.ends_with("/FirmwareInventory?$expand=."))
    {
        return kFirmwareInventoryResponse;
    }
    if (request.path.ends_with("/EventLog/Entries"))
    {
        return kLogEntriesResponse;
    }
    return "[]";
}

} // namespace

// End-to-end: RedfishClient::run() must spawn the sensor, update-service, and
// log-service handlers, and each must reach out / publish to dbus.
TEST(RedfishClientTests, WiresUpHandlers)
{
    sdbusplus::async::context ctx;
    SimpleTestHttpServer server(generateResponse, {});
    auto config =
        Config::parse(std::format(kTestConfigFormat, server.getPort()));
    Software::randomIdGenerator() = []() { return 1234; };
    auto clientThread = std::make_unique<std::thread>([&ctx, &config]() {
        ctx.request_name(kServiceName);
        RedfishClient client(ctx, std::vector<Config>{config},
                             /*persistDir=*/"");
        ctx.spawn(client.run());
        ctx.run();
    });

    ctx.spawn([&server](
                  sdbusplus::async::context& ctx) -> sdbusplus::async::task<> {
        auto sensorClient =
            sdbusplus::client::xyz::openbmc_project::sensor::Value<>(ctx)
                .service(kServiceName)
                .path("/xyz/openbmc_project/sensors/temperature/Host0_Temp0");
        auto versionClient =
            sdbusplus::client::xyz::openbmc_project::software::Version<>(ctx)
                .service(kServiceName)
                .path("/xyz/openbmc_project/software/Host0_FW0_1234");

        // Poll until both handlers have published (each does one load()).
        std::optional<double> value;
        std::optional<std::string> version;
        for (int i = 0; i < 100 && (!value || !version); ++i)
        {
            bool pending = false;
            try
            {
                if (!value)
                {
                    value = co_await sensorClient.value();
                }
                if (!version)
                {
                    version = co_await versionClient.version();
                }
            }
            catch (const sdbusplus::exception::exception&)
            {
                pending = true;
            }
            if (pending)
            {
                co_await sdbusplus::async::sleep_for(
                    ctx, std::chrono::milliseconds(20));
            }
        }

        // SensorHandler published the sensor; UpdateServiceHandler the
        // firmware.
        EXPECT_EQ(100.0, value.value_or(0.0));
        EXPECT_EQ("FW0-V1", version.value_or(""));

        // LogServiceHandler doesn't publish a readable object (it commits
        // events), so confirm it was wired by checking it polled its URL.
        bool logPolled = false;
        for (int i = 0; i < 100 && !logPolled; ++i)
        {
            for (const auto& request : server.getReceivedRequests())
            {
                if (request.path.ends_with("/EventLog/Entries"))
                {
                    logPolled = true;
                    break;
                }
            }
            if (!logPolled)
            {
                co_await sdbusplus::async::sleep_for(
                    ctx, std::chrono::milliseconds(20));
            }
        }
        EXPECT_TRUE(logPolled);

        ctx.request_stop();
        co_return;
    }(ctx));
    clientThread->join();
    clientThread = nullptr; // Make sure it's destroyed before the context.
}
