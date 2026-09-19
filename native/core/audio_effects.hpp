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

// ── WHAT THE DECK KNOWS THAT A PLUGIN CANNOT ────────────────────────────────
//
// Every audio effect ever written receives a buffer of samples and nothing
// else. That is not a limitation anybody chose; it is what a plugin IS. A
// compressor inside a mixing desk cannot know that the thing it is compressing
// is a drone shot, that the shot is a quarter of the way across the screen,
// that it has four seconds left, or that the operator has just held it.
//
// Deckboy holds the picture and the sound in the same object. So this is the
// context the engine publishes alongside the samples, and the five effects
// below it are the ones that could not be written anywhere else.
//
// Every field has a neutral value that makes the effect behave as though the
// information were absent -- a cue with no video, no geometry and no known
// duration still passes through all five without a fault, it simply has
// nothing for them to follow.
struct AudioEffectContext {
  // ── The picture, as the frame the audio is playing under ──
  float luma = 0.5f;          // 0-1, the frame's average brightness
  float motion = 0.0f;        // 0-1, how much of it changed since the last one
  bool hasPicture = false;    // false for an audio-only cue: luma/motion are
                              // then defaults and must not be followed

  // ── Where the picture IS, in the output raster ──
  float centerX = 0.5f;       // 0 = left edge of the output, 1 = right edge
  float centerY = 0.5f;       // 0 = top, 1 = bottom
  float coverage = 1.0f;      // fraction of the output's area it fills

  // ── When we are ──
  double position = 0.0;      // seconds into the cue
  double duration = 0.0;      // its length; 0 = open-ended or unknown
  double framePeriod = 0.0;   // seconds per video frame; 0 = no video clock
  bool held = false;          // the operator is holding this cue

  // ── The picture AFTER its effects, as the audience sees it ──
  //
  // luma/motion above are measured on the DECODED frame, before any picture
  // effect has touched it. These are measured on the finished composite, which
  // is what closes the loop Ouroboros is built on: the bent sound moves a picture
  // effect (through an Audio LFO), the picture effect changes what is on
  // screen, and what is on screen is read back here. Neutral when nothing has
  // published one -- no output armed, or a cue with no picture.
  float postLuma = 0.5f;
  float postMotion = 0.0f;
  bool hasPostPicture = false;

  // ── Who owns the third-party plugins ──
  // Null everywhere this header is tested, benched or dumped, which is why a
  // Plugin slot with no host passes the audio through untouched rather than
  // being an error. Declared below; see AudioEffectHost.
  struct AudioEffectHost* host = nullptr;
};

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
  // ── THE ONES THAT NEED THE DECK ──
  // Appended, never inserted: the token is what goes in the show file, but the
  // ENUM VALUE is what a switch falls through, and reordering this list would
  // silently turn one effect into another in code that indexes it.
  Picture,      // the cue's own video drives a filter
  Placement,    // where the picture sits on screen is where the sound is
  Seam,         // the approaching end of the cue resolves the tail
  FrameLock,    // granular stutter quantised to the VIDEO frame period
  Suspend,      // a held cue keeps its room tone instead of stopping dead
  // ── THE BENDS ──
  // Circuit bending done to the numbers rather than to a board: what a sound
  // becomes when the machinery carrying it is shorted, starved or misread.
  // The style comes from hardware video synths -- a short closes for a
  // moment on a timer or a trigger, does something violent, and the clean
  // signal comes straight back when it opens -- and "wrap, don't clip", because
  // overflow is the sound. The last four read the deck.
  Crush,        // fewer bits, fewer samples, rotting bits, overflow that wraps
  Word,         // sample words misread: rotated, byte-swapped, offset, mu-law
  Skip,         // a CD skipping: jumps back and repeats, spliced clean
  Rail,         // a filter with corrupted coefficients, screaming but bounded
  Resolution,   // the picture's size and brightness are the sound's resolution
  Scrub,        // brightness is TIME: dark reaches back, a cut throws the head
  Short,        // every cut closes a short across a small virtual circuit
  Ouroboros,    // the loop: the finished picture drives the bend that drives it
  // ── SOMEBODY ELSE'S EFFECT ──
  // A hole in the chain that a third-party plugin fills. Everything above is
  // ours and runs anywhere Deckboy runs; this one is a slot the operator's own
  // library plugs into, and it is the only kind whose behaviour this file
  // cannot describe. See AudioEffectHost below.
  Plugin,
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
    case AudioEffectKind::Picture:    return "Picture";
    case AudioEffectKind::Placement:  return "Placement";
    case AudioEffectKind::Seam:       return "Seam";
    case AudioEffectKind::FrameLock:  return "Frame lock";
    case AudioEffectKind::Suspend:    return "Suspend";
    case AudioEffectKind::Crush:      return "Crush";
    case AudioEffectKind::Word:       return "Word";
    case AudioEffectKind::Skip:       return "Skip";
    case AudioEffectKind::Rail:       return "Rail";
    case AudioEffectKind::Resolution: return "Resolution";
    case AudioEffectKind::Scrub:      return "Scrub";
    case AudioEffectKind::Short:      return "Short";
    case AudioEffectKind::Ouroboros:  return "Ouroboros";
    case AudioEffectKind::Plugin:     return "Plugin";
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
    case AudioEffectKind::Picture:    return "picture";
    case AudioEffectKind::Placement:  return "placement";
    case AudioEffectKind::Seam:       return "seam";
    case AudioEffectKind::FrameLock:  return "framelock";
    case AudioEffectKind::Suspend:    return "suspend";
    case AudioEffectKind::Crush:      return "crush";
    case AudioEffectKind::Word:       return "word";
    case AudioEffectKind::Skip:       return "skip";
    case AudioEffectKind::Rail:       return "rail";
    case AudioEffectKind::Resolution: return "resolution";
    case AudioEffectKind::Scrub:      return "scrub";
    case AudioEffectKind::Short:      return "short";
    case AudioEffectKind::Ouroboros:     return "ouroboros";
    case AudioEffectKind::Plugin:        return "plugin";
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
  if (token == "picture")   return AudioEffectKind::Picture;
  if (token == "placement") return AudioEffectKind::Placement;
  if (token == "seam")      return AudioEffectKind::Seam;
  if (token == "framelock") return AudioEffectKind::FrameLock;
  if (token == "suspend")   return AudioEffectKind::Suspend;
  if (token == "crush")      return AudioEffectKind::Crush;
  if (token == "word")       return AudioEffectKind::Word;
  if (token == "skip")       return AudioEffectKind::Skip;
  if (token == "rail")       return AudioEffectKind::Rail;
  if (token == "resolution") return AudioEffectKind::Resolution;
  if (token == "scrub")      return AudioEffectKind::Scrub;
  if (token == "short")      return AudioEffectKind::Short;
  if (token == "ouroboros")     return AudioEffectKind::Ouroboros;
  if (token == "plugin")        return AudioEffectKind::Plugin;
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
    case AudioEffectKind::Picture:
      return slot == 0 ? "depth"
           : slot == 1 ? "motion"
           : slot == 2 ? "follow"
           : slot == 3 ? "invert" : nullptr;
    case AudioEffectKind::Placement:
      return slot == 0 ? "position" : (slot == 1 ? "size" : nullptr);
    case AudioEffectKind::Seam:
      return slot == 0 ? "length"
           : slot == 1 ? "darken"
           : slot == 2 ? "room" : nullptr;
    case AudioEffectKind::FrameLock:
      return slot == 0 ? "frames"
           : slot == 1 ? "repeats"
           : slot == 2 ? "reverse" : nullptr;
    case AudioEffectKind::Suspend:
      return slot == 0 ? "loop" : (slot == 1 ? "settle" : nullptr);
    case AudioEffectKind::Crush:
      return slot == 0 ? "bits"
           : slot == 1 ? "rate"
           : slot == 2 ? "rot"
           : slot == 3 ? "overflow" : nullptr;
    case AudioEffectKind::Word:
      return slot == 0 ? "depth"
           : slot == 1 ? "self"
           : slot == 2 ? "rate"
           : slot == 3 ? "wire" : nullptr;
    case AudioEffectKind::Skip:
      return slot == 0 ? "jump"
           : slot == 1 ? "repeats"
           : slot == 2 ? "rate"
           : slot == 3 ? "reverse" : nullptr;
    case AudioEffectKind::Rail:
      return slot == 0 ? "cutoff"
           : slot == 1 ? "corrupt"
           : slot == 2 ? "sag"
           : slot == 3 ? "drive" : nullptr;
    case AudioEffectKind::Resolution:
      return slot == 0 ? "size"
           : slot == 1 ? "dark"
           : slot == 2 ? "cuts"
           : slot == 3 ? "floor" : nullptr;
    case AudioEffectKind::Scrub:
      return slot == 0 ? "depth"
           : slot == 1 ? "throw"
           : slot == 2 ? "follow"
           : slot == 3 ? "invert" : nullptr;
    case AudioEffectKind::Short:
      return slot == 0 ? "trigger"
           : slot == 1 ? "hold"
           : slot == 2 ? "damage"
           : slot == 3 ? "wire" : nullptr;
    case AudioEffectKind::Ouroboros:
      return slot == 0 ? "coupling"
           : slot == 1 ? "leak"
           : slot == 2 ? "cut reset"
           : slot == 3 ? "character" : nullptr;
    case AudioEffectKind::Plugin:
      // The plugin's OWN four names replace these wherever the plugin is
      // loaded. These are the fallback for a chain whose plugin is missing --
      // the show still opens, the rows still draw, and the numbers are still
      // the ones that were saved.
      return slot == 0 ? "control 1"
           : slot == 1 ? "control 2"
           : slot == 2 ? "control 3"
           : slot == 3 ? "control 4" : nullptr;
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
    case AudioEffectKind::Picture:
      return slot == 0 ? "How far the picture swings the filter. The cue's own "
                         "brightness opens and closes it, so a cut to black "
                         "takes the top off the sound."
           : slot == 1 ? "How much MOVEMENT opens it further. A still shot "
                         "sits back; a fast one comes forward."
           : slot == 2 ? "How quickly it follows. Fast tracks the grain and "
                         "flickers; slow breathes with the edit."
           : slot == 3 ? "Above halfway, dark opens it instead of closing it."
                       : nullptr;
    case AudioEffectKind::Placement:
      return slot == 0 ? "How much of the picture's position on the output is "
                         "used. A PIP on the left sounds on the left -- and it "
                         "is a placement, not a pan: the far ear gets the "
                         "delay and the head shadow too."
           : slot == 1 ? "How much its SIZE becomes distance. Shrink the "
                         "picture and the sound goes away from you: the top "
                         "comes off, the image narrows, the level drops."
                       : nullptr;
    case AudioEffectKind::Seam:
      return slot == 0 ? "How long before the end it starts. The cue has to "
                         "have a known length -- an open-ended one has no end "
                         "to resolve into."
           : slot == 1 ? "How far the top comes down as the end approaches. "
                         "This is not a fade: the level is left alone."
           : slot == 2 ? "How much room comes up under it, so the sound "
                         "settles into the cut instead of being severed."
                       : nullptr;
    case AudioEffectKind::FrameLock:
      return slot == 0 ? "How long a grain is, in VIDEO FRAMES. On a 23.976 "
                         "clip one frame is 41.7ms, which is not a musical "
                         "value and is exactly the point."
           : slot == 1 ? "How many times a grain repeats before the next is "
                         "captured. One is a hiccup; eight is a lock-up."
           : slot == 2 ? "Above halfway, each grain plays backwards."
                       : nullptr;
    case AudioEffectKind::Suspend:
      return slot == 0 ? "How much of the tail is kept going while the cue is "
                         "held. Long enough to hide the splice in room tone, "
                         "short enough not to smear a rhythm in it."
           : slot == 1 ? "How fast it settles away while held. At zero it "
                         "holds indefinitely, which is what room tone under a "
                         "held title wants."
                       : nullptr;
    case AudioEffectKind::Crush:
      return slot == 0 ? "How many bits are left. Left is clean; right is one "
                         "bit, which is barely sound at all."
           : slot == 1 ? "How often a new sample is taken. Up adds the metallic "
                         "fold-back of an old sampler."
           : slot == 2 ? "How often a high bit rots for a moment. A rotted bit "
                         "is a burst of damage that snaps straight back."
           : slot == 3 ? "Below halfway, loud peaks hit a ceiling. Above it they "
                         "WRAP round to the other side, like an overflowing "
                         "counter -- the bent-circuit sound."
                       : nullptr;
    case AudioEffectKind::Word:
      return slot == 0 ? "How much of each sample the stray wire can reach. Low "
                         "is fizz on the surface; high is the whole word."
           : slot == 1 ? "Mixes each sample with an echo of itself at the bit "
                         "level, not the audio level. Off at zero."
           : slot == 2 ? "How often the wire touches. Each touch is short and "
                         "the clean sound comes back the moment it lets go."
           : slot == 3 ? "Which fault: rotated bits, swapped bytes, offset "
                         "reading, or read as telephone mu-law. Fully left "
                         "picks one at random each time, like a real loose wire."
                       : nullptr;
    case AudioEffectKind::Skip:
      return slot == 0 ? "How far back each skip jumps, from a stutter to half "
                         "a second."
           : slot == 1 ? "How many times a skip repeats before the sound "
                         "catches up again."
           : slot == 2 ? "How often it skips."
           : slot == 3 ? "Above halfway, the repeated piece plays backwards."
                       : nullptr;
    case AudioEffectKind::Rail:
      return slot == 0 ? "Where the filter sits."
           : slot == 1 ? "How often its settings are corrupted. Corrupted far "
                         "enough it oscillates on its own -- loud, but it "
                         "cannot run away."
           : slot == 2 ? "A slow sag in the supply, pulling the filter up and "
                         "down like a dying battery."
           : slot == 3 ? "How hard it is driven into its own limits."
                       : nullptr;
    case AudioEffectKind::Resolution:
      return slot == 0 ? "How much the picture's SIZE costs the sound. Full "
                         "frame is clean; shrink the picture to a corner and "
                         "the sound loses bits and samples with it."
           : slot == 1 ? "How much darkness costs too. A fade to black takes "
                         "the sound down to its bones."
           : slot == 2 ? "A cut in the picture rots bits for one frame."
           : slot == 3 ? "The least resolution it is allowed to reach."
                       : nullptr;
    case AudioEffectKind::Scrub:
      return slot == 0 ? "How far back the sound can be pulled. Dark pictures "
                         "reach further back; bright ones catch up to now, and "
                         "the movement between them bends the pitch."
           : slot == 1 ? "How far a cut throws the playhead."
           : slot == 2 ? "How quickly the playhead follows. Slow is a tape "
                         "machine winding; fast is a scratch."
           : slot == 3 ? "Above halfway, bright reaches back instead of dark."
                       : nullptr;
    case AudioEffectKind::Short:
      return slot == 0 ? "How small a cut sets it off. With no picture it runs "
                         "on a timer instead, and this is how often."
           : slot == 1 ? "How long a short stays closed, in VIDEO FRAMES."
           : slot == 2 ? "How hard the short hits."
           : slot == 3 ? "Where the wire lands. Fully left is a different place "
                         "each time -- the same cut always gets the same one; "
                         "further right picks one and keeps it."
                       : nullptr;
    case AudioEffectKind::Ouroboros:
      return slot == 0 ? "How strongly the finished picture pushes the bend. "
                         "Pair it with a picture effect whose LFO is set to "
                         "Audio and the two drive each other."
           : slot == 1 ? "How quickly the loop relaxes when nothing feeds it."
           : slot == 2 ? "Above halfway, a hard cut in the picture resets the "
                         "loop to clean."
           : slot == 3 ? "Left leans on crushing; right leans on skipping."
                       : nullptr;
    case AudioEffectKind::Plugin:
      // Replaced by the plugin's own parameter names once it is loaded, so
      // these describe the MAPPING rather than any particular knob.
      return "The plugin's first four automatable controls, in its own order. "
             "Everything else it has keeps whatever the plugin was left set to.";
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
  // ── PLUGIN SLOTS ONLY ───────────────────────────────────────────────────
  // Empty for all twenty-two effects above, and that is the point: a plugin
  // is the one kind whose identity is not in its kind. The id is the
  // "<format>:<reference>" spelling platform/audio_plugin.hpp hands out.
  std::string pluginId;
  // The plugin's own saved settings, as the RAW BYTES it handed over (base64
  // only at the show-file boundary, where the format is text). Opaque by
  // design -- a plugin's state is the plugin's business, and the four mapped
  // parameters above are what Deckboy claims to understand.
  std::string pluginState;
};

