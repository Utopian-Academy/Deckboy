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

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace deckboy::gpu {
struct GpuEffects;
}
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
#include "platform/display_topology.hpp"
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
namespace deckboy::core::hdr { class HdrEncoder; }

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
  // Set for an HDR frame: videoBytes are the compositor's RGBA HALF floats,
  // and the WRITER thread turns them into the encoder's 16-bit PQ or HLG
  // just before the pipe (MEASURED: 4.5 ms a 1080p frame that was the render
  // thread's). Null for everything else, which is already what ffmpeg reads.
  std::shared_ptr<const deckboy::core::hdr::HdrEncoder> hdrEncoder;
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
  bool preserveAudioTimeline = false; // Web playout must never discard PCM bytes
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

// ── KEY + FILL ────────────────────────────────────────────────────────────
//
// Split one composited RGBA frame into the two signals a broadcast keyer
// wants: FILL, the picture, and KEY, a greyscale matte saying how opaque the
// fill is. A vision mixer downstream lays the fill over its programme through
// the key, which is how a graphic reaches air in a gallery.
//
// IN CORE, NOT BEHIND THE DECKLINK IFDEF, on purpose. This is arithmetic on a
// buffer; putting it beside the card code meant the one part of key+fill that
// can be verified without hardware could only be compiled on a machine with
// the SDK. Now --smoke checks it everywhere.
//
// THE FILL IS PREMULTIPLIED, which every hardware keyer expects: a
// half-transparent white goes out as mid grey at 50% key, not white at 50%
// key. Unmultiplied fill puts a bright fringe around every soft edge and is
// invisible until it is on air.
inline void splitKeyAndFill(const std::uint8_t* rgba, std::size_t pixelCount,
                            std::uint8_t* fill, std::uint8_t* key) {
  if (!rgba || !fill || !key) return;
  for (std::size_t i = 0; i < pixelCount; ++i) {
    const std::size_t at = i * 4;
    const unsigned a = rgba[at + 3];
    // ROUNDED, not truncated: (v * a + 127) / 255 is the exact 8-bit multiply.
    // Truncating darkens every partly transparent pixel by up to a code value,
    // which reads as a dirty fringe on a large soft-edged graphic.
    fill[at + 0] = static_cast<std::uint8_t>((rgba[at + 0] * a + 127) / 255);
    fill[at + 1] = static_cast<std::uint8_t>((rgba[at + 1] * a + 127) / 255);
    fill[at + 2] = static_cast<std::uint8_t>((rgba[at + 2] * a + 127) / 255);
    fill[at + 3] = 255;
    // The key is the alpha drawn as a picture: white where the fill is solid.
    // Opaque itself, because a keyer reads luminance and a key carrying its
    // own transparency is invisible to one.
    key[at + 0] = static_cast<std::uint8_t>(a);
    key[at + 1] = static_cast<std::uint8_t>(a);
    key[at + 2] = static_cast<std::uint8_t>(a);
    key[at + 3] = 255;
  }
}

// ── A TAKE'S CLOCK FOLLOWS THE SOUND CARD ──────────────────────────────────
//
// The sound reaches the recorder as the card PLAYS it, at the card's clock;
// the pacer used to count the frames owed by the PC's clock. No two crystals
// agree, so on a long take one of them won. A card running slow left the sound
// short of what the frames asked for, and past the 60 ms carry the shortfall
// was written as silence -- a click a second after about 54 minutes at the
// 18.6 ppm measured on a real card. A card running fast left unread sound
// piling up, so the sound fell behind the picture for good: about 67 ms an
// hour. CI's stand-in sound device runs about 1% off, and the loop check heard
// those holes from five seconds into its take.
//
// So while a deck is sounding, the take's timeline runs at the card's rate:
// the BALANCE of the sound carry (what the ring holds unread, less what the
// frames have asked for and not had) is steered to zero, by trimming the rate
// the timeline advances at. Zero is where the balance sits on average when the
// two clocks agree -- it swings either side by one push of the card's audio --
// so the take keeps the sync it was calibrated at. NOT where it happened to
// sit when the deck began: that was tried, and a take that began in debt held
// its debt, its swings crossed the carry limit and wrote a frame of silence
// every two seconds. The sound stays exactly what was played, sample for
// sample; the picture absorbs the difference as the pacer's ordinary repeats,
// about one frame in half an hour on a real card. Silent, it is the PC's clock
// again. The trim is capped at 2%: a device further off than that keeps the
// old carry-and-pad.
struct RecordClockSteer {
  double seconds = 0.0;     // the take's timeline since the pacer started
  Uint64 updatedMs = 0;
  double trim = 0.0;        // the timeline's rate offset while a deck sounds
  double integral = 0.0;    // the card's long-run rate error, as learned
  double balance = 0.0;     // smoothed carry balance, seconds
  double settled = 0.0;     // seconds since the deck began sounding
  bool sounding = false;

