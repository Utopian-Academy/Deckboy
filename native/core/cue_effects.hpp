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
#include <condition_variable>
#include <mutex>
#include <thread>
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
  DyeAdvect,
  ReactionBloom,
  Relativistic,
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
    case CueEffectKind::DyeAdvect:      return "dye advect";
    case CueEffectKind::ReactionBloom:  return "reaction bloom";
    case CueEffectKind::Relativistic:   return "lightspeed";
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
    case CueEffectKind::DyeAdvect:      return "dye_advect";
    case CueEffectKind::ReactionBloom:  return "reaction_bloom";
    case CueEffectKind::Relativistic:   return "relativistic";
    default:                            return "none";
  }
}

// What an effect's two extra parameters MEAN, or null when it has none.
//
// paramA and paramB existed from the start and nothing in the UI could reach
// them, so solarise always folded at its default pivot and kaleidoscope always
// cut the same number of wedges. A parameter the operator cannot set is a
// parameter that does not exist -- and the effects added since (dye advect,
// reaction bloom, lightspeed) carry two thirds of their character in these.
//
// Naming them here rather than in the inspector keeps the meaning next to the
// code that reads it, so a parameter cannot be repurposed without the label
// moving too.
inline const char* cueEffectParamLabel(CueEffectKind kind, int which) {
  switch (kind) {
    case CueEffectKind::Solarise:
      return which == 0 ? "fold point" : nullptr;
    case CueEffectKind::Threshold:
      return which == 0 ? "pivot" : nullptr;
    case CueEffectKind::Kaleidoscope:
      return which == 0 ? "wedges" : nullptr;
    case CueEffectKind::DyeAdvect:
      return which == 0 ? "bleed" : "curl detail";
    case CueEffectKind::ReactionBloom:
      return which == 0 ? "feed rate" : "growth";
    case CueEffectKind::Relativistic:
      return which == 0 ? "field of view" : "doppler";
    default:
      return nullptr;
  }
}

