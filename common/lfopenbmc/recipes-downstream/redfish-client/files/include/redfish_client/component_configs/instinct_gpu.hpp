#pragma once
#include <redfish_client/core/instinct_gpu_mapper.hpp>
#include <redfish_client/core/log_entry_mapper_registry.hpp>

namespace redfish_client::component_config
{

inline void registerInstinctGpuMappers(
    const std::optional<core::SensorConfig>& sensorConfig)
{
    auto& registry = core::LogEntryMapperRegistry::instance();

    registry.registerMapper(std::make_unique<core::InstinctGpuMapper>(sensorConfig->associationPath),
                            107);
}

} // namespace redfish_client::component_config