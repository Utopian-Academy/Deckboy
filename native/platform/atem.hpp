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
//   AtemSwitcherClient  A client that speaks the switcher's own protocol, so
//                       the switcher putting Deckboy on program rolls the clip
//                       with no middleman and nothing to configure elsewhere.
//                       READ-ONLY: it never sends the switcher a command,
//                       because a playout machine that can cut the show is a
//                       playout machine that will eventually cut the show by
//                       accident.
// ============================================================================

#ifndef DECKBOY_PLATFORM_ATEM_HPP
#define DECKBOY_PLATFORM_ATEM_HPP

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <vector>
#include <cstring>
#include <utility>
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


// ── The switcher client ─────────────────────────────────────────────────────
//
// Connects, is told the state of the world, and watches one number: which
// input is on the program bus. It knows which input is OURS so it can turn a
// change of that number into an on-air / off-air edge, and it emits nothing
// for a change that does not cross us.
//
// The host and our input are SNAPSHOT at start rather than read live, because
// the session runs on its own thread and must not touch the project while the
// main thread is editing it.
class AtemSwitcherClient {
 public:
  AtemSwitcherClient() = default;
  ~AtemSwitcherClient() { stop(); }
  AtemSwitcherClient(const AtemSwitcherClient&) = delete;
  AtemSwitcherClient& operator=(const AtemSwitcherClient&) = delete;

  // Called on the SESSION THREAD, so it must queue rather than edit.
  void setEventSink(std::function<void(const std::string&)> sink) {
    sink_ = std::move(sink);
  }

  // What the settings screen shows: whether the switcher answered, and the
  // input list it sent us so the operator can pick theirs by name.
  bool connected() const { return connected_.load(); }
  int program() const { return programInput_.load(); }

  // The operator can change which input is theirs while the client is
  // connected -- picking it from the list the switcher just sent. Live, so it
  // does not need a reconnect to take effect.
  void setOurInput(int input) { ourInput_.store(input); }

  // ── Talking to an actual ATEM ───────────────────────────────────────────
  //
  // Everything above named "ATEM" is a command bridge: a UDP port that accepts
  // text somebody else took the trouble to send us. Useful, and not the thing
  // an operator means by "trigger from the switcher". What they mean is that
  // the switcher put Deckboy on program and the clip should roll -- with no
  // middleman, no Companion, nothing to configure on a third machine.
  //
  // So this speaks the switcher's own protocol. It is a READ-ONLY client: it
  // connects, it is told the state of the world, and it watches one number --
  // which input is on the program bus. Deckboy never sends the switcher a
  // command, because a playout machine that can cut the show is a playout
  // machine that will eventually cut the show by accident.
  //
  // The protocol, only as much of it as this needs:
  //
  //   12-byte header, big endian:
  //     0-1  (flags << 11) | total length
  //     2-3  session id          6-9  unused
  //     4-5  acked packet id    10-11 our packet id
  //   flags: 0x01 wants-ack  0x02 hello  0x04 resend
  //          0x08 request-next  0x10 ack
  //
  //   after the header, a run of command blocks:
  //     0-1  block length, including these 8 bytes
  //     2-3  reserved      4-7  four-character name
  //     8..  payload
  //
  // The one this wants is PrgI: program input, as [ME, pad, source hi, lo].
  //
  // ACK EVERY PACKET THAT ASKS. A switcher that is not acked assumes the
  // client has gone and drops the session -- which presents as tally working
  // for about ten seconds and then never again.
  static constexpr int kAtemUdpPort = 9910;

  // A SESSION ID NOBODY ELSE IS ALREADY USING.
  //
  // The client proposes the id and the switcher keeps it, so a constant is a
  // collision waiting to happen: 0x1337 is the value in every published example
  // of this protocol, which means Deckboy, a second Deckboy, and any other tool
  // written from the same references would all introduce themselves as the same
  // session. The switcher then has one confused client instead of several
  // working ones -- observed on the bench, where a probe and a control script
  // both announced 0x1337 and commands were silently ignored.
  //
  // Kept inside the documented client range and away from 0.
  static unsigned atemFreshSessionId() {
    static std::atomic<unsigned> counter {0};
    const unsigned seed = static_cast<unsigned>(
      std::chrono::steady_clock::now().time_since_epoch().count());
    return 0x1000u + ((seed + counter.fetch_add(0x2D9u)) % 0x6000u);
  }

