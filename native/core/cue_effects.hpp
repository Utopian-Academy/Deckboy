// cue_effects.hpp — the per-cue effect stack.
//
// pixel_effects.hpp already carried chroma key and the colour controls, which
// are per-cue effects in everything but name. This adds the ORDERED, operator
// -built stack on top of them: a list of {kind, amount, params} on the Cue,
// applied in the order the operator arranged it.
//
// WHERE THIS RUNS, and why it is allowed to run at full raster:
//
// The video synth's CRT stage cost 23ms a frame at 4K until it was moved to
// the internal raster, and that lesson has to be respected here. The rule that
// came out of it is per-EFFECT, not per-stack:
//
//   - A single-pass per-pixel operation (invert, posterise, threshold, grain,
//     vignette, scanlines, dither) reads one pixel and writes one pixel. At 4K
//     that is 8.3M iterations of a few arithmetic ops -- real, but linear and
//     unavoidable, and downscaling first would soften a colour grade for no
//     reason. These run at full raster.
//   - Anything with a WINDOW or an ITERATION (blur, bloom, sort, seam carve,
//     flow) must compute its field or its kernel on a small buffer and apply
//     the result at full resolution. Those are the ones that killed the CRT.
//
// Only the first group is implemented here. The second gets its own file when
// it arrives, so the distinction stays visible rather than becoming a comment
// nobody reads.
#ifndef DECKBOY_CORE_CUE_EFFECTS_HPP
#define DECKBOY_CORE_CUE_EFFECTS_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../engine/motion_field.hpp"

