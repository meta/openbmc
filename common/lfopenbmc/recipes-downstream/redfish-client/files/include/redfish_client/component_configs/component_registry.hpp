#pragma once
#include <redfish_client/component_configs/grace_cpu.hpp>
#include <redfish_client/component_configs/blackwell_gpu.hpp>
#include <redfish_client/component_configs/hgx_power_supply.hpp>
#include <redfish_client/component_configs/hgx_thermal.hpp>
#include <redfish_client/component_configs/hgx_leak_detector.hpp>
#include <redfish_client/component_configs/sensor_threshold.hpp>
#include <redfish_client/component_configs/instinct_cper.hpp>
#include <redfish_client/component_configs/instinct_gpu.hpp>
#include <redfish_client/component_configs/instinct_amc.hpp>
#include <redfish_client/component_configs/instinct_thermal.hpp>
#include <redfish_client/component_configs/instinct_power_supply.hpp>
#include <redfish_client/component_configs/common_openbmc.hpp>
#include <redfish_client/component_configs/common_update.hpp>
#include <redfish_client/component_configs/common_environmental.hpp>
#include <redfish_client/component_configs/vera_cpu.hpp>
#include <redfish_client/component_configs/rubin_gpu.hpp>
#include <redfish_client/core/config.hpp>
#include <string>
#include <stdexcept>

namespace redfish_client::component_config {

inline void registerComponent(const std::string& componentName,
                               const core::Config& config,
                               sdbusplus::async::context& ctx, 
                               const std::string& host) {
    if (componentName == "grace_cpu")
    {
        registerGraceCpuMappers();
    }
    else if (componentName == "blackwell_gpu")
    {
        registerBlackwellGpuMappers();
    }
    else if (componentName == "hgx_power_supply")
    {
        registerHgxPowerSupplyMappers();
    }
    else if (componentName == "hgx_thermal")
    {
        registerHgxThermalMappers();
    }
    else if (componentName == "hgx_leak_detector")
    {
        registerHgxLeakDetectorMappers();
    }
    else if (componentName == "sensor_threshold")
    {
        registerSensorThresholdMappers(config.sensorConfig);
    }
    else if (componentName == "instinct_cper")
    {
        registerInstinctCperMappers(ctx, host);
    }
    else if (componentName == "instinct_gpu")
    {
        registerInstinctGpuMappers(config.sensorConfig);
    }
    else if (componentName == "instinct_amc")
    {
        registerInstinctAmcMappers(config.sensorConfig);
    }
    else if (componentName == "instinct_thermal")
    {
        registerInstinctThermalMappers();
    }
    else if (componentName == "instinct_power_supply")
    {
        registerInstinctPowerSupplyMappers();
    }
    else if (componentName == "common_openbmc")
    {
        registerCommonOpenBmcMappers();
    }
    else if (componentName == "common_update")
    {
        registerCommonUpdateMappers();
    }
    else if (componentName == "common_environmental")
    {
        registerCommonEnvironmentalMappers();
    }
    else if (componentName == "vera_cpu")
    {
        registerVeraCpuMappers();
    }
    else if (componentName == "rubin_gpu")
    {
        registerRubinGpuMappers();
    }
    else
    {
        throw std::runtime_error("Unknown component: " + componentName);
    }
}

} // namespace redfish_client::component_config
