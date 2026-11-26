// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.
#pragma once
// SURF (board BBfda3Tf, James 2026-11-25): a source. The sea from the side as
// a particle fluid (FLIP), sets of waves sent in by a paddle working off the
// left edge, standing up over the shallows and breaking; things dropped in
// ride the water's own flow, tumble in the break and wash up. A new pixel-art
// beach for every seed. "Its all about the motion" -- the water is a
// simulation, never a drawing of waves.
//
// Things arrive by name (a beach thing or a critter) or as a cue's picture,
// "cue:<id>", which the desk registers here (registerPicture) before it asks
// for the drop. Drops go through a queue every live Surf reads (pushDrop), so
// Companion, a Target cue, a drag onto the monitor and Pocket all land alike.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/critter_sprites.hpp"

namespace deckboy::surf {

// What the cue keeps (saved at the end of the cue record, as one field).
struct Settings {
  double waveHeight = 0.6;   // 0-1
  double period = 0.5;       // 0-1: long slow sets to short quick ones
  double wind = 0.3;         // 0-1: pushes what floats toward the beach
  int sky = 0;               // 0 day, 1 sunset, 2 night
  std::uint32_t seed = 1;    // which beach (NEW BEACH picks another)
  double throwEvery = 0.0;   // seconds; 0: it never throws anything in by itself
  std::vector<std::string> throwList;   // what it throws in by itself: names or "cue:<id>"
};

inline std::string serialize(const Settings& s) {
  char buf[160];
  std::snprintf(buf, sizeof buf, "v1|%.3f|%.3f|%.3f|%d|%u|%.2f|", s.waveHeight, s.period, s.wind, s.sky,
                static_cast<unsigned>(s.seed), s.throwEvery);
  std::string out = buf;
  for (std::size_t i = 0; i < s.throwList.size(); ++i) {
    if (i) out += ',';
    for (char c : s.throwList[i]) if (c != ',' && c != '|' && c != '\t' && c != '\n') out += c;
  }
  return out;
}

inline Settings parse(const std::string& text) {
  Settings s;
  if (text.rfind("v1|", 0) != 0) return s;
  std::vector<std::string> f;
  std::size_t at = 3;
  while (true) {
    const std::size_t end = text.find('|', at);
    f.push_back(text.substr(at, end == std::string::npos ? std::string::npos : end - at));
    if (end == std::string::npos) break;
    at = end + 1;
  }
  auto num = [&](std::size_t i, double dflt) { return i < f.size() && !f[i].empty() ? std::atof(f[i].c_str()) : dflt; };
  s.waveHeight = std::clamp(num(0, 0.6), 0.0, 1.0);
  s.period = std::clamp(num(1, 0.5), 0.0, 1.0);
  s.wind = std::clamp(num(2, 0.3), 0.0, 1.0);
  s.sky = std::clamp(static_cast<int>(num(3, 0)), 0, 2);
  s.seed = static_cast<std::uint32_t>(std::strtoul(f.size() > 4 ? f[4].c_str() : "1", nullptr, 10));
  s.throwEvery = std::clamp(num(5, 0.0), 0.0, 600.0);
  if (f.size() > 6 && !f[6].empty()) {
    std::size_t a = 0;
    while (a <= f[6].size()) {
      const std::size_t e = f[6].find(',', a);
      const std::string item = f[6].substr(a, e == std::string::npos ? std::string::npos : e - a);
      if (!item.empty() && s.throwList.size() < 64) s.throwList.push_back(item);
      if (e == std::string::npos) break;
      a = e + 1;
    }
  }
  return s;
}

// ── PICTURES AND DROPS (shared by the desk, its outputs and Pocket) ─────────
struct Picture {
  int w = 0, h = 0;
  std::vector<std::uint8_t> rgba;   // straight alpha
};
using PicturePtr = std::shared_ptr<const Picture>;

struct Drop {
  std::string surf;     // the Surf cue's id; empty: every Surf on air
  std::string what;     // a beach thing's or critter's name, "surprise", "cue:<id>", or "#clear"
  float x01 = -1.0f;    // where across the picture; <0 somewhere over the water
  std::uint64_t serial = 0;
  double atSeconds = 0.0;   // wall clock it was asked at, so old ones are dropped
};

namespace detail {
inline std::mutex& lock() { static std::mutex m; return m; }
inline std::map<std::string, PicturePtr>& pictures() { static std::map<std::string, PicturePtr> p; return p; }
inline std::vector<Drop>& drops() { static std::vector<Drop> d; return d; }
inline std::uint64_t& serial() { static std::uint64_t s = 0; return s; }
}  // namespace detail

inline void registerPicture(const std::string& key, PicturePtr picture) {
  std::lock_guard<std::mutex> hold(detail::lock());
  if (detail::pictures().size() > 256) detail::pictures().clear();
  detail::pictures()[key] = std::move(picture);
}

inline PicturePtr pictureFor(const std::string& key) {
  std::lock_guard<std::mutex> hold(detail::lock());
  const auto it = detail::pictures().find(key);
  return it == detail::pictures().end() ? nullptr : it->second;
}

inline bool hasPicture(const std::string& key) { return pictureFor(key) != nullptr; }

// A cue's picture, made small enough to float (its longest side at most
// maxSide pixels), nearest-sampled; straight alpha is kept.
inline PicturePtr shrinkPicture(int w, int h, const std::vector<std::uint8_t>& rgba, int maxSide = 240) {
  if (w <= 0 || h <= 0 || rgba.size() < static_cast<std::size_t>(w) * h * 4) return nullptr;
  const float k = std::min(1.0f, static_cast<float>(maxSide) / static_cast<float>(std::max(w, h)));
  auto out = std::make_shared<Picture>();
  out->w = std::max(1, static_cast<int>(std::lround(w * k)));
  out->h = std::max(1, static_cast<int>(std::lround(h * k)));
  out->rgba.resize(static_cast<std::size_t>(out->w) * out->h * 4);
  for (int y = 0; y < out->h; ++y) {
    for (int x = 0; x < out->w; ++x) {
      const int sx = std::min(w - 1, static_cast<int>(x / k)), sy = std::min(h - 1, static_cast<int>(y / k));
      std::memcpy(out->rgba.data() + (static_cast<std::size_t>(y) * out->w + x) * 4,
                  rgba.data() + (static_cast<std::size_t>(sy) * w + sx) * 4, 4);
    }
  }
  return out;
}

// Asked for now; every Surf on air (or the one named) takes it on its next frame.
inline void pushDrop(Drop drop, double nowSeconds) {
  std::lock_guard<std::mutex> hold(detail::lock());
  drop.serial = ++detail::serial();
  drop.atSeconds = nowSeconds;
  auto& d = detail::drops();
  d.erase(std::remove_if(d.begin(), d.end(), [&](const Drop& o) { return nowSeconds - o.atSeconds > 3.0; }), d.end());
  if (d.size() < 256) d.push_back(std::move(drop));
}

// The drops this Surf has not seen yet (by serial).
inline std::vector<Drop> dropsSince(const std::string& surfId, std::uint64_t& seen) {
  std::lock_guard<std::mutex> hold(detail::lock());
  std::vector<Drop> out;
  std::uint64_t top = seen;
  for (const Drop& d : detail::drops()) {
    if (d.serial <= seen) continue;
    top = std::max(top, d.serial);
    if (d.surf.empty() || d.surf == surfId) out.push_back(d);
  }
  seen = top;
  return out;
}

inline std::uint64_t latestDropSerial() {
  std::lock_guard<std::mutex> hold(detail::lock());
  return detail::serial();
}

// ── THE BEACH THINGS (drawn for Deckboy; the critters are critter_sprites) ──
struct ThingArt { const char* name; const char* rows[12]; };
inline constexpr ThingArt kThings[] = {
  {"duck",    {"..yy.....", ".yyky....", ".yyyyo...", "..yyy....", "yyyyyyy..", "yyyyyyyy.", ".yyyyyy..", nullptr}},
  {"ball",    {"..rrww..", ".rrrwwb.", "rrrrwwbb", "yyyyybbb", "yyyyybbb", "yyyywwbb", ".yyywwb.", "..yyww..", nullptr}},
  {"chicken", {"....rr...", "...yyyr..", "...ykyyo.", "...yyyy..", "..yyyy...", "yyyyyyy..", "yyyyyyyy.", ".yyyyyy..", "..o..o...", nullptr}},
  {"toaster", {"..k..k....", ".ssssssss.", "sswwssssss", "ssssssssss", "sskssssssk", "ssssssssss", ".k......k.", nullptr}},
  {"melon",   {"...gggg...", ".ggdggdgg.", "gdggdggdgg", "gggdggdggg", ".ggdggdgg.", "...gggg...", nullptr}},
  {"cone",    {"...oo...", "...ww...", "..oooo..", "..wwww..", ".oooooo.", "kkkkkkkk", nullptr}},
  {"bottle",  {"...ss...", "...cc...", "..cccc..", "..cwcc..", "..cccc..", "..cccc..", "..cccc..", "..cccc..", nullptr}},
  {"board",   {"..............", ".oooooooooooo.", "oowwoooooowwoo", ".oooooooooooo.", "..............", nullptr}},
};
inline constexpr int kThingCount = static_cast<int>(sizeof(kThings) / sizeof(kThings[0]));

inline PicturePtr artPicture(const char* const* rows, int maxRows) {
  auto p = std::make_shared<Picture>();
  int h = 0, w = 0;
  while (h < maxRows && rows[h]) { w = std::max<int>(w, static_cast<int>(std::strlen(rows[h]))); ++h; }
  p->w = w; p->h = h;
  p->rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; rows[y][x]; ++x) {
      const std::uint32_t ink = deckboy::draw::critterInk(rows[y][x]);
      if (!ink) continue;
      std::uint8_t* d = p->rgba.data() + (static_cast<std::size_t>(y) * w + x) * 4;
      d[0] = static_cast<std::uint8_t>(ink >> 24); d[1] = static_cast<std::uint8_t>(ink >> 16);
      d[2] = static_cast<std::uint8_t>(ink >> 8); d[3] = 255;
    }
  }
  return p;
}

