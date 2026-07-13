// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// libav_decoder.cpp — In-process libav* decode pipelines. See the header for
// the architecture; docs/GPU_DECODE_PLAN.md for the plan this implements.
// ============================================================================

#if DECKBOY_INPROC_DECODE

#include "engine/libav_decoder.hpp"

#include "core/constants.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

// d3d11.h must be included as C++ (it defines operator overloads) BEFORE the
// extern "C" libav block — hwcontext_d3d11va.h includes it and would drag it
// into C linkage otherwise.
#ifdef _WIN32
#include <d3d10_1.h>  // ID3D10Multithread (the interface ffmpeg itself uses)
#include <d3d11.h>
#include <d3d11_4.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#ifdef _WIN32
#include <libavutil/hwcontext_d3d11va.h>
#endif
}

namespace deckboy::libav {

namespace {

// Give up after this many consecutive failed packets/frames — a corrupt file
// must degrade to EOF (deck reracks), never spin or crash.
constexpr int kMaxConsecutiveErrors = 40;
// Packet budget for priming the first frame in open(). Generous enough for
// long GOPs + hw decoder latency, small enough to fail fast on garbage.
constexpr int kPrimePacketBudget = 600;

double streamTimeSeconds(int64_t ts, AVRational timeBase) {
  if (ts == AV_NOPTS_VALUE) {
    return -1.0;
  }
  return static_cast<double>(ts) * av_q2d(timeBase);
}

int decodeThreadCount(bool hwDecode) {
  int cores = SDL_GetNumLogicalCPUCores();
  if (hwDecode) {
    // The GPU does the heavy lifting; extra threads only fight the render
    // loop for the few cores the Pocket has.
    return 2;
  }
  return std::clamp(cores / 2, 2, 8);
}

#ifdef _WIN32
// Enable D3D11 multithread protection on a device we share with SDL's
// renderer: the decode thread drives the video/immediate context while the
// main thread renders and copies on the same device, and the D3D runtime
// must serialize them. Without this the shared-device path crashes or
// deadlocks at random — so adoption REFUSES devices where it can't be
// enabled (the pipeline then decodes on its own device, CPU output).
// Note: requires SDL_HINT_RENDER_DIRECT3D_THREADSAFE=1 at app init — SDL
// otherwise creates its device D3D11_CREATE_DEVICE_SINGLETHREADED and the
// multithread interfaces are unavailable.
bool enableMultithreadProtection(ID3D11Device* device) {
  // ffmpeg's own device_create path uses the D3D10 interface QI'd from the
  // device; the 11.4 interface off the immediate context is the newer route.
  // Accept whichever the runtime provides.
  ID3D10Multithread* mt10 = nullptr;
  if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D10Multithread),
                                       reinterpret_cast<void**>(&mt10))) && mt10) {
    mt10->SetMultithreadProtected(TRUE);
    mt10->Release();
    return true;
  }
  bool enabled = false;
  ID3D11DeviceContext* immediate = nullptr;
  device->GetImmediateContext(&immediate);
  if (immediate) {
    ID3D11Multithread* mt11 = nullptr;
    if (SUCCEEDED(immediate->QueryInterface(__uuidof(ID3D11Multithread),
                                            reinterpret_cast<void**>(&mt11))) && mt11) {
      mt11->SetMultithreadProtected(TRUE);
      mt11->Release();
      enabled = true;
    }
    immediate->Release();
  }
  return enabled;
}

// Wrap an existing ID3D11Device (the SDL output renderer's) in an ffmpeg
// hw-device context for zero-copy decode.
AVBufferRef* adoptD3D11Device(void* devicePtr) {
  if (!devicePtr) {
    return nullptr;
  }
  auto* device = reinterpret_cast<ID3D11Device*>(devicePtr);
  if (!enableMultithreadProtection(device)) {
    return nullptr;  // sharing would be unsafe — decode on our own device
  }
  AVBufferRef* ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
  if (!ref) {
    return nullptr;
  }
  auto* hwctx = reinterpret_cast<AVHWDeviceContext*>(ref->data);
  auto* d3dctx = reinterpret_cast<AVD3D11VADeviceContext*>(hwctx->hwctx);
  device->AddRef();  // the hw ctx owns one reference now
  d3dctx->device = device;
  if (av_hwdevice_ctx_init(ref) < 0) {
    av_buffer_unref(&ref);
    return nullptr;
  }
  return ref;
}
#endif

