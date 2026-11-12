#pragma once

#include <redfish_client/core/log_entry_mapper.hpp>

namespace redfish_client::core
{

class InstinctAmcMapper : public LogEntryMapper
{
  public:
    InstinctAmcMapper(const std::string& associationPath) :
        associationPath(associationPath)
    {}

    bool canHandle(redfish_binding::LogEntry::LogEntry& entry) const override;

    void map(redfish_binding::LogEntry::LogEntry& entry) override;

  private:
    std::string associationPath;
};

} // namespace redfish_client::core
