#include "helper.hpp"

#include <org/freedesktop/DBus/Error/event.hpp>
#include <redfish_client/core/config.hpp>
#include <redfish_client/core/update_service_handler.hpp>
#include <xyz/openbmc_project/Software/Activation/client.hpp>
#include <xyz/openbmc_project/Software/Update/client.hpp>
#include <xyz/openbmc_project/Software/Version/client.hpp>

#include <format>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace redfish_client::core;
namespace DBusError = sdbusplus::error::org::freedesktop::d_bus::Error;

namespace
{

constexpr auto kServiceName = "xyz.openbmc_project.test.UpdateServiceHandler";

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
    },
    {
      "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/FW1",
      "@odata.type": "#SoftwareInventory.v1_4_0.SoftwareInventory",
      "Id": "FW1",
      "Version": "FW1-V2"
    },
    {
      "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/FW2",
      "@odata.type": "#SoftwareInventory.v1_4_0.SoftwareInventory",
      "Id": "FW2",
      "Version": "FW2-V4"
    }
  ],
  "Members@odata.count": 3,
  "Name": "Software Inventory Collection"
}
)";

constexpr auto kSoftwareInventoryResponse = R"(
{
  "@odata.id": "/redfish/v1/UpdateService/SoftwareInventory",
  "@odata.type": "#SoftwareInventoryCollection.SoftwareInventoryCollection",
  "Members": [
    {
      "@odata.id": "/redfish/v1/UpdateService/SoftwareInventory/SW0",
      "@odata.type": "#SoftwareInventory.v1_4_0.SoftwareInventory",
      "Id": "SW0",
      "Version": "SW-v1.5"
    },
    {
      "@odata.id": "/redfish/v1/UpdateService/SoftwareInventory/SW1",
      "@odata.type": "#SoftwareInventory.v1_4_0.SoftwareInventory",
      "Id": "SW1",
      "Version": "SW-v2.0.0"
    }
  ],
  "Members@odata.count": 2,
  "Name": "Software Inventory Collection"
}
)";

constexpr auto kUpdateableFirmwareInventoryResponse = R"(
{
  "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory",
  "@odata.type": "#SoftwareInventoryCollection.SoftwareInventoryCollection",
  "Members": [
    {
      "@odata.id": "/redfish/v1/UpdateService/FirmwareInventory/FW0",
      "@odata.type": "#SoftwareInventory.v1_4_0.SoftwareInventory",
      "Id": "FW0",
      "Updateable": true,
      "Version": "FW0-V1"
    }
  ],
  "Members@odata.count": 1,
  "Name": "Software Inventory Collection"
}
)";

constexpr auto kUpdateServiceResponse = R"(
{
  "@odata.id": "/redfish/v1/UpdateService",
  "@odata.type": "#UpdateService.v1_11_0.UpdateService",
  "Id": "UpdateService",
  "MultipartHttpPushUri": "/redfish/v1/UpdateService/update-multipart",
  "Name": "Update Service"
}
)";

std::string generateResponse(
    const SimpleTestHttpServer::ReceivedHttpRequest& request)
{
    if (request.path.ends_with("/FirmwareInventory?$expand=."))
    {
        return kFirmwareInventoryResponse;
    }
    if (request.path.ends_with("/SoftwareInventory?$expand=."))
    {
        return kSoftwareInventoryResponse;
    }
    return "[]";
}

UpdateServiceMapper makeMapper(const std::string& fromId,
                               const std::string& toId)
{
    return UpdateServiceMapper{fromId, toId, std::nullopt};
}

} // namespace

class UpdateServiceHandlerTest : public ::testing::Test
{
  protected:
    UpdateServiceHandlerTest() : manager(ctx, SoftwareVersion::namespace_path)
    {}

    ~UpdateServiceHandlerTest() noexcept override {}

    auto getVersionClient(const char* path)
    {
        return sdbusplus::client::xyz::openbmc_project::software::Version<>(ctx)
            .service(kServiceName)
            .path(path);
    }

    auto getActivationClient(const char* path)
    {
        return sdbusplus::client::xyz::openbmc_project::software::Activation<>(
                   ctx)
            .service(kServiceName)
            .path(path);
    }

    auto getUpdateClient(const char* path)
    {
        return sdbusplus::client::xyz::openbmc_project::software::Update<>(ctx)
            .service(kServiceName)
            .path(path);
    }

    sdbusplus::async::context ctx;
    sdbusplus::server::manager_t manager;
};

