#pragma once
#include <redfish_client/core/log_entry_mapper_registry.hpp>
#include <redfish_client/core/instinct_power_supply_mapper.hpp>

namespace redfish_client::component_config {

inline void registerInstinctPowerSupplyMappers() {
    auto& registry = core::LogEntryMapperRegistry::instance();

    registry.registerMapper(std::make_unique<redfish_client::core::InstinctPowerSupplyMapper>(), 106);
}

} // namespace redfish_client::component_config
