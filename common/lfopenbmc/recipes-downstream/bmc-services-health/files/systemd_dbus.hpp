#pragma once

#include "checks.hpp"

#include <sdbusplus/bus.hpp>

namespace services_health
{

class SystemdDbus : public SystemdApi
{
  public:
    explicit SystemdDbus(sdbusplus::bus_t& bus);
    std::optional<std::vector<Unit>> listServices(
        const std::vector<std::string>& states) override;
    std::optional<uint32_t> nRestarts(const Unit& unit) override;
    std::optional<std::string> systemState() override;

  private:
    sdbusplus::bus_t& bus;
};

} // namespace services_health
