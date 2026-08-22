// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// media_engine.hpp — Core playback engine for all cue types.
//
// MediaEngine is the heart of Deckboy. It manages:
//   - Video decode:    in-process libav (d3d11va zero-copy or CPU frames);
//                      ffmpeg subprocess pipe as fallback (live streams,
//                      rotated files, --no-inproc-decode)
//   - Audio decode:    in-process libav → s16/48k stereo → SDL stream;
//                      ffmpeg subprocess fallback as above
//   - Still images:    decodes a single frame via ffmpeg, holds on screen
//   - Pattern cues:    generates procedural test patterns (CPU-rendered)
//   - Browser cues:    receives frames from the browser backend (CEF/WebKit)
//   - Source capture:  receives frames from capture backend (camera/window)
//   - Transitions:     crossfade/dip-to-black between outgoing and incoming cues
//   - Transport:       play, pause, stop, seek, speed control, pause points
//   - Fade in/out:     per-cue visual and audio fading at start/end
//
// Threading model:
//   - Video decode thread: reads raw frames from ffmpeg stdout via readExact()
//     and pushes them into frameQueue_ (protected by frameMutex_)
//   - Audio decode thread: reads PCM from ffmpeg stdout via readSome() and
//     queues to SDL audio device
//   - Image thread:  decodes a single still frame asynchronously
//   - Main thread:   calls update() to pop frames from queue, render() to blit
//
// One MediaEngine instance exists per deck (created in main.cpp).
// The activeCue_ pointer is non-owning — it points into the Deck::cues vector.
//
// Implementation: media_engine.cpp
// ============================================================================

#pragma once

#include "core/sdl_compat.hpp"
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

#if DECKBOY_INPROC_DECODE
#include "engine/libav_decoder.hpp"
#endif

// Compute the outgoing cue's fade gain at the moment a transition begins.
// When transport is paused/stopped (e.g. TAKE from standby), returns 1.0
// so the outgoing frame is fully visible during the transition — prevents
// an ugly flash to black when crossfading from a paused cue.
inline float transitionSourceGainForLoadCue(const Cue* activeCue, TransportState state, double fadeGainAtPosition) {
  if (activeCue && state == TransportState::Playing) {
    return static_cast<float>(std::clamp(fadeGainAtPosition, 0.0, 1.0));
  }
  return 1.0f;
}

// ============================================================================
// MediaEngine — Playback engine for a single deck.
//
// Lifecycle:
//   1. Construct with an SDL renderer and audio device
//   2. loadCue() to start playing a cue (with optional transition)
//   3. update() each frame to advance the decode pipeline
//   4. render() to blit the current frame to the output
//   5. Destructor stops all decode threads and frees textures
// ============================================================================
class MediaEngine {
 public:
  // Callback to tap decoded audio samples (for waveform display / VU meter).
  using AudioTapCallback = std::function<void(const std::vector<std::int16_t>&)>;
  // Optional resolver to transform cue paths before decode (e.g. relative→absolute).
  using CuePathResolver = std::function<std::string(const Cue&)>;
  // Optional provider of the D3D11 device to decode onto (the program output
  // renderer's), queried at decode start. Engines without one (preview, PiP)
  // decode in-process to CPU frames; with one, NV12 video stays GPU-resident
  // end-to-end (zero-copy). Only meaningful when DECKBOY_INPROC_DECODE.
  using DecodeDeviceProvider = std::function<void*()>;
  // Optional provider of the CURRENT program-output mode: raster in pixels
  // and refresh rate in Hz. Patterns build at this size every rebuild (so
  // they stay pixel-mapped to the selected display even when it changes
  // mid-show) and animate at this refresh rate (unless the project's
  // explicit refresh override supplies it instead — the app decides).
  // Zeroes / no provider fall back to the engine's own renderer size and
  // 60 Hz.
  struct OutputModeHint {
    int width = 0;
    int height = 0;
    double refreshHz = 0.0;
  };
  using OutputSizeProvider = std::function<OutputModeHint()>;

  explicit MediaEngine(SDL_Renderer* outputRenderer,
                       SDL_AudioStream* audioStream,
                       AudioTapCallback audioTap = {},
                       CuePathResolver cuePathResolver = {},
                       DecodeDeviceProvider decodeDeviceProvider = {},
                       OutputSizeProvider outputSizeProvider = {})
    : outputRenderer_(outputRenderer),
      audioStream_(audioStream),
      audioTap_(std::move(audioTap)),
      cuePathResolver_(std::move(cuePathResolver)),
      decodeDeviceProvider_(std::move(decodeDeviceProvider)),
      outputSizeProvider_(std::move(outputSizeProvider)) {}

  ~MediaEngine();  // calls stopAll() to clean up threads and processes

