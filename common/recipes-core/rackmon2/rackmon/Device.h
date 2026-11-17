// Copyright 2021-present Facebook. All Rights Reserved.
#pragma once
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <system_error>

namespace rackmon {

struct TimeoutException : public std::runtime_error {
  TimeoutException() : std::runtime_error("Timeout") {}
};

class Device {
 protected:
  const std::string device_;
  std::atomic<int> deviceFd_{-1};
  int waitTimePerByteMs = -1;

  // Wait for read for provided timeoutMs, returns
  // remaining time in ms.
  virtual int waitRead(int timeoutMs);
  virtual void waitWrite() {}
  // Close the device if err means it is gone, e.g. a USB serial adapter
  // that disconnected and hung up its tty. This releases the tty so the
  // adapter gets the same name back when it reconnects.
  void closeIfGone(const std::error_code& err);

 public:
  explicit Device(const std::string& device) : device_(device) {}
  virtual ~Device() = default;
  virtual void open();
  virtual void close();
  virtual void write(const uint8_t* buf, size_t len);
  virtual void ioctl(unsigned long cmd, void* data);
  virtual size_t read(uint8_t* buf, size_t exactLen, int timeoutMs);
  virtual bool exists();
  virtual bool isOpened() const {
    return deviceFd_ != -1;
  }
};
} // namespace rackmon
