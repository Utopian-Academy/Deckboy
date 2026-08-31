// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.
//
// See nmos_node.hpp for scope, and in particular for the list of things this
// deliberately does NOT implement.

#include "nmos_node.hpp"

// network.hpp pulls in <winsock2.h>. Without these two, windows.h defines
// min/max as macros and every std::max in this file becomes a syntax error.
// CMake also defines NOMINMAX globally, but the sibling platform sources
// (st2110_output.cpp, ptp_client.cpp) self-define it so they stay compilable
// outside the CMake build; match that.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "network.hpp"

// network.hpp brings in sockets, but NOT name resolution. On Windows
// getaddrinfo/addrinfo arrive via <ws2tcpip.h>, which is why the registry
// client's hostname path compiled there and failed on clang with "unknown type
// name 'addrinfo'". POSIX puts them in <netdb.h>.
#ifndef _WIN32
#include <netdb.h>
#endif

#include <cctype>    // std::tolower  (header-scan on the request line)
#include <cstdlib>   // std::strtoul / std::strtod / std::atoi (JSON + HTTP parsing)

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>

namespace deckboy {
namespace platform {
namespace video {

using deckboy::platform::closeSocket;
using deckboy::platform::createBoundSocket;
using deckboy::platform::kInvalidSocket;
using deckboy::platform::kSocketSendFlags;
using deckboy::platform::selectNfds;
using deckboy::platform::SocketHandle;
using deckboy::platform::setCloseOnExec;

namespace {

// ── SHA-1, for UUIDv5 ───────────────────────────────────────────────────────
// RFC 3174. Present only to derive stable resource ids; it is not used for
// anything security-bearing and must not be repurposed for anything that is.
class Sha1 {
 public:
  Sha1() { reset(); }

  void reset() {
    state_[0] = 0x67452301u;
    state_[1] = 0xEFCDAB89u;
    state_[2] = 0x98BADCFEu;
    state_[3] = 0x10325476u;
    state_[4] = 0xC3D2E1F0u;
    bitCount_ = 0;
    bufferLength_ = 0;
  }

  void update(const std::uint8_t* data, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
      buffer_[bufferLength_++] = data[i];
      bitCount_ += 8;
      if (bufferLength_ == 64) {
        transform(buffer_.data());
        bufferLength_ = 0;
      }
    }
  }

  void finish(std::uint8_t digest[20]) {
    const std::uint64_t bits = bitCount_;
    std::uint8_t pad = 0x80;
    update(&pad, 1);
    pad = 0x00;
    while (bufferLength_ != 56) {
      update(&pad, 1);
    }
    std::uint8_t lengthBytes[8];
    for (int i = 0; i < 8; ++i) {
      lengthBytes[i] = static_cast<std::uint8_t>((bits >> (56 - 8 * i)) & 0xFF);
    }
    // Feed the length directly: update() would re-count these bits.
    for (int i = 0; i < 8; ++i) {
      buffer_[bufferLength_++] = lengthBytes[i];
    }
    transform(buffer_.data());
    bufferLength_ = 0;
    for (int i = 0; i < 5; ++i) {
      digest[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFF);
      digest[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFF);
      digest[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFF);
      digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xFF);
    }
  }

 private:
  static std::uint32_t rol(std::uint32_t value, int bits) {
    return (value << bits) | (value >> (32 - bits));
  }

  void transform(const std::uint8_t* chunk) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(chunk[i * 4 + 0]) << 24) |
             (static_cast<std::uint32_t>(chunk[i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(chunk[i * 4 + 2]) << 8) |
             static_cast<std::uint32_t>(chunk[i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) {
      w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];
    for (int i = 0; i < 80; ++i) {
      std::uint32_t f = 0;
      std::uint32_t k = 0;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      const std::uint32_t temp = rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = temp;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
  }

  std::array<std::uint8_t, 64> buffer_ {};
  std::uint32_t state_[5] {};
  std::uint64_t bitCount_ = 0;
  std::size_t bufferLength_ = 0;
};

// Fixed namespace UUID for Deckboy resources. Any random-but-constant v4 value
// is valid here; what matters is that it never changes, because changing it
// renames every sender in every controller that has us saved.
constexpr std::uint8_t kDeckboyNamespace[16] = {
  0x9f, 0x2c, 0x41, 0x88, 0x6b, 0x3d, 0x4e, 0x1a,
  0xb7, 0x55, 0x0c, 0xd8, 0x21, 0x64, 0xaf, 0x93,
};

std::string trimCopy(const std::string& value) {
  std::size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  std::size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string jsonEscape(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char raw : value) {
    const unsigned char c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
          out += buffer;
        } else {
          out += raw;
        }
        break;
    }
  }
  return out;
}

std::string jsonQuote(const std::string& value) {
  return "\"" + jsonEscape(value) + "\"";
}

// The IS-04 node schema types `hostname` as a hostname, so the operator's label
// cannot go in raw — "Deckboy Test" contains a space and a strict registry
// rejects the whole node for it. Fold to the RFC 1123 character set.
std::string hostnameSafe(const std::string& label) {
  std::string out;
  out.reserve(label.size());
  for (char raw : label) {
    const unsigned char c = static_cast<unsigned char>(raw);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
      out += raw;
    } else if (c >= 'A' && c <= 'Z') {
      out += static_cast<char>(c - 'A' + 'a');
    } else if (!out.empty() && out.back() != '-') {
      out += '-';
    }
  }
  while (!out.empty() && out.back() == '-') {
    out.pop_back();
  }
  return out.empty() ? std::string("deckboy") : out;
}

// Convert a frame rate to the exact rational NMOS expects. Broadcast rates are
// snapped to their canonical /1001 forms — emitting 59.94 as 5994/100 is the
// kind of thing that makes a receiver reject the flow.
void rationalFrameRate(double fps, int& numerator, int& denominator) {
  struct Known { double value; int num; int den; };
  static const Known table[] = {
    {24000.0 / 1001.0, 24000, 1001}, {24.0, 24, 1},
    {25.0, 25, 1},                   {30000.0 / 1001.0, 30000, 1001},
    {30.0, 30, 1},                   {50.0, 50, 1},
    {60000.0 / 1001.0, 60000, 1001}, {60.0, 60, 1},
    {120.0, 120, 1},
  };
  for (const Known& known : table) {
    if (std::fabs(fps - known.value) < 0.005) {
      numerator = known.num;
      denominator = known.den;
      return;
    }
  }
  // Unknown rate: express in thousandths rather than silently rounding to an
  // integer, which would misdescribe the flow.
  numerator = static_cast<int>(std::lround(fps * 1000.0));
  denominator = 1000;
}

// ── Minimal JSON value, for parsing IS-05 PATCH bodies ──────────────────────
// Deliberately small: objects, arrays, strings, numbers, bools, null. Enough
// to read a Connection API PATCH exactly, and no more.
struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
  bool boolValue = false;
  double numberValue = 0.0;
  std::string stringValue;
  std::vector<JsonValue> arrayValue;
  std::map<std::string, JsonValue> objectValue;

  const JsonValue* find(const std::string& key) const {
    if (type != Type::Object) {
      return nullptr;
    }
    auto it = objectValue.find(key);
    return it == objectValue.end() ? nullptr : &it->second;
  }
};

class JsonParser {
 public:
  explicit JsonParser(const std::string& text) : text_(text) {}

  bool parse(JsonValue& out) {
    skipWhitespace();
    if (!parseValue(out)) {
      return false;
    }
    skipWhitespace();
    return true;
  }

