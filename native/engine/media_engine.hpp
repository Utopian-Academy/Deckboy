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
#include <array>
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
#include "platform/audio_plugin.hpp"     // AudioPluginInstance (plugin chain slots)
#include "platform/capture_backend.hpp"  // SourceCapturePlan (window/camera capture)
#include "platform/decklink.hpp"
#include "platform/ndi_input.hpp"
#include "platform/spout_input.hpp"
#include "core/row_workers.hpp"
#include "core/subprocess.hpp"
#include "core/code_source.hpp"
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
  void setForcePixelFrames(bool on) { forcePixelFrames_ = on; }
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

  // ── The two ends of the picture/sound loop ──
  // The FINISHED picture (after its effects), from the output compositor.
  // RGBA, any size; sampled on a coarse grid. Main thread.
  void publishPostEffectStats(const std::uint8_t* rgba, int width, int height);
  // This deck's sound after its effects, 0-1, meter-smoothed. Any thread.
  // The recent played audio in time order, oldest first, mono, decimated.
  // Returns how many samples were written; zero while nothing has played.
  std::size_t copyRecentProgramAudio(std::vector<float>& out) const;

  // Publish what the PICTURE follows: the waveform audioprint draws with and
  // the level an Audio LFO rides. Called with the finished stereo by every
  // producer -- decoded audio AND generated audio -- because a generator that
  // skips it leaves both halves of the loop dead on that cue kind.
  void noteProgrammeAudio(const std::int16_t* stereo, std::size_t frames);

  double programAudioLevel01() const {
    return static_cast<double>(programLevel_.load(std::memory_order_relaxed));
  }

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

  // The operator moved an in/out point on a cue that is ON AIR.
  //
  // Cheap and idempotent: it compares against the trim the engine is actually
  // running on and returns immediately when nothing moved, so it is safe to
  // call from the per-tick snapshot sync.
  void applyActiveCueTrimEdit(const Cue& cue);
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
  // UP TO 64 NOW, not 8. A Dante Virtual Soundcard or an ASIO interface
  // offers far more than eight, and the crosspoint matrix only means anything
  // if the device can be opened wide enough to reach them.
  void setAudioDeviceChannels(int channels) {
    audioDeviceChannels_.store(std::clamp(channels, 2, kMaxAudioMatrixOuts));
  }

  // ── THE AUDIO MATRIX, MIRRORED INTO ATOMICS ───────────────────────────
  //
  // The audio thread must never touch activeCue_ or any vector -- the same
  // rule the per-cue fades already follow. So the matrix is published here as
  // a flat array of atomic gains that the mixer reads relaxed, and the "is
  // there a matrix at all" question is one more atomic so the common case
  // (no matrix, use the pair) costs a single load.
  static constexpr int kMaxAudioMatrixOuts = 64;
  static constexpr int kAudioMatrixSources = 2;

  void setAudioMatrix(const std::vector<AudioCrosspoint>& points) {
    for (auto& g : audioMatrixGain_) {
      g.store(0.0f, std::memory_order_relaxed);
    }
    int live = 0;
    for (const AudioCrosspoint& p : points) {
      if (p.source < 0 || p.source >= kAudioMatrixSources ||
          p.dest < 0 || p.dest >= kMaxAudioMatrixOuts) {
        continue;
      }
      const float gain = p.gain < 0.0f ? 0.0f : (p.gain > 4.0f ? 4.0f : p.gain);
      audioMatrixGain_[static_cast<std::size_t>(p.source) * kMaxAudioMatrixOuts +
                       static_cast<std::size_t>(p.dest)]
        .store(gain, std::memory_order_relaxed);
      if (gain > 0.0f) {
        ++live;
      }
    }
    // PUBLISHED LAST, with release, so the audio thread cannot see "a matrix
    // is active" before it can see the gains that make it up.
    audioMatrixActive_.store(live > 0, std::memory_order_release);
  }

  // ONE CROSSPOINT SUM, pulled out so it can be tested.
  //
  // Two sources landing on one output is the POINT of a matrix -- a mono
  // fold-down is L and R onto the same channel -- so they must add. Adding can
  // exceed full scale, and the difference between clamping and wrapping there
  // is the difference between a loud moment and a bang out of the PA.
  static std::int16_t mixCrosspointSample(float left, float right,
                                          float gainL, float gainR) {
    const float mixed = left * gainL + right * gainR;
    if (mixed > 32767.0f) return 32767;
    if (mixed < -32768.0f) return -32768;
    return static_cast<std::int16_t>(mixed);
  }

  void clearAudioMatrix() {
    audioMatrixActive_.store(false, std::memory_order_release);
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

  // The last ~40ms of what this deck actually PUT OUT, left and right, for a
  // live visualiser to draw. Returns false when nothing has been emitted yet.
  //
  // Written by two different threads -- the tone generator on the main thread
  // and the decode path on the audio thread -- so it is copied under a lock
  // rather than handed out by reference. It is 40ms of int16 either way, which
  // is a 2KB copy per drawn frame and not worth being clever about.
  bool copyScopeSamples(std::vector<std::int16_t>& left,
                        std::vector<std::int16_t>& right,
                        std::size_t& writePos) const;

  // Drop the ring, so a new cue cannot be drawn with the last one's audio.
  void clearScopeSamples();
  // Oscillator video with feedback. `audioLevel` is 0..1 and may be 0 -- the
  // synth free-runs when nothing is playing, because a visualiser that shows
  // nothing without audio is useless during setup.
  void rebuildVideoSynthFrame(const Cue& cue, double wallSeconds, double audioLevel);
  // Row-aware copy into a locked STREAMING texture. See the definition for
  // why this is not SDL_UpdateTexture.
  // Tell the engine its renderer is a PRIVATE, HIDDEN upload target that
  // nothing else will ever present. Every deck has one: a hidden window whose
  // only job is to be somewhere frames can be uploaded to. See uploadFrame for
  // why that is a memory leak unless somebody presents it.
  void setHiddenUploadTarget(bool hidden) { hiddenUploadTarget_ = hidden; }
  // Draw one picture as a grid of character cells. Public because text mode
  // is no longer a property of the video synth: it is a look that can be put
  // on ANY picture -- a clip, a capture card, a camera -- through the effect
  // stack. Any source size in, any destination size out.
  void renderTextMode(const std::uint8_t* src, int srcW, int srcH,
                      std::uint8_t* dst, int dstW, int dstH,
                      const VideoSynthSettings& vs, std::uint64_t serial,
                      double seconds);

  static void writeStreamingTexture(SDL_Texture* texture,
                                    const std::uint8_t* rgba,
                                    int width, int height);
  // Rebuilt at DISPLAY rate, not render-loop rate. The render loop has a
  // 240 Hz floor, so without this the synth generated four frames for every
  // one anybody saw -- and each of those built a thread pool. Patterns
  // already worked this way; the synth never did.
  double lastVsynthRebuildSeconds_ = -1.0;
  // One sample of the FDS voice. Advances the carrier, modulator and
  // envelope by dt seconds.
  double fdsNextSample(const ToneSettings& tone, double dt);
  // One sample of the 2A03 voice: pulse, triangle or noise.
  double nesNextSample(const ToneSettings& tone, double dt);
  // Attack/hold/release shared by every chip.
  double chipEnvelope(const ToneSettings& tone, double dt);

  // ---- Played notes --------------------------------------------------------
  // A note from a MIDI keyboard or the computer keyboard. Until one arrives the
  // voice free-runs on the cue's own pitch and retrigger settings, which is
  // what a drone or a test signal wants; the first note switches it to GATED,
  // where the envelope follows key down and key up instead.
  //
  // Monophonic with last-note priority, which is what the hardware was: a
  // single 2A03 pulse channel plays one note, and pretending otherwise would
  // be a synth wearing a chip's clothes.
  void synthNoteOn(double hz, int velocity);
  // Play a note on every instrument plugin in the live cue's chain. Main
  // thread; each instance queues it for its own audio thread.
  void sendNoteToPlugins(bool on, double hz, int velocity);
  // True when the live cue carries a plugin that MAKES sound rather than
  // treats it. What makes a cue playable from the keyboard, MIDI or the wire:
  // before this, only the built-in FDS chip counted, so an instrument plugin
  // loaded fine and could never be played. Main thread.
  bool hasInstrumentPlugin() const;
  // Run the live cue's audio chain over GENERATED device-width audio, in
  // place. The decode path gets the chain inside applyGainAndQueueAudio; a
  // tone or chip synth never went through that function, so its rack was
  // drawn, set, and silent.
  void applyAudioEffectsToGeneratedAudio(std::vector<std::int16_t>& out,
                                         std::size_t frames, int channels);
  void synthNoteOff(double hz);
  void synthAllNotesOff();
  bool synthGated() const { return chipGated_; }

  // Sprite sheet slicing. Public because --sheet-probe drives it
  // directly: a sheet that yields no tiles needs diagnosing without
  // opening the app.
  // EXPENSIVE: decodes the whole sheet or folder, spawning a subprocess per
  // file. Call it when a set is selected, never from a render path.
  bool ensureSpriteSheet(const std::string& path, int tileW, int tileH);
  // Diagnostics for --sheet-probe: how the sheet sliced, and how many
  // tiles survived the coverage filter.
  int spriteSheetWidth() const { return spriteSheetW_; }
  int spriteSheetHeight() const { return spriteSheetH_; }
  int spriteSheetCols() const { return spriteSheetCols_; }
  int spriteSheetRows() const { return spriteSheetRows_; }
  int spriteUsableTiles() const {
    return static_cast<int>(spriteTilesByLuma_.size());
  }
 



  // -- Browser cue interface (called from platform/browser.*) ------------------
  bool startBrowserCapture(const std::string& displayId, int w, int h,
                           double fadeInSeconds, double fadeOutSeconds,
                           double transSecs, TransitionStyle transStyle);
  void stopBrowserCapture();
  bool startBrowserFrameMode(int w, int h, double transSecs, TransitionStyle transStyle);
  void pushBrowserFrame(const uint8_t* rgba, int w, int h); // receive a frame from browser backend
  // Start capturing from a Blackmagic input. Native SDK rather than an ffmpeg
  // pipe: the bundled ffmpeg has no decklink demuxer, and the SDK is already
  // linked here for playout.
  bool startDeckLinkCapture(const Cue& cue);
  void stopDeckLinkCapture();
  bool isDeckLinkCapturing() const { return deckLinkCapturing_; }

  // NDI input, received natively for the same reason DeckLink is captured
  // natively: no ffmpeg build has carried the libndi_newtek device since 2021.
  bool startNdiCapture(const Cue& cue);
  void stopNdiCapture();
  bool isNdiCapturing() const { return ndiCapturing_; }

  // Spout input, received natively for the same reason NDI is: no ffmpeg can
  // do it, and the platform capture backend has only ever been a scaffold.
  bool startSpoutCapture(const Cue& cue);
  void stopSpoutCapture();
  bool isSpoutCapturing() const { return spoutCapturing_; }
  std::string spoutCaptureError() const;
  // Why there is no picture, in words an operator can act on. Empty when the
  // source is arriving.
  std::string ndiCaptureError() const;
  // What the card reports it is receiving. Zero until the first frame lands,
  // which is the difference between "connected" and "has a picture".
  int deckLinkSignalWidth() const;
  int deckLinkSignalHeight() const;

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
  // Which decoder actually ran. "cpu" answers a different question from
  // "software": on macOS and Linux the decode is on hardware and only the
  // frames come down, so reporting the copy alone hid the whole thing.
  const char* activeDecodeName() const { return activeDecodeName_; }
  // WHETHER THE FRAME AVOIDED A COPY, asked directly rather than inferred from
  // a device pointer. That inference was a Windows detail: on macOS zero-copy
  // there IS no device to hand back (an IOSurface needs no matching), so a
  // caller testing activeDecodeDevice() would call a zero-copy frame a CPU
  // one -- and the bench would have reported the new path as the old one.
  bool activeDecodeZeroCopy() const { return activeDecodeZeroCopy_; }
  bool consumeDecodeStall();

  // A cue that should have had sound and got none. Returns the reason ONCE,
  // then empties -- same shape as consumeDecodeStall, polled by the transport.
  // Silence is the one fault an operator cannot see, so it has to be said.
  std::string consumeAudioStartFailure();
  // Latches true once when a still-image cue failed to decode (unsupported
  // format or corrupt file). The app polls this and tells the operator, instead
  // of the cue silently showing nothing. Cleared on read.
  bool consumeStillDecodeFailure();
  // The plugin instance sitting in a chain position, or null when that slot
  // holds one of Deckboy's own effects, has no plugin chosen, or names one this
  // machine has not got. The inspector asks so it can draw the plugin's OWN
  // parameter names on the rows; nothing else may hold on to the pointer.
  // Main thread only.
  std::shared_ptr<deckboy::platform::audioplugin::AudioPluginInstance>
  audioPluginForSlot(int index) const;
  // Latches true once when a plugin overran its time budget often enough to be
  // taken out of the chain. The operator is told WHICH one, because a plugin
  // that cannot keep up is a plugin to take out of the show. Cleared on read.
  bool consumeAudioPluginOverrun();
  // The plugin a cue's chain asked for and this machine has not got, if any.
  // A touring show keeps the id and the settings; it just cannot make the
  // sound, and saying so is better than a slot that silently passes through.
  std::string consumeMissingAudioPlugin();
  // Latches true once when a window capture had to drop back to the older
  // backend (see serviceWindowCaptureWatchdog). The operator is told, because
  // the fallback's picture can be wrong on a scaled display and that is worth
  // knowing before the show, not during it. Cleared on read.
  bool consumeSourceCaptureFallback();
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

  // IS THERE ANYTHING TO PUT ON SCREEN RIGHT NOW?
  //
  // True while the engine holds either the current frame or the outgoing one it
  // is still showing during a transition. False means the output draws black.
  //
  // Exposed because "does a cut flash black" is otherwise only answerable by
  // filming the screen: a cut used to drop the outgoing frame and then wait for
  // the incoming still to decode, and this is the predicate that was false in
  // between.
  // IS A TRANSITION RUNNING, and how far through is it?
  //
  // Exposed so "does a crossfade actually happen" is answerable over the wire
  // rather than by photographing a screen. The engine's own transition state
  // lives on the texture path, which the output does not use -- so a caller
  // needs to be able to see this to know whether the two agree.
  bool transitionRunning() const { return transitionActive_; }

  // ── THE TRANSITION THE OUTPUT CAN ACTUALLY USE ──────────────────────────
  //
  // The state above belongs to render(), which nothing calls: the output
  // composites from currentFrame(). So a crossfade was tracked by the engine
  // and never appeared on screen -- every transition in the program was a cut.
  //
  // These three give the compositor what it needs to do the blend itself: the
  // outgoing picture, how far through we are, and which style was asked for.
  // The outgoing frame is the one heldFrame_ already keeps to stop the black
  // flash between cues; a transition is that same hold, drawn on top.
  const DecodedFrame* outgoingFrame() const {
    return heldFrame_.has_value() ? &(*heldFrame_) : nullptr;
  }

  // 0 at the take, 1 when the transition is over. Returns 1 when nothing is
  // running, so a caller can treat "finished" and "never started" alike.
  double outgoingProgress01() const {
    if (!heldFrame_.has_value() || outgoingSeconds_ <= 0.0001) {
      return 1.0;
    }
    const double elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - heldFrameSince_).count();
    return std::clamp(elapsed / outgoingSeconds_, 0.0, 1.0);
  }

  // Let the outgoing frame go once it has nothing left to contribute. For a
  // cut that is the moment the new picture arrives; for a crossfade it is when
  // the blend reaches the end, because until then it IS the transition.
  void releaseHeldFrameIfTransitionDone() {
    if (!heldFrame_.has_value()) return;
    if (outgoingSeconds_ <= 0.0001) {
      heldFrame_.reset();
      return;
    }
    const double elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - heldFrameSince_).count();
    if (elapsed >= outgoingSeconds_) {
      heldFrame_.reset();
    }
  }

  TransitionStyle outgoingStyle() const { return outgoingStyle_; }
  double outgoingSeconds() const { return outgoingSeconds_; }

  bool hasPictureToShow() const {
    return texture_ != nullptr || (transitionActive_ && transitionTexture_ != nullptr);
  }

  // HOW MANY TIMES THIS DECK HAD NOTHING TO SHOW.
  //
  // Counted per rendered frame while a cue is racked. A cut that drops the
  // outgoing picture before the incoming one has decoded shows as a run of
  // these, and on a slide deck that run is the black flash between pages.
  // Reported by STATUS so it can be measured over the wire instead of filmed.
  std::uint64_t blankFrameCount() const { return blankFrames_; }
  void resetBlankFrameCount() { blankFrames_ = 0; }

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
  double currentVisualFadeGain() const {
    return visualFadeGainAt(fadeRidePositionSeconds());
  }

  // ── WHAT A FADE-IN RIDES ───────────────────────────────────────────────
  //
  // Normally the cue's own position, which is what a fade belongs to.
  //
  // But a HELD still -- any still-type cue whose duration is 0, which is what
  // "hold" means and what every pattern cue is by default -- has no timeline
  // at all. It sits at position 0 for as long as it is up. So a fade-in
  // evaluated against position was pinned at gain 0 and the cue was drawn at
  // alpha 0 FOREVER: a test pattern taken to a live output showed nothing,
  // and nothing in the app said why, because the frame was present and
  // correct and simply invisible.
  //
  // A held still therefore rides the wall clock from the moment it was taken:
  // the fade-in plays once, at the length asked for, and then sits at 1.
  double fadeRidePositionSeconds() const;

  // ── Font-drawn glyphs ──────────────────────────────────────────────────────
  //
  // The character grid is otherwise built from 5x7 bitmaps compiled into
  // media_engine.cpp, so a character exists only if somebody drew it -- which
  // caps the alphabet at the hundred-odd that were. Rendering through a FONT
  // lifts that cap entirely: anything an operator can type or paste can be
  // drawn, including emoji where the platform has a colour font for them.
  //
  // Rasterised ONCE per character per cell size and cached, because the cell
  // loop is threaded and runs for every cell of every frame. The key carries
  // the glyph string and the cell size, so a change to either rebuilds it and
  // nothing else does.
  struct FontCellGlyph {
    std::vector<std::uint8_t> rgba;   // cellW * cellH * 4
    bool colour = false;              // a colour emoji: ink must not tint it
  };
  std::vector<FontCellGlyph> fontGlyphs_;
  std::string fontGlyphsKey_;
  int fontGlyphW_ = 0;
  int fontGlyphH_ = 0;
  void rebuildFontGlyphs(const std::string& glyphs, int cellW, int cellH,
                         const std::string& fontPath);

 private:
  // -- Internal helpers -------------------------------------------------------
  double visualFadeGainAt(double positionSeconds) const;  // fade gain for visual (may suppress fade-out for auto-advance)
  double fadeGainAt(double positionSeconds) const;         // raw fade gain (in+out curve) at position
  double audioFadeGainAt(double positionSeconds) const;    // fade gain from atomic mirrors — the ONLY variant safe on the audio thread
  void syncAudioFadeParams();                              // publish fade params to the atomic mirrors (main thread)
  void refreshAudioEffectStack();                          // pull a changed effect stack across (audio thread)
  // Open, re-point and retire the plugin instances a cue's chain asks for.
  // Main thread, from the same sync that publishes the stack.
  std::vector<std::shared_ptr<deckboy::platform::audioplugin::AudioPluginInstance>>
  reconcileAudioPlugins(const Cue* cue);
  bool processAudioPluginSlot(std::size_t index,
                              const deckboy::audiofx::AudioEffect& fx,
                              double* samples, std::size_t frames);
  void publishPictureStats(const DecodedFrame& frame);      // frame brightness/motion for the audio effects (main thread)
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
  bool buildSourceCapturePlan(const Cue& cue, int w, int h,
                              deckboy::platform::SourceCapturePlan& plan) const; // the whole plan: args, fallback line, repaint title
  void serviceWindowCaptureWatchdog();                     // nudge a silent window cue, fall back if the capture never starts
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
  // Park samples handed to the device and release to audioTap_ only what the
  // device has actually played. See tapFifo_ for why.
  void tapPlayedAudio(const std::vector<std::int16_t>& sentToDevice);
  // Release whatever is still parked. Called when playback ends, so a take
  // does not lose its final buffer's worth of sound.
  void flushTappedAudio();
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
  // ── PRIMING ───────────────────────────────────────────────────────────
  //
  // Set when a file cue starts playing, cleared the moment the device is
  // actually released. While it is set the device stays paused, so the first
  // sound heard is the first sample decoded rather than whatever the device
  // makes of an empty stream.
  //
  // THE DEADLINE IS NOT OPTIONAL. A cue whose audio track is silent, whose
  // decoder failed, or which simply has no audio at all would otherwise wait
  // for a sample that is never coming -- so after this long the device is
  // released regardless and playback proceeds exactly as it did before.
  // 400ms is longer than a warm start needs by an order of magnitude and
  // shorter than an operator can attribute to anything.
  static constexpr int kAudioPrimeDeadlineMs = 400;
  // Roughly 20ms at 48k stereo: enough that the device has a run-up, small
  // enough that a warm file clears it on the first read.
  static constexpr int kAudioPrimeFrames = 960;

  bool audioPrimePending_ = false;
  std::chrono::steady_clock::time_point audioPrimeStartedAt_ {};

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
  // Pure function of the clock: the hearth replays a fixed window of its own
  // history each frame rather than carrying state, so any frame renders alone
  // and identically -- see the note in the builder.
  static void buildFireside(DecodedFrame& frame, double t,
                            double intensity = 1.0, int sparks = 34);

  // -- Engineering patterns ----------------------------------------------------
  //
  // The set Deckboy's own LED page generates in a browser. It drew twelve
  // charts the app could not, which is the wrong way round: the site is the
  // advert and the app is the product. Every one is a pure function of the
  // raster, so they dump, diff and bench like any other pattern.
  //
  // A shared 3x5 glyph routine, because the same font was already a lambda
  // inside buildTestClock AND buildFrameCount and a third copy is how two
  // definitions of one function start.
  static int patternGlyphWidth(std::size_t chars, int scale);
  static void drawPatternGlyphs(DecodedFrame& frame, int x, int y,
                                const std::string& text, int scale, SDL_Color color);
  // panelW/panelH are the LED tile size in pixels -- 128x128 is the common
  // one, but a wall that is not made of those is exactly the wall that needs
  // the map.
  static void buildPanelMap(DecodedFrame& frame, int panelW, int panelH);
  static void buildMoire(DecodedFrame& frame);
  static void buildDarkDetail(DecodedFrame& frame);
  static void buildUniformity(DecodedFrame& frame);
  static void buildBandingRamps(DecodedFrame& frame);
  static void buildSafeAreas(DecodedFrame& frame);
  static void buildBoresight(DecodedFrame& frame);
  static void buildPluge(DecodedFrame& frame);
  static void buildGreyscaleSteps(DecodedFrame& frame);
  static void buildConvergence(DecodedFrame& frame);
  static void buildMultiburst(DecodedFrame& frame);
  static void buildWindowPattern(DecodedFrame& frame, int percent);
  void syncPixelEffectsFromCue();   // colour/key edits reach EVERY view, not just the preview
  static void buildFrameCount(DecodedFrame& frame, double t, bool emojiBackdrop = false);                           // drop/duplicate + latency counter
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
  // Fed by BOTH the tone generator and the decode path (see pushScopeSamples),
  // so an audio cue's visualiser draws the same signal a tone card does.
  std::vector<std::int16_t> toneScopeL_;
  std::vector<std::int16_t> toneScopeR_;
  std::size_t toneScopePos_ = 0;
  mutable std::mutex scopeMutex_;
  void pushScopeSamples(const std::int16_t* interleaved, std::size_t frames,
                        int channels);
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
  // One-pole DC blocker on the chip voice's output; see nesNextSample.
  double chipDcPrevIn_ = 0.0;
  double chipDcPrevOut_ = 0.0;
  double nesNoiseAccum_ = 0.0;
  unsigned nesLfsr_ = 1u;
  bool chipGated_ = false;      // a keyboard has taken over from the cue's own pitch
  bool chipGateOpen_ = false;   // a key is currently down
  double chipNoteHz_ = 0.0;     // the note being held
  double chipVelocity_ = 1.0;
  double chipReleaseLevel_ = 0.0;   // level at the moment the key was released
  // Previous video-synth frame, for the feedback path. Kept as RGBA at the
  // output raster; reallocated only when the raster changes.
  std::vector<std::uint8_t> vsynthPrev_;
  // Persistent working buffers. These were allocated fresh every frame -- the
  // low-res raster, and an 8MB copy of the whole output for the CRT pass.
  // Reused now, so a steady-state frame allocates nothing.
  std::vector<std::uint8_t> vsynthSmall_;
  std::vector<std::uint8_t> vsynthCrtSrc_;
  // Imported sprite sheet, decoded once and sliced into tiles. Cached by path
  // and tile size: decoding is a synchronous ffmpeg call, fine as a one-off
  // when the operator picks a sheet and unacceptable per frame.
  std::string spriteSheetLoaded_;
  int spriteSheetTileW_ = 0;
  int spriteSheetTileH_ = 0;
  int spriteSheetCols_ = 0;
  int spriteSheetRows_ = 0;
  int spriteSheetW_ = 0;
  int spriteSheetH_ = 0;
  std::vector<std::uint8_t> spriteSheetRgba_;
  // Mean brightness per tile, so a tile can be chosen by density the same way
  // a glyph is. Blank tiles are excluded -- a sheet is mostly empty space and
  // picking those would just punch holes in the picture.
  std::vector<std::pair<int, int>> spriteTilesByLuma_;   // {luma, tileIndex}
 int vsynthPrevW_ = 0;
  int vsynthPrevH_ = 0;
  double vsynthRotation_ = 0.0;
  double vsynthLastSeconds_ = 0.0;   // for a real dt, not a per-frame step
  double lastVideoSynthSeconds_ = 0.0;   // display-rate limiter
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
  mutable std::uint64_t blankFrames_ = 0;    // asks answered with no picture at all
  // THE OUTGOING PICTURE, HELD.
  //
  // loadCue drops displayFrame_ the moment a new cue is racked, and the
  // compositor reads exactly that -- so between the drop and the incoming
  // cue's first decoded frame there is nothing to draw. On video you rarely
  // catch it; on a slide deck, where every page is decoded fresh, it is a
  // black frame between every slide.
  //
  // The old frame moves here instead and currentFrame() keeps handing it out
  // until the new one lands. Bounded, because a cue that never produces a
  // frame must not leave the previous slide up for the rest of the show.
  std::optional<DecodedFrame> heldFrame_;
  std::chrono::steady_clock::time_point heldFrameSince_;
  // When the current cue was taken. A held still has no position to read, so
  // this is the only clock its fade-in can ride -- see fadeRidePositionSeconds.
  std::chrono::steady_clock::time_point cueTakenAt_{};
  // What the operator asked for when this cue was taken, kept alongside the
  // held frame so the compositor can honour it.
  double outgoingSeconds_ = 0.0;
  TransitionStyle outgoingStyle_ = TransitionStyle::Cut;
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
  std::atomic<bool> audioMatrixActive_ {false};
  std::array<std::atomic<float>, kAudioMatrixSources * kMaxAudioMatrixOuts>
    audioMatrixGain_ {};
  std::atomic<int> audioCuePairOffset_ {0};  // 0 = outs 1-2, 1 = outs 3-4, ...
  std::deque<std::int16_t> audioDelayFifo_;  // holds processed samples for the delay

  // ── THE TAP RUNS ON PLAYED TIME, NOT QUEUE TIME ──────────────────────────
  //
  // audioTap_ feeds the recorder, the stream writers and NDI. It used to be
  // called the instant samples were handed to the device, while the PICTURE is
  // slaved to the audio clock computed as "queued minus still-buffered" -- what
  // has actually played. So every captured file carried audio ahead of its own
  // picture by the device buffer depth.
  //
  // MEASURED on 2026-09-14, two independent harnesses, sound early by 190-330ms
  // depending on rate -- about seven frames at 25fps. The live output was
  // measured clean in the same session (20ms better than ffplay through an
  // identical capture rig), which is the whole reason this is the right place
  // to fix it: the room hears played audio against the played-audio clock and
  // already agrees. Only the tap was reading from the wrong end of the buffer.
  //
  // So samples are parked here and released only once the device has actually
  // consumed them. Self-correcting: it releases against the device's own
  // reported queue rather than any assumed latency, so it needs no constant.
  std::mutex tapMutex_;
  std::vector<std::int16_t> tapFifo_;         // queued but not yet played
  std::uint64_t tapEmittedFrames_ = 0;        // stereo frames handed to the tap
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

  // -- State: audio effect stack (v0.100) ---------------------------------------
  // The chain is a VECTOR, so it cannot ride an atomic the way gain and pan do.
  // The main thread writes the pending copy under the mutex and bumps the
  // generation; the audio thread compares generations and only takes the lock
  // when the operator has actually changed something -- which is almost never,
  // measured against 48,000 samples a second. It then works from its own copy
  // and touches no shared memory for the rest of the chunk.
  std::mutex audioEffectsMutex_;
  std::vector<deckboy::audiofx::AudioEffect> audioEffectsPending_;  // main thread writes
  std::atomic<std::uint32_t> audioEffectsGeneration_ {0};
  std::vector<deckboy::audiofx::AudioEffect> audioEffectsActive_;   // audio thread only
  std::uint32_t audioEffectsSeen_ = 0;                              // audio thread only
  deckboy::audiofx::AudioEffectState audioEffectState_;             // audio thread only

  // -- State: third-party plugin slots -----------------------------------------
  // A Plugin slot in the chain is somebody else's code, and everything about
  // owning it is about WHICH THREAD does what:
  //
  //   OPENED on the main thread. Loading a module is a LoadLibrary, a factory
  //   walk and the plugin's own initialise; some of them check a licence server.
  //   None of that can happen between two audio buffers.
  //
  //   PROCESSED on the audio thread, through audioPluginsActive_, which is
  //   index-parallel to audioEffectsActive_ so a slot's instance is found
  //   without a lookup.
  //
  //   DESTROYED on the main thread. The audio thread hands an instance it is
  //   done with back through audioPluginRetired_ rather than dropping the last
  //   reference itself -- a plugin's destructor stops threads, frees megabytes
  //   and occasionally closes a window, and a show does not need that happening
  //   between two buffers either.
  // The biggest block a plugin is set up for, and therefore the size the audio
  // is handed to it in. A chunk longer than this is split rather than grown:
  // a plugin refuses a block bigger than it was prepared for, and growing its
  // buffers mid-show is the allocation on the audio thread this whole design
  // is arranged to avoid. 2048 frames is 43ms at 48k -- comfortably more than
  // this engine queues in one go.
  static constexpr int kMaxAudioPluginBlockFrames = 2048;
  using AudioPluginRef =
    std::shared_ptr<deckboy::platform::audioplugin::AudioPluginInstance>;
  std::vector<AudioPluginRef> audioPluginsPending_;   // main writes, under the mutex
  std::vector<std::string> audioPluginPendingIds_;    // main thread only
  std::vector<AudioPluginRef> audioPluginsActive_;    // audio thread only
  std::vector<AudioPluginRef> audioPluginRetired_;    // audio hands back, main frees
  std::vector<float> audioPluginScratch_;             // audio thread only
  std::atomic<bool> audioPluginOverran_ {false};      // a plugin took itself out
  std::string missingAudioPlugin_;                    // main thread only

  // The chain calls back out here for a Plugin slot; see audio_effects.hpp.
  struct AudioPluginHost final : deckboy::audiofx::AudioEffectHost {
    MediaEngine* engine = nullptr;
    bool processPluginSlot(std::size_t index,
                           const deckboy::audiofx::AudioEffect& fx,
                           double* samples, std::size_t frames) override;
  };
  AudioPluginHost audioPluginHost_;

  // -- State: what the deck-aware audio effects read ----------------------------
  // Picture, Placement, Seam, Frame lock and Suspend are the five effects that
  // use something only a cue deck knows. Same contract as the fade mirrors
  // directly above: the audio thread must never touch activeCue_, state_ or a
  // DecodedFrame, so the answers come across as atomics and are assembled into
  // an AudioEffectContext once per chunk.
  std::atomic<float> audioCtxLuma_ {0.5f};        // frame brightness, 0-1
  std::atomic<float> audioCtxMotion_ {0.0f};      // change since the last frame
  std::atomic<bool> audioCtxHasPicture_ {false};  // false = the two above are
                                                  // defaults, do not follow them
  std::atomic<float> audioCtxCenterX_ {0.5f};     // where the picture sits, 0-1
  std::atomic<float> audioCtxCenterY_ {0.5f};
  std::atomic<float> audioCtxCoverage_ {1.0f};    // fraction of the output filled
  std::atomic<double> audioCtxFramePeriod_ {0.0}; // seconds per video frame
  std::atomic<bool> audioCtxHeld_ {false};        // the cue is held
  // The picture AFTER its effects, published from the output compositor by
  // publishPostEffectStats. Ouroboros reads these; see AudioEffectContext.
  std::atomic<float> audioCtxPostLuma_ {0.5f};
  std::atomic<float> audioCtxPostMotion_ {0.0f};
  std::atomic<std::uint64_t> audioCtxPostAtMs_ {0};      // when it was last published
  float postLumaPrev_ = -1.0f;                    // main thread only
  // The deck's sound AFTER its effects, 0-1, with meter ballistics. Read by
  // the picture side's Audio LFO shape, which is the other half of the loop.
  std::atomic<float> programLevel_ {0.0f};
  double programLevelState_ = 0.0;                // audio thread only
  // A decimated mono copy of the played programme audio, for the picture
  // effects that draw with the SOUND rather than with its level. Written by the
  // audio thread and read by whoever is compositing, so it sits behind a mutex
  // and the reader takes a copy rather than holding the lock while it paints.
  static constexpr std::size_t kRecentAudioSamples = 4096;  // ~0.68s at 48k/8
  mutable std::mutex recentAudioMutex_;
  std::vector<float> recentAudio_;
  std::size_t recentAudioWrite_ = 0;
  int recentAudioPhase_ = 0;                      // audio thread only
  // The previous frame's luma, so motion is a difference. Main thread only.
  double pictureStatsPrevLuma_ = -1.0;

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
  const char* activeDecodeName_ = "software";  // d3d11va / videotoolbox / vaapi
  bool activeDecodeZeroCopy_ = false;        // frames never touched the CPU
  void* activeDecodeDevice_ = nullptr;       // device zero-copy frames live on (null = CPU)
  std::atomic<Uint64> lastFramePushMs_ {0};  // decode watchdog: last frame produced
  bool decodeStallLatched_ = false;          // watchdog tripped (consumed by transport)
  std::mutex audioStartFailureMutex_;
  std::string audioStartFailure_;            // why this cue has no sound
  void latchAudioStartFailure(std::string reason);
  std::uint64_t lastUploadedFrameIndex_ = static_cast<std::uint64_t>(-1); // skip redundant re-uploads in update()
  bool hiddenUploadTarget_ = false;   // see setHiddenUploadTarget
  double lastPatternRebuildSeconds_ = -1.0;  // animated-pattern rebuild throttle (30 fps; terrarium 9)
  // Persistent workers for the synth's row loops. Creating a pool per frame
  // is what ran the machine out of committed memory: 31 threads a rebuild on
  // a 32-core box, thousands a second, each committing a stack the OS could
  // not reclaim fast enough.
  deckboy::RowWorkers vsynthWorkers_;
  // Set while VJ mode is on: decode to CPU pixels so the control window can
  // build A and B previews from them. Takes effect on the next take.
  bool forcePixelFrames_ = false;

  // -- State: browser capture --------------------------------------------------
  bool isBrowserCapturing_ = false;          // browser backend is sending frames
  int browserCaptureW_ = 1280;               // browser frame width
  int browserCaptureH_ = 720;                // browser frame height
  std::uint64_t browserFrameIdx_ = 0;        // sequential browser frame counter
  std::unique_ptr<deckboy::platform::video::DeckLinkInput> deckLinkInput_;
  bool deckLinkCapturing_ = false;
  std::uint64_t deckLinkFrameIdx_ = 0;
  std::vector<std::uint8_t> deckLinkRgba_;   // BGRA -> RGBA scratch, reused
  std::unique_ptr<deckboy::platform::video::NdiInput> ndiInput_;
  bool ndiCapturing_ = false;
  std::uint64_t ndiFrameIdx_ = 0;
  std::vector<std::uint8_t> ndiRgba_;        // BGRA -> RGBA scratch, reused
  std::unique_ptr<deckboy::platform::video::SpoutInput> spoutInput_;
  bool spoutCapturing_ = false;
  std::uint64_t spoutFrameIdx_ = 0;
  std::vector<std::uint8_t> spoutRgba_;      // BGRA -> RGBA scratch, reused

  // -- State: source capture ---------------------------------------------------
  bool isSourceCapturing_ = false;           // source capture is active
  // Window capture through Windows.Graphics.Capture only produces a frame when
  // the window paints, and a machine with an older ffmpeg has no WGC at all, so
  // the capture is watched for its first frame rather than assumed live.
  // serviceWindowCaptureWatchdog() owns all of these; the capture thread only
  // ever increments the counter.
  std::atomic<std::uint64_t> sourceFramesSeen_{0};   // frames the capture thread has pushed
  std::string sourceRepaintTitle_;                   // window to poke while waiting (empty = nothing to poke)
  std::vector<std::string> sourceFallbackArgs_;      // capture line to try if this one never delivers
  std::string sourceFallbackBackendId_;
  std::chrono::steady_clock::time_point sourceCaptureStartedAt_{};
  std::chrono::steady_clock::time_point sourceNudgedAt_{};
  int sourceNudgeCount_ = 0;
  int sourceCaptureWidth_ = 0;
  int sourceCaptureHeight_ = 0;
  std::atomic<bool> sourceCaptureFellBack_{false};  // read once by the app, then cleared

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
