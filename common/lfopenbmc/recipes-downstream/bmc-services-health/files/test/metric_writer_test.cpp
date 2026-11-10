#include "log_capture.hpp"
#include "metric_writer.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

namespace sh = services_health;

class MetricWriterTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        auto tmpl =
            (std::filesystem::temp_directory_path() / "bsh-writer-XXXXXX")
                .string();
        ASSERT_NE(mkdtemp(tmpl.data()), nullptr);
        dir = tmpl;
        script = dir / "bmc-oem-metric";
        log = dir / "log";
    }
    void TearDown() override
    {
        std::filesystem::remove_all(dir);
    }
    void makeScript(const std::string& last)
    {
        std::ofstream(script)
            << "#!/bin/sh\necho \"$@\" >> " << log.string() << "\n"
            << last << "\n";
        std::filesystem::permissions(script, std::filesystem::perms::owner_all);
    }
    std::string logText()
    {
        std::ifstream in(log);
        std::istreambuf_iterator<char> begin(in);
        std::istreambuf_iterator<char> end;
        return std::string(begin, end);
    }
    std::filesystem::path dir;
    std::filesystem::path script;
    std::filesystem::path log;
};

TEST_F(MetricWriterTest, RunsSetWithNameValueAndUnit)
{
    makeScript("exit 0");
    sh::BmcOemMetricWriter writer(script);
    EXPECT_TRUE(writer.set("failed_services", 2, "count"));
    EXPECT_TRUE(writer.set("services_health_heartbeat", 61, "seconds"));
    EXPECT_EQ(logText(), "set failed_services 2 count\n"
                         "set services_health_heartbeat 61 seconds\n");
}

TEST_F(MetricWriterTest, NonZeroExitIsFailure)
{
    makeScript("exit 1");
    EXPECT_FALSE(sh::BmcOemMetricWriter(script).set("x", 1, "count"));
}

TEST_F(MetricWriterTest, KilledBySignalIsFailure)
{
    makeScript("kill -9 $$");
    EXPECT_FALSE(sh::BmcOemMetricWriter(script).set("x", 1, "count"));
    EXPECT_EQ(logText(), "set x 1 count\n");
}

TEST_F(MetricWriterTest, FailureLogsExitCodeOrSignal)
{
    if (!LogCapture::available())
    {
        GTEST_SKIP() << "lg2 does not copy messages to stderr";
    }
    {
        makeScript("exit 3");
        LogCapture log;
        EXPECT_FALSE(sh::BmcOemMetricWriter(script).set("x", 1, "count"));
        EXPECT_NE(log.text().find("exited with 3"), std::string::npos)
            << log.text();
    }
    {
        makeScript("kill -9 $$");
        LogCapture log;
        EXPECT_FALSE(sh::BmcOemMetricWriter(script).set("x", 1, "count"));
        EXPECT_NE(log.text().find("killed by signal 9"), std::string::npos)
            << log.text();
    }
}

TEST_F(MetricWriterTest, MissingExecutableIsFailure)
{
    EXPECT_FALSE(sh::BmcOemMetricWriter(dir / "missing").set("x", 1, "count"));
}