// ── THE HOLE IN THE CHAIN ───────────────────────────────────────────────────
//
// This file stays SDL-free, dlopen-free and SDK-free, which is what lets it be
// benched and unit-tested without a window -- so it cannot load a plugin and
// does not try. A Plugin slot calls back out to whoever owns the instances.
// The host does the whole slot, dry/wet included, because it already owns the
// de-interleaved scratch a plugin needs and the audio thread must not allocate
// a second copy here.
struct AudioEffectHost {
  virtual ~AudioEffectHost() = default;
  // `samples` is the interleaved-stereo buffer, processed in place. Returning
  // false means nothing is loaded in that slot, and the chain then leaves the
  // audio ALONE: an empty plugin slot is an empty slot, never a silence.
  virtual bool processPluginSlot(std::size_t index, const AudioEffect& fx,
                                 double* samples, std::size_t frames) = 0;
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
    case AudioEffectKind::Picture:
      // Following firmly and smoothly, with movement contributing. Instant
      // following is a zipper and following nothing is the effect switched off.
      fx.paramA = 0.70f; fx.paramB = 0.40f; fx.paramC = 0.45f; fx.paramD = 0.0f;
      break;
    case AudioEffectKind::Placement:
      // Full position, moderate distance. A full-frame cue is centred and
      // full-size, so this is inaudible until the geometry moves -- which is
      // the correct behaviour, not a broken one.
      fx.paramA = 1.0f; fx.paramB = 0.6f;
      break;
    case AudioEffectKind::Seam:
      // Three seconds, a firm darkening and a real room.
      fx.paramA = 0.33f; fx.paramB = 0.7f; fx.paramC = 0.5f;
      break;
    case AudioEffectKind::FrameLock:
      // Two-frame grains repeating three times, forwards. Audible as a lock
      // rather than as a fault, and it is a send-ish amount because a full-wet
      // frame lock is a very strong effect.
      fx.amount = 0.8f; fx.paramA = 0.14f; fx.paramB = 0.29f; fx.paramC = 0.0f;
      break;
    case AudioEffectKind::Suspend:
      // A half-second loop that holds indefinitely. Settling away is the thing
      // you ask for; not stopping dead is the thing you wanted.
      fx.paramA = 0.23f; fx.paramB = 0.0f;
      break;
    // THE BENDS ARRIVE AUDIBLE BUT NOT SAVAGE. Each is set so that adding one
    // during a show is obviously doing something within a second, and nothing
    // about it is loud enough to make anyone reach for the fader.
    case AudioEffectKind::Crush:
      // Six bits, a light sample hold, occasional rot, no wrap.
      fx.paramA = 0.67f; fx.paramB = 0.25f; fx.paramC = 0.25f; fx.paramD = 0.0f;
      break;
    case AudioEffectKind::Word:
      // Half the word reachable, no self-logic, a few touches a second,
      // a different fault each touch.
      fx.paramA = 0.5f; fx.paramB = 0.0f; fx.paramC = 0.35f; fx.paramD = 0.0f;
      break;
    case AudioEffectKind::Skip:
      // Short jumps, three repeats, a skip every second or so, forwards.
      fx.paramA = 0.3f; fx.paramB = 0.3f; fx.paramC = 0.55f; fx.paramD = 0.0f;
      break;
    case AudioEffectKind::Rail:
      // Mid-low cutoff, some corruption, some sag, moderate drive.
      fx.amount = 0.8f; fx.paramA = 0.45f; fx.paramB = 0.35f; fx.paramC = 0.3f; fx.paramD = 0.4f;
      break;
    case AudioEffectKind::Resolution:
      // Size and darkness both count, cuts glitch, floor at four bits. Clean on
      // a bright full-frame picture -- correct, not broken.
      fx.paramA = 0.8f; fx.paramB = 0.5f; fx.paramC = 0.5f; fx.paramD = 0.2f;
      break;
    case AudioEffectKind::Scrub:
      // Up to a second back, cuts throw it, a musical follow speed.
      fx.amount = 0.7f; fx.paramA = 0.5f; fx.paramB = 0.4f; fx.paramC = 0.35f; fx.paramD = 0.0f;
      break;
    case AudioEffectKind::Short:
      // Moderately sensitive, three-frame holds, firm damage, a new wire each time.
      fx.paramA = 0.5f; fx.paramB = 0.1f; fx.paramC = 0.6f; fx.paramD = 0.0f;
      break;
    case AudioEffectKind::Ouroboros:
      // Loop gain below one, a few seconds to relax, cuts reset it, balanced.
      fx.paramA = 0.7f; fx.paramB = 0.3f; fx.paramC = 1.0f; fx.paramD = 0.4f;
      break;
    case AudioEffectKind::Plugin:
      // Fully wet and no plugin yet -- the operator picks one on the next row,
      // and the four parameters are seeded from that plugin's own defaults at
      // that moment, because a plugin's idea of neutral is not 50%.
      fx.amount = 1.0f;
      fx.paramA = fx.paramB = fx.paramC = fx.paramD = 0.5f;
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
  // Picture's smoothed follow value and the filter it last built from it --
  // recomputing a biquad per sample would be honest and slow, and the cutoff
  // cannot move fast enough for once every 64 samples to be audible.
  double followed = 0.5;
  // The filter it last built from that value, as plain coefficients -- the
  // Biquad type is declared below this struct (it takes a reference to one),
  // so the state cannot hold one without an ordering knot that buys nothing.
  double cached[5] {1, 0, 0, 0, 0};
  // Frame lock and suspend read out of the line at their own pace, which is
  // not the write pace -- that is the whole point of both of them.
  std::size_t readAt = 0;
  int grainPass = 0;

