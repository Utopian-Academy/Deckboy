// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#include "gpu_effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "../core/sdl_compat.hpp"

#ifdef _WIN32
#include <d3d11_3.h>
#endif

namespace deckboy::gpu {
namespace fx = deckboy::effects;
using fx::CueEffectKind;

namespace {
// The shader in every form (see gpu_effects.hlsl).
#include "gpu_effects_shaders.inc"

// gpu_effects.hlsl's cbuffer, field for field. Rows of four, so HLSL, MSL
// and C++ all lay it out the same way.
struct alignas(16) Params {
  std::uint32_t kind = 0, width = 0, height = 0, frame = 0;
  std::uint32_t srcW = 0, srcH = 0, auxW = 0, auxH = 0;
  std::int32_t i0 = 0, i1 = 0, i2 = 0, i3 = 0;
  float amt = 0, pA = 0, pB = 0, pC = 0;
  float pD = 0, f0 = 0, f1 = 0, f2 = 0;
  float f3 = 0, f4 = 0, f5 = 0, f6 = 0;
  float f7 = 0, f8 = 0, f9 = 0, f10 = 0;
  float f11 = 0, f12 = 0, f13 = 0, f14 = 0;
  std::uint8_t lut[768] = {};
  float tab[256] = {};
};
static_assert(sizeof(Params) == 128 + 768 + 1024, "Params must match gpu_effects.hlsl");

enum GpuKind : std::uint32_t {
  K_LUT = 1, K_THRESHOLD, K_COLOUR, K_COLOUR_REPLACE, K_VIGNETTE, K_GRAIN, K_CHANNEL_OFFSET,
  K_MIRROR, K_LUMA_KEY, K_EDGES, K_WARP, K_MOSAIC, K_MIX
};
enum Entry { E_POINT = 0, E_DOWN, E_BOX, E_STREAK, E_TILE, E_NV12, E_TRANS, E_COUNT };
const char* const kEntryNames[E_COUNT] = {"pointMain", "downMain", "boxMain", "streakMain", "tileMain", "nv12Main",
                                          "transMain"};

// A transition's flat `aux` array as RGBA32F texels, kAuxRow to a row
// (transMain's auxAt reads it back the same way).
constexpr int kAuxRow = 4096;
void auxShape(std::size_t floats, int& tw, int& th) {
  const std::size_t texels = std::max<std::size_t>(1, (floats + 3) / 4);
  tw = static_cast<int>(std::min<std::size_t>(texels, kAuxRow));
  th = static_cast<int>((texels + kAuxRow - 1) / kAuxRow);
}
// `aux` padded to the whole texture; `aux` itself when it already fills it.
const float* auxPadded(const std::vector<float>& aux, int tw, int th, std::vector<float>& pad) {
  const std::size_t want = static_cast<std::size_t>(tw) * th * 4;
  if (aux.size() == want) return aux.data();
  pad.resize(want);
  std::copy(aux.begin(), aux.end(), pad.begin());
  std::fill(pad.begin() + static_cast<std::ptrdiff_t>(aux.size()), pad.end(), 0.0f);
  return pad.data();
}

// The NV12 kernel's constants for a frame's colour space: its matrix (BT.601,
// 709 or 2020) and its range, as SDL's own YCbCr conversion takes them.
// tenBit: P010, whose 10-bit codes sit in the top of 16-bit samples, so
// normalised black, white and mid-chroma land a little off 8-bit's.
void nv12Constants(unsigned colorspace, int width, Params& p, bool tenBit = false) {
  const SDL_Colorspace cs = static_cast<SDL_Colorspace>(colorspace);
  double kr = 0.2126, kb = 0.0722;   // BT.709
  switch (SDL_COLORSPACEMATRIX(cs)) {
    case SDL_MATRIX_COEFFICIENTS_BT601:
    case SDL_MATRIX_COEFFICIENTS_BT470BG:
      kr = 0.299; kb = 0.114; break;
    case SDL_MATRIX_COEFFICIENTS_BT2020_NCL:
      kr = 0.2627; kb = 0.0593; break;
    case SDL_MATRIX_COEFFICIENTS_BT709:
      break;
    default:
      if (width < 1280) { kr = 0.299; kb = 0.114; }   // unspecified: SD is 601
      break;
  }
  const bool full = SDL_COLORSPACERANGE(cs) == SDL_COLOR_RANGE_FULL;
  const double kg = 1.0 - kr - kb;
  // The sample as read (0-1) back to code values, then the range.
  const double codeMax = tenBit ? 65535.0 / 64.0 : 255.0;   // 1023.98 or 255
  const double black = tenBit ? 64.0 : 16.0, lumaSpan = tenBit ? 876.0 : 219.0;
  const double mid = tenBit ? 512.0 : 128.0, chromaSpan = tenBit ? 896.0 : 224.0;
  const double fullMax = tenBit ? 1023.0 : 255.0;
  const double yScale = full ? codeMax / fullMax : codeMax / lumaSpan;
  const double cScale = full ? codeMax / fullMax : codeMax / chromaSpan;
  p.f0 = static_cast<float>(full ? 0.0 : -black / codeMax);
  p.f1 = static_cast<float>(-mid / codeMax);
  p.f2 = static_cast<float>(yScale);
  p.f3 = static_cast<float>(cScale * (2.0 - 2.0 * kr));
  p.f4 = static_cast<float>(cScale * (2.0 * kb * (1.0 - kb) / kg));
  p.f5 = static_cast<float>(cScale * (2.0 * kr * (1.0 - kr) / kg));
  p.f6 = static_cast<float>(cScale * (2.0 - 2.0 * kb));
}

// The buffers a plan reads and writes. CUR/ALT ping-pong at full size and
// hold the picture; F1/F2 are full-size scratch; S1/S2 are scratch at up to
// half size (every reduced copy is at least 2x smaller).
enum Buf { CUR = 0, ALT, F1, F2, S1, S2, BUF_COUNT };

struct Pass {
  Entry entry = E_POINT;
  Params params;
  int src = CUR;   // a Buf, or CUR/ALT meaning "the picture as it stands"
  int aux = CUR;
  int dst = ALT;
  int w = 0, h = 0;
};

// ---------------------------------------------------------------------------
// THE PLAN: a stack as a list of dispatches. Everything an effect works out
// once per frame is worked out here by the CPU code's own expressions, so the
// shader does the per-pixel part only.

void common(Params& p, const fx::CueEffect& e, double amt, int w, int h, const fx::CueEffectContext& ctx) {
  p.width = static_cast<std::uint32_t>(w);
  p.height = static_cast<std::uint32_t>(h);
  p.srcW = p.width;
  p.srcH = p.height;
  p.frame = static_cast<std::uint32_t>(ctx.frameIndex);
  p.amt = static_cast<float>(amt);
  p.pA = static_cast<float>(std::clamp(static_cast<double>(e.paramA), 0.0, 1.0));
  p.pB = static_cast<float>(std::clamp(static_cast<double>(e.paramB), 0.0, 1.0));
  p.pC = static_cast<float>(std::clamp(static_cast<double>(e.paramC), 0.0, 1.0));
  p.pD = static_cast<float>(std::clamp(static_cast<double>(e.paramD), 0.0, 1.0));
}

// An effect that maps each channel through a table, independent of where the
// pixel is: its table is what the CPU effect does to a 256-pixel grey ramp.
// Exact by construction -- the CPU code made every entry.
void rampLut(const fx::CueEffect& e, const fx::CueEffectContext& ctx, std::uint8_t lut[768], bool alpha = false) {
  std::vector<std::uint8_t> ramp(256 * 4);
  for (int v = 0; v < 256; ++v) {
    ramp[v * 4] = ramp[v * 4 + 1] = ramp[v * 4 + 2] = static_cast<std::uint8_t>(v);
    ramp[v * 4 + 3] = 255;
  }
  fx::CueEffectContext c = ctx;
  c.width = 256;
  c.height = 1;
  c.effectState = nullptr;
  c.audioSamples = nullptr;
  c.audioSampleCount = 0;
  fx::applyCueEffectStack(ramp, {e}, c);
  for (int v = 0; v < 256; ++v) {
    if (alpha) {
      lut[v] = ramp[v * 4 + 3];
    } else {
      for (int k = 0; k < 3; ++k) lut[k * 256 + v] = ramp[v * 4 + k];
    }
  }
}

bool isRampLutKind(CueEffectKind k) {
  return k == CueEffectKind::Invert || k == CueEffectKind::Posterise || k == CueEffectKind::Solarise ||
         k == CueEffectKind::Levels || k == CueEffectKind::Curves || k == CueEffectKind::Strobe;
}

Pass point(const Params& p, int w, int h, int aux = CUR) {
  Pass pass;
  pass.entry = E_POINT;
  pass.params = p;
  pass.src = CUR;
  pass.aux = aux;
  pass.dst = ALT;
  pass.w = w;
  pass.h = h;
  return pass;
}

// Box passes as detail::boxBlurRgb: `passes` of horizontal then vertical,
// each a pass of its own here. Reads `from` first, then alternates a/b.
// Returns the buffer holding the result.
int boxPasses(std::vector<Pass>& plan, int from, int a, int b, int w, int h, int radius, int passes) {
  radius = std::clamp(radius, 0, std::max(w, h));
  int at = from;
  for (int pass = 0; pass < passes; ++pass) {
    for (int axis = 0; axis < 2; ++axis) {
      const int r = std::min(radius, axis == 0 ? w : h);
      if (r <= 0) continue;
      Pass box;
      box.entry = E_BOX;
      box.params.width = box.params.srcW = static_cast<std::uint32_t>(w);
      box.params.height = box.params.srcH = static_cast<std::uint32_t>(h);
      box.params.i0 = r;
      box.params.i1 = axis;
      box.src = at;
      box.aux = at;
      box.dst = at == a ? b : a;
      box.w = w;
      box.h = h;
      plan.push_back(box);
      at = box.dst;
    }
  }
  return at;
}

Pass down(int from, int to, int w, int h, int factor, int& ow, int& oh) {
  ow = std::max(1, (w + factor - 1) / factor);
  oh = std::max(1, (h + factor - 1) / factor);
  Pass d;
  d.entry = E_DOWN;
  d.params.width = static_cast<std::uint32_t>(ow);
  d.params.height = static_cast<std::uint32_t>(oh);
  d.params.srcW = static_cast<std::uint32_t>(w);
  d.params.srcH = static_cast<std::uint32_t>(h);
  d.params.i0 = factor;
  d.src = from;
  d.aux = from;
  d.dst = to;
  d.w = ow;
  d.h = oh;
  return d;
}

Pass mix(int mode, int aux, int auxW, int auxH, int w, int h, double amt) {
  Pass m;
  m.entry = E_POINT;
  m.params.kind = K_MIX;
  m.params.width = m.params.srcW = static_cast<std::uint32_t>(w);
  m.params.height = m.params.srcH = static_cast<std::uint32_t>(h);
  m.params.auxW = static_cast<std::uint32_t>(auxW);
  m.params.auxH = static_cast<std::uint32_t>(auxH);
  m.params.i0 = mode;
  m.params.amt = static_cast<float>(amt);
  m.src = CUR;
  m.aux = aux;
  m.dst = ALT;
  m.w = w;
  m.h = h;
  return m;
}

// One effect, appended to `plan`. False: no GPU version (the caller then
// leaves the whole stack to the CPU).
bool planEffect(std::vector<Pass>& plan, const fx::CueEffect& e, int w, int h, const fx::CueEffectContext& ctx) {
  const double amt = std::clamp(static_cast<double>(e.amount), 0.0, 1.0);
  if (e.kind == CueEffectKind::None || e.bypassed || amt <= 0.0005) return true;   // nothing to do
  const double pA = std::clamp(static_cast<double>(e.paramA), 0.0, 1.0);
  const double pB = std::clamp(static_cast<double>(e.paramB), 0.0, 1.0);
  const double pC = std::clamp(static_cast<double>(e.paramC), 0.0, 1.0);
  const double pD = std::clamp(static_cast<double>(e.paramD), 0.0, 1.0);
  Params p;
  common(p, e, amt, w, h, ctx);
  if (isRampLutKind(e.kind)) {
    p.kind = K_LUT;
    rampLut(e, ctx, p.lut);
    plan.push_back(point(p, w, h));
    return true;
  }
  switch (e.kind) {
    case CueEffectKind::Scanlines: {
      p.kind = K_LUT;
      p.i0 = std::max(2, static_cast<int>(std::lround(2.0 + pA * 10.0)));
      const double lineDark = 0.7 + pB * 0.3;
      for (int v = 0; v < 256; ++v) {
        const std::uint8_t out = fx::detail::clamp8(v * (1.0 - amt * lineDark));
        p.lut[v] = p.lut[256 + v] = p.lut[512 + v] = out;
      }
      plan.push_back(point(p, w, h));
      return true;
    }
    case CueEffectKind::Threshold:
      p.kind = K_THRESHOLD;
      p.f0 = static_cast<float>(255.0 * std::clamp(pA, 0.02, 0.98));
      p.f1 = static_cast<float>(pB * 110.0);
      plan.push_back(point(p, w, h));
      return true;
    case CueEffectKind::Colour: {
      p.kind = K_COLOUR;
      const double angle = (pA - 0.5) * 2.0 * 3.14159265358979;
      const double cosA = std::cos(angle), sinA = std::sin(angle);
      const double m[9] = {
        0.2126 + cosA * 0.7874 - sinA * 0.2126, 0.7152 - cosA * 0.7152 - sinA * 0.7152,
        0.0722 - cosA * 0.0722 + sinA * 0.9278,
        0.2126 - cosA * 0.2126 + sinA * 0.1430, 0.7152 + cosA * 0.2848 + sinA * 0.1400,
        0.0722 - cosA * 0.0722 - sinA * 0.2830,
        0.2126 - cosA * 0.2126 - sinA * 0.7874, 0.7152 - cosA * 0.7152 + sinA * 0.7152,
        0.0722 + cosA * 0.9278 + sinA * 0.0722,
      };
      const double sector = pC * 6.0;
      const double f = sector - std::floor(sector);
      double tr = 1.0, tg = 0.0, tb = 0.0;
      switch (static_cast<int>(std::floor(sector)) % 6) {
        case 0: tr = 1.0; tg = f; tb = 0.0; break;
        case 1: tr = 1.0 - f; tg = 1.0; tb = 0.0; break;
        case 2: tr = 0.0; tg = 1.0; tb = f; break;
        case 3: tr = 0.0; tg = 1.0 - f; tb = 1.0; break;
        case 4: tr = f; tg = 0.0; tb = 1.0; break;
        default: tr = 1.0; tg = 0.0; tb = 1.0 - f; break;
      }
      const double tintLuma = 0.2126 * tr + 0.7152 * tg + 0.0722 * tb;
      float* mf[9] = {&p.f0, &p.f1, &p.f2, &p.f3, &p.f4, &p.f5, &p.f6, &p.f7, &p.f8};
      for (int i = 0; i < 9; ++i) *mf[i] = static_cast<float>(m[i]);
      p.f9 = static_cast<float>(tr - tintLuma);
      p.f10 = static_cast<float>(tg - tintLuma);
      p.f11 = static_cast<float>(tb - tintLuma);
      p.f12 = static_cast<float>(1.0 - pD);
      p.f13 = static_cast<float>(pB);
      plan.push_back(point(p, w, h));
      return true;
    }
    case CueEffectKind::ColourReplace:
      p.kind = K_COLOUR_REPLACE;
      p.f0 = static_cast<float>(pA);
      p.f1 = static_cast<float>(pC - pA);
      p.f2 = static_cast<float>(0.03 + pB * 0.22);
      p.f3 = static_cast<float>(0.02 + pD * 0.2);
      plan.push_back(point(p, w, h));
      return true;
    case CueEffectKind::Vignette: {
      p.kind = K_VIGNETTE;
      const double cx = w * 0.5, cy = h * 0.5;
      p.f0 = static_cast<float>(cx);
      p.f1 = static_cast<float>(cy);
      p.f2 = static_cast<float>(std::sqrt(cx * cx + cy * cy));
      p.f3 = static_cast<float>(std::max(0.05, pA * 2.0));
      p.f4 = static_cast<float>(2.0 + pB * 6.0);
      plan.push_back(point(p, w, h));
      return true;
    }
    case CueEffectKind::Grain: {
      p.kind = K_GRAIN;
      const std::uint32_t seed = static_cast<std::uint32_t>(ctx.frameIndex * 2654435761u) | 1u;
      std::memcpy(&p.i0, &seed, sizeof(seed));
      p.i1 = 1 + static_cast<int>(std::max(0.0, pA - 0.5) * 2.0 * 7.0);
      p.f0 = static_cast<float>(amt * 64.0);
      plan.push_back(point(p, w, h));
      return true;
    }
    case CueEffectKind::ChannelOffset: {
      p.kind = K_CHANNEL_OFFSET;
      const int shift = std::max(1, static_cast<int>(std::lround(amt * w * 0.02)));
      const double angle = (pA - 0.5) * 2.0 * 3.14159265358979323846;
      const bool horizontal = std::fabs(angle) < 1e-6;
      const int greenShift = static_cast<int>(std::lround(shift * pB));
      p.i0 = shift;
      p.i1 = greenShift;
      p.i2 = (horizontal && greenShift == 0) ? 1 : 0;
      p.f0 = static_cast<float>(std::cos(angle) * shift);
      p.f1 = static_cast<float>(std::sin(angle) * shift);
      plan.push_back(point(p, w, h));
      return true;
    }
    case CueEffectKind::Mirror:
      p.kind = K_MIRROR;
      p.i0 = std::min(7, static_cast<int>(pA * 8));
      p.f0 = static_cast<float>(0.5 + pB * 0.45);
      plan.push_back(point(p, w, h));
      return true;
    case CueEffectKind::LumaKey:
      p.kind = K_LUMA_KEY;
      rampLut(e, ctx, p.lut, true);
      plan.push_back(point(p, w, h));
      return true;
    case CueEffectKind::Edges:
      p.kind = K_EDGES;
      p.f0 = static_cast<float>((0.5 + pA * 3.0) / 4.0);
      p.f1 = static_cast<float>((e.colour >> 16) & 0xFF);
      p.f2 = static_cast<float>((e.colour >> 8) & 0xFF);
      p.f3 = static_cast<float>(e.colour & 0xFF);
      plan.push_back(point(p, w, h));
      return true;
    case CueEffectKind::PolarWarp:
    case CueEffectKind::LumaDisplace:
    case CueEffectKind::Relativistic:
    case CueEffectKind::Ripple:
    case CueEffectKind::Kaleidoscope: {
      p.kind = K_WARP;
      p.i0 = e.kind == CueEffectKind::PolarWarp ? 0 : e.kind == CueEffectKind::LumaDisplace ? 1
           : e.kind == CueEffectKind::Relativistic ? 2 : e.kind == CueEffectKind::Ripple ? 3 : 4;
      const double cx = w * 0.5, cy = h * 0.5;
      const double maxR = std::sqrt(cx * cx + cy * cy);
      p.f0 = static_cast<float>(cx);
      p.f1 = static_cast<float>(cy);
      p.f2 = static_cast<float>(maxR);
      p.f3 = static_cast<float>(static_cast<double>(ctx.frameIndex) * 0.08);
      p.f4 = static_cast<float>(ctx.pixelScale > 0.0 ? ctx.pixelScale : 1.0);
      p.i1 = 2 + static_cast<int>(pA * 10.0);
      if (e.kind == CueEffectKind::Relativistic) {
        // The CPU effect's own radial tables, built the same way.
        const double beta = amt * 0.995;
        const double gamma = 1.0 / std::sqrt(std::max(1e-6, 1.0 - beta * beta));
        const double halfFov = (12.0 + pA * 68.0) * 0.017453292519943295;
        // The shader works out the two table entries it needs (boostRadius,
        // boostGain) from these, the CPU effect's own constants.
        p.f5 = static_cast<float>(pC * cx * 0.8);
        p.f6 = static_cast<float>(beta);
        p.f7 = static_cast<float>(gamma);
        p.f8 = static_cast<float>(halfFov);
      }
      plan.push_back(point(p, w, h));
      return true;
    }
    case CueEffectKind::Mosaic: {
      const double shortSide = std::min(w, h);
      const int tile = std::max(2, static_cast<int>(std::lround(shortSide * (0.004 + pA * pA * 0.08))));
      const int tilesX = (w + tile - 1) / tile, tilesY = (h + tile - 1) / tile;
      Pass t;
      t.entry = E_TILE;
      t.params.width = static_cast<std::uint32_t>(tilesX);
      t.params.height = static_cast<std::uint32_t>(tilesY);
      t.params.srcW = static_cast<std::uint32_t>(w);
      t.params.srcH = static_cast<std::uint32_t>(h);
      t.params.i0 = tile;
      t.src = CUR;
      t.aux = CUR;
      t.dst = S1;
      t.w = tilesX;
      t.h = tilesY;
      plan.push_back(t);
      p.kind = K_MOSAIC;
      p.i0 = tile;
      p.i1 = (pB > 0.0 || pC > 0.0) ? 1 : 0;
      const double inset = pB * 0.18 * tile;
      const double half = tile * 0.5 - inset;
      p.f0 = static_cast<float>(half);
      p.f1 = static_cast<float>(pC * half);
      p.auxW = static_cast<std::uint32_t>(tilesX);
      p.auxH = static_cast<std::uint32_t>(tilesY);
      plan.push_back(point(p, w, h, S1));
      return true;
    }
    case CueEffectKind::Blur: {
      const double shortSide = std::min(w, h);
      const double radius = std::max(0.6, pA * pA * 0.06 * shortSide);
      const int factor = std::clamp(static_cast<int>(radius / 3.0), 1, 16);
      if (factor == 1 && pB <= 0.0 && amt >= 0.9995) {
        if (radius >= 0.25) {
          const int box = std::max(1, static_cast<int>(std::lround(radius * 0.5)));
          const int at = boxPasses(plan, CUR, F1, F2, w, h, box, 3);
          plan.push_back(mix(0, at, w, h, w, h, 1.0));
        }
        return true;
      }
      int sw = w, sh = h;
      int reduced = CUR;
      if (factor > 1) {
        plan.push_back(down(CUR, S1, w, h, factor, sw, sh));
        reduced = S1;
      }
      const int bufA = factor > 1 ? S1 : F1;
      const int bufB = factor > 1 ? S2 : F2;
      const double r = radius / factor;
      const double even = r * (1.0 - pB);
      if (even >= 0.25) {
        const int box = std::max(1, static_cast<int>(std::lround(even * 0.5)));
        reduced = boxPasses(plan, reduced, reduced == bufA ? bufB : bufA, reduced == bufA ? bufA : bufB, sw, sh, box, 3);
      }
      if (pB > 0.0) {
        const double length = r * pB * 3.0;
        const double angle = pC * 3.14159265358979;
        Pass s;
        s.entry = E_STREAK;
        s.params.width = s.params.srcW = static_cast<std::uint32_t>(sw);
        s.params.height = s.params.srcH = static_cast<std::uint32_t>(sh);
        s.params.f0 = static_cast<float>(std::cos(angle) * length);
        s.params.f1 = static_cast<float>(-std::sin(angle) * length);
        s.params.i0 = std::clamp(static_cast<int>(std::ceil(length * 2.0)), 2, 64);
        s.src = reduced;
        s.aux = reduced;
        s.dst = reduced == bufA ? bufB : bufA;
        s.w = sw;
        s.h = sh;
        plan.push_back(s);
        reduced = s.dst;
      }
      plan.push_back(mix(0, reduced, sw, sh, w, h, amt));
      return true;
    }
    case CueEffectKind::Glow: {
      const double shortSide = std::min(w, h);
      const double radius = (0.012 + pB * 0.07) * shortSide;
      const int factor = std::clamp(static_cast<int>(radius / 3.0), 2, 16);
      int sw = 0, sh = 0;
      Pass d = down(CUR, S1, w, h, factor, sw, sh);
      d.params.i1 = 1;
      const double knee = 0.95 - pA * 0.7;
      for (int v = 0; v < 256; ++v) {
        d.params.tab[v] = static_cast<float>(fx::detail::smoothstep01(knee - 0.08, knee + 0.08, v / 255.0));
      }
      plan.push_back(d);
      const int box = std::max(1, static_cast<int>(std::lround(radius / factor * 0.5)));
      const int at = boxPasses(plan, S1, S2, S1, sw, sh, box, 3);
      Pass m = mix(1, at, sw, sh, w, h, amt);
      m.params.f0 = static_cast<float>(1.2 * amt);
      plan.push_back(m);
      return true;
    }
    case CueEffectKind::Sharpen: {
      const int radius = 1 + static_cast<int>(std::lround(pB * 5.0));
      const int at = boxPasses(plan, CUR, F1, F2, w, h, radius, 2);
      Pass m = mix(2, at, w, h, w, h, amt);
      m.params.f0 = static_cast<float>(pA * 2.0 * amt);
      m.params.f1 = static_cast<float>(pC * 40.0);
      plan.push_back(m);
      return true;
    }
    default:
      return false;
  }
}

bool supportedKind(CueEffectKind k) {
  if (isRampLutKind(k)) return true;
  switch (k) {
    case CueEffectKind::Scanlines: case CueEffectKind::Threshold: case CueEffectKind::Colour:
    case CueEffectKind::ColourReplace: case CueEffectKind::Vignette: case CueEffectKind::Grain:
    case CueEffectKind::ChannelOffset: case CueEffectKind::Mirror: case CueEffectKind::LumaKey:
    case CueEffectKind::Edges: case CueEffectKind::PolarWarp: case CueEffectKind::LumaDisplace:
    case CueEffectKind::Relativistic: case CueEffectKind::Ripple: case CueEffectKind::Kaleidoscope:
    case CueEffectKind::Mosaic: case CueEffectKind::Blur: case CueEffectKind::Glow:
    case CueEffectKind::Sharpen:
      return true;
    default:
      return false;
  }
}

bool cpuForced() {
  static const bool forced = [] {
    const char* e = std::getenv("DECKBOY_CPU_EFFECTS");
    return e && *e && *e != '0';
  }();
  return forced;
}

// Each backend runs a plan: upload the picture into CUR, run the passes
// (swapping CUR and ALT after every pass that writes ALT), copy the result
// to the slot's display texture.
}  // namespace

bool gpuEffectKindSupported(CueEffectKind kind) { return supportedKind(kind); }

bool gpuEffectsSupportStack(const std::vector<fx::CueEffect>& stack) {
  if (cpuForced()) return false;
  bool any = false;
  for (const auto& e : stack) {
    if (e.kind == CueEffectKind::None || e.bypassed || e.amount <= 0.0005f) continue;
    if (!supportedKind(e.kind)) return false;
    any = true;
  }
  return any;
}

// ---------------------------------------------------------------------------
// The backends share this: a slot's buffers and the display texture.
struct SlotBase {
  int w = 0, h = 0;
  SDL_Texture* display = nullptr;
};

#ifdef _WIN32
namespace {
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR,
                                     UINT, UINT, ID3DBlob**, ID3DBlob**);
template <typename T> void releaseCom(T*& p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}
std::mutex gCodeLock;
std::map<std::string, std::vector<char>> gCode;   // empty = did not compile

const std::vector<char>& d3d11Bytecode(const char* entry) {
  std::lock_guard<std::mutex> hold(gCodeLock);
  auto found = gCode.find(entry);
  if (found != gCode.end()) return found->second;
  std::vector<char>& out = gCode[entry];
  static HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
  if (!compiler) return out;
  auto compile = reinterpret_cast<D3DCompileFn>(GetProcAddress(compiler, "D3DCompile"));
  if (!compile) return out;
  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  constexpr UINT kOptimise = 1u << 15;   // D3DCOMPILE_OPTIMIZATION_LEVEL3
  const HRESULT hr = compile(kEffectsHlsl, sizeof(kEffectsHlsl) - 1, "gpu_effects", nullptr, nullptr, entry,
                             "cs_5_0", kOptimise, 0, &code, &errors);
  if (FAILED(hr) || !code) {
    if (errors) {
      std::cerr << "gpu-effects: shader " << entry << " did not compile: "
                << static_cast<const char*>(errors->GetBufferPointer()) << std::endl;
    }
    releaseCom(errors);
    releaseCom(code);
    return out;
  }
  releaseCom(errors);
  const char* bytes = static_cast<const char*>(code->GetBufferPointer());
  out.assign(bytes, bytes + code->GetBufferSize());
  code->Release();
  return out;
}

struct D3DBuf {
  ID3D11Texture2D* tex = nullptr;
  ID3D11ShaderResourceView* srv = nullptr;
  ID3D11UnorderedAccessView* uav = nullptr;
  int w = 0, h = 0;
};

struct D3DSlot : SlotBase {
  D3DBuf buf[BUF_COUNT];
  ID3D11Texture2D* nv12 = nullptr;          // the input the two views below are of (borrowed)
  ID3D11Texture2D* decoded = nullptr;       // our copy of a decoder slice (owned; unused now)
  // The decoder's frame through Direct3D 11's VIDEO PROCESSOR (runGpuEffectsDecoded).
  ID3D11VideoProcessorEnumerator* vpEnum = nullptr;
  ID3D11VideoProcessor* vp = nullptr;
  ID3D11Texture2D* vpOut = nullptr;
  ID3D11VideoProcessorOutputView* vpOutView = nullptr;
  int vpInW = 0, vpInH = 0, vpOutW = 0, vpOutH = 0;
  DXGI_FORMAT vpInFormat = DXGI_FORMAT_UNKNOWN;
  unsigned vpColorspace = 0;
  ID3D11Texture2D* vpSource = nullptr;      // the decoder array the views below are of (borrowed)
  std::map<int, ID3D11VideoProcessorInputView*> vpInViews;   // by array slice
  ID3D11ShaderResourceView* nv12Luma = nullptr;
  ID3D11ShaderResourceView* nv12Chroma = nullptr;
  ID3D11Texture2D* displayTex = nullptr;   // SDL's, borrowed
  int result = CUR;                         // which buffer held the last result
  ID3D11Texture2D* staging = nullptr;
  // A transition's (runGpuTransition): its small inputs, and which picture
  // CUR holds.
  ID3D11Texture2D* auxTex = nullptr;
  ID3D11ShaderResourceView* auxSrv = nullptr;
  int auxW = 0, auxH = 0;
  std::vector<float> auxPad;
  std::uint64_t srcStamp = 0;
};

void releaseBuf(D3DBuf& b) {
  releaseCom(b.uav);
  releaseCom(b.srv);
  releaseCom(b.tex);
  b.w = b.h = 0;
}

bool makeBuf(ID3D11Device* device, D3DBuf& b, int w, int h) {
  if (b.tex && b.w == w && b.h == h) return true;
  releaseBuf(b);
  D3D11_TEXTURE2D_DESC desc {};
  desc.Width = static_cast<UINT>(w);
  desc.Height = static_cast<UINT>(h);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
  if (FAILED(device->CreateTexture2D(&desc, nullptr, &b.tex))) return false;
  if (FAILED(device->CreateShaderResourceView(b.tex, nullptr, &b.srv)) ||
      FAILED(device->CreateUnorderedAccessView(b.tex, nullptr, &b.uav))) {
    releaseBuf(b);
    return false;
  }
  b.w = w;
  b.h = h;
  return true;
}
}  // namespace
#endif

