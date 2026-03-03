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

struct Cue {
  std::string id;
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
  std::uintmax_t sizeBytes = 0;
  SDL_Color color {48, 98, 48, 255};
  double fadeInSeconds = 0.0;
  double fadeOutSeconds = 0.0;
  bool loop = false;
  bool pauseOnLastFrame = false;
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
  double timecodeFps = 30.0;
  double timecodeCurrentSeconds = 0.0;
  double timecodeLastSeconds = 0.0;
  bool timecodeDirty = false;
};

struct Project {
  std::string title = std::string(kAppTitle);
  std::vector<Deck> decks {Deck {}};
  int focusedDeckIndex = 0;
  bool advancedOutputMode = false;
  bool uiSoundsEnabled = true;
  bool uiTransitionsEnabled = true;
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
  ToggleLoop, ToggleHold, CycleEndAction,
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
  ScaleXDec, ScaleXInc,
  ScaleYDec, ScaleYInc,
  OffsetXDec, OffsetXInc,
  OffsetYDec, OffsetYInc,
  RotDec, RotInc,
  CropLDec, CropLInc,
  CropRDec, CropRInc,
  CropTDec, CropTInc,
  CropBDec, CropBInc,
  KeyToggle,
  KeyTolDec, KeyTolInc,
  KeySoftDec, KeySoftInc,
  EditKeyColor,
  EditCueNumber,
  AddPausePoint, ClearPausePoints
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