// Rotation side data means the CLI (which autorotates) and libav (which does
// not) would show different pictures. Those files stay on the CLI path.
bool streamHasRotation(const AVStream* stream) {
  const AVPacketSideData* sd = av_packet_side_data_get(
    stream->codecpar->coded_side_data, stream->codecpar->nb_coded_side_data,
    AV_PKT_DATA_DISPLAYMATRIX);
  if (!sd || sd->size < 9 * sizeof(int32_t)) {
    return false;
  }
  double rotation = av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
  return std::isfinite(rotation) && std::abs(rotation) > 1.0;
}

bool codecSupportsD3D11(const AVCodec* codec) {
  for (int i = 0;; ++i) {
    const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
    if (!config) {
      return false;
    }
    if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
        config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
      return true;
    }
  }
}

AVPixelFormat pickDecodeFormat(AVCodecContext* ctx, const AVPixelFormat* formats) {
  bool wantHw = ctx->opaque != nullptr;  // opaque flags "hw requested" (see below)
  if (wantHw) {
    for (const AVPixelFormat* p = formats; *p != AV_PIX_FMT_NONE; ++p) {
      if (*p == AV_PIX_FMT_D3D11) {
        return *p;
      }
    }
  }
  return avcodec_default_get_format(ctx, formats);
}

struct SharedAvFrameDeleter {
  void operator()(void* p) const {
    auto* frame = reinterpret_cast<AVFrame*>(p);
    av_frame_free(&frame);
  }
};

} // namespace

// ============================================================================
// VideoPipeline
// ============================================================================

struct VideoPipeline::Impl {
  VideoOpenParams params;
  std::atomic<bool> interrupt {false};

  AVFormatContext* fmtCtx = nullptr;
  AVCodecContext* codecCtx = nullptr;
  AVBufferRef* hwDevice = nullptr;
  SwsContext* swsCtx = nullptr;
  AVPacket* packet = nullptr;
  AVFrame* frame = nullptr;
  AVFrame* swFrame = nullptr;

  int streamIndex = -1;
  AVRational timeBase {1, 1};
  bool zeroCopy = false;      // hw decode on the caller-supplied device
  bool drainSent = false;
  bool finished = false;
  bool havePending = false;
  DecodedFrame pendingFrame;  // primed by open()
  double dropBeforeSeconds = -1.0;  // seek target: drop earlier frames
  int consecutiveErrors = 0;

  static int interruptCb(void* opaque) {
    return reinterpret_cast<Impl*>(opaque)->interrupt.load() ? 1 : 0;
  }

  void closeAll() {
    if (swsCtx) { sws_freeContext(swsCtx); swsCtx = nullptr; }
    if (swFrame) { av_frame_free(&swFrame); }
    if (frame) { av_frame_free(&frame); }
    if (packet) { av_packet_free(&packet); }
    if (codecCtx) { avcodec_free_context(&codecCtx); }
    if (fmtCtx) { avformat_close_input(&fmtCtx); }
    if (hwDevice) { av_buffer_unref(&hwDevice); }
    streamIndex = -1;
    zeroCopy = false;
    drainSent = false;
    finished = false;
    havePending = false;
    pendingFrame = DecodedFrame{};
    consecutiveErrors = 0;
  }

  bool openInternal(bool tryHw) {
    closeAll();

    fmtCtx = avformat_alloc_context();
    if (!fmtCtx) {
      return false;
    }
    fmtCtx->interrupt_callback.callback = &Impl::interruptCb;
    fmtCtx->interrupt_callback.opaque = this;
    if (avformat_open_input(&fmtCtx, params.path.c_str(), nullptr, nullptr) < 0) {
      fmtCtx = nullptr;  // avformat_open_input frees on failure
      return false;
    }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
      return false;
    }
    const AVCodec* codec = nullptr;
    streamIndex = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (streamIndex < 0 || !codec) {
      return false;
    }
    AVStream* stream = fmtCtx->streams[streamIndex];
    if (streamHasRotation(stream)) {
      return false;  // CLI autorotates; we don't — fall back
    }
    timeBase = stream->time_base;

    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx ||
        avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0) {
      return false;
    }

    bool useHw = tryHw && codecSupportsD3D11(codec);
