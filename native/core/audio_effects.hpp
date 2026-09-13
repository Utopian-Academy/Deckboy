#pragma once

// ── AUDIO EFFECTS ───────────────────────────────────────────────────────────
//
// The audio half of what cue_effects.hpp is for pictures: a per-cue STACK of
// small, honest processors, evaluated in order, each with an amount and up to
// four shaping parameters. Same shape on purpose -- the same serialisation
// trick (one field), the same bypass-is-not-amount-zero rule, the same
// neutral-parameter contract -- so an operator who has used one already knows
// the other.
//
// SDL-free, like its sibling, so it can be unit-tested and benched without a
// window.
//
// WHERE THIS RUNS. MediaEngine::applyGainAndQueueAudio already builds a float
// scratch of the gained, panned, mono-folded signal before the peak limiter
// quantises it. That is exactly the right place: after the operator's gain
// decisions, before the safety net. The limiter stays last and stays
// non-negotiable -- an effect that adds 6dB must not be able to clip the
// output, and the one thing between the effects and the device should be the
// thing whose whole job is stopping that.
//
// SAMPLE RATE is fixed at 48k through this engine (see the audio thread), so
// the coefficients below take it as a constant rather than threading it
// through every call. If that ever stops being true this is the file that has
// to learn about it.
//
// EVERY EFFECT IS STATEFUL in a way picture effects mostly are not: a filter
// remembers the last sample, a delay remembers the last second. State lives in
// AudioEffectState, which the engine owns and clears when a cue is loaded --
// otherwise the tail of the last cue arrives on top of the next one, which on
// a show is worse than no effect at all.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace deckboy::audiofx {

constexpr double kSampleRate = 48000.0;

// ── WHAT THERE IS ───────────────────────────────────────────────────────────
//
// Deliberately a short list, and deliberately the useful end of one. A live
// events deck needs to fix a room and shape a voice; it does not need eleven
// flavours of chorus. Anything added here has to earn its place by being
// something an operator reaches for during a show.
enum class AudioEffectKind : int {
  None = 0,
  HighPass,     // rumble, handling noise, air conditioning
  LowPass,      // take the top off a harsh source
  Tilt,         // one knob: darker one way, brighter the other
  Compressor,   // even out a speaker who moves around the mic
  Gate,         // shut the mic between sentences
  Delay,        // slap-back to a full echo
  Reverb,       // put a dry source in the room
  Width,        // narrow to mono, or widen
  Binaural,     // place the source around the listener's head
  // The end marker, so the inspector's picker is built FROM this list rather
  // than from a second copy of it that can fall behind -- which is exactly how
  // four cue kinds ended up missing from cueKindToken.
  Count,
};

inline const char* audioEffectLabel(AudioEffectKind kind) {
  switch (kind) {
    case AudioEffectKind::HighPass:   return "High pass";
    case AudioEffectKind::LowPass:    return "Low pass";
    case AudioEffectKind::Tilt:       return "Tilt EQ";
    case AudioEffectKind::Compressor: return "Compressor";
    case AudioEffectKind::Gate:       return "Gate";
    case AudioEffectKind::Delay:      return "Delay";
    case AudioEffectKind::Reverb:     return "Reverb";
    case AudioEffectKind::Width:      return "Width";
    case AudioEffectKind::Binaural:   return "Binaural";
    case AudioEffectKind::None:
    case AudioEffectKind::Count:      break;
  }
  return "None";
}

// The token is what goes in the show file. Never change one: it would silently
// turn an effect in an existing show into a different effect, or into nothing.
inline const char* audioEffectToken(AudioEffectKind kind) {
  switch (kind) {
    case AudioEffectKind::HighPass:   return "hpf";
    case AudioEffectKind::LowPass:    return "lpf";
    case AudioEffectKind::Tilt:       return "tilt";
    case AudioEffectKind::Compressor: return "comp";
    case AudioEffectKind::Gate:       return "gate";
    case AudioEffectKind::Delay:      return "delay";
    case AudioEffectKind::Reverb:     return "reverb";
    case AudioEffectKind::Width:      return "width";
    case AudioEffectKind::Binaural:   return "binaural";
    case AudioEffectKind::None:
    case AudioEffectKind::Count:      break;
  }
  return "none";
}