 private:
  void skipWhitespace() {
    while (pos_ < text_.size() &&
           (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
      ++pos_;
    }
  }

  bool literal(const char* word) {
    const std::size_t length = std::strlen(word);
    if (text_.compare(pos_, length, word) != 0) {
      return false;
    }
    pos_ += length;
    return true;
  }

  bool parseString(std::string& out) {
    if (pos_ >= text_.size() || text_[pos_] != '"') {
      return false;
    }
    ++pos_;
    out.clear();
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') {
        return true;
      }
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= text_.size()) {
        return false;
      }
      const char escape = text_[pos_++];
      switch (escape) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          if (pos_ + 4 > text_.size()) {
            return false;
          }
          const std::string hex = text_.substr(pos_, 4);
          pos_ += 4;
          const unsigned code = static_cast<unsigned>(std::strtoul(hex.c_str(), nullptr, 16));
          // Only the BMP subset we could ever receive here; surrogate pairs are
          // passed through as replacement rather than mis-decoded.
          if (code < 0x80) {
            out += static_cast<char>(code);
          } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
          } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
          }
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  bool parseValue(JsonValue& out) {
    skipWhitespace();
    if (pos_ >= text_.size()) {
      return false;
    }
    const char c = text_[pos_];
    if (c == '{') {
      ++pos_;
      out.type = JsonValue::Type::Object;
      skipWhitespace();
      if (pos_ < text_.size() && text_[pos_] == '}') {
        ++pos_;
        return true;
      }
      for (;;) {
        skipWhitespace();
        std::string key;
        if (!parseString(key)) {
          return false;
        }
        skipWhitespace();
        if (pos_ >= text_.size() || text_[pos_] != ':') {
          return false;
        }
        ++pos_;
        JsonValue child;
        if (!parseValue(child)) {
          return false;
        }
        out.objectValue[key] = std::move(child);
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == ',') {
          ++pos_;
          continue;
        }
        if (pos_ < text_.size() && text_[pos_] == '}') {
          ++pos_;
          return true;
        }
        return false;
      }
    }
    if (c == '[') {
      ++pos_;
      out.type = JsonValue::Type::Array;
      skipWhitespace();
      if (pos_ < text_.size() && text_[pos_] == ']') {
        ++pos_;
        return true;
      }
      for (;;) {
        JsonValue child;
        if (!parseValue(child)) {
          return false;
        }
        out.arrayValue.push_back(std::move(child));
        skipWhitespace();
        if (pos_ < text_.size() && text_[pos_] == ',') {
          ++pos_;
          continue;
        }
        if (pos_ < text_.size() && text_[pos_] == ']') {
          ++pos_;
          return true;
        }
        return false;
      }
    }
    if (c == '"') {
      out.type = JsonValue::Type::String;
      return parseString(out.stringValue);
    }
    if (literal("true")) {
      out.type = JsonValue::Type::Bool;
      out.boolValue = true;
      return true;
    }
    if (literal("false")) {
      out.type = JsonValue::Type::Bool;
      out.boolValue = false;
      return true;
    }
    if (literal("null")) {
      out.type = JsonValue::Type::Null;
      return true;
    }
    // Number
    const std::size_t start = pos_;
    if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) {
      ++pos_;
    }
    bool sawDigit = false;
    while (pos_ < text_.size() &&
           ((text_[pos_] >= '0' && text_[pos_] <= '9') || text_[pos_] == '.' ||
            text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '-' || text_[pos_] == '+')) {
      if (text_[pos_] >= '0' && text_[pos_] <= '9') {
        sawDigit = true;
      }
      ++pos_;
    }
    if (!sawDigit) {
      return false;
    }
    out.type = JsonValue::Type::Number;
    out.numberValue = std::strtod(text_.substr(start, pos_ - start).c_str(), nullptr);
    return true;
  }

  const std::string& text_;
  std::size_t pos_ = 0;
};

// Current time as the NMOS version stamp "<seconds>:<nanoseconds>".
std::string versionStampNow() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now);
  const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now - seconds);
  std::ostringstream out;
  out << seconds.count() << ':' << nanos.count();
  return out.str();
}

// Best-effort local address: open a UDP socket "towards" the target and read
// back which interface the routing table chose. No packet is sent.
std::string localAddressTowards(const std::string& peer) {
  SocketHandle probe = deckboy::platform::createDatagramSocket(false);
  if (probe == kInvalidSocket) {
    return {};
  }
  sockaddr_in target {};
  target.sin_family = AF_INET;
  target.sin_port = htons(53);
  if (inet_pton(AF_INET, peer.empty() ? "8.8.8.8" : peer.c_str(), &target.sin_addr) != 1) {
    closeSocket(probe);
    return {};
  }
  std::string result;
  if (::connect(probe, reinterpret_cast<sockaddr*>(&target), sizeof(target)) == 0) {
    sockaddr_in local {};
    socklen_t length = sizeof(local);
    if (::getsockname(probe, reinterpret_cast<sockaddr*>(&local), &length) == 0) {
      result = deckboy::platform::socketAddressToString(local);
    }
  }
  closeSocket(probe);
  return result;
}

// ── Tiny blocking HTTP/1.1 client, used only for registration ───────────────
// Returns the numeric status code, or 0 when the request never completed.
int httpRequest(const std::string& host, int port, const std::string& method,
                const std::string& path, const std::string& contentType,
                const std::string& body, std::string* responseBody, int timeoutSeconds) {
  SocketHandle sock = ::socket(AF_INET, SOCK_STREAM, 0);
  if (sock == kInvalidSocket) {
    return 0;
  }
  setCloseOnExec(sock);

  sockaddr_in address {};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<std::uint16_t>(port));
  if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
    // Not a literal address — resolve it.
    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* resolved = nullptr;
    if (::getaddrinfo(host.c_str(), nullptr, &hints, &resolved) != 0 || !resolved) {
      closeSocket(sock);
      return 0;
    }
    address.sin_addr = reinterpret_cast<sockaddr_in*>(resolved->ai_addr)->sin_addr;
    ::freeaddrinfo(resolved);
  }

  if (::connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    closeSocket(sock);
    return 0;
  }

  std::ostringstream request;
  request << method << ' ' << path << " HTTP/1.1\r\n"
          << "Host: " << host << ':' << port << "\r\n"
          << "User-Agent: Deckboy/NMOS\r\n"
          << "Connection: close\r\n";
  if (!body.empty()) {
    request << "Content-Type: " << contentType << "\r\n"
            << "Content-Length: " << body.size() << "\r\n";
  } else if (method == "POST") {
    request << "Content-Length: 0\r\n";
  }
  request << "\r\n" << body;
  const std::string payload = request.str();

  if (::send(sock, payload.c_str(), static_cast<int>(payload.size()), kSocketSendFlags) < 0) {
    closeSocket(sock);
    return 0;
  }

  std::string response;
  std::array<char, 2048> buffer {};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
  for (;;) {
    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
      deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) {
      break;
    }
    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(sock, &readFds);
    timeval tv {};
    tv.tv_sec = static_cast<long>(remaining.count() / 1000000);
    tv.tv_usec = static_cast<long>(remaining.count() % 1000000);
    if (::select(selectNfds(sock), &readFds, nullptr, nullptr, &tv) <= 0) {
      break;
    }
    const int bytes = ::recv(sock, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (bytes <= 0) {
      break;
    }
    response.append(buffer.data(), static_cast<std::size_t>(bytes));
    if (response.size() > 262144) {
      break;
    }
  }
  closeSocket(sock);

  if (response.rfind("HTTP/", 0) != 0) {
    return 0;
  }
  const std::size_t firstSpace = response.find(' ');
  if (firstSpace == std::string::npos) {
    return 0;
  }
  const int status = std::atoi(response.c_str() + firstSpace + 1);
  if (responseBody) {
    const std::size_t split = response.find("\r\n\r\n");
    *responseBody = split == std::string::npos ? std::string() : response.substr(split + 4);
  }
  return status;
}

}  // namespace

// ── Public helpers ──────────────────────────────────────────────────────────

std::string nmosDeterministicUuid(const std::string& seed) {
  Sha1 sha;
  sha.update(kDeckboyNamespace, sizeof(kDeckboyNamespace));
  sha.update(reinterpret_cast<const std::uint8_t*>(seed.data()), seed.size());
  std::uint8_t digest[20];
  sha.finish(digest);

  // RFC 4122 §4.3: take the first 16 octets, then stamp version 5 and the
  // RFC 4122 variant. Without these two lines the value is a hash, not a UUID,
  // and a strict registry will reject it.
  digest[6] = static_cast<std::uint8_t>((digest[6] & 0x0F) | 0x50);
  digest[8] = static_cast<std::uint8_t>((digest[8] & 0x3F) | 0x80);

  static const char* hex = "0123456789abcdef";
  std::string out;
  out.reserve(36);
  for (int i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      out += '-';
    }
    out += hex[(digest[i] >> 4) & 0x0F];
    out += hex[digest[i] & 0x0F];
  }
  return out;
}