  void start(bool enabled, const std::string& switcherHost, int ourTallyInput) {
    stop();
    if (!enabled || switcherHost.empty()) {
      return;
    }
    // Snapshotted before the thread exists, so the thread never touches
    // project_ at all. See host_.
    host_ = switcherHost;
    ourInput_.store(ourTallyInput);
    stopFlag_.store(false);
    thread_ = std::thread([this]() { loop(); });
  }

  void stop() {
    stopFlag_.store(true);
    if (thread_.joinable()) {
      thread_.join();
    }
    connected_.store(false);
    programInput_.store(-1);
  }

  // Build the 12-byte header in place. Returns the header size.
  static std::size_t atemWriteHeader(unsigned char* out, unsigned flags,
                                     unsigned length, unsigned session,
                                     unsigned ackedId, unsigned packetId) {
    const unsigned first = (flags << 11) | (length & 0x07FFu);
    out[0] = static_cast<unsigned char>((first >> 8) & 0xFF);
    out[1] = static_cast<unsigned char>(first & 0xFF);
    out[2] = static_cast<unsigned char>((session >> 8) & 0xFF);
    out[3] = static_cast<unsigned char>(session & 0xFF);
    out[4] = static_cast<unsigned char>((ackedId >> 8) & 0xFF);
    out[5] = static_cast<unsigned char>(ackedId & 0xFF);
    out[6] = 0; out[7] = 0; out[8] = 0; out[9] = 0;
    out[10] = static_cast<unsigned char>((packetId >> 8) & 0xFF);
    out[11] = static_cast<unsigned char>(packetId & 0xFF);
    return 12;
  }