// Every name a drop may use, beach things first, then the critters.
inline std::vector<std::string> thingNames() {
  std::vector<std::string> out;
  for (const ThingArt& t : kThings) out.push_back(t.name);
  for (int i = 0; i < deckboy::draw::kCritterCount; ++i) {
    const std::string n = deckboy::draw::kCritters[i].name;
    if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);   // "duck" is both
  }
  return out;
}

inline PicturePtr builtinPicture(const std::string& name) {
  static std::map<std::string, PicturePtr> cache;
  static std::mutex m;
  std::lock_guard<std::mutex> hold(m);
  const auto it = cache.find(name);
  if (it != cache.end()) return it->second;
  PicturePtr p;
  for (const ThingArt& t : kThings) if (name == t.name) p = artPicture(t.rows, 12);
  for (int i = 0; !p && i < deckboy::draw::kCritterCount; ++i) {
    if (name == deckboy::draw::kCritters[i].name) p = artPicture(deckboy::draw::kCritters[i].frames[0], 10);
  }
  if (p) cache[name] = p;
  return p;
}

// ── THE SIMULATION ──────────────────────────────────────────────────────
//
// Port of the board's prototype (approved 2026-11-25), in the same units:
// a 320x180 view, y up in the sim, the tank 90 pixels wider than the view on
// the left so the paddle works out of sight ("the ocean is floating" when it
// stopped at a wall).
class Sim {
 public:
  static constexpr int kW = 320, kH = 180, kOff = 90, kSimW = kW + kOff;
  static constexpr float kCell = 2.5f;
  static constexpr int kNX = static_cast<int>(kSimW / 2.5f) + 1, kNY = static_cast<int>(kH / 2.5f) + 1;
  static constexpr int kCells = kNX * kNY;
  static constexpr float kR = 0.3f * kCell, kGrav = -320.0f, kFlip = 0.96f, kSea = 74.0f;
  static constexpr int kMaxP = 16000;