struct GpuEffects {
  SDL_Renderer* renderer = nullptr;
  std::string backend;
#ifdef _WIN32
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  ID3D11ComputeShader* shaders[E_COUNT] = {};
  ID3D11Buffer* constants = nullptr;
  std::map<int, D3DSlot> d3dSlots;
#endif
#if SDL_VERSION_ATLEAST(3, 4, 0)
  SDL_GPUDevice* gpu = nullptr;
  SDL_GPUComputePipeline* pipelines[E_COUNT] = {};
  SDL_GPUSampler* sampler = nullptr;
  struct SdlSlot : SlotBase {
    SDL_GPUTexture* buf[BUF_COUNT] = {};
    int bw[BUF_COUNT] = {}, bh[BUF_COUNT] = {};
    SDL_GPUTransferBuffer* upload = nullptr;
    std::size_t uploadBytes = 0;
    // A transition's (runGpuTransition).
    SDL_GPUTexture* auxTex = nullptr;
    int auxW = 0, auxH = 0;
    SDL_GPUTransferBuffer* auxUpload = nullptr;
    std::size_t auxUploadBytes = 0;
    std::vector<float> auxPad;
    std::uint64_t srcStamp = 0;
  };
  std::map<int, SdlSlot> sdlSlots;
#endif
};

