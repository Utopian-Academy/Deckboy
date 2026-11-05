#pragma once
// MOTION: how much is moving in a camera's picture, and where.
//
// James, 2026-11-04 (board DGrGWwFS, "I say build it"), after his old Max
// patch: grab, grey, difference against the last frame, threshold, smooth,
// count. Here the count becomes three numbers per ZONE -- how much moved,
// and the centre of what moved across and down -- and any control with an
// oscillator can follow them (LfoShape::Motion, MotionX, MotionY).
//
// SDL-free, so tools/motion_check.cpp can drive it with made-up frames.
// Works on a small grey copy (kGridW x kGridH): a hand waving reads the same
// at that size, and it costs well under a millisecond.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace deckboy::motion {

constexpr int kGridW = 160;
constexpr int kGridH = 90;
constexpr int kMaxZones = 8;
// Reading 0 is the whole picture; 1..kMaxZones are the zones.
constexpr int kReadings = kMaxZones + 1;

// A rectangle in fractions of the picture, as seen (after the mirror).
struct Zone {
  float x = 0.0f;
  float y = 0.0f;
  float w = 1.0f;
  float h = 1.0f;
};

struct Settings {
  // How different a pixel must be from the last frame to count as moving,
  // as a fraction of full scale. Low catches a finger, high only an arm.
  float sensitivity = 0.12f;
  // Seconds to rise and to fall most of the way (a one-pole smoother each
  // way): fast up, slow down, like the patch's slide 2 / 10, so a gesture
  // lands at once and lets go gently.
  float riseSeconds = 0.05f;
  float fallSeconds = 0.40f;
  // Flip left and right, so moving your right hand moves the right side of
  // the reading, as in a mirror. On by default: a webcam faces you.
  bool mirror = true;
  std::vector<Zone> zones;
};

// The share of a zone that has to be moving to read as full. A hand waving
// in front of a webcam changes about a twentieth of the picture from one
// frame to the next, so a twentieth is "all of it"; a whole arm saturates.
constexpr float kFullScale = 0.05f;

struct Reading {
  float amount = 0.0f;   // 0 still .. 1 a lot moving
  float x = 0.5f;        // 0 left .. 1 right, where the movement is centred
  float y = 0.5f;        // 0 top .. 1 bottom
};

inline Settings clampSettings(Settings s) {
  s.sensitivity = std::clamp(s.sensitivity, 0.01f, 0.9f);
  s.riseSeconds = std::clamp(s.riseSeconds, 0.0f, 5.0f);
  s.fallSeconds = std::clamp(s.fallSeconds, 0.0f, 10.0f);
  if (s.zones.size() > static_cast<std::size_t>(kMaxZones)) s.zones.resize(kMaxZones);
  for (Zone& z : s.zones) {
    z.x = std::clamp(z.x, 0.0f, 1.0f);
    z.y = std::clamp(z.y, 0.0f, 1.0f);
    z.w = std::clamp(z.w, 0.01f, 1.0f - z.x);
    z.h = std::clamp(z.h, 0.01f, 1.0f - z.y);
  }
  return s;
}

// The pixel layouts a camera or a playlist hands over.
enum class PixelLayout { Rgba, Grey };

class Analyzer {
 public:
  // One frame. `pixels` is `height` rows of `stride` bytes; RGBA is 4 bytes a
  // pixel, GREY one (the Y plane of NV12 or P010's high bytes is exactly that).
  // `dt` is the seconds since the last frame, for the smoothing. The first
  // frame after a reset only primes the comparison.
  void feed(const std::uint8_t* pixels, int width, int height, int stride, PixelLayout layout,
            double dt, const Settings& settings) {
    if (!pixels || width <= 0 || height <= 0 || stride <= 0) return;
    shrink(pixels, width, height, stride, layout, settings.mirror);
    if (!primed_) {
      previous_ = current_;
      primed_ = true;
      return;
    }
    const int threshold = static_cast<int>(std::lround(std::clamp(settings.sensitivity, 0.01f, 0.9f) * 255.0f));
    for (int i = 0; i < kGridW * kGridH; ++i) {
      const int d = std::abs(static_cast<int>(current_[static_cast<std::size_t>(i)]) -
                             static_cast<int>(previous_[static_cast<std::size_t>(i)]));
      moved_[static_cast<std::size_t>(i)] = d > threshold ? 255 : 0;
    }
    previous_ = current_;

    std::array<Reading, kReadings> raw {};
    std::array<bool, kReadings> seen {};
    measure(0.0f, 0.0f, 1.0f, 1.0f, raw[0], seen[0]);
    for (std::size_t z = 0; z < settings.zones.size() && z < static_cast<std::size_t>(kMaxZones); ++z) {
      const Zone& zone = settings.zones[z];
      measure(zone.x, zone.y, zone.w, zone.h, raw[z + 1], seen[z + 1]);
    }
    zoneCount_ = static_cast<int>(std::min<std::size_t>(settings.zones.size(), kMaxZones));

    // Smooth each reading. Where nothing moved, the position HOLDS: a hand
    // that stops should leave the pitch where it was, not snap it to the
    // middle.
    const double rise = smoothing(dt, settings.riseSeconds);
    const double fall = smoothing(dt, settings.fallSeconds);
    for (int r = 0; r < kReadings; ++r) {
      Reading& out = readings_[static_cast<std::size_t>(r)];
      const Reading& in = raw[static_cast<std::size_t>(r)];
      if (r > zoneCount_) {
        out = Reading {};
        continue;
      }
      const double k = in.amount > out.amount ? rise : fall;
      out.amount = static_cast<float>(out.amount + (in.amount - out.amount) * k);
      if (seen[static_cast<std::size_t>(r)]) {
        out.x = static_cast<float>(out.x + (in.x - out.x) * rise);
        out.y = static_cast<float>(out.y + (in.y - out.y) * rise);
      }
    }
  }

