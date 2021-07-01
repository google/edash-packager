// Copyright 2014 Google Inc. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "packager/file/udp_file.h"

#if defined(OS_WIN)

#include <windows.h>
#include <ws2tcpip.h>
#define close closesocket
#define EINTR_CODE WSAEINTR

#else

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#define INVALID_SOCKET -1
#define EINTR_CODE EINTR

// IP_MULTICAST_ALL has been supported since kernel version 2.6.31 but we may be
// building on a machine that is older than that.
#ifndef IP_MULTICAST_ALL
#define IP_MULTICAST_ALL      49
#endif

#endif  // defined(OS_WIN)

#include <limits>
#include <cassert>

#include "packager/base/logging.h"
#include "packager/file/udp_options.h"

namespace shaka {

namespace {

bool IsIpv4MulticastAddress(const struct in_addr& addr) {
  return (ntohl(addr.s_addr) & 0xf0000000) == 0xe0000000;
}

bool IsIpv6MulticastAddress(const struct in6_addr& addr) {
  if (addr.s6_addr[0] == 0xff) {
    return true;
  }

  return false;
}

bool SpecificMalticast(int ai_family,
                       int level,
                       SOCKET socket,
                       struct group_source_req& gsr,
                       std::unique_ptr<UdpOptions>&& options) {
  if (inet_pton(ai_family,
                options->interface_address().c_str(),
                &gsr.gsr_interface) != 1) {
    LOG(ERROR) << "Malformed IP interface address "
               << options->interface_address();
    return false;
  }
  if (inet_pton(ai_family,
                options->source_address().c_str(),
                &gsr.gsr_source) != 1) {
    LOG(ERROR) << "Malformed IP source specific multicast address "
               << options->source_address();
    return false;
  }
  if (setsockopt(socket,
                 level,
                 MCAST_JOIN_SOURCE_GROUP,
                 reinterpret_cast<const char*>(&gsr),
                 sizeof(gsr)) < 0) {
    LOG(ERROR) << "Failed to join multicast group.";
    return false;
  }

  return true;
}

bool Multicast(int ai_family,
               int level,
               SOCKET socket,
               struct group_req& greq,
               std::unique_ptr<UdpOptions>&& options) {
    if (inet_pton(ai_family,
                  options->interface_address().c_str(),
                  &greq.gr_interface) != 1) {
      LOG(ERROR) << "Malformed IP interface address "
                 << options->interface_address();
      return false;
    }
    if (setsockopt(socket, level, MCAST_JOIN_GROUP,
                  reinterpret_cast<const char*>(&greq),
                  sizeof(greq)) < 0) {
      LOG(ERROR) << "Failed to join multicast group.";
      return false;
    }

#if defined(__linux__)
    // Disable IP_MULTICAST_ALL to avoid interference caused when two sockets
    // are bound to the same port but joined to different multicast groups.
    const int optval_zero = 0;
    if (setsockopt(socket, level, IP_MULTICAST_ALL,
                   reinterpret_cast<const char*>(&optval_zero),
                   sizeof(optval_zero)) < 0 &&
        errno != ENOPROTOOPT) {
      LOG(ERROR) << "Failed to disable IP_MULTICAST_ALL option.";
      return false;
    }
#endif  // #if defined(__linux__)

    return true;
}

int GetSocketErrorCode() {
#if defined(OS_WIN)
  return WSAGetLastError();
#else
  return errno;
#endif
}

}  // anonymous namespace

UdpFile::UdpFile(const char* file_name)
    : File(file_name), socket_(INVALID_SOCKET) {}

UdpFile::~UdpFile() {}

bool UdpFile::Close() {
  if (socket_ != INVALID_SOCKET) {
    close(socket_);
    socket_ = INVALID_SOCKET;
  }
  delete this;
#if defined(OS_WIN)
  if (wsa_started_)
    WSACleanup();
#endif
  return true;
}

int64_t UdpFile::Read(void* buffer, uint64_t length) {
  DCHECK(buffer);
  DCHECK_GE(length, 65535u)
      << "Buffer may be too small to read entire datagram.";

  if (socket_ == INVALID_SOCKET)
    return -1;

  int64_t result;
  do {
    result =
        recvfrom(socket_, reinterpret_cast<char*>(buffer), length, 0, NULL, 0);
  } while (result == -1 && GetSocketErrorCode() == EINTR_CODE);

  return result;
}

int64_t UdpFile::Write(const void* buffer, uint64_t length) {
  NOTIMPLEMENTED();
  return -1;
}

int64_t UdpFile::Size() {
  if (socket_ == INVALID_SOCKET)
    return -1;

  return std::numeric_limits<int64_t>::max();
}

bool UdpFile::Flush() {
  NOTIMPLEMENTED();
  return false;
}

bool UdpFile::Seek(uint64_t position) {
  NOTIMPLEMENTED();
  return false;
}

bool UdpFile::Tell(uint64_t* position) {
  NOTIMPLEMENTED();
  return false;
}

class ScopedSocket {
 public:
  explicit ScopedSocket(SOCKET sock_fd) : sock_fd_(sock_fd) {}

