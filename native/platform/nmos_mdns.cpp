// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// In-house DNS-SD browse client: RFC 6762 sections 5.1/6.7 and RFC 6763.
#include "nmos_mdns.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "network.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>

namespace deckboy::platform::video::nmos_mdns {
namespace {
constexpr std::uint16_t kA = 1, kPtr = 12, kTxt = 16, kSrv = 33;
constexpr std::size_t kMaxRecords = 512;

std::string dnsName(const std::string& dotted) {
  std::string wire;
  for (std::size_t begin = 0; begin < dotted.size();) {
    const auto dot = dotted.find('.', begin);
    const auto end = dot == std::string::npos ? dotted.size() : dot;
    wire += static_cast<char>(end - begin);
    wire.append(dotted, begin, end - begin);
    begin = end + 1;
  }
  wire += '\0';
  return wire;
}
const std::array<std::string, 2> kServices = {
  dnsName("_nmos-register._tcp.local"), dnsName("_nmos-registration._tcp.local")
};
bool isService(const std::string& name) {
  return std::find(kServices.begin(), kServices.end(), name) != kServices.end();
}
std::uint16_t u16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}
std::uint32_t u32(const std::uint8_t* p) {
  return (static_cast<std::uint32_t>(u16(p)) << 16) | u16(p + 2);
}
void put16(Packet& p, std::uint16_t value) {
  p.push_back(static_cast<std::uint8_t>(value >> 8));
  p.push_back(static_cast<std::uint8_t>(value));
}
char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; }

// Bounds cover both the enclosing RDATA and any compressed suffix elsewhere
// in the datagram. Backward-only pointers plus a hop cap reject cycles.
bool readName(const std::uint8_t* bytes, std::size_t size, std::size_t& pos,
              std::size_t end, std::string& name) {
  name.clear();
  std::size_t cursor = pos, limit = end;
  bool jumped = false;
  for (int hops = 0; hops < 128; ++hops) {
    if (cursor >= limit) return false;
    const auto length = bytes[cursor++];
    if ((length & 0xc0) == 0xc0) {
      if (cursor >= limit) return false;
      const std::size_t target = ((length & 0x3f) << 8) | bytes[cursor++];
      if (target < 12 || target >= cursor - 2) return false;
      if (!jumped) pos = cursor;
      jumped = true;
      cursor = target;
      limit = size;
      continue;
    }
    if ((length & 0xc0) || cursor + length > limit || name.size() + length + 1 > 255) return false;
    name += static_cast<char>(length);
    for (unsigned i = 0; i < length; ++i) name += lower(static_cast<char>(bytes[cursor++]));
    if (!jumped) pos = cursor;
    if (!length) return true;
  }
  return false;
}
std::string hostname(const std::string& wire) {
  std::string out;
  for (std::size_t pos = 0; pos < wire.size();) {
    const auto length = static_cast<unsigned char>(wire[pos++]);
    if (!length) return out;
    if (pos + length > wire.size()) return {};
    if (!out.empty()) out += '.';
    for (unsigned i = 0; i < length; ++i) {
      const char c = wire[pos++];
      // An SRV target must be a host, never an instance label, URL or header.
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return {};
      out += c;
    }
  }
  return {};
}
bool versionSupported(const std::string& versions) {
  for (std::size_t begin = 0; begin < versions.size();) {
    const auto comma = versions.find(',', begin);
    const auto end = comma == std::string::npos ? versions.size() : comma;
    std::string token = versions.substr(begin, end - begin);
    const auto first = token.find_first_not_of(" \t");
    if (first != std::string::npos) {
      token = token.substr(first, token.find_last_not_of(" \t") - first + 1);
      if (token == kApiVersion) return true;
    }
    begin = end + 1;
  }
  return false;
}
bool priorityValue(const std::string& value, int& result) {
  if (value.empty()) return false;
  result = 0;
  for (char c : value) {
    if (c < '0' || c > '9' || result > (INT_MAX - (c - '0')) / 10) return false;
    result = result * 10 + (c - '0');
  }
  return true;
}

