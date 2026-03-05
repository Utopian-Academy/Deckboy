// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Playboy Contributors
// This file is part of Playboy, a cue deck for live events.
// See LICENSE for details.


#ifndef PLAYBOY_CORE_TYPES_HPP
#define PLAYBOY_CORE_TYPES_HPP

#include <SDL.h>
#include <string>
#include <vector>
#include "constants.hpp"

// Domain types for the cue deck. SDL_Color/SDL_Rect used for UI integration.

enum class CueKind {
  Video,
  Image,
  Pattern,
  Browser,
  WindowSource,
  Camera,
  Syphon,
  LowerThird,
  Audio
};

enum class CueEndAction { Inherit, Stop, Loop, PauseOnLast, AutoNext };

enum class TransportState {
  Stopped,
  Paused,
  Playing
};

enum class TransitionStyle {
  Cut,
  Crossfade,
  DipBlack
};

enum class ScaleMode {
  Fit,         // letterbox: fit entire image, maintain aspect ratio
  Fill,        // fill screen and crop, maintain aspect ratio
  Stretch,     // fill screen, ignore aspect ratio (distort)
  Unscaled     // 1:1 pixel mapping (no scaling)
};

struct Cue {
  std::string id;
  std::string cueId;  // operator-facing short cue id (max 6 chars)
  std::string path;
  std::string name;
  CueKind kind = CueKind::Video;
  double duration = 0.0;
  int width = 0;
  int height = 0;
  double fps = 30.0;
  std::string formatName;
  std::string videoCodec;
  std::string audioCodec;
  bool hasAudio = false;
  bool audioEnabled = true;
  std::uintmax_t sizeBytes = 0;
  SDL_Color color {48, 98, 48, 255};
  double fadeInSeconds = 0.0;
  double fadeOutSeconds = 0.0;
  bool loop = false;
  bool pauseAtBeginning = false;
  bool pauseOnLastFrame = false;
  bool transitionToNext = true;
  std::string gotoTarget;
  double inPointSeconds = 0.0;
  double outPointSeconds = 0.0;
  double triggerTimecodeSeconds = -1.0;
  CueEndAction endAction = CueEndAction::Inherit;
  double stillDurationSeconds = 0.0;
  double cueTransitionSeconds = -1.0;
  std::string cueTransitionStyle;
  std::string lowerThirdText;
  std::string lowerThirdSubtext;
  int lowerThirdBgAlpha = 180;
  int loopCount = 0;
  double playbackSpeed = 1.0;
  std::string colorTag;
  std::string notes;
  float outputScaleX = 1.0f;
  float outputScaleY = 1.0f;
  ScaleMode scaleMode = ScaleMode::Fit;
  float outputOffsetX = 0.0f;
  float outputOffsetY = 0.0f;
  float outputRotationDegrees = 0.0f;
  float cropLeft = 0.0f;
  float cropRight = 0.0f;
  float cropTop = 0.0f;
  float cropBottom = 0.0f;
  bool chromaKeyEnabled = false;
  SDL_Color chromaKeyColor {0, 255, 0, 255};
  float chromaKeyTolerance = 60.0f;
  float chromaKeySoftness = 20.0f;
  float brightness = 1.0f;        // 0.0 (black) to 2.0 (bright)
  float contrast = 1.0f;          // 0.0 (gray) to 2.0 (high)
  float saturation = 1.0f;        // 0.0 (grayscale) to 2.0 (vibrant)
  float hueShift = 0.0f;          // -180 to +180 degrees
  std::string cueNumber;
  std::vector<double> pausePoints;
};

