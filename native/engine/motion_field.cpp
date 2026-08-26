#include "motion_field.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#if DECKBOY_INPROC_DECODE
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/motion_vector.h>
}
#endif

namespace deckboy::motion {

#if DECKBOY_INPROC_DECODE
namespace {

struct MotionSource {
  AVFormatContext* fmt = nullptr;
  AVCodecContext* codec = nullptr;
  AVPacket* packet = nullptr;
  AVFrame* frame = nullptr;
  int streamIndex = -1;
  int cellPixels = 16;
  std::uint64_t frameIndex = 0;
};

void accumulate(MotionField& field, const AVMotionVector& mv, int cellPixels) {
  // src_x/src_y are where the block CAME FROM; dst_x/dst_y where it landed.
  // The displacement an effect wants is destination minus source.
  const float ddx = static_cast<float>(mv.dst_x - mv.src_x);
  const float ddy = static_cast<float>(mv.dst_y - mv.src_y);
  const int col = std::clamp(mv.dst_x / cellPixels, 0, field.cols - 1);
  const int row = std::clamp(mv.dst_y / cellPixels, 0, field.rows - 1);
  const std::size_t i = static_cast<std::size_t>(row) * field.cols + col;
  // A macroblock can be split into several vectors (16x16 down to 4x4), so a
  // cell may receive more than one. Taking the LARGEST rather than averaging:
  // averaging a split block's opposing halves cancels them to nothing, which
  // reads as the busiest part of the picture being the stillest.
  if (ddx * ddx + ddy * ddy > field.dx[i] * field.dx[i] + field.dy[i] * field.dy[i]) {
    field.dx[i] = ddx;
    field.dy[i] = ddy;
  }
}

}  // namespace
#endif

void* openMotionSource(const std::string& path, int cellPixels) {
#if DECKBOY_INPROC_DECODE
  auto* src = new MotionSource();
  src->cellPixels = std::max(4, cellPixels);

  if (avformat_open_input(&src->fmt, path.c_str(), nullptr, nullptr) < 0) {
    delete src;
    return nullptr;
  }
  if (avformat_find_stream_info(src->fmt, nullptr) < 0) {
    closeMotionSource(src);
    return nullptr;
  }
  const AVCodec* codec = nullptr;
  src->streamIndex =
    av_find_best_stream(src->fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
  if (src->streamIndex < 0 || !codec) {
    closeMotionSource(src);
    return nullptr;
  }
  src->codec = avcodec_alloc_context3(codec);
  if (!src->codec ||
      avcodec_parameters_to_context(
        src->codec, src->fmt->streams[src->streamIndex]->codecpar) < 0) {
    closeMotionSource(src);
    return nullptr;
  }
  // The whole point. Without this the decoder discards what it worked out.
  AVDictionary* opts = nullptr;
  av_dict_set(&opts, "flags2", "+export_mvs", 0);
  // Deliberately software and single-threaded-ish: a hardware decoder does not
  // surface its vectors at all, and this clip's PIXELS are thrown away, so it
  // has no frame-rate obligation to meet -- only its motion is wanted.
  src->codec->thread_count = 2;
  const int opened = avcodec_open2(src->codec, codec, &opts);
  av_dict_free(&opts);
  if (opened < 0) {
    closeMotionSource(src);
    return nullptr;
  }
  src->packet = av_packet_alloc();
  src->frame = av_frame_alloc();
  if (!src->packet || !src->frame) {
    closeMotionSource(src);
    return nullptr;
  }
  return src;
#else
  (void) path; (void) cellPixels;
  return nullptr;
#endif
}

bool readMotionField(void* handle, MotionField& out) {
#if DECKBOY_INPROC_DECODE
  auto* src = static_cast<MotionSource*>(handle);
  if (!src) {
    return false;
  }
  for (;;) {
    int got = avcodec_receive_frame(src->codec, src->frame);
    if (got == 0) {
      out.sourceWidth = src->frame->width;
      out.sourceHeight = src->frame->height;
      out.cols = std::max(1, (src->frame->width + src->cellPixels - 1) / src->cellPixels);
      out.rows = std::max(1, (src->frame->height + src->cellPixels - 1) / src->cellPixels);
      const std::size_t cells = static_cast<std::size_t>(out.cols) * out.rows;
      out.dx.assign(cells, 0.0f);
      out.dy.assign(cells, 0.0f);
      out.frameIndex = src->frameIndex++;

      if (AVFrameSideData* sd =
            av_frame_get_side_data(src->frame, AV_FRAME_DATA_MOTION_VECTORS)) {
        const auto* mvs = reinterpret_cast<const AVMotionVector*>(sd->data);
        const std::size_t count = sd->size / sizeof(AVMotionVector);
        for (std::size_t i = 0; i < count; ++i) {
          accumulate(out, mvs[i], src->cellPixels);
        }
      } else {
        // An I-frame predicted nothing, so it has nothing to say about motion.
        // Reported as an empty field, not a failure.
        out.dx.clear();
        out.dy.clear();
      }
      av_frame_unref(src->frame);
      return true;
    }
    if (got != AVERROR(EAGAIN)) {
      return false;   // EOF or a real decode error
    }
    // Need more input.
    int read = av_read_frame(src->fmt, src->packet);
    if (read < 0) {
      avcodec_send_packet(src->codec, nullptr);   // drain
      continue;
    }
    if (src->packet->stream_index == src->streamIndex) {
      avcodec_send_packet(src->codec, src->packet);
    }
    av_packet_unref(src->packet);
  }
#else
  (void) handle; (void) out;
  return false;
#endif
}

void rewindMotionSource(void* handle) {
#if DECKBOY_INPROC_DECODE
  auto* src = static_cast<MotionSource*>(handle);
  if (!src || !src->fmt) {
    return;
  }
  av_seek_frame(src->fmt, src->streamIndex, 0, AVSEEK_FLAG_BACKWARD);
  avcodec_flush_buffers(src->codec);
  src->frameIndex = 0;
#else
  (void) handle;
#endif
}

void closeMotionSource(void* handle) {
#if DECKBOY_INPROC_DECODE
  auto* src = static_cast<MotionSource*>(handle);
  if (!src) {
    return;
  }
  if (src->frame) av_frame_free(&src->frame);
  if (src->packet) av_packet_free(&src->packet);
  if (src->codec) avcodec_free_context(&src->codec);
  if (src->fmt) avformat_close_input(&src->fmt);
  delete src;
#else
  (void) handle;
#endif
}

}  // namespace deckboy::motion
