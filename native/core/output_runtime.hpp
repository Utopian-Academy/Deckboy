// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// output_runtime.hpp — the per-output runtime state.
//
// One OutputRuntime per output destination: its SDL window and renderer, its
// compositor texture, and whichever of the stream writer, NDI sender, DeckLink
// card, Spout/Syphon sender and ST 2110 sender that output is using. Plus the
// health state machine they all report through, and the writer thread's
// queue.
//
// PURE DATA, AND THAT IS THE POINT. These declarations sat in main.cpp, which
// meant the eight files that use them -- app_output_mgmt, app_render_output,
// app_update, app_accessors, app_cue_mgmt, app_project_state,
// app_render_settings and main.cpp itself -- could only see them by being
// compiled INTO main.cpp as .ipp includes. Nothing here needs the App class,
// so nothing here was ever the reason for that.
//
// Fields are added under the same #ifdef the backend is compiled under, so a
// build without NDI or DeckLink carries neither the fields nor the headers.
// ============================================================================

#ifndef DECKBOY_CORE_OUTPUT_RUNTIME_HPP
#define DECKBOY_CORE_OUTPUT_RUNTIME_HPP

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/sdl_compat.hpp"
#include "core/subprocess.hpp"
#include "core/types.hpp"

// UNGUARDED, exactly as main.cpp has always included them: each of these
// headers handles its own disabled build internally, and guarding them here
// instead would be a second, different answer to a question already settled.
// The FIELDS below stay guarded, so a build without a backend still carries
// neither the field nor its type.
#include "platform/decklink.hpp"
#include "platform/ndi_api.hpp"
#include "platform/siphon_spout.hpp"
#include "platform/st2110_output.hpp"

#ifdef _WIN32
#include <d3d11.h>
#endif

// ── Output runtime structs ──────────────────────────────────────────────────
// These structs track the state of each output destination (window, stream,
// NDI, DeckLink). Each output has its own SDL window/renderer, compositor
// texture, and optional stream writer thread.

// Health state machine for output windows and streams.
enum class OutputHealthState {
  Off,           // Output not enabled
  Armed,         // Enabled but not yet rendering (waiting for first frame)
  Live,          // Actively rendering frames
  Recovering,    // Recovering from an error (auto-restart)
  Error,         // Failed — requires manual intervention
};

// One captured frame + audio chunk queued for the stream writer thread.
struct OutputStreamPacket {
  int width = 0;
  int height = 0;
  Uint64 capturedAtMs = 0;                   // SDL tick when frame was captured
  // SHARED, not copied. This was a plain vector, so handing a frame to the
  // writer copied the whole raster on the render thread -- 33MB at 4K, every
  // frame, and again for every frame the CFR pacer repeats. The writer only
  // ever reads it, so one immutable buffer can be handed to as many packets as
  // the pacer emits.
  std::shared_ptr<const std::vector<std::uint8_t>> videoBytes;
  std::vector<std::int16_t> audioSamples;     // Interleaved 16-bit PCM
};

