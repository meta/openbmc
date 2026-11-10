#include "checks.hpp"
#include "log_capture.hpp"

#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace sh = services_health;

namespace
{

sh::Unit unit(const std::string& name, const std::string& active = "active",
              const std::string& load = "loaded")
{
    return sh::Unit{name, load, active, "/org/freedesktop/systemd1/unit/x"};
}

class FakeSystemd : public sh::SystemdApi
{
  public:
    std::optional<std::vector<sh::Unit>> failed = std::vector<sh::Unit>{};
    std::optional<std::vector<sh::Unit>> all = std::vector<sh::Unit>{};
    std::map<std::string, std::optional<uint32_t>> restarts;
    std::optional<std::string> state = "running";
    std::vector<std::vector<std::string>> listCalls;

    std::optional<std::vector<sh::Unit>> listServices(
        const std::vector<std::string>& states) override
    {
        listCalls.push_back(states);
        return states.empty() ? all : failed;
    }
    std::optional<uint32_t> nRestarts(const sh::Unit& u) override
    {
        auto it = restarts.find(u.name);
        return it == restarts.end() ? std::nullopt : it->second;
    }
    std::optional<std::string> systemState() override
    {
        return state;
    }
};

class Checks : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        auto tmpl =
            (std::filesystem::temp_directory_path() / "bsh-checks-XXXXXX")
                .string();
        ASSERT_NE(mkdtemp(tmpl.data()), nullptr);
        dir = tmpl;
        statePath = dir / "state";
    }
    void TearDown() override
    {
        std::filesystem::remove_all(dir);
    }
    std::optional<uint64_t> restartsTotal()
    {
        return sh::serviceRestartsTotal(systemd, exclusions, statePath, [this] {
            return outOfTime;
        });
    }
    FakeSystemd systemd;
    sh::ExclusionList exclusions =
        sh::ExclusionList::parse("systemd-coredump@*.service\n");
    std::filesystem::path dir;
    std::filesystem::path statePath;
    bool outOfTime = false;
};

} // namespace

TEST_F(Checks, FailedServicesSplitsByExclusionList)
{
    systemd.failed = std::vector<sh::Unit>{
        unit("a.service", "failed"),
        unit("systemd-coredump@1.service", "failed"),
        unit("b.service", "failed"),
        unit("gone.service", "failed", "not-found")};
    EXPECT_EQ(sh::failedServices(systemd, exclusions), 2u);
    EXPECT_EQ(sh::failedServicesExcluded(systemd, exclusions), 1u);
    EXPECT_EQ(systemd.listCalls.front(), (std::vector<std::string>{"failed"}));
}

TEST_F(Checks, FailedServicesListErrorIsCheckError)
{
    systemd.failed = std::nullopt;
    EXPECT_EQ(sh::failedServices(systemd, exclusions), std::nullopt);
    EXPECT_EQ(sh::failedServicesExcluded(systemd, exclusions), std::nullopt);
}

TEST_F(Checks, SystemdStarting)
{
    systemd.state = "starting";
    EXPECT_EQ(sh::systemdStarting(systemd), 1u);
    systemd.state = "degraded";
    EXPECT_EQ(sh::systemdStarting(systemd), 0u);
    systemd.state = std::nullopt;
    EXPECT_EQ(sh::systemdStarting(systemd), std::nullopt);
}

TEST_F(Checks, RestartsBaselineThenCount)
{
    systemd.all = std::vector<sh::Unit>{unit("a.service"), unit("b.service")};
    systemd.restarts = {{"a.service", 5}, {"b.service", 0}};
    EXPECT_EQ(restartsTotal(), 0u);
    systemd.restarts = {{"a.service", 8}, {"b.service", 1}};
    EXPECT_EQ(restartsTotal(), 4u);
}

TEST_F(Checks, CorruptStateFileRebaselines)
{
    systemd.all = std::vector<sh::Unit>{unit("a.service")};
    systemd.restarts = {{"a.service", 40}};
    ASSERT_TRUE(sh::writeFileAtomic(statePath, "garbage\n"));
    EXPECT_EQ(restartsTotal(), 0u);
}

TEST_F(Checks, CorruptOrUnreadableStateFileIsLogged)
{
    if (!LogCapture::available())
    {
        GTEST_SKIP() << "lg2 does not copy messages to stderr";
    }
    systemd.all = std::vector<sh::Unit>{unit("a.service")};
    systemd.restarts = {{"a.service", 1}};
    {
        LogCapture log;
        EXPECT_EQ(restartsTotal(), 0u);
        EXPECT_EQ(log.text().find("rebaselining"), std::string::npos)
            << log.text();
    }
    {
        ASSERT_TRUE(sh::writeFileAtomic(statePath, "garbage\n"));
        LogCapture log;
        EXPECT_EQ(restartsTotal(), 0u);
        EXPECT_NE(
            log.text().find("Corrupt " + statePath.string() + "; rebaselining"),
            std::string::npos)
            << log.text();
    }
    {
        // A directory opens but fails to read, like an I/O error.
        statePath = dir / "subdir";
        std::filesystem::create_directory(statePath);
        LogCapture log;
        EXPECT_EQ(restartsTotal(), std::nullopt);
        EXPECT_NE(log.text().find(
                      "Cannot read " + statePath.string() + "; rebaselining"),
                  std::string::npos)
            << log.text();
    }
}

TEST_F(Checks, RestartsNotAddedWhileStarting)
{
    systemd.all = std::vector<sh::Unit>{unit("a.service")};
    systemd.restarts = {{"a.service", 0}};
    ASSERT_EQ(restartsTotal(), 0u);
    systemd.state = "starting";
    systemd.restarts = {{"a.service", 6}};
    EXPECT_EQ(restartsTotal(), 0u);
    systemd.state = "running";
    systemd.restarts = {{"a.service", 7}};
    EXPECT_EQ(restartsTotal(), 1u);
}