inline AudioEffectKind audioEffectKindFromToken(const std::string& token) {
  if (token == "hpf")      return AudioEffectKind::HighPass;
  if (token == "lpf")      return AudioEffectKind::LowPass;
  if (token == "tilt")     return AudioEffectKind::Tilt;
  if (token == "comp")     return AudioEffectKind::Compressor;
  if (token == "gate")     return AudioEffectKind::Gate;
  if (token == "delay")    return AudioEffectKind::Delay;
  if (token == "reverb")   return AudioEffectKind::Reverb;
  if (token == "width")    return AudioEffectKind::Width;
  if (token == "binaural") return AudioEffectKind::Binaural;
  return AudioEffectKind::None;
}

// What each parameter slot does, per effect. An unnamed slot draws no row in
// the inspector -- the same contract the picture effects use, so an effect
// never shows a control it ignores.
inline const char* audioEffectParamLabel(AudioEffectKind kind, int slot) {
  switch (kind) {
    case AudioEffectKind::HighPass:
    case AudioEffectKind::LowPass:
      return slot == 0 ? "frequency" : (slot == 1 ? "resonance" : nullptr);
    case AudioEffectKind::Tilt:
      return slot == 0 ? "tilt" : (slot == 1 ? "pivot" : nullptr);
    case AudioEffectKind::Compressor:
      return slot == 0 ? "threshold"
           : slot == 1 ? "ratio"
           : slot == 2 ? "attack"
           : slot == 3 ? "release" : nullptr;
    case AudioEffectKind::Gate:
      return slot == 0 ? "threshold" : (slot == 1 ? "release" : nullptr);
    case AudioEffectKind::Delay:
      return slot == 0 ? "time"
           : slot == 1 ? "feedback"
           : slot == 2 ? "ping-pong" : nullptr;
    case AudioEffectKind::Reverb:
      return slot == 0 ? "size" : (slot == 1 ? "damping" : nullptr);
    case AudioEffectKind::Width:
      return slot == 0 ? "width" : nullptr;
    case AudioEffectKind::Binaural:
      return slot == 0 ? "azimuth" : (slot == 1 ? "distance" : nullptr);
    case AudioEffectKind::None:
    case AudioEffectKind::Count:
      break;
  }
  return nullptr;
}

// WHAT THE KNOB DOES, in the operator's terms rather than the DSP's. Every
// named slot gets one: a row whose label is "paramB" and whose tip is empty is
// a control nobody can use during a show.
inline const char* audioEffectParamTip(AudioEffectKind kind, int slot) {
  switch (kind) {
    case AudioEffectKind::HighPass:
      return slot == 0 ? "Where the cut starts, 20Hz to 2kHz. Low takes out "
                         "rumble and handling noise; high thins a voice on "
                         "purpose."
           : slot == 1 ? "Lift right at the corner. A little adds bite; a lot "
                         "rings."
                       : nullptr;
    case AudioEffectKind::LowPass:
      return slot == 0 ? "Where the top ends, 200Hz to 20kHz. Pull it down to "
                         "take the harshness off a bright source."
           : slot == 1 ? "Lift right at the corner. A little adds bite; a lot "
                         "rings."
                       : nullptr;
    case AudioEffectKind::Tilt:
      return slot == 0 ? "One knob for the whole balance: below the middle is "
                         "darker, above it is brighter."
           : slot == 1 ? "The frequency it pivots around -- what counts as "
                         "'low' and 'high' for the tilt."
                       : nullptr;
    case AudioEffectKind::Compressor:
      return slot == 0 ? "The level it starts working at. Lower catches more "
                         "of the performance."
           : slot == 1 ? "How hard it holds once it is working. Gentle at the "
                         "bottom, a limiter at the top."
           : slot == 2 ? "How fast it grabs. Quick catches consonants; slow "
                         "lets them through and only rides the body."
           : slot == 3 ? "How fast it lets go. Too quick breathes, too slow "
                         "ducks the next line."
                       : nullptr;
    case AudioEffectKind::Gate:
      return slot == 0 ? "The level below which the source is closed. Set it "
                         "under the quietest thing you want to hear."
           : slot == 1 ? "How fast it closes. Slow enough not to chop the "
                         "ends off words."
                       : nullptr;
    case AudioEffectKind::Delay:
      return slot == 0 ? "How long until the repeat, up to a second. Short is "
                         "slap-back; long is a real echo."
           : slot == 1 ? "How much comes back round. High enough and it runs "
                         "away, which is sometimes the point."
           : slot == 2 ? "Send the repeats across the stereo image instead of "
                         "straight back where they started."
                       : nullptr;
    case AudioEffectKind::Reverb:
      return slot == 0 ? "How big the room is. Small is a booth, large is a "
                         "hall."
           : slot == 1 ? "How quickly the room eats the top end. More damping "
                         "is a softer, more furnished space."
                       : nullptr;
    case AudioEffectKind::Width:
      return slot == 0 ? "Below the middle narrows toward mono -- which is "
                         "what a single PA cluster is. Above it widens."
                       : nullptr;
    case AudioEffectKind::Binaural:
      return slot == 0 ? "Where the source sits around the listener, all the "
                         "way round. For headphones -- it is a timing and "
                         "shading trick, not a pan."
           : slot == 1 ? "How far away. Distance dulls the top and softens "
                         "the difference between the ears."
                       : nullptr;
    case AudioEffectKind::None:
    case AudioEffectKind::Count:
      break;
  }
  return nullptr;
}