#ifdef _WIN32
    if (useHw) {
      if (params.d3dDevice && params.format == FramePixelFormat::NV12) {
        hwDevice = adoptD3D11Device(params.d3dDevice);
        zeroCopy = hwDevice != nullptr;
      }
      if (!hwDevice) {
        av_hwdevice_ctx_create(&hwDevice, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
        zeroCopy = false;
      }
      useHw = hwDevice != nullptr;
    }
#else
    useHw = false;
#endif
    if (useHw) {
      codecCtx->hw_device_ctx = av_buffer_ref(hwDevice);
      // Surfaces the decoder pool must cover beyond its own reorder needs:
      // MediaEngine's frame queue plus in-flight display/copy frames.
      codecCtx->extra_hw_frames = kMaxVideoFrames + 6;
      codecCtx->opaque = this;  // flags "hw requested" for pickDecodeFormat
    } else {
      zeroCopy = false;
      codecCtx->opaque = nullptr;
    }
    codecCtx->get_format = &pickDecodeFormat;
    codecCtx->thread_count = decodeThreadCount(useHw);
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
      return false;
    }

    packet = av_packet_alloc();
    frame = av_frame_alloc();
    swFrame = av_frame_alloc();
    if (!packet || !frame || !swFrame) {
      return false;
    }

    if (params.startSeconds > 0.001) {
      int64_t ts = static_cast<int64_t>(params.startSeconds / av_q2d(timeBase));
      if (av_seek_frame(fmtCtx, streamIndex, ts, AVSEEK_FLAG_BACKWARD) >= 0) {
        dropBeforeSeconds = params.startSeconds;
      }
    }

    // Prime the first frame so open() proves the whole pipeline works —
    // validation-before-decode for corrupt files, and the hw→sw retry gate.
    for (int budget = 0; budget < kPrimePacketBudget; ++budget) {
      if (interrupt.load()) {
        return false;
      }
      int produced = pumpOnce(pendingFrame);
      if (produced > 0) {
        havePending = true;
        return true;
      }
      if (produced < 0) {
        return false;  // EOF or fatal before any frame
      }
    }
    return false;
  }

  // Drive demux+decode until one frame is produced.
  // Returns 1 = frame written to `out`, 0 = keep pumping, -1 = EOF/fatal.
  int pumpOnce(DecodedFrame& out) {
    if (finished) {
      return -1;
    }
    int recv = avcodec_receive_frame(codecCtx, frame);
    if (recv == 0) {
      consecutiveErrors = 0;
      bool converted = convertFrame(out);
      av_frame_unref(frame);
      return converted ? 1 : 0;  // dropped pre-seek frames keep pumping
    }
    if (recv == AVERROR_EOF) {
      finished = true;
      return -1;
    }
    if (recv != AVERROR(EAGAIN)) {
      if (++consecutiveErrors > kMaxConsecutiveErrors) {
        finished = true;
        return -1;
      }
      return 0;
    }

    // Decoder wants input.
    if (drainSent) {
      return 0;
    }
    while (true) {
      if (interrupt.load()) {
        finished = true;
        return -1;
      }
      int readErr = av_read_frame(fmtCtx, packet);
      if (readErr == AVERROR_EOF) {
        avcodec_send_packet(codecCtx, nullptr);
        drainSent = true;
        return 0;
      }
      if (readErr < 0) {
        if (++consecutiveErrors > kMaxConsecutiveErrors) {
          finished = true;
          return -1;
        }
        return 0;
      }
      if (packet->stream_index != streamIndex) {
        av_packet_unref(packet);
        continue;
      }
      int sendErr = avcodec_send_packet(codecCtx, packet);
      av_packet_unref(packet);
      if (sendErr < 0 && sendErr != AVERROR(EAGAIN)) {
        if (++consecutiveErrors > kMaxConsecutiveErrors) {
          finished = true;
          return -1;
        }
      }
      return 0;
    }
  }

  // Fill `out` from the decoded `frame`. Returns false when the frame is
  // dropped (pre-seek-target) — the caller keeps pumping.
  bool convertFrame(DecodedFrame& out) {
    if (dropBeforeSeconds >= 0.0) {
      double pts = streamTimeSeconds(frame->best_effort_timestamp, timeBase);
      if (pts >= 0.0) {
        double durSec = frame->duration > 0
          ? static_cast<double>(frame->duration) * av_q2d(timeBase)
          : 0.0;
        if (pts + durSec <= dropBeforeSeconds + 1e-6) {
          return false;
        }
      }
      dropBeforeSeconds = -1.0;  // reached the target — stop checking
    }

    // Real presentation time (display order) so telecined / variable-rate
    // video schedules by its actual timestamps rather than a constant-fps
    // counter (which drifts against the audio clock on 3:2-pulldown DVD MPEG-2).
    const double ptsSeconds = streamTimeSeconds(frame->best_effort_timestamp, timeBase);

#ifdef _WIN32
    // Zero-copy only works when the decoded surface is 8-bit NV12 — that's the
    // one layout the compositor's NV12 texture path can read. 10-bit content
    // (HEVC Main 10, VP9/AV1 Profile 2, ...) decodes to P010 surfaces; handing
    // those to the NV12 path renders a flat green frame. Detect the surface's
    // software format and only zero-copy true NV12; everything else falls
    // through to the CPU transfer + swscale below, which converts P010 (and
    // any other format) down to NV12/RGBA correctly.
    if (frame->format == AV_PIX_FMT_D3D11 && zeroCopy && frame->hw_frames_ctx) {
      auto* framesCtx = reinterpret_cast<AVHWFramesContext*>(frame->hw_frames_ctx->data);
      if (framesCtx && framesCtx->sw_format == AV_PIX_FMT_NV12) {
        AVFrame* ref = av_frame_clone(frame);
        if (!ref) {
          return false;
        }
        out = DecodedFrame{};
        out.width = frame->width & ~1;
        out.height = frame->height & ~1;
        out.format = FramePixelFormat::NV12;
        out.presentationSeconds = ptsSeconds;
        out.gpuFrameRef = std::shared_ptr<void>(ref, SharedAvFrameDeleter{});
        out.gpuTexture = ref->data[0];
        out.gpuSubresource = static_cast<int>(reinterpret_cast<intptr_t>(ref->data[1]));
        out.gpuDevice = params.d3dDevice;
        return out.width > 0 && out.height > 0;
      }
    }
#endif

    // CPU output. Hardware frames transfer down first; then swscale (or a
    // straight plane copy) into the packed layout the engine expects.
    AVFrame* src = frame;
    if (frame->hw_frames_ctx) {
      av_frame_unref(swFrame);
      if (av_hwframe_transfer_data(swFrame, frame, 0) < 0) {
        return false;
      }
      src = swFrame;
    }
    int dstW = params.targetWidth > 0 ? params.targetWidth : src->width;
    int dstH = params.targetHeight > 0 ? params.targetHeight : src->height;
    dstW &= ~1;
    dstH &= ~1;
    const std::size_t bytes = frameBufferSize(params.format, dstW, dstH);
    if (bytes == 0) {
      return false;
    }
    out = DecodedFrame{};
    out.width = dstW;
    out.height = dstH;
    out.format = params.format;
    out.presentationSeconds = ptsSeconds;
    out.pixels.resize(bytes);

    const auto srcFormat = static_cast<AVPixelFormat>(src->format);
    if (params.format == FramePixelFormat::NV12 && srcFormat == AV_PIX_FMT_NV12 &&
        src->width == dstW && src->height == dstH) {
      // Fast path: same-size NV12 — the skip-the-no-op-scale win.
      std::uint8_t* dstY = out.pixels.data();
      std::uint8_t* dstUV = dstY + static_cast<std::size_t>(dstW) * dstH;
      for (int y = 0; y < dstH; ++y) {
        std::memcpy(dstY + static_cast<std::size_t>(y) * dstW,
                    src->data[0] + static_cast<std::size_t>(y) * src->linesize[0], dstW);
      }
      for (int y = 0; y < dstH / 2; ++y) {
        std::memcpy(dstUV + static_cast<std::size_t>(y) * dstW,
                    src->data[1] + static_cast<std::size_t>(y) * src->linesize[1], dstW);
      }
      return true;
    }

    const AVPixelFormat dstFormat =
      params.format == FramePixelFormat::NV12 ? AV_PIX_FMT_NV12 : AV_PIX_FMT_RGBA;
    swsCtx = sws_getCachedContext(swsCtx, src->width, src->height, srcFormat,
                                  dstW, dstH, dstFormat, SWS_FAST_BILINEAR,
                                  nullptr, nullptr, nullptr);
    if (!swsCtx) {
      return false;
    }
    std::uint8_t* dstData[4] = {out.pixels.data(), nullptr, nullptr, nullptr};
    int dstLinesize[4] = {0, 0, 0, 0};
    if (params.format == FramePixelFormat::NV12) {
      dstData[1] = out.pixels.data() + static_cast<std::size_t>(dstW) * dstH;
      dstLinesize[0] = dstW;
      dstLinesize[1] = dstW;
    } else {
      dstLinesize[0] = dstW * 4;
    }
    return sws_scale(swsCtx, src->data, src->linesize, 0, src->height,
                     dstData, dstLinesize) == dstH;
  }
};

