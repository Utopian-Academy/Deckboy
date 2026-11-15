// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// sha256.hpp -- SHA-256 (FIPS 180-4), small and dependency-free.
//
// The constants are the standard's own: the first 32 bits of the fractional
// parts of the square roots of the first 8 primes (H) and the cube roots of
// the first 64 (K). The smoke test checks the FIPS examples.
//
// Plus a keystream built from it (SHA-256 of key || block counter), for
// assets the app keeps sealed until it has the key.
// ============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace deckboy::core {

class Sha256 {
 public:
  using Digest = std::array<std::uint8_t, 32>;

  Sha256() { reset(); }

  void reset() {
    h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    length_ = 0;
    used_ = 0;
  }

  void update(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    length_ += size;
    while (size > 0) {
      const std::size_t take = std::min<std::size_t>(64 - used_, size);
      std::memcpy(buffer_.data() + used_, p, take);
      used_ += take;
      p += take;
      size -= take;
      if (used_ == 64) {
        block(buffer_.data());
        used_ = 0;
      }
    }
  }
  void update(const std::string& text) { update(text.data(), text.size()); }

  Digest digest() {
    const std::uint64_t bits = length_ * 8;
    buffer_[used_++] = 0x80;
    if (used_ > 56) {
      while (used_ < 64) buffer_[used_++] = 0;
      block(buffer_.data());
      used_ = 0;
    }
    while (used_ < 56) buffer_[used_++] = 0;
    for (int i = 7; i >= 0; --i) buffer_[used_++] = static_cast<std::uint8_t>(bits >> (i * 8));
    block(buffer_.data());
    Digest out {};
    for (int i = 0; i < 8; ++i) {
      for (int b = 0; b < 4; ++b) out[static_cast<std::size_t>(i * 4 + b)] = static_cast<std::uint8_t>(h_[static_cast<std::size_t>(i)] >> (24 - b * 8));
    }
    reset();
    return out;
  }

  static Digest of(const std::string& text) {
    Sha256 s;
    s.update(text);
    return s.digest();
  }

  static std::string hex(const Digest& d) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint8_t v : d) {
      out.push_back(kHex[v >> 4]);
      out.push_back(kHex[v & 15]);
    }
    return out;
  }

 private:
  static std::uint32_t rotr(std::uint32_t v, int n) { return (v >> n) | (v << (32 - n)); }

  void block(const std::uint8_t* p) {
    static const std::uint32_t k[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
      0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
      0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
      0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(p[4 * i]) << 24) | (static_cast<std::uint32_t>(p[4 * i + 1]) << 16) |
             (static_cast<std::uint32_t>(p[4 * i + 2]) << 8) | static_cast<std::uint32_t>(p[4 * i + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
      const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
  }

  std::array<std::uint32_t, 8> h_ {};
  std::array<std::uint8_t, 64> buffer_ {};
  std::uint64_t length_ = 0;
  std::size_t used_ = 0;
};

// SHA-256 of `text`, then of that digest again, `rounds` times in all: a
// deliberately slow hash, so that guessing what was hashed costs real time
// per guess.
inline Sha256::Digest sha256Stretched(const std::string& text, int rounds) {
  Sha256 s;
  Sha256::Digest d = Sha256::of(text);
  for (int i = 1; i < rounds; ++i) {
    s.update(d.data(), d.size());
    d = s.digest();
  }
  return d;
}

// XOR `size` bytes at `data` in place with the keystream SHA-256(key || nonce
// || n), n the 32-byte block's number as a little-endian 64-bit integer. Its
// own inverse. Each file has its own nonce, so two files sealed with one key
// never share a keystream.
inline void sha256Keystream(const Sha256::Digest& key, const std::uint8_t* nonce, std::size_t nonceSize,
                            std::uint8_t* data, std::size_t size) {
  Sha256 s;
  for (std::size_t at = 0, n = 0; at < size; ++n) {
    std::uint8_t counter[8];
    for (int i = 0; i < 8; ++i) counter[i] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(n) >> (i * 8));
    s.update(key.data(), key.size());
    s.update(nonce, nonceSize);
    s.update(counter, sizeof(counter));
    const Sha256::Digest pad = s.digest();
    for (std::size_t i = 0; i < pad.size() && at < size; ++i, ++at) data[at] ^= pad[i];
  }
}

}  // namespace deckboy::core