struct AudioEffect {
  AudioEffectKind kind = AudioEffectKind::None;
  // 0 = inactive. For the shaping effects this is a dry/wet mix; for the
  // dynamics it scales how much gain reduction is applied; for the delay and
  // the reverb it is a SEND, added on top of a source that stays where it is.
  // Three behaviours, one meaning: turning it down is always "less of this"
  // and never "something different", which is the only property that lets one
  // control serve nine effects.
  float amount = 1.0f;
  // THE NEUTRAL VALUES ARE LOAD-BEARING, exactly as in cue_effects.hpp: a show
  // saved before a parameter existed carries 0.5 / 0 / 0 / 0, so every
  // parameter is defined so those reproduce what the effect did without it.
  float paramA = 0.5f;
  float paramB = 0.0f;
  float paramC = 0.0f;
  float paramD = 0.0f;
  // Bypass is not amount 0: turning an effect down loses the setting you spent
  // time on, bypass takes it out of the chain and gives it back.
  bool bypassed = false;
};


// ── WHAT AN EFFECT ARRIVES SET TO ───────────────────────────────────────────
//
// NOT the struct's defaults, and the difference matters.
//
// The struct's 0.5 / 0 / 0 / 0 exist for BACKWARD COMPATIBILITY: a show saved
// before a parameter existed carries those values, so every parameter is
// defined so they reproduce what the effect did without it. That rule is about
// old shows and it stays.
//
// This is about a NEW effect, where there is no old show to be faithful to and
// the only thing that matters is what happens when somebody adds one during a
// show. Those two wants are opposites for half of these: a compressor's
// backward-compatible ratio is 1:1, which is a compressor that does not
// compress, and an operator who adds one and hears no difference has been
// handed a control that does nothing. So an effect ARRIVES set to something
// worth hearing, and a saved show still loads exactly as it was written.
inline AudioEffect audioEffectDefaults(AudioEffectKind kind) {
  AudioEffect fx;
  fx.kind = kind;
  switch (kind) {
    case AudioEffectKind::HighPass:
      // ~80Hz: the standard "take the rumble out" setting, and the one a
      // console's little HPF button is wired to.
      fx.paramA = 0.30f; fx.paramB = 0.0f;
      break;
    case AudioEffectKind::LowPass:
      // ~8kHz: audibly softer without sounding broken.
      fx.paramA = 0.75f; fx.paramB = 0.0f;
      break;
    case AudioEffectKind::Tilt:
      // Slightly bright, so the control is visibly doing something and the
      // direction of the knob is obvious on the first move.
      fx.paramA = 0.62f; fx.paramB = 0.5f;
      break;
    case AudioEffectKind::Compressor:
      // -20 dBFS, 4:1, 10ms attack, 150ms release. A vocal setting: it catches
      // a speaker who moves around the mic and is not audible as an effect.
      fx.paramA = 0.67f; fx.paramB = 0.16f; fx.paramC = 0.10f; fx.paramD = 0.13f;
      break;
    case AudioEffectKind::Gate:
      // -45 dBFS, 250ms release: under speech, over room noise.
      fx.paramA = 0.58f; fx.paramB = 0.23f;
      break;
    case AudioEffectKind::Delay:
      // 250ms, a third feeding back, straight rather than ping-pong -- a
      // repeat you can hear as a repeat rather than as a smear, and at a
      // length that fits inside a short cue.
      fx.amount = 0.35f; fx.paramA = 0.12f; fx.paramB = 0.35f; fx.paramC = 0.0f;
      break;
    case AudioEffectKind::Reverb:
      // A medium room with the top taken off it, sent at a level that sits
      // behind the source instead of swallowing it.
      fx.amount = 0.30f; fx.paramA = 0.5f; fx.paramB = 0.4f;
      break;
    case AudioEffectKind::Width:
      // Wider, because narrower is the thing people reach for deliberately and
      // wider is the thing they are exploring when they add this.
      fx.paramA = 0.75f;
      break;
    case AudioEffectKind::Binaural:
      // Slightly to the left at a middle distance: off-centre, so the effect
      // announces itself, and not hard over, which sounds like a fault.
      fx.paramA = 0.30f; fx.paramB = 0.5f;
      break;
    case AudioEffectKind::None:
    case AudioEffectKind::Count:
      break;
  }
  return fx;
}