VideoPipeline::VideoPipeline() : impl_(std::make_unique<Impl>()) {}
VideoPipeline::~VideoPipeline() { close(); }

bool VideoPipeline::open(const VideoOpenParams& params) {
  impl_->params = params;
  if (impl_->openInternal(true)) {
    return true;
  }
  if (impl_->interrupt.load()) {
    return false;
  }
  return impl_->openInternal(false);  // hw failed — retry pure software
}

bool VideoPipeline::nextFrame(DecodedFrame& out) {
  if (impl_->havePending) {
    out = std::move(impl_->pendingFrame);
    impl_->pendingFrame = DecodedFrame{};
    impl_->havePending = false;
    return true;
  }
  while (!impl_->interrupt.load()) {
    int produced = impl_->pumpOnce(out);
    if (produced > 0) {
      return true;
    }
    if (produced < 0) {
      return false;
    }
  }
  return false;
}

bool VideoPipeline::zeroCopyActive() const { return impl_->zeroCopy; }

void* VideoPipeline::device() const {
  return impl_->zeroCopy ? impl_->params.d3dDevice : nullptr;
}

void VideoPipeline::requestStop() { impl_->interrupt.store(true); }

void VideoPipeline::close() { impl_->closeAll(); }

// ============================================================================
// AudioPipeline
// ============================================================================

