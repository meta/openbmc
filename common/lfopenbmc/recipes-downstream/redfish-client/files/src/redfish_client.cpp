#include <redfish_client/core/redfish_client.hpp>
#include <redfish_client/core/log_service_handler.hpp>
#include <redfish_client/core/sensor_handler.hpp>
#include <redfish_client/core/update_service_handler.hpp>
#include <redfish_client/core/log_entry_mapper_registry.hpp>
#include <redfish_client/core/unhandled_mapper.hpp>
#include <redfish_client/component_configs/component_registry.hpp>

#include <phosphor-logging/lg2.hpp>

#include <fstream>

PHOSPHOR_LOG2_USING;

namespace redfish_client::core
{

static inline auto subtree(sdbusplus::async::context& ctx,
                           const auto& subpath, const auto& interface,
                           size_t depth = 0)
{
    using ObjectMapper =
        sdbusplus::client::xyz::openbmc_project::ObjectMapper<>;
    auto mapper = ObjectMapper(ctx)
                      .service(ObjectMapper::default_service)
                      .path(ObjectMapper::instance_path);
    return mapper.get_sub_tree(subpath, depth, {interface});
}

RedfishClient::RedfishClient(sdbusplus::async::context& ctx, const std::string& configDir,
              const std::string& persistDir) :
    ctx(ctx), configDir(configDir), persistDir(persistDir)
{}

RedfishClient::RedfishClient(sdbusplus::async::context& ctx,
                             const std::vector<Config>& configs,
                             const std::string& persistDir) :
    ctx(ctx), configs(configs), persistDir(persistDir)
{}

auto RedfishClient::run() -> sdbusplus::async::task<>
{
    info("Running RedfishClient");
    if (configs.empty())
    {
        co_await loadConfig();
    }

    registerLogMappers();

    for (const auto& config : configs)
    {
        if (config.sensorConfig.has_value())
        {
            const auto& sensorConfig = config.sensorConfig.value();
            info("Configured Sensor objects: {SIZE}", "SIZE",
                 sensorConfig.mappers.size());
            ctx.spawn(SensorHandler::run(ctx, config.host, sensorConfig));
        }

        if (config.logServiceConfig.has_value())
        {
            ctx.spawn(LogServiceHandler::run(
                ctx, config.host, config.logServiceConfig.value(), persistDir));
        }

        if (config.updateServiceConfig.has_value())
        {
            ctx.spawn(UpdateServiceHandler::run(
                ctx, config.host, config.updateServiceConfig.value()));
        }
    }
    co_return;
}

RedfishClient::~RedfishClient() = default;

auto RedfishClient::loadConfig() -> sdbusplus::async::task<>
{
    auto compatiblePlatformName = co_await getCompatiblePlatformNames();
    configs = loadCompatibleConfigs(configDir, compatiblePlatformName);
    co_return;
}

void RedfishClient::registerLogMappers()
{
    auto& registry = LogEntryMapperRegistry::instance();
    if (!configs.empty() && configs.front().components)
    {
        const auto& config = configs.front();
        for (const auto& componentName : config.components.value())
        {
            info("Registering component: {COMPONENT}", "COMPONENT", componentName);
            component_config::registerComponent(componentName, config, ctx,
                                                config.host);
        }
    }
    registry.registerMapper(std::make_unique<UnhandledMapper>(), 0);
    info("Mapper registration complete");
}

std::vector<Config> RedfishClient::loadCompatibleConfigs(
    const std::string& configDir,
    const std::vector<std::string>& compatiblePlatformNames)
{
    std::vector<Config> matchedConfigs;
    namespace fs = std::filesystem;
    for (const auto& entry : fs::directory_iterator(configDir))
    {
        std::ifstream file(entry.path());
        if (!file.is_open())
        {
            error("Failed to open file: {FILE}", "FILE",
                  entry.path().string());
            continue;
        }
        std::string jsonContents((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
        auto config = Config::parse(jsonContents);

        for (auto platformName: compatiblePlatformNames)
        {
            if (config.compatible == platformName)
            {
                info("Matched config file: {FILE}", "FILE",
                     entry.path().string());
                matchedConfigs.push_back(std::move(config));
                break;
            }
        }
    }
    if (matchedConfigs.empty())
    {
        error("No matching config file found for platform list: {PLATFORM}",
              "PLATFORM", std::format("{}", compatiblePlatformNames));
        throw std::runtime_error("No matching config file found");
    }

    return matchedConfigs;
}

auto RedfishClient::subtree_for_target_interface(
    sdbusplus::async::context& ctx, const std::string& subpath,
    const std::string& targetInterface,
    const std::function<sdbusplus::async::task<>(
        const std::string&, const std::string&, const std::string&)>&
        coroutine,
    size_t depth) -> sdbusplus::async::task<>
{
    auto objects = co_await subtree(ctx, subpath, targetInterface, depth);
    info("iterating over entries.");
    for (const auto& [path, services] : objects)
    {
        for (const auto& [service, interfaces] : services)
        {
            info("Examining {INTERFACE} at {PATH} by {SERVICE}",
                 "INTERFACE", targetInterface, "PATH", path, "SERVICE",
                 service);
            co_await coroutine(path, service, targetInterface);
        }
    }
    co_return;
}

auto RedfishClient::getCompatiblePlatformNames()
    -> sdbusplus::async::task<std::vector<std::string>>
{
    std::vector<std::string> platformNames;
    while (!ctx.stop_requested() && platformNames.empty())
    {
        try
        {
            co_await subtree_for_target_interface(
                ctx, "/xyz/openbmc_project/inventory/system/board/",
                "xyz.openbmc_project.Inventory.Decorator.Compatible",
                [&](const auto& path, const auto& service,
                    const auto& interface) -> sdbusplus::async::task<> {
                    auto names =
                        co_await sdbusplus::async::proxy()
                            .service(service)
                            .path(path)
                            .interface(
                                "xyz.openbmc_project.Inventory.Decorator.Compatible")
                            .template get_property<
                                std::vector<std::string>>(ctx, "Names");
                    for (auto name : names)
                    {
                        info("Compatible : Names : {PLATFORMNAME}",
                             "PLATFORMNAME", name);
                    }

                    platformNames.insert(platformNames.end(), names.begin(),
                                         names.end());
                    co_return;
                },
                1);

            if (platformNames.empty())
            {
                info("platformNames is still empty. Retry again");
            }
        }
        catch (const sdbusplus::exception::exception& e)
        {
            info("subtree query failed: {ERR}", "ERR", e.what());
        }
        co_await sdbusplus::async::sleep_for(
            ctx, std::chrono::milliseconds(500));
    }
    co_return platformNames;
}

} // namespace redfish_client::core
