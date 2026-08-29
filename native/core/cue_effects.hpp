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
  Caustics,
  Feedback,
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
    case CueEffectKind::Caustics:       return "caustics";
    case CueEffectKind::Feedback:       return "feedback";
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
    case CueEffectKind::Caustics:       return "caustics";
    case CueEffectKind::Feedback:       return "feedback";
    default:                            return "none";
  }
}

// Whether this effect looks DIFFERENT from one frame to the next on an
// unchanging picture.
//
// This matters because a still cue decodes exactly one frame, and both render
// paths skip re-applying the stack when the source frame has not changed. An
// effect that advances with time therefore ran once on a still and froze --
// grain that does not move, a ripple standing perfectly still, and caustics and
// feedback, whose entire subject is motion, reduced to one arbitrary frame of
// themselves. The gate is right for the effects it was written for; it just
// cannot know about these.
//
// Two ways an effect can move: it reads ctx.frameIndex, or it carries state
// between calls. Both are listed here, and tools/check_effects_offline.py
// --animation renders each effect at nine frame indices and fails if this list
// disagrees with what the pixels do -- so a new effect cannot be misfiled here
// without something saying so.
inline bool cueEffectKindAnimates(CueEffectKind kind) {
  switch (kind) {
    // Advance with the frame index.
    case CueEffectKind::Grain:
    case CueEffectKind::TemporalDither:
    case CueEffectKind::BlockGlitch:
    case CueEffectKind::Ripple:
    case CueEffectKind::Caustics:
    // Carry state between calls, so they move without reading the index.
    case CueEffectKind::Feedback:
    case CueEffectKind::MotionPuppet:
      return true;
    default:
      return false;
  }
}

// What an effect's shaping parameters MEAN, or null when it has no such slot.
//
// `which` is 0-3 for paramA-paramD. Naming them here rather than in the
// inspector keeps the meaning next to the code that reads it, so a parameter
// cannot be repurposed without the label moving with it.
//
// Read the neutral-value note on CueEffect before adding one: A=0.5, B=0, C=0,
// D=0 must reproduce what the effect did before the parameter existed.
inline const char* cueEffectParamLabel(CueEffectKind kind, int which) {
  switch (kind) {
    case CueEffectKind::Invert:
      return which == 0 ? "pivot" : which == 1 ? "channel spread" : nullptr;
    case CueEffectKind::Posterise:
      return which == 0 ? "band curve" : which == 1 ? "channel skew" : nullptr;
    case CueEffectKind::Solarise:
      return which == 0 ? "fold point" : which == 1 ? "knee" : nullptr;
    case CueEffectKind::Threshold:
      return which == 0 ? "pivot" : which == 1 ? "softness" : nullptr;
    case CueEffectKind::Vignette:
      return which == 0 ? "size" : which == 1 ? "falloff" : nullptr;
    case CueEffectKind::Grain:
      return which == 0 ? "grain size" : which == 1 ? "colour" : nullptr;
    case CueEffectKind::Scanlines:
      return which == 0 ? "pitch" : which == 1 ? "darkness" : nullptr;
    case CueEffectKind::ChannelOffset:
      return which == 0 ? "angle" : which == 1 ? "green split" : nullptr;
    case CueEffectKind::TemporalDither:
      return which == 0 ? "palette" : which == 1 ? "hold" : nullptr;
    case CueEffectKind::PixelSort:
      return which == 0 ? "threshold" : which == 1 ? "reverse" : nullptr;
    case CueEffectKind::BlockGlitch:
      return which == 0 ? "bands" : which == 1 ? "tear width" : nullptr;
    case CueEffectKind::PolarWarp:
      return which == 0 ? "twist" : which == 1 ? "radial zoom" : nullptr;
    case CueEffectKind::LumaDisplace:
      return which == 0 ? "vertical bias" : which == 1 ? "pivot" : nullptr;
    case CueEffectKind::Ripple:
      return which == 0 ? "frequency" : which == 1 ? "speed" : nullptr;
    case CueEffectKind::Kaleidoscope:
      return which == 0 ? "wedges" : which == 1 ? "rotation" : nullptr;
    case CueEffectKind::MotionPuppet:
      return which == 0 ? "spring" : which == 1 ? "memory" : nullptr;
    case CueEffectKind::DyeAdvect:
      return which == 0 ? "bleed" : which == 1 ? "curl detail"
           : which == 2 ? "swirl" : nullptr;
    case CueEffectKind::ReactionBloom:
      return which == 0 ? "feed rate" : which == 1 ? "growth"
           : which == 2 ? "seed density" : which == 3 ? "glow" : nullptr;
    case CueEffectKind::Relativistic:
      return which == 0 ? "field of view" : which == 1 ? "doppler"
           : which == 2 ? "off-axis" : nullptr;
    case CueEffectKind::Caustics:
      return which == 0 ? "chop" : which == 1 ? "swell speed"
           : which == 2 ? "focus" : nullptr;
    case CueEffectKind::Feedback:
      return which == 0 ? "zoom" : which == 1 ? "spin"
           : which == 2 ? "drift" : which == 3 ? "colour bleed" : nullptr;
    default:
      return nullptr;
  }
}

