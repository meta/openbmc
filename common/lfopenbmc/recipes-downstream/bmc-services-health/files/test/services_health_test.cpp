#include "services_health.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace sh = services_health;

namespace
{

class TempDir : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        auto tmpl = (std::filesystem::temp_directory_path() / "bsh-test-XXXXXX")
                        .string();
        ASSERT_NE(mkdtemp(tmpl.data()), nullptr);
        dir = tmpl;
    }
    void TearDown() override
    {
        std::filesystem::remove_all(dir);
    }
    static void write(const std::filesystem::path& path,
                      const std::string& text)
    {
        std::ofstream(path) << text;
    }
    std::filesystem::path dir;
};

} // namespace

TEST(ExclusionList, SkipsCommentsAndBlankLinesAndTrims)
{
    auto list = sh::ExclusionList::parse(
        "# image default\n\n  systemd-coredump@*.service  \nfoo.service\r\n");
    EXPECT_EQ(list.patterns(),
              (std::vector<std::string>{"systemd-coredump@*.service",
                                        "foo.service"}));
}

TEST(ExclusionList, MatchesGlobs)
{
    auto list =
        sh::ExclusionList::parse("systemd-coredump@*.service\nfoo.service\n");
    EXPECT_TRUE(list.matches("systemd-coredump@0-123-0.service"));
    EXPECT_TRUE(list.matches("foo.service"));
    EXPECT_FALSE(list.matches("foobar.service"));
    EXPECT_FALSE(list.matches("bmcweb.service"));
}

TEST(ExclusionList, EmptyMatchesNothing)
{
    EXPECT_FALSE(sh::ExclusionList{}.matches("bmcweb.service"));
}

TEST(ExclusionList, MatchesSystemdEscapedNamesVerbatim)
{
    auto exact = sh::ExclusionList::parse("foo@a\\x2db.service\n");
    EXPECT_TRUE(exact.matches("foo@a\\x2db.service"));
    auto glob = sh::ExclusionList::parse("foo@*.service\n");
    EXPECT_TRUE(glob.matches("foo@a\\x2db.service"));
}

using LoadExclusions = TempDir;

TEST_F(LoadExclusions, UnionOfFilesAndMissingFilesIgnored)
{
    write(dir / "image", "systemd-coredump@*.service\n");
    write(dir / "local", "obmc-console@ttyS3.service\n");
    auto list =
        sh::loadExclusions({dir / "image", dir / "missing", dir / "local"});
    EXPECT_TRUE(list.matches("systemd-coredump@1.service"));
    EXPECT_TRUE(list.matches("obmc-console@ttyS3.service"));
    EXPECT_FALSE(list.matches("bmcweb.service"));
}

TEST(State, RoundTrip)
{
    sh::RestartState state{.total = 7,
                           .prev = {{"a.service", 3}, {"b.service", 0}},
                           .unknown = {"c.service"}};
    auto text = sh::serializeState(state);
    EXPECT_EQ(text, "total 7\nunit a.service 3\nunit b.service 0\n"
                    "unknown c.service\n");
    EXPECT_EQ(sh::parseState(text), state);
}

TEST(State, RejectsMalformed)
{
    EXPECT_EQ(sh::parseState(""), std::nullopt);
    EXPECT_EQ(sh::parseState("unit a.service 3\n"), std::nullopt);
    EXPECT_EQ(sh::parseState("total x\n"), std::nullopt);
    EXPECT_EQ(sh::parseState("total 1\nunit a.service\n"), std::nullopt);
    EXPECT_EQ(sh::parseState("total 1\nunit a.service -1\n"), std::nullopt);
    EXPECT_EQ(sh::parseState("total 1\nunit a.service 4294967296\n"),
              std::nullopt);
    EXPECT_EQ(sh::parseState("total 1\ngarbage\n"), std::nullopt);
    EXPECT_EQ(sh::parseState("total 1\ntotal 2\n"), std::nullopt);
    EXPECT_EQ(sh::parseState("total 1\nunknown\n"), std::nullopt);
    EXPECT_EQ(sh::parseState("total 1\nunknown a.service 3\n"), std::nullopt);
    EXPECT_EQ(sh::parseState("total 1\nunit a.service 3"), std::nullopt);
    EXPECT_EQ(sh::parseState("total 1 2\n"), std::nullopt);
    EXPECT_EQ(sh::parseState("total 1\nunit a.service 3 4\n"), std::nullopt);
}

