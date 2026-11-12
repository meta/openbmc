#pragma once
#include <redfish_client/core/log_entry_mapper_registry.hpp>
#include <redfish_client/core/instinct_cper_mapper.hpp>
#include <sdbusplus/async/context.hpp>

namespace redfish_client::component_config {

inline void registerInstinctCperMappers(
    sdbusplus::async::context& ctx,
    const std::string& host)
{
    auto& registry = core::LogEntryMapperRegistry::instance();
    registry.registerMapper(
        std::make_unique<redfish_client::core::InstinctCperMapper>(
            ctx, host), 110);
}

} // namespace redfish_client::component_config