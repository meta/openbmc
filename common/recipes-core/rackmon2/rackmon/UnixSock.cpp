// Copyright 2021-present Facebook. All Rights Reserved.
#include "UnixSock.h"
#include <Log.h>
#include <poll.h>
#include <unistd.h>
#include <csignal>
#include <thread>

namespace rackmonsvc {

std::list<UnixService*> UnixService::activeServiceList{};
std::mutex UnixService::activeServiceListLock{};

std::tuple<struct sockaddr_un, size_t> UnixSock::getServiceAddr(
    const std::string& sockPath) {
  struct sockaddr_un ret{};
  ret.sun_family = AF_UNIX;
  std::strncpy(ret.sun_path, sockPath.c_str(), sizeof(ret.sun_path) - 1);
  return std::make_tuple(ret, sockPath.size() + sizeof(ret.sun_family));
}

static void setRecvTimeout(int fd, int timeoutSec) {
  struct timeval timeout;
  timeout.tv_sec = timeoutSec;
  timeout.tv_usec = 0;
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) < 0) {
    logError << "Unable to set recv timeout" << std::endl;
  }
}

static void setSendTimeout(int fd, int timeoutSec) {
  struct timeval timeout;
  timeout.tv_sec = timeoutSec;
  timeout.tv_usec = 0;
  if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout) < 0) {
    logError << "Unable to set send timeout" << std::endl;
  }
}

int UnixSock::getServiceSock() {
  int ret = socket(AF_UNIX, SOCK_STREAM, 0);
  if (ret < 0) {
    throw std::system_error(
        std::error_code(errno, std::generic_category()), "socket_creation");
  }
  return ret;
}

int UnixSock::createService(const std::string& sockPath) {
  int sock = getServiceSock();
  if (access(sockPath.c_str(), F_OK) != -1) {
    logWarn << "WARNING: " << sockPath << " is being recreated" << std::endl;
    unlink(sockPath.c_str());
  }
  auto [local, sockLen] = getServiceAddr(sockPath);
  if (bind(sock, (struct sockaddr*)&local, sockLen) != 0) {
    close(sock);
    throw std::system_error(
        std::error_code(errno, std::generic_category()), "socket_bind");
  }
  if (listen(sock, 20) != 0) {
    close(sock);
    throw std::system_error(
        std::error_code(errno, std::generic_category()), "socket_listen");
  }
  return sock;
}

int UnixSock::createClient(const std::string& sockPath) {
  int sock = getServiceSock();
  // This include time client will wait for a connect to succeed.
  setSendTimeout(sock, 15);
  // If we are waiting longer than 30s, then something is really off.
  setRecvTimeout(sock, 30);
  auto [rackmondAddr, addrLen] = getServiceAddr(sockPath);
  if (connect(sock, (struct sockaddr*)&rackmondAddr, addrLen)) {
    close(sock);
    throw std::system_error(
        std::error_code(errno, std::generic_category()), "socket_connect");
  }
  return sock;
}

UnixSock::~UnixSock() {
  if (sock_ != -1)
    close(sock_);
}

void UnixSock::sendRaw(const char* buf, size_t len) {
  int retries = 0;
  const int maxRetries = 3;
  size_t sentSize = 0;
  while (sentSize < len) {
    int sz = ::send(sock_, buf, len - sentSize, 0);
    if (sz <= 0) {
      if (++retries == maxRetries) {
        throw std::system_error(
            std::error_code(errno, std::generic_category()), "send");
      }
      continue;
    }
    retries = 0;
    sentSize += (size_t)sz;
    buf += sz;
  }
}

void UnixSock::recvRaw(char* buf, size_t len) {
  const int maxRetries = 3;
  int retries = 0;
  size_t recvSize = 0;
  while (recvSize < len) {
    int sz = ::recv(sock_, buf, len - recvSize, 0);
    if (sz <= 0) {
      retries++;
      if (retries == maxRetries) {
        throw std::system_error(
            std::error_code(errno, std::generic_category()), "recv");
      }
      continue;
    }
    retries = 0;
    recvSize += (size_t)sz;
    buf += sz;
  }
}

void UnixSock::send(const char* buf, size_t len) {
  uint32_t sendLen = len;
  sendRaw((const char*)&sendLen, sizeof(sendLen));
  sendRaw(buf, len);
}

void UnixSock::recv(std::vector<char>& resp) {
  uint32_t recvLen = 0;
  recvRaw((char*)&recvLen, sizeof(recvLen));
  if (recvLen == 0) {
    throw std::runtime_error("Zero sized packet received");
  }
  resp.resize(recvLen);
  recvRaw((char*)resp.data(), recvLen);
}