  // ── For the bends ──
  // A DC blocker on every output that does arithmetic on sample WORDS: an
  // offset reading or a flipped sign bit is a constant step, which a speaker
  // does not want and the limiter does not remove.
  double dcX[2] {0.0, 0.0};
  double dcY[2] {0.0, 0.0};
  // Loudness compensation, carried between chunks. Bent audio is routinely
  // near full scale whatever went in, so the wet side is scaled back to the
  // dry side's level -- never UP.
  double comp = 1.0;
  // Sample-and-hold for the rate reduction.
  double holdValue[2] {0.0, 0.0};
  double holdPhase = 0.0;
  // The current bend event: which time slot it belongs to, how much of it is
  // left, and what it does. Events are decided from the cue POSITION, so a
  // rehearsal and the show make the same noise at the same moment.
  std::int64_t eventSlot = -1;
  std::size_t eventLeft = 0;
  int eventMode = 0;
  std::uint32_t eventBits = 0;
  // A 3ms ramp between clean and bent, so a short opening or closing is a
  // splice rather than a click.
  double bend = 0.0;
  // A smoothed level of the input, so misreads that turn silence into noise
  // (byte swap, offset reading) cannot.
  double inLevel = 0.0;
  // State-variable filter integrators, bounded, for Rail and Short.
  double svf1[2] {0.0, 0.0};
  double svf2[2] {0.0, 0.0};
  // Skip and Scrub read back from the line at their own position.
  double head = 0.0;
  std::size_t skipLeft = 0;
  std::size_t skipLength = 0;
  int skipRepeats = 0;
  // Short's divider flip-flop and the sign it last saw, per channel.
  int divider[2] {0, 0};
  double lastSign[2] {1.0, 1.0};
  double prevMotion = 0.0;
  // Ouroboros's loop state and its safety watchdog.
  double loop = 0.0;
  double hotSeconds = 0.0;
  double tripSeconds = 0.0;
  // The bent chunk, built before it is mixed so its loudness can be measured
  // and corrected in the SAME chunk. Grows once to the largest chunk seen.
  std::vector<double> bent;