  // Forget the last frame and every reading: a new camera, or Motion off.
  void reset() {
    primed_ = false;
    readings_ = {};
    std::fill(moved_.begin(), moved_.end(), std::uint8_t {0});
  }

  const Reading& reading(int index) const {
    return readings_[static_cast<std::size_t>(std::clamp(index, 0, kReadings - 1))];
  }
  int zoneCount() const { return zoneCount_; }
  // What moved in the last frame, white on black, kGridW x kGridH: the
  // picture the settings page shows.
  const std::array<std::uint8_t, kGridW * kGridH>& moved() const { return moved_; }

 private:
  static double smoothing(double dt, float seconds) {
    if (seconds <= 0.0f || dt <= 0.0) return 1.0;
    // The share of the way to go this frame so that 'seconds' covers ~95%.
    return 1.0 - std::exp(-3.0 * dt / static_cast<double>(seconds));
  }

  // Box-average the frame down to the grid, in grey.
  void shrink(const std::uint8_t* pixels, int width, int height, int stride, PixelLayout layout, bool mirror) {
    const int bytes = layout == PixelLayout::Rgba ? 4 : 1;
    for (int gy = 0; gy < kGridH; ++gy) {
      const int y0 = gy * height / kGridH;
      const int y1 = std::max(y0 + 1, (gy + 1) * height / kGridH);
      for (int gx = 0; gx < kGridW; ++gx) {
        const int x0 = gx * width / kGridW;
        const int x1 = std::max(x0 + 1, (gx + 1) * width / kGridW);
        // Sample at most 4 x 4 per cell: enough to average out noise on a
        // 4K frame without reading every pixel of it.
        const int sx = std::max(1, (x1 - x0) / 4);
        const int sy = std::max(1, (y1 - y0) / 4);
        unsigned sum = 0;
        unsigned count = 0;
        for (int y = y0; y < y1 && y < height; y += sy) {
          const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
          for (int x = x0; x < x1 && x < width; x += sx) {
            const std::uint8_t* p = row + static_cast<std::size_t>(x) * bytes;
            // Rec. 601-ish luma in integers; exactness does not matter here.
            sum += bytes == 4 ? (p[0] * 77u + p[1] * 150u + p[2] * 29u) >> 8 : p[0];
            ++count;
          }
        }
        const int cell = mirror ? (kGridW - 1 - gx) : gx;
        current_[static_cast<std::size_t>(gy * kGridW + cell)] =
          static_cast<std::uint8_t>(count ? sum / count : 0);
      }
    }
  }

  void measure(float fx, float fy, float fw, float fh, Reading& out, bool& seen) const {
    const int x0 = std::clamp(static_cast<int>(std::floor(fx * kGridW)), 0, kGridW - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(fy * kGridH)), 0, kGridH - 1);
    const int x1 = std::clamp(static_cast<int>(std::ceil((fx + fw) * kGridW)), x0 + 1, kGridW);
    const int y1 = std::clamp(static_cast<int>(std::ceil((fy + fh) * kGridH)), y0 + 1, kGridH);
    double count = 0.0, sx = 0.0, sy = 0.0;
    for (int y = y0; y < y1; ++y) {
      for (int x = x0; x < x1; ++x) {
        if (moved_[static_cast<std::size_t>(y * kGridW + x)]) {
          count += 1.0;
          sx += x + 0.5;
          sy += y + 0.5;
        }
      }
    }
    const double cells = static_cast<double>((x1 - x0) * (y1 - y0));
    out.amount = static_cast<float>(std::min(1.0, count / cells / kFullScale));
    // A few scattered pixels are noise, not a position.
    seen = count >= 3.0;
    if (seen) {
      out.x = static_cast<float>(((sx / count) - x0) / (x1 - x0));
      out.y = static_cast<float>(((sy / count) - y0) / (y1 - y0));
    }
  }

  std::array<std::uint8_t, kGridW * kGridH> current_ {};
  std::array<std::uint8_t, kGridW * kGridH> previous_ {};
  std::array<std::uint8_t, kGridW * kGridH> moved_ {};
  std::array<Reading, kReadings> readings_ {};
  int zoneCount_ = 0;
  bool primed_ = false;
};

// The show file's zone list: "x,y,w,h;x,y,w,h", fractions.
inline std::string zonesToText(const std::vector<Zone>& zones) {
  std::string out;
  char buf[96];
  for (std::size_t i = 0; i < zones.size(); ++i) {
    std::snprintf(buf, sizeof buf, "%s%.4f,%.4f,%.4f,%.4f", i ? ";" : "",
                  zones[i].x, zones[i].y, zones[i].w, zones[i].h);
    out += buf;
  }
  return out;
}

inline std::vector<Zone> zonesFromText(const std::string& text) {
  std::vector<Zone> zones;
  std::size_t at = 0;
  while (at < text.size() && zones.size() < static_cast<std::size_t>(kMaxZones)) {
    const std::size_t end = std::min(text.find(';', at), text.size());
    // Four numbers split by commas; a part that is not four numbers is skipped.
    const std::string part = text.substr(at, end - at);
    float v[4] = {0, 0, 0, 0};
    const char* p = part.c_str();
    int got = 0;
    for (; got < 4; ++got) {
      char* stop = nullptr;
      v[got] = std::strtof(p, &stop);
      if (stop == p) break;
      p = stop;
      if (*p == ',') ++p;
    }
    if (got == 4) zones.push_back(Zone {v[0], v[1], v[2], v[3]});
    at = end + 1;
  }
  return zones;
}

}  // namespace deckboy::motion
