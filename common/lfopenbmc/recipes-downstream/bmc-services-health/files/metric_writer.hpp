#pragma once

#include "checks.hpp"

#include <filesystem>
#include <string>

namespace services_health
{

class BmcOemMetricWriter : public MetricWriter
{
  public:
    explicit BmcOemMetricWriter(std::filesystem::path executable);
    bool set(const std::string& name, uint64_t value,
             const std::string& unit) override;

  private:
    std::filesystem::path executable;
};

} // namespace services_health