  MediaEngine(const MediaEngine&) = delete;
  MediaEngine& operator=(const MediaEngine&) = delete;

  // -- Transport controls (called from app_cue_transport.ipp) -----------------
  void stopAll();                         // kill all decode, clear everything
  void loadCue(const Cue* cue, bool autoplay,  // load a cue for playback
               double transitionSeconds = 0.0,
               TransitionStyle transitionStyle = TransitionStyle::Cut,
               bool suppressFadeIn = false);
  // Re-read runtime params (speed, in/out, pause points) and restart decode.
  // Pass the app's current cue so the engine's owned snapshot is refreshed
  // first — the engine never keeps pointers into Deck::cues (see below).
  void refreshActiveCueRuntime(const Cue* updatedCue = nullptr);
  // Replace the owned active-cue snapshot with fresh content (no decode
  // restart). Called by the app after any edit that may touch the active cue
  // so live-editable fields (fade in/out, etc.) stay current. No-op when
  // nothing is loaded.
  void syncActiveCueSnapshot(const Cue& cue);
  void play();                            // resume playback
  // Hot-swap the SDL output device (the engine never owns it) without
  // disturbing the loaded cue, decode, or transport — used when the operator
  // changes the deck's audio output while a cue is playing.
  void setAudioDevice(SDL_AudioStream* stream);

  // Redirect finished audio somewhere other than SDL. Set by the app when an
  // ASIO device is armed; cleared to fall back to SDL. Deliberately a
  // std::function of plain interleaved s16 so the engine keeps knowing nothing
  // about ASIO, Windows, or COM.
  //
  //   write   accepts FRAMES, returns how many it took. A short return is
  //           backpressure, not an error -- the caller retries.
  //   queued  frames still to play, so the decode threads can pace themselves
  //           exactly as they do against SDL's queue.
  struct ExternalAudioSink {
    std::function<std::size_t(const std::int16_t*, std::size_t)> write;
    std::function<std::size_t()> queued;
    int channels = 0;
  };
  void setExternalAudioSink(ExternalAudioSink sink);
  bool usingExternalAudioSink() const;
  // Give the output device back before the owner destroys it. Stops decode
  // (which joins the audio thread) and forgets the stream, so nothing in the
  // engine — including the destructor's own stopAll() — can touch a stream
  // that is about to be, or has already been, freed. Every owner must call
  // this before SDL_DestroyAudioStream.
  void detachAudioDevice();
  void pause();                           // pause playback (hold current frame)
  void toggle();                          // play ↔ pause toggle
  // Stop playback and rerack to the start. clearVisual=true additionally
  // darkens the deck (visual cleared, decode pipes released) — the operator-
  // facing STOP verb; RERACK uses seek(0)+pause to hold the first frame.
  void stop(bool clearVisual = false);
  void clear();                           // stop + release the active cue + black output
  void seek(double seconds, bool clearVisualFrame = false); // jump to time position
  void setVolume(float value);            // set playback volume (0.0–1.0)
  // Master (show-level) gain multiplied on top of the per-cue volume in the
  // audio thread. Synced from Project::masterVolume every app tick so the
  // header fader affects all decks no matter which path changed it.
  void setMasterGain(float value) { masterGain_.store(std::clamp(value, 0.0f, 2.0f)); }
  // Chain A/V offset: all queued audio is held back this long before it
  // reaches the SDL stream (0–1000 ms). Synced from Project::audioDelayMs
  // per tick like the master gain; applied in the audio-thread tail.
  void setAudioDelayMs(int ms) { audioDelayMs_.store(std::clamp(ms, 0, 1000)); }
  // Channel count the deck's SDL stream was opened with (2/4/6/8). Must match
  // the open spec or every byte↔frame conversion in the engine goes wrong.
  void setAudioDeviceChannels(int channels) {
    audioDeviceChannels_.store(std::clamp(channels, 2, 8));
  }
  void setPausePoints(std::vector<double> points); // set auto-pause timecodes

