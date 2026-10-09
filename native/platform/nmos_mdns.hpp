// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace deckboy::platform::video::nmos_mdns {

// The registration client emits v1.3 resources; accepting a lower advertised
// version without translating those resources would be incorrect.
inline constexpr const char* kApiVersion = "v1.3";
using Clock = std::chrono::steady_clock;
using Packet = std::vector<std::uint8_t>;

struct Registry {
  std::string host;       // SRV target, for display and the HTTP Host header
  std::string address;    // A record, so .local never depends on the OS resolver
  std::uint16_t port = 0;
  int priority = 0;       // TXT pri, not SRV priority/weight
  std::string url() const;
};

// Packet/cache helpers are also used by the standalone regression check. DNS
// names stay in length-prefixed wire form, folded to ASCII lower case: a dot in
// an instance label must not become a label separator during a follow-up query.
struct Record {
  std::string name, target, address;
  std::map<std::string, std::string> txt;
  std::uint16_t type = 0, port = 0;
  Clock::time_point expires;
};
struct Question {
  std::string name;
  std::uint16_t type;
};
class Cache {
 public:
  bool ingest(const std::uint8_t* bytes, std::size_t size, std::uint16_t id,
              Clock::time_point now);
  std::vector<Question> questions(Clock::time_point now);
  std::vector<Registry> registries(Clock::time_point now);
 private:
  void expire(Clock::time_point now);
  std::vector<Record> records_;
};
Packet query(const Question& question, std::uint16_t id);
void order(std::vector<Registry>& registries, std::mt19937& random);

// RFC 6762 section 5.1 one-shot queries to 224.0.0.251:5353, from an ephemeral
// port on each active IPv4 interface. Section 6.7 replies are unicast, so this
// coexists with Bonjour/Avahi/Windows DNS without stealing their UDP 5353 port.
// Collect for a full browse window before choosing (never first-response wins).
// No thread or persistent socket: the NMOS registration thread owns this call.
// Windows caller owns WSAStartup/WSACleanup. stop is checked at most 50ms apart.
// interfaceAddress is empty in production; loopback is useful for the test.
std::vector<Registry> discover(const std::atomic<bool>& stop, std::string& error,
                              const std::string& interfaceAddress = {},
                              std::chrono::milliseconds window = std::chrono::milliseconds(2500));

}  // namespace deckboy::platform::video::nmos_mdns