// ── STATE ───────────────────────────────────────────────────────────────────
//
// One slot per position in the stack, so two delays cannot tread on each
// other. Owned by the engine and CLEARED ON CUE LOAD -- a delay tail from the
// previous cue arriving over the next one is worse on a show than no delay.
struct AudioEffectSlotState {
  // Biquad / one-pole memory, per channel.
  double x1[2] {0.0, 0.0};
  double x2[2] {0.0, 0.0};
  double y1[2] {0.0, 0.0};
  double y2[2] {0.0, 0.0};
  // Dynamics envelope, per channel, as a linear gain.
  double envelope[2] {1.0, 1.0};
  // Delay / reverb line. Sized on first use; a second of stereo at 48k.
  std::vector<double> line;
  std::size_t writeAt = 0;
  // Reverb needs several taps at different lengths out of the one line.
  double combY[2][4] {{0, 0, 0, 0}, {0, 0, 0, 0}};

  void reset() {
    for (int c = 0; c < 2; ++c) {
      x1[c] = x2[c] = y1[c] = y2[c] = 0.0;
      envelope[c] = 1.0;
      for (int i = 0; i < 4; ++i) combY[c][i] = 0.0;
    }
    std::fill(line.begin(), line.end(), 0.0);
    writeAt = 0;
  }
};

struct AudioEffectState {
  std::vector<AudioEffectSlotState> slots;

  void ensure(std::size_t count) {
    if (slots.size() < count) {
      slots.resize(count);
    }
  }
  void clear() {
    for (AudioEffectSlotState& slot : slots) {
      slot.reset();
    }
  }
};

namespace detail {

inline double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

// A parameter's 0-1 mapped onto a frequency the ear reads as even. Linear
// would put almost everything in the top octave, where nobody sets a filter.
inline double logFrequency(double t, double low, double high) {
  t = clamp01(t);
  return low * std::pow(high / low, t);
}

// Transposed direct form II, which is the numerically better-behaved one at
// the low corner frequencies a high-pass actually gets used at.
struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

  double run(AudioEffectSlotState& s, int ch, double x) const {
    const double y = b0 * x + s.x1[ch];
    s.x1[ch] = b1 * x - a1 * y + s.x2[ch];
    s.x2[ch] = b2 * x - a2 * y;
    return y;
  }
};

inline Biquad makeHighPass(double freq, double q) {
  const double w = 2.0 * 3.14159265358979323846 * freq / kSampleRate;
  const double cosw = std::cos(w), sinw = std::sin(w);
  const double alpha = sinw / (2.0 * std::max(0.1, q));
  const double a0 = 1.0 + alpha;
  Biquad f;
  f.b0 = ((1.0 + cosw) / 2.0) / a0;
  f.b1 = (-(1.0 + cosw)) / a0;
  f.b2 = f.b0;
  f.a1 = (-2.0 * cosw) / a0;
  f.a2 = (1.0 - alpha) / a0;
  return f;
}