bool nmosParseUrl(const std::string& url, std::string& hostOut, int& portOut,
                  std::string& pathOut) {
  std::string working = trimCopy(url);
  if (working.empty()) {
    return false;
  }
  int defaultPort = 80;
  if (working.rfind("http://", 0) == 0) {
    working = working.substr(7);
  } else if (working.rfind("https://", 0) == 0) {
    // No TLS client here. Say so rather than silently talking plaintext to 443.
    return false;
  } else if (working.find("://") != std::string::npos) {
    return false;
  }
  if (working.empty()) {
    return false;
  }

  std::size_t slash = working.find('/');
  std::string authority = slash == std::string::npos ? working : working.substr(0, slash);
  pathOut = slash == std::string::npos ? std::string("/") : working.substr(slash);
  if (pathOut.empty()) {
    pathOut = "/";
  }
  // Strip a trailing slash so callers can concatenate a rooted path safely.
  while (pathOut.size() > 1 && pathOut.back() == '/') {
    pathOut.pop_back();
  }
  if (pathOut == "/") {
    pathOut.clear();
  }

  const std::size_t colon = authority.rfind(':');
  if (colon != std::string::npos && authority.find(']') == std::string::npos) {
    hostOut = authority.substr(0, colon);
    portOut = std::atoi(authority.c_str() + colon + 1);
  } else {
    hostOut = authority;
    portOut = defaultPort;
  }
  if (hostOut.empty() || portOut <= 0 || portOut > 65535) {
    return false;
  }
  return true;
}

// ── NmosNode ────────────────────────────────────────────────────────────────

NmosNode::NmosNode() = default;

NmosNode::~NmosNode() {
  stop();
}

void NmosNode::setLastError(const std::string& message) {
  std::lock_guard<std::mutex> lock(errorMutex_);
  lastError_ = message;
}

std::string NmosNode::lastError() const {
  std::lock_guard<std::mutex> lock(errorMutex_);
  return lastError_;
}

std::string NmosNode::nodeId() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return nodeId_;
}

std::string NmosNode::nodeApiUrl() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_.load(std::memory_order_relaxed) || resolvedHost_.empty()) {
    return {};
  }
  std::ostringstream out;
  out << "http://" << resolvedHost_ << ':' << config_.nodePort << "/x-nmos/node/v1.3/";
  return out.str();
}

void NmosNode::setPatchHandler(NmosPatchHandler handler) {
  std::lock_guard<std::mutex> lock(handlerMutex_);
  patchHandler_ = std::move(handler);
}

void NmosNode::setPtpState(bool locked, const std::string& grandmaster) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (config_.ptpLocked == locked && config_.ptpGrandmaster == grandmaster) {
    return;
  }
  config_.ptpLocked = locked;
  config_.ptpGrandmaster = grandmaster;
  rebuildResources();
  resourcesDirty_.store(true, std::memory_order_relaxed);
}

bool NmosNode::start(const NmosConfig& config) {
  stop();
  if (!config.enabled) {
    return false;
  }

#ifdef _WIN32
  {
    WSADATA data {};
    if (WSAStartup(MAKEWORD(2, 2), &data) == 0) {
      winsockStarted_ = true;
    }
  }
#endif

  {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    if (config_.nodePort <= 0 || config_.nodePort > 65535) {
      config_.nodePort = 3210;
    }
    resolvedHost_ = trimCopy(config_.hostAddress);
    if (resolvedHost_.empty()) {
      std::string registryHost;
      int registryPort = 0;
      std::string registryPath;
      // Resolve the interface that actually routes to the registry, so the
      // address we advertise is the one the registry can reach us on. Guessing
      // the "first" NIC is how a multi-homed machine registers an unreachable
      // href.
      if (nmosParseUrl(config_.registryUrl, registryHost, registryPort, registryPath)) {
        resolvedHost_ = localAddressTowards(registryHost);
      }
      if (resolvedHost_.empty()) {
        resolvedHost_ = localAddressTowards({});
      }
      if (resolvedHost_.empty()) {
        resolvedHost_ = "127.0.0.1";
      }
    }
    nodeId_ = nmosDeterministicUuid("node:" + config_.label + ":" + resolvedHost_);
    deviceId_ = nmosDeterministicUuid("device:" + nodeId_);
    versionStamp_ = versionStampNow();
    rebuildResources();
  }

  SocketHandle listener = createBoundSocket(SOCK_STREAM, config_.nodePort, true,
                                            !config.allowRemote);
  if (listener == kInvalidSocket) {
    setLastError("NMOS: could not bind port " + std::to_string(config_.nodePort));
    return false;
  }
  listenSocket_ = static_cast<std::uintptr_t>(listener);
  listenValid_ = true;

  stop_.store(false);
  running_.store(true);
  httpReady_.store(true);
  registered_.store(false);
  heartbeats_.store(0);

  httpThread_ = std::thread([this]() { httpLoop(); });
  registrationThread_ = std::thread([this]() { registrationLoop(); });
  return true;
}

void NmosNode::stop() {
  if (!running_.load(std::memory_order_relaxed) && !httpThread_.joinable() &&
      !registrationThread_.joinable()) {
    return;
  }
  stop_.store(true);
  wake_.notify_all();

  if (listenValid_) {
    closeSocket(static_cast<SocketHandle>(listenSocket_));
    listenValid_ = false;
    listenSocket_ = 0;
  }
  if (httpThread_.joinable()) {
    httpThread_.join();
  }
  if (registrationThread_.joinable()) {
    registrationThread_.join();
  }

  running_.store(false);
  httpReady_.store(false);
  registered_.store(false);

#ifdef _WIN32
  if (winsockStarted_) {
    WSACleanup();
    winsockStarted_ = false;
  }
#endif
}

void NmosNode::setSenders(const std::vector<NmosSenderInfo>& senders) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Cheap equality on the fields a controller can observe. Bumping the resource
  // version on every frame would spam the registry and make every controller
  // re-read the tree 60 times a second.
  bool changed = senders.size() != senders_.size();
  if (!changed) {
    for (std::size_t i = 0; i < senders.size(); ++i) {
      const NmosSenderInfo& a = senders[i];
      const NmosSenderInfo& b = senders_[i];
      if (a.key != b.key || a.label != b.label || a.format != b.format ||
          a.destinationAddress != b.destinationAddress ||
          a.destinationPort != b.destinationPort || a.active != b.active ||
          a.sdp != b.sdp || a.width != b.width || a.height != b.height ||
          a.channels != b.channels || a.bitDepth != b.bitDepth ||
          std::fabs(a.frameRate - b.frameRate) > 0.001) {
        changed = true;
        break;
      }
    }
  }
  if (!changed) {
    return;
  }
  senders_ = senders;
  senderCount_.store(static_cast<int>(senders_.size()), std::memory_order_relaxed);
  versionStamp_ = versionStampNow();
  rebuildResources();
  resourcesDirty_.store(true, std::memory_order_relaxed);
  wake_.notify_all();
}