  static constexpr double kLimit = 0.02;
  static constexpr double kSmoothSeconds = 0.5;
  static constexpr double kSettleSeconds = 0.5;  // let the average form first
  static constexpr double kProportional = 0.5;   // per second of balance
  static constexpr double kIntegral = 0.1;       // per second of balance, per second

  void restart(Uint64 nowMs) {
    seconds = 0.0;
    updatedMs = nowMs;
  }

  void advance(Uint64 nowMs) {
    if (updatedMs != 0 && nowMs > updatedMs) {
      seconds += static_cast<double>(nowMs - updatedMs) / 1000.0 * (1.0 + trim);
    }
    updatedMs = nowMs;
  }

  // One written frame's worth of sound was asked for; `rawBalance` is the
  // ring's unread sound less the carry owed, in seconds, before the take.
  void observe(double rawBalance, double dt) {
    if (!sounding) {
      sounding = true;
      settled = 0.0;
      balance = 0.0;
    }
    balance += (rawBalance - balance) * std::min(1.0, dt / kSmoothSeconds);
    settled += dt;
    if (settled < kSettleSeconds) {
      trim = integral;
      return;
    }
    integral = std::clamp(integral + kIntegral * balance * dt, -kLimit, kLimit);
    trim = std::clamp(kProportional * balance + integral, -kLimit, kLimit);
  }