  void reset() {
    for (int c = 0; c < 2; ++c) {
      x1[c] = x2[c] = y1[c] = y2[c] = 0.0;
      envelope[c] = 1.0;
      for (int i = 0; i < 4; ++i) combY[c][i] = 0.0;
      dcX[c] = dcY[c] = 0.0;
      holdValue[c] = 0.0;
      svf1[c] = svf2[c] = 0.0;
      divider[c] = 0;
      lastSign[c] = 1.0;
    }
    std::fill(line.begin(), line.end(), 0.0);
    writeAt = 0;
    readAt = 0;
    grainPass = 0;
    followed = 0.5;
    comp = 1.0;
    holdPhase = 0.0;
    eventSlot = -1;
    eventLeft = 0;
    eventMode = 0;
    eventBits = 0;
    bend = 0.0;
    inLevel = 0.0;
    head = 0.0;
    skipLeft = skipLength = 0;
    skipRepeats = 0;
    prevMotion = 0.0;
    loop = 0.0;
    hotSeconds = 0.0;
    tripSeconds = 0.0;
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

// ── BEND MACHINERY ──────────────────────────────────────────────────────────
//
// The scratch buffer carries doubles at INT16 SCALE (plus or minus 32768), so
// "the sample word" below is a real sixteen-bit word, the same one a DAC would
// have been handed.

constexpr double kFullScale = 32768.0;
// Every bend output is held inside one and a half times full scale before the
// limiter ever sees it. The limiter is a limiter, not a crash barrier.
constexpr double kBendCeiling = 32768.0 * 1.5;
// 3ms: long enough that a short opening is a splice, short enough to be a snap.
constexpr double kBendRampStep = 1.0 / (0.003 * kSampleRate);

// Recursive states are snapped to zero rather than left to decay into
// denormals, which on some CPUs cost a hundred times a normal multiply.
// 1e-9 of int16 full scale is far below anything audible.
inline double snap(double v) { return std::fabs(v) < 1e-9 ? 0.0 : v; }

// DETERMINISTIC NOISE. Never rand(): a bend decided from the cue's position
// makes the same noise at the same moment in rehearsal and in the show, the
// same rule the picture effects keep.
inline std::uint32_t hash32(std::uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  x ^= x >> 31;
  return static_cast<std::uint32_t>(x >> 32);
}
inline double hash01(std::uint64_t a, std::uint64_t b) {
  return static_cast<double>(hash32(a * 0x9E3779B97F4A7C15ULL ^ (b + 0x632BE59BD9B4E019ULL)))
         / 4294967296.0;
}

// Which slot of a `rate`-per-second clock the moment `pos` is in, and whether
// that is a different slot from the last one this effect saw. A seek or a
// re-take counts as a new slot, which is the honest answer.
inline bool enterEventSlot(AudioEffectSlotState& s, double pos, double rate,
                           std::int64_t& slotOut) {
  const std::int64_t k = static_cast<std::int64_t>(std::floor(pos * rate));
  slotOut = k;
  if (k == s.eventSlot) {
    return false;
  }
  s.eventSlot = k;
  return true;
}

// Ten hertz, one pole: takes out the step an offset reading or a flipped sign
// bit leaves behind, and nothing a person can hear.
inline double dcBlock(AudioEffectSlotState& s, int c, double x) {
  const double y = x - s.dcX[c] + 0.9987 * s.dcY[c];
  s.dcX[c] = x;
  s.dcY[c] = snap(y);
  return y;
}

// A two's-complement sixteen-bit word from a 0-65535 value, spelled out rather
// than cast so it means the same thing on every compiler.
inline std::int32_t wordFromBits(std::uint32_t u) {
  u &= 0xFFFFu;
  return u >= 0x8000u ? static_cast<std::int32_t>(u) - 0x10000
                      : static_cast<std::int32_t>(u);
}

// The sample as a sixteen-bit word. WRAP is the bent-circuit behaviour: a
// value past the top comes round from the bottom, the way an overflowing
// counter does, instead of flattening against a ceiling.
inline std::int32_t toWord(double v, bool wrap) {
  if (!std::isfinite(v)) {
    return 0;
  }
  const double c = std::clamp(v, -1.0e9, 1.0e9);
  const long long i = std::llround(c);
  if (wrap) {
    return wordFromBits(static_cast<std::uint32_t>(i & 0xFFFFLL));
  }
  return static_cast<std::int32_t>(std::clamp(i, -32768LL, 32767LL));
}

// Mid-tread requantising: zero stays exactly zero, so silence in is silence
// out. Mid-rise would sit on a half step and hum.
inline double requantise(double v, double bits) {
  const double b = std::clamp(bits, 1.0, 16.0);
  if (b >= 15.999) {
    return v;
  }
  const double step = std::pow(2.0, 16.0 - b);
  return std::round(v / step) * step;
}

// G.711 mu-law, read the wrong way on purpose: the high byte of a linear
// sample treated as if it were a telephone byte.
inline std::int32_t muLawDecode(std::uint32_t byteIn) {
  const std::uint32_t byte = (~byteIn) & 0xFFu;
  const std::int32_t exponent = static_cast<std::int32_t>((byte >> 4) & 7u);
  const std::int32_t mantissa = static_cast<std::int32_t>(byte & 0x0Fu);
  std::int32_t sample = ((mantissa << 3) + 0x84) << exponent;
  sample -= 0x84;
  return (byte & 0x80u) ? -sample : sample;
}

// LOUDNESS, NEVER UP. Bent audio sits near full scale whatever went in, and a
// limiter only stops PEAKS -- it will happily pass full-scale noise at -1dBFS,
// which through a PA is the actual danger. So each bend builds its whole chunk
// into slot.bent first, and finishBend measures it against the dry chunk and
// scales it back to the dry level -- in the SAME chunk, ramped from the last
// chunk's correction so there is no step. It can only reduce, and silence in
// drives it to silence out.
//
// The first version corrected the NEXT chunk from this one's measurement, and
// the check caught it: a burst that started was uncompensated for its first
// 40ms, and Word came out 4.6dB louder than its input with peaks past full
// scale.
inline void prepareBend(AudioEffectSlotState& s, std::size_t frames) {
  if (s.bent.size() < frames * 2) {
    s.bent.resize(frames * 2);
  }
}

inline void finishBend(std::vector<double>& samples, std::size_t frames,
                       AudioEffectSlotState& s, double dry, double wet) {
  // IN 5ms BLOCKS, INSTANT DOWN AND SLOW UP -- a limiter's shape, not an
  // average's. Measured over a whole chunk and ramped across it, a burst that
  // began mid-chunk went out at nearly full gain: in the running app Crush put
  // a full-scale spike on a -35dB input. Now a block that is too loud is
  // brought down for its whole length, and the gain only recovers gradually.
  constexpr std::size_t kBlock = 256;
  for (std::size_t start = 0; start < frames; start += kBlock) {
    const std::size_t end = std::min(frames, start + kBlock);
    double dryEnergy = 0.0;
    double wetEnergy = 0.0;
    for (std::size_t i = start * 2; i < end * 2; ++i) {
      dryEnergy += samples[i] * samples[i];
      wetEnergy += s.bent[i] * s.bent[i];
    }
    double target = 1.0;
    if (wetEnergy > 1e-3) {
      target = std::min(1.0, std::sqrt(dryEnergy / wetEnergy));
    }
    const double from = s.comp;
    const bool falling = target < from;
    const double span = static_cast<double>(end - start);
    for (std::size_t i = start; i < end; ++i) {
      // Falling: the whole block at the lower gain, now. Rising: eased, and
      // only a third of the way per block, so it breathes back rather than
      // stepping.
      const double g = falling
        ? target
        : from + (target - from) * (static_cast<double>(i - start) / span) * 0.33;
      for (int c = 0; c < 2; ++c) {
        double& x = samples[i * 2 + c];
        const double w = std::clamp(s.bent[i * 2 + c] * g, -kBendCeiling, kBendCeiling);
        x = dry * x + wet * w;
      }
    }
    s.comp = snap(falling ? target : from + (target - from) * 0.33);
  }
}

// THE CRUSH KERNEL, shared by Crush, Resolution and Ouroboros so the three agree
// on what "fewer bits" means. One frame at a time: the sample hold advances
// once per FRAME, so both channels are latched at the same instant and the
// stereo image does not smear.
struct CrushSettings {
  double bits = 16.0;
  double hold = 1.0;       // samples per held value; 1 = every sample
  bool wrap = false;
  double drive = 1.0;
};

inline bool crushLatch(AudioEffectSlotState& s, double hold) {
  if (hold <= 1.0) {
    return true;
  }
  s.holdPhase += 1.0;
  if (s.holdPhase >= hold) {
    s.holdPhase -= hold;
    return true;
  }
  return false;
}

// The bit just above a sample's own size. Rot and stray-wire damage is placed
// there rather than at the top of the word: flipping bit fifteen of a quiet
// sample is a jump from -40dB to full scale, which is a speaker hazard and not
// a sound. Relative to the signal, a flip is still a violent step -- sized to
// the music it happened to.
inline std::uint32_t bitAbove(std::int32_t word, int offset) {
  std::uint32_t mag = static_cast<std::uint32_t>(word < 0 ? -word : word);
  int top = 0;
  while (mag > 1u && top < 14) {
    mag >>= 1u;
    ++top;
  }
  return 1u << static_cast<unsigned>(std::clamp(top + offset, 1, 14));
}

inline double crushSample(AudioEffectSlotState& s, int c, double x, bool latch,
                          const CrushSettings& k, std::uint32_t rot) {
  if (latch) {
    s.holdValue[c] = x;
  }
  std::int32_t word = toWord(s.holdValue[c] * k.drive, k.wrap);
  if (rot != 0u) {
    // `rot` carries which of the three bits above the signal to flip.
    word = wordFromBits(static_cast<std::uint32_t>(word) ^
                        bitAbove(word, static_cast<int>(rot % 3u)));
  }
  const double q = requantise(static_cast<double>(word), k.bits);
  // Drive raised the level to reach the wrap; take it back off afterwards so
  // overflow changes the SHAPE, and the loudness meter handles the rest.
  return dcBlock(s, c, q / std::max(1.0, k.drive));
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
                                  AudioEffectState& state,
                                  const AudioEffectContext& ctx = {}) {
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
      case AudioEffectKind::Plugin: {
        // Somebody else's code, at the position in the chain the operator put
        // it. Everything about it -- the instance, the dry/wet, the scratch,
        // the time budget it has to keep to -- belongs to the host; this is
        // just where in the order it happens.
        if (ctx.host) {
          ctx.host->processPluginSlot(index, fx, samples.data(), frames);
        }
        break;
      }
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
      // ── THE FIVE THAT NEED THE DECK ─────────────────────────────────────
      //
      // Each of these reads `ctx`. On a cue that cannot supply what it wants --
      // an audio-only cue for Picture, a cue of unknown length for Seam -- the
      // effect passes the signal through rather than inventing the information,
      // and the inspector says so on the row. A control that quietly does
      // something arbitrary when its input is missing is worse than one that
      // does nothing.

      case AudioEffectKind::Picture: {
        // THE PICTURE PLAYS THE FILTER.
        //
        // Brightness opens it and darkness closes it, so a cut to black takes
        // the top off the sound and a bright frame gives it back. Movement
        // pushes it further open, so a still shot sits back and a fast one
        // comes forward. This is the thing a plugin cannot do at all: the
        // sound of a shot following the shot.
        if (!ctx.hasPicture) {
          break;
        }
        const double depth = detail::clamp01(fx.paramA);
        const double motionAmount = detail::clamp01(fx.paramB);
        // HOW FAST IT FOLLOWS, and why it is not instant. Video arrives 24 to
        // 60 times a second; a filter cutoff that jumped to a new value on
        // each frame would be a zipper. This smooths over 10ms to 2s, and the
        // slow end is the musical one -- a filter that breathes with the edit
        // rather than flickering with the grain.
        const double followMs = 10.0 + std::pow(detail::clamp01(fx.paramC), 2.0) * 1990.0;
        const double follow = detail::timeCoefficient(followMs);
        const bool invert = fx.paramD >= 0.5;

        double drive = detail::clamp01(static_cast<double>(ctx.luma));
        if (invert) {
          drive = 1.0 - drive;
        }
        drive = detail::clamp01(drive + motionAmount * detail::clamp01(
                                  static_cast<double>(ctx.motion)));
        // The target cutoff, and the smoothing that gets us there. `followed`
        // persists across chunks, so the ramp is continuous across the buffer
        // boundary rather than restarting at every callback.
        const double target = 0.5 + (drive - 0.5) * depth;
        for (std::size_t i = 0; i < frames; ++i) {
          slot.followed = target + (slot.followed - target) * follow;
          // Recomputing the biquad per sample would be honest and slow. The
          // cutoff moves at most a few hundred Hz per millisecond at the fast
          // end, so once every 64 samples (1.3ms) is inaudible and a
          // twentieth of the cost.
          if ((i & 63u) == 0u || i == 0) {
            const detail::Biquad built = detail::makeLowPass(
              detail::logFrequency(slot.followed, 300.0, 18000.0), 0.707);
            slot.cached[0] = built.b0; slot.cached[1] = built.b1;
            slot.cached[2] = built.b2; slot.cached[3] = built.a1;
            slot.cached[4] = built.a2;
          }
          for (int c = 0; c < 2; ++c) {
            double& s = samples[i * 2 + c];
            detail::Biquad f;
            f.b0 = slot.cached[0]; f.b1 = slot.cached[1]; f.b2 = slot.cached[2];
            f.a1 = slot.cached[3]; f.a2 = slot.cached[4];
            s = dry * s + wet * f.run(slot, c, s);
          }
        }
        break;
      }

      case AudioEffectKind::Placement: {
        // THE SOUND IS WHERE THE PICTURE IS.
        //
        // A PIP three-quarters of the way across the output sounds three
        // quarters of the way across the room. Shrink it and it goes away from
        // you: the top comes off, the image narrows, the level drops the way
        // distance actually does. Nothing about this is available to a plugin,
        // because the geometry lives in the cue and the cue is not something a
        // plugin can see.
        //
        // Deliberately NOT a pan. A pan puts a sound between two speakers; this
        // puts it in a position, which is why the far ear gets the delay and
        // the shadow rather than just less level.
        const double follow = detail::clamp01(fx.paramA);
        const double depthAmount = detail::clamp01(fx.paramB);
        const std::size_t maxDelay = 128;
        if (slot.line.size() < maxDelay * 2) {
          slot.line.assign(maxDelay * 2, 0.0);
          slot.writeAt = 0;
        }
        // Centre is centre: an offset of 0.5 means no placement at all, so a
        // full-frame cue with this armed sounds exactly as it did.
        const double azimuth = (detail::clamp01(static_cast<double>(ctx.centerX))
                                - 0.5) * 2.0 * follow;
        // Coverage is an AREA, so its square root is the linear size -- which
        // is what the eye reads as "how far away". A quarter-area PIP is half
        // the size, not a quarter of it.
        const double size = std::sqrt(detail::clamp01(
          static_cast<double>(ctx.coverage)));
        const double distance = (1.0 - size) * depthAmount;
        const std::size_t itd = static_cast<std::size_t>(
          std::fabs(azimuth) * 0.0007 * kSampleRate);
        const detail::Biquad air = detail::makeLowPass(
          detail::logFrequency(1.0 - distance * 0.75, 1500.0, 20000.0), 0.707);
        // Distance also narrows: two ears stop being able to tell much apart
        // about something far away, which is why a distant source collapses
        // toward the middle.
        const double width = 1.0 - distance * 0.8;
        const double level = 1.0 - distance * 0.5;
        const double farGain = 1.0 - std::fabs(azimuth) * 0.3;
        for (std::size_t i = 0; i < frames; ++i) {
          const double l = samples[i * 2], r = samples[i * 2 + 1];
          const double mid = (l + r) * 0.5;
          const double side = (l - r) * 0.5 * width;
          double outL = mid + side;
          double outR = mid - side;
          if (itd > 0) {
            const double mono = mid;
            slot.line[slot.writeAt * 2] = mono;
            const std::size_t readAt =
              (slot.writeAt + maxDelay - std::max<std::size_t>(itd, 1)) % maxDelay;
            const double late = slot.line[readAt * 2];
            slot.writeAt = (slot.writeAt + 1) % maxDelay;
            if (azimuth > 0.0) {
              outR = mid + side;
              outL = air.run(slot, 0, late) * farGain + side * 0.2;
            } else {
              outL = mid - side;
              outR = air.run(slot, 1, late) * farGain - side * 0.2;
            }
          }
          outL = air.run(slot, 0, outL) * level;
          outR = air.run(slot, 1, outR) * level;
          samples[i * 2] = dry * l + wet * outL;
          samples[i * 2 + 1] = dry * r + wet * outR;
        }
        break;
      }

      case AudioEffectKind::Seam: {
        // THE CUE RESOLVES INSTEAD OF BEING SEVERED.
        //
        // A fade is a volume ramp: it makes the last seconds QUIETER, which is
        // not the same as making them sound finished, and on speech it sounds
        // exactly like somebody turning a knob. This is what happens instead --
        // over the last few seconds the top comes down and a short room comes
        // up, so the sound settles into the cut the way a sound settles into a
        // room. The level is left alone; the fade still does that job if you
        // want it.
        //
        // It needs to know how long the cue is and where in it we are, which
        // no plugin is ever told.
        if (ctx.duration <= 0.0) {
          break;   // open-ended: there is no end to resolve into
        }
        const double window = 0.5 + detail::clamp01(fx.paramA) * 7.5;
        const double remaining = ctx.duration - ctx.position;
        if (remaining > window) {
          // Not yet in the seam. The room is still fed, so it is already
          // ringing when the seam opens rather than arriving from nothing.
          break;
        }
        const double into = detail::clamp01(1.0 - remaining / window);
        const double darken = detail::clamp01(fx.paramB) * into;
        const double roomAmount = detail::clamp01(fx.paramC) * into;
        const std::size_t maxDelay = static_cast<std::size_t>(kSampleRate * 0.08);
        if (slot.line.size() < maxDelay * 2) {
          slot.line.assign(maxDelay * 2, 0.0);
          slot.writeAt = 0;
        }
        const detail::Biquad top = detail::makeLowPass(
          detail::logFrequency(1.0 - darken * 0.85, 400.0, 20000.0), 0.707);
        for (std::size_t i = 0; i < frames; ++i) {
          for (int c = 0; c < 2; ++c) {
            const std::size_t readAt =
              (slot.writeAt + maxDelay - (maxDelay * 3 / 4)) % maxDelay;
            const double room = slot.line[readAt * 2 + c];
            double s = top.run(slot, c, samples[i * 2 + c]);
            slot.line[slot.writeAt * 2 + c] = s + room * 0.45;
            s += room * roomAmount;
            samples[i * 2 + c] = dry * samples[i * 2 + c] + wet * s;
          }
          slot.writeAt = (slot.writeAt + 1) % maxDelay;
        }
        break;
      }

      case AudioEffectKind::FrameLock: {
        // STUTTER ON THE FRAME, NOT ON THE BEAT.
        //
        // Every stutter effect there has ever been is quantised to a tempo,
        // because a tempo is the only clock a plugin has. This one is
        // quantised to the VIDEO FRAME PERIOD, so a repeat is exactly one, two
        // or four frames long and the chop lands on a frame boundary. Against
        // a picture that is a different thing entirely: the sound and the image
        // step together instead of merely being near each other.
        //
        // On a 23.976 clip the grain is 41.708ms, which is not a musical value
        // and is precisely the point.
        if (ctx.framePeriod <= 0.0) {
          break;   // no video clock: nothing to lock to
        }
        const int grainFrames = 1 + static_cast<int>(
          detail::clamp01(fx.paramA) * 7.0 + 0.5);
        const std::size_t grain = std::max<std::size_t>(
          static_cast<std::size_t>(ctx.framePeriod * grainFrames * kSampleRate), 16);
        const std::size_t maxDelay = static_cast<std::size_t>(kSampleRate * 0.5);
        if (slot.line.size() < maxDelay * 2) {
          slot.line.assign(maxDelay * 2, 0.0);
          slot.writeAt = 0;
          slot.readAt = 0;
        }
        const std::size_t hold = std::min(grain, maxDelay - 1);
        // How many times a grain repeats before the next one is captured. One
        // is a one-frame hiccup; eight is a full lock-up.
        const int repeats = 1 + static_cast<int>(
          detail::clamp01(fx.paramB) * 7.0 + 0.5);
        const bool reverse = fx.paramC >= 0.5;
        for (std::size_t i = 0; i < frames; ++i) {
          // Capture on the first pass through a grain, replay on the rest.
          const bool capturing = (slot.grainPass == 0);
          const std::size_t at = slot.readAt % hold;
          for (int c = 0; c < 2; ++c) {
            if (capturing) {
              slot.line[at * 2 + c] = samples[i * 2 + c];
            }
            const std::size_t from = reverse ? (hold - 1 - at) : at;
            const double held = slot.line[from * 2 + c];
            samples[i * 2 + c] = dry * samples[i * 2 + c] + wet * held;
          }
          if (++slot.readAt >= hold) {
            slot.readAt = 0;
            slot.grainPass = (slot.grainPass + 1) % repeats;
          }
        }
        break;
      }

      case AudioEffectKind::Suspend: {
        // A HELD CUE KEEPS ITS ROOM.
        //
        // Holding a cue holds the picture -- that is what hold IS -- and the
        // sound stops dead, which on any cue with room tone, an audience, rain
        // or a hum is an obvious hole. This keeps the last moment of it going:
        // a loop of the tail, crossfaded into itself so there is no seam, for
        // as long as the hold lasts.
        //
        // No plugin can do this because no plugin is told that a hold has
        // happened; all it sees is the samples stopping, which is
        // indistinguishable from silence in the material.
        const double loopSeconds = 0.05 + detail::clamp01(fx.paramA) * 1.95;
        const std::size_t loop = std::max<std::size_t>(
          static_cast<std::size_t>(loopSeconds * kSampleRate), 64);
        if (slot.line.size() < loop * 2) {
          slot.line.assign(loop * 2, 0.0);
          slot.writeAt = 0;
          slot.readAt = 0;
        }
        // How fast it lets go while held. 0 holds it indefinitely, which is
        // what a room tone under a held title wants; further up it settles away
        // over a few seconds.
        const double decayPerLoop = 1.0 - detail::clamp01(fx.paramB) * 0.5;
        if (!ctx.held) {
          // RUNNING: just keep the tail fresh. The effect is inaudible here,
          // which is correct -- it is a thing that happens at the hold, not a
          // thing you hear during the cue.
          for (std::size_t i = 0; i < frames; ++i) {
            for (int c = 0; c < 2; ++c) {
              slot.line[slot.writeAt * 2 + c] = samples[i * 2 + c];
            }
            slot.writeAt = (slot.writeAt + 1) % loop;
          }
          slot.readAt = slot.writeAt;
          slot.followed = 1.0;
          break;
        }
        // HELD: play the ring back, crossfaded across the splice so the loop
        // point is not a click. The fade is a quarter of the loop at each end,
        // which is long enough to hide a splice in room tone and short enough
        // not to smear a rhythm in it.
        const std::size_t fade = std::max<std::size_t>(loop / 4, 8);
        for (std::size_t i = 0; i < frames; ++i) {
          const std::size_t at = slot.readAt % loop;
          double gain = 1.0;
          if (at < fade) {
            gain = static_cast<double>(at) / static_cast<double>(fade);
          }
          const std::size_t mirror = (at + loop - fade) % loop;
          const double mirrorGain = 1.0 - gain;
          for (int c = 0; c < 2; ++c) {
            const double v = slot.line[at * 2 + c] * gain +
                             slot.line[mirror * 2 + c] * mirrorGain;
            samples[i * 2 + c] = dry * samples[i * 2 + c] +
                                 wet * v * slot.followed;
          }
          if (++slot.readAt >= loop) {
            slot.readAt = 0;
            slot.followed *= decayPerLoop;
          }
        }
        break;
      }

      // ── THE BENDS ────────────────────────────────────────────────────────
      //
      // Four rules every one of these keeps, because each is a way a bend
      // effect hurts a show:
      //  - wet is held inside kBendCeiling before anything else sees it;
      //  - anything that does arithmetic on sample WORDS is DC-blocked;
      //  - level-changing bends go through finishBend, which only ever reduces;
      //  - events come from the cue position, never from rand().

      case AudioEffectKind::Crush: {
        // FEWER BITS, FEWER SAMPLES, AND BITS THAT ROT.
        detail::CrushSettings k;
        k.bits = 16.0 - detail::clamp01(fx.paramA) * 15.0;
        const double b = detail::clamp01(fx.paramB);
        k.hold = 1.0 + b * b * 63.0;
        const double d = detail::clamp01(fx.paramD);
        k.wrap = d >= 0.5;
        k.drive = k.wrap ? 1.0 + (d - 0.5) * 14.0 : 1.0 + d * 2.0;
        const double rotChance = detail::clamp01(fx.paramC);
        const std::uint64_t salt = index * 7919u + 1u;
        detail::prepareBend(slot, frames);
        for (std::size_t i = 0; i < frames; ++i) {
          const double pos = ctx.position + static_cast<double>(i) / kSampleRate;
          std::int64_t eventSlot = 0;
          if (detail::enterEventSlot(slot, pos, 8.0, eventSlot)) {
            const std::uint64_t key = static_cast<std::uint64_t>(eventSlot);
            if (rotChance > 0.0 && detail::hash01(key, salt) < rotChance) {
              // One of the three bits just above the signal, for 10-200ms.
              slot.eventBits = 3u + detail::hash32(key ^ salt) % 3u;
              slot.eventLeft = static_cast<std::size_t>(
                (0.01 + 0.19 * detail::hash01(key, salt + 1u)) * kSampleRate);
            }
          }
          const std::uint32_t rot = slot.eventLeft > 0 ? slot.eventBits : 0u;
          if (slot.eventLeft > 0) {
            --slot.eventLeft;
          }
          const bool latch = detail::crushLatch(slot, k.hold);
          for (int c = 0; c < 2; ++c) {
            double& s = samples[i * 2 + c];
            double w = detail::crushSample(slot, c, s, latch, k, rot);
            slot.bent[i * 2 + c] = w;
          }
        }
        detail::finishBend(samples, frames, slot, dry, wet);
        break;
      }

      case AudioEffectKind::Word: {
        // A LOOSE WIRE ACROSS THE DAC'S BUS.
        //
        // Now and then -- on a clock, like a switch wired to a timer and
        // slammed -- each sample word is misread: bits rotated, bytes swapped,
        // read as unsigned, or read as a telephone byte. The clean sound
        // comes straight back the moment the wire lets go.
        const int depthBits = 1 + static_cast<int>(detail::clamp01(fx.paramA) * 11.0 + 0.5);
        const std::uint32_t lowMask = (1u << depthBits) - 1u;
        const double self = detail::clamp01(fx.paramB);
        const double c01 = detail::clamp01(fx.paramC);
        const double rate = 0.5 + c01 * c01 * 19.5;
        const double wire = detail::clamp01(fx.paramD);
        const std::uint64_t salt = index * 7919u + 2u;
        const std::size_t selfDelay = std::max<std::size_t>(
          static_cast<std::size_t>((0.001 + self * 0.049) * kSampleRate), 1);
        const std::size_t lineFrames = static_cast<std::size_t>(0.05 * kSampleRate) + 2;
        if (slot.line.size() < lineFrames * 2) {
          slot.line.assign(lineFrames * 2, 0.0);
          slot.writeAt = 0;
        }
        const std::uint32_t selfMask =
          self > 0.0 ? ((1u << (4 + static_cast<int>(self * 11.0))) - 1u) : 0u;
        detail::prepareBend(slot, frames);
        for (std::size_t i = 0; i < frames; ++i) {
          const double pos = ctx.position + static_cast<double>(i) / kSampleRate;
          std::int64_t eventSlot = 0;
          if (detail::enterEventSlot(slot, pos, rate, eventSlot)) {
            const std::uint64_t key = static_cast<std::uint64_t>(eventSlot);
            if (detail::hash01(key, salt) < 0.5) {
              slot.eventLeft = static_cast<std::size_t>(
                (0.3 + 0.7 * detail::hash01(key, salt + 1u)) * kSampleRate / rate);
              slot.eventBits = detail::hash32(key * 31u + salt);
              slot.eventMode = wire <= 0.2
                ? static_cast<int>(detail::hash32(key + salt) % 4u)
                : std::min(3, static_cast<int>((wire - 0.2) / 0.2));
            }
          }
          const double target = slot.eventLeft > 0 ? 1.0 : 0.0;
          if (slot.eventLeft > 0) {
            --slot.eventLeft;
          }
          slot.bend += std::clamp(target - slot.bend, -detail::kBendRampStep,
                                  detail::kBendRampStep);
          const double inMag = std::max(std::fabs(samples[i * 2]),
                                        std::fabs(samples[i * 2 + 1])) / detail::kFullScale;
          slot.inLevel = detail::snap(std::max(inMag, slot.inLevel * 0.9995));
          const std::size_t readAt =
            (slot.writeAt + lineFrames - std::min(selfDelay, lineFrames - 1)) % lineFrames;
          for (int c = 0; c < 2; ++c) {
            double& s = samples[i * 2 + c];
            const double x = s;
            const double other = slot.line[readAt * 2 + c];
            slot.line[slot.writeAt * 2 + c] = x;
            if (slot.bend <= 0.0) {
              slot.bent[i * 2 + c] = x;
              continue;   // clean: dry and wet are the same sound
            }
            std::uint32_t u = static_cast<std::uint32_t>(detail::toWord(x, false)) & 0xFFFFu;
            switch (slot.eventMode) {
              case 0: {
                const unsigned r = 1u + slot.eventBits % 15u;
                u = ((u << r) | (u >> (16u - r))) & 0xFFFFu;
                break;
              }
              case 1:
                u = ((u >> 8) | (u << 8)) & 0xFFFFu;
                break;
              case 2:
                u ^= 0x8000u;
                break;
              default:
                u = static_cast<std::uint32_t>(detail::muLawDecode(u >> 8)) & 0xFFFFu;
                break;
            }
            u ^= (slot.eventBits & lowMask);
            if (selfMask != 0u) {
              const std::uint32_t o =
                static_cast<std::uint32_t>(detail::toWord(other, false)) & 0xFFFFu;
              u ^= (o & selfMask);
            }
            double bent = static_cast<double>(detail::wordFromBits(u));
            // Every one of these misreads can turn a quiet sample into a
            // full-scale one. So the damage is held to a few times the input's
            // own level, whatever the fault -- a quiet moment stays quiet, a
            // loud one gets the whole wire.
            {
              const double limit = std::max(64.0, slot.inLevel * detail::kFullScale * 3.0);
              bent = x + std::clamp(bent - x, -limit, limit);
            }
            bent = detail::dcBlock(slot, c, bent);
            double w = x + slot.bend * (bent - x);
            slot.bent[i * 2 + c] = w;
          }
          slot.writeAt = (slot.writeAt + 1) % lineFrames;
        }
        detail::finishBend(samples, frames, slot, dry, wet);
        break;
      }

      case AudioEffectKind::Skip: {
        // A DISC SKIPPING. Jump back, play the same piece again a few times,
        // catch up. Every splice is ramped, so it skips without clicking.
        const std::size_t lineFrames = static_cast<std::size_t>(2.0 * kSampleRate);
        if (slot.line.size() < lineFrames * 2) {
          slot.line.assign(lineFrames * 2, 0.0);
          slot.writeAt = 0;
          slot.skipRepeats = 0;
          slot.readAt = 0;   // how much of the line holds real audio yet
        }
        const double a = detail::clamp01(fx.paramA);
        const std::size_t jump = std::max<std::size_t>(
          static_cast<std::size_t>((0.005 + a * a * 0.495) * kSampleRate), 32);
        const int repeats = 1 + static_cast<int>(detail::clamp01(fx.paramB) * 7.0 + 0.5);
        const double c01 = detail::clamp01(fx.paramC);
        const double rate = 0.2 + c01 * c01 * 7.8;
        const bool reverse = fx.paramD >= 0.5;
        const std::uint64_t salt = index * 7919u + 3u;
        const double edge = 0.003 * kSampleRate;
        for (std::size_t i = 0; i < frames; ++i) {
          const double pos = ctx.position + static_cast<double>(i) / kSampleRate;
          std::int64_t eventSlot = 0;
          if (detail::enterEventSlot(slot, pos, rate, eventSlot) &&
              slot.skipRepeats == 0 && slot.readAt >= jump &&
              detail::hash01(static_cast<std::uint64_t>(eventSlot), salt) < 0.75) {
            slot.skipLength = jump;
            slot.skipLeft = jump;
            slot.skipRepeats = repeats;
            slot.head = static_cast<double>((slot.writeAt + lineFrames - jump) % lineFrames);
          }
          const double target = slot.skipRepeats > 0 ? 1.0 : 0.0;
          slot.bend += std::clamp(target - slot.bend, -detail::kBendRampStep,
                                  detail::kBendRampStep);
          double replay[2] {0.0, 0.0};
          if (slot.skipLength > 0 && slot.bend > 0.0) {
            const std::size_t progress = slot.skipLength - std::min(slot.skipLeft, slot.skipLength);
            const std::size_t offset = reverse ? (slot.skipLength - 1 - progress) : progress;
            const std::size_t at =
              (static_cast<std::size_t>(slot.head) + offset) % lineFrames;
            // Each pass is faded in and out at its ends, so the loop point is
            // a splice rather than a step.
            const double env = std::min({1.0, static_cast<double>(progress) / edge,
                                         static_cast<double>(slot.skipLength - progress) / edge});
            replay[0] = slot.line[at * 2] * env;
            replay[1] = slot.line[at * 2 + 1] * env;
          }
          for (int c = 0; c < 2; ++c) {
            double& s = samples[i * 2 + c];
            const double x = s;
            slot.line[slot.writeAt * 2 + c] = x;
            const double w = x * (1.0 - slot.bend) + replay[c] * slot.bend;
            s = dry * x + wet * std::clamp(w, -detail::kBendCeiling, detail::kBendCeiling);
          }
          slot.writeAt = (slot.writeAt + 1) % lineFrames;
          slot.readAt = std::min(slot.readAt + 1, lineFrames);
          if (slot.skipRepeats > 0 && slot.skipLeft > 0 && --slot.skipLeft == 0) {
            if (--slot.skipRepeats > 0) {
              slot.skipLeft = slot.skipLength;
            }
          }
        }
        break;
      }

      case AudioEffectKind::Rail: {
        // A FILTER WHOSE SETTINGS ARE BEING CORRUPTED.
        //
        // A state-variable filter with every integrator pushed through tanh.
        // A corrupted damping value can go NEGATIVE, which in a normal filter
        // is a runaway; here it becomes self-oscillation that the saturation
        // bounds, so it screams without escaping. A slow triangle sags the
        // cutoff like a failing supply.
        const double fcBase = detail::logFrequency(fx.paramA, 60.0, 12000.0);
        const double corrupt = detail::clamp01(fx.paramB);
        const double sagDepth = detail::clamp01(fx.paramC);
        const double drive = 1.0 + detail::clamp01(fx.paramD) * 5.0;
        const std::uint64_t salt = index * 7919u + 4u;
        detail::prepareBend(slot, frames);
        double k = 0.5;
        double gScale = 1.0;
        for (std::size_t i = 0; i < frames; ++i) {
          const double pos = ctx.position + static_cast<double>(i) / kSampleRate;
          std::int64_t eventSlot = 0;
          detail::enterEventSlot(slot, pos, 6.0, eventSlot);
          const std::uint64_t key = static_cast<std::uint64_t>(eventSlot);
          if (corrupt > 0.0 && detail::hash01(key, salt) < corrupt) {
            k = 0.5 * (1.0 - 2.2 * corrupt * detail::hash01(key, salt + 1u));
            gScale = std::pow(2.0, (detail::hash01(key, salt + 2u) - 0.5) * 6.0 * corrupt);
          } else {
            k = 0.5;
            gScale = 1.0;
          }
          // Once per 32 samples: the tan() is the costly part.
          if ((i & 31u) == 0u) {
            const double phase = pos * 0.3 - std::floor(pos * 0.3);
            const double tri = 1.0 - 4.0 * std::fabs(phase - 0.5);
            const double fc = std::min(fcBase * std::pow(2.0, sagDepth * 2.0 * tri) * gScale,
                                       0.45 * kSampleRate);
            slot.followed = std::tan(3.14159265358979323846 * fc / kSampleRate);
          }
          const double g = slot.followed;
          const double denom = std::max(0.25, 1.0 + g * k + g * g);
          for (int c = 0; c < 2; ++c) {
            double& s = samples[i * 2 + c];
            const double x = s;
            const double xn = x / detail::kFullScale * drive;
            const double hp = (xn - (k + g) * slot.svf1[c] - slot.svf2[c]) / denom;
            const double bp = g * hp + slot.svf1[c];
            slot.svf1[c] = detail::snap(std::tanh(g * hp + bp));
            const double lp = g * bp + slot.svf2[c];
            slot.svf2[c] = detail::snap(std::tanh(g * bp + lp));
            if (!std::isfinite(slot.svf1[c]) || !std::isfinite(slot.svf2[c])) {
              slot.svf1[c] = slot.svf2[c] = 0.0;
            }
            double w = std::isfinite(lp) ? lp / drive * detail::kFullScale : 0.0;
            slot.bent[i * 2 + c] = w;
          }
        }
        detail::finishBend(samples, frames, slot, dry, wet);
        break;
      }

      case AudioEffectKind::Resolution: {
        // THE PICTURE'S SIZE IS THE SOUND'S RESOLUTION.
        //
        // A full-frame cue is clean. Shrink it into a corner and its sound
        // loses bits and samples in proportion; fade it to black and it goes
        // down to its bones. A cut rots bits for exactly one video frame.
        // No plugin knows how big its picture is.
        const double size = std::sqrt(detail::clamp01(static_cast<double>(ctx.coverage)));
        double target = detail::clamp01(fx.paramA) * (1.0 - size);
        if (ctx.hasPicture) {
          target += detail::clamp01(fx.paramB) *
                    (1.0 - detail::clamp01(static_cast<double>(ctx.luma)));
          const double motion = detail::clamp01(static_cast<double>(ctx.motion));
          if (fx.paramC > 0.0f && motion > 0.35 && slot.prevMotion <= 0.35) {
            const double framePeriod = ctx.framePeriod > 0.0 ? ctx.framePeriod : 0.04;
            slot.eventLeft = static_cast<std::size_t>(framePeriod * kSampleRate);
            // Further up the knob, further above the signal the flip lands.
            slot.eventBits = 3u + static_cast<std::uint32_t>(
              detail::clamp01(fx.paramC) * 2.0 + 0.5);
          }
          slot.prevMotion = motion;
        }
        target = detail::clamp01(target);
        const double floorBits = 2.0 + detail::clamp01(fx.paramD) * 10.0;
        const double follow = detail::timeCoefficient(30.0);
        detail::prepareBend(slot, frames);
        for (std::size_t i = 0; i < frames; ++i) {
          slot.loop = detail::snap(target + (slot.loop - target) * follow);
          const double r = slot.loop;
          const std::uint32_t rot = slot.eventLeft > 0 ? slot.eventBits : 0u;
          if (slot.eventLeft > 0) {
            --slot.eventLeft;
          }
          if (r < 0.001 && rot == 0u) {
            slot.bent[i * 2] = samples[i * 2];
            slot.bent[i * 2 + 1] = samples[i * 2 + 1];
            continue;   // clean, and the state stays where it was
          }
          detail::CrushSettings k;
          k.bits = 16.0 - r * (16.0 - floorBits);
          k.hold = 1.0 + r * r * 31.0;
          const bool latch = detail::crushLatch(slot, k.hold);
          for (int c = 0; c < 2; ++c) {
            double& s = samples[i * 2 + c];
            double w = detail::crushSample(slot, c, s, latch, k, rot);
            slot.bent[i * 2 + c] = w;
          }
        }
        detail::finishBend(samples, frames, slot, dry, wet);
        break;
      }

      case AudioEffectKind::Scrub: {
        // BRIGHTNESS IS TIME.
        //
        // The playhead sits behind "now" by an amount the picture decides:
        // darker is further back. It MOVES between those positions rather than
        // jumping, and a moving playhead bends pitch the way a tape machine
        // winding does -- so a fade to black is a sound falling away, a flash
        // is a chirp up to the present, and a hard cut throws the head.
        if (!ctx.hasPicture) {
          break;   // nothing decides where the head is
        }
        const std::size_t lineFrames = static_cast<std::size_t>(2.2 * kSampleRate);
        if (slot.line.size() < lineFrames * 2) {
          slot.line.assign(lineFrames * 2, 0.0);
          slot.writeAt = 0;
          slot.head = 1.0;
        }
        const double maxHead = static_cast<double>(lineFrames) - 4.0;
        const double depth = detail::clamp01(fx.paramA) * 2.0 * kSampleRate;
        double luma = detail::clamp01(static_cast<double>(ctx.luma));
        if (fx.paramD < 0.5f) {
          luma = 1.0 - luma;
        }
        const double target = std::clamp(depth * luma, 1.0, maxHead);
        const double motion = detail::clamp01(static_cast<double>(ctx.motion));
        if (motion > 0.35 && slot.prevMotion <= 0.35) {
          slot.head = std::clamp(slot.head + detail::clamp01(fx.paramB) * 0.5 * kSampleRate,
                                 1.0, maxHead);
        }
        slot.prevMotion = motion;
        const double c01 = detail::clamp01(fx.paramC);
        const double follow = detail::timeCoefficient(30.0 + c01 * c01 * 1970.0);
        for (std::size_t i = 0; i < frames; ++i) {
          slot.head = target + (slot.head - target) * follow;
          const double readPos = static_cast<double>(slot.writeAt) - slot.head +
                                 static_cast<double>(lineFrames);
          const double whole = std::floor(readPos);
          const double frac = readPos - whole;
          const std::size_t r0 = static_cast<std::size_t>(whole) % lineFrames;
          const std::size_t r1 = (r0 + 1) % lineFrames;
          for (int c = 0; c < 2; ++c) {
            double& s = samples[i * 2 + c];
            const double x = s;
            slot.line[slot.writeAt * 2 + c] = x;
            const double w = slot.line[r0 * 2 + c] * (1.0 - frac) +
                             slot.line[r1 * 2 + c] * frac;
            s = dry * x + wet * std::clamp(w, -detail::kBendCeiling, detail::kBendCeiling);
          }
          slot.writeAt = (slot.writeAt + 1) % lineFrames;
        }
        break;
      }

      case AudioEffectKind::Short: {
        // EVERY CUT CLOSES A SHORT.
        //
        // A small circuit -- a filter, a delay, a divider clocked by the
        // signal's own zero crossings, a supply -- with a wire that lands
        // somewhere new each time the picture cuts, held for a number of VIDEO
        // FRAMES. The same cut always gets the same wire, because the choice
        // comes from where in the cue the cut is. With no picture it runs on a
        // timer instead. A plugin has no idea where the edits are.
        const double sensitivity = detail::clamp01(fx.paramA);
        const int holdFrames = 1 + static_cast<int>(detail::clamp01(fx.paramB) * 23.0 + 0.5);
        const double framePeriod = ctx.framePeriod > 0.0 ? ctx.framePeriod : 1.0 / 25.0;
        const std::size_t hold = static_cast<std::size_t>(holdFrames * framePeriod * kSampleRate);
        const double damage = detail::clamp01(fx.paramC);
        const double fixedWire = detail::clamp01(fx.paramD);
        const std::uint64_t salt = index * 7919u + 7u;
        const std::size_t lineFrames = static_cast<std::size_t>(0.03 * kSampleRate);
        if (slot.line.size() < lineFrames * 2) {
          slot.line.assign(lineFrames * 2, 0.0);
          slot.writeAt = 0;
        }
        auto close = [&](std::uint64_t key) {
          slot.eventLeft = std::max<std::size_t>(hold, 1);
          slot.eventMode = fixedWire <= 0.001
            ? static_cast<int>(detail::hash32(key ^ salt) % 5u)
            : std::min(4, static_cast<int>(fixedWire * 5.0));
        };
        if (ctx.hasPicture) {
          const double motion = detail::clamp01(static_cast<double>(ctx.motion));
          const double threshold = 0.6 - sensitivity * 0.5;
          if (motion > threshold && slot.prevMotion <= threshold) {
            close(static_cast<std::uint64_t>(std::llround(ctx.position * 100.0)));
          }
          slot.prevMotion = motion;
        }
        const double lpCoef = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * 600.0 / kSampleRate);
        detail::prepareBend(slot, frames);
        for (std::size_t i = 0; i < frames; ++i) {
          if (!ctx.hasPicture) {
            const double pos = ctx.position + static_cast<double>(i) / kSampleRate;
            std::int64_t eventSlot = 0;
            if (detail::enterEventSlot(slot, pos, 0.5 + sensitivity * 7.5, eventSlot) &&
                detail::hash01(static_cast<std::uint64_t>(eventSlot), salt) < 0.5) {
              close(static_cast<std::uint64_t>(eventSlot));
            }
          }
          const double target = slot.eventLeft > 0 ? 1.0 : 0.0;
          if (slot.eventLeft > 0) {
            --slot.eventLeft;
          }
          slot.bend += std::clamp(target - slot.bend, -detail::kBendRampStep,
                                  detail::kBendRampStep);
          slot.holdPhase += 30.0 / kSampleRate;
          slot.holdPhase -= std::floor(slot.holdPhase);
          const double sag = 0.5 + 0.5 * (1.0 - 4.0 * std::fabs(slot.holdPhase - 0.5));
          const std::size_t readAt = slot.writeAt;   // the oldest sample in the line
          for (int c = 0; c < 2; ++c) {
            double& s = samples[i * 2 + c];
            const double xs = s;
            const double x = xs / detail::kFullScale;
            slot.svf1[c] = detail::snap(slot.svf1[c] + lpCoef * (x - slot.svf1[c]));
            const double lp = slot.svf1[c];
            const double delayed = slot.line[readAt * 2 + c];
            const double sign = x >= 0.0 ? 1.0 : -1.0;
            if (sign != slot.lastSign[c]) {
              slot.lastSign[c] = sign;
              slot.divider[c] ^= 1;
            }
            const double square = slot.divider[c] ? 1.0 : -1.0;
            slot.inLevel = detail::snap(std::max(std::fabs(x), slot.inLevel * 0.9995));
            double shorted = x;
            double feedback = 0.0;
            if (slot.bend > 0.0) {
              switch (slot.eventMode) {
                case 0:   // the filter's output bridged to a hot input
                  shorted = std::tanh(lp * (1.0 + 6.0 * damage)) * 0.8;
                  break;
                case 1:   // the delay's output wired back into itself
                  feedback = std::tanh(delayed * (0.6 + 0.35 * damage));
                  shorted = x + feedback;
                  break;
                case 2:   // the divider steals the clock: an octave-down fuzz
                  shorted = x * (1.0 - damage) + square * slot.inLevel * damage * 1.5;
                  break;
                case 3:   // the supply sagging at 30Hz under the load
                  shorted = x * (1.0 - damage * 0.9 * sag);
                  break;
                default:  // everything bridged onto one node
                  shorted = std::tanh((x + delayed + lp) * (1.0 + 4.0 * damage)) * 0.7;
                  break;
              }
            }
            // The delay line is fed the clean signal, plus its own output only
            // while that wire is closed -- so a short cannot leave a feedback
            // loop running after it opens.
            slot.line[slot.writeAt * 2 + c] = x + feedback * slot.bend * 0.9;
            double w = (x + slot.bend * (shorted - x)) * detail::kFullScale;
            w = detail::dcBlock(slot, c, w);
            slot.bent[i * 2 + c] = w;
          }
          slot.writeAt = (slot.writeAt + 1) % lineFrames;
        }
        detail::finishBend(samples, frames, slot, dry, wet);
        break;
      }

      case AudioEffectKind::Ouroboros: {
        // THE LOOP.
        //
        // The bend is driven by the FINISHED picture -- after its effects, as
        // the audience sees it. Put an effect on that picture whose LFO is set
        // to Audio, and it follows this cue's sound. So: the sound bends, the
        // bent sound moves the picture effect, the picture changes, the changed
        // picture bends the sound. A feedback loop through a person's eyes.
        //
        // Every term in it is held between 0 and 1, so it cannot diverge; the
        // worst it can do is sit somewhere loud or strobing. What stops that:
        //  - coupling arrives below one, so the loop damps unless pushed;
        //  - its input is slewed over 150ms, slower than one trip round it;
        //  - it leaks back toward clean, so it needs feeding to stay excited;
        //  - it bends TIMBRE, through the loudness meter, never level;
        //  - a watchdog: hot input with a deep bend for two seconds opens the
        //    loop for five, and a hard cut can reset it.
        const double chunkSeconds = static_cast<double>(frames) / kSampleRate;
        const double pl = ctx.hasPostPicture ? ctx.postLuma
                        : (ctx.hasPicture ? ctx.luma : 0.5);
        const double pm = ctx.hasPostPicture ? ctx.postMotion
                        : (ctx.hasPicture ? ctx.motion : 0.0);
        const double coupling = detail::clamp01(fx.paramA);
        const double drive = detail::clamp01(
          coupling * (2.0 * std::fabs(detail::clamp01(pl) - 0.5) +
                      1.0 * detail::clamp01(pm)));
        slot.loop += (drive - slot.loop) * (1.0 - std::exp(-chunkSeconds / 0.15));
        const double leakSeconds = 1.0 + detail::clamp01(fx.paramB) * 9.0;
        slot.loop = detail::snap(slot.loop * std::exp(-chunkSeconds / leakSeconds));
        if (fx.paramC >= 0.5f && pm > 0.6 && slot.prevMotion <= 0.6) {
          slot.loop = 0.0;
        }
        slot.prevMotion = pm;
        double inEnergy = 0.0;
        for (std::size_t i = 0; i < frames * 2; ++i) {
          const double v = samples[i] / detail::kFullScale;
          inEnergy += v * v;
        }
        const double inRms = std::sqrt(inEnergy / static_cast<double>(frames * 2));
        if (slot.tripSeconds > 0.0) {
          slot.tripSeconds = std::max(0.0, slot.tripSeconds - chunkSeconds);
          slot.loop = 0.0;
        } else if (inRms > 0.316 && slot.loop > 0.8) {
          slot.hotSeconds += chunkSeconds;
          if (slot.hotSeconds > 2.0) {
            slot.tripSeconds = 5.0;
            slot.hotSeconds = 0.0;
            slot.loop = 0.0;
          }
        } else {
          slot.hotSeconds = std::max(0.0, slot.hotSeconds - chunkSeconds);
        }
        const double depth = detail::clamp01(slot.loop);
        const double character = detail::clamp01(fx.paramD);
        detail::CrushSettings k;
        k.bits = 16.0 - depth * 14.0 * (1.0 - character * 0.6);
        k.hold = 1.0 + depth * depth * 40.0 * (1.0 - character * 0.6);
        const std::size_t lineFrames = static_cast<std::size_t>(0.25 * kSampleRate);
        if (slot.line.size() < lineFrames * 2) {
          slot.line.assign(lineFrames * 2, 0.0);
          slot.writeAt = 0;
          slot.skipLeft = 0;
        }
        const std::uint64_t salt = index * 7919u + 8u;
        const double stutterChance = depth * character;
        detail::prepareBend(slot, frames);
        for (std::size_t i = 0; i < frames; ++i) {
          const double pos = ctx.position + static_cast<double>(i) / kSampleRate;
          std::int64_t eventSlot = 0;
          if (detail::enterEventSlot(slot, pos, 3.0, eventSlot) && slot.skipLeft == 0 &&
              detail::hash01(static_cast<std::uint64_t>(eventSlot), salt) < stutterChance) {
            slot.skipLength = static_cast<std::size_t>(
              (0.03 + 0.09 * detail::hash01(static_cast<std::uint64_t>(eventSlot), salt + 1u)) *
              kSampleRate);
            slot.skipLeft = slot.skipLength * 2;
            slot.head = static_cast<double>(
              (slot.writeAt + lineFrames - slot.skipLength) % lineFrames);
          }
          const double target = (depth > 0.02) ? 1.0 : 0.0;
          slot.bend += std::clamp(target - slot.bend, -detail::kBendRampStep,
                                  detail::kBendRampStep);
          const bool latch = detail::crushLatch(slot, k.hold);
          for (int c = 0; c < 2; ++c) {
            double& s = samples[i * 2 + c];
            const double x = s;
            slot.line[slot.writeAt * 2 + c] = x;
            double source = x;
            if (slot.skipLeft > 0 && slot.skipLength > 0) {
              const std::size_t progress = (slot.skipLength * 2 - slot.skipLeft) % slot.skipLength;
              const std::size_t at =
                (static_cast<std::size_t>(slot.head) + progress) % lineFrames;
              const double env = std::min({1.0, static_cast<double>(progress) / 144.0,
                                           static_cast<double>(slot.skipLength - progress) / 144.0});
              source = slot.line[at * 2 + c] * env;
            }
            const double crushed = detail::crushSample(slot, c, source, latch, k, 0u);
            double w = x + slot.bend * (crushed - x);
            slot.bent[i * 2 + c] = w;
          }
          slot.writeAt = (slot.writeAt + 1) % lineFrames;
          if (slot.skipLeft > 0) {
            --slot.skipLeft;
          }
        }
        detail::finishBend(samples, frames, slot, dry, wet);
        break;
      }

      case AudioEffectKind::None:
      case AudioEffectKind::Count:
        break;
    }
  }
}