// Build the full IS-04 resource tree. Caller holds mutex_.
void NmosNode::rebuildResources() {
  resources_.clear();
  const std::string& version = versionStamp_;
  const std::string base = "http://" + resolvedHost_ + ":" + std::to_string(config_.nodePort);

  // ── node ──────────────────────────────────────────────────────────────────
  {
    std::ostringstream json;
    json << "{"
         << "\"id\":" << jsonQuote(nodeId_) << ","
         << "\"version\":" << jsonQuote(version) << ","
         << "\"label\":" << jsonQuote(config_.label) << ","
         << "\"description\":\"Deckboy cue deck\","
         << "\"tags\":{},"
         << "\"href\":" << jsonQuote(base + "/") << ","
         << "\"hostname\":" << jsonQuote(hostnameSafe(config_.label)) << ","
         << "\"caps\":{},"
         << "\"services\":[],"
         << "\"api\":{"
         << "\"versions\":[\"v1.3\"],"
         << "\"endpoints\":[{"
         << "\"host\":" << jsonQuote(resolvedHost_) << ","
         << "\"port\":" << config_.nodePort << ","
         << "\"protocol\":\"http\""
         << "}]},"
         // The clock is reported from real PTP state, never optimistically.
         << "\"clocks\":[{"
         << "\"name\":\"clk0\",";
    if (config_.ptpLocked && !config_.ptpGrandmaster.empty()) {
      json << "\"ref_type\":\"ptp\","
           << "\"traceable\":true,"
           << "\"version\":\"IEEE1588-2008\","
           << "\"gmid\":" << jsonQuote(config_.ptpGrandmaster) << ","
           << "\"locked\":true";
    } else {
      json << "\"ref_type\":\"internal\"";
    }
    json << "}],"
         << "\"interfaces\":[{"
         << "\"name\":" << jsonQuote(config_.interfaceName) << ","
         << "\"chassis_id\":null,"
         << "\"port_id\":\"00-00-00-00-00-00\""
         << "}]"
         << "}";
    resources_.push_back({nodeId_, json.str(), "node"});
  }

  // ── device ────────────────────────────────────────────────────────────────
  {
    std::ostringstream senderIds;
    senderIds << '[';
    for (std::size_t i = 0; i < senders_.size(); ++i) {
      if (i) {
        senderIds << ',';
      }
      senderIds << jsonQuote(nmosDeterministicUuid("sender:" + senders_[i].key));
    }
    senderIds << ']';

    std::ostringstream json;
    json << "{"
         << "\"id\":" << jsonQuote(deviceId_) << ","
         << "\"version\":" << jsonQuote(version) << ","
         << "\"label\":" << jsonQuote(config_.label + " Outputs") << ","
         << "\"description\":\"Deckboy programme outputs\","
         << "\"tags\":{},"
         << "\"type\":\"urn:x-nmos:device:generic\","
         << "\"node_id\":" << jsonQuote(nodeId_) << ","
         << "\"senders\":" << senderIds.str() << ","
         << "\"receivers\":[],"
         // This control href is how a broadcast controller finds IS-05. Without
         // it the device is discoverable but not connectable.
         << "\"controls\":[{"
         << "\"href\":" << jsonQuote(base + "/x-nmos/connection/v1.1/") << ","
         << "\"type\":\"urn:x-nmos:control:sr-ctrl/v1.1\""
         << "}]"
         << "}";
    resources_.push_back({deviceId_, json.str(), "device"});
  }

  // ── sources, flows, senders (registration order matters) ──────────────────
  for (const NmosSenderInfo& sender : senders_) {
    const std::string sourceId = nmosDeterministicUuid("source:" + sender.key);
    const std::string flowId = nmosDeterministicUuid("flow:" + sender.key);
    const std::string senderId = nmosDeterministicUuid("sender:" + sender.key);
    const bool isVideo = sender.format == NmosFormat::Video;

    int rateNum = 0;
    int rateDen = 1;
    rationalFrameRate(sender.frameRate, rateNum, rateDen);

    {
      std::ostringstream json;
      json << "{"
           << "\"id\":" << jsonQuote(sourceId) << ","
           << "\"version\":" << jsonQuote(version) << ","
           << "\"label\":" << jsonQuote(sender.label) << ","
           << "\"description\":" << jsonQuote(sender.description) << ","
           << "\"tags\":{},"
           << "\"device_id\":" << jsonQuote(deviceId_) << ","
           << "\"parents\":[],"
           << "\"clock_name\":\"clk0\","
           << "\"caps\":{},";
      if (isVideo) {
        json << "\"format\":\"urn:x-nmos:format:video\","
             << "\"grain_rate\":{\"numerator\":" << rateNum << ",\"denominator\":" << rateDen << "}";
      } else {
        json << "\"format\":\"urn:x-nmos:format:audio\","
             << "\"channels\":[";
        for (int c = 0; c < std::max(1, sender.channels); ++c) {
          if (c) {
            json << ',';
          }
          // Two channels are a conventional L/R pair; beyond that fall back to
          // undefined symbols rather than inventing a surround mapping.
          const char* symbol = "U01";
          const char* label = "Undefined";
          if (sender.channels == 2) {
            symbol = (c == 0) ? "L" : "R";
            label = (c == 0) ? "Left" : "Right";
          }
          json << "{\"label\":" << jsonQuote(label) << ",\"symbol\":";
          if (sender.channels == 2) {
            json << jsonQuote(symbol);
          } else {
            // Sized for any int the compiler can imagine, not for the channel
            // counts we expect: at 8 bytes GCC could not prove "U%02d" fits and
            // was right not to -- a truncated symbol would be a silently wrong
            // channel label in the IS-04 description.
            char buffer[16];
            std::snprintf(buffer, sizeof(buffer), "U%02d", c + 1);
            json << jsonQuote(buffer);
          }
          json << '}';
        }
        json << ']';
      }
      json << "}";
      resources_.push_back({sourceId, json.str(), "source"});
    }

    {
      std::ostringstream json;
      json << "{"
           << "\"id\":" << jsonQuote(flowId) << ","
           << "\"version\":" << jsonQuote(version) << ","
           << "\"label\":" << jsonQuote(sender.label) << ","
           << "\"description\":" << jsonQuote(sender.description) << ","
           << "\"tags\":{},"
           << "\"source_id\":" << jsonQuote(sourceId) << ","
           << "\"device_id\":" << jsonQuote(deviceId_) << ","
           << "\"parents\":[],";
      if (isVideo) {
        const int chromaWidth = std::max(1, sender.width / 2);
        json << "\"format\":\"urn:x-nmos:format:video\","
             << "\"media_type\":\"video/raw\","
             << "\"frame_width\":" << sender.width << ","
             << "\"frame_height\":" << sender.height << ","
             << "\"colorspace\":\"BT709\","
             << "\"transfer_characteristic\":\"SDR\","
             << "\"interlace_mode\":"
             << (sender.interlaced ? "\"interlaced_tff\"" : "\"progressive\"") << ","
             << "\"grain_rate\":{\"numerator\":" << rateNum << ",\"denominator\":" << rateDen << "},"
             // 4:2:2 — chroma planes are half width, full height.
             << "\"components\":["
             << "{\"name\":\"Y\",\"width\":" << sender.width << ",\"height\":" << sender.height
             << ",\"bit_depth\":" << sender.bitDepth << "},"
             << "{\"name\":\"Cb\",\"width\":" << chromaWidth << ",\"height\":" << sender.height
             << ",\"bit_depth\":" << sender.bitDepth << "},"
             << "{\"name\":\"Cr\",\"width\":" << chromaWidth << ",\"height\":" << sender.height
             << ",\"bit_depth\":" << sender.bitDepth << "}"
             << "]";
      } else {
        json << "\"format\":\"urn:x-nmos:format:audio\","
             << "\"media_type\":\"audio/L24\","
             << "\"sample_rate\":{\"numerator\":" << sender.sampleRate << ",\"denominator\":1},"
             << "\"bit_depth\":24";
      }
      json << "}";
      resources_.push_back({flowId, json.str(), "flow"});
    }

    {
      std::ostringstream json;
      json << "{"
           << "\"id\":" << jsonQuote(senderId) << ","
           << "\"version\":" << jsonQuote(version) << ","
           << "\"label\":" << jsonQuote(sender.label) << ","
           << "\"description\":" << jsonQuote(sender.description) << ","
           << "\"tags\":{},"
           << "\"flow_id\":" << jsonQuote(flowId) << ","
           << "\"transport\":\"urn:x-nmos:transport:rtp.mcast\","
           << "\"device_id\":" << jsonQuote(deviceId_) << ","
           << "\"manifest_href\":"
           << jsonQuote(base + "/x-nmos/connection/v1.1/single/senders/" + senderId + "/transportfile") << ","
           << "\"interface_bindings\":[" << jsonQuote(config_.interfaceName) << "],"
           << "\"caps\":{},"
           << "\"subscription\":{\"receiver_id\":null,\"active\":"
           << (sender.active ? "true" : "false") << "}"
           << "}";
      resources_.push_back({senderId, json.str(), "sender"});
    }
  }
}

// ── Registration ────────────────────────────────────────────────────────────