  // -- Frame update and rendering (called from main loop) ----------------------
  void update();                          // pop frames from decode queue, advance position
  void render(SDL_Rect target);           // blit current frame + transition overlay to renderer
  void rebuildPatternFrame(const Cue& cue, double wallSeconds); // regenerate a pattern cue's pixels
  // Regenerate a Timer cue. Elapsed comes from the TRANSPORT rather than a
  // separate clock: taking the cue starts it, pause holds it, rerack resets it,
  // so the timer inherits the transport the operator already knows instead of
  // inventing a parallel set of controls.
  void rebuildTimerFrame(const Cue& cue, double elapsedSeconds, bool running);
  // ---- Tone generator ------------------------------------------------------
  // Procedural audio, the counterpart of the pattern generator. Called from the
  // update loop while a Tone cue is live: it tops the output up rather than
  // rendering a fixed block, so the tone is continuous no matter how the frame
  // rate wanders.
  void pumpToneAudio(const Cue& cue);
  // The cue's on-screen card: what is playing, at what level, on which output.
  void rebuildToneFrame(const Cue& cue);
  // Oscillator video with feedback. `audioLevel` is 0..1 and may be 0 -- the
  // synth free-runs when nothing is playing, because a visualiser that shows
  // nothing without audio is useless during setup.
  void rebuildVideoSynthFrame(const Cue& cue, double wallSeconds, double audioLevel);
  // One sample of the FDS voice. Advances the carrier, modulator and
  // envelope by dt seconds.
  double fdsNextSample(const ToneSettings& tone, double dt);
  // One sample of the 2A03 voice: pulse, triangle or noise.
  double nesNextSample(const ToneSettings& tone, double dt);
  // Attack/hold/release shared by every chip.
  double chipEnvelope(const ToneSettings& tone, double dt);


  // -- Browser cue interface (called from platform/browser.*) ------------------
  bool startBrowserCapture(const std::string& displayId, int w, int h,
                           double fadeInSeconds, double fadeOutSeconds,
                           double transSecs, TransitionStyle transStyle);
  void stopBrowserCapture();
  bool startBrowserFrameMode(int w, int h, double transSecs, TransitionStyle transStyle);
  void pushBrowserFrame(const uint8_t* rgba, int w, int h); // receive a frame from browser backend

  // -- Source capture interface (called from platform/capture_backend.*) --------
  bool startSourceCapture(const Cue& cue);

  // -- End-of-playback handling ------------------------------------------------
  void finalizeReachedEnd(bool keepVisibleFrame); // called by transport when cue ends

  // -- Read-only accessors (thread-safe where marked) --------------------------
  float volume() const { return volume_.load(); }  // atomic
  const Cue* activeCue() const { return activeCue_; }
  TransportState state() const { return state_; }
  double duration() const { return duration_; }
  double position() const;                // current playback position in seconds
  double mediaFpsMeasured() const { return mediaFpsMeasured_; } // actual decode fps
  bool reachedEnd();                      // true once playback reached the end
  bool shouldClearVisualOnReachedEnd() const { return clearVisualOnReachedEnd_; }
  bool isBrowserCapturing() const { return isBrowserCapturing_; }
  bool isSourceCapturing() const { return isSourceCapturing_; }
  const DecodedFrame* currentFrame() const; // pointer to the currently displayed frame

  // -- FPS telemetry (for performance monitoring) ------------------------------
  void resetMediaFpsTelemetry();
  bool shouldMeasureMediaFps() const;
  void recordMediaFrameAdvance(std::uint64_t frameIndex);

  // -- In-process decode introspection ------------------------------------------
  // True while the active cue decodes in-process (libav) rather than via the
  // ffmpeg CLI pipe. activeDecodeDevice() is the ID3D11Device* zero-copy
  // frames are bound to (null in CPU/software mode) — the app compares it
  // against the current program output to restart decode after output
  // topology changes. consumeDecodeStall() returns true once when the decode
  // watchdog trips (playing, no EOF, no frame produced for several seconds):
  // the transport handler should rerack the deck and toast the operator.
  bool inprocDecodeActive() const { return inprocDecodeActive_; }
  void* activeDecodeDevice() const { return activeDecodeDevice_; }
  bool consumeDecodeStall();
  // Latches true once when a still-image cue failed to decode (unsupported
  // format or corrupt file). The app polls this and tells the operator, instead
  // of the cue silently showing nothing. Cleared on read.
  bool consumeStillDecodeFailure();
  // Process-wide break-glass switch (--no-inproc-decode, decode bench): when
  // disabled, every new decode uses the ffmpeg CLI pipe path even in builds
  // compiled with DECKBOY_INPROC_DECODE. Affects the next TAKE, not running decodes.
  static void setInprocDecodeDisabled(bool disabled);
  static bool inprocDecodeDisabled();

  // Latched by App::shutdown() immediately before SDL_Quit(). SDL_Quit frees
  // every audio stream and texture SDL still owns, so a MediaEngine torn down
  // after that point must not call into SDL — the handles it holds are already
  // freed and touching one is a segfault, not a leak.
  //
  // This is a backstop, not the plan: engines are shut down inside shutdown()
  // while SDL is alive. It exists because a field crash proved an engine can
  // reach ~App (the stack was ~MediaEngine -> stopAll -> SDL_ClearAudioStream
  // called from runDeckboyMain, i.e. after shutdown() had already returned),
  // and a crash on quit is worth making structurally impossible rather than
  // merely unlikely.
  static void setSdlTornDown(bool tornDown);
  static bool sdlTornDown();

