#include <redfish_client/core/config.hpp>
#include <xyz/openbmc_project/Sensor/Value/common.hpp>

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace redfish_client::core
{

namespace
{

// Validate a configured sensor namespace against the set defined by the D-Bus
// Sensor.Value interface. The configured string is used verbatim to compose
// sensor object paths, so it must be one the interface actually supports.
bool isValidSensorNamespace(std::string_view ns)
{
    using NamespacePath =
        sdbusplus::common::xyz::openbmc_project::sensor::Value::namespace_path;
    static const std::unordered_set<std::string_view> valid = {
        NamespacePath::airflow,     NamespacePath::altitude,
        NamespacePath::current,     NamespacePath::energy,
        NamespacePath::fan_tach,    NamespacePath::humidity,
        NamespacePath::liquidflow,  NamespacePath::power,
        NamespacePath::pressure,    NamespacePath::temperature,
        NamespacePath::utilization, NamespacePath::voltage};
    return valid.contains(ns);
}

} // namespace

void from_json(const nlohmann::json& json, UpdateServiceMapper& mapper)
{
    json.at("fromId").get_to(mapper.fromId);
    json.at("toId").get_to(mapper.toId);
    if (auto it = json.find("updateParametersTargetsOverride");
        it != json.end())
    {
        mapper.updateParametersTargetsOverride =
            it->template get<std::vector<std::string>>();
    }
}

void from_json(const nlohmann::json& json, LogServiceConfig& config)
{
    json.at("urls").get_to(config.urls);
    json.at("intervalMilliseconds").get_to(config.intervalMilliseconds);
    if (auto it = json.find("skipHistoricalEntriesThresholdSeconds");
        it != json.end())
    {
        if (it->is_null())
        {
            config.skipHistoricalEntriesThresholdSeconds = std::nullopt;
        }
        else
        {
            it->get_to(config.skipHistoricalEntriesThresholdSeconds.emplace());
        }
    }
}

namespace
{

void parseThresholdOverrideLimit(const nlohmann::json& json,
                                 const std::string& key,
                                 ThresholdOverride::Limit& limit)
{
    auto it = json.find(key);
    if (it == json.end())
    {
        limit.mode = ThresholdOverride::Mode::Inherit;
        return;
    }
    if (it->is_null())
    {
        limit.mode = ThresholdOverride::Mode::Disabled;
        return;
    }
    limit.mode = ThresholdOverride::Mode::Value;
    it->get_to(limit.value);
}

} // namespace

void from_json(const nlohmann::json& json, ThresholdOverride& override)
{
    parseThresholdOverrideLimit(json, "warningHigh", override.warningHigh);
    parseThresholdOverrideLimit(json, "warningLow", override.warningLow);
    parseThresholdOverrideLimit(json, "criticalHigh", override.criticalHigh);
    parseThresholdOverrideLimit(json, "criticalLow", override.criticalLow);
    parseThresholdOverrideLimit(json, "hardShutdownHigh",
                                override.hardShutdownHigh);
    parseThresholdOverrideLimit(json, "hardShutdownLow",
                                override.hardShutdownLow);
}

void from_json(const nlohmann::json& json, SensorMapper& mapper)
{
    json.at("fromUrl").get_to(mapper.fromUrl);
    json.at("toNamespace").get_to(mapper.toNamespace);
    json.at("toId").get_to(mapper.toId);
    if (auto it = json.find("thresholds"); it != json.end())
    {
        it->get_to(mapper.thresholds);
    }
}

void from_json(const nlohmann::json& json, SensorConfig& config)
{
    json.at("associationPath").get_to(config.associationPath);
    json.at("mappers").get_to(config.mappers);
    if (auto it = std::ranges::find_if(config.mappers,
                                       [](const auto& mapper) {
                                           return !isValidSensorNamespace(
                                               mapper.toNamespace);
                                       });
        it != config.mappers.end())
    {
        throw std::invalid_argument(
            "Unknown sensor namespace in config: " + it->toNamespace);
    }
    json.at("intervalMilliseconds").get_to(config.intervalMilliseconds);
    json.at("maxRetries").get_to(config.maxRetries);
    json.at("retryIntervalMilliseconds")
        .get_to(config.retryIntervalMilliseconds);
    if (auto it = json.find("metricReportUrls"); it != json.end())
    {
        config.metricReportUrls = it->template get<std::vector<std::string>>();
    }
    if (auto it = json.find("cacheConnection"); it != json.end())
    {
        it->get_to(config.cacheConnection);
    }
    if (auto it = json.find("ignoreUnavailableSensor"); it != json.end())
    {
        it->get_to(config.ignoreUnavailableSensor);
    }
}

void from_json(const nlohmann::json& json, Config& config)
{
    json.at("host").get_to(config.host);
    json.at("compatible").get_to(config.compatible);
    if (auto it = json.find("components"); it != json.end())
    {
        config.components = it->template get<std::vector<std::string>>();
    }
    if (auto it = json.find("sensorConfig"); it != json.end())
    {
        config.sensorConfig = it->template get<SensorConfig>();
    }
    if (auto it = json.find("logServiceConfig"); it != json.end())
    {
        config.logServiceConfig = it->template get<LogServiceConfig>();
    }
    if (auto it = json.find("updateServiceConfig"); it != json.end())
    {
        config.updateServiceConfig = it->template get<UpdateServiceConfig>();
    }
}

Config Config::parse(const std::string& configJson)
{
    return nlohmann::json::parse(configJson).template get<Config>();
}

} // namespace redfish_client::core
