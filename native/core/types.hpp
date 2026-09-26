// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// types.hpp — Central domain model for the entire Deckboy application.
//
// Every major subsystem depends on this file:
//   - MediaEngine (engine/media_engine.*) reads Cue fields for decode config
//   - Project save/load (app/app_project_state.ipp) serializes all structs here
//   - UI rendering (app/app_render_*.ipp) reads struct fields for display
//   - Companion/OSC (app/app_remote_command.ipp) maps remote commands to fields
//   - Output compositor (app/app_render_output.ipp) reads OutputTarget + Deck
//
// Struct layout notes: fields are ordered by alignment (8→4→1 byte) to
// minimize padding on both MSVC and GCC/Clang. Do NOT reorder fields
// without understanding alignment implications.
//
// Serialization: all structs are persisted to tab-delimited .deckboy files.
// Adding a new field requires updating BOTH saveProject() and loadProject()
// in app/app_project_state.ipp, with a backwards-compat guard on field count.
// ============================================================================

#ifndef DECKBOY_CORE_TYPES_HPP
#define DECKBOY_CORE_TYPES_HPP

#include "cue_effects.hpp"
#include "audio_effects.hpp"
#include "core/sdl_compat.hpp"
#include <memory>
#include <string>
#include <vector>
#include "constants.hpp"

// Domain types for the cue deck. SDL_Color/SDL_Rect used for UI integration.

// ---------------------------------------------------------------------------
// CueKind — Discriminator for what a cue represents and how it is decoded.
//
// MediaEngine uses this to choose the ffmpeg pipeline (or skip decode entirely
// for patterns). The UI uses it to show kind-specific inspector fields.
// Save/load writes it as an integer index — never reorder existing values.
// ---------------------------------------------------------------------------
enum class CueKind {
  Video,         // file-based video (decoded by ffmpeg pipe)
  Image,         // single still image (loaded as one decoded frame)
  Pattern,       // procedurally generated (bars, gradient, etc.) — no ffmpeg
  Browser,       // CEF/WebKit page rendered to texture (platform/browser.*)
  WindowSource,  // desktop window capture (platform/capture_backend.*)
  Camera,        // live camera input (v4l2 / dshow / avfoundation)
  Syphon,        // macOS Syphon / Windows Spout texture sharing
  SrtStream,     // live stream input (srt://, rtmp://, rtsp://, udp://)
  NdiSource,     // NDI receive input (ndi://SOURCE_NAME)
  DeckLinkSource,// Blackmagic capture input (decklink://DEVICE_INDEX)
                 // Native SDK capture rather than an ffmpeg pipe: the bundled
                 // ffmpeg has no decklink demuxer, and the SDK is already
                 // linked here for playout. See platform/decklink.*
  Pip,           // picture-in-picture composite (references another cue)
  LowerThird,    // text overlay with optional background bar
  Composite,     // multi-slot layout (quad-split, side-by-side, etc.)
  Audio,         // audio-only cue (no video output)
  Timer          // stage/speaker countdown, generated like a pattern but with
                 // its own transport-linked state (see TimerState)
,
  Tone,          // procedurally generated audio: line-up tone, noise, sweep,
                 // channel identify. The audio equivalent of Pattern
  VideoSynth,    // oscillator-driven video with feedback and mirroring, in the
                 // lineage of Atari Video Music and Sleepy Circuits Hypno
  Master,        // fires an assigned cue on each of several decks at once.
                 // Carries no media of its own — see MasterAssignment
  Text,
  MidiFile,          // words on the screen, as a SOURCE rather than an overlay:
                 // a title card, a holding slide, a scrolling notice. Animated
                 // from the cue's own transport clock -- see CueTextAnimation
  Dmx,           // sends DMX channel levels over Art-Net on GO, with a fade
                 // time. NOT a lighting console -- "house lights to 20% on cue
                 // 14" is the case this serves, at a fraction of the cost
  Script,        // runs Deckboy's OWN remote-protocol lines on GO. Every
                 // verb the socket accepts, from the cue list -- which is 90%
                 // of what a script cue is for without embedding a language
  Timecode,      // starts, stops or jams the LTC generator on GO. The
                 // generator already existed and could only be reached from a
                 // settings toggle, which is not something a show can cue
  Network,       // SENDS on GO: an OSC message, a UDP datagram, or a line
                 // of TCP. The other half of the show-control story -- Deckboy
                 // has listened on all three and spoken on none
  Midi,          // SENDS a MIDI message on GO: note, CC, program or MSC.
                 // Deckboy has listened to MIDI since early on and never
                 // spoken a word of it -- see platform/midi.hpp
  Fade,          // ramps something over time: a deck's opacity, its audio, or
                 // the master dimmer. Carries no media either -- see CueFadeWhat
  Target         // acts ON another cue rather than playing anything: start it,
                 // stop it, pause it, arm it. Carries no media either — see
                 // CueTargetVerb and Cue::targetCueId
};

// ---------------------------------------------------------------------------
// What a Target cue does to the cue it points at.
//
// Every one of these is a transport call the app already makes; the cue is
// only a way to put one in the list and fire it in sequence. That is also why
// there is no verb here that does not already exist as an operator action --
// a Target cue must never be the only way to reach a behaviour, or the
// behaviour goes untested everywhere else.
// ---------------------------------------------------------------------------
enum class CueTargetVerb {
  Start,     // take it on its own deck
  Stop,      // stop that deck
  Pause,     // pause it where it is
  Resume,    // carry on from where it was paused
  Load,      // select it without taking it -- next GO on that deck fires it
  Arm,       // make it live-able again
  Disarm,    // leave it in the list, inert: GO passes straight over it
};

// ---------------------------------------------------------------------------
// What a Fade cue ramps.
//
// Three things, chosen because all three already exist as live values that are
// safe to move: a deck's picture level, a deck's audio level, and the master
// dimmer. None of them is a field the show file keeps a fade in -- the deck's
// engine volume is runtime, and the opacity has a target the app already ramps
// toward -- so a fade cannot leave a show permanently quieter than it was
// saved.
// ---------------------------------------------------------------------------
enum class CueFadeWhat {
  DeckOpacity,   // Deck::playlistOpacity -- how much of that deck reaches the output
  DeckVolume,    // the deck engine's runtime volume, not any cue's saved gain
  MasterDimmer,  // Project::masterDimmer -- everything, at once
};

inline const char* cueFadeWhatToken(CueFadeWhat w) {
  switch (w) {
    case CueFadeWhat::DeckVolume:   return "volume";
    case CueFadeWhat::MasterDimmer: return "dimmer";
    case CueFadeWhat::DeckOpacity:  break;
  }
  return "opacity";
}

inline const char* cueFadeWhatLabel(CueFadeWhat w) {
  switch (w) {
    case CueFadeWhat::DeckVolume:   return "Deck volume";
    case CueFadeWhat::MasterDimmer: return "Master dimmer";
    case CueFadeWhat::DeckOpacity:  break;
  }
  return "Deck opacity";
}

inline CueFadeWhat cueFadeWhatFromToken(const std::string& token) {
  if (token == "volume") return CueFadeWhat::DeckVolume;
  if (token == "dimmer") return CueFadeWhat::MasterDimmer;
  return CueFadeWhat::DeckOpacity;
}

// The shape of the ramp. Linear is the default because it is what every
// existing fade in the app already does, so a show that gains a fade cue does
// not also gain a curve nobody asked for.
enum class CueFadeCurve {
  Linear,
  EaseIn,    // slow to start: good for bringing something up under speech
  EaseOut,   // slow to finish: the standard music fade-out
  SCurve,    // slow at both ends
};

inline const char* cueFadeCurveToken(CueFadeCurve c) {
  switch (c) {
    case CueFadeCurve::EaseIn:  return "ease-in";
    case CueFadeCurve::EaseOut: return "ease-out";
    case CueFadeCurve::SCurve:  return "s-curve";
    case CueFadeCurve::Linear:  break;
  }
  return "linear";
}

inline const char* cueFadeCurveLabel(CueFadeCurve c) {
  switch (c) {
    case CueFadeCurve::EaseIn:  return "Ease in";
    case CueFadeCurve::EaseOut: return "Ease out";
    case CueFadeCurve::SCurve:  return "S-curve";
    case CueFadeCurve::Linear:  break;
  }
  return "Linear";
}

inline CueFadeCurve cueFadeCurveFromToken(const std::string& token) {
  if (token == "ease-in")  return CueFadeCurve::EaseIn;
  if (token == "ease-out") return CueFadeCurve::EaseOut;
  if (token == "s-curve")  return CueFadeCurve::SCurve;
  return CueFadeCurve::Linear;
}

// Progress 0-1 in, shaped 0-1 out. Endpoints are exact for every curve, which
// is what lets a fade finish ON its target rather than near it.
inline double applyCueFadeCurve(CueFadeCurve c, double t) {
  t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
  switch (c) {
    case CueFadeCurve::EaseIn:  return t * t;
    case CueFadeCurve::EaseOut: return t * (2.0 - t);
    case CueFadeCurve::SCurve:  return t * t * (3.0 - 2.0 * t);
    case CueFadeCurve::Linear:  break;
  }
  return t;
}

inline const char* cueTargetVerbToken(CueTargetVerb v) {
  switch (v) {
    case CueTargetVerb::Stop:   return "stop";
    case CueTargetVerb::Pause:  return "pause";
    case CueTargetVerb::Resume: return "resume";
    case CueTargetVerb::Load:   return "load";
    case CueTargetVerb::Arm:    return "arm";
    case CueTargetVerb::Disarm: return "disarm";
    case CueTargetVerb::Start:  break;
  }
  return "start";
}

inline const char* cueTargetVerbLabel(CueTargetVerb v) {
  switch (v) {
    case CueTargetVerb::Stop:   return "Stop";
    case CueTargetVerb::Pause:  return "Pause";
    case CueTargetVerb::Resume: return "Resume";
    case CueTargetVerb::Load:   return "Load";
    case CueTargetVerb::Arm:    return "Arm";
    case CueTargetVerb::Disarm: return "Disarm";
    case CueTargetVerb::Start:  break;
  }
  return "Start";
}

inline CueTargetVerb cueTargetVerbFromToken(const std::string& token) {
  if (token == "stop")   return CueTargetVerb::Stop;
  if (token == "pause")  return CueTargetVerb::Pause;
  if (token == "resume") return CueTargetVerb::Resume;
  if (token == "load")   return CueTargetVerb::Load;
  if (token == "arm")    return CueTargetVerb::Arm;
  if (token == "disarm") return CueTargetVerb::Disarm;
  return CueTargetVerb::Start;
}

// ---------------------------------------------------------------------------
// Video synth.
//
// Two machines are the reference. Atari Video Music (1976) folded simple
// shapes through mirrors and cycled colour with the music -- hard-edged,
// symmetrical, chunky. Sleepy Circuits Hypno is the modern descendant, and its
// signature is FEEDBACK: each frame is transformed and blended back into the
// next, which is what produces the endless tunnels and blooms that no
// single-pass generator can imitate.
//
// So feedback is not an effect bolted on here, it is the point. A generator
// without it looks like a screensaver; with it, it looks alive.
// ---------------------------------------------------------------------------
enum class VideoSynthShape {
  Plasma,     // interfering sine fields -- the warm, liquid one
  Diamond,    // Atari Video Music's hard rhombus lattice
  Rings,      // concentric, good with feedback zoom for tunnels
  Grid,       // rectilinear interference, sharp and technical
  Moire,      // two rotating grids beating against each other
};

// How the frame is folded before it is drawn. Mirroring is what turns an
// arbitrary pattern into something that reads as designed.
enum class VideoSynthMirror {
  None,
  Horizontal,
  Quad,       // both axes: the classic kaleidoscope quarter
  Kaleido,    // quad plus a diagonal fold, six-way symmetry
};

enum class VideoSynthPalette {
  Spectrum,   // full hue sweep
  Amber,      // single-hue phosphor, closest to the 1976 look
  Ice,
  Fire,
  Mono,
  // Hardware palettes. These are not arbitrary colour schemes -- each is the
  // actual set a machine could display, which is why work made on them shares
  // a look that a freely-chosen palette never quite gets.
  Ega,        // the 16-colour IBM set: harsh, saturated, unmistakable
  C64,        // Commodore 64: muted, muddy, and instantly period
  Gameboy,    // four greens, the original DMG
  Cga,        // cyan/magenta/white on black -- the loudest four colours in
              // computing, and the reason CGA is remembered at all
  Nes,        // NES-ish: soft pastels against hard darks
  Vapor,      // pink/cyan/purple, the modern glitch-art convention
};

// THE LIST HAS GROWN TWICE and the loader's clamp did not come with it: a cue
// saved on Game Boy, CGA, NES or Vapor reopened on Mono, because anything
// above 4 was clamped away. The look survived the show and died in the file.
inline constexpr int kVideoSynthPaletteCount = 11;

// ── A LOWER THIRD, AS A TEXT CUE'S LAYOUT ─────────────────────────────────
//
// A name and a role, drawn low on the frame over a TRANSPARENT background so
// the cue sits on a layer over whatever another playlist is showing. It
// animates IN from the cue's own transport clock -- so it scrubs, and two
// outputs showing it cannot disagree -- and OUT when it is told to, or after
// its time on screen, and then the playlist takes it off.
//
// The title and subtitle are the first two lines of the text cue's body, so
// the words are edited, saved and sent over the network the way every text
// cue's already are.
enum class LowerThirdLook : int {
  Bar,     // one solid bar, an accent down its leading edge
  Boxes,   // the title in a box, the subtitle in a smaller one under it
  Line,    // no box: the words over a thick accent rule
  Tag,     // an accent tab for the title, the subtitle beside it
  Glass,   // a translucent band across the frame, a thin accent line on top
  Count
};

enum class LowerThirdMove : int {
  None,
  Fade,
  SlideLeft,    // in from the left edge (out the same way)
  SlideRight,   // in from the right edge
  SlideUp,      // up from below the frame
  Wipe,         // revealed left to right
  Grow,         // the bar draws itself across, then the words arrive
  Typewriter,   // the bar, then the title a letter at a time
  Pop,          // springs up from nothing, overshoots a touch, settles
  Count
};

inline const char* lowerThirdLookLabel(LowerThirdLook look) {
  switch (look) {
    case LowerThirdLook::Bar:   return "bar";
    case LowerThirdLook::Boxes: return "boxes";
    case LowerThirdLook::Line:  return "line";
    case LowerThirdLook::Tag:   return "tag";
    case LowerThirdLook::Glass: return "glass";
    default: break;
  }
  return "bar";
}

inline const char* lowerThirdMoveLabel(LowerThirdMove move) {
  switch (move) {
    case LowerThirdMove::None:       return "cut";
    case LowerThirdMove::Fade:       return "fade";
    case LowerThirdMove::SlideLeft:  return "slide from left";
    case LowerThirdMove::SlideRight: return "slide from right";
    case LowerThirdMove::SlideUp:    return "rise";
    case LowerThirdMove::Wipe:       return "wipe";
    case LowerThirdMove::Grow:       return "grow";
    case LowerThirdMove::Typewriter: return "typewriter";
    case LowerThirdMove::Pop:        return "pop";
    default: break;
  }
  return "cut";
}

// One word each, for the show file and the network.
inline const char* lowerThirdMoveToken(LowerThirdMove move) {
  switch (move) {
    case LowerThirdMove::Fade:       return "fade";
    case LowerThirdMove::SlideLeft:  return "left";
    case LowerThirdMove::SlideRight: return "right";
    case LowerThirdMove::SlideUp:    return "rise";
    case LowerThirdMove::Wipe:       return "wipe";
    case LowerThirdMove::Grow:       return "grow";
    case LowerThirdMove::Typewriter: return "typewriter";
    case LowerThirdMove::Pop:        return "pop";
    default: break;
  }
  return "cut";
}

inline LowerThirdMove lowerThirdMoveFromToken(const std::string& token) {
  for (int i = 0; i < static_cast<int>(LowerThirdMove::Count); ++i) {
    const auto move = static_cast<LowerThirdMove>(i);
    if (token == lowerThirdMoveToken(move)) {
      return move;
    }
  }
  return LowerThirdMove::None;
}

// The colours a lower third can be made of. A short fixed list rather than a
// picker: these are the ones that read over a picture, and a list is a thing
// an operator can step through with one button on a desk.
inline constexpr int kLowerThirdColourCount = 9;
inline SDL_Color lowerThirdColour(int index) {
  static const SDL_Color kColours[kLowerThirdColourCount] = {
    {18, 22, 30, 255},     // ink
    {244, 244, 240, 255},  // paper
    {206, 38, 52, 255},    // red
    {240, 138, 32, 255},   // orange
    {245, 204, 44, 255},   // yellow
    {56, 186, 96, 255},    // green
    {38, 110, 228, 255},   // blue
    {138, 78, 218, 255},   // violet
    {155, 188, 15, 255},   // deckboy
  };
  const int i = ((index % kLowerThirdColourCount) + kLowerThirdColourCount) %
                kLowerThirdColourCount;
  return kColours[i];
}

inline const char* lowerThirdColourName(int index) {
  static const char* const kNames[kLowerThirdColourCount] = {
    "ink", "paper", "red", "orange", "yellow", "green", "blue", "violet", "deckboy",
  };
  const int i = ((index % kLowerThirdColourCount) + kLowerThirdColourCount) %
                kLowerThirdColourCount;
  return kNames[i];
}

struct LowerThirdDesign {
  bool on = false;                          // this text cue is a lower third
  LowerThirdLook look = LowerThirdLook::Bar;
  LowerThirdMove moveIn = LowerThirdMove::SlideLeft;
  LowerThirdMove moveOut = LowerThirdMove::Fade;
  double inSeconds = 0.6;
  double outSeconds = 0.5;
  double holdSeconds = 0.0;   // 0: on screen until OUT is pressed
  int side = 0;               // 0 left, 1 centre, 2 right
  double height = 0.10;       // bottom margin, as a fraction of the frame
  double size = 1.0;          // 0.5-2, on a title a sixteenth of the frame tall
  int bar = 0;                // lowerThirdColour index
  int accent = 6;             // lowerThirdColour index
};

// A PORTAL source: a swarm of particles melted into metaballs, each blob a
// window into deep space with a neon rim that runs green, yellow, pink and
// blue. Outside the blobs the picture is TRANSPARENT, so it is made to sit on
// a layer over another playlist. See MediaEngine::buildPortal.
//
// Every control is 0-1 or a count, and the defaults are the look it arrives
// with -- a show that never touched them gets exactly that.
struct PortalSettings {
  int blobs = 18;        // how many are alive at once, 3-48
  double size = 0.5;     // how big a blob grows, 0-1
  double blend = 0.5;    // how readily neighbours melt together, 0-1
  double outline = 0.4;  // rim thickness, 0-1
  double speed = 1.0;    // how fast they are born, drift and fade, 0.1-3
  double hue = 0.0;      // turns the rim's colours round the wheel, 0-1
};