  // -- Single-frame decode (for thumbnail generation) --------------------------
  std::optional<DecodedFrame> decodeSingleFrame(ChildProcess& process, const std::string& path,
                                                 int width, int height, double seconds);

  // -- Static pattern frame generator -----------------------------------------
  // Stage/speaker countdown. Seven-segment geometry, never text: a stage clock
  // has to render the same on every machine whatever fonts are installed.
  // Public alongside the pattern builders so the smoke suite can render it
  // headlessly without a deck.
  static void buildTimerFrame(DecodedFrame& frame, const TimerSettings& cfg,
                              double elapsedSeconds, bool running);

  static std::optional<DecodedFrame> buildPatternFrame(const Cue& cue, double animTime = 0.0,
                                                       int fallbackWidth = kOutputWidth,
                                                       int fallbackHeight = kOutputHeight);
  // Fill a caller-owned frame, reusing its pixel buffer. The per-frame path
  // MUST use this: constructing a DecodedFrame per rebuild allocated a full
  // raster every frame (33 MB at 4K, ~2 GB/s), which exhausted the system
  // commit limit and slowed the entire machine. Resets all GPU/format state,
  // since a reused frame may previously have held a zero-copy video frame.
  static void buildPatternFrameInto(DecodedFrame& frame, const Cue& cue, double animTime,
                                    int fallbackWidth, int fallbackHeight);

  // Current visual fade gain (0–1) factoring in fade-in and fade-out curves.
  double currentVisualFadeGain() const { return visualFadeGainAt(position()); }

 private:
  // -- Internal helpers -------------------------------------------------------
  double visualFadeGainAt(double positionSeconds) const;  // fade gain for visual (may suppress fade-out for auto-advance)
  double fadeGainAt(double positionSeconds) const;         // raw fade gain (in+out curve) at position
  double audioFadeGainAt(double positionSeconds) const;    // fade gain from atomic mirrors — the ONLY variant safe on the audio thread
  void syncAudioFadeParams();                              // publish fade params to the atomic mirrors (main thread)
  void initStillTimer(const Cue& cue, bool autoplay);     // set up duration timer for still/pattern/browser cues
  void beginTransition(double seconds, TransitionStyle style, float sourceGain = 1.0f); // start a visual transition
  void clearTransitionTexture();                           // release the outgoing-cue snapshot texture
  bool drawTextureFitted(SDL_Texture* texture, int width, int height, const SDL_Rect& target, Uint8 alphaValue); // draw texture with scale mode
  void drawTransitionOverlay(const SDL_Rect& target, bool drewCurrent); // render the transition blend
  void handlePlaybackEnd();                                // called when playback naturally reaches the end
  void clearTexture();                                     // release the main frame texture
  void uploadFrame(const DecodedFrame& frame);             // push decoded frame pixels to GPU texture
  void stopImageThread();                                  // join and clean up the still-image decode thread
  std::pair<int, int> currentOutputSizeHint() const;       // get output dimensions for ffmpeg -s flag
  std::string mediaPathForCue(const Cue& cue) const;       // resolve cue path (may use CuePathResolver callback)
  // True only for a file-backed video cue that has a prepared mosh copy on
  // disk. Everything else reports false so the toggle can say why.
  bool datamoshActiveForCue(const Cue& cue) const;
  void loadStillFrame(const Cue& cue);                     // async-decode a single frame for still cues
  void loadPatternFrame(const Cue& cue);                   // generate a pattern frame and upload
  void loadSourceFrame(const Cue& cue);                    // start source capture for camera/window cues
  void clearAudio();                                       // flush the SDL audio queue
  void queuePocketSyncAudio();                             // synthesize the pocket-test A/V sync pop
  size_t queuedFrames();                                   // number of frames waiting in frameQueue_
  void stopDecoderThreads();                               // kill ffmpeg processes and join threads
  bool buildSourceCaptureArgs(const Cue& cue, int w, int h, std::vector<std::string>& args) const; // build ffmpeg args for source capture
  void startDecoderThreads(const Cue& cue, double mediaStartSeconds, double cueStartSeconds);       // launch decode (in-process libav, or ffmpeg subprocess fallback)
#if DECKBOY_INPROC_DECODE
  // In-process decode path. Returns false when this cue must use the CLI
  // pipe path instead (rotated file, no decodable frame, pipeline failure) —
  // startDecoderThreads falls through to the subprocess code.
  bool startInprocDecoders(const Cue& cue, const std::string& mediaPath,
                           double mediaStartSeconds, double cueStartSeconds,
                           int decodeW, int decodeH, FramePixelFormat decodeFormat,
                           double speed);
#endif
  // Shared audio-thread tail: per-sample fade/volume/master gain, waveform
  // tap, queue to the SDL stream, advance the audio clock counters. Used by
  // both the CLI pipe thread and the in-process thread so the audio-master
  // clock semantics stay identical.
  void applyGainAndQueueAudio(std::vector<std::int16_t>& samples, double& audioTime);
  // Peak limiter, applied in place to the gained float scratch between the
  // gain stage and int16 quantisation. See media_engine.cpp for the design.
  void applyPeakLimiter();
  // Final stage: delay FIFO (chain A/V offset) → tap → SDL stream. Shared by
  // decode audio and the pocket-test sync pop.
  void queueDelayedAudio(std::vector<std::int16_t>& samples);
  // Last write: expand processed stereo onto the cue's output pair when the
  // stream is open with >2 channels, then SDL_PutAudioStreamData.
  void putAudioToStream(const std::vector<std::int16_t>& stereo);
  // Already at DEVICE width. putAudioToStream takes STEREO and widens it onto
  // a pair, which is wrong for a generator that addresses channels
  // individually -- a 1kHz tone sent to output 6 must not be folded into a
  // stereo pair on the way out.
  void putWideAudioToStream(const std::vector<std::int16_t>& wide, int channels);
  // Bytes per sample FRAME as the SDL stream sees them (channels × s16).
  // Every queued-bytes → seconds/backpressure conversion must use this.
  int audioStreamBytesPerFrame() const {
    return std::max(2, audioDeviceChannels_.load(std::memory_order_relaxed))
           * static_cast<int>(sizeof(std::int16_t));
  }

