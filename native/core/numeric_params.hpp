// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#pragma once
#include "types.hpp"
#include "pattern_helpers.hpp"
#include "isf_cue.hpp"
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

namespace deckboy::params {
enum class NumericParam : int {
  None = -1,

  // Video synth
  VsSpeed = 0,
  VsScale,
  VsFeedback,
  VsZoom,
  VsReact,
  VsResolution,
  VsPixelSort,
  VsGlitch,
  VsCrt,
  VsFreeAngle,

  // Tone generator
  ToneLevel,
  ToneFreq,

  // Motion driver
  MotionDriverSpeed,

  // Chip synth
  SynthNote,
  SynthAttack,
  SynthRelease,
  FdsDepth,
  FdsRatio,
  FdsRetrig,

  // Portal source
  PortalBlobs,
  PortalSize,
  PortalBlend,
  PortalOutline,
  PortalSpeed,
  PortalHue,

  // Lower third (a text cue's layout)
  L3Height,
  L3Size,
  L3InSeconds,
  L3OutSeconds,
  L3HoldSeconds,
  CodeWidth,
  CodeHeight,
  SwirlSpeed,
  SwirlTwist,
  SwirlCells,
  SwirlHue,
  SwirlZoom,
  SwirlFlow,
  SwirlCentreX,
  SwirlCentreY,
  SwirlBands,
  SwirlPalette,
  // Moire source. Stored 0-1 (an oscillator swings 0-1); typed and shown in
  // the operator's units -- percent, degrees -- mapped in read/write below.
  MoireShape,
  MoirePalette,
  MoireDensity,
  MoireSpeed,
  MoireWarp,
  MoireTurn,
  MoireCentreX,
  MoireCentreY,
  MoireGrey,
  MoireHue,
  // Lumia source, mapped like Moire.
  LumiaPalette,
  LumiaMode,
  LumiaTopBottom,
  LumiaListens,
  LumiaFlow,
  LumiaWater,
  LumiaRipple,
  LumiaJet,
  LumiaPrism,
  LumiaSplit,
  LumiaFocus,
  LumiaShimmer,
  LumiaMirrors,
  LumiaAngle,
  LumiaSpin,
  LumiaLens,
  LumiaGlow,
  LumiaBright,
  LumiaTrail,
  // Illusion source, mapped like Moire. The desk names the shared controls
  // for the shape (illusionRowLabel).
  IllusionShape,
  IllusionForm,
  IllusionFlow,
  IllusionFlip,
  IllusionPalette,
  IllusionSize,
  IllusionCount,
  IllusionEdge,
  IllusionSpeed,
  IllusionCentreX,
  IllusionCentreY,
  IllusionTone,
  IllusionHue1,
  IllusionHue2,
  // Panel Map: one LED tile, in pixels.
  LedPanelWidth,
  LedPanelHeight,
  // The mask in hand (app_mask_editor.ipp, maskFocus), in operator units:
  // percent of the frame, percent, degrees.
  MaskX,
  MaskY,
  MaskScale,
  MaskRotation,
  MaskFeather,
  MaskGrow,
  MaskOpacity,
  // The draw brush (the show's) and a Draw cue's playback.
  DrawSize,
  DrawOpacity,
  DrawFps,
  DrawHold,
  StingPitch,
  StingLen,
  StingSweep,
  StingBody,
  VoicePitch,
  VoiceSize,
  VoiceBreath,
  ToneChannel,
  VsTileW,
  VsTileH,
  VsJitter,
  VsChaos,
  SynthReference,
  TimerDuration,
  TimerAmber,
  TimerRed,
  TimerLogoSize,
  FireIntensity,
  FireSparks,
};

struct NumericParamSpec {
  const char* token;      // inline editor identity, so it can restore position
  const char* label;      // shown in the editor
  const char* hint;       // range, in the operator's words
  double lo = 0.0;
  double hi = 1.0;
  int precision = 2;
  bool integral = false;
  // One press of - or +. Zero means a fiftieth of the range, which suits a
  // row that only ever had typed entry; a count wants 1.
  double step = 0.0;
};

// Install once before using shader parameters. The host owns file loading/caching.
using ShaderLookup = std::shared_ptr<const deckboy::isf::Shader> (*)(const std::string&);
// Configure before starting threads. A lookup may return parsed metadata without compiling.
inline ShaderLookup shaderLookup = nullptr;
inline void setShaderLookup(ShaderLookup lookup) { shaderLookup = lookup; }
inline constexpr int kIsfParamBase = 4000;
inline int isfParamId(int input, int comp) { return kIsfParamBase + input * 4 + comp; }
inline bool isfParamIs(NumericParam id) {
  const int v = static_cast<int>(id);
  return v >= kIsfParamBase && v < kIsfParamBase + kIsfMaxInputs * 4;
}
inline bool isShaderCue(const Cue& cue) {
  return cue.kind == CueKind::Pattern && normalizePatternTypeId(cue.path) == "isf";
}
inline std::shared_ptr<const deckboy::isf::Shader> shaderOfCue(const Cue& cue) {
  // Keep path parsing local: metadata consumers need not link the engine.
  auto path = cue.path;
  if (path.rfind("pattern://", 0) == 0) path.erase(0, 10);
  return shaderLookup ? shaderLookup(path.substr(4)) : nullptr;
}
inline bool cueIsCodeSource(const Cue& cue) {
  return cue.kind == CueKind::Pattern && stripPatternMotionSuffix(normalizePatternTypeId(cue.path)) == "code";
}
inline const NumericParamSpec* isfParamSpec(const Cue* cue, NumericParam id) {
    static thread_local NumericParamSpec specs[kIsfMaxInputs * 4];
    static thread_local std::string texts[kIsfMaxInputs * 4][3];
    if (!isfParamIs(id)) return nullptr;
    const int n = static_cast<int>(id) - kIsfParamBase;
    const int input = n / 4, comp = n % 4;
    if (!cue || !isShaderCue(*cue)) return nullptr;
    const auto shader = shaderOfCue(*cue);
    if (!shader || input >= static_cast<int>(shader->inputs.size())) return nullptr;
    const auto& in = shader->inputs[static_cast<std::size_t>(input)];
    if (in.type != deckboy::isf::InputType::Float && in.type != deckboy::isf::InputType::Point2D) return nullptr;
    if (comp >= in.comps) return nullptr;
    auto& t = texts[n];
    t[0] = "isf." + in.name + "." + std::to_string(comp);
    t[1] = in.label + (in.type == deckboy::isf::InputType::Point2D ? (comp == 0 ? " x" : " y") : "");
    char range[64];
    std::snprintf(range, sizeof(range), "%g to %g", static_cast<double>(in.mn[comp]), static_cast<double>(in.mx[comp]));
    t[2] = range;
    const double lo = in.mn[comp], hi = in.mx[comp];
    specs[n] = NumericParamSpec {t[0].c_str(), t[1].c_str(), t[2].c_str(), lo, hi > lo ? hi : lo + 1.0, 3, false,
                                 (hi - lo) / 50.0};
    return &specs[n];
  }

inline bool readIsfParam(const Cue& cue, NumericParam id, double& out) {
    if (!isfParamIs(id) || !isShaderCue(cue)) return false;
    const int n = static_cast<int>(id) - kIsfParamBase;
    const auto shader = shaderOfCue(cue);
    if (!shader || n / 4 >= static_cast<int>(shader->inputs.size())) return false;
    const auto& in = shader->inputs[static_cast<std::size_t>(n / 4)];
    if (n % 4 >= in.comps) return false;
    const IsfSetting* set = deckboy::isf::findSetting(cue, in.name);
    out = set ? set->v[n % 4] : in.def[n % 4];
    return true;
  }

inline void writeIsfParam(Cue& cue, NumericParam id, double value) {
    if (!isfParamIs(id) || !isShaderCue(cue)) return;
    const int n = static_cast<int>(id) - kIsfParamBase;
    const auto shader = shaderOfCue(cue);
    if (!shader || n / 4 >= static_cast<int>(shader->inputs.size())) return;
    const auto& in = shader->inputs[static_cast<std::size_t>(n / 4)];
    if (n % 4 >= in.comps) return;
    deckboy::isf::ensureSetting(cue, in).v[n % 4] =
      static_cast<float>(std::clamp<double>(value, in.mn[n % 4], std::max(in.mx[n % 4], in.mn[n % 4])));
  }

inline const NumericParamSpec* numericParamSpec(NumericParam id, const Cue* cue = nullptr) {
  switch (id) {
    case NumericParam::SynthReference: { static const NumericParamSpec s {"synth.reference", "A =", "Reference pitch. 440 is modern standard, 432 the common "
                     "alternative, and older instruments sat lower still.", 380, 480, 0, false, 1}; return &s; }

    case NumericParam::StingPitch: { static const NumericParamSpec s {"tone.stingPitchHz", "pitch", "Where the sting starts. Under a voice it wants to stay "
                       "out of the way; over a room it wants to cut through.", 20, 12000, 2, false, 0}; return &s; }
    case NumericParam::StingLen: { static const NumericParamSpec s {"tone.stingSeconds", "length", "The whole gesture. This IS the cue's duration -- a "
                       "sting is the one generated signal here that ends.", 0.05, 10, 2, false, 0.05}; return &s; }
    case NumericParam::StingSweep: { static const NumericParamSpec s {"tone.stingSweepSemitones", "sweep", "How far it travels, in semitones. Up reads as arrival, "
                       "down as departure, zero is a struck note.", -24, 24, 2, false, 1}; return &s; }
    case NumericParam::StingBody: { static const NumericParamSpec s {"tone.stingBody", "body", "Harmonic weight: a pure chime at zero, brass at full. "
                       "This is the difference between a ping and a fanfare.", 0, 100, 2, false, 5}; return &s; }
    case NumericParam::VoicePitch: { static const NumericParamSpec s {"tone.voicePitchHz", "pitch", "The speaking pitch. Steps in semitones: about 100 Hz is a deep "
                         "voice, 220 Hz a high one.", 50, 400, 2, false, 0}; return &s; }
    case NumericParam::VoiceSize: { static const NumericParamSpec s {"tone.voiceSize", "size", "The length of the throat. 0.8 is a child, 1.25 a giant.", 0.8, 1.25, 2, false, 0.05}; return &s; }
    case NumericParam::VoiceBreath: { static const NumericParamSpec s {"tone.voiceBreath", "breath", "Airiness of the voice: 0 is pressed and hard, 100 a whisper.", 0, 100, 2, false, 5}; return &s; }
    case NumericParam::ToneChannel: { static const NumericParamSpec s {"tone.channel", "output", "Which output channel carries the signal. 'all' feeds "
                       "every one.", -1, 15, 0, true, 1}; return &s; }
    case NumericParam::VsTileW: { static const NumericParamSpec s {"videoSynth.spriteTileW", "tile w", "Tile width in pixels. Must match the sheet's grid or "
                       "the slices land across neighbouring sprites.", 8, 128, 0, true, 8}; return &s; }
    case NumericParam::VsTileH: { static const NumericParamSpec s {"videoSynth.spriteTileH", "tile h", "Tile height in pixels.", 8, 128, 0, true, 8}; return &s; }
    case NumericParam::VsJitter: { static const NumericParamSpec s {"videoSynth.spriteJitter", "jitter", "Vary tile size per cell, so the grid stops looking "
                       "like a grid.", 0, 1, 2, false, 0.1}; return &s; }
    case NumericParam::VsChaos: { static const NumericParamSpec s {"videoSynth.spriteChaos", "chaos", "0 picks tiles by brightness so the picture reads. 1 "
                       "picks at random so the grid becomes texture.", 0, 1, 2, false, 0.1}; return &s; }

    case NumericParam::TimerDuration: {
      static thread_local NumericParamSpec s {"timer.durationSeconds", "duration", "Total time", 0, 86400, 0, true, 300};
      return &s;
    }
    case NumericParam::TimerAmber: {
      static thread_local NumericParamSpec s {"timer.amberSeconds", "amber at", "Seconds REMAINING when the clock turns amber", 0, 86400, 0, true, 15};
      s.hi = cue ? cue->timer.durationSeconds : 86400;
      return &s;
    }
    case NumericParam::TimerRed: {
      static thread_local NumericParamSpec s {"timer.redSeconds", "red at", "Seconds REMAINING when the clock turns red", 0, 86400, 0, true, 5};
      s.hi = cue ? cue->timer.amberSeconds : 86400;
      return &s;
    }
    case NumericParam::TimerLogoSize: {
      static thread_local NumericParamSpec s {"timer.logoHeightPercent", "logo size", "How tall the logo is above the clock", 2, 40, 0, true, 2};
      return &s;
    }
    case NumericParam::FireIntensity: {
      static thread_local NumericParamSpec s {"firesideIntensity", "burns", "Embers to roaring. It still surges and settles at any setting -- a fire that does not move reads as a photo", 0.2, 2, 1, false, 0.1};
      return &s;
    }
    case NumericParam::FireSparks: {
      static thread_local NumericParamSpec s {"firesideSparks", "sparks", "How much it throws off; 0 is a clean burn", 0, 160, 0, true, 4};
      return &s;
    }

    case NumericParam::SwirlSpeed: {
      static const NumericParamSpec s {"swirl.speed", "Speed", "-3 to 3: 0 holds, negative runs backwards", -3, 3, 2, false, 0.1};
      return &s;
    }
    case NumericParam::SwirlTwist: {
      static const NumericParamSpec s {"swirl.twist", "Twist", "Spiral strength, 0-4", 0, 4, 2, false, 0.1};
      return &s;
    }
    case NumericParam::SwirlCells: {
      static const NumericParamSpec s {"swirl.cells", "Pixel density", "Horizontal cells, 64-960", 64, 960, 0, true, 16};
      return &s;
    }
    case NumericParam::SwirlHue: {
      static const NumericParamSpec s {"swirl.hue", "Colour", "Palette turn, 0-1", 0, 1, 2, false, 0.02};
      return &s;
    }
    case NumericParam::SwirlZoom: {
      static const NumericParamSpec s {"swirl.zoom", "Zoom", "Spiral size, 0.25-4", 0.25, 4, 2, false, 0.05};
      return &s;
    }
    case NumericParam::SwirlFlow: {
      static const NumericParamSpec s {"swirl.flow", "Flow", "Turbulence, 0 still to 3 boiling", 0, 3, 2, false, 0.1};
      return &s;
    }
    case NumericParam::SwirlCentreX: {
      static const NumericParamSpec s {"swirl.cx", "Centre X", "Where the eye sits, 0 left to 1 right", 0, 1, 2, false, 0.02};
      return &s;
    }
    case NumericParam::SwirlCentreY: {
      static const NumericParamSpec s {"swirl.cy", "Centre Y", "Where the eye sits, 0 top to 1 bottom", 0, 1, 2, false, 0.02};
      return &s;
    }
    case NumericParam::SwirlBands: {
      static const NumericParamSpec s {"swirl.bands", "Bands", "Colour steps, 2-8", 2, 8, 0, true, 1};
      return &s;
    }
    case NumericParam::SwirlPalette: {
      static const NumericParamSpec s {"swirl.palette", "Palette", "0 classic, 1 fire, 2 ice, 3 neon, 4 mono", 0, 4, 0, true, 1};
      return &s;
    }
    case NumericParam::MoireShape: {
      static const NumericParamSpec s {"moire.shape", "Shape", "0 zone, 1 twin, 2 flow, 3 saddle, 4 ripple, 5 shear", 0, 5, 0, true, 1};
      return &s;
    }
    case NumericParam::MoirePalette: {
      static const NumericParamSpec s {"moire.palette", "Palette", "0 rainbow + grey, 1 rainbow, 2 theme, 3 mono", 0, 3, 0, true, 1};
      return &s;
    }
    case NumericParam::MoireDensity: {
      static const NumericParamSpec s {"moire.density", "Density", "0-100%: 50 is 1x, 0 a quarter, 100 four times", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::MoireSpeed: {
      static const NumericParamSpec s {"moire.speed", "Speed", "0-100%: 0 holds the field still", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::MoireWarp: {
      static const NumericParamSpec s {"moire.warp", "Warp", "0-100%: how far the field bends", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::MoireTurn: {
      static const NumericParamSpec s {"moire.turn", "Turn", "Degrees, 0-360", 0, 360, 0, false, 5};
      return &s;
    }
    case NumericParam::MoireCentreX: {
      static const NumericParamSpec s {"moire.cx", "Centre X", "0-100%, left to right", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::MoireCentreY: {
      static const NumericParamSpec s {"moire.cy", "Centre Y", "0-100%, top to bottom", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::MoireGrey: {
      static const NumericParamSpec s {"moire.grey", "Grey", "0-100%: the share of each band drawn as grey stripes", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::MoireHue: {
      static const NumericParamSpec s {"moire.hue", "Hue", "Degrees round the colour wheel, 0-360", 0, 360, 0, false, 5};
      return &s;
    }
    case NumericParam::IllusionShape: {
      static const NumericParamSpec s {"illusion.shape", "Shape", "0 drift, 1 ouchi, 2 cafe, 3 vanish, 4 grid, 5 dancer", 0, 5, 0, true, 1};
      return &s;
    }
    case NumericParam::IllusionForm: {
      static const NumericParamSpec s {"illusion.form", "Form", "0 leaf, 1 petal, 2 scale, 3 ring", 0, 3, 0, true, 1};
      return &s;
    }
    case NumericParam::IllusionFlow: {
      static const NumericParamSpec s {"illusion.flow", "Flow", "0 turn, 1 out, 2 in, 3 wave", 0, 3, 0, true, 1};
      return &s;
    }
    case NumericParam::IllusionFlip: {
      static const NumericParamSpec s {"illusion.flip", "Flip", "0 or 1: the other way round", 0, 1, 0, true, 1};
      return &s;
    }
    case NumericParam::IllusionPalette: {
      static const NumericParamSpec s {"illusion.palette", "Palette", "0 classic, 1 theme, 2 hues", 0, 2, 0, true, 1};
      return &s;
    }
    case NumericParam::IllusionSize: {
      static const NumericParamSpec s {"illusion.size", "Size", "0-100%", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::IllusionCount: {
      static const NumericParamSpec s {"illusion.count", "Count", "0-100%", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::IllusionEdge: {
      static const NumericParamSpec s {"illusion.edge", "Edge", "0-100%", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::IllusionSpeed: {
      static const NumericParamSpec s {"illusion.speed", "Speed", "0-100%: 0 holds it still", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::IllusionCentreX: {
      static const NumericParamSpec s {"illusion.cx", "Centre X", "0-100%, left to right", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::IllusionCentreY: {
      static const NumericParamSpec s {"illusion.cy", "Centre Y", "0-100%, top to bottom", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::IllusionTone: {
      static const NumericParamSpec s {"illusion.tone", "Grey", "0-100%: 0 black, 100 the light tile", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::IllusionHue1: {
      static const NumericParamSpec s {"illusion.hue1", "Colour 1", "Degrees round the colour wheel, 0-360", 0, 360, 0, false, 5};
      return &s;
    }
    case NumericParam::IllusionHue2: {
      static const NumericParamSpec s {"illusion.hue2", "Colour 2", "Degrees round the colour wheel, 0-360", 0, 360, 0, false, 5};
      return &s;
    }
    case NumericParam::LumiaPalette: {
      static const NumericParamSpec s {"lumia.palette", "Lasers", "0 rgb, 1 rainbow, 2 floyd, 3 warm, 4 cool, 5 green", 0, 5, 0, true, 1};
      return &s;
    }
    case NumericParam::LumiaMode: {
      static const NumericParamSpec s {"lumia.mode", "Mirrors", "0 tube, 1 mandala, 2 none", 0, 2, 0, true, 1};
      return &s;
    }
    case NumericParam::LumiaTopBottom: {
      static const NumericParamSpec s {"lumia.topbottom", "Top / bottom", "0 off, 1 on: the top mirrors the bottom round a dark seam", 0, 1, 0, true, 1};
      return &s;
    }
    case NumericParam::LumiaListens: {
      static const NumericParamSpec s {"lumia.listens", "Listens", "0 off, 1 on: the flow surges with the programme sound", 0, 1, 0, true, 1};
      return &s;
    }
    case NumericParam::LumiaFlow: {
      static const NumericParamSpec s {"lumia.flow", "Flow", "0-100%: how fast the water moves", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaWater: {
      static const NumericParamSpec s {"lumia.water", "Water", "0-100%: how bright the water is", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaRipple: {
      static const NumericParamSpec s {"lumia.ripple", "Ripple size", "0-100%: 0 broad, 100 fine and busy", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaJet: {
      static const NumericParamSpec s {"lumia.jet", "Second jet", "0-100%: a second jet crossing the first", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaPrism: {
      static const NumericParamSpec s {"lumia.prism", "Prism", "0-100%: how bright the laser lines are", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaSplit: {
      static const NumericParamSpec s {"lumia.split", "Split", "0-100%: how far the colours fan apart", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaFocus: {
      static const NumericParamSpec s {"lumia.focus", "Focus", "0-100%: how hard the lines are bent", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaShimmer: {
      static const NumericParamSpec s {"lumia.shimmer", "Shimmer", "0-100%: laser speckle", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaMirrors: {
      static const NumericParamSpec s {"lumia.mirrors", "Mirrors", "Panels, 0-12", 0, 12, 0, true, 1};
      return &s;
    }
    case NumericParam::LumiaAngle: {
      static const NumericParamSpec s {"lumia.angle", "Panel angle", "0-100%: how far each panel tilts from the one before", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaSpin: {
      static const NumericParamSpec s {"lumia.spin", "Spin", "0-100%: the whole picture turns", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaLens: {
      static const NumericParamSpec s {"lumia.lens", "Lens", "0-100%: the round lens, barrel and dark rim", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaGlow: {
      static const NumericParamSpec s {"lumia.glow", "Glow", "0-100%: the halo round the lines", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaBright: {
      static const NumericParamSpec s {"lumia.bright", "Brightness", "0-100%: overall brightness", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::LumiaTrail: {
      static const NumericParamSpec s {"lumia.trail", "Trail", "0-100%: light lingers on the screen", 0, 100, 0, false, 2};
      return &s;
    }
    case NumericParam::CodeWidth:
    case NumericParam::CodeHeight: {
      static const NumericParamSpec w {"code.width", "Render width", "Pixels, 64-3840", 64, 3840, 0, true, 64};
      static const NumericParamSpec h {"code.height", "Render height", "Pixels, 64-2160", 64, 2160, 0, true, 36};
      return id == NumericParam::CodeWidth ? &w : &h;
    }
    case NumericParam::VsSpeed: {
      static const NumericParamSpec s {"vs.speed", "Speed",
                                       "Master oscillator rate, 0.01-8", 0.01, 8.0, 2, false};
      return &s;
    }
    case NumericParam::VsScale: {
      static const NumericParamSpec s {"vs.scale", "Scale",
                                       "Feature size, 0.1-8", 0.1, 8.0, 2, false};
      return &s;
    }
    case NumericParam::VsFeedback: {
      static const NumericParamSpec s {"vs.feedback", "Feedback",
                                       "How much of each frame feeds the next, 0-0.95",
                                       0.0, 0.95, 2, false};
      return &s;
    }
    case NumericParam::VsZoom: {
      static const NumericParamSpec s {"vs.zoom", "Feedback zoom",
                                       "Per-frame zoom of the feedback path, 0.90-1.15",
                                       0.90, 1.15, 3, false};
      return &s;
    }
    case NumericParam::VsReact: {
      static const NumericParamSpec s {"vs.react", "Audio reactivity",
                                       "How hard the audio drives it, 0-1", 0.0, 1.0, 2, false};
      return &s;
    }
    case NumericParam::VsResolution: {
      static const NumericParamSpec s {"vs.resolution", "Detail",
                                       "Internal resolution, 1 (chunky) to 5 (fine)",
                                       1.0, 5.0, 0, true};
      return &s;
    }
    case NumericParam::VsPixelSort: {
      static const NumericParamSpec s {"vs.smear", "Smear",
                                       "Pixel sort amount, 0-1", 0.0, 1.0, 2, false};
      return &s;
    }
    case NumericParam::VsGlitch: {
      static const NumericParamSpec s {"vs.glitch", "Glitch",
                                       "Block displacement and channel tearing, 0-1",
                                       0.0, 1.0, 2, false};
      return &s;
    }
    case NumericParam::VsCrt: {
      static const NumericParamSpec s {"vs.crt", "CRT",
                                       "Scanlines, bloom and gun misalignment, 0-1",
                                       0.0, 1.0, 2, false};
      return &s;
    }
    case NumericParam::VsFreeAngle: {
      static const NumericParamSpec s {"vs.spin", "Sprite spin",
                                       "Degrees per second, -720 to 720",
                                       -720.0, 720.0, 0, false};
      return &s;
    }
    case NumericParam::ToneLevel: {
      static const NumericParamSpec s {"tone.level", "Level",
                                       "dBFS, -60 to 0 (-18 is EBU alignment)",
                                       -60.0, 0.0, 1, false};
      return &s;
    }
    case NumericParam::ToneFreq: {
      static const NumericParamSpec s {"tone.freq", "Frequency",
                                       "Hertz, 20-20000", 20.0, 20000.0, 1, false};
      return &s;
    }
    case NumericParam::MotionDriverSpeed: {
      static const NumericParamSpec s {"motion.speed", "Driver speed",
                                       "Fields per frame, 0-4 (1 = one field "
                                       "per rendered frame)", 0.0, 4.0, 2, false};
      return &s;
    }
    case NumericParam::SynthNote: {
      static const NumericParamSpec s {"synth.note", "Note",
                                       "Pitch in hertz, 20-8000", 20.0, 8000.0, 2, false};
      return &s;
    }
    case NumericParam::SynthAttack: {
      static const NumericParamSpec s {"synth.attack", "Attack",
                                       "Seconds, 0-4", 0.0, 4.0, 3, false};
      return &s;
    }
    case NumericParam::SynthRelease: {
      static const NumericParamSpec s {"synth.release", "Release",
                                       "Seconds, 0-8", 0.0, 8.0, 3, false};
      return &s;
    }
    case NumericParam::FdsDepth: {
      static const NumericParamSpec s {"fds.depth", "Mod depth",
                                       "FDS hardware gain range, 0-63", 0.0, 63.0, 0, true};
      return &s;
    }
    case NumericParam::FdsRatio: {
      static const NumericParamSpec s {"fds.ratio", "Mod ratio",
                                       "Modulator as a ratio of the note, 0.05-16",
                                       0.05, 16.0, 2, false};
      return &s;
    }
    case NumericParam::FdsRetrig: {
      static const NumericParamSpec s {"fds.retrig", "Retrigger",
                                       "Seconds between retriggers, 0-8", 0.0, 8.0, 2, false};
      return &s;
    }
    case NumericParam::PortalBlobs: {
      static const NumericParamSpec s {"portal.blobs", "Blobs",
                                       "How many are alive at once, 3-48", 3.0, 48.0, 0, true, 1.0};
      return &s;
    }
    case NumericParam::PortalSize: {
      static const NumericParamSpec s {"portal.size", "Size",
                                       "How big a blob grows, 0-1", 0.0, 1.0, 2, false, 0.05};
      return &s;
    }
    case NumericParam::PortalBlend: {
      static const NumericParamSpec s {"portal.melt", "Melt",
                                       "How readily neighbours merge, 0 (never) to 1",
                                       0.0, 1.0, 2, false, 0.05};
      return &s;
    }
    case NumericParam::PortalOutline: {
      static const NumericParamSpec s {"portal.rim", "Rim",
                                       "Thickness of the glowing edge, 0-1", 0.0, 1.0, 2, false, 0.05};
      return &s;
    }
    case NumericParam::PortalSpeed: {
      static const NumericParamSpec s {"portal.speed", "Speed",
                                       "Birth, drift and fade rate, 0.1-3", 0.1, 3.0, 2, false, 0.1};
      return &s;
    }
    case NumericParam::PortalHue: {
      static const NumericParamSpec s {"portal.hue", "Colour",
                                       "Turns the rim's colours round the wheel, 0-1",
                                       0.0, 1.0, 2, false, 0.05};
      return &s;
    }
    case NumericParam::MaskX: {
      static const NumericParamSpec s {"mask.x", "Mask X", "Across, % of the width, -100 to 100", -100, 100, 0, false, 1};
      return &s;
    }
    case NumericParam::MaskY: {
      static const NumericParamSpec s {"mask.y", "Mask Y", "Down, % of the height, -100 to 100", -100, 100, 0, false, 1};
      return &s;
    }
    case NumericParam::MaskScale: {
      static const NumericParamSpec s {"mask.scale", "Mask scale", "Size, %, 5 to 400", 5, 400, 0, false, 5};
      return &s;
    }
    case NumericParam::MaskRotation: {
      static const NumericParamSpec s {"mask.rotation", "Mask rotation", "Degrees, -360 to 360", -360, 360, 0, false,
                                       5};
      return &s;
    }
    case NumericParam::MaskFeather: {
      static const NumericParamSpec s {"mask.feather", "Feather", "Edge softness, % of the height, 0 to 25", 0, 25, 1,
                                       false, 0.5};
      return &s;
    }
    case NumericParam::MaskGrow: {
      static const NumericParamSpec s {"mask.grow", "Grow", "Edge out (+) or in (-), % of the height, -25 to 25", -25,
                                       25, 1, false, 0.5};
      return &s;
    }
    case NumericParam::DrawSize: {
      static const NumericParamSpec s {"draw.size", "Brush size", "% of the picture's height, 0.1 to 20", 0.1, 20, 1,
                                       false, 0.2};
      return &s;
    }
    case NumericParam::DrawOpacity: {
      static const NumericParamSpec s {"draw.opacity", "Brush opacity", "%, 1 to 100", 1, 100, 0, false, 5};
      return &s;
    }
    case NumericParam::DrawFps: {
      static const NumericParamSpec s {"draw.fps", "Flipbook rate", "Frames a second, 1 to 60", 1, 60, 0, true, 1};
      return &s;
    }
    case NumericParam::DrawHold: {
      static const NumericParamSpec s {"draw.hold", "Replay hold", "Seconds the finished drawing stays, 0 to 60", 0,
                                       60, 1, false, 0.5};
      return &s;
    }
    case NumericParam::MaskOpacity: {
      static const NumericParamSpec s {"mask.opacity", "Mask opacity", "How hard it cuts, %, 0 to 100", 0, 100, 0,
                                       false, 5};
      return &s;
    }
    case NumericParam::LedPanelWidth: {
      static const NumericParamSpec s {"panel.w", "Tile width",
                                       "One LED tile's width in pixels, 8-8192",
                                       8.0, 8192.0, 0, true, 1.0};
      return &s;
    }
    case NumericParam::LedPanelHeight: {
      static const NumericParamSpec s {"panel.h", "Tile height",
                                       "One LED tile's height in pixels, 8-8192",
                                       8.0, 8192.0, 0, true, 1.0};
      return &s;
    }
    case NumericParam::L3Height: {
      static const NumericParamSpec s {"l3.height", "Height",
                                       "Space under it, as a fraction of the frame, 0-0.8",
                                       0.0, 0.8, 2, false, 0.01};
      return &s;
    }
    case NumericParam::L3Size: {
      static const NumericParamSpec s {"l3.size", "Size",
                                       "Scale of the whole graphic, 0.5-2", 0.5, 2.0, 2, false, 0.05};
      return &s;
    }
    case NumericParam::L3InSeconds: {
      static const NumericParamSpec s {"l3.in", "In time",
                                       "Seconds the in move takes, 0-5", 0.0, 5.0, 1, false, 0.1};
      return &s;
    }
    case NumericParam::L3OutSeconds: {
      static const NumericParamSpec s {"l3.out", "Out time",
                                       "Seconds the out move takes, 0-5", 0.0, 5.0, 1, false, 0.1};
      return &s;
    }
    case NumericParam::L3HoldSeconds: {
      static const NumericParamSpec s {"l3.hold", "On screen",
                                       "Seconds before it leaves by itself; 0 waits for OUT",
                                       0.0, 600.0, 0, false, 1.0};
      return &s;
    }
    default:
      return isfParamSpec(cue, id);
  }
}

inline bool readNumericParam(const Cue& cue, NumericParam id, double& out) {
  const VideoSynthSettings& v = cue.videoSynth;
  switch (id) {
    case NumericParam::SynthReference: out = cue.tone.synth.referenceHz; return true;

    case NumericParam::StingPitch: out = cue.tone.stingPitchHz * 1.0; return true;
    case NumericParam::StingLen: out = cue.tone.stingSeconds * 1.0; return true;
    case NumericParam::StingSweep: out = cue.tone.stingSweepSemitones * 1.0; return true;
    case NumericParam::StingBody: out = cue.tone.stingBody * 100.0; return true;
    case NumericParam::VoicePitch: out = cue.tone.voicePitchHz * 1.0; return true;
    case NumericParam::VoiceSize: out = cue.tone.voiceSize * 1.0; return true;
    case NumericParam::VoiceBreath: out = cue.tone.voiceBreath * 100.0; return true;
    case NumericParam::ToneChannel: out = cue.tone.channel * 1.0; return true;
    case NumericParam::VsTileW: out = cue.videoSynth.spriteTileW * 1.0; return true;
    case NumericParam::VsTileH: out = cue.videoSynth.spriteTileH * 1.0; return true;
    case NumericParam::VsJitter: out = cue.videoSynth.spriteJitter * 1.0; return true;
    case NumericParam::VsChaos: out = cue.videoSynth.spriteChaos * 1.0; return true;

    case NumericParam::TimerDuration: out = cue.timer.durationSeconds; return true;
    case NumericParam::TimerAmber: out = cue.timer.amberSeconds; return true;
    case NumericParam::TimerRed: out = cue.timer.redSeconds; return true;
    case NumericParam::TimerLogoSize: out = cue.timer.logoHeightPercent; return true;
    case NumericParam::FireIntensity: out = cue.firesideIntensity; return true;
    case NumericParam::FireSparks: out = cue.firesideSparks; return true;

    case NumericParam::SwirlSpeed:
    case NumericParam::SwirlTwist:
    case NumericParam::SwirlCells:
    case NumericParam::SwirlHue:
    case NumericParam::SwirlZoom:
    case NumericParam::SwirlFlow:
    case NumericParam::SwirlCentreX:
    case NumericParam::SwirlCentreY:
    case NumericParam::SwirlBands:
    case NumericParam::SwirlPalette:
      if (cue.kind != CueKind::Pattern || normalizePatternTypeId(cue.path) != "swirl") return false;
      out = id == NumericParam::SwirlSpeed ? cue.swirl.speed
          : id == NumericParam::SwirlTwist ? cue.swirl.twist
          : id == NumericParam::SwirlCells ? cue.swirl.cells
          : id == NumericParam::SwirlHue ? cue.swirl.hue
          : id == NumericParam::SwirlZoom ? cue.swirl.zoom
          : id == NumericParam::SwirlFlow ? cue.swirl.flow
          : id == NumericParam::SwirlCentreX ? cue.swirl.centreX
          : id == NumericParam::SwirlCentreY ? cue.swirl.centreY
          : id == NumericParam::SwirlBands ? cue.swirl.bands : cue.swirl.palette;
      return true;
    case NumericParam::MoireShape:
    case NumericParam::MoirePalette:
    case NumericParam::MoireDensity:
    case NumericParam::MoireSpeed:
    case NumericParam::MoireWarp:
    case NumericParam::MoireTurn:
    case NumericParam::MoireCentreX:
    case NumericParam::MoireCentreY:
    case NumericParam::MoireGrey:
    case NumericParam::MoireHue: {
      if (cue.kind != CueKind::Pattern || normalizePatternTypeId(cue.path) != "moire-field") return false;
      const MoireSettings& m = cue.moire;
      out = id == NumericParam::MoireShape ? m.shape
          : id == NumericParam::MoirePalette ? m.palette
          : id == NumericParam::MoireDensity ? m.density * 100.0
          : id == NumericParam::MoireSpeed ? m.speed * 100.0
          : id == NumericParam::MoireWarp ? m.warp * 100.0
          : id == NumericParam::MoireTurn ? m.turn * 360.0
          : id == NumericParam::MoireCentreX ? m.centreX * 100.0
          : id == NumericParam::MoireCentreY ? m.centreY * 100.0
          : id == NumericParam::MoireGrey ? m.grey * 100.0
          : m.hue * 360.0;
      return true;
    }
    case NumericParam::IllusionShape:
    case NumericParam::IllusionForm:
    case NumericParam::IllusionFlow:
    case NumericParam::IllusionFlip:
    case NumericParam::IllusionPalette:
    case NumericParam::IllusionSize:
    case NumericParam::IllusionCount:
    case NumericParam::IllusionEdge:
    case NumericParam::IllusionSpeed:
    case NumericParam::IllusionCentreX:
    case NumericParam::IllusionCentreY:
    case NumericParam::IllusionTone:
    case NumericParam::IllusionHue1:
    case NumericParam::IllusionHue2: {
      if (cue.kind != CueKind::Pattern || normalizePatternTypeId(cue.path) != "illusion") return false;
      const IllusionSettings& il = cue.illusion;
      switch (id) {
        case NumericParam::IllusionShape: out = il.shape; break;
        case NumericParam::IllusionForm: out = il.form; break;
        case NumericParam::IllusionFlow: out = il.flow; break;
        case NumericParam::IllusionFlip: out = il.flip ? 1.0 : 0.0; break;
        case NumericParam::IllusionPalette: out = il.palette; break;
        case NumericParam::IllusionSize: out = il.size * 100.0; break;
        case NumericParam::IllusionCount: out = il.count * 100.0; break;
        case NumericParam::IllusionEdge: out = il.edge * 100.0; break;
        case NumericParam::IllusionSpeed: out = il.speed * 100.0; break;
        case NumericParam::IllusionCentreX: out = il.centreX * 100.0; break;
        case NumericParam::IllusionCentreY: out = il.centreY * 100.0; break;
        case NumericParam::IllusionTone: out = il.tone * 100.0; break;
        case NumericParam::IllusionHue1: out = il.hue1 * 360.0; break;
        case NumericParam::IllusionHue2: out = il.hue2 * 360.0; break;
        default: break;
      }
      return true;
    }
    case NumericParam::LumiaPalette:
    case NumericParam::LumiaMode:
    case NumericParam::LumiaTopBottom:
    case NumericParam::LumiaListens:
    case NumericParam::LumiaFlow:
    case NumericParam::LumiaWater:
    case NumericParam::LumiaRipple:
    case NumericParam::LumiaJet:
    case NumericParam::LumiaPrism:
    case NumericParam::LumiaSplit:
    case NumericParam::LumiaFocus:
    case NumericParam::LumiaShimmer:
    case NumericParam::LumiaMirrors:
    case NumericParam::LumiaAngle:
    case NumericParam::LumiaSpin:
    case NumericParam::LumiaLens:
    case NumericParam::LumiaGlow:
    case NumericParam::LumiaBright:
    case NumericParam::LumiaTrail: {
      if (cue.kind != CueKind::Pattern || normalizePatternTypeId(cue.path) != "lumia") return false;
      const LumiaSettings& l = cue.lumia;
      switch (id) {
        case NumericParam::LumiaPalette: out = l.palette; break;
        case NumericParam::LumiaMode: out = l.mode; break;
        case NumericParam::LumiaTopBottom: out = l.topBottom ? 1.0 : 0.0; break;
        case NumericParam::LumiaListens: out = l.listens ? 1.0 : 0.0; break;
        case NumericParam::LumiaFlow: out = l.flow * 100.0; break;
        case NumericParam::LumiaWater: out = l.water * 100.0; break;
        case NumericParam::LumiaRipple: out = l.ripple * 100.0; break;
        case NumericParam::LumiaJet: out = l.jet * 100.0; break;
        case NumericParam::LumiaPrism: out = l.prism * 100.0; break;
        case NumericParam::LumiaSplit: out = l.split * 100.0; break;
        case NumericParam::LumiaFocus: out = l.focus * 100.0; break;
        case NumericParam::LumiaShimmer: out = l.shimmer * 100.0; break;
        case NumericParam::LumiaMirrors: out = std::round(l.mirrors * 12.0); break;
        case NumericParam::LumiaAngle: out = l.angle * 100.0; break;
        case NumericParam::LumiaSpin: out = l.spin * 100.0; break;
        case NumericParam::LumiaLens: out = l.lens * 100.0; break;
        case NumericParam::LumiaGlow: out = l.glow * 100.0; break;
        case NumericParam::LumiaBright: out = l.bright * 100.0; break;
        case NumericParam::LumiaTrail: out = l.trail * 100.0; break;
        default: break;
      }
      return true;
    }
    case NumericParam::CodeWidth:
    case NumericParam::CodeHeight:
      if (!cueIsCodeSource(cue)) return false;
      out = id == NumericParam::CodeWidth ? cue.width : cue.height;
      return true;
    case NumericParam::VsSpeed:      out = v.speed; return true;
    case NumericParam::VsScale:      out = v.scale; return true;
    case NumericParam::VsFeedback:   out = v.feedbackAmount; return true;
    case NumericParam::VsZoom:       out = v.feedbackZoom; return true;
    case NumericParam::VsReact:      out = v.audioReactivity; return true;
    case NumericParam::VsResolution: out = v.resolution; return true;
    case NumericParam::VsPixelSort:  out = v.pixelSort; return true;
    case NumericParam::VsGlitch:     out = v.glitch; return true;
    case NumericParam::VsCrt:        out = v.crt; return true;
    case NumericParam::VsFreeAngle:  out = v.spriteFreeAngle; return true;
    case NumericParam::ToneLevel:    out = cue.tone.levelDbfs; return true;
    case NumericParam::ToneFreq:     out = cue.tone.frequencyHz; return true;
    case NumericParam::DrawFps:      out = cue.drawing.fps; return true;
    case NumericParam::DrawHold:     out = cue.drawing.holdSeconds; return true;
    case NumericParam::MotionDriverSpeed: out = cue.motionDriverSpeed; return true;
    case NumericParam::SynthNote:    out = cue.tone.synth.noteHz; return true;
    case NumericParam::SynthAttack:  out = cue.tone.synth.attackSeconds; return true;
    case NumericParam::SynthRelease: out = cue.tone.synth.releaseSeconds; return true;
    case NumericParam::FdsDepth:     out = cue.tone.synth.modDepth; return true;
    case NumericParam::FdsRatio:     out = cue.tone.synth.modRatio; return true;
    case NumericParam::FdsRetrig:    out = cue.tone.synth.retriggerSeconds; return true;
    // Only on a portal: the other pattern cues carry these fields too (every
    // Cue does), and a stale row must not write into a colour-bars cue.
    case NumericParam::PortalBlobs:
    case NumericParam::PortalSize:
    case NumericParam::PortalBlend:
    case NumericParam::PortalOutline:
    case NumericParam::PortalSpeed:
    case NumericParam::PortalHue: {
      if (cue.kind != CueKind::Pattern || normalizePatternTypeId(cue.path) != "portal") {
        return false;
      }
      const PortalSettings& p = cue.portal;
      out = id == NumericParam::PortalBlobs ? p.blobs
          : id == NumericParam::PortalSize ? p.size
          : id == NumericParam::PortalBlend ? p.blend
          : id == NumericParam::PortalOutline ? p.outline
          : id == NumericParam::PortalSpeed ? p.speed
          : p.hue;
      return true;
    }
    // Only on a panel map, for the same reason as the portal's rows above.
    case NumericParam::LedPanelWidth:
    case NumericParam::LedPanelHeight: {
      if (cue.kind != CueKind::Pattern || stripPatternMotionSuffix(normalizePatternTypeId(cue.path)) != "panel-map") {
        return false;
      }
      out = id == NumericParam::LedPanelWidth ? cue.ledPanelWidth : cue.ledPanelHeight;
      return true;
    }
    case NumericParam::L3Height:
    case NumericParam::L3Size:
    case NumericParam::L3InSeconds:
    case NumericParam::L3OutSeconds:
    case NumericParam::L3HoldSeconds: {
      if (cue.kind != CueKind::Text || !cue.lowerThird.on) {
        return false;
      }
      const LowerThirdDesign& l = cue.lowerThird;
      out = id == NumericParam::L3Height ? l.height
          : id == NumericParam::L3Size ? l.size
          : id == NumericParam::L3InSeconds ? l.inSeconds
          : id == NumericParam::L3OutSeconds ? l.outSeconds
          : l.holdSeconds;
      return true;
    }
    default: return readIsfParam(cue, id, out);
  }
}

inline void writeNumericParam(Cue& cue, NumericParam id, double value) {
  VideoSynthSettings& v = cue.videoSynth;
  switch (id) {
    case NumericParam::SynthReference: cue.tone.synth.referenceHz = std::clamp(value, 380.0, 480.0); break;

    case NumericParam::StingPitch: cue.tone.stingPitchHz = std::clamp(value, 20.0, 12000.0) / 1.0; break;
    case NumericParam::StingLen: cue.tone.stingSeconds = std::clamp(value, 0.05, 10.0) / 1.0; break;
    case NumericParam::StingSweep: cue.tone.stingSweepSemitones = std::clamp(value, -24.0, 24.0) / 1.0; break;
    case NumericParam::StingBody: cue.tone.stingBody = std::clamp(value, 0.0, 100.0) / 100.0; break;
    case NumericParam::VoicePitch: cue.tone.voicePitchHz = std::clamp(value, 50.0, 400.0) / 1.0; break;
    case NumericParam::VoiceSize: cue.tone.voiceSize = std::clamp(value, 0.8, 1.25) / 1.0; break;
    case NumericParam::VoiceBreath: cue.tone.voiceBreath = std::clamp(value, 0.0, 100.0) / 100.0; break;
    case NumericParam::ToneChannel: cue.tone.channel = std::clamp(static_cast<int>(std::lround(value)), -1, 15) / 1; break;
    case NumericParam::VsTileW: cue.videoSynth.spriteTileW = std::clamp(static_cast<int>(std::lround(value)), 8, 128) / 1; break;
    case NumericParam::VsTileH: cue.videoSynth.spriteTileH = std::clamp(static_cast<int>(std::lround(value)), 8, 128) / 1; break;
    case NumericParam::VsJitter: cue.videoSynth.spriteJitter = std::clamp(value, 0.0, 1.0) / 1.0; break;
    case NumericParam::VsChaos: cue.videoSynth.spriteChaos = std::clamp(value, 0.0, 1.0) / 1.0; break;

    case NumericParam::TimerDuration: cue.timer.durationSeconds = std::clamp(static_cast<int>(std::lround(value)), 0, 86400); cue.timer.amberSeconds = std::min(cue.timer.amberSeconds, cue.timer.durationSeconds); cue.timer.redSeconds = std::min(cue.timer.redSeconds, cue.timer.amberSeconds); break;
    case NumericParam::TimerAmber: cue.timer.amberSeconds = std::clamp(static_cast<int>(std::lround(value)), 0, 86400); cue.timer.amberSeconds = std::min(cue.timer.amberSeconds, cue.timer.durationSeconds); cue.timer.redSeconds = std::min(cue.timer.redSeconds, cue.timer.amberSeconds); break;
    case NumericParam::TimerRed: cue.timer.redSeconds = std::clamp(static_cast<int>(std::lround(value)), 0, 86400); cue.timer.amberSeconds = std::min(cue.timer.amberSeconds, cue.timer.durationSeconds); cue.timer.redSeconds = std::min(cue.timer.redSeconds, cue.timer.amberSeconds); break;
    case NumericParam::TimerLogoSize: cue.timer.logoHeightPercent = std::clamp(static_cast<int>(std::lround(value)), 2, 40); break;
    case NumericParam::FireIntensity: cue.firesideIntensity = std::clamp(value, 0.2, 2.0); break;
    case NumericParam::FireSparks: cue.firesideSparks = std::clamp(static_cast<int>(std::lround(value)), 0, 160); break;

    case NumericParam::DrawFps: cue.drawing.fps = static_cast<float>(value); ++cue.drawing.revision; break;
    case NumericParam::DrawHold: cue.drawing.holdSeconds = static_cast<float>(value); ++cue.drawing.revision; break;
    case NumericParam::SwirlSpeed: cue.swirl.speed = value; break;
    case NumericParam::SwirlTwist: cue.swirl.twist = value; break;
    case NumericParam::SwirlCells: cue.swirl.cells = static_cast<int>(std::lround(value)); break;
    case NumericParam::SwirlHue: cue.swirl.hue = value; break;
    case NumericParam::SwirlZoom: cue.swirl.zoom = value; break;
    case NumericParam::SwirlFlow: cue.swirl.flow = value; break;
    case NumericParam::SwirlCentreX: cue.swirl.centreX = value; break;
    case NumericParam::SwirlCentreY: cue.swirl.centreY = value; break;
    case NumericParam::SwirlBands: cue.swirl.bands = static_cast<int>(std::lround(value)); break;
    case NumericParam::SwirlPalette: cue.swirl.palette = static_cast<int>(std::lround(value)); break;
    case NumericParam::MoireShape:
      cue.moire.shape = std::clamp(static_cast<int>(std::lround(value)), 0, kMoireShapeCount - 1);
      break;
    case NumericParam::MoirePalette:
      cue.moire.palette = std::clamp(static_cast<int>(std::lround(value)), 0, kMoirePaletteCount - 1);
      // THE THEME PALETTE IS CAPTURED WHEN IT IS PICKED, so the cue keeps the
      // colours it was made with on a desk wearing another theme.

      break;
    case NumericParam::MoireDensity: cue.moire.density = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::MoireSpeed: cue.moire.speed = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::MoireWarp: cue.moire.warp = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::MoireTurn: cue.moire.turn = std::clamp(value / 360.0, 0.0, 1.0); break;
    case NumericParam::MoireCentreX: cue.moire.centreX = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::MoireCentreY: cue.moire.centreY = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::MoireGrey: cue.moire.grey = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::MoireHue: cue.moire.hue = std::clamp(value / 360.0, 0.0, 1.0); break;
    case NumericParam::IllusionShape: cue.illusion.shape = std::clamp(static_cast<int>(std::lround(value)), 0, kIllusionShapeCount - 1); break;
    case NumericParam::IllusionForm: cue.illusion.form = std::clamp(static_cast<int>(std::lround(value)), 0, kIllusionFormCount - 1); break;
    case NumericParam::IllusionFlow: cue.illusion.flow = std::clamp(static_cast<int>(std::lround(value)), 0, kIllusionFlowCount - 1); break;
    case NumericParam::IllusionFlip: cue.illusion.flip = value >= 0.5; break;
    case NumericParam::IllusionPalette:
      cue.illusion.palette = std::clamp(static_cast<int>(std::lround(value)), 0, kIllusionPaletteCount - 1);
      // Captured when picked, as Moire's: the cue keeps the colours it was made with.

      break;
    case NumericParam::IllusionSize: cue.illusion.size = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::IllusionCount: cue.illusion.count = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::IllusionEdge: cue.illusion.edge = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::IllusionSpeed: cue.illusion.speed = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::IllusionCentreX: cue.illusion.centreX = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::IllusionCentreY: cue.illusion.centreY = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::IllusionTone: cue.illusion.tone = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::IllusionHue1: cue.illusion.hue1 = std::clamp(value / 360.0, 0.0, 1.0); break;
    case NumericParam::IllusionHue2: cue.illusion.hue2 = std::clamp(value / 360.0, 0.0, 1.0); break;
    case NumericParam::LumiaPalette: cue.lumia.palette = std::clamp(static_cast<int>(std::lround(value)), 0, kLumiaPaletteCount - 1); break;
    case NumericParam::LumiaMode: cue.lumia.mode = std::clamp(static_cast<int>(std::lround(value)), 0, kLumiaModeCount - 1); break;
    case NumericParam::LumiaTopBottom: cue.lumia.topBottom = value >= 0.5; break;
    case NumericParam::LumiaListens: cue.lumia.listens = value >= 0.5; break;
    case NumericParam::LumiaFlow: cue.lumia.flow = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaWater: cue.lumia.water = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaRipple: cue.lumia.ripple = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaJet: cue.lumia.jet = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaPrism: cue.lumia.prism = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaSplit: cue.lumia.split = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaFocus: cue.lumia.focus = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaShimmer: cue.lumia.shimmer = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaMirrors: cue.lumia.mirrors = std::clamp(value / 12.0, 0.0, 1.0); break;
    case NumericParam::LumiaAngle: cue.lumia.angle = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaSpin: cue.lumia.spin = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaLens: cue.lumia.lens = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaGlow: cue.lumia.glow = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaBright: cue.lumia.bright = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::LumiaTrail: cue.lumia.trail = std::clamp(value / 100.0, 0.0, 1.0); break;
    case NumericParam::CodeWidth: cue.width = static_cast<int>(std::lround(value)); break;
    case NumericParam::CodeHeight: cue.height = static_cast<int>(std::lround(value)); break;
    case NumericParam::VsSpeed:      v.speed = value; break;
    case NumericParam::VsScale:      v.scale = value; break;
    case NumericParam::VsFeedback:   v.feedbackAmount = value; break;
    case NumericParam::VsZoom:       v.feedbackZoom = value; break;
    case NumericParam::VsReact:      v.audioReactivity = value; break;
    case NumericParam::VsResolution: v.resolution = static_cast<int>(std::lround(value)); break;
    case NumericParam::VsPixelSort:  v.pixelSort = value; break;
    case NumericParam::VsGlitch:     v.glitch = value; break;
    case NumericParam::VsCrt:        v.crt = value; break;
    case NumericParam::VsFreeAngle:  v.spriteFreeAngle = value; break;
    case NumericParam::ToneLevel:    cue.tone.levelDbfs = value; break;
    case NumericParam::ToneFreq:     cue.tone.frequencyHz = value; break;
    case NumericParam::MotionDriverSpeed:
      cue.motionDriverSpeed = static_cast<float>(value); break;
    case NumericParam::SynthNote:    cue.tone.synth.noteHz = value; break;
    case NumericParam::SynthAttack:  cue.tone.synth.attackSeconds = value; break;
    case NumericParam::SynthRelease: cue.tone.synth.releaseSeconds = value; break;
    case NumericParam::FdsDepth:     cue.tone.synth.modDepth = static_cast<int>(std::lround(value)); break;
    case NumericParam::FdsRatio:     cue.tone.synth.modRatio = value; break;
    case NumericParam::FdsRetrig:    cue.tone.synth.retriggerSeconds = value; break;
    case NumericParam::PortalBlobs:  cue.portal.blobs = static_cast<int>(std::lround(value)); break;
    case NumericParam::PortalSize:   cue.portal.size = value; break;
    case NumericParam::PortalBlend:  cue.portal.blend = value; break;
    case NumericParam::PortalOutline: cue.portal.outline = value; break;
    case NumericParam::PortalSpeed:  cue.portal.speed = value; break;
    case NumericParam::PortalHue:    cue.portal.hue = value; break;
    case NumericParam::L3Height:      cue.lowerThird.height = value; break;
    case NumericParam::L3Size:        cue.lowerThird.size = value; break;
    case NumericParam::L3InSeconds:   cue.lowerThird.inSeconds = value; break;
    case NumericParam::L3OutSeconds:  cue.lowerThird.outSeconds = value; break;
    case NumericParam::L3HoldSeconds: cue.lowerThird.holdSeconds = value; break;
    case NumericParam::LedPanelWidth:  cue.ledPanelWidth = static_cast<int>(std::lround(value)); break;
    case NumericParam::LedPanelHeight: cue.ledPanelHeight = static_cast<int>(std::lround(value)); break;
    default: writeIsfParam(cue, id, value); break;
  }
}

// Keys/tips are untranslated desk text. Hosts translate at drawing time.
// Optional headings use None; the default list contains only editable parameters.
// ISF row strings live until the next enumeration on this thread; copy to retain.
struct ParamRow { const char* key; NumericParam id; const char* tip; };
inline std::vector<ParamRow> paramRowsForCue(const Cue& cue, bool includeHeadings = false) {
  std::vector<ParamRow> rows;
  const auto type = normalizePatternTypeId(cue.path);
  if (cue.kind == CueKind::Timer) {
    if (!cue.timer.logoPath.empty()) rows.push_back({"logo size", NumericParam::TimerLogoSize, "How tall the logo is above the clock"});
    rows.push_back({"duration", NumericParam::TimerDuration, "Total time"});
    rows.push_back({"amber at", NumericParam::TimerAmber, "Seconds REMAINING when the clock turns amber"});
    rows.push_back({"red at", NumericParam::TimerRed, "Seconds REMAINING when the clock turns red"});
    return rows;
  }
  if (cue.kind == CueKind::Pattern && type == "fireside") {
    rows.push_back({"burns", NumericParam::FireIntensity, "Embers to roaring. It still surges and settles at any setting -- a fire that does not move reads as a photo"});
    rows.push_back({"sparks", NumericParam::FireSparks, "How much it throws off; 0 is a clean burn"});
    return rows;
  }

  if (cue.kind == CueKind::Pattern && type == "swirl") {
    rows = {
      {"palette", NumericParam::SwirlPalette, "Classic green, fire, ice, neon or mono"},
      {"colour", NumericParam::SwirlHue, "Turn the palette through all colours; 0 restores it"},
      {"bands", NumericParam::SwirlBands, "Colour steps: 2 is stark, 8 is smooth"},
      {"speed", NumericParam::SwirlSpeed, "0 holds the field; negative runs it backwards"},
      {"twist", NumericParam::SwirlTwist, "Spiral strength, from a flowing field to tight turns"},
      {"flow", NumericParam::SwirlFlow, "Turbulence: 0 is still ripples, 3 boils"},
      {"zoom", NumericParam::SwirlZoom, "Spiral size: above 1 is bigger"},
      {"centre x", NumericParam::SwirlCentreX, "Where the eye of the swirl sits, left to right"},
      {"centre y", NumericParam::SwirlCentreY, "Where the eye of the swirl sits, top to bottom"},
      {"pixels", NumericParam::SwirlCells, "Horizontal cells. More detail uses more CPU"},
    };
    return rows;
  }
  if (cue.kind == CueKind::Pattern && type == "moire-field") {
    rows = {
      {"shape", NumericParam::MoireShape, "Zone, twin, flow, saddle, ripple or shear"},
      {"density", NumericParam::MoireDensity, "How many bands: 50% is the design, higher beats harder against the pixels"},
      {"speed", NumericParam::MoireSpeed, "How fast the bands move; 0 holds the field still"},
      {"warp", NumericParam::MoireWarp, "How far the field bends"},
      {"turn", NumericParam::MoireTurn, "Rotate the field round its centre"},
      {"centre x", NumericParam::MoireCentreX, "Where the field's centre sits, left to right"},
      {"centre y", NumericParam::MoireCentreY, "Where the field's centre sits, top to bottom"},
      {"grey", NumericParam::MoireGrey, "The share of each band drawn as grey stripes"},
      {"hue", NumericParam::MoireHue, "Turn the colours round the wheel"},
      {"palette", NumericParam::MoirePalette, "Rainbow + grey, rainbow, the theme's colours (taken when picked) or mono"},
    };
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const ParamRow& row) { return (row.id == NumericParam::MoireGrey && cue.moire.palette == 1) || (row.id == NumericParam::MoireHue && cue.moire.palette == 3); }), rows.end());
    return rows;
  }
  if (cue.kind == CueKind::Pattern && type == "illusion") {
    const auto& il = cue.illusion;
          rows.push_back({"shape", NumericParam::IllusionShape,
                          "Drift (still shapes that seem to turn), ouchi (a disc that floats), cafe (straight rows that look tilted), "
                          "vanish (dots that disappear while you stare), grid (dots that flicker and are not there) or "
                          "dancer (it spins whichever way you see it)"});
          if (il.shape == 0) {
            rows.push_back({"form", NumericParam::IllusionForm, "Leaf, petal, scale or ring"});
            rows.push_back({"flow", NumericParam::IllusionFlow, "Which way the field seems to go: turn round the centre, out, in, or in waves"});
            rows.push_back({"count", NumericParam::IllusionCount, "How many shapes across the screen"});
            rows.push_back({"size", NumericParam::IllusionSize, "How much of its place each shape fills"});
            rows.push_back({"edge width", NumericParam::IllusionEdge, "How much of the black and white edges shows. They make the drift"});
            rows.push_back({"turn", NumericParam::IllusionSpeed, "Turn the shapes slowly, so the drift changes direction. 0 holds them still"});
            rows.push_back({"centre x", NumericParam::IllusionCentreX, "Where the field's centre sits, left to right"});
            rows.push_back({"centre y", NumericParam::IllusionCentreY, "Where the field's centre sits, top to bottom"});
            rows.push_back({"flip", NumericParam::IllusionFlip, "Swap the black and white edges: the field drifts the other way"});
          } else if (il.shape == 1) {
            rows.push_back({"tile length", NumericParam::IllusionSize, "How long each tile is"});
            rows.push_back({"tile thickness", NumericParam::IllusionCount, "How thick each tile is, against its length"});
            rows.push_back({"disc size", NumericParam::IllusionEdge, "How big the floating disc is"});
            rows.push_back({"wander", NumericParam::IllusionSpeed, "Move the disc in a small loop. 0 holds it still: the eye moves it anyway"});
            rows.push_back({"disc x", NumericParam::IllusionCentreX, "Where the disc sits, left to right"});
            rows.push_back({"disc y", NumericParam::IllusionCentreY, "Where the disc sits, top to bottom"});
          } else if (il.shape == 3) {
            rows.push_back({"crosses", NumericParam::IllusionCount, "How many crosses across the screen"});
            rows.push_back({"cross size", NumericParam::IllusionEdge, "How big each cross is"});
            rows.push_back({"dot size", NumericParam::IllusionSize, "How big the three dots are. Small dots vanish more easily"});
            rows.push_back({"turn", NumericParam::IllusionSpeed, "How fast the crosses turn. They always turn a little: the dots vanish only while they move"});
            rows.push_back({"centre x", NumericParam::IllusionCentreX, "Where the centre point sits, left to right"});
            rows.push_back({"centre y", NumericParam::IllusionCentreY, "Where the centre point sits, top to bottom"});
            rows.push_back({"flip", NumericParam::IllusionFlip, "Turn the crosses the other way"});
          } else if (il.shape == 4) {
            rows.push_back({"squares", NumericParam::IllusionCount, "How many squares across the screen"});
            rows.push_back({"street width", NumericParam::IllusionEdge, "How wide the grey lines between the squares are"});
            rows.push_back({"disc size", NumericParam::IllusionSize, "How big the light discs at the crossings are"});
            rows.push_back({"street grey", NumericParam::IllusionTone, "How light the grey lines are"});
            rows.push_back({"drift", NumericParam::IllusionSpeed, "Slide the grid slowly. 0 holds it still"});
          } else if (il.shape == 5) {
            rows.push_back({"size", NumericParam::IllusionSize, "How tall the dancer is"});
            rows.push_back({"pose", NumericParam::IllusionEdge, "How far the free leg and the arms reach"});
            rows.push_back({"spin", NumericParam::IllusionSpeed, "How fast the dancer turns. It always turns a little"});
            rows.push_back({"centre x", NumericParam::IllusionCentreX, "Where the dancer stands, left to right"});
            rows.push_back({"centre y", NumericParam::IllusionCentreY, "Where the dancer stands, top to bottom"});
            rows.push_back({"flip", NumericParam::IllusionFlip, "Spin the other way. Most people cannot tell which way it turns anyway"});
          } else {
            rows.push_back({"tile size", NumericParam::IllusionSize, "How big each tile is"});
            rows.push_back({"offset", NumericParam::IllusionCount, "How far each row is shifted. This makes the straight rows look tilted"});
            rows.push_back({"mortar", NumericParam::IllusionEdge, "How thick the grey line between the rows is"});
            rows.push_back({"mortar grey", NumericParam::IllusionTone, "How light the grey line is. Mid grey tilts the rows most"});
            rows.push_back({"slide", NumericParam::IllusionSpeed, "Slide the rows back and forth, so the wall bends and straightens. 0 holds it still"});
          }
          rows.push_back({"palette", NumericParam::IllusionPalette,
                          "Classic, the theme's colours (taken when picked) or two colours of your own"});
          if (il.palette == 2) {
            rows.push_back({il.shape == 0 ? "shape colour" : il.shape == 3 ? "dot colour" : "colour 1",
                            NumericParam::IllusionHue1, "The first colour, round the colour wheel"});
            rows.push_back({il.shape == 0 ? "ground colour" : il.shape == 3 ? "cross colour" : "colour 2",
                            NumericParam::IllusionHue2, "The second colour, round the colour wheel"});
          }
    return rows;
  }
  if (cue.kind == CueKind::Pattern && type == "lumia") {
    rows = {
      {"WATER", NumericParam::None, ""},
      {"flow", NumericParam::LumiaFlow, "How fast the water moves"},
      {"water", NumericParam::LumiaWater, "How bright the water's pale light is"},
      {"ripple size", NumericParam::LumiaRipple, "Broad ripples at 0, fine and busy at 100"},
      {"second jet", NumericParam::LumiaJet, "A second jet of water crossing the first"},
      {"PRISM", NumericParam::None, ""},
      {"lasers", NumericParam::LumiaPalette, "The laser colours: rgb, rainbow, floyd, warm, cool or green"},
      {"prism", NumericParam::LumiaPrism, "How bright the laser lines are"},
      {"split", NumericParam::LumiaSplit, "How far the colours fan apart"},
      {"focus", NumericParam::LumiaFocus, "How hard the lines are bent"},
      {"shimmer", NumericParam::LumiaShimmer, "Laser speckle: the focus trembles"},
      {"MIRRORS", NumericParam::None, ""},
      {"mode", NumericParam::LumiaMode, "Tube, mandala or none"},
      {"mirrors", NumericParam::LumiaMirrors, "How many mirror panels, 0 to 12"},
      {"panel angle", NumericParam::LumiaAngle, "How far each panel tilts from the one before"},
      {"top / bottom", NumericParam::LumiaTopBottom, "The top mirrors the bottom round a dark seam"},
      {"spin", NumericParam::LumiaSpin, "Turn the whole picture"},
      {"LIGHT", NumericParam::None, ""},
      {"lens", NumericParam::LumiaLens, "The round lens: a curved picture and a dark rim"},
      {"glow", NumericParam::LumiaGlow, "The halo round the lines"},
      {"brightness", NumericParam::LumiaBright, "How bright the picture is"},
      {"trail", NumericParam::LumiaTrail, "How long light lingers on the screen"},
      {"listens", NumericParam::LumiaListens, "The flow surges with the programme sound"},
    };
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const ParamRow& row) { return (cue.lumia.mode == 2 && (row.id == NumericParam::LumiaMirrors || row.id == NumericParam::LumiaAngle)) || (cue.lumia.mode == 1 && (row.id == NumericParam::LumiaAngle || row.id == NumericParam::LumiaTopBottom)); }), rows.end());
    if (!includeHeadings) rows.erase(std::remove_if(rows.begin(), rows.end(), [](const ParamRow& row) { return row.id == NumericParam::None; }), rows.end());
    return rows;
  }
  if (cue.kind == CueKind::Pattern && type == "portal") {
    rows = {
      {"blobs", NumericParam::PortalBlobs, "How many are alive at once. Each is born, drifts out, grows and fades"},
      {"size", NumericParam::PortalSize, "How big a blob grows"},
      {"melt", NumericParam::PortalBlend, "How readily neighbours merge into one shape. 0 keeps every blob round"},
      {"rim", NumericParam::PortalOutline, "Thickness of the glowing edge"},
      {"speed", NumericParam::PortalSpeed, "How fast they are born, drift and fade"},
      {"colour", NumericParam::PortalHue, "Turns the rim's colours round the wheel -- green, yellow, pink, "
             "violet, blue"},
    };
    return rows;
  }
  if (cue.kind == CueKind::VideoSynth) {
    rows.push_back({"speed", NumericParam::VsSpeed, "Master oscillator rate."});
    rows.push_back({"scale", NumericParam::VsScale, "How many features fit on screen."});
    rows.push_back({"feedback", NumericParam::VsFeedback, "Each frame blended into the next. This is the Hypno "
                     "signature -- at 0 it is a pattern generator, above it "
                     "the picture starts feeding on itself."});
    rows.push_back({"zoom", NumericParam::VsZoom, "Above 1 tunnels inward, below 1 blooms outward."});
    rows.push_back({"audio", NumericParam::VsReact, "How much playing audio widens and brightens the "
                     "pattern. 0 free-runs."});
    rows.push_back({"detail", NumericParam::VsResolution, "Internal resolution, 1 chunkiest to 5 finest. This is an "
                     "aesthetic control as much as a speed one -- the 8-bit "
                     "look comes from big pixels."});
    rows.push_back({"smear", NumericParam::VsPixelSort, "Pixel sort: runs of pixels sorted by brightness within a "
                     "row, which is what makes datamosh look melted."});
    rows.push_back({"glitch", NumericParam::VsGlitch, "Torn scanline bands and RGB separation. In text mode it "
                     "also corrupts CELLS -- whole rows lock to one repeating "
                     "character, the way a real text screen loses sync."});
    rows.push_back({"crt", NumericParam::VsCrt, "Scanlines, phosphor bloom and RGB fringing. The bloom is "
                     "the part that matters -- a phosphor spills sideways, "
                     "which is why a CRT looks like it is emitting light."});
    if (!cue.videoSynth.spriteSheetPath.empty()) rows.push_back({"tile w", NumericParam::VsTileW, "Tile width in pixels. Must match the sheet's grid or "
                       "the slices land across neighbouring sprites."});
    if (!cue.videoSynth.spriteSheetPath.empty()) rows.push_back({"tile h", NumericParam::VsTileH, "Tile height in pixels."});
    if (!cue.videoSynth.spriteSheetPath.empty() && cue.videoSynth.spriteRotate == 5) rows.push_back({"spin", NumericParam::VsFreeAngle, "Rotation speed, degrees per second."});
    if (!cue.videoSynth.spriteSheetPath.empty()) rows.push_back({"jitter", NumericParam::VsJitter, "Vary tile size per cell, so the grid stops looking "
                       "like a grid."});
    if (!cue.videoSynth.spriteSheetPath.empty()) rows.push_back({"chaos", NumericParam::VsChaos, "0 picks tiles by brightness so the picture reads. 1 "
                       "picks at random so the grid becomes texture."});
    return rows;
  }
  if (cue.kind == CueKind::Tone) {
    if (cue.tone.waveform == ToneWaveform::Fds) {
    rows.push_back({"note", NumericParam::SynthNote, "Pitch, in semitones."});
    if (cue.tone.synth.chip != SynthChip::Nes) rows.push_back({"depth", NumericParam::FdsDepth, "How far the modulator bends the pitch. "
                       "0-63, the hardware's own gain range."});
    if (cue.tone.synth.chip != SynthChip::Nes) rows.push_back({"ratio", NumericParam::FdsRatio, "Modulator speed as a ratio of the note, "
                       "so the bend tracks pitch."});
    rows.push_back({"A =", NumericParam::SynthReference, "Reference pitch. 440 is modern standard, 432 the common "
                     "alternative, and older instruments sat lower still."});
    rows.push_back({"attack", NumericParam::SynthAttack, "How long the note takes to reach full level."});
    rows.push_back({"release", NumericParam::SynthRelease, "How long it takes to fall away after the "
                     "attack. Only audible when retrigger is set."});
    rows.push_back({"retrigger", NumericParam::FdsRetrig, "Re-strike the envelope this often. Hold "
                     "sustains one note, which is what a drone wants."});
    }
    rows.push_back({"level", NumericParam::ToneLevel, "-18 dBFS is EBU alignment, -20 is SMPTE. Capped below 0: "
                     "this feeds a live PA."});
    if (cue.tone.waveform == ToneWaveform::Sine) rows.push_back({"freq", NumericParam::ToneFreq, "Steps in third-octaves, because the ear works in "
                       "ratios. 1kHz is the convention."});
    if (cue.tone.waveform == ToneWaveform::Sting) rows.push_back({"pitch", NumericParam::StingPitch, "Where the sting starts. Under a voice it wants to stay "
                       "out of the way; over a room it wants to cut through."});
    if (cue.tone.waveform == ToneWaveform::Sting) rows.push_back({"length", NumericParam::StingLen, "The whole gesture. This IS the cue's duration -- a "
                       "sting is the one generated signal here that ends."});
    if (cue.tone.waveform == ToneWaveform::Sting) rows.push_back({"sweep", NumericParam::StingSweep, "How far it travels, in semitones. Up reads as arrival, "
                       "down as departure, zero is a struck note."});
    if (cue.tone.waveform == ToneWaveform::Sting) rows.push_back({"body", NumericParam::StingBody, "Harmonic weight: a pure chime at zero, brass at full. "
                       "This is the difference between a ping and a fanfare."});
    if (cue.tone.waveform == ToneWaveform::Voice && cue.tone.voiceCharacter == "CUSTOM") rows.push_back({"pitch", NumericParam::VoicePitch, "The speaking pitch. Steps in semitones: about 100 Hz is a deep "
                         "voice, 220 Hz a high one."});
    if (cue.tone.waveform == ToneWaveform::Voice && cue.tone.voiceCharacter == "CUSTOM") rows.push_back({"size", NumericParam::VoiceSize, "The length of the throat. 0.8 is a child, 1.25 a giant."});
    if (cue.tone.waveform == ToneWaveform::Voice && cue.tone.voiceCharacter == "CUSTOM") rows.push_back({"breath", NumericParam::VoiceBreath, "Airiness of the voice: 0 is pressed and hard, 100 a whisper."});
    if (cue.tone.waveform != ToneWaveform::Identify) rows.push_back({"output", NumericParam::ToneChannel, "Which output channel carries the signal. 'all' feeds "
                       "every one."});
    return rows;
  }
  if (cue.kind == CueKind::Pattern && stripPatternMotionSuffix(type) == "panel-map") {
    rows.push_back({"tile w", NumericParam::LedPanelWidth, "LED panel width in pixels - what one tile really is. "
                             "Click to type it"});
    rows.push_back({"tile h", NumericParam::LedPanelHeight, "LED panel height in pixels. Click to type it"});
    return rows;
  }
  if (cue.kind == CueKind::Pattern && type == "draw") {
    if (cue.drawing.playback == deckboy::draw::Playback::Flipbook) rows.push_back({"fps", NumericParam::DrawFps, "Frames a second"});
    if (cue.drawing.playback == deckboy::draw::Playback::Replay) rows.push_back({"hold", NumericParam::DrawHold, "How long the finished drawing stays before it is drawn "
                       "again"});
    return rows;
  }
  if (cue.kind == CueKind::Text && cue.lowerThird.on) {
    return {
      {"height", NumericParam::L3Height, "The gap under it, as a share of the frame"},
      {"size", NumericParam::L3Size, "Scale of the whole graphic"},
      {"in time", NumericParam::L3InSeconds, "How long the in move takes"},
      {"out time", NumericParam::L3OutSeconds, "How long the out move takes"},
      {"on screen", NumericParam::L3HoldSeconds, "How long it stays before leaving by itself. 0 waits for OUT"},
    };
  }
  if (cueIsCodeSource(cue)) {
    rows.push_back({"render width", NumericParam::CodeWidth, "Source pixels, independent of output placement"});
    rows.push_back({"render height", NumericParam::CodeHeight, "Source pixels, independent of output placement"});
    return rows;
  }
  if (isShaderCue(cue)) {
    const auto shader = shaderOfCue(cue);
    if (!shader) return rows;
    static thread_local std::string keys[kIsfMaxInputs * 4];
    for (std::size_t i = 0; i < shader->inputs.size() && i < kIsfMaxInputs; ++i) {
      const auto& in = shader->inputs[i];
      if (in.type != deckboy::isf::InputType::Float && in.type != deckboy::isf::InputType::Point2D) continue;
      for (int comp = 0; comp < in.comps; ++comp) {
        const int n = static_cast<int>(i) * 4 + comp;
        keys[n] = in.label + (in.type == deckboy::isf::InputType::Point2D ? (comp == 0 ? " x" : " y") : "");
        rows.push_back({keys[n].c_str(), static_cast<NumericParam>(kIsfParamBase + n), in.type == deckboy::isf::InputType::Float ? "Drag, click to type, or put it on an oscillator" : "Drag or click to type"});
      }
    }
  }
  return rows;
}
inline ParamRow paramRowForCue(const Cue& cue, NumericParam id) {
  if (id == NumericParam::MotionDriverSpeed) return {"driver speed", id, "Fields per rendered frame. 1 is one for one, 0.5 holds "
                         "each field for two frames, 0 freezes it."};
  for (const auto& row : paramRowsForCue(cue)) if (row.id == id) return row;
  return {"", NumericParam::None, ""};
}

inline const char* vsShapeLabel(VideoSynthShape s) {
    switch (s) {
      case VideoSynthShape::Diamond: return "diamond";
      case VideoSynthShape::Rings:   return "rings";
      case VideoSynthShape::Grid:    return "grid";
      case VideoSynthShape::Moire:   return "moire";
      default:                       return "plasma";
    }
  }

inline const char* vsMirrorLabel(VideoSynthMirror m) {
    switch (m) {
      case VideoSynthMirror::Horizontal: return "horizontal";
      case VideoSynthMirror::Quad:       return "quad";
      case VideoSynthMirror::Kaleido:    return "kaleido";
      default:                           return "none";
    }
  }

inline const char* vsPaletteLabel(VideoSynthPalette p) {
    switch (p) {
      case VideoSynthPalette::Amber: return "amber";
      case VideoSynthPalette::Ice:   return "ice";
      case VideoSynthPalette::Fire:  return "fire";
      case VideoSynthPalette::Mono:    return "mono";
      case VideoSynthPalette::Ega:     return "EGA 16";
      case VideoSynthPalette::C64:     return "C64";
      case VideoSynthPalette::Gameboy: return "Game Boy";
      case VideoSynthPalette::Cga:     return "CGA";
      case VideoSynthPalette::Nes:     return "NES";
      case VideoSynthPalette::Vapor:   return "vapor";
      default:                       return "spectrum";
    }
  }

inline const char* toneWaveformLabel(ToneWaveform w) {
    switch (w) {
      case ToneWaveform::Pink:     return "pink noise";
      case ToneWaveform::White:    return "white noise";
      case ToneWaveform::Sweep:    return "sweep";
      case ToneWaveform::Identify: return "identify";
      case ToneWaveform::Fds:      return "FDS synth";
      case ToneWaveform::Sting:    return "sting";
      case ToneWaveform::Voice:    return "voice";
      default:                     return "sine";
    }
  }
inline const char* toneVisualLabel(ToneVisual v) {
    switch (v) {
      case ToneVisual::Scope:     return "scope";
      case ToneVisual::Lissajous: return "lissajous";
      case ToneVisual::Spectrum:  return "spectrum";
      case ToneVisual::Tract:     return "tract";
      default:                    return "off";
    }
  }
inline const char* synthChipLabel(SynthChip c) {
    return c == SynthChip::Nes ? "2A03 (NES)" : "FDS";
  }
inline const char* nesVoiceLabel(NesVoice v) {
    switch (v) {
      case NesVoice::Triangle: return "triangle";
      case NesVoice::Noise:    return "noise";
      default:                 return "pulse";
    }
  }
inline const char* nesDutyLabel(NesDuty d) {
    switch (d) {
      case NesDuty::Eighth:       return "12.5%";
      case NesDuty::Quarter:      return "25%";
      case NesDuty::ThreeQuarter: return "75%";
      default:                    return "50%";
    }
  }
inline const char* fdsCarrierLabel(FdsCarrier c) {
    switch (c) {
      case FdsCarrier::Triangle: return "triangle";
      case FdsCarrier::Pulse25:  return "pulse 25%";
      case FdsCarrier::Saw:      return "saw";
      case FdsCarrier::Additive: return "additive";
      default:                   return "sine";
    }
  }
inline const char* fdsModulatorLabel(FdsModulator m) {
    switch (m) {
      case FdsModulator::Ramp:    return "ramp";
      case FdsModulator::Square:  return "square";
      case FdsModulator::Vibrato: return "vibrato";
      case FdsModulator::Growl:   return "growl";
      default:                    return "off";
    }
  }
inline const char* synthTuningLabel(SynthTuning t) {
    switch (t) {
      case SynthTuning::Just:         return "just";
      case SynthTuning::Pythagorean:  return "pythagorean";
      case SynthTuning::Meantone:     return "meantone";
      case SynthTuning::Equal19:      return "19-TET";
      case SynthTuning::Equal24:      return "24-TET";
      case SynthTuning::BohlenPierce: return "bohlen-pierce";
      default:                        return "12-TET";
    }
  }
inline const char* vsRotateLabel(int r) {
    switch (r) {
      case 1:  return "90";
      case 2:  return "180";
      case 3:  return "270";
      case 4:  return "by brightness";
      case 5:  return "spinning";
      default: return "none";
    }
  }
inline const char* vsFlipLabel(int f) {
    switch (f) {
      case 1:  return "horizontal";
      case 2:  return "vertical";
      case 3:  return "alternating";
      default: return "none";
    }
  }

struct ChoiceRow {
  std::string label;
  std::vector<std::string> options;
  std::function<int(const Cue&)> get;
  std::function<void(Cue&, int)> set;
};
inline std::vector<ChoiceRow> choiceRowsForCue(const Cue& cue) {
  std::vector<ChoiceRow> rows;
  if (cue.kind == CueKind::Tone) {
    std::vector<std::string> options;
    for (int i = 0; i < 8; ++i) options.push_back(toneWaveformLabel(static_cast<ToneWaveform>(i)));
    rows.push_back({"signal", std::move(options), [](const Cue& c) { return static_cast<int>(c.tone.waveform); },
      [](Cue& c, int value) { if (value >= 0 && value < 8) c.tone.waveform = static_cast<ToneWaveform>(value); }});
  }
  if (cue.kind == CueKind::Tone) {
    std::vector<std::string> options;
    const int count = cue.tone.waveform == ToneWaveform::Voice ? 5 : 4;
    for (int i = 0; i < count; ++i) options.push_back(toneVisualLabel(static_cast<ToneVisual>(i)));
    rows.push_back({"display", std::move(options), [](const Cue& c) { return static_cast<int>(c.tone.visual); },
      [count](Cue& c, int value) { if (value >= 0 && value < count) c.tone.visual = static_cast<ToneVisual>(value); }});
  }
  if (cue.kind == CueKind::Tone && cue.tone.waveform == ToneWaveform::Fds) {
    std::vector<std::string> options;
    for (int i = 0; i < 2; ++i) options.push_back(synthChipLabel(static_cast<SynthChip>(i)));
    rows.push_back({"chip", std::move(options), [](const Cue& c) { return static_cast<int>(c.tone.synth.chip); },
      [](Cue& c, int value) { if (value >= 0 && value < 2) c.tone.synth.chip = static_cast<SynthChip>(value); }});
  }
  if (cue.kind == CueKind::Tone && cue.tone.waveform == ToneWaveform::Fds && cue.tone.synth.chip == SynthChip::Nes) {
    std::vector<std::string> options;
    for (int i = 0; i < 3; ++i) options.push_back(nesVoiceLabel(static_cast<NesVoice>(i)));
    rows.push_back({"voice", std::move(options), [](const Cue& c) { return static_cast<int>(c.tone.synth.nesVoice); },
      [](Cue& c, int value) { if (value >= 0 && value < 3) c.tone.synth.nesVoice = static_cast<NesVoice>(value); }});
  }
  if (cue.kind == CueKind::Tone && cue.tone.waveform == ToneWaveform::Fds && cue.tone.synth.chip == SynthChip::Nes && cue.tone.synth.nesVoice == NesVoice::Pulse) {
    std::vector<std::string> options;
    for (int i = 0; i < 4; ++i) options.push_back(nesDutyLabel(static_cast<NesDuty>(i)));
    rows.push_back({"duty", std::move(options), [](const Cue& c) { return static_cast<int>(c.tone.synth.nesDuty); },
      [](Cue& c, int value) { if (value >= 0 && value < 4) c.tone.synth.nesDuty = static_cast<NesDuty>(value); }});
  }
  if (cue.kind == CueKind::Tone && cue.tone.waveform == ToneWaveform::Fds && cue.tone.synth.chip == SynthChip::Fds) {
    std::vector<std::string> options;
    for (int i = 0; i < 5; ++i) options.push_back(fdsCarrierLabel(static_cast<FdsCarrier>(i)));
    rows.push_back({"carrier", std::move(options), [](const Cue& c) { return static_cast<int>(c.tone.synth.carrier); },
      [](Cue& c, int value) { if (value >= 0 && value < 5) c.tone.synth.carrier = static_cast<FdsCarrier>(value); }});
  }
  if (cue.kind == CueKind::Tone && cue.tone.waveform == ToneWaveform::Fds && cue.tone.synth.chip == SynthChip::Fds) {
    std::vector<std::string> options;
    for (int i = 0; i < 5; ++i) options.push_back(fdsModulatorLabel(static_cast<FdsModulator>(i)));
    rows.push_back({"modulator", std::move(options), [](const Cue& c) { return static_cast<int>(c.tone.synth.modulator); },
      [](Cue& c, int value) { if (value >= 0 && value < 5) c.tone.synth.modulator = static_cast<FdsModulator>(value); }});
  }
  if (cue.kind == CueKind::Tone && cue.tone.waveform == ToneWaveform::Fds) {
    std::vector<std::string> options;
    for (int i = 0; i < 7; ++i) options.push_back(synthTuningLabel(static_cast<SynthTuning>(i)));
    rows.push_back({"tuning", std::move(options), [](const Cue& c) { return static_cast<int>(c.tone.synth.tuning); },
      [](Cue& c, int value) { if (value >= 0 && value < 7) c.tone.synth.tuning = static_cast<SynthTuning>(value); }});
  }
  if (cue.kind == CueKind::VideoSynth && !cue.videoSynth.spriteSheetPath.empty()) {
    std::vector<std::string> options;
    for (int i = 0; i < 6; ++i) options.push_back(vsRotateLabel(static_cast<int>(i)));
    rows.push_back({"rotate", std::move(options), [](const Cue& c) { return static_cast<int>(c.videoSynth.spriteRotate); },
      [](Cue& c, int value) { if (value >= 0 && value < 6) c.videoSynth.spriteRotate = static_cast<int>(value); }});
  }
  if (cue.kind == CueKind::VideoSynth && !cue.videoSynth.spriteSheetPath.empty()) {
    std::vector<std::string> options;
    for (int i = 0; i < 4; ++i) options.push_back(vsFlipLabel(static_cast<int>(i)));
    rows.push_back({"flip", std::move(options), [](const Cue& c) { return static_cast<int>(c.videoSynth.spriteFlip); },
      [](Cue& c, int value) { if (value >= 0 && value < 4) c.videoSynth.spriteFlip = static_cast<int>(value); }});
  }
  if (cue.kind == CueKind::VideoSynth) {
    std::vector<std::string> options;
    for (int i = 0; i < 5; ++i) options.push_back(vsShapeLabel(static_cast<VideoSynthShape>(i)));
    rows.push_back({"shape", std::move(options),
      [](const Cue& c) { return static_cast<int>(c.videoSynth.shape); },
      [](Cue& c, int value) { if (value >= 0 && value < 5) c.videoSynth.shape = static_cast<VideoSynthShape>(value); }});
  }
  if (cue.kind == CueKind::VideoSynth) {
    std::vector<std::string> options;
    for (int i = 0; i < 4; ++i) options.push_back(vsMirrorLabel(static_cast<VideoSynthMirror>(i)));
    rows.push_back({"mirror", std::move(options),
      [](const Cue& c) { return static_cast<int>(c.videoSynth.mirror); },
      [](Cue& c, int value) { if (value >= 0 && value < 4) c.videoSynth.mirror = static_cast<VideoSynthMirror>(value); }});
  }
  if (cue.kind == CueKind::VideoSynth) {
    std::vector<std::string> options;
    for (int i = 0; i < 11; ++i) options.push_back(vsPaletteLabel(static_cast<VideoSynthPalette>(i)));
    rows.push_back({"palette", std::move(options),
      [](const Cue& c) { return static_cast<int>(c.videoSynth.palette); },
      [](Cue& c, int value) { if (value >= 0 && value < 11) c.videoSynth.palette = static_cast<VideoSynthPalette>(value); }});
  }
  if (cue.kind == CueKind::Pattern && normalizePatternTypeId(cue.path) == "fireside") {
    std::vector<std::string> options;
    for (int i = 0; i < kFiresideViewCount; ++i) options.push_back(firesideViewLabel(i));
    rows.push_back({"window", std::move(options), [](const Cue& c) { return c.firesideView; },
      [](Cue& c, int value) { if (value >= 0 && value < kFiresideViewCount) c.firesideView = value; }});
  }
  if (cue.kind == CueKind::Timer) {
    rows.push_back({"mode", {"countdown", "count up", "time of day"},
      [](const Cue& c) { return static_cast<int>(c.timer.mode); },
      [](Cue& c, int value) { if (value >= 0 && value < 3) c.timer.mode = static_cast<TimerMode>(value); }});
    rows.push_back({"face", {"7-segment", "blocky", "typeface"},
      [](const Cue& c) { return static_cast<int>(c.timer.face); },
      [](Cue& c, int value) { if (value >= 0 && value < 3) c.timer.face = static_cast<TimerFace>(value); }});
  }
  if (isShaderCue(cue)) {
    const auto shader = shaderOfCue(cue);
    if (shader) for (const auto& in : shader->inputs) {
      if (in.type != deckboy::isf::InputType::Bool && in.type != deckboy::isf::InputType::Long) continue;
      const auto input = in;
      std::vector<std::string> options;
      if (in.type == deckboy::isf::InputType::Bool) options = {"off", "on"};
      else for (std::size_t i = 0; i < in.values.size(); ++i)
        options.push_back(i < in.labels.size() ? in.labels[i] : std::to_string(in.values[i]));
      rows.push_back({in.label.c_str(), std::move(options),
        [input](const Cue& c) {
          const auto* setting = deckboy::isf::findSetting(c, input.name);
          const float value = setting ? setting->v[0] : input.def[0];
          if (input.type == deckboy::isf::InputType::Bool) return value >= 0.5f ? 1 : 0;
          for (std::size_t i = 0; i < input.values.size(); ++i)
            if (static_cast<float>(input.values[i]) == std::round(value)) return static_cast<int>(i);
          return 0;
        },
        [input](Cue& c, int value) {
          if (value < 0) return;
          if (input.type == deckboy::isf::InputType::Bool) {
            if (value < 2) deckboy::isf::ensureSetting(c, input).v[0] = static_cast<float>(value);
          } else if (value < static_cast<int>(input.values.size()))
            deckboy::isf::ensureSetting(c, input).v[0] = static_cast<float>(input.values[static_cast<std::size_t>(value)]);
        }});
    }
  }
  for (const auto& row : paramRowsForCue(cue)) {
    int count = 0;
    const char* (*name)(int) = nullptr;
    switch (row.id) {
      case NumericParam::SwirlPalette: count = 5; name = swirlPaletteName; break;
      case NumericParam::MoireShape: count = kMoireShapeCount; name = moireShapeName; break;
      case NumericParam::MoirePalette: count = kMoirePaletteCount; name = moirePaletteName; break;
      case NumericParam::LumiaPalette: count = kLumiaPaletteCount; name = lumiaPaletteName; break;
      case NumericParam::LumiaMode: count = kLumiaModeCount; name = lumiaModeName; break;
      case NumericParam::IllusionShape: count = kIllusionShapeCount; name = illusionShapeName; break;
      case NumericParam::IllusionForm: count = kIllusionFormCount; name = illusionFormName; break;
      case NumericParam::IllusionFlow: count = kIllusionFlowCount; name = illusionFlowName; break;
      case NumericParam::IllusionPalette: count = kIllusionPaletteCount; name = illusionPaletteName; break;
      default: break;
    }
    if (!name) continue;
    std::vector<std::string> options;
    for (int i = 0; i < count; ++i) options.push_back(name(i));
    const NumericParam id = row.id;
    rows.push_back({row.key, std::move(options),
      [id](const Cue& c) { double value = 0; readNumericParam(c, id, value); return static_cast<int>(value); },
      [id, count](Cue& c, int value) { if (value >= 0 && value < count) writeNumericParam(c, id, value); }});
  }
  if (cue.kind == CueKind::Timer) rows.push_back({"overtime", {"stop at 0", "count up"},
    [](const Cue& c) { return c.timer.countUpAfterZero ? 1 : 0; }, [](Cue& c, int value) { if (value == 0 || value == 1) c.timer.countUpAfterZero = value != 0; }});
  if (cue.kind == CueKind::Timer) rows.push_back({"chime amber", {"off", "on"},
    [](const Cue& c) { return c.timer.chimeAtAmber ? 1 : 0; }, [](Cue& c, int value) { if (value == 0 || value == 1) c.timer.chimeAtAmber = value != 0; }});
  if (cue.kind == CueKind::Timer) rows.push_back({"chime red", {"off", "on"},
    [](const Cue& c) { return c.timer.chimeAtRed ? 1 : 0; }, [](Cue& c, int value) { if (value == 0 || value == 1) c.timer.chimeAtRed = value != 0; }});
  if (cue.kind == CueKind::Timer) rows.push_back({"chime zero", {"off", "on"},
    [](const Cue& c) { return c.timer.chimeAtZero ? 1 : 0; }, [](Cue& c, int value) { if (value == 0 || value == 1) c.timer.chimeAtZero = value != 0; }});
  if (cue.kind == CueKind::Timer) rows.push_back({"prog bar", {"off", "on"},
    [](const Cue& c) { return c.timer.showProgressBar ? 1 : 0; }, [](Cue& c, int value) { if (value == 0 || value == 1) c.timer.showProgressBar = value != 0; }});
  if (cue.kind == CueKind::Timer) rows.push_back({"urgent", {"normal", "RED"},
    [](const Cue& c) { return c.timer.messageIsUrgent ? 1 : 0; }, [](Cue& c, int value) { if (value == 0 || value == 1) c.timer.messageIsUrgent = value != 0; }});
  if (cue.kind == CueKind::Tone) rows.push_back({"visuals", {"off", "on"},
    [](const Cue& c) { return c.tone.visualEnabled ? 1 : 0; }, [](Cue& c, int value) { if (value == 0 || value == 1) c.tone.visualEnabled = value != 0; }});
  if (cue.kind == CueKind::Tone && cue.tone.waveform == ToneWaveform::Voice) rows.push_back({"says", {"WORDS", "SOUNDS"},
    [](const Cue& c) { return c.tone.voiceSounds ? 1 : 0; }, [](Cue& c, int value) { if (value == 0 || value == 1) c.tone.voiceSounds = value != 0; }});
  if (cue.kind == CueKind::Tone && cue.tone.waveform == ToneWaveform::Fds && cue.tone.synth.chip == SynthChip::Nes) rows.push_back({"4-bit", {"off", "on"},
    [](const Cue& c) { return c.tone.synth.nesQuantise ? 1 : 0; }, [](Cue& c, int value) { if (value == 0 || value == 1) c.tone.synth.nesQuantise = value != 0; }});
  if (cue.kind == CueKind::Tone && cue.tone.waveform == ToneWaveform::Fds && cue.tone.synth.chip == SynthChip::Nes && cue.tone.synth.nesVoice == NesVoice::Noise) rows.push_back({"periodic", {"off", "on"},
    [](const Cue& c) { return c.tone.synth.nesNoiseShort ? 1 : 0; }, [](Cue& c, int value) { if (value == 0 || value == 1) c.tone.synth.nesNoiseShort = value != 0; }});
  if (cue.kind == CueKind::Pattern && normalizePatternTypeId(cue.path) == "draw") {
    rows.push_back({"plays", {"still: this frame", "flipbook: every frame", "replay: as it was drawn"},
      [](const Cue& c) { return static_cast<int>(c.drawing.playback); },
      [](Cue& c, int value) { if (value >= 0 && value < 3) { c.drawing.playback = static_cast<deckboy::draw::Playback>(value); ++c.drawing.revision; } }});
  }
  return rows;
}
} // namespace deckboy::params