TEST_F(UpdateServiceHandlerTest, FirmwareInventory)
{
    Software::randomIdGenerator() = []() { return 1234; };
    SimpleTestHttpServer server(generateResponse, {});
    std::vector<UpdateServiceMapper> mappers = {makeMapper("FW0", "Host0_FW0"),
                                                makeMapper("FW1", "BMC_FW1")};
    UpdateServiceHandler handler(std::format("localhost:{}", server.getPort()),
                                 "FirmwareInventory", mappers);

    ctx.request_name(kServiceName);
    ctx.spawn([&]() -> sdbusplus::async::task<> {
        co_await handler.load(ctx);

        // FW0 is mapped -> published as Host0_FW0.
        auto fw0 =
            getVersionClient("/xyz/openbmc_project/software/Host0_FW0_1234");
        EXPECT_EQ("FW0-V1", co_await fw0.version());
        EXPECT_EQ(SoftwareVersion::VersionPurpose::Other,
                  co_await fw0.purpose());
        auto fw0Act =
            getActivationClient("/xyz/openbmc_project/software/Host0_FW0_1234");
        EXPECT_EQ(SoftwareActivation::Activations::Active,
                  co_await fw0Act.activation());
        EXPECT_EQ(SoftwareActivation::RequestedActivations::None,
                  co_await fw0Act.requested_activation());

        // FW1 is mapped -> published as BMC_FW1.
        auto fw1 =
            getVersionClient("/xyz/openbmc_project/software/BMC_FW1_1234");
        EXPECT_EQ("FW1-V2", co_await fw1.version());

        // FW2 has no mapper -> not published.
        auto fw2 = getVersionClient("/xyz/openbmc_project/software/FW2_1234");
        EXPECT_THROW(co_await fw2.version(), DBusError::UnknownObject);

        ctx.request_stop();
        co_return;
    }());
    ctx.run();
}

TEST_F(UpdateServiceHandlerTest, SoftwareInventory)
{
    Software::randomIdGenerator() = []() { return 1234; };
    SimpleTestHttpServer server(generateResponse, {});
    std::vector<UpdateServiceMapper> mappers = {makeMapper("SW0", "CPLD_SW0")};
    UpdateServiceHandler handler(std::format("localhost:{}", server.getPort()),
                                 "SoftwareInventory", mappers);

    ctx.request_name(kServiceName);
    ctx.spawn([&]() -> sdbusplus::async::task<> {
        co_await handler.load(ctx);

        // SW0 is mapped -> published as CPLD_SW0.
        auto sw0 =
            getVersionClient("/xyz/openbmc_project/software/CPLD_SW0_1234");
        EXPECT_EQ("SW-v1.5", co_await sw0.version());
        EXPECT_EQ(SoftwareVersion::VersionPurpose::Other,
                  co_await sw0.purpose());
        auto sw0Act =
            getActivationClient("/xyz/openbmc_project/software/CPLD_SW0_1234");
        EXPECT_EQ(SoftwareActivation::Activations::Active,
                  co_await sw0Act.activation());

        // SW1 has no mapper -> not published.
        auto sw1 = getVersionClient("/xyz/openbmc_project/software/SW1_1234");
        EXPECT_THROW(co_await sw1.version(), DBusError::UnknownObject);

        ctx.request_stop();
        co_return;
    }());
    ctx.run();
}

TEST_F(UpdateServiceHandlerTest, MultipartUpdateEnabledOnceUpdateServiceReady)
{
    Software::randomIdGenerator() = []() { return 1234; };
    bool updateServiceReady = false;
    SimpleTestHttpServer server(
        [&](const SimpleTestHttpServer::ReceivedHttpRequest& request)
            -> std::string {
            if (request.path.ends_with("/FirmwareInventory?$expand=."))
            {
                return kUpdateableFirmwareInventoryResponse;
            }
            if (request.path.ends_with("/UpdateService"))
            {
                return updateServiceReady ? kUpdateServiceResponse : "{}";
            }
            return "[]";
        },
        {});
    std::vector<UpdateServiceMapper> mappers = {makeMapper("FW0", "Host0_FW0")};
    UpdateServiceHandler handler(std::format("localhost:{}", server.getPort()),
                                 "FirmwareInventory", mappers);

    ctx.request_name(kServiceName);
    ctx.spawn([&]() -> sdbusplus::async::task<> {
        constexpr auto path = "/xyz/openbmc_project/software/Host0_FW0_1234";

        co_await handler.load(ctx);
        EXPECT_EQ("FW0-V1", co_await getVersionClient(path).version());
        EXPECT_THROW(co_await getUpdateClient(path).allowed_apply_times(),
                     DBusError::UnknownProperty);

        updateServiceReady = true;
        co_await handler.load(ctx);
        EXPECT_EQ(std::set<RequestedApplyTimes>{RequestedApplyTimes::Immediate},
                  co_await getUpdateClient(path).allowed_apply_times());

        ctx.request_stop();
        co_return;
    }());
    ctx.run();
}