// The one-line explanation the inspector shows for that parameter.
inline const char* cueEffectParamTip(CueEffectKind kind, int which) {
  switch (kind) {
    case CueEffectKind::Solarise:
      return "Where highlights fold back through black. Low folds most of the "
             "picture, high folds only the brightest.";
    case CueEffectKind::Threshold:
      return "The brightness that decides black from white.";
    case CueEffectKind::Kaleidoscope:
      return "Two is a mirror, twelve is a snowflake, and they are completely "
             "different pictures.";
    case CueEffectKind::DyeAdvect:
      return which == 0
        ? "0 flows ALONG the edges, so colour orbits the shapes. 1 flows across "
          "them, which bleeds the picture into itself."
        : "How many steps each pixel walks. More steps curve further around "
          "the shapes; the total distance does not change.";
    case CueEffectKind::ReactionBloom:
      return which == 0
        ? "The character of the growth: low is coral and veins, high is spots "
          "that divide."
        : "How long the reaction runs each frame. Short is a stain, long is an "
          "organism.";
    case CueEffectKind::Relativistic:
      return which == 0
        ? "How wide a view is being compressed. Narrow is a punch down the "
          "middle, wide folds the whole frame into the centre."
        : "Blueshift toward the direction of travel and redshift at the rim. "
          "Zero leaves the colour alone and it reads as a lens instead.";
    default:
      return "";
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

// How many workers to split a frame across.
//
// Capped rather than "all of them": a show is not only compositing, and taking
// every core for an effect stack starves the decoder threads that are feeding
// it. Sixteen is well past the point where memory bandwidth, not arithmetic,
// is the limit for this kind of work.
inline unsigned effectWorkers() {
  static const unsigned workers = [] {
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;              // the standard is allowed to say "no idea"
    return std::min(hw, 16u);
  }();
  return workers;
}

// Run `fn(firstRow, lastRow)` over the frame, split into bands.
//
// Every effect here writes each output row from inputs that are either in that
// same row or in an untouched COPY of the frame, so the bands cannot see each
// other's work and the result is identical to running it serially. The two that
// are not row-independent (block glitch, which shifts overlapping random bands
// in a fixed RNG order) simply do not use this.
//
// Threads are created per call rather than pooled. A pool would save roughly
// two tenths of a millisecond per effect, and the effects it matters for take
// tens of milliseconds; a race in the render path would cost far more than that
// is worth. The size gate keeps the hand-off from dominating small frames.
template <typename Fn>
inline void parallelRows(int height, int width, Fn fn) {
  const unsigned workers = effectWorkers();
  if (workers < 2 || height < 2 ||
      static_cast<long long>(height) * width < 120000) {
    fn(0, height);
    return;
  }
  const int bands = static_cast<int>(std::min<unsigned>(
    workers, static_cast<unsigned>(height)));
  const int rowsPerBand = (height + bands - 1) / bands;
  std::vector<std::thread> helpers;
  helpers.reserve(static_cast<std::size_t>(bands - 1));
  for (int band = 1; band < bands; ++band) {
    const int first = band * rowsPerBand;
    const int last = std::min(height, first + rowsPerBand);
    if (first >= last) {
      break;
    }
    helpers.emplace_back([&fn, first, last] { fn(first, last); });
  }
  // The calling thread takes the first band instead of idling.
  fn(0, std::min(height, rowsPerBand));
  for (std::thread& helper : helpers) {
    helper.join();
  }
}

// A 256-entry table for any effect whose output channel depends only on its
// input channel.
//
// Invert, posterise, solarise and the rest were evaluating the same handful of
// double expressions two million times a frame to produce, at most, 256
// distinct answers. Building the table with the SAME expression keeps the
// result identical to the arithmetic it replaces -- this is a lookup of the old
// answer, not a new approximation of it.
template <typename Fn>
inline void buildChannelLut(std::uint8_t (&lut)[256], Fn f) {
  for (int v = 0; v < 256; ++v) {
    lut[v] = f(v);
  }
}

// Apply a channel table across the frame. Alpha is left alone.
inline void applyChannelLut(std::uint8_t* pixels, int width, int height,
                            const std::uint8_t (&lut)[256]) {
  parallelRows(height, width, [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      std::uint8_t* p = pixels + static_cast<std::size_t>(y) * width * 4;
      for (int x = 0; x < width; ++x, p += 4) {
        p[0] = lut[p[0]];
        p[1] = lut[p[1]];
        p[2] = lut[p[2]];
      }
    }
  });
}

// A reusable barrier, for work that is many small DEPENDENT steps.
//
// parallelRows above creates its threads per call, which is right for one pass
// over a frame and wrong for reaction-diffusion: that is hundreds of steps on a
// small grid, and paying the hand-off once per step made the effect 1.8x
// SLOWER than running it on one core. Measured, not guessed.
//
// So the threads are created once and parked here between steps instead.
class Barrier {
 public:
  explicit Barrier(int parties) : parties_(parties) {}

  void arriveAndWait() {
    std::unique_lock<std::mutex> lock(mutex_);
    const unsigned long long myGeneration = generation_;
    if (++waiting_ == parties_) {
      waiting_ = 0;
      ++generation_;          // releases everyone parked on this generation
      condition_.notify_all();
    } else {
      condition_.wait(lock, [&] { return generation_ != myGeneration; });
    }
  }

 private:
  std::mutex mutex_;
  std::condition_variable condition_;
  int parties_;
  int waiting_ = 0;
  unsigned long long generation_ = 0;
};

// Run `step(round, firstRow, lastRow)` for every round, split into bands, with
// every band finished before `between(round)` runs and `between` finished
// before the next round starts.
//
// The two barriers are the whole contract: the first says every band has
// written its output, the second says the coordinator's swap has happened. A
// band that raced past either would read a half-updated grid.
template <typename Step, typename Between>
inline void iteratedBands(int rounds, int rows, int minRowsPerBand,
                          Step step, Between between) {
  const unsigned workers = effectWorkers();
  const int bands = static_cast<int>(std::min<long long>(
    workers, std::max(1, rows / std::max(1, minRowsPerBand))));
  if (rounds <= 0) {
    return;
  }
  if (bands < 2) {
    for (int round = 0; round < rounds; ++round) {
      step(round, 0, rows);
      between(round);
    }
    return;
  }
  const int rowsPerBand = (rows + bands - 1) / bands;
  Barrier barrier(bands);
  std::vector<std::thread> helpers;
  helpers.reserve(static_cast<std::size_t>(bands - 1));
  for (int band = 1; band < bands; ++band) {
    const int first = std::min(rows, band * rowsPerBand);
    const int last = std::min(rows, first + rowsPerBand);
    helpers.emplace_back([&, first, last] {
      for (int round = 0; round < rounds; ++round) {
        if (first < last) {
          step(round, first, last);
        }
        barrier.arriveAndWait();   // everyone has written
        barrier.arriveAndWait();   // the coordinator has swapped
      }
    });
  }
  for (int round = 0; round < rounds; ++round) {
    step(round, 0, std::min(rows, rowsPerBand));
    barrier.arriveAndWait();
    between(round);
    barrier.arriveAndWait();
  }
  for (std::thread& helper : helpers) {
    helper.join();
  }
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
  // Rows are independent -- each one only ever reads and writes itself -- so
  // this splits across cores. The run buffer moves INSIDE the band: one shared
  // scratch vector across threads would be a race, and it is the only mutable
  // state here.
  detail::parallelRows(h, w, [&](int firstRow, int lastRow) {
  std::vector<std::uint32_t> run;
  for (int y = firstRow; y < lastRow; ++y) {
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
  });
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
      // WRAPPED, not clamped: a clamped shift smears its edge pixel across the
      // gap, which reads as a stretch. Wrapping reads as torn, which is what
      // corruption actually looks like.
      //
      // A wrapped shift is a ROTATION, so it is two bulk copies rather than a
      // four-byte memcpy per pixel. The per-pixel version moved the same bytes
      // and cost 27ms on a 4K frame doing it.
      const int rot = ((shift % w) + w) % w;   // dst[x] = src[(x - rot) mod w]
      if (rot == 0) {
        std::memcpy(row, rowCopy.data(), rowCopy.size());
      } else {
        const std::size_t head = static_cast<std::size_t>(rot) * 4;
        const std::size_t tail = static_cast<std::size_t>(w - rot) * 4;
        std::memcpy(row, rowCopy.data() + tail, head);
        std::memcpy(row + head, rowCopy.data(), tail);
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
        std::uint8_t lut[256];
        detail::buildChannelLut(lut, [&](int v) {
          return detail::clamp8(v * (1.0 - amt) + (255 - v) * amt);
        });
        detail::applyChannelLut(pixels.data(), ctx.width, ctx.height, lut);
        break;
      }
      case CueEffectKind::Posterise: {
        // Amount picks the level count: 1.0 is brutal (2 levels), low amounts
        // barely touch it. Inverted deliberately -- "more effect" should mean
        // "fewer levels", which is not what a naive mapping gives.
        const int levels = std::max(2, static_cast<int>(std::lround(2 + (1.0 - amt) * 30)));
        const double step = 255.0 / (levels - 1);
        std::uint8_t lut[256];
        detail::buildChannelLut(lut, [&](int v) {
          return detail::clamp8(std::round(v / step) * step);
        });
        detail::applyChannelLut(pixels.data(), ctx.width, ctx.height, lut);
        break;
      }
      case CueEffectKind::Solarise: {
        // Everything above the threshold inverts, which is the darkroom
        // effect: highlights fold back through black.
        const double pivot = 255.0 * std::clamp(static_cast<double>(fx.paramA), 0.05, 0.95);
        std::uint8_t lut[256];
        detail::buildChannelLut(lut, [&](int v) {
          const double folded = v > pivot ? (255.0 - v) : v;
          return detail::clamp8(v * (1.0 - amt) + folded * amt);
        });
        detail::applyChannelLut(pixels.data(), ctx.width, ctx.height, lut);
        break;
      }
      case CueEffectKind::Threshold: {
        const double pivot = 255.0 * std::clamp(static_cast<double>(fx.paramA), 0.02, 0.98);
        std::uint8_t lutLit[256], lutDark[256];
        detail::buildChannelLut(lutLit, [&](int v) {
          return detail::clamp8(v * (1.0 - amt) + 255.0 * amt);
        });
        detail::buildChannelLut(lutDark, [&](int v) {
          return detail::clamp8(v * (1.0 - amt) + 0.0 * amt);
        });
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            std::uint8_t* p = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
            for (int x = 0; x < ctx.width; ++x, p += 4) {
              const double luma = p[0] * 0.299 + p[1] * 0.587 + p[2] * 0.114;
              const std::uint8_t* lut = luma >= pivot ? lutLit : lutDark;
              p[0] = lut[p[0]];
              p[1] = lut[p[1]];
              p[2] = lut[p[2]];
            }
          }
        });
        break;
      }
      case CueEffectKind::Vignette: {
        const double cx = ctx.width * 0.5;
        const double cy = ctx.height * 0.5;
        const double maxR = std::sqrt(cx * cx + cy * cy);
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
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
        });
        break;
      }
      case CueEffectKind::Grain: {
        // Hashed from position AND frame, not a running generator: the same
        // frame must look the same however many frames have been drawn before
        // it, or scrubbing the deck changes the grain.
        const std::uint32_t seed =
          static_cast<std::uint32_t>(ctx.frameIndex * 2654435761u) | 1u;
        const double strength = amt * 64.0;
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            std::size_t i = static_cast<std::size_t>(y) * ctx.width;
            std::uint8_t* p = pixels.data() + i * 4;
            for (int x = 0; x < ctx.width; ++x, ++i, p += 4) {
              std::uint32_t h = static_cast<std::uint32_t>(i) ^ seed;
              h ^= h << 13; h ^= h >> 17; h ^= h << 5;
              const double n = (static_cast<double>(h & 0xFFFF) / 32768.0 - 1.0) * strength;
              p[0] = detail::clamp8(p[0] + n);
              p[1] = detail::clamp8(p[1] + n);
              p[2] = detail::clamp8(p[2] + n);
            }
          }
        });
        break;
      }
      case CueEffectKind::Scanlines: {
        // Every other line darkened. paramA sets the line pitch in pixels so
        // this reads at 4K as well as at 720 -- a one-pixel line on a 2160-line
        // raster is invisible, which is the mistake the synth's CRT made.
        const int pitch = std::max(2, static_cast<int>(std::lround(
          2.0 + std::clamp(static_cast<double>(fx.paramA), 0.0, 1.0) * 10.0)));
        std::uint8_t lut[256];
        detail::buildChannelLut(lut, [&](int v) {
          return detail::clamp8(v * (1.0 - amt * 0.7));
        });
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            if (((y / (pitch / 2)) & 1) == 0) {
              continue;
            }
            std::uint8_t* p = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
            for (int x = 0; x < ctx.width; ++x, p += 4) {
              p[0] = lut[p[0]];
              p[1] = lut[p[1]];
              p[2] = lut[p[2]];
            }
          }
        });
        break;
      }
      case CueEffectKind::ChannelOffset: {
        // Red and blue slide apart horizontally. Assumes byte 0 and byte 2 are
        // the outer colour channels, which holds for both BGRA and RGBA -- the
        // direction of the fringe flips between them and neither is wrong.
        const int shift = std::max(1, static_cast<int>(std::lround(amt * ctx.width * 0.02)));
        // The row scratch lives INSIDE the band: one shared buffer across
        // threads would be a race, and it is the only mutable state here.
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
        std::vector<std::uint8_t> row(static_cast<std::size_t>(ctx.width) * 4);
        for (int y = firstRow; y < lastRow; ++y) {
          std::uint8_t* dst = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
          std::memcpy(row.data(), dst, row.size());
          for (int x = 0; x < ctx.width; ++x) {
            const int xr = std::clamp(x + shift, 0, ctx.width - 1);
            const int xb = std::clamp(x - shift, 0, ctx.width - 1);
            dst[static_cast<std::size_t>(x) * 4 + 2] = row[static_cast<std::size_t>(xr) * 4 + 2];
            dst[static_cast<std::size_t>(x) * 4 + 0] = row[static_cast<std::size_t>(xb) * 4 + 0];
          }
        }
        });
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
        std::uint8_t lut[16][256];
        for (int cell = 0; cell < 16; ++cell) {
          const double bias = (cell / 16.0 - 0.5) * step;
          for (int v = 0; v < 256; ++v) {
            const double q = std::round((v + bias) / step) * step;
            lut[cell][v] = detail::clamp8(v * (1.0 - amt) + q * amt);
          }
        }
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            std::uint8_t* p = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
            for (int x = 0; x < ctx.width; ++x, p += 4) {
              const std::uint8_t* cell =
                lut[detail::bayer4(x + phase, y + (phase >> 1))];
              p[0] = cell[p[0]];
              p[1] = cell[p[1]];
              p[2] = cell[p[2]];
            }
          }
        });
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
        // Split across cores: every pixel reads the untouched source copy.
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
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
        });
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
      case CueEffectKind::Relativistic:
      case CueEffectKind::Kaleidoscope: {
        // These all RESAMPLE: every output pixel is fetched from somewhere else
        // in the source, so they need an untouched copy to read from. Written
        // as one block because the only thing that differs is where each pixel
        // looks, and near-identical loops would drift apart.
        std::vector<std::uint8_t> source(pixels.begin(),
                                         pixels.begin() + static_cast<std::ptrdiff_t>(count * 4));
        const double cx = ctx.width * 0.5;
        const double cy = ctx.height * 0.5;
        const double maxR = std::sqrt(cx * cx + cy * cy);
        const double t = static_cast<double>(ctx.frameIndex) * 0.08;
        // Lightspeed's constants, hoisted: they do not vary per pixel, and a
        // sqrt for gamma would otherwise be paid four million times at 4K.
        const double beta = std::clamp(amt, 0.0, 1.0) * 0.995;
        const double gamma = 1.0 / std::sqrt(std::max(1e-6, 1.0 - beta * beta));
        const double halfFov = (12.0 + std::clamp(static_cast<double>(fx.paramA), 0.0, 1.0)
                                * 68.0) * 0.017453292519943295;
        const double dopplerAmt = std::clamp(static_cast<double>(fx.paramB), 0.0, 1.0);
        // Both halves of lightspeed depend on ONE thing: distance from the
        // centre. So they are a radial lookup built once, not an acos and two
        // cosines per pixel -- which is the difference between 11ms and under
        // 2ms on a small frame, and between unusable and fine at 4K.
        constexpr int kBoostTableSize = 1024;
        std::vector<float> boostRadius, boostGain;
        if (fx.kind == CueEffectKind::Relativistic) {
          boostRadius.resize(kBoostTableSize);
          boostGain.resize(kBoostTableSize);
          for (int i = 0; i < kBoostTableSize; ++i) {
            const double frac = static_cast<double>(i) / (kBoostTableSize - 1);
            const double cosSeen = std::cos(frac * halfFov);
            const double denom = 1.0 - beta * cosSeen;
            const double cosRest = std::clamp(
              (cosSeen - beta) / (std::fabs(denom) < 1e-6 ? 1e-6 : denom), -1.0, 1.0);
            boostRadius[i] = static_cast<float>((std::acos(cosRest) / halfFov) * maxR);
            const double doppler = 1.0 / std::max(1e-6, gamma * denom);
            boostGain[i] = static_cast<float>(
              std::tanh((doppler - 1.0) * 1.2) * dopplerAmt);
          }
        }
        // Split across cores: every pixel reads the untouched source copy.
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
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
                  const double luma = (p[0] * 0.299 + p[1] * 0.587 + p[2] * 0.114) / 255.0;
                  const double push = (luma - 0.5) * amt * ctx.width * 0.25;
                  sxf = x + push;
                  syf = y + push * 0.35;
                  break;
                }
                case CueEffectKind::Relativistic: {
                  // What the frame looks like from something travelling into it
                  // at a fraction of c. Relativistic aberration folds the forward
                  // hemisphere toward the direction of travel, so the centre
                  // opens out and the rim smears away -- which is why the view
                  // from a near-light ship is a bright compressed disc and not a
                  // zoom.
                  //
                  // The aberration formula is inverted here, because the loop
                  // walks OUTPUT pixels and has to find where each came from:
                  //   cos t' = (cos t + B) / (1 + B cos t)   is the forward map,
                  //   cos t  = (cos t' - B) / (1 - B cos t') is the one wanted.
                  const double ax = x - cx;
                  const double ay = y - cy;
                  const double ar = std::sqrt(ax * ax + ay * ay);
                  if (ar < 0.5 || maxR < 1.0) {
                    break;                        // the centre maps to itself
                  }
                  const double slot = std::clamp(ar / maxR, 0.0, 1.0) * (kBoostTableSize - 1);
                  const int i0 = static_cast<int>(slot);
                  const int i1 = std::min(kBoostTableSize - 1, i0 + 1);
                  const double frac = slot - i0;
                  const double srcR = boostRadius[i0] * (1.0 - frac) + boostRadius[i1] * frac;
                  sxf = cx + ax / ar * srcR;
                  syf = cy + ay / ar * srcR;
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
              if (fx.kind == CueEffectKind::Relativistic && dopplerAmt > 0.0005) {
                // The other half of the physics: light from ahead arrives
                // blueshifted and brighter, light from the sides redshifted and
                // dimmer. Without it the warp reads as a lens; with it, as speed.
                const double bx = x - cx;
                const double by = y - cy;
                const double br = std::clamp(
                  std::sqrt(bx * bx + by * by) / std::max(1.0, maxR), 0.0, 1.0);
                // Already squashed through tanh when the table was built, so a
                // high beta tints the picture instead of clipping it to blue.
                const double shift =
                  boostGain[static_cast<int>(br * (kBoostTableSize - 1))];
                const double gain = 1.0 + shift * 0.6;      // the headlight effect
                dp[0] = detail::clamp8(sp[0] * gain * (1.0 - shift * 0.55));   // R
                dp[1] = detail::clamp8(sp[1] * gain);                          // G
                dp[2] = detail::clamp8(sp[2] * gain * (1.0 + shift * 0.55));   // B
              } else {
                dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
              }
            }
          }
        });
        break;
      }
      case CueEffectKind::DyeAdvect: {
        // The picture treated as dye in a fluid, and carried along the flow of
        // its own structure.
        //
        // The velocity field is the PERPENDICULAR of the luma gradient, which
        // is the part that matters: a gradient points across an edge, so its
        // perpendicular runs ALONG one. Advecting down the gradient would only
        // smear the picture into mush across its own boundaries; advecting
        // along it makes colour orbit the shapes instead, and edges survive as
        // the banks of a river. paramA bleeds the field back toward the raw
        // gradient for when that is wanted.
        //
        // Each output pixel walks BACKWARD through the field for several steps
        // and fetches from where it ends up -- semi-Lagrangian advection, the
        // standard way to move a quantity through a velocity field without it
        // diffusing away. Several short steps rather than one long one, because
        // a single jump follows a straight line and the curl is the whole point.
        //
        // No state is kept between frames. A persistent dye buffer drifts
        // toward its own fixed point and stops being about the picture;
        // re-deriving the field every frame means the flow always describes
        // what is on screen NOW, and it moves because the picture does.
        const int gw = std::clamp(ctx.width / 8, 8, 256);
        const int gh = std::clamp(ctx.height / 8, 8, 256);
        std::vector<float> luma(static_cast<std::size_t>(gw) * gh, 0.0f);
        for (int gy = 0; gy < gh; ++gy) {
          const int sy = std::min(ctx.height - 1, gy * ctx.height / gh);
          for (int gx = 0; gx < gw; ++gx) {
            const int sx = std::min(ctx.width - 1, gx * ctx.width / gw);
            const std::uint8_t* p =
              pixels.data() + (static_cast<std::size_t>(sy) * ctx.width + sx) * 4;
            luma[static_cast<std::size_t>(gy) * gw + gx] =
              static_cast<float>(p[0] * 0.299 + p[1] * 0.587 + p[2] * 0.114) / 255.0f;
          }
        }
        const double bleed = std::clamp(static_cast<double>(fx.paramA), 0.0, 1.0);
        std::vector<float> vx(static_cast<std::size_t>(gw) * gh, 0.0f);
        std::vector<float> vy(static_cast<std::size_t>(gw) * gh, 0.0f);
        for (int gy = 0; gy < gh; ++gy) {
          const int ym = std::max(0, gy - 1), yp = std::min(gh - 1, gy + 1);
          for (int gx = 0; gx < gw; ++gx) {
            const int xm = std::max(0, gx - 1), xp = std::min(gw - 1, gx + 1);
            const float gxv = luma[static_cast<std::size_t>(gy) * gw + xp] -
                              luma[static_cast<std::size_t>(gy) * gw + xm];
            const float gyv = luma[static_cast<std::size_t>(yp) * gw + gx] -
                              luma[static_cast<std::size_t>(ym) * gw + gx];
            const std::size_t i = static_cast<std::size_t>(gy) * gw + gx;
            // Perpendicular at bleed 0, straight down the gradient at bleed 1.
            vx[i] = static_cast<float>(-gyv * (1.0 - bleed) + gxv * bleed);
            vy[i] = static_cast<float>( gxv * (1.0 - bleed) + gyv * bleed);
          }
        }
        const int steps = 3 + static_cast<int>(
          std::clamp(static_cast<double>(fx.paramB), 0.0, 1.0) * 13.0);
        // Total travel is what the operator set; the step count only decides
        // how CURVED the path is. Otherwise raising the detail would also raise
        // the strength and neither control would mean anything on its own.
        const double travel = amt * std::max(ctx.width, ctx.height) * 0.09;
        const double stepLen = travel / steps;
        // Fold the step length into the field once rather than multiplying by
        // it inside the walk. The walk runs `steps` times per pixel -- twenty
        // million multiplies on a 1080p frame at the default detail -- and the
        // product is the same every time.
        for (std::size_t i = 0; i < vx.size(); ++i) {
          vx[i] = static_cast<float>(vx[i] * stepLen);
          vy[i] = static_cast<float>(vy[i] * stepLen);
        }
        std::vector<std::uint8_t> source(pixels.begin(),
                                         pixels.begin() + static_cast<std::ptrdiff_t>(count * 4));
        // Scale factors, not divisions. The inner loop runs once per step per
        // pixel -- eleven million times on a 4K frame at the default detail --
        // and two integer divides in there were most of the cost.
        const double gxScale = static_cast<double>(gw) / ctx.width;
        const double gyScale = static_cast<double>(gh) / ctx.height;
        // Split across cores: every pixel reads the untouched source copy.
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            std::uint8_t* dstRow = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
            for (int x = 0; x < ctx.width; ++x) {
              double px = x;
              double py = y;
              for (int step = 0; step < steps; ++step) {
                const int gx = std::clamp(static_cast<int>(px * gxScale), 0, gw - 1);
                const int gy = std::clamp(static_cast<int>(py * gyScale), 0, gh - 1);
                const std::size_t i = static_cast<std::size_t>(gy) * gw + gx;
                px -= vx[i];
                py -= vy[i];
              }
              const int sx = std::clamp(static_cast<int>(std::lround(px)), 0, ctx.width - 1);
              const int sy = std::clamp(static_cast<int>(std::lround(py)), 0, ctx.height - 1);
              const std::uint8_t* sp =
                source.data() + (static_cast<std::size_t>(sy) * ctx.width + sx) * 4;
              std::uint8_t* dp = dstRow + static_cast<std::size_t>(x) * 4;
              dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2];
            }
          }
        });
        break;
      }
      case CueEffectKind::ReactionBloom: {
        // Gray-Scott reaction-diffusion, seeded by the picture and grown for a
        // few dozen iterations every frame.
        //
        // Two notional chemicals: U is everywhere, V is dropped wherever the
        // picture is bright. V consumes U, both diffuse, and that single rule
        // is enough to produce the coral, veins and dividing cells Turing
        // predicted in 1952. The pattern is not drawn -- it GROWS, and it grows
        // out of whatever is on screen.
        //
        // Re-seeded from the frame each time rather than carried forward. A
        // persistent grid settles into its own attractor and stops having
        // anything to do with the video; re-seeding means the growth tracks the
        // picture, and a cut to a new shot grows a new organism.
        // The grid stays small and the ITERATION COUNT is what the operator
        // buys with it. Gray-Scott needs hundreds of steps before anything
        // grows -- at a few dozen it has only blurred the seed, which is what
        // the first version of this did, and it looked like a coloured haze
        // because that is all it was. Cells are cheap; steps are the effect.
        const int gw = std::clamp(ctx.width / 6, 32, 192);
        const int gh = std::clamp(ctx.height / 6, 32, 192);
        const std::size_t cells = static_cast<std::size_t>(gw) * gh;
        std::vector<float> u(cells, 1.0f), v(cells, 0.0f);
        for (int gy = 0; gy < gh; ++gy) {
          const int sy = std::min(ctx.height - 1, gy * ctx.height / gh);
          for (int gx = 0; gx < gw; ++gx) {
            const int sx = std::min(ctx.width - 1, gx * ctx.width / gw);
            const std::uint8_t* p =
              pixels.data() + (static_cast<std::size_t>(sy) * ctx.width + sx) * 4;
            const float l =
              static_cast<float>(p[0] * 0.299 + p[1] * 0.587 + p[2] * 0.114) / 255.0f;
            // SPARSE seeds in a full field of U, which is what Gray-Scott
            // needs and the reason a first attempt grew nothing. Seeding V
            // across every bright pixel leaves those cells with no U around
            // them to consume: U is eaten in one step, V then decays 9% a step
            // while feed replenishes U at 3%, and the whole field is dead long
            // before it could organise. (It was also four times SLOWER that
            // way -- a field decaying toward zero spends its last few hundred
            // steps in denormal arithmetic.)
            //
            // Scattered seeds in a full reservoir is the arrangement every
            // working Gray-Scott uses: each one eats outward into the U around
            // it, and the front is the pattern.
            const std::size_t ci = static_cast<std::size_t>(gy) * gw + gx;
            // Seeded in 2x2 BLOCKS, not single cells. A lone cell of V is
            // mostly boundary: it diffuses into the surrounding U faster than
            // the reaction can consume it, and the gentler presets (solitons,
            // mitosis, worms) all died on contact. A block has an interior and
            // survives long enough to organise.
            std::uint32_t h = static_cast<std::uint32_t>((gx >> 1) * 374761393 +
                                                         (gy >> 1) * 668265263);
            h = (h ^ (h >> 13)) * 1274126177u;
            const float pick = static_cast<float>(h >> 8) / 16777216.0f;
            v[ci] = pick < l * l * 0.30f ? 1.0f : 0.0f;
            u[ci] = 1.0f;
          }
        }
        // Feed and kill are NOT independent knobs here, and that is deliberate.
        // Gray-Scott only produces anything over a thin curved sliver of the
        // (F,k) plane; almost everywhere else the reaction dies out flat or
        // floods solid, and a first attempt at this shipped a pair sitting in
        // the dead zone -- 500 iterations of nothing, which looked like a
        // coloured haze because that is all it was. So paramA walks ALONG the
        // living region instead of across it: one knob, and every position on
        // it grows something.
        // paramA walks a curve through PRESETS THAT ARE KNOWN TO LIVE, rather
        // than interpolating F and k independently. Two earlier attempts picked
        // plausible-looking pairs and both died: one showed nothing at all, the
        // other grew for a hundred steps and had vanished by nine hundred. The
        // living region is a thin sliver and it does not run along either axis,
        // so the only reliable way to stay inside it is to steer between points
        // that are documented to work.
        static const struct { float feed, kill; } kLiving[] = {
          {0.0390f, 0.0580f},   // waves
          {0.0460f, 0.0594f},   // labyrinth
          {0.0545f, 0.0620f},   // coral
          {0.0580f, 0.0650f},   // worms
          {0.0620f, 0.0610f},   // holes, on the edge of chaos
        };
        const int kLivingCount = static_cast<int>(sizeof(kLiving) / sizeof(kLiving[0]));
        const double along = std::clamp(static_cast<double>(fx.paramA), 0.0, 1.0)
                             * (kLivingCount - 1);
        const int lo = std::min(kLivingCount - 1, static_cast<int>(along));
        const int hi = std::min(kLivingCount - 1, lo + 1);
        const float blend = static_cast<float>(along - lo);
        const float feed = kLiving[lo].feed + blend * (kLiving[hi].feed - kLiving[lo].feed);
        const float kill = kLiving[lo].kill + blend * (kLiving[hi].kill - kLiving[lo].kill);
        // Karl Sims' weights and rates, which are the ones known to actually
        // evolve: a 9-point laplacian normalised to a -1 centre, and diffusion
        // an order of magnitude faster than a naive 5-point stencil can carry.
        // The earlier 5-point version was stable but so slow that hundreds of
        // steps moved nothing.
        const int iters = 60 + static_cast<int>(
          std::clamp(static_cast<double>(fx.paramB), 0.0, 1.0) * 440.0);
        std::vector<float> un(cells), vn(cells);
        // Hundreds of small DEPENDENT steps, so the threads are created once
        // and parked on a barrier between them. Handing the work out per step
        // instead -- which is what suits a single pass over a frame -- made
        // this 1.8x SLOWER than one core, measured.
        //
        // Row pointers, too: recomputing gy*gw+gx nine times a cell was most
        // of what was left after the threading.
        detail::iteratedBands(iters, gh, 24,
          [&](int, int firstRow, int lastRow) {
          for (int gy = firstRow; gy < lastRow; ++gy) {
            const int ym = std::max(0, gy - 1), yp = std::min(gh - 1, gy + 1);
            const float* uMid = u.data() + static_cast<std::size_t>(gy) * gw;
            const float* uUp  = u.data() + static_cast<std::size_t>(ym) * gw;
            const float* uDn  = u.data() + static_cast<std::size_t>(yp) * gw;
            const float* vMid = v.data() + static_cast<std::size_t>(gy) * gw;
            const float* vUp  = v.data() + static_cast<std::size_t>(ym) * gw;
            const float* vDn  = v.data() + static_cast<std::size_t>(yp) * gw;
            float* uOut = un.data() + static_cast<std::size_t>(gy) * gw;
            float* vOut = vn.data() + static_cast<std::size_t>(gy) * gw;
            for (int gx = 0; gx < gw; ++gx) {
              const int xm = gx > 0 ? gx - 1 : 0;
              const int xp = gx + 1 < gw ? gx + 1 : gw - 1;
              const float lapU = (uMid[xm] + uMid[xp] + uUp[gx] + uDn[gx]) * 0.2f +
                                 (uUp[xm] + uUp[xp] + uDn[xm] + uDn[xp]) * 0.05f - uMid[gx];
              const float lapV = (vMid[xm] + vMid[xp] + vUp[gx] + vDn[gx]) * 0.2f +
                                 (vUp[xm] + vUp[xp] + vDn[xm] + vDn[xp]) * 0.05f - vMid[gx];
              const float uvv = uMid[gx] * vMid[gx] * vMid[gx];
              uOut[gx] = std::clamp(
                uMid[gx] + (1.0f * lapU - uvv + feed * (1.0f - uMid[gx])), 0.0f, 1.0f);
              const float nv = vMid[gx] + (0.5f * lapV + uvv - (feed + kill) * vMid[gx]);
              // Flushed rather than merely clamped: a value decaying toward
              // zero goes denormal and denormal arithmetic costs an order of
              // magnitude, which showed up as the effect getting slower the
              // less it had to say.
              vOut[gx] = nv < 1e-7f ? 0.0f : std::clamp(nv, 0.0f, 1.0f);
            }
          }
          },
          [&](int) {
            // Between steps, with every band stopped: the swap is the one
            // moment the grid is not safe to read.
            u.swap(un);
            v.swap(vn);
          });
        // V is where the reaction ran, and that is what gets drawn: the picture
        // folds through its own negative wherever the growth reached, so the
        // veins read as light coming THROUGH the image rather than paint on it.
        // Sampled bilinearly on the way out. The reaction grid is a fraction
        // of the raster, and reading it nearest-neighbour drew the pattern as
        // visible rectangles -- the growth is organic and it should not arrive
        // looking like a spreadsheet.
        // Split across cores: the reaction grid is read-only here.
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            const double fy = std::clamp((y + 0.5) * gh / ctx.height - 0.5, 0.0, gh - 1.0);
            const int gy0 = static_cast<int>(fy);
            const int gy1 = std::min(gh - 1, gy0 + 1);
            const double wy = fy - gy0;
            std::uint8_t* row = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
            for (int x = 0; x < ctx.width; ++x) {
              const double fxs = std::clamp((x + 0.5) * gw / ctx.width - 0.5, 0.0, gw - 1.0);
              const int gx0 = static_cast<int>(fxs);
              const int gx1 = std::min(gw - 1, gx0 + 1);
              const double wx = fxs - gx0;
              const double top =
                v[static_cast<std::size_t>(gy0) * gw + gx0] * (1.0 - wx) +
                v[static_cast<std::size_t>(gy0) * gw + gx1] * wx;
              const double bot =
                v[static_cast<std::size_t>(gy1) * gw + gx0] * (1.0 - wx) +
                v[static_cast<std::size_t>(gy1) * gw + gx1] * wx;
              const double grown =
                std::clamp((top * (1.0 - wy) + bot * wy) * 3.2, 0.0, 1.0);
              if (grown <= 0.002) continue;
              const double mix = grown * amt;
              std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
              for (int c = 0; c < 3; ++c) {
                p[c] = detail::clamp8(p[c] * (1.0 - mix) + (255 - p[c]) * mix);
              }
            }
          }
        });
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

// Does this effect need a driver clip to do anything?
//
// Only motion puppet, today. Asked as a question about the KIND rather than
// tested against it at the call sites, so a second driver-fed effect cannot be
// added without this answering for it too.
inline bool cueEffectNeedsDriver(CueEffectKind kind) {
  return kind == CueEffectKind::MotionPuppet;
}

// A BYPASSED puppet still counts. Bypass is a temporary "not right now" and
// throwing the operator's driver away because they muted an effect for a
// moment would be losing their work to a toggle.
inline bool cueEffectStackNeedsDriver(const std::vector<CueEffect>& stack) {
  for (const CueEffect& fx : stack) {
    if (cueEffectNeedsDriver(fx.kind)) {
      return true;
    }
  }
  return false;
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
