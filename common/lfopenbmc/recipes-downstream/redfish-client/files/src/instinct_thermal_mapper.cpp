#include <phosphor-logging/lg2.hpp>
#include <phosphor-logging/commit.hpp>
#include <redfish_client/core/instinct_thermal_mapper.hpp>
#include <redfish_client/core/log_entry_mapper_utils.hpp>
#include <xyz/openbmc_project/State/Thermal/event.hpp>
#include "redfish-binding/LogEntry_EventSeverity.hpp"
#include <xyz/openbmc_project/Logging/Extension/CPER/Processed/common.hpp>

#include <algorithm>
#include <vector>

namespace redfish_client::core {

namespace {
using MsgArgs = std::vector<std::string>;
using EventSeverity = redfish_binding::LogEntry::EventSeverity;

namespace ThermalError = sdbusplus::error::xyz::openbmc_project::state::Thermal;

/**
 * Define the key strings to be identified according to the specification.
 */
const std::vector<std::string> thermalKeywords = {
    "Critical - Throttling due to VR Hot",
    "Critical - Throttling due to Die Over Temp",
    "Critical - Throttling due to HBM Over Temp",
    "Critical Temperature Fault",
};

std::string getFailureData(const MsgArgs& args)
{
    auto getArg = [&args](size_t index) -> std::string_view {
        if (index < args.size())
        {
            return args[index];
        }
        return "unknown";
    };

    return std::format("The health of resource '{}' has changed to {}.",
                        getArg(0), getArg(1));
}

} // anonymous namespace

bool InstinctThermalMapper::canHandle(redfish_binding::LogEntry::LogEntry& entry) const {
    auto& maybeMessageId = entry.getMessageId();
    if (!maybeMessageId.hasValue()) return false;

    const std::string& msgId = maybeMessageId.value();

    // Check whether the MessageId complies with the specification.
    bool isValidMsgId = 
        (msgId.find("ResourceStatusChangedCritical") != std::string::npos ||
        msgId.find("ResourceStatusChangedWarning") != std::string::npos);

    if (!isValidMsgId) return false;

    // Check whether the MessageArgs contain thermal event keywords.
    auto& maybeArgs = entry.getMessageArgs();
    if (maybeArgs.hasValue()) {
        for (const auto& arg : maybeArgs.value()) {
            for (const auto& keyword : thermalKeywords) {
                if (arg == keyword) {
                    return true;
                }
            }
        }
    }

    return false;
}

void InstinctThermalMapper::map(redfish_binding::LogEntry::LogEntry& entry) {
    static const MsgArgs kNoArgs;
    auto& maybeArgs = entry.getMessageArgs();
    const MsgArgs& args = maybeArgs.hasValue() ? maybeArgs.value() : kNoArgs;
    std::string failureData = getFailureData(args);
    std::string source = extractOrigin(entry);
    auto oemData = extractOemData(entry);

    std::optional<sdbusplus::common::xyz::openbmc_project::logging::extension::cper::Processed::properties_t> maybeCper;

    if (oemData.has_value())
    {
        using CPERProcessed = sdbusplus::common::xyz::openbmc_project::logging::extension::cper::Processed;
        using CPERTypes = sdbusplus::common::xyz::openbmc_project::logging::cper::Types;

        CPERProcessed::properties_t cper{};
        cper.diagnostic_data_type = CPERTypes::ContentType::CPER;
        cper.oem = {
            {oemData->vendor , oemData->jsonData},
        };
        maybeCper = std::move(cper);
    }

    auto commitWithOem = [&maybeCper](auto&& error)
    {
        if (maybeCper.has_value())
        {
            lg2::commit(std::move(error).extend(*maybeCper));
        }
        else
        {
            lg2::commit(std::move(error));
        }
    };

    // Determine the mapped BMC event based on the Severity.
    auto severity = entry.getSeverity().hasValue()
                        ? entry.getSeverity().value()
                        : EventSeverity::Critical;

    if (severity == EventSeverity::Critical)
    {
        commitWithOem(ThermalError::DeviceOverOperatingTemperatureFault(
            "DEVICE", source,
            "FAILURE_DATA", failureData));
    }
    else
    {
        commitWithOem(ThermalError::DeviceOverOperatingTemperature(
            "DEVICE", source,
            "FAILURE_DATA", failureData));
    }
}

} // namespace redfish_client::core