struct Deck {
  std::string name = "Deck 1";
  std::vector<Cue> cues;
  int selectedIndex = -1;
  int activeIndex = -1;
  std::vector<int> overlayActiveIndices;
  bool autoAdvance = false;
  bool playlistLoop = false;
  bool shuffle = false;
  float playlistOpacity = 1.0f;    // 0.0 - 1.0 per-deck contribution
  bool playlistAutoFade = false;   // auto-fade deck in on take
  double playlistFadeSeconds = 0.8;
  double playlistTimebaseFps = 30.0;             // operator playlist SMPTE base (24/25/29.97/30)
  double playlistStartOffsetSeconds = 0.0;       // playlist start timecode offset
  double playlistDefaultCueFadeSeconds = 0.5;    // default fade duration for new cues
  double playlistDefaultStillDurationSeconds = 8.0; // default duration for non-movie cues
  bool playlistDefaultLoop = false;
  bool playlistDefaultFadeInEnabled = true;
  bool playlistDefaultFadeOutEnabled = true;
  bool playlistDefaultAudioEnabled = true;
  bool playlistDefaultPauseAtBeginning = false;
  bool playlistDefaultPauseAtEnd = false;
  bool playlistDefaultTransitionToNext = true;
  std::vector<int> selectedIndices;  // optional multi-selection in cue list
  std::string audioOutputDeviceName;
  int outputDisplayIndex = 0;
  int outputRouteDeckIndex = -1;
  int outputLayerIndex = 0;
  bool ndiEnabled = false;
  std::string ndiSourceName;
  bool ndiKeyEnabled = false;
  std::string ndiKeySourceName;
  int canvasViewX = 0;
  int canvasViewY = 0;
  bool warpEnabled = false;
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
  bool timeOverlayEnabled = false;
  double transitionSeconds = 0.0;
  std::string transitionStyle = "crossfade";
  bool timecodeChaseEnabled = false;
  bool timecodeRunEnabled = false;
  bool timecodeTriggerEnabled = true;
  bool timecodeJamSyncEnabled = true;
  double timecodeFreewheelSeconds = 1.0;
  double timecodeFps = 30.0;
  double timecodeCurrentSeconds = 0.0;
  double timecodeLastSeconds = 0.0;
  bool timecodeDirty = false;
};

// Transitional output entity: decouples output routing intent from Deck internals.
// hostDeckIndex keeps compatibility with the current renderer (one output window per host deck).
struct OutputTarget {
  std::string name = "Output 1";
  int hostDeckIndex = 0;
  int displayIndex = 0;
  bool enabled = false;
  std::string outputType = "window"; // window | stream
  int mirrorSourceOutputIndex = -1;  // -1 = render own layer assignments
  bool streamEnabled = false;
  std::string streamProtocol = "srt"; // srt | rtmp
  std::string streamUrl;
  int streamBitrateKbps = 6000;
  bool ndiEnabled = false;
  std::string ndiSourceName;
  bool ndiKeyEnabled = false;
  std::string ndiKeySourceName;
  std::string outputId;
  float outputAlpha = 1.0f;          // 0.0-1.0 output dimmer (per output)
  int outputDelayMs = 0;             // 0-5000 egress delay (ms)
  bool outputTimeOverlayEnabled = false; // output-scoped time/ID overlay
  std::string outputColorSpace = "auto"; // auto | bt709 | srgb
};

// Deck-to-output layer assignment.
// Multiple assignments for one deck are allowed (fan-out to multiple outputs).
struct LayerAssignment {
  int deckIndex = 0;
  int outputIndex = 0;
  int layerIndex = 0;
  bool enabled = true;
  std::string outputId;
  std::string layerId;
};

struct GroupSlot {
  bool bypass = false;
  std::string cueId;
};

struct GroupPreset {
  std::string name = "Group 1";
  std::vector<GroupSlot> slots;
};

