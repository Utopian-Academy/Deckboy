// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// libav_decoder.hpp — In-process libav* decode pipelines (GPU decode rewrite).
//
// Replaces the per-deck ffmpeg video+audio subprocess pipes for file-backed
// Video/Audio cues (docs/GPU_DECODE_PLAN.md §7–§11). Live streams (SRT/NDI),
// stills, source capture, waveform, ffprobe and stream encode-out stay on the
// ffmpeg CLI — decode was only half its jobs.
//
//   VideoPipeline — demux + decode one video stream.
//     * Windows + D3D11 device supplied: d3d11va hardware decode on THAT
//       device (the program output renderer's), frames stay GPU-resident as
//       NV12 texture-array slices carried in DecodedFrame's gpu fields.
//       The output compositor GPU-copies the slice into an SDL_Texture
//       wrapped once via SDL_CreateTextureWithProperties — zero CPU touch.
//     * No device (preview/PiP engines, non-Windows) or RGBA wanted (CPU
//       effects path): hardware decode + av_hwframe_transfer_data to CPU,
//       or software decode, then swscale into the classic `pixels` layout.
//     * open() primes the first frame; if hardware decode fails it retries
//       in software internally, and if the file can't produce one good frame
//       open() fails — the caller falls back to the CLI pipe path (also the
//       break-glass path for rotated-metadata files, which libav does not
//       autorotate).
//
//   AudioPipeline — demux + decode one audio stream to interleaved s16
//     stereo 48 kHz, matching the old `ffmpeg -f s16le -ac 2 -ar 48000`
//     pipe byte-for-byte so MediaEngine's gain/tap/queue logic and the
//     audio-master A/V clock are untouched. Playback speed uses an
//     avfilter atempo chain (same semantics as the CLI args).
//
// Both pipelines install an AVIO interrupt callback wired to requestStop()
// so a decode thread blocked on I/O (dead network share) can be unblocked
// during stopDecoderThreads() — the in-process replacement for killing the
// child process to break a pipe read.
//
// Compiled only when DECKBOY_INPROC_DECODE=1 (CMake option, default ON).
// This header is libav-free (pimpl) so it is safe to include anywhere.
// ============================================================================

#pragma once

#if DECKBOY_INPROC_DECODE

#include "core/sdl_compat.hpp"
#include "core/types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace deckboy::libav {

// Parameters for VideoPipeline::open(). targetWidth/Height and format apply
// to CPU-output mode only (mirrors the old scale filter + -pix_fmt choice);
// zero-copy frames are always source-sized NV12 — the compositor scales on
// the GPU at blit time.
struct VideoOpenParams {
  std::string path;
  double startSeconds = 0.0;           // in-point seek (replaces -ss)
  int targetWidth = 0;                 // CPU mode scale target (0 = source)
  int targetHeight = 0;
  FramePixelFormat format = FramePixelFormat::NV12;
  void* d3dDevice = nullptr;           // ID3D11Device* → zero-copy; null → CPU output
  // Datamosh: drop every keyframe after the first so P-frames apply their
  // motion onto a stale reference and the picture smears. Forces SOFTWARE
  // decode regardless of d3dDevice - hardware decoders fed P-frames with no
  // valid reference are driver-dependent (may smear, may error, may reset),
  // and the error run would trip kMaxConsecutiveErrors and kill the decode.
  bool datamosh = false;
};

class VideoPipeline {
 public:
  VideoPipeline();
  ~VideoPipeline();
  VideoPipeline(const VideoPipeline&) = delete;
  VideoPipeline& operator=(const VideoPipeline&) = delete;

  // Open, seek, and prime the first frame. Returns false when this file
  // should go to the CLI fallback (no video stream, rotation metadata,
  // no decodable frame). Safe to call once per instance.
  bool open(const VideoOpenParams& params);

  // Produce the next decoded frame (display order). Returns false on EOF or
  // after a run of unrecoverable decode errors. Frame `index` is left for
  // the caller to assign.
  bool nextFrame(DecodedFrame& out);

