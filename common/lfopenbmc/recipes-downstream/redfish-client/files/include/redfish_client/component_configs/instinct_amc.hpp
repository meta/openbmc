#pragma once
#include <redfish_client/core/instinct_amc_mapper.hpp>
#include <redfish_client/core/log_entry_mapper_registry.hpp>

namespace redfish_client::component_config
{

inline void registerInstinctAmcMappers(
    const std::optional<core::SensorConfig>& sensorConfig)
{
    auto& registry = core::LogEntryMapperRegistry::instance();

    registry.registerMapper(std::make_unique<core::InstinctAmcMapper>(sensorConfig->associationPath),
                            108);
}

} // namespace redfish_client::component_config