std::vector<in_addr> interfaces(const std::string& specified) {
  std::vector<in_addr> result;
  auto add = [&](in_addr address) {
    if (std::none_of(result.begin(), result.end(), [&](in_addr old) { return old.s_addr == address.s_addr; }))
      result.push_back(address);
  };
  if (!specified.empty()) {
    in_addr address {};
    if (inet_pton(AF_INET, specified.c_str(), &address) == 1) add(address);
    return result;
  }
#ifdef _WIN32
  ULONG size = 16384;
  std::vector<unsigned char> bytes(size);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());
  ULONG status = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
      GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &size);
  if (status == ERROR_BUFFER_OVERFLOW) {
    bytes.resize(size);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());
    status = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
        GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &size);
  }
  if (status != NO_ERROR) return result;
  for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
        (adapter->Flags & IP_ADAPTER_NO_MULTICAST)) continue;
    for (auto* entry = adapter->FirstUnicastAddress; entry; entry = entry->Next) {
      if (entry->Address.lpSockaddr && entry->Address.lpSockaddr->sa_family == AF_INET)
        add(reinterpret_cast<sockaddr_in*>(entry->Address.lpSockaddr)->sin_addr);
    }
  }
#else
  ifaddrs* list = nullptr;
  if (getifaddrs(&list) != 0) return result;
  for (const auto* entry = list; entry; entry = entry->ifa_next) {
    if (entry->ifa_addr && entry->ifa_addr->sa_family == AF_INET &&
        (entry->ifa_flags & IFF_UP) && (entry->ifa_flags & IFF_MULTICAST) &&
        !(entry->ifa_flags & IFF_LOOPBACK))
      add(reinterpret_cast<sockaddr_in*>(entry->ifa_addr)->sin_addr);
  }
  freeifaddrs(list);
#endif
  return result;
}
}  // namespace

std::string Registry::url() const { return "http://" + host + ':' + std::to_string(port); }

Packet query(const Question& question, std::uint16_t id) {
  Packet p(12, 0);
  p[0] = static_cast<std::uint8_t>(id >> 8); p[1] = static_cast<std::uint8_t>(id);
  p[5] = 1;
  p.insert(p.end(), question.name.begin(), question.name.end());
  put16(p, question.type); put16(p, 1);
  return p;
}

void Cache::expire(Clock::time_point now) {
  records_.erase(std::remove_if(records_.begin(), records_.end(),
      [&](const Record& r) { return r.expires <= now; }), records_.end());
}

