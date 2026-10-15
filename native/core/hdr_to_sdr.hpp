// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// hdr_to_sdr.hpp -- an HDR10 (PQ) or HLG picture, made right for an SDR output.
//
// Measured 2026-10-15 (tools/check_hdr_sources.py): handed to SDL as a
// BT.2020 PQ/HLG texture, an HDR clip came out DIM on Direct3D 11 -- the
// 203-nit reference white at 149 of 255, colours washed out, and the result
// depending on the desktop's own HDR settings -- and BLACK on the software
// renderer, which cannot draw that colourspace at all. So Deckboy does the
// conversion itself, the same on every platform and every display:
//
//  - the signal is decoded to display light in nits (PQ absolute; HLG on a
//    1000-nit display, its OOTF gamma 1.2)
//  - BT.2020 primaries to BT.709, linear
//  - the BT.2408 reference white, 203 nits, is SDR white
//  - up to 90% of that is unchanged, so the 75% bars come back as 191/0;
//    above it the brightest channel eases into SDR white instead of clipping
//    (hue kept: all three channels scaled together)
//  - BT.1886 (gamma 2.4) for the SDR display
//
// SDL-free. Input: 4:4:4 Y'CbCr at 16 bits per sample (the decoder's swscale
// brings any 10/12-bit layout there first). Output: 8-bit RGBA.
// ============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

namespace deckboy::core::hdr {

enum class Transfer { Sdr, Pq, Hlg };

constexpr double kReferenceWhiteNits = 203.0;   // ITU-R BT.2408
constexpr double kHlgDisplayPeakNits = 1000.0;  // the BT.2100 HLG reference display
constexpr double kKnee = 0.9;                   // of reference white: unchanged below

// The highlight roll-off, in units of reference white: exact up to the knee,
// then an exponential ease whose slope matches at the knee and which never
// reaches 1 -- so SDR white is approached, not slammed into.
inline double rolloff(double x) {
  if (x <= kKnee) return x;
  const double room = 1.0 - kKnee;
  return kKnee + room * (1.0 - std::exp(-(x - kKnee) / room));
}

inline double pqToNits(double e) {
  const double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
  const double c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
  const double p = std::pow(std::clamp(e, 0.0, 1.0), 1.0 / m2);
  return 10000.0 * std::pow(std::max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1);
}

// HLG signal to SCENE light, 0..1 (inverse OETF, BT.2100).
inline double hlgToScene(double e) {
  const double a = 0.17883277, b = 0.28466892, c = 0.55991073;
  e = std::clamp(e, 0.0, 1.0);
  return e <= 0.5 ? e * e / 3.0 : (std::exp((e - c) / a) + b) / 12.0;
}

struct Source {
  Transfer transfer = Transfer::Pq;
  bool bt2020Primaries = true;
  double kr = 0.2627, kb = 0.0593;  // the Y'CbCr matrix (BT.2020 NCL unless tagged otherwise)
  bool fullRange = false;
  int bitDepth = 10;                // what the 16-bit samples were before scaling up
};

class Converter {
 public:
  explicit Converter(const Source& source) : s_(source) {
    // Signal (0..1, 4096 steps) -> linear light in units of reference white,
    // for PQ; scene light for HLG (the OOTF needs all three channels).
    for (int i = 0; i < kLut; ++i) {
      const double e = static_cast<double>(i) / (kLut - 1);
      eotf_[i] = static_cast<float>(s_.transfer == Transfer::Pq ? pqToNits(e) / kReferenceWhiteNits
                                                               : hlgToScene(e));
    }
    // Linear (0..1 of SDR white) -> 8-bit BT.1886 code. 64K steps, indexed
    // directly: a 4096-step linear table has no step below code 8 (dark
    // scenes band) and a square-root index cost a sqrt and a round per channel
    // per pixel -- most of a 4K frame's 68 ms (measured).
    for (int i = 0; i < kEncode; ++i) {
      encode_[i] = static_cast<std::uint8_t>(
        std::lround(255.0 * std::pow(static_cast<double>(i) / (kEncode - 1), 1.0 / 2.4)));
    }
    // HLG's Ys^0.2 (the OOTF), tabled over Ys.
    for (int i = 0; i < kLut; ++i) {
      ootf_[i] = static_cast<float>(kHlgDisplayPeakNits / kReferenceWhiteNits *
                                    std::pow(static_cast<double>(i) / (kLut - 1), 0.2));
    }
    const double scale = static_cast<double>(1 << (s_.bitDepth - 8));
    const double sampleMax = static_cast<double>((1 << s_.bitDepth) - 1);
    // 16-bit samples back to the source's code values.
    toCode_ = static_cast<float>(sampleMax / 65535.0);
    yOff_ = s_.fullRange ? 0.0f : static_cast<float>(16.0 * scale);
    yGain_ = static_cast<float>(1.0 / (s_.fullRange ? sampleMax : 219.0 * scale));
    cOff_ = static_cast<float>(s_.fullRange ? (sampleMax + 1) / 2.0 : 128.0 * scale);
    cGain_ = static_cast<float>(1.0 / (s_.fullRange ? sampleMax : 224.0 * scale));
  }

