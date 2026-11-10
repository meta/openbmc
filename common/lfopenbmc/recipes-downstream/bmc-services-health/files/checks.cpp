#include "checks.hpp"

#include <phosphor-logging/lg2.hpp>

#include <system_error>
#include <utility>

namespace services_health
{

namespace
{

std::optional<std::pair<uint64_t, uint64_t>> countFailed(
    SystemdApi& systemd, const ExclusionList& exclusions)
{
    auto units = systemd.listServices({"failed"});
    if (!units)
    {
        return std::nullopt;
    }
    uint64_t counted = 0;
    uint64_t excluded = 0;
    for (const auto& unit : *units)
    {
        if (unit.loadState != "loaded" || unit.activeState != "failed")
        {
            continue;
        }
        if (exclusions.matches(unit.name))
        {
            ++excluded;
        }
        else
        {
            ++counted;
        }
    }
    return std::pair{counted, excluded};
}

} // namespace

std::optional<uint64_t> failedServices(SystemdApi& systemd,
                                       const ExclusionList& exclusions)
{
    auto counts = countFailed(systemd, exclusions);
    if (!counts)
    {
        return std::nullopt;
    }
    return counts->first;
}

std::optional<uint64_t> failedServicesExcluded(SystemdApi& systemd,
                                               const ExclusionList& exclusions)
{
    auto counts = countFailed(systemd, exclusions);
    if (!counts)
    {
        return std::nullopt;
    }
    return counts->second;
}

std::optional<uint64_t> systemdStarting(SystemdApi& systemd)
{
    auto state = systemd.systemState();
    if (!state)
    {
        return std::nullopt;
    }
    return isStarting(*state) ? 1U : 0U;
}

std::optional<RestartCounts> readRestartCounts(
    SystemdApi& systemd, const std::vector<Unit>& units,
    const std::function<bool()>& outOfTime)
{
    RestartCounts current;
    for (const auto& unit : units)
    {
        if (unit.loadState != "loaded")
        {
            continue;
        }
        if (outOfTime())
        {
            // A partial list would make the units not read look gone and
            // then new (their lifetime count added), so the run is dropped.
            lg2::error("NRestarts reads ran out of time after {COUNT} units",
                       "COUNT", current.size());
            return std::nullopt;
        }
        current[unit.name] = systemd.nRestarts(unit);
    }
    return current;
}

std::optional<RestartState> previousState(
    const std::filesystem::path& statePath)
{
    auto text = readFile(statePath);
    if (!text)
    {
        // A missing file is normal after boot; one that exists but can't be
        // read loses restarts, so it is worth a line in the journal.
        std::error_code ec;
        if (std::filesystem::exists(statePath, ec) || ec)
        {
            lg2::warning("Cannot read {PATH}; rebaselining restart counts",
                         "PATH", statePath.string());
        }
        return std::nullopt;
    }
    auto previous = parseState(*text);
    if (!previous)
    {
        lg2::warning("Corrupt {PATH}; rebaselining restart counts", "PATH",
                     statePath.string());
    }
    return previous;
}

std::optional<uint64_t> serviceRestartsTotal(
    SystemdApi& systemd, const ExclusionList& exclusions,
    const std::filesystem::path& statePath,
    const std::function<bool()>& outOfTime)
{
    auto state = systemd.systemState();
    if (!state)
    {
        return std::nullopt;
    }
    auto units = systemd.listServices({});
    if (!units)
    {
        return std::nullopt;
    }

    auto current = readRestartCounts(systemd, *units, outOfTime);
    if (!current)
    {
        return std::nullopt;
    }

    auto next = accountRestarts(previousState(statePath), *current, exclusions,
                                isStarting(*state));
    if (!writeFileAtomic(statePath, serializeState(next)))
    {
        lg2::error("Cannot write {PATH}", "PATH", statePath.string());
        return std::nullopt;
    }
    return next.total;
}

bool runOnce(std::span<const Check> checks, MetricWriter& writer,
             const std::function<uint64_t()>& uptimeSeconds)
{
    bool allSucceeded = true;
    for (const auto& check : checks)
    {
        auto value = check.run();
        if (!value)
        {
            // Never write a default: the previous value stays, and the stale
            // heartbeat is what reports the failure.
            lg2::error("Check {METRIC} failed; keeping its last value",
                       "METRIC", check.metric);
            allSucceeded = false;
            continue;
        }
        if (!writer.set(check.metric, *value, countUnit))
        {
            allSucceeded = false;
        }
    }
    // Last, and only after a complete run, so a frozen heartbeat means the
    // producer stopped, hung or has a check that keeps failing.
    return allSucceeded &&
           writer.set(heartbeatMetric, uptimeSeconds(), secondsUnit);
}

} // namespace services_health