struct Project {
  std::string title = std::string(kAppTitle);
  std::vector<Deck> decks {Deck {}};
  int focusedDeckIndex = 0;
  std::vector<OutputTarget> outputs {OutputTarget {}};
  int focusedOutputIndex = 0;
  std::vector<LayerAssignment> layerAssignments;
  std::vector<GroupPreset> groupPresets;
  int focusedGroupPresetIndex = 0;
  std::vector<std::string> layerNames {"BG", "LayerA", "LayerB", "LayerC", "LayerD"};
  bool advancedOutputMode = false;
  bool uiSoundsEnabled = true;
  bool uiTransitionsEnabled = true;
  bool oscQueryEnabled = false;
  int oscQueryPort = 5511;
  bool oscFeedbackMirrorEnabled = false;
  int oscFeedbackRateMs = 120;
  std::string jumpMode = "trigger"; // trigger | load
  bool jumpTransitionEnabled = true;
  std::string panicProfile = "outputs_off"; // outputs_off | fade_pause | fade_rewind | fade_load_next
  double panicFadeSeconds = 0.9;
  bool panicAutoRestore = false;
  double masterVolume = 1.0;
  double masterDimmer = 1.0;
  bool outputFollowDisplay = true;
  int outputRenderWidth = 1920;
  int outputRenderHeight = 1080;
  double outputRefreshRateHz = 0.0; // 0 = auto
  int outputBitDepth = 0; // 0=auto, 8=8-bit, 10=10-bit
  bool outputCanvasEnabled = false;
  int outputCanvasWidth = 3840;
  int outputCanvasHeight = 2160;
};

struct DecodedFrame {
  int width = 0;
  int height = 0;
  std::uint64_t index = 0;
  std::vector<std::uint8_t> pixels;
};

struct Button {
  std::string label;
  std::string tip;
  SDL_Rect rect {};
  SDL_Color fill {48, 40, 31, 255};
  SDL_Color outline {255, 255, 255, 20};
  SDL_Color text {245, 234, 215, 255};
};

enum class QuickAction {
  ToggleLoop, ToggleHold, TogglePauseBegin, ToggleCueAudio, ToggleNextTransition, EditGotoTarget, CycleEndAction,
  FadeInDec, FadeInInc, FadeOutDec, FadeOutInc,
  VolDec, VolInc,
  InDec, InInc, OutDec, OutInc,
  TransDec, TransInc, CycleTransStyle,
  LowerBgDec, LowerBgInc,
  DurDec, DurInc,
  LoopCountDec, LoopCountInc,
  SpeedDec, SpeedInc,
  CycleColorTag,
  EditNotes,
  GotoMinus10, GotoMinus20, GotoMinus30,
  CycleScaleMode,
  ScaleXDec, ScaleXInc,
  ScaleYDec, ScaleYInc,
  EditScaleX, EditScaleY,
  OffsetXDec, OffsetXInc,
  OffsetYDec, OffsetYInc,
  EditOffsetX, EditOffsetY,
  RotDec, RotInc,
  EditRotation,
  CropLDec, CropLInc,
  CropRDec, CropRInc,
  CropTDec, CropTInc,
  CropBDec, CropBInc,
  KeyToggle,
  KeyTolDec, KeyTolInc,
  KeySoftDec, KeySoftInc,
  EditKeyColor,
  EditCueNumber,
  AddPausePoint, ClearPausePoints,
  BrightnessDec, BrightnessInc,
  ContrastDec, ContrastInc,
  SaturationDec, SaturationInc,
  HueShiftDec, HueShiftInc,
  PatternTypePrev, PatternTypeNext,
  TogglePatternMotion,
  CueSectionPlaybackToggle,
  CueSectionGeometryToggle,
  CueSectionKeyToggle,
  CueSectionRoutingToggle,
  CueRouteOutputPrev,
  CueRouteOutputNext,
  CueRouteLayerDec,
  CueRouteLayerInc,
  CueRouteAssignToggle
};

struct QuickButton {
  SDL_Rect rect;
  QuickAction action;
  std::string tip;
};

struct DragState {
  bool active = false;
  int cueIndex = -1;
  int deckIndex = 0;
};

struct ToastState {
  bool active = false;
  Uint64 startedAt = 0;
  Uint32 durationMs = 1200;
  std::string message;
  SDL_Color fill {155, 188, 15, 220};
  SDL_Color ink {15, 56, 15, 255};
};

enum class UiSoundEffect {
  Navigate,
  Import,
  Take,
  Toggle,
  Stop,
  Clear,
  Delete
};

#endif
