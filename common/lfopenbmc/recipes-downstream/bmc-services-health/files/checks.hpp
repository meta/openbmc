#pragma once

#include "services_health.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace services_health
{

// cppcheck reads this header alone, so it can't see that checks.cpp and
// systemd_dbus.cpp read every field.
struct Unit
{
    // cppcheck-suppress unusedStructMember
    std::string name;
    // cppcheck-suppress unusedStructMember
    std::string loadState;
    // cppcheck-suppress unusedStructMember
    std::string activeState;
    // cppcheck-suppress unusedStructMember
    std::string objectPath;
};

// The checks only see this interface, so they are tested without D-Bus.
class SystemdApi
{
  public:
    virtual ~SystemdApi() = default;
    // Lists only *.service units whose load, active or sub state is in
    // states; empty states means any state (systemd's ListUnitsByPatterns).
    virtual std::optional<std::vector<Unit>> listServices(
        const std::vector<std::string>& states) = 0;
    virtual std::optional<uint32_t> nRestarts(const Unit& unit) = 0;
    virtual std::optional<std::string> systemState() = 0;
};

class MetricWriter
{
  public:
    virtual ~MetricWriter() = default;
    // unit is a bmc-oem-metric unit. adhoc-sensor republishes the D-Bus
    // object when a metric's unit changes, so each metric keeps one unit.
    virtual bool set(const std::string& name, uint64_t value,
                     const std::string& unit) = 0;
};

struct Check
{
    // cppcheck-suppress unusedStructMember
    std::string metric;
    std::function<std::optional<uint64_t>()> run;
};

inline constexpr const char* heartbeatMetric = "services_health_heartbeat";
inline constexpr const char* countUnit = "count";
inline constexpr const char* secondsUnit = "seconds";

std::optional<uint64_t> failedServices(SystemdApi& systemd,
                                       const ExclusionList& exclusions);
std::optional<uint64_t> failedServicesExcluded(SystemdApi& systemd,
                                               const ExclusionList& exclusions);
std::optional<uint64_t> systemdStarting(SystemdApi& systemd);
// outOfTime is asked before each NRestarts read; when it says yes, the check
// fails without touching the state file, so the run stays inside the
// watchdog budget (main.cpp).
std::optional<uint64_t> serviceRestartsTotal(
    SystemdApi& systemd, const ExclusionList& exclusions,
    const std::filesystem::path& statePath,
    const std::function<bool()>& outOfTime);

bool runOnce(std::span<const Check> checks, MetricWriter& writer,
             const std::function<uint64_t()>& uptimeSeconds);

} // namespace services_health