  struct Thing {
    PicturePtr pic;
    bool pixelArt = true;     // a beach thing or critter: whole-pixel steps; a cue's picture: smooth
    float x = 0, y = 0, vx = 0, vy = 0, a = 0, va = 0;
    float halfW = 4, halfH = 4;   // in view pixels
    bool wet = false;
  };

  // A new beach, from the seed.
  void reset(std::uint32_t seed) {
    seed_ = seed;
    std::uint32_t st = seed ? seed : 1u;
    auto r = [&]() {
      st += 0x6D2B79F5u;
      std::uint32_t q = st;
      q = (q ^ (q >> 15)) * (1u | q);
      q ^= q + ((q ^ (q >> 7)) * (61u | q));
      return static_cast<float>((q ^ (q >> 14)) & 0xFFFFFFu) / 16777216.0f;
    };
    const float slopeAt = 0.44f + r() * 0.12f, slope = 170 + r() * 80, reefAt = slopeAt + 0.03f + r() * 0.1f;
    const float reefH = 5 + r() * 17, reefW = 0.0006f + r() * 0.0016f;
    const bool bar = r() < 0.5f;
    const float barAt = 0.24f + r() * 0.12f, barH = 4 + r() * 8, barW = 0.002f + r() * 0.003f;
    for (int x = 0; x <= kSimW; ++x) {
      const float f = static_cast<float>(x - kOff) / kW;
      bed_[x] = 6 + std::max(0.0f, (f - slopeAt) * slope) + reefH * std::exp(-((f - reefAt) * (f - reefAt)) / reefW) +
                (bar ? barH * std::exp(-((f - barAt) * (f - barAt)) / barW) : 0.0f);
    }
    head_.x1 = 110 + r() * 100; head_.peak = 20 + r() * 24;
    for (int i = 0; i < 3; ++i) head_.ph[i] = r() * 6.3f;
    head_.fr[0] = 0.6f + r() * 0.9f; head_.fr[1] = 2 + r() * 3; head_.fr[2] = 6 + r() * 6;
    islands_.clear();
    for (int n = 1 + static_cast<int>(r() * 2); n > 0; --n) islands_.push_back({150 + r() * 120, 12 + r() * 26, 5 + r() * 12, r() * 6.3f});
    clouds_.clear();
    for (int n = 2 + static_cast<int>(r() * 3); n > 0; --n) clouds_.push_back({r() * kW, 10 + r() * 40, 14 + r() * 26});
    bushes_.clear();
    for (int n = 3 + static_cast<int>(r() * 5); n > 0; --n) bushes_.push_back(r());
    for (int x = 0; x <= kW; ++x) tuft_[x] = r();
    landKey_ = -1;
    paddle_ = 6; paddleV_ = 0; t_ = 0; restDensity_ = 0;
    fill();
  }

  std::uint32_t seed() const { return seed_; }
  bool ready() const { return np_ > 0; }

  void clearThings() { things_.clear(); }

  // A thing into the water: by name, or a picture already registered.
  void drop(const std::string& what, float x01, std::uint32_t salt) {
    PicturePtr pic;
    bool art = true;
    std::string name = what;
    if (name == "surprise" || name.empty()) {
      const auto names = thingNames();
      name = names[(salt * 2654435761u) % names.size()];
    } else if (name == "critter") {
      name = deckboy::draw::kCritters[(salt * 2654435761u) % static_cast<std::uint32_t>(deckboy::draw::kCritterCount)].name;
    }
    if (name.rfind("cue:", 0) == 0) {
      pic = pictureFor(name);
      art = false;
    } else {
      pic = builtinPicture(name);
    }
    if (!pic || pic->w <= 0 || pic->h <= 0) return;
    Thing t;
    t.pic = pic;
    t.pixelArt = art;
    if (art) {
      t.halfW = pic->w * 0.5f; t.halfH = pic->h * 0.5f;
    } else {
      // a picture: about a sixth of the width, no taller than a fifth of the height
      const float s = std::min(54.0f / pic->w, 34.0f / pic->h);
      t.halfW = pic->w * s * 0.5f; t.halfH = pic->h * s * 0.5f;
    }
    const float jitter = static_cast<float>((salt * 40503u) % 1000u) / 1000.0f;
    const float sx = x01 >= 0.0f ? x01 * kW : 30.0f + jitter * 200.0f;
    t.x = kOff + std::clamp(sx, 8.0f, kW - 8.0f);
    t.y = kH - 20.0f;
    t.vx = (jitter - 0.5f) * 20.0f; t.vy = -10.0f; t.va = (jitter - 0.5f) * 2.0f;
    things_.push_back(t);
    if (things_.size() > 30) things_.erase(things_.begin());
  }

  // Advance to `seconds` on the cue's clock, in fixed sixtieths.
  void update(double seconds, const Settings& s) {
    if (lastSeconds_ < 0.0 || seconds < lastSeconds_ || seconds - lastSeconds_ > 2.0) lastSeconds_ = seconds - 1.0 / 60.0;
    int steps = static_cast<int>(std::floor((seconds - lastSeconds_) * 60.0 + 1e-6));
    steps = std::clamp(steps, 0, 4);
    lastSeconds_ += steps / 60.0;
    if (seconds - lastSeconds_ > 0.1) lastSeconds_ = seconds;   // fell far behind: let go
    for (int i = 0; i < steps; ++i) step(1.0f / 60.0f, s);
  }

  // Draw into RGBA pixels W x H: the beach and the water in the 320x180 view's
  // pixels (scaled by whole pixels), then the things at the picture's own size.
  void render(std::uint8_t* out, int W, int H, int sky) {
    if (!out || W <= 0 || H <= 0) return;
    sky = std::clamp(sky, 0, 2);
    renderView(sky);
    // the view, nearest, to fill
    for (int y = 0; y < H; ++y) {
      const int vy = std::min(kH - 1, y * kH / H);
      const std::uint32_t* src = view_.data() + static_cast<std::size_t>(vy) * kW;
      std::uint8_t* row = out + static_cast<std::size_t>(y) * W * 4;
      for (int x = 0; x < W; ++x) {
        const std::uint32_t c = src[std::min(kW - 1, x * kW / W)];
        row[x * 4 + 0] = static_cast<std::uint8_t>(c);
        row[x * 4 + 1] = static_cast<std::uint8_t>(c >> 8);
        row[x * 4 + 2] = static_cast<std::uint8_t>(c >> 16);
        row[x * 4 + 3] = 255;
      }
    }
    const float sx = static_cast<float>(W) / kW, sy = static_cast<float>(H) / kH;
    for (const Thing& t : things_) drawThing(out, W, H, t, sx, sy);
  }