struct AudioPipeline::Impl {
  AudioOpenParams params;
  std::atomic<bool> interrupt {false};

  AVFormatContext* fmtCtx = nullptr;
  AVCodecContext* codecCtx = nullptr;
  AVPacket* packet = nullptr;
  AVFrame* frame = nullptr;
  AVFrame* filtFrame = nullptr;

  SwrContext* swrCtx = nullptr;          // speed == 1 path
  AVFilterGraph* filterGraph = nullptr;  // speed != 1 path (atempo chain)
  AVFilterContext* filterSrc = nullptr;
  AVFilterContext* filterSink = nullptr;
  bool useFilter = false;

  int streamIndex = -1;
  AVRational timeBase {1, 1};
  bool drainSent = false;
  bool finished = false;
  int consecutiveErrors = 0;

  // Sample-accurate in-point trim, applied in the 48 kHz output domain.
  bool trimPending = false;
  std::int64_t skipOutValues = 0;   // int16 values (stereo frames × 2) to drop

  std::vector<std::int16_t> staged;  // converted output awaiting read()
  std::size_t stagedOffset = 0;

  static int interruptCb(void* opaque) {
    return reinterpret_cast<Impl*>(opaque)->interrupt.load() ? 1 : 0;
  }

  void closeAll() {
    if (filterGraph) { avfilter_graph_free(&filterGraph); filterSrc = filterSink = nullptr; }
    if (swrCtx) { swr_free(&swrCtx); }
    if (filtFrame) { av_frame_free(&filtFrame); }
    if (frame) { av_frame_free(&frame); }
    if (packet) { av_packet_free(&packet); }
    if (codecCtx) { avcodec_free_context(&codecCtx); }
    if (fmtCtx) { avformat_close_input(&fmtCtx); }
    streamIndex = -1;
    drainSent = false;
    finished = false;
    useFilter = false;
    trimPending = false;
    skipOutValues = 0;
    staged.clear();
    stagedOffset = 0;
    consecutiveErrors = 0;
  }

