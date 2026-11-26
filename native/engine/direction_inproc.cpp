// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "engine/direction_inproc.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace deckboy::engine {

namespace {

struct Owned {
  AVFormatContext* in = nullptr;
  AVFormatContext* out = nullptr;
  bool outOpened = false;
  AVCodecContext* vdec = nullptr;
  AVCodecContext* adec = nullptr;
  AVCodecContext* enc = nullptr;
  SwsContext* sws = nullptr;
  SwrContext* swr = nullptr;
  AVFrame* frame = av_frame_alloc();
  AVFrame* scaled = av_frame_alloc();
  AVPacket* pkt = av_packet_alloc();
  AVPacket* encoded = av_packet_alloc();
  std::FILE* jpegs = nullptr;
  std::string jpegPath;
  ~Owned() {
    if (jpegs) std::fclose(jpegs);
    if (!jpegPath.empty()) std::remove(jpegPath.c_str());
    if (out) {
      if (outOpened) avio_closep(&out->pb);
      avformat_free_context(out);
    }
    if (in) avformat_close_input(&in);
    avcodec_free_context(&vdec);
    avcodec_free_context(&adec);
    avcodec_free_context(&enc);
    sws_freeContext(sws);
    swr_free(&swr);
    av_frame_free(&frame);
    av_frame_free(&scaled);
    av_packet_free(&pkt);
    av_packet_free(&encoded);
  }
};

struct Jpeg { long offset; int size; };

constexpr int kRate = 48000;
// Sound held in memory to be reversed: twelve minutes of 16-bit stereo is
// 138 MB. Longer, and the copy has no sound rather than a phone out of memory.
constexpr double kMaxSoundSeconds = 12 * 60;

AVCodecContext* openDecoder(AVStream* st) {
  const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);   // the software decoder
  if (!codec) return nullptr;
  AVCodecContext* ctx = avcodec_alloc_context3(codec);
  if (!ctx) return nullptr;
  avcodec_parameters_to_context(ctx, st->codecpar);
  ctx->thread_count = 0;
  ctx->pkt_timebase = st->time_base;
  if (avcodec_open2(ctx, codec, nullptr) < 0) avcodec_free_context(&ctx);
  return ctx;
}

}  // namespace

bool decodeStills(const std::string& source, const std::vector<double>& times, int maxWidth, std::vector<Still>& out) {
  out.assign(times.size(), Still {});
  AVFormatContext* in = nullptr;
  if (avformat_open_input(&in, source.c_str(), nullptr, nullptr) < 0) return false;
  struct Close { AVFormatContext*& f; ~Close() { avformat_close_input(&f); } } closeIn {in};
  if (avformat_find_stream_info(in, nullptr) < 0) return false;
  const int v = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (v < 0) return false;
  AVStream* st = in->streams[v];
  AVCodecContext* dec = openDecoder(st);
  if (!dec) return false;
  struct FreeDec { AVCodecContext*& c; ~FreeDec() { avcodec_free_context(&c); } } freeDec {dec};
  AVFrame* frame = av_frame_alloc();
  AVPacket* pkt = av_packet_alloc();
  SwsContext* sws = nullptr;
  const int64_t start = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
  bool any = false;
  for (std::size_t k = 0; k < times.size(); ++k) {
    const int64_t want = start + static_cast<int64_t>(times[k] / av_q2d(st->time_base));
    if (av_seek_frame(in, v, want, AVSEEK_FLAG_BACKWARD) < 0) continue;
    avcodec_flush_buffers(dec);
    bool got = false;
    int packets = 0;
    while (!got && packets < 600 && av_read_frame(in, pkt) >= 0) {
      if (pkt->stream_index == v && avcodec_send_packet(dec, pkt) >= 0) {
        ++packets;
        while (!got && avcodec_receive_frame(dec, frame) == 0) {
          const int64_t ts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
          // the first frame at or past the time (a filmstrip forgives a frame or two)
          if (ts == AV_NOPTS_VALUE || ts >= want - static_cast<int64_t>(0.04 / av_q2d(st->time_base))) {
            int w = frame->width, h = frame->height;
            if (maxWidth > 0 && w > maxWidth) { h = std::max(2, static_cast<int>(std::lround(static_cast<double>(h) * maxWidth / w))); w = maxWidth; }
            sws = sws_getCachedContext(sws, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format), w, h,
                                       AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (sws) {
              Still& s = out[k];
              s.width = w;
              s.height = h;
              // Room past the end: sws_scale writes in whole SIMD blocks and
              // can run up to 32 bytes past the last row (libav_decoder.cpp,
              // kScalerSlack).
              s.rgba.reserve(static_cast<std::size_t>(w) * h * 4 + 256);
              s.rgba.resize(static_cast<std::size_t>(w) * h * 4);
              uint8_t* dst[1] = {s.rgba.data()};
              int dstStride[1] = {w * 4};
              sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
              any = true;
            }
            got = true;
          }
          av_frame_unref(frame);
        }
      }
      av_packet_unref(pkt);
    }
  }
  sws_freeContext(sws);
  av_frame_free(&frame);
  av_packet_free(&pkt);
  return any;
}

