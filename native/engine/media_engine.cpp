// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "engine/media_engine.hpp"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

#include "core/cue_helpers.hpp"
#include "core/io_utils.hpp"
#include "core/pattern_helpers.hpp"
#include "core/pixel_effects.hpp"
#include "core/subprocess.hpp"
#include "core/utils.hpp"
#include "platform/capture_backend.hpp"

#ifndef _WIN32
#include <unistd.h>
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

} // namespace

MediaEngine::~MediaEngine() {
  stopAll();
}

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
  frameRate_ = 0.0;
  lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
  displayFrameSerial_ = 0;
  displayFrame_.reset();
  resetMediaFpsTelemetry();
}

void MediaEngine::loadCue(const Cue* cue, bool autoplay, double transitionSeconds,
                           TransitionStyle transitionStyle, bool suppressFadeIn) {
  float outgoingGain = transitionSourceGainForLoadCue(activeCue_, state_, visualFadeGainAt(position()));
  stopDecoderThreads();
  beginTransition(transitionSeconds, transitionStyle, outgoingGain);
  clearTexture();
  clearAudio();
  activeCue_ = cue;
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
  suppressFadeInForCurrentCue_ = suppressFadeIn;
  suppressVisualFadeOutForCurrentCue_ =
    cue && (cueAdvancesWhenFinished(*cue) || resolvedCueEndAction(*cue) == CueEndAction::Loop);

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
  if (audioDevice_ != 0) {
    SDL_PauseAudioDevice(audioDevice_, autoplay ? 0 : 1);
  }
}

void MediaEngine::refreshActiveCueRuntime() {
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
  if (audioDevice_ != 0) {
    SDL_PauseAudioDevice(audioDevice_, state_ == TransportState::Playing ? 0 : 1);
  }
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
  playbackClockStart_ = std::chrono::steady_clock::now();
  playbackStartPosition_ = pausedPosition_;
  state_ = TransportState::Playing;
  if (audioDevice_ != 0 && (activeCue_->kind == CueKind::Video || activeCue_->kind == CueKind::Audio)) {
    SDL_PauseAudioDevice(audioDevice_, 0);
  }
}

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
  if (audioDevice_ != 0 && (activeCue_->kind == CueKind::Video || activeCue_->kind == CueKind::Audio)) {
    SDL_PauseAudioDevice(audioDevice_, 1);
  }
}

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

void MediaEngine::stop() {
  if (!activeCue_) {
    return;
  }
  clearVisualOnReachedEnd_ = false;
  if (isSourceCueKind(activeCue_->kind)) {
    if (isSourceCapturing_) {
      stopDecoderThreads();
    }
    isSourceCapturing_ = false;
    loadSourceFrame(*activeCue_);
    state_ = TransportState::Paused;
    pausedPosition_ = 0.0;
    currentPosition_ = 0.0;
    return;
  }
  if (activeCue_->kind != CueKind::Video) {
    state_ = TransportState::Paused;
    pausedPosition_ = 0.0;
    currentPosition_ = 0.0;
    return;
  }
  seek(0.0, false);
  state_ = TransportState::Stopped;
  pausedPosition_ = 0.0;
  currentPosition_ = 0.0;
  if (audioDevice_ != 0) {
    SDL_PauseAudioDevice(audioDevice_, 1);
  }
}

void MediaEngine::clear() {
  stopAll();
}

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
  if (audioDevice_ != 0) {
    SDL_PauseAudioDevice(audioDevice_, state_ == TransportState::Playing ? 0 : 1);
  }
}

void MediaEngine::setVolume(float value) {
  volume_.store(std::clamp(value, 0.0f, 1.0f));
}

void MediaEngine::setPausePoints(std::vector<double> points) {
  std::sort(points.begin(), points.end());
  pausePoints_ = std::move(points);
  double pos = position();
  nextPausePointIdx_ = 0;
  while (nextPausePointIdx_ < pausePoints_.size() && pausePoints_[nextPausePointIdx_] <= pos) {
    ++nextPausePointIdx_;
  }
}

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

bool MediaEngine::reachedEnd() {
  if (reachedEnd_) {
    reachedEnd_ = false;
    return true;
  }
  return false;
}

void MediaEngine::finalizeReachedEnd(bool keepVisibleFrame) {
  if (!keepVisibleFrame && clearVisualOnReachedEnd_) {
    displayFrame_.reset();
    clearTexture();
  }
  clearVisualOnReachedEnd_ = false;
}

void MediaEngine::update() {
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
      currentPosition_ = 0.0;
    }
    return;
  }

  currentPosition_ = position();

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

  if (displayFrame_) {
    uploadFrame(*displayFrame_);
  }

  if (state_ == TransportState::Playing && duration_ > 0.0 && currentPosition_ >= duration_ - 0.01) {
    handlePlaybackEnd();
  }

  if (state_ == TransportState::Playing && decoderEof_ && queuedFrames() == 0 && currentPosition_ >= duration_ - 0.02) {
    handlePlaybackEnd();
  }
}