inline Biquad makeLowPass(double freq, double q) {
  const double w = 2.0 * 3.14159265358979323846 * freq / kSampleRate;
  const double cosw = std::cos(w), sinw = std::sin(w);
  const double alpha = sinw / (2.0 * std::max(0.1, q));
  const double a0 = 1.0 + alpha;
  Biquad f;
  f.b0 = ((1.0 - cosw) / 2.0) / a0;
  f.b1 = (1.0 - cosw) / a0;
  f.b2 = f.b0;
  f.a1 = (-2.0 * cosw) / a0;
  f.a2 = (1.0 - alpha) / a0;
  return f;
}

inline Biquad makeLowShelf(double freq, double gainDb) {
  const double A = std::pow(10.0, gainDb / 40.0);
  const double w = 2.0 * 3.14159265358979323846 * freq / kSampleRate;
  const double cosw = std::cos(w), sinw = std::sin(w);
  const double alpha = sinw / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / 0.9 - 1.0) + 2.0);
  const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;
  const double a0 = (A + 1.0) + (A - 1.0) * cosw + twoSqrtAalpha;
  Biquad f;
  f.b0 = (A * ((A + 1.0) - (A - 1.0) * cosw + twoSqrtAalpha)) / a0;
  f.b1 = (2.0 * A * ((A - 1.0) - (A + 1.0) * cosw)) / a0;
  f.b2 = (A * ((A + 1.0) - (A - 1.0) * cosw - twoSqrtAalpha)) / a0;
  f.a1 = (-2.0 * ((A - 1.0) + (A + 1.0) * cosw)) / a0;
  f.a2 = ((A + 1.0) + (A - 1.0) * cosw - twoSqrtAalpha) / a0;
  return f;
}

inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }

// One-pole smoothing coefficient for a time constant in milliseconds.
inline double timeCoefficient(double ms) {
  return std::exp(-1.0 / (std::max(0.1, ms) * 0.001 * kSampleRate));
}

}  // namespace detail

