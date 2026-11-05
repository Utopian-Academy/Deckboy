// Motion against made-up frames: a white box moving over black.
//
//   deckboy-motion-check
#include "core/motion.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace deckboy::motion;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %-70s %s\n", what.c_str(), ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

// A 640x360 RGBA frame, black, with a white box whose left edge is at bx.
static std::vector<std::uint8_t> frame(int bx, int by = 140, int size = 80, std::uint8_t noise = 0,
                                       unsigned seed = 1) {
  const int w = 640, h = 360;
  std::vector<std::uint8_t> px(static_cast<std::size_t>(w * h * 4), 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      std::uint8_t v = (x >= bx && x < bx + size && y >= by && y < by + size) ? 255 : 0;
      if (noise) {
        seed = seed * 1103515245u + 12345u;
        v = static_cast<std::uint8_t>(std::min(255, v + static_cast<int>((seed >> 16) % noise)));
      }
      std::uint8_t* p = &px[static_cast<std::size_t>((y * w + x) * 4)];
      p[0] = p[1] = p[2] = v;
      p[3] = 255;
    }
  }
  return px;
}

static void feed(Analyzer& a, const std::vector<std::uint8_t>& px, const Settings& s, double dt = 1.0 / 30) {
  a.feed(px.data(), 640, 360, 640 * 4, PixelLayout::Rgba, dt, s);
}

int main() {
  std::printf("check: motion\n\n");
  Settings s;
  s.mirror = false;
  s.riseSeconds = 0.0f;   // unsmoothed, so one frame tells
  s.fallSeconds = 0.0f;

  {
    Analyzer a;
    feed(a, frame(100), s);
    feed(a, frame(100), s);
    check(a.reading(0).amount == 0.0f, "a still picture reads no motion");
    feed(a, frame(140), s);
    check(a.reading(0).amount > 0.0f, "a box moving reads as motion");
    const float half = a.reading(0).amount;
    feed(a, frame(400), s);
    check(a.reading(0).amount > half, "a box jumping further reads as more motion");
  }
  {
    Analyzer a;
    feed(a, frame(60), s);
    feed(a, frame(80), s);
    const float left = a.reading(0).x;
    feed(a, frame(480), s);
    feed(a, frame(500), s);
    const float right = a.reading(0).x;
    check(left < 0.3f && right > 0.7f, "x follows the box: left " + std::to_string(left) +
                                       ", right " + std::to_string(right));
    feed(a, frame(500), s);
    check(a.reading(0).x == right, "when it stops, x holds where it was");
  }
  {
    Settings m = s;
    m.mirror = true;
    Analyzer a;
    feed(a, frame(60), m);
    feed(a, frame(80), m);
    check(a.reading(0).x > 0.7f, "mirrored, a box on the left reads on the right");
  }
  {
    Settings z = s;
    z.zones = {{0.0f, 0.0f, 0.5f, 1.0f}, {0.5f, 0.0f, 0.5f, 1.0f}};
    Analyzer a;
    feed(a, frame(60), z);
    feed(a, frame(90), z);
    check(a.reading(1).amount > 0.0f && a.reading(2).amount == 0.0f,
          "zones: movement on the left is zone 1's, not zone 2's");
    check(a.reading(1).amount > a.reading(0).amount, "zones: a zone reads fuller than the whole picture");
    check(a.reading(3).amount == 0.0f && a.zoneCount() == 2, "a zone that does not exist reads nothing");
  }
  {
    Analyzer a;
    feed(a, frame(100, 140, 80, 20, 7), s);
    feed(a, frame(100, 140, 80, 20, 99), s);
    check(a.reading(0).amount == 0.0f, "camera noise under the sensitivity is ignored");
  }
  {
    Settings slow = s;
    slow.riseSeconds = 0.5f;
    slow.fallSeconds = 1.0f;
    Analyzer a;
    feed(a, frame(100), slow);
    feed(a, frame(400), slow);
    const float first = a.reading(0).amount;
    feed(a, frame(100), slow);
    check(first > 0.0f && a.reading(0).amount > first, "smoothed: it rises over several frames");
    float last = a.reading(0).amount;
    feed(a, frame(100), slow);
    check(a.reading(0).amount < last && a.reading(0).amount > 0.0f, "smoothed: it falls gradually when still");
  }
  {
    std::vector<std::uint8_t> grey(640 * 360, 0);
    Analyzer a;
    a.feed(grey.data(), 640, 360, 640, PixelLayout::Grey, 1.0 / 30, s);
    for (int y = 100; y < 200; ++y)
      for (int x = 300; x < 400; ++x) grey[static_cast<std::size_t>(y * 640 + x)] = 255;
    a.feed(grey.data(), 640, 360, 640, PixelLayout::Grey, 1.0 / 30, s);
    check(a.reading(0).amount > 0.0f, "a grey (Y plane) frame works too");
  }
  {
    const std::vector<Zone> zones {{0.1f, 0.2f, 0.3f, 0.4f}, {0.5f, 0.0f, 0.5f, 1.0f}};
    const auto back = zonesFromText(zonesToText(zones));
    check(back.size() == 2 && std::abs(back[0].y - 0.2f) < 1e-4f && std::abs(back[1].w - 0.5f) < 1e-4f,
          "zones round-trip through the show file's text");
    Settings big;
    big.zones.resize(12);
    check(clampSettings(big).zones.size() == static_cast<std::size_t>(kMaxZones), "no more than 8 zones");
  }

  if (failures) {
    std::printf("\nmotion: %d FAILED\n", failures);
    return 1;
  }
  std::printf("\nmotion: all cases pass\n");
  return 0;
}