void MediaEngine::resetMediaFpsTelemetry() {
  mediaFpsSampleStartedAtMs_ = 0;
  mediaFpsFrameCount_ = 0;
  mediaFpsMeasured_ = 0.0;
  lastMeasuredMediaFrameIndex_ = static_cast<std::uint64_t>(-1);
}

bool MediaEngine::shouldMeasureMediaFps() const {
  return activeCue_ &&
         (activeCue_->kind == CueKind::Video || isBrowserCapturing_ || isSourceCapturing_);
}

void MediaEngine::recordMediaFrameAdvance(std::uint64_t frameIndex) {
  if (frameIndex == static_cast<std::uint64_t>(-1) ||
      frameIndex == lastMeasuredMediaFrameIndex_) {
    return;
  }
  lastMeasuredMediaFrameIndex_ = frameIndex;
  Uint64 now = SDL_GetTicks64();
  if (mediaFpsSampleStartedAtMs_ == 0) {
    mediaFpsSampleStartedAtMs_ = now;
    mediaFpsFrameCount_ = 0;
    mediaFpsMeasured_ = 0.0;
  }
  mediaFpsFrameCount_ += 1;
  Uint64 elapsedMs = now - mediaFpsSampleStartedAtMs_;
  if (elapsedMs >= 750) {
    mediaFpsMeasured_ = elapsedMs > 0
      ? (static_cast<double>(mediaFpsFrameCount_) * 1000.0 / static_cast<double>(elapsedMs))
      : mediaFpsMeasured_;
    mediaFpsFrameCount_ = 0;
    mediaFpsSampleStartedAtMs_ = now;
  }
}

const DecodedFrame* MediaEngine::currentFrame() const {
  return displayFrame_.has_value() ? &(*displayFrame_) : nullptr;
}

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

void MediaEngine::rebuildPatternFrame(const Cue& cue, double wallSeconds) {
  auto [fallbackW, fallbackH] = currentOutputSizeHint();
  auto frame = buildPatternFrame(cue, wallSeconds, fallbackW, fallbackH);
  if (frame) {
    frame->index = ++displayFrameSerial_;
    displayFrame_ = std::move(frame);
    uploadFrame(*displayFrame_);
  }
}

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

  deckboy::platform::SourceCaptureRequest request;
  request.kind = deckboy::platform::SourceCaptureKind::Window;
  request.sourceRef = trim(displayId);
  if (!request.sourceRef.empty() && request.sourceRef.front() == ':' &&
      request.sourceRef.find('.') == std::string::npos) {
    request.sourceRef += ".0";
  }
  request.width = w;
  request.height = h;
  request.frameRate = 30;
  request.drawMouse = false;
  auto plan = deckboy::platform::planSourceCapture(request);
  if (!plan.supported || plan.ffmpegArgs.empty()) {
    return false;
  }
  if (!spawnPipeProcess(videoProcess_, plan.ffmpegArgs)) {
    return false;
  }

  const size_t frameBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
  int videoFd = videoProcess_.readFd;
  playbackClockStart_ = std::chrono::steady_clock::now();
  playbackStartPosition_ = 0.0;
  state_ = TransportState::Playing;
  isBrowserCapturing_ = true;

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

void MediaEngine::stopBrowserCapture() {
  isBrowserCapturing_ = false;
  stopDecoderThreads();
}

bool MediaEngine::startSourceCapture(const Cue& cue) {
#ifdef _WIN32
  (void) cue;
  return false;
#else
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
#endif
}

// ── Private methods ──────────────────────────────────────────────────────────

double MediaEngine::visualFadeGainAt(double positionSeconds) const {
  if (!activeCue_) {
    return 1.0;
  }
  double gain = 1.0;
  if (!suppressFadeInForCurrentCue_ && activeCue_->fadeInSeconds > 0.001) {
    gain = std::min(gain, std::clamp(positionSeconds / activeCue_->fadeInSeconds, 0.0, 1.0));
  }
  if (!suppressVisualFadeOutForCurrentCue_ && activeCue_->fadeOutSeconds > 0.001 && duration_ > 0.0) {
    double remaining = std::max(0.0, duration_ - positionSeconds);
    gain = std::min(gain, std::clamp(remaining / activeCue_->fadeOutSeconds, 0.0, 1.0));
  }
  return std::clamp(gain, 0.0, 1.0);
}

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

void MediaEngine::beginTransition(double seconds, TransitionStyle style, float sourceGain) {
  clearTransitionTexture();
  if (!texture_) {
    return;
  }
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
  transitionWaitingForFirstFrame_ = true;
}

