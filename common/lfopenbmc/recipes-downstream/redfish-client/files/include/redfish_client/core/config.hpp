#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace redfish_client::core
{

struct ThresholdOverride
{
    // A single limit's override: inherit from the target (absent from
    // config), forced off regardless of what the target reports (present as
    // JSON null), or forced to a specific value (present as a number).
    enum class Mode
    {
        Inherit,
        Disabled,
        Value,
    };

    struct Limit
    {
        Mode mode = Mode::Inherit;
        double value = 0.0;
    };

    Limit warningHigh;
    Limit warningLow;
    Limit criticalHigh;
    Limit criticalLow;
    Limit hardShutdownHigh;
    Limit hardShutdownLow;
};

void from_json(const nlohmann::json& json, ThresholdOverride& override);

struct SensorMapper
{
    std::string fromUrl;
    std::string toNamespace;
    std::string toId;
    ThresholdOverride thresholds{};
};

void from_json(const nlohmann::json& json, SensorMapper& mapper);

struct SensorConfig
{
    std::string associationPath;
    std::vector<SensorMapper> mappers;
    size_t intervalMilliseconds;
    size_t maxRetries;
    size_t retryIntervalMilliseconds;
    std::optional<std::vector<std::string>> metricReportUrls;
    bool cacheConnection = true;
    bool ignoreUnavailableSensor = false;
};

void from_json(const nlohmann::json& json, SensorConfig& config);

struct LogServiceConfig
{
    std::vector<std::string> urls;
    size_t intervalMilliseconds;
    // Historical log entries are skipped when no persist file is found
    // (e.g. after BMC factory reset) to avoid re-committing pre-existing
    // logs from the SMC. The value is the threshold in seconds.
    // Defaults to 600s (10min) to account for BMC factory reset time.
    // Can be set to null in JSON to disable filtering.
    std::optional<size_t> skipHistoricalEntriesThresholdSeconds = 600;
};

void from_json(const nlohmann::json& json, LogServiceConfig& config);

struct UpdateServiceMapper
{
    std::string fromId;
    std::string toId;
    std::optional<std::vector<std::string>> updateParametersTargetsOverride;
};

void from_json(const nlohmann::json& json, UpdateServiceMapper& mapper);

struct UpdateServiceConfig
{
    std::vector<UpdateServiceMapper> firmwareMappers;
    std::vector<UpdateServiceMapper> softwareMappers;
    size_t intervalMilliseconds;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(UpdateServiceConfig, firmwareMappers,
                                   softwareMappers, intervalMilliseconds)
};

struct Config
{
    std::string host;
    std::string compatible;
    std::optional<std::vector<std::string>> components;
    std::optional<SensorConfig> sensorConfig;
    std::optional<LogServiceConfig> logServiceConfig;
    std::optional<UpdateServiceConfig> updateServiceConfig;

    static Config parse(const std::string& configJson);
};

void from_json(const nlohmann::json& json, Config& config);

} // namespace redfish_client::core
