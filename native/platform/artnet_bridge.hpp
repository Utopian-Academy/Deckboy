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
#include <cstring>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <thread>

#include "platform/network.hpp"

namespace deckboy::platform {

// ── ART-NET OUT ─────────────────────────────────────────────────────────────
//
// The bridge above LISTENS for ArtDMX. This builds one, because a cue deck
// that can be told "house lights to 20%" and cannot say it is only half of a
// show-control system.
//
// THE PACKET IS A PURE FUNCTION, for the same reason the MIDI encoder is: it
// is the only part of this a machine can check without a lighting rig in the
// room, and a header one byte wrong is dropped by every node on the network
// while looking exactly like a cabling fault.
//
// ArtDMX, per the Art-Net 4 specification:
//
//   0   "Art-Net\0"          8 bytes, null terminated
//   8   OpCode 0x5000        LITTLE endian, so 0x00 0x50
//   10  ProtVer 14           BIG endian, so 0x00 0x0E
//   12  Sequence             1-255, 0 means "sequencing disabled"
//   13  Physical             informational only
//   14  SubUni               low byte of the 15-bit port address
//   15  Net                  high byte
//   16  Length               BIG endian, even, 2..512
//   18  Data                 Length bytes
// ---------------------------------------------------------------------------
// ── sACN OUT (ANSI E1.31) ──────────────────────────────────────────────────
//
// The other lighting network: what most consoles and nodes speak beside, or
// instead of, Art-Net. Same 512 slots; a different envelope. Also a pure
// function, checked byte for byte by the smoke test, for the reason above.
//
//   Root layer     0  preamble 0x0010, postamble 0x0000, "ASC-E1.17\0\0\0"
//                  16 flags+length (0x7000 | bytes from 16 to the end)
//                  18 vector 0x00000004 (E131 data), 22 CID (16 bytes)
//   Framing layer  38 flags+length (from 38), 40 vector 0x00000002,
//                  44 source name (64 bytes, NUL padded), 108 priority,
//                  109 sync address, 111 sequence, 112 options, 113 universe
//   DMP layer      115 flags+length (from 115), 117 vector 0x02, 118 0xA1,
//                  119 first address 0, 121 increment 1, 123 count (1 + slots),
//                  125 start code 0, 126 the slots
// Universes are 1-63999. Multicast goes to 239.255.<hi>.<lo>, port 5568.
inline constexpr int kSacnPort = 5568;

inline std::string sacnMulticastAddress(int universe) {
  const int u = universe < 1 ? 1 : (universe > 63999 ? 63999 : universe);
  return "239.255." + std::to_string((u >> 8) & 0xFF) + "." + std::to_string(u & 0xFF);
}

inline std::vector<std::uint8_t> buildSacnPacket(
    int universe, const std::vector<std::uint8_t>& channels, std::uint8_t sequence,
    const std::uint8_t (&cid)[16], const std::string& sourceName) {
  const std::size_t slots = channels.size() > 512 ? 512 : channels.size();
  const std::size_t total = 126 + slots;
  std::vector<std::uint8_t> p(total, 0);
  auto be16 = [&](std::size_t at, unsigned v) {
    p[at] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[at + 1] = static_cast<std::uint8_t>(v & 0xFF);
  };
  auto be32 = [&](std::size_t at, unsigned v) {
    be16(at, (v >> 16) & 0xFFFF);
    be16(at + 2, v & 0xFFFF);
  };
  be16(0, 0x0010);
  be16(2, 0x0000);
  static const char kId[12] = {'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0, 0, 0};
  for (int i = 0; i < 12; ++i) p[4 + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(kId[i]);
  be16(16, 0x7000u | static_cast<unsigned>(total - 16));
  be32(18, 0x00000004u);
  for (int i = 0; i < 16; ++i) p[22 + static_cast<std::size_t>(i)] = cid[i];
  be16(38, 0x7000u | static_cast<unsigned>(total - 38));
  be32(40, 0x00000002u);
  for (std::size_t i = 0; i < 63 && i < sourceName.size(); ++i) p[44 + i] = static_cast<std::uint8_t>(sourceName[i]);
  p[108] = 100;                                 // priority: the default
  be16(109, 0);                                 // no synchronisation
  p[111] = sequence;
  p[112] = 0;                                   // options
  const int u = universe < 1 ? 1 : (universe > 63999 ? 63999 : universe);
  be16(113, static_cast<unsigned>(u));
  be16(115, 0x7000u | static_cast<unsigned>(total - 115));
  p[117] = 0x02;
  p[118] = 0xA1;
  be16(119, 0x0000);
  be16(121, 0x0001);
  be16(123, static_cast<unsigned>(slots + 1));
  p[125] = 0x00;                                // the DMX start code
  for (std::size_t i = 0; i < slots; ++i) p[126 + i] = channels[i];
  return p;
}

inline std::vector<std::uint8_t> buildArtDmxPacket(
    int universe, const std::vector<std::uint8_t>& channels, std::uint8_t sequence) {
  std::vector<std::uint8_t> packet;
  packet.reserve(18 + 512);

  static const char kId[] = "Art-Net";
  for (int i = 0; i < 7; ++i) {
    packet.push_back(static_cast<std::uint8_t>(kId[i]));
  }
  packet.push_back(0);                                   // the terminator

  packet.push_back(0x00);                                // OpCode, little endian
  packet.push_back(0x50);
  packet.push_back(0x00);                                // ProtVer, big endian
  packet.push_back(0x0E);
  packet.push_back(sequence);
  packet.push_back(0x00);                                // Physical

  const int clampedUniverse = universe < 0 ? 0 : (universe > 32767 ? 32767 : universe);
  packet.push_back(static_cast<std::uint8_t>(clampedUniverse & 0xFF));
  packet.push_back(static_cast<std::uint8_t>((clampedUniverse >> 8) & 0xFF));

  // AT LEAST 2, AT MOST 512, AND ALWAYS EVEN. An odd length is legal to write
  // and illegal to read: nodes reject the packet outright, so a rig would go
  // dark with nothing in any log to say why.
  std::size_t length = channels.size();
  if (length < 2) {
    length = 2;
  }
  if (length > 512) {
    length = 512;
  }
  if ((length % 2) != 0) {
    ++length;
  }
  packet.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFF));   // big endian
  packet.push_back(static_cast<std::uint8_t>(length & 0xFF));

  for (std::size_t i = 0; i < length; ++i) {
    packet.push_back(i < channels.size() ? channels[i] : 0);
  }
  return packet;
}

// "1=255, 5=128, 10-14=64" into a sparse list of (1-based channel, value).
//
// Returns nothing at all when any part of it is malformed, rather than as much
// as it could read. A DMX line that half-applies is a lighting state nobody
// asked for, and on a show that is worse than one that plainly did not fire.
inline std::optional<std::vector<std::pair<int, std::uint8_t>>>
parseDmxChannelSpec(const std::string& spec) {
  std::vector<std::pair<int, std::uint8_t>> out;
  std::string token;
  std::stringstream stream(spec);
  while (std::getline(stream, token, ',')) {
    // trim
    std::size_t a = token.find_first_not_of(" \t");
    if (a == std::string::npos) {
      continue;                                          // an empty item is not a fault
    }
    std::size_t b = token.find_last_not_of(" \t");
    token = token.substr(a, b - a + 1);

    const std::size_t eq = token.find('=');
    if (eq == std::string::npos) {
      return std::nullopt;
    }
    const std::string left = token.substr(0, eq);
    const std::string right = token.substr(eq + 1);

    int value = 0;
    try {
      value = std::stoi(right);
    } catch (...) {
      return std::nullopt;
    }
    if (value < 0 || value > 255) {
      return std::nullopt;
    }

    const std::size_t dash = left.find('-');
    int first = 0;
    int last = 0;
    try {
      if (dash == std::string::npos) {
        first = last = std::stoi(left);
      } else {
        first = std::stoi(left.substr(0, dash));
        last = std::stoi(left.substr(dash + 1));
      }
    } catch (...) {
      return std::nullopt;
    }
    // CHANNELS ARE 1-BASED, as every lighting desk in the world counts them.
    if (first < 1 || last < first || last > 512) {
      return std::nullopt;
    }
    for (int channel = first; channel <= last; ++channel) {
      out.emplace_back(channel, static_cast<std::uint8_t>(value));
    }
  }
  return out;
}

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