  void quiet() {
    sounding = false;
    trim = 0.0;
  }
};

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
    std::vector<std::uint8_t> pixels;    // BGRA (read back as SDL_PIXELFORMAT_BGRA32)
    // An HDR frame: pixels are RGBA half floats, to be encoded by this
    // (OutputStreamPacket::hdrEncoder) on the writer thread.
    std::shared_ptr<const deckboy::core::hdr::HdrEncoder> hdrEncoder;
    // ...and they may live HERE instead of in `pixels`: a pooled buffer the
    // readback wrote straight into, handed to the writer as it is. At 4K that
    // is 66 MB a frame the render thread no longer copies (MEASURED 16 ms).
    std::shared_ptr<const std::vector<std::uint8_t>> shared;
    bool empty() const { return pixels.empty() && (!shared || shared->empty()); }
    // The bytes wherever they live. A reader that looks only at `pixels`
    // sees an SDR recording's frames as missing.
    const std::uint8_t* data() const { return shared ? shared->data() : pixels.data(); }
    std::size_t bytes() const { return shared ? shared->size() : pixels.size(); }
  };
  // Buffers an SDR recording's asynchronous readback writes into and its
  // writer reads from (pooledEgressBuffer), so a frame is copied once on the
  // render thread instead of twice.
  std::vector<std::shared_ptr<std::vector<std::uint8_t>>> egressFramePool;

  // SDL output window and renderer (one per output destination)
  SDL_Window* outputWindow = nullptr;
  SDL_Renderer* outputRenderer = nullptr;
  // One-shot latch: while an output is disabled we paint its still-visible
  // window black exactly once (not every frame — presenting is vsync-blocking).
  // Reset whenever the output renders again, so re-disabling re-blacks it.
  bool blackedWhileDisabled = false;
  // The renderer outputs scRGB and the compositor is linear RGBA64_FLOAT
  // (Project::hdrOutputs, and the renderer agreed to it).
  bool hdr = false;
  bool hdrUnavailable = false;   // asked for HDR and the renderer could not
  SDL_Texture* compositorTexture = nullptr;  // Offscreen compositor target
  // Scratch target at the RECORDING raster. The composite is blitted into this
  // on the GPU and this is what gets read back, so a 1080 recording off a 4K
  // programme moves a quarter of the bytes across the bus.
  SDL_Texture* egressScaleTexture = nullptr;
  int egressScaleW = 0;
  int egressScaleH = 0;
  // The renderer egressScaleTexture and egressReadback were made on. Not
  // always this output's own: a recording that reads another output's
  // composite (egressProgrammeSource) keeps them on THAT output's renderer,
  // and they are released before that renderer goes (releaseEgressGpuOn).
  SDL_Renderer* egressGpuRenderer = nullptr;
  // Which programme this output's recording last read (-1: its own
  // composite), so the path is logged once whenever it changes.
  int egressProgrammeLogged = -2;
  // Staging ring for the asynchronous readback (see gpu_readback.hpp). Null on
  // a non-D3D11 renderer, where the path falls back to SDL_RenderReadPixels.
  void* egressReadback = nullptr;
  int egressReadbackW = 0;
  int egressReadbackH = 0;
  // The same ring for an HDR output's float compositor, read for its HDR
  // stream or recording (readHdrCompositorSignal). The synchronous read it
  // replaces stalled the GPU 7 ms a 1080p frame and dragged a 60 Hz output
  // to 25 (MEASURED). Same contract, same retirement after a second of misses.
  // First choice on Direct3D 11: the GPU makes the P010 itself
  // (deckboy::gpu::createHdrP010Readback) and the CPU only copies it out.
  // An HDR output's ST 2110 / SDI picture: the same GPU conversion at 4:2:2
  // (P216 layout), read for the wire every frame.
  void* hdrWireReadback = nullptr;
  bool hdrWireUnavailable = false;
  int hdrWireMisses = 0;
  std::vector<std::uint8_t> hdrWirePlanes;
  // Read once a frame however many wires want it (ST 2110 and SDI both).
  std::uint64_t hdrWireReadAtPresent = ~0ull;
  bool hdrWireLastRead = false;
  void* hdrP010Readback = nullptr;
  bool hdrP010Unavailable = false;
  int hdrP010Misses = 0;
  void* hdrReadback = nullptr;
  int hdrReadbackW = 0;
  int hdrReadbackH = 0;
  bool hdrReadbackUnavailable = false;
  int hdrReadbackMisses = 0;
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
  // THE PICTURE COMES OUT OF THE READBACK LATE, SO THE SOUND WAITS FOR IT.
  // The asynchronous readback hands the writer a frame kAsyncReadbackDepth
  // captures old, while the sound was read from the ring as it played: every
  // recording made that way had its picture three frames behind its sound
  // (MEASURED, tools/record_avsync_check.py at 30 fps: +106 ms, outside EBU
  // R37; the synchronous read +23 ms). So the sound runs through a delay line
  // of the same depth -- the same number of samples per written frame, the
  // first ones silence -- set at the start of each take.
  std::deque<std::vector<std::int16_t>> recordAudioDelayLine;
  std::size_t recordAudioDelayChunks = 0;
  // The take's frame clock, following the sound card (RecordClockSteer).
  RecordClockSteer recordClock;
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
  // ONE WARNING PER STALL, not one a second. Cleared the moment a fresh
  // capture arrives, so a genuine stall still announces itself and a
  // legitimately motionless picture does not nag for the length of the take.
  bool recordStallWarned = false;
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
  std::map<int, std::uintptr_t> layerBridgePixelPointers;
  std::map<int, std::string> layerBridgeCueKeys;
  // DIAGNOSTIC ONLY -- not read by any draw path. Which deck-layers on this
  // output were actually drawn last frame, so renderDeckLayerIntoOutput's
  // early-return guards can log once on a TRANSITION to/from "nothing drawn"
  // instead of every frame it stays that way. Added chasing a live report
  // that SEEK/TAKE on one layer deck blanks the others on the same output;
  // absent from the map means "assume visible", so silence at startup
  // (nothing has drawn yet either way) does not log a false drop.
  std::map<int, bool> layerWasVisibleForDiagnostics;
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
  // A zero-copy (GPU) frame has no CPU pixels, so a transition drawn from one
  // drew nothing at all. Each is downloaded ONCE, keyed by deck and role
  // ("out"/"in"), and reused until the frame changes.
  std::map<std::string, std::pair<std::uintptr_t, DecodedFrame>> transitionCpuFrames;
  // Keep the last processed picture without running feedback a second time.
  // At a cue change it becomes the outgoing look; storage is bounded per deck.
  std::map<int, std::pair<std::string, DecodedFrame>> layerLookFrames;
  std::map<int, std::pair<std::string, DecodedFrame>> heldLookFrames;
  std::vector<std::uint8_t> layerBridgeScratchPixels;
  // The reduction each deck's bridge texture was last filled at (see
  // layerFxShrinkFor): a change makes a new, empty texture, which must be
  // filled again even when the frame itself has not changed.
  std::map<int, int> layerFxShrinkUsed;
  // PICTURE EFFECTS ON THE GPU (engine/gpu_effects), made for this output's
  // renderer the first time a layer wants them. Null where the renderer is
  // not one they run on, and then the CPU path runs, as it always did.
  deckboy::gpu::GpuEffects* gpuEffects = nullptr;
  bool gpuEffectsTried = false;
  // What a deck's effects left on the GPU, drawn again while its frame has
  // not changed. Present only while the GPU path is the one in use.
  std::map<int, SDL_Texture*> layerGpuFxTextures;
  // A source made on the card: its desk copy as it comes back (runGpuSource,
  // readGpuSourceDesk), handed to the deck's engine for the desk.
  std::vector<std::uint8_t> gpuSourceDesk;
  // What that result was of. A transition needs the outgoing look as PIXELS,
  // so when the cue changes it is read back once (layerLookFrames' job on the
  // CPU path, which keeps the pixels every frame).
  struct GpuLook {
    std::string key;
    std::uint64_t index = 0;
    int logicalWidth = 0;
    int logicalHeight = 0;
  };
  std::map<int, GpuLook> layerGpuLooks;
  // Which frame (index, cue key) the GPU effects last ran on from the NV12
  // picture, so an unchanged frame with a still stack is not run again.
  std::map<int, std::pair<std::uint64_t, std::string>> layerGpuFxRan;
  // An outgoing look still on the GPU: a copy of the last GPU result, drawn
  // by the transition as it is. heldLookFrames carries its descriptor (size,
  // index, key) with NO pixels; a style that needs pixels (portal, shatter,
  // clouds, ghastly) reads it back the first time it asks. Destroyed with the
  // descriptor or the renderer.
  std::map<int, SDL_Texture*> heldGpuLookTextures;
  // A decoded outgoing frame still on this output's device (a cue playing on
  // through its transition), copied on the card into a wrap of its own and
  // drawn from there -- downloading it every frame cost ~10 ms a frame.
  struct TransitionGpuWrap {
    SDL_Texture* texture = nullptr;
    void* texture2D = nullptr;   // owned reference (deckboy::libav::releaseD3D11Texture)
    int width = 0;
    int height = 0;
    std::uintptr_t stamp = 0;
  };
  std::map<int, TransitionGpuWrap> transitionGpuWraps;
  // Portal, shatter, clouds and ghastly drawn on the card (runGpuTransition),
  // by deck: how far the last frame was, to tell a new transition from the
  // next frame of this one, and a count of transitions, so a new one never
  // reuses the outgoing picture of the last. Released when it ends.
  struct GpuTransitionSlot {
    double lastProgress = 2.0;
    std::uint64_t generation = 0;
  };
  std::map<int, GpuTransitionSlot> gpuTransitionSlots;
  // The Portal transition's picture: the outgoing frame with the holes melted
  // through it, rebuilt every frame the transition runs.
  std::vector<std::uint8_t> portalTransitionPixels;
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
  std::string streamAudioPipePath;   // macOS/Linux: the FIFO ffmpeg reads sound from