void MediaEngine::clearTransitionTexture() {
  if (transitionTexture_) {
    SDL_DestroyTexture(transitionTexture_);
    transitionTexture_ = nullptr;
  }
  transitionTextureWidth_ = 0;
  transitionTextureHeight_ = 0;
  transitionActive_ = false;
  transitionWaitingForFirstFrame_ = false;
  transitionDurationSeconds_ = 0.0;
  transitionStyle_ = TransitionStyle::Cut;
  transitionSourceGain_ = 1.0f;
}

bool MediaEngine::drawTextureFitted(SDL_Texture* texture, int width, int height, const SDL_Rect& target, Uint8 alphaValue) {
  if (!texture || width <= 0 || height <= 0) {
    return false;
  }
  int cropL = std::clamp(static_cast<int>(std::lround(static_cast<double>(width) * cropLeft_)), 0, width - 1);
  int cropR = std::clamp(static_cast<int>(std::lround(static_cast<double>(width) * cropRight_)), 0, width - 1);
  int cropT = std::clamp(static_cast<int>(std::lround(static_cast<double>(height) * cropTop_)), 0, height - 1);
  int cropB = std::clamp(static_cast<int>(std::lround(static_cast<double>(height) * cropBottom_)), 0, height - 1);
  int srcW = std::max(1, width - cropL - cropR);
  int srcH = std::max(1, height - cropT - cropB);
  SDL_Rect source {cropL, cropT, srcW, srcH};

  double scale;
  if (scaleMode_ == ScaleMode::Fit) {
    scale = std::min(
      static_cast<double>(target.w) / static_cast<double>(srcW),
      static_cast<double>(target.h) / static_cast<double>(srcH)
    );
  } else if (scaleMode_ == ScaleMode::Fill) {
    scale = std::max(
      static_cast<double>(target.w) / static_cast<double>(srcW),
      static_cast<double>(target.h) / static_cast<double>(srcH)
    );
  } else if (scaleMode_ == ScaleMode::Stretch) {
    scale = 1.0;
  } else {
    scale = 1.0;
  }

  int drawW, drawH;
  if (scaleMode_ == ScaleMode::Stretch) {
    drawW = target.w;
    drawH = target.h;
  } else if (scaleMode_ == ScaleMode::Unscaled) {
    drawW = srcW;
    drawH = srcH;
  } else {
    drawW = std::max(1, static_cast<int>(std::round(srcW * scale)));
    drawH = std::max(1, static_cast<int>(std::round(srcH * scale)));
  }

  int scaledW = std::max(1, static_cast<int>(drawW * outputScaleX_));
  int scaledH = std::max(1, static_cast<int>(drawH * outputScaleY_));
  SDL_Rect destination {
    target.x + (target.w - scaledW) / 2 + static_cast<int>(outputOffsetX_),
    target.y + (target.h - scaledH) / 2 + static_cast<int>(outputOffsetY_),
    scaledW,
    scaledH
  };

  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureAlphaMod(texture, alphaValue);
  SDL_Point center {destination.w / 2, destination.h / 2};
  SDL_RenderCopyEx(outputRenderer_, texture, &source, &destination, outputRotationDegrees_, &center, SDL_FLIP_NONE);
  SDL_SetTextureAlphaMod(texture, 255);
  return true;
}

