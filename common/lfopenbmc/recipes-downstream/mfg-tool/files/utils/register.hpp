#pragma once

#include <CLI/CLI.hpp>
#include <sdbusplus/async.hpp>

#include <exception>
#include <functional>
#include <iostream>
#include <memory>

namespace mfgtool
{

template <typename T>
using command_t = std::unique_ptr<T>;

#define MFGTOOL_REGISTER(type, ...)                                            \
    static auto command_registration_##type =                                  \
        mfgtool::details::register_command<type>(__VA_ARGS__)

template <typename T>
void init_callback(CLI::App*, T&);

namespace details
{
void register_command(std::function<void(CLI::App&)>);

template <typename T>
concept has_init_with_app = requires(T& t, CLI::App& app) { t.init(app); };

template <typename T, typename... Args>
concept has_valid_constructor = requires(Args... args) { T(args...); };

template <typename T, typename... Args>
auto register_command(Args... args) -> command_t<T>
    requires has_init_with_app<T> && has_valid_constructor<T, Args...>
{
    auto c = std::make_unique<T>(args...);

    details::register_command([command = c.get()](auto& app) {
        command->init(app);
    });

    return c;
}

template <typename T>
concept has_run = requires(T& t) { t.run(); };

template <typename T>
concept has_async_run =
    requires(T& t, sdbusplus::async::context& ctx) { ctx.spawn(t.run(ctx)); };

/* Run an async command, turning an escaped exception (e.g. a D-Bus NoReply)
 * into an error message and a failure flag instead of std::terminate, and
 * always stopping the context so `ctx.run()` returns.
 */
template <typename T>
auto run_guarded(sdbusplus::async::context& ctx, T& t, bool& failed)
    -> sdbusplus::async::task<>
{
    try
    {
        co_await t.run(ctx);
    }
    catch (const std::exception& e)
    {
        std::cerr << "mfg-tool: " << e.what() << std::endl;
        failed = true;
    }
    ctx.request_stop();
}

} // namespace details

template <typename T>
void init_callback(CLI::App*, T&)
{
    static_assert(false, "T has neither static or async run function.");
}

template <typename T>
void init_callback(CLI::App* cmd, T& t)
    requires details::has_run<T>
{
    cmd->callback([&]() {
        try
        {
            t.run();
        }
        catch (const CLI::Error&)
        {
            throw;
        }
        catch (const std::exception& e)
        {
            std::cerr << "mfg-tool: " << e.what() << std::endl;
            throw CLI::RuntimeError(1);
        }
    });
}

template <typename T>
void init_callback(CLI::App* cmd, T& t)
    requires details::has_async_run<T>
{
    cmd->callback([&]() {
        bool failed = false;
        {
            sdbusplus::async::context ctx;
            ctx.spawn(details::run_guarded(ctx, t, failed));
            ctx.run();
        }
        if (failed)
        {
            throw CLI::RuntimeError(1);
        }
    });
}

void init_commands(CLI::App&);

} // namespace mfgtool