struct VideoSynthSettings {
  VideoSynthShape shape = VideoSynthShape::Plasma;
  VideoSynthMirror mirror = VideoSynthMirror::Quad;
  VideoSynthPalette palette = VideoSynthPalette::Spectrum;

  double speed = 1.0;         // master rate for every oscillator
  double scale = 1.0;         // spatial frequency: how many features fit
  double warp = 0.35;         // cross-modulation between the two axes

  // Feedback. amount 0 disables the whole path, which also skips keeping the
  // previous frame around.
  double feedbackAmount = 0.55;
  double feedbackZoom = 1.02;    // >1 tunnels inward, <1 blooms outward
  double feedbackRotate = 0.6;   // degrees per frame
  // Audio reactivity. 0 = free-running, which must stay usable: a video synth
  // with no audio playing should still be worth looking at.
  double audioReactivity = 0.5;

  // Internal render resolution, 1 (chunkiest, cheapest) to 5 (finest). This is
  // an AESTHETIC control as much as a performance one -- the 8-bit look comes
  // from big pixels -- so it belongs to the operator rather than being tuned
  // once in code.
  int resolution = 2;

  // ---- Glitch stack --------------------------------------------------------
  // Each is 0 = off, so the synth starts clean and every effect is something
  // the operator turned on deliberately. They stack in a fixed order:
  // pattern -> feedback -> pixel sort -> block glitch -> ASCII.

  // Datamosh-style smear: runs of pixels sorted by brightness within a row,
  // which is what produces the dragged, melted look.
  double pixelSort = 0.0;
  // Displaced scanline bands plus RGB channel separation -- the 8-bit
  // corrupted-frame look.
  double glitch = 0.0;
  // Render the picture as ASCII characters. Not a filter over the image but a
  // REPLACEMENT of it, which is why it is a mode rather than an amount.
  bool ascii = false;
  int asciiCols = 80;              // characters across; height follows aspect
  // Which glyphs the grid is built from. Density is all a cell needs to say,
  // but WHICH marks carry that density changes the character of the whole
  // image, so it is a choice rather than a constant.
  // 0 blocks, 1 ASCII density, 2 symbols, 3 mixed, 4 ASCII raw, 5 sprite sheet
  int asciiCharSet = 0;
  // An imported sheet. Tiles are sliced on a fixed grid and used exactly like
  // glyphs -- chosen by brightness, corrupted by the same cell logic. Kept as
  // a PATH rather than baked into the show so the show file stays small and
  // the operator keeps their own artwork where they put it.
  std::string spriteSheetPath;
  // ---- Tile manipulation ---------------------------------------------------
  // Rotation in 90-degree STEPS by default. Pixel art rotated to an arbitrary
  // angle through a nearest-neighbour sampler tears badly; quarter turns are
  // exact and stay crisp. Free rotation is available for when that roughness
  // is wanted, which for this aesthetic it sometimes is.
  int spriteRotate = 0;        // 0 none, 1 90, 2 180, 3 270, 4 by brightness, 5 free
  double spriteFreeAngle = 0.0;   // degrees per second, only when spriteRotate == 5
  int spriteFlip = 0;          // 0 none, 1 horizontal, 2 vertical, 3 alternating
  double spriteJitter = 0.0;   // 0..1 size variation per cell
  // 0 picks strictly by brightness so the picture reads; 1 picks at random so
  // the grid becomes texture. In between is the interesting part.
  double spriteChaos = 0.0;

  int spriteTileW = 16;
  int spriteTileH = 16;
  // Shuffles which glyph maps to which density. Same set, different
  // handwriting -- and it is a seed rather than live randomness so the look is
  // repeatable and stays put when the show is reopened.
  int asciiShuffle = 0;            // 0 = ordered by density
  // HOW MUCH THE GRID STOPS CARING WHAT THE PICTURE SAYS.
  //
  // 0 picks strictly by brightness, so the image reads. 1 picks at random per
  // cell, so the whole alphabet appears at once and the grid becomes texture.
  // In between is where it is interesting: enough order to read a face,
  // enough disorder that every mark in the set turns up.
  //
  // Sprites have had this since they arrived (spriteChaos) and glyphs never
  // did, which is why a set of seventeen marks could only ever show as many
  // distinct marks as the picture had distinct brightnesses -- a flat area
  // picked ONE. Shuffle does not help: it permutes which mark means which
  // brightness, so a flat area still picks one, just a different one.
  //
  // Position-hashed, not per-frame random. A cell that re-rolls every frame is
  // a flicker rather than a texture -- the same reason spriteChaos is hashed.
  double asciiChaos = 0.0;
  // WHICH FACE to draw the glyphs with. Empty means the automatic chain: the
  // platform's emoji and symbol fonts, tried in order.
  //
  // A named font is tried FIRST and the chain still backs it up per character,
  // so picking a decorative face for its stars does not cost you letters it
  // does not have.
  std::string asciiFontPath;
  // WOBBLE. Each cell rocks as though the character were a card being tilted:
  // a squash across one axis, a stretch across the other and a shear between
  // them, which the eye reads as depth even though nothing is projected.
  //
  // 0 is still. The phase is per CELL, so the grid breathes rather than sliding
  // about as one sheet -- the same thing that makes the startup mascot look
  // alive rather than animated.
  double asciiWobble = 0.0;
  // Where each cell's tilt points.
  //   0 drift  -- its own phase, from its position. Time only.
  //   1 flow   -- along the picture's luma gradient, so characters lean the way
  //               the image does and edges comb the grid.
  //   2 hue    -- from the cell's colour, so the picture steers the tilt by
  //               what it is rather than by where its edges are.
  int asciiWobbleMode = 0;
  // GLITCH TEXT. Marks stacked above, below and through the characters, the
  // way combining diacritics overflow a line -- drawn rather than borrowed
  // from a font, so where they go and how far is ours to decide.
  double asciiZalgoUp = 0.0;       // density of marks above, 0-1
  double asciiZalgoDown = 0.0;     // density of marks below, 0-1
  double asciiZalgoMid = 0.0;      // density of marks through the character
  int asciiZalgoReach = 2;         // how many cells they may climb, 1-6
  double asciiZalgoDrift = 0.0;    // 0 holds still, 1 re-rolls every frame

  // YOUR OWN GLYPHS. When this is not empty the grid is built from exactly
  // these characters, in the order given, mapped darkest-to-brightest. Two
  // characters is a legitimate answer and so is thirty; a set of "01" gives
  // falling code, ". o O @" gives a dot ramp, and a single character gives a
  // field of that character at varying ink.
  std::string asciiGlyphs;

  // PHRASES woven into the field. Separated by |, one shown at a time, moving
  // to a new place every few seconds. The point is words surfacing out of
  // noise and sinking back, which is a thing text mode could suggest but not
  // do -- until now it could only ever say what the picture's brightness said.
  std::string asciiPhrases;
  // Seconds each phrase holds its place before moving. 0 turns them off even
  // when phrases are set, so an operator can mute them without losing them.
  double asciiPhraseHold = 2.5;

  // Ink colour. The old on/off green toggle only offered two of these.
  //   0 picture   colour sampled from the image, 16-colour quantised
  //   1 green     terminal phosphor
  //   2 amber     the other terminal phosphor
  //   3 cyan
  //   4 white
  //   5 palette   locked to whichever hardware palette is selected above,
  //               which is how a real machine would have drawn it
  int asciiInk = 1;
  bool asciiGreen = true;          // legacy; kept so old shows still load

  // CRT: scanlines, phosphor bloom and RGB fringing. Applied at OUTPUT
  // resolution, after everything else, because it models the DISPLAY rather
  // than the signal -- doing it before the upscale would scale the scanlines
  // up with the picture and they would read as stripes instead of a screen.
  double crt = 0.0;
};

// ---------------------------------------------------------------------------
// Tone generator settings, per cue. The audio counterpart of a test pattern:
// what an engineer reaches for to line up a desk, ring out a PA, or prove
// which physical output is which before doors.
//
// Levels are dBFS because that is the unit printed on every meter the operator
// will be looking at. -18 dBFS is the EBU alignment level and the default;
// SMPTE houses use -20. Deliberately NOT full scale: a test tone is played
// into a live PA, and a mistake at 0 dBFS damages ears and drivers.
// ---------------------------------------------------------------------------
enum class ToneVisual {
  None,       // just the text card
  Scope,      // waveform against time
  Lissajous,  // channel 1 against channel 2: phase and polarity at a glance
  Spectrum,   // third-octave bars via a Goertzel bank
};

// What an AUDIO cue puts on the screen while it plays.
//
// An audio cue has always drawn one thing -- the whole file's peaks with a
// playhead crossing them -- which is the right picture for finding your place
// in a track and the wrong one for a house PA, where the screen is showing an
// audience a piece of music rather than showing an operator a file.
//
// Waveform is FIRST so it is the zero value: every show saved before this
// existed loads with the picture it has always had.
enum class AudioVisual {
  Waveform,   // the file's peaks, playhead, in/out and pause points (default)
  Scope,      // live samples against time
  Lissajous,  // left against right -- phase and polarity at a glance
  Spectrum,   // third-octave bars via a Goertzel bank
  Level,      // large peak/RMS meters
  Cover,      // the cue name and clock alone, on a clean field
};

enum class ToneWaveform {
  Sine,      // the line-up tone. 1kHz unless changed
  Pink,      // equal energy per octave -- what a PA is tuned with
  White,     // equal energy per Hz; harsher, for finding rattles
  Sweep,     // slow log sweep, for hearing where a room rings
  Identify,  // walks the channels one at a time, so an engineer can point at
             // a speaker and say which output feeds it
  Fds,       // Chip voice -- FDS or 2A03. A musical source rather than a test
             // signal, and a usable emergency synth. The token stays "Fds" so
             // shows saved before the 2A03 merge still load.
  Sting,     // A short musical gesture for a walk-up: struck, swept and
             // decayed. The only waveform here that ENDS by itself.
};

// THE READER HAS TO KNOW THIS NUMBER, and it did not. The loader clamped the
// saved waveform to 0-4 while the UI cycled through 6, so a 2A03 synth cue
// saved as 5 came back as 4 -- Identify, the channel-walking test tone. The
// cue kept its name and its synth settings, the inspector kept drawing them,
// and SYNTHNOTEON answered "no synth cue is live" because nothing on air was
// a synth any more. It worked all session and only broke on reopen.
//
// Anything that turns an int back into one of these clamps to this, never to
// a literal.
inline constexpr int kToneWaveformCount = 7;

// ---------------------------------------------------------------------------
// FDS wavetable voice.
//
// Implemented from the DOCUMENTED behaviour of the Famicom Disk System sound
// hardware, which is public: a 64-step, 6-bit wavetable carrier whose pitch is
// bent by a separate 32-step modulator table. Nothing here is derived from any
// plugin binary.
//
// The modulator is what gives FDS its character. It does not mix with the
// carrier like FM; it accumulates a signed offset that BENDS the carrier's
// frequency, so a static wavetable still growls and sweeps.
// ---------------------------------------------------------------------------
enum class FdsCarrier {
  Sine,       // the mildest starting point
  Triangle,
  Pulse25,    // hollow and reedy
  Saw,
  Additive,   // first four harmonics, organ-like
};

enum class FdsModulator {
  Off,        // static wavetable, no bend
  Ramp,       // steady rising bend -- the classic FDS sweep
  Square,     // alternating bend, a hard vibrato
  Vibrato,    // gentle symmetric bend
  Growl,      // deep alternating bend, the sound FDS is remembered for
};

// Which chip the voice imitates. Both are implemented from public hardware
// documentation; neither derives from any plugin binary.
enum class SynthChip {
  Fds,    // Famicom Disk System: wavetable carrier bent by a modulator table
  Nes,    // 2A03: the pulse/triangle/noise set most chiptune is actually made of
};

// 2A03 voice selection. These are the actual channels of the chip, and they
// differ in kind rather than in tone -- triangle has no volume control on the
// hardware, and noise is a shift register rather than an oscillator.
enum class NesVoice {
  Pulse,     // duty-cycled square: leads and bass
  Triangle,  // fixed-volume, 4-bit stepped: basslines
  Noise,     // LFSR: percussion and effects
};

// Pulse duty cycles the hardware actually offers. 12.5 and 25 are thin and
// reedy, 50 is hollow, and 75 sounds identical to 25 (inverted phase) -- it is
// included because trackers expose it and people expect to see it.
enum class NesDuty { Eighth, Quarter, Half, ThreeQuarter };

// Tuning systems. Equal temperament is a compromise that lets you change key
// freely at the cost of every interval being slightly wrong; the older systems
// are exactly in tune in one key and progressively worse as you move away.
// Chip music has no reason to be stuck with the compromise.
enum class SynthTuning {
  Equal12,      // the modern default
  Just,         // small whole-number ratios: audibly PURE, and only in one key
  Pythagorean,  // stacked fifths; bright thirds, and one unusable interval
  Meantone,     // renaissance compromise, sweeter thirds than equal
  Equal19,      // 19 steps: better thirds than 12, and genuinely playable
  Equal24,      // quarter tones
  BohlenPierce, // divides a TWELFTH, not an octave -- no octaves at all, which
                // is why it sounds alien rather than merely unusual
};

struct SynthSettings {
  SynthChip chip = SynthChip::Fds;
  SynthTuning tuning = SynthTuning::Equal12;
  // Reference pitch. 440 is the modern standard, 432 the common alternative,
  // and older instruments sat anywhere from 415 upward.
  double referenceHz = 440.0;

  // Shared by every chip: pitch and envelope belong to the NOTE, not to the
  // oscillator that happens to be playing it.
  double noteHz = 220.0;      // A3
  double attackSeconds = 0.01;
  double releaseSeconds = 0.30;
  // Retrigger the envelope this often. 0 = hold one note indefinitely, which
  // is what a drone or a held pad wants.
  double retriggerSeconds = 0.0;

  // -- FDS ------------------------------------------------------------------
  FdsCarrier carrier = FdsCarrier::Sine;
  FdsModulator modulator = FdsModulator::Ramp;
  int modDepth = 16;          // 0-63, the hardware gain range
  double modRatio = 0.5;      // modulator frequency as a ratio of the note

  // -- 2A03 -----------------------------------------------------------------
  NesVoice nesVoice = NesVoice::Pulse;
  NesDuty nesDuty = NesDuty::Half;
  // The chip's noise has a short mode whose period is so brief it reads as
  // pitched metal rather than hiss. Trackers call it "periodic noise".
  bool nesNoiseShort = false;
  // 4-bit output like the hardware. Off is cleaner but wrong: the steps ARE
  // the sound, and smoothing them makes a chiptune voice sound like a synth
  // pretending.
  bool nesQuantise = true;
};

struct ToneSettings {
  ToneWaveform waveform = ToneWaveform::Sine;
  double frequencyHz = 1000.0;    // Sine only. 1kHz is the convention
  double levelDbfs = -18.0;       // EBU alignment level
  double sweepLowHz = 20.0;       // Sweep only
  double sweepHighHz = 20000.0;
  double sweepSeconds = 10.0;
  // Which output channel to feed. -1 = every channel. Identify overrides this
  // by walking channels itself.
  int channel = -1;
  double identifySecondsPerChannel = 2.0;

  // ── THE STING ─────────────────────────────────────────────────────────
  //
  // Defaults chosen to be usable on the first press rather than to be
  // neutral: a rising major gesture around A4 over three quarters of a
  // second, which is what a walk-up sting sounds like before anybody touches
  // a slider. A default of "silence until configured" is a feature nobody
  // discovers.
  double stingPitchHz = 440.0;      // where it starts
  double stingSeconds = 0.75;       // the whole gesture
  double stingSweepSemitones = 7.0; // + rises, - falls, 0 is a struck note
  double stingBody = 0.45;          // 0 a pure sine, 1 a bright stack

  // On-screen display. These are DIAGNOSTIC first and decorative second --
  // each one answers a question an engineer actually asks during a check.
  //   Scope      is the signal clipping, and is it the shape I asked for
  //   Lissajous  are these two channels in phase, and is either inverted
  //   Spectrum   what is the room or the desk doing to the signal
  // Separate from the style so the operator can kill the display outright
  // without losing which style they had chosen.
  bool visualEnabled = true;
  ToneVisual visual = ToneVisual::Scope;

  // Only meaningful when waveform == ToneWaveform::Fds. Named for what it is
  // now that it covers more than one chip.
  SynthSettings synth;
};

// Stage/speaker timer settings, per cue. Ported from the owner's SpeakerTimer
// (C#/WPF) -- see docs/TIMER_PLAN.md. Thresholds are SECONDS REMAINING, so
// amber 60 means "turn amber with a minute left".
// Timer display mode. Matches the three stagetimer.io offers, because they are
// the three a show actually needs: how long is left, how long you have run,
// and what time it is now.
enum class TimerMode {
  Countdown,   // duration -> 0, then overtime
  CountUp,     // 0 -> duration
  TimeOfDay,   // wall clock
};

// Which face the clock is drawn with. Seven-segment is the default because it
// never depends on installed fonts; the others use the UI faces already loaded.
// Which face the clock is drawn with. Both are BUNDLED GEOMETRY, not TTFs: the
// frame is built in the engine, which has no access to the app's fonts, and a
// stage screen must render identically wherever it runs regardless.
enum class TimerFace {
  SevenSegment,  // LED-panel look, chunky, maximum legibility at distance
  Blocky,        // 5x7 dot-matrix, squarer and more retro
  Typeface,      // a real font -- the app's own, or any face the operator picks
};

struct TimerSettings {
  int durationSeconds = 300;    // 5:00
  int amberSeconds = 60;        // <= this many left: amber
  int redSeconds = 15;          // <= this many left: red
  bool countUpAfterZero = true; // keep counting as +m:ss instead of stopping
  bool blinkAtZero = true;      // flash once time is up
  TimerMode mode = TimerMode::Countdown;
  TimerFace face = TimerFace::SevenSegment;
  // WHICH FACE, when `face` is Typeface. Empty is the app's own bundled sans,
  // so the mode works without picking anything and looks the same on all three
  // platforms. A named font is tried first and the platform chain still backs
  // it up per character -- the same contract the character grid uses.
  //
  // The geometric faces above are NOT a limitation and are staying: seven
  // segments render identically on every machine whatever is installed, which
  // is what a clock on a stage screen wants. This is for the times an event
  // has a typeface and the clock is expected to be in it.
  std::string fontPath;
  bool showProgressBar = true;  // length is readable from further back than digits
  bool messageIsUrgent = false; // red rather than white: the "wrap up NOW" state
  std::string message;          // optional line under the clock

