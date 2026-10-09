// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// Production parser, selection and UDP loopback checks; no SDL or dependencies.
#include "platform/nmos_mdns.hpp"
#include "platform/network.hpp"
#include "platform/nmos_node.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <set>
#include <thread>

using namespace deckboy::platform;
using namespace deckboy::platform::video;
namespace mdns = deckboy::platform::video::nmos_mdns;
namespace {
using mdns::Packet;
constexpr std::uint16_t kId = 0x4251;
int failures = 0;
void expect(bool ok, const char* message) {
  std::cout << (ok ? "[ok] " : "[FAIL] ") << message << '\n';
  if (!ok) ++failures;
}
std::string name(std::initializer_list<const char*> labels) {
  std::string wire;
  for (const char* label : labels) { wire += static_cast<char>(std::strlen(label)); wire += label; }
  wire += '\0';
  return wire;
}
const auto service = name({"_nmos-register", "_tcp", "local"});
const auto legacyService = name({"_nmos-registration", "_tcp", "local"});
const auto instance = name({"Test.registry", "_nmos-register", "_tcp", "local"});
const auto legacyInstance = name({"Old registry", "_nmos-registration", "_tcp", "local"});
const auto host = name({"test-registry", "local"});

struct Message {
  Packet p = Packet(12, 0);
  std::map<std::string, std::size_t> offsets;
  bool legacyReply = false;
  Message() { p[0] = kId >> 8; p[1] = kId & 255; p[2] = 0x84; }
  void u16(std::uint16_t value) { p.push_back(static_cast<std::uint8_t>(value >> 8)); p.push_back(static_cast<std::uint8_t>(value)); }
  void dns(const std::string& wire) {
    for (std::size_t pos = 0; pos < wire.size();) {
      auto known = offsets.find(wire.substr(pos));
      if (known != offsets.end()) { u16(static_cast<std::uint16_t>(0xc000 | known->second)); return; }
      offsets[wire.substr(pos)] = p.size();
      const auto length = static_cast<unsigned char>(wire[pos]);
      p.insert(p.end(), wire.begin() + pos, wire.begin() + pos + length + 1);
      pos += length + 1;
      if (!length) return;
    }
  }
  template<class Write> void rr(const std::string& owner, std::uint16_t type, std::uint32_t ttl, Write write) {
    dns(owner); u16(type); u16(legacyReply ? 1 : 0x8001);
    u16(static_cast<std::uint16_t>(ttl >> 16)); u16(static_cast<std::uint16_t>(ttl));
    const auto len = p.size(); u16(0);
    write();
    const auto size = p.size() - len - 2;
    p[len] = static_cast<std::uint8_t>(size >> 8); p[len + 1] = static_cast<std::uint8_t>(size);
    ++p[7];
  }
  void txt(const std::string& value) {
    p.push_back(static_cast<std::uint8_t>(value.size()));
    p.insert(p.end(), value.begin(), value.end());
  }
};
Message advertisement(const std::string& priority = "10", const std::string& versions = "v1.2,v1.3",
                      const std::string& protocol = "http", const std::string& auth = "false",
                      unsigned mask = 15, std::uint32_t ttl = 10, bool legacy = false, std::uint16_t port = 8010,
                      const Packet& question = {}) {
  Message m;
  if (!question.empty()) {
    // RFC 6762 section 6.7: echo the question and ID, use class IN without
    // cache-flush, and do not compress the SRV target in a legacy reply.
    m.legacyReply = true;
    m.p[0] = question[0]; m.p[1] = question[1]; m.p[5] = 1;
    m.p.insert(m.p.end(), question.begin() + 12, question.end());
  }
  const auto& browse = legacy ? legacyService : service;
  const auto& inst = legacy ? legacyInstance : instance;
  if (mask & 1) m.rr(browse, 12, ttl, [&] { m.dns(inst); });
  if (mask & 2) m.rr(inst, 33, ttl, [&] {
    m.u16(100); m.u16(999); m.u16(port);
    if (m.legacyReply) m.p.insert(m.p.end(), host.begin(), host.end()); else m.dns(host);
  });
  if (mask & 4) m.rr(inst, 16, ttl, [&] {
    m.txt("API_PROTO=" + protocol); m.txt("api_ver=" + versions);
    if (!auth.empty()) m.txt("api_auth=" + auth);
    m.txt("pri=" + priority);
  });
  if (mask & 8) m.rr(host, 1, ttl, [&] { m.p.insert(m.p.end(), {127, 0, 0, 1}); });
  return m;
}
bool ingest(mdns::Cache& cache, const Packet& p, mdns::Clock::time_point now) {
  return cache.ingest(p.data(), p.size(), kId, now);
}

void packets() {
  const auto now = mdns::Clock::now();
  mdns::Cache cache;
  auto full = advertisement();
  // One answer with three additional records, all using DNS name compression.
  full.p[7] = 1; full.p[11] = 3;
  expect(ingest(cache, full.p, now), "compressed answer and additional records parse");
  auto found = cache.registries(now);
  expect(found.size() == 1 && found[0].url() == "http://test-registry.local:8010" &&
         found[0].address == "127.0.0.1" && found[0].priority == 10,
         "PTR/SRV/TXT/A assemble; TXT priority overrides SRV priority");
  expect(cache.registries(now + std::chrono::seconds(11)).empty(), "expired records cannot be selected");
  mdns::Cache echoed;
  auto reply = advertisement("10", "v1.3", "http", "false", 15, 10, false, 8010,
                             mdns::query({service, 12}, kId));
  expect(ingest(echoed, reply.p, now) && echoed.registries(now).size() == 1,
         "legacy unicast response with echoed question and uncompressed SRV target");
  mdns::Cache split;
  for (unsigned mask : {8u, 4u, 1u, 2u}) {
    auto p = advertisement("1", "v1.3", "http", "false", mask);
    expect(ingest(split, p.p, now), "split/out-of-order DNS records parse");
  }
  expect(split.registries(now).size() == 1, "split records assemble independent of packet order");
  const auto pending = split.questions(now);
  expect(std::any_of(pending.begin(), pending.end(), [](const mdns::Question& q) {
    return q.type == 33 && q.name == name({"test.registry", "_nmos-register", "_tcp", "local"});
  }), "instance label containing a dot stays one DNS label");
  auto goodbye = advertisement("1", "v1.3", "http", "false", 1, 0);
  ingest(split, goodbye.p, now);
  expect(split.registries(now).empty(), "TTL-zero PTR withdraws the registry");
  mdns::Cache old;
  auto legacy = advertisement("100", "v1.3", "http", "false", 15, 10, true);
  ingest(old, legacy.p, now);
  expect(old.registries(now).size() == 1 && old.registries(now)[0].priority == 100,
         "legacy service name and development priority are supported");

  for (const auto& p : {
      advertisement("-1"), advertisement("1x"), advertisement("999999999999"), advertisement(""),
      advertisement("1", "v1.30"), advertisement("1", "v1.2"),
      advertisement("1", "v1.3", "https"), advertisement("1", "v1.3", "http", "true"),
      advertisement("1", "v1.3", "http", ""), advertisement("1", "v1.3", "http", "FALSE"),
      advertisement("1", "v1.3", "http", "false", 15, 10, false, 0),
      advertisement("1", "v1.3", "http", "false", 7)}) {
    mdns::Cache incompatible;
    ingest(incompatible, p.p, now);
    expect(incompatible.registries(now).empty(), "incompatible or incomplete registry excluded");
  }
  auto malformed = full.p;
  bool allRejected = true;
  for (std::size_t size = 0; size < full.p.size(); ++size) {
    mdns::Cache truncated;
    if (truncated.ingest(full.p.data(), size, kId, now) || !truncated.registries(now).empty()) allRejected = false;
  }
  expect(allRejected, "every truncated packet rejected atomically");
  for (const std::array<std::uint8_t, 2> pointer : {std::array<std::uint8_t, 2>{0xc0, 12}, {0xff, 255}, {0x80, 0}}) {
    malformed = full.p; malformed[12] = pointer[0]; malformed[13] = pointer[1];
    mdns::Cache bad;
    expect(!ingest(bad, malformed, now), "cyclic/out-of-range/reserved DNS name encoding rejected");
  }
  mdns::Cache wrong;
  expect(!wrong.ingest(full.p.data(), full.p.size(), kId + 1, now), "wrong DNS transaction ignored");
  malformed = full.p; malformed[2] = 0;
  expect(!ingest(wrong, malformed, now), "query packets do not populate response cache");
  malformed = full.p; malformed[3] = 3;
  expect(!ingest(wrong, malformed, now), "DNS error responses ignored");
  // Deterministic malformed corpus, useful under the target's ASan/UBSan mode.
  std::mt19937 random(12345);
  for (int trial = 0; trial < 10000; ++trial) {
    malformed = full.p;
    for (int mutation = 0; mutation < 4; ++mutation)
      malformed[random() % malformed.size()] = static_cast<std::uint8_t>(random());
    mdns::Cache fuzz;
    ingest(fuzz, malformed, now);
    fuzz.questions(now); fuzz.registries(now);
  }
  expect(true, "10,000 mutated DNS packets processed without crash");
  std::set<std::string> winners;
  bool ordering = true;
  for (int trial = 0; trial < 200; ++trial) {
    std::vector<mdns::Registry> candidates {
      {"dev.local", "127.0.0.1", 8010, 100}, {"a.local", "127.0.0.1", 8010, 5},
      {"b.local", "127.0.0.1", 8010, 5}, {"a.local", "127.0.0.1", 8010, 5},
      {"a.local", "127.0.0.2", 8010, 5}
    };
    mdns::order(candidates, random);
    ordering = ordering && candidates.size() == 3 && candidates.front().priority == 5 && candidates.back().priority == 100;
    winners.insert(candidates.front().host);
  }
  expect(ordering && winners.size() == 2, "priority sorting, randomized ties and duplicate/multi-address deduplication");
}

void loopback(bool legacy) {
  const auto sock = createDatagramSocket();
  expect(sock != kInvalidSocket, "loopback responder socket created");
  if (sock == kInvalidSocket) return;
  struct Close { SocketHandle socket; ~Close() { closeSocket(socket); } } close {sock};
  const int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#ifdef SO_REUSEPORT
  setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif
  sockaddr_in local {}; local.sin_family = AF_INET; local.sin_port = htons(5353);
  const bool bound = ::bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == 0;
  expect(bound, "loopback responder bound to UDP 5353");
  if (!bound) {
#ifdef _WIN32
    std::cerr << "UDP 5353 bind failed: Winsock " << WSAGetLastError() << '\n';
#else
    std::cerr << "UDP 5353 bind failed: " << std::strerror(errno) << '\n';
#endif
    return;
  }
  ip_mreq membership {};
  inet_pton(AF_INET, "224.0.0.251", &membership.imr_multiaddr);
  inet_pton(AF_INET, "127.0.0.1", &membership.imr_interface);
  const bool joined = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
      reinterpret_cast<const char*>(&membership), sizeof(membership)) == 0;
  expect(joined, "loopback responder joined mDNS multicast group");
  if (!joined) return;
  std::atomic<bool> done {false};
  std::atomic<int> answered {0};
  std::thread responder([&] {
    std::array<std::uint8_t, 9000> buffer {};
    while (!done.load()) {
      fd_set fds; FD_ZERO(&fds); watchFd(sock, &fds);
      timeval timeout {}; timeout.tv_usec = 50000;
      if (::select(selectNfds(sock), &fds, nullptr, nullptr, &timeout) <= 0) continue;
      sockaddr_in sender {}; socklen_t len = sizeof(sender);
      const int count = ::recvfrom(sock, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()),
                                    0, reinterpret_cast<sockaddr*>(&sender), &len);
      if (count < 17 || sender.sin_addr.s_addr != htonl(INADDR_LOOPBACK) || (buffer[2] & 0x80)) continue;
      // The production query has one uncompressed question. Answer only that
      // record, forcing the browser to issue SRV, TXT and A follow-ups.
      const unsigned type = (buffer[count - 4] << 8) | buffer[count - 3];
      const unsigned mask = type == 12 ? 1 : type == 33 ? 2 : type == 16 ? 4 : type == 1 ? 8 : 0;
      if (!mask) continue;
      const auto asked = std::string(reinterpret_cast<char*>(buffer.data() + 12), count - 16);
      auto foldedInstance = legacy ? legacyInstance : instance;
      std::transform(foldedInstance.begin(), foldedInstance.end(), foldedInstance.begin(),
                     [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; });
      const auto expected = type == 12 ? (legacy ? legacyService : service) : type == 1 ? host : foldedInstance;
      if (asked != expected) continue;
      auto reply = advertisement("7", "v1.3", "http", "false", mask, 10, legacy, 8010,
                                 Packet(buffer.begin(), buffer.begin() + count));
      ::sendto(sock, reinterpret_cast<const char*>(reply.p.data()), static_cast<int>(reply.p.size()), 0,
               reinterpret_cast<sockaddr*>(&sender), len);
      answered.fetch_add(1);
    }
  });
  std::atomic<bool> stop {false};
  std::string error;
  auto found = mdns::discover(stop, error, "127.0.0.1");
  done.store(true); responder.join();
  expect(answered.load() >= 4 && found.size() == 1 && found[0].url() == "http://test-registry.local:8010",
         legacy ? "legacy service: real loopback query and PTR/SRV/TXT/A resolution"
                : "current service: real loopback query and PTR/SRV/TXT/A resolution");
  std::cout << "loopback replies=" << answered.load() << " registry="
            << (found.empty() ? "none" : found[0].url()) << '\n';
  if (found.empty()) std::cerr << error << '\n';
  const auto begin = mdns::Clock::now();
  std::thread cancel([&] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); stop.store(true); });
  mdns::discover(stop, error, "127.0.0.1", std::chrono::seconds(5));
  cancel.join();
  expect(mdns::Clock::now() - begin < std::chrono::seconds(1), "discovery cancellation closes sockets promptly");
}

