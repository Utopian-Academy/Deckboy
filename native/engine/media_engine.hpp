// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/constants.hpp"
#include "core/subprocess.hpp"
#include "core/types.hpp"

// Compute the fade gain of the outgoing frame at the moment loadCue() is called.
// When transport is paused/stopped, return 1.0 so the outgoing frame stays fully
// visible during the transition (avoids a flash to black on TAKE from standby).
inline float transitionSourceGainForLoadCue(const Cue* activeCue, TransportState state, double fadeGainAtPosition) {
  if (activeCue && state == TransportState::Playing) {
    return static_cast<float>(std::clamp(fadeGainAtPosition, 0.0, 1.0));
  }
  return 1.0f;
}

class MediaEngine {
 public:
  using AudioTapCallback = std::function<void(const std::vector<std::int16_t>&)>;
  using CuePathResolver = std::function<std::string(const Cue&)>;

  explicit MediaEngine(SDL_Renderer* outputRenderer,
                       SDL_AudioDeviceID audioDevice,
                       AudioTapCallback audioTap = {},
                       CuePathResolver cuePathResolver = {})
    : outputRenderer_(outputRenderer),
      audioDevice_(audioDevice),
      audioTap_(std::move(audioTap)),
      cuePathResolver_(std::move(cuePathResolver)) {}

  ~MediaEngine();

  MediaEngine(const MediaEngine&) = delete;
  MediaEngine& operator=(const MediaEngine&) = delete;

  void stopAll();
  void loadCue(const Cue* cue, bool autoplay, double transitionSeconds = 0.0,
               TransitionStyle transitionStyle = TransitionStyle::Cut,
               bool suppressFadeIn = false);
  void refreshActiveCueRuntime();
  void play();
  void pause();
  void toggle();
  void stop();
  void clear();
  void seek(double seconds, bool clearVisualFrame = true);
  void setVolume(float value);
  void setPausePoints(std::vector<double> points);
  void update();
  void render(SDL_Rect target);
  void rebuildPatternFrame(const Cue& cue, double wallSeconds);

  bool startBrowserCapture(const std::string& displayId, int w, int h,
                           double fadeInSeconds, double fadeOutSeconds,
                           double transSecs, TransitionStyle transStyle);
  void stopBrowserCapture();
  bool startSourceCapture(const Cue& cue);
  void finalizeReachedEnd(bool keepVisibleFrame);

  float volume() const { return volume_.load(); }
  const Cue* activeCue() const { return activeCue_; }
  TransportState state() const { return state_; }
  double duration() const { return duration_; }
  double position() const;
  double mediaFpsMeasured() const { return mediaFpsMeasured_; }
  bool reachedEnd();
  bool shouldClearVisualOnReachedEnd() const { return clearVisualOnReachedEnd_; }
  bool isBrowserCapturing() const { return isBrowserCapturing_; }
  bool isSourceCapturing() const { return isSourceCapturing_; }
  const DecodedFrame* currentFrame() const;

  void resetMediaFpsTelemetry();
  bool shouldMeasureMediaFps() const;
  void recordMediaFrameAdvance(std::uint64_t frameIndex);

  std::optional<DecodedFrame> decodeSingleFrame(ChildProcess& process, const std::string& path, int width, int height, double seconds);

  static std::optional<DecodedFrame> buildPatternFrame(const Cue& cue, double animTime = 0.0,
                                                       int fallbackWidth = kOutputWidth,
                                                       int fallbackHeight = kOutputHeight);

 private:
  double visualFadeGainAt(double positionSeconds) const;
  double fadeGainAt(double positionSeconds) const;
  void initStillTimer(const Cue& cue, bool autoplay);
  void beginTransition(double seconds, TransitionStyle style, float sourceGain = 1.0f);
  void clearTransitionTexture();
  bool drawTextureFitted(SDL_Texture* texture, int width, int height, const SDL_Rect& target, Uint8 alphaValue);
  void drawTransitionOverlay(const SDL_Rect& target, bool drewCurrent);
  void handlePlaybackEnd();
  void clearTexture();
  void uploadFrame(const DecodedFrame& frame);
  void stopImageThread();
  std::pair<int, int> currentOutputSizeHint() const;
  std::string mediaPathForCue(const Cue& cue) const;
  void loadStillFrame(const Cue& cue);
  void loadPatternFrame(const Cue& cue);
  void loadSourceFrame(const Cue& cue);
  void clearAudio();
  size_t queuedFrames();
  void stopDecoderThreads();
  bool buildSourceCaptureArgs(const Cue& cue, int w, int h, std::vector<std::string>& args) const;
  void startDecoderThreads(const Cue& cue, double mediaStartSeconds, double cueStartSeconds);