#endif
  std::map<int, std::uint64_t> streamAudioReadSamplesByDeck;
  std::map<const void*, deckboy::audiofx::ProgramAudioState> programAudioStates;
  std::map<const void*, std::map<int, std::uint64_t>> inputAudioReadSamplesBySink;
  const void* meteredAudioSink = nullptr;
  double streamAudioSampleRemainder = 0.0;
  // Samples this sink is owed but could not take yet, because the decks had not
  // produced them at the moment the frame was written. Carried rather than
  // written as silence — see collectOutputAudioFrameSamples.
  std::uint64_t streamAudioOwedSamples = 0;
  // True while the decks feeding this sink are producing nothing. A deck that
  // starts up owes NOTHING for the silence written before it opened its mouth:
  // the audio did not start late, it started when it started, and carrying that
  // "debt" pushed the whole take's sound ahead of its picture.
  bool streamAudioDecksQuiet = true;
  // The decks this sink is mixing, captured when its read positions were
  // primed, so the tail flush at stop does not need the output index.
  std::vector<int> streamAudioDecks;
  std::map<int, std::uint64_t> ndiAudioReadSamplesByDeck;
  double ndiAudioSampleRemainder = 0.0;
  std::map<int, std::uint64_t> deckLinkAudioReadSamplesByDeck;
  double deckLinkAudioSampleRemainder = 0.0;
  std::map<int, std::uint64_t> st2110AudioReadSamplesByDeck;
  double st2110AudioSampleRemainder = 0.0;
  std::string streamSpec;
  // The frame rate a LIVE NETWORK stream was opened with, held for the life of
  // the connection. See ensureOutputStreamRunning: the rate is declared to the
  // encoder when it starts, and a stream that re-declares it has to reconnect.
  double streamLockedFps = 0.0;
  std::uint64_t webFramesQueued = 0;
  double webClockSeconds = 0.0;
  double webAudioClockSeconds = 0.0;
  Uint64 webClockUpdatedMs = 0;
  int webClockDeck = -1;
  std::uint64_t webClockSamples = 0;
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
  int streamFrameWidth = 0;
  int streamFrameHeight = 0;
  bool streamStartFailed = false;
  std::string streamFailureReason;   // why the last start failed, shown while retrying
  // A file sink has to FINALIZE — flush the muxer and write its trailer. A
  // network sink has nothing to finalize, so the two get different shutdown
  // budgets; see stopOutputStreamRuntime.
  bool streamToFile = false;
  Uint64 streamRestartBlockedUntilMs = 0;
  std::shared_ptr<OutputStreamWriterState> streamWriter;
  CapturedFrame latestCapturedFrame;
  // HDR RECORDING: the encoder was started for 16-bit PQ BT.2020 (rgb48le),
  // and this is its frame -- captured beside the BGRA one that NDI, DeckLink,
  // the scopes and everything else read, so none of them ever sees PQ bytes.
  bool streamHdr = false;
  int streamHdrWidth = 0, streamHdrHeight = 0;   // the raster the encoder was told
  CapturedFrame hdrRecordFrame;
  std::deque<CapturedFrame> delayFrames;
  Uint64 lastEgressCaptureAtMs = 0;
  // When the next egress capture is DUE (ms, fractional): a running deadline,
  // advanced by one capture interval each time, so a rate that does not
  // divide the loop's comes out right on average (see captureOutputFrameForEgress).
  double nextEgressCaptureDueMs = 0.0;
  Uint64 lastStreamCaptureSentAtMs = 0;
  // House overlay: the still laid over this output's picture. Cached against
  // the path it came from, so it is decoded once rather than every frame.
  // Outside the NDI guard deliberately -- the matte and overlay belong to every
  // output on every build, and a build without the NDI SDK still has them.
  SDL_Texture* overlayTexture = nullptr;
  std::string overlayTexturePath;

  // MASKS (app_masks.ipp). One texture per mask, size and use, uploaded from
  // the app's shared raster cache when its serial moves; and the scratch a
  // masked layer is drawn into before it is cut and laid on the compositor.
  struct MaskTexture {
    SDL_Texture* texture = nullptr;
    std::uint64_t serial = 0;
    Uint64 usedAtMs = 0;
  };
  std::map<std::string, MaskTexture> maskTextures;
  SDL_Texture* maskScratch = nullptr;
  int maskScratchW = 0;
  int maskScratchH = 0;
  // A slice whose source is one playlist draws that playlist here first.
  SDL_Texture* sliceScratch = nullptr;
  int sliceScratchW = 0;
  int sliceScratchH = 0;

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
  // KEY + FILL. A second card carries the matte, and the two buffers the
  // composite is split into live here rather than being allocated per frame:
  // at 1080p that would be sixteen megabytes of churn every frame.
  std::unique_ptr<deckboy::platform::video::DeckLinkOutput> deckLinkKeyOutput;
  std::vector<std::uint8_t> deckLinkFillBuffer;
  std::vector<std::uint8_t> deckLinkKeyBuffer;
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
  // The tap as the window presents it -- warped, cropped, rotated, blended --
  // rather than the flat composite: while the warp editor is open on this
  // output (see previewTapShowsWarp). Anything that needs the flat picture
  // checks this.
  bool previewTapWarped = false;
  // The tap through the asynchronous staging ring where the renderer has one,
  // so the output pass never waits on the GPU for the control window's
  // monitor. Unavailable is latched, as for egress, so a renderer with no
  // async path is asked once rather than every frame.
  void* previewTapReadback = nullptr;
  bool previewTapReadbackUnavailable = false;
  int previewTapReadbackMisses = 0;
  std::vector<std::uint8_t> previewTapScratch;  // BGRA32 as the ring hands it over
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
  // STUTTER, which an average hides: a 60fps output that drops one frame in
  // twenty still averages 57. A hitch is a present more than 1.5 frames after
  // the one before, and the frames it missed are counted too. Counted from the
  // output's start; a measurement diffs two readings. The worst gap is reset
  // only with the output.
  // A FRAME IS THE SCREEN'S FRAME (presentTargetHz: its refresh, else a set
  // rate). Judged against the rate it MEASURED, an output stuck at 56 on a
  // 60 Hz screen took every 17.9 ms gap as on time and reported no hitches.
  Uint64 lastPresentNs = 0;
  std::uint64_t presentsCounted = 0;
  std::uint64_t presentHitches = 0;
  std::uint64_t presentsMissed = 0;
  double presentTargetHz = 0.0;
  double worstPresentGapMs = 0.0;
  Uint64 lastMissNs = 0;
  Uint64 missWindowStartNs = 0;   // the operator is warned at 3 missed in 5 s
  int missedInWindow = 0;
  Uint64 lastMissWarningMs = 0;
  int lastWarnedMissed = 0;       // how bad it was when the operator was last told
  Uint64 firstPresentNs = 0;      // when this window first presented: the warm-up starts
  // WHOSE CLOCK ITS PRESENTS KEEP (presentPacedHz): the mean of the intervals
  // that were not hitches, over ten seconds. The screen's refresh when the
  // screen paces them; on Windows a COMPOSED window keeps the compositor's,
  // which need not be its screen's (platform/display_topology.hpp). The probe
  // says why, and is refreshed with each ten seconds.
  double pacedSumMs = 0.0;
  std::uint32_t pacedCount = 0;
  Uint64 pacedWindowStartNs = 0;
  double presentPacedHz = 0.0;
  deckboy::platform::PresentPathInfo presentPath;
  bool presentClockSuspect = false;   // the last ten seconds kept a foreign clock
  bool presentClockWarned = false;
  std::string presentClockLogged;     // the last summary written to the render log
  // Kept on top while full screen on a screen the desk is not on, so nothing
  // makes Windows compose it (serviceOutputOnTop). Raises counts the times
  // another program's window had to be put back under it.
  bool onTop = false;
  Uint64 onTopCheckedMs = 0;
  std::uint64_t onTopRaises = 0;
  Uint64 streamFpsSampleStartedAtMs = 0;
  std::uint64_t streamFpsPacketsAtSampleStart = 0;
  double streamFpsMeasured = 0.0;
  // The test card's cached halves (core/test_card.hpp): the canvas pattern at
  // the compositor's raster, and the 1-pixel resolution patch at this
  // screen's cell size. Rebuilt when either size changes.
  SDL_Texture* testCardCanvas = nullptr;
  int testCardCanvasW = 0, testCardCanvasH = 0;
  SDL_Texture* testCardPatch = nullptr;
  int testCardPatchCell = 0;
};

#endif  // DECKBOY_CORE_OUTPUT_RUNTIME_HPP
