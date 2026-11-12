#pragma once

#include <redfish_client/core/log_entry_mapper.hpp>

namespace redfish_client::core
{

class InstinctThermalMapper : public LogEntryMapper
{
  public:
    InstinctThermalMapper() = default;
    ~InstinctThermalMapper() override = default;

    bool canHandle(redfish_binding::LogEntry::LogEntry& entry) const override;
    void map(redfish_binding::LogEntry::LogEntry& entry) override;
};

} // namespace redfish_client::core