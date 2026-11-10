#include "services_health.hpp"

#include <fnmatch.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <sstream>
#include <system_error>

namespace services_health
{

namespace
{

std::string_view trim(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
    {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

} // namespace

ExclusionList ExclusionList::parse(std::string_view text)
{
    ExclusionList list;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line))
    {
        const auto glob = trim(line);
        if (glob.empty() || glob.front() == '#')
        {
            continue;
        }
        list.globs.emplace_back(glob);
    }
    return list;
}

void ExclusionList::append(const ExclusionList& other)
{
    globs.insert(globs.end(), other.globs.begin(), other.globs.end());
}

bool ExclusionList::matches(const std::string& unit) const
{
    // systemd matches unit patterns with FNM_NOESCAPE, and escaped names
    // such as foo@a\x2db.service must match when copied verbatim.
    return std::any_of(globs.begin(), globs.end(), [&](const std::string& glob) {
        return fnmatch(glob.c_str(), unit.c_str(), FNM_NOESCAPE) == 0;
    });
}

ExclusionList loadExclusions(const std::vector<std::filesystem::path>& paths)
{
    ExclusionList list;
    for (const auto& path : paths)
    {
        if (auto text = readFile(path))
        {
            list.append(ExclusionList::parse(*text));
        }
    }
    return list;
}

std::optional<std::string> readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    // istream::read turns a read error into badbit; istreambuf_iterator
    // would let libstdc++'s ios_base::failure escape instead.
    std::string text;
    std::array<char, 4096> buf{};
    while (in.read(buf.data(), buf.size()) || in.gcount() > 0)
    {
        text.append(buf.data(), static_cast<size_t>(in.gcount()));
    }
    if (in.bad())
    {
        return std::nullopt;
    }
    return text;
}

namespace
{

template <typename T>
std::optional<T> parseUnsigned(const std::string& text)
{
    T value{};
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end)
    {
        return std::nullopt;
    }
    return value;
}

// Returns false on a malformed line; the whole state file is then rejected.
bool parseStateLine(const std::string& line, RestartState& state,
                    bool& haveTotal)
{
    std::istringstream fields{line};
    std::string kind;
    std::string name;
    std::string value;
    std::string extra;
    fields >> kind;
    if (kind == "total" && !haveTotal)
    {
        if (!(fields >> value) || (fields >> extra))
        {
            return false;
        }
        const auto total = parseUnsigned<uint64_t>(value);
        if (!total)
        {
            return false;
        }
        state.total = *total;
        haveTotal = true;
        return true;
    }
    if (kind == "unit")
    {
        if (!(fields >> name >> value) || (fields >> extra))
        {
            return false;
        }
        const auto count = parseUnsigned<uint32_t>(value);
        if (!count)
        {
            return false;
        }
        state.prev[name] = *count;
        return true;
    }
    if (kind == "unknown")
    {
        if (!(fields >> name) || (fields >> extra))
        {
            return false;
        }
        state.unknown.insert(name);
        return true;
    }
    return false;
}

} // namespace

std::optional<RestartState> parseState(std::string_view text)
{
    // serializeState always ends with a newline, so a missing one means the
    // file was truncated.
    if (!text.empty() && text.back() != '\n')
    {
        return std::nullopt;
    }
    RestartState state;
    bool haveTotal = false;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && !parseStateLine(line, state, haveTotal))
        {
            return std::nullopt;
        }
    }
    if (!haveTotal)
    {
        return std::nullopt;
    }
    return state;
}

std::string serializeState(const RestartState& state)
{
    // systemd unit names cannot contain whitespace, so space-separated fields
    // are unambiguous.
    std::ostringstream out;
    out << "total " << state.total << '\n';
    for (const auto& [unit, count] : state.prev)
    {
        out << "unit " << unit << ' ' << count << '\n';
    }
    for (const auto& unit : state.unknown)
    {
        out << "unknown " << unit << '\n';
    }
    return out.str();
}

bool writeFileAtomic(const std::filesystem::path& path,
                     std::string_view content)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        return false;
    }
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        if (!out)
        {
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    // rename() is atomic, so a watchdog kill mid-write leaves the old state.
    std::filesystem::rename(tmp, path, ec);
    if (ec)
    {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

namespace
{

// First run after boot, or the state file was lost: publishing systemd's
// lifetime counts as new restarts would fire the alert.
RestartState baselineRestarts(const RestartCounts& current)
{
    RestartState next;
    for (const auto& [unit, count] : current)
    {
        if (count)
        {
            next.prev[unit] = *count;
        }
        else
        {
            next.unknown.insert(unit);
        }
    }
    return next;
}

} // namespace

RestartState accountRestarts(const std::optional<RestartState>& previous,
                             const RestartCounts& current,
                             const ExclusionList& exclusions, bool booting)
{
    if (!previous)
    {
        return baselineRestarts(current);
    }

    RestartState next;
    next.total = previous->total;
    for (const auto& [unit, count] : current)
    {
        const auto old = previous->prev.find(unit);
        const bool wasUnknown = previous->unknown.contains(unit);
        if (!count)
        {
            if (old != previous->prev.end())
            {
                next.prev[unit] = old->second;
            }
            else if (wasUnknown)
            {
                next.unknown.insert(unit);
            }
            continue;
        }
        if (wasUnknown)
        {
            next.prev[unit] = *count;
            continue;
        }
        // A drop means systemd reset NRestarts (manual start, reset-failed,
        // RESTART_RESET=1), so the current value is what accrued since.
        // A reset followed by more than prev restarts within one 60 s
        // interval looks like an increase and is undercounted by prev; that
        // needs a manual restart and a fast crash loop in the same minute, so
        // it is accepted.
        uint32_t increment = *count;
        if (old != previous->prev.end() && *count >= old->second)
        {
            increment = *count - old->second;
        }
        // While booting, prev still moves on: otherwise boot-time restarts
        // would be counted at the first run after boot finishes.
        if (!booting && !exclusions.matches(unit))
        {
            next.total += increment;
        }
        next.prev[unit] = *count;
    }
    return next;
}

bool isStarting(std::string_view systemState)
{
    return systemState == "initializing" || systemState == "starting";
}

} // namespace services_health
