// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "engine/direction_remux.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

namespace deckboy::engine {

namespace {

struct Input {
  AVFormatContext* fmt = nullptr;
  ~Input() { if (fmt) avformat_close_input(&fmt); }
};

struct Output {
  AVFormatContext* fmt = nullptr;
  bool opened = false;
  ~Output() {
    if (!fmt) return;
    if (opened) avio_closep(&fmt->pb);
    avformat_free_context(fmt);
  }
};

struct Packet {
  AVPacket* p = av_packet_alloc();
  ~Packet() { av_packet_free(&p); }
};

}  // namespace

RemuxResult reverseIntraVideo(const std::string& source, const std::string& dest, double in, double out,
                              bool bounce, std::atomic<double>* progress, std::atomic<bool>* cancel,
                              std::string* why) {
  auto fail = [&](RemuxResult r, const char* reason) {
    if (why) *why = reason;
    return r;
  };
  Input src;
  if (avformat_open_input(&src.fmt, source.c_str(), nullptr, nullptr) < 0) return fail(RemuxResult::Failed, "cannot open the file");
  if (avformat_find_stream_info(src.fmt, nullptr) < 0) return fail(RemuxResult::Failed, "cannot read the file's streams");
  const int v = av_find_best_stream(src.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
  if (v < 0) return fail(RemuxResult::Failed, "no picture in the file");
  AVStream* ist = src.fmt->streams[v];
  const AVCodecDescriptor* desc = avcodec_descriptor_get(ist->codecpar->codec_id);
  if (!desc || !(desc->props & AV_CODEC_PROP_INTRA_ONLY)) {
    return fail(RemuxResult::NotIntra, "the codec stores frames as changes from others");
  }
  const AVRational tb = ist->time_base;
  const int64_t start = ist->start_time != AV_NOPTS_VALUE ? ist->start_time : 0;

  // The frame length, in the stream's own ticks.
  AVRational rate = ist->avg_frame_rate.num > 0 ? ist->avg_frame_rate : ist->r_frame_rate;
  int64_t frameTicks = rate.num > 0 ? av_rescale_q(1, av_inv_q(rate), tb) : 0;

  // PASS 1: which frames are in the range, and is every one a keyframe? Only
  // timestamps are kept, never the pictures.
  std::vector<int64_t> frames;
  {
    Packet pkt;
    if (!pkt.p) return fail(RemuxResult::Failed, "out of memory");
    while (av_read_frame(src.fmt, pkt.p) >= 0) {
      if (cancel && cancel->load()) return fail(RemuxResult::Cancelled, "cancelled");
      if (pkt.p->stream_index == v) {
        if (!(pkt.p->flags & AV_PKT_FLAG_KEY)) {
          av_packet_unref(pkt.p);
          return fail(RemuxResult::NotIntra, "a frame in the file depends on another");
        }
        const int64_t ts = pkt.p->pts != AV_NOPTS_VALUE ? pkt.p->pts : pkt.p->dts;
        if (frameTicks <= 0 && pkt.p->duration > 0) frameTicks = pkt.p->duration;
        if (ts != AV_NOPTS_VALUE) {
          // In the range by the frame's middle, so a boundary on a frame's
          // edge cannot take one frame too many or too few.
          const double t = (ts - start) * av_q2d(tb);
          const double half = frameTicks > 0 ? 0.5 * frameTicks * av_q2d(tb) : 0.0;
          if (t + half > in && t + half < out) frames.push_back(ts);
        }
      }
      av_packet_unref(pkt.p);
    }
  }
  if (frameTicks <= 0) return fail(RemuxResult::Failed, "the frame rate is unknown");
  std::sort(frames.begin(), frames.end());
  frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
  if (frames.size() < 3) return fail(RemuxResult::Failed, "the range is shorter than three frames");

  std::vector<int64_t> order;
  if (bounce) {
    order = frames;
    for (std::size_t k = frames.size() - 1; k-- > 1;) order.push_back(frames[k]);
  } else {
    order.assign(frames.rbegin(), frames.rend());
  }

  // PASS 2: the packets, in that order, restamped one frame apart.
  Output dst;
  if (avformat_alloc_output_context2(&dst.fmt, nullptr, "mov", dest.c_str()) < 0 || !dst.fmt) {
    return fail(RemuxResult::Failed, "cannot make the copy");
  }
  AVStream* ost = avformat_new_stream(dst.fmt, nullptr);
  if (!ost || avcodec_parameters_copy(ost->codecpar, ist->codecpar) < 0) return fail(RemuxResult::Failed, "cannot make the copy");
  ost->codecpar->codec_tag = 0;   // the muxer picks the tag for .mov
  ost->time_base = tb;
  ost->avg_frame_rate = rate;
  if (avio_open(&dst.fmt->pb, dest.c_str(), AVIO_FLAG_WRITE) < 0) return fail(RemuxResult::Failed, "cannot write the copy");
  dst.opened = true;
  if (avformat_write_header(dst.fmt, nullptr) < 0) return fail(RemuxResult::Failed, "the copy's format refused this codec");

  Packet pkt;
  int64_t lastRead = AV_NOPTS_VALUE;
  for (std::size_t k = 0; k < order.size(); ++k) {
    if (cancel && cancel->load()) return fail(RemuxResult::Cancelled, "cancelled");
    const int64_t want = order[k];
    // Running forwards (a bounce's first half), the next packet is the next
    // frame; anything else needs a seek, which the file's index makes cheap.
    if (lastRead == AV_NOPTS_VALUE || want <= lastRead) {
      if (av_seek_frame(src.fmt, v, want, AVSEEK_FLAG_BACKWARD) < 0) return fail(RemuxResult::Failed, "cannot seek in the file");
    }
    bool found = false;
    for (int guard = 0; guard < 4096 && !found; ++guard) {
      if (av_read_frame(src.fmt, pkt.p) < 0) break;
      if (pkt.p->stream_index != v) { av_packet_unref(pkt.p); continue; }
      const int64_t ts = pkt.p->pts != AV_NOPTS_VALUE ? pkt.p->pts : pkt.p->dts;
      lastRead = ts;
      if (ts < want) { av_packet_unref(pkt.p); continue; }
      if (ts > want) { av_packet_unref(pkt.p); break; }
      found = true;
    }
    if (!found) return fail(RemuxResult::Failed, "a frame could not be read back");
    pkt.p->stream_index = ost->index;
    pkt.p->pts = pkt.p->dts = static_cast<int64_t>(k) * frameTicks;
    pkt.p->duration = frameTicks;
    pkt.p->pos = -1;
    av_packet_rescale_ts(pkt.p, tb, ost->time_base);
    const int w = av_interleaved_write_frame(dst.fmt, pkt.p);
    av_packet_unref(pkt.p);
    if (w < 0) return fail(RemuxResult::Failed, "cannot write the copy");
    if (progress) progress->store(std::min(0.9, 0.9 * static_cast<double>(k + 1) / static_cast<double>(order.size())));
  }
  if (av_write_trailer(dst.fmt) < 0) return fail(RemuxResult::Failed, "cannot finish the copy");
  return RemuxResult::Done;
}

}  // namespace deckboy::engine
