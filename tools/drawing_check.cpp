// The Draw source's strokes against their pixels: where ink lands, how wide,
// erasing, shapes, the incremental raster against a full redraw, replay
// timing, the flipbook, and the show file.
//
//   deckboy-drawing-check
#include "core/drawing.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace deckboy::draw;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %-70s %s\n", what.c_str(), ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

static const int W = 320, H = 180;

static int alphaAt(const std::vector<std::uint8_t>& px, float fx, float fy) {
  const int x = std::min(W - 1, static_cast<int>(fx * W)), y = std::min(H - 1, static_cast<int>(fy * H));
  return px[static_cast<std::size_t>((y * W + x) * 4 + 3)];
}

static int redAt(const std::vector<std::uint8_t>& px, float fx, float fy) {
  const int x = std::min(W - 1, static_cast<int>(fx * W)), y = std::min(H - 1, static_cast<int>(fy * H));
  return px[static_cast<std::size_t>((y * W + x) * 4)];
}

static Stroke line(float x0, float y0, float x1, float y1, std::uint32_t rgba = 0xFF0000FFu, float size = 0.05f,
                   Tool tool = Tool::Brush, float t0 = 0.0f) {
  Stroke s;
  s.tool = tool;
  s.rgba = rgba;
  s.size = size;
  for (int i = 0; i <= 10; ++i) {
    const float u = i / 10.0f;
    s.points.push_back({x0 + (x1 - x0) * u, y0 + (y1 - y0) * u, 1.0f, t0 + u});
  }
  return s;
}

static std::vector<std::uint8_t> render(const Frame& f) {
  Raster r;
  r.update(f, f.strokes.size(), f.strokes.empty() ? 0 : f.strokes.back().points.size(), W, H, 1, 0);
  return r.pixels();
}