bool Cache::ingest(const std::uint8_t* bytes, std::size_t size, std::uint16_t id, Clock::time_point now) {
  if (size < 12 || size > 9000 || u16(bytes) != id || (u16(bytes + 2) & 0xf80f) != 0x8000) return false;
  const unsigned questions = u16(bytes + 4);
  const unsigned count = u16(bytes + 6) + u16(bytes + 8) + u16(bytes + 10);
  if (questions > 64 || count > kMaxRecords) return false;
  std::size_t pos = 12;
  std::string ignored;
  for (unsigned i = 0; i < questions; ++i) {
    if (!readName(bytes, size, pos, size, ignored) || pos + 4 > size) return false;
    pos += 4;
  }
  std::vector<Record> parsed;
  for (unsigned i = 0; i < count; ++i) {
    Record r;
    if (!readName(bytes, size, pos, size, r.name) || pos + 10 > size) return false;
    r.type = u16(bytes + pos);
    const auto cls = u16(bytes + pos + 2) & 0x7fff;
    const auto ttl = u32(bytes + pos + 4);
    const auto length = u16(bytes + pos + 8);
    pos += 10;
    const auto end = pos + length;
    if (end > size) return false;
    r.expires = now + std::chrono::seconds(std::min<std::uint32_t>(ttl, 10));
    if (cls != 1) { pos = end; continue; }
    if (r.type == kPtr || r.type == kSrv) {
      if (r.type == kSrv) {
        if (length < 7) return false;
        r.port = u16(bytes + pos + 4);
        pos += 6;
      }
      if (!readName(bytes, size, pos, end, r.target) || pos != end) return false;
    } else if (r.type == kTxt) {
      while (pos < end) {
        const auto len = bytes[pos++];
        if (pos + len > end) return false;
        const std::string item(reinterpret_cast<const char*>(bytes + pos), len);
        pos += len;
        const auto equal = item.find('=');
        auto key = item.substr(0, equal);
        std::transform(key.begin(), key.end(), key.begin(), lower);
        // RFC 6763: first occurrence wins; keys are case insensitive.
        r.txt.emplace(key, equal == std::string::npos ? std::string() : item.substr(equal + 1));
      }
    } else if (r.type == kA) {
      if (length != 4) return false;
      sockaddr_in addr {};
      std::memcpy(&addr.sin_addr, bytes + pos, 4);
      const auto host = ntohl(addr.sin_addr.s_addr);
      if (host != 0 && (host >> 24) < 224) r.address = socketAddressToString(addr);
      pos = end;
    } else { pos = end; continue; }
    parsed.push_back(std::move(r));
  }
  // Reject a malformed packet as a whole; never retain its valid prefix.
  expire(now);
  for (auto& r : parsed) {
    auto same = [&](const Record& old) {
      return old.name == r.name && old.type == r.type &&
          (r.type != kPtr || old.target == r.target) &&
          (r.type != kA || old.address == r.address);
    };
    records_.erase(std::remove_if(records_.begin(), records_.end(), same), records_.end());
    if (r.expires > now && records_.size() < kMaxRecords) records_.push_back(std::move(r));
  }
  return true;
}

std::vector<Question> Cache::questions(Clock::time_point now) {
  expire(now);
  std::vector<Question> result;
  for (const auto& service : kServices) result.push_back({service, kPtr});
  for (const auto& r : records_) {
    if (r.type != kPtr || !isService(r.name)) continue;
    result.push_back({r.target, kSrv});
    result.push_back({r.target, kTxt});
    for (const auto& srv : records_)
      if (srv.type == kSrv && srv.name == r.target && !hostname(srv.target).empty())
        result.push_back({srv.target, kA});
  }
  return result;
}

std::vector<Registry> Cache::registries(Clock::time_point now) {
  expire(now);
  std::vector<Registry> result;
  for (const auto& ptr : records_) {
    if (ptr.type != kPtr || !isService(ptr.name)) continue;
    const Record* srv = nullptr;
    const Record* txt = nullptr;
    for (const auto& r : records_) {
      if (r.name != ptr.target) continue;
      if (r.type == kSrv) srv = &r;
      if (r.type == kTxt) txt = &r;
    }
    if (!srv || !txt || !srv->port) continue;
    auto value = [&](const char* key) {
      const auto it = txt->txt.find(key);
      return it == txt->txt.end() ? std::string() : it->second;
    };
    int priority = 0;
    if (value("api_proto") != "http" || !versionSupported(value("api_ver")) ||
        value("api_auth") != "false" || !priorityValue(value("pri"), priority)) continue;
    const auto host = hostname(srv->target);
    if (host.empty()) continue;
    for (const auto& r : records_) {
      if (r.type == kA && r.name == srv->target && !r.address.empty())
        result.push_back({host, r.address, srv->port, priority});
    }
  }
  return result;
}

void order(std::vector<Registry>& registries, std::mt19937& random) {
  // Duplicate advertisements and multi-address hosts must not get extra votes.
  // Shuffle first to choose an address without preferring DNS response order.
  std::shuffle(registries.begin(), registries.end(), random);
  std::stable_sort(registries.begin(), registries.end(), [](const Registry& a, const Registry& b) {
    if (a.url() != b.url()) return a.url() < b.url();
    return a.priority < b.priority;
  });
  registries.erase(std::unique(registries.begin(), registries.end(), [](const Registry& a, const Registry& b) {
    return a.url() == b.url();
  }), registries.end());
  std::shuffle(registries.begin(), registries.end(), random);
  std::stable_sort(registries.begin(), registries.end(), [](const Registry& a, const Registry& b) {
    return a.priority < b.priority;
  });
}