  int thingCount() const { return static_cast<int>(things_.size()); }
  const std::vector<Thing>& things() const { return things_; }

 private:
  struct Head { float x1 = 150, peak = 30, ph[3] {}, fr[3] {1, 3, 8}; };
  struct Island { float cx, half, ht, ph; };
  struct Cloud { float x, y, w; };

  // ── the fluid ──
  float bedAt(float x) const {
    const float i = std::clamp(x, 0.0f, static_cast<float>(kSimW));
    const int i0 = static_cast<int>(i);
    const float t = i - i0;
    return bed_[i0] * (1 - t) + bed_[std::min(kSimW, i0 + 1)] * t;
  }
  static int id(int i, int j) { return i * kNY + j; }

  void fill() {
    np_ = 0;
    const float dx = 2 * kR, dy = std::sqrt(3.0f) / 2 * dx;
    for (int row = 0;; ++row) {
      const float y = kCell + kR + row * dy;
      if (y > kSea) break;
      for (float x = paddle_ + kCell + kR + (row % 2) * kR; x < kSimW - kCell - kR; x += dx) {
        if (y < bedAt(x) + kR + 0.5f || np_ >= kMaxP) continue;
        px_[np_] = x; py_[np_] = y; vx_[np_] = 0; vy_[np_] = 0; ++np_;
      }
    }
  }

  void solids() {
    for (int i = 0; i < kNX; ++i) {
      for (int j = 0; j < kNY; ++j) {
        const float cx = (i + 0.5f) * kCell, cy = (j + 0.5f) * kCell;
        const bool solid = i == 0 || i >= kNX - 1 || j == 0 || cx < paddle_ || cy < bedAt(cx);
        s_[id(i, j)] = solid ? 0.0f : 1.0f;
      }
    }
  }

  void pushApart() {
    constexpr float kHS = 2.2f * kR;
    constexpr int kHX = static_cast<int>(kSimW / kHS) + 1, kHY = static_cast<int>(kH / kHS) + 1;
    cellCount_.assign(kHX * kHY + 1, 0);
    cellPart_.resize(static_cast<std::size_t>(np_));
    auto cellOf = [&](int k) {
      const int xi = std::clamp(static_cast<int>(px_[k] / kHS), 0, kHX - 1), yi = std::clamp(static_cast<int>(py_[k] / kHS), 0, kHY - 1);
      return xi * kHY + yi;
    };
    for (int k = 0; k < np_; ++k) cellCount_[cellOf(k)]++;
    int first = 0;
    for (int c = 0; c < kHX * kHY; ++c) { first += cellCount_[c]; cellCount_[c] = first; }
    cellCount_[kHX * kHY] = first;
    for (int k = 0; k < np_; ++k) cellPart_[--cellCount_[cellOf(k)]] = k;
    const float minD = 2 * kR, minD2 = minD * minD;
    for (int k = 0; k < np_; ++k) {
      const int pxi = static_cast<int>(px_[k] / kHS), pyi = static_cast<int>(py_[k] / kHS);
      for (int xi = std::max(pxi - 1, 0); xi <= std::min(pxi + 1, kHX - 1); ++xi) {
        for (int yi = std::max(pyi - 1, 0); yi <= std::min(pyi + 1, kHY - 1); ++yi) {
          const int c = xi * kHY + yi;
          for (int q = cellCount_[c]; q < cellCount_[c + 1]; ++q) {
            const int o = cellPart_[q];
            if (o == k) continue;
            float dx = px_[o] - px_[k], dy = py_[o] - py_[k];
            const float d2 = dx * dx + dy * dy;
            if (d2 > minD2 || d2 == 0) continue;
            const float d = std::sqrt(d2), sc = 0.5f * (minD - d) / d;
            dx *= sc; dy *= sc;
            px_[k] -= dx; py_[k] -= dy; px_[o] += dx; py_[o] += dy;
          }
        }
      }
    }
  }

  void collide() {
    for (int k = 0; k < np_; ++k) {
      if (px_[k] < paddle_ + kR) { px_[k] = paddle_ + kR; if (vx_[k] < paddleV_) vx_[k] = paddleV_; }
      if (px_[k] > kSimW - kCell - kR) { px_[k] = kSimW - kCell - kR; if (vx_[k] > 0) vx_[k] = 0; }
      if (py_[k] > kH - kR) { py_[k] = kH - kR; if (vy_[k] > 0) vy_[k] = 0; }
      const float b = bedAt(px_[k]) + kR;
      if (py_[k] < b) {
        py_[k] = b;
        const float sl = (bedAt(px_[k] + 1) - bedAt(px_[k] - 1)) * 0.5f, nl = std::sqrt(sl * sl + 1), nx = -sl / nl, ny = 1 / nl;
        const float vn = vx_[k] * nx + vy_[k] * ny;
        if (vn < 0) { vx_[k] -= vn * nx; vy_[k] -= vn * ny; }
        vx_[k] *= 0.98f;
      }
    }
  }