using AtomicWrite = TempDir;

TEST_F(AtomicWrite, CreatesParentAndReplaces)
{
    auto path = dir / "sub" / "state";
    ASSERT_TRUE(sh::writeFileAtomic(path, "one\n"));
    ASSERT_TRUE(sh::writeFileAtomic(path, "two\n"));
    EXPECT_EQ(sh::readFile(path), std::string("two\n"));
    EXPECT_FALSE(std::filesystem::exists(dir / "sub" / "state.tmp"));
}

TEST_F(AtomicWrite, ReadMissingIsNullopt)
{
    EXPECT_EQ(sh::readFile(dir / "missing"), std::nullopt);
}

TEST_F(AtomicWrite, FailsWhenParentIsAFile)
{
    write(dir / "f", "x");
    EXPECT_FALSE(sh::writeFileAtomic(dir / "f" / "state", "one\n"));
}

TEST(ReadFile, ReadErrorIsNullopt)
{
    if (!std::ifstream("/proc/self/mem"))
    {
        GTEST_SKIP() << "cannot open /proc/self/mem";
    }
    // Offset 0 of /proc/self/mem is never mapped, so read() fails with EIO.
    EXPECT_EQ(sh::readFile("/proc/self/mem"), std::nullopt);
}

namespace
{
const sh::ExclusionList noExclusions{};
} // namespace

TEST(AccountRestarts, FirstRunBaselinesAndStartsAtZero)
{
    auto next = sh::accountRestarts(
        std::nullopt, {{"a.service", 5}, {"b.service", std::nullopt}},
        noExclusions, false);
    EXPECT_EQ(next.total, 0u);
    EXPECT_EQ(next.prev, (std::map<std::string, uint32_t>{{"a.service", 5}}));
}

TEST(AccountRestarts, FirstRunUnreadableUnitIsBaselinedLater)
{
    auto first = sh::accountRestarts(
        std::nullopt, {{"a.service", 5}, {"b.service", std::nullopt}},
        noExclusions, false);
    EXPECT_EQ(first.unknown, (std::set<std::string>{"b.service"}));
    auto second = sh::accountRestarts(
        first, {{"a.service", 5}, {"b.service", std::nullopt}}, noExclusions,
        false);
    EXPECT_EQ(second.unknown, (std::set<std::string>{"b.service"}));
    auto third = sh::accountRestarts(
        second, {{"a.service", 5}, {"b.service", 40}}, noExclusions, false);
    EXPECT_EQ(third.total, 0u);
    EXPECT_EQ(third.prev.at("b.service"), 40u);
    EXPECT_TRUE(third.unknown.empty());
}

TEST(AccountRestarts, UnreadableNewUnitIsNotTracked)
{
    sh::RestartState prev{
        .total = 0, .prev = {{"a.service", 0}}, .unknown = {}};
    auto next = sh::accountRestarts(
        prev, {{"a.service", 0}, {"new.service", std::nullopt}}, noExclusions,
        false);
    EXPECT_FALSE(next.prev.contains("new.service"));
    EXPECT_TRUE(next.unknown.empty());
}

TEST(AccountRestarts, CountsIncrements)
{
    sh::RestartState prev{.total = 10,
                          .prev = {{"a.service", 5}, {"b.service", 1}},
                          .unknown = {}};
    auto next = sh::accountRestarts(prev, {{"a.service", 7}, {"b.service", 1}},
                                    noExclusions, false);
    EXPECT_EQ(next.total, 12u);
}

TEST(AccountRestarts, ResetCountsCurrentValue)
{
    sh::RestartState prev{
        .total = 10, .prev = {{"a.service", 5}}, .unknown = {}};
    auto next =
        sh::accountRestarts(prev, {{"a.service", 2}}, noExclusions, false);
    EXPECT_EQ(next.total, 12u);
    EXPECT_EQ(next.prev.at("a.service"), 2u);
}