bool NmosNode::postResource(const Resource& resource) {
  std::string host;
  int port = 0;
  std::string basePath;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!nmosParseUrl(config_.registryUrl, host, port, basePath)) {
      return false;
    }
  }
  const std::string body =
    std::string("{\"type\":\"") + resource.type + "\",\"data\":" + resource.json + "}";
  std::string response;
  const int status = httpRequest(host, port, "POST",
                                 basePath + "/x-nmos/registration/v1.3/resource",
                                 "application/json", body, &response, 5);
  // 201 = created, 200 = updated. Both are success.
  if (status == 200 || status == 201) {
    return true;
  }
  setLastError("NMOS: registry rejected " + std::string(resource.type) + " (HTTP " +
               std::to_string(status) + ")");
  return false;
}

bool NmosNode::registerAll() {
  std::vector<Resource> snapshot;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot = resources_;
  }
  // Order is not cosmetic: a registry rejects a flow whose source it has not
  // seen, and a sender whose flow it has not seen. rebuildResources() emits
  // node → device → (source, flow, sender)* for exactly this reason.
  for (const Resource& resource : snapshot) {
    if (stop_.load(std::memory_order_relaxed)) {
      return false;
    }
    if (!postResource(resource)) {
      return false;
    }
  }
  return true;
}

bool NmosNode::sendHeartbeat() {
  std::string host;
  int port = 0;
  std::string basePath;
  std::string node;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!nmosParseUrl(config_.registryUrl, host, port, basePath)) {
      return false;
    }
    node = nodeId_;
  }
  const int status = httpRequest(host, port, "POST",
                                 basePath + "/x-nmos/registration/v1.3/health/nodes/" + node,
                                 "application/json", std::string(), nullptr, 5);
  if (status == 200 || status == 201) {
    heartbeats_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }
  // 404 means the registry has forgotten us — usually because it restarted.
  // Re-registering is the specified recovery, not an error to sit in.
  if (status == 404) {
    registered_.store(false, std::memory_order_relaxed);
    setLastError("NMOS: registry dropped the node; re-registering");
    return false;
  }
  setLastError("NMOS: heartbeat failed (HTTP " + std::to_string(status) + ")");
  return false;
}

void NmosNode::registrationLoop() {
  bool hasRegistry = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    hasRegistry = !trimCopy(config_.registryUrl).empty();
  }
  if (!hasRegistry) {
    // Node API only. Legitimate for bench testing; not discoverable in a plant.
    setLastError("NMOS: no registry configured — Node API served locally only");
    return;
  }

  auto sleepFor = [this](int milliseconds) {
    std::unique_lock<std::mutex> lock(wakeMutex_);
    wake_.wait_for(lock, std::chrono::milliseconds(milliseconds),
                   [this]() { return stop_.load(std::memory_order_relaxed); });
  };

  int backoffMs = 1000;
  while (!stop_.load(std::memory_order_relaxed)) {
    if (!registered_.load(std::memory_order_relaxed)) {
      if (registerAll()) {
        registered_.store(true, std::memory_order_relaxed);
        resourcesDirty_.store(false, std::memory_order_relaxed);
        setLastError({});
        backoffMs = 1000;
      } else {
        // Back off so a missing registry does not hammer the network.
        sleepFor(backoffMs);
        backoffMs = std::min(backoffMs * 2, 30000);
        continue;
      }
    }

    if (resourcesDirty_.exchange(false, std::memory_order_relaxed)) {
      if (!registerAll()) {
        registered_.store(false, std::memory_order_relaxed);
        continue;
      }
    }

    // The registry's default health timeout is 12 s; 5 s gives two chances to
    // land before it garbage-collects us.
    sleepFor(5000);
    if (stop_.load(std::memory_order_relaxed)) {
      break;
    }
    sendHeartbeat();
  }

  registered_.store(false, std::memory_order_relaxed);
}

// ── HTTP server ─────────────────────────────────────────────────────────────

void NmosNode::httpLoop() {
  while (!stop_.load(std::memory_order_relaxed)) {
    if (!listenValid_) {
      break;
    }
    const SocketHandle listener = static_cast<SocketHandle>(listenSocket_);
    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(listener, &readFds);
    timeval timeout {};
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000;
    const int ready = ::select(selectNfds(listener), &readFds, nullptr, nullptr, &timeout);
    if (ready < 0) {
#ifndef _WIN32
      if (errno == EINTR) {
        continue;
      }
#endif
      break;
    }
    if (ready == 0 || !FD_ISSET(listener, &readFds)) {
      continue;
    }
    sockaddr_in clientAddress {};
    socklen_t clientLength = sizeof(clientAddress);
    const SocketHandle client =
      ::accept(listener, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
    if (client == kInvalidSocket) {
      continue;
    }
    setCloseOnExec(client);
    serveClient(static_cast<std::uintptr_t>(client));
    closeSocket(client);
  }
  httpReady_.store(false);
}

void NmosNode::serveClient(std::uintptr_t clientHandle) {
  const SocketHandle client = static_cast<SocketHandle>(clientHandle);
  std::string request;
  std::array<char, 4096> buffer {};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

  std::size_t headerEnd = std::string::npos;
  std::size_t contentLength = 0;
  bool haveHeaders = false;

  for (;;) {
    if (haveHeaders && request.size() >= headerEnd + 4 + contentLength) {
      break;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
      deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) {
      break;
    }
    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(client, &readFds);
    timeval tv {};
    tv.tv_sec = static_cast<long>(remaining.count() / 1000000);
    tv.tv_usec = static_cast<long>(remaining.count() % 1000000);
    if (::select(selectNfds(client), &readFds, nullptr, nullptr, &tv) <= 0) {
      break;
    }
    const int bytes = ::recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
    if (bytes <= 0) {
      break;
    }
    request.append(buffer.data(), static_cast<std::size_t>(bytes));
    if (request.size() > 1048576) {
      break;
    }
    if (!haveHeaders) {
      headerEnd = request.find("\r\n\r\n");
      if (headerEnd != std::string::npos) {
        haveHeaders = true;
        // Case-insensitive Content-Length scan over the header block only.
        std::string headers = request.substr(0, headerEnd);
        std::string lowered = headers;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const std::size_t at = lowered.find("content-length:");
        if (at != std::string::npos) {
          contentLength = static_cast<std::size_t>(
            std::strtoul(headers.c_str() + at + 15, nullptr, 10));
        }
      }
    }
  }

  if (request.empty() || !haveHeaders) {
    return;
  }

  const std::size_t lineEnd = request.find("\r\n");
  const std::string requestLine = request.substr(0, lineEnd);
  std::istringstream lineStream(requestLine);
  std::string method;
  std::string target;
  lineStream >> method >> target;
  if (method.empty() || target.empty()) {
    return;
  }
  std::transform(method.begin(), method.end(), method.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

  const std::size_t queryAt = target.find('?');
  std::string path = queryAt == std::string::npos ? target : target.substr(0, queryAt);
  const std::string body = request.substr(std::min(request.size(), headerEnd + 4));

  std::string status = "200 OK";
  std::string contentType = "application/json";
  const std::string payload = routeRequest(method, path, body, status, contentType);

  std::ostringstream response;
  response << "HTTP/1.1 " << status << "\r\n"
           << "Content-Type: " << contentType << "\r\n"
           << "Content-Length: " << payload.size() << "\r\n"
           // Controllers are browser-based more often than not; without CORS
           // their fetch() to this node fails while curl works, which is a
           // miserable thing to debug in a truck.
           << "Access-Control-Allow-Origin: *\r\n"
           << "Access-Control-Allow-Methods: GET, POST, PATCH, PUT, OPTIONS\r\n"
           << "Access-Control-Allow-Headers: Content-Type\r\n"
           << "Connection: close\r\n"
           << "\r\n"
           << payload;
  const std::string encoded = response.str();
  ::send(client, encoded.c_str(), static_cast<int>(encoded.size()), kSocketSendFlags);
}

namespace {

std::string jsonArrayOf(const std::vector<std::string>& items) {
  std::ostringstream out;
  out << '[';
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i) {
      out << ',';
    }
    out << items[i];
  }
  out << ']';
  return out.str();
}

// Serialise a parsed JsonValue back to compact JSON. Needed so a bulk entry's
// `params` object can be handed to the very same code path a single PATCH
// takes, instead of a second, subtly different parser.
std::string jsonRender(const JsonValue& value) {
  std::ostringstream out;
  switch (value.type) {
    case JsonValue::Type::Null:
      return "null";
    case JsonValue::Type::Bool:
      return value.boolValue ? "true" : "false";
    case JsonValue::Type::Number: {
      // Emit whole numbers without a decimal tail — ports must not serialise
      // as 21000.0, which is not an integer to a schema validator.
      if (value.numberValue == static_cast<double>(static_cast<long long>(value.numberValue))) {
        out << static_cast<long long>(value.numberValue);
      } else {
        out << value.numberValue;
      }
      return out.str();
    }
    case JsonValue::Type::String:
      return jsonQuote(value.stringValue);
    case JsonValue::Type::Array: {
      out << '[';
      for (std::size_t i = 0; i < value.arrayValue.size(); ++i) {
        if (i) {
          out << ',';
        }
        out << jsonRender(value.arrayValue[i]);
      }
      out << ']';
      return out.str();
    }
    case JsonValue::Type::Object: {
      out << '{';
      bool first = true;
      for (const auto& [key, child] : value.objectValue) {
        if (!first) {
          out << ',';
        }
        first = false;
        out << jsonQuote(key) << ':' << jsonRender(child);
      }
      out << '}';
      return out.str();
    }
  }
  return "null";
}

// Split "/x-nmos/node/v1.3/senders/xyz" into its non-empty segments.
std::vector<std::string> splitPath(const std::string& path) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start < path.size()) {
    const std::size_t slash = path.find('/', start);
    const std::string piece = path.substr(start, slash == std::string::npos ? std::string::npos
                                                                            : slash - start);
    if (!piece.empty()) {
      parts.push_back(piece);
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  return parts;
}

}  // namespace