  bool open() {
    closeAll();
    fmtCtx = avformat_alloc_context();
    if (!fmtCtx) {
      return false;
    }
    fmtCtx->interrupt_callback.callback = &Impl::interruptCb;
    fmtCtx->interrupt_callback.opaque = this;
    if (avformat_open_input(&fmtCtx, params.path.c_str(), nullptr, nullptr) < 0) {
      fmtCtx = nullptr;
      return false;
    }
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
      return false;
    }
    const AVCodec* codec = nullptr;
    streamIndex = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (streamIndex < 0 || !codec) {
      return false;
    }
    timeBase = fmtCtx->streams[streamIndex]->time_base;
    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx ||
        avcodec_parameters_to_context(codecCtx, fmtCtx->streams[streamIndex]->codecpar) < 0) {
      return false;
    }
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
      return false;
    }
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    filtFrame = av_frame_alloc();
    if (!packet || !frame || !filtFrame) {
      return false;
    }
    if (params.startSeconds > 0.001) {
      int64_t ts = static_cast<int64_t>(params.startSeconds / av_q2d(timeBase));
      if (av_seek_frame(fmtCtx, streamIndex, ts, AVSEEK_FLAG_BACKWARD) >= 0) {
        trimPending = true;
      }
    }
    useFilter = std::abs(params.speed - 1.0) > 0.01;
    // swr / filter graph are initialized lazily from the first decoded frame,
    // whose sample format/rate/layout are authoritative.
    return true;
  }

  // Mirror the CLI's atempo chain construction: atempo only accepts
  // [0.5, 2.0], so factor extreme speeds into a chain.
  static std::string buildAtempoChain(double speed) {
    std::string chain;
    auto append = [&chain](const std::string& part) {
      if (!chain.empty()) chain += ",";
      chain += part;
    };
    double remaining = speed;
    while (remaining < 0.5 - 0.001) {
      append("atempo=0.5");
      remaining /= 0.5;
    }
    while (remaining > 2.0 + 0.001) {
      append("atempo=2.0");
      remaining /= 2.0;
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "atempo=%.4f", remaining);
    append(buf);
    return chain;
  }

  bool initFilterGraph(const AVFrame* first) {
    filterGraph = avfilter_graph_alloc();
    if (!filterGraph) {
      return false;
    }
    char layout[128] = {0};
    av_channel_layout_describe(&first->ch_layout, layout, sizeof(layout));
    char srcArgs[256];
    std::snprintf(srcArgs, sizeof(srcArgs),
                  "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                  timeBase.num, timeBase.den, first->sample_rate,
                  av_get_sample_fmt_name(static_cast<AVSampleFormat>(first->format)),
                  layout);
    if (avfilter_graph_create_filter(&filterSrc, avfilter_get_by_name("abuffer"),
                                     "in", srcArgs, nullptr, filterGraph) < 0 ||
        avfilter_graph_create_filter(&filterSink, avfilter_get_by_name("abuffersink"),
                                     "out", nullptr, nullptr, filterGraph) < 0) {
      return false;
    }
    // aformat pins the sink output to the exact stream format the engine
    // expects, so no sink option plumbing is needed.
    std::string chain = buildAtempoChain(params.speed) +
      ",aformat=sample_fmts=s16:sample_rates=48000:channel_layouts=stereo";
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs = avfilter_inout_alloc();
    if (!outputs || !inputs) {
      avfilter_inout_free(&outputs);
      avfilter_inout_free(&inputs);
      return false;
    }
    outputs->name = av_strdup("in");
    outputs->filter_ctx = filterSrc;
    outputs->pad_idx = 0;
    outputs->next = nullptr;
    inputs->name = av_strdup("out");
    inputs->filter_ctx = filterSink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;
    int err = avfilter_graph_parse_ptr(filterGraph, chain.c_str(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);
    if (err < 0) {
      return false;
    }
    return avfilter_graph_config(filterGraph, nullptr) >= 0;
  }

  bool initSwr(const AVFrame* first) {
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&swrCtx, &outLayout, AV_SAMPLE_FMT_S16, 48000,
                            &first->ch_layout,
                            static_cast<AVSampleFormat>(first->format),
                            first->sample_rate, 0, nullptr) < 0) {
      return false;
    }
    return swr_init(swrCtx) >= 0;
  }

  void stageSamples(const std::int16_t* data, std::size_t count) {
    if (skipOutValues > 0) {
      std::size_t skip = std::min<std::size_t>(count, static_cast<std::size_t>(skipOutValues));
      skipOutValues -= static_cast<std::int64_t>(skip);
      data += skip;
      count -= skip;
    }
    if (count > 0) {
      staged.insert(staged.end(), data, data + count);
    }
  }

  bool convertAndStage(AVFrame* input) {  // input == nullptr flushes swr/filter
    if (input && trimPending) {
      // First frame after the seek: figure out how much pre-in-point audio
      // the keyframe seek gave us and drop it in the output domain, so the
      // stream starts exactly at startSeconds — like the CLI's -ss.
      double pts = streamTimeSeconds(input->best_effort_timestamp, timeBase);
      if (pts >= 0.0) {
        double frameEnd = pts + static_cast<double>(input->nb_samples) /
                                std::max(1, input->sample_rate);
        if (frameEnd <= params.startSeconds + 1e-9) {
          return true;  // wholly before the in-point — drop
        }
        double lead = std::max(0.0, params.startSeconds - pts);
        if (useFilter) {
          lead /= std::max(0.01, params.speed);  // atempo rescales time
        }
        skipOutValues = static_cast<std::int64_t>(std::llround(lead * 48000.0)) * 2;
      }
      trimPending = false;
    }

    if (useFilter) {
      if (input && !filterGraph && !initFilterGraph(input)) {
        return false;
      }
      if (!filterGraph) {
        return true;  // flush before any frame — nothing to do
      }
      if (av_buffersrc_add_frame(filterSrc, input) < 0) {
        return false;
      }
      while (true) {
        int err = av_buffersink_get_frame(filterSink, filtFrame);
        if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
          break;
        }
        if (err < 0) {
          return false;
        }
        stageSamples(reinterpret_cast<const std::int16_t*>(filtFrame->data[0]),
                     static_cast<std::size_t>(filtFrame->nb_samples) * 2);
        av_frame_unref(filtFrame);
      }
      return true;
    }

    if (input && !swrCtx && !initSwr(input)) {
      return false;
    }
    if (!swrCtx) {
      return true;
    }
    int inCount = input ? input->nb_samples : 0;
    int64_t delay = swr_get_delay(swrCtx, input ? input->sample_rate : 48000);
    int maxOut = static_cast<int>(av_rescale_rnd(delay + inCount, 48000,
                                                 input ? input->sample_rate : 48000,
                                                 AV_ROUND_UP)) + 64;
    std::vector<std::int16_t> buf(static_cast<std::size_t>(maxOut) * 2);
    std::uint8_t* outPlanes[1] = {reinterpret_cast<std::uint8_t*>(buf.data())};
    int converted = swr_convert(swrCtx, outPlanes, maxOut,
                                input ? const_cast<const std::uint8_t**>(input->extended_data)
                                      : nullptr,
                                inCount);
    if (converted < 0) {
      return false;
    }
    stageSamples(buf.data(), static_cast<std::size_t>(converted) * 2);
    return true;
  }

  // Pump one packet through decode+convert. Returns false when the stream is
  // exhausted (everything flushed into `staged`).
  bool pump() {
    if (finished) {
      return false;
    }
    while (true) {
      int recv = avcodec_receive_frame(codecCtx, frame);
      if (recv == 0) {
        consecutiveErrors = 0;
        bool ok = convertAndStage(frame);
        av_frame_unref(frame);
        if (!ok && ++consecutiveErrors > kMaxConsecutiveErrors) {
          finished = true;
          return false;
        }
        return true;
      }
      if (recv == AVERROR_EOF) {
        convertAndStage(nullptr);  // flush resampler/filter tail
        finished = true;
        return false;
      }
      if (recv != AVERROR(EAGAIN)) {
        if (++consecutiveErrors > kMaxConsecutiveErrors) {
          finished = true;
          return false;
        }
        return true;
      }
      if (drainSent) {
        return true;
      }
      while (true) {
        if (interrupt.load()) {
          finished = true;
          return false;
        }
        int readErr = av_read_frame(fmtCtx, packet);
        if (readErr == AVERROR_EOF) {
          avcodec_send_packet(codecCtx, nullptr);
          drainSent = true;
          break;
        }
        if (readErr < 0) {
          if (++consecutiveErrors > kMaxConsecutiveErrors) {
            finished = true;
            return false;
          }
          return true;
        }
        if (packet->stream_index != streamIndex) {
          av_packet_unref(packet);
          continue;
        }
        int sendErr = avcodec_send_packet(codecCtx, packet);
        av_packet_unref(packet);
        if (sendErr < 0 && sendErr != AVERROR(EAGAIN) &&
            ++consecutiveErrors > kMaxConsecutiveErrors) {
          finished = true;
          return false;
        }
        break;
      }
    }
  }
};