TEST(AccountRestarts, NewUnitContributesCurrentAndGoneUnitIsDropped)
{
    sh::RestartState prev{
        .total = 0, .prev = {{"gone.service", 9}}, .unknown = {}};
    auto next =
        sh::accountRestarts(prev, {{"new.service", 3}}, noExclusions, false);
    EXPECT_EQ(next.total, 3u);
    EXPECT_EQ(next.prev, (std::map<std::string, uint32_t>{{"new.service", 3}}));
}

TEST(AccountRestarts, UnreadableUnitKeepsPrevAndAddsNothing)
{
    sh::RestartState prev{
        .total = 4, .prev = {{"a.service", 5}}, .unknown = {}};
    auto next = sh::accountRestarts(prev, {{"a.service", std::nullopt}},
                                    noExclusions, false);
    EXPECT_EQ(next.total, 4u);
    EXPECT_EQ(next.prev.at("a.service"), 5u);
}

TEST(AccountRestarts, BootingUpdatesPrevButNotTotal)
{
    sh::RestartState prev{
        .total = 4, .prev = {{"a.service", 0}}, .unknown = {}};
    auto next =
        sh::accountRestarts(prev, {{"a.service", 6}}, noExclusions, true);
    EXPECT_EQ(next.total, 4u);
    EXPECT_EQ(next.prev.at("a.service"), 6u);
}

TEST(AccountRestarts, ExcludedUnitsTrackedButNotCounted)
{
    auto exclusions = sh::ExclusionList::parse("systemd-coredump@*.service\n");
    sh::RestartState prev{
        .total = 0, .prev = {{"systemd-coredump@1.service", 0}}, .unknown = {}};
    auto next = sh::accountRestarts(prev, {{"systemd-coredump@1.service", 4}},
                                    exclusions, false);
    EXPECT_EQ(next.total, 0u);
    EXPECT_EQ(next.prev.at("systemd-coredump@1.service"), 4u);
}

TEST(AccountRestarts, BootingNewUnitIsTrackedButNotCounted)
{
    sh::RestartState prev{
        .total = 4, .prev = {{"a.service", 0}}, .unknown = {}};
    auto next = sh::accountRestarts(
        prev, {{"a.service", 0}, {"new.service", 3}}, noExclusions, true);
    EXPECT_EQ(next.total, 4u);
    EXPECT_EQ(next.prev.at("new.service"), 3u);
}

TEST(AccountRestarts, BootingResetUpdatesPrevButNotTotal)
{
    sh::RestartState prev{
        .total = 4, .prev = {{"a.service", 5}}, .unknown = {}};
    auto next =
        sh::accountRestarts(prev, {{"a.service", 2}}, noExclusions, true);
    EXPECT_EQ(next.total, 4u);
    EXPECT_EQ(next.prev.at("a.service"), 2u);
}

TEST(AccountRestarts, NewExcludedUnitIsTrackedButNotCounted)
{
    auto exclusions = sh::ExclusionList::parse("systemd-coredump@*.service\n");
    sh::RestartState prev{
        .total = 2, .prev = {{"a.service", 0}}, .unknown = {}};
    auto next = sh::accountRestarts(
        prev, {{"a.service", 0}, {"systemd-coredump@2.service", 3}}, exclusions,
        false);
    EXPECT_EQ(next.total, 2u);
    EXPECT_EQ(next.prev.at("systemd-coredump@2.service"), 3u);
}

TEST(AccountRestarts, UnknownExcludedUnitIsBaselinedOnceReadable)
{
    auto exclusions = sh::ExclusionList::parse("systemd-coredump@*.service\n");
    auto first = sh::accountRestarts(
        std::nullopt, {{"systemd-coredump@1.service", std::nullopt}},
        exclusions, false);
    EXPECT_EQ(first.unknown,
              (std::set<std::string>{"systemd-coredump@1.service"}));
    auto second = sh::accountRestarts(
        first, {{"systemd-coredump@1.service", 7}}, exclusions, false);
    EXPECT_EQ(second.total, 0u);
    EXPECT_EQ(second.prev.at("systemd-coredump@1.service"), 7u);
    EXPECT_TRUE(second.unknown.empty());
}

TEST(IsStarting, OnlyInitializingAndStarting)
{
    EXPECT_TRUE(sh::isStarting("initializing"));
    EXPECT_TRUE(sh::isStarting("starting"));
    EXPECT_FALSE(sh::isStarting("running"));
    EXPECT_FALSE(sh::isStarting("degraded"));
}