int main() {
  std::printf("check: drawing\n\n");
  {
    Frame f;
    f.strokes.push_back(line(0.1f, 0.5f, 0.9f, 0.5f));
    const auto px = render(f);
    check(alphaAt(px, 0.5f, 0.5f) == 255 && redAt(px, 0.5f, 0.5f) == 255, "a brush stroke puts ink on its path");
    check(alphaAt(px, 0.5f, 0.2f) == 0, "and none away from it");
    // 0.05 of 180 px is 9 px across: solid 3 px from the middle, clear at 7.
    check(alphaAt(px, 0.5f, 0.5f + 3.0f / H) == 255 && alphaAt(px, 0.5f, 0.5f + 7.0f / H) == 0,
          "a stroke is as wide as its size");
  }
  {
    Frame f;
    Stroke light = line(0.1f, 0.3f, 0.9f, 0.3f);
    for (auto& p : light.points) p.pressure = 0.0f;
    Stroke hard = line(0.1f, 0.7f, 0.9f, 0.7f);
    f.strokes = {light, hard};
    const auto px = render(f);
    check(alphaAt(px, 0.5f, 0.3f + 3.0f / H) == 0 && alphaAt(px, 0.5f, 0.7f + 3.0f / H) == 255,
          "a pen pressed lightly draws thinner");
  }
  {
    Frame f;
    Stroke half = line(0.1f, 0.5f, 0.9f, 0.5f);
    half.opacity = 0.5f;
    Stroke back = line(0.9f, 0.52f, 0.1f, 0.52f);   // over itself
    for (auto& p : back.points) half.points.push_back(p);
    f.strokes = {half};
    const auto px = render(f);
    const int a = alphaAt(px, 0.5f, 0.51f);
    check(a >= 120 && a <= 135, "a half-opaque stroke crossing itself stays half opaque (" + std::to_string(a) + ")");
  }
  {
    Frame f;
    f.strokes.push_back(line(0.1f, 0.5f, 0.9f, 0.5f));
    f.strokes.push_back(line(0.5f, 0.1f, 0.5f, 0.9f, 0xFFFFFFFFu, 0.1f, Tool::Erase));
    const auto px = render(f);
    check(alphaAt(px, 0.5f, 0.5f) == 0 && alphaAt(px, 0.3f, 0.5f) == 255, "the eraser takes ink away, only where it goes");
  }
  {
    Frame f;
    Stroke box;
    box.tool = Tool::Box;
    box.size = 0.02f;
    box.points = {{0.2f, 0.2f, 1, 0}, {0.8f, 0.8f, 1, 1}};
    f.strokes = {box};
    const auto px = render(f);
    check(alphaAt(px, 0.2f, 0.5f) == 255 && alphaAt(px, 0.5f, 0.5f) == 0, "a box is an outline, not a fill");
    Stroke oval = box;
    oval.tool = Tool::Oval;
    f.strokes = {oval};
    const auto po = render(f);
    check(alphaAt(po, 0.5f, 0.2f) == 255 && alphaAt(po, 0.2f, 0.2f) == 0, "an oval touches its box's edges, not its corners");
    Stroke fill;
    fill.tool = Tool::Fill;
    fill.rgba = 0x0000FFFFu;
    fill.points = {{0.5f, 0.5f, 1, 0}};
    f.strokes = {fill};
    const auto pf = render(f);
    check(alphaAt(pf, 0.01f, 0.01f) == 255 && alphaAt(pf, 0.99f, 0.99f) == 255, "fill covers the frame");
  }
  {
    // Incremental: points arriving one at a time must give the same pixels.
    Frame f;
    f.strokes.push_back(line(0.1f, 0.2f, 0.9f, 0.8f, 0x00FF00C0u));
    f.strokes.push_back(line(0.1f, 0.8f, 0.9f, 0.2f, 0xFF00FF80u));
    const auto whole = render(f);
    Raster r;
    std::uint64_t rev = 1;
    for (std::size_t s = 1; s <= f.strokes.size(); ++s) {
      for (std::size_t n = 1; n <= f.strokes[s - 1].points.size(); ++n) r.update(f, s, n, W, H, ++rev, 0);
    }
    check(r.pixels() == whole, "drawn a point at a time, the picture is exactly the whole redraw");
    Frame undo = f;
    undo.strokes.pop_back();
    r.update(undo, 1, undo.strokes[0].points.size(), W, H, ++rev, 0);
    check(r.pixels() == render(undo), "and taking a stroke back redraws without it");
  }
  {
    Drawing d;
    d.playback = Playback::Replay;
    d.holdSeconds = 1.0f;
    d.frames[0].strokes = {line(0.1f, 0.3f, 0.9f, 0.3f, 0xFF0000FFu, 0.05f, Tool::Brush, 0.0f),
                           line(0.1f, 0.7f, 0.9f, 0.7f, 0xFF0000FFu, 0.05f, Tool::Brush, 10.0f)};
    const Moment start = momentAt(d, 0.0);
    const Moment half = momentAt(d, 0.55);
    // Stroke 1 runs 0-1 s; the nine-second gap replays as 0.6 s, so stroke 2
    // runs 1.6-2.6 s; the hold runs to 3.6 s.
    const Moment second = momentAt(d, 1.7);
    check(start.strokes == 1 && start.lastPoints == 1, "replay starts at the first point");
    check(half.strokes == 1 && half.lastPoints == 6, "half way through the first stroke shows half of it");
    check(second.strokes == 2, "a nine-second pause between strokes replays as a short one");
    const Moment held = momentAt(d, 3.0);
    check(held.strokes == 2 && held.lastPoints == 11, "then the drawing holds whole");
    const Moment again = momentAt(d, 3.65);
    check(again.strokes == 1, "and draws again from the start");
  }
  {
    Drawing d;
    d.playback = Playback::Flipbook;
    d.fps = 4.0f;
    d.frames.resize(3);
    check(momentAt(d, 0.1).frame == 0 && momentAt(d, 0.3).frame == 1 && momentAt(d, 0.8).frame == 0,
          "the flipbook turns its frames at its rate, round and round");
    d.playback = Playback::Still;
    d.editFrame = 2;
    check(momentAt(d, 5.0).frame == 2, "still holds the frame being edited");
  }
  {
    Drawing d;
    d.playback = Playback::Flipbook;
    d.fps = 12.0f;
    d.frames.resize(2);
    d.frames[0].strokes = {line(0.1f, 0.2f, 0.3f, 0.4f, 0x3080F0C8u, 0.013f)};
    d.frames[0].strokes[0].points[3].pressure = 0.42f;
    d.frames[1].strokes = {line(0.5f, 0.5f, 0.6f, 0.6f, 0xFFFFFFFFu, 0.1f, Tool::Erase, 3.0f)};
    const std::string text = serialize(d);
    Drawing back;
    const bool ok = parse(text, back);
    check(ok && back.playback == Playback::Flipbook && back.fps == 12.0f && back.frames.size() == 2,
          "the show file keeps the playback and the frames");
    const auto& s = back.frames[0].strokes[0];
    check(s.rgba == 0x3080F0C8u && std::abs(s.size - 0.013f) < 1e-5f && s.points.size() == 11 &&
            std::abs(s.points[3].pressure - 0.42f) < 0.006f && std::abs(s.points[10].x - 0.3f) < 1e-4f,
          "and every stroke's colour, size, points and pressure");
    check(back.frames[1].strokes[0].tool == Tool::Erase && std::abs(back.frames[1].strokes[0].points[10].t - 4.0f) < 0.011f,
          "and the tool and the timing");
    check(text.find('\t') == std::string::npos && text.find('\n') == std::string::npos,
          "and fits in one field of a cue line");
    Drawing junk;
    check(!parse("banana", junk) && junk.frames.size() == 1, "text that is not a drawing is refused");
  }

  if (failures) {
    std::printf("\ndrawing: %d FAILED\n", failures);
    return 1;
  }
  std::printf("\ndrawing: all cases pass\n");
  return 0;
}
