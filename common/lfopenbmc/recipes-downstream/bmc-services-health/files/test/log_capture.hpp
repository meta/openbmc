#pragma once

#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>

// lg2 copies each message to std::cerr only when stderr is a TTY or
// LG2_FORCE_STDERR is set (test/meson.build sets it), and decides that once
// at startup, so tests that read the log skip when neither holds.
class LogCapture
{
  public:
    LogCapture() : old(std::cerr.rdbuf(captured.rdbuf())) {}
    ~LogCapture()
    {
        std::cerr.rdbuf(old);
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    static bool available()
    {
        return std::getenv("LG2_FORCE_STDERR") != nullptr ||
               isatty(STDERR_FILENO) != 0;
    }
    std::string text() const
    {
        return captured.str();
    }

  private:
    std::ostringstream captured;
    std::streambuf* old;
};
