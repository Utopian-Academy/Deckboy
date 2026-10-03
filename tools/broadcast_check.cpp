// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// Deterministic transport, clock and pixel tests against production code.
#include "platform/network.hpp"
#include "platform/ptp_client.hpp"
#include "platform/st2110_output.hpp"
#include "platform/nmos_node.hpp"
#include "platform/decklink.hpp"
#include "core/audio_effects.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

namespace deckboy::platform::video {
struct BroadcastChecks {
  int failures = 0;
  void expect(bool ok, const char* message) {
    std::cout << (ok ? "[ok] " : "[fail] ") << message << '\n';
    if (!ok) ++failures;
  }
  using Packet = std::vector<std::uint8_t>;
  static Packet packet(int type, int source, int size) {
    Packet p(size, 0);
    p[0] = static_cast<std::uint8_t>(type); p[1] = 2;
    p[2] = static_cast<std::uint8_t>(size >> 8); p[3] = static_cast<std::uint8_t>(size);
    p[20] = static_cast<std::uint8_t>(source); p[29] = 1; p[31] = 7;
    return p;
  }
  static void timestamp(Packet& p, std::uint64_t time) {
    const auto seconds = time / 1000000000ull, nanos = time % 1000000000ull;
    for (int i = 0; i < 6; ++i) p[34+i] = static_cast<std::uint8_t>(seconds >> ((5-i)*8));
    for (int i = 0; i < 4; ++i) p[40+i] = static_cast<std::uint8_t>(nanos >> ((3-i)*8));
  }
  static void announce(PtpClient& clock, int master = 1) {
    auto p = packet(11, 1, 64); p[53] = static_cast<std::uint8_t>(master);
    clock.handleGeneralPacket(p.data(), p.size());
  }
  void clocks() {
    PtpClient clock;
    announce(clock);
    constexpr std::uint64_t now = 1000000000000000ull;
    auto sync = packet(0, 1, 44); timestamp(sync, now + 37000000000ull);
    for (int i = 0; i < 4; ++i) clock.handleEventPacket(sync.data(), sync.size(), now);
    expect(clock.locked() && clock.offsetNanos() == 37000000000ll, "TAI offset acquires lock");
    timestamp(sync, now + 37000100000ull);
    clock.handleEventPacket(sync.data(), sync.size(), now);
    expect(clock.offsetNanos() > 37000000000ll, "TAI offset continues updating after lock");
    announce(clock, 2);
    expect(!clock.locked() && !clock.haveOffset_, "grandmaster change clears old lock");
    sync[6] = 2;
    clock.handleEventPacket(sync.data(), sync.size(), now);
    auto follow = packet(8, 2, 44); timestamp(follow, now);
    clock.handleGeneralPacket(follow.data(), follow.size());
    expect(clock.awaitingFollowUp_ && !clock.haveOffset_, "wrong-source Follow_Up rejected");
    follow[20] = 1;
    clock.handleGeneralPacket(follow.data(), follow.size());
    expect(!clock.awaitingFollowUp_ && clock.haveOffset_, "selected-source Follow_Up accepted");
    clock.awaitingDelayResp_ = true; clock.delayRequestSteadyNanos_ = clock.lastSyncSteadyNanos_;
    clock.expireExchanges(clock.lastSyncSteadyNanos_ + 3000000001ull);
    expect(!clock.locked() && !clock.awaitingDelayResp_, "lost Sync and Delay_Resp expire");
    clock.expireExchanges(clock.lastAnnounceSteadyNanos_ + 6000000001ull);
    expect(!clock.haveSource_ && clock.grandmasterIdentity().empty(), "lost Announce releases selected master");
    // Every truncated length and a bounded random corpus exercise the actual
    // packet handlers. CI instruments this target with ASan and UBSan.
    for (std::size_t size = 0; size < 64; ++size) {
      auto p = packet(11, 1, 64);
      clock.handleGeneralPacket(p.data(), size);
      p[0] = 0;
      clock.handleEventPacket(p.data(), size, now);
    }
    PtpClient malformed;
    std::uint32_t random = 0x12345678;
    Packet bytes(96);
    for (int trial = 0; trial < 4096; ++trial) {
      for (auto& byte : bytes) {
        random = random * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(random >> 24);
      }
      const std::size_t size = trial % bytes.size();
      malformed.handleGeneralPacket(bytes.data(), size);
      malformed.handleEventPacket(bytes.data(), size, now);
    }
    expect(!malformed.haveSource_ && !malformed.locked(), "malformed PTP corpus does not acquire a clock");
    announce(malformed);
    for (int trial = 0; trial < 1024; ++trial) {
      auto p = packet(0, 1, 44);
      for (int i = 8; i < 16; ++i) {
        random = random * 1664525u + 1013904223u;
        p[i] = static_cast<std::uint8_t>(random >> 24);
      }
      timestamp(p, now);
      std::fill(p.begin() + 34, p.begin() + 40, 255); // unrepresentable seconds
      malformed.handleEventPacket(p.data(), p.size(), now);
    }
    expect(!malformed.haveOffset_, "selected-source PTP rejects unrepresentable timestamps");
    auto negative = packet(0, 1, 44);
    timestamp(negative, now + 37000000000ull);
    std::fill(negative.begin() + 8, negative.begin() + 14, 255); // -1 ns, scaled
    malformed.handleEventPacket(negative.data(), negative.size(), now);
    expect(malformed.offsetNanos() == 36999999999ll, "negative PTP correction uses signed wire value");
  }
  void nmos() {
    NmosNode node;
    NmosSenderInfo target; target.key = "video";
    target.destinationAddress = "239.20.10.1"; target.destinationPort = 20000;
    target.sourceAddress = "127.0.0.1"; target.sourcePort = 21000;
    target.masterEnabled = true;
    bool delivered = false;
    node.setPatchHandler([&](const NmosSenderPatch& patch) {
      delivered = patch.sourceChanged && patch.sourcePort == 21002 &&
        patch.rtpEnabledChanged && !patch.rtpEnabled;
      return true;
    });
    bool activated = false; std::string error;
    const int status = node.applyStagedPatch(target,
      R"({"transport_params":[{"source_port":21002,"rtp_enabled":false}],"activation":{"mode":"activate_immediate"}})",
      activated, error);
    expect(status == 200 && activated && delivered, "NMOS carries source port and RTP enable to application");
    const int invalid = node.applyStagedPatch(target,
      R"({"transport_params":[{"source_port":70000}]})", activated, error);
    expect(invalid == 400, "NMOS rejects invalid transport ports");
    node.setPatchHandler([](const NmosSenderPatch&) { return false; });
    expect(node.applyStagedPatch(target,
      R"({"master_enable":false,"activation":{"mode":"activate_immediate"}})",
      activated, error) >= 400, "NMOS failed activation is not acknowledged as success");
  }
  void pixels() {
    std::vector<std::uint8_t> source(8 * 4, 255), output(128 + 16, 0xCD);
    expect(deckLinkConvertBgra(source.data(), 8, 1, 32, output.data(), 8, 1, 128, true), "SDI v210 conversion succeeds");
    auto word = [&](int i) {
      const auto* p = output.data() + i*4;
      return static_cast<std::uint32_t>(p[0]) | (std::uint32_t(p[1])<<8) |
        (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24);
    };
    expect((word(0)&1023) == 512 && ((word(0)>>10)&1023) == 940 &&
      ((word(0)>>20)&1023) == 512 && (word(1)&1023) == 940, "SDI white has BT.709 studio code values");
    expect(output[127] == 0 && output[128] == 0xCD, "SDI partial groups fill padding without overwriting guard");
    std::fill(source.begin(), source.end(), 0);
    deckLinkConvertBgra(source.data(), 8, 1, 32, output.data(), 8, 1, 128, true);
    expect(((word(0)>>10)&1023) == 64, "SDI black has legal ten-bit luma");
    expect(!deckLinkConvertBgra(source.data(), 8, 1, 32, output.data(), 7, 1, 128, true), "SDI refuses incomplete chroma pairs");
  }
  void rtp() {
    using namespace deckboy::platform;
#ifdef _WIN32
    WSADATA wsa {};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { expect(false, "RTP socket runtime"); return; }
#endif
    SocketHandle receiver = createBoundSocket(SOCK_DGRAM, 0, false);
    sockaddr_in address {}; socklen_t length = sizeof(address);
    if (receiver == kInvalidSocket || getsockname(receiver, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
      expect(false, "RTP loopback receiver"); closeSocket(receiver); return;
    }
    PtpClient clock;
    St2110AudioConfig config; config.destinationAddress = "127.0.0.1";
    config.destinationPort = ntohs(address.sin_port); config.interfaceAddress = "127.0.0.1";
    St2110AudioOutput sender; sender.setPtpClient(&clock);
    expect(sender.open(config), "RTP audio opens and binds its requested interface");
    std::vector<std::int16_t> samples(960, -1234);
    sender.pushSamples(samples.data(), samples.size()/2);
    auto receive = [&]() {
      Packet p(512); fd_set ready; FD_ZERO(&ready); FD_SET(receiver, &ready);
      timeval timeout {1, 0};
      if (select(selectNfds(receiver), &ready, nullptr, nullptr, &timeout) <= 0) return Packet{};
      const int bytes = recv(receiver, reinterpret_cast<char*>(p.data()), static_cast<int>(p.size()), 0);
      if (bytes <= 0) return Packet{};
      p.resize(bytes); return p;
    };
    auto first = receive();
    expect(first.size() == 300 && first[12] == 0xFB && first[13] == 0x2E && first[14] == 0,
      "RTP emits stereo L24 with correct negative-sample widening");
    auto readTimestamp = [](const Packet& p) {
      return p.size() >= 12 ? (std::uint32_t(p[4])<<24) | (std::uint32_t(p[5])<<16) |
        (std::uint32_t(p[6])<<8) | p[7] : 0u;
    };
    clock.offsetNanos_.store(37000000000ll); clock.locked_.store(true);
    bool reseeded = false;
    for (int i = 0; i < 100 && !reseeded; ++i) {
      const auto p = receive();
      if (p.empty()) break;
      const auto delta = static_cast<std::uint32_t>(readTimestamp(p) - readTimestamp(first));
      reseeded = (p[1] & 0x80) && delta > 1700000 && delta < 1900000;
    }
    expect(reseeded, "RTP audio reseeds to a subsequently acquired TAI clock");
    sender.close(); expect(!sender.isOpen(), "RTP audio joins its sender before closing");
    closeSocket(receiver);
#ifdef _WIN32
    WSACleanup();
#endif
  }
  void audio() {
    deckboy::audiofx::TruePeak4x detector;
    double peak = 0.0;
    for (int i = 0; i < 4800; ++i) {
      const double sample = std::sin(i * 0.5 * 3.141592653589793 + 0.25 * 3.141592653589793);
      peak = std::max(peak, detector.push(sample, sample));
    }
    expect(peak > 0.95, "four-times detector catches peaks above sample maxima");
    deckboy::audiofx::ProgramAudioState state;
    std::vector<std::int32_t> chunk(9600);
    for (int i = 0; i < 4800; ++i) {
      const auto sample = static_cast<std::int32_t>(std::lround(50000.0 *
        std::sin(i * 0.5 * 3.141592653589793 + 0.25 * 3.141592653589793)));
      chunk[i*2] = chunk[i*2+1] = sample;
    }
    for (int i = 0; i < 40; ++i) state.process(chunk);
    expect(state.meter.peakDb() < -1.0, "final programme mix protects inter-sample peaks after summing");
    std::uint32_t noise = 1234567;
    for (int block = 0; block < 100; ++block) {
      chunk.resize(static_cast<std::size_t>(2 * (13 + block * 37 % 1500)));
      for (auto& sample : chunk) {
        noise = noise * 1664525u + 1013904223u;
        sample = block % 7 == 0 ? 0 : static_cast<std::int32_t>(noise % 180001) - 90000;
      }
      state.process(chunk);
    }
    expect(state.meter.peakDb() < -1.0, "programme peak ceiling survives changing chunks, gain and silence");
    deckboy::audiofx::ProgramLoudnessMeter meter;
    std::vector<std::int16_t> sine(9600);
    for (int i = 0; i < 4800; ++i) sine[i*2] = sine[i*2+1] =
      static_cast<std::int16_t>(std::lround(3276.8 * std::sin(i * 2.0 * 3.141592653589793 / 48.0)));
    for (int i = 0; i < 40; ++i) meter.push(sine);
    expect(std::fabs(meter.integrated + 20.0) < 0.15 && std::fabs(meter.shortTerm + 20.0) < 0.15,
      "BS.1770 stereo 1 kHz reference measures -20 LUFS");
    std::fill(sine.begin(), sine.end(), 0);
    for (int i = 0; i < 40; ++i) meter.push(sine);
    expect(std::fabs(meter.integrated + 20.0) < 0.3, "integrated gate excludes sustained silence");
  }
  int run() { clocks(); nmos(); pixels(); audio(); rtp(); return failures ? 1 : 0; }
};
}
int main() { return deckboy::platform::video::BroadcastChecks{}.run(); }