  void toGrid() {
    for (int c = 0; c < kCells; ++c) type_[c] = s_[c] == 0 ? kSolid : kAir;
    for (int k = 0; k < np_; ++k) {
      const int i = std::clamp(static_cast<int>(px_[k] / kCell), 0, kNX - 1), j = std::clamp(static_cast<int>(py_[k] / kCell), 0, kNY - 1);
      if (type_[id(i, j)] == kAir) type_[id(i, j)] = kFluid;
    }
    for (int comp = 0; comp < 2; ++comp) {
      float* f = comp == 0 ? u_.data() : v_.data();
      float* d = comp == 0 ? du_.data() : dv_.data();
      float* prev = comp == 0 ? pu_.data() : pv_.data();
      std::fill(f, f + kCells, 0.0f);
      std::fill(d, d + kCells, 0.0f);
      const float ox = comp == 0 ? 0 : kCell / 2, oy = comp == 0 ? kCell / 2 : 0;
      for (int k = 0; k < np_; ++k) {
        const float x = std::clamp(px_[k], kCell, (kNX - 1) * kCell) - ox, y = std::clamp(py_[k], kCell, (kNY - 1) * kCell) - oy;
        const int x0 = std::min(static_cast<int>(x / kCell), kNX - 2), y0 = std::min(static_cast<int>(y / kCell), kNY - 2);
        const float tx = (x - x0 * kCell) / kCell, ty = (y - y0 * kCell) / kCell, val = comp == 0 ? vx_[k] : vy_[k];
        const float w0 = (1 - tx) * (1 - ty), w1 = tx * (1 - ty), w2 = tx * ty, w3 = (1 - tx) * ty;
        f[id(x0, y0)] += val * w0; d[id(x0, y0)] += w0;
        f[id(x0 + 1, y0)] += val * w1; d[id(x0 + 1, y0)] += w1;
        f[id(x0 + 1, y0 + 1)] += val * w2; d[id(x0 + 1, y0 + 1)] += w2;
        f[id(x0, y0 + 1)] += val * w3; d[id(x0, y0 + 1)] += w3;
      }
      for (int c = 0; c < kCells; ++c) if (d[c] > 0) f[c] /= d[c];
      for (int i = 0; i < kNX; ++i) {
        for (int j = 0; j < kNY; ++j) {
          const bool solid = type_[id(i, j)] == kSolid;
          if (comp == 0 && (solid || (i > 0 && type_[id(i - 1, j)] == kSolid))) f[id(i, j)] = (i * kCell <= paddle_ + kCell) ? paddleV_ : 0.0f;
          if (comp == 1 && (solid || (j > 0 && type_[id(i, j - 1)] == kSolid))) f[id(i, j)] = 0.0f;
        }
      }
      std::copy(f, f + kCells, prev);
    }
  }

  void density() {
    std::fill(dens_.begin(), dens_.end(), 0.0f);
    for (int k = 0; k < np_; ++k) {
      const float x = std::clamp(px_[k], kCell, (kNX - 1) * kCell) - kCell / 2, y = std::clamp(py_[k], kCell, (kNY - 1) * kCell) - kCell / 2;
      const int x0 = std::min(static_cast<int>(x / kCell), kNX - 2), y0 = std::min(static_cast<int>(y / kCell), kNY - 2);
      const float tx = (x - x0 * kCell) / kCell, ty = (y - y0 * kCell) / kCell;
      dens_[id(x0, y0)] += (1 - tx) * (1 - ty); dens_[id(x0 + 1, y0)] += tx * (1 - ty);
      dens_[id(x0 + 1, y0 + 1)] += tx * ty; dens_[id(x0, y0 + 1)] += (1 - tx) * ty;
    }
    if (restDensity_ == 0) {
      float sum = 0; int n = 0;
      for (int c = 0; c < kCells; ++c) if (type_[c] == kFluid) { sum += dens_[c]; ++n; }
      if (n) restDensity_ = sum / n;
    }
  }

  void solve(int iters) {
    for (int it = 0; it < iters; ++it) {
      for (int i = 1; i < kNX - 1; ++i) {
        for (int j = 1; j < kNY - 1; ++j) {
          const int c = id(i, j);
          if (type_[c] != kFluid) continue;
          const int l = id(i - 1, j), r = id(i + 1, j), b = id(i, j - 1), t = id(i, j + 1);
          const float sum = s_[l] + s_[r] + s_[b] + s_[t];
          if (sum == 0) continue;
          float div = u_[r] - u_[c] + v_[t] - v_[c];
          if (restDensity_ > 0) { const float comp = dens_[c] - restDensity_; if (comp > 0) div -= comp; }
          const float p = -div / sum * 1.9f;
          u_[c] -= s_[l] * p; u_[r] += s_[r] * p; v_[c] -= s_[b] * p; v_[t] += s_[t] * p;
        }
      }
    }
  }

  float sampleGrid(float x, float y, int comp) const {
    const float* f = comp == 0 ? u_.data() : v_.data();
    const float ox = comp == 0 ? 0 : kCell / 2, oy = comp == 0 ? kCell / 2 : 0;
    x = std::clamp(x, kCell, (kNX - 1) * kCell) - ox; y = std::clamp(y, kCell, (kNY - 1) * kCell) - oy;
    const int x0 = std::min(static_cast<int>(x / kCell), kNX - 2), y0 = std::min(static_cast<int>(y / kCell), kNY - 2);
    const float tx = (x - x0 * kCell) / kCell, ty = (y - y0 * kCell) / kCell;
    return f[id(x0, y0)] * (1 - tx) * (1 - ty) + f[id(x0 + 1, y0)] * tx * (1 - ty) + f[id(x0 + 1, y0 + 1)] * tx * ty + f[id(x0, y0 + 1)] * (1 - tx) * ty;
  }