void MediaEngine::drawTransitionOverlay(const SDL_Rect& target, bool drewCurrent) {
  if (!transitionActive_) {
    return;
  }

  if (transitionWaitingForFirstFrame_) {
    if (drewCurrent) {
      transitionWaitingForFirstFrame_ = false;
      transitionStartedAt_ = std::chrono::steady_clock::now();
      if (transitionDurationSeconds_ <= 0.001) {
        clearTransitionTexture();
        return;
      }
    } else {
      Uint8 waitAlpha = static_cast<Uint8>(transitionSourceGain_ * 255.0f);
      drawTextureFitted(transitionTexture_, transitionTextureWidth_, transitionTextureHeight_, target, waitAlpha);
      if (waitAlpha < 255) {
        SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, 255 - waitAlpha);
        SDL_RenderFillRect(outputRenderer_, nullptr);
        SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_NONE);
      }
      return;
    }
  }

  double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - transitionStartedAt_).count();
  double progress = transitionDurationSeconds_ <= 0.0001 ? 1.0 : std::clamp(elapsed / transitionDurationSeconds_, 0.0, 1.0);
  if (progress >= 1.0) {
    clearTransitionTexture();
    return;
  }

  if (transitionStyle_ == TransitionStyle::DipBlack) {
    if (progress < 0.5) {
      Uint8 srcA = static_cast<Uint8>(transitionSourceGain_ * 255.0f);
      drawTextureFitted(transitionTexture_, transitionTextureWidth_, transitionTextureHeight_, target, srcA);
      SDL_SetRenderDrawBlendMode(outputRenderer_, SDL_BLENDMODE_BLEND);
      double blackAlpha = std::clamp(progress * 2.0, 0.0, 1.0);
      if (transitionSourceGain_ < 1.0f) blackAlpha = std::max(blackAlpha, 1.0 - transitionSourceGain_);
      SDL_SetRenderDrawColor(outputRenderer_, 0, 0, 0, static_cast<Uint8>(blackAlpha * 255.0));
      SDL_RenderFillRect(outputRenderer_, nullptr);
    } else {
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

  Uint8 alphaValue = static_cast<Uint8>(transitionSourceGain_ * std::clamp(1.0 - progress, 0.0, 1.0) * 255.0);
  drawTextureFitted(transitionTexture_, transitionTextureWidth_, transitionTextureHeight_, target, alphaValue);
}

void MediaEngine::handlePlaybackEnd() {
  if (!activeCue_) {
    return;
  }
  CueEndAction act = resolvedCueEndAction(*activeCue_);

  if (act == CueEndAction::Loop) {
    suppressFadeInForCurrentCue_ = true;
    suppressVisualFadeOutForCurrentCue_ = true;
    seek(0.0, false);
    state_ = TransportState::Playing;
    playbackClockStart_ = std::chrono::steady_clock::now();
    playbackStartPosition_ = 0.0;
    pausedPosition_ = 0.0;
    if (audioDevice_ != 0) {
      SDL_PauseAudioDevice(audioDevice_, 0);
    }
    return;
  }
  if (act == CueEndAction::PauseOnLast) {
    state_ = TransportState::Paused;
    pausedPosition_ = duration_;
    currentPosition_ = duration_;
    clearVisualOnReachedEnd_ = false;
    if (audioDevice_ != 0) {
      SDL_PauseAudioDevice(audioDevice_, 1);
    }
    return;
  }
  state_ = TransportState::Stopped;
  pausedPosition_ = duration_;
  currentPosition_ = duration_;
  clearVisualOnReachedEnd_ = true;
  if (audioDevice_ != 0) {
    SDL_PauseAudioDevice(audioDevice_, 1);
  }
  reachedEnd_ = true;
}

void MediaEngine::clearTexture() {
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
}

void MediaEngine::uploadFrame(const DecodedFrame& frame) {
  if (frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) {
    return;
  }
  if (!texture_ || textureWidth_ != frame.width || textureHeight_ != frame.height) {
    clearTexture();
    texture_ = SDL_CreateTexture(
      outputRenderer_,
      SDL_PIXELFORMAT_RGBA32,
      SDL_TEXTUREACCESS_STREAMING,
      frame.width,
      frame.height
    );
    textureWidth_ = frame.width;
    textureHeight_ = frame.height;
  }
  if (!texture_) {
    return;
  }
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

void MediaEngine::stopImageThread() {
  imageProcess_.stop();
  if (imageThread_.joinable()) {
    imageThread_.join();
  }
  std::lock_guard<std::mutex> lk(imageMutex_);
  pendingImageFrame_.reset();
  imageFramePending_.store(false);
}

std::pair<int, int> MediaEngine::currentOutputSizeHint() const {
  int w = kOutputWidth;
  int h = kOutputHeight;
  if (outputRenderer_) {
    int rw = 0;
    int rh = 0;
    if (SDL_GetRendererOutputSize(outputRenderer_, &rw, &rh) == 0 && rw > 0 && rh > 0) {
      w = rw;
      h = rh;
    }
  }
  return {w, h};
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

void MediaEngine::loadStillFrame(const Cue& cue) {
  stopImageThread();
  std::string mediaPath = mediaPathForCue(cue);
  if (mediaPath.empty()) {
    return;
  }

  auto [capW, capH] = currentOutputSizeHint();
  int w = cue.width > 0 ? cue.width : capW;
  int h = cue.height > 0 ? cue.height : capH;
  if (w > capW || h > capH) {
    double scale = std::min(
      static_cast<double>(capW) / w,
      static_cast<double>(capH) / h
    );
    w = std::max(1, static_cast<int>(w * scale));
    h = std::max(1, static_cast<int>(h * scale));
  }

  if (!spawnPipeProcess(imageProcess_, {
    "ffmpeg", "-hide_banner", "-loglevel", "error",
    "-i", mediaPath,
    "-frames:v", "1",
    "-vf", "scale=" + std::to_string(w) + ":" + std::to_string(h) + ":flags=neighbor",
    "-f", "rawvideo", "-pix_fmt", "rgba", "pipe:1"
  })) {
    return;
  }

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
      imageFramePending_.store(true);
    }
  });
}

void MediaEngine::loadPatternFrame(const Cue& cue) {
  auto [fallbackW, fallbackH] = currentOutputSizeHint();
  auto frame = buildPatternFrame(cue, 0.0, fallbackW, fallbackH);
  if (frame) {
    frame->index = ++displayFrameSerial_;
    displayFrame_ = std::move(frame);
    uploadFrame(*displayFrame_);
  }
}

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

void MediaEngine::clearAudio() {
  if (audioDevice_ != 0) {
    SDL_ClearQueuedAudio(audioDevice_);
    SDL_PauseAudioDevice(audioDevice_, 1);
  }
}

