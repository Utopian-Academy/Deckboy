// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <cstdint>
#include <string>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace deckboy::platform {

#ifndef _WIN32

using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#ifdef MSG_NOSIGNAL
constexpr int kSocketSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSocketSendFlags = 0;
#endif

inline void closeSocket(SocketHandle socketHandle) {
  if (socketHandle >= 0) {
    close(socketHandle);
  }
}

inline void setCloseOnExec(SocketHandle socketHandle) {
  if (socketHandle < 0) {
    return;
  }
  int flags = fcntl(socketHandle, F_GETFD, 0);
  if (flags < 0) {
    return;
  }
  fcntl(socketHandle, F_SETFD, flags | FD_CLOEXEC);
}

inline SocketHandle createBoundSocket(int type, int port, bool shouldListen) {
  SocketHandle socketHandle = socket(AF_INET, type, 0);
  if (socketHandle < 0) {
    return kInvalidSocket;
  }
  setCloseOnExec(socketHandle);

  int reuse = 1;
  setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<uint16_t>(port));

  if (bind(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    closeSocket(socketHandle);
    return kInvalidSocket;
  }

  if (shouldListen && listen(socketHandle, 8) != 0) {
    closeSocket(socketHandle);
    return kInvalidSocket;
  }

  return socketHandle;
}

inline SocketHandle createDatagramSocket(bool enableBroadcast = false) {
  SocketHandle socketHandle = socket(AF_INET, SOCK_DGRAM, 0);
  if (socketHandle < 0) {
    return kInvalidSocket;
  }
  setCloseOnExec(socketHandle);
  if (enableBroadcast) {
    int allow = 1;
    setsockopt(socketHandle, SOL_SOCKET, SO_BROADCAST, &allow, sizeof(allow));
  }
  return socketHandle;
}

inline std::string socketAddressToString(const sockaddr_in& address) {
  char buffer[INET_ADDRSTRLEN] = {};
  const char* rendered = inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer));
  if (!rendered) {
    return "";
  }
  return std::string(rendered);
}

#else // _WIN32

using SocketHandle = uintptr_t;
constexpr SocketHandle kInvalidSocket = ~static_cast<uintptr_t>(0);
constexpr int kSocketSendFlags = 0;

inline void closeSocket(SocketHandle socketHandle) {
  if (socketHandle != kInvalidSocket) {
    closesocket(static_cast<SOCKET>(socketHandle));
  }
}

inline void setCloseOnExec(SocketHandle /*socketHandle*/) {
  // Windows sockets are not inherited by child processes by default.
}

inline SocketHandle createBoundSocket(int type, int port, bool shouldListen) {
  SOCKET s = ::socket(AF_INET, type, 0);
  if (s == INVALID_SOCKET) {
    return kInvalidSocket;
  }

  int reuse = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char*>(&reuse), sizeof(reuse));

  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(static_cast<uint16_t>(port));

  if (::bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    closesocket(s);
    return kInvalidSocket;
  }

  if (shouldListen && ::listen(s, 8) != 0) {
    closesocket(s);
    return kInvalidSocket;
  }

  return static_cast<SocketHandle>(s);
}

inline SocketHandle createDatagramSocket(bool enableBroadcast = false) {
  SOCKET s = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (s == INVALID_SOCKET) {
    return kInvalidSocket;
  }
  if (enableBroadcast) {
    int allow = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&allow), sizeof(allow));
  }
  return static_cast<SocketHandle>(s);
}

inline std::string socketAddressToString(const sockaddr_in& address) {
  char buffer[INET_ADDRSTRLEN] = {};
  const char* rendered = inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer));
  if (!rendered) {
    return "";
  }
  return std::string(rendered);
}

#endif // _WIN32

} // namespace deckboy::platform