  // One output pixel from one Y'CbCr sample triple (16-bit).
  void pixel(std::uint16_t y16, std::uint16_t cb16, std::uint16_t cr16, std::uint8_t* rgba) const {
    const float y = (y16 * toCode_ - yOff_) * yGain_;
    const float cb = (cb16 * toCode_ - cOff_) * cGain_;
    const float cr = (cr16 * toCode_ - cOff_) * cGain_;
    const float kr = static_cast<float>(s_.kr), kb = static_cast<float>(s_.kb), kg = 1.0f - kr - kb;
    const float rp = y + 2.0f * (1.0f - kr) * cr;
    const float bp = y + 2.0f * (1.0f - kb) * cb;
    const float gp = (y - kr * rp - kb * bp) / kg;
    float r = lookup(eotf_, rp), g = lookup(eotf_, gp), b = lookup(eotf_, bp);
    if (s_.transfer == Transfer::Hlg) {
      // OOTF on the 1000-nit reference display: Fd = Lw * Ys^(gamma-1) * Es.
      const float ys = std::max(0.0f, kr * r + kg * g + kb * b);
      const float k = lookup(ootf_, ys);
      r *= k; g *= k; b *= k;
    }
    if (s_.bt2020Primaries) {
      const float r7 = 1.6605f * r - 0.5876f * g - 0.0728f * b;   // ITU-R BT.2087, 2020 -> 709
      const float g7 = -0.1246f * r + 1.1329f * g - 0.0083f * b;
      const float b7 = -0.0182f * r - 0.1006f * g + 1.1187f * b;
      r = r7; g = g7; b = b7;
    }
    r = std::max(r, 0.0f); g = std::max(g, 0.0f); b = std::max(b, 0.0f);  // outside BT.709: clipped
    const float m = std::max({r, g, b});
    if (m > static_cast<float>(kKnee)) {
      const float k = static_cast<float>(rolloff(m) / m);
      r *= k; g *= k; b *= k;
    }
    rgba[0] = encodeLinear(r);
    rgba[1] = encodeLinear(g);
    rgba[2] = encodeLinear(b);
    rgba[3] = 255;
  }

  // Planar 4:4:4, 16-bit; rows across the cores.
  void convert(const std::uint16_t* yPlane, const std::uint16_t* cbPlane, const std::uint16_t* crPlane,
               int strideSamples, int width, int height, std::uint8_t* rgba, int rgbaStride) const {
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const int bands = std::clamp(static_cast<int>(cores) - 2, 1, 12);
    const int rows = (height + bands - 1) / bands;
    std::vector<std::future<void>> work;
    for (int y0 = 0; y0 < height; y0 += rows) {
      const int y1 = std::min(height, y0 + rows);
      work.push_back(std::async(std::launch::async, [=, this] {
        for (int y = y0; y < y1; ++y) {
          const std::size_t at = static_cast<std::size_t>(y) * strideSamples;
          std::uint8_t* out = rgba + static_cast<std::size_t>(y) * rgbaStride;
          for (int x = 0; x < width; ++x) pixel(yPlane[at + x], cbPlane[at + x], crPlane[at + x], out + x * 4);
        }
      }));
    }
    for (auto& w : work) w.get();
  }

