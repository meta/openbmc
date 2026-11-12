#pragma once
#include <redfish_client/core/log_entry_mapper_registry.hpp>
#include <redfish_client/core/openbmc_mapper.hpp>

namespace redfish_client::component_config {

inline void registerCommonOpenBmcMappers() {
    auto& registry = core::LogEntryMapperRegistry::instance();

    registry.registerMapper(std::make_unique<core::OpenBmcMapper>(), 102);
}

} // namespace redfish_client::component_config