std::vector<Registry> discover(const std::atomic<bool>& stop, std::string& error,
                              const std::string& interfaceAddress, std::chrono::milliseconds window) {
  struct Channel {
    SocketHandle socket = kInvalidSocket;
    Cache cache;
    std::map<std::pair<std::string, std::uint16_t>, Clock::time_point> sent;
  };
  std::vector<Channel> channels;
  // All sockets belong to this stack frame, including early cancellation.
  struct Cleanup {
    std::vector<Channel>& channels;
    ~Cleanup() { for (auto& c : channels) closeSocket(c.socket); }
  } cleanup {channels};
  error.clear();
  for (const auto address : interfaces(interfaceAddress)) {
    if (channels.size() >= 32 || stop.load()) break;
    const auto sock = createDatagramSocket();
    if (sock == kInvalidSocket) continue;
    sockaddr_in local {};
    local.sin_family = AF_INET;
    local.sin_addr = address;
#ifdef _WIN32
    const DWORD ttl = 255;
#else
    const unsigned char ttl = 255;
#endif
    if (::bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0 ||
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&address), sizeof(address)) != 0 ||
        setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof(ttl)) != 0) {
      closeSocket(sock); continue;
    }
    channels.push_back({sock, {}, {}});
  }
  if (channels.empty()) { error = "NMOS: mDNS has no usable IPv4 interface"; return {}; }
  sockaddr_in destination {};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(5353);
  inet_pton(AF_INET, "224.0.0.251", &destination.sin_addr);
  std::mt19937 random(std::random_device{}());
  const auto id = static_cast<std::uint16_t>(std::uniform_int_distribution<int>(1, 65535)(random));
  const auto deadline = Clock::now() + window;
  std::array<std::uint8_t, 9000> bytes {};
  while (!stop.load() && Clock::now() < deadline) {
    fd_set reads;
    FD_ZERO(&reads);
    SocketHandle maxFd = 0;
    bool watched = false;
    for (auto& channel : channels) {
      const auto now = Clock::now();
      for (const auto& q : channel.cache.questions(now)) {
        const auto key = std::make_pair(q.name, q.type);
        const auto previous = channel.sent.find(key);
        if (previous != channel.sent.end() && now - previous->second < std::chrono::seconds(1)) continue;
        if (channel.sent.size() >= kMaxRecords && previous == channel.sent.end()) continue;
        const auto packet = query(q, id);
        ::sendto(channel.socket, reinterpret_cast<const char*>(packet.data()), static_cast<int>(packet.size()), 0,
                 reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
        channel.sent[key] = now;
      }
      watched = watchFd(channel.socket, &reads, maxFd) || watched;
    }
    if (!watched) break;
    timeval timeout {}; timeout.tv_usec = 50000;
    if (::select(selectNfds(maxFd), &reads, nullptr, nullptr, &timeout) <= 0) continue;
    for (auto& channel : channels) {
      if (!readyFd(channel.socket, &reads)) continue;
      sockaddr_in source {};
      socklen_t size = sizeof(source);
      const int length = ::recvfrom(channel.socket, reinterpret_cast<char*>(bytes.data()), static_cast<int>(bytes.size()),
                                    0, reinterpret_cast<sockaddr*>(&source), &size);
      if (length > 0 && source.sin_port == htons(5353))
        channel.cache.ingest(bytes.data(), static_cast<std::size_t>(length), id, Clock::now());
    }
  }
  std::vector<Registry> result;
  if (stop.load()) return result;
  for (auto& channel : channels) {
    auto found = channel.cache.registries(Clock::now());
    result.insert(result.end(), found.begin(), found.end());
  }
  order(result, random);
  if (result.empty()) error = "NMOS: searching mDNS for an HTTP v1.3 registry (no authorization)";
  return result;
}
}  // namespace deckboy::platform::video::nmos_mdns