 private:
  static constexpr int kLut = 4096;
  static constexpr int kEncode = 65536;

  static float lookup(const std::array<float, kLut>& lut, float e) {
    const float at = (e < 0.0f ? 0.0f : e) * (kLut - 1);
    const int i = static_cast<int>(at);
    if (i >= kLut - 1) return lut[kLut - 1];
    const float f = at - static_cast<float>(i);
    return lut[i] + (lut[i + 1] - lut[i]) * f;
  }
  std::uint8_t encodeLinear(float l) const {
    // l >= 0 already (clipped above); only the top needs bounding.
    const int i = static_cast<int>(l * (kEncode - 1) + 0.5f);
    return encode_[i < kEncode ? i : kEncode - 1];
  }

  Source s_;
  std::array<float, kLut> eotf_ {};
  std::array<float, kLut> ootf_ {};
  std::vector<std::uint8_t> encode_ = std::vector<std::uint8_t>(kEncode);
  float toCode_ = 1.0f, yOff_ = 0.0f, yGain_ = 1.0f, cOff_ = 0.0f, cGain_ = 1.0f;
};

// The other direction, for an HDR recording: linear BT.709 light (1.0 = the
// output's SDR white, `whiteNits`) to 16-bit PQ BT.2020 R'G'B' (rgb48le), the
// way an HDR output's float compositor holds it. Table-driven: a 64K-entry PQ
// curve indexed by the square root of the luminance (fine steps near black).
class PqEncoder {
 public:
  explicit PqEncoder(double whiteNits) : white_(static_cast<float>(whiteNits)) {
    const double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
    const double c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
    for (int i = 0; i < kSteps; ++i) {
      const double root = static_cast<double>(i) / (kSteps - 1);
      const double y = std::pow(root * root, m1);   // luminance / 10000 nits
      const double e = std::pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
      lut_[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(std::lround(std::clamp(e, 0.0, 1.0) * 65535.0));
    }
  }

  // RGBA float (linear, 709) rows in, RGB 16-bit little-endian rows out.
  void convert(const float* rgba, int width, int height, int rgbaStrideFloats, std::uint16_t* rgb48) const {
    const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    const int bands = std::clamp(static_cast<int>(cores) - 2, 1, 12);
    const int rows = (height + bands - 1) / bands;
    std::vector<std::future<void>> work;
    for (int y0 = 0; y0 < height; y0 += rows) {
      const int y1 = std::min(height, y0 + rows);
      work.push_back(std::async(std::launch::async, [=, this] {
        for (int y = y0; y < y1; ++y) {
          const float* in = rgba + static_cast<std::size_t>(y) * rgbaStrideFloats;
          std::uint16_t* out = rgb48 + static_cast<std::size_t>(y) * width * 3;
          for (int x = 0; x < width; ++x) {
            const float r = in[x * 4] * white_, g = in[x * 4 + 1] * white_, b = in[x * 4 + 2] * white_;
            // ITU-R BT.2087: BT.709 -> BT.2020 primaries, linear.
            out[x * 3] = encode(0.6274f * r + 0.3293f * g + 0.0433f * b);
            out[x * 3 + 1] = encode(0.0691f * r + 0.9195f * g + 0.0114f * b);
            out[x * 3 + 2] = encode(0.0164f * r + 0.0880f * g + 0.8956f * b);
          }
        }
      }));
    }
    for (auto& w : work) w.get();
  }

 private:
  static constexpr int kSteps = 65536;
  std::uint16_t encode(float nits) const {
    const float root = std::sqrt(std::clamp(nits, 0.0f, 10000.0f) / 10000.0f);
    return lut_[static_cast<std::size_t>(root * (kSteps - 1) + 0.5f)];
  }
  float white_;
  std::vector<std::uint16_t> lut_ = std::vector<std::uint16_t>(kSteps);
};

}  // namespace deckboy::core::hdr