  // Custom colours, packed 0xRRGGBB. -1 means "use the built-in default for
  // this state", which is what every existing show carries -- so adding these
  // changes nothing until an operator sets one.
  int colorNormal = -1;         // default white
  int colorAmber = -1;          // default amber
  int colorRed = -1;            // default red
  int colorBackground = -1;     // default black

  // Audible cues. A speaker looking at the audience is not looking at the
  // clock, which is the whole reason stage timers chime.
  bool chimeAtAmber = false;
  bool chimeAtRed = false;
  bool chimeAtZero = true;
  // Which chime. Six because a stage timer often shares a room with other
  // cues and the operator needs one that does not collide with them.
  int chimeSound = 0;
  // Optional logo drawn above the clock (event branding, sponsor mark). Empty
  // = none. Decoded once and cached; see MediaEngine::timerLogoPixels.
  std::string logoPath;
  int logoHeightPercent = 18;   // of frame height; width follows the aspect
};

// What happens when a cue reaches its end. "Inherit" defers to the deck-level default.
// Used by MediaEngine to decide post-playback behavior and by the cue list UI to
// show the end-action badge icon. Serialized as integer index.
enum class CueEndAction { Inherit, Stop, Loop, PauseOnLast, AutoNext };

// Whether the next cue goes by itself, and from when. See Cue::continueMode.
//
// Distinct from CueEndAction::AutoNext, which is about what THIS cue does when
// its media runs out. A continue is about the NEXT cue, fires whether or not
// this one has ended, and is what lets a sequence of cues run from one GO.
enum class CueContinueMode {
  DoNotContinue,   // the operator fires the next one
  AutoContinue,    // post-wait counted from when this cue STARTS
  AutoFollow,      // post-wait counted from when this cue ENDS
};

inline const char* cueContinueModeToken(CueContinueMode mode) {
  switch (mode) {
    case CueContinueMode::AutoContinue: return "auto-continue";
    case CueContinueMode::AutoFollow:   return "auto-follow";
    case CueContinueMode::DoNotContinue: break;
  }
  return "none";
}

inline const char* cueContinueModeLabel(CueContinueMode mode) {
  switch (mode) {
    case CueContinueMode::AutoContinue: return "Auto-continue";
    case CueContinueMode::AutoFollow:   return "Auto-follow";
    case CueContinueMode::DoNotContinue: break;
  }
  return "Do not continue";
}

inline CueContinueMode cueContinueModeFromToken(const std::string& token) {
  if (token == "auto-continue") return CueContinueMode::AutoContinue;
  if (token == "auto-follow")   return CueContinueMode::AutoFollow;
  return CueContinueMode::DoNotContinue;
}

// Current transport state of a deck's active cue. Drives the play/pause/stop
// buttons in app_render_control.ipp and the MediaEngine decode loop.
enum class TransportState {
  Stopped,   // no decode running; preview shows freeze frame or black
  Paused,    // decode paused; last frame held on screen
  Playing    // active decode; frames streaming from ffmpeg
};

// How one cue transitions into the next. Applied at the deck level
// (Deck::transitionStyle) or overridden per-cue (Cue::cueTransitionStyle).
// The output compositor in app_render_output.ipp blends layers accordingly.
// HOW ONE CUE BECOMES THE NEXT.
//
// Appended to, never reordered: the value is serialised as a token, but the
// inspector's cycle and every saved show still expect the first three to mean
// what they always meant.
//
// All of them are composited in app_render_output.ipp, which is where every
// cue kind meets as a frame -- so a transition works the same on a video, a
// slide, a pattern, a camera or an NDI source without knowing which it is.
enum class TransitionStyle {
  Cut,         // instant switch — no blending
  Crossfade,   // gradual alpha blend between outgoing and incoming
  DipBlack,    // fade out to black, then fade in the new cue
  DipWhite,    // the same through white: a flash rather than a blink
  PushLeft,    // the outgoing picture slides off left, the new one follows it in
  PushRight,
  PushUp,
  PushDown,
  WipeLeft,    // a hard edge travels across; neither picture moves
  WipeRight,
  WipeUp,
  WipeDown,
  Iris,        // a circle opens from the centre of the frame
  Portal,      // the Portal's melting blobs open through the old picture, neon rim
  Count
};

// How a cue's source frame maps to the output resolution.
// Per-cue (Cue::scaleMode) and per-composite-slot (CompositeSlot::scaleMode).
// The output renderer reads this to compute the destination rect.
enum class ScaleMode {
  Fit,         // letterbox: fit entire image, maintain aspect ratio
  Fill,        // fill screen and crop, maintain aspect ratio
  Stretch,     // fill screen, ignore aspect ratio (distort)
  Unscaled     // 1:1 pixel mapping (no scaling)
};

// ---------------------------------------------------------------------------
// CompositeSlot — One sub-region inside a Composite cue layout.
//
// A Composite cue contains 1–4 CompositeSlots, each pointing to a media
// source and positioned in normalized coordinates (0–1) relative to the
// output frame. The compositor in app_render_output.ipp iterates these
// slots to blit each source into its designated rectangle.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// MasterAssignment — one line of a master cue: "deck 2 plays cue 7".
//
// A master cue is an Analog Way LiveCore MASTER MEMORY. A deck is a
// destination holding its own content, a cue is that destination's memory, and
// a master recalls one memory on each destination at once.
//
// The cue is referenced BY ID, not by index: indices move the moment somebody
// reorders or deletes a cue above the target, and a master that silently
// repoints at its neighbour is worse than one that reports a broken link.
// ---------------------------------------------------------------------------
// Which geometry parameter each of Cue::geometryLfo drives.
//
// SCALE X ALONE PULSES THE WHOLE SIZE. A breathing picture is what almost
// everybody reaching for a scale LFO wants, and making them arm two identical
// oscillators to get it would be a trap; give height its own and the two move
// separately, which is how squash-and-stretch is made.
// Where the geometry oscillators sit in the packed LFO id the inspector hands
// its controls (effectIndex * 8 + slot for an effect). Far above any effect.
constexpr int kGeometryLfoPackBase = 1 << 20;

enum CueGeometryLfoSlot : int {
  kGeoLfoOffsetX = 0,
  kGeoLfoOffsetY,
  kGeoLfoScaleX,
  kGeoLfoScaleY,
  kGeoLfoRotation,
  kGeoLfoCropLeft,
  kGeoLfoCropRight,
  kGeoLfoCropTop,
  kGeoLfoCropBottom,
  kGeoLfoCount
};

struct MasterAssignment {
  int deckIndex = -1;         // which destination
  std::string cueId;          // which of that deck's cues, by id
  bool bypassed = false;      // skip this deck when the master fires
};

// ---------------------------------------------------------------------------
// How a Text cue moves.
//
// Every one of these is driven by the cue's transport position, not a wall
// clock, so a text cue scrubs, pauses and loops with everything else -- and
// two outputs showing the same deck cannot drift apart.
// ---------------------------------------------------------------------------
enum class CueTextAnimation {
  None,        // it just sits there, which is what a title card wants
  FadeIn,      // up over the first second
  Typewriter,  // a character at a time
  ScrollUp,    // credits: bottom to top
  Crawl,       // a news ticker: right to left, on one line
  Pulse,       // breathes, for a holding slide nobody should mistake for frozen
  Wobble,      // every character on its own little orbit, like the TEXT MODE
               // effect -- asked for by name, and the one animation here that
               // moves the letters against each other rather than the block
               // as a whole
};

inline const char* cueTextAnimationToken(CueTextAnimation a) {
  switch (a) {
    case CueTextAnimation::FadeIn:     return "fade";
    case CueTextAnimation::Typewriter: return "typewriter";
    case CueTextAnimation::ScrollUp:   return "scroll";
    case CueTextAnimation::Crawl:      return "crawl";
    case CueTextAnimation::Pulse:      return "pulse";
    case CueTextAnimation::Wobble:     return "wobble";
    case CueTextAnimation::None:       break;
  }
  return "none";
}

// How a lower third arrives and leaves. Named rather than numbered on the
// row, because `3` tells an operator nothing and `slide` tells them
// everything they need.
inline const char* lowerThirdStyleLabel(int style) {
  switch (style) {
    case 1: return "fade";
    case 2: return "rise";
    case 3: return "slide";
    case 4: return "wipe";
    default: return "none (instant)";
  }
}
inline int lowerThirdStyleFromToken(const std::string& token) {
  if (token == "fade")  return 1;
  if (token == "rise")  return 2;
  if (token == "slide") return 3;
  if (token == "wipe")  return 4;
  return 0;
}
inline constexpr int kLowerThirdStyleCount = 5;

inline const char* cueTextAnimationLabel(CueTextAnimation a) {
  switch (a) {
    case CueTextAnimation::FadeIn:     return "Fade in";
    case CueTextAnimation::Typewriter: return "Typewriter";
    case CueTextAnimation::ScrollUp:   return "Scroll up";
    case CueTextAnimation::Crawl:      return "Crawl";
    case CueTextAnimation::Pulse:      return "Pulse";
    case CueTextAnimation::Wobble:     return "Wobble";
    case CueTextAnimation::None:       break;
  }
  return "Still";
}

// The window on the hearth wall, named. The inspector row says what the view
// IS rather than showing a number the operator has to learn, and the token form
// is what `--pattern-dump fireside:snow` takes.
inline const char* firesideViewLabel(int view) {
  switch (view) {
    case 1: return "garden";
    case 2: return "rain";
    case 3: return "snow";
    case 4: return "sea";
    default: return "no window";
  }
}
inline int firesideViewFromToken(const std::string& token) {
  if (token == "garden") return 1;
  if (token == "rain")   return 2;
  if (token == "snow")   return 3;
  if (token == "sea")    return 4;
  return 0;
}
// One past the last view, so a cycling control wraps without repeating the
// list in two places.
inline constexpr int kFiresideViewCount = 5;

inline CueTextAnimation cueTextAnimationFromToken(const std::string& t) {
  if (t == "fade")       return CueTextAnimation::FadeIn;
  if (t == "typewriter") return CueTextAnimation::Typewriter;
  if (t == "scroll")     return CueTextAnimation::ScrollUp;
  if (t == "crawl")      return CueTextAnimation::Crawl;
  if (t == "pulse")      return CueTextAnimation::Pulse;
  if (t == "wobble")     return CueTextAnimation::Wobble;
  return CueTextAnimation::None;
}

// ---------------------------------------------------------------------------
// AUDIO CROSSPOINT.
//
// One cell of the matrix: how much of a cue's source channel reaches one
// channel of the deck's audio device. This is QLab's signature audio feature
// and the thing sound designers name first, and it is what makes a 64-channel
// Dante or ASIO interface worth having -- 64 channels of transport behind a
// fixed stereo pair is 62 channels nobody can reach.
//
// SPARSE, deliberately. A show with one cue routed to outs 7-8 should carry
// two crosspoints, not a 2x64 grid of zeroes in every cue record.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ONE LAYER OF AN OUTPUT.
//
// A playlist, WHERE it sits in the frame, and HOW it is blended over what is
// under it. It was a bare deck index, so every layer filled the frame and the
// top one simply hid the rest -- which makes a lower-thirds playlist over a
// camera impossible, and is what James meant by "no way to mix or scale them".
//
// GEOMETRY IS FRACTIONS OF THE OUTPUT, never pixels: a layout built against a
// 1080 projector has to mean the same thing on a 2160 one. 0,0,1,1 is the
// whole frame, which is what every existing layer becomes.
//
// The BLEND is where VJ mode's blend modes now live. They were a property of
// "the B deck" -- but a B deck IS a layer, so this is the same idea with the
// special case removed.
// ---------------------------------------------------------------------------
// One window of the multiview, and what is drawn over it.
//
// `source` is a small language rather than an int so a show file stays
// readable and so a new kind of source (an output's finished composite, a
// return feed) can be added without renumbering anything that already exists:
//   ""           an empty window -- a hole, kept so the grid does not reflow
//   "programme"  what the programme output is showing
//   "deck:<n>"   playlist n, zero based
struct MultiviewTile {
  std::string source = "programme";
  // Off by default, both of them. A multiview whose every window arrives
  // wearing meters and safe areas is a multiview you cannot see the pictures
  // in; these are for the one or two windows that need them.
  bool vuMeter = false;
  bool safeAreas = false;
  bool label = true;
};

struct OutputLayer {
  int deckIndex = 0;
  float x = 0.0f;
  float y = 0.0f;
  float w = 1.0f;
  float h = 1.0f;
  std::string blendMode = "dissolve";   // dissolve | add | multiply | screen ...
  // -- MAPPING, ON THE LAYER ---------------------------------------------
  //
  // The output's warp lines a projector up with its screen. THIS one puts a
  // layer onto a surface within that screen, which is what mapping several
  // objects from one projector means. They compose: the layer is placed
  // first, then the output's correction is applied over the whole raster.
  //
  // NORMALISED to the layer's own rect, unlike the output's pixel offsets. A
  // layer can be moved and resized, and corner offsets in pixels would mean
  // something different the moment it was.
  bool warpEnabled = false;
  float warpTopLeftX = 0.0f;
  float warpTopLeftY = 0.0f;
  float warpTopRightX = 0.0f;
  float warpTopRightY = 0.0f;
  float warpBottomRightX = 0.0f;
  float warpBottomRightY = 0.0f;
  float warpBottomLeftX = 0.0f;
  float warpBottomLeftY = 0.0f;
};

struct AudioCrosspoint {
  int source = 0;      // 0 = the cue's left, 1 = its right
  int dest = 0;        // 0-based channel of the deck's audio device
  float gain = 1.0f;   // 0..1, linear
};

struct CompositeSlot {
  std::string id;                           // unique slot identifier (UUID)
  std::string name;                         // operator-facing label ("Slot 1")
  std::string sourceType = "media";         // "media" | "camera" | "ndi" etc.
  std::string source;                       // file path, device name, or NDI source
  bool visible = true;                      // false = skip during composite render
  bool audioEnabled = false;                // route this slot's audio to output mix
  ScaleMode scaleMode = ScaleMode::Fit;     // how source maps into the slot rect
  float normX = 0.0f;                       // left edge (0–1 fraction of output width)
  float normY = 0.0f;                       // top edge  (0–1 fraction of output height)
  float normW = 0.5f;                       // width     (0–1 fraction of output width)
  float normH = 0.5f;                       // height    (0–1 fraction of output height)
};

// ---------------------------------------------------------------------------
// Datamosh flavours, weakest to strongest. Plain ints rather than an enum class
// because Cue is serialized field-by-field and an unknown future value must
// clamp rather than become an invalid enumerator.
//
// The differences are MEASURED, not stylistic preference:
//   SUBTLE  - H.264. A P-frame may legally carry INTRA-coded macroblocks, so
//             x264 refreshes regions on its own and the smear heals within a
//             few frames, fastest on high-detail content. No x264 switch
//             suppresses intra MBs, so this is a floor, not a tuning problem.
//   CLASSIC - MPEG-4 Part 2. No in-loop deblocking, no self-healing: the old
//             picture is dragged through the new motion and stays smeared.
//             This is the look people mean by "datamosh".
//   EXTREME - CLASSIC with a coarser quantiser and a much shorter GOP, so the
//             blocks are bigger and a fresh smear starts roughly every second
//             instead of every five.
// ---------------------------------------------------------------------------
inline constexpr int kDatamoshLookSubtle  = 0;
inline constexpr int kDatamoshLookClassic = 1;
inline constexpr int kDatamoshLookExtreme = 2;
inline constexpr int kDatamoshLookCount   = 3;

// ---------------------------------------------------------------------------
// Cue — A single playback item in a deck's cue list.
//
// This is the central content unit. Each Cue holds everything needed to:
//   1. Decode media  → MediaEngine reads path, kind, fps, duration, codec info
//   2. Render output → compositor reads scale/offset/crop/rotation/chroma/color
//   3. Drive transport → end action, loop, pause points, speed, fade in/out
//   4. Serialize → all fields saved/loaded in app/app_project_state.ipp
//   5. Display in UI → name, colorTag, notes shown in cue list + inspector
//
// Fields are grouped by alignment to minimize struct padding.
// ---------------------------------------------------------------------------
struct Cue {
  // -- 8-byte aligned: strings -----------------------------------------------
  std::string id;                          // internal UUID (generated on import)
  std::string cueId;                       // operator-facing short ID (max 6 chars, shown in cue list)
  std::string path;                        // file path, URL, or device for media source
  std::string name;                        // display name (defaults to filename on import)
  std::string formatName;                  // ffprobe container format (e.g. "mov,mp4")
  std::string videoCodec;                  // ffprobe video codec name (e.g. "h264")
  std::string audioCodec;                  // ffprobe audio codec name (e.g. "aac")
  std::string gotoTarget;                  // cue ID to jump to on AutoNext end action
  // FIRESIDE, as a source rather than a test card. How hard it burns and how
  // much it throws off -- the two things anybody actually wants to change
  // about a fire behind a panel.
  double firesideIntensity = 1.0;          // 0.2 embers .. 2.0 roaring
  int firesideSparks = 34;                 // 0 none .. 160
  // What is through the window on the hearth wall: 0 none, 1 garden, 2 rain,
  // 3 snow, 4 sea. A wall with a window is a room; a wall without one is a
  // texture.
  int firesideView = 0;

  // A Text cue. The body is the operator's own words, newlines and all.
  std::string textBody = "DECKBOY";

  // A DMX cue. The channel spec is the operator's own text -- "1=255,
  // 10-14=64" -- kept as typed so it reads back the way it was written.
  std::string dmxChannels;
  std::string dmxHost = "255.255.255.255";   // Art-Net's broadcast default

  // A Script cue's lines, newline-separated. Edited in the same multi-line
  // editor the code source uses -- a script that has to be typed on one line
  // is a script nobody writes.
  std::string scriptText;

  // A Timecode cue. start | stop | jam.
  std::string tcAction = "start";

  // A Network cue's message. Host and payload are text because that is what
  // an operator types and what a show file should carry; the port is a number
  // because it is one.
  std::string netProtocol = "osc";          // osc | udp | tcp
  std::string netHost = "127.0.0.1";
  std::string netAddress = "/deckboy/go";   // the OSC path; unused by udp/tcp
  std::string netPayload;

  // A MIDI cue's message. The KIND is kept as its token rather than an enum
  // so core/types.hpp stays free of any platform header -- the encoder that
  // turns these into bytes lives in platform/midi.hpp and is a pure function
  // (see encodeOutMessage), which is what lets it be tested without a desk.
  std::string midiPortName;                // the port asked for, by NAME
  std::string midiMessage = "note-on";     // note-on|note-off|cc|program|msc-*|raw
  std::string midiRawHex;                  // "90 3C 7F", for the raw kind
  std::string mscCue;                      // "12.5" -- text, because the dots matter
  std::string mscList;

