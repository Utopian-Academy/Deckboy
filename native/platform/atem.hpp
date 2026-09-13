// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// atem.hpp — the two things called "ATEM".
//
// They are not the same thing and an operator means the second one.
//
//   AtemTallyBridge  A UDP port that accepts text somebody else took the
//                    trouble to send us -- a Companion button, a script, a
//                    third machine. Useful, and a command bridge, not a
//                    switcher connection.
//
//   (to follow)      A client that speaks the switcher's own protocol, so the
//                    switcher putting Deckboy on program rolls the clip with
//                    no middleman and nothing to configure elsewhere.
//
// This file is the bridge. The client lives beside it and is extracted
// separately, because its session handshake and packet parsing are a larger
// piece of work than a listener that forwards a line of text.
// ============================================================================

#ifndef DECKBOY_PLATFORM_ATEM_HPP
#define DECKBOY_PLATFORM_ATEM_HPP

#include <array>
#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "core/utils.hpp"
#include "platform/network.hpp"

namespace deckboy::platform {

// Receives a line of text on a UDP port and hands it to the app as a remote
// command. Deliberately unfiltered by sender: unlike the NMC bridge, which
// chases one nominated clock and must ignore every other, this exists to be
// addressed by whatever the operator has pointed at it.
class AtemTallyBridge {
 public:
  AtemTallyBridge() = default;
  ~AtemTallyBridge() { stop(); }
  AtemTallyBridge(const AtemTallyBridge&) = delete;
  AtemTallyBridge& operator=(const AtemTallyBridge&) = delete;

  // Called on the LISTENER THREAD, so it must queue rather than edit.
  void setEventSink(std::function<void(const std::string&)> sink) {
    sink_ = std::move(sink);
  }

  bool start(int port, bool localOnly) {
    stop();
    port_ = port;
    socket_ = createBoundSocket(SOCK_DGRAM, port_, false, localOnly);
    if (socket_ == kInvalidSocket) {
      return false;
    }
    stopFlag_.store(false);
    thread_ = std::thread([this]() { loop(); });
    return true;
  }

  void stop() {
    stopFlag_.store(true);
    if (socket_ != kInvalidSocket) {
      closeSocket(socket_);
      socket_ = kInvalidSocket;
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  // The settings screen shows the port it actually bound, and the project
  // state asks whether it is up before starting it again. Those two questions
  // are why this is not entirely opaque.
  bool running() const { return socket_ != kInvalidSocket; }
  int port() const { return port_; }

 private:
  void loop() {
    while (!stopFlag_.load()) {
      fd_set readFds;
      FD_ZERO(&readFds);
      watchFd(socket_, &readFds);
      timeval timeout {};
      timeout.tv_sec = 0;
      timeout.tv_usec = 200000;
      const int ready = select(selectNfds(socket_), &readFds, nullptr, nullptr, &timeout);
      if (ready <= 0 || !readyFd(socket_, &readFds)) {
        continue;
      }
      std::array<char, 1024> buffer {};
      sockaddr_in sourceAddr {};
      socklen_t sourceLen = sizeof(sourceAddr);
      const int bytes = recvfrom(socket_, buffer.data(),
                                 static_cast<int>(buffer.size() - 1), 0,
                                 reinterpret_cast<sockaddr*>(&sourceAddr), &sourceLen);
      if (bytes <= 0) {
        continue;
      }
      buffer[bytes] = '\0';
      const std::string payload = deckboy::core::utils::trim(
        std::string(buffer.data(), static_cast<std::size_t>(bytes)));
      if (payload.empty()) {
        continue;
      }
      if (sink_) {
        sink_("ATEMEVENT " + payload);
      }
    }
  }

  std::function<void(const std::string&)> sink_;
  SocketHandle socket_ = kInvalidSocket;
  std::thread thread_;
  std::atomic<bool> stopFlag_ {false};
  int port_ = 0;
};

}  // namespace deckboy::platform

#endif  // DECKBOY_PLATFORM_ATEM_HPP