  void loop() {
    while (!stopFlag_.load()) {
      session();
      // A switcher that is off, or on another subnet, is an ordinary condition
      // on a show floor -- so this waits and tries again rather than giving up
      // for the run. Two seconds, in 100ms steps so shutdown stays prompt.
      for (int i = 0; i < 20 && !stopFlag_.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
  }

  // One session, start to finish. Returns when it ends, for any reason.
  bool session() {
    SocketHandle sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == kInvalidSocket) {
      return false;
    }
    sockaddr_in dest {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(static_cast<unsigned short>(kAtemUdpPort));
    if (inet_pton(AF_INET, host_.c_str(), &dest.sin_addr) != 1) {
      closeSocket(sock);
      return false;
    }

    unsigned session = atemFreshSessionId();
    unsigned char hello[20] {};
    atemWriteHeader(hello, 0x02, 20, session, 0, 0);
    hello[12] = 0x01;
    if (sendto(sock, reinterpret_cast<const char*>(hello), 20, 0,
               reinterpret_cast<sockaddr*>(&dest), sizeof(dest)) <= 0) {
      closeSocket(sock);
      return false;
    }

    bool greeted = false;
    auto lastHeard = std::chrono::steady_clock::now();
    std::array<unsigned char, 2048> buffer {};

    while (!stopFlag_.load()) {
      fd_set readFds;
      FD_ZERO(&readFds);
      watchFd(sock, &readFds);
      timeval timeout {};
      timeout.tv_sec = 0;
      timeout.tv_usec = 200000;
      const int ready = select(selectNfds(sock), &readFds, nullptr, nullptr, &timeout);
      if (ready < 0) break;
      if (ready == 0 || !readyFd(sock, &readFds)) {
        // FIVE SECONDS OF SILENCE IS A DEAD SESSION. A connected switcher
        // talks constantly, so a gap that long means the link or the box is
        // gone and the right move is to start over, not to sit here.
        if (std::chrono::steady_clock::now() - lastHeard > std::chrono::seconds(5)) {
          break;
        }
        continue;
      }
      sockaddr_in from {};
      socklen_t fromLen = sizeof(from);
      const int bytes = recvfrom(sock, reinterpret_cast<char*>(buffer.data()),
                                 static_cast<int>(buffer.size()), 0,
                                 reinterpret_cast<sockaddr*>(&from), &fromLen);
      if (bytes < 12) continue;
      lastHeard = std::chrono::steady_clock::now();

      const unsigned first = (static_cast<unsigned>(buffer[0]) << 8) | buffer[1];
      const unsigned flags = first >> 11;
      const unsigned length = first & 0x07FFu;
      const unsigned pktSession = (static_cast<unsigned>(buffer[2]) << 8) | buffer[3];
      const unsigned pktId = (static_cast<unsigned>(buffer[10]) << 8) | buffer[11];

      if ((flags & 0x02) != 0 && !greeted) {
        // NOT the session to use from here on, despite appearances. The hello
        // answer ECHOES the id we proposed -- so a client that adopts it and
        // stops looking is addressing a session the switcher has already left.
        // The real one arrives on the next packet, and is picked up below.
        //
        // Measured on an ATEM Mini Pro: we proposed 0x5B33, the greeting came
        // back 0x5B33, and every packet after it was 0x98E0. Acks sent to the
        // old id are ignored, which is survivable for a listener -- the
        // switcher keeps talking -- and fatal for anything that wants to be
        // heard.
        session = pktSession;
        greeted = true;
        connected_.store(true);
        unsigned char ack[12] {};
        atemWriteHeader(ack, 0x10, 12, session, 0, 0);
        sendto(sock, reinterpret_cast<const char*>(ack), 12, 0,
               reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        continue;
      }

      // THE SWITCHER'S OWN ID WINS, from the first packet that carries one.
      // See the note on the greeting above.
      if (greeted && pktSession != session) {
        session = pktSession;
      }

      if ((flags & 0x01) != 0) {
        unsigned char ack[12] {};
        atemWriteHeader(ack, 0x10, 12, session, pktId, 0);
        sendto(sock, reinterpret_cast<const char*>(ack), 12, 0,
               reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
      }

      const unsigned usable =
        std::min<unsigned>(length, static_cast<unsigned>(bytes));
      parseCommands(buffer.data(), usable);
    }

    closeSocket(sock);
    connected_.store(false);
    return greeted;
  }

  // WHAT THE SWITCHER CALLS ITS INPUTS.
  //
  // The ATEM sends an InPr block per source on connect: id, then a 20-byte long
  // name and a 4-byte short one, both NUL-padded. Without this an operator has
  // to know that Deckboy is "input 3" and type the 3; with it the picker says
  // what the switcher's own panel says, which is the only naming anybody in the
  // room has agreed on.
  //
  // Sources above 1000 are internal -- colour bars, media players, supersources,
  // the black and the aux buses. They cannot be a Deckboy, so they are not
  // offered as one.
  static void atemInputNamesFromPacket(const unsigned char* packet, unsigned length,
                                       std::map<int, std::string>& into) {
    unsigned offset = 12;
    while (offset + 8 <= length) {
      const unsigned blockLength =
        (static_cast<unsigned>(packet[offset]) << 8) | packet[offset + 1];
      if (blockLength < 8 || offset + blockLength > length) break;
      const char* name = reinterpret_cast<const char*>(packet + offset + 4);
      if (std::memcmp(name, "InPr", 4) == 0 && blockLength >= 8 + 22) {
        const unsigned char* data = packet + offset + 8;
        const int source = (static_cast<int>(data[0]) << 8) | data[1];
        if (source > 0 && source < 1000) {
          // NUL-padded and not guaranteed terminated: take at most 20.
          std::string label;
          for (int i = 0; i < 20 && data[2 + i] != 0; ++i) {
            label.push_back(static_cast<char>(data[2 + i]));
          }
          if (!label.empty()) into[source] = label;
        }
      }
      offset += blockLength;
    }
  }

  // The program input this packet reports, if it reports one at all.
  //
  // Static and side-effect free so --atem-probe can run the same parser the
  // live client runs. A probe that exercises a reimplementation of the thing
  // it is meant to be testing proves nothing about the thing.
  static std::optional<int> atemProgramInputFromPacket(const unsigned char* packet,
                                                       unsigned length) {
    std::optional<int> found;
    unsigned offset = 12;
    while (offset + 8 <= length) {
      const unsigned blockLength =
        (static_cast<unsigned>(packet[offset]) << 8) | packet[offset + 1];
      // A zero-length or oversized block would spin here forever or walk off
      // the end of the packet; both are things a malformed datagram can do.
      if (blockLength < 8 || offset + blockLength > length) break;
      const char* name = reinterpret_cast<const char*>(packet + offset + 4);
      if (std::memcmp(name, "PrgI", 4) == 0 && blockLength >= 12) {
        const unsigned char* data = packet + offset + 8;
        found = (static_cast<int>(data[2]) << 8) | data[3];
      }
      offset += blockLength;
    }
    return found;
  }

  void parseCommands(const unsigned char* packet, unsigned length) {
    {
      // Under a lock: the settings UI reads this from the main thread while the
      // switcher thread is still being told about sources.
      std::lock_guard<std::mutex> lock(inputNamesMutex_);
      atemInputNamesFromPacket(packet, length, inputNames_);
    }
    if (const auto source = atemProgramInputFromPacket(packet, length)) {
      programChanged(*source);
    }
  }

  // A snapshot for the UI, in switcher order.
  std::vector<std::pair<int, std::string>> inputList() {
    std::lock_guard<std::mutex> lock(inputNamesMutex_);
    return {inputNames_.begin(), inputNames_.end()};
  }

  enum class TallyEdge { None, OnAir, OffAir };

  // DOES THIS PROGRAM-BUS READING MEAN ANYTHING TO US.
  //
  // Pure, and separate from the socket, because it is the part with rules
  // rather than the part with I/O -- and the rules are the part that can be
  // wrong in a way nothing notices until a show. The switcher reports program
  // roughly once a second whether or not it changed, so the difference between
  // "state" and "event" is the whole feature.
  //
  //   previous < 0   we have only just been told: state, not a transition. A
  //                  client connecting while already on program must not fire
  //                  a take for something that was true before it arrived.
  //   ours <= 0      nobody has said which input Deckboy is. Guessing would
  //                  mean rolling on somebody else's camera.
  //   wasUs == isUs  the bus moved, but not across us. Not our business.
  static TallyEdge tallyEdge(int previous, int current, int ours) {
    if (ours <= 0) return TallyEdge::None;
    if (previous < 0) return TallyEdge::None;
    if (previous == current) return TallyEdge::None;
    const bool wasUs = previous == ours;
    const bool isUs = current == ours;
    if (wasUs == isUs) return TallyEdge::None;
    return isUs ? TallyEdge::OnAir : TallyEdge::OffAir;
  }

  // The program bus moved. Only a change that crosses OUR input is an event.
  void programChanged(int source) {
    const int previous = programInput_.exchange(source);
    switch (tallyEdge(previous, source, ourInput_.load())) {
      case TallyEdge::OnAir:  emit("TALLYEVENT ON ATEM"); break;
      case TallyEdge::OffAir: emit("TALLYEVENT OFF ATEM"); break;
      case TallyEdge::None:   break;
    }
  }

 private:
  void emit(const std::string& line) {
    if (sink_) {
      sink_(line);
    }
  }

  std::function<void(const std::string&)> sink_;
  std::thread thread_;
  std::atomic<bool> stopFlag_ {true};
  std::atomic<bool> connected_ {false};
  std::atomic<int> programInput_ {-1};
  std::atomic<int> ourInput_ {0};
  std::string host_;
  std::map<int, std::string> inputNames_;
  mutable std::mutex inputNamesMutex_;
};

}  // namespace deckboy::platform

#endif  // DECKBOY_PLATFORM_ATEM_HPP