  void toParticles() {
    for (int comp = 0; comp < 2; ++comp) {
      const float* f = comp == 0 ? u_.data() : v_.data();
      const float* prev = comp == 0 ? pu_.data() : pv_.data();
      const float ox = comp == 0 ? 0 : kCell / 2, oy = comp == 0 ? kCell / 2 : 0;
      const int off = comp == 0 ? kNY : 1;
      for (int k = 0; k < np_; ++k) {
        const float x = std::clamp(px_[k], kCell, (kNX - 1) * kCell) - ox, y = std::clamp(py_[k], kCell, (kNY - 1) * kCell) - oy;
        const int x0 = std::min(static_cast<int>(x / kCell), kNX - 2), y0 = std::min(static_cast<int>(y / kCell), kNY - 2);
        const float tx = (x - x0 * kCell) / kCell, ty = (y - y0 * kCell) / kCell;
        const int n[4] = {id(x0, y0), id(x0 + 1, y0), id(x0 + 1, y0 + 1), id(x0, y0 + 1)};
        const float w[4] = {(1 - tx) * (1 - ty), tx * (1 - ty), tx * ty, (1 - tx) * ty};
        float d = 0, pic = 0, corr = 0;
        for (int q = 0; q < 4; ++q) {
          const bool ok = type_[n[q]] != kAir || (n[q] - off >= 0 && type_[n[q] - off] != kAir);
          const float ww = ok ? w[q] : 0.0f;
          d += ww; pic += ww * f[n[q]]; corr += ww * (f[n[q]] - prev[n[q]]);
        }
        if (d > 0) {
          pic /= d; corr /= d;
          float& cur = comp == 0 ? vx_[k] : vy_[k];
          cur = (1 - kFlip) * pic + kFlip * (cur + corr);
        }
      }
    }
  }

  bool fluidAt(float x, float y) const {
    const int i = static_cast<int>(x / kCell), j = static_cast<int>(y / kCell);
    return i > 0 && j > 0 && i < kNX && j < kNY && type_[id(i, j)] == kFluid;
  }

  void step(float dt, const Settings& s) {
    t_ += dt;
    // the paddle: a long quick push and a slow return, in sets
    const float period = 3.8f - static_cast<float>(s.period) * 1.8f, amp = 10 + static_cast<float>(s.waveHeight) * 46;
    const float set = 0.55f + 0.45f * std::max(0.0f, std::sin(t_ * 6.2831853f / (period * 4)));
    const float ph = std::fmod(t_ / period, 1.0f);
    const float push = ph < 0.4f ? 0.5f - 0.5f * std::cos(ph / 0.4f * 3.14159265f) : 0.5f + 0.5f * std::cos((ph - 0.4f) / 0.6f * 3.14159265f);
    const float target = 6 + amp * set * push;
    paddleV_ = (target - paddle_) / dt;
    paddle_ = target;
    solids();
    for (int sub = 0; sub < 2; ++sub) {
      const float sdt = dt / 2;
      for (int k = 0; k < np_; ++k) { vy_[k] += kGrav * sdt; px_[k] += vx_[k] * sdt; py_[k] += vy_[k] * sdt; }
      pushApart(); collide(); toGrid(); density(); solve(25); toParticles();
    }
    // the things
    const float wind = static_cast<float>(s.wind);
    for (Thing& o : things_) {
      const bool inWater = fluidAt(o.x, o.y - 1);
      if (inWater) {
        const float fu = sampleGrid(o.x, o.y, 0), fv = sampleGrid(o.x, o.y, 1);
        if (!o.wet && o.vy < -60) {   // a splash: the water near it thrown up and out
          for (int k = 0; k < np_; ++k) {
            const float dx = px_[k] - o.x, dy = py_[k] - o.y, d = std::sqrt(dx * dx + dy * dy);
            if (d < 9 && d > 0) { vx_[k] += dx / d * 90; vy_[k] += 140 * (1 - d / 9); }
          }
        }
        o.wet = true;
        o.vx += (fu + wind * 12.0f - o.vx) * 0.12f;
        o.vy += (fv - o.vy) * 0.12f + 300 * dt * (fluidAt(o.x, o.y + 3) ? 1.4f : 0.7f);
        o.va += ((sampleGrid(o.x, o.y + 3, 0) - sampleGrid(o.x, o.y - 3, 0)) * 0.02f - o.va) * 0.1f;
      } else {
        o.wet = false;
        o.vy += kGrav * dt;
      }
      o.vx *= 0.995f; o.vy *= 0.995f;
      o.x += o.vx * dt; o.y += o.vy * dt; o.a += o.va * dt;
      const float b = bedAt(o.x) + std::min(o.halfH, 4.0f);
      if (o.y < b) { o.y = b; o.vy = std::max(0.0f, o.vy); o.vx *= 0.8f; o.va *= 0.7f; }
      if (o.x < kOff + 6) { o.x = kOff + 6; o.vx = std::abs(o.vx); }
      if (o.x > kSimW - 6) { o.x = kSimW - 6; o.vx = -std::abs(o.vx); }
    }
  }