  // Bytes the output device still has to play, read from the decode threads.
  // Guarded like every other audio-thread touch of the stream; reads 0 once the
  // device has been detached, which unblocks the backpressure wait so the
  // thread can see decoderStop_ and exit.
  int queuedAudioBytes() {
    std::lock_guard<std::mutex> lock(audioStreamMutex_);
    // With an external sink the SDL stream is not the thing playing, so its
    // queue is meaningless -- reading it would report 0 forever and the decode
    // threads would race ahead until memory ran out.
    if (externalSink_.queued) {
      const int chans = std::max(2, externalSink_.channels);
      return static_cast<int>(externalSink_.queued()) * chans *
             static_cast<int>(sizeof(std::int16_t));
    }
    return audioStream_ ? std::max(0, SDL_GetAudioStreamQueued(audioStream_)) : 0;
  }

  // -- Pattern rendering helpers (static, pure) --------------------------------
  static void writePixel(DecodedFrame& frame, int x, int y, SDL_Color color);
  static void fillPixelRect(DecodedFrame& frame, int x, int y, int w, int h, SDL_Color color);
  static void drawHeart(DecodedFrame& frame, int centerX, int centerY, int radius, SDL_Color color);
  static void buildSmpte75Bars(DecodedFrame& frame);       // SMPTE 75% color bars
  static void buildCrosshatch(DecodedFrame& frame, int phaseX = 0, int phaseY = 0);   // crosshatch grid
  static void buildCheckerboard(DecodedFrame& frame, int phaseX = 0, int phaseY = 0); // checkerboard
  static void buildPocketTest(DecodedFrame& frame, double t, int forcedScene = -1);    // animated pixel art scene
  static void buildPocketTestCard(DecodedFrame& frame, double t);                      // PM5544-style card: bouncing scene porthole
  static void buildTestBars(DecodedFrame& frame, double t);                            // testsrc2-style motion-diagnostics bars
  static void buildTestClock(DecodedFrame& frame, double t);
  static void drawPocketTestCardStatic(DecodedFrame& frame);                           // cacheable layer: grid, bands, patches, border, crosshair
  static void drawPocketTestCard(DecodedFrame& frame, const DecodedFrame& sceneFrame,
                                 double t, int scene);                                 // dynamic layer: sweep, ball, shimmer, beacon, ID

