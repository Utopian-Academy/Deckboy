// motion_field.hpp — read a clip's real motion vectors out of its bitstream.
//
// H.264 and MPEG-4 already contain a per-macroblock description of what moved
// where; it is how they achieve their compression. Decoding normally throws
// that away and keeps only the pictures. Asking libav for it costs nothing
// extra, because the decoder computed it regardless.
//
// This exists so one clip's MOTION can drive a different source's PIXELS --
// a camera feed animated by a crowd scene, a synth driven by a dancer. Editing
// motion vectors offline is mature practice (FFglitch and friends); the point
// here is doing it live, on a running cue, across two sources, which a tool
// that rewrites a bitstream into a new file cannot put on a cue.
//
// TWO CONSTRAINTS, both discovered rather than assumed:
//
//   - Vectors are exported by the SOFTWARE decoder only. A d3d11va frame comes
//     back as a GPU surface and the hardware decoder does not surface what it
//     used. That is affordable here because the driver clip's pixels are
//     thrown away -- only its motion is wanted -- so it can be decoded small.
//   - An I-frame carries no vectors, because nothing was predicted. MEASURED
//     on a test clip: 19 of 20 frames had them, and the one that did not was
//     the keyframe. Callers get an empty field and should hold the last one.
#ifndef DECKBOY_ENGINE_MOTION_FIELD_HPP
#define DECKBOY_ENGINE_MOTION_FIELD_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace deckboy::motion {

// A coarse grid of displacements, in SOURCE PIXELS, sampled from the clip's
// macroblocks. Deliberately not per-pixel: the codec's own granularity is a
// macroblock, and pretending to more precision than the data has would be
// inventing detail.
struct MotionField {
  int cols = 0;
  int rows = 0;
  int sourceWidth = 0;
  int sourceHeight = 0;
  std::vector<float> dx;          // cols*rows, source pixels
  std::vector<float> dy;
  std::uint64_t frameIndex = 0;
  bool empty() const { return cols <= 0 || rows <= 0 || dx.empty(); }
};

// Opens a file for motion extraction. Returns null when libav is not compiled
// in, the file will not open, or it carries no decodable video.
//
// `cellPixels` is the grid pitch in source pixels; 16 matches an H.264
// macroblock, which is the honest resolution of the data.
void* openMotionSource(const std::string& path, int cellPixels = 16);

// Advances one frame and fills `out`. Returns false at end of stream. A frame
// with no vectors (an I-frame) yields an EMPTY field rather than a failure --
// that is a normal condition, not an error, and the caller decides whether to
// hold the previous field or let the motion stop.
bool readMotionField(void* handle, MotionField& out);

// Back to the first frame, for a looping driver clip.
void rewindMotionSource(void* handle);

// Where the driver has got to, and how long it is.
//
// The driver is not a cue and never reaches the screen, so nothing else in the
// app knows anything about it -- which meant an operator arming one had no way
// to tell whether it was running, where it was, or whether the clip they picked
// was the one they meant. This is what the inspector shows.
struct MotionSourceStatus {
  double positionSeconds = 0.0;
  double durationSeconds = 0.0;   // 0 when the container will not say
  std::uint64_t frameIndex = 0;
  int thumbWidth = 0;             // 0 until a frame has been decoded
  int thumbHeight = 0;
};
bool motionSourceStatus(void* handle, MotionSourceStatus& out);

// The last decoded picture as a tiny luma thumbnail, or null before the first
// frame. Owned by the source and valid until the next read; the pixels come
// free because the decoder produced them on the way to the vectors.
const std::uint8_t* motionSourceThumbnail(void* handle);

// Jump the driver to a position in seconds. Falls back to a rewind when the
// container cannot seek.
void seekMotionSource(void* handle, double seconds);

void closeMotionSource(void* handle);

}  // namespace deckboy::motion

#endif  // DECKBOY_ENGINE_MOTION_FIELD_HPP