// ── DOES THIS CHAIN NEED TO SEE THE PICTURE? ────────────────────────────────
//
// ONE definition, used by the app's CPU-path decision AND by the engine's own
// decode-format decision. There used to be only the app's, and the engine
// never asked it: on the zero-copy GPU path a cue whose sound follows its
// picture got no picture at all, and the effect sat there doing nothing.
inline bool audioChainNeedsPicture(const std::vector<AudioEffect>& stack) {
  for (const AudioEffect& fx : stack) {
    if (fx.bypassed || fx.amount <= 0.0f) {
      continue;
    }
    // Every effect that reads the frame's brightness or its cuts. Short and
    // Resolution still do something without a picture (a timer, the
    // geometry), but the part worth having is the part that follows the edit.
    switch (fx.kind) {
      case AudioEffectKind::Picture:
      case AudioEffectKind::Resolution:
      case AudioEffectKind::Scrub:
      case AudioEffectKind::Short:
      case AudioEffectKind::Ouroboros:
        return true;
      default:
        break;
    }
  }
  return false;
}

// ── SERIALISATION ───────────────────────────────────────────────────────────
//
// ONE FIELD, "token:amount:a:b:c:d:bypass|...", for the same reason the
// picture stack uses one: a variable number of columns would shift every
// positional index after it, which this project file already carries scars
// from.
namespace detail {

// A show file is tab-delimited text and this record is colon-delimited inside
// it, so a plugin id -- which on Windows reads "vst3:C:/Program Files/..." --
// cannot go in raw. Percent-encoding, restricted to the characters that would
// break a field, so the id stays readable in a diff.
inline std::string escapeAudioField(const std::string& text) {
  static const char* kHex = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size());
  for (const unsigned char c : text) {
    if (c == ':' || c == '|' || c == '%' || c == '\t' || c == '\n' ||
        c == '\r' || c < 0x20) {
      out.push_back('%');
      out.push_back(kHex[c >> 4]);
      out.push_back(kHex[c & 0x0f]);
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  return out;
}

inline std::string unescapeAudioField(const std::string& text) {
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' && i + 2 < text.size()) {
      const int hi = hex(text[i + 1]);
      const int lo = hex(text[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    out.push_back(text[i]);
  }
  return out;
}

// A plugin's state is arbitrary binary -- often tens of kilobytes of it -- so
// it goes in base64 rather than percent-encoded, which would treble the size of
// something already the largest thing in the record.
inline std::string base64Encode(const std::string& bytes) {
  static const char* kSet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < bytes.size(); i += 3) {
    const std::uint32_t v = (static_cast<unsigned char>(bytes[i]) << 16) |
                            (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                            static_cast<unsigned char>(bytes[i + 2]);
    out.push_back(kSet[(v >> 18) & 0x3f]);
    out.push_back(kSet[(v >> 12) & 0x3f]);
    out.push_back(kSet[(v >> 6) & 0x3f]);
    out.push_back(kSet[v & 0x3f]);
  }
  if (i < bytes.size()) {
    std::uint32_t v = static_cast<unsigned char>(bytes[i]) << 16;
    const bool two = (i + 1) < bytes.size();
    if (two) {
      v |= static_cast<unsigned char>(bytes[i + 1]) << 8;
    }
    out.push_back(kSet[(v >> 18) & 0x3f]);
    out.push_back(kSet[(v >> 12) & 0x3f]);
    out.push_back(two ? kSet[(v >> 6) & 0x3f] : '=');
    out.push_back('=');
  }
  return out;
}

inline std::string base64Decode(const std::string& text) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  out.reserve(text.size() / 4 * 3);
  std::uint32_t acc = 0;
  int bits = 0;
  for (const char c : text) {
    const int v = value(c);
    if (v < 0) {
      continue;   // padding, whitespace, or a character a text editor added
    }
    acc = (acc << 6) | static_cast<std::uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((acc >> bits) & 0xff));
    }
  }
  return out;
}

}  // namespace detail

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
    // Only a plugin slot carries these, so every record written before plugins
    // existed still round-trips byte for byte.
    if (fx.kind == AudioEffectKind::Plugin) {
      out += ':';
      out += detail::escapeAudioField(fx.pluginId);
      out += ':';
      out += detail::base64Encode(fx.pluginState);
    }
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
    if (fx.kind == AudioEffectKind::Plugin) {
      if (parts.size() > 7) {
        fx.pluginId = detail::unescapeAudioField(parts[7]);
      }
      if (parts.size() > 8) {
        fx.pluginState = detail::base64Decode(parts[8]);
      }
    }
    stack.push_back(std::move(fx));
  }
  return stack;
}

}  // namespace deckboy::audiofx
