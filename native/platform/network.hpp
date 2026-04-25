// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// network.hpp — Cross-platform socket utilities for UDP/TCP networking.
//
// Provides a thin abstraction layer over POSIX sockets and Winsock2:
//   - SocketHandle type alias (int on POSIX, uintptr_t on Windows)
//   - kInvalidSocket sentinel (-1 on POSIX, ~0 on Windows)
//   - createBoundSocket(): bind a TCP or UDP socket to a port
//   - createDatagramSocket(): create a UDP socket (optionally broadcast-enabled)
//   - closeSocket(): close a socket handle
//   - setCloseOnExec(): prevent socket inheritance by child processes (POSIX)
//   - socketAddressToString(): convert sockaddr_in to dotted-decimal string
//
// Used by: main.cpp for OSC/Companion/Art-Net/tally UDP listeners, and by
// the integration backend for protocol-specific networking.
//
// Platform differences:
//   - POSIX uses close(), MSG_NOSIGNAL, fcntl for FD_CLOEXEC
//   - Windows uses closesocket(), no MSG_NOSIGNAL (sockets aren't inherited)
//   - Windows requires WSAStartup() before any socket call (done in main.cpp)
// ============================================================================

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

// ── POSIX implementation ────────────────────────────────────────────────────
#ifndef _WIN32

using SocketHandle = int;                        // file descriptor on POSIX
constexpr SocketHandle kInvalidSocket = -1;      // sentinel for failed socket()
#ifdef MSG_NOSIGNAL
constexpr int kSocketSendFlags = MSG_NOSIGNAL;   // prevent SIGPIPE on broken connections (Linux)
#else
constexpr int kSocketSendFlags = 0;              // macOS: no MSG_NOSIGNAL, use SO_NOSIGPIPE instead
#endif

// Close a socket file descriptor. No-op for invalid handles.
inline void closeSocket(SocketHandle socketHandle) {
  if (socketHandle >= 0) {
    close(socketHandle);
  }
}

// Set FD_CLOEXEC so child processes (ffmpeg) don't inherit this socket.
// Without this, spawned ffmpeg processes would hold the socket open,
// preventing rebind on restart.
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

// Create a socket, bind to a port, optionally listen.
// type = SOCK_STREAM for TCP (OSC, Companion), SOCK_DGRAM for UDP (Art-Net).
// localOnly = true binds to 127.0.0.1 (localhost only), false binds to all interfaces.
// SO_REUSEADDR allows quick rebind after restart without TIME_WAIT delay.
inline SocketHandle createBoundSocket(int type, int port, bool shouldListen, bool localOnly = true) {
  SocketHandle socketHandle = socket(AF_INET, type, 0);
  if (socketHandle < 0) {
    return kInvalidSocket;
  }
  setCloseOnExec(socketHandle);

  int reuse = 1;
  setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(localOnly ? INADDR_LOOPBACK : INADDR_ANY);
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

// Create an unbound UDP socket. enableBroadcast=true sets SO_BROADCAST
// for Art-Net broadcast discovery.
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

// Convert a sockaddr_in to a dotted-decimal IP string (e.g. "192.168.1.42").
// Used for logging and displaying the source address of incoming packets.
inline std::string socketAddressToString(const sockaddr_in& address) {
  char buffer[INET_ADDRSTRLEN] = {};
  const char* rendered = inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer));
  if (!rendered) {
    return "";
  }
  return std::string(rendered);
}

// ── Windows (Winsock2) implementation ───────────────────────────────────────
#else // _WIN32

using SocketHandle = uintptr_t;                                    // SOCKET cast to uintptr_t
constexpr SocketHandle kInvalidSocket = ~static_cast<uintptr_t>(0); // INVALID_SOCKET equivalent
constexpr int kSocketSendFlags = 0;                                 // no MSG_NOSIGNAL on Windows

// Close a Winsock socket handle.
inline void closeSocket(SocketHandle socketHandle) {
  if (socketHandle != kInvalidSocket) {
    closesocket(static_cast<SOCKET>(socketHandle));
  }
}

// No-op on Windows — sockets are not inherited by child processes by default.
inline void setCloseOnExec(SocketHandle /*socketHandle*/) {
}

// Create a socket, bind to a port, optionally listen (Windows Winsock2).
// localOnly = true binds to 127.0.0.1 (localhost only), false binds to all interfaces.
inline SocketHandle createBoundSocket(int type, int port, bool shouldListen, bool localOnly = true) {
  SOCKET s = ::socket(AF_INET, type, 0);
  if (s == INVALID_SOCKET) {
    return kInvalidSocket;
  }

  // SO_EXCLUSIVEADDRUSE prevents other processes from binding to the same port,
  // blocking port-hijacking attacks. Preferred over SO_REUSEADDR on Windows.
  int exclusive = 1;
  setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
             reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(localOnly ? INADDR_LOOPBACK : INADDR_ANY);
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

// Create an unbound UDP socket (Windows Winsock2).
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

// ── Cross-platform helpers ─────────────────────────────────────────────────

// Compute the nfds argument for select().
// POSIX requires the highest fd + 1; Windows ignores this parameter.
inline int selectNfds(SocketHandle maxHandle) {
#ifdef _WIN32
  (void)maxHandle;
  return 0;
#else
  return static_cast<int>(maxHandle) + 1;
#endif
}

} // namespace deckboy::platform