  static void writePixel(DecodedFrame& frame, int x, int y, SDL_Color color);
  static void fillPixelRect(DecodedFrame& frame, int x, int y, int w, int h, SDL_Color color);
  static void drawHeart(DecodedFrame& frame, int centerX, int centerY, int radius, SDL_Color color);
  static void buildSmpte75Bars(DecodedFrame& frame);
  static void buildCrosshatch(DecodedFrame& frame, int phaseX = 0, int phaseY = 0);
  static void buildCheckerboard(DecodedFrame& frame, int phaseX = 0, int phaseY = 0);
  static void buildPocketTest(DecodedFrame& frame, double t, int forcedScene = -1);

  SDL_Renderer* outputRenderer_ = nullptr;
  SDL_AudioDeviceID audioDevice_ = 0;
  CuePathResolver cuePathResolver_;
  const Cue* activeCue_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  int textureWidth_ = 0;
  int textureHeight_ = 0;
  SDL_Texture* transitionTexture_ = nullptr;
  int transitionTextureWidth_ = 0;
  int transitionTextureHeight_ = 0;
  bool transitionActive_ = false;
  bool transitionWaitingForFirstFrame_ = false;
  double transitionDurationSeconds_ = 0.0;
  TransitionStyle transitionStyle_ = TransitionStyle::Cut;
  float transitionSourceGain_ = 1.0f;
  float outputScaleX_ = 1.0f;
  float outputScaleY_ = 1.0f;
  ScaleMode scaleMode_ = ScaleMode::Fit;
  float outputOffsetX_ = 0.0f;
  float outputOffsetY_ = 0.0f;
  float outputRotationDegrees_ = 0.0f;
  float cropLeft_ = 0.0f;
  float cropRight_ = 0.0f;
  float cropTop_ = 0.0f;
  float cropBottom_ = 0.0f;
  bool chromaKeyEnabled_ = false;
  SDL_Color chromaKeyColor_ {0, 255, 0, 255};
  float chromaKeyTolerance_ = 60.0f;
  float chromaKeySoftness_ = 20.0f;
  float brightness_ = 1.0f;
  float contrast_ = 1.0f;
  float saturation_ = 1.0f;
  float hueShift_ = 0.0f;
  std::vector<std::uint8_t> keyedPixelsScratch_;
  std::vector<double> pausePoints_;
  size_t nextPausePointIdx_ = 0;
  std::chrono::steady_clock::time_point transitionStartedAt_ = std::chrono::steady_clock::now();
  std::atomic<float> volume_ {1.0f};
  TransportState state_ = TransportState::Stopped;
  double currentPosition_ = 0.0;
  double pausedPosition_ = 0.0;
  double playbackStartPosition_ = 0.0;
  double duration_ = 0.0;
  double cueInPointSeconds_ = 0.0;
  double cueOutPointSeconds_ = 0.0;
  double frameRate_ = 30.0;
  double playbackSpeed_ = 1.0;
  std::chrono::steady_clock::time_point playbackClockStart_ = std::chrono::steady_clock::now();
  std::optional<DecodedFrame> displayFrame_;
  std::uint64_t lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1);
  std::mutex frameMutex_;
  std::deque<DecodedFrame> frameQueue_;
  ChildProcess videoProcess_;
  ChildProcess audioProcess_;
  ChildProcess imageProcess_;
  std::thread videoThread_;
  std::thread audioThread_;
  std::thread imageThread_;
  std::mutex imageMutex_;
  std::optional<DecodedFrame> pendingImageFrame_;
  std::atomic<bool> imageFramePending_ {false};
  AudioTapCallback audioTap_;
  std::atomic<bool> decoderStop_ {false};
  std::atomic<bool> decoderEof_ {false};
  bool reachedEnd_ = false;
  bool isBrowserCapturing_ = false;
  int browserCaptureW_ = 1280;
  int browserCaptureH_ = 720;
  bool isSourceCapturing_ = false;
  bool clearVisualOnReachedEnd_ = false;
  bool suppressFadeInForCurrentCue_ = false;
  bool suppressVisualFadeOutForCurrentCue_ = false;
  Uint64 mediaFpsSampleStartedAtMs_ = 0;
  Uint32 mediaFpsFrameCount_ = 0;
  double mediaFpsMeasured_ = 0.0;
  std::uint64_t lastMeasuredMediaFrameIndex_ = static_cast<std::uint64_t>(-1);
  std::uint64_t displayFrameSerial_ = 0;
};