  // -- State: core references --------------------------------------------------
  SDL_Renderer* outputRenderer_ = nullptr;  // SDL renderer for texture upload and blit
  // Device-bound SDL3 stream for PCM output. NOT owned — the app opens and
  // destroys it. The audio decode thread writes to it while the main thread can
  // swap or detach it (device change, deck teardown), so those two operations
  // are serialized by audioStreamMutex_; see detachAudioDevice().
  // Tone generator state. Lives across pump calls so the waveform is
  // continuous -- restarting the phase every block would click audibly.
  double tonePhase_ = 0.0;
  double toneSweepSeconds_ = 0.0;
  int toneIdentifyChannel_ = 0;
  std::uint32_t toneSeed_ = 0x1234567u;
  // Voss-McCartney pink rows, kept as plain members: the algorithm needs no
  // type of its own and a pimpl here would buy nothing.
  double tonePinkRows_[16] = {};
  double tonePinkRunning_ = 0.0;
  std::uint32_t tonePinkCounter_ = 0;
  // The last fraction of a second of generated audio, kept so the card can
  // draw what was ACTUALLY emitted rather than re-deriving it and possibly
  // drawing something the operator is not hearing.
  std::vector<std::int16_t> toneScopeL_;
  std::vector<std::int16_t> toneScopeR_;
  std::size_t toneScopePos_ = 0;
  // FDS voice state. Phases are kept in the 0..64 and 0..32 table domains
  // rather than radians, because that is how the hardware addresses them and
  // it keeps the wrap arithmetic exact.
  double fdsCarrierPhase_ = 0.0;
  double fdsModPhase_ = 0.0;
  double fdsModAccum_ = 0.0;
  double fdsEnvSeconds_ = 0.0;
  // Wavetables, rebuilt only when the operator changes carrier or modulator.
  double fdsCarrierTable_[64] = {};
  double fdsModTable_[32] = {};
  int fdsCachedCarrier_ = -1;
  int fdsCachedModulator_ = -1;
  bool fdsTablesValid_ = false;
  // 2A03 state. The LFSR seeds to 1 because a zero register never leaves zero
  // -- the hardware powers up with bit 0 set for the same reason.
  double nesPhase_ = 0.0;
  double nesNoiseAccum_ = 0.0;
  unsigned nesLfsr_ = 1u;
  // Previous video-synth frame, for the feedback path. Kept as RGBA at the
  // output raster; reallocated only when the raster changes.
  std::vector<std::uint8_t> vsynthPrev_;
  int vsynthPrevW_ = 0;
  int vsynthPrevH_ = 0;
  double vsynthRotation_ = 0.0;
  ExternalAudioSink externalSink_;   // guarded by audioStreamMutex_
  // Audio the sink could not take yet. Carried rather than dropped or waited
  // on; see putAudioToStream for why waiting here is not an option.
  std::vector<std::int16_t> pendingSinkAudio_;
  SDL_AudioStream* audioStream_ = nullptr;
  std::mutex audioStreamMutex_;
  CuePathResolver cuePathResolver_;          // optional path transform callback
  // The engine OWNS a snapshot of the loaded cue. activeCue_ points at
  // activeCueSnapshot_ (or nullptr) — never into Deck::cues, whose vector
  // reallocates on import and shifts on delete while decode threads and the
  // render path are still reading. The app refreshes the snapshot via
  // syncActiveCueSnapshot() / refreshActiveCueRuntime() after edits.
  std::optional<Cue> activeCueSnapshot_;
  const Cue* activeCue_ = nullptr;           // points at activeCueSnapshot_, or nullptr

  // -- State: video frame texture ----------------------------------------------
  SDL_Texture* texture_ = nullptr;           // GPU texture for the current frame
  int textureWidth_ = 0;                     // texture dimensions (match decoded frame)
  int textureHeight_ = 0;
  Uint32 textureFormat_ = 0;                 // SDL pixel format of the live texture (0 if none)

  // -- State: transition (crossfade / dip-to-black) ----------------------------
  SDL_Texture* transitionTexture_ = nullptr; // snapshot of the outgoing cue's last frame
  int transitionTextureWidth_ = 0;
  int transitionTextureHeight_ = 0;
  bool transitionActive_ = false;            // true while a transition is in progress
  bool transitionWaitingForFirstFrame_ = false; // wait for incoming cue's first frame before starting blend
  double transitionDurationSeconds_ = 0.0;
  TransitionStyle transitionStyle_ = TransitionStyle::Cut;
  float transitionSourceGain_ = 1.0f;       // outgoing cue's opacity at transition start

  // -- State: per-cue geometry (copied from Cue on load) -----------------------
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

  // -- State: per-cue pixel effects (copied from Cue on load) ------------------
  bool chromaKeyEnabled_ = false;
  SDL_Color chromaKeyColor_ {0, 255, 0, 255};
  float chromaKeyTolerance_ = 60.0f;
  float chromaKeySoftness_ = 20.0f;
  float brightness_ = 1.0f;
  float contrast_ = 1.0f;
  float saturation_ = 1.0f;
  float hueShift_ = 0.0f;
  std::vector<std::uint8_t> keyedPixelsScratch_; // scratch buffer for chroma key processing

  // -- State: pause points (auto-pause at specific timecodes) ------------------
  std::vector<double> pausePoints_;          // sorted list of pause timecodes
  size_t nextPausePointIdx_ = 0;             // index of the next pause point to check

