#pragma once

#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <phosphor-logging/lg2.hpp>

#include <cerrno>
#include <functional>

namespace mfg_tool
{
PHOSPHOR_LOG2_USING;

namespace details
{
// Grace period between the timeout SIGTERM and a follow-up SIGKILL.
static constexpr unsigned kill_grace_seconds = 5;

static volatile sig_atomic_t child_pid = 0;
static volatile sig_atomic_t was_killed = 0;
static volatile sig_atomic_t forwarded_signal = 0;

inline void alarm_handler(int)
{
    if (child_pid <= 0)
    {
        return;
    }
    if (!was_killed)
    {
        was_killed = 1;
        kill(child_pid, SIGTERM);
        alarm(kill_grace_seconds);
    }
    else
    {
        kill(child_pid, SIGKILL);
    }
}

inline void forward_handler(int signum)
{
    forwarded_signal = signum;
    if (child_pid > 0)
    {
        kill(child_pid, signum);
    }
}

inline void install(int signum, void (*handler)(int))
{
    struct sigaction sa{};
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sigaction(signum, &sa, nullptr);
}

inline int exit_status(int status)
{
    if (WIFSIGNALED(status))
    {
        return 128 + WTERMSIG(status);
    }
    return WEXITSTATUS(status);
}
} // namespace details

/** Run `fn` in a forked child, killing and retrying it on timeout.
 *
 *  The child is tied to this process: it gets SIGKILL if we die, and
 *  SIGTERM/SIGINT/SIGHUP sent to us are forwarded to it.  This keeps an
 *  outer `timeout` wrapper (or a ^C) from orphaning a hung child that still
 *  holds the caller's stdout.
 */
inline int supervise(std::function<int()> fn, int timeout, int retries)
{
    using namespace details;

    const pid_t parent = getpid();
    for (auto signum : {SIGTERM, SIGINT, SIGHUP})
    {
        install(signum, forward_handler);
    }
    install(SIGALRM, alarm_handler);

    int es = 0;
    for (int attempt = 0; attempt <= retries; attempt++)
    {
        was_killed = 0;
        pid_t pid = fork();
        if (pid < 0)
        {
            error("Unable to fork: {ERRNO}", "ERRNO", errno);
            return 1;
        }
        if (pid == 0)
        {
            for (auto signum : {SIGTERM, SIGINT, SIGHUP, SIGALRM})
            {
                install(signum, SIG_DFL);
            }
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != parent)
            {
                _exit(137);
            }
            exit(fn());
        }

        child_pid = pid;
        if (forwarded_signal)
        {
            kill(pid, forwarded_signal);
        }
        alarm(timeout);

        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        {}

        alarm(0);
        child_pid = 0;
        es = exit_status(status);

        if (forwarded_signal)
        {
            return 128 + forwarded_signal;
        }
        if (was_killed)
        {
            error("Child process timed out after {TIMEOUT}s (attempt {ATTEMPT})",
                  "TIMEOUT", timeout, "ATTEMPT", attempt + 1);
            continue;
        }
        if (es == 0)
        {
            return es;
        }
    }
    // If the last attempt was killed by the timeout, then we
    // return 137 to mimic the returncode from the timeout binary
    if (was_killed)
    {
        exit(137);
    }
    return es;
}
} // namespace mfg_tool