std::string NmosNode::routeRequest(const std::string& method, const std::string& path,
                                   const std::string& body, std::string& statusOut,
                                   std::string& contentTypeOut) {
  statusOut = "200 OK";
  contentTypeOut = "application/json";

  if (method == "OPTIONS") {
    // 200 with a body, not 204. The CORS headers are attached to every
    // response anyway, and conformance tooling treats a 204 here as the
    // endpoint declining to answer.
    return "{}";
  }

  const std::vector<std::string> parts = splitPath(path);

  // Discovery ladder: /x-nmos → /x-nmos/node → /x-nmos/node/v1.3 → resources.
  // Test suites walk this, so each rung has to answer.
  if (parts.empty()) {
    return "[\"x-nmos/\"]";
  }
  if (parts[0] != "x-nmos") {
    statusOut = "404 Not Found";
    return "{\"code\":404,\"error\":\"Not found\",\"debug\":null}";
  }
  if (parts.size() == 1) {
    return "[\"node/\",\"connection/\"]";
  }

  if (parts[1] == "connection") {
    return handleConnectionApi(method, path, body, statusOut, contentTypeOut);
  }

  if (parts[1] != "node") {
    statusOut = "404 Not Found";
    return "{\"code\":404,\"error\":\"Not found\",\"debug\":null}";
  }
  if (parts.size() == 2) {
    return "[\"v1.3/\"]";
  }
  if (parts[2] != "v1.3") {
    statusOut = "404 Not Found";
    return "{\"code\":404,\"error\":\"Unsupported API version\",\"debug\":null}";
  }

  std::lock_guard<std::mutex> lock(mutex_);

  auto collect = [this](const char* type) {
    std::vector<std::string> items;
    for (const Resource& resource : resources_) {
      if (std::strcmp(resource.type, type) == 0) {
        items.push_back(resource.json);
      }
    }
    return items;
  };

  if (parts.size() == 3) {
    return "[\"self/\",\"sources/\",\"flows/\",\"devices/\",\"senders/\",\"receivers/\"]";
  }

  const std::string& collection = parts[3];
  const std::string wanted = parts.size() >= 5 ? parts[4] : std::string();

  const char* type = nullptr;
  if (collection == "self") {
    for (const Resource& resource : resources_) {
      if (std::strcmp(resource.type, "node") == 0) {
        return resource.json;
      }
    }
    statusOut = "404 Not Found";
    return "{\"code\":404,\"error\":\"No node resource\",\"debug\":null}";
  }
  if (collection == "devices") {
    type = "device";
  } else if (collection == "sources") {
    type = "source";
  } else if (collection == "flows") {
    type = "flow";
  } else if (collection == "senders") {
    type = "sender";
  } else if (collection == "receivers") {
    // Deckboy is a source device. An empty list is the honest answer, and it is
    // what stops a controller offering to route something INTO us.
    return "[]";
  } else {
    statusOut = "404 Not Found";
    return "{\"code\":404,\"error\":\"Not found\",\"debug\":null}";
  }

  if (wanted.empty()) {
    return jsonArrayOf(collect(type));
  }
  for (const Resource& resource : resources_) {
    if (std::strcmp(resource.type, type) == 0 && resource.id == wanted) {
      return resource.json;
    }
  }
  statusOut = "404 Not Found";
  return "{\"code\":404,\"error\":\"Not found\",\"debug\":null}";
}

// Render the IS-05 staged or active object for one sender.
//
// `active` reflects what the sender is really doing. `staged` is the scratch
// area: it starts as a copy of active and then carries whatever a controller
// has PATCHed but not yet activated. Caller holds mutex_.
std::string NmosNode::renderConnectionState(const NmosSenderInfo& target, bool activeEndpoint,
                                            const std::string& host) {
  const std::string sourceIp = target.sourceAddress.empty() ? host : target.sourceAddress;

  bool masterEnable = target.active;
  std::string destinationIp = target.destinationAddress;
  int destinationPort = target.destinationPort;
  int sourcePort = target.sourcePort > 0 ? target.sourcePort : target.destinationPort;
  bool rtpEnabled = true;
  std::string receiverId;
  bool haveReceiverId = false;

  if (!activeEndpoint) {
    auto it = staged_.find(target.key);
    if (it != staged_.end()) {
      const NmosStagedState& s = it->second;
      if (s.masterEnableSet) masterEnable = s.masterEnable;
      if (s.destinationAddressSet) destinationIp = s.destinationAddress;
      if (s.destinationPortSet) destinationPort = s.destinationPort;
      if (s.sourcePortSet) sourcePort = s.sourcePort;
      if (s.rtpEnabledSet) rtpEnabled = s.rtpEnabled;
      if (s.receiverIdSet && !s.receiverId.empty()) {
        receiverId = s.receiverId;
        haveReceiverId = true;
      }
    }
  }

  // /staged reports a settled null until something is activated. /active
  // reports the activation that put it in its current state, so a controller
  // can confirm its request actually landed.
  std::string activationJson =
    "\"activation\":{\"mode\":null,\"requested_time\":null,\"activation_time\":null}";
  if (activeEndpoint) {
    auto it = lastActivation_.find(target.key);
    if (it != lastActivation_.end()) {
      activationJson = "\"activation\":{\"mode\":" + jsonQuote(it->second) +
                       ",\"requested_time\":null,\"activation_time\":" +
                       jsonQuote(versionStampNow()) + "}";
    }
  }

  std::ostringstream out;
  out << "{"
      << "\"master_enable\":" << (masterEnable ? "true" : "false") << ","
      << activationJson << ","
      << "\"receiver_id\":" << (haveReceiverId ? jsonQuote(receiverId) : std::string("null")) << ","
      << "\"transport_params\":[{"
      << "\"source_ip\":" << jsonQuote(sourceIp) << ","
      << "\"destination_ip\":" << jsonQuote(destinationIp) << ","
      << "\"source_port\":" << sourcePort << ","
      << "\"destination_port\":" << destinationPort << ","
      // Exactly the five RTP core parameters, and no more. IS-05 defines FEC
      // and RTCP as all-or-nothing groups: declaring a lone `fec_enabled`
      // without fec_destination_ip/fec_mode/fec_type/... is an invalid
      // combination, not a modest subset. We support neither, so neither
      // appears — here or in /constraints, which must list the same keys.
      << "\"rtp_enabled\":" << (rtpEnabled ? "true" : "false")
      << "}]}";
  return out.str();
}