void UnixService::triggerExit(int /* unused */) {
  std::unique_lock lk(activeServiceListLock);
  for (auto& ent : activeServiceList) {
    ent->requestExit();
  }
}

void UnixService::registerStaticExitHandler() {
  struct sigaction ign_action, exit_action;
  ign_action.sa_flags = 0;
  sigemptyset(&ign_action.sa_mask);
  ign_action.sa_handler = SIG_IGN;
  if (sigaction(SIGPIPE, &ign_action, NULL) != 0) {
    throw std::system_error(
        std::error_code(errno, std::generic_category()), "SIGPIPE handler");
  }

  exit_action.sa_flags = 0;
  sigemptyset(&exit_action.sa_mask);
  exit_action.sa_handler = UnixService::triggerExit;
  if (sigaction(SIGTERM, &exit_action, NULL) != 0) {
    throw std::system_error(
        std::error_code(errno, std::generic_category()), "SIGTERM handler");
  }
  if (sigaction(SIGINT, &exit_action, NULL) != 0) {
    throw std::system_error(
        std::error_code(errno, std::generic_category()), "SIGINT handler");
  }
}

void UnixService::unregisterStaticExitHandler() {
  signal(SIGTERM, SIG_DFL);
  signal(SIGINT, SIG_DFL);
}

void UnixService::registerExitHandler() {
  if (pipe(backChannelFDs_) != 0) {
    std::system_error(
        std::error_code(errno, std::generic_category()),
        "Backchannel Pipe creation");
  }
  std::unique_lock lk(activeServiceListLock);
  activeServiceList.push_back(this);
  if (activeServiceList.size() == 1) {
    registerStaticExitHandler();
  }
}

void UnixService::unregisterExitHandler() {
  std::unique_lock lk(activeServiceListLock);
  for (auto it = activeServiceList.begin(); it != activeServiceList.end();
       ++it) {
    if (*it == this) {
      activeServiceList.erase(it);
      break;
    }
  }
  if (activeServiceList.size() == 0) {
    unregisterStaticExitHandler();
  }
  if (backChannelRequestor_ != -1) {
    close(backChannelRequestor_);
    backChannelRequestor_ = -1;
  }
  if (backChannelHandler_ != -1) {
    close(backChannelHandler_);
    backChannelHandler_ = -1;
  }
}

void UnixService::initialize() {
  registerExitHandler();
  sock_ = std::make_unique<UnixServiceSock>(sockPath_);
}

void UnixService::deinitialize() {
  unregisterExitHandler();
  unlink(sockPath_.c_str());
}

void UnixService::requestExit() {
  char c = 'c';
  logInfo << "Got request exit" << std::endl;
  if (write(backChannelRequestor_, &c, 1) != 1) {
    logError << "Could not request rackmon svc to exit!" << std::endl;
  }
}

void UnixService::handleConnection(std::unique_ptr<UnixSock> sock) {
  std::vector<char> buf;
  try {
    sock->recv(buf);
  } catch (...) {
    logError << "Failed to receive message" << std::endl;
    return;
  }
  handleRequest(buf, std::move(sock));
}

void UnixService::doLoop() {
  struct sockaddr_un client;
  struct pollfd pfd[2] = {
      {sock_->getSock(), POLLIN, 0},
      {backChannelHandler_, POLLIN, 0},
  };

  while (1) {
    int ret;
    socklen_t clisocklen = sizeof(struct sockaddr_un);

    ret = poll(pfd, 2, -1);
    if (ret <= 0) {
      // This should be the common case. The entire thing
      // with the pipe is to handle the race condition when
      // we get a signal when we are actively handling a
      // previous request.
      logInfo << "Handling termination signal" << std::endl;
      break;
    }
    if (pfd[1].revents & POLLIN) {
      char c;
      if (read(pfd[1].fd, &c, 1) != 1) {
        logError << "Got something but no data!" << std::endl;
      } else if (c == 'c') {
        logInfo << "Handling termination request" << std::endl;
        break;
      } else {
        logError << "Got unknown command: " << c << std::endl;
      }
    }
    if (pfd[0].revents & POLLIN) {
      int clifd =
          accept(sock_->getSock(), (struct sockaddr*)&client, &clisocklen);
      if (clifd < 0) {
        logError << "Failed to accept new connection" << std::endl;
        continue;
      }

      // We operate in 64k chunks. We should never wait longer than
      // 2s to send/recv. Even after that we retry in the calls,
      // so this is a fair value.
      setRecvTimeout(clifd, 1);
      setSendTimeout(clifd, 1);
      auto clisock = std::make_unique<UnixSock>(clifd);
      handleConnection(std::move(clisock));
    }
  }
}

} // namespace rackmonsvc
