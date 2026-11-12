#pragma once
#include <redfish_client/core/log_entry_mapper_registry.hpp>
#include <redfish_client/core/environmental_mapper.hpp>

namespace redfish_client::component_config {

inline void registerCommonEnvironmentalMappers() {
    auto& registry = core::LogEntryMapperRegistry::instance();

    registry.registerMapper(std::make_unique<core::EnvironmentalMapper>(), 105);
}

} // namespace redfish_client::component_config
