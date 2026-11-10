#include "checks.hpp"
#include "metric_writer.hpp"
#include "services_health.hpp"
#include "systemd_dbus.hpp"

#include <systemd/sd-daemon.h>
#include <time.h>

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <thread>
#include <vector>

namespace sh = services_health;

namespace
{

constexpr auto interval = std::chrono::seconds(60);

// Watchdog budget (WatchdogSec=180). The watchdog is pinged at the start and
// the end of every run, so the longest gap is max(sleep, one run). Worst case,
// with every D-Bus call hitting sd-bus's 25 s timeout: failed_services 25 s,
// failed_services_excluded 25 s, then SystemState and the unit list 25 s each
// (100 s). The NRestarts loop starts no read after restartsBudget, so it ends
// by 90 + 25 = 115 s. systemd_starting ends by 140 s, and the five
// bmc-oem-metric runs (tmpfs, well under 1 s each) by about 145 s. That
// leaves about 35 s. Each D-Bus check added later uses up 25 s of it, so
// redo this budget then (lower restartsBudget or raise WatchdogSec).
// bmc-oem-metric runs have no timeout: a hung child is left to the watchdog,
// which aborts the daemon, and the unit's default KillMode=control-group
// then kills the child too.
constexpr auto restartsBudget = std::chrono::seconds(90);

// tmpfs: survives a producer restart (watchdog), not a reboot, after which
// restart counts must be rebaselined anyway.
const std::filesystem::path statePath = "/run/bmc-services-health/state";

// The image default is read-only and follows firmware updates; /etc holds
// on-call additions only, so an edit on the BMC never hides a new default.
const std::vector<std::filesystem::path> exclusionFiles{
    "/usr/share/bmc-services-health/exclude",
    "/etc/bmc-services-health/exclude",
};

// The cost of the NRestarts reads grows with the unit count and with how busy
// systemd is, so run time is measured on real BMCs. Logged on the first run and
// hourly rather than every minute, because the BMC journal is small and in RAM.
// A slow run is always logged: one past restartsBudget stops the heartbeat.
void logRunTime(std::chrono::steady_clock::duration took, uint64_t run)
{
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(took).count();
    if (took > restartsBudget / 3)
    {
        lg2::warning("Run took {MS} ms, over a third of the {BUDGET} s "
                     "NRestarts budget",
                     "MS", ms, "BUDGET", restartsBudget.count());
    }
    else if (run % 60 == 0)
    {
        lg2::info("Run took {MS} ms", "MS", ms);
    }
}

uint64_t uptimeSeconds()
{
    timespec now{};
    clock_gettime(CLOCK_BOOTTIME, &now);
    return static_cast<uint64_t>(now.tv_sec);
}

} // namespace

int main()
{
    try
    {
        auto bus = sdbusplus::bus::new_default_system();
        sh::SystemdDbus systemd(bus);
        sh::BmcOemMetricWriter writer("/usr/bin/bmc-oem-metric");

        // A producer whose checks keep failing after a reboot then leaves a
        // frozen 0, which the heartbeat rule catches, instead of a missing key,
        // which it can't.
        if (!writer.set(sh::heartbeatMetric, 0, sh::secondsUnit))
        {
            lg2::error("Cannot reset {METRIC}", "METRIC", sh::heartbeatMetric);
        }
        // A hang before READY=1 is bounded by TimeoutStartSec, not the
        // watchdog, and Restart=always then retries.
        sd_notify(0, "READY=1");

        sh::ExclusionList exclusions;
        std::chrono::steady_clock::time_point runStart;
        uint64_t runs = 0;
        const auto outOfTime = [&] {
            return std::chrono::steady_clock::now() - runStart > restartsBudget;
        };
        const std::array checks{
            sh::Check{"failed_services",
                      [&] { return sh::failedServices(systemd, exclusions); }},
            sh::Check{"failed_services_excluded",
                      [&] {
                          return sh::failedServicesExcluded(systemd,
                                                            exclusions);
                      }},
            sh::Check{"service_restarts_total",
                      [&] {
                          return sh::serviceRestartsTotal(systemd, exclusions,
                                                          statePath, outOfTime);
                      }},
            sh::Check{"systemd_starting",
                      [&] { return sh::systemdStarting(systemd); }},
        };

        while (true)
        {
            sd_notify(0, "WATCHDOG=1");
            runStart = std::chrono::steady_clock::now();
            // Read every run so an on-BMC edit applies without a restart.
            exclusions = sh::loadExclusions(exclusionFiles);
            sh::runOnce(checks, writer, uptimeSeconds);
            logRunTime(std::chrono::steady_clock::now() - runStart, runs++);
            // sd-bus never reconnects; a new process gets a new connection,
            // and the restart state survives in /run.
            if (!bus.is_open())
            {
                lg2::error(
                    "System bus connection lost; exiting for systemd to restart");
                return EXIT_FAILURE;
            }
            sd_notify(0, "WATCHDOG=1");
            // Keeps the period at 60 s whatever the run took, so the next
            // start-of-run ping comes at most 60 s after this one.
            std::this_thread::sleep_until(runStart + interval);
        }
    }
    catch (const std::exception& e)
    {
        // An uncaught exception would abort, and systemd-coredump would write
        // a core to rwfs on every restart, e.g. while the system bus is down.
        lg2::error("bmc-services-health: {ERROR}", "ERROR", e.what());
        return EXIT_FAILURE;
    }
}
