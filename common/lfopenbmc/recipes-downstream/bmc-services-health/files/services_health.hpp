#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace services_health
{

class ExclusionList
{
  public:
    static ExclusionList parse(std::string_view text);
    void append(const ExclusionList& other);
    bool matches(const std::string& unit) const;
    const std::vector<std::string>& patterns() const
    {
        return globs;
    }

  private:
    std::vector<std::string> globs;
};

ExclusionList loadExclusions(const std::vector<std::filesystem::path>& paths);

// cppcheck reads this header alone, so it can't see that services_health.cpp
// and checks.cpp read every field.
struct RestartState
{
    uint64_t total = 0;
    // cppcheck-suppress unusedStructMember
    std::map<std::string, uint32_t> prev;
    // Units listed on the baseline run whose NRestarts was unreadable. They
    // are baselined once readable: treating them as new would publish their
    // lifetime count as restarts.
    // cppcheck-suppress unusedStructMember
    std::set<std::string> unknown;

    bool operator==(const RestartState&) const = default;
};

std::optional<RestartState> parseState(std::string_view text);
std::string serializeState(const RestartState& state);

// nullopt marks a unit whose NRestarts read failed: it keeps its previous
// count instead of failing the whole check.
using RestartCounts = std::map<std::string, std::optional<uint32_t>>;

RestartState accountRestarts(const std::optional<RestartState>& previous,
                             const RestartCounts& current,
                             const ExclusionList& exclusions, bool booting);

bool isStarting(std::string_view systemState);

std::optional<std::string> readFile(const std::filesystem::path& path);
bool writeFileAtomic(const std::filesystem::path& path,
                     std::string_view content);

} // namespace services_health
