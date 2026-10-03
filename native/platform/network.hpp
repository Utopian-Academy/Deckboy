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
#include <cerrno>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
// netdb.h is getaddrinfo/addrinfo. Windows gets both from ws2tcpip.h below,
// so a caller that resolves a host compiles there and fails to on POSIX --
// which is how the NMC bridge broke Linux and nothing else.
#include <netdb.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#endif

namespace deckboy::platform {
inline std::string socketAddressToString(const sockaddr_in& address);
inline std::string ipv4AddressToString(const sockaddr_in& address) {
  const auto* octets = reinterpret_cast<const unsigned char*>(&address.sin_addr.s_addr);
  char buffer[16] {};
  const int written = std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u",
      static_cast<unsigned>(octets[0]), static_cast<unsigned>(octets[1]),
      static_cast<unsigned>(octets[2]), static_cast<unsigned>(octets[3]));
  return written > 0 && static_cast<std::size_t>(written) < sizeof(buffer)
      ? std::string(buffer) : std::string();
}
}

// Return an IPv4 address that a peer on the private LAN can use to reach this
// host. Do not infer it from the default route: VPNs commonly install a lower
// metric default route and that route's source address is unreachable to a
// phone on the Wi-Fi LAN. Only return an address on an active private
// interface; callers can bind the monitor to that address instead of exposing
// it on every VPN, tunnel, and virtual adapter.
namespace deckboy::platform {
inline std::string privateLanIPv4Address() {
#ifdef _WIN32
  ULONG bytes = 16 * 1024;
  std::vector<unsigned char> storage(bytes);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
  constexpr ULONG kAdapterFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                  GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
  ULONG result = GetAdaptersAddresses(AF_INET, kAdapterFlags,
      nullptr, adapters, &bytes);
  if (result == ERROR_BUFFER_OVERFLOW) {
    storage.resize(bytes);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    result = GetAdaptersAddresses(AF_INET, kAdapterFlags, nullptr, adapters, &bytes);
  }
  if (result != NO_ERROR) return {};

  std::string ethernet;
  std::string wifi;
  for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp ||
        (adapter->IfType != IF_TYPE_ETHERNET_CSMACD &&
         adapter->IfType != IF_TYPE_IEEE80211)) continue;
    const wchar_t* adapterLabel = adapter->FriendlyName ? adapter->FriendlyName : adapter->Description;
    std::wstring label = adapterLabel ? adapterLabel : L"";
    std::transform(label.begin(), label.end(), label.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    constexpr const wchar_t* kVirtualOrVpnLabels[] = {
      L"vpn", L"wireguard", L"wintun", L"tailscale", L"zerotier", L"zero tier",
      L"proton", L"openvpn", L"tap-windows", L"tunnel", L"virtualbox", L"hyper-v"
    };
    bool virtualOrVpn = false;
    for (const wchar_t* token : kVirtualOrVpnLabels) {
      if (label.find(token) != std::wstring::npos) { virtualOrVpn = true; break; }
    }
    if (virtualOrVpn) continue;
    bool hasGateway = false;
    for (auto* gateway = adapter->FirstGatewayAddress; gateway; gateway = gateway->Next) {
      if (!gateway->Address.lpSockaddr || gateway->Address.lpSockaddr->sa_family != AF_INET) continue;
      const auto* address = reinterpret_cast<const sockaddr_in*>(gateway->Address.lpSockaddr);
      if (address->sin_addr.s_addr != htonl(INADDR_ANY)) { hasGateway = true; break; }
    }
    if (!hasGateway) continue;
    for (auto* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
      if (!unicast->Address.lpSockaddr || unicast->Address.lpSockaddr->sa_family != AF_INET) continue;
      const auto* address = reinterpret_cast<const sockaddr_in*>(unicast->Address.lpSockaddr);
      const std::uint32_t host = ntohl(address->sin_addr.s_addr);
      const bool privateAddress = (host >> 24) == 10 ||
          (host >> 20) == 0xAC1 || (host >> 16) == 0xC0A8;
      if (!privateAddress) continue;
      const std::string found = deckboy::platform::ipv4AddressToString(*address);
      std::string& preferred = adapter->IfType == IF_TYPE_ETHERNET_CSMACD ? ethernet : wifi;
      if (preferred.empty()) preferred = found;
    }
  }
  return ethernet.empty() ? wifi : ethernet;
#else
  ifaddrs* interfaces = nullptr;
  if (getifaddrs(&interfaces) != 0 || !interfaces) return {};
  std::string ethernet;
  std::string wifi;
  for (const ifaddrs* entry = interfaces; entry; entry = entry->ifa_next) {
    if (!entry->ifa_name || !entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET ||
        (entry->ifa_flags & IFF_UP) == 0 ||
        (entry->ifa_flags & (IFF_LOOPBACK | IFF_POINTOPOINT)) != 0) continue;
    const std::string name(entry->ifa_name);
    const std::string lower = [&]() {
      std::string value = name;
      std::transform(value.begin(), value.end(), value.begin(),
          [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return value;
    }();
    if (lower.rfind("lo", 0) == 0 || lower.rfind("utun", 0) == 0 ||
        lower.rfind("tun", 0) == 0 || lower.rfind("tap", 0) == 0 ||
        lower.rfind("wg", 0) == 0 || lower.rfind("tailscale", 0) == 0 ||
        lower.rfind("zt", 0) == 0 || lower.rfind("ppp", 0) == 0 ||
        lower.rfind("docker", 0) == 0 || lower.rfind("veth", 0) == 0 ||
        lower.rfind("virbr", 0) == 0 || lower.rfind("vmnet", 0) == 0) continue;
    const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
    const std::uint32_t host = ntohl(address->sin_addr.s_addr);
    const bool privateAddress = (host >> 24) == 10 ||
        (host >> 20) == 0xAC1 || (host >> 16) == 0xC0A8;
    if (!privateAddress) continue;
    const std::string found = deckboy::platform::ipv4AddressToString(*address);
    const bool wiFi = lower.rfind("wl", 0) == 0 || lower.rfind("wlan", 0) == 0 ||
                      lower.rfind("wifi", 0) == 0 || lower.rfind("wi-fi", 0) == 0;
    const bool wired = lower.rfind("en", 0) == 0 || lower.rfind("eth", 0) == 0 ||
                       lower.rfind("em", 0) == 0 || lower.rfind("igb", 0) == 0 ||
                       lower.rfind("ix", 0) == 0 || lower.rfind("re", 0) == 0;
    if (!wiFi && !wired) continue;
    std::string& preferred = wiFi ? wifi : ethernet;
    if (preferred.empty()) preferred = found;
  }
  freeifaddrs(interfaces);
  return ethernet.empty() ? wifi : ethernet;
#endif
}

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
inline SocketHandle createBoundSocket(int type, int port, bool shouldListen, bool localOnly = true,
                                      const std::string& bindAddress = {}) {
  SocketHandle socketHandle = socket(AF_INET, type, 0);
  if (socketHandle < 0) {
    return kInvalidSocket;
  }
  setCloseOnExec(socketHandle);

  int reuse = 1;
  setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in address {};
  address.sin_family = AF_INET;
  if (localOnly) address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  else if (bindAddress.empty()) address.sin_addr.s_addr = htonl(INADDR_ANY);
  else if (inet_pton(AF_INET, bindAddress.c_str(), &address.sin_addr) != 1) {
    closeSocket(socketHandle);
    return kInvalidSocket;
  }
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
inline SocketHandle createBoundSocket(int type, int port, bool shouldListen, bool localOnly = true,
                                      const std::string& bindAddress = {}) {
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
  if (localOnly) address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  else if (bindAddress.empty()) address.sin_addr.s_addr = htonl(INADDR_ANY);
  else if (inet_pton(AF_INET, bindAddress.c_str(), &address.sin_addr) != 1) {
    closesocket(s);
    return kInvalidSocket;
  }
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

// FD_SET ABORTS THE PROCESS ON A BAD DESCRIPTOR.
//
// glibc's fortified FD_SET does not return an error for a descriptor that is
// negative or >= FD_SETSIZE -- it terminates the program:
//
//   *** bit out of range 0 - FD_SETSIZE on fd_set ***: terminated
//
// Shutdown closes these sockets and sets them to kInvalidSocket (-1 on
// POSIX), and the loops here kept handing them to FD_SET. So quitting
// Deckboy on Linux ended in "Aborted (core dumped)" rather than an exit --
// which also stalled the updater, whose helper waits for this process to go
// away cleanly.
//
// Every FD_SET in the app goes through here now. A socket that is not
// valid is simply not watched, which is what the old code meant to do.
// AND THE SAME FOR FD_ISSET.
//
// glibc fortifies FD_ISSET exactly as it does FD_SET, so testing a closed
// socket aborts the process just as surely as adding one. Guarding only the
// FD_SET side moved the crash rather than fixing it -- the loop skipped the
// invalid socket, then asked whether it was ready and died there instead.
inline bool readyFd(SocketHandle fd, const fd_set* set) {
  if (fd == kInvalidSocket) {
    return false;
  }
#ifndef _WIN32
  if (fd < 0 || fd >= static_cast<SocketHandle>(FD_SETSIZE)) {
    return false;
  }
#endif
  return FD_ISSET(fd, set) != 0;
}

inline bool watchFd(SocketHandle fd, fd_set* set, SocketHandle& maxFd) {
  if (fd == kInvalidSocket) {
    return false;
  }
#ifndef _WIN32
  if (fd < 0 || fd >= static_cast<SocketHandle>(FD_SETSIZE)) {
    return false;
  }
#endif
  FD_SET(fd, set);
  if (fd > maxFd) {
    maxFd = fd;
  }
  return true;
}

// Same guard where the caller sizes select() from the fd itself and keeps
// no running maximum.
inline bool watchFd(SocketHandle fd, fd_set* set) {
  SocketHandle ignored = 0;
  return watchFd(fd, set, ignored);
}

} // namespace deckboy::platform