AudioPipeline::AudioPipeline() : impl_(std::make_unique<Impl>()) {}
AudioPipeline::~AudioPipeline() { close(); }

bool AudioPipeline::open(const AudioOpenParams& params) {
  impl_->params = params;
  return impl_->open();
}

int AudioPipeline::read(std::int16_t* out, int maxSamples) {
  if (maxSamples <= 0) {
    return 0;
  }
  auto& im = *impl_;
  while (im.staged.size() - im.stagedOffset == 0) {
    if (im.interrupt.load()) {
      return 0;
    }
    if (!im.pump() && im.staged.size() - im.stagedOffset == 0) {
      return 0;  // EOF, fully drained
    }
  }
  std::size_t available = im.staged.size() - im.stagedOffset;
  std::size_t take = std::min<std::size_t>(available, static_cast<std::size_t>(maxSamples));
  std::memcpy(out, im.staged.data() + im.stagedOffset, take * sizeof(std::int16_t));
  im.stagedOffset += take;
  if (im.stagedOffset >= im.staged.size()) {
    im.staged.clear();
    im.stagedOffset = 0;
  } else if (im.stagedOffset > 1u << 18) {
    im.staged.erase(im.staged.begin(),
                    im.staged.begin() + static_cast<std::ptrdiff_t>(im.stagedOffset));
    im.stagedOffset = 0;
  }
  return static_cast<int>(take);
}

void AudioPipeline::requestStop() { impl_->interrupt.store(true); }

void AudioPipeline::close() { impl_->closeAll(); }

// ============================================================================
// D3D11 interop helpers
// ============================================================================

void* rendererD3D11Device(SDL_Renderer* renderer) {
#ifdef _WIN32
  if (!renderer) {
    return nullptr;
  }
  return SDL_GetPointerProperty(SDL_GetRendererProperties(renderer),
                                SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr);
#else
  (void) renderer;
  return nullptr;
#endif
}

