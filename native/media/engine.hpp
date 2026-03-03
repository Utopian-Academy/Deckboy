#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <SDL.h>

#include "../core/types.hpp"

namespace playboy::media {

// Decoded video frame with pixel data
struct DecodedFrame {
  std::vector<std::uint8_t> rgba;
  int width = 0;
  int height = 0;
  std::uint64_t frameNumber = 0;
};

// Deck-local playback engine
class MediaEngine {
 public:
  using AudioTapCallback = std::function<void(const std::vector<std::int16_t>&)>;

  explicit MediaEngine(SDL_Renderer* outputRenderer, SDL_AudioDeviceID audioDevice, AudioTapCallback audioTap = {});
  ~MediaEngine();

  // Prevent copying
  MediaEngine(const MediaEngine&) = delete;
  MediaEngine& operator=(const MediaEngine&) = delete;

  // Lifecycle
  void stopAll();
  void loadCue(const Cue* cue, bool autoplay, 
               double transitionSeconds = 0.0, 
               TransitionStyle transitionStyle = TransitionStyle::Cut);

  // Transport control
  void play();
  void pause();
  void toggle();
  void stop();
  void clear();
  void seek(double seconds);

  // Controls
  void setVolume(float value);
  void setPausePoints(std::vector<double> points);

  // State queries
  double duration() const;
  double position() const;
  bool reachedEnd() const;
  TransportState state() const;
  const Cue* activeCue() const;

  // Browser cue capture (platform-specific)
  void startBrowserCapture(const std::string& displayId, int w, int h,
                           double fadeInSeconds, double fadeOutSeconds,
                           double transSecs, TransitionStyle transStyle);
  void stopBrowserCapture();
  bool isBrowserCapturing() const;

  // Pattern generation
  void rebuildPatternFrame(const Cue& cue, double wallSeconds);

  // Rendering pipeline
  void update();  // Per-frame decode, transition progress
  void render(SDL_Rect target);  // Present current frame to SDL renderer

 private:
  // Internal state (implementation details in main.cpp for now)
  SDL_Renderer* outputRenderer_;
  SDL_AudioDeviceID audioDevice_;
  AudioTapCallback audioTap_;
  
  TransportState state_ = TransportState::Stopped;
  const Cue* activeCue_ = nullptr;
  double duration_ = 0.0;
  double currentPosition_ = 0.0;
  double pausedPosition_ = 0.0;
  
  // Placeholder for full implementation
  friend class App;  // Temporary: allow App to access internal state during transition
};

}  // namespace playboy::media