// Apply one PATCH body to a sender's staged state, activating if asked.
// Returns the HTTP status to send. Shared by single/ and bulk/ so the two can
// never disagree about what is acceptable.
int NmosNode::applyStagedPatch(const NmosSenderInfo& target, const std::string& body,
                               bool& activatedOut, std::string& errorOut) {
  activatedOut = false;
  errorOut.clear();

  JsonValue parsed;
  JsonParser parser(body);
  if (!parser.parse(parsed) || parsed.type != JsonValue::Type::Object) {
    errorOut = "Malformed JSON";
    return 400;
  }

  // Reject anything not in the IS-05 staged schema. Silently ignoring unknown
  // keys means a controller with a typo — or a genuinely different intent —
  // gets a 200 and believes something happened. `{"bad":"data"}` must be a 400.
  for (const auto& [key, unused] : parsed.objectValue) {
    (void) unused;
    if (key != "master_enable" && key != "activation" && key != "receiver_id" &&
        key != "transport_params" && key != "transport_file" && key != "sender_id") {
      errorOut = "Unrecognised field '" + key + "'";
      return 400;
    }
  }

  // Work on a copy so a rejected PATCH leaves the staged state untouched —
  // a half-applied stage is worse than a refused one.
  NmosStagedState next;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = staged_.find(target.key);
    if (it != staged_.end()) {
      next = it->second;
    }
  }

  if (const JsonValue* enable = parsed.find("master_enable")) {
    if (enable->type != JsonValue::Type::Bool) {
      errorOut = "master_enable must be a boolean";
      return 400;
    }
    next.masterEnable = enable->boolValue;
    next.masterEnableSet = true;
  }

  if (const JsonValue* receiver = parsed.find("receiver_id")) {
    if (receiver->type == JsonValue::Type::String) {
      next.receiverId = receiver->stringValue;
      next.receiverIdSet = true;
    } else if (receiver->type == JsonValue::Type::Null) {
      next.receiverId.clear();
      next.receiverIdSet = true;
    } else {
      errorOut = "receiver_id must be a string or null";
      return 400;
    }
  }

  if (const JsonValue* params = parsed.find("transport_params")) {
    if (params->type != JsonValue::Type::Array || params->arrayValue.empty()) {
      errorOut = "transport_params must be a non-empty array";
      return 400;
    }
    // One leg. More than one means the controller thinks we are redundant
    // (ST 2022-7), and quietly accepting the first would look like it worked.
    if (params->arrayValue.size() > 1) {
      errorOut = "This sender has a single leg; ST 2022-7 redundancy is not supported";
      return 400;
    }
    const JsonValue& leg = params->arrayValue[0];
    if (leg.type != JsonValue::Type::Object) {
      errorOut = "transport_params entries must be objects";
      return 400;
    }
    if (const JsonValue* ip = leg.find("destination_ip")) {
      if (ip->type == JsonValue::Type::String) {
        // "auto" is the spec's "you pick" sentinel; resolve it to what we will
        // really send to, so /active never reports a literal "auto".
        next.destinationAddress = (ip->stringValue == "auto")
          ? target.destinationAddress : ip->stringValue;
        next.destinationAddressSet = true;
      } else {
        errorOut = "destination_ip must be a string";
        return 400;
      }
    }
    auto readPort = [&](const char* name, int& slot, bool& slotSet) -> bool {
      const JsonValue* value = leg.find(name);
      if (!value) {
        return true;
      }
      if (value->type == JsonValue::Type::String && value->stringValue == "auto") {
        slotSet = false;   // fall back to the sender's own choice
        return true;
      }
      if (value->type != JsonValue::Type::Number) {
        errorOut = std::string(name) + " must be a number";
        return false;
      }
      const int port = static_cast<int>(value->numberValue);
      if (port < 1 || port > 65535) {
        errorOut = std::string(name) + " out of range";
        return false;
      }
      slot = port;
      slotSet = true;
      return true;
    };
    if (!readPort("destination_port", next.destinationPort, next.destinationPortSet)) {
      return 400;
    }
    if (!readPort("source_port", next.sourcePort, next.sourcePortSet)) {
      return 400;
    }
    if (const JsonValue* rtp = leg.find("rtp_enabled")) {
      if (rtp->type != JsonValue::Type::Bool) {
        errorOut = "rtp_enabled must be a boolean";
        return 400;
      }
      next.rtpEnabled = rtp->boolValue;
      next.rtpEnabledSet = true;
    }
    for (const char* unsupported : {"fec_enabled", "rtcp_enabled"}) {
      if (const JsonValue* value = leg.find(unsupported)) {
        if (value->type == JsonValue::Type::Bool && value->boolValue) {
          errorOut = std::string(unsupported) + " is not supported by this sender";
          return 400;
        }
      }
    }
  }

  std::string activationMode;
  bool haveActivation = false;
  if (const JsonValue* activation = parsed.find("activation")) {
    if (activation->type != JsonValue::Type::Object) {
      errorOut = "activation must be an object";
      return 400;
    }
    haveActivation = true;
    if (const JsonValue* mode = activation->find("mode")) {
      if (mode->type == JsonValue::Type::String) {
        activationMode = mode->stringValue;
      } else if (mode->type != JsonValue::Type::Null) {
        errorOut = "activation.mode must be a string or null";
        return 400;
      }
    }
  }

  if (!activationMode.empty() && activationMode != "activate_immediate" &&
      activationMode != "activate_scheduled_absolute" &&
      activationMode != "activate_scheduled_relative") {
    errorOut = "Unknown activation mode";
    return 400;
  }
  if (activationMode == "activate_scheduled_absolute" ||
      activationMode == "activate_scheduled_relative") {
    // Honouring these needs the PTP clock to gate the switch. Taking a source
    // at the wrong instant is worse than refusing, so this is a deliberate,
    // documented non-conformance rather than a stub that pretends.
    errorOut = "Deckboy supports activate_immediate only";
    return 501;
  }

  // Stage it.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    staged_[target.key] = next;
  }

  if (activationMode != "activate_immediate") {
    // Staged only. 200 for a plain stage; the spec reserves 202 for scheduled.
    return 200;
  }

  NmosSenderPatch patch;
  patch.senderKey = target.key;
  patch.destinationAddress = next.destinationAddressSet ? next.destinationAddress
                                                        : target.destinationAddress;
  patch.destinationPort = next.destinationPortSet ? next.destinationPort
                                                  : target.destinationPort;
  patch.destinationChanged = (patch.destinationAddress != target.destinationAddress) ||
                             (patch.destinationPort != target.destinationPort);
  if (next.masterEnableSet) {
    patch.masterEnableChanged = next.masterEnable != target.active;
    patch.masterEnable = next.masterEnable;
  }
  patch.activateImmediate = true;

  NmosPatchHandler handler;
  {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    handler = patchHandler_;
  }
  if (!handler) {
    errorOut = "No handler installed";
    return 500;
  }
  if (!handler(patch)) {
    errorOut = "Sender could not be reconfigured";
    return 500;
  }

  activatedOut = true;
  (void) haveActivation;
  // The activation has been consumed: staged reverts to tracking active, which
  // is what a controller expects to read back after a successful activate.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    staged_.erase(target.key);
    lastActivation_[target.key] = "activate_immediate";
  }
  return 200;
}