  ~ScopedSocket() {
    if (sock_fd_ != INVALID_SOCKET)
      close(sock_fd_);
  }

  SOCKET get() { return sock_fd_; }

  SOCKET release() {
    SOCKET socket = sock_fd_;
    sock_fd_ = INVALID_SOCKET;
    return socket;
  }

 private:
  SOCKET sock_fd_;

  DISALLOW_COPY_AND_ASSIGN(ScopedSocket);
};

bool UdpFile::Open() {
#if defined(OS_WIN)
  WSADATA wsa_data;
  int wsa_error = WSAStartup(MAKEWORD(2, 2), &wsa_data);
  if (wsa_error != 0) {
    LOG(ERROR) << "Winsock start up failed with error " << wsa_error;
    return false;
  }
  wsa_started_ = true;
#endif  // defined(OS_WIN)

  DCHECK_EQ(INVALID_SOCKET, socket_);

  std::unique_ptr<UdpOptions> options =
      UdpOptions::ParseFromString(file_name());
  if (!options)
    return false;

  unsigned long addr = inet_addr(options->address().c_str());

  if (addr == INADDR_NONE)
    return false;

  struct addrinfo hints = {0};
  struct hostent *host_info = gethostbyaddr((const char *)&addr, (socklen_t)sizeof(SOCKET), AF_UNSPEC);
  struct addrinfo *res;

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  if (getaddrinfo(host_info->h_name, std::to_string(options->port()).c_str(), &hints, &res) != 0) {
    LOG(ERROR) << "getaddrinfo";
    return false;
  }
  ScopedSocket new_socket(socket(res->ai_family, res->ai_socktype, res->ai_protocol));
  if (new_socket.get() == INVALID_SOCKET) {
    LOG(ERROR) << "Could not allocate socket, error = " << GetSocketErrorCode();
    freeaddrinfo(res);
    return false;
  }

  if (options->reuse()) {
    const int optval = 1;
    if (setsockopt(new_socket.get(), SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&optval),
                   sizeof(optval)) < 0) {
      LOG(ERROR) << "Could not apply the SO_REUSEADDR property to the UDP "
                    "socket, error = "
                 << GetSocketErrorCode();
      freeaddrinfo(res);
      return false;
    }
  }

  // Use `sizeof(&res->ai_addr)` instead of `res->ai_addrlen` (for x64)
  if (bind(new_socket.get(),
           reinterpret_cast<struct sockaddr*>(res->ai_addr),
           sizeof(&res->ai_addr)) < 0) {
    LOG(ERROR) << "Could not bind UDP socket, error = " << GetSocketErrorCode();
    freeaddrinfo(res);
    return false;
  }