  bool zeroCopyActive() const;         // frames carry GPU payloads
  void* device() const;                // ID3D11Device* in use (null in CPU/sw mode)

  // Unblock any av_read_frame stuck in I/O; nextFrame() then returns false.
  // Callable from another thread, as is setDatamosh below; everything else on
  // this class belongs to the decode thread.
  void requestStop();

  // Toggle keyframe dropping on a already-open pipeline. Turning it OFF does
  // not clean the picture until the next keyframe arrives, which is why the
  // app swaps clips instead of relying on this mid-cue.
  void setDatamosh(bool enabled);

  void close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------------------
// HAP: demux only, never decode.
//
// HAP frames are already GPU-ready block data, so the whole point is to pull
// the packets out of the container and hand them to hap::decodeFrame -- NOT to
// run them through ffmpeg's hap decoder, which unpacks DXT to RGB on the CPU
// and is slower than H.264 for several times the file size.
// ---------------------------------------------------------------------------
struct HapProbeResult {
  bool isHap = false;
  int width = 0;
  int height = 0;
  double fps = 0.0;
  int frames = 0;              // packets successfully decoded to blocks
  std::string textureFormat;   // e.g. RGB_DXT1
  std::size_t blockBytes = 0;  // size of the last decoded frame
  // First frame expanded to RGBA, for verifying the CPU expansion against a
  // reference decoder. Empty unless the caller asked for it.
  std::vector<std::uint8_t> rgbaFirstFrame;
};

// Open `path`, confirm the video stream is HAP, and decode every frame to
// blocks via the vendored decoder. Used by --hap-probe and the smoke suite to
// prove the demux+decode path without needing a window.
bool probeHapFile(const std::string& path, HapProbeResult& out, std::string& error);

struct AudioOpenParams {
  std::string path;
  double startSeconds = 0.0;
  double speed = 1.0;                  // atempo chain when != 1
};

class AudioPipeline {
 public:
  AudioPipeline();
  ~AudioPipeline();
  AudioPipeline(const AudioPipeline&) = delete;
  AudioPipeline& operator=(const AudioPipeline&) = delete;

  // Returns false when the file has no decodable audio stream — the caller
  // simply runs without audio (same as a failed CLI spawn today).
  bool open(const AudioOpenParams& params);

  // Fill up to maxSamples int16 values (interleaved stereo @48 kHz).
  // Returns the count written, 0 on EOF, <0 on unrecoverable error.
  int read(std::int16_t* out, int maxSamples);

  void requestStop();                  // see VideoPipeline::requestStop
  void close();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// -- D3D11 interop helpers for the zero-copy composite path ------------------
// All return null/false on non-Windows builds or software renderers.

// The ID3D11Device* backing an SDL renderer (SDL_PROP_RENDERER_D3D11_DEVICE_POINTER).
void* rendererD3D11Device(SDL_Renderer* renderer);

// Create a persistent NV12 ID3D11Texture2D on the renderer's device and wrap
// it as an SDL_Texture (SDL_CreateTextureWithProperties). *outTexture2D
// receives the raw texture, released with releaseD3D11Texture AFTER the
// SDL_Texture is destroyed. Dimensions are rounded down to even.
SDL_Texture* createWrappedNV12Texture(SDL_Renderer* renderer, int w, int h,
                                      void** outTexture2D);
void releaseD3D11Texture(void* texture2D);

// GPU→GPU copy of a zero-copy frame's texture-array slice into a texture
// created by createWrappedNV12Texture on the SAME device.
bool copyGpuFrameToTexture(const DecodedFrame& frame, void* dstTexture2D);

// CPU download of a zero-copy frame (av_hwframe_transfer_data) into a packed
// NV12 DecodedFrame — the fallback for consumers on a different device
// (secondary outputs) and the throttled control-window preview.
bool downloadGpuFrameNV12(const DecodedFrame& frame, DecodedFrame& out);

} // namespace deckboy::libav

#endif // DECKBOY_INPROC_DECODE
