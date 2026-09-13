// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// artnet_bridge.hpp — Art-Net DMX in, cue triggers out.
//
// A lighting desk sends DMX over the network; this listens for ArtDMX packets
// and turns channel movement into remote commands, so a cue can be fired from
// a lighting cue stack.
//
// TWO KINDS OF CHANNEL, and the difference is deliberate:
//
//   1-8   are TRIGGERS. They fire on the rising edge through a threshold, so a
//         desk holding a channel at full does not re-fire every packet -- and a
//         desk that fades up through the threshold fires once, where it crosses.
//   9-10  are VALUES. They fire on any change to a non-zero level, because what
//         they carry is a number (a cue id, a level) rather than a press.
//
// Both need the PREVIOUS frame to decide, which is the only state here worth
// the name: an Art-Net source sends continuously whether anything changed or
// not, so without it every packet would be an event.
// ============================================================================

#ifndef DECKBOY_PLATFORM_ARTNET_BRIDGE_HPP
#define DECKBOY_PLATFORM_ARTNET_BRIDGE_HPP

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include "platform/network.hpp"

namespace deckboy::platform {

class ArtNetBridge {
 public:
  // A channel has to reach this to count as pressed: 127, the app's long-
  // standing kDmxTriggerThreshold, kept exactly. The comparison is `>=`, so a
  // desk sitting at precisely 127 DOES fire -- writing 128 here instead would
  // have moved the edge by one step and quietly stopped that desk working.
  static constexpr std::uint8_t kTriggerThreshold = 127;

  ArtNetBridge() = default;
  ~ArtNetBridge() { stop(); }
  ArtNetBridge(const ArtNetBridge&) = delete;
  ArtNetBridge& operator=(const ArtNetBridge&) = delete;

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
    lastDmx_.fill(0);
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

  // Stop and start again. A bare start() would already do that, but the
  // not-currently-running case is worth naming: restarting something that was
  // never up is how a bridge ends up with two threads.
  bool restart(int port, bool localOnly) { return start(port, localOnly); }

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
      std::array<std::uint8_t, 1024> packet {};
      sockaddr_in sourceAddr {};
      socklen_t sourceLen = sizeof(sourceAddr);
      const int bytes = recvfrom(socket_, reinterpret_cast<char*>(packet.data()),
                                 static_cast<int>(packet.size()), 0,
                                 reinterpret_cast<sockaddr*>(&sourceAddr), &sourceLen);
      // 18 bytes is the ArtDMX header; anything shorter cannot carry a slot.
      if (bytes < 18 || std::memcmp(packet.data(), "Art-Net\0", 8) != 0) {
        continue;
      }
      const std::uint16_t opCode = static_cast<std::uint16_t>(packet[8]) |
                                   (static_cast<std::uint16_t>(packet[9]) << 8);
      if (opCode != 0x5000) {   // ArtDMX
        continue;
      }
      int length = (static_cast<int>(packet[16]) << 8) | static_cast<int>(packet[17]);
      length = std::clamp(length, 0, std::min(512, bytes - 18));
      const std::uint8_t* data = packet.data() + 18;

      for (int ch = 0; ch < 8; ++ch) {          // triggers: rising edge only
        const std::uint8_t previous = lastDmx_[static_cast<std::size_t>(ch)];
        const std::uint8_t current = ch < length ? data[ch] : 0;
        if (previous < kTriggerThreshold && current >= kTriggerThreshold) {
          emit(ch, current);
        }
        lastDmx_[static_cast<std::size_t>(ch)] = current;
      }
      for (int ch = 8; ch < 10; ++ch) {         // values: any change, non-zero
        const std::uint8_t previous = lastDmx_[static_cast<std::size_t>(ch)];
        const std::uint8_t current = ch < length ? data[ch] : 0;
        if (current > 0 && current != previous) {
          emit(ch, current);
        }
        lastDmx_[static_cast<std::size_t>(ch)] = current;
      }
    }
  }

  void emit(int channelIndex, std::uint8_t value) {
    if (sink_) {
      sink_("ARTNETEVENT " + std::to_string(channelIndex + 1) + " " +
            std::to_string(static_cast<int>(value)));
    }
  }

  std::function<void(const std::string&)> sink_;
  SocketHandle socket_ = kInvalidSocket;
  std::thread thread_;
  std::atomic<bool> stopFlag_ {false};
  int port_ = 0;
  std::array<std::uint8_t, 512> lastDmx_ {};
};

}  // namespace deckboy::platform

#endif  // DECKBOY_PLATFORM_ARTNET_BRIDGE_HPP