// ── THE CHAIN ───────────────────────────────────────────────────────────────
//
// `samples` is interleaved stereo, one double per sample, as the engine's
// limiter scratch already is. Processed in place.
//
// Effects run in the order the operator put them in, which is the whole point
// of a stack: a gate before a compressor is a different sound from a
// compressor before a gate, and both are things people want.
inline void applyAudioEffectStack(std::vector<double>& samples,
                                  const std::vector<AudioEffect>& stack,
                                  AudioEffectState& state) {
  if (samples.empty() || stack.empty()) {
    return;
  }
  const std::size_t frames = samples.size() / 2;
  if (frames == 0) {
    return;
  }
  state.ensure(stack.size());

  for (std::size_t index = 0; index < stack.size(); ++index) {
    const AudioEffect& fx = stack[index];
    if (fx.bypassed || fx.kind == AudioEffectKind::None || fx.amount <= 0.0f) {
      continue;
    }
    AudioEffectSlotState& slot = state.slots[index];
    const double wet = detail::clamp01(fx.amount);
    const double dry = 1.0 - wet;

    switch (fx.kind) {
      case AudioEffectKind::HighPass:
      case AudioEffectKind::LowPass: {
        // 20Hz-2kHz for the high pass, 200Hz-20kHz for the low: the ranges
        // people actually set them over. A full 20-20k on one knob puts the
        // useful part of a high pass in the first eighth of its travel.
        const bool high = fx.kind == AudioEffectKind::HighPass;
        const double freq = high ? detail::logFrequency(fx.paramA, 20.0, 2000.0)
                                 : detail::logFrequency(fx.paramA, 200.0, 20000.0);
        const double q = 0.707 + detail::clamp01(fx.paramB) * 4.0;
        const detail::Biquad f = high ? detail::makeHighPass(freq, q)
                                      : detail::makeLowPass(freq, q);
        for (std::size_t i = 0; i < frames; ++i) {
          for (int c = 0; c < 2; ++c) {
            double& s = samples[i * 2 + c];
            s = dry * s + wet * f.run(slot, c, s);
          }
        }
        break;
      }
      case AudioEffectKind::Tilt: {
        // ONE KNOB. 0.5 is flat, which is the neutral-parameter rule: a show
        // that had this before the pivot control existed sounds the same.
        const double tiltDb = (detail::clamp01(fx.paramA) - 0.5) * 24.0;
        const double pivot = detail::logFrequency(fx.paramB > 0.0 ? fx.paramB : 0.5,
                                                  200.0, 4000.0);
        const detail::Biquad f = detail::makeLowShelf(pivot, -tiltDb);
        const double makeUp = detail::dbToGain(tiltDb * 0.5);
        for (std::size_t i = 0; i < frames; ++i) {
          for (int c = 0; c < 2; ++c) {
            double& s = samples[i * 2 + c];
            s = dry * s + wet * (f.run(slot, c, s) * makeUp);
          }
        }
        break;
      }
      case AudioEffectKind::Compressor: {
        // Threshold in dBFS, ratio 1:1 to 20:1. Feed-forward, peak-detecting,
        // with the gain smoothed rather than the signal -- which is what keeps
        // it from sounding like a tremolo on percussive material.
        const double thresholdDb = -60.0 + detail::clamp01(fx.paramA) * 60.0;
        const double threshold = detail::dbToGain(thresholdDb) * 32768.0;
        const double ratio = 1.0 + detail::clamp01(fx.paramB) * 19.0;
        const double attack = detail::timeCoefficient(
          0.5 + detail::clamp01(fx.paramC) * 99.5);
        const double release = detail::timeCoefficient(
          20.0 + detail::clamp01(fx.paramD) * 980.0);
        for (std::size_t i = 0; i < frames; ++i) {
          // Stereo-linked: the loudest channel decides, or the image shifts
          // every time something hits one side.
          const double peak = std::max(std::fabs(samples[i * 2]),
                                       std::fabs(samples[i * 2 + 1]));
          double target = 1.0;
          if (peak > threshold && peak > 0.0) {
            const double over = peak / threshold;
            target = std::pow(over, 1.0 / ratio - 1.0);
          }
          const double coeff = target < slot.envelope[0] ? attack : release;
          slot.envelope[0] = target + (slot.envelope[0] - target) * coeff;
          const double g = 1.0 + (slot.envelope[0] - 1.0) * wet;
          samples[i * 2] *= g;
          samples[i * 2 + 1] *= g;
        }
        break;
      }
      case AudioEffectKind::Gate: {
        const double thresholdDb = -80.0 + detail::clamp01(fx.paramA) * 60.0;
        const double threshold = detail::dbToGain(thresholdDb) * 32768.0;
        const double release = detail::timeCoefficient(
          20.0 + detail::clamp01(fx.paramB) * 980.0);
        for (std::size_t i = 0; i < frames; ++i) {
          const double peak = std::max(std::fabs(samples[i * 2]),
                                       std::fabs(samples[i * 2 + 1]));
          const double target = peak > threshold ? 1.0 : 0.0;
          // Opens fast, closes slowly: the other way round chops the front off
          // every word.
          slot.envelope[0] = target > slot.envelope[0]
                               ? target
                               : target + (slot.envelope[0] - target) * release;
          const double g = 1.0 + (slot.envelope[0] - 1.0) * wet;
          samples[i * 2] *= g;
          samples[i * 2 + 1] *= g;
        }
        break;
      }
      case AudioEffectKind::Delay: {
        const std::size_t maxDelay = static_cast<std::size_t>(kSampleRate * 2.0);
        if (slot.line.size() < maxDelay * 2) {
          slot.line.assign(maxDelay * 2, 0.0);
          slot.writeAt = 0;
        }
        const double seconds = 0.01 + detail::clamp01(fx.paramA) * 1.99;
        const std::size_t delay =
          std::clamp<std::size_t>(static_cast<std::size_t>(seconds * kSampleRate),
                                  1, maxDelay - 1);
        const double feedback = detail::clamp01(fx.paramB) * 0.95;
        const double pingPong = detail::clamp01(fx.paramC);
        for (std::size_t i = 0; i < frames; ++i) {
          const std::size_t readAt = (slot.writeAt + maxDelay - delay) % maxDelay;
          const double dl = slot.line[readAt * 2];
          const double dr = slot.line[readAt * 2 + 1];
          // Ping-pong crosses the feedback over, so repeats alternate sides.
          const double inL = samples[i * 2] + (dl * (1.0 - pingPong) + dr * pingPong) * feedback;
          const double inR = samples[i * 2 + 1] + (dr * (1.0 - pingPong) + dl * pingPong) * feedback;
          slot.line[slot.writeAt * 2] = inL;
          slot.line[slot.writeAt * 2 + 1] = inR;
          slot.writeAt = (slot.writeAt + 1) % maxDelay;
          // A SEND, NOT A MIX. Amount is how much delay you hear ON TOP of
          // the source, the way it works on every console -- which is what an
          // operator means by turning the delay up. Treated as a dry/wet it
          // meant amount 100% removed the speaker entirely and left only the
          // echo, and with a one-second time and a half-second cue that is a
          // cue which plays silence.
          samples[i * 2] += wet * dl;
          samples[i * 2 + 1] += wet * dr;
        }
        break;
      }
      case AudioEffectKind::Reverb: {
        // Four combs into the same line at mutually prime-ish lengths, which
        // is the cheap Schroeder answer and sounds like a room rather than a
        // flutter. Not a convolution, and not pretending to be.
        const std::size_t maxDelay = static_cast<std::size_t>(kSampleRate * 0.2);
        if (slot.line.size() < maxDelay * 2) {
          slot.line.assign(maxDelay * 2, 0.0);
          slot.writeAt = 0;
        }
        const double size = 0.25 + detail::clamp01(fx.paramA) * 0.75;
        const double damping = detail::clamp01(fx.paramB) * 0.6;
        const std::size_t taps[4] = {
          static_cast<std::size_t>(maxDelay * 0.31 * size),
          static_cast<std::size_t>(maxDelay * 0.53 * size),
          static_cast<std::size_t>(maxDelay * 0.71 * size),
          static_cast<std::size_t>(maxDelay * 0.97 * size),
        };
        for (std::size_t i = 0; i < frames; ++i) {
          for (int c = 0; c < 2; ++c) {
            double sum = 0.0;
            for (int t = 0; t < 4; ++t) {
              const std::size_t d = std::clamp<std::size_t>(taps[t], 1, maxDelay - 1);
              const std::size_t readAt = (slot.writeAt + maxDelay - d) % maxDelay;
              const double v = slot.line[readAt * 2 + c];
              // A little low pass in the feedback is what makes a room sound
              // like a room instead of a pipe.
              slot.combY[c][t] = v * (1.0 - damping) + slot.combY[c][t] * damping;
              sum += slot.combY[c][t];
            }
            sum *= 0.25;
            slot.line[slot.writeAt * 2 + c] = samples[i * 2 + c] + sum * 0.7;
            // A send, for the same reason the delay is one: putting a source
            // in a room does not mean removing the source.
            samples[i * 2 + c] += wet * sum;
          }
          slot.writeAt = (slot.writeAt + 1) % maxDelay;
        }
        break;
      }
      case AudioEffectKind::Width: {
        // Mid/side. 0 is mono, 0.5 is untouched, 1 is double width -- so the
        // neutral parameter really is neutral.
        const double width = detail::clamp01(fx.paramA) * 2.0;
        for (std::size_t i = 0; i < frames; ++i) {
          const double l = samples[i * 2], r = samples[i * 2 + 1];
          const double mid = (l + r) * 0.5;
          const double side = (l - r) * 0.5 * width;
          samples[i * 2] = dry * l + wet * (mid + side);
          samples[i * 2 + 1] = dry * r + wet * (mid - side);
        }
        break;
      }
      case AudioEffectKind::Binaural: {
        // PLACING A SOURCE AROUND THE HEAD, the cheap-but-real way: the two
        // cues the ear actually uses are the time difference between the ears
        // (about 0.7ms at full offset) and the shadow the head casts over the
        // far ear at high frequencies. Both are here; a measured HRTF set is
        // not, and this does not pretend otherwise.
        const std::size_t maxDelay = 128;
        if (slot.line.size() < maxDelay * 2) {
          slot.line.assign(maxDelay * 2, 0.0);
          slot.writeAt = 0;
        }
        const double azimuth = (detail::clamp01(fx.paramA) - 0.5) * 2.0;  // -1 left, +1 right
        const double distance = 0.2 + detail::clamp01(fx.paramB) * 0.8;
        const std::size_t itd = static_cast<std::size_t>(
          std::fabs(azimuth) * 0.0007 * kSampleRate);
        // The far ear is shadowed: a gentle low pass and a little level off it.
        const detail::Biquad shadow =
          detail::makeLowPass(detail::logFrequency(1.0 - std::fabs(azimuth) * 0.7,
                                                   1200.0, 18000.0), 0.707);
        const double farGain = 1.0 - std::fabs(azimuth) * 0.3;
        for (std::size_t i = 0; i < frames; ++i) {
          const double mono = (samples[i * 2] + samples[i * 2 + 1]) * 0.5;
          slot.line[slot.writeAt * 2] = mono;
          const std::size_t readAt = (slot.writeAt + maxDelay - std::max<std::size_t>(itd, 1)) % maxDelay;
          const double late = slot.line[readAt * 2];
          slot.writeAt = (slot.writeAt + 1) % maxDelay;
          // Whichever ear is further away gets the delayed, shadowed copy.
          double nearSig = mono;
          double farSig = shadow.run(slot, 0, late) * farGain;
          double outL = azimuth <= 0.0 ? nearSig : farSig;
          double outR = azimuth <= 0.0 ? farSig : nearSig;
          // Distance: further away is quieter and duller, which is most of what
          // distance sounds like indoors.
          outL *= distance;
          outR *= distance;
          samples[i * 2] = dry * samples[i * 2] + wet * outL;
          samples[i * 2 + 1] = dry * samples[i * 2 + 1] + wet * outR;
        }
        break;
      }
      case AudioEffectKind::None:
      case AudioEffectKind::Count:
        break;
    }
  }
}

