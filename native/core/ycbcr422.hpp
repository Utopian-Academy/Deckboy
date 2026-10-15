// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ycbcr422.hpp -- the one BT.709 Y'CbCr 4:2:2 encoder.
//
// DeckLink SDI and ST 2110-20 both send the output frame as narrow-range
// BT.709 Y'CbCr 4:2:2: each pixel's own Y', and one Cb Cr shared by each
// horizontal pair (the average of the pair's chroma). They had a copy each.
// The picture scopes measure that same signal, so all three call this: a scope
// built on a copy of the maths would prove nothing about the copy that ships.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace deckboy::core {

struct Ycbcr422Pair {
  std::uint16_t cb = 0, y0 = 0, cr = 0, y1 = 0;  // codes, in Cb Y Cr Y wire order
};

// p0 and p1 are adjacent BGRA pixels (B, G, R, A bytes); tenBit picks
// 64-940 / 512 +-448 codes over 16-235 / 128 +-112.
inline Ycbcr422Pair encodeBgraPair709(const std::uint8_t* p0, const std::uint8_t* p1, bool tenBit) {
  const double scale = tenBit ? 4.0 : 1.0;
  const long maxCode = tenBit ? 1023 : 255;
  const auto luma = [](const std::uint8_t* p) {
    return (0.2126 * p[2] + 0.7152 * p[1] + 0.0722 * p[0]) / 255.0;
  };
  const double y0 = luma(p0), y1 = luma(p1);
  const double pairLuma = (y0 + y1) * 0.5;
  // (B' - Y') / (2 (1 - Kb)) and (R' - Y') / (2 (1 - Kr)), averaged over the pair.
  const double cb = ((p0[0] + p1[0]) / 510.0 - pairLuma) / 1.8556;
  const double cr = ((p0[2] + p1[2]) / 510.0 - pairLuma) / 1.5748;
  const auto code = [maxCode](double value) {
    return static_cast<std::uint16_t>(std::clamp(std::lround(value), 0L, maxCode));
  };
  return {code(scale * (128.0 + 224.0 * cb)), code(scale * (16.0 + 219.0 * y0)),
          code(scale * (128.0 + 224.0 * cr)), code(scale * (16.0 + 219.0 * y1))};
}

}  // namespace deckboy::core