  switch (res->ai_family) {
    case AF_INET: {
      struct in_addr local_in_addr = {0};
      if (inet_pton(AF_INET, options->address().c_str(), &local_in_addr) != 1) {
        LOG(ERROR) << "Malformed IPv4 address " << options->address();
        freeaddrinfo(res);
        return false;
      }
      const bool is_multicast = IsIpv4MulticastAddress(local_in_addr);
      if (is_multicast) {
        if (options->is_source_specific_multicast()) {
          struct group_source_req gsr;
          gsr.gsr_interface = 0;
          memcpy(&gsr.gsr_group, res->ai_addr, res->ai_addrlen);
          memcpy(&gsr.gsr_source, res->ai_addr, res->ai_addrlen);

          bool result = SpecificMalticast(AF_INET,
                                          IPPROTO_IP,
                                          new_socket.get(),
                                          gsr,
                                          std::move(options));
          if (!result) {
            freeaddrinfo(res);
            return false;
          }
        } else {
          // this is a v2 join without a specific source.
          struct group_req greq;
          greq.gr_interface = 0;
          memcpy(&greq.gr_group, res->ai_addr, res->ai_addrlen);

          bool result = Multicast(AF_INET,
                                  IPPROTO_IP,
                                  new_socket.get(),
                                  greq,
                                  std::move(options));
          if (!result) {
            freeaddrinfo(res);
            return false;
          }
        }
      }
      break;
    }
    case AF_INET6: {
      struct in6_addr local_in_addr = {{}};
      if (inet_pton(AF_INET6, options->address().c_str(), &local_in_addr) != 1) {
        LOG(ERROR) << "Malformed IPv6 address " << options->address();
        freeaddrinfo(res);
        return false;
      }
      const bool is_multicast = IsIpv6MulticastAddress(local_in_addr);
      if (is_multicast) {
        if (options->is_source_specific_multicast()) {
          struct group_source_req gsr;
          gsr.gsr_interface = 0;
          memcpy(&gsr.gsr_group, res->ai_addr, res->ai_addrlen);
          memcpy(&gsr.gsr_source, res->ai_addr, res->ai_addrlen);

          bool result = SpecificMalticast(AF_INET6,
                                          IPPROTO_IPV6,
                                          new_socket.get(),
                                          gsr,
                                          std::move(options));
          if (!result) {
            freeaddrinfo(res);
            return false;
          }
        } else {
          // this is a v2 join without a specific source.
          struct group_req greq;
          greq.gr_interface = 0;
          memcpy(&greq.gr_group, res->ai_addr, res->ai_addrlen);

          bool result = Multicast(AF_INET6,
                                  IPPROTO_IPV6,
                                  new_socket.get(),
                                  greq,
                                  std::move(options));
          if (!result) {
            freeaddrinfo(res);
            return false;
          }
        }
      }
      break;
    }
    default:
      freeaddrinfo(res);
      assert(false);
      LOG(ERROR) << "Unknown `ai_family`";
      break;
  }

  // Set timeout if needed.
  if (options->timeout_us() != 0) {
    struct timeval tv;
    tv.tv_sec = options->timeout_us() / 1000000;
    tv.tv_usec = options->timeout_us() % 1000000;
    if (setsockopt(new_socket.get(), SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&tv), sizeof(tv)) < 0) {
      LOG(ERROR) << "Failed to set socket timeout, error = "
                 << GetSocketErrorCode();
      return false;
    }
  }

  if (options->buffer_size() > 0) {
    const int receive_buffer_size = options->buffer_size();
    if (setsockopt(new_socket.get(), SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&receive_buffer_size),
                   sizeof(receive_buffer_size)) < 0) {
      LOG(ERROR) << "Failed to set the maximum receive buffer size, error = "
                 << GetSocketErrorCode();
      freeaddrinfo(res);
      return false;
    }
  }

  socket_ = new_socket.release();

  freeaddrinfo(res);

  return true;
}

}  // namespace shaka