RemuxResult bakeDirectionInProcess(const std::string& source, const std::string& dest, double in, double out,
                                   bool bounce, int maxWidth, std::atomic<double>* progress,
                                   std::atomic<bool>* cancel, std::string* why, DirectionCopy* made) {
  auto fail = [&](const std::string& reason) {
    if (why) *why = reason;
    std::remove(dest.c_str());
    return RemuxResult::Failed;
  };
  auto cancelled = [&] { return cancel && cancel->load(); };
  if (!(out > in)) return fail("the range is empty");
  Owned o;
  if (avformat_open_input(&o.in, source.c_str(), nullptr, nullptr) < 0) return fail("cannot open the file");
  if (avformat_find_stream_info(o.in, nullptr) < 0) return fail("cannot read the file's streams");
  const int v = av_find_best_stream(o.in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (v < 0) return fail("no picture in the file");
  const int a = av_find_best_stream(o.in, AVMEDIA_TYPE_AUDIO, -1, v, nullptr, 0);
  AVStream* vst = o.in->streams[v];
  AVStream* ast = a >= 0 ? o.in->streams[a] : nullptr;
  o.vdec = openDecoder(vst);
  if (!o.vdec) return fail("no decoder for the picture");
  if (ast) o.adec = openDecoder(ast);

  AVRational rate = av_guess_frame_rate(o.in, vst, nullptr);
  if (rate.num <= 0 || rate.den <= 0 || av_q2d(rate) > 240.0) rate = AVRational {30, 1};
  const double fps = av_q2d(rate);

  // The JPEG encoder, sized once from the stream (a phone's 4K comes down).
  const AVCodec* mjpeg = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
  if (!mjpeg) return fail("this build has no JPEG encoder");
  int w = o.vdec->width, h = o.vdec->height;
  if (w <= 0 || h <= 0) return fail("the picture has no size");
  if (maxWidth > 0 && w > maxWidth) {
    h = static_cast<int>(std::lround(static_cast<double>(h) * maxWidth / w));
    w = maxWidth;
  }
  w &= ~1;
  h &= ~1;
  o.enc = avcodec_alloc_context3(mjpeg);
  o.enc->width = w;
  o.enc->height = h;
  o.enc->pix_fmt = AV_PIX_FMT_YUVJ420P;
  o.enc->color_range = AVCOL_RANGE_JPEG;
  o.enc->time_base = av_inv_q(rate);
  o.enc->framerate = rate;
  o.enc->flags |= AV_CODEC_FLAG_QSCALE;
  o.enc->global_quality = FF_QP2LAMBDA * 3;   // q 3: no second generation to see
  o.enc->qmin = 2;
  o.enc->qmax = 4;
  if (avcodec_open2(o.enc, mjpeg, nullptr) < 0) return fail("cannot start the JPEG encoder");
  o.scaled->format = AV_PIX_FMT_YUVJ420P;
  o.scaled->width = w;
  o.scaled->height = h;
  if (av_frame_get_buffer(o.scaled, 0) < 0) return fail("out of memory");

  o.jpegPath = dest + ".frames";
  o.jpegs = std::fopen(o.jpegPath.c_str(), "w+b");
  if (!o.jpegs) return fail("cannot write beside the copy");
  std::vector<Jpeg> frames;

  if (o.adec) {
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&o.swr, &stereo, AV_SAMPLE_FMT_S16, kRate, &o.adec->ch_layout, o.adec->sample_fmt,
                            o.adec->sample_rate, 0, nullptr) < 0 || swr_init(o.swr) < 0) {
      avcodec_free_context(&o.adec);   // no sound rather than no copy
    }
  }
  const bool wantSound = o.adec && (out - in) <= kMaxSoundSeconds;
  std::vector<int16_t> sound;   // interleaved stereo, the range only
  if (wantSound) sound.reserve(static_cast<std::size_t>((out - in) * kRate * 2) + 4096);

  const int64_t vStart = vst->start_time != AV_NOPTS_VALUE ? vst->start_time : 0;
  const int64_t aStart = ast && ast->start_time != AV_NOPTS_VALUE ? ast->start_time : 0;
  auto vSeconds = [&](int64_t ts) { return static_cast<double>(ts - vStart) * av_q2d(vst->time_base); };
  auto aSeconds = [&](int64_t ts) { return static_cast<double>(ts - aStart) * av_q2d(ast->time_base); };
  const int64_t seekTo = vStart + static_cast<int64_t>(in / av_q2d(vst->time_base));
  if (in > 0.0 && av_seek_frame(o.in, v, seekTo, AVSEEK_FLAG_BACKWARD) < 0) return fail("cannot seek in the file");

  const double half = 0.5 / fps;
  bool videoDone = false, soundDone = !wantSound;
  std::string encodeError;

  auto takeVideoFrame = [&](AVFrame* f) -> bool {
    const int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    const double t = ts == AV_NOPTS_VALUE ? in : vSeconds(ts);
    if (t < in - half) return true;
    if (t >= out - half) { videoDone = true; return true; }
    o.sws = sws_getCachedContext(o.sws, f->width, f->height, static_cast<AVPixelFormat>(f->format), w, h,
                                 AV_PIX_FMT_YUVJ420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!o.sws || av_frame_make_writable(o.scaled) < 0) { encodeError = "cannot convert the picture"; return false; }
    sws_scale(o.sws, f->data, f->linesize, 0, f->height, o.scaled->data, o.scaled->linesize);
    o.scaled->pts = static_cast<int64_t>(frames.size());
    if (avcodec_send_frame(o.enc, o.scaled) < 0) { encodeError = "the JPEG encoder refused a frame"; return false; }
    while (avcodec_receive_packet(o.enc, o.encoded) == 0) {
      Jpeg j {std::ftell(o.jpegs), o.encoded->size};
      if (std::fwrite(o.encoded->data, 1, static_cast<std::size_t>(o.encoded->size), o.jpegs) !=
          static_cast<std::size_t>(o.encoded->size)) {
        encodeError = "the disk is full";
        return false;
      }
      frames.push_back(j);
      av_packet_unref(o.encoded);
    }
    if (progress) progress->store(0.85 * std::clamp((t - in) / (out - in), 0.0, 1.0));
    return true;
  };
  auto takeSoundFrame = [&](AVFrame* f) {
    const int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    const double t0 = ts == AV_NOPTS_VALUE ? in : aSeconds(ts);
    const int maxOut = swr_get_out_samples(o.swr, f->nb_samples);
    std::vector<int16_t> buf(static_cast<std::size_t>(std::max(0, maxOut)) * 2);
    uint8_t* dst = reinterpret_cast<uint8_t*>(buf.data());
    const int got = swr_convert(o.swr, &dst, maxOut, const_cast<const uint8_t**>(f->extended_data), f->nb_samples);
    for (int i = 0; i < got; ++i) {
      const double t = t0 + static_cast<double>(i) / kRate;
      if (t < in) continue;
      if (t >= out) { soundDone = true; break; }
      sound.push_back(buf[static_cast<std::size_t>(i) * 2]);
      sound.push_back(buf[static_cast<std::size_t>(i) * 2 + 1]);
    }
  };
  auto drain = [&](AVCodecContext* dec, bool video) -> bool {
    while (avcodec_receive_frame(dec, o.frame) == 0) {
      if (video) {
        if (!videoDone && !takeVideoFrame(o.frame)) return false;
      } else if (!soundDone) {
        takeSoundFrame(o.frame);
      }
      av_frame_unref(o.frame);
    }
    return true;
  };

  while (!(videoDone && soundDone)) {
    if (cancelled()) { std::remove(dest.c_str()); return RemuxResult::Cancelled; }
    const int r = av_read_frame(o.in, o.pkt);
    if (r < 0) {   // the end of the file: flush both decoders
      avcodec_send_packet(o.vdec, nullptr);
      if (!drain(o.vdec, true)) return fail(encodeError);
      if (o.adec && wantSound) {
        avcodec_send_packet(o.adec, nullptr);
        drain(o.adec, false);
      }
      break;
    }
    if (o.pkt->stream_index == v && !videoDone) {
      if (avcodec_send_packet(o.vdec, o.pkt) >= 0 && !drain(o.vdec, true)) {
        av_packet_unref(o.pkt);
        return fail(encodeError);
      }
    } else if (o.pkt->stream_index == a && wantSound && !soundDone) {
      if (avcodec_send_packet(o.adec, o.pkt) >= 0) drain(o.adec, false);
    }
    av_packet_unref(o.pkt);
  }
  if (frames.size() < 3) return fail("fewer than three frames in the range");
  std::fflush(o.jpegs);

  // The order the frames play in the copy.
  std::vector<int> order;
  const int n = static_cast<int>(frames.size());
  if (bounce) {
    for (int i = 0; i < n; ++i) order.push_back(i);
    for (int i = n - 2; i >= 1; --i) order.push_back(i);   // no frame twice at a turnaround
  } else {
    for (int i = n - 1; i >= 0; --i) order.push_back(i);
  }
  // The sound to match: reversed, or there and back, cut to the picture's length.
  std::vector<int16_t> played;
  if (wantSound && !sound.empty()) {
    const std::size_t pairs = sound.size() / 2;
    auto reversed = [&](std::vector<int16_t>& into) {
      for (std::size_t i = pairs; i-- > 0;) {
        into.push_back(sound[i * 2]);
        into.push_back(sound[i * 2 + 1]);
      }
    };
    if (bounce) {
      played = sound;
      reversed(played);
    } else {
      reversed(played);
    }
    const std::size_t want = static_cast<std::size_t>(std::llround(order.size() / fps * kRate)) * 2;
    played.resize(std::min(played.size(), want));
  }

  // Write the copy.
  if (avformat_alloc_output_context2(&o.out, nullptr, "mov", dest.c_str()) < 0 || !o.out) return fail("cannot make the copy");
  AVStream* ov = avformat_new_stream(o.out, nullptr);
  if (!ov || avcodec_parameters_from_context(ov->codecpar, o.enc) < 0) return fail("cannot make the copy's picture");
  ov->time_base = o.enc->time_base;
  ov->avg_frame_rate = rate;
  // A phone's portrait clip stores "turn me" beside the picture; the copy keeps it.
  if (const AVPacketSideData* dm = av_packet_side_data_get(vst->codecpar->coded_side_data,
                                                           vst->codecpar->nb_coded_side_data,
                                                           AV_PKT_DATA_DISPLAYMATRIX)) {
    if (AVPacketSideData* copy = av_packet_side_data_new(&ov->codecpar->coded_side_data,
                                                         &ov->codecpar->nb_coded_side_data,
                                                         AV_PKT_DATA_DISPLAYMATRIX, dm->size, 0)) {
      std::memcpy(copy->data, dm->data, dm->size);
    }
  }
  AVStream* oa = nullptr;
  if (!played.empty()) {
    oa = avformat_new_stream(o.out, nullptr);
    oa->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    oa->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
    oa->codecpar->sample_rate = kRate;
    oa->codecpar->format = AV_SAMPLE_FMT_S16;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    av_channel_layout_copy(&oa->codecpar->ch_layout, &stereo);
    oa->codecpar->bits_per_coded_sample = 16;
    oa->codecpar->block_align = 4;
    oa->codecpar->bit_rate = static_cast<int64_t>(kRate) * 32;
    oa->time_base = AVRational {1, kRate};
  }
  if (avio_open(&o.out->pb, dest.c_str(), AVIO_FLAG_WRITE) < 0) return fail("cannot write the copy");
  o.outOpened = true;
  if (avformat_write_header(o.out, nullptr) < 0) return fail("cannot start the copy");

  std::vector<uint8_t> buf;
  const int chunk = 1024;   // sample frames per sound packet
  std::size_t vi = 0, ai = 0;
  const std::size_t soundFrames = played.size() / 2;
  while (vi < order.size() || ai < soundFrames) {
    if (cancelled()) { std::remove(dest.c_str()); return RemuxResult::Cancelled; }
    const double vt = vi < order.size() ? static_cast<double>(vi) / fps : 1e18;
    const double at = ai < soundFrames ? static_cast<double>(ai) / kRate : 1e18;
    AVPacket* p = o.pkt;
    if (vt <= at) {
      const Jpeg& j = frames[static_cast<std::size_t>(order[vi])];
      buf.resize(static_cast<std::size_t>(j.size));
      std::fseek(o.jpegs, j.offset, SEEK_SET);
      if (std::fread(buf.data(), 1, buf.size(), o.jpegs) != buf.size()) return fail("cannot read the frames back");
      if (av_new_packet(p, j.size) < 0) return fail("out of memory");
      std::memcpy(p->data, buf.data(), buf.size());
      p->stream_index = ov->index;
      p->pts = p->dts = static_cast<int64_t>(vi);
      p->duration = 1;
      p->flags |= AV_PKT_FLAG_KEY;
      av_packet_rescale_ts(p, o.enc->time_base, ov->time_base);
      ++vi;
      if (progress) progress->store(0.85 + 0.15 * static_cast<double>(vi) / static_cast<double>(order.size()));
    } else {
      const std::size_t count = std::min<std::size_t>(chunk, soundFrames - ai);
      if (av_new_packet(p, static_cast<int>(count * 4)) < 0) return fail("out of memory");
      std::memcpy(p->data, played.data() + ai * 2, count * 4);
      p->stream_index = oa->index;
      p->pts = p->dts = static_cast<int64_t>(ai);
      p->duration = static_cast<int64_t>(count);
      p->flags |= AV_PKT_FLAG_KEY;
      av_packet_rescale_ts(p, AVRational {1, kRate}, oa->time_base);
      ai += count;
    }
    if (av_interleaved_write_frame(o.out, p) < 0) return fail("cannot write the copy");
  }
  if (av_write_trailer(o.out) < 0) return fail("cannot finish the copy");
  if (made) {
    made->seconds = static_cast<double>(order.size()) / fps;
    made->width = w;
    made->height = h;
    made->sound = oa != nullptr;
  }
  if (progress) progress->store(1.0);
  return RemuxResult::Done;
}

}  // namespace deckboy::engine
