// Copyright 2021-present Facebook. All Rights Reserved.
#pragma once
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <string_view>
#include <system_error>
#include <tuple>
#include <vector>

namespace rackmonsvc {

class UnixSock {
 protected:
  int sock_ = -1;
  std::tuple<struct sockaddr_un, size_t> getServiceAddr(
      const std::string& sockPath);
  int getServiceSock();
  int createService(const std::string& sockPath);
  int createClient(const std::string& sockPath);

  void recvRaw(char* buf, size_t len);

 public:
  explicit UnixSock(int fd) : sock_(fd) {}
  int getSock() const {
    return sock_;
  }
  virtual ~UnixSock();
  void send(const char* buf, size_t len);
  void send(const std::vector<char>& buf) {
    send(buf.data(), buf.size());
  }
  void sendRaw(const char* buf, size_t len);
  void recv(std::vector<char>& resp);
};

class UnixServiceSock : public UnixSock {
 public:
  UnixServiceSock(const std::string& sockPath)
      : UnixSock(createService(sockPath)) {}
};

class UnixClientSock : public UnixSock {
 public:
  UnixClientSock(const std::string& sockPath)
      : UnixSock(createClient(sockPath)) {}
};

class UnixService {
 private:
  // The pipe used for the signal handler to request
  // for the loops to exit.
  int backChannelFDs_[2] = {-1, -1};
  // End of the pipe used by the service loop.
  int& backChannelHandler_ = backChannelFDs_[0];
  // End of the pipe to be used by the signal handler.
  int& backChannelRequestor_ = backChannelFDs_[1];
  // The socket we want to receive connections from.
  std::unique_ptr<UnixServiceSock> sock_ = nullptr;
  const std::string sockPath_;

  void registerExitHandler();
  void unregisterExitHandler();

  void handleConnection(std::unique_ptr<UnixSock> sock);

  virtual void handleRequest(
      const std::vector<char>& req,
      std::unique_ptr<UnixSock> cli) = 0;

 public:
  UnixService(const std::string& sockPath) : sockPath_(sockPath) {}
  virtual ~UnixService() {}
  // Request the doLoop to exit.
  void requestExit();
  virtual void initialize();
  virtual void deinitialize();
  // The main service loop. This will block
  // till someone requests it to exit.
  void doLoop();

  static std::mutex activeServiceListLock;
  static std::list<UnixService*> activeServiceList;
  static void triggerExit(int /* unused */);
  static void registerStaticExitHandler();
  static void unregisterStaticExitHandler();
};

class UnixClient {
  UnixClientSock sock_;

 public:
  UnixClient(const std::string& sockPath) : sock_(sockPath) {}
  std::string request(const std::string& req) {
    sock_.send(req.data(), req.size());
    std::vector<char> resp;
    sock_.recv(resp);
    return std::string(resp.begin(), resp.end());
  }
  std::vector<char> request(const std::vector<char>& req) {
    sock_.send(req.data(), req.size());
    std::vector<char> resp;
    sock_.recv(resp);
    return resp;
  }
};

class RackmonClient : public UnixClient {
 public:
  RackmonClient() : UnixClient("/var/run/rackmond.sock") {}
};

} // namespace rackmonsvc