std::string NmosNode::handleConnectionApi(const std::string& method, const std::string& path,
                                          const std::string& body, std::string& statusOut,
                                          std::string& contentTypeOut) {
  const std::vector<std::string> parts = splitPath(path);
  // parts: x-nmos / connection / v1.1 / (single|bulk) / senders / {id} / {endpoint}
  if (parts.size() == 2) {
    return "[\"v1.1/\"]";
  }
  if (parts[2] != "v1.1") {
    statusOut = "404 Not Found";
    return "{\"code\":404,\"error\":\"Unsupported API version\",\"debug\":null}";
  }
  if (parts.size() == 3) {
    return "[\"bulk/\",\"single/\"]";
  }

  // Snapshot every sender once; both single/ and bulk/ work from this.
  std::vector<NmosSenderInfo> senders;
  std::string host;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    senders = senders_;
    host = resolvedHost_;
  }
  auto findByKeyId = [&](const std::string& id, NmosSenderInfo& out) {
    for (const NmosSenderInfo& sender : senders) {
      if (nmosDeterministicUuid("sender:" + sender.key) == id) {
        out = sender;
        return true;
      }
    }
    return false;
  };

  // ── bulk ─────────────────────────────────────────────────────────────────
  if (parts[3] == "bulk") {
    if (parts.size() == 4) {
      return "[\"senders/\",\"receivers/\"]";
    }
    const bool isSenders = parts[4] == "senders";
    if (!isSenders && parts[4] != "receivers") {
      statusOut = "404 Not Found";
      return "{\"code\":404,\"error\":\"Not found\",\"debug\":null}";
    }
    if (method == "GET") {
      // The bulk endpoints are POST-only; GET is defined to be rejected.
      statusOut = "405 Method Not Allowed";
      return "{\"code\":405,\"error\":\"Bulk endpoints accept POST only\",\"debug\":null}";
    }
    if (method != "POST") {
      statusOut = "405 Method Not Allowed";
      return "{\"code\":405,\"error\":\"Method not allowed\",\"debug\":null}";
    }
    if (!isSenders) {
      // Deckboy has no receivers, so a bulk receiver request has nothing to
      // act on. An empty success is the honest answer.
      return "[]";
    }

    JsonValue parsed;
    JsonParser parser(body);
    if (!parser.parse(parsed) || parsed.type != JsonValue::Type::Array) {
      statusOut = "400 Bad Request";
      return "{\"code\":400,\"error\":\"Bulk body must be an array\",\"debug\":null}";
    }

    std::ostringstream out;
    out << '[';
    for (std::size_t i = 0; i < parsed.arrayValue.size(); ++i) {
      const JsonValue& entry = parsed.arrayValue[i];
      if (i) {
        out << ',';
      }
      const JsonValue* idValue = entry.find("id");
      const JsonValue* paramsValue = entry.find("params");
      if (!idValue || idValue->type != JsonValue::Type::String || !paramsValue) {
        out << "{\"id\":null,\"code\":400,\"error\":\"Entry needs id and params\",\"debug\":null}";
        continue;
      }
      NmosSenderInfo target;
      if (!findByKeyId(idValue->stringValue, target)) {
        out << "{\"id\":" << jsonQuote(idValue->stringValue)
            << ",\"code\":404,\"error\":\"Unknown sender\",\"debug\":null}";
        continue;
      }
      // Re-serialise this entry's params and run it through the SAME path a
      // single PATCH takes.
      std::ostringstream entryBody;
      entryBody << '{';
      bool first = true;
      for (const auto& [key, value] : paramsValue->objectValue) {
        if (!first) {
          entryBody << ',';
        }
        first = false;
        entryBody << jsonQuote(key) << ':' << jsonRender(value);
      }
      entryBody << '}';

      bool activated = false;
      std::string error;
      const int status = applyStagedPatch(target, entryBody.str(), activated, error);
      out << "{\"id\":" << jsonQuote(idValue->stringValue) << ",\"code\":" << status;
      if (status >= 400) {
        out << ",\"error\":" << jsonQuote(error) << ",\"debug\":null";
      }
      out << '}';
    }
    out << ']';
    return out.str();
  }

  if (parts[3] != "single") {
    statusOut = "404 Not Found";
    return "{\"code\":404,\"error\":\"Not found\",\"debug\":null}";
  }
  if (parts.size() == 4) {
    return "[\"senders/\",\"receivers/\"]";
  }
  if (parts[4] == "receivers") {
    return "[]";
  }
  if (parts[4] != "senders") {
    statusOut = "404 Not Found";
    return "{\"code\":404,\"error\":\"Not found\",\"debug\":null}";
  }

  if (parts.size() == 5) {
    std::vector<std::string> ids;
    ids.reserve(senders.size());
    for (const NmosSenderInfo& sender : senders) {
      ids.push_back(jsonQuote(nmosDeterministicUuid("sender:" + sender.key) + "/"));
    }
    return jsonArrayOf(ids);
  }

  NmosSenderInfo target;
  if (!findByKeyId(parts[5], target)) {
    statusOut = "404 Not Found";
    return "{\"code\":404,\"error\":\"Unknown sender\",\"debug\":null}";
  }
  if (parts.size() == 6) {
    return "[\"constraints/\",\"staged/\",\"active/\",\"transporttype/\",\"transportfile/\"]";
  }

  const std::string& endpoint = parts[6];
  const std::string sourceIp = target.sourceAddress.empty() ? host : target.sourceAddress;

  // How a controller learns this is an RTP sender. Omitting it does not just
  // lose one endpoint: the AMWA test suite (and real controllers) classify a
  // sender from here, and with no answer they treat it as having no transport
  // and skip everything transport-specific.
  if (endpoint == "transporttype") {
    // The BASE transport URN, deliberately — not "rtp.mcast". The multicast
    // subclassification belongs on the IS-04 sender resource (and in the SDP's
    // connection line), which is where it stays. Controllers and the AMWA test
    // suite gate their RTP-specific handling on the base URN, so reporting the
    // subclass here reads as "some other transport" and skips that handling.
    return jsonQuote("urn:x-nmos:transport:rtp");
  }

  if (endpoint == "constraints") {
    // One leg. The essence geometry is deliberately absent: the raster comes
    // from the output, not from IS-05, and advertising it as constrainable
    // would invite a controller to try to change it.
    std::ostringstream out;
    out << "[{"
        << "\"source_ip\":{\"enum\":[" << jsonQuote(sourceIp) << "]},"
        << "\"destination_ip\":{},"
        << "\"destination_port\":{\"minimum\":1,\"maximum\":65535},"
        << "\"source_port\":{\"minimum\":1,\"maximum\":65535},"
        << "\"rtp_enabled\":{}"
        << "}]";
    return out.str();
  }

  if (endpoint == "transportfile") {
    if (target.sdp.empty()) {
      statusOut = "404 Not Found";
      return "{\"code\":404,\"error\":\"No transport file available\",\"debug\":null}";
    }
    contentTypeOut = "application/sdp";
    return target.sdp;
  }

  if (endpoint == "active" || endpoint == "staged") {
    if (method == "GET") {
      std::lock_guard<std::mutex> lock(mutex_);
      return renderConnectionState(target, endpoint == "active", host);
    }
    if (endpoint == "active") {
      // /active is read-only; it changes only as a consequence of activating
      // /staged.
      statusOut = "405 Method Not Allowed";
      return "{\"code\":405,\"error\":\"/active is read-only; PATCH /staged\",\"debug\":null}";
    }
    if (method == "PATCH" || method == "PUT") {
      bool activated = false;
      std::string error;
      const int status = applyStagedPatch(target, body, activated, error);
      if (status >= 400) {
        statusOut = std::to_string(status) + (status == 400 ? " Bad Request"
                                            : status == 501 ? " Not Implemented"
                                                            : " Internal Server Error");
        return "{\"code\":" + std::to_string(status) + ",\"error\":" + jsonQuote(error) +
               ",\"debug\":null}";
      }
      // Re-read the sender: an immediate activation has just changed it.
      std::vector<NmosSenderInfo> refreshed;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        refreshed = senders_;
      }
      for (const NmosSenderInfo& sender : refreshed) {
        if (sender.key == target.key) {
          target = sender;
          break;
        }
      }
      std::lock_guard<std::mutex> lock(mutex_);
      std::string state = renderConnectionState(target, false, host);
      if (activated) {
        // Report the activation that just happened rather than a settled null.
        const std::string settled =
          "\"activation\":{\"mode\":null,\"requested_time\":null,\"activation_time\":null}";
        const std::string done =
          "\"activation\":{\"mode\":\"activate_immediate\",\"requested_time\":null,"
          "\"activation_time\":\"0:0\"}";
        const std::size_t at = state.find(settled);
        if (at != std::string::npos) {
          state.replace(at, settled.size(), done);
        }
      }
      return state;
    }
    statusOut = "405 Method Not Allowed";
    return "{\"code\":405,\"error\":\"Method not allowed\",\"debug\":null}";
  }

  statusOut = "404 Not Found";
  return "{\"code\":404,\"error\":\"Not found\",\"debug\":null}";
}

}  // namespace video
}  // namespace platform
}  // namespace deckboy