SDL_Texture* createWrappedNV12Texture(SDL_Renderer* renderer, int w, int h,
                                      void** outTexture2D) {
#ifdef _WIN32
  if (outTexture2D) {
    *outTexture2D = nullptr;
  }
  w &= ~1;
  h &= ~1;
  if (!renderer || !outTexture2D || w <= 0 || h <= 0) {
    return nullptr;
  }
  // Let SDL create and own the texture (its normal NV12 path, with SRVs and
  // shaders it manages), then pull out the backing ID3D11Texture2D so the
  // compositor can GPU-copy decoded slices into it. STATIC access maps to
  // D3D11_USAGE_DEFAULT — a valid CopySubresourceRegion destination.
  SDL_Texture* texture = deckboyCreateTexture(renderer, SDL_PIXELFORMAT_NV12,
                                              SDL_TEXTUREACCESS_STATIC, w, h);
  if (!texture) {
    return nullptr;
  }
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  void* texture2D = SDL_GetPointerProperty(SDL_GetTextureProperties(texture),
                                           SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr);
  if (!texture2D) {
    SDL_DestroyTexture(texture);  // software renderer or non-D3D11 backend
    return nullptr;
  }
  // Own reference: callers release it independently of the SDL_Texture.
  reinterpret_cast<ID3D11Texture2D*>(texture2D)->AddRef();
  *outTexture2D = texture2D;
  return texture;
#else
  (void) renderer; (void) w; (void) h; (void) outTexture2D;
  return nullptr;
#endif
}

void releaseD3D11Texture(void* texture2D) {
#ifdef _WIN32
  if (texture2D) {
    reinterpret_cast<ID3D11Texture2D*>(texture2D)->Release();
  }
#else
  (void) texture2D;
#endif
}

bool copyGpuFrameToTexture(const DecodedFrame& frame, void* dstTexture2D) {
#ifdef _WIN32
  if (!frame.isGpu() || !frame.gpuDevice || !dstTexture2D) {
    return false;
  }
  auto* device = reinterpret_cast<ID3D11Device*>(frame.gpuDevice);
  ID3D11DeviceContext* context = nullptr;
  device->GetImmediateContext(&context);
  if (!context) {
    return false;
  }
  D3D11_BOX box = {};
  box.right = static_cast<UINT>(frame.width & ~1);
  box.bottom = static_cast<UINT>(frame.height & ~1);
  box.back = 1;
  context->CopySubresourceRegion(
    reinterpret_cast<ID3D11Texture2D*>(dstTexture2D), 0, 0, 0, 0,
    reinterpret_cast<ID3D11Texture2D*>(frame.gpuTexture),
    static_cast<UINT>(frame.gpuSubresource), &box);
  context->Release();
  return true;
#else
  (void) frame; (void) dstTexture2D;
  return false;
#endif
}

bool downloadGpuFrameNV12(const DecodedFrame& frame, DecodedFrame& out) {
  if (!frame.isGpu() || !frame.gpuFrameRef) {
    return false;
  }
  auto* src = reinterpret_cast<AVFrame*>(frame.gpuFrameRef.get());
  AVFrame* cpu = av_frame_alloc();
  if (!cpu) {
    return false;
  }
  bool ok = av_hwframe_transfer_data(cpu, src, 0) >= 0 &&
            cpu->format == AV_PIX_FMT_NV12;
  if (ok) {
    int w = cpu->width & ~1;
    int h = cpu->height & ~1;
    out.width = w;
    out.height = h;
    out.index = frame.index;
    out.format = FramePixelFormat::NV12;
    out.gpuFrameRef.reset();
    out.gpuTexture = nullptr;
    out.gpuSubresource = 0;
    out.gpuDevice = nullptr;
    out.pixels.resize(frameBufferSize(FramePixelFormat::NV12, w, h));
    std::uint8_t* dstY = out.pixels.data();
    std::uint8_t* dstUV = dstY + static_cast<std::size_t>(w) * h;
    for (int y = 0; y < h; ++y) {
      std::memcpy(dstY + static_cast<std::size_t>(y) * w,
                  cpu->data[0] + static_cast<std::size_t>(y) * cpu->linesize[0], w);
    }
    for (int y = 0; y < h / 2; ++y) {
      std::memcpy(dstUV + static_cast<std::size_t>(y) * w,
                  cpu->data[1] + static_cast<std::size_t>(y) * cpu->linesize[1], w);
    }
  }
  av_frame_free(&cpu);
  return ok;
}

} // namespace deckboy::libav

#endif // DECKBOY_INPROC_DECODE
