#include "systemd_dbus.hpp"

#include <systemd/sd-bus.h>

#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/message.hpp>

#include <algorithm>
#include <exception>
#include <iterator>
#include <tuple>
#include <variant>

namespace services_health
{

namespace
{

constexpr auto systemdService = "org.freedesktop.systemd1";
constexpr auto managerPath = "/org/freedesktop/systemd1";
constexpr auto managerInterface = "org.freedesktop.systemd1.Manager";
constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";

// ListUnitsByPatterns returns a(ssssssouso): id, description, load state,
// active state, sub state, following, unit path, job id, job type, job path.
using ListUnitsEntry =
    std::tuple<std::string, std::string, std::string, std::string, std::string,
               std::string, sdbusplus::object_path, uint32_t, std::string,
               sdbusplus::object_path>;

// sdbusplus default-constructs a variant whose type doesn't match instead of
// failing, which would read as an NRestarts reset or as "not starting".
bool variantHolds(sdbusplus::message_t& reply, const char* contents)
{
    return sd_bus_message_verify_type(reply.get(), SD_BUS_TYPE_VARIANT,
                                      contents) > 0;
}

} // namespace

SystemdDbus::SystemdDbus(sdbusplus::bus_t& bus) : bus(bus) {}

std::optional<std::vector<Unit>> SystemdDbus::listServices(
    const std::vector<std::string>& states)
{
    try
    {
        auto method =
            bus.new_method_call(systemdService, managerPath, managerInterface,
                                "ListUnitsByPatterns");
        method.append(states, std::vector<std::string>{"*.service"});
        auto reply = bus.call(method);
        std::vector<ListUnitsEntry> entries;
        reply.read(entries);
        std::vector<Unit> units;
        units.reserve(entries.size());
        std::transform(entries.begin(), entries.end(),
                       std::back_inserter(units), [](const ListUnitsEntry& e) {
                           return Unit{std::get<0>(e), std::get<2>(e),
                                       std::get<3>(e), std::get<6>(e).str};
                       });
        return units;
    }
    catch (const std::exception& e)
    {
        lg2::error("ListUnitsByPatterns failed: {ERROR}", "ERROR", e.what());
        return std::nullopt;
    }
}

std::optional<uint32_t> SystemdDbus::nRestarts(const Unit& unit)
{
    try
    {
        auto method =
            bus.new_method_call(systemdService, unit.objectPath.c_str(),
                                propertiesInterface, "Get");
        method.append("org.freedesktop.systemd1.Service", "NRestarts");
        auto reply = bus.call(method);
        if (!variantHolds(reply, "u"))
        {
            lg2::error("NRestarts of {UNIT} is not a uint32", "UNIT",
                       unit.name);
            return std::nullopt;
        }
        std::variant<uint32_t> value;
        reply.read(value);
        return std::get<uint32_t>(value);
    }
    catch (const std::exception& e)
    {
        // A unit unloaded since the list call usually reads as a stub with
        // NRestarts 0, not as an error, which is harmless; this catches real
        // D-Bus errors.
        lg2::info("NRestarts of {UNIT} unreadable: {ERROR}", "UNIT", unit.name,
                  "ERROR", e.what());
        return std::nullopt;
    }
}

std::optional<std::string> SystemdDbus::systemState()
{
    try
    {
        auto method = bus.new_method_call(systemdService, managerPath,
                                          propertiesInterface, "Get");
        method.append(managerInterface, "SystemState");
        auto reply = bus.call(method);
        if (!variantHolds(reply, "s"))
        {
            lg2::error("SystemState is not a string");
            return std::nullopt;
        }
        std::variant<std::string> value;
        reply.read(value);
        return std::get<std::string>(value);
    }
    catch (const std::exception& e)
    {
        lg2::error("SystemState read failed: {ERROR}", "ERROR", e.what());
        return std::nullopt;
    }
}

} // namespace services_health
