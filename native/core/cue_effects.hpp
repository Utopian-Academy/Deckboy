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
};

// What an effect needs beyond the pixels. Kept in one struct so adding a
// context-dependent effect does not change every call site.
struct CueEffectContext {
  int width = 0;
  int height = 0;
  std::uint64_t frameIndex = 0;   // for anything that advances per frame
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
    if (fx.kind == CueEffectKind::None || amt <= 0.0005) {
      continue;   // zero costs nothing, as the synth's glitch stack does
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
      default:
        break;
    }
  }
}

inline bool cueEffectStackActive(const std::vector<CueEffect>& stack) {
  for (const CueEffect& fx : stack) {
    if (fx.kind != CueEffectKind::None && fx.amount > 0.0005f) {
      return true;
    }
  }
  return false;
}

}  // namespace deckboy::effects

#endif  // DECKBOY_CORE_CUE_EFFECTS_HPP
