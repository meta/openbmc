#include <phosphor-logging/commit.hpp>
#include <phosphor-logging/lg2.hpp>
#include <redfish_client/core/instinct_gpu_mapper.hpp>
#include <redfish_client/core/log_entry_mapper_utils.hpp>
#include <xyz/openbmc_project/State/Accelerator/event.hpp>
#include "redfish-binding/LogEntry_EventSeverity.hpp"
#include <xyz/openbmc_project/Logging/Extension/CPER/Processed/common.hpp>

#include <string>
#include <vector>

namespace redfish_client::core
{

namespace
{
using MsgArgs = std::vector<std::string>;
using EventSeverity = redfish_binding::LogEntry::EventSeverity;
namespace GPUError = sdbusplus::error::xyz::openbmc_project::state::Accelerator;

std::string_view getSuffix(const std::string& messageId)
{
    std::string_view id = messageId;
    auto pos = id.find_last_of('.');
    return pos == std::string_view::npos ? id : id.substr(pos + 1);
}

std::string getFailureType(std::string_view suffix, const MsgArgs& args)
{
    auto getArg = [&args](size_t index) -> std::string_view {
        if (index < args.size())
        {
            return args[index];
        }
        return "unknown";
    };

    if (suffix == "ResourceErrorsDetected")
    {
        return std::format("The resource property {} has detected errors of type '{}'.",
                           getArg(0), getArg(1));
    }
    else if (suffix == "ResourceSelfTestFailed")
    {
        return std::format("A self-test has failed. The following message was returned: '{}'.",
                           getArg(0));
    }
    else if (suffix == "ResourceStatusChangedCritical" ||
             suffix == "ResourceStatusChangedWarning")
    {
        return std::format("The health of resource '{}' has changed to {}.",
                           getArg(0), getArg(1));
    }
    else
    {
        lg2::warning("InstinctGpuMapper: unhandled suffix '{SUFFIX}'",
                     "SUFFIX", suffix);
        return "unknown";
    }
}

template <typename T>
T makeGPUError(const auto& associationPath, const auto& failure_type)
{
    return T("SOURCE", sdbusplus::object_path(associationPath),
             "FAILURE_TYPE", failure_type);
}
} // anonymous namespace

bool InstinctGpuMapper::canHandle(
    redfish_binding::LogEntry::LogEntry& entry) const
{
    auto& maybeMessageId = entry.getMessageId();
    bool isResourceEvent =
        maybeMessageId.hasValue() &&
        maybeMessageId.value().find("ResourceEvent") !=
            std::string::npos;

    if (!isResourceEvent)
    {
        return false;
    }

    std::string origin = extractOrigin(entry);
    if (origin.find("EAM") == std::string::npos)
    {
        return false;
    }

    auto& maybeMessageArgs = entry.getMessageArgs();
    return maybeMessageArgs.hasValue() && !maybeMessageArgs.value().empty();
}

void InstinctGpuMapper::map(redfish_binding::LogEntry::LogEntry& entry)
{
    auto oemData = extractOemData(entry);
    auto severity = entry.getSeverity().hasValue()
                        ? entry.getSeverity().value()
                        : EventSeverity::Critical;

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

    static const MsgArgs kNoArgs;
    auto& maybeArgs = entry.getMessageArgs();
    const MsgArgs& args = maybeArgs.hasValue() ? maybeArgs.value() : kNoArgs;
    auto suffix = getSuffix(entry.getMessageId().value());
    auto failure_type = getFailureType(suffix, args);

    if (severity == EventSeverity::Critical) {
        commitWithOem(makeGPUError<GPUError::AcceleratorCritical>(associationPath, failure_type));
    } else {
        commitWithOem(makeGPUError<GPUError::AcceleratorFailed>(associationPath, failure_type));
    }
}

} // namespace redfish_client::core