  // A Target cue's victim, by ID for the same reason a MasterAssignment is:
  // an index repoints at the neighbour the moment anything above it moves.
  std::string targetCueId;
  std::string cueTransitionStyle;          // per-cue override: "cut"/"crossfade"/"dipblack" (empty=inherit)
  std::string lowerThirdText;              // primary text line for LowerThird cue kind
  std::string lowerThirdSubtext;           // secondary text line for LowerThird cue kind
  std::string pipTargetCue;               // cue ID whose output is the PiP background
  std::string pipSourceType;              // PiP source kind ("media"/"camera"/"ndi")
  std::string attachedLowerThirdCue;      // cue ID of an attached lower-third overlay
  std::string attachedPipCue;             // cue ID of an attached PiP overlay
  std::string compositeLayoutPreset;      // preset name ("2up"/"quad"/"7030") for Composite cue
  std::string compositeAudioSlotId;       // which CompositeSlot's audio to route to output
  std::string colorTag;                   // operator color label for cue list ("red","blue",etc.)
  std::string notes;                      // free-form operator notes shown in inspector
  std::string cueNumber;                  // traditional show cue number (e.g. "Q1.5")
  std::string subtitlePath;               // path to external .srt file (empty = use embedded)
  std::string subtitleStreamId;           // embedded subtitle stream index (e.g. "0:s:0")

  // -- 8-byte aligned: vectors ------------------------------------------------
  std::vector<CompositeSlot> compositeSlots; // sub-regions for Composite cue layout
  std::vector<MasterAssignment> masterAssignments;  // Master cue: what it fires
  // The audio matrix. EMPTY MEANS "use audioOutputPair", which is what every
  // show saved before this existed says -- and which reproduces exactly what
  // those shows did. Nothing is migrated on load; the fallback IS the old
  // behaviour, so a show that never touches the matrix behaves identically
  // whether it is opened by this build or an older one.
  std::vector<AudioCrosspoint> audioMatrix;
  std::vector<double> pausePoints;           // timecodes (seconds) where playback auto-pauses
  // Named jump marks inside a clip (PLAYDECK-style). Distinct from pausePoints,
  // which STOP playback: a marker is somewhere you can jump TO. Kept sorted by
  // time so "next marker" is a scan forward rather than a search.
  std::vector<double> markerSeconds;
  std::vector<std::string> markerNames;      // parallel to markerSeconds

  // -- 8-byte aligned: doubles + uint64 ----------------------------------------
  double duration = 0.0;                   // total media duration in seconds (from ffprobe)
  double fps = 30.0;                       // frame rate (from ffprobe; default 30 for stills)
  double fadeInSeconds = 0.0;              // visual+audio fade-in duration at cue start
  double fadeOutSeconds = 0.0;             // visual+audio fade-out duration before cue end
  double inPointSeconds = 0.0;            // trim: playback starts here (0 = beginning)
  double outPointSeconds = 0.0;           // trim: playback ends here (0 = use full duration)
  double triggerTimecodeSeconds = -1.0;   // SMPTE timecode to auto-trigger this cue (-1 = disabled)
  // Wall-clock auto-start: seconds since local midnight at which this cue fires
  // (-1 = disabled). Distinct from triggerTimecodeSeconds, which chases INCOMING
  // timecode -- this one needs no external source, which is what makes
  // unattended playback possible.
  double scheduledStartSeconds = -1.0;
  // Runtime only: set once the schedule has fired so it cannot re-fire every
  // tick for the rest of that second, and cleared at midnight rollover.
  bool scheduledStartFired = false;
  // ── THE SEQUENCING SPINE ───────────────────────────────────────────────
  //
  // What turns a list you fire into a list that runs itself, and the thing a
  // theatre operator means by a cue list. All three default to "do what
  // Deckboy always did", so a show saved before they existed behaves
  // identically: no waits, and nothing follows anything.
  //
  // PRE-WAIT delays the cue's own start after GO. POST-WAIT is measured from
  // the point the continue is armed, and CONTINUE says whether the next cue
  // goes at all -- and if so, from WHEN:
  //
  //   AutoContinue  counts the post-wait from when this cue STARTS, so a
  //                 sequence is laid out in absolute time from the GO.
  //   AutoFollow    counts it from when this cue ENDS, so the next thing
  //                 happens after this one is done however long it took.
  //
  // That distinction is the whole reason both exist: the first is for a
  // designed sequence, the second for "and then the next thing".
  double preWaitSeconds = 0.0;            // delay between GO and this cue starting
  double postWaitSeconds = 0.0;           // delay before the continue fires
  double stillDurationSeconds = 0.0;      // display time for Image/Pattern/Browser cues
  double cueTransitionSeconds = -1.0;     // per-cue transition duration override (-1 = inherit)
  double playbackSpeed = 1.0;             // speed multiplier (0.25–4.0; 1.0 = normal)
  std::uintmax_t sizeBytes = 0;           // file size in bytes (from ffprobe, for display)

  // -- 4-byte aligned: floats -------------------------------------------------
  // Geometry transforms applied by the output compositor (normalized/degrees)
  float outputScaleX = 1.0f;              // horizontal scale (1.0 = 100%)
  float outputScaleY = 1.0f;              // vertical scale   (1.0 = 100%)
  float outputOffsetX = 0.0f;             // horizontal offset (fraction of output width)
  float outputOffsetY = 0.0f;             // vertical offset   (fraction of output height)
  float outputRotationDegrees = 0.0f;     // clockwise rotation in degrees
  float cropLeft = 0.0f;                  // crop fraction from left edge   (0–1)
  float cropRight = 0.0f;                 // crop fraction from right edge  (0–1)
  float cropTop = 0.0f;                   // crop fraction from top edge    (0–1)
  float cropBottom = 0.0f;               // crop fraction from bottom edge (0–1)
  // Chroma key (green-screen removal) parameters
  float chromaKeyTolerance = 60.0f;       // color distance threshold for key
  float chromaKeySoftness = 20.0f;        // edge softness gradient width
  // Color correction (applied per-pixel in the output compositor)
  float brightness = 1.0f;                // 0.0 (black) to 2.0 (overbright)
  float contrast = 1.0f;                  // 0.0 (flat gray) to 2.0 (high contrast)
  float saturation = 1.0f;                // 0.0 (grayscale) to 2.0 (hyper-saturated)
  float hueShift = 0.0f;                  // -180 to +180 degrees hue rotation

  // -- 4-byte aligned: ints + enums -------------------------------------------
  int width = 0;                           // source video width  (pixels, from ffprobe)
  int height = 0;                          // source video height (pixels, from ffprobe)
  int audioChannels = 0;                   // number of audio channels (from ffprobe)
  int audioSampleRate = 0;                 // audio sample rate in Hz (from ffprobe)
  int lowerThirdBgAlpha = 180;             // background bar opacity for LowerThird (0-255)
  // HOW IT ARRIVES AND LEAVES. 0 none (instant), 1 fade, 2 rise, 3 slide,
  // 4 wipe. `none` is the struct default on purpose: a show written before
  // this popped, and it must go on popping rather than quietly acquiring an
  // animation nobody asked for. A lower third MADE today arrives as `rise`
  // -- see applyDeckDefaultsToCue.
  int lowerThirdStyle = 0;
  // Seconds for the move, each way. 0 is instant whatever the style says,
  // which is the one value that has to keep working.
  double lowerThirdAnimSeconds = 0.4;
  int loopCount = 0;                       // number of times to loop (0 = infinite when loop=true)
  CueKind kind = CueKind::Video;           // discriminator — see CueKind enum above
  CueEndAction endAction = CueEndAction::Inherit; // what to do when playback finishes
  // DoNotContinue is the default because it is what every existing show does.
  CueContinueMode continueMode = CueContinueMode::DoNotContinue;
  // The LED tile size the panel map is drawn to. 128x128 is the common one,
  // but a wall that is not made of those is exactly the wall that needs a map,
  // and mapping a 168px panel as 128 puts every label in the wrong place.
  // Text size as a PERCENT OF THE RASTER HEIGHT, not points. A cue that reads
  // right on a 1080 screen then has to read right on a 2160 one, and a point
  // size cannot promise that.
  double textSizePct = 12.0;
  double textSpeed = 1.0;                  // multiplier on whatever it does

  // How long the levels take to arrive. 0 is a snap, which is what a
  // blackout wants.
  double dmxFadeSeconds = 0.0;

  // Where a jam puts the clock. Seconds, because that is what the rest of the
  // app keeps time in; the inspector shows it as a timecode.
  double tcJamSeconds = 0.0;

  int textAlign = 1;                       // 0 left, 1 centre, 2 right
  int textBgAlpha = 0;                     // 0 is over the picture, 255 is a card
  CueTextAnimation textAnimation = CueTextAnimation::None;
  int dmxUniverse = 0;                     // Art-Net port address, 0-32767
  int dmxPort = 6454;                      // 6454 is Art-Net's registered port

  // 53000 is QLab's OSC port, which makes the commonest thing somebody wants
  // to do with this cue work without configuring anything.
  int netPort = 53000;
  int midiChannel = 1;                     // 1-16, as an operator counts them
  int midiData1 = 60;                      // note, controller, or program number
  int midiData2 = 127;                     // velocity or controller value
  int mscDevice = 0;                       // 0-127; 127 addresses every device
  int ledPanelWidth = 128;
  int ledPanelHeight = 128;
  // A Fade cue aims with the SAME fields a Target cue does -- targetDeckIndex
  // says which deck, and it needs no cue id because everything it can move is
  // a deck-wide or master value.
  double fadeOverSeconds = 3.0;            // how long the ramp takes
  double fadeToValue = 0.0;                // where it ends up, 0-1
  int targetDeckIndex = -1;                // Target cue: which deck its victim is on
  CueTargetVerb targetVerb = CueTargetVerb::Start;  // Target cue: what it does to it
  CueFadeWhat fadeWhat = CueFadeWhat::DeckOpacity;  // Fade cue: what it ramps
  CueFadeCurve fadeCurve = CueFadeCurve::Linear;    // Fade cue: the shape of it
  ScaleMode scaleMode = ScaleMode::Fit;    // how source maps to output — see ScaleMode enum

  // -- 4-byte aligned: SDL_Color (RGBA) ----------------------------------------
  SDL_Color color {48, 98, 48, 255};                 // cue list row tint (DMG green default)
  SDL_Color compositeBackgroundColor {18, 24, 18, 255}; // background fill for Composite cue
  SDL_Color chromaKeyColor {0, 255, 0, 255};          // target color for chroma key removal
  SDL_Color textColor {255, 255, 255, 255};           // a Text cue's ink

  // -- 1-byte aligned: bools ---------------------------------------------------
  bool hasAudio = false;          // true if ffprobe detected an audio stream
  bool audioEnabled = true;       // operator toggle — mute this cue's audio
  // Per-cue gain trim in dB, applied live in the audio thread. Range is
  // kCueAudioGainMinDb..kCueAudioGainMaxDb — never hardcode it at a clamp site.
  float audioGainDb = 0.0f;
  // THE CUE'S AUDIO EFFECT STACK, the ear's half of what `effects` is for the
  // eye. Evaluated in order, between the operator's gain decisions and the
  // peak limiter -- see audio_effects.hpp for why that is the right place.
  // Serialised as ONE field like the picture stack, for the same reason.
  std::vector<deckboy::audiofx::AudioEffect> audioEffects;
  float audioPan = 0.0f;          // stereo balance: -1 full left .. +1 full right (0 = center)
  bool audioMono = false;         // downmix this cue to mono (mono sources / mono PA)
  // Independent audio fades: -1 = follow the visual fadeIn/OutSeconds
  // (default), 0 = no audio fade, >0 = explicit seconds. Lets audio duck
  // early under a long video tail, or hold under a fast visual cut.
  float audioFadeInSeconds = -1.0f;
  float audioFadeOutSeconds = -1.0f;
  // Which pair of the deck audio device's outputs this cue's (post gain/pan/
  // mono) stereo lands on: 0 = outs 1-2, 1 = outs 3-4, ... Pairs beyond the
  // device's opened channel count clamp back to 1-2 at play time.
  int audioOutputPair = 0;
  // What this cue draws while it plays. Only consulted for Audio cues -- a
  // video cue's picture is its own.
  AudioVisual audioVisual = AudioVisual::Waveform;

  // -- The picture as a surface in space ----------------------------------
  //
  // Deckboy is a 2D compositor: a cue is a textured quad. This bends that
  // quad into a grid and pushes each vertex out of the plane by the
  // brightness of the picture underneath it, so the video becomes a
  // landscape of itself, turning slowly under a viewpoint.
  //
  // Real geometry, not a shader trick: the same SDL_RenderGeometry call the
  // perspective warp already uses, with more vertices and a Z that feeds the
  // projection. Off by default and free at zero height, so a show that never
  // touches it is unchanged.
  bool meshEnabled = false;
  float meshHeight = 0.35f;     // 0-1: how far bright pixels stand out
  float meshTiltX = 0.25f;      // -1..1 pitch
  float meshTiltY = 0.0f;       // -1..1 yaw
  float meshSpin = 0.15f;       // 0-1: how fast the yaw drifts on its own
  int meshGrid = 48;            // cells across; the cost is this squared
  // A disarmed cue stays in the list, keeps its settings, and does nothing.
  // GO steps over it. True by default so every show that predates the flag
  // behaves exactly as it did.
  bool armed = true;
  // Stop the deck when the fade reaches the end. This is the single most used
  // fade in any show -- take the music down and stop it -- and without the
  // flag it is two cues that have to be kept in step by hand.
  bool fadeStopWhenDone = false;
  bool loop = false;              // loop playback (respects loopCount if > 0)
  bool pauseAtBeginning = false;  // load cue paused on first frame (wait for manual play)
  bool pauseOnLastFrame = false;  // hold last frame instead of going to black
  bool transitionToNext = true;   // allow deck-level transition when this cue ends
  // Datamosh: play the prepared copy with keyframes withheld, so P-frames drag
  // the previous picture along their motion. moshPath is the prepared file
  // (Encoder tab -> Datamosh preset); empty means this cue has not been
  // prepared and the toggle should say so rather than silently doing nothing.
  // Timer cue settings. Only meaningful when kind == CueKind::Timer.
  TimerSettings timer;
  // Tone generator settings. Only meaningful when kind == CueKind::Tone.
  ToneSettings tone;
  // The operator's effect stack, applied in list order. Empty on every cue
  // that has never had one, which is the common case and costs nothing.
  std::vector<deckboy::effects::CueEffect> effects;
  // An oscillator per GEOMETRY parameter, indexed by CueGeometryLfoSlot. The
  // same ParamLfo the effect parameters use -- shape, rate, depth, tempo sync,
  // a drawn curve -- evaluated at composite time, so the cue moves on the
  // output and in the preview alike without its pixels being touched. All off
  // on every cue that has never had one, which is what an older show means.
  std::array<deckboy::effects::ParamLfo, 9> geometryLfo {};
  // A Portal source's controls. Only meaningful on a "portal" pattern cue.
  PortalSettings portal;
  // A text cue laid out as a lower third. Off on every other cue.
  LowerThirdDesign lowerThird;
  // Clip whose MOTION drives the motion-puppet effect. Its pictures are never
  // shown -- only the per-macroblock vectors its codec already computed -- so
  // it can be small, and it loops independently of this cue's transport.
  std::string motionDriverPath;
  // The driver has no transport of its own -- it is not a cue, it never
  // reaches the screen. These are how an operator drives it.
  float motionDriverSpeed = 1.0f;      // fields per rendered frame, 0.05..4
  bool motionDriverPaused = false;     // hold the current field
  bool motionDriverRestartOnTake = true;  // every take starts the puppetry the
                                          // same way, which a show needs
  // Only meaningful when kind == CueKind::VideoSynth.
  VideoSynthSettings videoSynth;

  bool datamoshEnabled = false;
  std::string moshPath;
  // CODE SOURCE. The expression a "code" pattern evaluates per pixel: one
  // expression, or three separated by commas for red, green and blue.
  // Compiled when it changes, not parsed per pixel -- see code_source.hpp.
  // Defaults to the plasma example: a new code cue should look like something
  // the moment it is taken, not like a black rectangle waiting to be typed at.
  std::string codeExpression =
    "sin(x*8+t)*0.5+0.5, sin(y*8+t*1.3)*0.5+0.5, sin((x+y)*8-t)*0.5+0.5";
  // Which mosh recipe this cue was prepared with. Per-cue because it is a look
  // choice about THIS clip, and because a global flag could not be saved with
  // the show -- it reset to the weakest flavour on every launch, so the toggle
  // quietly did less than the operator expected. Changing it clears moshPath so
  // the cue re-prepares with the new recipe. See DatamoshLook.
  int datamoshLook = kDatamoshLookClassic;
  bool chromaKeyEnabled = false;  // enable chroma key removal in the compositor
  bool subtitleEnabled = true;    // render subtitles (if subtitle track available)
  bool refreshOnTake = false;     // Browser cue: reload page each time cue is taken
  // Runtime-only (never serialized): set by scanProjectMediaPresence() when a
  // file-backed cue's media can't be found on disk. Drives the MISSING row
  // badge and the toolbar RELINK button.
  bool mediaMissing = false;
};