size_t MediaEngine::queuedFrames() {
  std::lock_guard<std::mutex> lock(frameMutex_);
  return frameQueue_.size();
}

void MediaEngine::stopDecoderThreads() {
  stopImageThread();
  decoderStop_.store(true);
  videoProcess_.stop();
  audioProcess_.stop();

  if (videoThread_.joinable()) {
    videoThread_.join();
  }
  if (audioThread_.joinable()) {
    audioThread_.join();
  }

  {
    std::lock_guard<std::mutex> lock(frameMutex_);
    frameQueue_.clear();
  }
  decoderStop_.store(false);
  decoderEof_ = false;
  isSourceCapturing_ = false;
  isBrowserCapturing_ = false;
}

bool MediaEngine::buildSourceCaptureArgs(const Cue& cue, int w, int h, std::vector<std::string>& args) const {
  std::string sourceRef = sourceCueRefFromCue(cue);
  if (sourceRef.empty()) {
    sourceRef = defaultSourceRefForKind(cue.kind);
  }
  std::string displayEnv;
  if (const char* envDisplay = std::getenv("DISPLAY"); envDisplay && *envDisplay) {
    std::string trimmed = trim(envDisplay);
    if (!trimmed.empty()) {
      displayEnv = trimmed;
    }
  }

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

void MediaEngine::startDecoderThreads(const Cue& cue, double mediaStartSeconds, double cueStartSeconds) {
  std::string mediaPath = mediaPathForCue(cue);
  if (mediaPath.empty()) {
    return;
  }
  int decodeW = cue.width;
  int decodeH = cue.height;
  if (cue.kind == CueKind::Video) {
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
  std::string scaleFilter = "scale=" + std::to_string(decodeW) + ":" + std::to_string(decodeH)
                          + ":flags=bicubic";
  double speed = std::clamp(cue.playbackSpeed, 0.25, 4.0);
  if (std::abs(speed - 1.0) > 0.01) {
    std::ostringstream pts;
    pts << std::fixed << std::setprecision(4) << (1.0 / speed);
    scaleFilter += ",setpts=" + pts.str() + "*PTS";
  }
  if (!spawnPipeProcess(videoProcess_, {
    "ffmpeg",
    "-hide_banner",
    "-loglevel",
    "error",
    "-ss",
    std::to_string(mediaStartSeconds),
    "-i",
    mediaPath,
    "-map", "0:v:0",
    "-an",
    "-vf", scaleFilter,
    "-f",
    "rawvideo",
    "-pix_fmt",
    "rgba",
    "pipe:1"
  })) {
    return;
  }

  const size_t frameBytes = static_cast<size_t>(decodeW) * static_cast<size_t>(decodeH) * 4u;
  if (frameBytes == 0) {
    videoProcess_.stop();
    decoderEof_ = true;
    return;
  }
  int videoFd = videoProcess_.readFd;
  videoThread_ = std::thread([this, decodeW, decodeH, frameBytes, cueStartSeconds, videoFd]() {
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

  if (audioDevice_ != 0 && cue.hasAudio && cue.audioEnabled) {
    std::vector<std::string> audioArgs = {
      "ffmpeg", "-hide_banner", "-loglevel", "error",
      "-ss", std::to_string(mediaStartSeconds),
      "-i", mediaPath, "-vn"
    };
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
          if (SDL_GetQueuedAudioSize(audioDevice_) > 23040) {
            SDL_Delay(4);
            continue;
          }

          int bytesRead = readSome(audioFd, buffer.data(), buffer.size());
          if (bytesRead <= 0) {
            break;
          }

          std::vector<std::int16_t> scaled(static_cast<size_t>(bytesRead) / sizeof(std::int16_t));
          std::memcpy(scaled.data(), buffer.data(), static_cast<size_t>(bytesRead));
          for (size_t index = 0; index < scaled.size(); index += 2) {
            double gain = static_cast<double>(volume_.load()) * fadeGainAt(audioTime);
            for (size_t channel = 0; channel < 2 && index + channel < scaled.size(); ++channel) {
              auto& sample = scaled[index + channel];
              sample = static_cast<std::int16_t>(std::clamp(
                static_cast<int>(std::lround(static_cast<double>(sample) * gain)),
                -32768,
                32767
              ));
            }
            audioTime += 1.0 / 48000.0;
          }
          if (audioTap_) {
            audioTap_(scaled);
          }
          SDL_QueueAudio(audioDevice_, scaled.data(), static_cast<Uint32>(scaled.size() * sizeof(std::int16_t)));
        }
      });
    }
  }
}

