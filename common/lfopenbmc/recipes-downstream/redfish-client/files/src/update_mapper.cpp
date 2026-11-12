#include <redfish_client/core/update_mapper.hpp>

#include <phosphor-logging/commit.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/message.hpp>
#include <xyz/openbmc_project/Software/Update/event.hpp>
#include <redfish_client/core/log_entry_mapper_utils.hpp>
#include <xyz/openbmc_project/Logging/Extension/CPER/Processed/common.hpp>


#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

PHOSPHOR_LOG2_USING;

namespace redfish_client::core
{

namespace
{

using MsgArgs = std::vector<std::string>;

namespace UpdateError = sdbusplus::error::xyz::openbmc_project::software::Update;
namespace UpdateEvent = sdbusplus::event::xyz::openbmc_project::software::Update;
using CommonHost = sdbusplus::common::xyz::openbmc_project::state::Host;

std::string argStr(const MsgArgs& args, size_t i)
{
    return i < args.size() ? args[i] : std::string{};
}

sdbusplus::object_path argPath(const MsgArgs& args, size_t i)
{
    return sdbusplus::object_path(argStr(args, i));
}

std::string_view getPrefix(const std::string& messageId)
{
    std::string_view id = messageId;
    auto pos = id.find('.');
    return pos == std::string_view::npos ? id : id.substr(0, pos);
}

std::string_view getSuffix(const std::string& messageId)
{
    std::string_view id = messageId;
    auto pos = id.find_last_of('.');
    return pos == std::string_view::npos ? id : id.substr(pos + 1);
}

template <typename T>
T makeImageTarget(const MsgArgs& args)
{
    return T("IMAGE_IDENTIFIER", argStr(args, 0),
             "TARGET_NAME", argPath(args, 1));
}

template <typename T>
T makeTargetImage(const MsgArgs& args)
{
    return T("TARGET_NAME", argPath(args, 0),
             "IMAGE_IDENTIFIER", argStr(args, 1));
}

std::optional<UpdateEvent::ResetRequired> makeResetRequired(const MsgArgs& args)
{
    auto transition = sdbusplus::message::convert_from_string<
        CommonHost::Transition>(argStr(args, 1));
    if (!transition)
    {
        return std::nullopt;
    }
    return UpdateEvent::ResetRequired("TARGET_NAME", argPath(args, 0),
                                      "RESET_TYPE", *transition);
}

} // anonymous namespace

bool UpdateMapper::canHandle(redfish_binding::LogEntry::LogEntry& entry) const
{
    auto& maybeMessageId = entry.getMessageId();
    if (!maybeMessageId.hasValue())
    {
        return false;
    }
    const auto& msgId = maybeMessageId.value();
    auto prefix = getPrefix(msgId);
    auto suffix = getSuffix(msgId);

    if (prefix == "Update")
    {
        return suffix == "VerificationFailed"      ||
               suffix == "TargetDetermined"        ||
               suffix == "UpdateSuccessful"        ||
               suffix == "ActivateFailed"          ||
               suffix == "TransferFailed"          ||
               suffix == "UpdateNotApplicable"     ||
               suffix == "InstallingOnComponent"   ||
               suffix == "TransferringToComponent" ||
               suffix == "VerifyingAtComponent";
    }
    if (prefix == "Base")
    {
        return suffix == "ResetRequired";
    }
    return false;
}

void UpdateMapper::map(redfish_binding::LogEntry::LogEntry& entry)
{
    auto suffix = getSuffix(entry.getMessageId().value());

    static const MsgArgs kNoArgs;
    auto& maybeArgs = entry.getMessageArgs();
    const MsgArgs& args = maybeArgs.hasValue() ? maybeArgs.value() : kNoArgs;

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

    if (suffix == "VerificationFailed")
    {
        commitWithOem(makeImageTarget<UpdateError::VerificationFailed>(args));
    }
    else if (suffix == "ActivateFailed")
    {
        commitWithOem(makeImageTarget<UpdateError::ActivateFailed>(args));
    }
    else if (suffix == "TransferFailed")
    {
        commitWithOem(makeImageTarget<UpdateError::TransferFailed>(args));
    }
    else if (suffix == "UpdateNotApplicable")
    {
        commitWithOem(makeImageTarget<UpdateError::UpdateNotApplicable>(args));
    }
    else if (suffix == "InstallingOnComponent")
    {
        commitWithOem(makeImageTarget<UpdateEvent::InstallingOnComponent>(args));
    }
    else if (suffix == "TransferringToComponent")
    {
        commitWithOem(
            makeImageTarget<UpdateEvent::TransferringToComponent>(args));
    }
    else if (suffix == "VerifyingAtComponent")
    {
        commitWithOem(makeImageTarget<UpdateEvent::VerifyingAtComponent>(args));
    }
    else if (suffix == "TargetDetermined")
    {
        commitWithOem(makeTargetImage<UpdateEvent::TargetDetermined>(args));
    }
    else if (suffix == "UpdateSuccessful")
    {
        commitWithOem(makeTargetImage<UpdateEvent::UpdateSuccessful>(args));
    }
    else if (suffix == "ResetRequired")
    {
        if (auto event = makeResetRequired(args))
        {
            commitWithOem(std::move(*event));
        }
        else
        {
            warning("UpdateMapper::map: invalid RESET_TYPE {VAL}", "VAL",
                    argStr(args, 1));
        }
    }
    else
    {
        warning("UpdateMapper::map: unhandled suffix {SUFFIX}", "SUFFIX",
                std::string(suffix));
    }
}

} // namespace redfish_client::core
