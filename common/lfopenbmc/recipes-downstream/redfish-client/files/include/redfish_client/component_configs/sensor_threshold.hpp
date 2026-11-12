#pragma once
#include <redfish_client/core/sensor_threshold_mapper.hpp>
#include <redfish_client/core/log_entry_mapper_registry.hpp>
#include <redfish_client/core/config.hpp>
#include <optional>

namespace redfish_client::component_config
{

inline void registerSensorThresholdMappers(
    const std::optional<core::SensorConfig>& sensorConfig)
{
    auto& registry = core::LogEntryMapperRegistry::instance();

    registry.registerMapper(
        std::make_unique<core::SensorThresholdMapper>(sensorConfig->mappers),
        103);
}

} // namespace redfish_client::component_config