TEST_F(Checks, RestartsSkipUnloadedAndUnreadableUnits)
{
    systemd.all = std::vector<sh::Unit>{
        unit("a.service"), unit("masked.service", "inactive", "masked"),
        unit("flaky.service")};
    systemd.restarts = {
        {"a.service", 0}, {"masked.service", 0}, {"flaky.service", 2}};
    ASSERT_EQ(restartsTotal(), 0u);
    systemd.restarts = {{"a.service", 1}, {"masked.service", 9}};
    EXPECT_EQ(restartsTotal(), 1u);
    systemd.restarts = {
        {"a.service", 1}, {"masked.service", 9}, {"flaky.service", 3}};
    EXPECT_EQ(restartsTotal(), 2u);
}

TEST_F(Checks, RestartsListOrStateErrorIsCheckError)
{
    systemd.all = std::nullopt;
    EXPECT_EQ(restartsTotal(), std::nullopt);
    systemd.all = std::vector<sh::Unit>{};
    systemd.state = std::nullopt;
    EXPECT_EQ(restartsTotal(), std::nullopt);
}

TEST_F(Checks, RestartsOutOfTimeIsCheckErrorAndKeepsState)
{
    systemd.all = std::vector<sh::Unit>{unit("a.service")};
    systemd.restarts = {{"a.service", 5}};
    ASSERT_EQ(restartsTotal(), 0u);
    const auto before = sh::readFile(statePath);
    systemd.restarts = {{"a.service", 9}};
    outOfTime = true;
    EXPECT_EQ(restartsTotal(), std::nullopt);
    EXPECT_EQ(sh::readFile(statePath), before);
    outOfTime = false;
    EXPECT_EQ(restartsTotal(), 4u);
}

TEST_F(Checks, RestartsOutOfTimeMidLoopKeepsState)
{
    systemd.all = std::vector<sh::Unit>{unit("a.service"), unit("b.service")};
    systemd.restarts = {{"a.service", 5}, {"b.service", 1}};
    ASSERT_EQ(restartsTotal(), 0u);
    const auto before = sh::readFile(statePath);
    systemd.restarts = {{"a.service", 9}, {"b.service", 3}};
    int asked = 0;
    EXPECT_EQ(sh::serviceRestartsTotal(systemd, exclusions, statePath,
                                       [&asked] { return ++asked > 1; }),
              std::nullopt);
    EXPECT_EQ(asked, 2);
    EXPECT_EQ(sh::readFile(statePath), before);
}

TEST_F(Checks, RestartsStateWriteErrorIsCheckError)
{
    systemd.all = std::vector<sh::Unit>{unit("a.service")};
    systemd.restarts = {{"a.service", 5}};
    ASSERT_TRUE(sh::writeFileAtomic(dir / "file", "x\n"));
    statePath = dir / "file" / "state";
    EXPECT_EQ(restartsTotal(), std::nullopt);
}

namespace
{

using Writes = std::vector<std::tuple<std::string, uint64_t, std::string>>;

class FakeWriter : public sh::MetricWriter
{
  public:
    Writes writes;
    std::string failOn;
    bool set(const std::string& name, uint64_t value,
             const std::string& unit) override
    {
        if (name == failOn)
        {
            return false;
        }
        writes.emplace_back(name, value, unit);
        return true;
    }
};

uint64_t fixedUptime()
{
    return 1234;
}

} // namespace

TEST(RunOnce, WritesEveryValueThenHeartbeatLast)
{
    FakeWriter writer;
    const std::vector<sh::Check> checks{
        {"one", [] { return std::optional<uint64_t>{1}; }},
        {"two", [] { return std::optional<uint64_t>{0}; }}};
    EXPECT_TRUE(sh::runOnce(checks, writer, fixedUptime));
    EXPECT_EQ(writer.writes,
              (Writes{{"one", 1, "count"},
                      {"two", 0, "count"},
                      {"services_health_heartbeat", 1234, "seconds"}}));
}

TEST(RunOnce, CheckErrorWritesNothingForItAndNoHeartbeat)
{
    FakeWriter writer;
    const std::vector<sh::Check> checks{
        {"one", [] { return std::optional<uint64_t>{}; }},
        {"two", [] { return std::optional<uint64_t>{3}; }}};
    EXPECT_FALSE(sh::runOnce(checks, writer, fixedUptime));
    EXPECT_EQ(writer.writes, (Writes{{"two", 3, "count"}}));
}

TEST(RunOnce, WriteErrorStillWritesTheRestButNoHeartbeat)
{
    FakeWriter writer;
    writer.failOn = "one";
    const std::vector<sh::Check> checks{
        {"one", [] { return std::optional<uint64_t>{1}; }},
        {"two", [] { return std::optional<uint64_t>{2}; }}};
    EXPECT_FALSE(sh::runOnce(checks, writer, fixedUptime));
    EXPECT_EQ(writer.writes, (Writes{{"two", 2, "count"}}));
}

TEST(RunOnce, HeartbeatWriteErrorIsFailure)
{
    FakeWriter writer;
    writer.failOn = "services_health_heartbeat";
    const std::vector<sh::Check> checks{
        {"one", [] { return std::optional<uint64_t>{1}; }}};
    EXPECT_FALSE(sh::runOnce(checks, writer, fixedUptime));
    EXPECT_EQ(writer.writes, (Writes{{"one", 1, "count"}}));
}
