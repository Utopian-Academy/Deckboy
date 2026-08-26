// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// media_engine.cpp — Implementation of the core playback engine.
//
// This is the largest and most critical implementation file. Key sections:
//
//   stopAll / loadCue:     lifecycle management, cue loading pipeline
//   refreshActiveCueRuntime: hot-update runtime params without reloading
//   play / pause / stop:   transport state transitions
//   seek:                  position jumping (restarts decode from new offset)
//   update:                per-frame tick — pops frames from decode queue,
//                          checks pause points, detects end-of-playback
//   render:                draws the current frame + transition overlay
//   startDecoderThreads:   launches ffmpeg video + audio subprocesses with
//                          piped stdout; spawns reader threads
//   buildPatternFrame:     procedural test pattern generation (static)
//   buildPocketTest:       animated pixel art scene generation (static)
//
// Threading:
//   Video decode thread reads raw RGBA from ffmpeg → pushes to frameQueue_
//   Audio decode thread reads raw PCM from ffmpeg → queues to SDL audio
//   Main thread calls update() to pop frames, render() to blit them
//   frameMutex_ protects frameQueue_ between decode and main threads
//
// Transition system:
//   When loadCue() is called with transitionSeconds > 0, the outgoing cue's
//   current frame is snapshot to transitionTexture_. During the transition
//   period, render() alpha-blends between the snapshot and the incoming cue's
//   live frames. For DipBlack, both fade independently to/from black.
// ============================================================================

#include "engine/media_engine.hpp"

#include "core/sdl_compat.hpp"
#include <ctime>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#include "core/cue_helpers.hpp"       // isSourceCueKind, resolvedCueEndAction
#include "core/io_utils.hpp"          // readExact, readSome (pipe I/O)
#include "core/pattern_helpers.hpp"   // normalizePatternTypeId, patternTypeIsAnimated
#include "core/pixel_effects.hpp"     // applyChromaKeyToPixels, applyColorControlsToPixels
#include "core/subprocess.hpp"        // spawnProcess, ChildProcess
#include "core/utils.hpp"             // trim, splitLines, formatTimecode
// Native Terrarium sim (pattern://terrarium, pattern://terrarium-pico).
// The sim itself is vendored untouched under extras/upstream/; the namespace
// wrapper and the RGBA renderer are Deckboy's. See extras/upstream/UPSTREAM.md.
#include "extras/terrarium_render_rgba.hpp"
#include "platform/capture_backend.hpp" // source capture for camera/window cues

#include <mutex>

#ifndef _WIN32
#include <unistd.h>                   // POSIX read() (used by io_utils on non-Windows)
#endif

using deckboy::core::utils::trim;
using deckboy::core::utils::splitLines;

namespace {

constexpr double kPatternMotionLoopSeconds = 4.0;
constexpr double kTau = 6.28318530717958647692;

double normalizedLoopProgress(double wallSeconds, double loopSeconds) {
  if (loopSeconds <= 0.0) {
    return 0.0;
  }
  double wrapped = std::fmod(wallSeconds, loopSeconds);
  if (wrapped < 0.0) {
    wrapped += loopSeconds;
  }
  return wrapped / loopSeconds;
}

int phaseFromProgress(double progress, int period) {
  if (period <= 0) {
    return 0;
  }
  double wrapped = progress - std::floor(progress);
  return static_cast<int>(std::floor(wrapped * static_cast<double>(period))) % period;
}

std::atomic<bool> g_inprocDecodeDisabled {false};
// Latched immediately before SDL_Quit(); after that no engine may call SDL.
std::atomic<bool> g_sdlTornDown {false};

// ---------------------------------------------------------------------------
// buildTerrariumFrame — Stateful pattern source for pattern://terrarium.
//
// One shared world per process (deliberate: every deck and preview shows THE
// terrarium, and its ecosystem persists across cue reloads for the whole
// show). Ticks at the sim's native 9 TPS off the wall clock; the expensive
// 1600x896 re-render only happens when the sim actually stepped. Guarded by
// a mutex because pattern builds can come from preview paths too.
// ---------------------------------------------------------------------------
void buildTerrariumFrame(DecodedFrame& frame, double wallSeconds, int cellPx) {
  static std::mutex mutex;
  static terra::World world;
  static terra::Rng rng {0xDECB0Fu};
  static std::string banner;
  static int tick = 0;
  static double lastStepSeconds = -1.0;
  static bool seeded = false;
  // Cached per cell size: the two patterns share ONE world (that is the point —
  // pico and the full-size view are the same terrarium seen at two scales) but
  // they rasterise to different rasters, so they cannot share a pixel buffer.
  static std::vector<std::uint8_t> cached[2];
  static float cachedAnimT[2] = {-1.0f, -1.0f};
  const int cacheSlot = (cellPx <= 1) ? 0 : 1;

  std::lock_guard<std::mutex> lock(mutex);
  constexpr double kStepSeconds = 1.0 / 9.0;  // DEFAULT_TPS
  bool dirty = false;
  if (!seeded) {
    terra::seedWorld(world, rng, terra::MEADOW);
    // Warm the ecosystem up (~13 sim-seconds) so the first TAKE shows a
    // grown, lived-in world instead of freshly raked dirt.
    for (int i = 0; i < 120; ++i) {
      terra::step(world, rng, banner, tick);
      ++tick;
    }
    seeded = true;
    lastStepSeconds = wallSeconds;
    dirty = true;
  }
  int steps = 0;
  while (wallSeconds - lastStepSeconds >= kStepSeconds && steps < 5) {
    terra::step(world, rng, banner, tick);
    ++tick;
    lastStepSeconds += kStepSeconds;
    ++steps;
    dirty = true;
  }
  if (wallSeconds - lastStepSeconds >= kStepSeconds) {
    lastStepSeconds = wallSeconds;  // fell far behind — drop the backlog
  }
  // Surf, motes and fireflies move on wall-clock time, not on sim ticks — the
  // sim only steps at 9 TPS, so animating from `tick` alone makes the water
  // lurch. Re-render when the sim stepped OR when animT has moved enough to be
  // worth a frame (~12 fps, matching the Pi panel's repaint rate).
  const float animT = static_cast<float>(wallSeconds);
  const bool animMoved = cachedAnimT[cacheSlot] < 0.0f ||
                         (animT - cachedAnimT[cacheSlot]) >= (1.0f / 12.0f);
  if (dirty || animMoved || cached[cacheSlot].empty()) {
    terra::renderWorldRgba(world, tick, animT, cellPx, cached[cacheSlot]);
    cachedAnimT[cacheSlot] = animT;
  }
  frame.width = terra::frameWidthForCellPx(cellPx);
  frame.height = terra::frameHeightForCellPx(cellPx);
  frame.format = FramePixelFormat::RGBA32;
  frame.pixels = cached[cacheSlot];
}

} // namespace

// Process-wide break-glass: force the ffmpeg CLI pipe path for new decodes
// (--no-inproc-decode). Static so the operator flag reaches every engine.
void MediaEngine::setInprocDecodeDisabled(bool disabled) {
  g_inprocDecodeDisabled.store(disabled);
}

bool MediaEngine::inprocDecodeDisabled() {
  return g_inprocDecodeDisabled.load();
}

// Set once, just before SDL_Quit(). See the header for why this exists.
void MediaEngine::setSdlTornDown(bool tornDown) {
  g_sdlTornDown.store(tornDown);
}

bool MediaEngine::sdlTornDown() {
  return g_sdlTornDown.load();
}

// Destructor ensures all decode threads are joined and subprocesses killed.
MediaEngine::~MediaEngine() {
  stopAll();
}

// Full reset: kill all subprocesses, join all threads, release all textures,
// clear audio queue, and reset all state to defaults. Called by destructor
// and when the deck is cleared (no cue loaded).
void MediaEngine::stopAll() {
  stopDecoderThreads();
  isBrowserCapturing_ = false;
  isSourceCapturing_ = false;
  clearVisualOnReachedEnd_ = false;
  suppressFadeInForCurrentCue_ = false;
  suppressVisualFadeOutForCurrentCue_ = false;
  clearTexture();
  clearTransitionTexture();
  clearAudio();
  state_ = TransportState::Stopped;
  currentPosition_ = 0.0;
  duration_ = 0.0;
  cueInPointSeconds_ = 0.0;
  cueOutPointSeconds_ = 0.0;
  activeCue_ = nullptr;
  activeCueSnapshot_.reset();
  syncAudioFadeParams();
  frameRate_ = 0.0;
  lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
  displayFrameSerial_ = 0;
  displayFrame_.reset();
  resetMediaFpsTelemetry();
}

// ---------------------------------------------------------------------------
// loadCue — Load a new cue for playback, optionally with a transition.
//
// This is the main entry point for starting playback of any cue type.
// It handles the full lifecycle:
//   1. Compute outgoing cue's fade gain (for transition blending)
//   2. Stop current decode threads and begin transition animation
//   3. Copy all per-cue parameters (geometry, effects, timing) from the Cue
//   4. Dispatch to the appropriate loader based on CueKind:
//      - Image:       loadStillFrame() → async single-frame decode
//      - Pattern:     loadPatternFrame() → CPU-generated pixels
//      - Source:      loadSourceFrame() → capture backend
//      - Browser/L3:  initStillTimer() → wait for browser frames
//      - Video/Audio: startDecoderThreads() → ffmpeg pipeline
//   5. Set transport state (Playing if autoplay, Paused otherwise)
//
// The suppressFadeIn flag is set when a cue is loaded mid-transition
// (e.g. auto-advancing) to avoid a double fade-in effect.
// ---------------------------------------------------------------------------
void MediaEngine::loadCue(const Cue* cue, bool autoplay, double transitionSeconds,
                           TransitionStyle transitionStyle, bool suppressFadeIn) {
  float outgoingGain = transitionSourceGainForLoadCue(activeCue_, state_, visualFadeGainAt(position()));
  stopDecoderThreads();
  beginTransition(transitionSeconds, transitionStyle, outgoingGain);
  clearTexture();
  clearAudio();
  // Own a snapshot of the cue. The caller's pointer typically aims into
  // Deck::cues, which the UI mutates freely (import push_back reallocates,
  // delete shifts) while decode threads and the render path still read the
  // active cue. Copy it and repoint the local param at the owned storage so
  // nothing below can capture the caller's pointer.
  if (cue) {
    activeCueSnapshot_ = *cue;
    activeCue_ = &*activeCueSnapshot_;
    cue = activeCue_;
  } else {
    activeCueSnapshot_.reset();
    activeCue_ = nullptr;
  }
  outputScaleX_ = cue ? cue->outputScaleX : 1.0f;
  outputScaleY_ = cue ? cue->outputScaleY : 1.0f;
  scaleMode_ = cue ? cue->scaleMode : ScaleMode::Fit;
  outputOffsetX_ = cue ? cue->outputOffsetX : 0.0f;
  outputOffsetY_ = cue ? cue->outputOffsetY : 0.0f;
  outputRotationDegrees_ = cue ? cue->outputRotationDegrees : 0.0f;
  cropLeft_ = cue ? cue->cropLeft : 0.0f;
  cropRight_ = cue ? cue->cropRight : 0.0f;
  cropTop_ = cue ? cue->cropTop : 0.0f;
  cropBottom_ = cue ? cue->cropBottom : 0.0f;
  chromaKeyEnabled_ = cue ? cue->chromaKeyEnabled : false;
  chromaKeyColor_ = cue ? cue->chromaKeyColor : SDL_Color {0, 255, 0, 255};
  chromaKeyTolerance_ = cue ? cue->chromaKeyTolerance : 60.0f;
  chromaKeySoftness_ = cue ? cue->chromaKeySoftness : 20.0f;
  brightness_ = cue ? std::clamp(cue->brightness, 0.0f, 2.0f) : 1.0f;
  contrast_ = cue ? std::clamp(cue->contrast, 0.0f, 2.0f) : 1.0f;
  saturation_ = cue ? std::clamp(cue->saturation, 0.0f, 2.0f) : 1.0f;
  hueShift_ = cue ? std::clamp(cue->hueShift, -180.0f, 180.0f) : 0.0f;
  pausePoints_   = cue ? cue->pausePoints   : std::vector<double>{};
  nextPausePointIdx_ = 0;
  playbackSpeed_ = cue ? std::clamp(cue->playbackSpeed, 0.25, 4.0) : 1.0;
  displayFrame_.reset();
  currentPosition_ = 0.0;
  pausedPosition_ = 0.0;
  playbackStartPosition_ = 0.0;
  duration_ = cue ? cue->duration : 0.0;
  cueInPointSeconds_ = 0.0;
  cueOutPointSeconds_ = cue ? cue->duration : 0.0;
  frameRate_ = cue && cue->kind == CueKind::Video && cue->fps > 1.0 ? cue->fps : 30.0;
  lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
  displayFrameSerial_ = 0;
  resetMediaFpsTelemetry();
  decoderEof_ = false;
  reachedEnd_ = false;
  clearVisualOnReachedEnd_ = false;
  // Per-cue fadeInSeconds/fadeOutSeconds apply uniformly to all cue kinds via
  // `currentVisualFadeGain()` → bridge texture alpha in `renderDeckLayerIntoOutput`.
  // Previously this branched on cue kind to avoid "double-fading" the crossfade
  // in `MediaEngine::render()`, but that render path is dead for output purposes
  // (see v0.76.4 DEVNOTE) — so the suppression just silently killed video-cue
  // fades on the actual output window. Honor the caller's suppressFadeIn hint
  // only; loop suppression is re-asserted in `handlePlaybackEnd`.
  suppressFadeInForCurrentCue_ = suppressFadeIn;
  suppressVisualFadeOutForCurrentCue_ = false;

  if (!cue) {
    state_ = TransportState::Stopped;
    return;
  }

  if (cue->kind == CueKind::Image) {
    loadStillFrame(*cue);
    initStillTimer(*cue, autoplay);
    return;
  }

  if (cue->kind == CueKind::Pattern) {
    loadPatternFrame(*cue);
    initStillTimer(*cue, autoplay);
    return;
  }

  // Generated cues that produce their own content from the update loop. Both
  // of these had NO branch here, so taking one fell through to the file-decode
  // path with a "tone://" or "vsynth://" path that cannot decode. The video
  // synth appeared to work because its frames come from the update loop
  // regardless, but the TONE never played a sample -- which is exactly what
  // the operator reported.
  //
  // Like Timer: duration 0 so the transport holds them on air indefinitely
  // rather than auto-advancing the playlist out from under a live signal.
  if (cue->kind == CueKind::Tone || cue->kind == CueKind::VideoSynth) {
    duration_ = 0.0;
    currentPosition_ = 0.0;
    pausedPosition_ = 0.0;
    playbackStartPosition_ = 0.0;
    playbackClockStart_ = std::chrono::steady_clock::now();
    // PLAYING when taken, unlike the timer: a tone or a synth is generating
    // continuously and the operator expects sound the moment it goes live.
    // The timer holds paused because a countdown is started deliberately.
    state_ = autoplay ? TransportState::Playing : TransportState::Paused;
    if (cue->kind == CueKind::VideoSynth) {
      rebuildVideoSynthFrame(*cue, 0.0, 0.0);
    } else {
      // The audio device is opened PAUSED, and the ONLY resume is at the end of
      // this function on the file-decode path -- which this branch returns
      // before reaching. So the tone generated its first fifth of a second,
      // found the stream still full because nothing was draining it, and never
      // produced another sample. MEASURED: pumpToneAudio ran exactly ONCE per
      // take (peak 29203, tap connected) -- so the generator, the routing and
      // the tap were all fine and the device was simply never started. That is
      // the silent output, the dead VU meter and the -91 dBFS recording, all
      // from one missing line.
      if (audioStream_) {
        deckboySetAudioPaused(audioStream_, !autoplay);
      }
      rebuildToneFrame(*cue);
    }
    return;
  }

  if (cue->kind == CueKind::Timer) {
    // Duration is the countdown PLUS an overtime allowance, so the transport
    // keeps advancing past zero instead of hitting its end and auto-advancing
    // to the next cue while a speaker is still talking. Without count-up it
    // stops exactly at zero, which is what the operator asked for.
    // The timer runs its OWN clock (see TimerRuntime); the transport just
    // holds the cue on air. Duration 0 = hold indefinitely, so taking a timer
    // never auto-advances the playlist out from under a live countdown.
    duration_ = 0.0;
    currentPosition_ = 0.0;
    pausedPosition_ = 0.0;
    playbackStartPosition_ = 0.0;
    playbackClockStart_ = std::chrono::steady_clock::now();
    state_ = TransportState::Paused;
    rebuildTimerFrame(*cue, 0.0, false);
    return;
  }

  if (isSourceCueKind(cue->kind)) {
    loadSourceFrame(*cue);
    duration_ = 0.0;
    pausedPosition_ = 0.0;
    currentPosition_ = 0.0;
    state_ = TransportState::Paused;
    if (autoplay) {
      startSourceCapture(*cue);
    }
    return;
  }

  if (cue->kind == CueKind::Browser || cue->kind == CueKind::LowerThird ||
      cue->kind == CueKind::Composite) {
    initStillTimer(*cue, autoplay);
    return;
  }

  cueInPointSeconds_ = std::clamp(cue->inPointSeconds, 0.0, std::max(0.0, cue->duration));
  cueOutPointSeconds_ = cue->outPointSeconds > 0.0 ? cue->outPointSeconds : cue->duration;
  cueOutPointSeconds_ = std::clamp(cueOutPointSeconds_, cueInPointSeconds_, std::max(cueInPointSeconds_, cue->duration));
  duration_ = std::max(0.01, cueOutPointSeconds_ - cueInPointSeconds_);

  startDecoderThreads(*cue, cueInPointSeconds_, 0.0);
  state_ = autoplay ? TransportState::Playing : TransportState::Paused;
  playbackClockStart_ = std::chrono::steady_clock::now();
  playbackStartPosition_ = 0.0;
  pausedPosition_ = 0.0;
  if (audioStream_) {
    deckboySetAudioPaused(audioStream_, !autoplay);
  }
}

// Replace the owned active-cue snapshot in place. The optional stays engaged,
// so activeCue_ remains stable across the assignment. Publishes fresh fade
// params to the audio-thread mirrors.
void MediaEngine::syncActiveCueSnapshot(const Cue& cue) {
  if (!activeCueSnapshot_) {
    return;  // nothing loaded — loadCue creates the snapshot
  }
  *activeCueSnapshot_ = cue;
  syncAudioFadeParams();
}

// Hot-update runtime parameters from the active cue without restarting decode.
// Called when the operator adjusts speed, in/out points, or pause points
// while a cue is playing. Recalculates the position to maintain continuity.
// updatedCue (when given) is the app's current cue — refresh the owned
// snapshot from it first so we don't re-read stale parameters.
void MediaEngine::refreshActiveCueRuntime(const Cue* updatedCue) {
  if (updatedCue) {
    syncActiveCueSnapshot(*updatedCue);
  }
  if (!activeCue_) {
    return;
  }

  playbackSpeed_ = std::clamp(activeCue_->playbackSpeed, 0.25, 4.0);
  pausePoints_ = activeCue_->pausePoints;

  if (activeCue_->kind != CueKind::Video && activeCue_->kind != CueKind::Audio) {
    nextPausePointIdx_ = 0;
    return;
  }

  double absoluteSeconds = cueInPointSeconds_ + position();
  cueInPointSeconds_ = std::clamp(activeCue_->inPointSeconds, 0.0, std::max(0.0, activeCue_->duration));
  cueOutPointSeconds_ = activeCue_->outPointSeconds > 0.0 ? activeCue_->outPointSeconds : activeCue_->duration;
  cueOutPointSeconds_ = std::clamp(cueOutPointSeconds_, cueInPointSeconds_,
                                   std::max(cueInPointSeconds_, activeCue_->duration));
  duration_ = std::max(0.01, cueOutPointSeconds_ - cueInPointSeconds_);
  frameRate_ = activeCue_->kind == CueKind::Video && activeCue_->fps > 1.0 ? activeCue_->fps : frameRate_;

  double clampedAbsoluteSeconds = std::clamp(absoluteSeconds, cueInPointSeconds_, cueOutPointSeconds_);
  double nextPosition = std::clamp(clampedAbsoluteSeconds - cueInPointSeconds_, 0.0, duration_);
  pausedPosition_ = nextPosition;
  currentPosition_ = nextPosition;
  playbackStartPosition_ = nextPosition;
  playbackClockStart_ = std::chrono::steady_clock::now();
  lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
  resetMediaFpsTelemetry();
  decoderEof_ = false;
  reachedEnd_ = false;

  nextPausePointIdx_ = 0;
  while (nextPausePointIdx_ < pausePoints_.size() && pausePoints_[nextPausePointIdx_] <= nextPosition) {
    ++nextPausePointIdx_;
  }

  stopDecoderThreads();
  clearAudio();
  startDecoderThreads(*activeCue_, cueInPointSeconds_ + nextPosition, nextPosition);
  if (audioStream_) {
    deckboySetAudioPaused(audioStream_, state_ != TransportState::Playing);
  }
}

// Resume playback from the current position. For source cues (camera/window),
// this starts or resumes the capture backend. For video/audio cues, this
// unpauses the SDL audio device and starts the wall-clock timer.
void MediaEngine::setAudioDevice(SDL_AudioStream* stream) {
  // Redirect PCM output to a newly opened device. The caller closes the old
  // device after this returns; start the new one clean and matched to the
  // current transport so a device change never interrupts playback.
  //
  // Under the lock: the audio thread keeps decoding across the swap, and it
  // must not be holding the outgoing pointer when the caller frees it.
  {
    std::lock_guard<std::mutex> lock(audioStreamMutex_);
    audioStream_ = stream;
  }
  if (audioStream_) {
    SDL_ClearAudioStream(audioStream_);
    deckboySetAudioPaused(audioStream_, state_ != TransportState::Playing);
  }
}

// Hand the device back. Decode stops first, which joins the audio thread — the
// only other user of the pointer — so after this returns nothing in the engine
// can reach the stream and the owner is free to destroy it.
void MediaEngine::detachAudioDevice() {
  stopDecoderThreads();
  SDL_AudioStream* stream = nullptr;
  {
    std::lock_guard<std::mutex> lock(audioStreamMutex_);
    stream = audioStream_;
    audioStream_ = nullptr;
  }
  if (stream) {
    SDL_ClearAudioStream(stream);
    deckboySetAudioPaused(stream, true);
  }
  audioDelayFifo_.clear();
}

void MediaEngine::play() {
  if (!activeCue_) return;
  if (isSourceCueKind(activeCue_->kind)) {
    if (isSourceCapturing_) {
      state_ = TransportState::Playing;
      return;
    }
    if (startSourceCapture(*activeCue_)) {
      state_ = TransportState::Playing;
    }
    return;
  }
  bool isTimedStill = activeCue_->kind != CueKind::Video && duration_ > 0.0;
  if (activeCue_->kind != CueKind::Video && !isTimedStill) return;
  if (state_ == TransportState::Playing) return;
  if (activeCue_->kind == CueKind::Video && !decodersRunning_) {
    // Pipes were released by a dark STOP — revive them from the reracked
    // position so PLAY works without a fresh TAKE.
    startDecoderThreads(*activeCue_, cueInPointSeconds_ + pausedPosition_, pausedPosition_);
  }
  playbackClockStart_ = std::chrono::steady_clock::now();
  playbackStartPosition_ = pausedPosition_;
  state_ = TransportState::Playing;
  if (audioStream_ != nullptr && (activeCue_->kind == CueKind::Video || activeCue_->kind == CueKind::Audio)) {
    deckboySetAudioPaused(audioStream_, false);
  }
}

// Pause playback and record the current position for later resume.
// For source cues, this stops the capture backend entirely.
// For video/audio, this pauses the SDL audio device and freezes the clock.
void MediaEngine::pause() {
  if (!activeCue_) return;
  if (isSourceCueKind(activeCue_->kind)) {
    if (isSourceCapturing_) {
      stopDecoderThreads();
    }
    isSourceCapturing_ = false;
    pausedPosition_ = 0.0;
    currentPosition_ = 0.0;
    state_ = TransportState::Paused;
    return;
  }
  bool isAV = activeCue_->kind == CueKind::Video || activeCue_->kind == CueKind::Audio;
  bool isTimedStill = !isAV && duration_ > 0.0;
  if (!isAV && !isTimedStill) return;
  if (state_ != TransportState::Playing) {
    state_ = TransportState::Paused;
    return;
  }
  pausedPosition_ = position();
  currentPosition_ = pausedPosition_;
  state_ = TransportState::Paused;
  if (audioStream_ != nullptr && (activeCue_->kind == CueKind::Video || activeCue_->kind == CueKind::Audio)) {
    deckboySetAudioPaused(audioStream_, true);
  }
}

// Play ↔ pause toggle. Dispatches to play() or pause() based on current state.
void MediaEngine::toggle() {
  if (!activeCue_) return;
  if (isSourceCueKind(activeCue_->kind)) {
    if (isSourceCapturing_ || state_ == TransportState::Playing) {
      pause();
    } else {
      play();
    }
    return;
  }
  bool isTimedStill = activeCue_->kind != CueKind::Video && duration_ > 0.0;
  if (activeCue_->kind != CueKind::Video && !isTimedStill) return;
  if (state_ == TransportState::Playing) { pause(); } else { play(); }
}

// Stop playback and rewind to the beginning. Default (clearVisual=false):
// video restarts decode at 0 and holds the first frame — the RERACK-style
// ready state. clearVisual=true is the operator STOP verb: the deck goes
// DARK (visual cleared, pipes released); play() revives the pipes from the
// reracked position, TAKE reloads as usual.
void MediaEngine::stop(bool clearVisual) {
  if (!activeCue_) {
    return;
  }
  clearVisualOnReachedEnd_ = false;
  auto applyVisualClear = [&]() {
    if (!clearVisual) {
      return;
    }
    displayFrame_.reset();
    clearTexture();
    clearTransitionTexture();
  };
  if (isSourceCueKind(activeCue_->kind)) {
    if (isSourceCapturing_) {
      stopDecoderThreads();
    }
    isSourceCapturing_ = false;
    if (!clearVisual) {
      loadSourceFrame(*activeCue_);  // hold the poster frame unless darkening
    }
    state_ = TransportState::Paused;
    pausedPosition_ = 0.0;
    currentPosition_ = 0.0;
    applyVisualClear();
    return;
  }
  bool isAV = activeCue_->kind == CueKind::Video || activeCue_->kind == CueKind::Audio;
  if (!isAV) {
    state_ = TransportState::Paused;
    pausedPosition_ = 0.0;
    currentPosition_ = 0.0;
    applyVisualClear();
    return;
  }
  if (clearVisual) {
    // Kill the pipes rather than seek-restarting them: a fresh decode's
    // first frame would immediately repopulate displayFrame_ and relight
    // the deck the next tick.
    stopDecoderThreads();
    clearAudio();
  } else {
    seek(0.0, false);
  }
  state_ = TransportState::Stopped;
  pausedPosition_ = 0.0;
  currentPosition_ = 0.0;
  applyVisualClear();
  if (audioStream_) {
    deckboySetAudioPaused(audioStream_, true);
  }
}

// Clear everything — equivalent to "no cue loaded". Output goes to black.
void MediaEngine::clear() {
  stopAll();
}

// Seek to a specific position (in seconds relative to the cue's in-point).
// For video/audio cues, this kills the current ffmpeg decode and restarts
// from the new position. The clearVisualFrame flag controls whether the
// current frame is blanked during the seek (true for hard stops).
void MediaEngine::seek(double seconds, bool clearVisualFrame) {
  if (!activeCue_) {
    return;
  }
  if (activeCue_->kind == CueKind::Image || isSourceCueKind(activeCue_->kind)) {
    return;
  }
  double clamped = std::clamp(seconds, 0.0, duration_);
  pausedPosition_ = clamped;
  currentPosition_ = clamped;
  playbackStartPosition_ = clamped;
  playbackClockStart_ = std::chrono::steady_clock::now();
  lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
  resetMediaFpsTelemetry();
  if (clearVisualFrame) {
    displayFrame_.reset();
  }
  nextPausePointIdx_ = 0;
  while (nextPausePointIdx_ < pausePoints_.size() && pausePoints_[nextPausePointIdx_] <= clamped) {
    ++nextPausePointIdx_;
  }
  clearTransitionTexture();
  if (clearVisualFrame) {
    clearTexture();
  }
  clearAudio();
  if (activeCue_->kind == CueKind::Pattern) {
    loadPatternFrame(*activeCue_);
    state_ = TransportState::Paused;
    return;
  }
  if (activeCue_->kind == CueKind::Browser) {
    state_ = TransportState::Paused;
    return;
  }

  stopDecoderThreads();
  startDecoderThreads(*activeCue_, cueInPointSeconds_ + clamped, clamped);
  if (audioStream_) {
    deckboySetAudioPaused(audioStream_, state_ != TransportState::Playing);
  }
}

// Set the playback volume (0.0–1.0). Atomic store — safe to call from any thread.
// The audio decode thread reads this each sample pair via volume_.load() to scale PCM output.
void MediaEngine::setVolume(float value) {
  volume_.store(std::clamp(value, 0.0f, 1.0f));
}

// Replace the auto-pause timecodes for the active cue. Sorts the list and
// advances the index past any points already behind the current position,
// so only future pause points trigger. Called from the inspector when the
// operator edits pause points on a playing cue.
void MediaEngine::setPausePoints(std::vector<double> points) {
  std::sort(points.begin(), points.end());
  pausePoints_ = std::move(points);
  double pos = position();
  nextPausePointIdx_ = 0;
  while (nextPausePointIdx_ < pausePoints_.size() && pausePoints_[nextPausePointIdx_] <= pos) {
    ++nextPausePointIdx_;
  }
}

// Current playback position in seconds (relative to the cue's in-point).
// When playing, this is computed from the wall clock (steady_clock) and
// speed multiplier. When paused/stopped, returns the stored position.
double MediaEngine::position() const {
  if (!activeCue_) {
    return 0.0;
  }
  if (state_ == TransportState::Playing) {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - playbackClockStart_).count() * playbackSpeed_;
    return std::clamp(playbackStartPosition_ + elapsed, 0.0,
                      duration_ > 0.0 ? duration_ : elapsed);
  }
  return currentPosition_;
}

// Check and consume the "reached end" flag. Returns true once per playback end.
// The caller (transport handler) polls this each frame and decides what to do
// (stop, loop, auto-advance, etc.) based on the cue's end action.
bool MediaEngine::reachedEnd() {
  if (reachedEnd_) {
    reachedEnd_ = false;
    return true;
  }
  return false;
}

// Called by the transport handler after reachedEnd() returns true.
// Stops decode and either holds the last frame or clears to black.
void MediaEngine::finalizeReachedEnd(bool keepVisibleFrame) {
  if (!keepVisibleFrame && clearVisualOnReachedEnd_) {
    displayFrame_.reset();
    clearTexture();
  }
  clearVisualOnReachedEnd_ = false;
}

// ---------------------------------------------------------------------------
// update — Per-frame tick called from the main render loop.
//
// Responsibilities:
//   1. Check for a completed async still-image decode (imageFramePending_)
//   2. For non-video cues: advance the still timer and check for end-of-duration
//   3. For video cues: pop decoded frames from frameQueue_, upload to GPU
//      texture, check pause points, detect end-of-playback (decoderEof_)
//   4. Apply per-pixel effects (chroma key, color controls) to the current frame
//
// This must be called every frame from the main thread (not a decode thread).
// ---------------------------------------------------------------------------
void MediaEngine::update() {
  // Catch-all publish: fade params, duration, and suppress flags can change
  // from several paths (handlePlaybackEnd loop re-assert, browser duration
  // restore, snapshot sync). One relaxed store set per tick keeps the audio
  // thread at most a frame behind without sprinkling syncs at every site.
  syncAudioFadeParams();
  if (imageFramePending_.exchange(false)) {
    std::lock_guard<std::mutex> lk(imageMutex_);
    if (pendingImageFrame_) {
      pendingImageFrame_->index = ++displayFrameSerial_;
      displayFrame_ = std::move(pendingImageFrame_);
      uploadFrame(*displayFrame_);
    }
  }

  if (!activeCue_) {
    return;
  }

  if (activeCue_->kind != CueKind::Video && !isBrowserCapturing_ && !isSourceCapturing_) {
    // Pocket-test A/V sync pop: the test card's buoy lamp flashes on each
    // wall-clock second; synthesize the matching 1 kHz pop into the deck's
    // audio stream so flash and pop leave Deckboy together. Keyed to the
    // VISUAL being live (the card animates even while held/paused), not to
    // transport state — if the lamp is flashing on the output, it pops;
    // STOP-dark clears the frame and silences it.
    if (displayFrame_.has_value() && audioStream_ != nullptr &&
        activeCue_->kind == CueKind::Pattern && activeCue_->audioEnabled &&
        stripPatternMotionSuffix(normalizePatternTypeId(activeCue_->path)) == "pocket-test") {
      queuePocketSyncAudio();
    }
    if (duration_ > 0.0 && state_ == TransportState::Playing) {
      currentPosition_ = position();
      if (nextPausePointIdx_ < pausePoints_.size() &&
          currentPosition_ >= pausePoints_[nextPausePointIdx_]) {
        ++nextPausePointIdx_;
        pause();
        return;
      }
      if (currentPosition_ >= duration_ - 0.01) {
        handlePlaybackEnd();
      }
    } else {
      // Paused/stopped still: hold the paused position rather than snapping to
      // 0. `pausedPosition_` is the real freeze point — 0 at the start, the
      // mid-cue spot for a manual pause, or `duration_` after a pause-on-last
      // end. Resetting to 0 here made the output evaluate the fade-IN ramp at
      // t=0 (gain 0 → fully transparent), so a held still vanished at the end
      // of its duration even though its frame was still resident.
      currentPosition_ = pausedPosition_;
    }
    return;
  }

  currentPosition_ = position();

  // Audio-master drift correction. The audio device consumes samples on its
  // own crystal; the wall clock driving position() drifts against it over
  // long-form playback (and immediately on VFR sources). When the audio
  // clock — frames queued minus frames still buffered — diverges from the
  // video position by more than ~2 frames, re-anchor the wall clock to it.
  // Skipped near EOF (audio drains before video finishes) and for live
  // streams (audioClockValid_ is false there).
  if (state_ == TransportState::Playing && audioClockValid_ && audioStream_ != nullptr &&
      !decoderEof_.load()) {
    std::uint64_t queuedFrames = audioFramesQueued_.load(std::memory_order_relaxed);
    if (queuedFrames >= 4800) {  // trust the clock only after ~100ms of audio
      double queuedSeconds = static_cast<double>(queuedFrames) / 48000.0;
      double bufferedSeconds =
        static_cast<double>(std::max(0, SDL_GetAudioStreamQueued(audioStream_)))
        / (48000.0 * static_cast<double>(audioStreamBytesPerFrame()));
      double playedWallSeconds = std::max(0.0, queuedSeconds - bufferedSeconds);
      // atempo re-times the pipe to wall rate; position space runs at
      // playbackSpeed_ × wall, so scale before comparing.
      double audioClock = audioClockStartSeconds_ + playedWallSeconds * playbackSpeed_;
      // Stall detection: the audio clock only counts as a master while it is
      // actually ADVANCING. If the device stops consuming (endpoint lost,
      // WASAPI stream dead) or the audio pipe dies mid-file, the clock
      // freezes — correcting against a frozen clock pins video at that
      // position forever. Fall back to the wall clock until it moves again.
      Uint64 nowMs = SDL_GetTicks();
      if (audioClock > lastAudioClockSeconds_ + 0.001) {
        lastAudioClockSeconds_ = audioClock;
        lastAudioClockAdvanceMs_ = nowMs;
      }
      bool clockAdvancing = lastAudioClockAdvanceMs_ != 0 &&
                            (nowMs - lastAudioClockAdvanceMs_) < 400;
      double drift = currentPosition_ - audioClock;
      bool nearEnd = duration_ > 0.0 && audioClock >= duration_ - 0.25;
      if (clockAdvancing && std::abs(drift) > 0.06 && audioClock >= 0.0 && !nearEnd) {
        playbackClockStart_ = std::chrono::steady_clock::now();
        playbackStartPosition_ = audioClock;
        currentPosition_ = audioClock;
      }
    }
  }

  if (state_ == TransportState::Playing && nextPausePointIdx_ < pausePoints_.size()) {
    if (currentPosition_ >= pausePoints_[nextPausePointIdx_]) {
      ++nextPausePointIdx_;
      pause();
      return;
    }
  }

  std::uint64_t targetFrame = static_cast<std::uint64_t>(std::floor(currentPosition_ * frameRate_));

  bool advancedDisplayFrame = false;
  std::uint64_t advancedFrameIndex = static_cast<std::uint64_t>(-1);
  {
    std::lock_guard<std::mutex> lock(frameMutex_);
    while (!frameQueue_.empty() && frameQueue_.front().index <= targetFrame) {
      displayFrame_ = std::move(frameQueue_.front());
      frameQueue_.pop_front();
      lastRenderedFrameIndex_ = displayFrame_->index;
      advancedDisplayFrame = true;
      advancedFrameIndex = displayFrame_->index;
    }
  }

  if (advancedDisplayFrame && shouldMeasureMediaFps()) {
    recordMediaFrameAdvance(advancedFrameIndex);
  }

  // Upload only when the display frame actually changed (or the texture was
  // torn down). The old unconditional call re-uploaded the same pixels every
  // render tick — at the 240 Hz loop floor that was hundreds of MB/s of bus
  // traffic per playing deck for nothing. render() still re-uploads directly
  // when effect parameters change without a new frame.
  if (displayFrame_ &&
      (displayFrame_->index != lastUploadedFrameIndex_ ||
       (!texture_ && !displayFrame_->isGpu()))) {
    uploadFrame(*displayFrame_);
    lastUploadedFrameIndex_ = displayFrame_->index;
  }

  // Decode watchdog (in-process path): playing, not at EOF, queue starved and
  // the decode thread hasn't produced a frame in seconds — a wedged decoder
  // must rerack the deck (transport polls consumeDecodeStall()), not hang it.
  if (inprocDecodeActive_ && decodersRunning_ && state_ == TransportState::Playing &&
      !decoderEof_.load() && activeCue_ && activeCue_->kind == CueKind::Video) {
    Uint64 lastPush = lastFramePushMs_.load();
    if (lastPush != 0 && SDL_GetTicks() - lastPush > 4000 && queuedFrames() == 0) {
      decodeStallLatched_ = true;
    }
  }

  if (state_ == TransportState::Playing && duration_ > 0.0 && currentPosition_ >= duration_ - 0.01) {
    handlePlaybackEnd();
  }

  if (state_ == TransportState::Playing && decoderEof_ && queuedFrames() == 0 && currentPosition_ >= duration_ - 0.02) {
    handlePlaybackEnd();
  }
}

// Check and consume the decode-stall watchdog latch. Returns true once per
// stall; the transport handler reracks the deck and toasts the operator.
bool MediaEngine::consumeDecodeStall() {
  if (decodeStallLatched_) {
    decodeStallLatched_ = false;
    return true;
  }
  return false;
}

bool MediaEngine::consumeStillDecodeFailure() {
  return imageDecodeFailed_.exchange(false, std::memory_order_acq_rel);
}

// Reset all FPS measurement state. Called on cue load and after runtime refresh
// to start a fresh measurement window for the new decode stream.
void MediaEngine::resetMediaFpsTelemetry() {
  mediaFpsSampleStartedAtMs_ = 0;
  mediaFpsFrameCount_ = 0;
  mediaFpsMeasured_ = 0.0;
  lastMeasuredMediaFrameIndex_ = static_cast<std::uint64_t>(-1);
}

// FPS measurement is only meaningful for continuously-decoded streams:
// video files, browser capture, and source capture. Still images and
// pattern cues don't have a decode frame rate to measure.
bool MediaEngine::shouldMeasureMediaFps() const {
  return activeCue_ &&
         (activeCue_->kind == CueKind::Video || isBrowserCapturing_ || isSourceCapturing_);
}

// Record a frame advance for FPS telemetry. Uses a sliding 750ms window:
// counts unique frames within the window, then computes frames/second when
// the window expires. The result is stored in mediaFpsMeasured_ and exposed
// to the UI via mediaFpsMeasured() for the performance overlay.
void MediaEngine::recordMediaFrameAdvance(std::uint64_t frameIndex) {
  if (frameIndex == static_cast<std::uint64_t>(-1) ||
      frameIndex == lastMeasuredMediaFrameIndex_) {
    return;  // duplicate or sentinel — skip
  }
  lastMeasuredMediaFrameIndex_ = frameIndex;
  Uint64 now = SDL_GetTicks();
  if (mediaFpsSampleStartedAtMs_ == 0) {
    // First frame in this measurement window — start the clock
    mediaFpsSampleStartedAtMs_ = now;
    mediaFpsFrameCount_ = 0;
    mediaFpsMeasured_ = 0.0;
  }
  mediaFpsFrameCount_ += 1;
  Uint64 elapsedMs = now - mediaFpsSampleStartedAtMs_;
  if (elapsedMs >= 750) {
    // Window expired — compute FPS and start a new window
    mediaFpsMeasured_ = elapsedMs > 0
      ? (static_cast<double>(mediaFpsFrameCount_) * 1000.0 / static_cast<double>(elapsedMs))
      : mediaFpsMeasured_;
    mediaFpsFrameCount_ = 0;
    mediaFpsSampleStartedAtMs_ = now;
  }
}

// Return a pointer to the currently displayed frame, or nullptr if no frame
// is loaded. Used by the output renderer (app_render_output.ipp) to read
// pixel data directly for compositing onto the output window/NDI/DeckLink.
const DecodedFrame* MediaEngine::currentFrame() const {
  return displayFrame_.has_value() ? &(*displayFrame_) : nullptr;
}

// ---------------------------------------------------------------------------
// render — Draw the current frame and transition overlay to the given rect.
//
// This handles:
//   1. Draw the current frame (if available) using drawTextureFitted()
//      with the cue's scale mode, offset, rotation, and crop
//   2. If a transition is active, draw the transition overlay (crossfade
//      blend or dip-to-black) using drawTransitionOverlay()
//   3. Apply the visual fade gain (fade-in/fade-out alpha)
//
// Called from the main render loop for the inline preview. The output window
// uses a separate path (app_render_output.ipp) that reads currentFrame()
// directly and composites with its own AOI/warp/edge-blend pipeline.
// ---------------------------------------------------------------------------
void MediaEngine::render(SDL_Rect target) {
  bool pixelEffectsChanged = false;
  if (activeCue_) {
    bool prevKeyEnabled = chromaKeyEnabled_;
    SDL_Color prevKeyColor = chromaKeyColor_;
    float prevKeyTolerance = chromaKeyTolerance_;
    float prevKeySoftness = chromaKeySoftness_;
    float prevBrightness = brightness_;
    float prevContrast = contrast_;
    float prevSaturation = saturation_;
    float prevHueShift = hueShift_;

    outputScaleX_ = activeCue_->outputScaleX;
    outputScaleY_ = activeCue_->outputScaleY;
    scaleMode_ = activeCue_->scaleMode;
    outputOffsetX_ = activeCue_->outputOffsetX;
    outputOffsetY_ = activeCue_->outputOffsetY;
    outputRotationDegrees_ = activeCue_->outputRotationDegrees;
    cropLeft_ = activeCue_->cropLeft;
    cropRight_ = activeCue_->cropRight;
    cropTop_ = activeCue_->cropTop;
    cropBottom_ = activeCue_->cropBottom;
    chromaKeyEnabled_ = activeCue_->chromaKeyEnabled;
    chromaKeyColor_ = activeCue_->chromaKeyColor;
    chromaKeyTolerance_ = activeCue_->chromaKeyTolerance;
    chromaKeySoftness_ = activeCue_->chromaKeySoftness;
    brightness_ = std::clamp(activeCue_->brightness, 0.0f, 2.0f);
    contrast_ = std::clamp(activeCue_->contrast, 0.0f, 2.0f);
    saturation_ = std::clamp(activeCue_->saturation, 0.0f, 2.0f);
    hueShift_ = std::clamp(activeCue_->hueShift, -180.0f, 180.0f);

    pixelEffectsChanged =
      prevKeyEnabled != chromaKeyEnabled_
      || prevKeyColor.r != chromaKeyColor_.r
      || prevKeyColor.g != chromaKeyColor_.g
      || prevKeyColor.b != chromaKeyColor_.b
      || std::fabs(prevKeyTolerance - chromaKeyTolerance_) > 0.001f
      || std::fabs(prevKeySoftness - chromaKeySoftness_) > 0.001f
      || std::fabs(prevBrightness - brightness_) > 0.001f
      || std::fabs(prevContrast - contrast_) > 0.001f
      || std::fabs(prevSaturation - saturation_) > 0.001f
      || std::fabs(prevHueShift - hueShift_) > 0.001f;
  }
  if (pixelEffectsChanged && displayFrame_) {
    uploadFrame(*displayFrame_);
  }

  SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, 255);
  SDL_RenderFillRect(outputRenderer_, nullptr);

  bool drewCurrent = drawTextureFitted(texture_, textureWidth_, textureHeight_, target, 255);
  drawTransitionOverlay(target, drewCurrent);

  double gain = visualFadeGainAt(position());
  if (drewCurrent && gain < 0.999) {
    SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, static_cast<Uint8>((1.0 - gain) * 255.0));
    SDL_RenderFillRect(outputRenderer_, nullptr);
  }
}

// Regenerate the pattern cue's pixels using the current wall-clock time.
// Called each frame from the main loop for animated patterns (e.g. pocket-test,
// any pattern with the -motion suffix). The wall time drives animation phase
// so the pattern progresses smoothly regardless of frame rate.
// Regenerate a Timer cue from the transport clock. See the header for why the
// transport IS the timer rather than a clock beside it.

void MediaEngine::rebuildTimerFrame(const Cue& cue, double elapsedSeconds, bool running) {
  auto size = currentOutputSizeHint();
  const int w = size.first;
  const int h = size.second;
  if (w <= 0 || h <= 0) {
    return;
  }
  DecodedFrame frame;
  frame.width = w;
  frame.height = h;
  frame.format = FramePixelFormat::RGBA32;
  frame.pixels.assign(static_cast<std::size_t>(w) * h * 4, 0);
  buildTimerFrame(frame, cue.timer, elapsedSeconds, running);
  displayFrame_ = std::move(frame);
  displayFrame_->index = ++displayFrameSerial_;
  uploadFrame(*displayFrame_);
}


// ---------------------------------------------------------------------------
// Video synth
//
// Atari Video Music folded simple shapes through mirrors; Sleepy Circuits
// Hypno adds FEEDBACK, transforming each frame and blending it into the next.
// The feedback is what makes it look alive rather than like a screensaver, so
// it is the structural centre of this rather than a post-effect.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Text-mode cell rendering.
//
// The references are not "a filter over a picture" -- they are a CHARACTER
// GRID with an indexed palette, corrupted at the CELL level: wrong glyph,
// wrong colour pair, whole rows locked to one repeating character. That is
// what makes a corrupted NES tilemap or a torn ANSI screen read as authentic
// rather than as a post-effect, so this renders cells rather than filtering
// pixels.
//
// The ramp is deliberately BLOCK and DITHER characters rather than letters.
// Letters carry meaning the picture does not have; blocks and dithers carry
// density, which is the only thing a cell needs to say.
// ---------------------------------------------------------------------------
namespace {

// 5x7 cells, densest last. Bit 4 is the leftmost pixel.
const std::uint8_t kCellRamp[][7] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00},   // empty
  {0x00,0x00,0x04,0x00,0x00,0x00,0x00},   // single dot
  {0x00,0x0A,0x00,0x0A,0x00,0x0A,0x00},   // sparse dither
  {0x15,0x00,0x0A,0x00,0x15,0x00,0x0A},   // 25% checker
  {0x15,0x0A,0x15,0x0A,0x15,0x0A,0x15},   // 50% checker
  {0x00,0x0E,0x0A,0x0E,0x00,0x00,0x00},   // small box
  {0x1F,0x11,0x11,0x11,0x11,0x11,0x1F},   // hollow block
  {0x0A,0x1F,0x1F,0x1F,0x1F,0x1F,0x0A},   // heavy
  {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F},   // solid
};
constexpr int kCellRampCount = static_cast<int>(sizeof(kCellRamp) / sizeof(kCellRamp[0]));

// Glyphs that are STRUCTURE rather than density -- lines and corners. Cell
// corruption swaps in one of these, which is what produces the box-drawing
// wreckage in a torn text screen instead of a tidy density gradient.
const std::uint8_t kCellJunk[][7] = {
  {0x04,0x04,0x04,0x1F,0x04,0x04,0x04},   // cross
  {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},   // equals
  {0x1C,0x10,0x10,0x10,0x10,0x10,0x1C},   // bracket
  {0x11,0x0A,0x04,0x0A,0x11,0x00,0x00},   // x
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},   // F
  {0x04,0x04,0x04,0x04,0x04,0x04,0x04},   // pipe
  {0x00,0x1F,0x00,0x00,0x00,0x1F,0x00},   // rails
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},   // O
};
constexpr int kCellJunkCount = static_cast<int>(sizeof(kCellJunk) / sizeof(kCellJunk[0]));

// A conventional ASCII density ramp, drawn rather than borrowed from the timer
// font so the set is self-contained and ordered exactly by ink coverage.
// space . : - + * o X # @
const std::uint8_t kCellAscii[][7] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x04,0x00},   // .
  {0x00,0x04,0x00,0x00,0x04,0x00,0x00},   // :
  {0x00,0x00,0x00,0x0E,0x00,0x00,0x00},   // -
  {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},   // +
  {0x00,0x11,0x0A,0x1F,0x0A,0x11,0x00},   // *
  {0x00,0x0E,0x11,0x11,0x11,0x0E,0x00},   // o
  {0x11,0x0A,0x04,0x0A,0x11,0x00,0x00},   // X
  {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00},   // #
  {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E},   // @
};
constexpr int kCellAsciiCount = static_cast<int>(sizeof(kCellAscii) / sizeof(kCellAscii[0]));

// The full printable ASCII set, 0x20..0x7E, drawn as 5x7 cells. Bit 4 is the
// leftmost pixel of each row. Written out rather than borrowed from the timer
// font because that one covers only what a clock needs.
//
// The set is NOT stored in density order -- it is stored in ASCII order, and
// the ramp order is computed once by counting lit pixels. Hand-ordering 95
// glyphs by eye would be guesswork, and counting is exact.
const std::uint8_t kCellAsciiFull[][7] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00},   // space
  {0x04,0x04,0x04,0x04,0x04,0x00,0x04},   // !
  {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00},   // "
  {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A},   // #
  {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},   // $
  {0x18,0x19,0x02,0x04,0x08,0x13,0x03},   // %
  {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D},   // &
  {0x04,0x04,0x00,0x00,0x00,0x00,0x00},   // '
  {0x02,0x04,0x08,0x08,0x08,0x04,0x02},   // (
  {0x08,0x04,0x02,0x02,0x02,0x04,0x08},   // )
  {0x00,0x04,0x15,0x0E,0x15,0x04,0x00},   // *
  {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},   // +
  {0x00,0x00,0x00,0x00,0x00,0x04,0x08},   // ,
  {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},   // -
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},   // .
  {0x00,0x01,0x02,0x04,0x08,0x10,0x00},   // /
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},   // 0
  {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},   // 1
  {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},   // 2
  {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},   // 3
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},   // 4
  {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},   // 5
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},   // 6
  {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},   // 7
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},   // 8
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},   // 9
  {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},   // :
  {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08},   // ;
  {0x02,0x04,0x08,0x10,0x08,0x04,0x02},   // <
  {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},   // =
  {0x08,0x04,0x02,0x01,0x02,0x04,0x08},   // >
  {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},   // ?
  {0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E},   // @
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},   // A
  {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},   // B
  {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},   // C
  {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},   // D
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},   // E
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},   // F
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},   // G
  {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},   // H
  {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},   // I
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},   // J
  {0x11,0x12,0x14,0x18,0x14,0x12,0x11},   // K
  {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},   // L
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},   // M
  {0x11,0x11,0x19,0x15,0x13,0x11,0x11},   // N
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},   // O
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},   // P
  {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},   // Q
  {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},   // R
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},   // S
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},   // T
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},   // U
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},   // V
  {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},   // W
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},   // X
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},   // Y
  {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},   // Z
  {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},   // [
  {0x00,0x10,0x08,0x04,0x02,0x01,0x00},   // backslash
  {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},   // ]
  {0x04,0x0A,0x11,0x00,0x00,0x00,0x00},   // ^
  {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},   // _
  {0x08,0x04,0x00,0x00,0x00,0x00,0x00},   // `
  {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},   // a
  {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},   // b
  {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E},   // c
  {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},   // d
  {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},   // e
  {0x06,0x09,0x08,0x1C,0x08,0x08,0x08},   // f
  {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},   // g
  {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},   // h
  {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},   // i
  {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},   // j
  {0x10,0x10,0x12,0x14,0x18,0x14,0x12},   // k
  {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},   // l
  {0x00,0x00,0x1A,0x15,0x15,0x15,0x15},   // m
  {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},   // n
  {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},   // o
  {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},   // p
  {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01},   // q
  {0x00,0x00,0x16,0x19,0x10,0x10,0x10},   // r
  {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},   // s
  {0x08,0x08,0x1C,0x08,0x08,0x09,0x06},   // t
  {0x00,0x00,0x11,0x11,0x11,0x11,0x0F},   // u
  {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},   // v
  {0x00,0x00,0x11,0x15,0x15,0x15,0x0A},   // w
  {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},   // x
  {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},   // y
  {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},   // z
  {0x06,0x08,0x08,0x10,0x08,0x08,0x06},   // {
  {0x04,0x04,0x04,0x04,0x04,0x04,0x04},   // |
  {0x0C,0x02,0x02,0x01,0x02,0x02,0x0C},   // }
  {0x00,0x00,0x08,0x15,0x02,0x00,0x00},   // ~
};
constexpr int kCellAsciiFullCount =
  static_cast<int>(sizeof(kCellAsciiFull) / sizeof(kCellAsciiFull[0]));

// Indices into kCellAsciiFull, sorted from least to most ink. Built once on
// first use: the density ramp needs an ordering, and coverage is the only
// honest measure of how dark a glyph reads.
const std::vector<int>& asciiRampOrder() {
  static const std::vector<int> order = [] {
    std::vector<int> idx(static_cast<std::size_t>(kCellAsciiFullCount));
    for (int i = 0; i < kCellAsciiFullCount; ++i) idx[static_cast<std::size_t>(i)] = i;
    auto ink = [](int i) {
      int n = 0;
      for (int r = 0; r < 7; ++r) {
        for (int b = 0; b < 5; ++b) {
          if ((kCellAsciiFull[i][r] >> b) & 1) ++n;
        }
      }
      return n;
    };
    std::stable_sort(idx.begin(), idx.end(),
                     [&](int a, int b) { return ink(a) < ink(b); });
    return idx;
  }();
  return order;
}
// Pick the glyph table for a set, and report how many entries it has.
inline const std::uint8_t (*cellSetFor(int set, int& count))[7] {
  switch (set) {
    case 1:  count = kCellAsciiFullCount; return kCellAsciiFull;   // 95 glyphs
    case 2:  count = kCellJunkCount;      return kCellJunk;
    case 4:  count = kCellAsciiFullCount; return kCellAsciiFull;
    default: count = kCellRampCount;      return kCellRamp;
  }
}

// Hardware palettes, each the real set the machine could show. A limited,
// AUTHENTIC palette is most of why period work looks period: the colours had
// relationships to each other that a freely-picked set does not.
const std::uint8_t kPalC64[16][3] = {
  {  0,  0,  0}, {255,255,255}, {136, 57, 50}, {103,182,189},
  {139, 63,150}, { 85,160, 73}, { 64, 49,141}, {191,206,114},
  {139, 84, 41}, { 87, 66,  0}, {184,105, 98}, { 80, 80, 80},
  {120,120,120}, {148,224,137}, {120,105,196}, {159,159,159},
};
const std::uint8_t kPalGameboy[4][3] = {
  { 15, 56, 15}, { 48, 98, 48}, {139,172, 15}, {155,188, 15},
};
const std::uint8_t kPalCga[4][3] = {
  {  0,  0,  0}, { 85,255,255}, {255, 85,255}, {255,255,255},
};
const std::uint8_t kPalNes[12][3] = {
  {  0,  0,  0}, { 60, 60, 60}, {124,124,124}, {188,188,188},
  {252,252,252}, {172, 16,  0}, {248, 56,  0}, {252,160, 68},
  {  0,120,248}, { 60,188,252}, {  0,168,  0}, {184,248,184},
};
const std::uint8_t kPalVapor[8][3] = {
  { 16,  0, 32}, { 64,  0, 96}, {148, 32,180}, {255, 64,180},
  {255,128,220}, { 64,224,255}, {160,255,255}, {255,255,255},
};

// Quantise a normalised value to a table. Nearest-by-index rather than
// nearest-by-colour: the input is a ramp position, not a measured colour, and
// walking the table in order keeps the palette's own progression intact.
inline void samplePalette(const std::uint8_t table[][3], int count, double u,
                          double& r, double& g, double& b) {
  const int i = std::clamp(static_cast<int>(u * count), 0, count - 1);
  r = table[i][0] / 255.0;
  g = table[i][1] / 255.0;
  b = table[i][2] / 255.0;
}

// A 16-entry indexed palette, the EGA/VGA set the references were made on.
// Quantising to it is most of why they look like hardware and not like a
// gradient: a limited palette forces flat areas and hard colour boundaries.
const std::uint8_t kCellPalette[16][3] = {
  {  0,  0,  0}, {  0,  0,170}, {  0,170,  0}, {  0,170,170},
  {170,  0,  0}, {170,  0,170}, {170, 85,  0}, {170,170,170},
  { 85, 85, 85}, { 85, 85,255}, { 85,255, 85}, { 85,255,255},
  {255, 85, 85}, {255, 85,255}, {255,255, 85}, {255,255,255},
};

int nearestPaletteIndex(int r, int g, int b) {
  int best = 0;
  int bestDist = 1 << 30;
  for (int i = 0; i < 16; ++i) {
    const int dr = r - kCellPalette[i][0];
    const int dg = g - kCellPalette[i][1];
    const int db = b - kCellPalette[i][2];
    const int dist = dr * dr + dg * dg + db * db;
    if (dist < bestDist) { bestDist = dist; best = i; }
  }
  return best;
}

}  // namespace


// ---------------------------------------------------------------------------
// Sprite sheet import.
//
// Tiles are used exactly like glyphs: chosen by brightness, corrupted by the
// same cell logic, drawn into the same grid. That is deliberate -- a sheet is
// another alphabet, not a separate feature, so everything already built for
// text mode applies to it unchanged.
// ---------------------------------------------------------------------------
// Decode one image, scaled to fit a tile and centred on it. Used by the folder
// path, where every file is a separate sprite of its own size rather than a
// cell in a grid.
//
// Scaled to FIT rather than filled: a sprite drawn to the edges of its cell
// loses the silhouette, and the silhouette is the only thing legible once a
// sprite is standing in for a character.
static bool decodeSpriteInto(const std::string& file, int tileW, int tileH,
                             std::vector<std::uint8_t>& atlas,
                             std::size_t atlasW, int col, int row) {
  // The filter does the fitting, so the output is ALWAYS tileW x tileH and its
  // size is known without probing the source first. Scaled to FIT and padded
  // centred: a sprite drawn to the edges of its cell loses the silhouette, and
  // the silhouette is the only legible thing once a sprite stands in for a
  // character. Nearest neighbour because this is pixel art and smoothing it on
  // the way in destroys what makes it worth using.
  const std::string chain =
    "scale=" + std::to_string(tileW) + ":" + std::to_string(tileH) +
    ":force_original_aspect_ratio=decrease:flags=neighbor,"
    "pad=" + std::to_string(tileW) + ":" + std::to_string(tileH) +
    ":(ow-iw)/2:(oh-ih)/2:color=0x00000000";

  ChildProcess proc;
  const std::vector<std::string> args {
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", file,
    "-frames:v", "1", "-vf", chain,
    "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1"
  };
  if (!spawnPipeProcess(proc, args)) return false;
  std::vector<std::uint8_t> px(static_cast<std::size_t>(tileW) * tileH * 4);
  const bool ok = readExact(proc.readFd, px.data(), px.size());
  proc.stop();
  if (!ok) return false;

  for (int y = 0; y < tileH; ++y) {
    for (int x = 0; x < tileW; ++x) {
      const std::size_t src = (static_cast<std::size_t>(y) * tileW + x) * 4;
      const std::size_t dstX = static_cast<std::size_t>(col * tileW + x);
      const std::size_t dstY = static_cast<std::size_t>(row * tileH + y);
      const std::size_t dst = (dstY * atlasW + dstX) * 4;
      if (dst + 3 >= atlas.size()) continue;
      atlas[dst + 0] = px[src + 0];
      atlas[dst + 1] = px[src + 1];
      atlas[dst + 2] = px[src + 2];
      atlas[dst + 3] = px[src + 3];
    }
  }
  return true;
}

bool MediaEngine::ensureSpriteSheet(const std::string& path, int tileW, int tileH) {
  if (path.empty() || tileW < 2 || tileH < 2) return false;
  if (spriteSheetLoaded_ == path && spriteSheetTileW_ == tileW &&
      spriteSheetTileH_ == tileH) {
    return !spriteTilesByLuma_.empty();
  }
  spriteSheetLoaded_ = path;
  spriteSheetTileW_ = tileW;
  spriteSheetTileH_ = tileH;
  spriteTilesByLuma_.clear();
  spriteSheetRgba_.clear();

  // A FOLDER of individual sprites, rather than one gridded sheet. This is how
  // most sprite collections actually arrive -- one file per sprite, each its
  // own size -- and slicing such a file on a grid yields a single tile, which
  // renders as the same sprite in every cell and looks like the feature is
  // doing nothing.
  std::error_code dirEc;
  if (std::filesystem::is_directory(path, dirEc)) {
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::directory_iterator(path, dirEc)) {
      if (!entry.is_regular_file()) continue;
      std::string ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (ext == ".png" || ext == ".gif" || ext == ".bmp" ||
          ext == ".jpg" || ext == ".jpeg" || ext == ".webp") {
        files.push_back(entry.path().string());
      }
    }
    std::sort(files.begin(), files.end());
    // Capped. Each file is a separate ffmpeg decode, so a folder of thousands
    // would stall for minutes; 256 tiles is already far more alphabet than a
    // cell grid can show at once.
    constexpr std::size_t kMaxTiles = 256;
    if (files.size() > kMaxTiles) files.resize(kMaxTiles);
    if (files.empty()) return false;

    const int cols = static_cast<int>(std::ceil(std::sqrt(
      static_cast<double>(files.size()))));
    const int rows = static_cast<int>((files.size() + cols - 1) / cols);
    spriteSheetW_ = cols * tileW;
    spriteSheetH_ = rows * tileH;
    spriteSheetCols_ = cols;
    spriteSheetRows_ = rows;
    spriteSheetRgba_.assign(
      static_cast<std::size_t>(spriteSheetW_) * spriteSheetH_ * 4, 0);

    // Per file, because the concat demuxer requires every input to share
    // stream parameters and a folder of individual sprites is by definition
    // all different sizes. The one-pass version built an atlas that scored
    // zero usable tiles and rendered black -- correct for a real sheet,
    // useless for the case this path exists to serve.
    //
    // ffprobe is NOT called any more: the filter chain pads every sprite to
    // exactly the tile size, so the output byte count is known in advance.
    // That halves the process count against the first version.
    for (std::size_t i = 0; i < files.size(); ++i) {
      decodeSpriteInto(files[i], tileW, tileH, spriteSheetRgba_,
                       static_cast<std::size_t>(spriteSheetW_),
                       static_cast<int>(i % cols), static_cast<int>(i / cols));
    }

    // Rank exactly as the sheet path does, but WITHOUT the coverage filter: a
    // small sprite centred on a large tile is legitimately sparse, and
    // dropping it would discard most of a folder.
    for (std::size_t i = 0; i < files.size(); ++i) {
      const int tx = static_cast<int>(i % cols);
      const int ty = static_cast<int>(i / cols);
      long long total = 0, covered = 0;
      for (int y = 0; y < tileH; ++y) {
        const std::size_t rowOff =
          (static_cast<std::size_t>(ty * tileH + y) * spriteSheetW_ + tx * tileW) * 4;
        for (int x = 0; x < tileW; ++x) {
          const std::size_t o = rowOff + static_cast<std::size_t>(x) * 4;
          if (o + 3 >= spriteSheetRgba_.size()) continue;
          if (spriteSheetRgba_[o + 3] < 32) continue;
          total += (spriteSheetRgba_[o + 0] * 77 + spriteSheetRgba_[o + 1] * 151 +
                    spriteSheetRgba_[o + 2] * 28) >> 8;
          ++covered;
        }
      }
      if (covered == 0) continue;
      const int mean = static_cast<int>(total / covered);
      const int weighted =
        static_cast<int>(mean * covered / std::max(1, tileW * tileH));
      spriteTilesByLuma_.emplace_back(weighted, static_cast<int>(i));
    }
    std::sort(spriteTilesByLuma_.begin(), spriteTilesByLuma_.end());
    return !spriteTilesByLuma_.empty();
  }

  // Size first, so the buffer can be exact. A sheet is an ordinary image, so
  // this is the same probe the still-image path uses.
  int srcW = 0, srcH = 0;
  {
    const std::vector<std::string> probeArgs {
      "ffprobe", "-v", "error", "-select_streams", "v:0",
      "-show_entries", "stream=width,height", "-of", "csv=p=0", path
    };
    if (auto probe = readAllText(probeArgs)) {
      std::sscanf(probe->c_str(), "%d,%d", &srcW, &srcH);
    }
  }
  if (srcW <= 0 || srcH <= 0) return false;

  ChildProcess proc;
  const std::vector<std::string> args {
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path,
    "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1"
  };
  if (!spawnPipeProcess(proc, args)) return false;
  const std::size_t want = static_cast<std::size_t>(srcW) * srcH * 4;
  spriteSheetRgba_.resize(want);
  const bool ok = readExact(proc.readFd, spriteSheetRgba_.data(), want);
  proc.stop();
  if (!ok) { spriteSheetRgba_.clear(); return false; }

  spriteSheetW_ = srcW;
  spriteSheetH_ = srcH;
  spriteSheetCols_ = srcW / tileW;
  spriteSheetRows_ = srcH / tileH;
  if (spriteSheetCols_ <= 0 || spriteSheetRows_ <= 0) return false;

  // Rank tiles by mean brightness, weighted by ALPHA. Sprite sheets are mostly
  // transparent padding, and a tile that is 90% empty should read as dark
  // whatever colour the remaining pixels are.
  for (int ty = 0; ty < spriteSheetRows_; ++ty) {
    for (int tx = 0; tx < spriteSheetCols_; ++tx) {
      long long total = 0;
      long long covered = 0;
      for (int y = 0; y < tileH; ++y) {
        const std::size_t rowOff =
          (static_cast<std::size_t>(ty * tileH + y) * srcW + tx * tileW) * 4;
        for (int x = 0; x < tileW; ++x) {
          const std::size_t o = rowOff + static_cast<std::size_t>(x) * 4;
          const int a = spriteSheetRgba_[o + 3];
          if (a < 32) continue;
          const int l = (spriteSheetRgba_[o + 0] * 77 +
                         spriteSheetRgba_[o + 1] * 151 +
                         spriteSheetRgba_[o + 2] * 28) >> 8;
          total += l;
          ++covered;
        }
      }
      const int area = tileW * tileH;
      if (covered * 4 < area) continue;   // mostly empty: not a usable tile
      const int mean = static_cast<int>(total / std::max<long long>(1, covered));
      // Scale by coverage so a sparse tile ranks darker than a solid one of
      // the same colour.
      const int weighted = static_cast<int>(mean * covered / area);
      spriteTilesByLuma_.emplace_back(weighted, ty * spriteSheetCols_ + tx);
    }
  }
  std::sort(spriteTilesByLuma_.begin(), spriteTilesByLuma_.end());
  return !spriteTilesByLuma_.empty();
}

void MediaEngine::rebuildVideoSynthFrame(const Cue& cue, double wallSeconds,
                                         double audioLevel) {
  auto size = currentOutputSizeHint();
  const int outW = size.first;
  const int outH = size.second;
  if (outW <= 0 || outH <= 0) return;

  const VideoSynthSettings& vs = cue.videoSynth;

  // DECKBOY_SYNTH_BENCH=1 reports where the frame time goes, averaged over 60
  // frames. The synth is the one source that can hold the whole render loop
  // down, so "it feels heavy" needs to become numbers before anything is
  // changed on its account.
  static const bool synthBench = [] {
    const char* e = std::getenv("DECKBOY_SYNTH_BENCH");
    return e && *e;
  }();
  using SynthClock = std::chrono::steady_clock;
  const auto benchT0 = SynthClock::now();
  auto benchSmall = benchT0, benchGlitch = benchT0, benchCrt = benchT0,
       benchScale = benchT0;

  // Rate-limited to the DISPLAY. This ran on every update tick, and the update
  // loop has a 240Hz floor -- so on a 60Hz output it was generating (and
  // discarding) up to four frames for every one shown. rebuildPatternFrame has
  // had this limiter all along; the video synth simply never got one, which is
  // the single largest cause of it feeling heavy.
  {
    double refreshHz = 60.0;
    if (outputSizeProvider_) {
      OutputModeHint mode = outputSizeProvider_();
      if (mode.refreshHz >= 1.0) {
        refreshHz = std::clamp(mode.refreshHz, 23.0, 240.0);
      }
    }
    const double minInterval = 1.0 / refreshHz;
    if (displayFrame_ && wallSeconds - lastVideoSynthSeconds_ < minInterval * 0.98) {
      return;
    }
    lastVideoSynthSeconds_ = wallSeconds;
  }

  // RENDER SMALL, THEN UPSCALE. The first version generated every pixel of a
  // 1080p raster with three or four sin() calls each -- about eight million
  // transcendental ops a frame, which measured out at roughly 3fps.
  //
  // Rendering at a low internal resolution is not a compromise here, it is the
  // correct look: Atari Video Music ran at broadcast-era resolution and Hypno
  // works at a low internal size too. Big soft pixels ARE the aesthetic. This
  // also makes the feedback path cheap enough to be interesting.
  // Finer than the first pass. At 480 wide the nearest-neighbour upscale makes
  // every internal pixel a 4x4 block, so slow motion crawls in visible jumps
  // rather than gliding -- the quantisation, not the speed, is what stops it
  // looking smooth. The hard-edged shapes cost no transcendentals, so this is
  // affordable now in a way it was not when everything went through sin().
  // Resolution is the operator's, not a constant. Divisor 8 down to 2: at the
  // coarse end this is both the chunky look and a quarter of the pixels.
  const int divisor = 10 - 2 * std::clamp(vs.resolution, 1, 5) + 2;  // 1->10 .. 5->2
  const int W = std::clamp(outW / std::max(2, divisor), 120, 960);
  const int H = std::max(90, (W * outH) / std::max(1, outW));

  const double lvl = std::clamp(audioLevel, 0.0, 1.0);
  const double react = std::clamp(vs.audioReactivity, 0.0, 1.0);
  const double drive = 1.0 + lvl * react * 1.5;
  const double t = wallSeconds * std::clamp(vs.speed, 0.01, 8.0);

  const bool feedbackOn = vs.feedbackAmount > 0.01;
  const bool havePrev = feedbackOn && vsynthPrevW_ == W && vsynthPrevH_ == H &&
                        vsynthPrev_.size() == static_cast<std::size_t>(W) * H * 4;
  // Per SECOND, not per frame, and scaled by speed like everything else.
  // This advanced once per frame regardless of elapsed time, so the rotation
  // ran at whatever rate the render loop happened to hit and ignored the speed
  // control entirely -- which is precisely why the motion could not be dialled
  // down. Framerate-dependent animation is never right; here it also made the
  // one control that should have fixed it useless.
  const double dt = (vsynthLastSeconds_ > 0.0)
    ? std::clamp(wallSeconds - vsynthLastSeconds_, 0.0, 0.25)
    : 0.0;
  vsynthLastSeconds_ = wallSeconds;
  vsynthRotation_ += vs.feedbackRotate * 0.017453292519943295 * dt *
                     std::clamp(vs.speed, 0.01, 8.0) * 60.0;

  const double scale = std::clamp(vs.scale, 0.1, 8.0) * 4.0;
  const double warp = std::clamp(vs.warp, 0.0, 2.0);
  const double cosR = std::cos(vsynthRotation_);
  const double sinR = std::sin(vsynthRotation_);
  const double zoom = std::clamp(vs.feedbackZoom, 0.90, 1.15);
  const double fb = std::clamp(vs.feedbackAmount, 0.0, 0.95);

  // Reused across frames. assign() only reallocates when the size changes,
  // which happens when the operator moves the detail control, not per frame.
  const std::size_t smallBytes = static_cast<std::size_t>(W) * H * 4;
  if (vsynthSmall_.size() != smallBytes) vsynthSmall_.assign(smallBytes, 0);
  std::vector<std::uint8_t>& small = vsynthSmall_;

  // Precompute the row term once per row instead of per pixel. Halves the
  // transcendental count on its own.
  // Threaded by row. Rows are independent -- each writes only its own output
  // and reads only the PREVIOUS frame -- so this parallelises with no locking.
  // Raising the internal resolution without doing this is what made the
  // controls feel like they bogged the app down.
  auto renderRows = [&](int rowBegin, int rowEnd) {
  for (int y = rowBegin; y < rowEnd; ++y) {
    const double nyRaw = (static_cast<double>(y) / H - 0.5) * 2.0;
    for (int x = 0; x < W; ++x) {
      double nx = (static_cast<double>(x) / W - 0.5) * 2.0 *
                  (static_cast<double>(outW) / outH);
      double ny = nyRaw;

      switch (vs.mirror) {
        case VideoSynthMirror::Horizontal: nx = std::abs(nx); break;
        case VideoSynthMirror::Quad:       nx = std::abs(nx); ny = std::abs(ny); break;
        case VideoSynthMirror::Kaleido:
          nx = std::abs(nx); ny = std::abs(ny);
          if (ny > nx) std::swap(nx, ny);
          break;
        default: break;
      }

      const double sx = nx * scale * drive;
      const double sy = ny * scale * drive;
      double v = 0.0;
      switch (vs.shape) {
        case VideoSynthShape::Diamond: {
          // HARD edges, not a sine field. Atari Video Music's look comes from
          // flat bands of colour with sharp boundaries; a smooth gradient
          // reads as a screensaver instead.
          const double band = std::fmod(std::abs(sx) + std::abs(sy) + t * 2.0, 2.0);
          v = band < 1.0 ? 1.0 : -1.0;
          break;
        }
        case VideoSynthShape::Rings: {
          const double r = std::sqrt(sx * sx + sy * sy);
          const double band = std::fmod(r * 1.5 - t * 2.0, 2.0);
          v = band < 1.0 ? 1.0 : -1.0;   // concentric BANDS, not a ripple
          break;
        }
        case VideoSynthShape::Grid: {
          const double gx = std::fmod(std::abs(sx + t), 2.0) < 1.0 ? 1.0 : -1.0;
          const double gy = std::fmod(std::abs(sy - t * 1.3), 2.0) < 1.0 ? 1.0 : -1.0;
          v = gx * gy;
          break;
        }
        case VideoSynthShape::Moire: {
          const double a1 = sx * cosR - sy * sinR;
          const double a2 = sx * cosR + sy * sinR;
          v = std::sin(a1 * 3.0 + t) * std::sin(a2 * 3.1 - t);
          break;
        }
        default:
          v = std::sin(sx + t) + std::sin(sy - t * 1.1)
            + std::sin((sx + sy) * warp + t * 0.7);
          v /= 3.0;
          break;
      }

      const double u = std::clamp(v * 0.5 + 0.5, 0.0, 1.0);

      // POSTERISED palettes. The reference machines had a handful of colours,
      // not a smooth ramp, and quantising is most of why they look designed
      // rather than computed.
      const int steps = 6;
      const double q = std::floor(u * steps) / (steps - 1);
      double r8 = 0.0, g8 = 0.0, b8 = 0.0;
      switch (vs.palette) {
        case VideoSynthPalette::Amber: r8 = q; g8 = q * 0.55; b8 = q * 0.05; break;
        case VideoSynthPalette::Ice:   r8 = q * 0.2; g8 = q * 0.65; b8 = q; break;
        case VideoSynthPalette::Fire:  r8 = q; g8 = q * q * 0.6; b8 = q * q * q * 0.2; break;
        case VideoSynthPalette::Mono:  r8 = g8 = b8 = q; break;
        case VideoSynthPalette::Ega: {
          const int i = std::clamp(static_cast<int>(q * 16), 0, 15);
          r8 = kCellPalette[i][0] / 255.0;
          g8 = kCellPalette[i][1] / 255.0;
          b8 = kCellPalette[i][2] / 255.0;
          break;
        }
        case VideoSynthPalette::C64:
          samplePalette(kPalC64, 16, q, r8, g8, b8); break;
        case VideoSynthPalette::Gameboy:
          samplePalette(kPalGameboy, 4, q, r8, g8, b8); break;
        case VideoSynthPalette::Cga:
          samplePalette(kPalCga, 4, q, r8, g8, b8); break;
        case VideoSynthPalette::Nes:
          samplePalette(kPalNes, 12, q, r8, g8, b8); break;
        case VideoSynthPalette::Vapor:
          samplePalette(kPalVapor, 8, q, r8, g8, b8); break;
        default: {
          const double hue = std::fmod(q + t * 0.08, 1.0) * 6.0;
          const int seg = static_cast<int>(hue) % 6;
          const double f = hue - std::floor(hue);
          switch (seg) {
            case 0: r8 = 1;     g8 = f;     b8 = 0;     break;
            case 1: r8 = 1 - f; g8 = 1;     b8 = 0;     break;
            case 2: r8 = 0;     g8 = 1;     b8 = f;     break;
            case 3: r8 = 0;     g8 = 1 - f; b8 = 1;     break;
            case 4: r8 = f;     g8 = 0;     b8 = 1;     break;
            default: r8 = 1;    g8 = 0;     b8 = 1 - f; break;
          }
          break;
        }
      }

      double fr = r8 * 255.0, fg = g8 * 255.0, fbc = b8 * 255.0;

      if (havePrev) {
        const double cx = W * 0.5, cy = H * 0.5;
        const double dx = (x - cx) / zoom;
        const double dy = (y - cy) / zoom;
        const int px = static_cast<int>(dx * cosR - dy * sinR + cx);
        const int py = static_cast<int>(dx * sinR + dy * cosR + cy);
        // CLAMPED, not skipped. With zoom below 1 the sample coordinates
        // expand past the raster at the edges, and skipping meant those pixels
        // received no feedback at all -- a clean border where the effect
        // visibly stopped. Clamping smears the edge outward instead, which is
        // what the analogue machines did anyway.
        {
          const int cpx = std::clamp(px, 0, W - 1);
          const int cpy = std::clamp(py, 0, H - 1);
          const std::size_t o = (static_cast<std::size_t>(cpy) * W + cpx) * 4;
          // Feedback is ADDITIVE-leaning rather than a plain crossfade: the
          // trails brighten where they overlap, which is what makes a video
          // feedback loop look like light rather than like a blur.
          fr = std::min(255.0, fr * (1.0 - fb * 0.6) + vsynthPrev_[o + 0] * fb);
          fg = std::min(255.0, fg * (1.0 - fb * 0.6) + vsynthPrev_[o + 1] * fb);
          fbc = std::min(255.0, fbc * (1.0 - fb * 0.6) + vsynthPrev_[o + 2] * fb);
          // Decay, or the loop saturates to white within a second.
          fr *= 0.94; fg *= 0.94; fbc *= 0.94;
        }
      }

      const std::size_t off = (static_cast<std::size_t>(y) * W + x) * 4;
      // Red first, matching RGBA32. This wrote blue into byte 0, which put
      // every hardware palette out by a red/blue exchange -- the palettes are
      // the whole point of those modes, so they were the most wrong thing on
      // screen while still looking superficially plausible.
      small[off + 0] = static_cast<std::uint8_t>(std::clamp(fr, 0.0, 255.0));
      small[off + 1] = static_cast<std::uint8_t>(std::clamp(fg, 0.0, 255.0));
      small[off + 2] = static_cast<std::uint8_t>(std::clamp(fbc, 0.0, 255.0));
      small[off + 3] = 255;
    }
  }
  };   // renderRows

  {
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 1;
    const int workers = std::max(1, std::min<int>(static_cast<int>(hw), std::max(1, H / 16)));
    if (workers <= 1) {
      renderRows(0, H);
    } else {
      std::vector<std::thread> pool;
      pool.reserve(static_cast<std::size_t>(workers - 1));
      const int span = (H + workers - 1) / workers;
      for (int w = 1; w < workers; ++w) {
        const int b = std::min(H, w * span);
        const int e = std::min(H, b + span);
        if (b >= e) break;
        pool.emplace_back(renderRows, b, e);
      }
      renderRows(0, std::min(H, span));
      for (std::thread& t : pool) t.join();
    }
  }
  if (synthBench) benchSmall = SynthClock::now();

  // ---- Glitch stack --------------------------------------------------------
  // Applied to the low-resolution buffer, in a fixed order. Each is skipped
  // entirely at 0, so an operator who wants the clean synth pays nothing.
  {
    // Deterministic per-frame randomness. A different seed each frame gives
    // movement; a HASH of the frame counter rather than a running generator
    // means the look does not depend on how many frames have been drawn, so
    // the same settings look the same whenever they are dialled up.
    std::uint32_t seed = static_cast<std::uint32_t>(displayFrameSerial_ * 2654435761u) | 1u;
    auto rnd = [&seed]() {
      seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
      return seed;
    };

    // PIXEL SORT. Runs of pixels within a row, sorted by brightness. This is
    // what produces the dragged, melted, datamosh look: bright material slides
    // along the row and pools against whatever stops it.
    const double sortAmt = std::clamp(vs.pixelSort, 0.0, 1.0);
    if (sortAmt > 0.01) {
      // The threshold falls as the amount rises, so more of each row qualifies
      // and the runs grow longer. Driving run LENGTH directly instead would
      // cut spans at arbitrary points and look like banding rather than flow.
      const int threshold = static_cast<int>(200.0 - sortAmt * 170.0);
      auto luma = [&](std::size_t o) {
        return (small[o + 2] * 77 + small[o + 1] * 151 + small[o + 0] * 28) >> 8;
      };
      std::vector<std::uint32_t> run;
      for (int y = 0; y < H; ++y) {
        const std::size_t rowOff = static_cast<std::size_t>(y) * W * 4;
        int x = 0;
        while (x < W) {
          if (static_cast<int>(luma(rowOff + static_cast<std::size_t>(x) * 4)) < threshold) {
            ++x; continue;
          }
          const int begin = x;
          while (x < W &&
                 static_cast<int>(luma(rowOff + static_cast<std::size_t>(x) * 4)) >= threshold) {
            ++x;
          }
          const int len = x - begin;
          if (len < 3) continue;
          run.clear();
          run.reserve(static_cast<std::size_t>(len));
          for (int i = 0; i < len; ++i) {
            std::uint32_t px = 0;
            std::memcpy(&px, small.data() + rowOff + static_cast<std::size_t>(begin + i) * 4, 4);
            run.push_back(px);
          }
          std::sort(run.begin(), run.end(), [](std::uint32_t a, std::uint32_t b) {
            const int la = ((a >> 16) & 0xFF) * 77 + ((a >> 8) & 0xFF) * 151 + (a & 0xFF) * 28;
            const int lb = ((b >> 16) & 0xFF) * 77 + ((b >> 8) & 0xFF) * 151 + (b & 0xFF) * 28;
            return la < lb;
          });
          for (int i = 0; i < len; ++i) {
            std::memcpy(small.data() + rowOff + static_cast<std::size_t>(begin + i) * 4,
                        &run[static_cast<std::size_t>(i)], 4);
          }
        }
      }
    }

    // BLOCK GLITCH. Displaced scanline bands plus RGB channel separation --
    // the corrupted-frame look. Bands are whole rows because that is how real
    // decode corruption presents: a block row loses sync and the remainder of
    // the line arrives shifted.
    const double glitchAmt = std::clamp(vs.glitch, 0.0, 1.0);
    if (glitchAmt > 0.01) {
      const int bands = 1 + static_cast<int>(glitchAmt * 14.0);
      std::vector<std::uint8_t> rowCopy(static_cast<std::size_t>(W) * 4);
      for (int b = 0; b < bands; ++b) {
        const int y0 = static_cast<int>(rnd() % static_cast<std::uint32_t>(std::max(1, H)));
        const int hgt = 1 + static_cast<int>(rnd() % static_cast<std::uint32_t>(
          std::max(1, static_cast<int>(H * glitchAmt / 8) + 1)));
        const int shift = static_cast<int>(rnd() % static_cast<std::uint32_t>(std::max(1, W / 3))) -
                          (W / 6);
        for (int y = y0; y < std::min(H, y0 + hgt); ++y) {
          std::uint8_t* row = small.data() + static_cast<std::size_t>(y) * W * 4;
          std::memcpy(rowCopy.data(), row, rowCopy.size());
          for (int x = 0; x < W; ++x) {
            // WRAPPED, not clamped: a shifted band that clamps smears its edge
            // pixel across the gap, which reads as a stretch. Wrapping reads
            // as torn, which is what corruption actually looks like.
            int sx = x - shift;
            sx = ((sx % W) + W) % W;
            std::memcpy(row + static_cast<std::size_t>(x) * 4,
                        rowCopy.data() + static_cast<std::size_t>(sx) * 4, 4);
          }
        }
      }
      // Channel separation. Red and blue pull apart horizontally, which is the
      // single most recognisable glitch cue and costs one pass.
      const int sep = static_cast<int>(glitchAmt * (W / 60.0)) + 1;
      if (sep > 0) {
        for (int y = 0; y < H; ++y) {
          std::uint8_t* row = small.data() + static_cast<std::size_t>(y) * W * 4;
          std::memcpy(rowCopy.data(), row, rowCopy.size());
          for (int x = 0; x < W; ++x) {
            const int xr = std::clamp(x + sep, 0, W - 1);
            const int xb = std::clamp(x - sep, 0, W - 1);
            row[static_cast<std::size_t>(x) * 4 + 2] =
              rowCopy[static_cast<std::size_t>(xr) * 4 + 2];   // R
            row[static_cast<std::size_t>(x) * 4 + 0] =
              rowCopy[static_cast<std::size_t>(xb) * 4 + 0];   // B
          }
        }
      }
    }
  }

  if (feedbackOn) {
    // The previous attempt at this swapped and then COPIED BACK, which is
    // exactly a copy with extra steps -- it saved nothing. The upscale below
    // still needs to read the frame just rendered, so the buffers are swapped
    // AFTER it, at the end of the function. Here we only record the geometry.
    vsynthPrevW_ = W;
    vsynthPrevH_ = H;
  } else if (!vsynthPrev_.empty()) {
    vsynthPrev_.clear();
    vsynthPrev_.shrink_to_fit();
    vsynthPrevW_ = vsynthPrevH_ = 0;
  }

  DecodedFrame frame;
  frame.width = outW;
  frame.height = outH;
  frame.format = FramePixelFormat::RGBA32;
  // Opaque BLACK, not white. The buffer was filled with 255 -- white, fully
  // opaque -- and in text mode the cell grid only covers outH/cellH whole
  // rows, so whatever remainder did not divide evenly stayed white. That was
  // the bright line along the bottom edge, and the same applied to any
  // leftover column on the right.
  frame.pixels.assign(static_cast<std::size_t>(outW) * outH * 4, 0);
  for (std::size_t i = 3; i < frame.pixels.size(); i += 4) frame.pixels[i] = 255;

  if (synthBench) benchGlitch = SynthClock::now();

  // ---- CRT ------------------------------------------------------------
  // Runs on the SMALL buffer, like every other stage.
  //
  // It used to run after the upscale, on the full output raster: a 33MB memcpy
  // of the frame plus a windowed bloom in doubles for every one of 8.3 million
  // pixels. MEASURED at 4K it cost 23ms of a 35ms frame -- two thirds of the
  // synth's entire budget, and the reason a 4K video synth could not hold 30fps.
  //
  // Nothing about the effect needed that resolution. Every quantity in it was
  // already expressed in BLOCKS, because a scanline is one source pixel tall
  // and the bloom radius spans about one source pixel: at output scale those
  // were being reconstructed by multiplying back up by the block size. Done
  // here they are simply 1, the picture is 100x smaller, and the nearest-
  // neighbour upscale carries the result out unchanged -- the blocks it
  // produces are exactly the blocks the effect was drawn in terms of.
  //
  // The one thing that genuinely lived at output scale was the gun
  // misalignment, a third of a block. At small scale that is a third of a
  // pixel, so it becomes a weighted blend with the neighbour instead of a
  // sample from it -- which is what a third-of-a-pixel offset means anyway,
  // and it now applies evenly rather than only near block edges.
  const double crtAmt = std::clamp(vs.crt, 0.0, 1.0);
  if (crtAmt > 0.01) {
    const int rowBytes = W * 4;
    const int radius = std::max(1, static_cast<int>(std::lround(0.4 + crtAmt * 0.9)));
    if (vsynthCrtSrc_.size() != small.size()) {
      vsynthCrtSrc_.resize(small.size());
    }
    std::memcpy(vsynthCrtSrc_.data(), small.data(), small.size());
    const std::vector<std::uint8_t>& src = vsynthCrtSrc_;
    // A third of a pixel of separation, applied as a blend.
    const double fringeMix = crtAmt * 0.35 / 3.0;

    auto crtRows = [&](int y0, int y1) {
      int sum[3] = {0, 0, 0};
      for (int y = y0; y < y1; ++y) {
        const std::uint8_t* s = src.data() + static_cast<std::size_t>(y) * rowBytes;
        std::uint8_t* dst = small.data() + static_cast<std::size_t>(y) * rowBytes;

        // One dark line per source row: after the upscale that is one dark
        // block row, which is what a scanline looks like.
        const double lineGain = (y & 1) ? (1.0 - crtAmt * 0.5) : 1.0;

        sum[0] = sum[1] = sum[2] = 0;
        for (int k = -radius; k <= radius; ++k) {
          const int sx = std::clamp(k, 0, W - 1);
          const std::size_t o = static_cast<std::size_t>(sx) * 4;
          sum[0] += s[o + 0]; sum[1] += s[o + 1]; sum[2] += s[o + 2];
        }
        const int window = radius * 2 + 1;

        for (int x = 0; x < W; ++x) {
          const std::size_t o = static_cast<std::size_t>(x) * 4;
          // Bloom ADDS, so edges stay sharp and only the light spills.
          const double bloom = crtAmt * 0.9;
          double ob = s[o + 0] + (sum[0] / static_cast<double>(window)) * bloom;
          double og = s[o + 1] + (sum[1] / static_cast<double>(window)) * bloom;
          double orr = s[o + 2] + (sum[2] / static_cast<double>(window)) * bloom;

          const int xr = std::min(x + 1, W - 1);
          const int xb = std::max(x - 1, 0);
          orr = orr * (1.0 - fringeMix) +
                s[static_cast<std::size_t>(xr) * 4 + 2] * fringeMix;
          ob = ob * (1.0 - fringeMix) +
               s[static_cast<std::size_t>(xb) * 4 + 0] * fringeMix;

          dst[o + 0] = static_cast<std::uint8_t>(std::clamp(ob * lineGain, 0.0, 255.0));
          dst[o + 1] = static_cast<std::uint8_t>(std::clamp(og * lineGain, 0.0, 255.0));
          dst[o + 2] = static_cast<std::uint8_t>(std::clamp(orr * lineGain, 0.0, 255.0));

          const int addX = std::min(x + radius + 1, W - 1);
          const int dropX = std::max(x - radius, 0);
          const std::size_t oa = static_cast<std::size_t>(addX) * 4;
          const std::size_t od = static_cast<std::size_t>(dropX) * 4;
          sum[0] += s[oa + 0] - s[od + 0];
          sum[1] += s[oa + 1] - s[od + 1];
          sum[2] += s[oa + 2] - s[od + 2];
        }
      }
    };
    // Small enough now that spawning a thread pool would cost more than the
    // work; the whole pass is a fraction of a millisecond.
    crtRows(0, H);
  }

  if (synthBench) benchCrt = SynthClock::now();
  if (vs.ascii) {
    // Text-mode render. Cells are drawn at OUTPUT resolution so the glyph
    // edges stay crisp -- rendering them small and scaling up would blur the
    // one thing the look depends on.
    const int cols = std::clamp(vs.asciiCols, 20, 200);
    const int cellW = std::max(3, outW / cols);
    const int cellH = std::max(4, (cellW * 7) / 5);   // 5x7 glyph aspect
    const int rows = std::max(1, outH / cellH);
    std::uint32_t cseed = static_cast<std::uint32_t>(displayFrameSerial_ * 2246822519u) | 1u;
    auto crnd = [&cseed]() {
      cseed ^= cseed << 13; cseed ^= cseed >> 17; cseed ^= cseed << 5;
      return cseed;
    };
    const double corrupt = std::clamp(vs.glitch, 0.0, 1.0);
    // Sheet mode only engages if the sheet actually loaded. A missing file
    // falls back to blocks rather than drawing nothing, because an empty
    // screen gives the operator no clue which of the two went wrong.
    // Uses the sheet only if it is ALREADY loaded. This used to call
    // ensureSpriteSheet from here, which on a folder spawns two subprocesses
    // per file -- two hundred of them for a hundred sprites -- synchronously,
    // in the middle of drawing a frame. The app froze hard.
    //
    // Loading now happens when the operator CHOOSES a set, which is a moment
    // that can afford to take a second, and this only ever reads the result.
    const bool useSprites = vs.asciiCharSet == 5 &&
      spriteSheetLoaded_ == vs.spriteSheetPath &&
      !spriteTilesByLuma_.empty() && spriteSheetCols_ > 0;

    // Threaded by cell row. Text mode writes the ENTIRE output raster one
    // cell at a time, which at 1080p is two million pixels on a single thread
    // -- the largest remaining cost once the CRT was fixed.
    auto renderCellRows = [&](int rowBegin, int rowEnd) {
    for (int cy = rowBegin; cy < rowEnd; ++cy) {
      // ROW LOCK. A corrupted text screen does not scatter random cells -- a
      // row loses sync and repeats ONE character across its whole width, in
      // one colour pair. That banding is the signature of the reference
      // images, and scattering instead just looks like noise.
      const bool rowLocked = corrupt > 0.05 &&
        (crnd() % 100u) < static_cast<std::uint32_t>(corrupt * 35.0);
      const int lockGlyph = static_cast<int>(crnd() % static_cast<std::uint32_t>(kCellJunkCount));
      const int lockInk = static_cast<int>(crnd() % 16u);
      const int lockBg = static_cast<int>(crnd() % 16u);

      for (int cx = 0; cx < cols; ++cx) {
        // Average the source cell rather than point-sampling: a single pixel
        // from a dithered pattern reports whichever phase it landed on and the
        // grid flickers.
        int sr = 0, sg = 0, sb = 0, count = 0;
        const int sx0 = (cx * W) / cols;
        const int sx1 = std::max(sx0 + 1, ((cx + 1) * W) / cols);
        const int sy0 = (cy * H) / rows;
        const int sy1 = std::max(sy0 + 1, ((cy + 1) * H) / rows);
        for (int sy = sy0; sy < std::min(sy1, H); ++sy) {
          for (int sx = sx0; sx < std::min(sx1, W); ++sx) {
            const std::size_t o = (static_cast<std::size_t>(sy) * W + sx) * 4;
            sb += small[o + 0]; sg += small[o + 1]; sr += small[o + 2];
            ++count;
          }
        }
        if (count == 0) count = 1;
        sr /= count; sg /= count; sb /= count;

        const int luma = (sr * 77 + sg * 151 + sb * 28) >> 8;
        int setCount = 0;
        const std::uint8_t (*set)[7] = cellSetFor(vs.asciiCharSet, setCount);
        // Mixed draws from both the density ramp and the structural glyphs,
        // chosen by position rather than at random -- random per frame would
        // make every cell flicker between alphabets.
        if (vs.asciiCharSet == 3) {
          if (((cx * 7 + cy * 13) & 3) == 0) { set = kCellJunk; setCount = kCellJunkCount; }
          else { set = kCellRamp; setCount = kCellRampCount; }
        }
        int rampIndex = (luma * (setCount - 1)) / 255;
        rampIndex = std::clamp(rampIndex, 0, setCount - 1);
        // Set 1 walks the ASCII font in DENSITY order so brightness maps to
        // ink coverage and the picture still reads. Set 4 walks it in raw
        // ASCII order instead: the mapping is then arbitrary, which is the
        // point -- it looks like text rather than like a gradient.
        if (vs.asciiCharSet == 1) {
          rampIndex = asciiRampOrder()[static_cast<std::size_t>(rampIndex)];
        }
        if (vs.asciiShuffle > 0) {
          // A fixed permutation from the seed: same glyphs, different
          // handwriting. Deterministic, so the look is repeatable.
          std::uint32_t h = static_cast<std::uint32_t>(rampIndex * 2654435761u +
                                                       vs.asciiShuffle * 40503u);
          h ^= h >> 13; h *= 2246822519u; h ^= h >> 16;
          rampIndex = static_cast<int>(h % static_cast<std::uint32_t>(setCount));
        }
        const std::uint8_t* glyph = set[rampIndex];

        // Ink. The old boolean offered green or picture-colour and nothing
        // else; these are the phosphors people actually mean, plus a mode that
        // locks the text to the selected hardware palette.
        int inkIdx;
        switch (vs.asciiInk) {
          case 1:  inkIdx = 10; break;   // green
          case 2:  inkIdx = 14; break;   // amber
          case 3:  inkIdx = 11; break;   // cyan
          case 4:  inkIdx = 15; break;   // white
          case 5: {
            // Palette-locked: quantise the cell's own colour into the palette
            // already chosen for the picture, so text mode and pixel mode
            // agree about what machine this is.
            double pr = 0, pg = 0, pb = 0;
            const double u = luma / 255.0;
            switch (vs.palette) {
              case VideoSynthPalette::C64:     samplePalette(kPalC64, 16, u, pr, pg, pb); break;
              case VideoSynthPalette::Gameboy: samplePalette(kPalGameboy, 4, u, pr, pg, pb); break;
              case VideoSynthPalette::Cga:     samplePalette(kPalCga, 4, u, pr, pg, pb); break;
              case VideoSynthPalette::Nes:     samplePalette(kPalNes, 12, u, pr, pg, pb); break;
              case VideoSynthPalette::Vapor:   samplePalette(kPalVapor, 8, u, pr, pg, pb); break;
              default: pr = sr / 255.0; pg = sg / 255.0; pb = sb / 255.0; break;
            }
            inkIdx = nearestPaletteIndex(static_cast<int>(pr * 255),
                                         static_cast<int>(pg * 255),
                                         static_cast<int>(pb * 255));
            break;
          }
          default: inkIdx = nearestPaletteIndex(sr, sg, sb); break;
        }
        const bool monoInk = vs.asciiInk >= 1 && vs.asciiInk <= 4;
        int bgIdx = 0;

        if (rowLocked) {
          glyph = kCellJunk[lockGlyph];
          // A mono phosphor screen corrupts by BRIGHTNESS, not by hue -- a
          // green terminal cannot suddenly show magenta. Colour-pair
          // corruption belongs to the palette modes.
          if (!monoInk) { inkIdx = lockInk; bgIdx = lockBg; }
        } else if (corrupt > 0.05 &&
                   (crnd() % 1000u) < static_cast<std::uint32_t>(corrupt * 90.0)) {
          // Scattered single-cell corruption on top of the row banding.
          glyph = kCellJunk[crnd() % static_cast<std::uint32_t>(kCellJunkCount)];
          if (!monoInk) inkIdx = static_cast<int>(crnd() % 16u);
        }

        // Cell origin, needed by BOTH the sprite branch below and the glyph
        // path after it, so it is computed once here.
        const int px0 = cx * cellW;
        const int py0 = cy * cellH;

        if (useSprites) {
          // Draw a TILE instead of a glyph, chosen by the same brightness the
          // glyph ramp uses, so the picture still reads through the sprites.
          const std::size_t n = spriteTilesByLuma_.size();
          std::size_t pick = static_cast<std::size_t>(luma) * n / 256u;
          if (pick >= n) pick = n - 1;
          if (rowLocked) pick = static_cast<std::size_t>(lockGlyph) % n;
          // CHAOS mixes a position-hashed random pick against the
          // brightness-ranked one. Hashed rather than time-random: a tile that
          // reshuffles every frame is a flicker, not a texture.
          if (vs.spriteChaos > 0.001) {
            std::uint32_t h = static_cast<std::uint32_t>(cx * 73856093u ^ cy * 19349663u);
            h ^= h >> 13; h *= 2246822519u; h ^= h >> 16;
            const double mix = (h & 0xFFFFu) / 65535.0;
            if (mix < vs.spriteChaos) pick = (h >> 16) % n;
          }
          const int tile = spriteTilesByLuma_[pick].second;
          const int tx0 = (tile % spriteSheetCols_) * spriteSheetTileW_;
          const int ty0 = (tile / spriteSheetCols_) * spriteSheetTileH_;

          // Per-cell transform. Quarter turns are index swaps rather than real
          // rotation, so they stay pixel-exact; only free rotation samples at
          // an angle, and it is opt-in for that reason.
          int quarter = 0;
          double freeRad = 0.0;
          switch (vs.spriteRotate) {
            case 1: quarter = 1; break;
            case 2: quarter = 2; break;
            case 3: quarter = 3; break;
            case 4: quarter = (luma * 4) / 256; break;   // by brightness
            case 5: freeRad = t * vs.spriteFreeAngle * 0.017453292519943295; break;
            default: break;
          }
          bool flipX = vs.spriteFlip == 1;
          bool flipY = vs.spriteFlip == 2;
          if (vs.spriteFlip == 3) {
            // Alternating by cell position: a checkerboard of mirrored tiles,
            // which reads as deliberate pattern where random flipping reads as
            // noise.
            flipX = ((cx + cy) & 1) != 0;
            flipY = ((cx ^ cy) & 2) != 0;
          }
          // Jitter shrinks a cell's tile, hashed by position so it holds still.
          double jitter = 1.0;
          if (vs.spriteJitter > 0.001) {
            std::uint32_t jh = static_cast<std::uint32_t>(cx * 2654435761u ^ cy * 40503u);
            jh ^= jh >> 15;
            jitter = 1.0 - vs.spriteJitter * ((jh & 0xFFFFu) / 65535.0) * 0.6;
          }
          // Nothing to transform: take the integer path. The transform below
          // does two divisions per PIXEL plus trig when spinning, which at
          // 1080p is millions of floating-point operations a frame -- and the
          // default is no transform at all, so paying for it unconditionally
          // was a straight regression.
          const bool plain = (quarter == 0) && (freeRad == 0.0) &&
                             !flipX && !flipY && (jitter > 0.999);
          if (plain) {
            for (int ry = 0; ry < cellH; ++ry) {
              if (py0 + ry >= outH) break;
              const int sy = ty0 + (ry * spriteSheetTileH_) / cellH;
              std::uint8_t* row = frame.pixels.data() +
                (static_cast<std::size_t>(py0 + ry) * outW) * 4;
              for (int rx = 0; rx < cellW; ++rx) {
                if (px0 + rx >= outW) break;
                const int sx = tx0 + (rx * spriteSheetTileW_) / cellW;
                const std::size_t so =
                  (static_cast<std::size_t>(sy) * spriteSheetW_ + sx) * 4;
                std::uint8_t* p = row + static_cast<std::size_t>(px0 + rx) * 4;
                if (so + 3 >= spriteSheetRgba_.size() ||
                    spriteSheetRgba_[so + 3] < 32) {
                  p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255;
                  continue;
                }
                p[0] = spriteSheetRgba_[so + 0];
                p[1] = spriteSheetRgba_[so + 1];
                p[2] = spriteSheetRgba_[so + 2];
                p[3] = 255;
              }
            }
            continue;
          }

          for (int ry = 0; ry < cellH; ++ry) {
            if (py0 + ry >= outH) break;
            // Cell-local coordinates, centred, so every transform is about
            // the tile's middle rather than its corner.
            double lx = (static_cast<double>(ry) / cellH) - 0.5;
            std::uint8_t* row = frame.pixels.data() +
              (static_cast<std::size_t>(py0 + ry) * outW) * 4;
            for (int rx = 0; rx < cellW; ++rx) {
              if (px0 + rx >= outW) break;
              double ux = (static_cast<double>(rx) / cellW) - 0.5;
              double uy = lx;
              if (jitter < 0.999) { ux /= jitter; uy /= jitter; }
              if (freeRad != 0.0) {
                const double cs = std::cos(freeRad), sn = std::sin(freeRad);
                const double rxr = ux * cs - uy * sn;
                uy = ux * sn + uy * cs;
                ux = rxr;
              }
              for (int q = 0; q < quarter; ++q) {
                const double tmp = ux;
                ux = -uy;
                uy = tmp;
              }
              if (flipX) ux = -ux;
              if (flipY) uy = -uy;
              // Outside the tile after transforming: leave it transparent
              // rather than clamping, which would smear the edge pixel into a
              // streak across the cell.
              if (ux < -0.5 || ux >= 0.5 || uy < -0.5 || uy >= 0.5) {
                std::uint8_t* pc = row + static_cast<std::size_t>(px0 + rx) * 4;
                pc[0] = 0; pc[1] = 0; pc[2] = 0; pc[3] = 255;
                continue;
              }
              const int sx = tx0 + static_cast<int>((ux + 0.5) * spriteSheetTileW_);
              const int sy = ty0 + static_cast<int>((uy + 0.5) * spriteSheetTileH_);
              const std::size_t so =
                (static_cast<std::size_t>(sy) * spriteSheetW_ + sx) * 4;
              if (so + 3 >= spriteSheetRgba_.size()) continue;
              const int a = spriteSheetRgba_[so + 3];
              std::uint8_t* p = row + static_cast<std::size_t>(px0 + rx) * 4;
              if (a < 32) { p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 255; continue; }
              p[0] = spriteSheetRgba_[so + 0];
              p[1] = spriteSheetRgba_[so + 1];
              p[2] = spriteSheetRgba_[so + 2];
              p[3] = 255;
            }
          }
          continue;
        }


        const std::uint8_t* ink = kCellPalette[inkIdx];
        const std::uint8_t* bg = kCellPalette[bgIdx];

        // A 5x7 glyph in a cell many pixels tall means each glyph row repeats
        // over several output rows. Draw it once and memcpy the repeats: the
        // per-pixel path ran for the entire output raster, which in text mode
        // was the dominant cost.
        int prevGy = -1;
        const std::uint8_t* prevRowStart = nullptr;
        const int spanW = std::min(cellW, outW - px0);
        if (spanW <= 0) continue;
        for (int ry = 0; ry < cellH; ++ry) {
          if (py0 + ry >= outH) break;
          std::uint8_t* row = frame.pixels.data() +
            (static_cast<std::size_t>(py0 + ry) * outW) * 4;
          std::uint8_t* cellStart = row + static_cast<std::size_t>(px0) * 4;
          const int gy = std::min(6, (ry * 7) / cellH);
          if (gy == prevGy && prevRowStart) {
            std::memcpy(cellStart, prevRowStart, static_cast<std::size_t>(spanW) * 4);
            continue;
          }
          for (int rx = 0; rx < spanW; ++rx) {
            const int gx = std::min(4, (rx * 5) / cellW);
            const bool on = ((glyph[gy] >> (4 - gx)) & 1) != 0;
            const std::uint8_t* c = on ? ink : bg;
            std::uint8_t* p = cellStart + static_cast<std::size_t>(rx) * 4;
            // RGBA32: byte 0 is RED. This wrote c[2] -- blue -- into it, so
            // every palette colour came out with red and blue exchanged.
            // Amber rendered as cyan and cyan as amber, which is how it
            // was spotted. fillTimerRect writes r,g,b in order and its
            // digits were always correct, which is the reference.
            p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = 255;
          }
          prevGy = gy;
          prevRowStart = cellStart;
        }
      }
    }
    };   // renderCellRows

    unsigned chw = std::thread::hardware_concurrency();
    if (chw == 0) chw = 1;
    const int cworkers = std::max(1, std::min<int>(static_cast<int>(chw),
                                                   std::max(1, rows / 4)));
    if (cworkers <= 1) {
      renderCellRows(0, rows);
    } else {
      std::vector<std::thread> cpool;
      cpool.reserve(static_cast<std::size_t>(cworkers - 1));
      const int cspan = (rows + cworkers - 1) / cworkers;
      for (int w = 1; w < cworkers; ++w) {
        const int b = std::min(rows, w * cspan);
        const int e = std::min(rows, b + cspan);
        if (b >= e) break;
        cpool.emplace_back(renderCellRows, b, e);
      }
      renderCellRows(0, std::min(rows, cspan));
      for (std::thread& t : cpool) t.join();
    }
  } else {
    // Nearest-neighbour upscale. Deliberately not interpolated: soft edges
    // would undo the posterisation and the hard shapes the look depends on.
    //
    // The x mapping is the same for every row, so it is computed ONCE rather
    // than per pixel, and consecutive output rows that map to the same source
    // row are a straight memcpy of the row above. At the default resolution
    // each internal row covers eight output rows, so seven in eight rows cost
    // one memcpy instead of 1920 four-byte copies.
    std::vector<int> xmap(static_cast<std::size_t>(outW));
    for (int x = 0; x < outW; ++x) xmap[static_cast<std::size_t>(x)] = ((x * W) / outW) * 4;

    int lastSrcRow = -1;
    const std::uint8_t* lastDstRow = nullptr;
    for (int y = 0; y < outH; ++y) {
      const int sy2 = (y * H) / outH;
      std::uint8_t* dstRow = frame.pixels.data() + static_cast<std::size_t>(y) * outW * 4;
      if (sy2 == lastSrcRow && lastDstRow) {
        std::memcpy(dstRow, lastDstRow, static_cast<std::size_t>(outW) * 4);
        continue;
      }
      const std::uint8_t* srcRow = small.data() + static_cast<std::size_t>(sy2) * W * 4;
      for (int x = 0; x < outW; ++x) {
        std::memcpy(dstRow + static_cast<std::size_t>(x) * 4,
                    srcRow + xmap[static_cast<std::size_t>(x)], 4);
      }
      lastSrcRow = sy2;
      lastDstRow = dstRow;
    }
  }

  if (synthBench) benchScale = SynthClock::now();

  // NOW the buffers can trade places: everything that needed to read this
  // frame has read it. Next frame renders into what is currently `prev` and
  // reads what is currently `small`, so neither is ever copied.
  if (feedbackOn) {
    vsynthSmall_.swap(vsynthPrev_);
  }

  if (synthBench) {
    const auto end = SynthClock::now();
    auto ms = [](auto a, auto b) {
      return std::chrono::duration<double, std::milli>(b - a).count();
    };
    static int n = 0;
    static double sSmall = 0, sGlitch = 0, sScale = 0, sCrt = 0, sTotal = 0;
    sSmall  += ms(benchT0, benchSmall);
    sGlitch += ms(benchSmall, benchGlitch);
    sCrt    += ms(benchGlitch, benchCrt);
    sScale  += ms(benchCrt, benchScale);
    sTotal  += ms(benchT0, end);
    if (++n == 20) {
      std::cerr << "synth-bench small=" << (sSmall / n) << " glitch="
                << (sGlitch / n) << " upscale=" << (sScale / n) << " crt="
                << (sCrt / n) << " total=" << (sTotal / n) << "ms  internal="
                << W << "x" << H << " out=" << outW << "x" << outH << std::endl;
      n = 0; sSmall = sGlitch = sScale = sCrt = sTotal = 0;
    }
  }

  displayFrame_ = std::move(frame);
  displayFrame_->index = ++displayFrameSerial_;
  uploadFrame(*displayFrame_);
}

void MediaEngine::rebuildPatternFrame(const Cue& cue, double wallSeconds) {
  // Animated patterns rebuild at the SELECTED DISPLAY's refresh rate (or
  // the project's explicit refresh override — the app's mode provider
  // decides), never the render-loop rate (240 Hz floor): a full-raster CPU
  // rebuild + texture upload per loop tick pegs a core and lags the app.
  // Terrarium is slower still: it only changes on its 9 TPS sim tick.
  const std::string terraBase = stripPatternMotionSuffix(normalizePatternTypeId(cue.path));
  const bool isTerrarium = terraBase == "terrarium" || terraBase == "terrarium-pico";
  double refreshHz = 60.0;
  if (outputSizeProvider_) {
    OutputModeHint mode = outputSizeProvider_();
    if (mode.refreshHz >= 1.0) {
      refreshHz = std::clamp(mode.refreshHz, 23.0, 240.0);
    }
  }
  const double minInterval = isTerrarium ? (1.0 / 9.0) : (1.0 / refreshHz);
  if (displayFrame_ && wallSeconds - lastPatternRebuildSeconds_ < minInterval * 0.98) {
    return;
  }
  lastPatternRebuildSeconds_ = wallSeconds;
  auto [fallbackW, fallbackH] = currentOutputSizeHint();
  // Build IN PLACE into the frame we already hold. Constructing a fresh
  // DecodedFrame here meant a new full-raster allocation every single frame —
  // 33 MB at 4K, ~2 GB/s of churn, which exhausted the system commit limit and
  // dragged the whole machine down. Reusing the buffer makes the steady state
  // allocation-free; only a raster change resizes it.
  if (!displayFrame_) {
    displayFrame_.emplace();
  }
  buildPatternFrameInto(*displayFrame_, cue, wallSeconds, fallbackW, fallbackH);
  if (!displayFrame_->pixels.empty()) {
    displayFrame_->index = ++displayFrameSerial_;
    uploadFrame(*displayFrame_);
  }
}

// ---------------------------------------------------------------------------
// startBrowserCapture — Begin screen-capture for browser cue rendering.
//
// This is the "capture a window" approach to browser cues: spawns ffmpeg to
// capture the browser window's pixels and pipe them as raw RGBA frames.
// Used on Linux/macOS where CEF integration captures the browser's X11/Cocoa
// window directly. On Windows, browser cues use pushBrowserFrame() instead.
//
// The displayId is the X11 display identifier (e.g. ":0.0") or window handle.
// fadeIn/fadeOut params are reserved for future use (currently unused).
// ---------------------------------------------------------------------------
bool MediaEngine::startBrowserCapture(const std::string& displayId, int w, int h,
                                       double /*fadeInSeconds*/, double /*fadeOutSeconds*/,
                                       double transSecs, TransitionStyle transStyle) {
  stopDecoderThreads();
  isBrowserCapturing_ = false;
  frameRate_ = 30.0;
  duration_ = 0.0;
  browserCaptureW_ = w;
  browserCaptureH_ = h;
  if (transSecs > 0.0 && texture_) {
    beginTransition(transSecs, transStyle);
  }

  // Build capture request targeting the browser window
  deckboy::platform::SourceCaptureRequest request;
  request.kind = deckboy::platform::SourceCaptureKind::Window;
  request.sourceRef = trim(displayId);
  // X11 display IDs need a screen suffix (e.g. ":0" → ":0.0")
  if (!request.sourceRef.empty() && request.sourceRef.front() == ':' &&
      request.sourceRef.find('.') == std::string::npos) {
    request.sourceRef += ".0";
  }
  request.width = w;
  request.height = h;
  request.frameRate = 30;
  request.drawMouse = false;  // don't capture cursor over the browser window
  auto plan = deckboy::platform::planSourceCapture(request);
  if (!plan.supported || plan.ffmpegArgs.empty()) {
    return false;
  }
  if (!spawnPipeProcess(videoProcess_, plan.ffmpegArgs)) {
    return false;
  }

  // Spawn decode thread — identical pattern to startDecoderThreads video thread
  const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
  int videoFd = videoProcess_.readFd;
  playbackClockStart_ = std::chrono::steady_clock::now();
  playbackStartPosition_ = 0.0;
  state_ = TransportState::Playing;
  isBrowserCapturing_ = true;

  videoThread_ = std::thread([this, w, h, frameBytes, videoFd]() {
    std::uint64_t frameIdx = 0;
    while (!decoderStop_.load()) {
      // Back-pressure: wait for room in the frame queue
      while (!decoderStop_.load()) {
        bool hasRoom = false;
        { std::lock_guard<std::mutex> lk(frameMutex_); hasRoom = frameQueue_.size() < kMaxVideoFrames; }
        if (hasRoom) break;
        SDL_Delay(4);
      }
      if (decoderStop_.load()) break;
      DecodedFrame frame;
      frame.width = w;
      frame.height = h;
      frame.index = frameIdx++;
      frame.pixels.resize(frameBytes);
      if (!readExact(videoFd, frame.pixels.data(), frameBytes)) {
        decoderEof_ = true;
        break;
      }
      std::lock_guard<std::mutex> lk(frameMutex_);
      frameQueue_.push_back(std::move(frame));
    }
    decoderEof_ = true;
  });
  return true;
}

// Stop browser capture: clear the flag and shut down decode threads.
void MediaEngine::stopBrowserCapture() {
  isBrowserCapturing_ = false;
  stopDecoderThreads();
}

// ---------------------------------------------------------------------------
// startBrowserFrameMode — Prepare for push-based browser frame delivery.
//
// Unlike startBrowserCapture (which spawns ffmpeg to capture a window), this
// mode receives frames directly from the browser backend via pushBrowserFrame().
// Used when the browser backend renders offscreen and delivers RGBA buffers.
//
// Preserves the still duration from activeCue so that timed browser cues
// (with stillDurationSeconds) correctly fade out and auto-advance. The
// playback clock is reset so fade-in timing starts from when frames arrive.
// ---------------------------------------------------------------------------
bool MediaEngine::startBrowserFrameMode(int w, int h, double transSecs, TransitionStyle transStyle) {
  stopDecoderThreads();
  isBrowserCapturing_ = false;
  frameRate_ = 30.0;
  // Preserve still duration from activeCue so fade-out and auto-advance work correctly.
  // loadCue already called initStillTimer which set duration_ = cue.stillDurationSeconds.
  // The clock is reset below so fade-in and duration countdown are relative to when the
  // first browser frame arrives (not when the cue was taken).
  duration_ = (activeCue_ && activeCue_->stillDurationSeconds > 0.0)
              ? activeCue_->stillDurationSeconds : 0.0;
  browserCaptureW_ = w;
  browserCaptureH_ = h;
  browserFrameIdx_ = 0;
  if (transSecs > 0.0 && texture_) {
    beginTransition(transSecs, transStyle);
  }
  playbackClockStart_ = std::chrono::steady_clock::now();
  playbackStartPosition_ = 0.0;
  state_ = TransportState::Playing;
  isBrowserCapturing_ = true;
  return true;
}

// Receive a single RGBA frame from the browser backend and queue it for display.
// Called from the browser backend thread — must be thread-safe via frameMutex_.
// Keeps at most 2 frames buffered (drops stale ones) to minimize latency.
void MediaEngine::pushBrowserFrame(const uint8_t* rgba, int w, int h) {
  if (!isBrowserCapturing_ || !rgba || w <= 0 || h <= 0) return;
  DecodedFrame frame;
  frame.width  = w;
  frame.height = h;
  frame.index  = browserFrameIdx_++;
  frame.pixels.assign(rgba, rgba + static_cast<size_t>(w) * h * 4);
  std::lock_guard<std::mutex> lk(frameMutex_);
  // Discard stale frames — keep at most 2 buffered to bound latency
  while (frameQueue_.size() >= 2) frameQueue_.pop_front();
  frameQueue_.push_back(std::move(frame));
}

// ---------------------------------------------------------------------------
// startSourceCapture — Begin live capture from a camera, window, or app texture.
//
// Spawns ffmpeg with platform-specific capture args (built by buildSourceCaptureArgs)
// and starts a decode thread to pipe raw RGBA frames into frameQueue_. The
// capture runs at 30fps with dimensions clamped to 160–3840 x 90–2160.
//
// Not implemented on Windows (returns false) — Windows source capture would
// need DXGI Desktop Duplication or DirectShow, which isn't wired up yet.
// ---------------------------------------------------------------------------
bool MediaEngine::startSourceCapture(const Cue& cue) {
  if (!isSourceCueKind(cue.kind)) {
    return false;
  }

  auto [fallbackW, fallbackH] = currentOutputSizeHint();
  int w = cue.width > 0 ? cue.width : fallbackW;
  int h = cue.height > 0 ? cue.height : fallbackH;
  w = std::clamp(w, 160, 3840);
  h = std::clamp(h, 90, 2160);

  std::vector<std::string> args;
  if (!buildSourceCaptureArgs(cue, w, h, args)) {
    return false;
  }

  stopDecoderThreads();
  isSourceCapturing_ = false;

  if (!spawnPipeProcess(videoProcess_, args)) {
    return false;
  }

  const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
  int videoFd = videoProcess_.readFd;
  frameRate_ = 30.0;
  duration_ = 0.0;
  playbackClockStart_ = std::chrono::steady_clock::now();
  playbackStartPosition_ = 0.0;
  pausedPosition_ = 0.0;
  currentPosition_ = 0.0;
  state_ = TransportState::Playing;
  isSourceCapturing_ = true;

  videoThread_ = std::thread([this, w, h, frameBytes, videoFd]() {
    std::uint64_t frameIdx = 0;
    while (!decoderStop_.load()) {
      while (!decoderStop_.load()) {
        bool hasRoom = false;
        { std::lock_guard<std::mutex> lk(frameMutex_); hasRoom = frameQueue_.size() < kMaxVideoFrames; }
        if (hasRoom) break;
        SDL_Delay(4);
      }
      if (decoderStop_.load()) break;

      DecodedFrame frame;
      frame.width = w;
      frame.height = h;
      frame.index = frameIdx++;
      frame.pixels.resize(frameBytes);
      if (!readExact(videoFd, frame.pixels.data(), frameBytes)) {
        decoderEof_ = true;
        break;
      }

      std::lock_guard<std::mutex> lk(frameMutex_);
      frameQueue_.push_back(std::move(frame));
    }
    decoderEof_ = true;
  });

  return true;
}

// ── Private methods ──────────────────────────────────────────────────────────

// Compute the visual fade gain (0–1) at a given playback position.
// Handles both fade-in (ramp from 0→1 over fadeInSeconds) and fade-out
// (ramp from 1→0 over fadeOutSeconds before the cue ends).
// The suppress flags allow disabling either fade independently:
//   - suppressFadeInForCurrentCue_: set when cue is loaded mid-transition
//   - suppressVisualFadeOutForCurrentCue_: set for auto-advancing cues
//     (the deck-level crossfade handles the visual transition instead)
//
// NOTE: visualFadeGainAt and fadeGainAt currently have identical logic.
// They were historically separate (visual vs audio fade paths) but converged.
// Kept as two entry points for API clarity — callers read differently.
double MediaEngine::visualFadeGainAt(double positionSeconds) const {
  if (!activeCue_) {
    return 1.0;  // no cue → fully visible (no fade)
  }
  double gain = 1.0;
  // Fade-in: linear ramp from 0 to 1 over fadeInSeconds
  if (!suppressFadeInForCurrentCue_ && activeCue_->fadeInSeconds > 0.001) {
    gain = std::min(gain, std::clamp(positionSeconds / activeCue_->fadeInSeconds, 0.0, 1.0));
  }
  // Fade-out: linear ramp from 1 to 0 over fadeOutSeconds before the end.
  // Honored whenever the operator configured a fade-out, including hold/
  // pause-on-last stills — if you set a fade-out you want to see it. Stills
  // default to fadeOutSeconds == 0 (see applyDeckDefaultsToCue) so the common
  // case holds cleanly; this branch only engages once it's turned on.
  if (!suppressVisualFadeOutForCurrentCue_ && activeCue_->fadeOutSeconds > 0.001 && duration_ > 0.0) {
    double remaining = std::max(0.0, duration_ - positionSeconds);
    gain = std::min(gain, std::clamp(remaining / activeCue_->fadeOutSeconds, 0.0, 1.0));
  }
  return std::clamp(gain, 0.0, 1.0);
}

// Compute the raw fade gain (used for audio fade and other non-visual purposes).
// Currently identical to visualFadeGainAt — see note above.
double MediaEngine::fadeGainAt(double positionSeconds) const {
  if (!activeCue_) {
    return 1.0;
  }
  double gain = 1.0;
  if (!suppressFadeInForCurrentCue_ && activeCue_->fadeInSeconds > 0.001) {
    gain = std::min(gain, std::clamp(positionSeconds / activeCue_->fadeInSeconds, 0.0, 1.0));
  }
  if (!suppressVisualFadeOutForCurrentCue_ &&
      activeCue_->fadeOutSeconds > 0.001 && duration_ > 0.0) {
    double remaining = std::max(0.0, duration_ - positionSeconds);
    gain = std::min(gain, std::clamp(remaining / activeCue_->fadeOutSeconds, 0.0, 1.0));
  }
  return std::clamp(gain, 0.0, 1.0);
}

// Publish fade parameters to the atomic mirrors the audio thread reads.
// Main thread only. Relaxed ordering is fine: each field is independently
// atomic and a one-buffer lag on a fade edit is inaudible.
void MediaEngine::syncAudioFadeParams() {
  const Cue* cue = activeCue_;
  // Audio fades follow the visual fades unless the cue overrides them
  // (audioFade*Seconds >= 0 — 0 means "no audio fade", >0 explicit length).
  double fadeIn = 0.0;
  double fadeOut = 0.0;
  if (cue) {
    fadeIn = cue->audioFadeInSeconds >= 0.0f
      ? static_cast<double>(cue->audioFadeInSeconds) : cue->fadeInSeconds;
    fadeOut = cue->audioFadeOutSeconds >= 0.0f
      ? static_cast<double>(cue->audioFadeOutSeconds) : cue->fadeOutSeconds;
  }
  audioFadeInSeconds_.store(fadeIn, std::memory_order_relaxed);
  audioFadeOutSeconds_.store(fadeOut, std::memory_order_relaxed);
  audioFadeDuration_.store(cue ? duration_ : 0.0, std::memory_order_relaxed);
  audioSuppressFadeIn_.store(suppressFadeInForCurrentCue_, std::memory_order_relaxed);
  audioSuppressFadeOut_.store(suppressVisualFadeOutForCurrentCue_, std::memory_order_relaxed);
  // Per-cue audio trim/pan/mono ride the same mirrors — edits apply live
  // (snapshot sync → next tick) with no decode restart.
  audioCueGain_.store(
    cue ? std::pow(10.0, std::clamp(cue->audioGainDb, kCueAudioGainMinDb, kCueAudioGainMaxDb) / 20.0) : 1.0,
    std::memory_order_relaxed);
  audioCuePan_.store(cue ? std::clamp(cue->audioPan, -1.0f, 1.0f) : 0.0f,
                     std::memory_order_relaxed);
  audioCueMono_.store(cue != nullptr && cue->audioMono, std::memory_order_relaxed);
  audioCuePairOffset_.store(cue ? std::clamp(cue->audioOutputPair, 0, 7) : 0,
                            std::memory_order_relaxed);
}

// Audio-thread-safe fade gain: same curve as fadeGainAt but reads only the
// atomic mirrors — never activeCue_ / duration_ / the suppress flags, which
// are plain members owned by the main thread.
double MediaEngine::audioFadeGainAt(double positionSeconds) const {
  double gain = 1.0;
  double fadeIn = audioFadeInSeconds_.load(std::memory_order_relaxed);
  if (!audioSuppressFadeIn_.load(std::memory_order_relaxed) && fadeIn > 0.001) {
    gain = std::min(gain, std::clamp(positionSeconds / fadeIn, 0.0, 1.0));
  }
  double fadeOut = audioFadeOutSeconds_.load(std::memory_order_relaxed);
  double duration = audioFadeDuration_.load(std::memory_order_relaxed);
  if (!audioSuppressFadeOut_.load(std::memory_order_relaxed) && fadeOut > 0.001 && duration > 0.0) {
    double remaining = std::max(0.0, duration - positionSeconds);
    gain = std::min(gain, std::clamp(remaining / fadeOut, 0.0, 1.0));
  }
  return std::clamp(gain, 0.0, 1.0);
}

// Set up the duration timer for still-type cues (Image, Pattern, Browser, Composite).
// If the cue has a stillDurationSeconds > 0, the engine treats it as a timed cue:
// playback progresses via wall clock and handlePlaybackEnd() fires when duration expires.
// If stillDurationSeconds is 0, the cue stays on screen indefinitely (manual control only).
// autoplay=true starts the clock immediately; false pauses at the start.
void MediaEngine::initStillTimer(const Cue& cue, bool autoplay) {
  if (cue.stillDurationSeconds > 0.0) {
    duration_ = cue.stillDurationSeconds;
    if (autoplay) {
      playbackClockStart_ = std::chrono::steady_clock::now();
      playbackStartPosition_ = 0.0;
      pausedPosition_ = 0.0;
      state_ = TransportState::Playing;
    } else {
      state_ = TransportState::Paused;
    }
  } else {
    duration_ = 0.0;
    state_ = TransportState::Paused;
  }
}

// ---------------------------------------------------------------------------
// beginTransition — Snapshot the current frame and start a visual transition.
//
// Takes ownership of the current texture_ (moves it to transitionTexture_)
// so the outgoing cue's last frame is preserved. The transition timer doesn't
// start until the incoming cue's first frame arrives (transitionWaitingForFirstFrame_).
// This prevents jarring partial-blends when the new cue takes a moment to decode.
//
// sourceGain captures the outgoing cue's fade level at the moment of transition —
// if the outgoing cue was mid-fade, the transition starts from that reduced opacity
// rather than snapping to full brightness.
//
// For cuts (seconds ≤ 0), the old frame is discarded immediately. We don't enter
// the waiting state because some cue types (e.g. browser on Windows) never produce
// frames, so waiting would hold the old frame forever.
// ---------------------------------------------------------------------------
void MediaEngine::beginTransition(double seconds, TransitionStyle style, float sourceGain) {
  clearTransitionTexture();
  if (!texture_) {
    return;
  }
  if (seconds <= 0.001) {
    // For cuts: discard the old frame immediately.  Entering the
    // "waiting for first frame" state would hold it on screen until
    // the next cue renders something — which never happens for cues
    // that don't produce frames (e.g. browser on Windows).
    clearTexture();
    return;
  }
  // Move the current frame texture to the transition snapshot
  transitionTexture_ = texture_;
  transitionTextureWidth_ = textureWidth_;
  transitionTextureHeight_ = textureHeight_;
  texture_ = nullptr;
  textureWidth_ = 0;
  textureHeight_ = 0;
  transitionDurationSeconds_ = std::clamp(seconds, 0.0, 10.0);
  transitionStyle_ = style;
  transitionSourceGain_ = std::clamp(sourceGain, 0.0f, 1.0f);
  transitionActive_ = true;
  transitionWaitingForFirstFrame_ = true;  // timer starts when incoming cue provides first frame
}

// Release the transition snapshot texture and reset all transition state.
// Called when a transition completes (progress >= 1.0), when a new cue is
// loaded (via beginTransition → clearTransitionTexture first), or during stopAll.
void MediaEngine::clearTransitionTexture() {
  if (transitionTexture_ && !sdlTornDown()) {
    SDL_DestroyTexture(transitionTexture_);
  }
  transitionTexture_ = nullptr;
  transitionTextureWidth_ = 0;
  transitionTextureHeight_ = 0;
  transitionActive_ = false;
  transitionWaitingForFirstFrame_ = false;
  transitionDurationSeconds_ = 0.0;
  transitionStyle_ = TransitionStyle::Cut;
  transitionSourceGain_ = 1.0f;
}

// ---------------------------------------------------------------------------
// drawTextureFitted — Blit a texture into the target rect with scale/crop/offset.
//
// This is the common draw path for both the current frame and the transition
// snapshot. It applies all per-cue geometry transforms in this order:
//   1. Crop: compute source rect from crop fractions (left/right/top/bottom)
//   2. Scale mode: Fit (letterbox), Fill (crop to fill), Stretch, or Unscaled
//   3. Output scale: per-cue X/Y scale multipliers (outputScaleX/Y)
//   4. Offset: per-cue X/Y pixel offset (outputOffsetX/Y)
//   5. Rotation: per-cue rotation in degrees (outputRotationDegrees)
//   6. Alpha: overall opacity (used for fade and transition blending)
//
// Returns true if the texture was drawn, false if inputs were invalid.
// ---------------------------------------------------------------------------
bool MediaEngine::drawTextureFitted(SDL_Texture* texture, int width, int height, const SDL_Rect& target, Uint8 alphaValue) {
  if (!texture || width <= 0 || height <= 0) {
    return false;
  }
  // Step 1: Compute the source rect after cropping (fractions → pixel offsets)
  int cropL = std::clamp(static_cast<int>(std::lround(static_cast<double>(width) * cropLeft_)), 0, width - 1);
  int cropR = std::clamp(static_cast<int>(std::lround(static_cast<double>(width) * cropRight_)), 0, width - 1);
  int cropT = std::clamp(static_cast<int>(std::lround(static_cast<double>(height) * cropTop_)), 0, height - 1);
  int cropB = std::clamp(static_cast<int>(std::lround(static_cast<double>(height) * cropBottom_)), 0, height - 1);
  int srcW = std::max(1, width - cropL - cropR);
  int srcH = std::max(1, height - cropT - cropB);
  SDL_Rect source {cropL, cropT, srcW, srcH};

  // Step 2: Compute the scale factor based on the chosen scale mode
  double scale;
  if (scaleMode_ == ScaleMode::Fit) {
    // Letterbox: scale to fit within target, preserving aspect ratio
    scale = std::min(
      static_cast<double>(target.w) / static_cast<double>(srcW),
      static_cast<double>(target.h) / static_cast<double>(srcH)
    );
  } else if (scaleMode_ == ScaleMode::Fill) {
    // Fill: scale to cover target entirely, cropping overflow
    scale = std::max(
      static_cast<double>(target.w) / static_cast<double>(srcW),
      static_cast<double>(target.h) / static_cast<double>(srcH)
    );
  } else if (scaleMode_ == ScaleMode::Stretch) {
    scale = 1.0;  // stretch handles dimensions separately below
  } else {
    scale = 1.0;  // Unscaled: 1:1 pixel mapping
  }

  // Step 3: Compute draw dimensions based on scale mode
  int drawW, drawH;
  if (scaleMode_ == ScaleMode::Stretch) {
    drawW = target.w;  // fill target exactly, ignoring aspect ratio
    drawH = target.h;
  } else if (scaleMode_ == ScaleMode::Unscaled) {
    drawW = srcW;  // native pixel size, may be smaller or larger than target
    drawH = srcH;
  } else {
    drawW = std::max(1, static_cast<int>(std::round(srcW * scale)));
    drawH = std::max(1, static_cast<int>(std::round(srcH * scale)));
  }

  // Step 4: Apply per-cue output scale (additional zoom) and center with offset
  int scaledW = std::max(1, static_cast<int>(drawW * outputScaleX_));
  int scaledH = std::max(1, static_cast<int>(drawH * outputScaleY_));
  SDL_Rect destination {
    target.x + (target.w - scaledW) / 2 + static_cast<int>(outputOffsetX_),
    target.y + (target.h - scaledH) / 2 + static_cast<int>(outputOffsetY_),
    scaledW,
    scaledH
  };

  // Step 5: Blit with alpha blending and rotation around the center point
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureAlphaMod(texture, alphaValue);
  SDL_Point center {destination.w / 2, destination.h / 2};
  SDL_RenderTextureRotated(outputRenderer_, texture, &source, &destination, outputRotationDegrees_, &center, SDL_FLIP_NONE);
  SDL_SetTextureAlphaMod(texture, 255);  // reset alpha mod to avoid leaking to other draws
  return true;
}

// ---------------------------------------------------------------------------
// drawTransitionOverlay — Render the transition blend between outgoing and incoming cues.
//
// Two transition styles:
//   Crossfade: outgoing frame fades out (alpha decreasing) while incoming draws at full
//   DipBlack:  first half fades outgoing to black, second half fades incoming from black
//
// State machine:
//   1. WaitingForFirstFrame: hold outgoing at sourceGain opacity until incoming arrives
//   2. Once incoming frame is drawn, start the real timer
//   3. Animate progress 0→1 over transitionDurationSeconds_
//   4. At progress=1.0, clean up (clearTransitionTexture)
//
// The drewCurrent flag tells us whether render() successfully drew the incoming cue's
// frame. If false, we're still waiting for the first decode — keep showing outgoing.
// ---------------------------------------------------------------------------
void MediaEngine::drawTransitionOverlay(const SDL_Rect& target, bool drewCurrent) {
  if (!transitionActive_) {
    return;
  }

  // Phase 1: waiting for the incoming cue to produce its first frame
  if (transitionWaitingForFirstFrame_) {
    if (drewCurrent) {
      // First frame arrived — start the transition timer
      transitionWaitingForFirstFrame_ = false;
      transitionStartedAt_ = std::chrono::steady_clock::now();
      if (transitionDurationSeconds_ <= 0.001) {
        clearTransitionTexture();  // instant cut
        return;
      }
    } else {
      // Still waiting — show the outgoing frame at its captured fade level
      Uint8 waitAlpha = static_cast<Uint8>(transitionSourceGain_ * 255.0f);
      drawTextureFitted(transitionTexture_, transitionTextureWidth_, transitionTextureHeight_, target, waitAlpha);
      if (waitAlpha < 255) {
        // If outgoing was partially faded, fill the rest with black
        SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, 255 - waitAlpha);
        SDL_RenderFillRect(outputRenderer_, nullptr);
        SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_NONE);
      }
      return;
    }
  }

  // Phase 2: transition is actively animating
  double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - transitionStartedAt_).count();
  double progress = transitionDurationSeconds_ <= 0.0001 ? 1.0 : std::clamp(elapsed / transitionDurationSeconds_, 0.0, 1.0);
  if (progress >= 1.0) {
    clearTransitionTexture();  // transition complete — release snapshot
    return;
  }

  if (transitionStyle_ == TransitionStyle::DipBlack) {
    // Dip-to-black: two halves
    if (progress < 0.5) {
      // First half: outgoing fades to black (0→100% black overlay)
      Uint8 srcA = static_cast<Uint8>(transitionSourceGain_ * 255.0f);
      drawTextureFitted(transitionTexture_, transitionTextureWidth_, transitionTextureHeight_, target, srcA);
      SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_BLEND);
      double blackAlpha = std::clamp(progress * 2.0, 0.0, 1.0);
      if (transitionSourceGain_ < 1.0f) blackAlpha = std::max(blackAlpha, 1.0 - transitionSourceGain_);
      SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, static_cast<Uint8>(blackAlpha * 255.0));
      SDL_RenderFillRect(outputRenderer_, nullptr);
    } else {
      // Second half: incoming emerges from black (100%→0% black overlay)
      if (!drewCurrent) {
        SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, 255);
        SDL_RenderFillRect(outputRenderer_, nullptr);
      }
      double fadeOutBlack = std::clamp(1.0 - (progress - 0.5) * 2.0, 0.0, 1.0);
      SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, static_cast<Uint8>(fadeOutBlack * 255.0));
      SDL_RenderFillRect(outputRenderer_, nullptr);
    }
    return;
  }

  // Crossfade: outgoing fades out linearly over the transition duration
  Uint8 alphaValue = static_cast<Uint8>(transitionSourceGain_ * std::clamp(1.0 - progress, 0.0, 1.0) * 255.0);
  drawTextureFitted(transitionTexture_, transitionTextureWidth_, transitionTextureHeight_, target, alphaValue);
}

// ---------------------------------------------------------------------------
// handlePlaybackEnd — Respond when playback reaches the end of the cue duration.
//
// The behaviour depends on the cue's end action (resolvedCueEndAction):
//   Loop:        seek back to 0, suppress fades (seamless loop), keep playing
//   PauseOnLast: pause on the final frame (hold visible), mute audio
//   Stop:        stop transport, set reachedEnd_ flag for the transport handler
//                to pick up (which may auto-advance to the next cue)
//
// clearVisualOnReachedEnd_ controls whether finalizeReachedEnd() clears the
// texture (go to black) or holds the last frame. Loop/PauseOnLast keep the
// frame; Stop clears it.
// ---------------------------------------------------------------------------
void MediaEngine::handlePlaybackEnd() {
  if (!activeCue_) {
    return;
  }
  CueEndAction act = resolvedCueEndAction(*activeCue_);

  if (act == CueEndAction::Loop) {
    // Seamless loop: suppress fades to avoid a flash at the loop point
    suppressFadeInForCurrentCue_ = true;
    suppressVisualFadeOutForCurrentCue_ = true;
    seek(0.0, false);  // restart decode from beginning
    state_ = TransportState::Playing;
    playbackClockStart_ = std::chrono::steady_clock::now();
    playbackStartPosition_ = 0.0;
    pausedPosition_ = 0.0;
    if (audioStream_) {
      deckboySetAudioPaused(audioStream_, false);  // ensure audio is unpaused
    }
    return;
  }
  if (act == CueEndAction::PauseOnLast) {
    // Freeze on the final frame — useful for "reveal" cues that hold an image
    state_ = TransportState::Paused;
    pausedPosition_ = duration_;
    currentPosition_ = duration_;
    clearVisualOnReachedEnd_ = false;  // keep last frame visible
    if (audioStream_) {
      deckboySetAudioPaused(audioStream_, true);
    }
    return;
  }
  // Default: stop and signal end-of-playback (the transport handler decides
  // whether to auto-advance, clear, or wait for operator input)
  state_ = TransportState::Stopped;
  pausedPosition_ = duration_;
  currentPosition_ = duration_;
  clearVisualOnReachedEnd_ = true;  // go to black when finalized
  if (audioStream_) {
    deckboySetAudioPaused(audioStream_, true);
  }
  reachedEnd_ = true;  // consumed by reachedEnd() in the next update cycle
}

// Release the main frame GPU texture. Called when clearing the deck,
// loading a new cue, or during stopAll cleanup.
void MediaEngine::clearTexture() {
  if (texture_ && !sdlTornDown()) {
    SDL_DestroyTexture(texture_);
  }
  texture_ = nullptr;
  textureWidth_ = 0;
  textureHeight_ = 0;
  textureFormat_ = 0;
  lastUploadedFrameIndex_ = static_cast<std::uint64_t>(-1);
}

// ---------------------------------------------------------------------------
// uploadFrame — Push a decoded frame from CPU to its GPU texture.
//
// Recreates the texture whenever dimensions OR pixel format change, so a
// cue switch from an effects-enabled RGBA cue to a plain NV12 cue rebuilds
// the texture once and then streams updates without further churn.
//
// NV12 frames take the fast path: SDL_UpdateNVTexture with the Y plane
// followed by the interleaved UV plane. They do not go through the effects
// scratch buffer — startDecoderThreads only picks NV12 when the cue had no
// chroma key or color controls active at TAKE time, so there is nothing to
// apply here. Toggling effects live on an NV12 cue is silently ignored
// until the next reload; the trade-off is recorded in DEVNOTES.
//
// RGBA frames keep the original CPU-effects pipeline: if effects are
// active, pixels are copied into keyedPixelsScratch_ and mutated there so
// the source frame stays intact for re-processing on parameter changes.
// ---------------------------------------------------------------------------
void MediaEngine::uploadFrame(const DecodedFrame& frame) {
  if (frame.isGpu()) {
    // Zero-copy frame: composited from its wrapped D3D11 texture at the
    // output bridge — nothing to upload on the deck's hidden renderer.
    return;
  }
  if (frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) {
    return;
  }
  const Uint32 wantFormat = sdlPixelFormat(frame.format);
  const bool sizeChanged = textureWidth_ != frame.width || textureHeight_ != frame.height;
  const bool formatChanged = textureFormat_ != wantFormat;
  if (!texture_ || sizeChanged || formatChanged) {
    clearTexture();
    texture_ = deckboyCreateTexture(
      outputRenderer_,
      wantFormat,
      SDL_TEXTUREACCESS_STREAMING,
      frame.width,
      frame.height
    );
    if (!texture_) {
      return;
    }
    textureWidth_ = frame.width;
    textureHeight_ = frame.height;
    textureFormat_ = wantFormat;
  }
  if (frame.format == FramePixelFormat::NV12) {
    const std::uint8_t* y = frame.pixels.data();
    const std::uint8_t* uv = y + static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
    SDL_UpdateNVTexture(texture_, nullptr, y, frame.width, uv, frame.width);
    return;
  }
  // RGBA path: apply per-pixel effects (chroma key + color controls) if any
  // are active. The scratch copy keeps the source frame reusable for the next
  // upload — needed because render() may call uploadFrame again when effect
  // parameters change without a new frame arriving.
  const std::uint8_t* uploadPixels = frame.pixels.data();
  if (chromaKeyEnabled_ || colorControlsActive(brightness_, contrast_, saturation_, hueShift_)) {
    keyedPixelsScratch_.assign(frame.pixels.begin(), frame.pixels.end());
    applyCueVisualEffectsToPixels(
      keyedPixelsScratch_,
      chromaKeyEnabled_,
      chromaKeyColor_,
      chromaKeyTolerance_,
      chromaKeySoftness_,
      brightness_,
      contrast_,
      saturation_,
      hueShift_);
    uploadPixels = keyedPixelsScratch_.data();
  }
  SDL_UpdateTexture(texture_, nullptr, uploadPixels, frame.width * 4);
}

// Join the still-image decode thread and clean up its process and pending frame.
// Must be called before starting a new image decode (loadStillFrame) to avoid
// leaking the previous thread. Uses the two-phase shutdown pattern: kill the
// process first (unblocks readExact in the thread), then join.
void MediaEngine::stopImageThread() {
  // Announce the kill BEFORE it happens: killing the process makes the decode
  // thread's pipe read fail, and without this flag that read would be
  // indistinguishable from a genuine decode failure and would false-alarm the
  // operator on every ordinary cue switch.
  imageCancelRequested_.store(true, std::memory_order_release);
  imageProcess_.killProcessOnly();
  if (imageThread_.joinable()) {
    imageThread_.join();
  }
  imageProcess_.stop();
  imageCancelRequested_.store(false, std::memory_order_release);
  // Clear any unconsumed failure latch here — this is the single point every
  // new still decode passes through (loadStillFrame calls it first), so a
  // failure from a previous cue can never leak a stale toast onto the next one.
  imageDecodeFailed_.store(false, std::memory_order_release);
  std::lock_guard<std::mutex> lk(imageMutex_);
  pendingImageFrame_.reset();
  imageFramePending_.store(false);
}

// Query the actual output dimensions from the SDL renderer. Falls back to
// kOutputWidth x kOutputHeight (1280x720) if the renderer isn't available.
// Used by decode functions to size frames appropriately when the cue doesn't
// specify explicit dimensions (e.g. un-ingested cues, live streams).
std::pair<int, int> MediaEngine::currentOutputSizeHint() const {
  // The app-provided program-output raster wins: patterns must stay
  // pixel-mapped to the display the operator actually selected, live.
  if (outputSizeProvider_) {
    OutputModeHint mode = outputSizeProvider_();
    if (mode.width > 0 && mode.height > 0) {
      return {mode.width, mode.height};
    }
  }
  int w = kOutputWidth;
  int h = kOutputHeight;
  if (outputRenderer_) {
    int rw = 0;
    int rh = 0;
    if (SDL_GetCurrentRenderOutputSize(outputRenderer_, &rw, &rh) && rw > 0 && rh > 0) {
      w = rw;
      h = rh;
    }
  }
  return {w, h};
}

// Resolve the media file path for a cue. If a CuePathResolver callback was
// provided (e.g. to transform relative paths to absolute), it gets first
// priority. Falls back to cue.path if the resolver returns empty.
// Datamosh only applies to a file-backed video cue that has actually been
// prepared. Everything else - stills, patterns, browser, live sources, and an
// unprepared cue - reports false, so the toggle can say "not prepared" instead
// of silently doing nothing.
bool MediaEngine::datamoshActiveForCue(const Cue& cue) const {
  if (!cue.datamoshEnabled || cue.kind != CueKind::Video) return false;
  if (cue.moshPath.empty()) return false;
  std::error_code ec;
  return std::filesystem::exists(cue.moshPath, ec);
}

std::string MediaEngine::mediaPathForCue(const Cue& cue) const {
  if (cuePathResolver_) {
    std::string resolved = cuePathResolver_(cue);
    if (!trim(resolved).empty()) {
      return resolved;
    }
  }
  return cue.path;
}

// ---------------------------------------------------------------------------
// loadStillFrame — Async-decode a single frame from an image file.
//
// Spawns ffmpeg to decode one frame (the first) at the target resolution,
// piping raw RGBA via stdout. The decode runs on imageThread_ to avoid
// blocking the main thread. When complete, the frame is placed in
// pendingImageFrame_ and the atomic flag imageFramePending_ is set.
// The main thread picks it up in update() on the next tick.
//
// Dimensions are clamped to the output resolution — no point decoding a
// 4K image when the output is 1280x720. Uses "neighbor" scaling (nearest-
// neighbor) since still images are often pixel-art or diagrams.
// ---------------------------------------------------------------------------
void MediaEngine::loadStillFrame(const Cue& cue) {
  stopImageThread();
  std::string mediaPath = mediaPathForCue(cue);
  if (mediaPath.empty()) {
    return;
  }

  // Determine decode dimensions: use cue size if available, else output size
  auto [capW, capH] = currentOutputSizeHint();
  int w = cue.width > 0 ? cue.width : capW;
  int h = cue.height > 0 ? cue.height : capH;
  // Downscale to output resolution if the image is larger (save decode time + memory)
  if (w > capW || h > capH) {
    double scale = std::min(
      static_cast<double>(capW) / w,
      static_cast<double>(capH) / h
    );
    w = std::max(1, static_cast<int>(w * scale));
    h = std::max(1, static_cast<int>(h * scale));
  }

  // HEIF/HEIC (iPhone photos) are reconstructed from tiles through an INTERNAL
  // complex filtergraph, and ffmpeg refuses to also apply a simple `-vf` filter
  // on top of that ("Simple and complex filtering cannot be used together for
  // the same stream") — so `-vf scale` produced zero bytes and the still cue
  // loaded but never showed a frame. The fix is to express the scale as a
  // `-filter_complex` graph, which composes with the tile reconstruction. That
  // form works for ordinary images too, but `-vf` is kept as the default so the
  // long-proven PNG/JPG path is untouched; only the complex-graph formats take
  // the new branch.
  const std::string scaleExpr =
    "scale=" + std::to_string(w) + ":" + std::to_string(h) + ":flags=neighbor";
  // Extension check without pulling in <filesystem> here: take the tail after
  // the last '.' and lowercase it.
  std::string lowerExt;
  if (auto dot = mediaPath.find_last_of('.'); dot != std::string::npos) {
    lowerExt = mediaPath.substr(dot);
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  }
  const bool complexGraphFormat = (lowerExt == ".heic" || lowerExt == ".heif");

  std::vector<std::string> stillArgs = {
    "ffmpeg", "-hide_banner", "-loglevel", "error",
    "-i", mediaPath,
    "-frames:v", "1",
  };
  if (complexGraphFormat) {
    stillArgs.push_back("-filter_complex");
    stillArgs.push_back("[0:v]" + scaleExpr + "[o]");
    stillArgs.push_back("-map");
    stillArgs.push_back("[o]");
  } else {
    stillArgs.push_back("-vf");
    stillArgs.push_back(scaleExpr);
  }
  stillArgs.push_back("-f");
  stillArgs.push_back("rawvideo");
  stillArgs.push_back("-pix_fmt");
  stillArgs.push_back("rgba");
  stillArgs.push_back("pipe:1");

  if (!spawnPipeProcess(imageProcess_, stillArgs)) {
    return;
  }

  // Read the single decoded frame on a background thread
  const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
  int imageFd = imageProcess_.readFd;
  imageThread_ = std::thread([this, w, h, frameBytes, imageFd]() {
    DecodedFrame frame;
    frame.width = w;
    frame.height = h;
    frame.index = 0;
    frame.pixels.resize(frameBytes);
    if (readExact(imageFd, frame.pixels.data(), frameBytes)) {
      std::lock_guard<std::mutex> lk(imageMutex_);
      pendingImageFrame_ = std::move(frame);
      imageFramePending_.store(true);  // signal main thread via update()
    } else if (!imageCancelRequested_.load(std::memory_order_acquire)) {
      // The pipe closed before a full frame arrived and we did NOT ask for that
      // (a cue switch would have — stopImageThread sets imageCancelRequested_
      // before killing the process). So this is a real decode failure: ffmpeg
      // could not produce the still. Latch it for the app to report.
      imageDecodeFailed_.store(true, std::memory_order_release);
    }
  });
}

// Generate a procedural pattern frame and upload it immediately.
// Called once on cue load for pattern cues. For animated patterns,
// rebuildPatternFrame() is called each frame to update the animation.
// animTime=0.0 gives the initial static state of the pattern.
void MediaEngine::loadPatternFrame(const Cue& cue) {
  auto [fallbackW, fallbackH] = currentOutputSizeHint();
  auto frame = buildPatternFrame(cue, 0.0, fallbackW, fallbackH);
  if (frame) {
    frame->index = ++displayFrameSerial_;
    displayFrame_ = std::move(frame);
    uploadFrame(*displayFrame_);
  }
}

// ---------------------------------------------------------------------------
// loadSourceFrame — Generate a placeholder frame for source capture cues.
//
// Source cues (WindowSource, Camera, Syphon) don't have media files to decode.
// Before capture starts, we show a placeholder frame: horizontal stripes in
// the DMG-inspired palette with a thin accent-color border inset. The color
// palette varies by source kind so the operator can visually distinguish them:
//   WindowSource: green tones (default DMG palette)
//   Camera:       teal/emerald tones
//   Syphon:       olive/khaki tones
//
// This frame is replaced by live capture data once startSourceCapture() runs.
// ---------------------------------------------------------------------------
void MediaEngine::loadSourceFrame(const Cue& cue) {
  auto [fallbackW, fallbackH] = currentOutputSizeHint();
  int w = cue.width > 0 ? cue.width : fallbackW;
  int h = cue.height > 0 ? cue.height : fallbackH;
  w = std::clamp(w, 64, 3840);
  h = std::clamp(h, 64, 2160);

  DecodedFrame frame;
  frame.width = w;
  frame.height = h;
  frame.index = 0;
  frame.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0);

  // Select color palette based on source kind
  SDL_Color bg {48, 98, 48, 255};
  SDL_Color stripe {15, 56, 15, 255};
  SDL_Color accent {139, 172, 15, 255};
  if (cue.kind == CueKind::Camera) {
    bg = SDL_Color {36, 82, 54, 255};
    stripe = SDL_Color {16, 38, 28, 255};
    accent = SDL_Color {155, 208, 125, 255};
  } else if (cue.kind == CueKind::Syphon) {
    bg = SDL_Color {56, 62, 30, 255};
    stripe = SDL_Color {28, 34, 16, 255};
    accent = SDL_Color {177, 188, 94, 255};
  }

  // Fill with alternating horizontal stripes (14px each)
  for (int y = 0; y < h; ++y) {
    bool stripeRow = ((y / 14) % 2) == 0;
    SDL_Color rowColor = stripeRow ? bg : stripe;
    for (int x = 0; x < w; ++x) {
      size_t offset = static_cast<size_t>(y * w + x) * 4u;
      frame.pixels[offset + 0] = rowColor.r;
      frame.pixels[offset + 1] = rowColor.g;
      frame.pixels[offset + 2] = rowColor.b;
      frame.pixels[offset + 3] = 255;
    }
  }

  // Draw a 2px accent-color border inset from the edges
  int margin = std::max(6, std::min(w, h) / 18);
  for (int y = margin; y < h - margin; ++y) {
    for (int x = margin; x < w - margin; ++x) {
      bool border = (x - margin < 2) || (h - margin - 1 - y < 2)
        || (y - margin < 2) || (w - margin - 1 - x < 2);
      if (!border) {
        continue;
      }
      size_t offset = static_cast<size_t>(y * w + x) * 4u;
      frame.pixels[offset + 0] = accent.r;
      frame.pixels[offset + 1] = accent.g;
      frame.pixels[offset + 2] = accent.b;
      frame.pixels[offset + 3] = 255;
    }
  }

  displayFrame_ = std::move(frame);
  displayFrame_->index = ++displayFrameSerial_;
  uploadFrame(*displayFrame_);
}

// Flush the SDL audio queue and pause the audio device. Called during cue
// transitions, stop, and cleanup to prevent leftover audio from bleeding
// into the next cue or playing after the cue has stopped.
void MediaEngine::clearAudio() {
  // sdlTornDown(): SDL_Quit already freed this stream. See setSdlTornDown().
  if (audioStream_ && !sdlTornDown()) {
    SDL_ClearAudioStream(audioStream_);
    deckboySetAudioPaused(audioStream_, true);
  }
  // Safe here: callers only clear audio after decode threads are stopped,
  // and the sync pop runs on this (main) thread.
  audioDelayFifo_.clear();
  // Open the limiter back up: a new cue must not start ducked by whatever
  // transient the previous one ended on.
  limiterGain_ = 1.0;
}

// ---------------------------------------------------------------------------
// queuePocketSyncAudio — The pocket-test card's A/V sync pop.
//
// An 80 ms 1 kHz tone fires at the top of every wall-clock second — the same
// clock rebuildPatternFrame animates on, and the same window in which the
// card's buoy lamp lights. Samples are synthesized keyed to the wall time at
// which they will actually PLAY (now + already-queued audio), so the pop
// leaves the audio device aligned with the flash leaving the renderer; what
// the operator then measures at the far end is the chain's own A/V offset.
// Keeps ~120 ms queued; honors deck volume, master gain, and cue fades, and
// feeds the VU/waveform tap like any decoded audio.
// ---------------------------------------------------------------------------
void MediaEngine::queuePocketSyncAudio() {
  constexpr int kRate = 48000;
  constexpr double kBeepSeconds = 0.08;
  constexpr int kChunkFrames = 1024;  // ~21 ms per fill
  int queuedBytes = std::max(0, SDL_GetAudioStreamQueued(audioStream_));
  double queuedSeconds = static_cast<double>(queuedBytes)
    / (kRate * static_cast<double>(audioStreamBytesPerFrame()));
  if (queuedSeconds > 0.12) {
    return;
  }
  deckboySetAudioPaused(audioStream_, false);
  const double startSeconds =
    static_cast<double>(SDL_GetTicks()) / 1000.0 + queuedSeconds;
  const double gain = static_cast<double>(volume_.load())
                    * static_cast<double>(masterGain_.load())
                    * audioCueGain_.load(std::memory_order_relaxed)
                    * audioFadeGainAt(position()) * 0.5;
  const double pan = static_cast<double>(audioCuePan_.load(std::memory_order_relaxed));
  const double panL = pan > 0.0 ? 1.0 - pan : 1.0;
  const double panR = pan < 0.0 ? 1.0 + pan : 1.0;
  std::vector<std::int16_t> samples(static_cast<size_t>(kChunkFrames) * 2);
  for (int i = 0; i < kChunkFrames; ++i) {
    double phase = std::fmod(startSeconds + static_cast<double>(i) / kRate, 1.0);
    double value = 0.0;
    if (phase < kBeepSeconds) {
      // 5 ms attack / 10 ms release envelope — no clicks on cheap speakers.
      double envelope = std::min({phase / 0.005, (kBeepSeconds - phase) / 0.010, 1.0});
      value = std::sin(phase * kTau * 1000.0) * envelope;
    }
    auto clip = [](double v) {
      return static_cast<std::int16_t>(std::clamp(
        static_cast<int>(std::lround(v)), -32768, 32767));
    };
    samples[static_cast<size_t>(i) * 2] = clip(value * gain * panL * 32767.0);
    samples[static_cast<size_t>(i) * 2 + 1] = clip(value * gain * panR * 32767.0);
  }
  // Through the same delay line as decode audio: the beacon stays keyed to
  // the wall clock, so adjusting the audio delay visibly/audibly shifts the
  // pop against the flash — that's the dial-in workflow.
  queueDelayedAudio(samples);
}

// Thread-safe query of the current frame queue depth. Used by update() to
// detect when the decoder is starved (queue empty + EOF = playback finished).
size_t MediaEngine::queuedFrames() {
  std::lock_guard<std::mutex> lock(frameMutex_);
  return frameQueue_.size();
}

// ---------------------------------------------------------------------------
// stopDecoderThreads — Kill ffmpeg subprocesses and join decode threads.
//
// Uses a two-phase shutdown to avoid crashes on Windows:
//   Phase 1: killProcessOnly() terminates the ffmpeg process. This closes
//            the write-end of the pipe, causing readExact/readSome to return
//            0 (EOF) in the decode thread — unblocking it cleanly.
//   Phase 2: join the threads (now unblocked), then call stop() to close
//            the readFd. Closing readFd before join would crash because
//            the thread is still in _read().
//
// Also clears the frame queue and resets capture flags.
// ---------------------------------------------------------------------------
void MediaEngine::stopDecoderThreads() {
  stopImageThread();
  decodersRunning_ = false;
  audioClockValid_ = false;  // audio clock dies with the pipe
  decoderStop_.store(true);  // signal threads to exit their loop
  // Phase 1: kill processes to unblock the decode threads' read calls
  videoProcess_.killProcessOnly();
  audioProcess_.killProcessOnly();
#if DECKBOY_INPROC_DECODE
  // In-process equivalent of the pipe kill: trip the AVIO interrupt callback
  // so a decode thread blocked inside av_read_frame (dead network share)
  // returns promptly and the joins below can't hang.
  if (videoPipeline_) {
    videoPipeline_->requestStop();
  }
  if (audioPipeline_) {
    audioPipeline_->requestStop();
  }
#endif

  // Phase 2: wait for threads to finish, then clean up process handles
  if (videoThread_.joinable()) {
    videoThread_.join();
  }
  if (audioThread_.joinable()) {
    audioThread_.join();
  }

  videoProcess_.stop();  // now safe to close readFd
  audioProcess_.stop();

#if DECKBOY_INPROC_DECODE
  videoPipeline_.reset();  // threads are joined — safe to tear down
  audioPipeline_.reset();
#endif
  inprocDecodeActive_ = false;
  activeDecodeDevice_ = nullptr;
  lastFramePushMs_.store(0);
  decodeStallLatched_ = false;

  {
    std::lock_guard<std::mutex> lock(frameMutex_);
    frameQueue_.clear();  // discard any buffered frames (releases GPU surfaces)
  }
  decoderStop_.store(false);  // reset for next cue
  decoderEof_ = false;
  isSourceCapturing_ = false;
  isBrowserCapturing_ = false;
}

// ---------------------------------------------------------------------------
// buildSourceCaptureArgs — Build ffmpeg command-line args for source capture.
//
// Delegates to the platform-specific capture backend (capture_backend.hpp)
// which knows how to capture from cameras, windows, and app textures on each
// OS. The backend returns a SourceCapturePlan with the full ffmpeg arg list.
//
// Source reference resolution:
//   1. Try sourceCueRefFromCue (the cue's stored device/window identifier)
//   2. Fall back to defaultSourceRefForKind (e.g. "/dev/video0" on Linux)
//
// On Linux/macOS, the $DISPLAY environment variable is forwarded to the
// capture backend for X11 screen capture (not needed on Windows/Wayland).
// ---------------------------------------------------------------------------
bool MediaEngine::buildSourceCaptureArgs(const Cue& cue, int w, int h, std::vector<std::string>& args) const {
  std::string sourceRef = sourceCueRefFromCue(cue);
  if (sourceRef.empty()) {
    sourceRef = defaultSourceRefForKind(cue.kind);
  }
  // Read $DISPLAY for X11 screen capture (Linux)
  std::string displayEnv;
  if (const char* envDisplay = std::getenv("DISPLAY"); envDisplay && *envDisplay) {
    std::string trimmed = trim(envDisplay);
    if (!trimmed.empty()) {
      displayEnv = trimmed;
    }
  }

  // Build the platform-specific capture request
  deckboy::platform::SourceCaptureRequest request;
  if (cue.kind == CueKind::Camera) {
    request.kind = deckboy::platform::SourceCaptureKind::Camera;
  } else if (cue.kind == CueKind::Syphon) {
    request.kind = deckboy::platform::SourceCaptureKind::AppTexture;
  } else {
    request.kind = deckboy::platform::SourceCaptureKind::Window;
  }
  request.sourceRef = sourceRef;
  request.width = w;
  request.height = h;
  request.frameRate = 30;
  request.drawMouse = true;
  request.display = displayEnv;
  auto plan = deckboy::platform::planSourceCapture(request);
  if (!plan.supported || plan.ffmpegArgs.empty()) {
    return false;
  }
  args = std::move(plan.ffmpegArgs);
  return true;
}

// ---------------------------------------------------------------------------
// startDecoderThreads — Launch ffmpeg video + audio decode subprocesses.
//
// This is where the actual media decoding happens. Two ffmpeg processes are
// spawned with piped stdout:
//
// Video pipeline:
//   ffmpeg -hwaccel auto -ss <start> -i <path> -map 0:v:0 -an
//          -vf scale=<w>:<h>:flags=fast_bilinear[,setpts=...]
//          -f rawvideo -pix_fmt {nv12|rgba} pipe:1
//   → videoThread_ reads raw frames via readExact() → frameQueue_
//
// Scaling stays inside ffmpeg so the pipe never carries more bytes than the
// output needs (a 4K source through a 1080p output ships ~250 MB/s, not 1 GB/s).
// The scaler is fast_bilinear, not bicubic: bicubic is ~3–4× the CPU cost and
// indistinguishable at deck-output sizes for moving video. If a still-image
// path ever shares this code, give it its own filter — neighbor/bicubic stays
// in the still-image helpers further down.
//
// Pixel format is decided per-cue: NV12 (planar YUV, 12 bpp, ~62% less pipe
// bandwidth than RGBA) for plain cues, RGBA for cues with chroma key or
// color controls — the effects path mutates RGBA scratch pixels and cannot
// operate on YUV planes without a shader rewrite.
//
// Audio pipeline:
//   ffmpeg -ss <start> -i <path> -map 0:a:0 -vn
//          -f s16le -ar 48000 -ac 2 pipe:1
//   → audioThread_ reads raw PCM via readSome() → SDL_QueueAudio()
//
// Special cases:
//   - Live streams (SRT/NDI): skip -ss (no seek) and -hwaccel (latency)
//   - NDI sources: use -f libndi_newtek -i <source_name> instead of -i <path>
//   - Speed != 1.0: add setpts filter for video, atempo filter for audio
//   - No audio: audio subprocess is not spawned
//   - Unknown dimensions: ffprobe is called first (only for non-ingested cues)
// ---------------------------------------------------------------------------
void MediaEngine::startDecoderThreads(const Cue& cue, double mediaStartSeconds, double cueStartSeconds) {
  // Datamosh swaps the FILE, not just the decode flag: the prepared copy is a
  // separate encode (no B-frames, single reference) sitting beside the
  // original. Done here rather than in mediaPathForCue on purpose - that goes
  // through the app resolver, which is ALSO used by waveform analysis,
  // thumbnails, the timeline filmstrip, the missing-media scan and loudness
  // normalize. Swapping there would quietly point all of them at the mosh file.
  const bool moshing = datamoshActiveForCue(cue);
  std::string mediaPath = moshing ? cue.moshPath : mediaPathForCue(cue);
  if (mediaPath.empty()) {
    return;
  }
  int decodeW = cue.width;
  int decodeH = cue.height;
  // Detect live stream sources: skip ffprobe (blocks on network) and seek.
  bool isNdiSource = (cue.kind == CueKind::NdiSource);
  bool isLiveStream = (cue.kind == CueKind::SrtStream || cue.kind == CueKind::NdiSource);
  // Only probe when dimensions are unknown (e.g. cue not yet ingested).
  // Ingest (probeCue) already handles rotation and stores final rasterised
  // dimensions in cue.width/height, so re-probing on every TAKE is redundant
  // and blocks the main thread for 200–500 ms per take.
  if (cue.kind == CueKind::Video && !isLiveStream && (decodeW <= 0 || decodeH <= 0)) {
    auto probeOut = readAllText({
      "ffprobe", "-v", "error", "-select_streams", "v:0",
      "-show_entries", "stream=width,height",
      "-show_entries", "stream_side_data=rotation",
      "-of", "default=noprint_wrappers=1",
      mediaPath
    });
    if (probeOut) {
      int pw = 0, ph = 0;
      int rot = 0;
      for (const auto& line : splitLines(*probeOut)) {
        auto sep = line.find('=');
        if (sep == std::string::npos) continue;
        std::string key = line.substr(0, sep);
        std::string val = line.substr(sep + 1);
        if (key == "width") pw = std::max(0, std::atoi(val.c_str()));
        else if (key == "height") ph = std::max(0, std::atoi(val.c_str()));
        else if (key == "rotation") rot = std::abs(std::atoi(val.c_str()));
      }
      if (pw > 0 && ph > 0) {
        if (rot == 90 || rot == 270) std::swap(pw, ph);
        decodeW = pw;
        decodeH = ph;
      }
    }
  }
  // Fall back to output resolution if no size is known (live streams)
  auto [fallbackW, fallbackH] = currentOutputSizeHint();
  if (decodeW <= 0 || decodeH <= 0) {
    decodeW = fallbackW;
    decodeH = fallbackH;
  }
  // NV12 needs even dimensions for its half-resolution chroma plane. Round
  // down so the pipe byte count matches what SDL_UpdateNVTexture expects.
  // For RGBA the same trim is a no-op in practice — sources are virtually
  // always even — but keeping it unconditional avoids a divergence later.
  decodeW &= ~1;
  decodeH &= ~1;
  if (decodeW <= 0 || decodeH <= 0) {
    return;
  }
  std::string scaleFilter = "scale=" + std::to_string(decodeW) + ":" + std::to_string(decodeH)
                          + ":flags=fast_bilinear";
  double speed = std::clamp(cue.playbackSpeed, 0.25, 4.0);
  if (std::abs(speed - 1.0) > 0.01) {
    std::ostringstream pts;
    pts << std::fixed << std::setprecision(4) << (1.0 / speed);
    scaleFilter += ",setpts=" + pts.str() + "*PTS";
  }
  // Pixel format is per-cue. Cues with chroma key or color controls need RGBA
  // because the effects path mutates interleaved RGBA bytes; everything else
  // takes the NV12 fast path (~62% less pipe bandwidth). The decision is
  // frozen at decode start — toggling effects mid-playback on an NV12 cue
  // will not take visual effect until the next TAKE. Documented in
  // DEVNOTES.md (`GPU Hardware Decode Note`).
  const bool needsRgbaForEffects =
    cue.chromaKeyEnabled ||
    colorControlsActive(cue.brightness, cue.contrast, cue.saturation, cue.hueShift);
  const FramePixelFormat decodeFormat =
    needsRgbaForEffects ? FramePixelFormat::RGBA32 : FramePixelFormat::NV12;
  const char* ffmpegPixFmt = needsRgbaForEffects ? "rgba" : "nv12";

#if DECKBOY_INPROC_DECODE
  // In-process libav decode for file-backed cues (GPU_DECODE_PLAN §11
  // Session 2). Live streams stay on the CLI (libndi_newtek input device,
  // latency-sensitive open behavior). When the pipeline can't take the file
  // (rotation metadata, undecodable), fall through to the CLI pipe path —
  // the break-glass fallback is automatic.
  if (!isLiveStream && !inprocDecodeDisabled() &&
      startInprocDecoders(cue, mediaPath, mediaStartSeconds, cueStartSeconds,
                          decodeW, decodeH, decodeFormat, speed)) {
    return;
  }
#endif

  // Cap subprocess decode threads so ffmpeg's pool doesn't starve the render
  // loop on small CPUs (the Pocket has 4 threads) — counterintuitively
  // smoother than letting it size itself.
  const int cliDecodeThreads = std::clamp(SDL_GetNumLogicalCPUCores() / 2, 1, 4);
  // The scale filter is a no-op for normal video cues (decode size == probed
  // cue size) — skip it and save a per-frame CPU pass; keep it whenever the
  // sizes differ or a speed change needs setpts.
  const bool needsVideoFilter =
    decodeW != cue.width || decodeH != cue.height || std::abs(speed - 1.0) > 0.01;

  // Build ffmpeg video args. Live streams skip seek and hwaccel (avoids latency/compat issues).
  // NDI sources use ffmpeg's libndi_newtek input device; path format: ndi://SOURCE_NAME
  std::vector<std::string> videoArgs = {
    "ffmpeg", "-hide_banner", "-loglevel", "error",
    "-threads", std::to_string(cliDecodeThreads)
  };
  if (!isLiveStream) {
    videoArgs.insert(videoArgs.end(), {"-hwaccel", "auto"});
    videoArgs.insert(videoArgs.end(), {"-ss", std::to_string(mediaStartSeconds)});
  }
  if (isNdiSource) {
    // Strip ndi:// prefix — the remainder is the NDI source name
    std::string ndiName = mediaPath.substr(6);
    videoArgs.insert(videoArgs.end(), {"-f", "libndi_newtek", "-i", ndiName});
  } else {
    videoArgs.insert(videoArgs.end(), {"-i", mediaPath});
  }
  videoArgs.insert(videoArgs.end(), {"-map", "0:v:0", "-an"});
  if (needsVideoFilter) {
    videoArgs.insert(videoArgs.end(), {"-vf", scaleFilter});
  }
  videoArgs.insert(videoArgs.end(), {
    "-f", "rawvideo",
    "-pix_fmt", ffmpegPixFmt,
    "pipe:1"
  });
  if (!spawnPipeProcess(videoProcess_, std::move(videoArgs))) {
    return;
  }

  const size_t frameBytes = frameBufferSize(decodeFormat, decodeW, decodeH);
  if (frameBytes == 0) {
    videoProcess_.stop();
    decoderEof_ = true;
    return;
  }
  int videoFd = videoProcess_.readFd;
  decodersRunning_ = true;
  videoThread_ = std::thread([this, decodeW, decodeH, decodeFormat, frameBytes, cueStartSeconds, videoFd]() {
    std::uint64_t frameIndex = static_cast<std::uint64_t>(std::floor(cueStartSeconds * frameRate_));
    while (!decoderStop_.load()) {
      while (!decoderStop_.load()) {
        bool hasRoom = false;
        {
          std::lock_guard<std::mutex> lock(frameMutex_);
          hasRoom = frameQueue_.size() < kMaxVideoFrames;
        }
        if (hasRoom) {
          break;
        }
        SDL_Delay(4);
      }
      if (decoderStop_.load()) {
        break;
      }

      DecodedFrame frame;
      frame.width = decodeW;
      frame.height = decodeH;
      frame.index = frameIndex++;
      frame.format = decodeFormat;
      frame.pixels.resize(frameBytes);

      if (!readExact(videoFd, frame.pixels.data(), frameBytes)) {
        decoderEof_ = true;
        break;
      }

      std::lock_guard<std::mutex> lock(frameMutex_);
      frameQueue_.push_back(std::move(frame));
    }
    decoderEof_ = true;
  });

  audioClockValid_ = false;
  if (audioStream_ != nullptr && cue.hasAudio && cue.audioEnabled) {
    syncAudioFadeParams();  // publish before the audio thread spawns
    audioFramesQueued_.store(0, std::memory_order_relaxed);
    audioClockStartSeconds_ = cueStartSeconds;
    lastAudioClockSeconds_ = -1.0;
    lastAudioClockAdvanceMs_ = 0;
    // Live streams have no deterministic mapping from queued samples to cue
    // position — leave the wall clock in charge for them.
    audioClockValid_ = !isLiveStream;
    std::vector<std::string> audioArgs = {
      "ffmpeg", "-hide_banner", "-loglevel", "error"
    };
    if (!isLiveStream) {
      audioArgs.insert(audioArgs.end(), {"-ss", std::to_string(mediaStartSeconds)});
    }
    if (isNdiSource) {
      std::string ndiName = mediaPath.substr(6);
      audioArgs.insert(audioArgs.end(), {"-f", "libndi_newtek", "-i", ndiName, "-vn"});
    } else {
      audioArgs.insert(audioArgs.end(), {"-i", mediaPath, "-vn"});
    }
    if (std::abs(speed - 1.0) > 0.01) {
      std::string atempoChain;
      double remaining = speed;
      while (remaining < 0.5 - 0.001) {
        atempoChain += (atempoChain.empty() ? "" : ",") + std::string("atempo=0.5");
        remaining /= 0.5;
      }
      while (remaining > 2.0 + 0.001) {
        atempoChain += (atempoChain.empty() ? "" : ",") + std::string("atempo=2.0");
        remaining /= 2.0;
      }
      {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(4) << remaining;
        atempoChain += (atempoChain.empty() ? std::string("") : std::string(",")) + "atempo=" + ss.str();
      }
      audioArgs.push_back("-af");
      audioArgs.push_back(atempoChain);
    }
    audioArgs.insert(audioArgs.end(), {
      "-f", "s16le", "-acodec", "pcm_s16le", "-ac", "2", "-ar", "48000", "pipe:1"
    });
    if (spawnPipeProcess(audioProcess_, audioArgs)) {
      int audioFd = audioProcess_.readFd;
      audioThread_ = std::thread([this, cueStartSeconds, audioFd]() {
        std::vector<std::uint8_t> buffer(8192);
        double audioTime = cueStartSeconds;
        while (!decoderStop_.load()) {
          // Backpressure: ~120 ms queued (5760 frames), whatever the stream's
          // channel count — byte thresholds must scale with the open spec.
          if (queuedAudioBytes() > 5760 * audioStreamBytesPerFrame()) {
            SDL_Delay(4);
            continue;
          }

          int bytesRead = readSome(audioFd, buffer.data(), buffer.size());
          if (bytesRead <= 0) {
            break;
          }
          // Align to s16le sample boundary: a stray trailing byte from a short
          // read would size `scaled` too small for the subsequent memcpy and
          // overflow by one byte. Drop the stray byte rather than paper over
          // it — the decoder pipe will deliver it on the next iteration.
          size_t alignedBytes = static_cast<size_t>(bytesRead) & ~size_t{1};
          if (alignedBytes == 0) {
            continue;
          }

          std::vector<std::int16_t> scaled(alignedBytes / sizeof(std::int16_t));
          std::memcpy(scaled.data(), buffer.data(), alignedBytes);
          applyGainAndQueueAudio(scaled, audioTime);
        }
      });
    }
  }
}

// Shared audio-thread tail (CLI pipe thread + in-process thread): per-sample
// fade/volume/master gain, waveform tap, queue to the SDL stream, advance the
// audio-clock counters. Keeping one implementation keeps the audio-master
// A/V clock semantics identical across both decode paths.
void MediaEngine::applyGainAndQueueAudio(std::vector<std::int16_t>& scaled, double& audioTime) {
  const double cueGain = audioCueGain_.load(std::memory_order_relaxed);
  const double pan = static_cast<double>(audioCuePan_.load(std::memory_order_relaxed));
  const bool mono = audioCueMono_.load(std::memory_order_relaxed);
  // Balance law: panning right attenuates left, and vice versa.
  const double panL = pan > 0.0 ? 1.0 - pan : 1.0;
  const double panR = pan < 0.0 ? 1.0 + pan : 1.0;
  auto clip = [](double v) {
    return static_cast<std::int16_t>(std::clamp(
      static_cast<int>(std::lround(v)), -32768, 32767));
  };
  const std::size_t frames = scaled.size() / 2;
  limiterScratch_.resize(frames * 2);
  limiterFramePeak_.resize(frames);
  // Stage 1: gain / mono / pan into a float scratch, recording the per-frame
  // peak the limiter needs. Quantising here (as this loop used to) would throw
  // away the overshoot the limiter exists to catch.
  for (std::size_t f = 0; f < frames; ++f) {
    const std::size_t index = f * 2;
    double gain = static_cast<double>(volume_.load())
                * static_cast<double>(masterGain_.load())
                * cueGain
                * audioFadeGainAt(audioTime);
    double left = static_cast<double>(scaled[index]);
    double right = static_cast<double>(scaled[index + 1]);
    if (mono) {
      left = right = (left + right) * 0.5;
    }
    left *= gain * panL;
    right *= gain * panR;
    limiterScratch_[index] = left;
    limiterScratch_[index + 1] = right;
    limiterFramePeak_[f] = std::max(std::fabs(left), std::fabs(right));
    audioTime += 1.0 / 48000.0;
  }
  // Stage 2: hold the peaks under the ceiling by reducing gain, not by
  // truncating the waveform.
  applyPeakLimiter();
  // Stage 3: quantise. The hard clamp stays as the last-resort safety net —
  // the limiter should mean it never actually binds.
  for (std::size_t i = 0; i < frames * 2; ++i) {
    scaled[i] = clip(limiterScratch_[i]);
  }
  // The A/V master clock counts frames at PROCESS time, before the delay
  // line: video must anchor to the undelayed timeline so the configured
  // audio delay produces a real skew at the device (audio late vs video)
  // instead of dragging video along with it.
  audioFramesQueued_.fetch_add(scaled.size() / 2, std::memory_order_relaxed);
  queueDelayedAudio(scaled);
}

// ── Peak limiter (v0.81.5) ──────────────────────────────────────────────────
// R128 normalize applies the full loudness gain now, and dialogue-heavy TV/film
// material runs a 18–22 dB peak-to-loudness ratio — so a quiet clip legitimately
// wants +8 to +11 dB and its transients land well over full scale. The old hard
// int16 clamp turned those into square waves. This is the standard broadcast
// answer: gain-reduce the peaks instead of truncating them.
//
// ZERO ADDED LATENCY. The whole chunk is already in hand, so the "look-ahead"
// costs nothing: the target gain at frame f is the minimum required gain over
// the next kLimiterLookaheadFrames, so the ramp is already on its way down
// before the peak arrives. Attack ramps over roughly that same window; release
// is slow enough to stay inaudible under speech. The residual overshoot from
// the exponential attack is what the −1 dBFS ceiling (rather than 0) absorbs.
//
// Operates on limiterScratch_/limiterFramePeak_, filled by the gain stage.
void MediaEngine::applyPeakLimiter() {
  constexpr int kLookaheadFrames = 72;         // 1.5 ms @ 48 kHz
  constexpr double kAttackFrames = 24.0;       // ~0.5 ms time constant
  constexpr double kReleaseFrames = 7200.0;    // 150 ms
  // −1 dBFS of full scale, in the int16 units the scratch buffer carries.
  constexpr double kCeiling = 32767.0 * 0.891250938133746;

  const std::size_t frames = limiterFramePeak_.size();
  if (frames == 0) {
    return;
  }
  const double attackCoef = 1.0 - std::exp(-1.0 / kAttackFrames);
  const double releaseCoef = 1.0 - std::exp(-1.0 / kReleaseFrames);

  auto required = [this](std::size_t f) {
    const double peak = limiterFramePeak_[f];
    return peak > kCeiling ? kCeiling / peak : 1.0;
  };

  // Monotonic deque of frame indices with non-decreasing required gain, so the
  // front is always the minimum over the live window.
  limiterWindow_.clear();
  std::size_t next = 0;
  for (std::size_t f = 0; f < frames; ++f) {
    const std::size_t windowEnd =
      std::min(frames, f + static_cast<std::size_t>(kLookaheadFrames));
    while (next < windowEnd) {
      const double r = required(next);
      while (!limiterWindow_.empty() && required(limiterWindow_.back()) >= r) {
        limiterWindow_.pop_back();
      }
      limiterWindow_.push_back(next);
      ++next;
    }
    while (!limiterWindow_.empty() && limiterWindow_.front() < f) {
      limiterWindow_.pop_front();
    }
    const double target = limiterWindow_.empty() ? 1.0 : required(limiterWindow_.front());
    const double coef = target < limiterGain_ ? attackCoef : releaseCoef;
    limiterGain_ += (target - limiterGain_) * coef;
    limiterScratch_[f * 2] *= limiterGain_;
    limiterScratch_[f * 2 + 1] *= limiterGain_;
  }
}

// Final audio stage shared by decode audio and the sync pop: hold samples in
// the delay FIFO so audio reaches the device Project::audioDelayMs late —
// the knob that lines Deckboy up with lagging displays/PA DSP. The tap sees
// the DELAYED stream so VU meters match what the room hears.
void MediaEngine::queueDelayedAudio(std::vector<std::int16_t>& samples) {
  const std::size_t holdValues =
    static_cast<std::size_t>(audioDelayMs_.load(std::memory_order_relaxed)) * 48u * 2u;
  if (holdValues == 0 && audioDelayFifo_.empty()) {
    if (audioTap_) {
      audioTap_(samples);
    }
    putAudioToStream(samples);
    return;
  }
  audioDelayFifo_.insert(audioDelayFifo_.end(), samples.begin(), samples.end());
  if (audioDelayFifo_.size() <= holdValues) {
    return;  // still filling the delay line
  }
  std::size_t emitCount = audioDelayFifo_.size() - holdValues;
  emitCount -= emitCount % 2;  // keep stereo pairs intact
  std::vector<std::int16_t> emit(audioDelayFifo_.begin(),
                                 audioDelayFifo_.begin() + static_cast<std::ptrdiff_t>(emitCount));
  audioDelayFifo_.erase(audioDelayFifo_.begin(),
                        audioDelayFifo_.begin() + static_cast<std::ptrdiff_t>(emitCount));
  if (audioTap_) {
    audioTap_(emit);
  }
  putAudioToStream(emit);
}

// Expand processed stereo onto the cue's output pair. The engine pipeline —
// decode, gain/pan/mono, fades, delay FIFO, VU tap — is stereo throughout;
// only this last write knows the stream is wider. Channels outside the pair
// carry silence. A pair beyond the opened channel count clamps to outs 1-2
// so a routed cue is never silently dropped.
void MediaEngine::setExternalAudioSink(ExternalAudioSink sink) {
  // Same lock as every other audio-thread touch of the output, so arming or
  // clearing a sink mid-show cannot race a write already in flight.
  std::lock_guard<std::mutex> lock(audioStreamMutex_);
  externalSink_ = std::move(sink);
}

bool MediaEngine::usingExternalAudioSink() const {
  return static_cast<bool>(externalSink_.write);
}

void MediaEngine::putWideAudioToStream(const std::vector<std::int16_t>& wide,
                                       int channels) {
  if (wide.empty() || channels <= 0) return;
  std::lock_guard<std::mutex> lock(audioStreamMutex_);
  if (externalSink_.write) {
    const std::size_t frames = wide.size() / static_cast<std::size_t>(channels);
    const std::size_t took = externalSink_.write(wide.data(), frames);
    if (took < frames) {
      pendingSinkAudio_.insert(
        pendingSinkAudio_.end(),
        wide.begin() + static_cast<std::ptrdiff_t>(took * static_cast<std::size_t>(channels)),
        wide.end());
    }
    return;
  }
  if (!audioStream_) return;
  SDL_PutAudioStreamData(audioStream_, wide.data(),
                         static_cast<int>(wide.size() * sizeof(std::int16_t)));
}

void MediaEngine::putAudioToStream(const std::vector<std::int16_t>& stereo) {
  // Runs on the audio decode thread. The lock is what makes a device swap or a
  // detach on the main thread safe: the owner cannot free the stream while a
  // write to it is in flight, and after detachAudioDevice() the pointer reads
  // null here instead of dangling.
  std::lock_guard<std::mutex> lock(audioStreamMutex_);
  // An armed external sink takes precedence over SDL. Checked FIRST so an ASIO
  // device works even when no SDL stream was ever opened -- otherwise the early
  // return below would silently discard every sample.
  if (externalSink_.write) {
    const int sinkChannels = std::max(2, externalSink_.channels);
    const std::size_t frames = stereo.size() / 2;
    std::vector<std::int16_t> wide(frames * static_cast<std::size_t>(sinkChannels), 0);
    int pair = audioCuePairOffset_.load(std::memory_order_relaxed);
    if (pair < 0 || pair * 2 + 1 >= sinkChannels) pair = 0;
    const std::size_t off = static_cast<std::size_t>(pair) * 2;
    for (std::size_t f = 0; f < frames; ++f) {
      std::int16_t* out = wide.data() + f * static_cast<std::size_t>(sinkChannels);
      out[off] = stereo[f * 2];
      out[off + 1] = stereo[f * 2 + 1];
    }
    // NEVER BLOCK HERE. This runs on the decode thread while holding the audio
    // lock, so sleeping to wait for ring space would stall any main-thread
    // device swap or detach behind it for as long as the wait -- textbook
    // priority inversion, and in a live tool it presents as a freeze.
    //
    // Instead: flush whatever was left over last time, write what fits now,
    // and carry the remainder. Nothing is dropped and nothing waits, because
    // the decode threads already pace themselves against queuedAudioBytes(),
    // which reports the sink's own queue.
    if (!pendingSinkAudio_.empty()) {
      const std::size_t pendFrames =
        pendingSinkAudio_.size() / static_cast<std::size_t>(sinkChannels);
      const std::size_t took = externalSink_.write(pendingSinkAudio_.data(), pendFrames);
      if (took >= pendFrames) {
        pendingSinkAudio_.clear();
      } else {
        pendingSinkAudio_.erase(
          pendingSinkAudio_.begin(),
          pendingSinkAudio_.begin() +
            static_cast<std::ptrdiff_t>(took * static_cast<std::size_t>(sinkChannels)));
        // Still backed up: queue this block behind the backlog rather than
        // letting it overtake and play out of order.
        pendingSinkAudio_.insert(pendingSinkAudio_.end(), wide.begin(), wide.end());
        return;
      }
    }
    const std::size_t took = externalSink_.write(wide.data(), frames);
    if (took < frames) {
      pendingSinkAudio_.assign(
        wide.begin() + static_cast<std::ptrdiff_t>(took * static_cast<std::size_t>(sinkChannels)),
        wide.end());
    }
    return;
  }
  if (!audioStream_) {
    return;
  }
  const int deviceChannels = audioDeviceChannels_.load(std::memory_order_relaxed);
  if (deviceChannels <= 2) {
    SDL_PutAudioStreamData(audioStream_, stereo.data(),
                           static_cast<int>(stereo.size() * sizeof(std::int16_t)));
    return;
  }
  int pair = audioCuePairOffset_.load(std::memory_order_relaxed);
  if (pair < 0 || pair * 2 + 1 >= deviceChannels) {
    pair = 0;
  }
  const std::size_t offset = static_cast<std::size_t>(pair) * 2;
  const std::size_t frames = stereo.size() / 2;
  std::vector<std::int16_t> wide(frames * static_cast<std::size_t>(deviceChannels), 0);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    std::int16_t* out = wide.data() + frame * static_cast<std::size_t>(deviceChannels);
    out[offset] = stereo[frame * 2];
    out[offset + 1] = stereo[frame * 2 + 1];
  }
  SDL_PutAudioStreamData(audioStream_, wide.data(),
                         static_cast<int>(wide.size() * sizeof(std::int16_t)));
}

#if DECKBOY_INPROC_DECODE
// ---------------------------------------------------------------------------
// startInprocDecoders — In-process libav decode (GPU_DECODE_PLAN §11).
//
// Replaces the two ffmpeg subprocess pipes with two in-process pipelines on
// the same thread structure: the video thread pushes into frameQueue_ under
// the same kMaxVideoFrames backpressure and frame indexing, the audio thread
// feeds the same gain/tap/queue tail — transport, A/V clock, EOF and loop
// semantics are unchanged.
//
// Zero-copy: when the cue takes the NV12 fast path and the app supplied a
// decode-device provider (deck engines), video decodes via d3d11va directly
// on the program output renderer's D3D11 device and frames stay GPU-resident
// (DecodedFrame::gpu*). Otherwise frames arrive as classic CPU pixels.
//
// Returns false → caller falls through to the CLI pipe path (rotated files,
// undecodable first frame, open failure).
// ---------------------------------------------------------------------------
bool MediaEngine::startInprocDecoders(const Cue& cue, const std::string& mediaPath,
                                      double mediaStartSeconds, double cueStartSeconds,
                                      int decodeW, int decodeH, FramePixelFormat decodeFormat,
                                      double speed) {
  const bool wantVideo = cue.kind != CueKind::Audio;
  if (wantVideo) {
    deckboy::libav::VideoOpenParams videoParams;
    videoParams.path = mediaPath;
    videoParams.startSeconds = mediaStartSeconds;
    videoParams.targetWidth = decodeW;
    videoParams.targetHeight = decodeH;
    videoParams.format = decodeFormat;
    // Withhold keyframes at decode; also forces software decode (see
    // VideoOpenParams::datamosh).
    videoParams.datamosh = datamoshActiveForCue(cue);
    if (decodeFormat == FramePixelFormat::NV12 && decodeDeviceProvider_ && !videoParams.datamosh) {
      videoParams.d3dDevice = decodeDeviceProvider_();
    }
    videoPipeline_ = std::make_unique<deckboy::libav::VideoPipeline>();
    if (!videoPipeline_->open(videoParams)) {
      videoPipeline_.reset();
      return false;
    }
    activeDecodeDevice_ = videoPipeline_->device();
  } else {
    // Audio-only cue: no video stream — mirrors the CLI video pipe's
    // immediate EOF so end-of-playback detection still keys off audio.
    decoderEof_ = true;
  }

  inprocDecodeActive_ = true;
  decodersRunning_ = true;
  decodeStallLatched_ = false;
  lastFramePushMs_.store(SDL_GetTicks());

  if (wantVideo) {
    videoThread_ = std::thread([this, cueStartSeconds]() {
      std::uint64_t frameIndex = static_cast<std::uint64_t>(std::floor(cueStartSeconds * frameRate_));
      while (!decoderStop_.load()) {
        while (!decoderStop_.load()) {
          bool hasRoom = false;
          {
            std::lock_guard<std::mutex> lock(frameMutex_);
            hasRoom = frameQueue_.size() < kMaxVideoFrames;
          }
          if (hasRoom) {
            break;
          }
          SDL_Delay(4);
        }
        if (decoderStop_.load()) {
          break;
        }
        DecodedFrame frame;
        if (!videoPipeline_->nextFrame(frame)) {
          break;
        }
        // Index by the frame's real presentation time when we have it, so
        // telecined / variable-rate video (3:2-pulldown DVD MPEG-2, VFR phone
        // clips) schedules against the audio clock by its actual timestamps
        // instead of a constant-fps counter that drifts. Kept strictly
        // increasing; falls back to the sequential counter when PTS is absent.
        if (frame.presentationSeconds >= 0.0 && frameRate_ > 0.0) {
          std::uint64_t ptsIndex = static_cast<std::uint64_t>(
            std::llround(frame.presentationSeconds * frameRate_));
          frame.index = std::max(ptsIndex, frameIndex);
          frameIndex = frame.index + 1;
        } else {
          frame.index = frameIndex++;
        }
        lastFramePushMs_.store(SDL_GetTicks());
        std::lock_guard<std::mutex> lock(frameMutex_);
        frameQueue_.push_back(std::move(frame));
      }
      decoderEof_ = true;
    });
  }

  audioClockValid_ = false;
  if (audioStream_ != nullptr && cue.hasAudio && cue.audioEnabled) {
    deckboy::libav::AudioOpenParams audioParams;
    audioParams.path = mediaPath;
    audioParams.startSeconds = mediaStartSeconds;
    audioParams.speed = speed;
    audioPipeline_ = std::make_unique<deckboy::libav::AudioPipeline>();
    if (!audioPipeline_->open(audioParams)) {
      audioPipeline_.reset();  // run silent — same as a failed CLI spawn
    } else {
      syncAudioFadeParams();  // publish before the audio thread spawns
      audioFramesQueued_.store(0, std::memory_order_relaxed);
      audioClockStartSeconds_ = cueStartSeconds;
      lastAudioClockSeconds_ = -1.0;
      lastAudioClockAdvanceMs_ = 0;
      audioClockValid_ = true;
      audioThread_ = std::thread([this, cueStartSeconds]() {
        std::vector<std::int16_t> samples(4096);
        double audioTime = cueStartSeconds;
        while (!decoderStop_.load()) {
          // ~120 ms queued (5760 frames) at the stream's channel count.
          if (queuedAudioBytes() > 5760 * audioStreamBytesPerFrame()) {
            SDL_Delay(4);
            continue;
          }
          int got = audioPipeline_->read(samples.data(), static_cast<int>(samples.size()));
          if (got <= 0) {
            break;
          }
          std::vector<std::int16_t> scaled(samples.begin(), samples.begin() + got);
          applyGainAndQueueAudio(scaled, audioTime);
        }
      });
    }
  }
  return true;
}
#endif // DECKBOY_INPROC_DECODE

// ---------------------------------------------------------------------------
// decodeSingleFrame — Synchronous single-frame decode for thumbnail generation.
//
// Unlike loadStillFrame (async, uses imageThread_), this blocks until the frame
// is decoded and returns it directly. Used by the thumbnail pipeline (cue list
// preview images) which can afford to block since it runs on a background thread.
//
// The caller provides the ChildProcess handle to use — this allows the thumbnail
// generator to manage multiple concurrent decodes with separate process lifetimes.
//
// Seeks to `seconds` if > 0 (for video thumbnails at a specific timecode).
// Dimensions are clamped to output resolution to avoid wasting decode time.
// ---------------------------------------------------------------------------
std::optional<DecodedFrame> MediaEngine::decodeSingleFrame(ChildProcess& process, const std::string& path, int width, int height, double seconds) {
  auto [capW, capH] = currentOutputSizeHint();
  int w = width > 0 ? width : capW;
  int h = height > 0 ? height : capH;
  // Clamp to output resolution — thumbnails don't need to be larger
  if (w > capW || h > capH) {
    double scale = std::min(
      static_cast<double>(capW) / w,
      static_cast<double>(capH) / h
    );
    w = std::max(1, static_cast<int>(w * scale));
    h = std::max(1, static_cast<int>(h * scale));
  }
  std::vector<std::string> args {
    "ffmpeg",
    "-hide_banner",
    "-loglevel",
    "error"
  };
  if (seconds > 0.0) {
    args.push_back("-ss");
    args.push_back(std::to_string(seconds));
  }
  args.insert(args.end(), {
    "-i",
    path,
    "-frames:v",
    "1",
    "-vf",
    "scale=" + std::to_string(w) + ":" + std::to_string(h) + ":flags=neighbor",
    "-f",
    "rawvideo",
    "-pix_fmt",
    "rgba",
    "pipe:1"
  });

  if (!spawnPipeProcess(process, args)) {
    return std::nullopt;
  }

  // Blocking read of the single decoded frame
  const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
  DecodedFrame frame;
  frame.width = w;
  frame.height = h;
  frame.index = 0;
  frame.pixels.resize(frameBytes);
  bool ok = readExact(process.readFd, frame.pixels.data(), frameBytes);
  process.stop();
  if (!ok) {
    return std::nullopt;
  }
  return frame;
}

// ── Static pattern builders ─────────────────────────────────────────────────
// These static methods generate procedural test patterns directly into
// DecodedFrame pixel buffers. They're CPU-rendered — no GPU shaders or
// ffmpeg involved. Used by buildPatternFrame() and rebuildPatternFrame().

// Write a single RGBA pixel at (x, y) into a DecodedFrame's pixel buffer.
// Bounds-checked to silently ignore out-of-range coordinates (safe for
// procedural drawing where shapes may extend past frame edges).
void MediaEngine::writePixel(DecodedFrame& frame, int x, int y, SDL_Color color) {
  if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
    return;
  }
  size_t offset = static_cast<size_t>(y * frame.width + x) * 4u;
  frame.pixels[offset + 0] = color.r;
  frame.pixels[offset + 1] = color.g;
  frame.pixels[offset + 2] = color.b;
  frame.pixels[offset + 3] = color.a;
}

// Fill a rectangular region with a solid color. Clamps to frame bounds so
// callers don't need to worry about edge clipping. Used extensively by all
// pattern builders to draw bars, blocks, and filled shapes.
void MediaEngine::fillPixelRect(DecodedFrame& frame, int x, int y, int w, int h, SDL_Color color) {
  for (int py = std::max(0, y); py < std::min(frame.height, y + h); ++py) {
    for (int px = std::max(0, x); px < std::min(frame.width, x + w); ++px) {
      writePixel(frame, px, py, color);
    }
  }
}

// Draw a filled heart shape using the implicit heart curve equation:
//   (x² + y² - 1)³ - x²y³ ≤ 0
// The radius parameter scales the heart. Used by the pocket-test pattern
// as a decorative element. The equation produces a mathematically perfect
// heart shape that fills solidly (no outline-only mode).
void MediaEngine::drawHeart(DecodedFrame& frame, int centerX, int centerY, int radius, SDL_Color color) {
  for (int y = -radius * 2; y <= radius * 2; ++y) {
    for (int x = -radius * 2; x <= radius * 2; ++x) {
      double fx = static_cast<double>(x) / static_cast<double>(radius);
      double fy = static_cast<double>(y) / static_cast<double>(radius);
      double equation = std::pow(fx * fx + fy * fy - 1.0, 3.0) - fx * fx * std::pow(fy, 3.0);
      if (equation <= 0.0) {
        writePixel(frame, centerX + x, centerY + y, color);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// buildSmpte75Bars — Generate SMPTE 75% color bar test pattern.
//
// Layout (standard broadcast test pattern):
//   Top 2/3:    7 vertical bars at 75% intensity
//               (gray, yellow, cyan, green, magenta, red, blue)
//   Middle 1/12: complementary/reversed bar strips for alignment checking
//   Bottom 1/4:  4 strips (black, white, near-black, super-black) for
//                monitor setup — used to calibrate brightness/contrast
//
// Small colored rectangles at the bottom of each top bar act as label markers
// (contrasting color so they're visible against the bar).
// ---------------------------------------------------------------------------
void MediaEngine::buildSmpte75Bars(DecodedFrame& frame) {
  int W = frame.width, H = frame.height;
  struct Bar { Uint8 r, g, b; };
  // SMPTE 75% bars: each channel is either 0 or 191 (75% of 255)
  constexpr std::array<Bar, 7> bars {{
    {191,191,191},  // gray
    {191,191,  0},  // yellow
    {  0,191,191},  // cyan
    {  0,191,  0},  // green
    {191,  0,191},  // magenta
    {191,  0,  0},  // red
    {  0,  0,191},  // blue
  }};
  int topH   = H * 2 / 3;    // top section: main color bars
  int midH   = H / 12;       // middle section: complementary strips
  int botH   = H - topH - midH;  // bottom section: grayscale setup
  int barW   = W / 7;
  // Draw 7 main color bars (last bar extends to fill remaining width)
  for (int i = 0; i < 7; ++i) {
    SDL_Color c {bars[i].r, bars[i].g, bars[i].b, 255};
    fillPixelRect(frame, i * barW, 0, barW + (i == 6 ? W - 6 * barW : 0), topH, c);
  }
  // Middle strips: complementary pattern for color alignment verification
  constexpr std::array<Bar, 7> midBars {{
    {  0,191,191},  // cyan
    {  0,  0,  0},  // black
    {191,  0,191},  // magenta
    {  0,  0,  0},  // black
    {191,191,191},  // gray
    {  0,  0,  0},  // black
    {  0,  0,191},  // blue
  }};
  for (int i = 0; i < 7; ++i) {
    SDL_Color c {midBars[i].r, midBars[i].g, midBars[i].b, 255};
    fillPixelRect(frame, i * barW, topH, barW + (i == 6 ? W - 6 * barW : 0), midH, c);
  }
  // Bottom: grayscale strips for brightness/contrast calibration
  int botBarW = W / 4;
  fillPixelRect(frame, 0,          topH + midH, botBarW, botH, {  0,  0,  0, 255});  // black
  fillPixelRect(frame, botBarW,    topH + midH, botBarW, botH, {255,255,255, 255});  // white
  fillPixelRect(frame, botBarW*2,  topH + midH, botBarW, botH, { 10, 10, 10, 255});  // near-black
  fillPixelRect(frame, botBarW*3,  topH + midH, W - botBarW*3, botH, { 4,  4,  4, 255});  // super-black
  // Label markers at the bottom of each top bar (contrasting color)
  for (int i = 0; i < 7; ++i) {
    SDL_Color label {bars[i].r > 100 ? Uint8(0) : Uint8(230),
                     bars[i].g > 100 ? Uint8(0) : Uint8(230),
                     bars[i].b > 100 ? Uint8(0) : Uint8(230), 255};
    fillPixelRect(frame, i * barW + 2, topH - 14, barW - 4, 10, label);
  }
}

// ---------------------------------------------------------------------------
// buildCrosshatch — Generate a crosshatch grid test pattern.
//
// White grid lines on black background at 64px intervals, plus:
//   - Red crosshair at the exact center (for alignment)
//   - Green safe-area rectangle at 10% inset (title-safe zone guide)
//
// phaseX/phaseY shift the grid for the -motion animated variant.
// ---------------------------------------------------------------------------
void MediaEngine::buildCrosshatch(DecodedFrame& frame, int phaseX, int phaseY) {
  int W = frame.width;
  int H = frame.height;
  constexpr int kStep = 64;  // grid spacing in pixels
  // Wrap phase to grid period to prevent drift accumulation
  int shiftX = ((phaseX % kStep) + kStep) % kStep;
  int shiftY = ((phaseY % kStep) + kStep) % kStep;
  // Black background
  fillPixelRect(frame, 0, 0, W, H, {0, 0, 0, 255});
  // Grid anchored to the CENTER so lines coincide with the red crosshair at
  // every raster (the old 0,0-anchored grid only lined up when W/2 and H/2
  // happened to be multiples of the step — at 1920x1080 the horizontal
  // center line floated 28px off the grid).
  int firstX = ((W / 2 - 1 - shiftX) % kStep + kStep) % kStep - kStep;
  for (int x = firstX; x < W; x += kStep) {
    fillPixelRect(frame, x, 0, 2, H, {255, 255, 255, 255});
  }
  int firstY = ((H / 2 - 1 - shiftY) % kStep + kStep) % kStep - kStep;
  for (int y = firstY; y < H; y += kStep) {
    fillPixelRect(frame, 0, y, W, 2, {255, 255, 255, 255});
  }
  // Red center crosshair (alignment reference)
  fillPixelRect(frame, W / 2 - 1, 0,     2, H, {220,  40,  40, 255});
  fillPixelRect(frame, 0,     H / 2 - 1, W, 2, {220,  40,  40, 255});
  // Green safe-area rectangle at 10% inset (broadcast title-safe zone)
  int sx = W / 10;
  int sy = H / 10;
  fillPixelRect(frame, sx, sy, W - sx * 2, 2, {60, 180, 60, 200});
  fillPixelRect(frame, sx, sy, 2, H - sy * 2, {60, 180, 60, 200});
  fillPixelRect(frame, W - sx - 2, sy, 2, H - sy * 2, {60, 180, 60, 200});
  fillPixelRect(frame, sx, H - sy - 2, W - sx * 2, 2, {60, 180, 60, 200});
}

// ---------------------------------------------------------------------------
// buildCheckerboard — Generate a black-and-white checkerboard test pattern.
//
// 64px cells in a standard checkerboard layout. phaseX/phaseY scroll the
// pattern for the -motion animated variant. cellOffsetX/Y track which
// cell-parity we're in so the checkerboard pattern remains consistent
// as it scrolls (otherwise the phase shift would flip black/white cells).
// ---------------------------------------------------------------------------
void MediaEngine::buildCheckerboard(DecodedFrame& frame, int phaseX, int phaseY) {
  int W = frame.width;
  int H = frame.height;
  constexpr int cell = 64;
  int shiftX = ((phaseX % cell) + cell) % cell;
  int shiftY = ((phaseY % cell) + cell) % cell;
  // Track cell-level offset so parity is maintained during scrolling
  int cellOffsetX = (phaseX - shiftX) / cell;
  int cellOffsetY = (phaseY - shiftY) / cell;
  for (int row = 0;; ++row) {
    int y = row * cell - shiftY;
    if (y >= H) {
      break;
    }
    int y0 = std::max(0, y);
    int y1 = std::min(H, y + cell);
    if (y1 <= y0) {
      continue;
    }
    for (int col = 0;; ++col) {
      int x = col * cell - shiftX;
      if (x >= W) {
        break;
      }
      int x0 = std::max(0, x);
      int x1 = std::min(W, x + cell);
      if (x1 <= x0) {
        continue;
      }
      bool white = ((row + col + cellOffsetX + cellOffsetY) % 2) == 0;
      SDL_Color c = white ? SDL_Color{255, 255, 255, 255}
                          : SDL_Color{0, 0, 0, 255};
      fillPixelRect(frame, x0, y0, x1 - x0, y1 - y0, c);
    }
  }
}

// ---------------------------------------------------------------------------
// buildPocketTest — Generate animated pixel-art island scene.
//
// This is the signature "pocket test" pattern — a charming animated beach
// scene rendered entirely in code with pixel-art aesthetics. It cycles
// through 4 time-of-day scenes (14 seconds each):
//   Scene 0: Day      — bright blue sky, yellow sun, full color
//   Scene 1: Sunset   — orange/pink sky, warm lighting
//   Scene 2: Night    — dark blue sky, moon with craters, twinkling stars
//   Scene 3: Storm    — grey-teal sky, lightning bolts, rain streaks
//
// Scene elements (all drawn with rect/disc primitives):
//   - Sky gradient with sun/moon and clouds
//   - Ocean with animated wave crests
//   - Island with vegetation
//   - Beach with sand texture and surf line
//   - Palm trees with animated sway
//   - Brick platforms and pipes (retro game homage)
//   - Animated characters: hero, crab, turtle, dinosaur, parrot, fish, puff friend
//   - Collectible coins
//   - Rainbow bar strip at the bottom (audio visualizer homage)
//   - Stars and lightning (night/storm scenes)
//
// The time parameter t drives all animation (position, phase, scene selection).
// forcedScene overrides the auto-cycling scene index (for pocket-day, etc.).
// All coordinates are proportional to frame dimensions for resolution independence.
// ---------------------------------------------------------------------------
void MediaEngine::buildPocketTest(DecodedFrame& frame, double t, int forcedScene) {
  const int W = frame.width;
  const int H = frame.height;

  // Local drawing helpers — capture W, H, and frame by reference.
  // These avoid the overhead of calling the class-level writePixel/fillPixelRect
  // methods (which do member lookups) for the tight inner loops of the scene.

  // Single pixel write with bounds check
  auto put = [&](int x, int y, const SDL_Color& color) {
    if (x < 0 || y < 0 || x >= W || y >= H) {
      return;
    }
    size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)) * 4u;
    frame.pixels[idx + 0] = color.r;
    frame.pixels[idx + 1] = color.g;
    frame.pixels[idx + 2] = color.b;
    frame.pixels[idx + 3] = color.a;
  };

  // Filled rectangle with bounds clamping
  auto rect = [&](int x, int y, int w, int h, const SDL_Color& color) {
    if (w <= 0 || h <= 0) {
      return;
    }
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(W, x + w);
    int y1 = std::min(H, y + h);
    for (int yy = y0; yy < y1; ++yy) {
      for (int xx = x0; xx < x1; ++xx) {
        put(xx, yy, color);
      }
    }
  };

  // Linear color interpolation (for sky/ocean/sand gradients)
  auto lerpColor = [&](const SDL_Color& a, const SDL_Color& b, double v) -> SDL_Color {
    double tClamped = std::clamp(v, 0.0, 1.0);
    auto mix = [&](Uint8 aa, Uint8 bb) -> Uint8 {
      return static_cast<Uint8>(std::round(static_cast<double>(aa) * (1.0 - tClamped) + static_cast<double>(bb) * tClamped));
    };
    return SDL_Color {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), 255};
  };

  // Filled circle (for sun, moon, coins, character bodies)
  auto disc = [&](int cx, int cy, int radius, const SDL_Color& color) {
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (dx * dx + dy * dy <= r2) {
          put(cx + dx, cy + dy, color);
        }
      }
    }
  };

  // Scene layout: proportional vertical zones
  const int oceanTop = H * 52 / 100;   // sky ends / ocean starts at 52%
  const int beachTop = H * 83 / 100;   // ocean ends / beach starts at 83%

  // Scene selection: cycles 0→1→2→3 every 14 seconds, or forced for pocket-day/sunset/night/storm
  int scene = forcedScene;
  if (scene < 0 || scene > 3) {
    scene = static_cast<int>(std::floor(t / 14.0)) % 4;
    if (scene < 0) {
      scene += 4;
    }
  }

  // Scene 0 (Day) color palette — defaults, overridden per-scene below
  SDL_Color skyTop {60, 170, 225, 255};
  SDL_Color skyBottom {190, 245, 255, 255};
  SDL_Color oceanNear {15, 95, 170, 255};
  SDL_Color oceanFar {25, 130, 195, 255};
  SDL_Color sandTop {248, 226, 154, 255};
  SDL_Color sandBottom {226, 186, 108, 255};
  SDL_Color sunCore {255, 241, 150, 255};
  SDL_Color sunGlow {255, 226, 120, 120};
  SDL_Color cloudMain {245, 255, 255, 255};
  SDL_Color cloudShadow {205, 235, 245, 255};
  bool drawMoon = false;

  // Scene 1 (Sunset): warm orange/pink palette
  if (scene == 1) {
    skyTop = SDL_Color {246, 134, 98, 255};
    skyBottom = SDL_Color {255, 205, 142, 255};
    oceanNear = SDL_Color {40, 82, 156, 255};
    oceanFar = SDL_Color {76, 122, 198, 255};
    sandTop = SDL_Color {255, 214, 144, 255};
    sandBottom = SDL_Color {234, 172, 108, 255};
    sunCore = SDL_Color {255, 216, 114, 255};
    sunGlow = SDL_Color {255, 145, 92, 140};
    cloudMain = SDL_Color {255, 235, 224, 255};
    cloudShadow = SDL_Color {236, 193, 183, 255};
  // Scene 2 (Night): deep blue palette, moon with craters, stars
  } else if (scene == 2) {
    skyTop = SDL_Color {24, 38, 102, 255};
    skyBottom = SDL_Color {92, 136, 206, 255};
    oceanNear = SDL_Color {10, 50, 110, 255};
    oceanFar = SDL_Color {25, 76, 150, 255};
    sandTop = SDL_Color {164, 148, 115, 255};
    sandBottom = SDL_Color {132, 114, 90, 255};
    sunCore = SDL_Color {235, 242, 255, 255};
    sunGlow = SDL_Color {170, 198, 255, 120};
    cloudMain = SDL_Color {170, 188, 240, 255};
    cloudShadow = SDL_Color {120, 136, 188, 255};
    drawMoon = true;
  // Scene 3 (Storm): grey-teal palette, extra clouds, rain + lightning
  } else if (scene == 3) {
    skyTop = SDL_Color {34, 78, 108, 255};
    skyBottom = SDL_Color {102, 166, 180, 255};
    oceanNear = SDL_Color {22, 86, 124, 255};
    oceanFar = SDL_Color {38, 122, 156, 255};
    sandTop = SDL_Color {198, 184, 142, 255};
    sandBottom = SDL_Color {164, 142, 108, 255};
    sunCore = SDL_Color {230, 236, 245, 255};
    sunGlow = SDL_Color {184, 206, 238, 124};
    cloudMain = SDL_Color {186, 216, 224, 255};
    cloudShadow = SDL_Color {140, 168, 176, 255};
    drawMoon = true;
  }

  // ── Draw sky gradient (top → ocean boundary) ──
  for (int y = 0; y < oceanTop; ++y) {
    double v = oceanTop > 1 ? static_cast<double>(y) / static_cast<double>(oceanTop - 1) : 0.0;
    rect(0, y, W, 1, lerpColor(skyTop, skyBottom, v));
  }

  // ── Stars (night and storm scenes only) ──
  if (scene >= 2) {
    int starCount = std::max(18, W / 54);
    for (int i = 0; i < starCount; ++i) {
      int sx = (i * 73 + 19) % std::max(1, W);
      int sy = 4 + ((i * 47 + 13) % std::max(8, oceanTop - 10));
      double twinkle = 0.5 + 0.5 * std::sin(t * 2.8 + static_cast<double>(i) * 0.71);
      Uint8 a = static_cast<Uint8>(120 + twinkle * 120.0);
      rect(sx, sy, 1, 1, SDL_Color {236, 242, 255, a});
      if ((i % 3) == 0) {
        rect(sx - 1, sy, 3, 1, SDL_Color {210, 226, 255, static_cast<Uint8>(a / 2)});
      }
    }
  }

  // ── Sun / moon (gently bobs with sine/cosine oscillation) ──
  int sunX = static_cast<int>(W * 0.78 + std::sin(t * 0.14) * (W * 0.04));
  int sunY = static_cast<int>(H * 0.17 + std::cos(t * 0.11) * (H * 0.03));
  int sunR = std::max(12, H / 16);
  disc(sunX, sunY, sunR + 6, sunGlow);
  disc(sunX, sunY, sunR, sunCore);
  if (drawMoon) {
    disc(sunX + sunR / 3, sunY - sunR / 4, std::max(5, sunR / 2), skyTop);
    disc(sunX - sunR / 3, sunY + sunR / 5, std::max(2, sunR / 8), SDL_Color {210, 216, 226, 200});
    disc(sunX, sunY - sunR / 6, std::max(2, sunR / 10), SDL_Color {190, 202, 214, 200});
  }

  // ── Clouds (3 overlapping discs + shadow strip, scrolling horizontally) ──
  auto drawCloud = [&](int x, int y, int scale) {
    rect(x + scale, y + scale * 2, scale * 9, scale * 2, cloudShadow);
    disc(x + scale * 2, y + scale * 2, scale * 2, cloudMain);
    disc(x + scale * 5, y + scale * 2, scale * 3, cloudMain);
    disc(x + scale * 8, y + scale * 2, scale * 2, cloudMain);
    rect(x + scale * 2, y + scale * 2, scale * 6, scale * 2, cloudMain);
  };

  int cloudCount = scene == 3 ? 6 : (scene >= 2 ? 3 : 4);
  for (int i = 0; i < cloudCount; ++i) {
    int cx = ((i * 280) - static_cast<int>(t * (16.0 + i * 3.0))) % (W + 260) - 180;
    int cy = H / 10 + i * (H / 18);
    drawCloud(cx, cy, std::max(2, H / 120));
  }

  // ── Ocean gradient (far → near, with animated wave crests) ──
  for (int y = oceanTop; y < beachTop; ++y) {
    double v = beachTop > oceanTop + 1
      ? static_cast<double>(y - oceanTop) / static_cast<double>(beachTop - oceanTop - 1)
      : 0.0;
    rect(0, y, W, 1, lerpColor(oceanFar, oceanNear, v));
  }

  for (int x = -8; x < W + 8; x += 6) {
    double waveA = std::sin(static_cast<double>(x) * 0.035 + t * 1.8);
    double waveB = std::sin(static_cast<double>(x) * 0.018 + t * 1.1 + 1.8);
    int y = oceanTop + 14 + static_cast<int>((waveA + waveB) * 4.0);
    rect(x, y, 4, 2, SDL_Color {184, 244, 255, 190});
  }

  // ── Island (parabolic silhouette with vegetation highlights) ──
  int islandCenter = W / 2 + static_cast<int>(std::sin(t * 0.09) * (W * 0.03));
  int islandHalfW = std::max(40, W / 5);
  int islandBaseY = beachTop - 4;
  SDL_Color islandDark {36, 92, 64, 255};
  SDL_Color islandLight {62, 135, 88, 255};
  for (int dx = -islandHalfW; dx <= islandHalfW; ++dx) {
    double u = std::abs(static_cast<double>(dx)) / static_cast<double>(islandHalfW);
    int height = std::max(2, static_cast<int>((1.0 - u * u) * (H * 0.11)));
    rect(islandCenter + dx, islandBaseY - height, 1, height, islandDark);
    if (height > 5 && dx % 3 == 0) {
      rect(islandCenter + dx, islandBaseY - height, 1, 2, islandLight);
    }
  }

  // ── Beach (sand gradient with animated surf line) ──
  for (int y = beachTop; y < H; ++y) {
    double v = H > beachTop + 1
      ? static_cast<double>(y - beachTop) / static_cast<double>(H - beachTop - 1)
      : 0.0;
    SDL_Color sand = lerpColor(sandTop, sandBottom, v);
    rect(0, y, W, 1, sand);
  }
  for (int x = 0; x < W; x += 5) {
    int y = beachTop + 2 + static_cast<int>(std::sin(static_cast<double>(x) * 0.09 + t * 0.8) * 2.0);
    rect(x, y, 3, 1, SDL_Color {255, 240, 186, 170});
  }

  // ── Palm trees (curved trunk + frond cluster, swaying in the wind) ──
  auto drawPalm = [&](int baseX, int baseY, int trunkH, double sway, bool backLayer) {
    SDL_Color trunkA = backLayer ? SDL_Color{96, 72, 44, 255} : SDL_Color{116, 84, 52, 255};
    SDL_Color trunkB = backLayer ? SDL_Color{130, 95, 58, 255} : SDL_Color{148, 108, 65, 255};
    SDL_Color leafA = backLayer ? SDL_Color{38, 118, 70, 255} : SDL_Color{46, 146, 82, 255};
    SDL_Color leafB = backLayer ? SDL_Color{64, 154, 90, 255} : SDL_Color{82, 176, 108, 255};

    int x = baseX;
    for (int i = 0; i < trunkH; ++i) {
      double bend = std::sin(static_cast<double>(i) * 0.18 + sway) * 0.7;
      x = baseX + static_cast<int>(bend * (1.0 + static_cast<double>(i) / static_cast<double>(trunkH)));
      rect(x - 1, baseY - i, 3, 1, (i % 3 == 0) ? trunkB : trunkA);
    }

    int crownX = x;
    int crownY = baseY - trunkH;
    for (int frond = 0; frond < 6; ++frond) {
      double angle = (-1.25 + frond * 0.48) + std::sin(t * 0.9 + frond) * 0.08;
      int len = std::max(12, H / 14) + (frond % 2 == 0 ? 2 : -1);
      for (int step = 0; step < len; ++step) {
        double s = static_cast<double>(step) / static_cast<double>(len);
        int fx = crownX + static_cast<int>(std::cos(angle) * step);
        int fy = crownY + static_cast<int>(std::sin(angle) * step + s * s * 3.0);
        rect(fx, fy, 2, 1, (step % 2 == 0) ? leafA : leafB);
      }
    }
  };

  // Place 4 palm trees: 2 background (smaller) on the island, 2 foreground (larger) on the beach
  drawPalm(W / 2 - W / 7, beachTop + 3, std::max(26, H / 7), t * 0.8 + 0.6, true);
  drawPalm(W / 2 + W / 8, beachTop + 3, std::max(24, H / 8), t * 0.85 + 1.8, true);
  drawPalm(W / 4, H - std::max(16, H / 8), std::max(28, H / 6), t * 0.9 + 0.2, false);
  drawPalm(W * 3 / 4, H - std::max(18, H / 8), std::max(30, H / 6), t * 0.95 + 2.1, false);

  // ── Retro game elements: brick platforms and pipes ──
  int blockSize = std::max(5, H / 34);
  // Brick platform: row of textured blocks (glow in sunset scene)
  auto drawBrickPlatform = [&](int x, int y, int blocks, bool glowing) {
    SDL_Color a = glowing ? SDL_Color {232, 188, 92, 255} : SDL_Color {176, 108, 66, 255};
    SDL_Color b = glowing ? SDL_Color {255, 228, 136, 255} : SDL_Color {214, 140, 84, 255};
    SDL_Color stroke = glowing ? SDL_Color {132, 82, 42, 255} : SDL_Color {104, 62, 36, 255};
    for (int i = 0; i < blocks; ++i) {
      int bx = x + i * blockSize;
      rect(bx, y, blockSize - 1, blockSize - 1, ((i + static_cast<int>(t * 2.0)) & 1) ? a : b);
      rect(bx, y + blockSize / 2, blockSize - 1, 1, stroke);
      rect(bx + blockSize / 2, y, 1, blockSize - 1, stroke);
    }
  };

  // Pipe: vertical tube with lip cap (green = enemy, blue = friendly)
  auto drawPipe = [&](int x, int baseY, int height, bool enemyPipe) {
    int pipeW = std::max(16, blockSize * 3);
    SDL_Color body = enemyPipe ? SDL_Color {80, 188, 98, 255} : SDL_Color {70, 168, 208, 255};
    SDL_Color lip = enemyPipe ? SDL_Color {122, 236, 128, 255} : SDL_Color {118, 218, 252, 255};
    SDL_Color dark = enemyPipe ? SDL_Color {38, 110, 56, 255} : SDL_Color {30, 108, 142, 255};
    rect(x, baseY - height, pipeW, height, body);
    rect(x + pipeW / 2 - 1, baseY - height, 2, height, lip);
    rect(x - 3, baseY - height - 4, pipeW + 6, 5, lip);
    rect(x + 1, baseY - height - 2, pipeW - 2, 1, dark);
  };

  int platformY = beachTop - std::max(20, H / 9);
  drawBrickPlatform(W / 8, platformY, 6, scene == 1);
  drawBrickPlatform(W / 2 + W / 16, platformY - blockSize * 2, 5, scene == 1);
  drawPipe(W / 3, beachTop + std::max(8, H / 40), std::max(18, H / 11), true);
  drawPipe(W * 3 / 5, beachTop + std::max(9, H / 38), std::max(15, H / 12), false);

  // ── Animated characters ──
  // Crab: red body with animated claws (up/down), walks across the beach
  auto drawCrab = [&](int x, int y, bool clawsUp) {
    SDL_Color shellA {214, 78, 68, 255};
    SDL_Color shellB {242, 118, 98, 255};
    disc(x, y, std::max(4, H / 55), shellA);
    rect(x - 4, y - 1, 8, 3, shellB);
    rect(x - 6, y + 2, 2, 2, shellA);
    rect(x + 4, y + 2, 2, 2, shellA);
    rect(x - 5, y + 4, 2, 1, SDL_Color {84, 42, 30, 255});
    rect(x + 3, y + 4, 2, 1, SDL_Color {84, 42, 30, 255});
    if (clawsUp) {
      rect(x - 8, y - 5, 2, 4, shellA);
      rect(x + 6, y - 5, 2, 4, shellA);
    } else {
      rect(x - 9, y - 2, 3, 2, shellA);
      rect(x + 6, y - 2, 3, 2, shellA);
    }
    rect(x - 2, y - 5, 1, 2, SDL_Color {255, 255, 255, 255});
    rect(x + 1, y - 5, 1, 2, SDL_Color {255, 255, 255, 255});
    rect(x - 2, y - 4, 1, 1, SDL_Color {0, 0, 0, 255});
    rect(x + 1, y - 4, 1, 1, SDL_Color {0, 0, 0, 255});
  };

  // Fish: simple body + tail + eye, jumps out of the ocean periodically
  auto drawFish = [&](int x, int y, bool facingRight, SDL_Color body) {
    auto toneDown = [](Uint8 v) -> Uint8 {
      return static_cast<Uint8>(std::max(0, static_cast<int>(v) - 30));
    };
    SDL_Color fin {toneDown(body.r), toneDown(body.g), toneDown(body.b), 255};
    rect(x - 4, y - 2, 8, 4, body);
    rect(x - 2, y - 3, 4, 1, body);
    rect(x - 1, y + 2, 2, 1, body);
    if (facingRight) {
      rect(x - 6, y - 1, 2, 2, fin);
      rect(x + 3, y - 1, 2, 2, fin);
      rect(x + 2, y - 1, 1, 1, SDL_Color {255, 255, 255, 255});
    } else {
      rect(x + 4, y - 1, 2, 2, fin);
      rect(x - 5, y - 1, 2, 2, fin);
      rect(x - 3, y - 1, 1, 1, SDL_Color {255, 255, 255, 255});
    }
  };

  // Parrot: green body + orange beak, animated wing flap, flies across sky
  auto drawParrot = [&](int x, int y, bool wingUp) {
    SDL_Color body {70, 214, 120, 255};
    SDL_Color beak {246, 182, 78, 255};
    rect(x - 4, y - 3, 8, 6, body);
    rect(x + 3, y - 1, 3, 2, beak);
    rect(x - 2, y - 1, 1, 1, SDL_Color {0, 0, 0, 255});
    if (wingUp) {
      rect(x - 7, y - 6, 3, 4, SDL_Color {52, 166, 98, 255});
      rect(x + 1, y - 6, 3, 4, SDL_Color {52, 166, 98, 255});
    } else {
      rect(x - 7, y + 0, 3, 4, SDL_Color {52, 166, 98, 255});
      rect(x + 1, y + 0, 3, 4, SDL_Color {52, 166, 98, 255});
    }
    rect(x - 1, y + 3, 1, 2, SDL_Color {170, 102, 54, 255});
    rect(x + 1, y + 3, 1, 2, SDL_Color {170, 102, 54, 255});
  };

  // Turtle: green shell with dark pattern, animated leg movement
  auto drawTurtle = [&](int x, int y, bool stepA) {
    SDL_Color shell {66, 172, 80, 255};
    SDL_Color shellDark {36, 116, 58, 255};
    SDL_Color skin {176, 214, 122, 255};
    rect(x - 7, y - 4, 14, 8, shell);
    rect(x - 5, y - 2, 10, 4, shellDark);
    rect(x + 7, y - 2, 3, 3, skin);
    rect(x + 8, y - 1, 1, 1, SDL_Color {0, 0, 0, 255});
    rect(x - 6, y + 4, 3, 2, skin);
    rect(x + 2, y + 4, 3, 2, skin);
    rect(x - 6 + (stepA ? 0 : 1), y + 6, 3, 1, SDL_Color {88, 72, 46, 255});
    rect(x + 2 + (stepA ? 1 : 0), y + 6, 3, 1, SDL_Color {88, 72, 46, 255});
  };

  // Dinosaur: green body + belly + tail, occasional blink animation
  auto drawDino = [&](int x, int y, bool blink) {
    SDL_Color body {102, 198, 98, 255};
    SDL_Color belly {186, 236, 154, 255};
    rect(x - 8, y - 8, 16, 10, body);
    rect(x - 4, y - 3, 8, 5, belly);
    rect(x + 6, y - 11, 8, 7, body);
    rect(x + 10, y - 9, 1, 1, blink ? SDL_Color {80, 110, 80, 255} : SDL_Color {0, 0, 0, 255});
    rect(x - 9, y + 2, 4, 4, body);
    rect(x + 1, y + 2, 4, 4, body);
    rect(x - 8, y + 6, 3, 1, SDL_Color {88, 72, 46, 255});
    rect(x + 1, y + 6, 3, 1, SDL_Color {88, 72, 46, 255});
    rect(x - 12, y - 5, 4, 2, body);
  };

  // Puff friend: pink bouncing companion that follows the hero
  auto drawPuffFriend = [&](int x, int y) {
    disc(x, y, std::max(5, H / 62), SDL_Color {255, 152, 198, 255});
    rect(x - 3, y + 4, 2, 2, SDL_Color {220, 76, 126, 255});
    rect(x + 1, y + 4, 2, 2, SDL_Color {220, 76, 126, 255});
    rect(x - 2, y - 1, 1, 1, SDL_Color {20, 20, 20, 255});
    rect(x + 1, y - 1, 1, 1, SDL_Color {20, 20, 20, 255});
    rect(x - 1, y + 1, 2, 1, SDL_Color {224, 82, 122, 255});
  };

  // Collectible coin: golden disc with cross highlight, scrolls across sky
  auto drawCoin = [&](int cx, int cy, int radius) {
    disc(cx, cy, radius, SDL_Color {255, 206, 62, 255});
    disc(cx, cy, std::max(1, radius - 2), SDL_Color {255, 236, 132, 255});
    rect(cx - 1, cy - radius + 2, 2, radius * 2 - 3, SDL_Color {244, 180, 46, 255});
    rect(cx - radius + 2, cy - 1, radius * 2 - 3, 2, SDL_Color {255, 248, 188, 255});
  };

  // ── Place animated elements ──

  // 5 coins bouncing above the beach
  for (int i = 0; i < 5; ++i) {
    int cx = ((i * (W / 4) + static_cast<int>(t * 34.0)) % (W + 60)) - 30;
    int cy = beachTop - 18 + static_cast<int>(std::sin(t * 2.4 + i * 1.3) * 6.0);
    drawCoin(cx, cy, std::max(4, H / 48));
  }

  // ── Hero character (walking sprite with hat, shirt, shorts, boots) ──
  int heroX = static_cast<int>(std::fmod(t * 26.0, static_cast<double>(W + 24))) - 12;
  int heroY = beachTop - std::max(16, H / 12);
  int step = (static_cast<int>(t * 8.0) & 1);
  SDL_Color skin {255, 224, 189, 255};
  SDL_Color hat {212, 62, 68, 255};
  SDL_Color shirt {46, 124, 222, 255};
  SDL_Color shorts {34, 78, 138, 255};
  SDL_Color boots {88, 60, 34, 255};
  rect(heroX + 3, heroY + 0, 6, 2, hat);
  rect(heroX + 2, heroY + 2, 8, 2, hat);
  rect(heroX + 3, heroY + 4, 6, 3, skin);
  rect(heroX + 2, heroY + 7, 8, 4, shirt);
  rect(heroX + 3, heroY + 11, 6, 3, shorts);
  rect(heroX + 1, heroY + 8, 2, 4, skin);
  rect(heroX + 9, heroY + 8, 2, 4, skin);
  rect(heroX + 3, heroY + 14, 2, 3, shorts);
  rect(heroX + 7, heroY + 14, 2, 3, shorts);
  rect(heroX + 2 + step, heroY + 17, 3, 2, boots);
  rect(heroX + 6 - step, heroY + 17, 3, 2, boots);

  // Crab: walks right-to-left across the beach
  int crabX = (static_cast<int>(t * 24.0) % (W + 80)) - 40;
  drawCrab(crabX, beachTop + std::max(8, H / 40), (static_cast<int>(t * 4.0) & 1) != 0);

  // Turtle: walks left-to-right (opposite direction to crab)
  int turtleX = W - ((static_cast<int>(t * 18.0) + 20) % (W + 90)) + 24;
  drawTurtle(turtleX, beachTop + std::max(5, H / 52), (static_cast<int>(t * 6.0) & 1) != 0);

  // Dinosaur: walks slowly across the platform area
  int dinoX = ((static_cast<int>(t * 11.0) + W / 3) % (W + 120)) - 60;
  drawDino(dinoX, beachTop - std::max(18, H / 11), (static_cast<int>(t * 2.4) % 5) == 0);

  // Puff friend: bounces alongside the hero with slight offset
  int puffX = heroX + 28 + static_cast<int>(std::sin(t * 1.7) * 10.0);
  int puffY = heroY + 6 + static_cast<int>(std::fabs(std::sin(t * 3.4)) * 4.0);
  drawPuffFriend(puffX, puffY);

  // Parrot: flies across the sky with wing flapping
  int parrotX = W - ((static_cast<int>(t * 34.0) + 60) % (W + 120));
  int parrotY = std::max(12, H / 9) + static_cast<int>(std::sin(t * 2.1) * (H / 28.0));
  drawParrot(parrotX, parrotY, (static_cast<int>(t * 8.0) & 1) == 0);

  // 4 fish: jump out of the ocean periodically (sine-driven arc)
  for (int i = 0; i < 4; ++i) {
    int fishX = ((i * (W / 4) + static_cast<int>(t * 28.0)) % (W + 80)) - 40;
    double jump = std::sin(t * 2.5 + static_cast<double>(i) * 1.2);
    if (jump > -0.2) {
      int fishY = oceanTop + 20 - static_cast<int>(std::max(0.0, jump) * 16.0);
      SDL_Color fishColor = (i % 2 == 0) ? SDL_Color {255, 178, 88, 255} : SDL_Color {96, 230, 220, 255};
      drawFish(fishX, fishY, (i % 2) == 0, fishColor);
    }
  }

  // ── Storm effects (scene 3 only): rain streaks + occasional lightning bolt ──
  if (scene == 3) {
    for (int i = 0; i < W; i += 14) {
      int rx = (i + static_cast<int>(t * 220.0)) % std::max(1, W);
      int ry = oceanTop / 2 + (i % 24);
      rect(rx, ry, 1, std::max(8, H / 28), SDL_Color {178, 222, 244, 140});
    }
    if (std::sin(t * 3.2) > 0.93) {
      int boltX = W / 3 + static_cast<int>(std::sin(t * 4.4) * (W / 10.0));
      rect(boltX, 0, 3, oceanTop + 24, SDL_Color {242, 248, 255, 170});
      rect(boltX + 3, oceanTop / 3, 2, oceanTop / 3, SDL_Color {242, 248, 255, 140});
    }
  }

  // ── Bottom rainbow strip (audio visualizer homage with pulsing bars) ──
  int stripH = std::max(16, H / 14);
  int stripY = H - stripH;
  rect(0, stripY, W, stripH, SDL_Color {12, 30, 56, 230});  // dark background

  // 8 rainbow-colored bars with a sine-wave pulse animation
  const std::array<SDL_Color, 8> bars {{
    SDL_Color{232, 78, 72, 255},
    SDL_Color{246, 160, 70, 255},
    SDL_Color{252, 226, 96, 255},
    SDL_Color{104, 202, 108, 255},
    SDL_Color{78, 198, 212, 255},
    SDL_Color{70, 144, 244, 255},
    SDL_Color{152, 116, 232, 255},
    SDL_Color{244, 244, 244, 255},
  }};
  int barW = std::max(10, (W - 24) / static_cast<int>(bars.size()));
  for (int i = 0; i < static_cast<int>(bars.size()); ++i) {
    rect(8 + i * barW, stripY + 4, barW - 2, stripH - 8, bars[i]);
    int pulseY = stripY + stripH / 2 + static_cast<int>(std::sin(t * 3.0 + i * 0.7) * (stripH / 4));
    rect(8 + i * barW + 2, pulseY, barW - 6, 1, SDL_Color {255, 255, 255, 255});
  }

}

// ---------------------------------------------------------------------------
// buildPocketTestCard — The Pocket Test, PM5544 edition v2 (v0.78.8).
//
// Performance architecture (the v0.78.7 card pegged a core):
//   - The STATIC layer (grid, bars, grayscale, ramp, PLUGE/detail patches,
//     border, corner marks, center crosshair) is rendered once per raster
//     size into a process-wide cache and memcpy'd each rebuild.
//   - The island scene renders at a fixed internal 640x360 raster (it's
//     chunky pixel art — nearest sampling into the porthole is on-brand),
//     so scene cost is constant regardless of output raster.
//   - Only the dynamics draw per rebuild: diagonal sweep, the bouncing
//     porthole ball, the shimmer patch, and the ID box.
//   - Rebuild cadence is locked to the selected display's refresh rate
//     (rebuildPatternFrame), so motion is as smooth as the output itself.
//
// The ball: the scene behaves as the full background BEHIND the card, and
// the ball is a slow DVD-style bouncing porthole revealing whatever it
// floats over — sky up top, beach and characters at the bottom. It doubles
// as a burn-in rover and a smooth-motion object; the A/V sync beacon rides
// at its 12 o'clock.
// ---------------------------------------------------------------------------
void MediaEngine::buildPocketTestCard(DecodedFrame& frame, double t) {
  constexpr double kSceneSeconds = 14.0;
  constexpr double kBlendSeconds = 1.4;
  constexpr int kSceneW = 640;
  constexpr int kSceneH = 360;
  double cycle = std::fmod(std::max(0.0, t), kSceneSeconds * 4.0);
  int scene = static_cast<int>(cycle / kSceneSeconds) % 4;
  double inScene = cycle - scene * kSceneSeconds;

  // Static layer, cached PER RASTER SIZE and shared process-wide.
  //
  // This was a single cache entry, which meant it only worked while exactly one
  // raster asked for it. Deckboy runs at least two pattern engines — the
  // programme output and the cue-preview runtime — and when their rasters
  // differ the single entry was invalidated on EVERY call: a full-raster
  // reallocation (33 MB at 4K) plus a complete redraw of the static card, twice
  // per displayed frame. That churned tens of gigabytes of committed memory per
  // minute, exhausted the system commit limit, and dragged the whole machine
  // into paging — reported as "pocket-test is laggy" and "my computer is slow".
  // No other pattern has this cache, which is why only this one misbehaved.
  //
  // A tiny keyed cache fixes it: each distinct raster keeps its own card, so
  // both engines hit rather than evict. Bounded, because the set of live
  // rasters is small (outputs + preview) and must never grow without limit.
  {
    struct CachedCard {
      int width = 0;
      int height = 0;
      std::vector<std::uint8_t> pixels;
    };
    constexpr std::size_t kMaxCachedCards = 4;
    static std::mutex cacheMutex;
    static std::vector<CachedCard> cardCaches;

    std::lock_guard<std::mutex> lock(cacheMutex);
    CachedCard* hit = nullptr;
    for (auto& entry : cardCaches) {
      if (entry.width == frame.width && entry.height == frame.height) {
        hit = &entry;
        break;
      }
    }
    if (hit == nullptr) {
      if (cardCaches.size() >= kMaxCachedCards) {
        // Evict oldest; rasters change rarely, so simple FIFO is enough.
        cardCaches.erase(cardCaches.begin());
      }
      cardCaches.push_back(CachedCard {});
      hit = &cardCaches.back();
      hit->width = frame.width;
      hit->height = frame.height;
      // Build the static layer once, into a scratch frame that borrows the
      // cache's buffer, then keep it.
      DecodedFrame scratch;
      scratch.width = frame.width;
      scratch.height = frame.height;
      scratch.pixels.assign(frame.pixels.size(), 255);
      drawPocketTestCardStatic(scratch);
      hit->pixels = std::move(scratch.pixels);
    }
    // Same size on both sides, so this reuses the destination's capacity.
    frame.pixels = hit->pixels;
  }

  // The living scene at its fixed internal raster (with the crossfade).
  DecodedFrame sceneFrame;
  sceneFrame.width = kSceneW;
  sceneFrame.height = kSceneH;
  sceneFrame.pixels.assign(static_cast<size_t>(kSceneW) * kSceneH * 4u, 255);
  buildPocketTest(sceneFrame, t, scene);
  if (inScene > kSceneSeconds - kBlendSeconds) {
    double alpha = (inScene - (kSceneSeconds - kBlendSeconds)) / kBlendSeconds;
    DecodedFrame next;
    next.width = kSceneW;
    next.height = kSceneH;
    next.pixels.assign(sceneFrame.pixels.size(), 255);
    buildPocketTest(next, t, (scene + 1) % 4);
    const int mix = static_cast<int>(std::lround(alpha * 256.0));
    for (std::size_t i = 0; i + 3 < sceneFrame.pixels.size(); i += 4) {
      sceneFrame.pixels[i + 0] = static_cast<Uint8>((sceneFrame.pixels[i + 0] * (256 - mix) + next.pixels[i + 0] * mix) >> 8);
      sceneFrame.pixels[i + 1] = static_cast<Uint8>((sceneFrame.pixels[i + 1] * (256 - mix) + next.pixels[i + 1] * mix) >> 8);
      sceneFrame.pixels[i + 2] = static_cast<Uint8>((sceneFrame.pixels[i + 2] * (256 - mix) + next.pixels[i + 2] * mix) >> 8);
    }
    if (alpha > 0.5) {
      scene = (scene + 1) % 4;
    }
  }

  drawPocketTestCard(frame, sceneFrame, t, scene);
}

// ---------------------------------------------------------------------------
// drawPocketTestCardStatic — The cacheable layer. Every element is a real
// output check: 1px checker border + corner marks (pixel mapping, crop),
// grid field (geometry), 75% bars (color), grayscale staircase (levels),
// ramp (banding), PLUGE 0/2/4% + 100/98/96% (crush/clip), 1px checker +
// stripe patches (fine detail), center crosshair (alignment).
// ---------------------------------------------------------------------------
void MediaEngine::drawPocketTestCardStatic(DecodedFrame& frame) {
  const int W = frame.width;
  const int H = frame.height;
  const int u = std::max(1, H / 240);

  auto put = [&](int x, int y, const SDL_Color& color) {
    if (x < 0 || y < 0 || x >= W || y >= H) {
      return;
    }
    size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)) * 4u;
    frame.pixels[idx + 0] = color.r;
    frame.pixels[idx + 1] = color.g;
    frame.pixels[idx + 2] = color.b;
    frame.pixels[idx + 3] = color.a;
  };
  auto rect = [&](int x, int y, int w, int h, const SDL_Color& color) {
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(W, x + w);
    int y1 = std::min(H, y + h);
    for (int yy = y0; yy < y1; ++yy) {
      for (int xx = x0; xx < x1; ++xx) {
        put(xx, yy, color);
      }
    }
  };
  const SDL_Color white {255, 255, 255, 255};
  const SDL_Color black {0, 0, 0, 255};

  // Grid field, anchored to frame center.
  {
    int cell = std::max(16, (H * 8 / 100) & ~1);
    rect(0, 0, W, H, SDL_Color {104, 104, 104, 255});
    for (int x = W / 2 % cell; x < W; x += cell) {
      rect(x, 0, 1, H, white);
    }
    for (int y = H / 2 % cell; y < H; y += cell) {
      rect(0, y, W, 1, white);
    }
  }

  const int inset = 8;
  const int barsY = H * 4 / 100;
  const int barsH = H * 10 / 100;
  const int stepsY = barsY + barsH + 2;
  const int stepsH = H * 7 / 100;
  const int rampY = H * 745 / 1000;
  const int rampH = H * 6 / 100;
  const int patchY = rampY + rampH + 2;
  const int patchH = H * 7 / 100;
  const int innerW = W - inset * 2;

  auto inkFrame = [&](int x, int y, int w, int h) {
    rect(x - 1, y - 1, w + 2, 1, black);
    rect(x - 1, y + h, w + 2, 1, black);
    rect(x - 1, y, 1, h, black);
    rect(x + w, y, 1, h, black);
  };

  // Color bars: 100% white, 75% set, black.
  {
    const std::array<SDL_Color, 8> bars {{
      {255, 255, 255, 255}, {191, 191, 0, 255}, {0, 191, 191, 255}, {0, 191, 0, 255},
      {191, 0, 191, 255}, {191, 0, 0, 255}, {0, 0, 191, 255}, {0, 0, 0, 255},
    }};
    int bw = innerW / static_cast<int>(bars.size());
    for (int i = 0; i < static_cast<int>(bars.size()); ++i) {
      int x0 = inset + i * bw;
      int x1 = (i + 1 == static_cast<int>(bars.size())) ? inset + innerW : x0 + bw;
      rect(x0, barsY, x1 - x0, barsH, bars[i]);
    }
    inkFrame(inset, barsY, innerW, barsH);
  }

  // Grayscale staircase 0..100%.
  {
    int sw = innerW / 11;
    for (int i = 0; i < 11; ++i) {
      Uint8 v = static_cast<Uint8>(std::lround(i * 255.0 / 10.0));
      int x0 = inset + i * sw;
      int x1 = (i == 10) ? inset + innerW : x0 + sw;
      rect(x0, stepsY, x1 - x0, stepsH, SDL_Color {v, v, v, 255});
    }
    inkFrame(inset, stepsY, innerW, stepsH);
  }

  // Continuous ramp (banding).
  {
    for (int x = 0; x < innerW; ++x) {
      Uint8 v = static_cast<Uint8>(std::lround(x * 255.0 / std::max(1, innerW - 1)));
      rect(inset + x, rampY, 1, rampH, SDL_Color {v, v, v, 255});
    }
    inkFrame(inset, rampY, innerW, rampH);
  }

  // Patch row: PLUGE black | PLUGE white | 1px checker | 1px stripes
  // (slot 5 belongs to the dynamic shimmer).
  {
    int pw = innerW / 5;
    auto patchX = [&](int i) { return inset + i * pw; };
    rect(patchX(0), patchY, pw - 1, patchH, black);
    int ph = std::max(2, patchH / 2);
    int py = patchY + (patchH - ph) / 2;
    int pq = std::max(3, (pw - 16) / 3);
    rect(patchX(0) + 4, py, pq, ph, SDL_Color {5, 5, 5, 255});
    rect(patchX(0) + 8 + pq, py, pq, ph, SDL_Color {10, 10, 10, 255});
    rect(patchX(1), patchY, pw - 1, patchH, white);
    rect(patchX(1) + 4, py, pq, ph, SDL_Color {250, 250, 250, 255});
    rect(patchX(1) + 8 + pq, py, pq, ph, SDL_Color {245, 245, 245, 255});
    for (int y = 0; y < patchH; ++y) {
      for (int x = 0; x < pw - 1; ++x) {
        Uint8 v = (((x + y) & 1) != 0) ? 255 : 0;
        put(patchX(2) + x, patchY + y, SDL_Color {v, v, v, 255});
      }
      Uint8 s = ((y & 1) != 0) ? 255 : 0;
      rect(patchX(3), patchY + y, pw - 1, 1, SDL_Color {s, s, s, 255});
    }
    for (int i = 0; i < 4; ++i) {
      inkFrame(patchX(i), patchY, pw - 1, patchH);
    }
  }

  // Center crosshair (alignment) — frame center; the ball roams free.
  {
    int cx = W / 2;
    int cy = H / 2;
    rect(cx - 8 * u, cy - 1, 16 * u + 1, 3, black);
    rect(cx - 1, cy - 8 * u, 3, 16 * u + 1, black);
    rect(cx - 8 * u, cy, 16 * u + 1, 1, white);
    rect(cx, cy - 8 * u, 1, 16 * u + 1, white);
  }

  // Pixel-mapping border + corner marks.
  for (int x = 0; x < W; ++x) {
    Uint8 v = (x & 1) ? 255 : 0;
    put(x, 0, SDL_Color {v, v, v, 255});
    put(x, H - 1, SDL_Color {v, v, v, 255});
  }
  for (int y = 0; y < H; ++y) {
    Uint8 v = (y & 1) ? 255 : 0;
    put(0, y, SDL_Color {v, v, v, 255});
    put(W - 1, y, SDL_Color {v, v, v, 255});
  }
  rect(1, 1, W - 2, 1, black);
  rect(1, H - 2, W - 2, 1, black);
  rect(1, 1, 1, H - 2, black);
  rect(W - 2, 1, 1, H - 2, black);
  {
    int arm = 8 * u;
    int th = u;
    auto corner = [&](int x, int y, int dx, int dy) {
      rect(std::min(x, x + dx * arm), y - (dy < 0 ? th - 1 : 0), arm + 1, th, white);
      rect(x - (dx < 0 ? th - 1 : 0), std::min(y, y + dy * arm), th, arm + 1, white);
    };
    corner(3, 3, 1, 1);
    corner(W - 4, 3, -1, 1);
    corner(3, H - 4, 1, -1);
    corner(W - 4, H - 4, -1, -1);
  }
}

// ---------------------------------------------------------------------------
// drawPocketTestCard — The dynamic layer, drawn over the cached static card
// each rebuild: diagonal sweep (judder), the bouncing porthole ball with the
// scene behind it + sync beacon, the cadence shimmer patch, and the ID box.
// ---------------------------------------------------------------------------
void MediaEngine::drawPocketTestCard(DecodedFrame& frame, const DecodedFrame& sceneFrame,
                                     double t, int scene) {
  const int W = frame.width;
  const int H = frame.height;
  const int u = std::max(1, H / 240);

  auto put = [&](int x, int y, const SDL_Color& color) {
    if (x < 0 || y < 0 || x >= W || y >= H) {
      return;
    }
    size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x)) * 4u;
    frame.pixels[idx + 0] = color.r;
    frame.pixels[idx + 1] = color.g;
    frame.pixels[idx + 2] = color.b;
    frame.pixels[idx + 3] = color.a;
  };
  auto rect = [&](int x, int y, int w, int h, const SDL_Color& color) {
    int x0 = std::max(0, x);
    int y0 = std::max(0, y);
    int x1 = std::min(W, x + w);
    int y1 = std::min(H, y + h);
    for (int yy = y0; yy < y1; ++yy) {
      for (int xx = x0; xx < x1; ++xx) {
        put(xx, yy, color);
      }
    }
  };
  const SDL_Color white {255, 255, 255, 255};
  const SDL_Color black {0, 0, 0, 255};
  const SDL_Color gbCream {252, 252, 252, 255};
  const SDL_Color gbInk {64, 64, 72, 255};
  const SDL_Color gbTextShadow {200, 204, 208, 255};
  const SDL_Color gbRed {216, 72, 64, 255};

  const int inset = 8;
  const int barsY = H * 4 / 100;
  const int barsH = H * 10 / 100;
  const int stepsY = barsY + barsH + 2;
  const int stepsH = H * 7 / 100;
  const int rampY = H * 745 / 1000;
  const int rampH = H * 6 / 100;
  const int patchY = rampY + rampH + 2;
  const int patchH = H * 7 / 100;
  const int innerW = W - inset * 2;

  // ── Diagonal sweep (12 s per pass) across the grid field ──
  {
    int bandY0 = stepsY + stepsH + 2;
    int bandY1 = rampY - 2;
    int span = W + (bandY1 - bandY0);
    double progress = std::fmod(std::max(0.0, t) / 12.0, 1.0);
    int c = static_cast<int>(std::floor(progress * span));
    for (int y = bandY0; y < bandY1; ++y) {
      int x = (c - (y - bandY0)) % span;
      if (x < 0) x += span;
      rect(x - 1, y, 2 * u, 1, white);
      rect(x - 1 + 2 * u, y, u, 1, black);
    }
  }

  // ── Cadence shimmer patch (~30 Hz phase-inverting 1px checker) ──
  {
    int pw = innerW / 5;
    int x0 = inset + 4 * pw;
    int flickPhase = static_cast<int>(static_cast<long long>(std::floor(t * 30.0)) & 1);
    int w = innerW - 4 * pw;
    for (int y = 0; y < patchH; ++y) {
      for (int x = 0; x < w; ++x) {
        Uint8 v = (((x + y) & 1) == flickPhase) ? 255 : 0;
        put(x0 + x, patchY + y, SDL_Color {v, v, v, 255});
      }
    }
    rect(x0 - 1, patchY - 1, w + 2, 1, black);
    rect(x0 - 1, patchY + patchH, w + 2, 1, black);
    rect(x0 - 1, patchY, 1, patchH, black);
    rect(x0 + w, patchY, 1, patchH, black);
  }

  // ── The ball: a slow DVD-style bouncing porthole. The scene is the full
  // background BEHIND the card; the ball reveals whatever it floats over.
  // Constant-velocity edges-bounce = smooth-motion object + burn-in rover.
  {
    int r = H * 22 / 100;
    int ringW = 3 * u;
    auto bounce = [](double tt, double speed, double lo, double hi) {
      double range = hi - lo;
      if (range <= 1.0) {
        return lo;
      }
      double x = std::fmod(tt * speed / range, 2.0);
      if (x < 0) x += 2.0;
      return lo + (x < 1.0 ? x : 2.0 - x) * range;
    };
    int margin = ringW + 4;
    int cx = static_cast<int>(std::lround(bounce(t, H * 0.055, r + margin, W - r - margin)));
    int cy = static_cast<int>(std::lround(bounce(t, H * 0.043, r + margin, H - r - margin)));

    for (int dy = -(r + ringW + 1); dy <= r + ringW + 1; ++dy) {
      int y = cy + dy;
      if (y < 0 || y >= H) continue;
      auto halfAt = [&](int radius) {
        return (std::abs(dy) > radius)
          ? -1
          : static_cast<int>(std::floor(std::sqrt(static_cast<double>(radius) * radius - static_cast<double>(dy) * dy)));
      };
      int hOuterEdge = halfAt(r + ringW + 1);
      int hOuter = halfAt(r + ringW);
      int hInner = halfAt(r);
      int hInnerEdge = halfAt(r - 1);
      if (hOuterEdge >= 0) {
        rect(cx - hOuterEdge, y, hOuterEdge * 2 + 1, 1, black);
      }
      if (hOuter >= 0) {
        rect(cx - hOuter, y, hOuter * 2 + 1, 1, white);
      }
      if (hInnerEdge >= 0) {
        rect(cx - hInnerEdge, y, hInnerEdge * 2 + 1, 1, black);
      }
      if (hInner >= 0) {
        // Reveal the scene "behind" the card: nearest-sample the internal
        // scene raster at this frame position.
        int srcY = std::clamp(y * sceneFrame.height / H, 0, sceneFrame.height - 1);
        const std::uint8_t* srcRow =
          sceneFrame.pixels.data() + static_cast<size_t>(srcY) * sceneFrame.width * 4u;
        std::uint8_t* dstRow =
          frame.pixels.data() + (static_cast<size_t>(y) * W + (cx - hInner)) * 4u;
        for (int x = cx - hInner; x <= cx + hInner; ++x, dstRow += 4) {
          int srcX = std::clamp(x * sceneFrame.width / W, 0, sceneFrame.width - 1);
          const std::uint8_t* s = srcRow + static_cast<size_t>(srcX) * 4u;
          dstRow[0] = s[0];
          dstRow[1] = s[1];
          dstRow[2] = s[2];
          dstRow[3] = 255;
        }
      }
    }

    // Sync beacon rides at the ball's 12 o'clock: lit for exactly the 80 ms
    // window in which the 1 kHz pop plays.
    bool pop = std::fmod(std::max(0.0, t), 1.0) < 0.08;
    int by = cy - r - ringW / 2;
    for (int dy = -(4 * u); dy <= 4 * u; ++dy) {
      int half = static_cast<int>(std::floor(std::sqrt(std::max(0.0, static_cast<double>(4 * u) * (4 * u) - static_cast<double>(dy) * dy))));
      rect(cx - half, by + dy, half * 2 + 1, 1, black);
    }
    for (int dy = -(3 * u); dy <= 3 * u; ++dy) {
      int half = static_cast<int>(std::floor(std::sqrt(std::max(0.0, static_cast<double>(3 * u) * (3 * u) - static_cast<double>(dy) * dy))));
      rect(cx - half, by + dy, half * 2 + 1, 1, pop ? white : SDL_Color {70, 70, 70, 255});
    }
  }

  // ── ID box (Emerald chrome): version, raster, clock, scene ──
  {
    auto glyphRows = [](char c) -> const std::uint8_t* {
      static const std::uint8_t digits[10][5] = {
        {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,3,1,7}, {5,5,7,1,1},
        {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,2,2}, {7,5,7,5,7}, {7,5,7,1,7},
      };
      static const std::uint8_t letters[26][5] = {
        {2,5,7,5,5}, {6,5,6,5,6}, {3,4,4,4,3}, {6,5,5,5,6}, {7,4,6,4,7},
        {7,4,6,4,4}, {3,4,5,5,3}, {5,5,7,5,5}, {7,2,2,2,7}, {1,1,1,5,2},
        {5,6,4,6,5}, {4,4,4,4,7}, {5,7,7,5,5}, {6,5,5,5,5}, {2,5,5,5,2},
        {6,5,6,4,4}, {2,5,5,6,3}, {6,5,6,6,5}, {3,4,2,1,6}, {7,2,2,2,2},
        {5,5,5,5,7}, {5,5,5,5,2}, {5,5,7,7,5}, {5,5,2,5,5}, {5,5,2,2,2},
        {7,1,2,4,7},
      };
      static const std::uint8_t colon[5] = {0,2,0,2,0};
      static const std::uint8_t dot[5]   = {0,0,0,0,2};
      static const std::uint8_t dash[5]  = {0,0,7,0,0};
      static const std::uint8_t blank[5] = {0,0,0,0,0};
      if (c >= '0' && c <= '9') return digits[c - '0'];
      if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
      if (c >= 'a' && c <= 'z') return letters[c - 'a'];
      if (c == ':') return colon;
      if (c == '.') return dot;
      if (c == '-') return dash;
      return blank;
    };
    auto drawText = [&](int x, int y, int scale, const std::string& s, const SDL_Color& color) {
      int cx = x;
      for (char c : s) {
        const std::uint8_t* rows = glyphRows(c);
        for (int ry = 0; ry < 5; ++ry) {
          for (int rx = 0; rx < 3; ++rx) {
            if (rows[ry] & (4 >> rx)) {
              rect(cx + rx * scale + scale, y + ry * scale + scale, scale, scale, gbTextShadow);
              rect(cx + rx * scale, y + ry * scale, scale, scale, color);
            }
          }
        }
        cx += 4 * scale;
      }
    };
    auto gbBox = [&](int x, int y, int w, int h) {
      const int b = u;
      const SDL_Color bandLight {168, 224, 216, 255};
      const SDL_Color bandDeep {88, 168, 160, 255};
      rect(x + b, y + b, w - 2 * b, h - 2 * b, gbCream);
      rect(x + 2 * b, y, w - 4 * b, b, gbInk);
      rect(x + 2 * b, y + h - b, w - 4 * b, b, gbInk);
      rect(x, y + 2 * b, b, h - 4 * b, gbInk);
      rect(x + w - b, y + 2 * b, b, h - 4 * b, gbInk);
      rect(x + b, y + b, b, b, gbInk);
      rect(x + w - 2 * b, y + b, b, b, gbInk);
      rect(x + b, y + h - 2 * b, b, b, gbInk);
      rect(x + w - 2 * b, y + h - 2 * b, b, b, gbInk);
      rect(x + 2 * b, y + b, w - 4 * b, b, bandLight);
      rect(x + b, y + 2 * b, b, h - 4 * b, bandLight);
      rect(x + 2 * b, y + h - 2 * b, w - 4 * b, b, bandDeep);
      rect(x + w - 2 * b, y + 2 * b, b, h - 4 * b, bandDeep);
    };

    static const char* kSceneNames[4] = {"DAY", "SUNSET", "NIGHT", "STORM"};
    int scale = u;
    int pd = 2 * u + 3;
    int totalSeconds = static_cast<int>(t);
    int tenths = static_cast<int>(t * 10.0) % 10;
    char clock[24];
    std::snprintf(clock, sizeof(clock), "%02d:%02d.%d", (totalSeconds / 60) % 100, totalSeconds % 60, tenths);
    std::string line = std::string("DECKBOY ") + deckboy::core::version::kVersionTag +
                       "  " + std::to_string(W) + "X" + std::to_string(H) +
                       "  " + clock + "  " + kSceneNames[std::clamp(scene, 0, 3)];
    int boxW = static_cast<int>(line.size()) * 4 * scale - scale + pd * 2 + 6 * scale;
    int boxH = 5 * scale + pd * 2 + 2;
    int boxX = (W - boxW) / 2;
    int boxY = H * 89 / 100;
    gbBox(boxX, boxY, boxW, boxH);
    drawText(boxX + pd, boxY + pd, scale, line, gbInk);
    if ((static_cast<long long>(std::floor(t * 2.0)) & 1) == 0) {
      int ax = boxX + boxW - pd - 5 * scale;
      int ay = boxY + boxH - pd - 3 * scale + 2;
      for (int row = 0; row < 3; ++row) {
        rect(ax + row * scale, ay + row * scale, (5 - 2 * row) * scale, scale, gbRed);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// buildTestBars — testsrc2-homage motion diagnostics ("Test Bars").
//
// The owner took a shine to ffmpeg's testsrc2 during decoder testing, so this is
// the Deckboy edition: six saturated bars for color checks, a bouncing
// rainbow diagonal for motion/tearing, a dissolving checker patch for
// deinterlace/scaler artefacts, a sliding grey reference block for judder,
// and a running clock + frame counter for latency / dropped-frame checks.
// Always animated (patternTypeIsAnimated) — motion is the whole point.
// ---------------------------------------------------------------------------
void MediaEngine::buildTestBars(DecodedFrame& frame, double t) {
  const int W = frame.width;
  const int H = frame.height;
  if (W <= 0 || H <= 0) {
    return;
  }
  t = std::max(0.0, t);
  auto rect = [&](int x, int y, int w, int h, SDL_Color c) {
    fillPixelRect(frame, x, y, w, h, c);
  };

  // ── 1. Saturated vertical bars (full frame background) ──
  static const SDL_Color kBars[6] = {
    {255, 0, 0, 255}, {0, 255, 0, 255}, {255, 255, 0, 255},
    {0, 0, 255, 255}, {255, 0, 255, 255}, {0, 255, 255, 255},
  };
  for (int i = 0; i < 6; ++i) {
    int x0 = W * i / 6;
    int x1 = W * (i + 1) / 6;
    rect(x0, 0, x1 - x0, H, kBars[i]);
  }

  // ── 2. Dissolving checker patch (scaler/deinterlace artefact magnet) ──
  {
    int px0 = W * 62 / 100, py0 = H * 60 / 100;
    int px1 = W * 92 / 100, py1 = H * 86 / 100;
    int cell = std::max(4, W / 120);
    int tick = static_cast<int>(t * 2.0);           // reseed twice a second
    int drift = static_cast<int>(t * cell) % (2 * cell);
    for (int y = py0; y < py1; y += cell) {
      for (int x = px0; x < px1; x += cell) {
        int cxI = (x - px0 + drift) / cell;
        int cyI = (y - py0 + drift) / cell;
        std::uint32_t h32 = static_cast<std::uint32_t>(cxI) * 73856093u
                          ^ static_cast<std::uint32_t>(cyI) * 19349663u
                          ^ static_cast<std::uint32_t>(tick) * 83492791u;
        bool checker = ((cxI + cyI) & 1) != 0;
        bool dissolve = (h32 & 7u) == 0u;            // ~12% of cells flip per tick
        bool lit = checker != dissolve;
        SDL_Color c = lit ? SDL_Color {230, 230, 230, 255} : SDL_Color {26, 26, 26, 255};
        int cw = std::min(cell, px1 - x);
        int ch = std::min(cell, py1 - y);
        rect(x, y, cw, ch, c);
      }
    }
  }

  // ── 3. Sliding grey reference block (judder check) ──
  {
    int side = std::max(12, H / 5);
    double slide = 0.5 + 0.5 * std::sin(t * kTau / 9.0);
    int gx = static_cast<int>((W * 0.08) + slide * (W * 0.35));
    int gy = H * 58 / 100;
    rect(gx, gy, side, side, {128, 128, 128, 255});
    rect(gx + side / 4, gy + side / 4, side / 2, side / 2, {96, 96, 96, 255});
  }

  // ── 4. Bouncing rainbow diagonal (motion / tearing / latency line) ──
  {
    double amp = H * 0.42;
    double midY = H * 0.5;
    int thickness = std::max(2, H / 240);
    double y0 = midY + amp * std::sin(t * kTau / 7.3);
    double y1 = midY + amp * std::sin(t * kTau / 5.1 + 1.7);
    auto hsvToRgb = [](double h) {
      double r = std::clamp(std::abs(std::fmod(h * 6.0 + 0.0, 6.0) - 3.0) - 1.0, 0.0, 1.0);
      double g = std::clamp(std::abs(std::fmod(h * 6.0 + 4.0, 6.0) - 3.0) - 1.0, 0.0, 1.0);
      double b = std::clamp(std::abs(std::fmod(h * 6.0 + 2.0, 6.0) - 3.0) - 1.0, 0.0, 1.0);
      return SDL_Color {static_cast<Uint8>(r * 255), static_cast<Uint8>(g * 255),
                        static_cast<Uint8>(b * 255), 255};
    };
    for (int x = 0; x < W; ++x) {
      double frac = static_cast<double>(x) / std::max(1, W - 1);
      int y = static_cast<int>(y0 + (y1 - y0) * frac);
      SDL_Color c = hsvToRgb(std::fmod(frac + t * 0.08, 1.0));
      rect(x, y - thickness / 2, 1, thickness, c);
    }
  }

  // ── 5. Clock + frame counter box (latency / drop diagnostics) ──
  {
    auto glyphRows = [](char c) -> const std::uint8_t* {
      static const std::uint8_t digits[10][5] = {
        {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,3,1,7}, {5,5,7,1,1},
        {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,2,2}, {7,5,7,5,7}, {7,5,7,1,7},
      };
      static const std::uint8_t colon[5] = {0,2,0,2,0};
      static const std::uint8_t dot[5]   = {0,0,0,0,2};
      static const std::uint8_t letterF[5] = {7,4,6,4,4};
      static const std::uint8_t letterT[5] = {7,2,2,2,2};
      static const std::uint8_t blank[5] = {0,0,0,0,0};
      if (c >= '0' && c <= '9') return digits[c - '0'];
      if (c == ':') return colon;
      if (c == '.') return dot;
      if (c == 'F') return letterF;
      if (c == 'T') return letterT;
      return blank;
    };
    int scale = std::max(1, H / 240);
    auto drawText = [&](int x, int y, const std::string& s, SDL_Color color) {
      int cx = x;
      for (char c : s) {
        const std::uint8_t* rows = glyphRows(c);
        for (int ry = 0; ry < 5; ++ry) {
          for (int rx = 0; rx < 3; ++rx) {
            if (rows[ry] & (4 >> rx)) {
              rect(cx + rx * scale, y + ry * scale, scale, scale, color);
            }
          }
        }
        cx += 4 * scale;
      }
    };
    int totalSeconds = static_cast<int>(t);
    int millis = static_cast<int>(t * 1000.0) % 1000;
    long long frameNo = static_cast<long long>(t * 30.0);   // nominal 30 fps counter
    char clockLine[32];
    std::snprintf(clockLine, sizeof(clockLine), "T %02d:%02d:%02d.%03d",
                  (totalSeconds / 3600) % 100, (totalSeconds / 60) % 60,
                  totalSeconds % 60, millis);
    char frameLine[24];
    std::snprintf(frameLine, sizeof(frameLine), "F %06lld", frameNo);
    int pad = 3 * scale;
    int lineW = 14 * 4 * scale;
    int boxW = lineW + pad * 2;
    int boxH = (5 + 2 + 5) * scale + pad * 2;
    rect(8, 8, boxW, boxH, {58, 12, 12, 255});
    drawText(8 + pad, 8 + pad, clockLine, {255, 214, 92, 255});
    drawText(8 + pad, 8 + pad + 7 * scale, frameLine, {255, 214, 92, 255});
  }
}

// ---------------------------------------------------------------------------
// buildTestClock — testsrc-homage sync card ("Test Clock").
//
// The card that proved the program-monitor tap: a big, readable seconds
// counter you can photograph on two screens at once and compare. Where Test
// Bars stresses the scaler, this one answers "is that display/preview/encoder
// showing me the same frame as everything else?".
//
//   - 8 saturated bars: quick colour + bar-boundary reference
//   - a TRUE circle (aspect-corrected): reads as an egg the instant the
//     raster's pixel aspect or a stretch mode is wrong
//   - a continuously scrolling hue band: sub-second phase reference, so two
//     captures a few frames apart are visibly different
//   - huge 2-digit seconds + a 3-digit frame counter: the sync read itself
//   - full timecode + frame number in the corner for exact comparisons
//
// Always animated (patternTypeIsAnimated) — a still sync card is useless.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// buildTimerFrame - stage/speaker countdown.
//
// Digits are drawn as SEVEN-SEGMENT geometry, not text. A timer on a stage
// screen must render identically on every machine regardless of which fonts are
// installed, and the cue-row icons already taught us that leaning on font
// coverage for a glyph is how you get tofu. Segments also scale to any raster
// without hinting artefacts, which a bitmap font would not.
//
// Format matches the source app: m:ss under an hour, h:mm:ss at or above it,
// and a "+" while in overtime. The ceiling/floor asymmetry is deliberate and
// load-bearing: counting DOWN rounds up so the final second reads 0:01 rather
// than 0:00, and overtime rounds down so it starts at +0:00. Get it wrong and
// the clock looks a second off all the way through.
// ---------------------------------------------------------------------------
namespace {

// Segment order: a(top) b(upper right) c(lower right) d(bottom) e(lower left)
// f(upper left) g(middle). Index 10 is a bare middle bar, used as "+".
const bool kSevenSeg[11][7] = {
  {1,1,1,1,1,1,0},  // 0
  {0,1,1,0,0,0,0},  // 1
  {1,1,0,1,1,0,1},  // 2
  {1,1,1,1,0,0,1},  // 3
  {0,1,1,0,0,1,1},  // 4
  {1,0,1,1,0,1,1},  // 5
  {1,0,1,1,1,1,1},  // 6
  {1,1,1,0,0,0,0},  // 7
  {1,1,1,1,1,1,1},  // 8
  {1,1,1,1,0,1,1},  // 9
  {0,0,0,0,0,0,1},  // 10 = plus
};

void fillTimerRect(DecodedFrame& f, int x, int y, int w, int h, SDL_Color c) {
  for (int py = std::max(0, y); py < std::min(f.height, y + h); ++py) {
    for (int px = std::max(0, x); px < std::min(f.width, x + w); ++px) {
      const std::size_t o = (static_cast<std::size_t>(py) * f.width + px) * 4;
      f.pixels[o + 0] = c.r;
      f.pixels[o + 1] = c.g;
      f.pixels[o + 2] = c.b;
      f.pixels[o + 3] = 255;
    }
  }
}

// 5x7 bitmap font for the timer message line. Bundled as data rather than
// drawn from a TTF for the same reason the digits are seven-segment: a stage
// screen must render identically wherever it runs.
const std::uint8_t* timerGlyph5x7(unsigned char c) {
  switch (c) {
    case ' ': { static const std::uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00}; return g; }
    case '!': { static const std::uint8_t g[7] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04}; return g; }
    case '\'': { static const std::uint8_t g[7] = {0x04,0x04,0x00,0x00,0x00,0x00,0x00}; return g; }
    case ',': { static const std::uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x04,0x08}; return g; }
    case '-': { static const std::uint8_t g[7] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}; return g; }
    case '.': { static const std::uint8_t g[7] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}; return g; }
    case '0': { static const std::uint8_t g[7] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}; return g; }
    case '1': { static const std::uint8_t g[7] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}; return g; }
    case '2': { static const std::uint8_t g[7] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}; return g; }
    case '3': { static const std::uint8_t g[7] = {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}; return g; }
    case '4': { static const std::uint8_t g[7] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; return g; }
    case '5': { static const std::uint8_t g[7] = {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}; return g; }
    case '6': { static const std::uint8_t g[7] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}; return g; }
    case '7': { static const std::uint8_t g[7] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; return g; }
    case '8': { static const std::uint8_t g[7] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; return g; }
    case '9': { static const std::uint8_t g[7] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}; return g; }
    case ':': { static const std::uint8_t g[7] = {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00}; return g; }
    case '?': { static const std::uint8_t g[7] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}; return g; }
    case 'A': { static const std::uint8_t g[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return g; }
    case 'B': { static const std::uint8_t g[7] = {0x1E,0x11,0x1E,0x11,0x11,0x11,0x1E}; return g; }
    case 'C': { static const std::uint8_t g[7] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return g; }
    case 'D': { static const std::uint8_t g[7] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}; return g; }
    case 'E': { static const std::uint8_t g[7] = {0x1F,0x10,0x1E,0x10,0x10,0x10,0x1F}; return g; }
    case 'F': { static const std::uint8_t g[7] = {0x1F,0x10,0x1E,0x10,0x10,0x10,0x10}; return g; }
    case 'G': { static const std::uint8_t g[7] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}; return g; }
    case 'H': { static const std::uint8_t g[7] = {0x11,0x11,0x1F,0x11,0x11,0x11,0x11}; return g; }
    case 'I': { static const std::uint8_t g[7] = {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}; return g; }
    case 'J': { static const std::uint8_t g[7] = {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}; return g; }
    case 'K': { static const std::uint8_t g[7] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11}; return g; }
    case 'L': { static const std::uint8_t g[7] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return g; }
    case 'M': { static const std::uint8_t g[7] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; return g; }
    case 'N': { static const std::uint8_t g[7] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11}; return g; }
    case 'O': { static const std::uint8_t g[7] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return g; }
    case 'P': { static const std::uint8_t g[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return g; }
    case 'Q': { static const std::uint8_t g[7] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; return g; }
    case 'R': { static const std::uint8_t g[7] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; return g; }
    case 'S': { static const std::uint8_t g[7] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return g; }
    case 'T': { static const std::uint8_t g[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return g; }
    case 'U': { static const std::uint8_t g[7] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return g; }
    case 'V': { static const std::uint8_t g[7] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; return g; }
    case 'W': { static const std::uint8_t g[7] = {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}; return g; }
    case 'X': { static const std::uint8_t g[7] = {0x11,0x0A,0x04,0x04,0x04,0x0A,0x11}; return g; }
    case 'Y': { static const std::uint8_t g[7] = {0x11,0x0A,0x04,0x04,0x04,0x04,0x04}; return g; }
    case 'Z': { static const std::uint8_t g[7] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; return g; }
    default: return nullptr;
  }
}
void drawSevenSeg(DecodedFrame& f, int digit, int x, int y, int w, int h,
                  int thick, SDL_Color on) {
  if (digit < 0 || digit > 10) return;
  const bool* s = kSevenSeg[digit];
  const int half = h / 2;
  if (s[0]) fillTimerRect(f, x, y, w, thick, on);
  if (s[1]) fillTimerRect(f, x + w - thick, y, thick, half, on);
  if (s[2]) fillTimerRect(f, x + w - thick, y + half, thick, half, on);
  if (s[3]) fillTimerRect(f, x, y + h - thick, w, thick, on);
  if (s[4]) fillTimerRect(f, x, y + half, thick, half, on);
  if (s[5]) fillTimerRect(f, x, y, thick, half, on);
  if (s[6]) fillTimerRect(f, x, y + half - thick / 2, w, thick, on);
}

}  // namespace

// Timer logo cache. buildTimerFrame is static, so this lives at file scope.
// Decoded ONCE per path+size: the decode is a synchronous ffmpeg call, which is
// fine as a one-off when the operator sets a logo, and unacceptable per frame.
// Failures are remembered too, so a missing file does not respawn ffmpeg 60
// times a second.
namespace {

struct TimerLogoCache {
  std::string path;
  int targetH = 0;
  int w = 0;
  int h = 0;
  bool failed = false;
  std::vector<std::uint8_t> rgba;   // tightly packed, w*h*4
};

std::mutex gTimerLogoMutex;
TimerLogoCache gTimerLogo;

const TimerLogoCache* timerLogoFor(const std::string& path, int targetH) {
  if (path.empty() || targetH <= 0) return nullptr;
  std::lock_guard<std::mutex> lock(gTimerLogoMutex);
  if (gTimerLogo.path == path && gTimerLogo.targetH == targetH) {
    return gTimerLogo.failed ? nullptr : &gTimerLogo;
  }
  gTimerLogo = TimerLogoCache{};
  gTimerLogo.path = path;
  gTimerLogo.targetH = targetH;

  // Height is fixed and width follows the source aspect, so a wordmark is not
  // squashed into a square.
  const std::vector<std::string> probeArgs {
    "ffprobe", "-v", "error", "-select_streams", "v:0",
    "-show_entries", "stream=width,height", "-of", "csv=p=0", path
  };
  int srcW = 0, srcH = 0;
  if (auto probe = readAllText(probeArgs)) {
    std::sscanf(probe->c_str(), "%d,%d", &srcW, &srcH);
  }
  if (srcW <= 0 || srcH <= 0) { gTimerLogo.failed = true; return nullptr; }
  const int outH = targetH;
  const int outW = std::max(1, static_cast<int>(std::llround(
    static_cast<double>(srcW) * outH / static_cast<double>(srcH))));

  ChildProcess proc;
  const std::vector<std::string> args {
    "ffmpeg", "-hide_banner", "-loglevel", "error", "-i", path,
    "-frames:v", "1",
    "-vf", "scale=" + std::to_string(outW) + ":" + std::to_string(outH),
    "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1"
  };
  if (!spawnPipeProcess(proc, args)) { gTimerLogo.failed = true; return nullptr; }
  const std::size_t want = static_cast<std::size_t>(outW) * outH * 4;
  gTimerLogo.rgba.resize(want);
  const bool ok = readExact(proc.readFd, gTimerLogo.rgba.data(), want);
  proc.stop();
  if (!ok) { gTimerLogo.failed = true; gTimerLogo.rgba.clear(); return nullptr; }
  gTimerLogo.w = outW;
  gTimerLogo.h = outH;
  return &gTimerLogo;
}

}  // namespace

// ---------------------------------------------------------------------------
// Tone generator
// ---------------------------------------------------------------------------

// Pink noise via the Voss-McCartney update: cheap, and flat to within a
// fraction of a dB across the audio band, which white-noise-through-a-filter
// is not without a much longer filter than a live tool wants to run.
namespace {

inline double nextWhite(std::uint32_t& seed) {
  // xorshift32: no allocation, no locking, and identical on every platform,
  // which matters because a test signal that differs between machines is not
  // a reference.
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return (static_cast<double>(seed) / 2147483648.0) - 1.0;
}

inline double nextPink(double* rows, double& running, std::uint32_t& counter,
                       std::uint32_t& seed) {
  // Only ONE row changes per sample -- which row is given by the lowest set
  // bit of the counter. That is the whole trick: 1/f without a filter.
  counter++;
  unsigned index = 0;
  std::uint32_t n = counter;
  while ((n & 1u) == 0u && index < 15u) { n >>= 1; ++index; }
  running -= rows[index];
  rows[index] = nextWhite(seed) * 0.25;
  running += rows[index];
  return running;
}

}  // namespace


// ---------------------------------------------------------------------------
// FDS wavetable voice
//
// Built from the DOCUMENTED behaviour of the Famicom Disk System sound
// hardware. The carrier is a 64-entry, 6-bit table; the modulator is a
// 32-entry table of signed increments that accumulate into a frequency BEND
// applied to the carrier. That bend, not any mixing of the two oscillators, is
// what makes FDS sound like FDS.
// ---------------------------------------------------------------------------
namespace {

// 64 steps, values 0..63, exactly the hardware's wave RAM shape.
void fdsBuildCarrier(FdsCarrier kind, double* table) {
  for (int i = 0; i < 64; ++i) {
    const double t = static_cast<double>(i) / 64.0;
    double v = 0.0;
    switch (kind) {
      case FdsCarrier::Triangle:
        v = (t < 0.5) ? (4.0 * t - 1.0) : (3.0 - 4.0 * t);
        break;
      case FdsCarrier::Pulse25:
        v = (t < 0.25) ? 1.0 : -1.0;
        break;
      case FdsCarrier::Saw:
        v = 2.0 * t - 1.0;
        break;
      case FdsCarrier::Additive:
        // First four harmonics at 1/n. Organ-like, and the closest thing the
        // hardware has to a "rich" preset.
        v = std::sin(2.0 * 3.14159265358979323846 * t)
          + std::sin(4.0 * 3.14159265358979323846 * t) / 2.0
          + std::sin(6.0 * 3.14159265358979323846 * t) / 3.0
          + std::sin(8.0 * 3.14159265358979323846 * t) / 4.0;
        v /= 2.083;   // normalise the harmonic sum back to +/-1
        break;
      default:
        v = std::sin(2.0 * 3.14159265358979323846 * t);
        break;
    }
    // Quantise to the hardware's 6 bits. This is audible -- it is part of the
    // sound, not an imperfection to smooth away.
    const double q = std::round((v * 0.5 + 0.5) * 63.0);
    table[i] = (q / 63.0) * 2.0 - 1.0;
  }
}

// 32 signed increments. The hardware stores 3-bit codes meaning
// +0 +1 +2 +4 reset -4 -2 -1; these presets are built from that vocabulary.
void fdsBuildModulator(FdsModulator kind, double* table) {
  for (int i = 0; i < 32; ++i) {
    double v = 0.0;
    switch (kind) {
      case FdsModulator::Ramp:    v = (i < 16) ? 1.0 : -1.0; break;
      case FdsModulator::Square:  v = (i < 16) ? 4.0 : -4.0; break;
      case FdsModulator::Vibrato: v = std::sin(2.0 * 3.14159265358979323846 * i / 32.0) * 2.0; break;
      case FdsModulator::Growl:   v = ((i / 4) % 2 == 0) ? 4.0 : -4.0; break;
      default:                    v = 0.0; break;
    }
    table[i] = v;
  }
}

}  // namespace

// One sample of the 2A03 voice. Implemented from the documented behaviour of
// the chip: duty-cycled pulse, a 4-bit stepped triangle with NO volume
// control, and noise from a 15-bit linear feedback shift register.
double MediaEngine::nesNextSample(const ToneSettings& tone, double dt) {
  // A played note overrides the cue's pitch; without a keyboard the cue's own
  // setting stands, so a drone still works untouched.
  const double note = std::clamp(
    chipGated_ ? chipNoteHz_ : tone.synth.noteHz, 20.0, 8000.0);
  double sample = 0.0;

  switch (tone.synth.nesVoice) {
    case NesVoice::Triangle: {
      nesPhase_ += note * dt;
      while (nesPhase_ >= 1.0) nesPhase_ -= 1.0;
      // 32 steps up and down, exactly like the hardware's sequencer. The
      // stepping is audible and is why the NES triangle sounds like a NES
      // triangle rather than a synth triangle.
      const int step = static_cast<int>(nesPhase_ * 32.0) & 31;
      const int level = (step < 16) ? (15 - step) : (step - 16);
      sample = (level / 7.5) - 1.0;
      break;
    }
    case NesVoice::Noise: {
      // The LFSR is clocked at a RATE derived from the note rather than being
      // an oscillator: noise has no pitch, only a period.
      nesNoiseAccum_ += note * dt * 16.0;
      while (nesNoiseAccum_ >= 1.0) {
        nesNoiseAccum_ -= 1.0;
        // Tap 1 normally, tap 6 in short mode. Short mode's period is brief
        // enough to read as pitched metal, which is what trackers call
        // "periodic noise".
        const unsigned tap = tone.synth.nesNoiseShort ? 6u : 1u;
        const unsigned bit = ((nesLfsr_ ^ (nesLfsr_ >> tap)) & 1u);
        nesLfsr_ = (nesLfsr_ >> 1) | (bit << 14);
      }
      sample = (nesLfsr_ & 1u) ? 1.0 : -1.0;
      break;
    }
    default: {
      nesPhase_ += note * dt;
      while (nesPhase_ >= 1.0) nesPhase_ -= 1.0;
      double duty = 0.5;
      switch (tone.synth.nesDuty) {
        case NesDuty::Eighth:       duty = 0.125; break;
        case NesDuty::Quarter:      duty = 0.25;  break;
        case NesDuty::ThreeQuarter: duty = 0.75;  break;
        default:                    duty = 0.5;   break;
      }
      sample = (nesPhase_ < duty) ? 1.0 : -1.0;
      break;
    }
  }

  // 4-bit output. Optional, but on by default: the steps ARE the sound, and
  // smoothing them makes a chiptune voice sound like a synth imitating one.
  if (tone.synth.nesQuantise) {
    sample = std::round(sample * 7.5) / 7.5;
  }
  return sample * chipEnvelope(tone, dt);
}

// Shared envelope. Pitch and envelope belong to the NOTE, not to whichever
// oscillator happens to be playing it, so both chips use this.

// ---------------------------------------------------------------------------
// Played notes
// ---------------------------------------------------------------------------
void MediaEngine::synthNoteOn(double hz, int velocity) {
  if (hz <= 0.0) return;
  chipGated_ = true;
  chipGateOpen_ = true;
  chipNoteHz_ = hz;
  // Velocity scales level. A chip had no velocity, but a keyboard does, and
  // ignoring it makes every note identical in a way that feels broken to
  // anyone playing rather than programming.
  chipVelocity_ = std::clamp(velocity / 127.0, 0.05, 1.0);
  fdsEnvSeconds_ = 0.0;   // restart the attack
}

void MediaEngine::synthNoteOff(double hz) {
  // Ignore the release of a note that is no longer the one sounding. On a
  // monophonic voice, rolling from one key to the next means the first key's
  // release arrives AFTER the second key's press, and honouring it would cut
  // off the note just started.
  if (!chipGated_) return;
  if (hz > 0.0 && std::abs(hz - chipNoteHz_) > 0.01) return;
  chipGateOpen_ = false;
  chipReleaseLevel_ = 1.0;
  fdsEnvSeconds_ = 0.0;   // release timing starts now
}

void MediaEngine::synthAllNotesOff() {
  chipGateOpen_ = false;
  chipGated_ = false;
  chipReleaseLevel_ = 0.0;
}
double MediaEngine::chipEnvelope(const ToneSettings& tone, double dt) {
  // Gated: the envelope follows the key rather than the cue's retrigger.
  if (chipGated_) {
    fdsEnvSeconds_ += dt;
    const double a = std::max(0.001, tone.synth.attackSeconds);
    const double r = std::max(0.001, tone.synth.releaseSeconds);
    if (chipGateOpen_) {
      // Attack, then HOLD. A held key sustains -- this is the difference
      // between an instrument and a one-shot.
      const double env = (fdsEnvSeconds_ < a) ? (fdsEnvSeconds_ / a) : 1.0;
      return env * chipVelocity_;
    }
    const double env = std::max(0.0, chipReleaseLevel_ - fdsEnvSeconds_ / r);
    return env * chipVelocity_;
  }
  if (tone.synth.retriggerSeconds > 0.0) {
    fdsEnvSeconds_ += dt;
    if (fdsEnvSeconds_ >= tone.synth.retriggerSeconds) fdsEnvSeconds_ = 0.0;
  } else {
    fdsEnvSeconds_ = std::min(fdsEnvSeconds_ + dt, 1e6);
  }
  const double a = std::max(0.001, tone.synth.attackSeconds);
  const double r = std::max(0.001, tone.synth.releaseSeconds);
  if (fdsEnvSeconds_ < a) return fdsEnvSeconds_ / a;
  if (tone.synth.retriggerSeconds > 0.0) {
    return std::max(0.0, 1.0 - (fdsEnvSeconds_ - a) / r);
  }
  return 1.0;
}

double MediaEngine::fdsNextSample(const ToneSettings& tone, double dt) {
  // Cached. These were rebuilt EVERY SAMPLE -- 96 doubles including four sin()
  // calls per entry, 48,000 times a second, to produce a table that only
  // changes when the operator turns a knob. Rebuild on change only.
  if (!fdsTablesValid_ ||
      fdsCachedCarrier_ != static_cast<int>(tone.synth.carrier) ||
      fdsCachedModulator_ != static_cast<int>(tone.synth.modulator)) {
    fdsBuildCarrier(tone.synth.carrier, fdsCarrierTable_);
    fdsBuildModulator(tone.synth.modulator, fdsModTable_);
    fdsCachedCarrier_ = static_cast<int>(tone.synth.carrier);
    fdsCachedModulator_ = static_cast<int>(tone.synth.modulator);
    fdsTablesValid_ = true;
  }
  const double* carrier = fdsCarrierTable_;
  const double* modulator = fdsModTable_;

  // A played note overrides the cue's pitch; without a keyboard the cue's
  // own setting stands, so a drone still works untouched.
  const double note = std::clamp(
    chipGated_ ? chipNoteHz_ : tone.synth.noteHz, 20.0, 8000.0);
  const double depth = std::clamp(tone.synth.modDepth, 0, 63) / 63.0;

  // Modulator runs at a RATIO of the note, which is what makes the bend track
  // pitch instead of turning into a fixed wobble as you play up the keyboard.
  const double modHz = note * std::clamp(tone.synth.modRatio, 0.0, 8.0);
  fdsModPhase_ += modHz * 32.0 * dt;
  while (fdsModPhase_ >= 32.0) fdsModPhase_ -= 32.0;
  const int modIndex = static_cast<int>(fdsModPhase_) & 31;

  // Accumulate, then decay slightly. Without the decay the accumulator walks
  // off and the pitch never returns, which the hardware avoids by resetting.
  fdsModAccum_ = fdsModAccum_ * 0.999 + modulator[modIndex] * dt * 60.0;
  fdsModAccum_ = std::clamp(fdsModAccum_, -8.0, 8.0);

  // The bend: a multiplier on the carrier frequency, scaled by depth.
  const double bend = 1.0 + fdsModAccum_ * depth * 0.25;
  fdsCarrierPhase_ += note * bend * 64.0 * dt;
  while (fdsCarrierPhase_ >= 64.0) fdsCarrierPhase_ -= 64.0;
  while (fdsCarrierPhase_ < 0.0) fdsCarrierPhase_ += 64.0;

  const int idx = static_cast<int>(fdsCarrierPhase_) & 63;
  double sample = carrier[idx];

  return sample * chipEnvelope(tone, dt);
}

void MediaEngine::pumpToneAudio(const Cue& cue) {
  if (cue.kind != CueKind::Tone) return;

  const int channels = std::max(2, audioDeviceChannels_.load(std::memory_order_relaxed));
  // Keep roughly a fifth of a second queued. Enough that a slow frame cannot
  // starve the device, short enough that a level or frequency change is heard
  // immediately rather than after the old buffer drains.
  const int targetBytes = (kAudioRate / 5) * channels * static_cast<int>(sizeof(std::int16_t));
  const int queued = queuedAudioBytes();
  if (queued >= targetBytes) return;

  const int bytesPerFrame = channels * static_cast<int>(sizeof(std::int16_t));
  const std::size_t frames =
    static_cast<std::size_t>((targetBytes - queued) / std::max(1, bytesPerFrame));
  if (frames == 0) return;

  // dBFS -> linear, clamped. The ceiling is -1 dBFS rather than 0: a test tone
  // goes into a live PA and there is never a reason to push a generated signal
  // to full scale.
  const double db = std::clamp(cue.tone.levelDbfs, -60.0, -1.0);
  const double amplitude = std::pow(10.0, db / 20.0) * 32767.0;

  std::vector<std::int16_t> out(frames * static_cast<std::size_t>(channels), 0);
  const double dt = 1.0 / static_cast<double>(kAudioRate);

  for (std::size_t f = 0; f < frames; ++f) {
    double sample = 0.0;
    switch (cue.tone.waveform) {
      case ToneWaveform::Sine: {
        const double hz = std::clamp(cue.tone.frequencyHz, 20.0, 20000.0);
        tonePhase_ += 2.0 * 3.14159265358979323846 * hz * dt;
        if (tonePhase_ > 6.283185307179586) tonePhase_ -= 6.283185307179586;
        sample = std::sin(tonePhase_);
        break;
      }
      case ToneWaveform::White:
        sample = nextWhite(toneSeed_);
        break;
      case ToneWaveform::Fds:
        sample = (cue.tone.synth.chip == SynthChip::Nes)
                   ? nesNextSample(cue.tone, dt)
                   : fdsNextSample(cue.tone, dt);
        break;
      case ToneWaveform::Pink:
        sample = nextPink(tonePinkRows_, tonePinkRunning_, tonePinkCounter_, toneSeed_);
        break;
      case ToneWaveform::Sweep: {
        // Logarithmic, because a room's response is heard in octaves. A linear
        // sweep spends most of its time above 10kHz and tells you little.
        const double span = std::max(0.5, cue.tone.sweepSeconds);
        const double lo = std::clamp(cue.tone.sweepLowHz, 20.0, 19000.0);
        const double hi = std::clamp(cue.tone.sweepHighHz, lo + 1.0, 20000.0);
        toneSweepSeconds_ += dt;
        if (toneSweepSeconds_ > span) toneSweepSeconds_ -= span;
        const double t = toneSweepSeconds_ / span;
        const double hz = lo * std::pow(hi / lo, t);
        tonePhase_ += 2.0 * 3.14159265358979323846 * hz * dt;
        if (tonePhase_ > 6.283185307179586) tonePhase_ -= 6.283185307179586;
        sample = std::sin(tonePhase_);
        break;
      }
      case ToneWaveform::Identify: {
        const double hold = std::max(0.25, cue.tone.identifySecondsPerChannel);
        toneSweepSeconds_ += dt;
        if (toneSweepSeconds_ >= hold) {
          toneSweepSeconds_ -= hold;
          toneIdentifyChannel_ = (toneIdentifyChannel_ + 1) % channels;
        }
        tonePhase_ += 2.0 * 3.14159265358979323846 * 1000.0 * dt;
        if (tonePhase_ > 6.283185307179586) tonePhase_ -= 6.283185307179586;
        // Gate the last quarter second so consecutive channels are separated
        // by silence; without it a walk sounds like one unbroken tone.
        const bool gap = toneSweepSeconds_ > (hold - 0.25);
        sample = gap ? 0.0 : std::sin(tonePhase_);
        break;
      }
    }

    const std::int16_t v = static_cast<std::int16_t>(
      std::clamp(sample * amplitude, -32767.0, 32767.0));
    if (cue.tone.waveform == ToneWaveform::Identify) {
      out[f * channels + toneIdentifyChannel_] = v;   // one channel at a time
    } else if (cue.tone.channel >= 0 && cue.tone.channel < channels) {
      out[f * channels + cue.tone.channel] = v;       // a single named output
    } else {
      for (int c = 0; c < channels; ++c) out[f * channels + c] = v;
    }
  }

  // Keep the tail of what we just produced for the on-screen scope. Sized to
  // about 40ms, which is a few cycles at 100Hz and plenty at 1kHz.
  {
    const std::size_t keep = static_cast<std::size_t>(kAudioRate) / 25;
    if (toneScopeL_.size() != keep) {
      toneScopeL_.assign(keep, 0);
      toneScopeR_.assign(keep, 0);
      toneScopePos_ = 0;
    }
    for (std::size_t f = 0; f < frames; ++f) {
      toneScopeL_[toneScopePos_] = out[f * channels + 0];
      toneScopeR_[toneScopePos_] = out[f * channels + (channels > 1 ? 1 : 0)];
      toneScopePos_ = (toneScopePos_ + 1) % keep;
    }
  }

  // Feed the metering/egress tap. The tap is invoked from queueDelayedAudio,
  // which ONLY the decode path goes through -- a generated tone went straight
  // to putWideAudioToStream, the last write, which deliberately knows nothing
  // about metering. So the tone was audible while the VU meter sat still, and
  // the stream/record mix never saw a sample: every recording of a tone
  // measured -91 dBFS, digital silence.
  //
  // The tap is STEREO by contract (see queueDelayedAudio), so hand it the
  // first output pair. A tone deliberately routed to some other channel is
  // genuinely not in the stereo programme and correctly reads as silence.
  if (audioTap_) {
    std::vector<std::int16_t> stereo(frames * 2, 0);
    for (std::size_t f = 0; f < frames; ++f) {
      stereo[f * 2]     = out[f * static_cast<std::size_t>(channels)];
      stereo[f * 2 + 1] = out[f * static_cast<std::size_t>(channels) +
                              (channels > 1 ? 1 : 0)];
    }
    audioTap_(stereo);
  }

  // Device-width already: the generator addresses channels individually, so it
  // must NOT go through the stereo-and-widen path.
  putWideAudioToStream(out, channels);
}

void MediaEngine::rebuildToneFrame(const Cue& cue) {
  auto [w, h] = currentOutputSizeHint();
  const int W = std::max(64, w);
  const int H = std::max(64, h);
  DecodedFrame frame;
  frame.width = W;
  frame.height = H;
  frame.format = FramePixelFormat::RGBA32;
  frame.pixels.assign(static_cast<std::size_t>(W) * H * 4, 0);
  for (std::size_t i = 0; i < frame.pixels.size(); i += 4) {
    frame.pixels[i + 3] = 255;
  }

  // The card says what is actually going out. An engineer at the far end of a
  // room needs to read the level and the channel from the screen, because the
  // whole point is that they are NOT at the machine.
  const char* name = "SINE";
  switch (cue.tone.waveform) {
    case ToneWaveform::Pink:     name = "PINK NOISE"; break;
    case ToneWaveform::White:    name = "WHITE NOISE"; break;
    case ToneWaveform::Sweep:    name = "SWEEP"; break;
    case ToneWaveform::Identify: name = "IDENTIFY"; break;
    case ToneWaveform::Fds:      name = "FDS SYNTH"; break;
    default: break;
  }
  char levelText[48];
  std::snprintf(levelText, sizeof(levelText), "%.1f dBFS", cue.tone.levelDbfs);

  std::string detail;
  if (cue.tone.waveform == ToneWaveform::Sine) {
    char hz[32];
    std::snprintf(hz, sizeof(hz), "%.0f Hz", cue.tone.frequencyHz);
    detail = hz;
  } else if (cue.tone.waveform == ToneWaveform::Sweep) {
    char sw[48];
    std::snprintf(sw, sizeof(sw), "%.0f-%.0f Hz", cue.tone.sweepLowHz, cue.tone.sweepHighHz);
    detail = sw;
  } else if (cue.tone.waveform == ToneWaveform::Identify) {
    detail = "CH " + std::to_string(toneIdentifyChannel_ + 1);
  } else if (cue.tone.channel >= 0) {
    detail = "CH " + std::to_string(cue.tone.channel + 1);
  } else {
    detail = "ALL CH";
  }

  const int cell = std::max(3, W / 120);
  auto drawLine = [&](const std::string& text, int y, SDL_Color ink, int scale) {
    const int c = cell * scale;
    const int charW = c * 6;
    const int textW = static_cast<int>(text.size()) * charW;
    int x = (W - textW) / 2;
    for (char raw : text) {
      const unsigned char ch = static_cast<unsigned char>(std::toupper(raw));
      if (const std::uint8_t* rows = timerGlyph5x7(ch)) {
        for (int r = 0; r < 7; ++r) {
          for (int b = 0; b < 5; ++b) {
            if (rows[r] & (1 << (4 - b))) {
              fillTimerRect(frame, x + b * c, y + r * c, c, c, ink);
            }
          }
        }
      }
      x += charW;
    }
  };

  const SDL_Color amber {255, 190, 40, 255};
  const SDL_Color white {255, 255, 255, 255};
  const SDL_Color green {80, 255, 140, 255};
  const SDL_Color dim   {60, 90, 70, 255};

  // Draw the diagnostic BEHIND the text, so the card stays readable across it.
  if (cue.tone.visualEnabled && cue.tone.visual != ToneVisual::None &&
      !toneScopeL_.empty()) {
    const int plotH = H / 3;
    const int plotY = H / 2 - plotH / 2;
    const std::size_t n = toneScopeL_.size();
    const int dot = std::max(1, W / 400);

    if (cue.tone.visual == ToneVisual::Scope) {
      // Centre line first: without a reference an operator cannot see DC
      // offset or asymmetric clipping, which is half the point of a scope.
      fillTimerRect(frame, 0, plotY + plotH / 2, W, std::max(1, dot / 2), dim);
      for (int x = 0; x < W; ++x) {
        const std::size_t i = (toneScopePos_ + (static_cast<std::size_t>(x) * n) / W) % n;
        const double v = toneScopeL_[i] / 32768.0;
        const int y = plotY + plotH / 2 - static_cast<int>(v * (plotH / 2 - 2));
        fillTimerRect(frame, x, y, dot, dot, green);
      }
    } else if (cue.tone.visual == ToneVisual::Lissajous) {
      // X = channel 1, Y = channel 2. A diagonal means in phase, the opposite
      // diagonal means one channel is POLARITY INVERTED, and a circle means
      // 90 degrees out. This finds a miswired cable in seconds.
      const int size = plotH;
      const int cx = W / 2;
      const int cy = plotY + plotH / 2;
      fillTimerRect(frame, cx - size / 2, cy, size, std::max(1, dot / 2), dim);
      fillTimerRect(frame, cx, cy - size / 2, std::max(1, dot / 2), size, dim);
      for (std::size_t i = 0; i < n; ++i) {
        const double lx = toneScopeL_[i] / 32768.0;
        const double ly = toneScopeR_[i] / 32768.0;
        const int px = cx + static_cast<int>(lx * (size / 2 - 2));
        const int py = cy - static_cast<int>(ly * (size / 2 - 2));
        fillTimerRect(frame, px, py, dot, dot, green);
      }
    } else if (cue.tone.visual == ToneVisual::Spectrum) {
      // Goertzel per band rather than an FFT: 16 third-octave bands is a tiny
      // amount of work, needs no window, no buffer of a power-of-two length,
      // and third-octaves are how a PA is actually measured.
      const int bands = 16;
      const int barW = std::max(2, W / (bands * 2));
      const int gap = barW;
      const int totalW = bands * (barW + gap) - gap;
      int bx = (W - totalW) / 2;
      for (int b = 0; b < bands; ++b) {
        const double hz = 31.5 * std::pow(2.0, b / 3.0 * 1.0);
        if (hz > kAudioRate / 2.0) break;
        const double w = 2.0 * 3.14159265358979323846 * hz / kAudioRate;
        const double coeff = 2.0 * std::cos(w);
        double s0 = 0.0, s1 = 0.0, s2 = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
          s0 = (toneScopeL_[i] / 32768.0) + coeff * s1 - s2;
          s2 = s1;
          s1 = s0;
        }
        const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        const double mag = std::sqrt(std::max(0.0, power)) / static_cast<double>(n);
        // dB, floored at -60 so a silent band is a stub rather than nothing.
        const double db = 20.0 * std::log10(std::max(mag, 1e-6));
        const double norm = std::clamp((db + 60.0) / 60.0, 0.0, 1.0);
        const int barH = std::max(1, static_cast<int>(norm * plotH));
        fillTimerRect(frame, bx, plotY + plotH - barH, barW, barH, green);
        bx += barW + gap;
      }
    }
  }

  drawLine(name,      H / 2 - cell * 22, amber, 3);
  drawLine(levelText, H / 2 + cell * 2,  white, 2);
  drawLine(detail,    H / 2 + cell * 20, white, 2);

  displayFrame_ = std::move(frame);
  displayFrame_->index = ++displayFrameSerial_;
  uploadFrame(*displayFrame_);
}


void MediaEngine::buildTimerFrame(DecodedFrame& frame, const TimerSettings& cfg,
                                  double elapsedSeconds, bool running) {
  const int W = frame.width;
  const int H = frame.height;
  if (W <= 0 || H <= 0 || frame.pixels.size() < static_cast<std::size_t>(W) * H * 4) {
    return;
  }

  // MODE decides what the clock shows. It was previously declared and cycled in
  // the inspector but never read here, so count-up and time-of-day rendered
  // identically to a countdown -- the same "wired to state but not to effect"
  // bug the encoder format picker had, found by auditing for it.
  double remaining = 0.0;
  bool overtime = false;
  int totalSeconds = 0;
  if (cfg.mode == TimerMode::TimeOfDay) {
    const std::time_t nowT = std::time(nullptr);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &nowT);
#else
    localtime_r(&nowT, &local);
#endif
    totalSeconds = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
    remaining = 1e9;              // a wall clock never runs low
  } else if (cfg.mode == TimerMode::CountUp) {
    totalSeconds = std::max(0, static_cast<int>(std::floor(elapsedSeconds)));
    remaining = static_cast<double>(cfg.durationSeconds) - elapsedSeconds;
    overtime = remaining < 0.0;
  } else {
    remaining = static_cast<double>(cfg.durationSeconds) - elapsedSeconds;
    overtime = remaining < 0.0;
    const double rawSeconds = overtime ? std::floor(-remaining) : std::ceil(remaining);
    totalSeconds = std::max(0, static_cast<int>(rawSeconds));
  }
  const int hours = totalSeconds / 3600;
  const int minutes = (totalSeconds % 3600) / 60;
  const int seconds = totalSeconds % 60;

  // Thresholds are SECONDS REMAINING, so amber 60 means "a minute left".
  // A packed override of -1 means "keep the built-in", so a show that predates
  // custom colours renders exactly as it always did.
  auto pick = [](int packed, SDL_Color fallback) {
    if (packed < 0) return fallback;
    return SDL_Color {static_cast<Uint8>((packed >> 16) & 0xFF),
                      static_cast<Uint8>((packed >> 8) & 0xFF),
                      static_cast<Uint8>(packed & 0xFF), 255};
  };
  SDL_Color ink = pick(cfg.colorNormal, SDL_Color {255, 255, 255, 255});
  if (overtime || remaining <= static_cast<double>(cfg.redSeconds)) {
    ink = pick(cfg.colorRed, SDL_Color {255, 64, 64, 255});
  } else if (remaining <= static_cast<double>(cfg.amberSeconds)) {
    ink = pick(cfg.colorAmber, SDL_Color {255, 190, 40, 255});
  }
  const SDL_Color bg = pick(cfg.colorBackground, SDL_Color {0, 0, 0, 255});

  // Blink keyed to wall-clock elapsed, so an overtime clock keeps flashing even
  // while the ticker is held. The source app is explicit that a stopped
  // overtime clock must still be visibly angry.
  bool blankThisFrame = false;
  if (cfg.blinkAtZero && overtime) {
    blankThisFrame = (static_cast<int>(std::floor(elapsedSeconds * 2.0)) % 2) == 1;
  }

  for (std::size_t i = 0; i < frame.pixels.size(); i += 4) {
    // RGBA32, so byte 0 is RED. This wrote blue into the red slot and vice
    // versa, which transposed every custom background: amber came out cyan
    // and blue came out amber. fillTimerRect three hundred lines up writes
    // r,g,b in order -- so the digits were right and only the background was
    // wrong, which is exactly how it presented.
    frame.pixels[i + 0] = bg.r;
    frame.pixels[i + 1] = bg.g;
    frame.pixels[i + 2] = bg.b;
    frame.pixels[i + 3] = 255;
  }
  if (blankThisFrame) {
    return;
  }

  std::vector<int> glyphs;            // 0-9 digit, 10 plus, -1 colon
  // Time of day never shows a "+" and always shows h:mm:ss.
  if (overtime && cfg.mode != TimerMode::TimeOfDay) glyphs.push_back(10);
  if (hours > 0 || cfg.mode == TimerMode::TimeOfDay) {
    glyphs.push_back(hours % 10);
    glyphs.push_back(-1);
    glyphs.push_back(minutes / 10);
    glyphs.push_back(minutes % 10);
  } else {
    if (minutes >= 10) glyphs.push_back(minutes / 10);
    glyphs.push_back(minutes % 10);
  }
  glyphs.push_back(-1);
  glyphs.push_back(seconds / 10);
  glyphs.push_back(seconds % 10);

  int colonCount = 0;
  for (int g : glyphs) {
    if (g == -1) ++colonCount;
  }
  const int digitCount = std::max(1, static_cast<int>(glyphs.size()) - colonCount);
  const int usableW = W * 82 / 100;
  const int gap = std::max(2, W / 120);
  int digitW = (usableW - gap * (static_cast<int>(glyphs.size()) - 1) -
                colonCount * (usableW / 24)) / digitCount;
  digitW = std::max(4, digitW);
  int digitH = std::min(H * 55 / 100, digitW * 9 / 5);
  digitW = std::min(digitW, std::max(4, digitH * 5 / 9));
  const int thick = std::max(2, digitH / 8);
  const int colonW = std::max(thick, digitW / 3);

  int totalW = 0;
  for (std::size_t i = 0; i < glyphs.size(); ++i) {
    totalW += (glyphs[i] == -1 ? colonW : digitW);
    if (i + 1 < glyphs.size()) totalW += gap;
  }
  int x = (W - totalW) / 2;
  const int y = (H - digitH) / 2;

  for (std::size_t i = 0; i < glyphs.size(); ++i) {
    if (glyphs[i] == -1) {
      const int dotH = std::max(thick, digitH / 10);
      fillTimerRect(frame, x + (colonW - thick) / 2, y + digitH / 3 - dotH / 2,
                    thick, dotH, ink);
      fillTimerRect(frame, x + (colonW - thick) / 2, y + digitH * 2 / 3 - dotH / 2,
                    thick, dotH, ink);
      x += colonW + gap;
    } else if (glyphs[i] == 10) {
      // Plus, drawn as a cross. Seven segments cannot express one -- a bare
      // middle bar reads as a MINUS, which states the opposite of the truth:
      // "-0:20" looks like twenty seconds remaining when the speaker is twenty
      // seconds over.
      const int armW = digitW * 3 / 4;
      const int cx = x + digitW / 2;
      const int cy = y + digitH / 2;
      fillTimerRect(frame, cx - armW / 2, cy - thick / 2, armW, thick, ink);
      fillTimerRect(frame, cx - thick / 2, cy - armW / 2, thick, armW, ink);
      x += digitW + gap;
    } else if (cfg.face == TimerFace::Blocky) {
      // Same 5x7 table the message line uses, scaled to digit height. A second
      // real face without reaching for a TTF the engine cannot see.
      const std::uint8_t* rows =
        timerGlyph5x7(static_cast<unsigned char>(48 + glyphs[i]));
      if (rows) {
        const int cw = std::max(1, digitW / 5);
        const int ch = std::max(1, digitH / 7);
        for (int r = 0; r < 7; ++r) {
          for (int b = 0; b < 5; ++b) {
            if (rows[r] & (1 << (4 - b))) {
              fillTimerRect(frame, x + b * cw, y + r * ch, cw, ch, ink);
            }
          }
        }
      }
      x += digitW + gap;
    } else {
      drawSevenSeg(frame, glyphs[i], x, y, digitW, digitH, thick, ink);
      x += digitW + gap;
    }
  }

  // Progress bar across the foot of the frame. Stagetimer charges for this and
  // it is genuinely useful: from the back of a room the bar is readable when
  // the digits are not, because length is easier to judge at distance than a
  // number is to read.
  if (cfg.showProgressBar && cfg.durationSeconds > 0 &&
      cfg.mode != TimerMode::TimeOfDay) {
    const double fraction =
      std::clamp(elapsedSeconds / static_cast<double>(cfg.durationSeconds), 0.0, 1.0);
    const int barH = std::max(4, H / 36);
    const int barY = H - barH - H / 24;
    const int inset = W / 12;
    const int barW = W - inset * 2;
    // Track first, then fill, so the remaining portion still reads as "space
    // left" rather than as empty background.
    SDL_Color track {40, 40, 40, 255};
    fillTimerRect(frame, inset, barY, barW, barH, track);
    const int fillW = static_cast<int>(barW * (1.0 - fraction));
    if (fillW > 0) {
      fillTimerRect(frame, inset, barY, fillW, barH, ink);
    }
  }

  // Message under the clock. Drawn as a blocky 5x7 bitmap for the same reason
  // the digits are seven-segment: a stage screen must not depend on which fonts
  // happen to be installed on the machine driving it.
  if (!cfg.message.empty()) {
    const int cell = std::max(2, W / 200);
    const int charW = cell * 6;
    // Placeholders, so one message can serve a whole show: "{remaining} left"
    // reads correctly at every point in the talk without the operator retyping
    // it. Substituted here rather than when the message is set, because the
    // values change every frame.
    std::string expanded = cfg.message;
    {
      auto two = [](int v) {
        char b[8];
        std::snprintf(b, sizeof(b), "%02d", v);
        return std::string(b);
      };
      const int remSecs = std::max(0, static_cast<int>(std::llround(std::fabs(remaining))));
      const int elaSecs = std::max(0, static_cast<int>(std::llround(elapsedSeconds)));
      auto clockText = [&](int total) {
        const int h = total / 3600;
        const std::string ms = two((total % 3600) / 60) + ":" + two(total % 60);
        return h > 0 ? (std::to_string(h) + ":" + ms) : ms;
      };
      std::time_t nowT = std::time(nullptr);
      char wall[16] = "";
      if (std::tm* lt = std::localtime(&nowT)) {
        std::strftime(wall, sizeof(wall), "%H:%M", lt);
      }
      const std::pair<const char*, std::string> kTokens[] = {
        {"{remaining}", clockText(remSecs)},
        {"{elapsed}",   clockText(elaSecs)},
        {"{duration}",  clockText(std::max(0, cfg.durationSeconds))},
        {"{time}",      wall},
      };
      for (const auto& [token, value] : kTokens) {
        for (std::size_t at = expanded.find(token);
             at != std::string::npos;
             at = expanded.find(token, at + value.size())) {
          expanded.replace(at, std::strlen(token), value);
        }
      }
    }
    const std::string msg = expanded.size() > 40 ? expanded.substr(0, 40) : expanded;
    const int textW = static_cast<int>(msg.size()) * charW;
    int mx = (W - textW) / 2;
    const int my = H - H / 5;
    SDL_Color msgInk = cfg.messageIsUrgent ? SDL_Color {255, 64, 64, 255}
                                           : SDL_Color {255, 255, 255, 255};
    for (char raw : msg) {
      const unsigned char c = static_cast<unsigned char>(std::toupper(raw));
      const std::uint8_t* rows = timerGlyph5x7(c);
      if (rows) {
        for (int r = 0; r < 7; ++r) {
          for (int b = 0; b < 5; ++b) {
            if (rows[r] & (1 << (4 - b))) {
              fillTimerRect(frame, mx + b * cell, my + r * cell, cell, cell, msgInk);
            }
          }
        }
      }
      mx += charW;
    }
  }

  // Event logo above the clock. Alpha-composited over whatever is already
  // there, so a PNG with transparency keeps its shape against the backdrop.
  if (!cfg.logoPath.empty()) {
    const int logoH = std::max(1, H * std::clamp(cfg.logoHeightPercent, 2, 40) / 100);
    if (const TimerLogoCache* logo = timerLogoFor(cfg.logoPath, logoH)) {
      const int lx = (W - logo->w) / 2;
      const int ly = H / 24;
      for (int y = 0; y < logo->h; ++y) {
        const int dy = ly + y;
        if (dy < 0 || dy >= H) continue;
        for (int x = 0; x < logo->w; ++x) {
          const int dx = lx + x;
          if (dx < 0 || dx >= W) continue;
          const std::size_t s = (static_cast<std::size_t>(y) * logo->w + x) * 4;
          const int a = logo->rgba[s + 3];
          if (a == 0) continue;
          const std::size_t o = (static_cast<std::size_t>(dy) * W + dx) * 4;
          // Frame is BGRA; the decoded logo is RGBA.
          frame.pixels[o + 0] = static_cast<std::uint8_t>(
            (logo->rgba[s + 2] * a + frame.pixels[o + 0] * (255 - a)) / 255);
          frame.pixels[o + 1] = static_cast<std::uint8_t>(
            (logo->rgba[s + 1] * a + frame.pixels[o + 1] * (255 - a)) / 255);
          frame.pixels[o + 2] = static_cast<std::uint8_t>(
            (logo->rgba[s + 0] * a + frame.pixels[o + 2] * (255 - a)) / 255);
        }
      }
    }
  }

  // Paused marker: two bars top-left, so an operator glancing at the stage
  // screen can tell a held clock from a running one.
  if (!running) {
    const int bw = std::max(3, W / 90);
    const int bh = bw * 4;
    fillTimerRect(frame, W / 40, H / 40, bw, bh, ink);
    fillTimerRect(frame, W / 40 + bw * 2, H / 40, bw, bh, ink);
  }
}

void MediaEngine::buildTestClock(DecodedFrame& frame, double t) {
  const int W = frame.width;
  const int H = frame.height;
  if (W <= 0 || H <= 0) {
    return;
  }
  t = std::max(0.0, t);
  auto rect = [&](int x, int y, int w, int h, SDL_Color c) {
    fillPixelRect(frame, x, y, w, h, c);
  };
  auto hueToRgb = [](double h) {
    h = h - std::floor(h);
    double r = std::clamp(std::abs(std::fmod(h * 6.0 + 0.0, 6.0) - 3.0) - 1.0, 0.0, 1.0);
    double g = std::clamp(std::abs(std::fmod(h * 6.0 + 4.0, 6.0) - 3.0) - 1.0, 0.0, 1.0);
    double b = std::clamp(std::abs(std::fmod(h * 6.0 + 2.0, 6.0) - 3.0) - 1.0, 0.0, 1.0);
    return SDL_Color {static_cast<Uint8>(r * 255), static_cast<Uint8>(g * 255),
                      static_cast<Uint8>(b * 255), 255};
  };

  // ── 1. Saturated bars ──
  static const SDL_Color kBars[8] = {
    {0, 0, 0, 255},     {255, 0, 0, 255},   {255, 0, 255, 255}, {0, 0, 255, 255},
    {255, 255, 0, 255}, {0, 255, 0, 255},   {0, 255, 255, 255}, {255, 255, 255, 255},
  };
  for (int i = 0; i < 8; ++i) {
    int x0 = W * i / 8;
    int x1 = W * (i + 1) / 8;
    rect(x0, 0, x1 - x0, H, kBars[i]);
  }

  // ── 2. Aspect-truth circle ──
  // Drawn from the true pixel radius, not a fraction of each axis, so a
  // stretched raster shows an ellipse. Column-wise so it costs one rect per x.
  {
    double cxF = W * 0.5;
    double cyF = H * 0.5;
    double radius = H * 0.46;
    int thickness = std::max(2, H / 150);
    SDL_Color ring {255, 255, 255, 255};
    SDL_Color shadow {0, 0, 0, 255};
    int x0 = std::max(0, static_cast<int>(cxF - radius) - thickness);
    int x1 = std::min(W - 1, static_cast<int>(cxF + radius) + thickness);
    for (int x = x0; x <= x1; ++x) {
      double dx = x + 0.5 - cxF;
      double inside = radius * radius - dx * dx;
      if (inside < 0.0) {
        continue;
      }
      int dy = static_cast<int>(std::lround(std::sqrt(inside)));
      // A thin dark rim under the white keeps the ring legible where it
      // crosses the white bar.
      rect(x, static_cast<int>(cyF) - dy - thickness, 1, thickness, shadow);
      rect(x, static_cast<int>(cyF) - dy, 1, thickness, ring);
      rect(x, static_cast<int>(cyF) + dy - thickness, 1, thickness, ring);
      rect(x, static_cast<int>(cyF) + dy, 1, thickness, shadow);
    }
  }

  // ── 3. Scrolling hue band ──
  {
    int bandH = std::max(4, H * 11 / 100);
    int bandY = (H - bandH) / 2;
    double scroll = t * 0.25;   // one full wrap every 4s
    for (int x = 0; x < W; ++x) {
      double frac = static_cast<double>(x) / std::max(1, W - 1);
      rect(x, bandY, 1, bandH, hueToRgb(frac + scroll));
    }
  }

  // ── 4. The sync read: huge seconds + frame counter ──
  {
    auto glyphRows = [](char c) -> const std::uint8_t* {
      static const std::uint8_t digits[10][5] = {
        {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,3,1,7}, {5,5,7,1,1},
        {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,2,2}, {7,5,7,5,7}, {7,5,7,1,7},
      };
      static const std::uint8_t colon[5] = {0,2,0,2,0};
      static const std::uint8_t dot[5]   = {0,0,0,0,2};
      static const std::uint8_t letterF[5] = {7,4,6,4,4};
      static const std::uint8_t letterT[5] = {7,2,2,2,2};
      static const std::uint8_t blank[5] = {0,0,0,0,0};
      if (c >= '0' && c <= '9') return digits[c - '0'];
      if (c == ':') return colon;
      if (c == '.') return dot;
      if (c == 'F') return letterF;
      if (c == 'T') return letterT;
      return blank;
    };
    auto drawGlyphs = [&](int x, int y, const std::string& s, int scale, SDL_Color color) {
      int cx = x;
      for (char c : s) {
        const std::uint8_t* rows = glyphRows(c);
        for (int ry = 0; ry < 5; ++ry) {
          for (int rx = 0; rx < 3; ++rx) {
            if (rows[ry] & (4 >> rx)) {
              rect(cx + rx * scale, y + ry * scale, scale, scale, color);
            }
          }
        }
        cx += 4 * scale;
      }
    };

    int totalSeconds = static_cast<int>(t);
    long long frameNo = static_cast<long long>(t * 30.0);   // nominal 30 fps counter

    // Big block: seconds (2 digits) over the frame counter (3 digits), on a
    // black plate so it stays readable over every bar colour.
    char bigSeconds[8];
    std::snprintf(bigSeconds, sizeof(bigSeconds), "%02d", totalSeconds % 100);
    char bigFrames[8];
    std::snprintf(bigFrames, sizeof(bigFrames), "%03lld", frameNo % 1000);
    int bigScale = std::max(2, H / 26);
    int smallScale = std::max(1, bigScale / 2);
    int bigW = 2 * 4 * bigScale - bigScale;
    int smallW = 3 * 4 * smallScale - smallScale;
    int plateW = std::max(bigW, smallW) + bigScale * 2;
    int plateH = (5 * bigScale) + bigScale + (5 * smallScale) + bigScale * 2;
    int plateX = std::clamp(static_cast<int>(W * 0.5 + H * 0.46) - plateW - bigScale,
                            0, std::max(0, W - plateW));
    int plateY = std::clamp((H - plateH) / 2, 0, std::max(0, H - plateH));
    rect(plateX, plateY, plateW, plateH, {0, 0, 0, 255});
    drawGlyphs(plateX + (plateW - bigW) / 2, plateY + bigScale, bigSeconds, bigScale,
               {255, 255, 255, 255});
    drawGlyphs(plateX + (plateW - smallW) / 2, plateY + bigScale + 5 * bigScale + bigScale,
               bigFrames, smallScale, {255, 214, 92, 255});

    // Corner readout: exact timecode + absolute frame, for frame-accurate
    // comparison between two captures.
    int millis = static_cast<int>(t * 1000.0) % 1000;
    char clockLine[32];
    std::snprintf(clockLine, sizeof(clockLine), "T %02d:%02d:%02d.%03d",
                  (totalSeconds / 3600) % 100, (totalSeconds / 60) % 60,
                  totalSeconds % 60, millis);
    char frameLine[24];
    std::snprintf(frameLine, sizeof(frameLine), "F %06lld", frameNo);
    int scale = std::max(1, H / 240);
    int pad = 3 * scale;
    int boxW = 14 * 4 * scale + pad * 2;
    int boxH = (5 + 2 + 5) * scale + pad * 2;
    rect(8, 8, boxW, boxH, {12, 12, 12, 255});
    drawGlyphs(8 + pad, 8 + pad, clockLine, scale, {255, 214, 92, 255});
    drawGlyphs(8 + pad, 8 + pad + 7 * scale, frameLine, scale, {255, 214, 92, 255});
  }
}

// ---------------------------------------------------------------------------
// buildPatternFrame — Static factory: generate a procedural test pattern frame.
//
// This is the main dispatch function for all pattern types. It:
//   1. Determines frame dimensions (from cue, or fallback to output size)
//   2. Normalizes the pattern type ID (handles aliases, -motion suffix)
//   3. Dispatches to the appropriate builder (SMPTE bars, crosshatch, etc.)
//   4. For -motion variants, applies animation overlays (scan lines, scrolling)
//   5. For solid color patterns (-motion), applies a pulsing brightness effect
//
// Returns nullopt only if something goes wrong (shouldn't happen in practice).
// The animTime parameter drives all animation — 0.0 gives the static initial state.
// ---------------------------------------------------------------------------
// Fill a CALLER-OWNED frame. This exists so the per-frame path can reuse one
// buffer forever instead of allocating a new full-raster one every rebuild.
//
// That allocation was a genuine bug, not a micro-optimisation: at 3840x2160 a
// pattern frame is 33 MB, and rebuildPatternFrame runs once per output frame,
// so an animated pattern churned ~2 GB/s. Committed memory ballooned to 40+ GB
// (oscillating, not leaking — the allocator simply could not recycle pages that
// fast), the system commit limit was exhausted, available RAM hit zero, and the
// WHOLE MACHINE started paging. It presented as "Deckboy is laggy" and as
// "my computer is slow", and it made every performance measurement meaningless.
//
// vector::assign on a vector that is already the right size reuses its capacity,
// so once the raster settles this allocates nothing at all.
void MediaEngine::buildPatternFrameInto(DecodedFrame& frame, const Cue& cue, double animTime,
                                        int fallbackWidth, int fallbackHeight) {
  // Patterns ALWAYS build at the live output raster (the fallback hint —
  // rebuildPatternFrame feeds it the current program-output size). A test
  // pattern that isn't pixel-mapped to the selected display is lying, and
  // backgrounds want native resolution too. The cue's stored size is only
  // a last resort when no hint exists.
  int sourceW = fallbackWidth > 0 ? fallbackWidth : cue.width;
  int sourceH = fallbackHeight > 0 ? fallbackHeight : cue.height;

  // ── Reset EVERY non-pixel field before reuse ──────────────────────────────
  // The caller's frame may be the one that previously held a hardware-decoded
  // VIDEO frame, because displayFrame_ persists across a cue change. Leaving
  // that state behind is not cosmetic:
  //   * gpuTexture non-null makes isGpu() true, so the compositor would render
  //     a stale decoder surface instead of this pattern;
  //   * gpuFrameRef would pin that decoder surface alive indefinitely;
  //   * a leftover NV12 `format` would send RGBA bytes through
  //     SDL_UpdateNVTexture.
  // A freshly-constructed DecodedFrame got these defaults for free; a reused
  // one must be given them explicitly.
  frame.gpuFrameRef.reset();
  frame.gpuTexture = nullptr;
  frame.gpuSubresource = 0;
  frame.gpuDevice = nullptr;
  frame.format = FramePixelFormat::RGBA32;
  frame.presentationSeconds = -1.0;
  frame.index = 0;

  // Size + clear. Every pattern is drawn over an opaque white ground, so this
  // also guarantees no stale pixels survive from the previous frame when the
  // buffer is reused.
  frame.width  = std::max(320, sourceW);
  frame.height = std::max(180, sourceH);
  frame.index  = 0;
  frame.pixels.assign(static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4u, 255);

  // Normalize pattern type and detect -motion variant
  std::string patternType = normalizePatternTypeId(cue.path);
  std::string basePatternType = stripPatternMotionSuffix(patternType);
  bool motion = endsWith(patternType, "-motion");

  // ── Pattern dispatch ──
  // Each branch builds the base pattern, then adds motion overlays if -motion is active.

  // Motion policy (the owner, v0.78.7): ALL pattern motion is slow, smooth and
  // DIAGONAL — a 45° drift reads as motion on any raster; axis-only scroll
  // barely registers on a grid.
  if (basePatternType == "smpte-bars") {
    buildSmpte75Bars(frame);
    if (motion) {
      // Slow diagonal sweep line (12 s per pass) — the bars stay put as the
      // color reference; the sweep is the motion/judder element.
      int span = frame.width + frame.height;
      int c = static_cast<int>(std::floor(normalizedLoopProgress(animTime, 12.0) * span));
      for (int y = 0; y < frame.height; ++y) {
        int x = (c - y) % span;
        if (x < 0) x += span;
        fillPixelRect(frame, x - 1, y, 3, 1, {255, 255, 255, 255});
        fillPixelRect(frame, x + 2, y, 1, 1, {8, 8, 8, 255});
      }
    }
  } else if (basePatternType == "crosshatch") {
    // Diagonal drift: one grid cell every 8 s, X and Y in lockstep (45°).
    int phase = motion ? phaseFromProgress(normalizedLoopProgress(animTime, 8.0), 64) : 0;
    buildCrosshatch(frame, phase, phase);
  } else if (basePatternType == "checkerboard" || basePatternType == "checker") {
    // Diagonal drift: one checker period every 10 s, X and Y in lockstep.
    int phase = motion ? phaseFromProgress(normalizedLoopProgress(animTime, 10.0), 128) : 0;
    buildCheckerboard(frame, phase, phase);
  // Solid color patterns are always static (no motion variant — a pulsing
  // reference level is a contradiction).
  } else if (basePatternType == "full-white") {
    fillPixelRect(frame, 0, 0, frame.width, frame.height, {255, 255, 255, 255});
  } else if (basePatternType == "full-black") {
    fillPixelRect(frame, 0, 0, frame.width, frame.height, {0, 0, 0, 255});
  } else if (basePatternType == "full-red") {
    fillPixelRect(frame, 0, 0, frame.width, frame.height, {255, 0, 0, 255});
  } else if (basePatternType == "full-green") {
    fillPixelRect(frame, 0, 0, frame.width, frame.height, {0, 255, 0, 255});
  } else if (basePatternType == "full-blue") {
    fillPixelRect(frame, 0, 0, frame.width, frame.height, {0, 0, 255, 255});
  } else if (basePatternType == "test-bars") {
    // Broadcast motion diagnostics — always animated, no -motion variant.
    buildTestBars(frame, animTime);
  } else if (basePatternType == "test-clock") {
    // Sync/latency card — always animated, no -motion variant.
    buildTestClock(frame, animTime);
  } else if (basePatternType == "terrarium") {
    // Native Terrarium — the living ecosystem, ticking at its own 9 TPS.
    //
    // The world is a fixed 200x112 CELL grid, so the raster can only be a whole
    // number of pixels per cell. Derive that from the requested width instead
    // of hardcoding 8: this pattern used to ignore the size it was asked for
    // entirely (--pattern-dump terrarium out.ppm 320x180 produced 1600x896,
    // while every other pattern honoured the argument), and on a 4K output it
    // was upscaling a 1600px image rather than rendering closer to native.
    // Still quantised to whole cells — 200x112 is the grid, not a suggestion.
    const int terraCellPx = std::clamp(frame.width / terra::W, 1, 24);
    buildTerrariumFrame(frame, animTime, terraCellPx);
  } else if (basePatternType == "terrarium-pico") {
    // The same world, one pixel per cell — exactly the picture on the Pi's LED
    // panel. Deliberately tiny (200x112); Deckboy's textures are nearest-
    // filtered, so it scales up to the output as crisp hard-edged pixels.
    buildTerrariumFrame(frame, animTime, 1);
  // Pocket scene variants: force a specific scene index (0=day, 1=sunset, 2=night, 3=storm)
  } else if (basePatternType == "pocket-day") {
    buildPocketTest(frame, animTime, 0);
  } else if (basePatternType == "pocket-sunset") {
    buildPocketTest(frame, animTime, 1);
  } else if (basePatternType == "pocket-night") {
    buildPocketTest(frame, animTime, 2);
  } else if (basePatternType == "pocket-storm") {
    buildPocketTest(frame, animTime, 3);
  } else {
    // Default / "pocket-test": the test card — scene cycle with crossfade
    // plus the diegetic instrumentation overlay.
    buildPocketTestCard(frame, animTime);
  }
}

// One-shot convenience wrapper. Used by --pattern-dump, --pattern-bench, the
// smoke suite and loadPatternFrame — places that build a single frame and do
// not care about reuse. The per-frame path must use buildPatternFrameInto.
std::optional<DecodedFrame> MediaEngine::buildPatternFrame(const Cue& cue, double animTime,
                                                           int fallbackWidth, int fallbackHeight) {
  DecodedFrame frame;
  buildPatternFrameInto(frame, cue, animTime, fallbackWidth, fallbackHeight);
  if (frame.pixels.empty()) {
    return std::nullopt;
  }
  return frame;
}