std::optional<DecodedFrame> MediaEngine::decodeSingleFrame(ChildProcess& process, const std::string& path, int width, int height, double seconds) {
  auto [capW, capH] = currentOutputSizeHint();
  int w = width > 0 ? width : capW;
  int h = height > 0 ? height : capH;
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

void MediaEngine::fillPixelRect(DecodedFrame& frame, int x, int y, int w, int h, SDL_Color color) {
  for (int py = std::max(0, y); py < std::min(frame.height, y + h); ++py) {
    for (int px = std::max(0, x); px < std::min(frame.width, x + w); ++px) {
      writePixel(frame, px, py, color);
    }
  }
}

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

void MediaEngine::buildSmpte75Bars(DecodedFrame& frame) {
  int W = frame.width, H = frame.height;
  struct Bar { Uint8 r, g, b; };
  constexpr std::array<Bar, 7> bars {{
    {191,191,191},
    {191,191,  0},
    {  0,191,191},
    {  0,191,  0},
    {191,  0,191},
    {191,  0,  0},
    {  0,  0,191},
  }};
  int topH   = H * 2 / 3;
  int midH   = H / 12;
  int botH   = H - topH - midH;
  int barW   = W / 7;
  for (int i = 0; i < 7; ++i) {
    SDL_Color c {bars[i].r, bars[i].g, bars[i].b, 255};
    fillPixelRect(frame, i * barW, 0, barW + (i == 6 ? W - 6 * barW : 0), topH, c);
  }
  constexpr std::array<Bar, 7> midBars {{
    {  0,191,191},
    {  0,  0,  0},
    {191,  0,191},
    {  0,  0,  0},
    {191,191,191},
    {  0,  0,  0},
    {  0,  0,191},
  }};
  for (int i = 0; i < 7; ++i) {
    SDL_Color c {midBars[i].r, midBars[i].g, midBars[i].b, 255};
    fillPixelRect(frame, i * barW, topH, barW + (i == 6 ? W - 6 * barW : 0), midH, c);
  }
  int botBarW = W / 4;
  fillPixelRect(frame, 0,          topH + midH, botBarW, botH, {  0,  0,  0, 255});
  fillPixelRect(frame, botBarW,    topH + midH, botBarW, botH, {255,255,255, 255});
  fillPixelRect(frame, botBarW*2,  topH + midH, botBarW, botH, { 10, 10, 10, 255});
  fillPixelRect(frame, botBarW*3,  topH + midH, W - botBarW*3, botH, { 4,  4,  4, 255});
  for (int i = 0; i < 7; ++i) {
    SDL_Color label {bars[i].r > 100 ? Uint8(0) : Uint8(230),
                     bars[i].g > 100 ? Uint8(0) : Uint8(230),
                     bars[i].b > 100 ? Uint8(0) : Uint8(230), 255};
    fillPixelRect(frame, i * barW + 2, topH - 14, barW - 4, 10, label);
  }
}

void MediaEngine::buildCrosshatch(DecodedFrame& frame, int phaseX, int phaseY) {
  int W = frame.width;
  int H = frame.height;
  constexpr int kStep = 64;
  int shiftX = ((phaseX % kStep) + kStep) % kStep;
  int shiftY = ((phaseY % kStep) + kStep) % kStep;
  fillPixelRect(frame, 0, 0, W, H, {0, 0, 0, 255});
  for (int x = -shiftX; x < W; x += kStep) {
    fillPixelRect(frame, x, 0, 2, H, {255, 255, 255, 255});
  }
  for (int y = -shiftY; y < H; y += kStep) {
    fillPixelRect(frame, 0, y, W, 2, {255, 255, 255, 255});
  }
  fillPixelRect(frame, W / 2 - 1, 0,     2, H, {220,  40,  40, 255});
  fillPixelRect(frame, 0,     H / 2 - 1, W, 2, {220,  40,  40, 255});
  int sx = W / 10;
  int sy = H / 10;
  fillPixelRect(frame, sx, sy, W - sx * 2, 2, {60, 180, 60, 200});
  fillPixelRect(frame, sx, sy, 2, H - sy * 2, {60, 180, 60, 200});
  fillPixelRect(frame, W - sx - 2, sy, 2, H - sy * 2, {60, 180, 60, 200});
  fillPixelRect(frame, sx, H - sy - 2, W - sx * 2, 2, {60, 180, 60, 200});
}

void MediaEngine::buildCheckerboard(DecodedFrame& frame, int phaseX, int phaseY) {
  int W = frame.width;
  int H = frame.height;
  constexpr int cell = 64;
  int shiftX = ((phaseX % cell) + cell) % cell;
  int shiftY = ((phaseY % cell) + cell) % cell;
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

void MediaEngine::buildPocketTest(DecodedFrame& frame, double t, int forcedScene) {
  const int W = frame.width;
  const int H = frame.height;

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

  auto lerpColor = [&](const SDL_Color& a, const SDL_Color& b, double v) -> SDL_Color {
    double tClamped = std::clamp(v, 0.0, 1.0);
    auto mix = [&](Uint8 aa, Uint8 bb) -> Uint8 {
      return static_cast<Uint8>(std::round(static_cast<double>(aa) * (1.0 - tClamped) + static_cast<double>(bb) * tClamped));
    };
    return SDL_Color {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), 255};
  };

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

  const int oceanTop = H * 52 / 100;
  const int beachTop = H * 83 / 100;
  int scene = forcedScene;
  if (scene < 0 || scene > 3) {
    scene = static_cast<int>(std::floor(t / 14.0)) % 4;
    if (scene < 0) {
      scene += 4;
    }
  }

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

  for (int y = 0; y < oceanTop; ++y) {
    double v = oceanTop > 1 ? static_cast<double>(y) / static_cast<double>(oceanTop - 1) : 0.0;
    rect(0, y, W, 1, lerpColor(skyTop, skyBottom, v));
  }

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

  drawPalm(W / 2 - W / 7, beachTop + 3, std::max(26, H / 7), t * 0.8 + 0.6, true);
  drawPalm(W / 2 + W / 8, beachTop + 3, std::max(24, H / 8), t * 0.85 + 1.8, true);
  drawPalm(W / 4, H - std::max(16, H / 8), std::max(28, H / 6), t * 0.9 + 0.2, false);
  drawPalm(W * 3 / 4, H - std::max(18, H / 8), std::max(30, H / 6), t * 0.95 + 2.1, false);

  int blockSize = std::max(5, H / 34);
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

  auto drawPuffFriend = [&](int x, int y) {
    disc(x, y, std::max(5, H / 62), SDL_Color {255, 152, 198, 255});
    rect(x - 3, y + 4, 2, 2, SDL_Color {220, 76, 126, 255});
    rect(x + 1, y + 4, 2, 2, SDL_Color {220, 76, 126, 255});
    rect(x - 2, y - 1, 1, 1, SDL_Color {20, 20, 20, 255});
    rect(x + 1, y - 1, 1, 1, SDL_Color {20, 20, 20, 255});
    rect(x - 1, y + 1, 2, 1, SDL_Color {224, 82, 122, 255});
  };

  auto drawCoin = [&](int cx, int cy, int radius) {
    disc(cx, cy, radius, SDL_Color {255, 206, 62, 255});
    disc(cx, cy, std::max(1, radius - 2), SDL_Color {255, 236, 132, 255});
    rect(cx - 1, cy - radius + 2, 2, radius * 2 - 3, SDL_Color {244, 180, 46, 255});
    rect(cx - radius + 2, cy - 1, radius * 2 - 3, 2, SDL_Color {255, 248, 188, 255});
  };

  for (int i = 0; i < 5; ++i) {
    int cx = ((i * (W / 4) + static_cast<int>(t * 34.0)) % (W + 60)) - 30;
    int cy = beachTop - 18 + static_cast<int>(std::sin(t * 2.4 + i * 1.3) * 6.0);
    drawCoin(cx, cy, std::max(4, H / 48));
  }

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

  int crabX = (static_cast<int>(t * 24.0) % (W + 80)) - 40;
  drawCrab(crabX, beachTop + std::max(8, H / 40), (static_cast<int>(t * 4.0) & 1) != 0);

  int turtleX = W - ((static_cast<int>(t * 18.0) + 20) % (W + 90)) + 24;
  drawTurtle(turtleX, beachTop + std::max(5, H / 52), (static_cast<int>(t * 6.0) & 1) != 0);

  int dinoX = ((static_cast<int>(t * 11.0) + W / 3) % (W + 120)) - 60;
  drawDino(dinoX, beachTop - std::max(18, H / 11), (static_cast<int>(t * 2.4) % 5) == 0);

  int puffX = heroX + 28 + static_cast<int>(std::sin(t * 1.7) * 10.0);
  int puffY = heroY + 6 + static_cast<int>(std::fabs(std::sin(t * 3.4)) * 4.0);
  drawPuffFriend(puffX, puffY);

  int parrotX = W - ((static_cast<int>(t * 34.0) + 60) % (W + 120));
  int parrotY = std::max(12, H / 9) + static_cast<int>(std::sin(t * 2.1) * (H / 28.0));
  drawParrot(parrotX, parrotY, (static_cast<int>(t * 8.0) & 1) == 0);

  for (int i = 0; i < 4; ++i) {
    int fishX = ((i * (W / 4) + static_cast<int>(t * 28.0)) % (W + 80)) - 40;
    double jump = std::sin(t * 2.5 + static_cast<double>(i) * 1.2);
    if (jump > -0.2) {
      int fishY = oceanTop + 20 - static_cast<int>(std::max(0.0, jump) * 16.0);
      SDL_Color fishColor = (i % 2 == 0) ? SDL_Color {255, 178, 88, 255} : SDL_Color {96, 230, 220, 255};
      drawFish(fishX, fishY, (i % 2) == 0, fishColor);
    }
  }

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

  int stripH = std::max(16, H / 14);
  int stripY = H - stripH;
  rect(0, stripY, W, stripH, SDL_Color {12, 30, 56, 230});

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

std::optional<DecodedFrame> MediaEngine::buildPatternFrame(const Cue& cue, double animTime,
                                                           int fallbackWidth, int fallbackHeight) {
  int sourceW = cue.width > 0 ? cue.width : fallbackWidth;
  int sourceH = cue.height > 0 ? cue.height : fallbackHeight;
  bool legacyRaster = cue.width == kOutputWidth && cue.height == kOutputHeight;
  if (legacyRaster && (fallbackWidth != kOutputWidth || fallbackHeight != kOutputHeight)) {
    sourceW = fallbackWidth;
    sourceH = fallbackHeight;
  }

  DecodedFrame frame;
  frame.width  = std::max(320, sourceW);
  frame.height = std::max(180, sourceH);
  frame.index  = 0;
  frame.pixels.assign(static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4u, 255);

  std::string patternType = normalizePatternTypeId(cue.path);
  std::string basePatternType = stripPatternMotionSuffix(patternType);
  bool motion = endsWith(patternType, "-motion");
  double motionProgress = motion ? normalizedLoopProgress(animTime, kPatternMotionLoopSeconds) : 0.0;

  auto pulseByte = [&](Uint8 fullScale, double speed, double minScale = 0.40, double maxScale = 1.0) -> Uint8 {
    double wave = 0.5 + 0.5 * std::sin(animTime * speed);
    double scaled = minScale + (maxScale - minScale) * wave;
    return static_cast<Uint8>(std::clamp(std::lround(static_cast<double>(fullScale) * scaled), 0l, 255l));
  };

  if (basePatternType == "smpte-bars") {
    buildSmpte75Bars(frame);
    if (motion) {
      int scanX = static_cast<int>(std::floor(motionProgress * static_cast<double>(frame.width + 120))) - 60;
      fillPixelRect(frame, scanX, 0, 4, frame.height, {255, 255, 255, 255});
      int scanY = static_cast<int>(std::floor(normalizedLoopProgress(animTime * 1.5, kPatternMotionLoopSeconds)
                                              * static_cast<double>(frame.height + 80))) - 40;
      fillPixelRect(frame, 0, scanY, frame.width, 2, {8, 8, 8, 255});
    }
  } else if (basePatternType == "crosshatch") {
    int phaseX = motion ? phaseFromProgress(motionProgress, 64) : 0;
    int phaseY = 0;
    buildCrosshatch(frame, phaseX, phaseY);
    if (motion) {
      int markerX = static_cast<int>(std::floor(motionProgress * static_cast<double>(frame.width + 24))) - 12;
      fillPixelRect(frame, markerX, std::max(0, frame.height - 18), 6, 6, {220, 190, 72, 220});
    }
  } else if (basePatternType == "checkerboard" || basePatternType == "checker") {
    int phaseX = motion ? phaseFromProgress(motionProgress * 2.0, 128) : 0;
    int phaseY = motion ? phaseFromProgress(motionProgress, 128) : 0;
    buildCheckerboard(frame, phaseX, phaseY);
    if (motion) {
      int y = static_cast<int>(std::floor(frame.height * (0.5 + 0.35 * std::sin(motionProgress * kTau))));
      fillPixelRect(frame, 0, y, frame.width, 2, {255, 96, 32, 255});
    }
  } else if (basePatternType == "full-white") {
    Uint8 v = motion ? pulseByte(255, 2.5, 0.55, 1.0) : 255;
    fillPixelRect(frame, 0, 0, frame.width, frame.height, {v, v, v, 255});
  } else if (basePatternType == "full-black") {
    Uint8 v = motion ? pulseByte(80, 2.3, 0.05, 1.0) : 0;
    fillPixelRect(frame, 0, 0, frame.width, frame.height, {v, v, v, 255});
  } else if (basePatternType == "full-red") {
    Uint8 r = motion ? pulseByte(255, 2.6, 0.30, 1.0) : 255;
    fillPixelRect(frame, 0, 0, frame.width, frame.height, {r, 0, 0, 255});
  } else if (basePatternType == "full-green") {
    Uint8 g = motion ? pulseByte(255, 2.7, 0.30, 1.0) : 255;
    fillPixelRect(frame, 0, 0, frame.width, frame.height, {0, g, 0, 255});
  } else if (basePatternType == "full-blue") {
    Uint8 b = motion ? pulseByte(255, 2.8, 0.30, 1.0) : 255;
    fillPixelRect(frame, 0, 0, frame.width, frame.height, {0, 0, b, 255});
  } else if (basePatternType == "pocket-day") {
    buildPocketTest(frame, animTime, 0);
  } else if (basePatternType == "pocket-sunset") {
    buildPocketTest(frame, animTime, 1);
  } else if (basePatternType == "pocket-night") {
    buildPocketTest(frame, animTime, 2);
  } else if (basePatternType == "pocket-storm") {
    buildPocketTest(frame, animTime, 3);
  } else {
    buildPocketTest(frame, animTime, -1);
  }

  return frame;
}
