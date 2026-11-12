#include "redfish-binding/SoftwareInventoryCollection_SoftwareInventoryCollection.hpp"
#include "redfish-binding/Task_Task.hpp"
#include "redfish-binding/UpdateService_UpdateParameters.hpp"
#include "redfish-binding/UpdateService_UpdateService.hpp"

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>
#include <redfish_client/core/update_service_handler.hpp>
#include <sdbusplus/async/timer.hpp>

#include <format>
#include <memory>

PHOSPHOR_LOG2_USING;

namespace redfish_client::core
{

namespace
{

auto loop(sdbusplus::async::context& ctx,
          std::unique_ptr<UpdateServiceHandler> handler,
          size_t intervalMilliseconds) -> sdbusplus::async::task<void>
{
    while (!ctx.stop_requested())
    {
        co_await handler->load(ctx);
        co_await sdbusplus::async::sleep_for(
            ctx, std::chrono::milliseconds(intervalMilliseconds));
    };
    co_return;
}

struct TaskInfo
{
    redfish_binding::Task::TaskState state;
    int percentComplete;
};

auto getTaskInfo(sdbusplus::async::context& ctx, AsyncHttpHandle& handle,
                 const std::string& url, int maxRetries,
                 int retryIntervalMilliseconds)
    -> sdbusplus::async::task<std::optional<TaskInfo>>
{
    for (int i = 0; i < maxRetries; i++)
    {
        if (i != 0)
        {
            co_await sdbusplus::async::sleep_for(
                ctx, std::chrono::milliseconds(retryIntervalMilliseconds));
        }
        auto response = co_await handle.tryGet(ctx);
        if (!response.has_value())
        {
            error("Http error for {URL}: {ERR}", "URL", url, "ERR",
                  response.error());
            continue;
        }
        if (response->code != 200)
        {
            error("Http response error code from {URL}: {CODE}", "URL", url,
                  "CODE", response->code);
            continue;
        }
        auto tryTask = redfish_binding::Task::tryParseTask(response->body);
        if (!tryTask.has_value())
        {
            error("Failed to parse task from {URL}: {ERR}, response: {RES}",
                  "URL", url, "ERR", tryTask.error(), "RES", response->body);
            continue;
        }
        auto& task = tryTask.value();
        auto& maybeState = task.getTaskState();
        auto& maybePercentComplete = task.getPercentComplete();
        if (!maybeState.hasValue() || !maybePercentComplete.hasValue())
        {
            error("Invalid http response from {URL}: {RES}", "URL", url, "RES",
                  response->body);
            continue;
        }
        auto state = maybeState.value();
        auto percentComplete = maybePercentComplete.value();
        info("Task info from {URL}: percent complete: {PCT}, state: {STATE}",
             "URL", url, "PCT", percentComplete, "STATE",
             nlohmann::json(state).dump());
        co_return TaskInfo{
            .state = state,
            .percentComplete = percentComplete,
        };
    }
    co_return std::nullopt;
}

} // anonymous namespace

SoftwareActivationProgress::SoftwareActivationProgress(
    sdbusplus::async::context& ctx, auto path) :
    sdbusplus::aserver::xyz::openbmc_project::software::ActivationProgress<
        SoftwareActivationProgress>(ctx, path)
{}

bool SoftwareActivationProgress::set_property(progress_t, auto progress)
{
    properties.progress = progress;
    // Always return true, even if the new value is unchanged.
    // This ensures that a property changed signal is emitted every time.
    // Otherwise, bmcweb may mark the associated task as aborted if it does not
    // receive a signal within a certain timeframe, especially whenmore time is
    // needed to make progress.
    return true;
}

MultipartSoftwareUpdate::MultipartSoftwareUpdate(
    sdbusplus::async::context& ctx, Software& software, const std::string& host,
    const std::string& uri,
    const std::set<RequestedApplyTimes>& allowedApplyTimes,
    const std::vector<std::string>& targets,
    SoftwareUpdateCallback successCallback) :
    sdbusplus::aserver::xyz::openbmc_project::software::Update<
        MultipartSoftwareUpdate>(ctx, software.getPath().str.c_str()),
    software(software), host(host), uri(uri),
    allowedApplyTimes(allowedApplyTimes), targets(targets),
    successCallback(std::move(successCallback))
{}

auto MultipartSoftwareUpdate::method_call(start_update_t /*unused*/, auto image,
                                          auto applyTime)
    -> sdbusplus::async::task<start_update_t::return_type>
{
    info("Request image update for {ID} with fd: {FD}", "ID", software.getId(),
         "FD", image.fd);
    if (inProgress)
    {
        error("An update is already in progress, cannot update.");
        phosphor::logging::elog<
            sdbusplus::xyz::openbmc_project::Common::Error::Unavailable>();
    }
    if (!allowedApplyTimes.contains(applyTime))
    {
        error("The selected apply time {APPLYTIME} is not allowed", "APPLYTIME",
              applyTime);
        phosphor::logging::elog<sdbusplus::error::xyz::openbmc_project::
                                    software::update::Incompatible>();
    }
    int imageDup = dup(image.fd);
    if (imageDup < 0)
    {
        error("ERROR calling dup on fd: {ERR}", "ERR", strerror(errno));
        phosphor::logging::elog<
            sdbusplus::xyz::openbmc_project::Common::Error::InternalFailure>();
    }
    inProgress = true;
    info("Start image update for {ID} with fd: {FD}", "ID", software.getId(), "FD",
         imageDup);
    auto newSoftware = std::make_unique<Software>(ctx, software.getId());
    newSoftware->setActivation(SoftwareActivation::Activations::NotReady);
    auto newPath = newSoftware->getPath();
    ctx.spawn(update(imageDup, std::move(newSoftware)) |
              sdbusplus::async::execution::then([imageDup, this]() {
                  inProgress = false;
                  close(imageDup);
              }));
    co_return newPath;
}

auto MultipartSoftwareUpdate::get_property(
    allowed_apply_times_t /*unused*/) const
{
    return allowedApplyTimes;
}

auto MultipartSoftwareUpdate::update(sdbusplus::message::unix_fd image,
                                     std::unique_ptr<Software> newSoftware)
    -> sdbusplus::async::task<void>
{
    const auto updateUrl = std::format("http://{}{}", host, uri);
    AsyncHttpHandle updateHandle{updateUrl, 0 /*timeoutSec*/};
    redfish_binding::UpdateService::UpdateParameters updateParameters;
    updateParameters.getTargets().setValue(targets);
    updateParameters.getForceUpdate().setValue(true);
    info("{ID} image update: start sending image to {URL}", "ID",
         software.getId(), "URL", updateUrl);
    auto response = co_await updateHandle.tryPost(
        ctx, std::vector<HttpMultipartBodyPart>{
                 {
                     .name = "UpdateParameters",
                     .type = "application/json",
                     .data = updateParameters.toJson().dump(),
                 },
                 {
                     .name = "UpdateFile",
                     .type = "application/octet-stream",
                     .fd = &image.fd,
                 },
             });
    info("{ID} image update: complete sending image to {URL}", "ID",
         software.getId(), "URL", updateUrl);
    if (!response.has_value())
    {
        error("Http error for {URL}: {ERR}", "URL", updateUrl, "ERR",
              response.error());
        newSoftware->setActivation(SoftwareActivation::Activations::Failed);
        co_return;
    }
    if (response->code != 200 && response->code != 202)
    {
        error("Http response error code from {URL}: {CODE}", "URL", updateUrl,
              "CODE", response->code);
        newSoftware->setActivation(SoftwareActivation::Activations::Failed);
        co_return;
    }
    auto tryTask = redfish_binding::Task::tryParseTask(response->body);
    if (!tryTask.has_value())
    {
        error("Failed to parse task from {URL}: {ERR}, response: {RES}", "URL",
              updateUrl, "ERR", tryTask.error(), "RES", response->body);
        newSoftware->setActivation(SoftwareActivation::Activations::Failed);
        co_return;
    }
    auto& task = tryTask.value();
    if (!task.getOdataId().hasValue())
    {
        error("Invalid http response from {URL}: {RES}", "URL", updateUrl,
              "RES", response->body);
        newSoftware->setActivation(SoftwareActivation::Activations::Failed);
        co_return;
    }
    newSoftware->setActivation(SoftwareActivation::Activations::Activating);
    SoftwareActivationProgress activationProgress{
        ctx, newSoftware->getPath().str.c_str()};
    activationProgress.emit_added();
    SoftwareActivationBlocksTransition activationBlocksTransition{
        ctx, newSoftware->getPath().str.c_str()};
    activationBlocksTransition.emit_added();
    const auto taskUrl =
        std::format("http://{}{}", host, task.getOdataId().value());
    AsyncHttpHandle taskHandle{taskUrl};
    while (!ctx.stop_requested())
    {
        auto taskInfo = co_await getTaskInfo(
            ctx, taskHandle, taskUrl, config().getTaskMaxRetries,
            config().getTaskRetryIntervalMilliseconds);
        if (!taskInfo.has_value())
        {
            break;
        }
        activationProgress.progress(taskInfo->percentComplete);
        if (taskInfo->state == redfish_binding::Task::TaskState::Completed)
        {
            info("{ID} image update succeeded", "ID", software.getId());
            co_await successCallback(ctx, std::move(newSoftware));
            co_return;
        }
        if (taskInfo->state != redfish_binding::Task::TaskState::Running)
        {
            break;
        }
        co_await sdbusplus::async::sleep_for(
            ctx,
            std::chrono::milliseconds(config().getTaskIntervalMilliseconds));
    }
    error("{ID} image update failed", "ID", software.getId());
    newSoftware->setActivation(SoftwareActivation::Activations::Failed);
    co_return;
}

Software::Software(sdbusplus::async::context& ctx, const std::string& id) :
    sdbusplus::async::context_ref(ctx), id(id),
    path(sdbusplus::object_path(SoftwareVersion::namespace_path) /
         std::format("{}_{}", id, randomIdGenerator()()))
{}

sdbusplus::object_path Software::getPath() const
{
    return path;
}

std::string Software::getId() const
{
    return id;
}

void Software::setVersion(const std::string& versionStr)
{
    if (!version)
    {
        version = std::make_unique<SoftwareVersion>(
            ctx, path.str.c_str(),
            SoftwareVersion::properties_t{
                .version = versionStr,
                .purpose = SoftwareVersion::VersionPurpose::Other,
            });
        version->emit_added();
        return;
    }
    version->version(versionStr);
}

void Software::setActivation(SoftwareActivation::Activations act)
{
    if (!activation)
    {
        activation = std::make_unique<SoftwareActivation>(
            ctx, path.str.c_str(),
            SoftwareActivation::properties_t{
                .activation = act,
                .requested_activation =
                    SoftwareActivation::RequestedActivations::None,
            });
        activation->emit_added();
        return;
    }
    activation->activation(act);
}

void Software::enableMultipartUpdate(const std::string& host,
                                     const std::string& uri,
                                     const std::vector<std::string>& targets,
                                     SoftwareUpdateCallback successCallback)
{
    multipartUpdate = std::make_unique<MultipartSoftwareUpdate>(
        ctx, *this, host, uri,
        std::set<RequestedApplyTimes>{RequestedApplyTimes::Immediate}, targets,
        std::move(successCallback));
    multipartUpdate->emit_added();
}

bool Software::isMultipartUpdateEnabled() const
{
    return multipartUpdate != nullptr;
}

std::function<int()>& Software::randomIdGenerator()
{
    static std::function<int()> generator = []() {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        unsigned int seed = ts.tv_nsec ^ getpid();
        srandom(seed);
        return random() % 10000;
    };
    return generator;
}

UpdateServiceHandler::UpdateServiceHandler(
    const std::string& host, const std::string& inventoryName,
    const std::vector<UpdateServiceMapper>& mappers) :
    host(host),
    inventoryUrl(std::format("http://{}/redfish/v1/UpdateService/{}?$expand=.",
                             host, inventoryName)),
    handle(std::make_unique<AsyncHttpHandle>(inventoryUrl)), mappers(mappers)
{}

auto UpdateServiceHandler::load(sdbusplus::async::context& ctx)
    -> sdbusplus::async::task<void>
{
    auto response = co_await handle->tryGet(ctx);
    if (!response.has_value())
    {
        error("Http error for {URL}: {ERR}", "URL", inventoryUrl, "ERR",
              response.error());
        co_return;
    }
    if (response->code != 200)
    {
        error("Http response error code from {URL}: {CODE}", "URL",
              inventoryUrl, "CODE", response->code);
        co_return;
    }
    auto tryInventoryCollection = redfish_binding::SoftwareInventoryCollection::
        tryParseSoftwareInventoryCollection(response->body);
    if (!tryInventoryCollection.has_value())
    {
        error(
            "Failed to parse software inventory collection from {URL}: {ERR}, response: {RES}",
            "URL", inventoryUrl, "ERR", tryInventoryCollection.error(), "RES",
            response->body);
        co_return;
    }
    auto& inventoryCollection = tryInventoryCollection.value();
    auto& maybeMembers = inventoryCollection.getMembers();
    if (!maybeMembers.hasValue())
    {
        co_return;
    }
    if (!multipartUpdateUri.has_value())
    {
        multipartUpdateUri = co_await getMultipartUpdateUri(ctx, host);
    }
    for (auto& member : maybeMembers.value())
    {
        auto& maybeUri = member.getOdataId();
        if (!maybeUri.hasValue())
        {
            continue;
        }
        const auto& uri = maybeUri.value();
        auto& maybeId = member.getId();
        if (!maybeId.hasValue())
        {
            continue;
        }
        const auto& id = maybeId.value();
        auto& maybeVersion = member.getVersion();
        if (!maybeVersion.hasValue())
        {
            continue;
        }
        const auto& version = maybeVersion.value();
        const auto mapperIt =
            std::find_if(mappers.begin(), mappers.end(),
                         [&](auto mapper) { return mapper.fromId == id; });
        if (mapperIt == mappers.end())
        {
            continue;
        }
        auto softwareIt = inventory.find(id);
        if (softwareIt == inventory.end())
        {
            softwareIt =
                inventory
                    .insert(
                        {id, std::make_unique<Software>(ctx, mapperIt->toId)})
                    .first;
            info("Create software {PATH} from {ID}", "PATH",
                 softwareIt->second->getPath().str, "ID", id);
        }
        softwareIt->second->setVersion(version);
        softwareIt->second->setActivation(
            SoftwareActivation::Activations::Active);
        auto& updateable = member.getUpdateable();
        if (multipartUpdateUri.has_value() &&
            !softwareIt->second->isMultipartUpdateEnabled() &&
            updateable.hasValue() && updateable.value())
        {
            auto targets =
                mapperIt->updateParametersTargetsOverride.value_or({uri});
            softwareIt->second->enableMultipartUpdate(
                host, multipartUpdateUri.value(), targets,
                [softwareIt, this](sdbusplus::async::context& ctx,
                                   std::unique_ptr<Software> newSoftware)
                    -> sdbusplus::async::task<void> {
                    std::swap(softwareIt->second, newSoftware);
                    info("Replace software {OLDPATH} with {NEWPATH}", "OLDPATH",
                         newSoftware->getPath().str, "NEWPATH",
                         softwareIt->second->getPath().str);
                    co_await load(ctx);
                    co_return;
                });
            info("Enable multipart update for {ID} with targets: {TARGETS}", "ID", id,
                 "TARGETS", nlohmann::json(targets).dump());
        }
    }
}

auto UpdateServiceHandler::run(sdbusplus::async::context& ctx,
                               const std::string& host,
                               const UpdateServiceConfig& config)
    -> sdbusplus::async::task<void>
{
    if (!config.firmwareMappers.empty())
    {
        ctx.spawn(loop(ctx,
                       std::make_unique<UpdateServiceHandler>(
                           host, "FirmwareInventory", config.firmwareMappers),
                       config.intervalMilliseconds));
    }
    if (!config.softwareMappers.empty())
    {
        ctx.spawn(loop(ctx,
                       std::make_unique<UpdateServiceHandler>(
                           host, "SoftwareInventory", config.softwareMappers),
                       config.intervalMilliseconds));
    }
    co_return;
}

auto UpdateServiceHandler::getMultipartUpdateUri(sdbusplus::async::context& ctx,
                                                 const std::string& host)
    -> sdbusplus::async::task<std::optional<std::string>>
{
    const auto updateServiceUrl =
        std::format("http://{}/redfish/v1/UpdateService", host);
    AsyncHttpHandle handle{updateServiceUrl};
    auto response = co_await handle.tryGet(ctx);
    if (!response.has_value())
    {
        info("Http error for {URL}: {ERR}", "URL", updateServiceUrl, "ERR",
             response.error());
        co_return std::nullopt;
    }
    if (response->code != 200)
    {
        info("Http response error from {URL}: {CODE}", "URL", updateServiceUrl,
             "CODE", response->code);
        co_return std::nullopt;
    }
    auto tryUpdateService =
        redfish_binding::UpdateService::tryParseUpdateService(response->body);
    if (!tryUpdateService.has_value())
    {
        info(
            "Failed to parse update service from {URL}: {ERR}, response: {RES}",
            "URL", updateServiceUrl, "ERR", tryUpdateService.error(), "RES",
            response->body);
        co_return std::nullopt;
    }
    auto& updateService = tryUpdateService.value();
    auto& maybeMultipartHttpPushUri = updateService.getMultipartHttpPushUri();
    if (!maybeMultipartHttpPushUri.hasValue())
    {
        co_return std::nullopt;
    }
    co_return maybeMultipartHttpPushUri.value();
}

} // namespace redfish_client::core