namespace {
// The display texture SDL owns and draws: made by SDL, so its state is SDL's.
SDL_Texture* ensureDisplay(GpuEffects* g, SlotBase& slot, int w, int h) {
  if (slot.display && slot.w == w && slot.h == h) return slot.display;
  if (slot.display) SDL_DestroyTexture(slot.display);
  slot.display = deckboyCreateTexture(g->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
  slot.w = slot.display ? w : 0;
  slot.h = slot.display ? h : 0;
  return slot.display;
}

// Sizes: full size for CUR/ALT/F1/F2, half (rounded up) for S1/S2 -- every
// reduced copy is at least 2x smaller, and a pass uses only its own corner.
void bufSize(int buf, int w, int h, int& bw, int& bh) {
  if (buf == S1 || buf == S2) {
    bw = std::max(1, (w + 1) / 2);
    bh = std::max(1, (h + 1) / 2);
  } else {
    bw = w;
    bh = h;
  }
}

std::vector<Pass> makePlan(const std::vector<fx::CueEffect>& stack, int w, int h, const fx::CueEffectContext& ctx,
                           bool& ok) {
  std::vector<Pass> plan;
  ok = true;
  for (const auto& e : stack) {
    if (!planEffect(plan, e, w, h, ctx)) {
      ok = false;
      break;
    }
  }
  return plan;
}

// The picture moves from CUR to ALT with every pass that writes ALT; the
// backends swap the two buffers' roles after one. This maps a plan's Buf to
// the physical buffer given which is current.
int physical(int buf, bool swapped) {
  if (buf == CUR) return swapped ? ALT : CUR;
  if (buf == ALT) return swapped ? CUR : ALT;
  return buf;
}
}  // namespace

// ---------------------------------------------------------------------------
#ifdef _WIN32
namespace {
bool d3d11Create(GpuEffects* g) {
  g->device = static_cast<ID3D11Device*>(
    SDL_GetPointerProperty(SDL_GetRendererProperties(g->renderer), SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr));
  if (!g->device) return false;
  g->device->GetImmediateContext(&g->context);
  for (int e = 0; e < E_COUNT; ++e) {
    const auto& code = d3d11Bytecode(kEntryNames[e]);
    if (code.empty() || FAILED(g->device->CreateComputeShader(code.data(), code.size(), nullptr, &g->shaders[e]))) {
      return false;
    }
  }
  D3D11_BUFFER_DESC cb {};
  cb.ByteWidth = sizeof(Params);
  cb.Usage = D3D11_USAGE_DYNAMIC;
  cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  if (FAILED(g->device->CreateBuffer(&cb, nullptr, &g->constants))) return false;
  g->backend = "direct3d11";
  return true;
}

void releaseVp(D3DSlot& s) {
  for (auto& [slice, view] : s.vpInViews) {
    (void) slice;
    releaseCom(view);
  }
  s.vpInViews.clear();
  s.vpSource = nullptr;
  releaseCom(s.vpOutView);
  releaseCom(s.vpOut);
  releaseCom(s.vp);
  releaseCom(s.vpEnum);
  s.vpInW = s.vpInH = s.vpOutW = s.vpOutH = 0;
  s.vpInFormat = DXGI_FORMAT_UNKNOWN;
  s.vpColorspace = 0;
}

void releaseNv12Views(D3DSlot& s) {
  releaseCom(s.nv12Luma);
  releaseCom(s.nv12Chroma);
  s.nv12 = nullptr;
  releaseCom(s.decoded);
}

void d3d11Destroy(GpuEffects* g) {
  for (auto& [slot, s] : g->d3dSlots) {
    releaseVp(s);
    releaseNv12Views(s);
    for (auto& b : s.buf) releaseBuf(b);
    releaseCom(s.staging);
    releaseCom(s.auxSrv);
    releaseCom(s.auxTex);
    if (s.display) SDL_DestroyTexture(s.display);
  }
  g->d3dSlots.clear();
  releaseCom(g->constants);
  for (auto*& s : g->shaders) releaseCom(s);
  releaseCom(g->context);
}

// The picture comes in as RGBA bytes, or (nv12 non-null) as the decoder's
// NV12 texture, converted and reduced into CUR by the first pass.
SDL_Texture* d3d11Run(GpuEffects* g, int slotIndex, const std::uint8_t* rgba, int w, int h,
                      const std::vector<Pass>& plan, ID3D11Texture2D* nv12 = nullptr,
                      const Params* nv12Params = nullptr, ID3D11Texture2D* rgbaOnCard = nullptr) {
  D3DSlot& slot = g->d3dSlots[slotIndex];
  for (int b = 0; b < BUF_COUNT; ++b) {
    int bw = 0, bh = 0;
    bufSize(b, w, h, bw, bh);
    if (!makeBuf(g->device, slot.buf[b], bw, bh)) return nullptr;
  }
  SDL_Texture* display = ensureDisplay(g, slot, w, h);
  if (!display) return nullptr;
  slot.displayTex = static_cast<ID3D11Texture2D*>(
    SDL_GetPointerProperty(SDL_GetTextureProperties(display), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
  if (!slot.displayTex) return nullptr;
  // SDL batches its draws: anything it has queued runs before this, in order.
  SDL_FlushRenderer(g->renderer);
  g->context->CSSetConstantBuffers(0, 1, &g->constants);
  if (nv12 && nv12Params) {
    if (slot.nv12 != nv12) {
      releaseNv12Views(slot);
      // Keep our own decoded copy alive across the view rebuild.
      ID3D11Texture2D* keep = slot.decoded;
      if (keep) keep->AddRef();
      releaseNv12Views(slot);
      slot.decoded = keep;
      D3D11_TEXTURE2D_DESC desc {};
      nv12->GetDesc(&desc);
      // NV12 planes read as R8 / R8G8; 10-bit P010's as R16 / R16G16, whose
      // normalised values are on the same 0-1 scale, so one kernel takes both.
      const bool tenBit = desc.Format == DXGI_FORMAT_P010;
      if (desc.Format != DXGI_FORMAT_NV12 && !tenBit) return nullptr;
      // One view per PLANE, named by its slice (Direct3D 11.3): the format
      // alone picks the plane on NV12, but creating a P010 view that way
      // crashed inside the NVIDIA driver (2026-11-12).
      ID3D11Device3* device3 = nullptr;
      if (SUCCEEDED(g->device->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void**>(&device3)))) {
        D3D11_SHADER_RESOURCE_VIEW_DESC1 view {};
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        view.Format = tenBit ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8_UNORM;     // the luma plane
        view.Texture2D.PlaneSlice = 0;
        ID3D11ShaderResourceView1* luma = nullptr;
        ID3D11ShaderResourceView1* chroma = nullptr;
        const bool made = SUCCEEDED(device3->CreateShaderResourceView1(nv12, &view, &luma)) &&
                          (view.Format = tenBit ? DXGI_FORMAT_R16G16_UNORM : DXGI_FORMAT_R8G8_UNORM,
                           view.Texture2D.PlaneSlice = 1,
                           SUCCEEDED(device3->CreateShaderResourceView1(nv12, &view, &chroma)));
        device3->Release();
        slot.nv12Luma = luma;
        slot.nv12Chroma = chroma;
        if (!made) {
          releaseNv12Views(slot);
          return nullptr;
        }
      } else if (tenBit) {
        return nullptr;   // no 11.3: P010 stays on the CPU path
      } else {
        D3D11_SHADER_RESOURCE_VIEW_DESC view {};
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        view.Format = DXGI_FORMAT_R8_UNORM;     // the luma plane
        if (FAILED(g->device->CreateShaderResourceView(nv12, &view, &slot.nv12Luma))) return nullptr;
        view.Format = DXGI_FORMAT_R8G8_UNORM;   // the chroma plane
        if (FAILED(g->device->CreateShaderResourceView(nv12, &view, &slot.nv12Chroma))) {
          releaseNv12Views(slot);
          return nullptr;
        }
      }
      slot.nv12 = nv12;
    }
    D3D11_MAPPED_SUBRESOURCE mapped {};
    if (FAILED(g->context->Map(g->constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return nullptr;
    std::memcpy(mapped.pData, nv12Params, sizeof(Params));
    g->context->Unmap(g->constants, 0);
    ID3D11ShaderResourceView* views[2] = {slot.nv12Luma, slot.nv12Chroma};
    ID3D11UnorderedAccessView* out = slot.buf[CUR].uav;
    g->context->CSSetShader(g->shaders[E_NV12], nullptr, 0);
    g->context->CSSetShaderResources(0, 2, views);
    g->context->CSSetUnorderedAccessViews(0, 1, &out, nullptr);
    g->context->Dispatch(static_cast<UINT>((w + 7) / 8), static_cast<UINT>((h + 7) / 8), 1);
    ID3D11ShaderResourceView* noViews[2] = {nullptr, nullptr};
    ID3D11UnorderedAccessView* noOut = nullptr;
    g->context->CSSetUnorderedAccessViews(0, 1, &noOut, nullptr);
    g->context->CSSetShaderResources(0, 2, noViews);
  } else if (rgbaOnCard) {
    g->context->CopyResource(slot.buf[CUR].tex, rgbaOnCard);   // same size, same format
  } else {
    g->context->UpdateSubresource(slot.buf[CUR].tex, 0, nullptr, rgba, static_cast<UINT>(w * 4), 0);
  }
  bool swapped = false;
  for (const Pass& pass : plan) {
    D3D11_MAPPED_SUBRESOURCE mapped {};
    if (FAILED(g->context->Map(g->constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return nullptr;
    std::memcpy(mapped.pData, &pass.params, sizeof(Params));
    g->context->Unmap(g->constants, 0);
    ID3D11ShaderResourceView* views[2] = {slot.buf[physical(pass.src, swapped)].srv,
                                          slot.buf[physical(pass.aux, swapped)].srv};
    ID3D11UnorderedAccessView* out = slot.buf[physical(pass.dst, swapped)].uav;
    g->context->CSSetShader(g->shaders[pass.entry], nullptr, 0);
    g->context->CSSetShaderResources(0, 2, views);
    g->context->CSSetUnorderedAccessViews(0, 1, &out, nullptr);
    g->context->Dispatch(static_cast<UINT>((pass.w + 7) / 8), static_cast<UINT>((pass.h + 7) / 8), 1);
    ID3D11ShaderResourceView* noViews[2] = {nullptr, nullptr};
    ID3D11UnorderedAccessView* noOut = nullptr;
    g->context->CSSetUnorderedAccessViews(0, 1, &noOut, nullptr);
    g->context->CSSetShaderResources(0, 2, noViews);
    if (pass.dst == ALT) swapped = !swapped;
  }
  g->context->CSSetShader(nullptr, nullptr, 0);
  slot.result = physical(CUR, swapped);
  g->context->CopyResource(slot.displayTex, slot.buf[slot.result].tex);
  return display;
}

bool d3d11Read(GpuEffects* g, int slotIndex, std::vector<std::uint8_t>& rgba, int& w, int& h) {
  auto it = g->d3dSlots.find(slotIndex);
  if (it == g->d3dSlots.end() || !it->second.buf[it->second.result].tex) return false;
  D3DSlot& slot = it->second;
  w = slot.w;
  h = slot.h;
  if (!slot.staging) {
    D3D11_TEXTURE2D_DESC desc {};
    slot.buf[CUR].tex->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(g->device->CreateTexture2D(&desc, nullptr, &slot.staging))) return false;
  }
  g->context->CopyResource(slot.staging, slot.buf[slot.result].tex);
  D3D11_MAPPED_SUBRESOURCE mapped {};
  if (FAILED(g->context->Map(slot.staging, 0, D3D11_MAP_READ, 0, &mapped))) return false;
  rgba.resize(static_cast<std::size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    std::memcpy(rgba.data() + static_cast<std::size_t>(y) * w * 4,
                static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                static_cast<std::size_t>(w) * 4);
  }
  g->context->Unmap(slot.staging, 0);
  return true;
}
}  // namespace
#endif

// ---------------------------------------------------------------------------
// SDL_GPU: Metal and Direct3D 12 (and Vulkan with SPIR-V). The work goes in
// its own command buffer, submitted at once: SDL's renderer submits its own
// at present, after it, on the same device, so the draws see the result.
#if SDL_VERSION_ATLEAST(3, 4, 0)
namespace {
bool sdlCreate(GpuEffects* g) {
  g->gpu = static_cast<SDL_GPUDevice*>(
    SDL_GetPointerProperty(SDL_GetRendererProperties(g->renderer), SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr));
  if (!g->gpu) {
    const char* name = SDL_GetRendererName(g->renderer);
    std::cerr << "gpu-effects: renderer " << (name ? name : "?") << " has neither Direct3D 11 nor SDL_GPU -- CPU effects"
              << std::endl;
    return false;
  }
  const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(g->gpu);
  const unsigned char* dxil[E_COUNT] = {kEffectsPointDxil, kEffectsDownDxil, kEffectsBoxDxil, kEffectsStreakDxil,
                                        kEffectsTileDxil, kEffectsNv12Dxil, kEffectsTransDxil};
  const std::size_t dxilSize[E_COUNT] = {sizeof(kEffectsPointDxil), sizeof(kEffectsDownDxil), sizeof(kEffectsBoxDxil),
                                         sizeof(kEffectsStreakDxil), sizeof(kEffectsTileDxil),
                                         sizeof(kEffectsNv12Dxil), sizeof(kEffectsTransDxil)};
  const unsigned char* spirv[E_COUNT] = {kEffectsPointSpirv, kEffectsDownSpirv, kEffectsBoxSpirv,
                                         kEffectsStreakSpirv, kEffectsTileSpirv, kEffectsNv12Spirv,
                                         kEffectsTransSpirv};
  const std::size_t spirvSize[E_COUNT] = {sizeof(kEffectsPointSpirv), sizeof(kEffectsDownSpirv),
                                          sizeof(kEffectsBoxSpirv), sizeof(kEffectsStreakSpirv),
                                          sizeof(kEffectsTileSpirv), sizeof(kEffectsNv12Spirv),
                                          sizeof(kEffectsTransSpirv)};
  for (int e = 0; e < E_COUNT; ++e) {
    SDL_GPUComputePipelineCreateInfo info {};
    info.entrypoint = kEntryNames[e];
    if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0) {
      info.format = SDL_GPU_SHADERFORMAT_MSL;
      info.code = reinterpret_cast<const Uint8*>(kEffectsMsl);
      info.code_size = sizeof(kEffectsMsl) - 1;
    } else if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0 && !kEffectsPointSpirvMissing) {
      info.format = SDL_GPU_SHADERFORMAT_SPIRV;
      info.code = spirv[e];
      info.code_size = spirvSize[e];
    } else if ((formats & SDL_GPU_SHADERFORMAT_DXIL) != 0) {
      info.format = SDL_GPU_SHADERFORMAT_DXIL;
      info.code = dxil[e];
      info.code_size = dxilSize[e];
    } else {
      std::cerr << "gpu-effects: no shader this GPU device takes (formats " << formats << ") -- CPU effects" << std::endl;
      return false;
    }
    info.num_samplers = 2;
    info.num_readwrite_storage_textures = 1;
    info.num_uniform_buffers = 1;
    info.threadcount_x = 8;
    info.threadcount_y = 8;
    info.threadcount_z = 1;
    g->pipelines[e] = SDL_CreateGPUComputePipeline(g->gpu, &info);
    if (!g->pipelines[e]) {
      std::cerr << "gpu-effects: the " << info.entrypoint << " pipeline did not build: " << SDL_GetError() << std::endl;
      return false;
    }
  }
  SDL_GPUSamplerCreateInfo sampler {};
  sampler.min_filter = SDL_GPU_FILTER_NEAREST;
  sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
  sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
  sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
  g->sampler = SDL_CreateGPUSampler(g->gpu, &sampler);
  if (!g->sampler) return false;
  const char* driver = SDL_GetGPUDeviceDriver(g->gpu);
  g->backend = std::string("sdl-gpu/") + (driver ? driver : "?");
  return true;
}

void sdlReleaseSlot(GpuEffects* g, GpuEffects::SdlSlot& s) {
  for (auto*& t : s.buf) {
    if (t) SDL_ReleaseGPUTexture(g->gpu, t);
    t = nullptr;
  }
  if (s.upload) SDL_ReleaseGPUTransferBuffer(g->gpu, s.upload);
  s.upload = nullptr;
  s.uploadBytes = 0;
  if (s.auxTex) SDL_ReleaseGPUTexture(g->gpu, s.auxTex);
  s.auxTex = nullptr;
  s.auxW = s.auxH = 0;
  if (s.auxUpload) SDL_ReleaseGPUTransferBuffer(g->gpu, s.auxUpload);
  s.auxUpload = nullptr;
  s.auxUploadBytes = 0;
  s.srcStamp = 0;
  if (s.display) SDL_DestroyTexture(s.display);
  s.display = nullptr;
}

void sdlDestroy(GpuEffects* g) {
  if (!g->gpu) return;
  SDL_WaitForGPUIdle(g->gpu);
  for (auto& [slot, s] : g->sdlSlots) sdlReleaseSlot(g, s);
  g->sdlSlots.clear();
  for (auto*& p : g->pipelines) {
    if (p) SDL_ReleaseGPUComputePipeline(g->gpu, p);
    p = nullptr;
  }
  if (g->sampler) SDL_ReleaseGPUSampler(g->gpu, g->sampler);
  g->sampler = nullptr;
}

SDL_Texture* sdlRun(GpuEffects* g, int slotIndex, const std::uint8_t* rgba, int w, int h,
                    const std::vector<Pass>& plan) {
  GpuEffects::SdlSlot& slot = g->sdlSlots[slotIndex];
  for (int b = 0; b < BUF_COUNT; ++b) {
    int bw = 0, bh = 0;
    bufSize(b, w, h, bw, bh);
    if (slot.buf[b] && slot.bw[b] == bw && slot.bh[b] == bh) continue;
    if (slot.buf[b]) SDL_ReleaseGPUTexture(g->gpu, slot.buf[b]);
    SDL_GPUTextureCreateInfo info {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    info.width = static_cast<Uint32>(bw);
    info.height = static_cast<Uint32>(bh);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    slot.buf[b] = SDL_CreateGPUTexture(g->gpu, &info);
    if (!slot.buf[b]) return nullptr;
    slot.bw[b] = bw;
    slot.bh[b] = bh;
  }
  SDL_Texture* display = ensureDisplay(g, slot, w, h);
  if (!display) return nullptr;
  auto* displayTex = static_cast<SDL_GPUTexture*>(
    SDL_GetPointerProperty(SDL_GetTextureProperties(display), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
  if (!displayTex) return nullptr;
  const std::size_t bytes = static_cast<std::size_t>(w) * h * 4;
  if (!slot.upload || slot.uploadBytes != bytes) {
    if (slot.upload) SDL_ReleaseGPUTransferBuffer(g->gpu, slot.upload);
    SDL_GPUTransferBufferCreateInfo info {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = static_cast<Uint32>(bytes);
    slot.upload = SDL_CreateGPUTransferBuffer(g->gpu, &info);
    slot.uploadBytes = slot.upload ? bytes : 0;
    if (!slot.upload) return nullptr;
  }
  void* mapped = SDL_MapGPUTransferBuffer(g->gpu, slot.upload, true);   // cycle: the last one may be in flight
  if (!mapped) return nullptr;
  std::memcpy(mapped, rgba, bytes);
  SDL_UnmapGPUTransferBuffer(g->gpu, slot.upload);
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g->gpu);
  if (!cmd) return nullptr;
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureTransferInfo from {slot.upload, 0, static_cast<Uint32>(w), static_cast<Uint32>(h)};
  SDL_GPUTextureRegion to {};
  to.texture = slot.buf[CUR];
  to.w = static_cast<Uint32>(w);
  to.h = static_cast<Uint32>(h);
  to.d = 1;
  SDL_UploadToGPUTexture(copy, &from, &to, false);
  SDL_EndGPUCopyPass(copy);
  bool swapped = false;
  for (const Pass& pass : plan) {
    SDL_GPUStorageTextureReadWriteBinding out {};
    out.texture = slot.buf[physical(pass.dst, swapped)];
    out.cycle = false;
    SDL_GPUComputePass* cp = SDL_BeginGPUComputePass(cmd, &out, 1, nullptr, 0);
    SDL_BindGPUComputePipeline(cp, g->pipelines[pass.entry]);
    const SDL_GPUTextureSamplerBinding views[2] = {{slot.buf[physical(pass.src, swapped)], g->sampler},
                                                   {slot.buf[physical(pass.aux, swapped)], g->sampler}};
    SDL_BindGPUComputeSamplers(cp, 0, views, 2);
    SDL_PushGPUComputeUniformData(cmd, 0, &pass.params, sizeof(Params));
    SDL_DispatchGPUCompute(cp, static_cast<Uint32>((pass.w + 7) / 8), static_cast<Uint32>((pass.h + 7) / 8), 1);
    SDL_EndGPUComputePass(cp);
    if (pass.dst == ALT) swapped = !swapped;
  }
  copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureLocation src {};
  src.texture = slot.buf[physical(CUR, swapped)];
  SDL_GPUTextureLocation dst {};
  dst.texture = displayTex;
  SDL_CopyGPUTextureToTexture(copy, &src, &dst, static_cast<Uint32>(w), static_cast<Uint32>(h), 1, false);
  SDL_EndGPUCopyPass(copy);
  if (!SDL_SubmitGPUCommandBuffer(cmd)) return nullptr;
  return display;
}

bool sdlRead(GpuEffects* g, int slotIndex, std::vector<std::uint8_t>& rgba, int& w, int& h) {
  auto it = g->sdlSlots.find(slotIndex);
  if (it == g->sdlSlots.end() || !it->second.display) return false;
  auto& slot = it->second;
  w = slot.w;
  h = slot.h;
  auto* displayTex = static_cast<SDL_GPUTexture*>(
    SDL_GetPointerProperty(SDL_GetTextureProperties(slot.display), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
  if (!displayTex) return false;
  const Uint32 bytes = static_cast<Uint32>(w * h * 4);
  SDL_GPUTransferBufferCreateInfo info {};
  info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
  info.size = bytes;
  SDL_GPUTransferBuffer* down = SDL_CreateGPUTransferBuffer(g->gpu, &info);
  if (!down) return false;
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g->gpu);
  bool ok = cmd != nullptr;
  if (ok) {
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion region {};
    region.texture = displayTex;
    region.w = static_cast<Uint32>(w);
    region.h = static_cast<Uint32>(h);
    region.d = 1;
    SDL_GPUTextureTransferInfo to {down, 0, static_cast<Uint32>(w), static_cast<Uint32>(h)};
    SDL_DownloadFromGPUTexture(copy, &region, &to);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    ok = fence != nullptr;
    if (ok) {
      SDL_WaitForGPUFences(g->gpu, true, &fence, 1);
      SDL_ReleaseGPUFence(g->gpu, fence);
      if (void* mapped = SDL_MapGPUTransferBuffer(g->gpu, down, false)) {
        rgba.assign(static_cast<const std::uint8_t*>(mapped), static_cast<const std::uint8_t*>(mapped) + bytes);
        SDL_UnmapGPUTransferBuffer(g->gpu, down);
      } else {
        ok = false;
      }
    }
  }
  SDL_ReleaseGPUTransferBuffer(g->gpu, down);
  return ok;
}
}  // namespace
#endif

// ---------------------------------------------------------------------------
GpuEffects* createGpuEffects(SDL_Renderer* renderer) {
  if (!renderer || cpuForced()) return nullptr;
  auto* g = new GpuEffects();
  g->renderer = renderer;
  bool ok = false;
#ifdef _WIN32
  ok = d3d11Create(g);
  if (!ok) d3d11Destroy(g);
#endif
#if SDL_VERSION_ATLEAST(3, 4, 0)
  if (!ok) {
    ok = sdlCreate(g);
    if (!ok) sdlDestroy(g);
  }
#endif
  if (!ok) {
    delete g;
    return nullptr;
  }
  std::cerr << "gpu-effects: on " << g->backend << std::endl;
  return g;
}

void destroyGpuEffects(GpuEffects* g) {
  if (!g) return;
#ifdef _WIN32
  d3d11Destroy(g);
#endif
#if SDL_VERSION_ATLEAST(3, 4, 0)
  sdlDestroy(g);
#endif
  delete g;
}

std::string gpuEffectsBackend(const GpuEffects* g) { return g ? g->backend : std::string(); }

SDL_Texture* runGpuEffects(GpuEffects* g, int slot, const std::uint8_t* rgba, int w, int h,
                           const std::vector<fx::CueEffect>& stack, const fx::CueEffectContext& ctx) {
  if (!g || !rgba || w < 2 || h < 2) return nullptr;
  bool ok = false;
  const std::vector<Pass> plan = makePlan(stack, w, h, ctx, ok);
  if (!ok) return nullptr;
#ifdef _WIN32
  if (g->device) return d3d11Run(g, slot, rgba, w, h, plan);
#endif
#if SDL_VERSION_ATLEAST(3, 4, 0)
  if (g->gpu) return sdlRun(g, slot, rgba, w, h, plan);
#endif
  return nullptr;
}

bool gpuEffectsTakeNv12(const GpuEffects* g) {
#ifdef _WIN32
  return g && g->device != nullptr;
#else
  (void) g;
  return false;
#endif
}

SDL_Texture* runGpuEffectsNv12(GpuEffects* g, int slot, SDL_Texture* nv12, int srcW, int srcH, int shrink,
                               unsigned colorspace, const std::vector<fx::CueEffect>& stack,
                               const fx::CueEffectContext& ctx) {
  if (!gpuEffectsTakeNv12(g) || !nv12 || srcW < 4 || srcH < 4) return nullptr;
#ifdef _WIN32
  shrink = std::clamp(shrink, 1, 8);
  const int w = std::max(2, srcW / shrink);
  const int h = std::max(2, srcH / shrink);
  auto* texture = static_cast<ID3D11Texture2D*>(
    SDL_GetPointerProperty(SDL_GetTextureProperties(nv12), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
  if (!texture) return nullptr;
  bool ok = false;
  fx::CueEffectContext sized = ctx;
  sized.width = w;
  sized.height = h;
  const std::vector<Pass> plan = makePlan(stack, w, h, sized, ok);
  if (!ok) return nullptr;
  Params in;
  in.width = static_cast<std::uint32_t>(w);
  in.height = static_cast<std::uint32_t>(h);
  in.srcW = static_cast<std::uint32_t>(srcW);
  in.srcH = static_cast<std::uint32_t>(srcH);
  in.i0 = shrink;
  nv12Constants(colorspace, srcW, in);
  return d3d11Run(g, slot, nullptr, w, h, plan, texture, &in);
#else
  (void) slot; (void) shrink; (void) colorspace; (void) stack; (void) ctx;
  return nullptr;
#endif
}

// The decoder's frame (NV12 or 10-bit P010) through the VIDEO PROCESSOR: the
// fixed-function block players use to show it, which reads the decode surface
// in place -- an input view on the array slice, no copy -- and converts it to
// RGB with the frame's own matrix and range, scaled to the size the effects
// run at. (A shader view on a P010 texture crashed inside the NVIDIA driver,
// 2026-11-12; this is the path the driver is built for.)
#ifdef _WIN32
namespace {
DXGI_COLOR_SPACE_TYPE vpInputColorSpace(unsigned colorspace, int width) {
  const SDL_Colorspace cs = static_cast<SDL_Colorspace>(colorspace);
  const bool full = SDL_COLORSPACERANGE(cs) == SDL_COLOR_RANGE_FULL;
  switch (SDL_COLORSPACEMATRIX(cs)) {
    case SDL_MATRIX_COEFFICIENTS_BT601:
    case SDL_MATRIX_COEFFICIENTS_BT470BG:
      return full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
    case SDL_MATRIX_COEFFICIENTS_BT2020_NCL:
      return full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P2020 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P2020;
    case SDL_MATRIX_COEFFICIENTS_BT709:
      return full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    default:
      if (width < 1280) {
        return full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P601 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P601;
      }
      return full ? DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709 : DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
  }
}

// Converts slice `subresource` of `source` into slot.vpOut (w x h, RGBA8).
bool videoProcess(GpuEffects* g, D3DSlot& slot, ID3D11Texture2D* source, int subresource, DXGI_FORMAT format,
                  int cw, int ch, int w, int h, unsigned colorspace) {
  ID3D11VideoDevice* videoDevice = nullptr;
  ID3D11VideoContext* videoContext = nullptr;
  if (FAILED(g->device->QueryInterface(__uuidof(ID3D11VideoDevice), reinterpret_cast<void**>(&videoDevice))) ||
      FAILED(g->context->QueryInterface(__uuidof(ID3D11VideoContext), reinterpret_cast<void**>(&videoContext)))) {
    releaseCom(videoDevice);
    return false;
  }
  bool ok = true;
  if (!slot.vp || slot.vpInW != cw || slot.vpInH != ch || slot.vpOutW != w || slot.vpOutH != h ||
      slot.vpInFormat != format) {
    releaseVp(slot);
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content {};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate = {30, 1};
    content.InputWidth = static_cast<UINT>(cw);
    content.InputHeight = static_cast<UINT>(ch);
    content.OutputFrameRate = {30, 1};
    content.OutputWidth = static_cast<UINT>(w);
    content.OutputHeight = static_cast<UINT>(h);
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    UINT inSupport = 0, outSupport = 0;
    ok = SUCCEEDED(videoDevice->CreateVideoProcessorEnumerator(&content, &slot.vpEnum)) &&
         SUCCEEDED(slot.vpEnum->CheckVideoProcessorFormat(format, &inSupport)) &&
         (inSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) &&
         SUCCEEDED(slot.vpEnum->CheckVideoProcessorFormat(DXGI_FORMAT_R8G8B8A8_UNORM, &outSupport)) &&
         (outSupport & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) &&
         SUCCEEDED(videoDevice->CreateVideoProcessor(slot.vpEnum, 0, &slot.vp));
    if (ok) {
      D3D11_TEXTURE2D_DESC out {};
      out.Width = static_cast<UINT>(w);
      out.Height = static_cast<UINT>(h);
      out.MipLevels = 1;
      out.ArraySize = 1;
      out.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      out.SampleDesc.Count = 1;
      out.Usage = D3D11_USAGE_DEFAULT;
      out.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
      D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outView {};
      outView.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
      ok = SUCCEEDED(g->device->CreateTexture2D(&out, nullptr, &slot.vpOut)) &&
           SUCCEEDED(videoDevice->CreateVideoProcessorOutputView(slot.vpOut, slot.vpEnum, &outView, &slot.vpOutView));
    }
    if (ok) {
      slot.vpInW = cw;
      slot.vpInH = ch;
      slot.vpOutW = w;
      slot.vpOutH = h;
      slot.vpInFormat = format;
      const RECT sourceRect {0, 0, cw, ch};   // the decode surface is padded (2176 rows for 2160)
      const RECT destRect {0, 0, w, h};
      videoContext->VideoProcessorSetStreamFrameFormat(slot.vp, 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
      videoContext->VideoProcessorSetStreamAutoProcessingMode(slot.vp, 0, FALSE);
      videoContext->VideoProcessorSetStreamSourceRect(slot.vp, 0, TRUE, &sourceRect);
      videoContext->VideoProcessorSetStreamDestRect(slot.vp, 0, TRUE, &destRect);
      videoContext->VideoProcessorSetOutputTargetRect(slot.vp, TRUE, &destRect);
    } else {
      releaseVp(slot);
    }
  }
  if (ok && slot.vpColorspace != colorspace) {
    ID3D11VideoContext1* videoContext1 = nullptr;
    if (SUCCEEDED(videoContext->QueryInterface(__uuidof(ID3D11VideoContext1), reinterpret_cast<void**>(&videoContext1)))) {
      videoContext1->VideoProcessorSetStreamColorSpace1(slot.vp, 0, vpInputColorSpace(colorspace, cw));
      videoContext1->VideoProcessorSetOutputColorSpace1(slot.vp, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
      videoContext1->Release();
    } else {
      const SDL_Colorspace cs = static_cast<SDL_Colorspace>(colorspace);
      D3D11_VIDEO_PROCESSOR_COLOR_SPACE in {};
      in.YCbCr_Matrix = SDL_COLORSPACEMATRIX(cs) == SDL_MATRIX_COEFFICIENTS_BT601 ? 0 : 1;
      in.Nominal_Range = SDL_COLORSPACERANGE(cs) == SDL_COLOR_RANGE_FULL ? D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255
                                                                         : D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
      videoContext->VideoProcessorSetStreamColorSpace(slot.vp, 0, &in);
      D3D11_VIDEO_PROCESSOR_COLOR_SPACE out {};
      out.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
      videoContext->VideoProcessorSetOutputColorSpace(slot.vp, &out);
    }
    slot.vpColorspace = colorspace;
  }
  ID3D11VideoProcessorInputView* inView = nullptr;
  if (ok) {
    if (slot.vpSource != source || slot.vpInViews.size() > 128) {
      for (auto& [slice, view] : slot.vpInViews) {
        (void) slice;
        releaseCom(view);
      }
      slot.vpInViews.clear();
      slot.vpSource = source;
    }
    auto found = slot.vpInViews.find(subresource);
    if (found != slot.vpInViews.end()) {
      inView = found->second;
    } else {
      D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inDesc {};
      inDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
      inDesc.Texture2D.ArraySlice = static_cast<UINT>(subresource);
      if (SUCCEEDED(videoDevice->CreateVideoProcessorInputView(source, slot.vpEnum, &inDesc, &inView))) {
        slot.vpInViews[subresource] = inView;
      } else {
        ok = false;
      }
    }
  }
  if (ok) {
    D3D11_VIDEO_PROCESSOR_STREAM stream {};
    stream.Enable = TRUE;
    stream.pInputSurface = inView;
    ok = SUCCEEDED(videoContext->VideoProcessorBlt(slot.vp, slot.vpOutView, 0, 1, &stream));
  }
  releaseCom(videoContext);
  releaseCom(videoDevice);
  return ok;
}
}  // namespace
#endif

SDL_Texture* runGpuEffectsDecoded(GpuEffects* g, int slot, void* texture2D, int subresource, int srcW, int srcH,
                                  int shrink, unsigned colorspace, const std::vector<fx::CueEffect>& stack,
                                  const fx::CueEffectContext& ctx) {
  if (!gpuEffectsTakeNv12(g) || !texture2D || srcW < 4 || srcH < 4) return nullptr;
#ifdef _WIN32
  auto* source = static_cast<ID3D11Texture2D*>(texture2D);
  ID3D11Device* sourceDevice = nullptr;
  source->GetDevice(&sourceDevice);
  const bool sameDevice = sourceDevice == g->device;
  releaseCom(sourceDevice);
  if (!sameDevice) return nullptr;
  D3D11_TEXTURE2D_DESC desc {};
  source->GetDesc(&desc);
  if (desc.Format != DXGI_FORMAT_NV12 && desc.Format != DXGI_FORMAT_P010) return nullptr;
  const int cw = srcW & ~1;
  const int ch = srcH & ~1;
  shrink = std::clamp(shrink, 1, 8);
  const int w = std::max(2, cw / shrink);
  const int h = std::max(2, ch / shrink);
  bool ok = false;
  fx::CueEffectContext sized = ctx;
  sized.width = w;
  sized.height = h;
  const std::vector<Pass> plan = makePlan(stack, w, h, sized, ok);
  if (!ok) return nullptr;
  D3DSlot& s = g->d3dSlots[slot];
  if (!videoProcess(g, s, source, subresource, desc.Format, cw, ch, w, h, colorspace)) return nullptr;
  return d3d11Run(g, slot, nullptr, w, h, plan, nullptr, nullptr, s.vpOut);
#else
  (void) slot; (void) subresource; (void) shrink; (void) colorspace; (void) stack; (void) ctx;
  return nullptr;
#endif
}

// ---------------------------------------------------------------------------
// TRANSITIONS (runGpuTransition): the outgoing picture into CUR -- only when
// it changed -- the frame's small inputs into the aux texture, one transMain
// pass into ALT, and ALT into the display texture.
namespace {
Params transitionParams(const TransitionPlan& plan, int auxW) {
  Params p;
  p.kind = static_cast<std::uint32_t>(plan.style);
  p.width = static_cast<std::uint32_t>(plan.width);
  p.height = static_cast<std::uint32_t>(plan.height);
  p.srcW = p.width;
  p.srcH = p.height;
  p.auxW = static_cast<std::uint32_t>(auxW);
  p.i0 = plan.i[0];
  p.i1 = plan.i[1];
  p.i2 = plan.i[2];
  p.i3 = plan.i[3];
  p.f0 = plan.f[0];
  p.f1 = plan.f[1];
  p.f2 = plan.f[2];
  p.f3 = plan.f[3];
  p.f4 = plan.f[4];
  p.f5 = plan.f[5];
  p.f6 = plan.f[6];
  p.f7 = plan.f[7];
  return p;
}

#ifdef _WIN32
SDL_Texture* d3d11Transition(GpuEffects* g, int slotIndex, const TransitionSource& source, const TransitionPlan& plan) {
  const int w = plan.width;
  const int h = plan.height;
  D3DSlot& slot = g->d3dSlots[slotIndex];
  if (slot.buf[CUR].w != w || slot.buf[CUR].h != h) slot.srcStamp = 0;
  if (!makeBuf(g->device, slot.buf[CUR], w, h) || !makeBuf(g->device, slot.buf[ALT], w, h)) return nullptr;
  SDL_Texture* display = ensureDisplay(g, slot, w, h);
  if (!display) return nullptr;
  slot.displayTex = static_cast<ID3D11Texture2D*>(
    SDL_GetPointerProperty(SDL_GetTextureProperties(display), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
  if (!slot.displayTex) return nullptr;
  SDL_FlushRenderer(g->renderer);   // SDL's queued draws first, in order

  if (source.stamp == 0 || source.stamp != slot.srcStamp) {
    slot.srcStamp = 0;
    if (source.decoderTexture2D) {
      auto* tex = static_cast<ID3D11Texture2D*>(source.decoderTexture2D);
      ID3D11Device* device = nullptr;
      tex->GetDevice(&device);
      const bool same = device == g->device;
      releaseCom(device);
      D3D11_TEXTURE2D_DESC desc {};
      tex->GetDesc(&desc);
      if (!same || (desc.Format != DXGI_FORMAT_NV12 && desc.Format != DXGI_FORMAT_P010)) return nullptr;
      if (!videoProcess(g, slot, tex, source.decoderSubresource, desc.Format, w, h, w, h, source.colorspace)) {
        return nullptr;
      }
      g->context->CopyResource(slot.buf[CUR].tex, slot.vpOut);
    } else if (source.texture) {
      auto* tex = static_cast<ID3D11Texture2D*>(SDL_GetPointerProperty(
        SDL_GetTextureProperties(source.texture), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
      if (!tex) return nullptr;
      D3D11_TEXTURE2D_DESC desc {};
      tex->GetDesc(&desc);
      if (static_cast<int>(desc.Width) != w || static_cast<int>(desc.Height) != h) return nullptr;
      g->context->CopyResource(slot.buf[CUR].tex, tex);
    } else if (source.rgba) {
      g->context->UpdateSubresource(slot.buf[CUR].tex, 0, nullptr, source.rgba, static_cast<UINT>(w * 4), 0);
    } else {
      return nullptr;
    }
    slot.srcStamp = source.stamp;
  }

  int tw = 0, th = 0;
  auxShape(plan.aux.size(), tw, th);
  if (!slot.auxTex || slot.auxW != tw || slot.auxH != th) {
    releaseCom(slot.auxSrv);
    releaseCom(slot.auxTex);
    slot.auxW = slot.auxH = 0;
    D3D11_TEXTURE2D_DESC desc {};
    desc.Width = static_cast<UINT>(tw);
    desc.Height = static_cast<UINT>(th);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g->device->CreateTexture2D(&desc, nullptr, &slot.auxTex)) ||
        FAILED(g->device->CreateShaderResourceView(slot.auxTex, nullptr, &slot.auxSrv))) {
      releaseCom(slot.auxSrv);
      releaseCom(slot.auxTex);
      return nullptr;
    }
    slot.auxW = tw;
    slot.auxH = th;
  }
  const float* auxData = auxPadded(plan.aux, tw, th, slot.auxPad);
  g->context->UpdateSubresource(slot.auxTex, 0, nullptr, auxData, static_cast<UINT>(tw * 16), 0);

  const Params params = transitionParams(plan, tw);
  D3D11_MAPPED_SUBRESOURCE mapped {};
  if (FAILED(g->context->Map(g->constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return nullptr;
  std::memcpy(mapped.pData, &params, sizeof(Params));
  g->context->Unmap(g->constants, 0);
  g->context->CSSetConstantBuffers(0, 1, &g->constants);
  ID3D11ShaderResourceView* views[2] = {slot.buf[CUR].srv, slot.auxSrv};
  ID3D11UnorderedAccessView* out = slot.buf[ALT].uav;
  g->context->CSSetShader(g->shaders[E_TRANS], nullptr, 0);
  g->context->CSSetShaderResources(0, 2, views);
  g->context->CSSetUnorderedAccessViews(0, 1, &out, nullptr);
  g->context->Dispatch(static_cast<UINT>((w + 7) / 8), static_cast<UINT>((h + 7) / 8), 1);
  ID3D11ShaderResourceView* noViews[2] = {nullptr, nullptr};
  ID3D11UnorderedAccessView* noOut = nullptr;
  g->context->CSSetUnorderedAccessViews(0, 1, &noOut, nullptr);
  g->context->CSSetShaderResources(0, 2, noViews);
  g->context->CSSetShader(nullptr, nullptr, 0);
  slot.result = ALT;
  g->context->CopyResource(slot.displayTex, slot.buf[ALT].tex);
  return display;
}
#endif

#if SDL_VERSION_ATLEAST(3, 4, 0)
SDL_Texture* sdlTransition(GpuEffects* g, int slotIndex, const TransitionSource& source, const TransitionPlan& plan) {
  if (source.decoderTexture2D && !source.texture && !source.rgba) return nullptr;   // Direct3D 11's alone
  const int w = plan.width;
  const int h = plan.height;
  GpuEffects::SdlSlot& slot = g->sdlSlots[slotIndex];
  for (int b : {static_cast<int>(CUR), static_cast<int>(ALT)}) {
    if (slot.buf[b] && slot.bw[b] == w && slot.bh[b] == h) continue;
    if (slot.buf[b]) SDL_ReleaseGPUTexture(g->gpu, slot.buf[b]);
    SDL_GPUTextureCreateInfo info {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    info.width = static_cast<Uint32>(w);
    info.height = static_cast<Uint32>(h);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    slot.buf[b] = SDL_CreateGPUTexture(g->gpu, &info);
    slot.srcStamp = 0;
    if (!slot.buf[b]) return nullptr;
    slot.bw[b] = w;
    slot.bh[b] = h;
  }
  SDL_Texture* display = ensureDisplay(g, slot, w, h);
  if (!display) return nullptr;
  auto* displayTex = static_cast<SDL_GPUTexture*>(
    SDL_GetPointerProperty(SDL_GetTextureProperties(display), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
  if (!displayTex) return nullptr;

  int tw = 0, th = 0;
  auxShape(plan.aux.size(), tw, th);
  if (!slot.auxTex || slot.auxW != tw || slot.auxH != th) {
    if (slot.auxTex) SDL_ReleaseGPUTexture(g->gpu, slot.auxTex);
    SDL_GPUTextureCreateInfo info {};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(tw);
    info.height = static_cast<Uint32>(th);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    slot.auxTex = SDL_CreateGPUTexture(g->gpu, &info);
    slot.auxW = slot.auxTex ? tw : 0;
    slot.auxH = slot.auxTex ? th : 0;
    if (!slot.auxTex) return nullptr;
  }
  const std::size_t auxBytes = static_cast<std::size_t>(tw) * th * 16;
  if (!slot.auxUpload || slot.auxUploadBytes != auxBytes) {
    if (slot.auxUpload) SDL_ReleaseGPUTransferBuffer(g->gpu, slot.auxUpload);
    SDL_GPUTransferBufferCreateInfo info {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = static_cast<Uint32>(auxBytes);
    slot.auxUpload = SDL_CreateGPUTransferBuffer(g->gpu, &info);
    slot.auxUploadBytes = slot.auxUpload ? auxBytes : 0;
    if (!slot.auxUpload) return nullptr;
  }
  void* auxMapped = SDL_MapGPUTransferBuffer(g->gpu, slot.auxUpload, true);
  if (!auxMapped) return nullptr;
  std::memcpy(auxMapped, auxPadded(plan.aux, tw, th, slot.auxPad), auxBytes);
  SDL_UnmapGPUTransferBuffer(g->gpu, slot.auxUpload);

  const bool fresh = source.stamp == 0 || source.stamp != slot.srcStamp;
  SDL_GPUTexture* fromTexture = nullptr;
  const std::size_t bytes = static_cast<std::size_t>(w) * h * 4;
  if (fresh && source.texture) {
    fromTexture = static_cast<SDL_GPUTexture*>(SDL_GetPointerProperty(
      SDL_GetTextureProperties(source.texture), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    float sw = 0.0f, sh = 0.0f;
    SDL_GetTextureSize(source.texture, &sw, &sh);
    if (!fromTexture || static_cast<int>(sw) != w || static_cast<int>(sh) != h) return nullptr;
  } else if (fresh && source.rgba) {
    if (!slot.upload || slot.uploadBytes != bytes) {
      if (slot.upload) SDL_ReleaseGPUTransferBuffer(g->gpu, slot.upload);
      SDL_GPUTransferBufferCreateInfo info {};
      info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
      info.size = static_cast<Uint32>(bytes);
      slot.upload = SDL_CreateGPUTransferBuffer(g->gpu, &info);
      slot.uploadBytes = slot.upload ? bytes : 0;
      if (!slot.upload) return nullptr;
    }
    void* mapped = SDL_MapGPUTransferBuffer(g->gpu, slot.upload, true);
    if (!mapped) return nullptr;
    std::memcpy(mapped, source.rgba, bytes);
    SDL_UnmapGPUTransferBuffer(g->gpu, slot.upload);
  } else if (fresh) {
    return nullptr;
  }

  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g->gpu);
  if (!cmd) return nullptr;
  SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
  if (fresh && fromTexture) {
    SDL_GPUTextureLocation from {};
    from.texture = fromTexture;
    SDL_GPUTextureLocation to {};
    to.texture = slot.buf[CUR];
    SDL_CopyGPUTextureToTexture(copy, &from, &to, static_cast<Uint32>(w), static_cast<Uint32>(h), 1, false);
  } else if (fresh) {
    SDL_GPUTextureTransferInfo from {slot.upload, 0, static_cast<Uint32>(w), static_cast<Uint32>(h)};
    SDL_GPUTextureRegion to {};
    to.texture = slot.buf[CUR];
    to.w = static_cast<Uint32>(w);
    to.h = static_cast<Uint32>(h);
    to.d = 1;
    SDL_UploadToGPUTexture(copy, &from, &to, false);
  }
  {
    SDL_GPUTextureTransferInfo from {slot.auxUpload, 0, static_cast<Uint32>(tw), static_cast<Uint32>(th)};
    SDL_GPUTextureRegion to {};
    to.texture = slot.auxTex;
    to.w = static_cast<Uint32>(tw);
    to.h = static_cast<Uint32>(th);
    to.d = 1;
    SDL_UploadToGPUTexture(copy, &from, &to, false);
  }
  SDL_EndGPUCopyPass(copy);
  SDL_GPUStorageTextureReadWriteBinding out {};
  out.texture = slot.buf[ALT];
  out.cycle = false;
  SDL_GPUComputePass* cp = SDL_BeginGPUComputePass(cmd, &out, 1, nullptr, 0);
  SDL_BindGPUComputePipeline(cp, g->pipelines[E_TRANS]);
  const SDL_GPUTextureSamplerBinding views[2] = {{slot.buf[CUR], g->sampler}, {slot.auxTex, g->sampler}};
  SDL_BindGPUComputeSamplers(cp, 0, views, 2);
  const Params params = transitionParams(plan, tw);
  SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(Params));
  SDL_DispatchGPUCompute(cp, static_cast<Uint32>((w + 7) / 8), static_cast<Uint32>((h + 7) / 8), 1);
  SDL_EndGPUComputePass(cp);
  copy = SDL_BeginGPUCopyPass(cmd);
  SDL_GPUTextureLocation from {};
  from.texture = slot.buf[ALT];
  SDL_GPUTextureLocation to {};
  to.texture = displayTex;
  SDL_CopyGPUTextureToTexture(copy, &from, &to, static_cast<Uint32>(w), static_cast<Uint32>(h), 1, false);
  SDL_EndGPUCopyPass(copy);
  if (!SDL_SubmitGPUCommandBuffer(cmd)) {
    slot.srcStamp = 0;
    return nullptr;
  }
  if (fresh) slot.srcStamp = source.stamp;
  return display;
}
#endif
}  // namespace

SDL_Texture* runGpuTransition(GpuEffects* g, int slot, const TransitionSource& source, const TransitionPlan& plan) {
  if (!g || plan.style == TransitionPlan::None || plan.width < 2 || plan.height < 2 || plan.aux.empty()) {
    return nullptr;
  }
#ifdef _WIN32
  if (g->device) return d3d11Transition(g, slot, source, plan);
#endif
#if SDL_VERSION_ATLEAST(3, 4, 0)
  if (g->gpu) return sdlTransition(g, slot, source, plan);
#endif
  return nullptr;
}

bool readGpuEffectsResult(GpuEffects* g, int slot, std::vector<std::uint8_t>& rgba, int& w, int& h) {
  if (!g) return false;
#ifdef _WIN32
  if (g->device) return d3d11Read(g, slot, rgba, w, h);
#endif
#if SDL_VERSION_ATLEAST(3, 4, 0)
  if (g->gpu) return sdlRead(g, slot, rgba, w, h);
#endif
  return false;
}

SDL_Texture* copyGpuEffectsResult(GpuEffects* g, int slot) {
  if (!g) return nullptr;
  SDL_Texture* display = nullptr;
  int w = 0, h = 0;
#ifdef _WIN32
  if (g->device) {
    auto it = g->d3dSlots.find(slot);
    if (it == g->d3dSlots.end() || !it->second.displayTex) return nullptr;
    display = it->second.display;
    w = it->second.w;
    h = it->second.h;
  }
#endif
#if SDL_VERSION_ATLEAST(3, 4, 0)
  if (g->gpu) {
    auto it = g->sdlSlots.find(slot);
    if (it == g->sdlSlots.end() || !it->second.display) return nullptr;
    display = it->second.display;
    w = it->second.w;
    h = it->second.h;
  }
#endif
  if (!display || w <= 0 || h <= 0) return nullptr;
  SDL_Texture* copy = deckboyCreateTexture(g->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
  if (!copy) return nullptr;
  bool ok = false;
#ifdef _WIN32
  if (g->device) {
    auto* from = static_cast<ID3D11Texture2D*>(
      SDL_GetPointerProperty(SDL_GetTextureProperties(display), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
    auto* to = static_cast<ID3D11Texture2D*>(
      SDL_GetPointerProperty(SDL_GetTextureProperties(copy), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
    if (from && to) {
      SDL_FlushRenderer(g->renderer);
      g->context->CopyResource(to, from);
      ok = true;
    }
  }
#endif
#if SDL_VERSION_ATLEAST(3, 4, 0)
  if (g->gpu) {
    auto* from = static_cast<SDL_GPUTexture*>(
      SDL_GetPointerProperty(SDL_GetTextureProperties(display), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    auto* to = static_cast<SDL_GPUTexture*>(
      SDL_GetPointerProperty(SDL_GetTextureProperties(copy), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    SDL_GPUCommandBuffer* cmd = (from && to) ? SDL_AcquireGPUCommandBuffer(g->gpu) : nullptr;
    if (cmd) {
      SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
      SDL_GPUTextureLocation src {};
      src.texture = from;
      SDL_GPUTextureLocation dst {};
      dst.texture = to;
      SDL_CopyGPUTextureToTexture(pass, &src, &dst, static_cast<Uint32>(w), static_cast<Uint32>(h), 1, false);
      SDL_EndGPUCopyPass(pass);
      ok = SDL_SubmitGPUCommandBuffer(cmd);
    }
  }
#endif
  if (!ok) {
    SDL_DestroyTexture(copy);
    return nullptr;
  }
  return copy;
}

bool readGpuEffectsTexture(GpuEffects* g, SDL_Texture* texture, std::vector<std::uint8_t>& rgba, int& w, int& h) {
  if (!g || !texture) return false;
  float fw = 0.0f, fh = 0.0f;
  if (!SDL_GetTextureSize(texture, &fw, &fh) || fw < 1.0f || fh < 1.0f) return false;
  w = static_cast<int>(fw);
  h = static_cast<int>(fh);
#ifdef _WIN32
  if (g->device) {
    auto* tex = static_cast<ID3D11Texture2D*>(
      SDL_GetPointerProperty(SDL_GetTextureProperties(texture), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
    if (!tex) return false;
    D3D11_TEXTURE2D_DESC desc {};
    tex->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(g->device->CreateTexture2D(&desc, nullptr, &staging))) return false;
    SDL_FlushRenderer(g->renderer);
    g->context->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE mapped {};
    bool ok = SUCCEEDED(g->context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped));
    if (ok) {
      rgba.resize(static_cast<std::size_t>(w) * h * 4);
      for (int y = 0; y < h; ++y) {
        std::memcpy(rgba.data() + static_cast<std::size_t>(y) * w * 4,
                    static_cast<const std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(y) * mapped.RowPitch,
                    static_cast<std::size_t>(w) * 4);
      }
      g->context->Unmap(staging, 0);
    }
    staging->Release();
    return ok;
  }
#endif
#if SDL_VERSION_ATLEAST(3, 4, 0)
  if (g->gpu) {
    auto* tex = static_cast<SDL_GPUTexture*>(
      SDL_GetPointerProperty(SDL_GetTextureProperties(texture), SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER, nullptr));
    if (!tex) return false;
    const Uint32 bytes = static_cast<Uint32>(w * h * 4);
    SDL_GPUTransferBufferCreateInfo info {};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = bytes;
    SDL_GPUTransferBuffer* down = SDL_CreateGPUTransferBuffer(g->gpu, &info);
    if (!down) return false;
    bool ok = false;
    if (SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g->gpu)) {
      SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
      SDL_GPUTextureRegion region {};
      region.texture = tex;
      region.w = static_cast<Uint32>(w);
      region.h = static_cast<Uint32>(h);
      region.d = 1;
      SDL_GPUTextureTransferInfo to {down, 0, static_cast<Uint32>(w), static_cast<Uint32>(h)};
      SDL_DownloadFromGPUTexture(pass, &region, &to);
      SDL_EndGPUCopyPass(pass);
      if (SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd)) {
        SDL_WaitForGPUFences(g->gpu, true, &fence, 1);
        SDL_ReleaseGPUFence(g->gpu, fence);
        if (void* mapped = SDL_MapGPUTransferBuffer(g->gpu, down, false)) {
          rgba.assign(static_cast<const std::uint8_t*>(mapped), static_cast<const std::uint8_t*>(mapped) + bytes);
          SDL_UnmapGPUTransferBuffer(g->gpu, down);
          ok = true;
        }
      }
    }
    SDL_ReleaseGPUTransferBuffer(g->gpu, down);
    return ok;
  }
#endif
  return false;
}

void releaseGpuEffectsSlot(GpuEffects* g, int slot) {
  if (!g) return;
#ifdef _WIN32
  if (auto it = g->d3dSlots.find(slot); it != g->d3dSlots.end()) {
    releaseVp(it->second);
    releaseNv12Views(it->second);
    for (auto& b : it->second.buf) releaseBuf(b);
    releaseCom(it->second.staging);
    releaseCom(it->second.auxSrv);
    releaseCom(it->second.auxTex);
    if (it->second.display) SDL_DestroyTexture(it->second.display);
    g->d3dSlots.erase(it);
  }
#endif
#if SDL_VERSION_ATLEAST(3, 4, 0)
  if (auto it = g->sdlSlots.find(slot); it != g->sdlSlots.end()) {
    SDL_WaitForGPUIdle(g->gpu);
    sdlReleaseSlot(g, it->second);
    g->sdlSlots.erase(it);
  }
#endif
}

void warmGpuEffectsShaders() {
#ifdef _WIN32
  for (const char* entry : kEntryNames) d3d11Bytecode(entry);
#endif
}

}  // namespace deckboy::gpu