// Thread-safe state for the ffmpeg stream writer background thread.
// The main thread pushes OutputStreamPackets via the condition variable;
// the writer thread pops them and pipes to ffmpeg's stdin.
struct OutputStreamWriterState {
  std::mutex mutex;
  std::condition_variable cv;
  std::thread thread;
  int videoPipeFd = -1;             // Pipe fd to ffmpeg video stdin
  // Atomic: on Windows this is assigned by the named-pipe connect thread
  // AFTER the writer starts, because ffmpeg only opens its end once running.
  std::atomic<int> audioPipeFd {-1};   // Pipe fd to ffmpeg audio input
  bool stop = false;                // Signal the writer thread to exit
  bool failed = false;              // Writer encountered a fatal error
  std::string failureReason;
  // A QUEUE, not a mailbox. This was a single pendingPacket slot, so a second
  // frame pushed before the writer drained the first SILENTLY REPLACED it --
  // while the pacer counted both as written. That is why a recording ran short
  // whenever capture outpaced the writer, and why the two attempts at filling
  // the cadence with repeats did nothing: every repeat landed in the same slot
  // and only one was ever written.
  //
  // Bounded, because the alternative to dropping under sustained overload is
  // growing without limit during a show. Depth is in FRAMES and deliberately
  // small: at 4K a frame is 33MB, and a deep queue would mean a recording that
  // lags seconds behind the programme before anyone notices.
  static constexpr std::size_t kMaxQueuedPackets = 8;
  std::deque<OutputStreamPacket> queue;
  std::uint64_t packetsDropped = 0;  // queue was full: a REAL lost frame
  // Audio rides its OWN thread and mailbox. Both pipes used to be fed from one
  // thread, audio first and then a BLOCKING video write -- which deadlocks: the
  // mp4 muxer will not drain video until it has audio covering the same
  // timestamps, so the video write blocked, which stopped the only thread that
  // could have supplied that audio. MEASURED: every recording froze after
  // exactly 170 frames with ffmpeg reporting frame=0, and the same ffmpeg
  // command driven by hand ran fine. Two pipes drained in an order the reader
  // chooses need two writers.
  std::mutex audioMutex;
  std::condition_variable audioCv;
  std::thread audioThread;
  std::vector<std::int16_t> pendingAudio;
  std::uint64_t packetsQueued = 0;  // Total packets queued by main thread
  std::uint64_t packetsWritten = 0; // Total packets written by writer thread
  std::uint64_t videoBytesWritten = 0;
  std::uint64_t audioBytesWritten = 0;
};

// Per-output runtime state: SDL window/renderer, compositor, stream writer,
// NDI sender, DeckLink output, and FPS telemetry.
struct OutputRuntime {
#ifdef _WIN32
  // Server end of the audio named pipe. Windows children get one piped
  // stdin and video already uses it, so audio needs its own channel.
  void* streamAudioPipeHandle = nullptr;
#endif
  // Pixel buffer snapshot for streaming/NDI/DeckLink sinks.
  struct CapturedFrame {
    int width = 0;
    int height = 0;
    Uint64 capturedAtMs = 0;
    std::vector<std::uint8_t> pixels;    // RGBA pixel data
  };

