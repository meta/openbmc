#include "metric_writer.hpp"

#include <spawn.h>
#include <sys/wait.h>

#include <phosphor-logging/lg2.hpp>

#include <array>
#include <cerrno>
#include <cstring>
#include <utility>

extern char** environ;

namespace services_health
{

BmcOemMetricWriter::BmcOemMetricWriter(std::filesystem::path executable) :
    executable(std::move(executable))
{}

bool BmcOemMetricWriter::set(const std::string& name, uint64_t value,
                             const std::string& unit)
{
    const std::string path = executable.string();
    const std::string text = std::to_string(value);
    // posix_spawn takes char* const[] but never writes through it.
    std::array<char*, 6> argv{
        const_cast<char*>(path.c_str()), const_cast<char*>("set"),
        const_cast<char*>(name.c_str()), const_cast<char*>(text.c_str()),
        const_cast<char*>(unit.c_str()), nullptr};
    pid_t pid = 0;
    if (int rc = posix_spawn(&pid, path.c_str(), nullptr, nullptr, argv.data(),
                             environ);
        rc != 0)
    {
        lg2::error("Cannot run {PATH}: {ERROR}", "PATH", path, "ERROR",
                   std::strerror(rc));
        return false;
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0)
    {
        if (errno != EINTR)
        {
            lg2::error("waitpid for {PATH} failed: {ERROR}", "PATH", path,
                       "ERROR", std::strerror(errno));
            return false;
        }
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
    {
        return true;
    }
    if (WIFSIGNALED(status))
    {
        lg2::error(
            "{PATH} set {METRIC} {VALUE} {UNIT} killed by signal {SIGNAL}",
            "PATH", path, "METRIC", name, "VALUE", value, "UNIT", unit,
            "SIGNAL", WTERMSIG(status));
    }
    else
    {
        lg2::error("{PATH} set {METRIC} {VALUE} {UNIT} exited with {EXIT}",
                   "PATH", path, "METRIC", name, "VALUE", value, "UNIT", unit,
                   "EXIT", WEXITSTATUS(status));
    }
    return false;
}

} // namespace services_health
