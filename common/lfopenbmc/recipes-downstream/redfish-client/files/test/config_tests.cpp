#include <redfish_client/core/config.hpp>

#include <stdexcept>

#include <gtest/gtest.h>

using redfish_client::core::Config;
using redfish_client::core::ThresholdOverride;

TEST(Config, FullConfig)
{
    std::string configJson = R"(
{
  "host": "0.0.0.1",
  "compatible": "com.meta.Hardware.BMC.TEST",
  "sensorConfig": {
    "associationPath": "/xyz/openbmc_project/inventory/system/board/A",
    "intervalMilliseconds": 2000,
    "maxRetries": 3,
    "retryIntervalMilliseconds": 100,
    "mappers": [
      {
        "fromUrl": "/redfish/v1/Chassis/Chassis_A/Sensors/Temp_0",
        "toNamespace": "temperature",
        "toId": "Chassis_A_Temp_0",
        "thresholds": {
          "hardShutdownHigh": 120.0,
          "criticalHigh": null
        }
      },
      {
        "fromUrl": "/redfish/v1/Chassis/Chassis_A/Sensors/Voltage_2",
        "toNamespace": "voltage",
        "toId": "Chassis_A_Voltage_2"
      }
    ]
  },
  "logServiceConfig": {
    "urls": [
      "/redfish/v1/Systems/system_0/LogServices/EventLog/Entries"
    ],
    "intervalMilliseconds": 5000,
    "skipHistoricalEntriesThresholdSeconds": 60
  },
  "updateServiceConfig": {
    "intervalMilliseconds": 100000,
    "firmwareMappers": [
      {
        "fromId": "FW1",
        "toId": "chassis/FW1",
        "updateParametersTargetsOverride": ["A"]
      },
      {
        "fromId": "FW2",
        "toId": "chassis/FW2"
      }
    ],
    "softwareMappers": [
      {
        "fromId": "SW0",
        "toId": "chassis/SW0"
      }
    ]
  }
}
)";
    auto config = Config::parse(configJson);
    EXPECT_EQ(config.host, "0.0.0.1");
    EXPECT_EQ(config.compatible, "com.meta.Hardware.BMC.TEST");
    EXPECT_EQ(config.sensorConfig.has_value(), true);
    EXPECT_EQ(config.sensorConfig->associationPath,
              "/xyz/openbmc_project/inventory/system/board/A");
    EXPECT_EQ(config.sensorConfig->intervalMilliseconds, 2000);
    EXPECT_EQ(config.sensorConfig->maxRetries, 3);
    EXPECT_EQ(config.sensorConfig->retryIntervalMilliseconds, 100);
    EXPECT_EQ(config.sensorConfig->metricReportUrls.has_value(), false);
    EXPECT_EQ(config.sensorConfig->mappers.size(), 2);
    EXPECT_EQ(config.sensorConfig->mappers[0].fromUrl,
              "/redfish/v1/Chassis/Chassis_A/Sensors/Temp_0");
    EXPECT_EQ(config.sensorConfig->mappers[0].toNamespace, "temperature");
    EXPECT_EQ(config.sensorConfig->mappers[0].toId, "Chassis_A_Temp_0");
    EXPECT_EQ(config.sensorConfig->mappers[0].thresholds.hardShutdownHigh.mode,
              ThresholdOverride::Mode::Value);
    EXPECT_EQ(config.sensorConfig->mappers[0].thresholds.hardShutdownHigh.value,
              120.0);
    EXPECT_EQ(config.sensorConfig->mappers[0].thresholds.criticalHigh.mode,
              ThresholdOverride::Mode::Disabled);
    EXPECT_EQ(config.sensorConfig->mappers[0].thresholds.warningHigh.mode,
              ThresholdOverride::Mode::Inherit);
    EXPECT_EQ(config.sensorConfig->mappers[1].fromUrl,
              "/redfish/v1/Chassis/Chassis_A/Sensors/Voltage_2");
    EXPECT_EQ(config.sensorConfig->mappers[1].toNamespace, "voltage");
    EXPECT_EQ(config.sensorConfig->mappers[1].toId, "Chassis_A_Voltage_2");
    EXPECT_EQ(config.sensorConfig->mappers[1].thresholds.hardShutdownHigh.mode,
              ThresholdOverride::Mode::Inherit);
    EXPECT_EQ(config.logServiceConfig.has_value(), true);
    EXPECT_EQ(config.logServiceConfig->urls.size(), 1);
    EXPECT_EQ(config.logServiceConfig->urls[0],
              "/redfish/v1/Systems/system_0/LogServices/EventLog/Entries");
    EXPECT_EQ(config.logServiceConfig->intervalMilliseconds, 5000);
    EXPECT_EQ(
        config.logServiceConfig->skipHistoricalEntriesThresholdSeconds
            .has_value(),
        true);
    EXPECT_EQ(
        config.logServiceConfig->skipHistoricalEntriesThresholdSeconds.value(),
        60);
    EXPECT_EQ(config.updateServiceConfig->intervalMilliseconds, 100000);
    EXPECT_EQ(config.updateServiceConfig->firmwareMappers.size(), 2);
    EXPECT_EQ(config.updateServiceConfig->firmwareMappers[0].fromId, "FW1");
    EXPECT_EQ(config.updateServiceConfig->firmwareMappers[0].toId,
              "chassis/FW1");
    EXPECT_EQ(config.updateServiceConfig->firmwareMappers[0]
                  .updateParametersTargetsOverride.has_value(),
              true);
    EXPECT_EQ(config.updateServiceConfig->firmwareMappers[0]
                  .updateParametersTargetsOverride.value(),
              std::vector<std::string>{"A"});
    EXPECT_EQ(config.updateServiceConfig->firmwareMappers[1].fromId, "FW2");
    EXPECT_EQ(config.updateServiceConfig->firmwareMappers[1].toId,
              "chassis/FW2");
    EXPECT_EQ(config.updateServiceConfig->firmwareMappers[1]
                  .updateParametersTargetsOverride.has_value(),
              false);
    EXPECT_EQ(config.updateServiceConfig->softwareMappers.size(), 1);
    EXPECT_EQ(config.updateServiceConfig->softwareMappers[0].fromId, "SW0");
    EXPECT_EQ(config.updateServiceConfig->softwareMappers[0].toId,
              "chassis/SW0");
    EXPECT_EQ(config.updateServiceConfig->softwareMappers[0]
                  .updateParametersTargetsOverride.has_value(),
              false);
}