  // ── the picture ──
  static std::uint32_t rgb(int r, int g, int b) { return 0xFF000000u | (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(r); }
  static std::uint32_t hex(std::uint32_t v) { return rgb(static_cast<int>(v >> 16), static_cast<int>((v >> 8) & 255), static_cast<int>(v & 255)); }

  void buildLand(int sky) {
    landKey_ = static_cast<int>(seed_ * 3u + static_cast<std::uint32_t>(sky));
    land_.assign(static_cast<std::size_t>(kW) * kH, 0u);
    // "distant", never "far": far is a macro in the Windows headers.
    struct Pal { std::uint32_t grass[5], distant[2], cloud[2]; };
    static const Pal kPal[3] = {
      {{0x2f5a36, 0x3f7442, 0x5a9450, 0x86bf64, 0xb6e08a}, {0x7d93b4, 0x94a8c6}, {0xffffff, 0xdfeaf6}},
      {{0x3a3048, 0x4e3f5a, 0x6a5068, 0x93687a, 0xc98a86}, {0x8a6a8e, 0xa888a6}, {0xffe0c0, 0xf0a890}},
      {{0x101c18, 0x16261f, 0x1f3428, 0x2a4434, 0x3a5a44}, {0x1a2846, 0x24365a}, {0x4a5a80, 0x36446a}},
    };
    const Pal& L = kPal[sky];
    auto put = [&](int x, int y, std::uint32_t c) { if (x >= 0 && x < kW && y >= 0 && y < kH) land_[static_cast<std::size_t>(y) * kW + x] = hex(c); };
    auto rect = [&](int x, int y, int w, int h, std::uint32_t c) { for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) put(x + i, y + j, c); };
    const int base = kH - static_cast<int>(kSea);
    for (const Cloud& cl : clouds_) {
      const int x0 = static_cast<int>(std::lround(cl.x)), y0 = static_cast<int>(std::lround(cl.y)), w = static_cast<int>(std::lround(cl.w));
      rect(x0 + 1, y0 + 3, w - 2, 2, L.cloud[1]);
      rect(x0, y0 + 1, w, 3, L.cloud[0]);
      rect(x0 + 3, y0 - 1, static_cast<int>(std::lround(w * 0.45f)), 2, L.cloud[0]);
      rect(x0 + static_cast<int>(std::lround(w * 0.45f)), y0 - 2, static_cast<int>(std::lround(w * 0.35f)), 3, L.cloud[0]);
    }
    for (const Island& is : islands_) {
      for (int x = static_cast<int>(std::lround(is.cx - is.half)); x <= is.cx + is.half; ++x) {
        const float t = 1 - std::abs(x - is.cx) / is.half;
        if (t <= 0) continue;
        const int hh = static_cast<int>(std::lround(is.ht * std::pow(t, 0.7f) * (0.85f + 0.15f * std::sin(x * 0.3f + is.ph))));
        rect(x, base - hh, 1, hh, L.distant[0]);
        put(x, base - hh, L.distant[1]);
      }
    }
    std::array<float, kW + 2> hh {};
    for (int x = 0; x <= head_.x1 && x <= kW; ++x) {
      const float t = x / head_.x1;
      const float env = t < 0.35f ? 1.0f : std::pow(std::cos((t - 0.35f) / 0.65f * 1.5707963f), 1.3f);
      hh[x] = std::max(0.0f, head_.peak * env * (0.86f + 0.1f * std::sin(x * 0.02f * head_.fr[0] + head_.ph[0]) + 0.04f * std::sin(x * 0.09f * head_.fr[1] + head_.ph[1])) +
                                 0.9f * std::sin(x * 0.7f * head_.fr[2] + head_.ph[2]));
    }
    for (int x = 0; x <= head_.x1 && x <= kW; ++x) {
      const int h0 = static_cast<int>(std::lround(hh[x]));
      if (h0 <= 0) continue;
      const int top = base - h0;
      const bool lit = hh[x + 1] < hh[x] - 0.05f, shade = hh[x + 1] > hh[x] + 0.35f;
      for (int y = top; y < base; ++y) {
        const int rel = y - top, fromFoot = base - 1 - y;
        std::uint32_t col = lit ? L.grass[2] : shade ? L.grass[1] : L.grass[2];
        if (rel > 3) col = ((x + y) & 1) && rel < 9 ? (lit ? L.grass[2] : L.grass[1]) : L.grass[1];
        if (rel > 9) col = L.grass[(x * 7 + y * 3) % 11 == 0 ? 2 : 1];
        if (fromFoot < 3) col = L.grass[0];
        if (rel == 0) col = lit ? L.grass[4] : L.grass[3];
        else if (rel == 1) col = lit ? L.grass[3] : L.grass[2];
        put(x, y, col);
      }
      if (tuft_[x] > 0.72f) { put(x, top - 1, L.grass[3]); if (tuft_[x] > 0.93f) put(x, top - 2, L.grass[4]); }
      for (int y = base; y < base + 16; ++y) put(x, y, ((x + y) & 1) && y < base + 4 ? L.grass[1] : L.grass[0]);
    }
    for (float b : bushes_) {
      const int x = static_cast<int>(std::lround(14 + b * (head_.x1 - 30)));
      if (x < 2 || x >= kW - 3 || hh[x] < 6) continue;
      const int top = base - static_cast<int>(std::lround(hh[x]));
      rect(x - 2, top - 2, 5, 3, L.grass[0]); rect(x - 1, top - 3, 3, 1, L.grass[0]);
      put(x - 1, top - 3, L.grass[2]); put(x, top - 2, L.grass[3]);
    }
  }

