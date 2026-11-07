#include <phosphor-logging/commit.hpp>
#include <phosphor-logging/lg2.hpp>
#include <redfish_client/core/instinct_cper_mapper.hpp>
#include <redfish_client/core/log_entry_mapper_utils.hpp>
#include <sdbusplus/async/timer.hpp>
#include <xyz/openbmc_project/Logging/CPER/Types/common.hpp>
#include <xyz/openbmc_project/Logging/Extension/CPER/Processed/common.hpp>
#include <xyz/openbmc_project/Logging/Extension/CPER/Raw/common.hpp>
#include <xyz/openbmc_project/State/CPER/event.hpp>

#include "redfish-binding/LogEntry_LogDiagnosticDataTypes.hpp"

namespace redfish_client::core
{

namespace
{
namespace CPER = sdbusplus::error::xyz::openbmc_project::state::CPER;
namespace CPERExtension =
    sdbusplus::common::xyz::openbmc_project::logging::extension::cper;
namespace CPERTypes = sdbusplus::common::xyz::openbmc_project::logging::cper;

constexpr auto unknownSource = "Unknown Source";

std::string getSource(auto& maybeLinks)
{
    if (!maybeLinks.hasValue())
    {
        return unknownSource;
    }

    auto& origin = maybeLinks.value().getOriginOfCondition();

    if (origin.hasValue() && origin.value().getOdataId().hasValue())
    {
        return origin.value().getOdataId().value();
    }

    return unknownSource;
}

std::string getNotificationType(auto& maybeCPER)
{
    if (!maybeCPER.hasValue())
    {
        return {};
    }

    if (!maybeCPER.value().getNotificationType().hasValue())
    {
        return {};
    }

    return maybeCPER.value().getNotificationType().value();
}

std::string getSectionType(auto& maybeCPER)
{
    if (!maybeCPER.hasValue())
    {
        return {};
    }

    if (!maybeCPER.value().getSectionType().hasValue())
    {
        return {};
    }

    return maybeCPER.value().getSectionType().value();
}

nlohmann::json getCperOemVendorJson(auto& maybeCPER)
{
    if (!maybeCPER.hasValue())
    {
        return nullptr;
    }

    if (!maybeCPER.value().getOem().hasValue())
    {
        return nullptr;
    }

    nlohmann::json cperOem = nlohmann::json(maybeCPER.value().getOem().value());

    if (!cperOem.is_object() || cperOem.empty())
    {
        return nullptr;
    }

    return cperOem;
}

// Returns true if the CPER.Oem vendor object already contains
// AMDFieldIdentifiers, which is the signal for old-format payloads where
// AFID data was embedded directly in CPER.Oem rather than the top-level Oem.
bool cperOemHasAfidData(const nlohmann::json& cperOemJson)
{
    if (!cperOemJson.is_object() || cperOemJson.empty())
    {
        return false;
    }

    const auto& vendorObj = cperOemJson.begin().value();

    if (!vendorObj.is_object())
    {
        return false;
    }

    return vendorObj.contains("AMDFieldIdentifiers");
}

// Merges top-level Oem into CPER.Oem for new-format payloads where AFID data
// lives at the top-level Oem and decoded CPER data lives in CPER.Oem.
//
// Merge rules:
//   - Top-level Oem values take priority on key conflicts.
//   - @odata.type is taken from top-level Oem.
//   - If vendor keys differ, a warning is emitted and the top-level Oem
//     values are merged into the CPER.Oem vendor regardless, since both
//     are always AMD on this platform.
//
// Returns {vendor, mergedJsonString} or empty pair if merge is not possible.
std::pair<std::string, std::string> mergeTopLevelOemIntoCperOem(
    const nlohmann::json& cperOemJson, auto& entry)
{
    auto& maybeTopOem = entry.getOem();
    if (!maybeTopOem.hasValue())
    {
        return {};
    }

    const nlohmann::json topOemJson = nlohmann::json(maybeTopOem.value());

    if (!topOemJson.is_object() || topOemJson.empty())
    {
        return {};
    }

    const auto cperVendorEntry = cperOemJson.begin();
    const auto topVendorEntry = topOemJson.begin();

    const std::string cperVendor = cperVendorEntry.key();
    const std::string topVendor = topVendorEntry.key();

    if (cperVendor != topVendor)
    {
        lg2::warning(
            "Top-level Oem vendor '{TOP_VENDOR}' differs from CPER.Oem "
            "vendor '{CPER_VENDOR}'. Merging top-level values into "
            "CPER.Oem vendor '{CPER_VENDOR}' anyway.",
            "TOP_VENDOR", topVendor.c_str(),
            "CPER_VENDOR", cperVendor.c_str());
    }

    nlohmann::json merged = topVendorEntry.value();

    if (!merged.is_object())
    {
        merged = nlohmann::json::object();
    }

    const nlohmann::json& cperVendorObj = cperVendorEntry.value();
    if (cperVendorObj.is_object())
    {
        for (const auto& [key, val] : cperVendorObj.items())
        {
            if (!merged.contains(key))
            {
                merged[key] = val;
            }
        }
    }

    return {cperVendor, merged.dump()};
}

// Entry point for OEM data extraction. Handles both old and new AMC formats
// and returns a {vendor, jsonString} pair ready for the commit phase.
//
// Old format: CPER.Oem.AMD contains both AMDFieldIdentifiers and DecodedCPER.
//             Use CPER.Oem as-is; top-level Oem is redundant.
//
// New format: CPER.Oem.AMD contains only DecodedCPER; AMDFieldIdentifiers
//             lives in top-level Oem.AMD. Merge top-level into CPER.Oem.
std::pair<std::string, std::string> getOemVendorAndData(
    auto& maybeCPER, auto& entry)
{
    const nlohmann::json cperOemJson = getCperOemVendorJson(maybeCPER);

    if (cperOemJson.is_null())
    {
        return {};
    }

    // Old format: AFID already embedded in CPER.Oem — use as-is.
    if (cperOemHasAfidData(cperOemJson))
    {
        const auto vendorEntry = cperOemJson.begin();
        return {vendorEntry.key(), vendorEntry.value().dump()};
    }

    // New format: merge top-level Oem (AFID) into CPER.Oem (DecodedCPER).
    auto merged = mergeTopLevelOemIntoCperOem(cperOemJson, entry);
    if (!merged.first.empty())
    {
        return merged;
    }

    // Top-level Oem absent or empty in new format — use CPER.Oem as-is
    // so we do not silently drop DecodedCPER data.
    lg2::warning(
        "New-format CPER entry has no top-level Oem to merge. "
        "Falling back to CPER.Oem only.");
    const auto vendorEntry = cperOemJson.begin();
    return {vendorEntry.key(), vendorEntry.value().dump()};
}

} // anonymous namespace

bool InstinctCperMapper::canHandle(
    redfish_binding::LogEntry::LogEntry& entry) const
{
    return entry.getCPER().hasValue();
}

void InstinctCperMapper::map(redfish_binding::LogEntry::LogEntry& entry)
{
    auto& maybeCPER = entry.getCPER();

    if (!maybeCPER.hasValue())
    {
        return;
    }

    CPERData data{};

    data.host = host;

    auto& maybeLinks = entry.getLinks();
    data.source = getSource(maybeLinks);
    data.notificationType = getNotificationType(maybeCPER);
    data.sectionType = getSectionType(maybeCPER);

    // Only CPER and CPERSection of the Redfish enum describe CPER content.
    // Absent or anything else means a whole record, which is what an entry
    // carrying a notification GUID is.
    if (auto& maybeType = entry.getDiagnosticDataType();
        maybeType.hasValue() &&
        maybeType.value() ==
            redfish_binding::LogEntry::LogDiagnosticDataTypes::CPERSection)
    {
        data.diagnosticDataType = CPERTypes::Types::ContentType::CPERSection;
    }

    // Vendor OEM data rides the CPER object -- that is where the AMC puts it,
    // and `CPER.Oem` is what the Redfish schema defines. Fall back to the
    // LogEntry-level `Oem` for producers that attach it there instead.
    //
    // Both are optional. `extractOemData` returns nullopt when the LogEntry
    // has no `Oem`, and dereferencing it unconditionally aborted the whole
    // daemon on any CPER entry that did not carry one.
    if (auto [vendor, oemJson] = getOemVendorAndData(maybeCPER, entry);
        !vendor.empty())
    {
        data.vendor = std::move(vendor);
        data.oemData = std::move(oemJson);
    }
    else if (auto oemData = extractOemData(entry); oemData.has_value())
    {
        data.vendor = std::move(oemData->vendor);
        data.oemData = std::move(oemData->jsonData);
    }

    auto& maybeURI = entry.getAdditionalDataURI();
    if (maybeURI.hasValue())
    {
        data.additionalDataURI = maybeURI.value();
    }

    // Hand off all processing and committing to async task
    ctx.spawn(commitCPER(ctx, std::move(data)));
}

auto InstinctCperMapper::commitCPER(sdbusplus::async::context& ctx,
                                    CPERData data)
    -> sdbusplus::async::task<void>
{
    std::optional<std::vector<uint8_t>> maybeBinary;
    if (!data.additionalDataURI.empty())
    {
        constexpr size_t maxRetries = 3;
        constexpr size_t retryIntervalMs = 1000;

        const std::string fullUrl =
            std::format("http://{}{}", data.host, data.additionalDataURI);

        auto httpHandle = std::make_unique<AsyncHttpHandle>(fullUrl);

        for (size_t i = 0; i < maxRetries; ++i)
        {
            if (i != 0)
            {
                co_await sdbusplus::async::sleep_for(
                    ctx, std::chrono::milliseconds(retryIntervalMs));
            }

            auto response = co_await httpHandle->tryGet(ctx);

            if (!response.has_value())
            {
                continue;
            }

            if (response->code != 200)
            {
                continue;
            }

            maybeBinary = std::vector<uint8_t>(response->body.begin(),
                                               response->body.end());
            break;
        }

        if (!maybeBinary.has_value())
        {
            lg2::warning("Exhausted retries fetching CPER binary from {URI}",
                         "URI", fullUrl.c_str());
        }
    }

    // Build CPERProcessed properties
    CPERExtension::Processed::properties_t cperProperties{};
    cperProperties.diagnostic_data_type = data.diagnosticDataType;

    if (!data.notificationType.empty())
    {
        cperProperties.notification_type = data.notificationType;
    }

    if (!data.sectionType.empty())
    {
        cperProperties.section_type = data.sectionType;
    }

    if (!data.vendor.empty() && !data.oemData.empty())
    {
        cperProperties.oem = {
            {data.vendor, data.oemData},
        };
    }

    // Build and extend CPER event
    auto cperEvent = CPER::GenericCPERFault("SOURCE", data.source, "CPER", "");
    cperEvent.extend(cperProperties);

    // Attach raw binary if successfully fetched
    if (maybeBinary.has_value())
    {
        CPERExtension::Raw::properties_t rawProperties{};
        rawProperties.data = std::move(maybeBinary.value());
        cperEvent.extend(rawProperties);
    }

    auto eventId = lg2::commit(std::move(cperEvent));
    co_return;
}

} // namespace redfish_client::core