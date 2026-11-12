#include <phosphor-logging/commit.hpp>
#include <phosphor-logging/lg2.hpp>
#include <redfish_client/core/instinct_power_supply_mapper.hpp>
#include <redfish_client/core/log_entry_mapper_utils.hpp>
#include <xyz/openbmc_project/State/PowerSupply/event.hpp>
#include "redfish-binding/LogEntry_EventSeverity.hpp"
#include <xyz/openbmc_project/Logging/Extension/CPER/Processed/common.hpp>

#include <format>
#include <string>
#include <vector>

namespace redfish_client::core
{

namespace
{
using MsgArgs = std::vector<std::string>;
using EventSeverity = redfish_binding::LogEntry::EventSeverity;
namespace PowerSupplyError = sdbusplus::error::xyz::openbmc_project::state::PowerSupply;

std::string_view getSuffix(const std::string& messageId)
{
    std::string_view id = messageId;
    auto pos = id.find_last_of('.');
    return pos == std::string_view::npos ? id : id.substr(pos + 1);
}

std::string getFailureData(std::string_view suffix, const MsgArgs& args)
{
    auto getArg = [&args](size_t index) -> std::string_view {
        if (index < args.size())
        {
            return args[index];
        }
        return "unknown";
    };

    if (suffix == "PowerSupplyFailed")
    {
        return std::format("Power supply '{}' has failed.", getArg(0));
    }
    else if (suffix == "PowerSupplyGroupCritical")
    {
        return std::format("Power supply group '{}' is in a critical state.",
                           getArg(0));
    }
    else if (suffix == "PowerSupplyGroupWarning")
    {
        return std::format("Power supply group '{}' is in a warning state.",
                           getArg(0));
    }
    else
    {
        lg2::warning("InstinctPowerSupplyMapper: unhandled suffix '{SUFFIX}'",
                     "SUFFIX", suffix);
        return std::string(suffix);
    }
}

} // anonymous namespace

bool InstinctPowerSupplyMapper::canHandle(
    redfish_binding::LogEntry::LogEntry& entry) const
{
    auto& maybeMessageId = entry.getMessageId();
    if (!maybeMessageId.hasValue())
    {
        return false;
    }

    const auto& msgId = maybeMessageId.value();
    if (msgId.find("Power") == std::string::npos)
    {
        return false;
    }

    auto suffix = getSuffix(msgId);
    return suffix == "PowerSupplyFailed"        ||
           suffix == "PowerSupplyGroupCritical" ||
           suffix == "PowerSupplyGroupWarning";
}

void InstinctPowerSupplyMapper::map(
    redfish_binding::LogEntry::LogEntry& entry)
{
    static const MsgArgs kNoArgs;
    auto& maybeArgs = entry.getMessageArgs();
    const MsgArgs& args = maybeArgs.hasValue() ? maybeArgs.value() : kNoArgs;
    auto suffix = getSuffix(entry.getMessageId().value());
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
            {oemData->vendor, oemData->jsonData},
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

    if (suffix == "PowerSupplyFailed")
    {
        commitWithOem(PowerSupplyError::PowerSupplyFailed(
            "POWER_SUPPLY_NAME", sdbusplus::object_path(source)));
    }
    else if (suffix == "PowerSupplyGroupCritical")
    {
        commitWithOem(PowerSupplyError::PowerSupplyGroupCritical(
            "POWER_SUPPLY_GROUP", sdbusplus::object_path(source)));
    }
    else if (suffix == "PowerSupplyGroupWarning")
    {
        commitWithOem(PowerSupplyError::PowerSupplyGroupWarning(
            "POWER_SUPPLY_GROUP", sdbusplus::object_path(source)));
    }
    else
    {
        lg2::warning(
            "InstinctPowerSupplyMapper: unhandled suffix '{SUFFIX}'",
            "SUFFIX", suffix);
    }
}

} // namespace redfish_client::core