// The one-line explanation the inspector shows for that parameter.
inline const char* cueEffectParamTip(CueEffectKind kind, int which) {
  switch (kind) {
    case CueEffectKind::Invert:
      return which == 0
        ? "What the picture folds around. Centre is a straight negative; move "
          "it and the highlights or the shadows survive."
        : "Inverts the channels by different amounts, so the negative comes "
          "back coloured instead of grey.";
    case CueEffectKind::Posterise:
      return which == 0
        ? "Where the bands bunch up. Centre spaces them evenly; either side "
          "crowds them into the shadows or the highlights."
        : "Gives each channel a different number of bands, which is what turns "
          "posterising into false colour.";
    case CueEffectKind::Solarise:
      return which == 0
        ? "Where highlights fold back through black. Low folds most of the "
          "picture, high folds only the brightest."
        : "Softens the fold so it ramps instead of creasing.";
    case CueEffectKind::Threshold:
      return which == 0 ? "The brightness that decides black from white."
                        : "Widens the decision into a ramp, so edges keep "
                          "their shape instead of jagging.";
    case CueEffectKind::Vignette:
      return which == 0 ? "How far out the darkening starts."
                        : "How abruptly it arrives. Low is a soft cloud, high "
                          "is a hard porthole.";
    case CueEffectKind::Grain:
      return which == 0
        ? "Fine sensor noise at the bottom, chunky film at the top."
        : "Zero is one noise value across all three channels, which reads as "
          "film. Higher gives each channel its own, which reads as video.";
    case CueEffectKind::Scanlines:
      return which == 0
        ? "Line pitch in pixels, so this reads at 4K as well as at 720."
        : "How dark the dark lines go. All the way up is black between lines.";
    case CueEffectKind::ChannelOffset:
      return which == 0
        ? "Which way red and blue pull apart. Centre is horizontal, the "
          "classic; turn it for a diagonal or vertical fringe."
        : "Drags green the other way, which turns a two-colour fringe into a "
          "full prism.";
    case CueEffectKind::TemporalDither:
      return which == 0
        ? "How many shades survive the quantise."
        : "Holds the dither pattern for more than one frame. At zero it "
          "advances every frame and the eye integrates it; wind it up and the "
          "checkerboard stops moving and becomes visible.";
    case CueEffectKind::PixelSort:
      return which == 0
        ? "How bright a pixel has to be to join a run. Lower catches more of "
          "the picture and the runs grow longer."
        : "Sorts the runs the other way, so they pour from light to dark.";
    case CueEffectKind::BlockGlitch:
      return which == 0 ? "How many torn bands per frame."
                        : "How far a band can slide.";
    case CueEffectKind::PolarWarp:
      return which == 0 ? "Rotates the reading, so straight lines wind into "
                          "spirals rather than rings."
                        : "Pushes the rings in or out of the centre.";
    case CueEffectKind::LumaDisplace:
      return which == 0
        ? "How much of the push goes downward rather than sideways."
        : "The brightness that stays put. Everything above it reaches one way "
          "and everything below it the other.";
    case CueEffectKind::Ripple:
      return which == 0 ? "How closely spaced the rings are."
                        : "How fast they travel outward.";
    case CueEffectKind::MotionPuppet:
      return which == 0
        ? "How quickly the picture springs back to rest once the driver stops "
          "pushing it. Only does anything when memory is up."
        : "How much of the driver's movement the picture KEEPS. At zero it "
          "follows the driver frame by frame, which is a twitch. Wind it up "
          "and the motion accumulates, so the driver sculpts the picture over "
          "time and it holds the pose -- which is what puppetry means.";
    case CueEffectKind::Kaleidoscope:
      return which == 0
        ? "Two is a mirror, twelve is a snowflake, and they are completely "
          "different pictures."
        : "Turns the whole arrangement, so the fold lines do not sit on the "
          "same part of the picture all night.";
    case CueEffectKind::DyeAdvect:
      return which == 0
        ? "0 flows ALONG the edges, so colour orbits the shapes. 1 flows "
          "across them, which bleeds the picture into itself."
        : which == 1
        ? "How many steps each pixel walks. More steps curve further around "
          "the shapes; the total distance does not change."
        : "Rotates the whole flow field, which turns orbiting into spiralling "
          "inward or outward.";
    case CueEffectKind::ReactionBloom:
      return which == 0
        ? "The character of the growth: low is waves, then labyrinth, coral, "
          "worms, and holes at the top."
        : which == 1
        ? "How long the reaction runs each frame. Short is a stain, long is an "
          "organism."
        : which == 2
        ? "How much of the picture is seeded. Sparse grows a few large "
          "colonies; dense grows a crowded one."
        : "Draws the growth as light instead of as a negative.";
    case CueEffectKind::Relativistic:
      return which == 0
        ? "How wide a view is being compressed. Narrow is a punch down the "
          "middle, wide folds the whole frame into the centre."
        : which == 1
        ? "Blueshift toward the direction of travel and redshift at the rim. "
          "Zero leaves the colour alone and it reads as a lens instead."
        : "Moves the direction of travel off the centre of frame, so the "
          "picture rushes past rather than straight at you.";
    case CueEffectKind::Feedback:
      return which == 0
        ? "How much the echo grows or shrinks each pass. Centre holds still; "
          "up pulls a tunnel toward you, down sucks it away."
        : which == 1
        ? "How far the echo turns each pass. A little makes a spiral."
        : which == 2
        ? "Slides the echo sideways, which turns the tunnel into a comet."
        : "Lets the channels decay at different rates, so the trail changes "
          "colour as it fades instead of just going dim.";
    case CueEffectKind::Caustics:
      return which == 0
        ? "How fine the water is. Long ocean swell at one end, rain on a "
          "puddle at the other."
        : which == 1
        ? "How fast the surface moves."
        : "How hard the light gathers where the rays converge. This is the "
          "part that makes it read as water rather than as a wobble.";
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

// ---------------------------------------------------------------------------
// A low-frequency oscillator, per parameter.
// ---------------------------------------------------------------------------

enum class LfoShape : std::uint8_t {
  Sine,      // the default: moves fastest through the middle, eases at the ends
  Triangle,  // constant speed, hard turn
  Saw,       // ramp up, snap back
  Ramp,      // ramp down, snap up
  Square,    // two values, nothing between
  Sample,    // a new random value each cycle, HELD -- steps, not noise
  Count
};

inline const char* lfoShapeToken(LfoShape shape) {
  switch (shape) {
    case LfoShape::Sine:     return "sine";
    case LfoShape::Triangle: return "triangle";
    case LfoShape::Saw:      return "saw";
    case LfoShape::Ramp:     return "ramp";
    case LfoShape::Square:   return "square";
    case LfoShape::Sample:   return "sample";
    default:                 return "sine";
  }
}

struct ParamLfo {
  bool on = false;
  LfoShape shape = LfoShape::Sine;
  // Free-running rate. Slow by default: an LFO you notice is usually too fast,
  // and a quarter of a hertz is a four-second breath.
  float rateHz = 0.25f;
  // How far it swings, as a fraction of the whole 0-1 parameter range. The
  // centre is the value the operator SET, so turning an LFO on never jumps the
  // picture -- it starts moving from where it already was.
  float depth = 0.5f;
  float phase = 0.0f;      // 0-1, for running two parameters out of step
  // Follow the tempo instead of the clock. The tap tempo already exists for VJ
  // mode, so an LFO that ignored it would be the second, disagreeing clock in a
  // machine that already knows what the music is doing.
  bool beatSync = false;
  float beats = 4.0f;      // cycle length in beats when synced
};

// The oscillator itself, 0-1 out.
//
// `seconds` is a shared clock and `beatPhase` is where we are inside the
// current beat (0-1). Both come from the caller so that the preview and the
// output evaluate the SAME oscillator at the same moment -- two clocks would
// mean the operator's monitor and the audience's screen disagreed.
inline double lfoUnitValue(const ParamLfo& lfo, double seconds, double beats01) {
  double phase = 0.0;
  if (lfo.beatSync) {
    const double cycle = std::max(0.25, static_cast<double>(lfo.beats));
    phase = beats01 / cycle;
  } else {
    phase = seconds * std::max(0.001, static_cast<double>(lfo.rateHz));
  }
  phase += lfo.phase;
  phase -= std::floor(phase);
  switch (lfo.shape) {
    case LfoShape::Triangle:
      return phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0;
    case LfoShape::Saw:
      return phase;
    case LfoShape::Ramp:
      return 1.0 - phase;
    case LfoShape::Square:
      return phase < 0.5 ? 0.0 : 1.0;
    case LfoShape::Sample: {
      // One value per cycle, held. Hashed from the cycle NUMBER rather than
      // drawn from a generator, so it is the same every time the show reaches
      // the same moment -- a random that changes between rehearsal and
      // performance is not usable.
      const std::uint64_t cycle = static_cast<std::uint64_t>(
        std::floor(lfo.beatSync ? beats01 / std::max(0.25, static_cast<double>(lfo.beats))
                                : seconds * std::max(0.001, static_cast<double>(lfo.rateHz))));
      std::uint64_t h = cycle * 6364136223846793005ull + 1442695040888963407ull;
      h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33;
      return static_cast<double>(h >> 11) / static_cast<double>(1ull << 53);
    }
    case LfoShape::Sine:
    default:
      return 0.5 - 0.5 * std::cos(phase * 6.283185307179586);
  }
}

// The value a parameter actually takes this frame.
//
// Centred on what the operator set, so switching an LFO on does not jump the
// picture, and clamped because every parameter here is 0-1. Near the ends of
// the range the swing is necessarily lopsided; that is better than either
// moving the centre or refusing to oscillate.
inline float lfoApply(const ParamLfo& lfo, float base, double seconds,
                      double beats01) {
  if (!lfo.on) {
    return base;
  }
  const double unit = lfoUnitValue(lfo, seconds, beats01);
  const double swing = (unit - 0.5) * 2.0 * static_cast<double>(lfo.depth);
  const double out = static_cast<double>(base) + swing * 0.5;
  return static_cast<float>(out < 0.0 ? 0.0 : (out > 1.0 ? 1.0 : out));
}

struct CueEffect {
  CueEffectKind kind = CueEffectKind::None;
  float amount = 1.0f;    // 0 = inactive; every effect is skipped entirely at 0
  // Four shaping parameters, all 0-1.
  //
  // THE NEUTRAL VALUES ARE LOAD-BEARING. A show saved before an effect grew a
  // parameter carries paramA = 0.5 (what the UI wrote when the effect was
  // added), paramB = 0, and no C or D at all. So every parameter is defined so
  // that A=0.5, B=0, C=0, D=0 reproduces exactly what that effect did before it
  // had them -- otherwise adding a control would silently restage every show
  // that already used the effect.
  float paramA = 0.0f;
  float paramB = 0.0f;
  float paramC = 0.0f;
  float paramD = 0.0f;
  // BYPASS is not the same as amount 0. Turning an effect down to nothing
  // loses the setting you spent time on; bypass takes it out of the chain and
  // gives it back. Every serious tool has both and they are not
  // interchangeable.
  bool bypassed = false;

  // One LFO per parameter, off by default.
  //
  // An effect stack you have to hold with your hands is a stack that does one
  // thing while you are touching it and nothing while you are not. A parameter
  // handed to an oscillator moves on its own, which is the difference between a
  // look and a performance -- and it is what Resolume operators reach for
  // first.
  //
  // Index 0-3 is paramA-paramD, and index 4 is the AMOUNT, because "how much of
  // this effect" is the parameter people want breathing most of all.
  ParamLfo lfo[5];
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
  // Somewhere to keep the LAST frame, for feedback. Owned by the caller and
  // one per deck, because the stack itself is deliberately stateless -- an
  // effect that remembered things internally could not be run twice, could not
  // be dumped headlessly, and would have to guess which cue it belonged to.
  // Null everywhere that has no such buffer, and feedback then does nothing
  // rather than pretending.
  std::vector<std::uint8_t>* feedback = nullptr;

  // Set when this is NOT the first consumer of the deck this frame -- a second
  // output showing the same deck, say. The loop then reads the same previous
  // frame the first consumer read and does not step, so every output gets the
  // identical picture instead of each one advancing the echo again and the two
  // screens drifting apart. The same fault the motion driver had, and the same
  // shape of answer.
  bool feedbackHold = false;
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
inline void applyPixelSort(std::uint8_t* pixels, int w, int h, double amount,
                           double bias = 0.5, bool reverse = false) {
  if (!pixels || w <= 2 || h <= 0 || amount <= 0.01) {
    return;
  }
  // The threshold FALLS as the amount rises, so more of each row qualifies and
  // the runs grow longer. Driving run length directly instead would cut spans
  // at arbitrary points and read as banding rather than flow.
  // bias moves the qualifying brightness up or down. At 0.5 it contributes
  // nothing and the threshold is what amount alone decided.
  const int threshold = std::clamp(static_cast<int>(
    200.0 - std::clamp(amount, 0.0, 1.0) * 170.0 +
    (std::clamp(bias, 0.0, 1.0) - 0.5) * 2.0 * 60.0), 2, 253);
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
      // A TOTAL ORDER, not just a luma comparison.
      //
      // std::sort says nothing about how it orders elements it considers
      // equal, and two standard libraries do not have to agree. Sorting by
      // luma alone left every equal-luma pixel free to land anywhere, so the
      // same cue rendered visibly differently on macOS than on Windows --
      // caught by diffing the two byte for byte, and invisible any other way.
      // Breaking the tie on the pixel itself makes the result the same
      // everywhere without a stable sort's allocation.
      std::sort(run.begin(), run.end(), [reverse](std::uint32_t a, std::uint32_t b) {
        const int la = ((a >> 16) & 0xFF) * 77 + ((a >> 8) & 0xFF) * 151 + (a & 0xFF) * 28;
        const int lb = ((b >> 16) & 0xFF) * 77 + ((b >> 8) & 0xFF) * 151 + (b & 0xFF) * 28;
        if (la != lb) {
          return reverse ? lb < la : la < lb;
        }
        return a < b;
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
                             std::uint32_t seed, double bandScale = 0.5,
                             double tearScale = 0.0) {
  if (!pixels || w <= 2 || h <= 0 || amount <= 0.01) {
    return;
  }
  amount = std::clamp(amount, 0.0, 1.0);
  std::uint32_t state = seed | 1u;
  auto rnd = [&state]() {
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return state;
  };
  // bands scales how many tears there are, tear width how far one can slide.
  // At 0.5 and 0 respectively both are what they were before they existed.
  const int bands = std::max(1, static_cast<int>(
    (1.0 + amount * 14.0) * (0.5 + std::clamp(bandScale, 0.0, 1.0))));
  const double tearWidth = 1.0 + std::clamp(tearScale, 0.0, 1.0) * 2.0;
  std::vector<std::uint8_t> rowCopy(static_cast<std::size_t>(w) * 4);
  for (int b = 0; b < bands; ++b) {
    const int y0 = static_cast<int>(rnd() % static_cast<std::uint32_t>(std::max(1, h)));
    const int hgt = 1 + static_cast<int>(rnd() % static_cast<std::uint32_t>(
      std::max(1, static_cast<int>(h * amount / 8) + 1)));
    const int shift = static_cast<int>(
      (static_cast<int>(rnd() % static_cast<std::uint32_t>(std::max(1, w / 3))) -
       (w / 6)) * tearWidth);
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
    // Named once. See the note on CueEffect: A=0.5, B=0, C=0, D=0 is the
    // "as it was before this parameter existed" position for every effect.
    const double pA = std::clamp(static_cast<double>(fx.paramA), 0.0, 1.0);
    const double pB = std::clamp(static_cast<double>(fx.paramB), 0.0, 1.0);
    const double pC = std::clamp(static_cast<double>(fx.paramC), 0.0, 1.0);
    const double pD = std::clamp(static_cast<double>(fx.paramD), 0.0, 1.0);
    (void) pC; (void) pD;
    if (fx.kind == CueEffectKind::None || fx.bypassed || amt <= 0.0005) {
      continue;   // zero and bypass both cost nothing
    }
    switch (fx.kind) {
      case CueEffectKind::Invert: {
        // pivot: the picture folds around pA rather than always around mid
        // grey. At 0.5 that is 255-v, which is what this always did.
        // spread: each channel inverts by a different amount, so the negative
        // comes back coloured. At 0 all three match and nothing changes.
        const double pivot = pA * 255.0;
        std::uint8_t lut[3][256];
        for (int c = 0; c < 3; ++c) {
          const double channelAmt = amt * (1.0 - pB * (c * 0.5));
          for (int v = 0; v < 256; ++v) {
            const double folded = 2.0 * pivot - v;
            lut[c][v] = detail::clamp8(v * (1.0 - channelAmt) + folded * channelAmt);
          }
        }
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            std::uint8_t* p = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
            for (int x = 0; x < ctx.width; ++x, p += 4) {
              p[0] = lut[0][p[0]];
              p[1] = lut[1][p[1]];
              p[2] = lut[2][p[2]];
            }
          }
        });
        break;
      }
      case CueEffectKind::Posterise: {
        // Amount picks the level count: 1.0 is brutal (2 levels), low amounts
        // barely touch it. Inverted deliberately -- "more effect" should mean
        // "fewer levels", which is not what a naive mapping gives.
        const int levels = std::max(2, static_cast<int>(std::lround(2 + (1.0 - amt) * 30)));
        // band curve: quantise through a gamma so the bands bunch into the
        // shadows or the highlights. At 0.5 the exponent is 1 and the spacing
        // is even, which is what this always did.
        // channel skew: a different level count per channel, which is what
        // turns posterising into false colour. At 0 all three match.
        const double curve = std::pow(2.0, (pA - 0.5) * 2.0);
        std::uint8_t lut[3][256];
        for (int c = 0; c < 3; ++c) {
          const int channelLevels = std::max(2, levels - static_cast<int>(
            std::lround(pB * c * (levels - 2) * 0.5)));
          const double channelStep = 255.0 / (channelLevels - 1);
          for (int v = 0; v < 256; ++v) {
            if (curve == 1.0) {
              lut[c][v] = detail::clamp8(std::round(v / channelStep) * channelStep);
            } else {
              const double shaped = std::pow(v / 255.0, curve) * 255.0;
              const double banded = std::round(shaped / channelStep) * channelStep;
              lut[c][v] = detail::clamp8(
                std::pow(std::clamp(banded / 255.0, 0.0, 1.0), 1.0 / curve) * 255.0);
            }
          }
        }
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            std::uint8_t* p = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
            for (int x = 0; x < ctx.width; ++x, p += 4) {
              p[0] = lut[0][p[0]];
              p[1] = lut[1][p[1]];
              p[2] = lut[2][p[2]];
            }
          }
        });
        break;
      }
      case CueEffectKind::Solarise: {
        // Everything above the threshold inverts, which is the darkroom
        // effect: highlights fold back through black.
        const double pivot = 255.0 * std::clamp(pA, 0.05, 0.95);
        // knee: how wide a ramp the fold takes. At 0 it is the hard crease
        // this always had.
        const double knee = pB * 96.0;
        std::uint8_t lut[256];
        detail::buildChannelLut(lut, [&](int v) {
          double folded;
          if (knee <= 0.5) {
            folded = v > pivot ? (255.0 - v) : v;
          } else {
            const double t = std::clamp((v - (pivot - knee)) / (2.0 * knee), 0.0, 1.0);
            const double eased = t * t * (3.0 - 2.0 * t);
            folded = v * (1.0 - eased) + (255.0 - v) * eased;
          }
          return detail::clamp8(v * (1.0 - amt) + folded * amt);
        });
        detail::applyChannelLut(pixels.data(), ctx.width, ctx.height, lut);
        break;
      }
      case CueEffectKind::Threshold: {
        const double pivot = 255.0 * std::clamp(pA, 0.02, 0.98);
        std::uint8_t lutLit[256], lutDark[256];
        detail::buildChannelLut(lutLit, [&](int v) {
          return detail::clamp8(v * (1.0 - amt) + 255.0 * amt);
        });
        detail::buildChannelLut(lutDark, [&](int v) {
          return detail::clamp8(v * (1.0 - amt) + 0.0 * amt);
        });
        // softness: widens the decision into a ramp. At 0 it is the hard
        // comparison this always made, and the two tables are used directly.
        const double soft = pB * 110.0;
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            std::uint8_t* p = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
            for (int x = 0; x < ctx.width; ++x, p += 4) {
              const double luma = p[0] * 0.299 + p[1] * 0.587 + p[2] * 0.114;
              if (soft <= 0.5) {
                const std::uint8_t* lut = luma >= pivot ? lutLit : lutDark;
                p[0] = lut[p[0]];
                p[1] = lut[p[1]];
                p[2] = lut[p[2]];
              } else {
                const double t = std::clamp((luma - (pivot - soft)) / (2.0 * soft), 0.0, 1.0);
                const double eased = t * t * (3.0 - 2.0 * t);
                for (int c = 0; c < 3; ++c) {
                  p[c] = detail::clamp8(lutDark[p[c]] * (1.0 - eased) +
                                        lutLit[p[c]] * eased);
                }
              }
            }
          }
        });
        break;
      }
      case CueEffectKind::Vignette: {
        const double cx = ctx.width * 0.5;
        const double cy = ctx.height * 0.5;
        const double maxR = std::sqrt(cx * cx + cy * cy);
        const double vignetteSize = std::max(0.05, pA * 2.0);
        const double vignetteFalloff = 2.0 + pB * 6.0;
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            std::uint8_t* row = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
            const double dy = y - cy;
            for (int x = 0; x < ctx.width; ++x) {
              const double dx = x - cx;
              // size: how far out the darkening starts. At 0.5 the divisor is
              // 1 and it begins at the centre, as it always did.
              // falloff: the exponent. At 0 it is the square this always used,
              // which keeps the centre clean and takes the corners.
              const double r = std::sqrt(dx * dx + dy * dy) / maxR / vignetteSize;
              const double shaped = vignetteFalloff == 2.0
                ? r * r : std::pow(std::clamp(r, 0.0, 4.0), vignetteFalloff);
              const double gain = 1.0 - amt * std::clamp(shaped, 0.0, 1.0);
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
        // grain size: hash a coarser cell so the noise clumps into film grain
        // rather than sensor noise. At 0.5 the cell is one pixel, as before.
        // colour: at 0 one value goes to all three channels, which is what
        // film does and what this always did; higher gives each its own.
        const int grainCell = 1 + static_cast<int>(std::max(0.0, pA - 0.5) * 2.0 * 7.0);
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            std::size_t i = static_cast<std::size_t>(y) * ctx.width;
            std::uint8_t* p = pixels.data() + i * 4;
            for (int x = 0; x < ctx.width; ++x, ++i, p += 4) {
              const std::size_t cell = grainCell == 1
                ? i
                : static_cast<std::size_t>(y / grainCell) *
                    static_cast<std::size_t>(ctx.width) + (x / grainCell);
              std::uint32_t h = static_cast<std::uint32_t>(cell) ^ seed;
              h ^= h << 13; h ^= h >> 17; h ^= h << 5;
              const double n = (static_cast<double>(h & 0xFFFF) / 32768.0 - 1.0) * strength;
              if (pB <= 0.0005) {
                p[0] = detail::clamp8(p[0] + n);
                p[1] = detail::clamp8(p[1] + n);
                p[2] = detail::clamp8(p[2] + n);
              } else {
                for (int c = 0; c < 3; ++c) {
                  std::uint32_t hc = h ^ static_cast<std::uint32_t>(c * 0x9E3779B9u);
                  hc ^= hc << 13; hc ^= hc >> 17; hc ^= hc << 5;
                  const double nc =
                    (static_cast<double>(hc & 0xFFFF) / 32768.0 - 1.0) * strength;
                  p[c] = detail::clamp8(p[c] + n * (1.0 - pB) + nc * pB);
                }
              }
            }
          }
        });
        break;
      }
      case CueEffectKind::Scanlines: {
        // Every other line darkened. paramA sets the line pitch in pixels so
        // this reads at 4K as well as at 720 -- a one-pixel line on a 2160-line
        // raster is invisible, which is the mistake the synth's CRT made.
        const int pitch = std::max(2, static_cast<int>(std::lround(2.0 + pA * 10.0)));
        // darkness: how far the dark lines go. At 0 it is the 0.7 this was
        // fixed at; all the way up is black between the lines.
        const double lineDark = 0.7 + pB * 0.3;
        std::uint8_t lut[256];
        detail::buildChannelLut(lut, [&](int v) {
          return detail::clamp8(v * (1.0 - amt * lineDark));
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
        // angle: which way the channels pull apart. At 0.5 the offset is
        // purely horizontal, which is what this always did -- and the row-copy
        // path is kept for exactly that case, because it is much faster than
        // a two-dimensional gather.
        // green split: drags green the opposite way, turning a two-colour
        // fringe into a prism. At 0 green does not move, as before.
        const double angle = (pA - 0.5) * 2.0 * 3.14159265358979323846;
        const bool horizontal = std::fabs(angle) < 1e-6;
        const int greenShift = static_cast<int>(std::lround(shift * pB));
        if (horizontal && greenShift == 0) {
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
        } else {
          const double ox = std::cos(angle) * shift;
          const double oy = std::sin(angle) * shift;
          std::vector<std::uint8_t> source(pixels.begin(),
                                           pixels.begin() + static_cast<std::ptrdiff_t>(count * 4));
          detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
            auto tap = [&](int x, int y, int channel) {
              const int sx = std::clamp(x, 0, ctx.width - 1);
              const int sy = std::clamp(y, 0, ctx.height - 1);
              return source[(static_cast<std::size_t>(sy) * ctx.width + sx) * 4 + channel];
            };
            for (int y = firstRow; y < lastRow; ++y) {
              std::uint8_t* dst = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
              for (int x = 0; x < ctx.width; ++x) {
                const int rx = x + static_cast<int>(std::lround(ox));
                const int ry = y + static_cast<int>(std::lround(oy));
                const int bx = x - static_cast<int>(std::lround(ox));
                const int by = y - static_cast<int>(std::lround(oy));
                dst[static_cast<std::size_t>(x) * 4 + 2] = tap(rx, ry, 2);
                dst[static_cast<std::size_t>(x) * 4 + 0] = tap(bx, by, 0);
                if (greenShift != 0) {
                  dst[static_cast<std::size_t>(x) * 4 + 1] =
                    tap(x + static_cast<int>(std::lround(oy * pB)),
                        y - static_cast<int>(std::lround(ox * pB)), 1);
                }
              }
            }
          });
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
        // palette: scales the surviving shade count. At 0.5 the scale is 1
        // and the count is what amount alone decided, as before.
        // hold: how many frames a dither phase lasts. At 0 it advances every
        // frame -- which is the whole trick, because the eye integrates it --
        // and winding it up freezes the checkerboard into visibility.
        const double paletteScale = 0.5 + pA;
        const int levels = std::max(2, static_cast<int>(std::lround(
          (2.0 + (1.0 - amt) * 6.0) * paletteScale)));
        const double step = 255.0 / (levels - 1);
        // Rotate the matrix through its four phases, one per frame.
        const int holdFrames = 1 + static_cast<int>(std::lround(pB * 15.0));
        const int phase = static_cast<int>((ctx.frameIndex / holdFrames) & 3);
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
        applyPixelSort(pixels.data(), ctx.width, ctx.height, amt, pA, pB > 0.5);
        break;
      case CueEffectKind::BlockGlitch:
        // Seeded from the FRAME, not a running generator, so the same frame
        // always glitches the same way -- scrubbing back gives you the picture
        // you saw, not a new one.
        applyBlockGlitch(pixels.data(), ctx.width, ctx.height, amt,
                         static_cast<std::uint32_t>(ctx.frameIndex * 2654435761u),
                         pA, pB);
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
        const double halfFov = (12.0 + pA * 68.0) * 0.017453292519943295;
        const double dopplerAmt = pB;
        const double boostOffsetX = (fx.kind == CueEffectKind::Relativistic)
          ? pC * cx * 0.8 : 0.0;
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
                  // twist: winds the angle with the radius, so straight lines
                  // become spirals rather than rings. At 0.5 there is no wind.
                  // radial zoom: pushes the rings in or out. At 0 it is 1:1.
                  a += (pA - 0.5) * 2.0 * r * 6.283185307179586;
                  const double u = (a / 6.283185307179586) * ctx.width;
                  const double v = r * ctx.height * (1.0 + pB * 2.0);
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
                  // pivot: the brightness that stays put. At 0 it is mid grey,
                  // which is what this always used.
                  // vertical bias: how much of the push goes downward. At 0.5
                  // that is 0.35, the ratio this was fixed at.
                  const double pivotLuma = 0.5 + pB * 0.5;
                  const double push = (luma - pivotLuma) * amt * ctx.width * 0.25;
                  sxf = x + push;
                  syf = y + push * (0.35 + (pA - 0.5) * 2.0 * 0.65);
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
                  // off-axis: moves the direction of travel away from the
                  // centre of frame, so the picture rushes past rather than
                  // straight at you. At 0 it is dead centre, as before.
                  const double ax = x - (cx + boostOffsetX);
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
                  // frequency and speed. At paramA 0.5 the spacing is the 0.06
                // this had, and at paramB 0 the rings travel at the 3.0 it had.
                const double wave =
                  std::sin(r * (0.06 * pA * 2.0) - t * (3.0 + pB * 9.0)) * amt * 24.0;
                  const double inv = r > 0.001 ? 1.0 / r : 0.0;
                  sxf = x + dx * inv * wave;
                  syf = y + dy * inv * wave;
                  break;
                }
                default: {   // Kaleidoscope
                  // Fold the frame into wedges about its centre. paramA picks how
                  // many, because two is a mirror and twelve is a snowflake and
                  // they are completely different pictures.
                  const int wedges = 2 + static_cast<int>(pA * 10.0);
                  const double nx = x - cx;
                  const double ny = y - cy;
                  const double r = std::sqrt(nx * nx + ny * ny);
                  const double seg = 6.283185307179586 / wedges;
                  // rotation: turns the whole arrangement so the fold lines do
                  // not sit on the same part of the picture all night. At 0 it
                  // is where it always was.
                  double a = std::atan2(ny, nx) + pB * 6.283185307179586;
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
        const double bleed = pA;
        // swirl: rotates the whole flow field, which turns orbiting into
        // spiralling inward or outward. At 0 the field is untouched.
        const double swirl = pC * 6.283185307179586;
        const double swirlCos = std::cos(swirl);
        const double swirlSin = std::sin(swirl);
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
            const double fieldX = -gyv * (1.0 - bleed) + gxv * bleed;
            const double fieldY =  gxv * (1.0 - bleed) + gyv * bleed;
            vx[i] = static_cast<float>(fieldX * swirlCos - fieldY * swirlSin);
            vy[i] = static_cast<float>(fieldX * swirlSin + fieldY * swirlCos);
          }
        }
        const int steps = 3 + static_cast<int>(pB * 13.0);
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
        const float seedDensity = static_cast<float>(0.30 + pC * 0.55);
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
            // seed density: how much of the picture is seeded. At 0 it is
            // the 0.30 this was fixed at.
            v[ci] = pick < l * l * seedDensity ? 1.0f : 0.0f;
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
        const double along = pA * (kLivingCount - 1);
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
        const int iters = 60 + static_cast<int>(pB * 440.0);
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
                // glow: at 0 the growth folds the picture through its own
                // negative, which is what this always did. Wound up, it lifts
                // toward white instead, so the veins read as light on top of the
                // picture rather than through it.
                const double target = (255 - p[c]) * (1.0 - pD) + 255.0 * pD;
                p[c] = detail::clamp8(p[c] * (1.0 - mix) + target * mix);
              }
            }
          }
        });
        break;
      }
      case CueEffectKind::Caustics: {
        // The picture seen through moving water -- and unlike every other
        // "water" effect, the LIGHT is computed rather than only the bend.
        //
        // A refracting surface does two things at once: it displaces what you
        // see through it, and it concentrates or spreads the light doing so.
        // Where neighbouring rays are pushed TOWARD each other the brightness
        // piles up, and those bright filaments are caustics -- the moving net
        // of light on the floor of a swimming pool. Displacement alone is a
        // wobble; the wobble plus the focusing is what the eye reads as water.
        //
        // The focusing term is the DIVERGENCE of the displacement field. Where
        // it is negative the rays converge and the pixel brightens, where it is
        // positive they spread and it dims. One finite difference per cell, and
        // it is the whole difference between this and a ripple.
        const int gw = std::clamp(ctx.width / 6, 16, 320);
        const int gh = std::clamp(ctx.height / 6, 16, 320);
        const double phase = static_cast<double>(ctx.frameIndex) * 0.05 *
                             (0.35 + pB * 2.4);
        // Four crossed waves at different angles and rates, which is what stops
        // it reading as a regular grid. paramA is how fine the chop is: long
        // ocean swell at one end, rain on a puddle at the other.
        const double chop = 3.0 + pA * 26.0;
        std::vector<float> height(static_cast<std::size_t>(gw) * gh, 0.0f);
        for (int gy = 0; gy < gh; ++gy) {
          const double v = static_cast<double>(gy) / gh;
          for (int gx = 0; gx < gw; ++gx) {
            const double u = static_cast<double>(gx) / gw;
            double h = std::sin((u * chop) + phase);
            h += std::sin((v * chop * 0.87) - phase * 1.31);
            h += std::sin((u * 0.7 + v * 0.7) * chop * 1.19 + phase * 0.63);
            h += std::sin((u * 0.6 - v * 0.8) * chop * 1.53 - phase * 0.41);
            height[static_cast<std::size_t>(gy) * gw + gx] = static_cast<float>(h * 0.25);
          }
        }
        // Slope gives the bend; the change in slope gives the focusing.
        const double bend = amt * std::max(ctx.width, ctx.height) * 0.045;
        std::vector<float> dxField(static_cast<std::size_t>(gw) * gh, 0.0f);
        std::vector<float> dyField(static_cast<std::size_t>(gw) * gh, 0.0f);
        std::vector<float> light(static_cast<std::size_t>(gw) * gh, 1.0f);
        for (int gy = 0; gy < gh; ++gy) {
          const int ym = std::max(0, gy - 1), yp = std::min(gh - 1, gy + 1);
          for (int gx = 0; gx < gw; ++gx) {
            const int xm = std::max(0, gx - 1), xp = std::min(gw - 1, gx + 1);
            const std::size_t i = static_cast<std::size_t>(gy) * gw + gx;
            dxField[i] = height[static_cast<std::size_t>(gy) * gw + xp] -
                         height[static_cast<std::size_t>(gy) * gw + xm];
            dyField[i] = height[static_cast<std::size_t>(yp) * gw + gx] -
                         height[static_cast<std::size_t>(ym) * gw + gx];
          }
        }
        for (int gy = 0; gy < gh; ++gy) {
          const int ym = std::max(0, gy - 1), yp = std::min(gh - 1, gy + 1);
          for (int gx = 0; gx < gw; ++gx) {
            const int xm = std::max(0, gx - 1), xp = std::min(gw - 1, gx + 1);
            const std::size_t i = static_cast<std::size_t>(gy) * gw + gx;
            const double divergence =
              (dxField[static_cast<std::size_t>(gy) * gw + xp] -
               dxField[static_cast<std::size_t>(gy) * gw + xm]) +
              (dyField[static_cast<std::size_t>(yp) * gw + gx] -
               dyField[static_cast<std::size_t>(ym) * gw + gx]);
            // Converging rays brighten, spreading rays dim.
            //
            // The scaling matters more than it looks. A steep tanh saturates
            // on almost every cell, which turns the water into hard black and
            // white bands -- it stops being light and becomes a stencil. A
            // gentler curve, a smaller amplitude and a floor above zero keep
            // the filaments bright while the troughs stay picture rather than
            // going to nothing.
            light[i] = static_cast<float>(std::clamp(
              1.0 + std::tanh(-divergence * 5.0) * (0.10 + pC * 0.55),
              0.35, 1.85));
          }
        }
        std::vector<std::uint8_t> source(pixels.begin(),
                                         pixels.begin() + static_cast<std::ptrdiff_t>(count * 4));
        const double gxScale = static_cast<double>(gw) / ctx.width;
        const double gyScale = static_cast<double>(gh) / ctx.height;
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            std::uint8_t* dstRow = pixels.data() + static_cast<std::size_t>(y) * ctx.width * 4;
            const int gy = std::clamp(static_cast<int>(y * gyScale), 0, gh - 1);
            for (int x = 0; x < ctx.width; ++x) {
              const int gx = std::clamp(static_cast<int>(x * gxScale), 0, gw - 1);
              const std::size_t i = static_cast<std::size_t>(gy) * gw + gx;
              const int sx = std::clamp(
                static_cast<int>(std::lround(x + dxField[i] * bend)), 0, ctx.width - 1);
              const int sy = std::clamp(
                static_cast<int>(std::lround(y + dyField[i] * bend)), 0, ctx.height - 1);
              const std::uint8_t* sp =
                source.data() + (static_cast<std::size_t>(sy) * ctx.width + sx) * 4;
              std::uint8_t* dp = dstRow + static_cast<std::size_t>(x) * 4;
              const double gain = light[i];
              dp[0] = detail::clamp8(sp[0] * gain);
              dp[1] = detail::clamp8(sp[1] * gain);
              dp[2] = detail::clamp8(sp[2] * gain);
            }
          }
        });
        break;
      }
      case CueEffectKind::Feedback: {
        // Video feedback: a camera pointed at its own monitor, except the
        // transform between passes is chosen rather than accidental.
        //
        // The whole character of feedback is what happens to the picture on its
        // way round the loop. Scale it slightly up and the echo walks toward
        // you as a tunnel; scale it down and it retreats; add a turn and the
        // tunnel becomes a spiral; slide it and it smears into a comet. Those
        // are the controls, because they are the loop.
        //
        // CONTROLLED, which is what makes it usable on a stage: the echo is
        // always mixed at LESS than one, so each pass is strictly dimmer than
        // the last and the loop cannot run away. Real feedback blows out to
        // white the moment the gain passes unity and there is no getting it
        // back during a show; here the worst case is a long trail.
        if (!ctx.feedback) {
          break;   // no buffer from this caller: do nothing rather than pretend
        }
        // TWO planes and a cursor byte, all inside the one buffer the caller
        // carries. The obvious version allocated a whole frame every pass and
        // copied it back afterwards -- at 1080p that is eight megabytes of
        // allocation plus eight of memcpy per frame, and it was most of the
        // cost. Here the previous frame is READ from one plane while the new
        // one is written to the other and to the picture at the same time, so
        // there is no allocation, no copy back, and the read and the write can
        // never alias.
        std::vector<std::uint8_t>& store = *ctx.feedback;
        const std::size_t bytes = count * 4;
        const std::size_t wanted = bytes * 2 + 1;
        if (store.size() != wanted) {
          // First frame, or the raster changed. Both planes start as the
          // current picture, so the effect fades UP rather than flashing black.
          store.assign(wanted, 0);
          std::memcpy(store.data(), pixels.data(), bytes);
          std::memcpy(store.data() + bytes, pixels.data(), bytes);
          break;
        }
        // The cursor names the plane holding the newest frame. Stepping reads
        // that one and writes the other, then moves the cursor; holding reads
        // the plane the step already read, which is the one the cursor does not
        // name, and writes nothing.
        const bool secondPlaneIsLatest = store[bytes * 2] != 0;
        const bool hold = ctx.feedbackHold;
        const std::uint8_t* previous =
          store.data() + ((secondPlaneIsLatest != hold) ? bytes : 0);
        std::uint8_t* record =
          hold ? nullptr : store.data() + (secondPlaneIsLatest ? 0 : bytes);
        if (!hold) {
          store[bytes * 2] = secondPlaneIsLatest ? 0 : 1;
        }

        // Capped below 1: at amount 1.0 the echo is 0.92 per pass, which is a
        // very long trail and still strictly decaying.
        const double echo = amt * 0.92;
        const double zoom = 1.0 + (pA - 0.5) * 0.24;      // 0.5 holds still
        const double spin = (pB - 0.5) * 0.12;            // radians per pass
        const double drift = (pC - 0.5) * 0.06 * ctx.width;
        // Per-channel decay, so a trail can change colour as it fades instead
        // of only going dim.
        const double bleed = pD * 0.35;
        const double chanEcho[3] = {echo, echo * (1.0 - bleed * 0.5),
                                    echo * (1.0 - bleed)};
        const double cx = ctx.width * 0.5;
        const double cy = ctx.height * 0.5;
        const double cosSpin = std::cos(spin);
        const double sinSpin = std::sin(spin);
        // The echo is a fixed function of one byte, so it is three tables
        // rather than three multiplies and a clamp per pixel -- the same trick
        // the channel effects use, for the same reason.
        std::uint8_t echoLut[3][256];
        for (int c = 0; c < 3; ++c) {
          for (int v = 0; v < 256; ++v) {
            echoLut[c][v] = detail::clamp8(v * chanEcho[c]);
          }
        }
        // The source coordinate is affine in x, so it is a start and a step
        // per row in fixed point instead of two rotations and two lrounds per
        // pixel. Rounding is floor(v + 0.5), which agrees with lround
        // everywhere except an exact -0.5, and that lands outside the frame.
        //
        // 32 fractional bits, not 16: at 16 the step's own rounding error
        // accumulates to about four thousandths of a pixel across a 1080p row,
        // which is enough to flip a sample that sits near a half and moved a
        // few dozen pixels of a colour bar. At 32 the drift across a row is
        // under a millionth of a pixel and the result matches the plain double
        // version byte for byte.
        const std::int64_t kOne = static_cast<std::int64_t>(1) << 32;
        const std::int64_t stepX =
          static_cast<std::int64_t>(std::llround((cosSpin / zoom) * 4294967296.0));
        const std::int64_t stepY =
          static_cast<std::int64_t>(std::llround((sinSpin / zoom) * 4294967296.0));
        detail::parallelRows(ctx.height, ctx.width, [&](int firstRow, int lastRow) {
          for (int y = firstRow; y < lastRow; ++y) {
            const double ny = (y - cy) / zoom;
            std::int64_t accX = static_cast<std::int64_t>(std::llround(
              (cx - cx * cosSpin / zoom - ny * sinSpin + drift) * 4294967296.0)) +
              kOne / 2;
            std::int64_t accY = static_cast<std::int64_t>(std::llround(
              (cy - cx * sinSpin / zoom + ny * cosSpin) * 4294967296.0)) + kOne / 2;
            const std::size_t rowBase = static_cast<std::size_t>(y) * ctx.width * 4;
            std::uint8_t* live = pixels.data() + rowBase;
            std::uint8_t* keep = record ? record + rowBase : nullptr;
            for (int x = 0; x < ctx.width; ++x, accX += stepX, accY += stepY) {
              const int sx = static_cast<int>(accX >> 32);
              const int sy = static_cast<int>(accY >> 32);
              std::uint8_t* lp = live + static_cast<std::size_t>(x) * 4;
              std::uint8_t* kp = keep ? keep + static_cast<std::size_t>(x) * 4 : nullptr;
              if (sx < 0 || sy < 0 || sx >= ctx.width || sy >= ctx.height) {
                // Off the edge is not black: there is simply no echo there, so
                // the live picture stands alone and the frame keeps its border.
                if (kp) {
                  kp[0] = lp[0]; kp[1] = lp[1]; kp[2] = lp[2]; kp[3] = 255;
                }
                continue;
              }
              const std::uint8_t* pp =
                previous + (static_cast<std::size_t>(sy) * ctx.width + sx) * 4;
              for (int c = 0; c < 3; ++c) {
                // LIGHTEN, not add. This is the whole safety argument, and it
                // was arrived at by watching the additive version turn a colour
                // bar to white in twenty frames -- a third of a second, on
                // stage. Adding has a fixed point at L/(1-echo), several times
                // the input and therefore clipped white; taking the brighter of
                // the two has its fixed point at L, so the picture can never
                // come out brighter than the picture went in. The echo is
                // strictly dimmer each pass, so trails decay to nothing and the
                // loop is bounded however long it runs.
                const std::uint8_t echoed = echoLut[c][pp[c]];
                const std::uint8_t v = echoed > lp[c] ? echoed : lp[c];
                lp[c] = v;
                if (kp) kp[c] = v;
              }
              lp[3] = 255;
              if (kp) kp[3] = 255;
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

inline bool cueEffectStackAnimates(const std::vector<CueEffect>& stack) {
  for (const CueEffect& fx : stack) {
    if (!fx.bypassed && cueEffectKindAnimates(fx.kind)) {
      return true;
    }
  }
  return false;
}

// Is anything in this stack moving on its own?
//
// Used to decide whether a STILL has to re-render every frame, the same way
// cueEffectStackAnimates does for the effects that advance with the frame
// index: an LFO on a still with no LFO-aware gate would set the parameter once
// and then sit there.
inline bool cueEffectStackHasLfo(const std::vector<CueEffect>& stack) {
  for (const CueEffect& fx : stack) {
    if (fx.bypassed) continue;
    for (const ParamLfo& lfo : fx.lfo) {
      if (lfo.on) return true;
    }
  }
  return false;
}

// The stack as it stands THIS FRAME, with every armed LFO evaluated.
//
// Done here rather than inside the effects because the stack is a pure function
// of its inputs and must stay one: it is dumped headlessly, benched, run twice,
// and applied by both the output and the preview. An effect that read a clock
// itself could not be any of those things. So the caller asks for a modulated
// copy and everything downstream is unchanged.
//
// Returns false when nothing is modulated, so the ordinary case does not pay
// for a copy of the stack every frame.
inline bool modulateCueEffectStack(const std::vector<CueEffect>& stack,
                                   double seconds, double beats01,
                                   std::vector<CueEffect>& out) {
  if (!cueEffectStackHasLfo(stack)) {
    return false;
  }
  out = stack;
  for (CueEffect& fx : out) {
    if (fx.bypassed) continue;
    float* slots[5] = {&fx.paramA, &fx.paramB, &fx.paramC, &fx.paramD, &fx.amount};
    for (int i = 0; i < 5; ++i) {
      *slots[i] = lfoApply(fx.lfo[i], *slots[i], seconds, beats01);
    }
  }
  return true;
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
