// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
//
// LAVA LAMP: a slow, warm generator. Blobs rise as they warm, sink as they
// cool, and merge and split on the way, in the desk theme's own colours.
//
// It is a secret: SOURCE > MADE BY DECKBOY lists it only once it has been
// found (the desk's type-ahead, 4-2-0, with no cue of that number). Once
// found it is an ordinary source.
//
// A smear can be computed small (CLAUDE.md, "Writing an effect"): the field is
// sampled on a grid a quarter of the raster in each direction and read back
// bilinearly, so a 4K frame costs a sixteenth of the field work.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "cue_effects.hpp"

#include "private_extras.hpp"

namespace deckboy::lava {

// The theme's deep, mid and light inks as 0xRRGGBB, published by the desk
// every frame so the lamp follows a theme change. Read on engine threads.
inline std::atomic<std::uint32_t>& ink(int which) {
  static std::atomic<std::uint32_t> inks[3] = {0x0f380f, 0x306230, 0x9bbc0f};
  return inks[std::clamp(which, 0, 2)];
}

inline void setInks(std::uint32_t deep, std::uint32_t mid, std::uint32_t light) {
  ink(0).store(deep, std::memory_order_relaxed);
  ink(1).store(mid, std::memory_order_relaxed);
  ink(2).store(light, std::memory_order_relaxed);
}

inline void render(std::uint8_t* rgba, int w, int h, double seconds) {
  if (!rgba || w <= 0 || h <= 0) return;
  const auto rgb = [](std::uint32_t c, int shift) { return static_cast<double>((c >> shift) & 0xff); };
  const std::uint32_t deep = ink(0).load(std::memory_order_relaxed);
  const std::uint32_t mid = ink(1).load(std::memory_order_relaxed);
  const std::uint32_t light = ink(2).load(std::memory_order_relaxed);

  // Six blobs, each on its own slow vertical cycle (warm: up; cool: down) with
  // a gentle sideways drift, and a radius that breathes.
  constexpr int kBlobs = 6;
  double bx[kBlobs], by[kBlobs], br[kBlobs];
  const double t = seconds * 0.12;
  for (int i = 0; i < kBlobs; ++i) {
    const double phase = i * 1.7;
    const double cycle = 0.5 - 0.5 * std::cos(t * (0.7 + 0.11 * i) + phase);   // 0 bottom .. 1 top
    by[i] = 1.05 - cycle * 1.1;
    bx[i] = 0.5 + 0.32 * std::sin(t * (0.43 + 0.07 * i) + phase * 2.3);
    br[i] = 0.115 + 0.04 * std::sin(t * 1.3 + phase);
  }

  // The field on a coarse grid.
  const int gw = std::max(2, w / 4 + 1), gh = std::max(2, h / 4 + 1);
  const double aspect = static_cast<double>(w) / static_cast<double>(h);
  std::vector<float> field(static_cast<std::size_t>(gw) * gh);
  for (int gy = 0; gy < gh; ++gy) {
    const double y = static_cast<double>(gy) / (gh - 1);
    for (int gx = 0; gx < gw; ++gx) {
      const double x = static_cast<double>(gx) / (gw - 1);
      double sum = 0.0;
      for (int i = 0; i < kBlobs; ++i) {
        const double dx = (x - bx[i]) * aspect;
        const double dy = y - by[i];
        sum += (br[i] * br[i]) / (dx * dx + dy * dy + 1e-4);
      }
      field[static_cast<std::size_t>(gy) * gw + gx] = static_cast<float>(sum);
    }
  }

  // Each pixel's colour depends only on the field value and its row (the
  // glass is lighter towards the top), so a row's colours come from a small
  // table and the pixel loop is a bilinear read and a copy. Rows run in
  // parallel bands; each band builds its own table.
  constexpr int kLevels = 256;           // field 0..2 in 256 steps; above is all wax
  std::vector<int> colX0(static_cast<std::size_t>(w));
  std::vector<float> colTx(static_cast<std::size_t>(w));
  for (int x = 0; x < w; ++x) {
    const double fx = static_cast<double>(x) / (w - 1 > 0 ? w - 1 : 1) * (gw - 1);
    colX0[static_cast<std::size_t>(x)] = std::min(gw - 2, static_cast<int>(fx));
    colTx[static_cast<std::size_t>(x)] = static_cast<float>(fx - colX0[static_cast<std::size_t>(x)]);
  }
  deckboy::effects::detail::parallelRows(h, w, [&](int firstRow, int lastRow) {
    std::uint8_t table[kLevels][4];
    for (int y = firstRow; y < lastRow; ++y) {
      const double glass = 0.10 + 0.12 * (1.0 - static_cast<double>(y) / h);
      for (int level = 0; level < kLevels; ++level) {
        const double v = level * (2.0 / (kLevels - 1));
        const double wax = std::clamp((v - 0.9) / 0.25, 0.0, 1.0);
        const double glow = std::clamp((v - 0.45) / 0.45, 0.0, 1.0) * (1.0 - wax);
        for (int c = 0; c < 3; ++c) {
          const int shift = 16 - 8 * c;
          double col = rgb(deep, shift) * (1.0 - glass) + rgb(mid, shift) * glass;
          col = col * (1.0 - glow * 0.6) + rgb(mid, shift) * glow * 0.6;
          col = col * (1.0 - wax) + rgb(light, shift) * wax;
          table[level][c] = static_cast<std::uint8_t>(std::clamp(col, 0.0, 255.0));
        }
        table[level][3] = 255;
      }
      const double fy = static_cast<double>(y) / (h - 1 > 0 ? h - 1 : 1) * (gh - 1);
      const int y0 = std::min(gh - 2, static_cast<int>(fy));
      const float ty = static_cast<float>(fy - y0);
      const float* r0 = &field[static_cast<std::size_t>(y0) * gw];
      const float* r1 = r0 + gw;
      std::uint8_t* row = rgba + static_cast<std::size_t>(y) * w * 4;
      for (int x = 0; x < w; ++x) {
        const int x0 = colX0[static_cast<std::size_t>(x)];
        const float tx = colTx[static_cast<std::size_t>(x)];
        const float v = (r0[x0] * (1 - tx) + r0[x0 + 1] * tx) * (1 - ty) +
                        (r1[x0] * (1 - tx) + r1[x0 + 1] * tx) * ty;
        const int level = std::clamp(static_cast<int>(v * ((kLevels - 1) / 2.0f) + 0.5f), 0, kLevels - 1);
        std::memcpy(row + x * 4, table[level], 4);
      }
    }
  });
}

// Registered once at start-up, hidden until found.
inline void registerGenerator() {
  for (const auto& g : deckboy::extras::generators()) {
    if (g.id == "lava-lamp") return;
  }
  deckboy::extras::Generator g;
  g.id = "lava-lamp";
  g.name = "Lava Lamp";
  g.render = [](std::uint8_t* rgba, int w, int h, double seconds) { render(rgba, w, h, seconds); };
  g.hidden = true;
  deckboy::extras::generators().push_back(std::move(g));
}

}  // namespace deckboy::lava