  void renderView(int sky) {
    struct Sky { int top[3], bot[3]; int sun[3]; bool hasSun; };
    static const Sky kSkies[3] = {
      {{118, 178, 236}, {196, 226, 246}, {0, 0, 0}, false},
      {{70, 78, 140}, {255, 170, 110}, {255, 214, 140}, true},
      {{6, 12, 40}, {24, 40, 88}, {235, 235, 210}, true},
    };
    static const int kWater[3][5][3] = {
      {{200, 255, 246}, {96, 232, 226}, {42, 190, 214}, {28, 140, 200}, {20, 96, 160}},
      {{255, 230, 200}, {120, 214, 220}, {56, 160, 196}, {36, 110, 170}, {24, 70, 130}},
      {{150, 220, 255}, {50, 150, 230}, {30, 110, 200}, {20, 80, 160}, {12, 50, 110}},
    };
    const Sky& S = kSkies[sky];
    view_.resize(static_cast<std::size_t>(kW) * kH);
    const int horizon = kH - static_cast<int>(kSea);
    for (int y = 0; y < kH; ++y) {
      const float t = std::min(1.0f, static_cast<float>(y) / horizon);
      const std::uint32_t c = rgb(static_cast<int>(S.top[0] + (S.bot[0] - S.top[0]) * t), static_cast<int>(S.top[1] + (S.bot[1] - S.top[1]) * t),
                                  static_cast<int>(S.top[2] + (S.bot[2] - S.top[2]) * t));
      std::fill(view_.begin() + static_cast<std::ptrdiff_t>(y) * kW, view_.begin() + static_cast<std::ptrdiff_t>(y + 1) * kW, c);
    }
    if (S.hasSun) {
      const int cx = sky == 2 ? 250 : 262, cy = sky == 2 ? 26 : horizon - 26, r = sky == 2 ? 7 : 13;
      for (int y = cy - r; y <= cy + r; ++y) for (int x = cx - r; x <= cx + r; ++x) {
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r && x >= 0 && x < kW && y >= 0 && y < kH) view_[static_cast<std::size_t>(y) * kW + x] = rgb(S.sun[0], S.sun[1], S.sun[2]);
      }
    }
    if (landKey_ != static_cast<int>(seed_ * 3u + static_cast<std::uint32_t>(sky))) buildLand(sky);
    for (std::size_t i = 0; i < view_.size(); ++i) if (land_[i]) view_[i] = land_[i];
    // the surface over each column, then the water, a particle at a time
    std::array<float, kW> top;
    top.fill(kH + 10.0f);
    for (int k = 0; k < np_; ++k) {
      const int x = static_cast<int>(px_[k] - kOff);
      const float ys = kH - py_[k];
      if (x >= 0 && x < kW && ys < top[x]) top[x] = ys;
    }
    for (int x = 1; x < kW - 1; ++x) top[x] = std::min(top[x], (top[x - 1] + top[x + 1]) / 2 + 2);
    const auto& pal = kWater[sky];
    for (int k = 0; k < np_; ++k) {
      const int x = static_cast<int>(px_[k] - kOff), ys = static_cast<int>(kH - py_[k]);
      if (x < 0 || x >= kW - 1 || ys < 0 || ys >= kH - 1) continue;
      const float depth = ys - top[x], sp = std::sqrt(vx_[k] * vx_[k] + vy_[k] * vy_[k]);
      const int band = depth < 1.5f ? 0 : depth < 5 ? 1 : depth < 13 ? 2 : depth < 26 ? 3 : 4;
      const int* c = (sp > 150 && depth < 8) ? pal[0] : pal[band];
      const std::uint32_t col = rgb(c[0], c[1], c[2]);
      view_[static_cast<std::size_t>(ys) * kW + x] = col; view_[static_cast<std::size_t>(ys) * kW + x + 1] = col;
      view_[static_cast<std::size_t>(ys + 1) * kW + x] = col; view_[static_cast<std::size_t>(ys + 1) * kW + x + 1] = col;
      if (sp > 70 && depth < 20) {   // the combed streak, along the flow
        const int sx = x - static_cast<int>(std::lround(vx_[k] / sp * 2)), sy = ys + static_cast<int>(std::lround(vy_[k] / sp * 2));
        if (sx >= 0 && sx < kW && sy >= 0 && sy < kH) {
          const int* sc = depth < 10 ? pal[0] : pal[1];
          view_[static_cast<std::size_t>(sy) * kW + sx] = rgb(sc[0], sc[1], sc[2]);
        }
      }
    }
    // the sand, in bands, in steps of two pixels
    static const std::uint32_t kSand[2][4] = {{0xe6c48a, 0xc99b5c, 0xa9783f, 0x2a3245}, {0x4a3c2c, 0x3c3024, 0x30261d, 0x1b2030}};
    const auto& sand = kSand[sky == 2 ? 1 : 0];
    for (int x = 0; x < kW; ++x) {
      const int xs = x & ~1;
      const int surface = static_cast<int>(std::lround(kH - bed_[xs + kOff]));
      for (int y = std::max(0, surface); y < kH; ++y) {
        const int band = std::min(3, (y - surface) / 4);
        view_[static_cast<std::size_t>(y) * kW + x] = hex(sand[band]);
      }
    }
  }

  // A thing at the picture's own size: whole pixels for pixel art, smooth for a picture.
  static void drawThing(std::uint8_t* out, int W, int H, const Thing& t, float sx, float sy) {
    const Picture& p = *t.pic;
    const float cx = (t.x - kOff) * sx, cy = (kH - t.y) * sy;
    const float scale = t.pixelArt ? std::max(1.0f, std::round(sx)) : (t.halfW * 2 * sx) / p.w;
    const float hw = p.w * scale * 0.5f, hh = p.h * scale * 0.5f;
    const float ca = std::cos(-t.a), sa = std::sin(-t.a);
    const float reach = std::sqrt(hw * hw + hh * hh) + 1;
    const int x0 = std::max(0, static_cast<int>(cx - reach)), x1 = std::min(W - 1, static_cast<int>(cx + reach));
    const int y0 = std::max(0, static_cast<int>(cy - reach)), y1 = std::min(H - 1, static_cast<int>(cy + reach));
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        // back into the picture, unturned
        const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
        const float u = (dx * ca + dy * sa + hw) / scale, v = (-dx * sa + dy * ca + hh) / scale;
        if (u < 0 || v < 0 || u >= p.w || v >= p.h) continue;
        const std::uint8_t* s = p.rgba.data() + (static_cast<std::size_t>(static_cast<int>(v)) * p.w + static_cast<int>(u)) * 4;
        const int a = s[3];
        if (a == 0) continue;
        std::uint8_t* d = out + (static_cast<std::size_t>(y) * W + x) * 4;
        for (int c = 0; c < 3; ++c) d[c] = static_cast<std::uint8_t>(d[c] + ((s[c] - d[c]) * a) / 255);
      }
    }
  }

  enum : std::uint8_t { kFluid = 0, kAir = 1, kSolid = 2 };
  std::uint32_t seed_ = 0;
  std::array<float, kSimW + 1> bed_ {};
  Head head_;
  std::vector<Island> islands_;
  std::vector<Cloud> clouds_;
  std::vector<float> bushes_;
  std::array<float, kW + 1> tuft_ {};
  std::vector<std::uint32_t> land_, view_;
  int landKey_ = -1;
  int np_ = 0;
  std::vector<float> px_ = std::vector<float>(kMaxP), py_ = std::vector<float>(kMaxP), vx_ = std::vector<float>(kMaxP), vy_ = std::vector<float>(kMaxP);
  std::vector<float> u_ = std::vector<float>(kCells), v_ = std::vector<float>(kCells), du_ = std::vector<float>(kCells), dv_ = std::vector<float>(kCells);
  std::vector<float> pu_ = std::vector<float>(kCells), pv_ = std::vector<float>(kCells), s_ = std::vector<float>(kCells), dens_ = std::vector<float>(kCells);
  std::vector<std::uint8_t> type_ = std::vector<std::uint8_t>(kCells);
  std::vector<int> cellCount_, cellPart_;
  float paddle_ = 6, paddleV_ = 0, t_ = 0, restDensity_ = 0;
  double lastSeconds_ = -1.0;
  std::vector<Thing> things_;
};

}  // namespace deckboy::surf
