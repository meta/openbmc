#pragma once

#include "redfish-binding/SoftwareInventory_SoftwareInventory.hpp"

#include <redfish_client/core/async_http_client.hpp>
#include <redfish_client/core/config.hpp>
#include <sdbusplus/async/context.hpp>
#include <xyz/openbmc_project/Software/Activation/aserver.hpp>
#include <xyz/openbmc_project/Software/ActivationBlocksTransition/aserver.hpp>
#include <xyz/openbmc_project/Software/ActivationProgress/aserver.hpp>
#include <xyz/openbmc_project/Software/Update/aserver.hpp>
#include <xyz/openbmc_project/Software/Version/aserver.hpp>

#include <functional>
#include <string>

namespace redfish_client::core
{

class Software;

using SoftwareVersion =
    sdbusplus::aserver::xyz::openbmc_project::software::Version<Software>;

using SoftwareActivation =
    sdbusplus::aserver::xyz::openbmc_project::software::Activation<Software>;

using SoftwareActivationBlocksTransition = sdbusplus::aserver::xyz::
    openbmc_project::software::ActivationBlocksTransition<Software>;

using RequestedApplyTimes = sdbusplus::common::xyz::openbmc_project::software::
    ApplyTime::RequestedApplyTimes;

using SoftwareUpdateCallback =
    std::move_only_function<sdbusplus::async::task<void>(
        sdbusplus::async::context&, std::unique_ptr<Software>)>;

class SoftwareActivationProgress :
    public sdbusplus::aserver::xyz::openbmc_project::software::
        ActivationProgress<SoftwareActivationProgress>
{
  public:
    SoftwareActivationProgress(sdbusplus::async::context& ctx, auto path);

    bool set_property(progress_t, auto progress);
};

class MultipartSoftwareUpdate :
    public sdbusplus::aserver::xyz::openbmc_project::software::Update<
        MultipartSoftwareUpdate>
{
  public:
    MultipartSoftwareUpdate(
        sdbusplus::async::context& ctx, Software& software,
        const std::string& host, const std::string& uri,
        const std::set<RequestedApplyTimes>& allowedApplyTimes,
        const std::vector<std::string>& targets,
        SoftwareUpdateCallback successCallback);

    auto method_call(start_update_t, auto image, auto applyTime)
        -> sdbusplus::async::task<start_update_t::return_type>;

    auto get_property(allowed_apply_times_t) const;

    struct Config
    {
        int getTaskMaxRetries = 4;
        int getTaskIntervalMilliseconds = 10000;
        int getTaskRetryIntervalMilliseconds = 2000;
    };

    static Config& config()
    {
        static Config config;
        return config;
    }

  private:
    Software& software;
    const std::string host;
    const std::string uri;
    const std::set<RequestedApplyTimes> allowedApplyTimes;
    const std::vector<std::string> targets;
    SoftwareUpdateCallback successCallback;
    bool inProgress{false};

    auto update(sdbusplus::message::unix_fd image,
                std::unique_ptr<Software> newSoftware)
        -> sdbusplus::async::task<void>;
};

class Software : private sdbusplus::async::context_ref
{
  public:
    Software() = delete;

    Software(sdbusplus::async::context& ctx, const std::string& id);

    sdbusplus::object_path getPath() const;

    std::string getId() const;

    void setVersion(const std::string& versionStr);

    void setActivation(SoftwareActivation::Activations act);

    void enableMultipartUpdate(const std::string& host, const std::string& uri,
                               const std::vector<std::string>& targets,
                               SoftwareUpdateCallback successCallback);

    bool isMultipartUpdateEnabled() const;

    static std::function<int()>& randomIdGenerator();

  private:
    const std::string id;
    const sdbusplus::object_path path;
    std::unique_ptr<SoftwareVersion> version{nullptr};
    std::unique_ptr<SoftwareActivation> activation{nullptr};
    std::unique_ptr<MultipartSoftwareUpdate> multipartUpdate{nullptr};
};

class UpdateServiceHandler
{
  public:
    UpdateServiceHandler() = delete;

    UpdateServiceHandler(const std::string& host,
                         const std::string& inventoryName,
                         const std::vector<UpdateServiceMapper>& mappers);

    static auto run(sdbusplus::async::context& ctx, const std::string& host,
                    const UpdateServiceConfig& config)
        -> sdbusplus::async::task<void>;

    auto load(sdbusplus::async::context& ctx) -> sdbusplus::async::task<void>;

  private:
    const std::string host;
    const std::string inventoryUrl;
    std::unique_ptr<AsyncHttpHandle> handle;
    std::optional<std::string> multipartUpdateUri;
    const std::vector<UpdateServiceMapper>& mappers;
    std::unordered_map<std::string, std::unique_ptr<Software>> inventory;

    static auto getMultipartUpdateUri(sdbusplus::async::context& ctx,
                                      const std::string& host)
        -> sdbusplus::async::task<std::optional<std::string>>;
};

} // namespace redfish_client::core