// Is any of this cue's geometry on an oscillator? Asked per frame by the
// compositor, so the ordinary cue pays for nine bools and nothing else.
inline bool cueHasGeometryLfo(const Cue& cue) {
  for (const auto& lfo : cue.geometryLfo) {
    if (lfo.on) {
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Deck — A playlist of cues with transport state and output configuration.
//
// The application supports multiple decks (Project::decks). Each deck:
//   - Owns its own cue list and selection/active indices
//   - Has independent transport (play/pause/stop via app_cue_transport.ipp)
//   - Routes to one or more OutputTargets (via outputRouteDeckIndex or direct)
//   - Maintains its own timecode chase state (for external TC-triggered playback)
//   - Has warp/edge-blend geometry for projection mapping (per-output)
//   - Carries playlist-level defaults that apply to newly imported cues
//
// The "active" cue is what's currently on-air; "selected" is the UI cursor.
// overlayActiveIndices holds indices of cues playing as overlays (PiP, L3rd).
// ---------------------------------------------------------------------------
struct Deck {
  std::string name = "Deck 1";            // operator-facing deck label
  // WATCH FOLDER. Empty is off, which is what every show that predates this
  // loads as. New media appearing here is imported into this playlist by
  // itself -- see serviceWatchFolders() in app_cue_mgmt.ipp for why the scan
  // runs on a worker and why a file has to hold still before it is taken.
  std::string watchFolder;
  std::vector<Cue> cues;                  // ordered cue list for this deck
  int selectedIndex = -1;                 // UI cursor position (-1 = nothing selected)
  int activeIndex = -1;                   // currently playing/on-air cue (-1 = none)
  std::vector<int> overlayActiveIndices;  // overlay cues currently composited on top

  // -- Playlist behavior ------------------------------------------------------
  bool playlistLoop = false;               // wrap around to first cue after last
  bool shuffle = false;                    // randomize next-cue order
  float playlistOpacity = 1.0f;            // 0.0–1.0 deck contribution to final mix
  bool playlistAutoFade = false;           // auto-fade deck opacity in on take
  double playlistFadeSeconds = 0.8;        // duration of deck auto-fade
  double playlistTimebaseFps = 30.0;       // SMPTE display base (24/25/29.97/30)
  double playlistStartOffsetSeconds = 0.0; // timecode offset for playlist start
  // Defaults applied to newly imported cues in this deck:
  double playlistDefaultCueFadeSeconds = 1.5;           // default fade in/out duration
  double playlistDefaultStillDurationSeconds = 8.0;     // default hold time for stills
  bool playlistDefaultLoop = false;                     // default loop setting
  // New clips import with fades OFF; the operator turns them on per cue
  // (cue-row icon / inspector fade rows) or flips these deck defaults in
  // Settings → Show Flow. Old show files keep their saved values.
  bool playlistDefaultFadeInEnabled = false;            // apply fade-in to newly imported cues
  bool playlistDefaultFadeOutEnabled = false;           // apply fade-out to newly imported cues
  bool playlistDefaultAudioEnabled = true;              // enable audio by default
  bool playlistDefaultPauseAtBeginning = false;         // pause on load by default
  bool playlistDefaultPauseAtEnd = true;                // hold last frame by default
  bool playlistDefaultTransitionToNext = true;          // allow transitions by default

  // -- Multi-selection (for batch operations in the cue list UI) ---------------
  std::vector<int> selectedIndices;

  // -- Audio + output routing --------------------------------------------------
  std::string audioOutputDeviceName;       // SDL audio device name (empty = system default)
  // ── AND ANYWHERE ELSE IT SHOULD GO ────────────────────────────────────
  //
  // The same finished audio, sent to more devices at the same time: the PA
  // and the stream encoder, the house and a wedge on another interface. The
  // crosspoint matrix routes a cue across the CHANNELS of one device; this is
  // the other axis, and there was no way to do it at all.
  //
  // BY NAME, like the primary, for the same reason: a name is what a show
  // file can carry and a device index is not. A name that is absent at open
  // is reported and skipped, never silently swapped for something else.
  std::vector<std::string> extraAudioDeviceNames;
  // Whether this playlist's sound reaches its OWN device -- the room.
  // True for every show that has ever been saved, because that is what
  // every deck did. Turn it off for a playlist that feeds a second screen
  // and should not also be in the PA; it stays audible on the monitor.
  bool audioToProgram = true;
  // Channels to open the deck's audio device with (2/4/6/8). Cues route
  // their stereo onto a pair of these outs (Cue::audioOutputPair). When the
  // physical device has fewer channels, SDL folds the extra pairs down.
  int audioOutputChannels = 2;
  // THE STANDBY POINTER: the cue GO will fire, kept apart from the selection.
  //
  // Selection is the EDITING cursor -- what the inspector shows, what the
  // arrows move, what a click lands on. Standby is the RUNNING ORDER -- what
  // happens next. One highlight doing both jobs is why clicking a cue to look
  // at it during a show also changes what the next GO does.
  //
  // -1 means unset, and unset reproduces the old behaviour exactly: GO takes
  // the selection, as it always has. Every show saved before this loads with
  // -1, so nothing changes until an operator arms one.
  int standbyIndex = -1;
  // A deck whose cues are MASTER cues: the running order, not a content pool.
  // It owns no media and routes to no output -- it only fires other decks.
  bool isMasterDeck = false;
  int outputDisplayIndex = 0;              // which display to open the output window on
  int outputRouteDeckIndex = -1;           // route this deck's output to another deck's window (-1=own)

  // -- NDI output (per-deck; also configurable per-OutputTarget) ---------------
  bool ndiEnabled = false;                 // enable NDI send for this deck
  std::string ndiSourceName;               // NDI source name visible on the network
  bool ndiKeyEnabled = false;              // enable NDI key (alpha) output
  std::string ndiKeySourceName;            // NDI key source name

  // -- Canvas viewport (for multi-output canvas mode) --------------------------
  int canvasViewX = 0;                     // viewport X offset in canvas pixels
  int canvasViewY = 0;                     // viewport Y offset in canvas pixels

  // -- Warp geometry (projection mapping per-output) ---------------------------
  bool warpEnabled = false;                // enable warp mesh
  std::string warpMode = "linear";         // "linear" (bilinear) | "perspective" (4-corner pin)
  float warpTopLeftX = 0.0f;              // corner offsets (normalized 0–1, relative to output)
  float warpTopLeftY = 0.0f;
  float warpTopRightX = 0.0f;
  float warpTopRightY = 0.0f;
  float warpBottomRightX = 0.0f;
  float warpBottomRightY = 0.0f;
  float warpBottomLeftX = 0.0f;
  float warpBottomLeftY = 0.0f;

  // -- Edge blending (for multi-projector soft-edge overlap) -------------------
  float edgeBlendLeft = 0.0f;             // blend gradient width from left   (0–1)
  float edgeBlendRight = 0.0f;            // blend gradient width from right  (0–1)
  float edgeBlendTop = 0.0f;              // blend gradient width from top    (0–1)
  float edgeBlendBottom = 0.0f;           // blend gradient width from bottom (0–1)

  // -- Overlays ----------------------------------------------------------------
  bool timeOverlayEnabled = false;         // show time/ID overlay on this deck's output

  // -- Deck-level transition defaults ------------------------------------------
  double transitionSeconds = 0.0;          // default transition duration for this deck
  std::string transitionStyle = "crossfade"; // "cut" | "crossfade" | "dipblack"

  // -- Timecode chase (external SMPTE timecode drives cue triggering) ----------
  bool timecodeChaseEnabled = false;       // arm timecode chase mode
  bool timecodeRunEnabled = false;         // timecode is actively running (set by ingest)
  bool timecodeTriggerEnabled = true;      // allow cues to auto-fire on TC match
  bool timecodeJamSyncEnabled = true;      // re-sync on TC discontinuity
  double timecodeFreewheelSeconds = 1.0;   // freewheel duration after TC dropout
  double timecodeFps = 30.0;              // TC frame rate for SMPTE display
  double timecodeCurrentSeconds = 0.0;    // latest received timecode value
  double timecodeLastSeconds = 0.0;       // previous frame's TC (for delta/freewheel)
  bool timecodeDirty = false;             // true if TC changed since last render frame
};

// ---------------------------------------------------------------------------
// OutputTarget — A single output destination (window, stream, or DeckLink).
//
// Decouples output routing intent from Deck internals. The "advanced output
// mode" (Project::advancedOutputMode) enables multiple OutputTargets.
// In simple mode, there is one OutputTarget per deck (hostDeckIndex maps 1:1).
//
// hostDeckIndex ties this output to its source deck for rendering.
// mirrorSourceOutputIndex lets one output mirror another (confidence monitor).
//
// Serialized in app/app_project_state.ipp with a 28+4 field layout.
// When adding fields, append to the end and bump the guard in loadProject().
// ---------------------------------------------------------------------------
struct OutputTarget {
  std::string name = "Output 1";           // operator-facing label
  int hostDeckIndex = 0;                   // which deck feeds this output (index into Project::decks)
  int displayIndex = 0;                    // OS display number for fullscreen window
  std::string displayName;                 // SDL display name recorded when the display was chosen.
                                           // SDL indices are enumeration-order-dependent and shuffle on
                                           // hot-plug/reboot — on topology change the display is re-matched
                                           // by this name first, index is only the fallback.
  bool enabled = false;                    // output is active (window open / stream running)
  std::string outputType = "window";       // "window" (SDL fullscreen) | "stream" (ffmpeg egress)
  int mirrorSourceOutputIndex = -1;        // mirror another output's frame (-1 = render own)

  // ── SUPER DECKBOY: THE LAYER STACK ────────────────────────────────────
  //
  // Extra decks composited ON TOP of hostDeckIndex, bottom-first. The host is
  // layer A and always exists; these are layers B, C, D... so an output can
  // show a camera playlist with a lower-thirds playlist over it, and a second
  // output can show the same camera with nothing over it -- the clean feed and
  // the one with the bug, at the same time, from one show.
  //
  // PER OUTPUT, which is the whole difference from VJ mode: VJ mode picks two
  // decks globally and crossfades them into every output at once. This is a
  // routing decision that each destination makes for itself.
  //
  // Each deck's own playlistOpacity is what blends it, so a layer fades in and
  // out with the control that already existed for exactly that.
  std::vector<OutputLayer> layerDecks;



  // -- Streaming egress (ffmpeg SRT/RTMP) --------------------------------------
  bool streamEnabled = false;              // start streaming when output is enabled
  std::string streamProtocol = "srt";      // "srt" | "rtmp" | "rtmps"
  std::string streamUrl;                   // destination URL (e.g. "srt://host:port")
  std::string streamKey;                   // stream key (appended to RTMP URL as /key)
  int streamBitrateKbps = 6000;            // target video bitrate for encoder
  // Audio bitrate for the stream/recording muxer. Was hardcoded at 160k, which
  // is thin for a music recording and wasteful for a talk.
  int streamAudioBitrateKbps = 160;
  // -- SRT transport parameters ------------------------------------------------
  // Previously the ONLY way to set these was hand-typing a query string onto
  // streamUrl, which is not something to ask of an operator mid-show. They are
  // merged into the URL query by buildOutputStreamArgs; anything the operator
  // typed by hand still wins, so existing shows keep working.
  int srtLatencyMs = 120;                  // receiver buffer; the main WAN knob
  std::string srtPassphrase;               // AES encryption (>=10 chars or SRT rejects it)
  std::string srtStreamId;                 // routing hint for the receiver
  std::string srtMode = "caller";          // "caller" (dial out) | "listener" (accept)
  // -- Encoder -----------------------------------------------------------------
  int streamKeyframeSeconds = 2;           // GOP length; was hardcoded to 1s
  // Which deck's program this stream carries is implicit (it mirrors PGM).

  // -- NDI output (per-output, independent of deck-level NDI) ------------------
  bool ndiEnabled = false;                 // enable NDI send for this specific output
  std::string ndiSourceName;               // NDI source name visible on the network
  bool ndiKeyEnabled = false;              // enable NDI key (alpha channel) output
  std::string ndiKeySourceName;            // NDI key source name

  // -- Output properties -------------------------------------------------------
  std::string outputId;                    // unique ID (UUID) for remote-command targeting
  float outputAlpha = 1.0f;               // 0.0–1.0 master dimmer for this output
  int outputDelayMs = 0;                   // egress delay in ms (0–5000, for sync alignment)
  bool outputTimeOverlayEnabled = false;   // burn time/ID overlay onto this output

  // -- Matte & overlay --------------------------------------------------------
  // A MASK AND A LAYER THAT BELONG TO THE OUTPUT, NOT TO A CUE.
  //
  // A house frame is a property of the screen, not of what is playing on it: a
  // 2.39 letterbox or a station bug has to survive every cut, every clear and
  // every panic, and doing it with an overlay cue means remembering to attach
  // it to all of them and losing it the moment anything goes wrong.
  //
  // Both are composited into the output's own picture before anything is taken
  // off it, so the mask and the bug reach the recording, the stream, NDI and
  // the program monitor identically -- there is one picture, and this is part
  // of it.
  //
  // "off" | "16:9" | "4:3" | "2.39:1" | "1.85:1" | "1:1" | "9:16"
  std::string matteAspect = "off";
  double matteOpacity = 1.0;               // 0-1; solid bars at 1
  std::string overlayImagePath;            // still image laid over the output
  double overlayOpacity = 1.0;             // 0-1
  bool overlayEnabled = false;

  // ── PRESENTER VIEW ──────────────────────────────────────────────────────
  //
  // Only read when outputType is "presenter". Kept in one struct so adding a
  // presenter option is one field in one place rather than four edits spread
  // across the type, the writer, the reader and the defaults.
  //
  // Defaults are what a presenter screen should look like with nobody having
  // configured anything: everything shown, dark ground, and notes at the size
  // the rest of the interface uses.
  struct PresenterOptions {
    // wide      current large, previous and next stacked beside it, notes below
    // filmstrip previous / current / next across the top, notes large below
    // notes     notes dominate; the three pictures ride a thin strip on top
    // custom    wherever the operator put them -- see customLayout below
    //
    // The three named ones REFLOW: switch a panel off and the others take its
    // room. A custom layout does not, because it is the arrangement somebody
    // chose and moving it under them would be a bug, not a courtesy.
    std::string layout = "wide";
    // Each panel's place, as FRACTIONS of the area below the header and above
    // the footer -- so a layout laid out on a 1080 laptop is the same shape on
    // the 4K screen it ends up on.
    //
    // One field rather than sixteen: "live:x,y,w,h|prev:...|next:...|notes:..."
    // is readable in the show file, survives a panel being added later, and
    // does not put four more columns on the output record for every output
    // that will never be a presenter view.
    std::string customLayout;
    bool showPrevious = true;
    bool showNext = true;
    bool showNotes = true;
    bool showClock = true;
    bool showTimers = true;
    // The live picture is a panel like the others. Off, with the other two
    // off as well, the notes get the whole screen -- which is what somebody
    // reading a long script from a lectern actually wants, and there was no
    // way to ask for it while one panel was compulsory.
    bool showLive = true;
    // How much of the screen below the header the notes take. The rest goes to
    // the pictures. Each layout has its own sensible default share and this
    // scales it, so "give the notes more room" is one control rather than a
    // choice between three fixed arrangements.
    double notesShare = 1.0;
    // Hex, because a presenter screen is often somebody else's laptop in
    // somebody else's room and "make it readable in here" is a real request.
    std::string background = "#0c0e0c";
    std::string ink = "#e8f0e4";
    std::string accent = "#8fbf60";
    // Notes are read from a lectern, not from a desk. 1.0 is the interface's
    // own size; most people want more.
    double notesScale = 1.4;
    // NOTE BUILDS. A cue's notes split on a line that is exactly "---", and
    // the presenter advances through them without changing the slide -- so a
    // long note is read at the speaker's pace instead of all at once.
    //
    // When this is on, the ordinary NEXT action spends the remaining builds
    // BEFORE it advances the cue, which is how a slide clicker behaves in
    // every other deck the presenter has used. Off by default, because it
    // changes what the transport does.
    bool buildsConsumeAdvance = false;
  };
  PresenterOptions presenter;
  // ── PROMPTER ────────────────────────────────────────────────────────────
  //
  // The talent's screen. Everything here is about one person reading out loud
  // under a piece of glass, which is a different job from the operator's
  // presenter view even though both are words on a second display.
  struct PrompterOptions {
    // The script. Empty means "follow the live cue's notes", which is what a
    // deck-driven show wants; a filled-in script is for a talk that has no
    // slides, or one whose slides are somebody else's problem.
    std::string script;
    // MIRRORED, because a teleprompter's glass reverses the picture on its way
    // to the reader. Both axes exist because rigs differ: the beamsplitter can
    // be above or below the lens.
    bool mirrorHorizontal = true;
    bool mirrorVertical = false;
    // Reading pace. Lines per minute rather than pixels per second: a pace is
    // a property of the READER, and it has to mean the same thing when the
    // type size or the screen changes.
    double linesPerMinute = 140.0;
    bool running = false;              // runtime, but saved so a show reopens armed the same way
    double fontScale = 2.6;            // prompter type is much bigger than a presenter's
    // The line the reader's eye sits on, as a fraction down the screen. Text
    // scrolls up THROUGH it, so what they are saying is always in the same
    // place -- which is the entire ergonomic point of a prompter.
    double readingLineFraction = 0.42;
    bool showReadingLine = true;
    std::string background = "#000000";
    std::string ink = "#ffffff";
    std::string accent = "#ffd24a";
  };
  PrompterOptions prompter;

  std::string outputColorSpace = "auto";   // "auto" | "bt709" | "srgb"
  std::string outputLayoutMode = "span";   // "span" (portion of canvas) | "duplicate" (full copy)
  int outputOrientationDegrees = 0;        // rotation: 0 | 90 | 180 | 270 degrees
  bool outputTestCardEnabled = false;      // force test card (bars + label) on this output

  // -- DeckLink SDI output (Blackmagic hardware) -------------------------------
  bool deckLinkEnabled = false;            // route output to DeckLink card
  int deckLinkDeviceId = -1;               // DeckLink device index (-1 = not assigned)
  std::string deckLinkMode = "1080p60";    // output mode string (e.g. "1080p60", "720p50")
  bool deckLink10Bit = true;               // use 10-bit output (vs 8-bit)
  // KEY + FILL. When on, this output composites over TRANSPARENT instead of
  // black and emits two signals: the fill (RGB premultiplied by alpha) on the
  // device above, and the key (alpha as a greyscale picture) on the device
  // below. That is how a graphic reaches a downstream keyer in a gallery.
  //
  // It changes what the composite MEANS, which is why it is a mode on the
  // output rather than a second device setting: everything sampled from this
  // output -- the preview tap, a recording, NDI -- sees the alpha too.
  bool deckLinkKeyFill = false;            // emit key + fill on two devices
  int deckLinkKeyDeviceId = -1;            // the KEY device (-1 = not assigned)

  // -- Spout output (Windows interprocess texture sharing) ---------------------
  bool spoutEnabled = false;               // route output to Spout sender
  std::string spoutSenderName;             // Spout sender name visible to receivers

  // -- SMPTE ST 2110-20 output (uncompressed video over IP) --------------------
  // EXPERIMENTAL: no PTP lock and no ST 2110-21 narrow pacing — see
  // native/platform/st2110_output.hpp and docs/ST2110_FEASIBILITY.md.
  bool st2110Enabled = false;              // route output to the ST 2110-20 sender
  std::string st2110Address = "239.20.10.1";  // destination multicast group
  std::string st2110Interface;             // local NIC to send from ("" = default route)
  int st2110Port = 20000;                  // destination UDP port
  bool st2110TenBit = true;                // YCbCr-4:2:2 10-bit (vs 8-bit)

  // -- Area of Interest: per-output crop (fraction from each edge, 0–1) --------
  // Allows cropping the rendered output to show only a subregion.
  // All zeros = full output (no crop). Used for multi-display slicing.
  float aoiLeft = 0.0f;                    // crop fraction from left edge
  float aoiRight = 0.0f;                   // crop fraction from right edge
  float aoiTop = 0.0f;                     // crop fraction from top edge
  float aoiBottom = 0.0f;                  // crop fraction from bottom edge



  // ── PROJECTION MAPPING, WHERE IT BELONGS ──────────────────────────────
  //
  // Warp and edge blend correct for the SCREEN a picture lands on -- the
  // keystone of a projector, the soft edge where two of them overlap. That is
  // a property of the destination, and Deck's copy of these even said
  // "per-output" in its comment while sitting on the deck.
  //
  // It worked while one deck fed one screen, because the compositor reached
  // them through the output's HOST deck. It stops working the moment a deck
  // feeds two destinations: a projector that needs keystone and a clean
  // stream that must not have it got the same warp, with no way to separate
  // them. James: "and warp per output?" -- yes.
  //
  // The Deck fields remain as the landing pad for shows saved before this;
  // normalizeProjectOutputsAndLayers lifts them onto the host's output once
  // and clears them. Nothing else reads them.
  // -- THE CROSSFADER ---------------------------------------------------
  //
  // Between two entries of THIS output's stack: 0 is the base, 1..N are
  // the layers over it. Off by default, so an output composites its stack
  // exactly as it always has.
  //
  // The blend is the LAYER's own, not a second setting -- a crossfader
  // that carried its own blend mode would disagree with the layer it is
  // fading, and one of them would have to win silently.
  bool crossfadeEnabled = false;
  int crossfadeFrom = 0;          // stack index faded OUT of
  int crossfadeTo = 1;            // stack index faded IN to
  double crossfadeMix = 0.0;      // 0 = all FROM, 1 = all TO
  bool warpEnabled = false;
  std::string warpMode = "linear";
  float warpTopLeftX = 0.0f;
  float warpTopLeftY = 0.0f;
  float warpTopRightX = 0.0f;
  float warpTopRightY = 0.0f;
  float warpBottomRightX = 0.0f;
  float warpBottomRightY = 0.0f;
  float warpBottomLeftX = 0.0f;
  float warpBottomLeftY = 0.0f;
  float edgeBlendLeft = 0.0f;
  float edgeBlendRight = 0.0f;
  float edgeBlendTop = 0.0f;
  float edgeBlendBottom = 0.0f;
};


// ---------------------------------------------------------------------------
// Project — Top-level state container for the entire show file.
//
// A .deckboy file serializes exactly one Project. Everything the operator
// configures is stored here: decks, outputs, integration enables, audio
// settings, master levels, and output resolution.
//
// Loaded/saved in app/app_project_state.ipp. The UI settings modal
// (app/app_render_settings.ipp) reads and writes most of these fields.
// Remote commands (app/app_remote_command.ipp) can also modify them.
// ---------------------------------------------------------------------------
// One button on the dashboard.
//
// A slot is a LABEL, A GLYPH AND A COMMAND -- and the command is any line the
// remote protocol accepts. That is the whole design, and everything else falls
// out of it: the panel and a control surface become two front-ends to the same
// list, so a slot an operator builds works identically pressed on screen or
// fired from Companion with DASH <n>. A second mechanism for "custom actions"
// would have needed its own storage, its own dispatch and its own way to drift
// out of step with the protocol.
//
// The glyph is a character, not an icon file: Deckboy already carries every
// symbol and emoji text mode can draw, so a dashboard can be decorated from
// the same alphabet without shipping any art.
// ── PRESETS: A MOMENT OF THE WHOLE SHOW, RECALLED AS MUCH OR AS LITTLE AS ASKED
//
// A preset always CAPTURES everything below, and recalls only the groups
// switched on in its scope. Capturing everything means the scope can be
// changed afterwards -- narrowing "everything" to "just positions" -- without
// having to set the show up again and capture it twice.
//
// Master cues say which cue each playlist plays; a preset can too (CUES), and
// can also put back HOW they are playing it.
enum PresetScopeBit : unsigned {
  kPresetCues     = 1u << 0,  // which cue each playlist is on, or that it is off
  kPresetPosition = 1u << 1,  // size, position, rotation, crop, fit, geometry LFOs
  kPresetLook     = 1u << 2,  // brightness, contrast, saturation, hue, key
  kPresetEffects  = 1u << 3,  // the effect stack, with its LFOs
  kPresetLevels   = 1u << 4,  // each playlist's fader
  kPresetRouting  = 1u << 5,  // what each output shows: base, layers, crossfader
  kPresetMaster   = 1u << 6,  // master dimmer and master volume
};
inline constexpr int kPresetScopeCount = 7;
inline constexpr unsigned kPresetScopeAll = (1u << kPresetScopeCount) - 1u;

inline const char* presetScopeToken(int bit) {
  static const char* const kTokens[kPresetScopeCount] = {
    "cues", "position", "look", "effects", "levels", "routing", "master"};
  return (bit >= 0 && bit < kPresetScopeCount) ? kTokens[bit] : "";
}

inline const char* presetScopeLabel(int bit) {
  static const char* const kLabels[kPresetScopeCount] = {
    "cues - what each playlist is playing",
    "position - size, position, rotation, crop",
    "look - brightness, contrast, colour, key",
    "effects - the effect stack",
    "levels - each playlist's fader",
    "routing - what each output shows",
    "master - dimmer and volume"};
  return (bit >= 0 && bit < kPresetScopeCount) ? kLabels[bit] : "";
}

// One playlist, as it was. The parameters are those of the cue that was live
// (or, with nothing live, the selected one), held by value so that editing the
// cue afterwards does not rewrite the preset.
struct PresetDeckState {
  int deckIndex = 0;
  bool live = false;
  std::string cueId;
  float opacity = 1.0f;
  int scaleMode = 0;
  float scaleX = 1.0f, scaleY = 1.0f;
  float offsetX = 0.0f, offsetY = 0.0f;
  float rotation = 0.0f;
  float cropLeft = 0.0f, cropRight = 0.0f, cropTop = 0.0f, cropBottom = 0.0f;
  std::string geometryLfo;   // as the show file spells it
  float brightness = 1.0f, contrast = 1.0f, saturation = 1.0f, hueShift = 0.0f;
  bool keyOn = false;
  SDL_Color keyColor {0, 255, 0, 255};
  float keyTolerance = 60.0f, keySoftness = 20.0f;
  std::string effects;       // as the show file spells it
};

struct PresetOutputState {
  int outputIndex = 0;
  int hostDeckIndex = 0;
  std::string layers;        // as the show file spells an output's stack
  bool crossfadeEnabled = false;
  int crossfadeFrom = 0;
  int crossfadeTo = 1;
  double crossfadeMix = 0.0;
};

struct ShowPreset {
  std::string id;
  std::string name;
  unsigned scope = kPresetScopeAll;
  std::vector<PresetDeckState> decks;
  std::vector<PresetOutputState> outputs;
  double masterDimmer = 1.0;
  double masterVolume = 1.0;
};

struct DashboardSlot {
  std::string label;    // what it says on the button
  std::string command;  // any remote command line, e.g. "VJ BLEND ember"
  std::string glyph;    // one character, decoration only
  int colorIndex = 0;   // 0..15, the same indexed palette cue tags use
};

struct Project {
  std::string title = std::string(kAppTitle); // show file title (displayed in title bar)
  std::vector<Deck> decks {Deck {}};          // all decks (at least one always exists)
  int focusedDeckIndex = 0;                   // which deck the UI is currently showing
  std::vector<OutputTarget> outputs {OutputTarget {}}; // all outputs (at least one)
  int focusedOutputIndex = 0;                 // which output is selected in settings UI
  std::vector<DashboardSlot> dashboard;       // operator-assembled buttons
  std::vector<ShowPreset> presets;            // see ShowPreset

  // -- UI preferences ----------------------------------------------------------
  // -- SMPTE LTC generator (timecode OUT) --------------------------------------
  // Deckboy could always CHASE timecode but never generate it, so it could only
  // ever be a slave in a rig. This makes it a master: LTC is encoded to a real
  // audio device, which is how every other box on the floor expects to receive
  // it (feed it to a spare output pair, or an interface's dedicated TC out).
  bool ltcOutputEnabled = false;
  std::string ltcOutputDeviceName;         // empty = system default playback device
  double ltcOutputFps = 30.0;              // 24 / 25 / 29.97 / 30
  // LTC must be individually routable: it is a control signal, not programme
  // audio, and putting it in the show mix is how you end up broadcasting a
  // buzzsaw. It gets its own device AND its own channel on that device, with
  // every other channel held silent — so a spare pair on the interface can
  // carry timecode while the mix runs elsewhere.
  int ltcOutputChannel = 0;                // 0-based channel index LTC is placed on
  int ltcOutputChannelCount = 2;           // channels to open on that device

  // PTP domain for ST 2110 media-clock alignment. 127 is the SMPTE ST 2059-2
  // default; 0 is the generic IEEE 1588 default. Machine-wide rather than
  // per-output, because there is one clock and one PTP client.
  int ptpDomain = 127;

  // AMWA NMOS IS-04/IS-05. An ST 2110 flow is undiscoverable on its own — a
  // facility expects the node to register itself with a Registration &
  // Discovery System and to be connectable through IS-05, not for an operator
  // to hand-carry an SDP. Machine-wide for the same reason as ptpDomain: one
  // node, advertising every armed 2110 sender on the box.
  //
  // NOTE: there is no mDNS/DNS-SD here, so the registry cannot be discovered
  // automatically — it is configured by URL. Leaving the URL empty still serves
  // the Node API and IS-05 locally (useful on the bench) but registers nowhere.
  bool nmosEnabled = false;
  std::string nmosRegistryUrl;             // e.g. "http://192.168.1.50:8010"
  int nmosPort = 3210;                     // port the Node + Connection API serve on
  std::string nmosInterfaceName = "eth0";  // name reported in interface_bindings

  // PARKED — reserved for Super Deckboy, read by nothing today.
  // It is set (and forced true when a show has >1 deck), saved and loaded, but
  // no code branches on it: the "multi-output routing panel" it was meant to
  // reveal does not exist. Kept, rather than deleted, so existing shows keep
  // round-tripping their `advanced_mode` line and so the intent survives — but
  // do not treat it as a live flag. When Super Deckboy lands this is the switch
  // that pairs with kSuperDeckboySpanningUi (constants.hpp).
  bool advancedOutputMode = false;
  bool uiSoundsEnabled = true;     // play UI sound effects (navigate, take, etc.)
  bool hoverTipsEnabled = true;    // show the little explainer under the pointer
  bool miamiCursorEnabled = true;  // Deckboy's own pointer, not the system arrow
  // Browser cues hide their scrollbars by default. A scrollbar is chrome, and
  // a cue is a picture the audience sees -- BROWSER SCROLL moves the page.
  bool browserScrollbars = false;
  // Follow an incoming MIDI clock instead of the tapped tempo. Off by default:
  // a controller that sends clock all the time would otherwise silently take
  // the tempo away from whoever tapped it.
  bool midiClockSlave = false;
  bool uiTransitionsEnabled = true; // animate UI transitions (panel slides, fades)
  // Splash mascot identity. Maps to data/ui/.../splash/deckboy_splash_<name>.png.
  // Default is "deckbot"; "deckgirl" is the legacy v2-pack illustration.
  // An animated mascot path will reuse this field — load .gif/.mp4 instead of
  // .png when the matching file exists, keeping the swap surface stable.
  std::string splashCharacter = "deckbot";

  // Where program recordings are written. Deliberately SEPARATE from the
  // encoder's output directory: an operator recording a live show usually
  // wants a different physical disk from the one Deckboy is reading media
  // from, both to avoid I/O contention and so a full disk cannot take the app
  // down with the recording. Empty = recordings/ beside the show.
  std::string recordingDir;

  // Recording FORMAT, independent of the program raster and of the display.
  // A recording is a deliverable: it has to land on a stated standard and rate
  // that an edit or a playout chain will accept, not on whatever raster the
  // operator's monitor happens to be.
  //
  // 0 = follow the program raster / rate. Any other value is honoured exactly:
  // the compositor is scaled into the recording raster ON THE GPU before
  // readback, which is also what makes the rate achievable -- reading a 4K
  // frame back costs 21-24ms (MEASURED), a ~45fps ceiling before the encoder
  // sees anything, while 1080 is roughly a quarter of that.
  int recordingWidth = 0;
  int recordingHeight = 0;

  // Recording CODEC. Long-GOP H.264 at a few Mb/s is a viewing copy, not a
  // deliverable: a facility ingests intra-frame mezzanine, where every frame is
  // a keyframe and the file cuts natively. Tokens:
  //   h264 | hevc
  //   prores_proxy | prores_lt | prores_422 | prores_hq | prores_4444
  //   dnxhr_lb | dnxhr_sq | dnxhr_hq | dnxhr_hqx
  // The container follows the codec (see recordingContainerExtension).
  std::string recordingCodec = "h264";

  // Timecode written into the recording. A deliverable that cannot be conformed
  // against a running order is not a deliverable.
  //   value       — start at recordingTimecodeStart (default)
  //   timeofday   — the machine clock when the take starts
  std::string recordingTimecodeMode = "value";
  std::string recordingTimecodeStart = "00:00:00:00";
  // Drop-frame reconciles fractional rates (29.97, 59.94) with wall clock by
  // skipping timecode NUMBERS -- never frames. Meaningless at integer rates.
  //   auto — DF for fractional rates, NDF for integer (what AJA does)
  //   df | ndf — force it
  std::string recordingTimecodeDropFrame = "auto";

  // Segmenting. A four-hour record must not be one unbounded file, and FAT32
  // media dies at 4GB. 0 = no limit.
  int recordingSegmentMinutes = 0;
  int recordingSegmentMegabytes = 0;

  // Rewrite the fragmented recording into a normal MP4/MOV when the take ends.
  // Fragmented is what makes a killed encoder still leave a playable file, but
  // browsers cannot show its duration, seeking breaks in some players and many
  // editors reject it outright. Remuxing on stop keeps the resilience and hands
  // over an ordinary file (the trade OBS calls "hybrid MP4").
  bool recordingRemuxOnStop = true;
  // Broadcast rates are not integers. 23.976/29.97/59.94 are 24000/1001 etc,
  // so this is a double and the muxer is given the exact ratio.
  double recordingFps = 0.0;

  // ASIO driver to play through. Empty = the SDL device, which is the
  // default and what every existing show carries. Stored by NAME rather than
  // index because driver indices shuffle when the operator installs anything.
  // Live audio input: a microphone or line feed. Deckboy had no capture path
  // at all -- only device ENUMERATION, used to pick an LTC timecode source --
  // so a room mic could neither drive a visualiser nor reach a recording.
  // Play the chip synths from the computer keyboard, Ableton-style. OFF by
  // default and deliberately so: while it is on the letter keys make notes
  // instead of firing cues, and silently stealing an operator's shortcuts
  // mid-show would be indefensible.
  bool synthKeyboardEnabled = false;
  int synthKeyboardOctave = 4;
  // Route incoming MIDI notes to a live synth cue instead of firing GOTO.
  bool midiToSynth = false;

  std::string audioInputDeviceName;   // empty = system default
  bool audioInputEnabled = false;     // opening a mic is opt-in, never implicit
  double audioInputGainDb = 0.0;      // -40..+40, applied before metering
  // Mix the input into what is STREAMED and RECORDED. Deliberately not into
  // the speakers: monitoring a room mic through the same machine that is
  // driving the PA is a feedback loop, and an operator who wants to hear
  // themselves has a desk for it.
  bool audioInputToProgram = true;
  // A microphone is a MONO source. Capturing it as stereo puts the signal in
  // one leg and silence in the other, which sounds like a dead channel to
  // anyone listening back -- so mono is the default and is summed to both.
  bool audioInputMono = true;
  // Latched clip indicator. A peak meter that has already fallen back tells
  // you nothing about the transient that distorted; this stays lit until
  // cleared, because the question is "did it clip at ANY point".
  bool audioInputClipLatch = false;

  std::string asioDriverName;
  int asioChannels = 2;

  // Set once the operator says no to the HAP suggestion, so it never nags
  // again for this show.
  bool hapSuggestionDismissed = false;
  // UI color theme — directory name under data/themes/ (e.g. "gameboy",
  // "nebula", "switch-neon"). Empty means "leave the active theme untouched"
  // so opening an older, theme-less show doesn't override the operator's pick.
  // Saved with the show so a chosen colorway survives restarts.
  std::string theme = "";
  // The MIDI input port, BY NAME. It lived only in memory, so every restart
  // fell back to "whichever port enumerates first" -- on a machine with a DAW's
  // virtual ports that is not a control surface at all, and a stray note from
  // it fires cues.
  std::string midiDeviceName = "";
  // Ask GitHub, at startup, whether there is a newer release. OFF BY DEFAULT
  // and deliberately so: this is the only outbound connection Deckboy makes on
  // its own, and a machine sitting on a venue's network should do nothing
  // nobody asked it to. Checking never installs anything -- see
  // checkForUpdateAsync.
  bool updateCheckEnabled = false;

  // ── THE WALL CLOCK ──────────────────────────────────────────────────────
  //
  // Off by default: the toolbar belongs to the show, and an operator who wants
  // the time usually has a clock on the wall behind them. Off / digital 24h /
  // digital 12h / analogue, cycled from one control -- offering only "on"
  // would make somebody's convention the default, and a round face is what a
  // lot of desks actually have.
  std::string clockMode = "off";   // off | 24h | 12h | analog

  // NOT SERIALISED -- true for this run only. False when the loader met a line
  // it did not understand, which is what a truncated or damaged show looks
  // like. The unattended auto-save refuses to write over a file in that state;
  // an explicit Save still does, because that is the operator deciding.
  bool loadedCleanly = true;
  // Terrarium is the Konami-code secret: it only appears in pattern pickers
  // once unlocked, and the unlock belongs to the SAVE (cheeky secrets don't
  // leak across shows). Saved cues load fine either way.
  bool terrariumUnlocked = false;
  // UI scale factor — multiplies every font point size at load time so text
  // grows on HiDPI / 4K screens without ballooning the layout chrome. 1.0 is
  // the native baseline tuned for 1080p. 1.5–2.0 covers 4K desktops and
  // small high-DPI handhelds (the GPD Pocket 3 lands around 2.0). The full
  // layout-chrome scale lives downstream of this field — for now only fonts
  // pick it up. Persist in the project so a show authored on a 4K monitor
  // doesn't have to re-pick the scale every launch.
  // Whether the theme's creatures are allowed out. On by default: a theme has
  // to ask for them before anything appears, so this switch is for an operator
  // who wants a themed machine WITHOUT the company rather than a guard against
  // surprise. They hide themselves whenever an output is live regardless.
  // ── THE MULTIVIEW ─────────────────────────────────────────────────────
  //
  // 0 = off, the single program monitor. 1 = on: the monitor area becomes a
  // grid with the programme in the first tile and every playlist after it.
  // Off by default, because a one-playlist show has nothing to multi-view and
  // the big monitor is worth more.
  int multiviewMode = 0;
  // ── ONE WINDOW OF THE MULTIVIEW ───────────────────────────────────────
  //
  // The multiview used to BE a rule: the programme, then every playlist, in
  // that order, forever. With sixteen playlists allowed that is seventeen
  // windows nobody asked for, and a playlist that only ever exists as a layer
  // over another one does not need a window of its own -- you are already
  // looking at it, composited, in the window of the output it feeds.
  //
  // James: "it should be a decision what gets added and shown in the
  // multiview. it doesnt need to show every layer separately as a window,
  // that is crazy" -- and, a moment later, "but the option to is still nice
  // to have". So the rule becomes the DEFAULT and the decision becomes
  // possible: an empty tile list means the old automatic behaviour, and the
  // moment a window is assigned the list is what is drawn.
  //
  // Everything a broadcast multiview puts over a window is per tile, because
  // that is how it is useful: safe areas on the one feeding a screen with
  // bezels, a meter on the one carrying the sound.
  std::vector<MultiviewTile> multiviewTiles;
  // The dashboard's view: 0 the tiles, 1 the master tracker. Tiles by
  // default, because that is what the dashboard has always been.
  int dashboardMode = 0;
  // The tracker's sequence. LOOP: PLAY goes from the last step back to the
  // first instead of stopping. CLICKER: Page Down / Page Up (what every
  // presentation remote sends) step the tracker instead of the focused
  // playlist, with the dashboard open or not. Both off in an older show,
  // which is what it did.
  bool trackerLoop = false;
  bool clickerDrivesTracker = false;
  // -- THE MONITOR ------------------------------------------------------
  //
  // The device you listen on, and which playlist you hear on it. Empty
  // device means there is no monitor at all, which is what every show
  // before this had.
  //
  // The device is a NAME and it is the REQUEST, the same contract every
  // other device in the show follows: if it is not there when the show
  // opens it is reported, never silently swapped for whatever enumerated
  // first, and never written back over what the operator asked for.
  std::string monitorDeviceName;
  // -1 = follow the focused playlist, which is what an operator flicking
  // through a show wants. Otherwise the playlist is pinned.
  int monitorDeckIndex = -1;
  bool creaturesEnabled = true;
  // Whether they stay out while an output is live. Off by default: during a
  // show the only thing moving on this machine should be the show. On for
  // anyone who would rather have them regardless -- which is the only way to
  // see them at all if you always have an output armed.
  bool creaturesWhileLive = false;
  // Which device ID this machine answers to in MIDI Show Control, 0-127.
  // A desk addresses 127 to reach everything in the rig; anything else has to
  // match exactly, because acting on a GO meant for the lighting desk is worse
  // than missing one.
  int showControlDeviceId = 0;

  // 0 = follow the desktop's own scaling, which is what a machine whose
  // desktop is at 125% or 150% should get without being asked. An explicit
  // value overrides it, for a desk that wants to disagree with its OS.
  // A show saved before this existed carries 1.0 and is unchanged.
  double uiScale = 0.0;

  // ── VJ MODE ────────────────────────────────────────────────────────────
  //
  // Off, Deckboy is a cue deck: one deck feeds an output and takes are
  // deliberate. On, it is a mixer: two decks run at once and a crossfader
  // decides what the audience sees.
  //
  // The decks themselves were always there -- Project::decks is a vector and
  // each one has had its own engine, playlist and transport for as long as
  // there have been decks. What was missing is that an output could only ever
  // be fed by ONE of them. That is the whole difference.
  bool vjModeEnabled = false;
  int vjDeckA = 0;                // which deck sits on the left of the fader
  int vjDeckB = 1;                // and which on the right
  double vjMixPosition = 0.0;     // 0 = all A, 1 = all B
  std::string vjBlendMode = "dissolve";   // dissolve | add | multiply
  // Tempo. Tapped in by the operator, because a VJ knows the tempo before any
  // analysis would and the track is usually coming off someone else's desk.
  double vjTempoBpm = 120.0;
  // Hold a take until the next beat. The point of tempo in a video mixer is
  // not that anything moves by itself -- it is that what the operator does
  // lands ON the music instead of a moment after it.
  bool vjQuantiseTakes = false;
  // Geometry aspect link: editing a cue's output width also scales its
  // height proportionally (and vice versa). Toggleable from the GEOMETRY
  // inspector section, like the chain-link in most media software.
  bool geometryAspectLinked = true;
  // Input model the operator expects. "mouse" is the default — full hover
  // affordances, splitter highlights, right-click menus. "touch" suppresses
  // hover-only feedback (a tap can't hover) and is the right pairing with
  // the Pocket 3 preset. Layout chrome still uses uiScale; this flag only
  // changes interaction feedback. Stored as a string so future modes can
  // land without a schema migration.
  std::string interactionMode = "mouse";
  // INTERFACE LANGUAGE. Empty or "en" is English, which is the source text and
  // therefore never a catalogue. Stored with the show for the same reason the
  // theme is: a desk that travels should arrive looking and reading the way it
  // was set up. See core/i18n.hpp.
  std::string language;

  // -- Network / integration enables -------------------------------------------
  // Each integration follows the pattern in platform/integration_backend.*:
  // enable flag here → runtime thread in main.cpp → settings toggle in UI.
  bool allowRemoteNetwork = false;       // false = listeners bind to localhost only; true = all interfaces
  bool oscQueryEnabled = false;          // OSC query server (Companion, TouchOSC, etc.)
  int oscQueryPort = 5511;               // TCP/UDP port for OSC
  // vMix-compatible control surface. OFF by default: it is a second way into
  // the show, and a second way in is a decision the operator makes rather than
  // one that arrives switched on. The ports are vMix's own, so a panel
  // configured for a vMix rig needs nothing changed.
  bool vmixApiEnabled = false;           // answer the vMix HTTP and TCP APIs
  int vmixHttpPort = 8088;               // vMix's HTTP API port
  int vmixTcpPort = 8099;                // vMix's TCP API port
  bool oscFeedbackMirrorEnabled = false; // mirror OSC feedback to all connected clients
  int oscFeedbackRateMs = 120;           // throttle interval for OSC feedback packets
  bool atemTriggerEnabled = false;       // ATEM switcher tally/trigger integration
  bool ndiTriggerEnabled = false;        // NDI source discovery + trigger integration

  // -- Tally-driven playback ---------------------------------------------------
  // GOING TO AIR IS THE CUE. On a switched show the operator's hands are on the
  // switcher, not on Deckboy: the roll should start because the clip was just
  // put to program, and stop behaving like it is on air the moment it is taken
  // off. An NDI receiver tells its sender exactly that, for free, so a Deckboy
  // output that is being watched knows when it is live.
  //
  // Separate from ndiTriggerEnabled, which is the metadata command channel --
  // that one is somebody sending us instructions, this one is us noticing.
  bool ndiTallyTriggerEnabled = false;   // play when a receiver puts us on program
  bool atemTallyTriggerEnabled = false;  // play when the ATEM puts our input on program
  std::string atemSwitcherHost;          // switcher IP, e.g. "192.168.1.240"
  // WHICH INPUT DECKBOY IS, on the switcher's own numbering. 0 = not set, and
  // nothing fires until it is: guessing would mean rolling on somebody else's
  // camera. This is the switcher's source id, the same number its panel shows.
  int atemTallyInput = 0;
  // HyperDeck emulation. On by default because it always has been, and a show
  // that relied on it must not lose it by upgrading -- but an operator who does
  // not want Deckboy answering as a deck can now say so.
  bool hyperDeckEnabled = true;
  // What to do when we come OFF program. Same vocabulary for every tally
  // source, so an ATEM and an NDI receiver cannot mean different things by it:
  // "nothing" | "next" (load the next cue, paused) | "pause" | "stop" | "clear"
  std::string tallySwitchOffAction = "nothing";
  bool nmcSyncEnabled = false;           // NMC (Network Machine Control) time sync
  // NMC IN AND OUT, as show settings rather than environment variables.
  //
  // Both directions have always worked -- input dispatches NMCEVENT, output
  // sends PLAY/PAUSE/STOP/LOCATE as the transport moves -- but every knob was
  // read from DECKBOY_NMC_* at launch, so using it meant setting environment
  // variables before starting the app and it could not be saved with the show.
  // A capability nobody can reach from the UI is not one the rig has.
  //
  // Empty means "use the old environment variable if one is set, else the
  // default", so a machine already launched with DECKBOY_NMC_MODE keeps
  // working exactly as it did.
  std::string nmcMode;                   // "" | "input" | "output"
  int nmcPort = 0;                       // 0 = default/env
  std::string nmcTargetHost;             // output mode: where to send ("" = env, else broadcast)
  std::string nmcSourceFilter;           // input mode: only accept from this sender
  bool mtcIngestEnabled = false;         // MIDI Timecode ingest (for TC chase)
  bool ltcIngestEnabled = false;         // Linear Timecode (audio) ingest
  bool dmxArtNetEnabled = false;         // Art-Net DMX universe receive
  int artNetPort = 6454;                 // Art-Net UDP port (standard = 6454)
  bool tslTallyEnabled = false;          // TSL 3.1 tally sender (program/preview status)
  int tslTallyPort = 5800;              // TSL UDP port
  std::string tslTallyAddress = "255.255.255.255"; // broadcast or unicast target IP

  // -- Audio configuration -----------------------------------------------------
  int audioBufferSamples = 1024;  // SDL audio callback buffer: 256/512/1024/2048 samples
  int audioDelayMs = 0;           // chain A/V offset: delay ALL deck audio 0–1000 ms
                                  // (displays/PA DSP lag video — dial in with the
                                  // Pocket Test beacon until flash and pop align)
  double masterVolume = 1.0;      // 0.0–1.0 master audio volume
  double masterDimmer = 1.0;      // 0.0–1.0 master video dimmer (all outputs)

  // -- Transport behavior ------------------------------------------------------
  std::string jumpMode = "trigger";   // "trigger" (play immediately) | "load" (load paused)
  bool jumpTransitionEnabled = true;  // use transitions when jumping between cues
  // Panic button behavior (emergency stop):
  std::string panicProfile = "outputs_off"; // "outputs_off"|"fade_pause"|"fade_rewind"|"fade_load_next"
  double panicFadeSeconds = 0.9;            // panic fade-out duration
  bool panicAutoRestore = false;            // auto-restore after panic timeout

  // -- Output resolution and display -------------------------------------------
  bool outputFollowDisplay = true;    // auto-detect resolution from display
  int outputRenderWidth = 1920;       // render resolution width  (pixels)
  int outputRenderHeight = 1080;      // render resolution height (pixels)
  double outputRefreshRateHz = 0.0;   // target refresh rate (0 = auto-detect)
  int outputBitDepth = 0;             // 0=auto, 8=8-bit, 10=10-bit color depth
  bool outputCanvasEnabled = false;   // enable multi-output canvas mode
  int outputCanvasWidth = 3840;       // canvas width  (pixels, for multi-display span)
  int outputCanvasHeight = 2160;      // canvas height (pixels, for multi-display span)
};

// ---------------------------------------------------------------------------
// FramePixelFormat — How a DecodedFrame's pixel buffer is laid out.
//
// The live video decoder picks NV12 when a cue has no chroma key or color
// controls active: it cuts pipe bandwidth by ~62% (12 bpp vs 32 bpp), and
// the GPU samples the YUV→RGB conversion for free at blit time. Everything
// else — stills, thumbnails, browser frames, patterns, source-capture
// placeholders, captured output readbacks — stays RGBA32, because those
// paths either build pixels in CPU code (writePixel writes 4 bytes) or
// receive RGBA from an external producer.
//
// When this is RGBA32, `pixels` holds width*height*4 bytes, tightly packed.
// When this is NV12, `pixels` holds a Y plane of width*height bytes
// followed by an interleaved UV plane of (width/2)*(height/2)*2 bytes,
// total width*height*3/2. Helpers below compute the offsets and pitches.
// ---------------------------------------------------------------------------
enum class FramePixelFormat {
  RGBA32,  // default, 32 bpp interleaved — works with all CPU pixel paths
  NV12,    // 12 bpp planar Y + interleaved UV — live video decode only
  P010,    // 10-bit planar Y + interleaved UV, 16 bpp samples with the data in
           // the HIGH bits. GPU-ONLY: this is what a d3d11va surface carries
           // for HEVC Main 10 / VP9 Profile 2 / AV1 10-bit, and it exists in
           // Deckboy solely as a zero-copy frame. Nothing ever produces P010
           // CPU pixels — downloadGpuFrame() converts to NV12 on the way down
           // — so every `pixels.data()` upload site can keep testing for NV12
           // alone and stay correct.
};

// ---------------------------------------------------------------------------
// DecodedFrame — One decoded video/image/pattern frame.
//
// Filled by MediaEngine's decode thread (readExact from ffmpeg stdout, or
// CPU pattern builders), pushed into frameQueue_, then uploaded to an
// SDL_Texture. Pixel layout is described by `format`; see FramePixelFormat.
// ---------------------------------------------------------------------------
struct DecodedFrame {
  int width = 0;                       // frame width in pixels
  int height = 0;                      // frame height in pixels
  std::uint64_t index = 0;             // display-order index (time * fps) for scheduling
  double presentationSeconds = -1.0;   // decoded PTS in seconds (-1 = unknown); used so
                                       // telecined / variable-rate video schedules by its
                                       // real timestamps instead of a constant-fps counter
  FramePixelFormat format = FramePixelFormat::RGBA32;  // pixel layout for `pixels`
  std::vector<std::uint8_t> pixels;    // packed pixel data, layout per `format`

  // ── GPU-RESIDENT PAYLOAD (in-process zero-copy decode) ────────────────
  //
  // When gpuTexture is set the frame never touched the CPU: `pixels` is empty
  // and the video lives in a surface the decoder owns. gpuFrameRef keeps that
  // surface alive (it holds an AVFrame ref) for as long as this DecodedFrame
  // exists, which is the whole reason a consumer can hold one across frames.
  //
  // WHAT gpuTexture POINTS AT DEPENDS ON THE PLATFORM, and gpuKind says which
  // rather than leaving a consumer to infer it from which of the other fields
  // happen to be null:
  //
  //   D3D11Texture  Windows. An ID3D11Texture2D* ARRAY texture; gpuSubresource
  //                 is the slice, gpuDevice the device that owns it. A
  //                 consumer must be on that same device, so it compares --
  //                 and GPU-COPIES the slice into its own wrapped texture.
  //   CVPixelBuffer macOS. A CVPixelBufferRef from VideoToolbox, backed by an
  //                 IOSurface. gpuSubresource and gpuDevice are unused: an
  //                 IOSurface can be wrapped by ANY Metal device, so there is
  //                 nothing to compare and nothing to copy -- the consumer
  //                 wraps this very buffer as a texture.
  enum class GpuKind { None, D3D11Texture, CVPixelBuffer };
  std::shared_ptr<void> gpuFrameRef;   // opaque AVFrame ref (owns the surface)
  void* gpuTexture = nullptr;          // ID3D11Texture2D* / CVPixelBufferRef
  int gpuSubresource = 0;              // D3D11 only: array slice within gpuTexture
  void* gpuDevice = nullptr;           // D3D11 only: the owning ID3D11Device*
  GpuKind gpuKind = GpuKind::None;
  bool isGpu() const { return gpuTexture != nullptr; }
};

// Byte count for a frame's pixel buffer at the given width/height/format.
// NV12 is rounded to even width/height because the chroma plane is at half
// resolution — odd dimensions would leave a partial UV sample at the edge.
inline std::size_t frameBufferSize(FramePixelFormat format, int width, int height) {
  if (width <= 0 || height <= 0) return 0;
  switch (format) {
    case FramePixelFormat::RGBA32:
      return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    case FramePixelFormat::NV12: {
      // NV12 requires even dimensions for the half-resolution chroma plane.
      int w = width & ~1;
      int h = height & ~1;
      std::size_t y = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
      std::size_t uv = y / 2u;
      return y + uv;
    }
    case FramePixelFormat::P010: {
      // NV12's layout at 16 bits a sample: 2 bytes per luma sample, and a
      // half-resolution chroma plane carrying two 2-byte components per pair.
      // Nothing allocates this today (P010 lives only as a GPU surface) but an
      // enum case that falls through to 0 is a buffer waiting to be undersized.
      int w = width & ~1;
      int h = height & ~1;
      std::size_t y = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
      return y * 3u;
    }
  }
  return 0;
}

// SDL pixel-format constant for the given DecodedFrame layout. Used when
// creating the destination SDL_Texture so the renderer samples correctly.
inline Uint32 sdlPixelFormat(FramePixelFormat format) {
  switch (format) {
    case FramePixelFormat::RGBA32: return SDL_PIXELFORMAT_RGBA32;
    case FramePixelFormat::NV12:   return SDL_PIXELFORMAT_NV12;
    case FramePixelFormat::P010:   return SDL_PIXELFORMAT_P010;
  }
  return SDL_PIXELFORMAT_RGBA32;
}

// ---------------------------------------------------------------------------
// Button — A clickable rectangle in the control UI.
//
// Used by the cue list, toolbar, settings modal, and control bar.
// The primitives layer (render/primitives.*) draws these using fill/outline
// colors, and the text renderer places the label inside rect.
// ---------------------------------------------------------------------------
struct Button {
  std::string label;                              // button text (rendered centered)
  std::string tip;                                // tooltip shown on hover
  SDL_Rect rect {};                               // screen-space bounding box
  SDL_Color fill {48, 40, 31, 255};               // background fill color
  SDL_Color outline {255, 255, 255, 20};          // border/outline color
  SDL_Color text {245, 234, 215, 255};            // label text color
};

// ---------------------------------------------------------------------------
// QuickAction — Every action available from the inspector quick-action bar.
//
// Handled in app/app_quick_action.ipp. Each value maps to a button in the
// inspector panel (app/app_render_inspector.ipp). The handler modifies the
// selected cue's fields and triggers a re-render or engine reload as needed.
//
// Naming convention: Toggle* = bool flip, *Dec/*Inc = step value down/up,
// Edit* = open inline text editor, Cycle* = rotate through enum values.
// ---------------------------------------------------------------------------
enum class QuickAction {
  // -- Playback behavior toggles -------
  ToggleLoop, ToggleHold, TogglePauseBegin, ToggleCueAudio, ToggleNextTransition,
  EditGotoTarget, CycleEndAction,
  // -- Fade in/out ---------
  ToggleFadeIn, ToggleFadeOut,
  FadeInDec, FadeInInc, FadeOutDec, FadeOutInc,
  // -- Volume --------------
  VolDec, VolInc,
  // -- In/out points (trim) --
  InDec, InInc, OutDec, OutInc,
  // -- Per-cue transition --
  TransDec, TransInc,
  // CycleTransStyle and PatternTypePrev/Next removed 2026-08-28: each had a
  // working handler and nothing left in the UI that could fire it, having been
  // superseded by the transition dropdown and the pattern picker. A control
  // that cannot be reached is this codebase's signature bug; keeping the enum
  // entry only makes the audit report it forever.
  // -- Lower third ---------
  LowerBgDec, LowerBgInc,
  // -- Duration (stills/browsers) --
  DurDec, DurInc,
  // -- Loop count ----------
  LoopCountDec, LoopCountInc,
  // -- Playback speed ------
  SpeedDec, SpeedInc,
  // -- Per-cue audio -------
  AudioGainDec, AudioGainInc,
  AudioPanDec, AudioPanInc,
  ToggleCueMono,
  NormalizeCueAudio,
  AudioFadeInDec, AudioFadeInInc,
  AudioFadeOutDec, AudioFadeOutInc,
  AudioOutPairDec, AudioOutPairInc,
  AudioVisualPrev, AudioVisualNext,
  // Dashboard tiles. Each carries its slot index in QuickButton::param.
  DashSlotFire, DashSlotEdit, DashSlotColor, DashSlotAdd, DashSlotDelete,
  MasterToDashboard,
  MultiviewToggle,
  CueSectionAudioToggle,
  // -- Metadata / labels ---
  CycleColorTag,
  EditNotes,
  EditSourceRef,
  EditBrowserUrl,
  GotoMinus10, GotoMinus20, GotoMinus30,
  // -- Geometry: scale -----
  CycleScaleMode,
  ToggleAspectLink,
  ScaleXDec, ScaleXInc,
  ScaleYDec, ScaleYInc,
  EditScaleX, EditScaleY,
  // -- Geometry: offset ----
  OffsetXDec, OffsetXInc,
  OffsetYDec, OffsetYInc,
  EditOffsetX, EditOffsetY,
  // -- Geometry: rotation --
  RotDec, RotInc,
  EditRotation,
  // Generic "type an exact value" for any numeric inspector row. Which value
  // it edits comes from QuickButton::param, so a new control needs a table
  // entry rather than a new action.
  EditNumericParam,
  // Effect stack. All of these carry the effect's INDEX in QuickButton::param,
  // which is what lets one action serve a list of arbitrary length.
  EffectAdd,
  // Pick / clear the clip whose motion drives the motion-puppet effect.
  MotionDriverPick,
  MotionDriverClear,
  MotionDriverSpeedDec,
  MotionDriverSpeedInc,
  MotionDriverPauseToggle,
  MotionDriverRestart,
  MotionDriverRestartOnTakeToggle,
  EffectRemove,
  EffectCycleKind,
  EffectToggleBypass,
  EffectAmountDec,
  EffectAmountInc,
  EffectEditAmount,
  EffectMoveUp,
  EffectMoveDown,
  // paramA / paramB. Which one an effect uses, and what it means, comes from
  // cueEffectParamLabel -- an effect with no second parameter draws no row.
  EffectParamADec,
  EffectParamAInc,
  EffectParamAEdit,
  EffectParamBDec,
  EffectParamBInc,
  EffectParamBEdit,
  EffectParamCDec,
  EffectParamCInc,
  EffectParamCEdit,
  EffectParamDDec,
  EffectParamDInc,
  EffectParamDEdit,
  // THE AUDIO STACK, the same shape as the picture stack above and for the
  // same reason: one action per control, the effect's INDEX in
  // QuickButton::param, so a list of any length needs no new actions. Kept
  // separate from the picture actions rather than sharing them with a flag --
  // a single mis-set flag would then route a filter edit into a posterise.
  AudioEffectAdd,
  AudioEffectRemove,
  AudioEffectCycleKind,
  AudioEffectToggleBypass,
  AudioEffectAmountDec,
  AudioEffectAmountInc,
  AudioEffectEditAmount,
  AudioEffectMoveUp,
  AudioEffectMoveDown,
  AudioEffectParamADec,
  AudioEffectParamAInc,
  AudioEffectParamAEdit,
  AudioEffectParamBDec,
  AudioEffectParamBInc,
  AudioEffectParamBEdit,
  AudioEffectParamCDec,
  AudioEffectParamCInc,
  AudioEffectParamCEdit,
  AudioEffectParamDDec,
  AudioEffectParamDInc,
  AudioEffectParamDEdit,
  // WHICH plugin fills a Plugin slot. Not a cycle: an operator with a hundred
  // and twenty plugins installed is not going to click through them.
  AudioEffectChoosePlugin,
  CodeOpenEditor,
  // Parameter LFOs. Every one of these carries a PACKED id in the action's
  // param: effectIndex * 8 + slot, where slot 0-3 is paramA-D and 4 is the
  // effect's amount. Packed rather than one action per slot because the slot is
  // data, not a different intent, and eight actions per slot would be forty
  // enum entries doing one job.
  EffectLfoToggle,
  EffectLfoShape,
  EffectLfoRateDec,
  EffectLfoRateInc,
  EffectLfoDepthDec,
  EffectLfoDepthInc,
  EffectLfoSync,
  // Drag inside the scribble pad to draw the wave. A scrub rather than a
  // click: the value follows the pointer for as long as the button is down.
  EffectLfoDraw,
  // CodeEditExpression and CodeCycleExample removed 2026-08-29: the one-line
  // value field and the cycle button were both replaced by the code editor,
  // which does what each of them did and can be seen while doing it.
  CueSectionCodeToggle,
  VjCycleBlend,
  VjTapTempo,
  VjToggleQuantise,
  EffectChainCopy,
  EffectChainPaste,
  // -- Geometry: crop ------
  CropLDec, CropLInc,
  CropRDec, CropRInc,
  CropTDec, CropTInc,
  CropBDec, CropBInc,
  // -- Chroma key ----------
  KeyToggle,
  KeyTolDec, KeyTolInc,
  KeySoftDec, KeySoftInc,
  EditKeyColor,
  PickKeyColor,
  // -- Cue number ----------
  EditCueNumber,
  // -- Copy/paste settings --
  CopyCueSettings, PasteCueSettings, ResetCueSettings, ConvertCueMedia,
  // -- Pause points --------
  AddPausePoint, ClearPausePoints,
  // -- Color correction ----
  BrightnessDec, BrightnessInc,
  ContrastDec, ContrastInc,
  SaturationDec, SaturationInc,
  HueShiftDec, HueShiftInc,
  // -- Pattern cue options --
  TogglePatternMotion,
  // -- Inspector section visibility toggles --
  CueSectionPlaybackToggle,
  CueSectionMetadataToggle,
  CueSectionGeometryToggle,
  CueSectionKeyToggle,
  // Per-cue effects section. Datamosh is the first member; the section exists
  // so future per-cue effects have an obvious home that is not "KEY".
  CueSectionEffectsToggle,
  // The sequencing spine, per cue. Every kind has these, so they live in a
  // section drawn after the per-kind chain -- see EFFECTS.
  CueSectionSequenceToggle,
  CueSectionMasterToggle,
  CuePreWaitDec,
  CuePreWaitInc,
  CuePostWaitDec,
  CuePostWaitInc,
  CueContinueCycle,
  CueStandbySet,
  // Master cue assignments. The param is the DECK INDEX being assigned, which
  // is what makes one row per deck work through the ordinary quick-row helper.
  MasterAssignPrev,
  MasterAssignNext,
  MasterAssignClear,
  MasterBypassToggle,
  // Target cues. The victim is picked deck-then-cue, and the verb cycles,
  // so three stepped rows and no free text: a target that points at a typo
  // is the fault the whole broken-cue panel exists to catch.
  // The LED tile size a panel map is drawn to. Only shown on that pattern:
  // a tile size on a colour-bars cue is a control that cannot do anything.
  PanelWidthDec, PanelWidthInc,
  PanelHeightDec, PanelHeightInc,
  // Audition the selected cue: play it to the operator, not to the room.
  AuditionSelected,
  // Rack the selected cue paused and off air, so GO is instant.
  PreloadSelected,
  FireIntensityDec, FireIntensityInc,
  FireSparksDec, FireSparksInc,
  FireViewCycle,
  // The master tracker on the dashboard. TrackerCell carries the row and the
  // column in one param (row * kMaxDecks + deck), because a button has one.
  TrackerToggle, TrackerAddStep, TrackerFire, TrackerCell,
  // The tracker's transport, and a step's length (param = the row).
  TrackerGo, TrackerBack, TrackerPlay,   // PLAY is also STOP while running
  TrackerLoopToggle, TrackerClickerToggle, TrackerLength,
  // Step a row of the numeric-parameter table; param is the NumericParam.
  NumericParamDec, NumericParamInc,
  CueSectionPortalToggle,
  // A text cue laid out as a lower third. LowerThirdCycle's param says which
  // choice steps: 0 look, 1 side, 2 bar colour, 3 accent, 4 in, 5 out.
  LowerThirdLayoutToggle, LowerThirdCycle, LowerThirdEditTitle,
  LowerThirdEditSub, LowerThirdOut,
  // How a lower third arrives and leaves, and how long it takes.
  LowerStyleCycle, LowerAnimDec, LowerAnimInc,
  CueSectionTextToggle,
  CueSectionFiresideToggle,
  CueSectionMidiFileToggle,
  MidiFilePortCycle,
  TextEditBody,
  TextAnimCycle,
  TextAlignCycle,
  TextSizeDec, TextSizeInc,
  TextSpeedDec, TextSpeedInc,
  TextBgDec, TextBgInc,
  CueSectionMatrixToggle,
  MatrixCellCycle,
  MatrixSeed,
  MatrixClear,
  CueSectionDmxToggle,
  DmxEditChannels,
  DmxEditHost,
  DmxFadeDec, DmxFadeInc,
  DmxUniverseDec, DmxUniverseInc,
  DmxFireNow,
  DmxBlackout,
  CueSectionScriptToggle,
  ScriptEdit,
  ScriptRunNow,
  CueSectionTimecodeToggle,
  TcActionCycle,
  TcJamDec, TcJamInc,
  TcEditJam,
  TcFireNow,
  CueSectionNetworkToggle,
  NetProtocolCycle,
  NetEditHost,
  NetPortDec, NetPortInc,
  NetEditAddress,
  NetEditPayload,
  NetSendNow,
  CueSectionMidiToggle,
  MidiKindCycle,
  MidiPortCycle,
  MidiChannelDec, MidiChannelInc,
  MidiData1Dec, MidiData1Inc,
  MidiData2Dec, MidiData2Inc,
  MidiMscDeviceDec, MidiMscDeviceInc,
  MidiEditCueNumber,
  MidiEditRawHex,
  MidiSendNow,
  CueSectionFadeToggle,
  FadeWhatCycle,
  FadeCurveCycle,
  FadeToDec, FadeToInc,
  FadeOverDec, FadeOverInc,
  FadeDeckPrev, FadeDeckNext,
  FadeStopToggle,
  FadeFire,
  CueSectionTargetToggle,
  TargetDeckPrev,
  TargetDeckNext,
  TargetCuePrev,
  TargetCueNext,
  TargetVerbCycle,
  TargetFire,
  // Arm/disarm the selected cue. Lives with the spine, not with targets --
  // every cue has it.
  CueArmToggle,
  CueSectionAudioFxToggle,
  TimerChimeAmberToggle, TimerChimeRedToggle, TimerChimeZeroToggle,
  TimerCycleChimeSound, TimerPickLogo, TimerClearLogo,
  TimerNudgeSecUp, TimerNudgeSecDown,
  CueSectionToneToggle,
  ToneCycleWaveform,
  ToneFreqDec, ToneFreqInc,
  ToneLevelDec, ToneLevelInc,
  StingPitchDec, StingPitchInc,
  StingLenDec, StingLenInc,
  StingSweepDec, StingSweepInc,
  StingBodyDec, StingBodyInc,
  ToneChannelDec, ToneChannelInc,
  ToneCycleVisual, ToneVisualToggle,
  FdsCycleCarrier, FdsCycleModulator,
  FdsDepthDec, FdsDepthInc,
  FdsRatioDec, FdsRatioInc,
  FdsNoteDec, FdsNoteInc,
  FdsRetrigDec, FdsRetrigInc,
  CueSectionSynthToggle, CueSectionVideoSynthToggle,
  VsCycleShape, VsCycleMirror, VsCyclePalette,
  VsSpeedDec, VsSpeedInc,
  VsScaleDec, VsScaleInc,
  VsFeedbackDec, VsFeedbackInc,
  VsZoomDec, VsZoomInc,
  VsReactDec, VsReactInc,
  VsResDec, VsResInc,
  VsSortDec, VsSortInc,
  VsGlitchDec, VsGlitchInc,
  VsAsciiToggle,   VsAsciiColsDec, VsAsciiColsInc,
  VsCrtDec, VsCrtInc,
  VsCharSetCycle, VsShuffleCycle, VsInkCycle,
  VsAsciiChaosDec, VsAsciiChaosInc,
  VsAsciiPresetPrev, VsAsciiPresetNext, VsAsciiFontPick,
  VsAsciiWobbleDec, VsAsciiWobbleInc, VsAsciiWobbleModeCycle,
  VsAsciiGlyphsEdit, VsAsciiPhrasesEdit, VsAsciiHoldDec, VsAsciiHoldInc,
  VsZalgoUpDec, VsZalgoUpInc, VsZalgoDownDec, VsZalgoDownInc,
  VsZalgoMidDec, VsZalgoMidInc, VsZalgoReachDec, VsZalgoReachInc,
  VsZalgoDriftDec, VsZalgoDriftInc,
  SynthKeyboardToggle, SynthMidiToggle,
  SynthCycleTuning, SynthRefDec, SynthRefInc,
  VsSheetPick, VsSheetClear,
  VsSpriteSetPrev, VsSpriteSetNext,
  VsRotateCycle, VsFlipCycle,
  VsJitterDec, VsJitterInc,
  VsChaosDec, VsChaosInc,
  VsFreeAngleDec, VsFreeAngleInc,
  VsTileWDec, VsTileWInc, VsTileHDec, VsTileHInc,
  SynthCycleChip, SynthCycleNesVoice, SynthCycleNesDuty,
  SynthToggleNoiseShort, SynthToggleQuantise,
  SynthAttackDec, SynthAttackInc,
  SynthReleaseDec, SynthReleaseInc,
  TimerCycleColorNormal, TimerCycleColorAmber, TimerCycleColorRed,
  TimerCycleColorBackground,
  // DatamoshToggle was removed with the effects-UI rework: datamosh is an
  // entry in the effect stack now, and syncDatamoshFromStack calls
  // toggleSelectedDatamosh directly. The action had a handler and nothing
  // left that could fire it.
  DatamoshLookPrev, DatamoshLookNext,
  // Stage timer. Run/Reset/Nudge act on the CLOCK, not the transport.
  CueSectionTimerToggle,
  TimerRunToggle,
  TimerResetAction,
  TimerNudgeDown,
  TimerNudgeUp,
  TimerDurDec, TimerDurInc,
  TimerAmberDec, TimerAmberInc,
  TimerRedDec, TimerRedInc,
  TimerCycleMode,
  TimerCycleFace,
  TimerPickFont,
  TimerCountUpToggle,
  TimerEditMessage,
  TimerUrgentToggle,
  TimerProgressToggle,
  CueSectionRoutingToggle,
  // -- Overlays (PiP / lower third / composite) --
  ClearOverlay,
  EditLowerThirdText,
  EditLowerThirdSubtext,
  EditPipTarget,
  EditPipSourcePath,
  EditAttachedLowerThirdCue,
  EditAttachedPipCue,
  EditCompositeSlot1Source,
  EditCompositeSlot2Source,
  EditCompositeSlot3Source,
  EditCompositeSlot4Source,
  CompositePreset2Up,
  CompositePreset7030,
  CompositePresetQuad,
  CycleCompositeAudioSlot,
  // -- PiP position presets --
  PipPresetCornerTL,
  PipPresetCornerTR,
  PipPresetCornerBL,
  PipPresetCornerBR,
  PipPresetSmall,
  PipPresetBig,
  PipPreset7030,
  // -- Transport controls (from inspector) --
  TransportSkipStart,   // |<  seek to beginning of cue
  TransportSkipBack,    // <<  skip back 10 seconds
  TransportPlayPause,   // play/pause toggle
  TransportSkipForward, // >>  skip forward 10 seconds
  TransportSkipNext,    // >|  take the next cue now (".")
  TransportSkipPrev,    // <|  take the previous cue now (",")
  TransportSkipEnd,     // >|  seek to end of cue
  // -- Trim reset ----------
  TrimReset,            // clear in/out points back to defaults
  // -- Browser options -----
  ToggleRefreshOnTake,  // toggle browser cue page reload on every take
  ToggleBrowserInteract // show/hide the real browser window for hands-on use
};

// ---------------------------------------------------------------------------
// QuickButton — A clickable action button in the inspector quick-action bar.
// Maps a screen rect to a QuickAction for hit-testing in app_input.ipp.
// ---------------------------------------------------------------------------
struct QuickButton {
  SDL_Rect rect;          // screen-space bounding box (set during layout)
  QuickAction action;     // which action to fire on click
  std::string tip;        // tooltip text shown on hover
  // Which numeric parameter this button edits, for the generic
  // EditNumericParam action. -1 for everything else. Without a payload every
  // editable value needed its own QuickAction, which is why the synth and
  // timer values were left with no way to type an exact number.
  int param = -1;
};

// ---------------------------------------------------------------------------
// DragState — Tracks an active cue drag-and-drop operation.
// Set in app_input.ipp on mouse-down over a cue row; cleared on mouse-up.
// The cue list renderer uses this to draw the drop indicator.
// ---------------------------------------------------------------------------
struct DragState {
  bool active = false;    // true while a drag is in progress
  int cueIndex = -1;      // index of the cue being dragged
  int deckIndex = 0;      // which deck the cue belongs to
};

// ---------------------------------------------------------------------------
// ToastState — Transient notification message shown to the operator.
// Triggered by triggerToast() in main.cpp; rendered as a floating bar.
// Auto-dismisses after durationMs milliseconds.
// ---------------------------------------------------------------------------
struct ToastState {
  bool active = false;                         // true while toast is visible
  Uint64 startedAt = 0;                       // SDL_GetPerformanceCounter() timestamp
  Uint32 durationMs = 1200;                   // how long to show (milliseconds)
  std::string message;                         // text content
  SDL_Color fill {155, 188, 15, 220};          // background bar color (DMG green)
  SDL_Color ink {15, 56, 15, 255};             // text color (DMG dark)
};

// ---------------------------------------------------------------------------
// UiSoundEffect — Logical sound effect identifiers for UI feedback.
// Played by the UI sound system when uiSoundsEnabled is true.
// The actual WAV files are loaded from the data/ directory at startup.
// ---------------------------------------------------------------------------
enum class UiSoundEffect {
  Navigate,   // cursor moved in cue list
  Import,     // media file imported
  Take,       // cue taken (put on air)
  Toggle,     // boolean toggled in inspector
  Stop,       // transport stopped
  Clear,      // cue cleared / output blacked out
  Delete,     // cue deleted from list
  Error,      // action refused (missing media, blocked take)
  Panic,      // panic — everything off
  Shuffle,    // shuffle mode toggled on
  PowerUp,    // a second deck arrives: Deckboy becomes Super Deckboy
  DeckAdded,  // a playlist joins the show
  DeckRemoved // a playlist leaves it
};

#endif