  // -- State: transition timing ------------------------------------------------
  std::chrono::steady_clock::time_point transitionStartedAt_ = std::chrono::steady_clock::now();

  // -- State: audio ------------------------------------------------------------
  std::atomic<float> volume_ {1.0f};         // playback volume (0–1, thread-safe)
  std::atomic<float> masterGain_ {1.0f};     // show master volume (0–2, thread-safe)

  // -- State: transport --------------------------------------------------------
  TransportState state_ = TransportState::Stopped;
  double currentPosition_ = 0.0;            // current playback position (seconds)
  double pausedPosition_ = 0.0;             // position at last pause (for resume)
  double playbackStartPosition_ = 0.0;      // position when play() was called
  double duration_ = 0.0;                   // effective duration (outPoint - inPoint)
  double cueInPointSeconds_ = 0.0;          // trim start (from Cue::inPointSeconds)
  double cueOutPointSeconds_ = 0.0;         // trim end (from Cue::outPointSeconds)
  double frameRate_ = 30.0;                 // decode frame rate (from Cue::fps)
  double playbackSpeed_ = 1.0;              // speed multiplier (from Cue::playbackSpeed)
  std::chrono::steady_clock::time_point playbackClockStart_ = std::chrono::steady_clock::now(); // wall-clock reference for position()

  // -- State: frame pipeline (decode thread → main thread) ---------------------
  std::optional<DecodedFrame> displayFrame_; // the frame currently being rendered
  std::uint64_t lastRenderedFrameIndex_ = static_cast<std::uint64_t>(-1); // dedup detection
  std::mutex frameMutex_;                    // protects frameQueue_ (shared between decode and main threads)
  std::deque<DecodedFrame> frameQueue_;      // decoded frames waiting to be displayed (max kMaxVideoFrames)

  // -- State: subprocess handles -----------------------------------------------
  ChildProcess videoProcess_;                // ffmpeg video decode subprocess
  ChildProcess audioProcess_;                // ffmpeg audio decode subprocess
  ChildProcess imageProcess_;                // ffmpeg single-frame decode subprocess (stills)

  // -- State: decode threads ---------------------------------------------------
  std::thread videoThread_;                  // reads raw frames from videoProcess_.readFd
  std::thread audioThread_;                  // reads PCM samples from audioProcess_.readFd
  std::thread imageThread_;                  // decodes a single still frame asynchronously
  std::mutex imageMutex_;                    // protects pendingImageFrame_
  std::optional<DecodedFrame> pendingImageFrame_; // still frame waiting to be consumed by main thread
  std::atomic<bool> imageFramePending_ {false};   // flag: pendingImageFrame_ is ready
  // A still decode that produces no frame (unsupported format — e.g. HEIC on an
  // ffmpeg without HEIF demux — or a corrupt/truncated file) used to fail
  // SILENTLY: the cue loaded and simply showed nothing, forever. imageDecodeFailed_
  // latches a genuine failure so the app can tell the operator. imageCancelRequested_
  // suppresses the latch when the decode was killed on purpose by a cue switch
  // (which also makes the pipe read fail), so a normal retake never false-alarms.
  std::atomic<bool> imageDecodeFailed_ {false};
  std::atomic<bool> imageCancelRequested_ {false};

  // -- State: audio tap --------------------------------------------------------
  AudioTapCallback audioTap_;                // callback for waveform/VU meter display

  // -- State: audio playback clock ----------------------------------------------
  // Video position runs on the wall clock; audio free-runs from the ffmpeg
  // pipe into the SDL queue. Their clocks drift (audio device clock !=
  // steady_clock, VFR sources, scheduler stalls). The audio thread counts
  // sample frames it queues; update() derives the audio playback clock
  // (queued − still-buffered) and re-anchors video position when drift
  // exceeds a threshold — audio is the master, as in any playout engine.
  std::atomic<std::uint64_t> audioFramesQueued_ {0};  // stereo frames queued to SDL this decode run
  double audioClockStartSeconds_ = 0.0;               // cue position where the audio pipe started
  bool audioClockValid_ = false;                      // audio pipe live for this cue (not a live stream)
  double lastAudioClockSeconds_ = -1.0;               // last observed audio clock (stall detection)
  Uint64 lastAudioClockAdvanceMs_ = 0;                // when the audio clock last moved forward