void registration() {
  // A small HTTP fixture on loopback, not an AMWA registry. Exercise the real
  // registration thread, including 404 recovery and a peer that stops replying.
  const auto listener = createBoundSocket(SOCK_STREAM, 0, true);
  const auto nodePortProbe = createBoundSocket(SOCK_STREAM, 0, true);
  struct Close {
    SocketHandle socket;
    ~Close() { closeSocket(socket); }
  } closeListener {listener};
  sockaddr_in endpoint {}, nodeEndpoint {};
  socklen_t length = sizeof(endpoint), nodeLength = sizeof(nodeEndpoint);
  const bool socketsReady = listener != kInvalidSocket && nodePortProbe != kInvalidSocket &&
    getsockname(listener, reinterpret_cast<sockaddr*>(&endpoint), &length) == 0 &&
    getsockname(nodePortProbe, reinterpret_cast<sockaddr*>(&nodeEndpoint), &nodeLength) == 0;
  closeSocket(nodePortProbe);
  expect(socketsReady, "HTTP fixture and node use available local ports");
  if (!socketsReady) return;
  std::atomic<bool> done {false}, hang {false}, hanging {false};
  std::atomic<int> resources {0}, health {0};
  std::thread server([&] {
    while (!done.load()) {
      fd_set reads; FD_ZERO(&reads); watchFd(listener, &reads);
      timeval timeout {}; timeout.tv_usec = 50000;
      if (::select(selectNfds(listener), &reads, nullptr, nullptr, &timeout) <= 0) continue;
      const auto client = ::accept(listener, nullptr, nullptr);
      if (client == kInvalidSocket) continue;
      Close closeClient {client};
      std::string request;
      const auto deadline = mdns::Clock::now() + std::chrono::seconds(2);
      while (!done.load() && mdns::Clock::now() < deadline && request.find("\r\n\r\n") == std::string::npos) {
        FD_ZERO(&reads); watchFd(client, &reads);
        timeout.tv_sec = 0; timeout.tv_usec = 50000;
        if (::select(selectNfds(client), &reads, nullptr, nullptr, &timeout) <= 0) continue;
        std::array<char, 4096> buffer {};
        const int count = ::recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (count <= 0) break;
        request.append(buffer.data(), static_cast<std::size_t>(count));
      }
      if (hang.load()) {
        hanging.store(true);
        while (!done.load()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      const bool heartbeat = request.find("/health/nodes/") != std::string::npos;
      const bool forgotten = heartbeat && health.fetch_add(1) == 0;
      if (!heartbeat) resources.fetch_add(1);
      const std::string response = std::string("HTTP/1.1 ") +
        (forgotten ? "404 Not Found" : heartbeat ? "200 OK" : "201 Created") +
        "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
#ifdef SO_NOSIGPIPE
      const int noSignal = 1;
      setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal));
#endif
      ::send(client, response.data(), static_cast<int>(response.size()), kSocketSendFlags);
    }
  });
  auto waitFor = [](auto ready, int milliseconds) {
    const auto until = mdns::Clock::now() + std::chrono::milliseconds(milliseconds);
    while (!ready() && mdns::Clock::now() < until)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return ready();
  };
  NmosNode node;
  NmosConfig config;
  config.enabled = true;
  config.hostAddress = "127.0.0.1";
  config.nodePort = ntohs(nodeEndpoint.sin_port);
  config.registryUrl = "http://127.0.0.1:" + std::to_string(ntohs(endpoint.sin_port));
  expect(node.start(config), "node starts with manual loopback registry");
  expect(waitFor([&] { return node.registered(); }, 2000) && node.registryUrl() == config.registryUrl &&
         node.discoveredRegistry().empty(), "manual URL wins and registers without a discovery result");
  const int initialResources = resources.load();
  expect(waitFor([&] { return node.heartbeatCount() > 0; }, 12000) &&
         resources.load() > initialResources && node.registered(), "heartbeat 404 re-registers at the same registry");
  node.stop();
  expect(node.registryUrl().empty() && !node.registered(), "stop clears effective registry state");
  config.allowRemote = false;
  config.hostAddress.clear();
  expect(node.start(config), "LOCAL ONLY node starts with a typed registry");
  expect(waitFor([&] { return node.registered(); }, 2000) &&
         node.registryUrl() == config.registryUrl && node.discoveredRegistry().empty(),
         "typed URL still registers with the remote network off");
  expect(waitFor([&] { return node.heartbeatCount() > 0; }, 6000),
         "typed URL still heartbeats with the remote network off");
  node.stop();
  const auto typedUrl = config.registryUrl;
  config.registryUrl.clear();
  const int beforeLocalOnly = resources.load();
  expect(node.start(config), "LOCAL ONLY node starts without a typed registry");
  waitFor([&] { return !node.lastError().empty(); }, 500);
  expect(node.registryUrl().empty() && resources.load() == beforeLocalOnly &&
         node.nodeApiUrl().find("http://127.0.0.1:") == 0 &&
         node.lastError() == "NMOS: discovery needs the remote network allowed",
         "LOCAL ONLY suppresses discovery and explains why; Node API stays on loopback");
  node.stop();
  config.allowRemote = true;
  config.registryUrl = typedUrl;
  config.hostAddress = "127.0.0.1";
  hang.store(true);
  expect(node.start(config) && waitFor([&] { return hanging.load(); }, 2000), "registry accepts a request but stops answering");
  const auto begin = mdns::Clock::now();
  node.stop();
  expect(mdns::Clock::now() - begin < std::chrono::seconds(1), "node shutdown cancels a stalled HTTP request");
  done.store(true);
  server.join();
}
}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  WSADATA data {};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
#endif
  if (argc > 1 && std::string(argv[1]) == "--loopback") { loopback(false); loopback(true); }
  else if (argc > 1 && std::string(argv[1]) == "--registration") registration();
  else packets();
#ifdef _WIN32
  WSACleanup();
#endif
  return failures ? 1 : 0;
}