TEST(Config, MetricReportConfig)
{
    std::string configJson = R"(
{
  "host": "0.0.0.1",
  "compatible": "com.meta.Hardware.BMC.TEST",
  "sensorConfig": {
    "associationPath": "/xyz/openbmc_project/inventory/system/board/A",
    "intervalMilliseconds": 2000,
    "maxRetries": 3,
    "retryIntervalMilliseconds": 100,
    "metricReportUrls": [
      "/redfish/v1/TelemetryService/MetricReports/Report1",
      "/redfish/v1/TelemetryService/MetricReports/Report2"
    ],
    "mappers": []
  }
}
)";
    auto config = Config::parse(configJson);
    EXPECT_EQ(config.sensorConfig.has_value(), true);
    EXPECT_EQ(config.sensorConfig->metricReportUrls.has_value(), true);
    EXPECT_EQ(config.sensorConfig->metricReportUrls.value().size(), 2);
    EXPECT_EQ(config.sensorConfig->metricReportUrls.value()[0],
              "/redfish/v1/TelemetryService/MetricReports/Report1");
    EXPECT_EQ(config.sensorConfig->metricReportUrls.value()[1],
              "/redfish/v1/TelemetryService/MetricReports/Report2");
}

TEST(Config, PartialConfig)
{
    std::string configJson = R"(
{
  "host": "0.0.0.1",
  "compatible": "com.meta.Hardware.BMC.TEST",
  "logServiceConfig": {
    "urls": [
      "/redfish/v1/Systems/system_0/LogServices/EventLog/Entries"
    ],
    "intervalMilliseconds": 5000
  }
}
)";
    auto config = Config::parse(configJson);
    EXPECT_EQ(config.host, "0.0.0.1");
    EXPECT_EQ(config.compatible, "com.meta.Hardware.BMC.TEST");
    EXPECT_EQ(config.sensorConfig.has_value(), false);
    EXPECT_EQ(config.logServiceConfig.has_value(), true);
    EXPECT_EQ(config.logServiceConfig->urls.size(), 1);
    EXPECT_EQ(config.logServiceConfig->urls[0],
              "/redfish/v1/Systems/system_0/LogServices/EventLog/Entries");
    EXPECT_EQ(config.logServiceConfig->intervalMilliseconds, 5000);
    // When not specified in JSON, should use default value of 600
    EXPECT_EQ(config.logServiceConfig->skipHistoricalEntriesThresholdSeconds
                  .has_value(),
              true);
    EXPECT_EQ(
        config.logServiceConfig->skipHistoricalEntriesThresholdSeconds.value(),
        600);
}

TEST(Config, DisableHistoricalFilterConfig)
{
    std::string configJson = R"(
{
  "host": "0.0.0.1",
  "compatible": "com.meta.Hardware.BMC.TEST",
  "logServiceConfig": {
    "urls": [
      "/redfish/v1/Systems/system_0/LogServices/EventLog/Entries"
    ],
    "intervalMilliseconds": 5000,
    "skipHistoricalEntriesThresholdSeconds": null
  }
}
)";
    auto config = Config::parse(configJson);
    EXPECT_EQ(config.logServiceConfig.has_value(), true);
    EXPECT_EQ(
        config.logServiceConfig->skipHistoricalEntriesThresholdSeconds
            .has_value(),
        false);
}

TEST(Config, InvalidConfig)
{
    std::string configJson = R"(
{
  "unknown": "unknown"
}
)";
    EXPECT_THROW(Config::parse(configJson), nlohmann::json::out_of_range);
}

TEST(Config, RejectsUnknownSensorNamespace)
{
    std::string configJson = R"(
{
  "host": "0.0.0.1",
  "compatible": "com.meta.Hardware.BMC.TEST",
  "sensorConfig": {
    "associationPath": "/xyz/openbmc_project/inventory/system/board/A",
    "intervalMilliseconds": 2000,
    "maxRetries": 3,
    "retryIntervalMilliseconds": 100,
    "mappers": [
      {
        "fromUrl": "/redfish/v1/Chassis/Chassis_A/Sensors/Temp_0",
        "toNamespace": "not_a_real_namespace",
        "toId": "Chassis_A_Temp_0"
      }
    ]
  }
}
)";
    EXPECT_THROW(Config::parse(configJson), std::invalid_argument);
}