  // -- State: audio-thread fade mirrors -----------------------------------------
  // The audio thread computes per-sample fade gain. It must not read
  // activeCue_/duration_/suppress flags (plain members mutated by the main
  // thread). These atomics mirror them; syncAudioFadeParams() publishes on
  // load/refresh and once per update() tick as a catch-all.
  std::atomic<double> audioFadeInSeconds_ {0.0};
  std::atomic<double> audioFadeOutSeconds_ {0.0};
  std::atomic<double> audioFadeDuration_ {0.0};
  std::atomic<bool> audioSuppressFadeIn_ {false};
  std::atomic<bool> audioSuppressFadeOut_ {false};
  std::atomic<double> audioCueGain_ {1.0};   // per-cue trim, linear (from Cue::audioGainDb)
  std::atomic<float> audioCuePan_ {0.0f};    // per-cue balance -1..+1
  std::atomic<bool> audioCueMono_ {false};   // per-cue mono downmix
  std::atomic<int> audioDelayMs_ {0};        // chain A/V offset (project-level)
  // Multichannel routing: the SDL stream was opened with this many channels
  // (deck setting); the cue's processed stereo lands on this pair of them.
  // The whole engine pipeline stays stereo — expansion happens only at the
  // final SDL_PutAudioStreamData (putAudioToStream).
  std::atomic<int> audioDeviceChannels_ {2}; // channels the SDL stream expects
  std::atomic<int> audioCuePairOffset_ {0};  // 0 = outs 1-2, 1 = outs 3-4, ...
  std::deque<std::int16_t> audioDelayFifo_;  // holds processed samples for the delay
                                             // (owned by whichever thread queues audio;
                                             // cleared only after threads are joined)
  // -- State: peak limiter (v0.81.5) -------------------------------------------
  // Touched only by the audio thread that is currently queueing (one decode
  // path is live at a time), so these need no synchronisation. limiterGain_
  // persists across chunks to keep the release continuous; reset on load/stop
  // so a new cue never starts ducked by the previous one's transient.
  double limiterGain_ = 1.0;                 // current gain reduction, 1.0 = open
  std::vector<double> limiterScratch_;       // gained interleaved stereo, pre-quantise
  std::vector<double> limiterFramePeak_;     // max(|L|,|R|) per frame
  std::deque<std::size_t> limiterWindow_;    // monotonic deque → look-ahead window min

  // -- State: decoder lifecycle flags ------------------------------------------
  std::atomic<bool> decoderStop_ {false};    // signal decode threads to exit
  std::atomic<bool> decoderEof_ {false};     // decode threads have reached EOF
  bool reachedEnd_ = false;                  // cue playback has finished
  bool decodersRunning_ = false;             // decode running for the active cue (main thread)

  // -- State: in-process decode (libav) ----------------------------------------
#if DECKBOY_INPROC_DECODE
  std::unique_ptr<deckboy::libav::VideoPipeline> videoPipeline_;
  std::unique_ptr<deckboy::libav::AudioPipeline> audioPipeline_;
#endif
  DecodeDeviceProvider decodeDeviceProvider_;
  OutputSizeProvider outputSizeProvider_;
  bool inprocDecodeActive_ = false;          // active cue decodes in-process
  void* activeDecodeDevice_ = nullptr;       // device zero-copy frames live on (null = CPU)
  std::atomic<Uint64> lastFramePushMs_ {0};  // decode watchdog: last frame produced
  bool decodeStallLatched_ = false;          // watchdog tripped (consumed by transport)
  std::uint64_t lastUploadedFrameIndex_ = static_cast<std::uint64_t>(-1); // skip redundant re-uploads in update()
  double lastPatternRebuildSeconds_ = -1.0;  // animated-pattern rebuild throttle (30 fps; terrarium 9)

  // -- State: browser capture --------------------------------------------------
  bool isBrowserCapturing_ = false;          // browser backend is sending frames
  int browserCaptureW_ = 1280;               // browser frame width
  int browserCaptureH_ = 720;                // browser frame height
  std::uint64_t browserFrameIdx_ = 0;        // sequential browser frame counter

  // -- State: source capture ---------------------------------------------------
  bool isSourceCapturing_ = false;           // source capture is active

  // -- State: fade control -----------------------------------------------------
  bool clearVisualOnReachedEnd_ = false;     // go to black when cue ends (vs hold last frame)
  bool suppressFadeInForCurrentCue_ = false; // skip fade-in (e.g. mid-transition load)
  bool suppressVisualFadeOutForCurrentCue_ = false; // skip visual fade-out (auto-advance handles it)

  // -- State: FPS telemetry ----------------------------------------------------
  Uint64 mediaFpsSampleStartedAtMs_ = 0;    // start time of current measurement window
  Uint32 mediaFpsFrameCount_ = 0;           // frames counted in current window
  double mediaFpsMeasured_ = 0.0;           // computed actual decode FPS
  std::uint64_t lastMeasuredMediaFrameIndex_ = static_cast<std::uint64_t>(-1);
  std::uint64_t displayFrameSerial_ = 0;    // monotonic frame counter for display
};
