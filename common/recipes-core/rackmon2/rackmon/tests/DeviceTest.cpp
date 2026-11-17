// Copyright 2021-present Facebook. All Rights Reserved.
#include "Device.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <fstream>
#include <thread>
#include "TempDir.h"
#include "TestUtils.h"

using namespace std::literals;
using namespace rackmon;

// Closing a pty's master hangs up its slave the same way a USB serial
// adapter that disconnects hangs up its ttyUSB.
class Pty {
  int master_ = -1;
  std::string slavePath_{};

 public:
  Pty() {
    master_ = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_ < 0 || grantpt(master_) != 0 || unlockpt(master_) != 0) {
      throw std::runtime_error("pty setup failed");
    }
    slavePath_ = ptsname(master_);
  }
  ~Pty() {
    hangUp();
  }
  const std::string& slavePath() const {
    return slavePath_;
  }
  void hangUp() {
    if (master_ >= 0) {
      ::close(master_);
      master_ = -1;
    }
  }
};

class DeviceTest : public ::testing::Test {
  TempDirectory test_dir_{};

 public:
  std::string test_device_path{};

 protected:
  void SetUp() override {
    test_device_path = test_dir_.path() + "/test.bin";
    std::vector<uint8_t> out = {1, 2, 3, 4};
    std::ofstream fp(test_device_path, std::ios::binary);
    std::copy(out.begin(), out.end(), std::ostreambuf_iterator<char>(fp));
    fp.close();
  }
};

TEST_F(DeviceTest, OpenClose) {
  Device dev_bad("./test2.bin");
  EXPECT_THROW(dev_bad.open(), std::system_error);
  ASSERT_FALSE(dev_bad.exists());
  Device dev(test_device_path);
  dev.open();
  ASSERT_TRUE(dev.exists());
  dev.close();
}

TEST_F(DeviceTest, IsOpened) {
  Device dev(test_device_path);
  ASSERT_FALSE(dev.isOpened());
  dev.open();
  ASSERT_TRUE(dev.isOpened());
  dev.close();
  ASSERT_FALSE(dev.isOpened());
}

TEST_F(DeviceTest, WriteClosesHungUpTty) {
  Pty pty;
  Device dev(pty.slavePath());
  dev.open();
  pty.hangUp();
  std::vector<uint8_t> buf = {1, 2, 3, 4};
  EXPECT_THROW(dev.write(buf.data(), buf.size()), std::system_error);
  ASSERT_FALSE(dev.isOpened());
}

TEST_F(DeviceTest, ReadClosesHungUpTty) {
  Pty pty;
  Device dev(pty.slavePath());
  dev.open();
  pty.hangUp();
  std::vector<uint8_t> buf(4);
  EXPECT_THROW(dev.read(buf.data(), buf.size(), 100), std::system_error);
  ASSERT_FALSE(dev.isOpened());
}

TEST_F(DeviceTest, ReadTimeoutKeepsTtyOpen) {
  Pty pty;
  Device dev(pty.slavePath());
  dev.open();
  std::vector<uint8_t> buf(4);
  EXPECT_THROW(dev.read(buf.data(), buf.size(), 10), TimeoutException);
  ASSERT_TRUE(dev.isOpened());
  dev.close();
}

TEST_F(DeviceTest, ReadTest) {
  Device dev(test_device_path);
  dev.open();
  std::vector<uint8_t> buf(4);
  dev.read(buf.data(), buf.size(), 10);
  std::vector<uint8_t> exp = {1, 2, 3, 4};
  ASSERT_EQ(buf, exp);
}

TEST_F(DeviceTest, WriteTest) {
  Device dev(test_device_path);
  dev.open();
  std::vector<uint8_t> wbuf = {5, 6, 7, 8};
  dev.write(wbuf.data(), wbuf.size());
  dev.close();
  dev.open();
  std::vector<uint8_t> rbuf(4);
  dev.read(rbuf.data(), rbuf.size(), 10);
  ASSERT_EQ(rbuf, wbuf);
}