// ── SERIALISATION ───────────────────────────────────────────────────────────
//
// ONE FIELD, "token:amount:a:b:c:d:bypass|...", for the same reason the
// picture stack uses one: a variable number of columns would shift every
// positional index after it, which this project file already carries scars
// from.
inline std::string serializeAudioEffects(const std::vector<AudioEffect>& stack) {
  std::string out;
  char buf[96];
  for (const AudioEffect& fx : stack) {
    if (fx.kind == AudioEffectKind::None) {
      continue;
    }
    std::snprintf(buf, sizeof(buf), "%s:%.4g:%.4g:%.4g:%.4g:%.4g:%d",
                  audioEffectToken(fx.kind), fx.amount, fx.paramA, fx.paramB,
                  fx.paramC, fx.paramD, fx.bypassed ? 1 : 0);
    if (!out.empty()) out += '|';
    out += buf;
  }
  return out;
}

inline std::vector<AudioEffect> parseAudioEffects(const std::string& text) {
  std::vector<AudioEffect> stack;
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t bar = text.find('|', at);
    const std::string chunk =
      text.substr(at, bar == std::string::npos ? std::string::npos : bar - at);
    at = (bar == std::string::npos) ? text.size() : bar + 1;
    if (chunk.empty()) continue;
    std::vector<std::string> parts;
    std::size_t from = 0;
    while (from <= chunk.size()) {
      const std::size_t colon = chunk.find(':', from);
      parts.push_back(chunk.substr(from, colon == std::string::npos
                                           ? std::string::npos : colon - from));
      if (colon == std::string::npos) break;
      from = colon + 1;
    }
    if (parts.empty()) continue;
    AudioEffect fx;
    fx.kind = audioEffectKindFromToken(parts[0]);
    if (fx.kind == AudioEffectKind::None) continue;
    auto num = [&](std::size_t i, float fallback) {
      if (i >= parts.size() || parts[i].empty()) return fallback;
      try { return std::stof(parts[i]); } catch (...) { return fallback; }
    };
    fx.amount = std::clamp(num(1, 1.0f), 0.0f, 1.0f);
    fx.paramA = std::clamp(num(2, 0.5f), 0.0f, 1.0f);
    fx.paramB = std::clamp(num(3, 0.0f), 0.0f, 1.0f);
    fx.paramC = std::clamp(num(4, 0.0f), 0.0f, 1.0f);
    fx.paramD = std::clamp(num(5, 0.0f), 0.0f, 1.0f);
    fx.bypassed = parts.size() > 6 && parts[6] == "1";
    stack.push_back(fx);
  }
  return stack;
}

}  // namespace deckboy::audiofx