  // SDL output window and renderer (one per output destination)
  SDL_Window* outputWindow = nullptr;
  SDL_Renderer* outputRenderer = nullptr;
  // One-shot latch: while an output is disabled we paint its still-visible
  // window black exactly once (not every frame — presenting is vsync-blocking).
  // Reset whenever the output renders again, so re-disabling re-blacks it.
  bool blackedWhileDisabled = false;
  SDL_Texture* compositorTexture = nullptr;  // Offscreen compositor target
  // Scratch target at the RECORDING raster. The composite is blitted into this
  // on the GPU and this is what gets read back, so a 1080 recording off a 4K
  // programme moves a quarter of the bytes across the bus.
  SDL_Texture* egressScaleTexture = nullptr;
  int egressScaleW = 0;
  int egressScaleH = 0;
  // Staging ring for the asynchronous readback (see gpu_readback.hpp). Null on
  // a non-D3D11 renderer, where the path falls back to SDL_RenderReadPixels.
  void* egressReadback = nullptr;
  int egressReadbackW = 0;
  int egressReadbackH = 0;
  // Latched when the renderer turns out to have no asynchronous readback, so
  // the creation is not retried on the render thread every single frame.
  bool egressReadbackUnavailable = false;
  // Consecutive frames the asynchronous readback has produced nothing. One or
  // two is ordinary -- the ring is filling, or the GPU is a frame behind, and
  // the caller repeats the previous picture. Forever is not: it means the
  // recording is one still frame for its whole length, which is what a macOS
  // take looked like, and every status line said the capture was healthy.
  int egressReadbackMisses = 0;
  // The captured picture, published once as an immutable buffer. The CFR pacer
  // repeats the last picture to cover a gap, and every repeat used to copy the
  // whole raster again -- 33MB at 4K, on the render thread, for pixels that had
  // not changed.
  std::shared_ptr<const std::vector<std::uint8_t>> egressPublished;
  Uint64 egressPublishedAtMs = 0;
  // Which capture path this output's recording is on, so the choice is
  // reportable rather than deduced. -1 = not yet logged.
  int egressPathLogged = -1;
  // CFR pacer for a file recording. A broadcast deliverable must contain
  // exactly rate x elapsed frames; the encoder stamps by ARRIVAL ORDER at the
  // declared rate, so delivering fewer frames than promised does not slow the
  // file down, it SHORTENS it. Counting what is owed and repeating the last
  // frame to cover a gap makes the duration correct by construction rather
  // than dependent on the capture keeping up.
  Uint64 recordPacerStartMs = 0;
  std::uint64_t recordFramesWritten = 0;
  // Frames written across the WHOLE take, surviving segment rolls, so each
  // segment's timecode continues where the previous one stopped rather than
  // restarting at the take's start value.
  std::uint64_t recordTakeFrames = 0;
  // Frames carrying a NEW picture, as opposed to frames delivered. The pacer
  // repeats the last picture to keep the duration right, and those repeats
  // must not be allowed to satisfy the dropped-frame alarm -- otherwise a
  // capture that has stalled completely produces a duration-correct file of
  // one still image and reports itself healthy.
  std::uint64_t recordFreshFrames = 0;
  Uint64 lastFreshCaptureMs = 0;     // when the picture last actually changed
  Uint64 lastSegmentSizeCheckMs = 0;   // segment size is stat'd ~1Hz, not per frame
  Uint64 lastDropWarnMs = 0;           // dropped-frame alarm, rate limited to 1Hz
  std::uint64_t recordDroppedFrames = 0;
  int compositorWidth = 0;
  int compositorHeight = 0;
  Uint32 compositorFormat = SDL_PIXELFORMAT_UNKNOWN;
  int compositorBitDepth = 8;
  // Per-deck bridge texture for compositing the source frame at this output.
  // Format tracks SDL_PixelFormat so an RGBA→NV12 (or vice-versa) cue
  // switch rebuilds the texture instead of silently corrupting its sampler.
  std::map<int, SDL_Texture*> layerBridgeTextures;
  std::map<int, int> layerBridgeTextureWidths;
  std::map<int, int> layerBridgeTextureHeights;
  std::map<int, Uint32> layerBridgeTextureFormats;
  std::map<int, std::uint64_t> layerBridgeFrameIndices;
  std::map<int, std::string> layerBridgeCueKeys;
  // Per-overlay bridge texture (same rationale, keyed by overlay identity).
  std::map<std::string, SDL_Texture*> overlayBridgeTextures;
  std::map<std::string, int> overlayBridgeTextureWidths;
  std::map<std::string, int> overlayBridgeTextureHeights;
  std::map<std::string, Uint32> overlayBridgeTextureFormats;
  std::map<std::string, std::uint64_t> overlayBridgeFrameIndices;
  std::map<std::string, std::string> overlayBridgeCueKeys;
  // RENDER TARGETS, which the bridge textures above cannot be: those are
  // STATIC-access and uploaded into, these are drawn into. The prompter needs
  // one to mirror the whole picture in one blit.
  struct BridgeTarget {
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
  };
  std::map<std::string, BridgeTarget> overlayBridgeTargets;
  // WHICH PICTURE IS ALREADY IN EACH TRANSITION BRIDGE TEXTURE, so a style
  // that draws the same frame several times in one pass uploads it once.
  //
  // PER OUTPUT, because the texture it describes is per output. It began as a
  // single app-wide map, and with two outputs armed the first one to render
  // uploaded and claimed the stamp, and the second then skipped the upload
  // into its OWN texture and drew one that had never been filled -- so a
  // recording taken while a window output was live showed black for the whole
  // transition.
  std::map<std::string, std::uintptr_t> transitionUploadStamps;
  std::vector<std::uint8_t> layerBridgeScratchPixels;
#if DECKBOY_INPROC_DECODE
  // Zero-copy compositing (in-process d3d11va decode): per-deck persistent
  // NV12 D3D11 texture wrapped as an SDL_Texture. Decoded texture-array
  // slices are GPU-copied into it — the frame never touches the CPU. Only
  // used when the frame's decode device IS this renderer's device; other
  // devices (secondary outputs) fall back to a CPU download into the
  // classic bridge texture.
  void* rendererD3DDevice = nullptr;                 // cached ID3D11Device*
  // macOS: one wrapped CVPixelBuffer texture per deck, REPLACED per  frame
  // advance rather than copied into. Kept separately from layerGpuTextures
  // because the lifetime differs -- a D3D11 wrap persists across frames and a
  // pixel-buffer wrap belongs to exactly one frame.
  std::map<int, SDL_Texture*> layerPixelBufferTextures;
  std::map<int, std::uint64_t> layerPixelBufferFrameIndices;
  std::map<int, SDL_Texture*> layerGpuTextures;      // wrapped SDL textures
  std::map<int, void*> layerGpuTexture2Ds;           // backing ID3D11Texture2D*
  std::map<int, std::pair<int, int>> layerGpuTextureSizes;
  // The wrap's layout must match the decoded surface, so a cue change from
  // 8-bit to 10-bit content (NV12 → P010) has to rebuild the texture just as a
  // size change does. Keyed here rather than folded into the size pair so the
  // reason a rebuild happened stays readable.
  std::map<int, FramePixelFormat> layerGpuTextureFormats;
  std::map<int, std::uint64_t> layerGpuFrameIndices;
  DecodedFrame gpuDownloadScratch;                   // device-mismatch fallback
  int gpuDownloadScratchDeck = -1;                   // deck the scratch holds
#endif
#ifdef _WIN32
  ChildProcess streamProcess;         // Windows: ffmpeg subprocess with stdin pipe
#else
  pid_t streamPid = -1;
  int streamPipeFd = -1;
  int streamAudioPipeFd = -1;
  std::string streamVideoPipePath;
#endif
  std::map<int, std::uint64_t> streamAudioReadSamplesByDeck;
  double streamAudioSampleRemainder = 0.0;
  std::map<int, std::uint64_t> ndiAudioReadSamplesByDeck;
  double ndiAudioSampleRemainder = 0.0;
  std::string streamSpec;
  // The frame rate a LIVE NETWORK stream was opened with, held for the life of
  // the connection. See ensureOutputStreamRunning: the rate is declared to the
  // encoder when it starts, and a stream that re-declares it has to reconnect.
  double streamLockedFps = 0.0;
  // Is the encoder actually SWALLOWING what it is handed? packetsWritten only
  // climbs when a write to it returns, so a frozen count with a full queue is
  // a stalled sink -- which is the state that used to be reported as "live".
  Uint64 streamLastDrainAtMs = 0;
  std::uint64_t streamLastSeenWritten = 0;
  Uint64 streamStartedAtMs = 0;
  // Sampled once a second so the panel can show a real rate rather than a
  // configured one; the two differ exactly when something is wrong.
  Uint64 streamRateSampledAtMs = 0;
  std::uint64_t streamRateSampleBytes = 0;
  double streamMeasuredKbps = 0.0;
  std::string streamCommand;
  std::vector<std::uint8_t> streamFrameBuffer;
  int streamFrameWidth = 0;
  int streamFrameHeight = 0;
  bool streamStartFailed = false;
  // A file sink has to FINALIZE — flush the muxer and write its trailer. A
  // network sink has nothing to finalize, so the two get different shutdown
  // budgets; see stopOutputStreamRuntime.
  bool streamToFile = false;
  Uint64 streamRestartBlockedUntilMs = 0;
  std::shared_ptr<OutputStreamWriterState> streamWriter;
  CapturedFrame latestCapturedFrame;
  std::deque<CapturedFrame> delayFrames;
  Uint64 lastEgressCaptureAtMs = 0;
  Uint64 lastStreamCaptureSentAtMs = 0;
  // House overlay: the still laid over this output's picture. Cached against
  // the path it came from, so it is decoded once rather than every frame.
  // Outside the NDI guard deliberately -- the matte and overlay belong to every
  // output on every build, and a build without the NDI SDK still has them.
  SDL_Texture* overlayTexture = nullptr;
  std::string overlayTexturePath;

#if defined(DECKBOY_HAS_NDI_SDK)
  NDIlib_send_instance_t ndiSender = nullptr;
  // Last tally state a receiver reported for this sender, and whether we have
  // heard one at all yet. Per output: two outputs can be watched by two
  // different receivers, and the one that changed is the one that means it.
  bool ndiTallyOnProgram = false;
  bool ndiTallySeen = false;
  std::string ndiSenderName;
  std::vector<std::uint8_t> ndiFrameBuffer;
  NDIlib_send_instance_t ndiKeySender = nullptr;
  std::string ndiKeySenderName;
  std::vector<std::uint8_t> ndiKeyFrameBuffer;
#endif
#if defined(DECKBOY_HAS_DECKLINK)
  std::unique_ptr<deckboy::platform::video::DeckLinkOutput> deckLinkOutput;
  std::vector<std::uint8_t> deckLinkFrameBuffer;
#endif
#if defined(DECKBOY_HAS_SPOUT)
  std::unique_ptr<deckboy::platform::video::SiphonSpoutSender> spoutSender;
#endif
  // ST 2110-20 sender. No SDK and no platform guard — plain sockets, so this
  // exists on every build.
  std::unique_ptr<deckboy::platform::video::St2110Output> st2110Sender;
  std::unique_ptr<deckboy::platform::video::St2110AudioOutput> st2110AudioSender;
  // Program-monitor tap: a small copy of this output's finished composite,
  // sampled on the output's own render pass so the control-window preview
  // advances in lockstep with what actually leaves the machine instead of
  // trailing it. Small on purpose — the readback cost scales with area.
  SDL_Texture* previewTapTexture = nullptr;
  int previewTapTextureW = 0;
  int previewTapTextureH = 0;
  std::vector<std::uint8_t> previewTapPixels;  // RGBA32
  int previewTapW = 0;
  int previewTapH = 0;
  std::uint64_t previewTapSerial = 0;  // bumped per successful tap; 0 = nothing captured
  bool recoveryPausedByEscape = false;
  bool fullscreenIntended = false;  // user explicitly wants fullscreen — re-assert if dropped
  // The display this output is pinned to (by name) is not currently attached.
  // Parks recovery instead of slamming the program feed fullscreen onto
  // whatever monitor inherited the index — unplugging a projector must not
  // take over the operator's control screen. Cleared when the panel returns.
  bool awaitingDisplayReturn = false;
  Uint64 lastFullscreenRequestMs = 0;
  Uint64 lastRecoveryAttemptMs = 0;
  bool pendingDisplayRuntimeRebuild = false;
  bool pendingDisplayMoveFullscreen = false;
  Uint64 displayMoveRetryAtMs = 0;
  Uint64 suppressRecoveryUntilMs = 0;
  // Recovery strike backoff: if recovery keeps firing for the same output,
  // something structural is wrong (SDL and the WM disagree about placement).
  // Repeated exit-fullscreen/move/re-enter/raise cycles steal keyboard focus
  // from the control window every pass — the operator experiences a fight.
  // After kMaxRecoveryStrikes within the strike window, recovery pauses.
  int recoveryStrikeCount = 0;
  Uint64 recoveryStrikeWindowStartMs = 0;
  OutputHealthState healthState = OutputHealthState::Off;
  std::string healthReason;
  Uint64 healthUpdatedAtMs = 0;
  Uint64 fpsSampleStartedAtMs = 0;
  Uint32 fpsFrameCount = 0;
  double fpsMeasured = 0.0;
  Uint64 streamFpsSampleStartedAtMs = 0;
  std::uint64_t streamFpsPacketsAtSampleStart = 0;
  double streamFpsMeasured = 0.0;
};

#endif  // DECKBOY_CORE_OUTPUT_RUNTIME_HPP