namespace deckboy::effects {

enum class CueEffectKind : int {
  None = 0,
  Invert,
  Posterise,
  Solarise,
  Threshold,
  Vignette,
  Grain,
  Scanlines,
  ChannelOffset,
  TemporalDither,
  MotionPuppet,
  Datamosh,
  PixelSort,
  BlockGlitch,
  PolarWarp,
  LumaDisplace,
  Ripple,
  Kaleidoscope,
  Count,
};

inline const char* cueEffectLabel(CueEffectKind kind) {
  switch (kind) {
    case CueEffectKind::Invert:         return "invert";
    case CueEffectKind::Posterise:      return "posterise";
    case CueEffectKind::Solarise:       return "solarise";
    case CueEffectKind::Threshold:      return "threshold";
    case CueEffectKind::Vignette:       return "vignette";
    case CueEffectKind::Grain:          return "grain";
    case CueEffectKind::Scanlines:      return "scanlines";
    case CueEffectKind::ChannelOffset:  return "rgb split";
    case CueEffectKind::TemporalDither: return "temporal dither";
    case CueEffectKind::MotionPuppet:   return "motion puppet";
    case CueEffectKind::Datamosh:       return "datamosh";
    case CueEffectKind::PixelSort:      return "pixel sort";
    case CueEffectKind::BlockGlitch:    return "block glitch";
    case CueEffectKind::PolarWarp:      return "polar warp";
    case CueEffectKind::LumaDisplace:   return "luma displace";
    case CueEffectKind::Ripple:         return "ripple";
    case CueEffectKind::Kaleidoscope:   return "kaleidoscope";
    default:                            return "none";
  }
}

// Serialised token. NOT the label: labels are for operators and may be
// reworded, tokens are in saved shows and may not.
inline const char* cueEffectToken(CueEffectKind kind) {
  switch (kind) {
    case CueEffectKind::Invert:         return "invert";
    case CueEffectKind::Posterise:      return "posterise";
    case CueEffectKind::Solarise:       return "solarise";
    case CueEffectKind::Threshold:      return "threshold";
    case CueEffectKind::Vignette:       return "vignette";
    case CueEffectKind::Grain:          return "grain";
    case CueEffectKind::Scanlines:      return "scanlines";
    case CueEffectKind::ChannelOffset:  return "channel_offset";
    case CueEffectKind::TemporalDither: return "temporal_dither";
    case CueEffectKind::MotionPuppet:   return "motion_puppet";
    case CueEffectKind::Datamosh:       return "datamosh";
    case CueEffectKind::PixelSort:      return "pixel_sort";
    case CueEffectKind::BlockGlitch:    return "block_glitch";
    case CueEffectKind::PolarWarp:      return "polar_warp";
    case CueEffectKind::LumaDisplace:   return "luma_displace";
    case CueEffectKind::Ripple:         return "ripple";
    case CueEffectKind::Kaleidoscope:   return "kaleidoscope";
    default:                            return "none";
  }
}

inline CueEffectKind cueEffectFromToken(const std::string& token) {
  for (int i = 1; i < static_cast<int>(CueEffectKind::Count); ++i) {
    const auto kind = static_cast<CueEffectKind>(i);
    if (token == cueEffectToken(kind)) {
      return kind;
    }
  }
  return CueEffectKind::None;
}

struct CueEffect {
  CueEffectKind kind = CueEffectKind::None;
  float amount = 1.0f;    // 0 = inactive; every effect is skipped entirely at 0
  float paramA = 0.0f;
  float paramB = 0.0f;
  // BYPASS is not the same as amount 0. Turning an effect down to nothing
  // loses the setting you spent time on; bypass takes it out of the chain and
  // gives it back. Every serious tool has both and they are not
  // interchangeable.
  bool bypassed = false;
};

// What an effect needs beyond the pixels. Kept in one struct so adding a
// context-dependent effect does not change every call site.
struct CueEffectContext {
  int width = 0;
  int height = 0;
  std::uint64_t frameIndex = 0;   // for anything that advances per frame
  // The driver clip's motion for THIS frame, when one is armed. Null the rest
  // of the time, which is almost always -- so MotionPuppet costs nothing on a
  // cue that has not been given a driver.
  const deckboy::motion::MotionField* motion = nullptr;
};

namespace detail {

inline std::uint8_t clamp8(double v) {
  return static_cast<std::uint8_t>(std::clamp(v, 0.0, 255.0));
}

// The classic 4x4 Bayer matrix, scaled to 0..15.
inline int bayer4(int x, int y) {
  static const int kMatrix[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
  };
  return kMatrix[y & 3][x & 3];
}

}  // namespace detail


// ---------------------------------------------------------------------------
// Stages shared with the video synth.
//
// These began life inside rebuildVideoSynthFrame and were only ever available
// to a synth cue, which is a waste: a pixel sort is at least as interesting on
// a face as on an oscillator. They live here now and the synth calls them, so
// there is ONE implementation rather than two that drift.
//
// All of them take a raw buffer plus its dimensions, because the synth runs
// them on its small internal raster while a cue runs them on the frame.
// ---------------------------------------------------------------------------

// Runs of bright pixels within a row, sorted by brightness. The dragged,
// melted look: bright material slides along the row and pools against whatever
// stops it.
inline void applyPixelSort(std::uint8_t* pixels, int w, int h, double amount) {
  if (!pixels || w <= 2 || h <= 0 || amount <= 0.01) {
    return;
  }
  // The threshold FALLS as the amount rises, so more of each row qualifies and
  // the runs grow longer. Driving run length directly instead would cut spans
  // at arbitrary points and read as banding rather than flow.
  const int threshold = static_cast<int>(200.0 - std::clamp(amount, 0.0, 1.0) * 170.0);
  auto luma = [&](std::size_t o) {
    return (pixels[o + 2] * 77 + pixels[o + 1] * 151 + pixels[o + 0] * 28) >> 8;
  };
  std::vector<std::uint32_t> run;
  for (int y = 0; y < h; ++y) {
    const std::size_t rowOff = static_cast<std::size_t>(y) * w * 4;
    int x = 0;
    while (x < w) {
      if (static_cast<int>(luma(rowOff + static_cast<std::size_t>(x) * 4)) < threshold) {
        ++x;
        continue;
      }
      const int begin = x;
      while (x < w &&
             static_cast<int>(luma(rowOff + static_cast<std::size_t>(x) * 4)) >= threshold) {
        ++x;
      }
      const int len = x - begin;
      if (len < 3) {
        continue;
      }
      run.clear();
      run.reserve(static_cast<std::size_t>(len));
      for (int i = 0; i < len; ++i) {
        std::uint32_t px = 0;
        std::memcpy(&px, pixels + rowOff + static_cast<std::size_t>(begin + i) * 4, 4);
        run.push_back(px);
      }
      std::sort(run.begin(), run.end(), [](std::uint32_t a, std::uint32_t b) {
        const int la = ((a >> 16) & 0xFF) * 77 + ((a >> 8) & 0xFF) * 151 + (a & 0xFF) * 28;
        const int lb = ((b >> 16) & 0xFF) * 77 + ((b >> 8) & 0xFF) * 151 + (b & 0xFF) * 28;
        return la < lb;
      });
      for (int i = 0; i < len; ++i) {
        std::memcpy(pixels + rowOff + static_cast<std::size_t>(begin + i) * 4,
                    &run[static_cast<std::size_t>(i)], 4);
      }
    }
  }
}

// Displaced scanline bands plus red/blue separation: the corrupted-frame look.
// Bands are whole rows because that is how real decode corruption presents --
// a block row loses sync and the rest of the line arrives shifted.
inline void applyBlockGlitch(std::uint8_t* pixels, int w, int h, double amount,
                             std::uint32_t seed) {
  if (!pixels || w <= 2 || h <= 0 || amount <= 0.01) {
    return;
  }
  amount = std::clamp(amount, 0.0, 1.0);
  std::uint32_t state = seed | 1u;
  auto rnd = [&state]() {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
  };
  const int bands = 1 + static_cast<int>(amount * 14.0);
  std::vector<std::uint8_t> rowCopy(static_cast<std::size_t>(w) * 4);
  for (int b = 0; b < bands; ++b) {
    const int y0 = static_cast<int>(rnd() % static_cast<std::uint32_t>(std::max(1, h)));
    const int hgt = 1 + static_cast<int>(rnd() % static_cast<std::uint32_t>(
      std::max(1, static_cast<int>(h * amount / 8) + 1)));
    const int shift = static_cast<int>(rnd() % static_cast<std::uint32_t>(std::max(1, w / 3))) -
                      (w / 6);
    for (int y = y0; y < std::min(h, y0 + hgt); ++y) {
      std::uint8_t* row = pixels + static_cast<std::size_t>(y) * w * 4;
      std::memcpy(rowCopy.data(), row, rowCopy.size());
      for (int x = 0; x < w; ++x) {
        // WRAPPED, not clamped: a clamped shift smears its edge pixel across
        // the gap, which reads as a stretch. Wrapping reads as torn, which is
        // what corruption actually looks like.
        int sx = x - shift;
        sx = ((sx % w) + w) % w;
        std::memcpy(row + static_cast<std::size_t>(x) * 4,
                    rowCopy.data() + static_cast<std::size_t>(sx) * 4, 4);
      }
    }
  }
  const int sep = static_cast<int>(amount * (w / 60.0)) + 1;
  for (int y = 0; y < h; ++y) {
    std::uint8_t* row = pixels + static_cast<std::size_t>(y) * w * 4;
    std::memcpy(rowCopy.data(), row, rowCopy.size());
    for (int x = 0; x < w; ++x) {
      const int xr = std::clamp(x + sep, 0, w - 1);
      const int xb = std::clamp(x - sep, 0, w - 1);
      row[static_cast<std::size_t>(x) * 4 + 2] = rowCopy[static_cast<std::size_t>(xr) * 4 + 2];
      row[static_cast<std::size_t>(x) * 4 + 0] = rowCopy[static_cast<std::size_t>(xb) * 4 + 0];
    }
  }
}

// ---------------------------------------------------------------------------
// applyCueEffectStack — run the operator's stack over a BGRA/RGBA buffer.
//
// Pixels are 4 bytes; the first three are colour in whatever order the caller
// uses. Every effect here is channel-symmetric except ChannelOffset, which
// says in its own comment what it assumes.
// ---------------------------------------------------------------------------
inline void applyCueEffectStack(std::vector<std::uint8_t>& pixels,
                                const std::vector<CueEffect>& stack,
                                const CueEffectContext& ctx) {
  if (pixels.empty() || stack.empty() || ctx.width <= 0 || ctx.height <= 0) {
    return;
  }
  const std::size_t count = static_cast<std::size_t>(ctx.width) * ctx.height;
  if (pixels.size() < count * 4) {
    return;
  }

  for (const CueEffect& fx : stack) {
    const double amt = std::clamp(static_cast<double>(fx.amount), 0.0, 1.0);
    if (fx.kind == CueEffectKind::None || fx.bypassed || amt <= 0.0005) {
      continue;   // zero and bypass both cost nothing
    }
    switch (fx.kind) {
      case CueEffectKind::Invert: {
        for (std::size_t i = 0; i < count; ++i) {
          std::uint8_t* p = pixels.data() + i * 4;
          for (int c = 0; c < 3; ++c) {
            p[c] = detail::clamp8(p[c] * (1.0 - amt) + (255 - p[c]) * amt);
          }
        }
        break;
      }
      case CueEffectKind::Posterise: {
        // Amount picks the level count: 1.0 is brutal (2 levels), low amounts
        // barely touch it. Inverted deliberately -- "more effect" should mean
        // "fewer levels", which is not what a naive mapping gives.
        const int levels = std::max(2, static_cast<int>(std::lround(2 + (1.0 - amt) * 30)));
        const double step = 255.0 / (levels - 1);
        for (std::size_t i = 0; i < count; ++i) {
          std::uint8_t* p = pixels.data() + i * 4;
          for (int c = 0; c < 3; ++c) {
            p[c] = detail::clamp8(std::round(p[c] / step) * step);
          }
        }
        break;
      }
      case CueEffectKind::Solarise: {
        // Everything above the threshold inverts, which is the darkroom
        // effect: highlights fold back through black.
        const double pivot = 255.0 * std::clamp(static_cast<double>(fx.paramA), 0.05, 0.95);
        for (std::size_t i = 0; i < count; ++i) {
          std::uint8_t* p = pixels.data() + i * 4;
          for (int c = 0; c < 3; ++c) {
            const double folded = p[c] > pivot ? (255.0 - p[c]) : p[c];
            p[c] = detail::clamp8(p[c] * (1.0 - amt) + folded * amt);
          }
        }
        break;
      }
      case CueEffectKind::Threshold: {
        const double pivot = 255.0 * std::clamp(static_cast<double>(fx.paramA), 0.02, 0.98);
        for (std::size_t i = 0; i < count; ++i) {
          std::uint8_t* p = pixels.data() + i * 4;
          const double luma = p[2] * 0.299 + p[1] * 0.587 + p[0] * 0.114;
          const double hit = luma >= pivot ? 255.0 : 0.0;
          for (int c = 0; c < 3; ++c) {
            p[c] = detail::clamp8(p[c] * (1.0 - amt) + hit * amt);
          }
        }
        break;
      }
      case CueEffectKind::Vignette: {
        const double cx = ctx.width * 0.5;
        const double cy = ctx.height * 0.5;
        const double maxR = std::sqrt(cx * cx + cy * cy);
        for (int y = 0; y < ctx.height; ++y) {
          std::uint8_t* row = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
          const double dy = y - cy;
          for (int x = 0; x < ctx.width; ++x) {
            const double dx = x - cx;
            const double r = std::sqrt(dx * dx + dy * dy) / maxR;
            // Squared falloff, so the centre stays clean and the corners go.
            const double gain = 1.0 - amt * r * r;
            std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
            for (int c = 0; c < 3; ++c) {
              p[c] = detail::clamp8(p[c] * gain);
            }
          }
        }
        break;
      }
      case CueEffectKind::Grain: {
        // Hashed from position AND frame, not a running generator: the same
        // frame must look the same however many frames have been drawn before
        // it, or scrubbing the deck changes the grain.
        const std::uint32_t seed =
          static_cast<std::uint32_t>(ctx.frameIndex * 2654435761u) | 1u;
        const double strength = amt * 64.0;
        for (std::size_t i = 0; i < count; ++i) {
          std::uint32_t h = static_cast<std::uint32_t>(i) ^ seed;
          h ^= h << 13; h ^= h >> 17; h ^= h << 5;
          const double n = (static_cast<double>(h & 0xFFFF) / 32768.0 - 1.0) * strength;
          std::uint8_t* p = pixels.data() + i * 4;
          for (int c = 0; c < 3; ++c) {
            p[c] = detail::clamp8(p[c] + n);
          }
        }
        break;
      }
      case CueEffectKind::Scanlines: {
        // Every other line darkened. paramA sets the line pitch in pixels so
        // this reads at 4K as well as at 720 -- a one-pixel line on a 2160-line
        // raster is invisible, which is the mistake the synth's CRT made.
        const int pitch = std::max(2, static_cast<int>(std::lround(
          2.0 + std::clamp(static_cast<double>(fx.paramA), 0.0, 1.0) * 10.0)));
        for (int y = 0; y < ctx.height; ++y) {
          if (((y / (pitch / 2)) & 1) == 0) {
            continue;
          }
          std::uint8_t* row = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
          for (int x = 0; x < ctx.width; ++x) {
            std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
            for (int c = 0; c < 3; ++c) {
              p[c] = detail::clamp8(p[c] * (1.0 - amt * 0.7));
            }
          }
        }
        break;
      }
      case CueEffectKind::ChannelOffset: {
        // Red and blue slide apart horizontally. Assumes byte 0 and byte 2 are
        // the outer colour channels, which holds for both BGRA and RGBA -- the
        // direction of the fringe flips between them and neither is wrong.
        const int shift = std::max(1, static_cast<int>(std::lround(amt * ctx.width * 0.02)));
        std::vector<std::uint8_t> row(static_cast<std::size_t>(ctx.width) * 4);
        for (int y = 0; y < ctx.height; ++y) {
          std::uint8_t* dst = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
          std::memcpy(row.data(), dst, row.size());
          for (int x = 0; x < ctx.width; ++x) {
            const int xr = std::clamp(x + shift, 0, ctx.width - 1);
            const int xb = std::clamp(x - shift, 0, ctx.width - 1);
            dst[static_cast<std::size_t>(x) * 4 + 2] = row[static_cast<std::size_t>(xr) * 4 + 2];
            dst[static_cast<std::size_t>(x) * 4 + 0] = row[static_cast<std::size_t>(xb) * 4 + 0];
          }
        }
        break;
      }
      case CueEffectKind::TemporalDither: {
        // A pixel-art idea on a video substrate. Quantise hard to a tiny
        // palette, but ADVANCE THE DITHER PATTERN EVERY FRAME: at 60Hz the eye
        // integrates shades that are not in the palette at all, so a four
        // colour picture reads as continuous tone -- and freezes into visible
        // checkerboard the moment the deck is paused. The still and the moving
        // image are deliberately different pictures.
        const int levels = std::max(2, static_cast<int>(std::lround(
          2.0 + (1.0 - amt) * 6.0)));
        const double step = 255.0 / (levels - 1);
        // Rotate the matrix through its four phases, one per frame.
        const int phase = static_cast<int>(ctx.frameIndex & 3);
        for (int y = 0; y < ctx.height; ++y) {
          std::uint8_t* rowp = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
          for (int x = 0; x < ctx.width; ++x) {
            const double bias =
              (detail::bayer4(x + phase, y + (phase >> 1)) / 16.0 - 0.5) * step;
            std::uint8_t* p = rowp + static_cast<std::size_t>(x) * 4;
            for (int c = 0; c < 3; ++c) {
              const double q = std::round((p[c] + bias) / step) * step;
              p[c] = detail::clamp8(p[c] * (1.0 - amt) + q * amt);
            }
          }
        }
        break;
      }
      case CueEffectKind::MotionPuppet: {
        // ONE CLIP'S MOTION, ANOTHER'S PIXELS.
        //
        // The driver clip is decoded only for the per-macroblock vectors its
        // codec already computed -- its pictures are thrown away. Those vectors
        // displace THIS cue's pixels, so a camera feed can be puppeteered by a
        // crowd scene, or a synth by a dancer.
        //
        // The field is coarse by nature: a macroblock is 16 pixels, so it is
        // sampled bilinearly rather than pretending to per-pixel precision the
        // data does not have. Rewriting vectors offline is mature practice;
        // doing it live, across two sources, is the part a file-based
        // toolchain cannot put on a cue.
        if (!ctx.motion || ctx.motion->empty()) {
          break;   // an I-frame, or no driver armed: leave the picture alone
        }
        const auto& mf = *ctx.motion;
        // Vectors are in the DRIVER's pixels; this cue may be a different size.
        const double sx = mf.sourceWidth > 0
          ? static_cast<double>(ctx.width) / mf.sourceWidth : 1.0;
        const double sy = mf.sourceHeight > 0
          ? static_cast<double>(ctx.height) / mf.sourceHeight : 1.0;
        const double gain = amt * 4.0;   // 1.0 reads as a shove, not a nudge

        std::vector<std::uint8_t> source(pixels.begin(),
                                         pixels.begin() + static_cast<std::ptrdiff_t>(count * 4));
        for (int y = 0; y < ctx.height; ++y) {
          const double gy = (static_cast<double>(y) / std::max(1, ctx.height)) * mf.rows;
          const int r0 = std::clamp(static_cast<int>(gy), 0, mf.rows - 1);
          const int r1 = std::min(r0 + 1, mf.rows - 1);
          const double fy = gy - r0;
          std::uint8_t* dstRow = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
          for (int x = 0; x < ctx.width; ++x) {
            const double gx = (static_cast<double>(x) / std::max(1, ctx.width)) * mf.cols;
            const int c0 = std::clamp(static_cast<int>(gx), 0, mf.cols - 1);
            const int c1 = std::min(c0 + 1, mf.cols - 1);
            const double fx2 = gx - c0;
            auto at = [&](int r, int c, bool wantX) {
              const std::size_t i = static_cast<std::size_t>(r) * mf.cols + c;
              return static_cast<double>(wantX ? mf.dx[i] : mf.dy[i]);
            };
            const double dxv =
              (at(r0, c0, true) * (1 - fx2) + at(r0, c1, true) * fx2) * (1 - fy) +
              (at(r1, c0, true) * (1 - fx2) + at(r1, c1, true) * fx2) * fy;
            const double dyv =
              (at(r0, c0, false) * (1 - fx2) + at(r0, c1, false) * fx2) * (1 - fy) +
              (at(r1, c0, false) * (1 - fx2) + at(r1, c1, false) * fx2) * fy;
            // MINUS: the vector says where the block came FROM, so sampling
            // backwards along it moves the picture the way the driver moved.
            const int srcX = std::clamp(
              static_cast<int>(std::lround(x - dxv * sx * gain)), 0, ctx.width - 1);
            const int srcY = std::clamp(
              static_cast<int>(std::lround(y - dyv * sy * gain)), 0, ctx.height - 1);
            const std::uint8_t* sp =
              source.data() + (static_cast<std::size_t>(srcY) * ctx.width + srcX) * 4;
            std::uint8_t* dp = dstRow + static_cast<std::size_t>(x) * 4;
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
          }
        }
        break;
      }
      case CueEffectKind::PixelSort:
        applyPixelSort(pixels.data(), ctx.width, ctx.height, amt);
        break;
      case CueEffectKind::BlockGlitch:
        // Seeded from the FRAME, not a running generator, so the same frame
        // always glitches the same way -- scrubbing back gives you the picture
        // you saw, not a new one.
        applyBlockGlitch(pixels.data(), ctx.width, ctx.height, amt,
                         static_cast<std::uint32_t>(ctx.frameIndex * 2654435761u));
        break;
      case CueEffectKind::PolarWarp:
      case CueEffectKind::LumaDisplace:
      case CueEffectKind::Ripple:
      case CueEffectKind::Kaleidoscope: {
        // All four RESAMPLE: every output pixel is fetched from somewhere else
        // in the source, so they need an untouched copy to read from. Written
        // as one block because the only thing that differs is where each pixel
        // looks, and four near-identical loops would drift apart.
        std::vector<std::uint8_t> source(pixels.begin(),
                                         pixels.begin() + static_cast<std::ptrdiff_t>(count * 4));
        const double cx = ctx.width * 0.5;
        const double cy = ctx.height * 0.5;
        const double maxR = std::sqrt(cx * cx + cy * cy);
        const double t = static_cast<double>(ctx.frameIndex) * 0.08;
        for (int y = 0; y < ctx.height; ++y) {
          std::uint8_t* dstRow = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
          for (int x = 0; x < ctx.width; ++x) {
            double sxf = x;
            double syf = y;
            switch (fx.kind) {
              case CueEffectKind::PolarWarp: {
                // Read the picture as if its rows were rings and its columns
                // were angles. Straight lines become spirals; a face becomes a
                // weather system.
                const double nx = (x - cx) / std::max(1.0, cx);
                const double ny = (y - cy) / std::max(1.0, cy);
                const double r = std::sqrt(nx * nx + ny * ny);
                double a = std::atan2(ny, nx);
                if (a < 0.0) a += 6.283185307179586;
                const double u = (a / 6.283185307179586) * ctx.width;
                const double v = r * ctx.height;
                sxf = x * (1.0 - amt) + u * amt;
                syf = y * (1.0 - amt) + v * amt;
                break;
              }
              case CueEffectKind::LumaDisplace: {
                // The picture bends by its OWN brightness -- bright regions
                // reach further for their colour than dark ones, so an image
                // distorts along its own structure rather than along a grid.
                const std::uint8_t* p =
                  source.data() + (static_cast<std::size_t>(y) * ctx.width + x) * 4;
                const double luma = (p[2] * 0.299 + p[1] * 0.587 + p[0] * 0.114) / 255.0;
                const double push = (luma - 0.5) * amt * ctx.width * 0.25;
                sxf = x + push;
                syf = y + push * 0.35;
                break;
              }
              case CueEffectKind::Ripple: {
                const double dx = x - cx;
                const double dy = y - cy;
                const double r = std::sqrt(dx * dx + dy * dy);
                const double wave = std::sin(r * 0.06 - t * 3.0) * amt * 24.0;
                const double inv = r > 0.001 ? 1.0 / r : 0.0;
                sxf = x + dx * inv * wave;
                syf = y + dy * inv * wave;
                break;
              }
              default: {   // Kaleidoscope
                // Fold the frame into wedges about its centre. paramA picks how
                // many, because two is a mirror and twelve is a snowflake and
                // they are completely different pictures.
                const int wedges = 2 + static_cast<int>(std::clamp(
                  static_cast<double>(fx.paramA), 0.0, 1.0) * 10.0);
                const double nx = x - cx;
                const double ny = y - cy;
                const double r = std::sqrt(nx * nx + ny * ny);
                const double seg = 6.283185307179586 / wedges;
                double a = std::atan2(ny, nx);
                a = std::fabs(std::fmod(a + seg * 0.5, seg) - seg * 0.5);
                const double fx2 = cx + std::cos(a) * r;
                const double fy2 = cy + std::sin(a) * r;
                sxf = x * (1.0 - amt) + fx2 * amt;
                syf = y * (1.0 - amt) + fy2 * amt;
                break;
              }
            }
            const int sx = std::clamp(static_cast<int>(std::lround(sxf)), 0, ctx.width - 1);
            const int sy = std::clamp(static_cast<int>(std::lround(syf)), 0, ctx.height - 1);
            const std::uint8_t* sp =
              source.data() + (static_cast<std::size_t>(sy) * ctx.width + sx) * 4;
            std::uint8_t* dp = dstRow + static_cast<std::size_t>(x) * 4;
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
          }
        }
        (void) maxR;
        break;
      }
      case CueEffectKind::Datamosh:
        // Deliberately nothing here. Datamosh is not a pixel operation: it
        // works by withholding keyframes from the DECODER, so by the time a
        // frame reaches this function the effect has already happened or it
        // has not. It sits in this list because from where the operator
        // stands it is an effect like any other, and having one effect
        // permanently present while the rest had to be added was incoherent.
        break;
      default:
        break;
    }
  }
}

inline bool cueEffectStackActive(const std::vector<CueEffect>& stack) {
  for (const CueEffect& fx : stack) {
    if (fx.kind != CueEffectKind::None && !fx.bypassed && fx.amount > 0.0005f) {
      return true;
    }
  }
  return false;
}

}  // namespace deckboy::effects

#endif  // DECKBOY_CORE_CUE_EFFECTS_HPP
