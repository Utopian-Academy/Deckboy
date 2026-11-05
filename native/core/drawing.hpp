#pragma once
// THE DRAW SOURCE: what was drawn, as strokes, and how it becomes pixels.
//
// James, 2026-11-04 (board X2N9qJe1, approved): "a source which gives you
// tools and a pallete to draw on the output, and save what you drew as
// stills or even possibly animations" -- and, asked which animation, "both":
// a FLIPBOOK (frame by frame, onion skin) and STROKE REPLAY (the drawing
// plays back the way it was drawn, like a telestrator).
//
// Kept as STROKES, not pixels: a stroke is its points (where, how hard the
// pen pressed, when), so the drawing is sharp at any raster, replay knows
// the order and timing, and the show file holds a few kilobytes rather than
// a picture per frame. Positions are fractions of the picture; a brush size
// is a fraction of the picture's height, so a line looks the same at 1080p
// and 4K.
//
// SDL-free, so tools/drawing_check.cpp can test it alone.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace deckboy::draw {

enum class Tool { Brush = 0, Erase, Line, Box, Oval, Fill, Count };

inline const char* toolToken(Tool t) {
  switch (t) {
    case Tool::Erase: return "erase";
    case Tool::Line:  return "line";
    case Tool::Box:   return "box";
    case Tool::Oval:  return "oval";
    case Tool::Fill:  return "fill";
    default:          return "brush";
  }
}

inline const char* toolLabel(Tool t) {
  switch (t) {
    case Tool::Erase: return "eraser";
    case Tool::Line:  return "line";
    case Tool::Box:   return "box";
    case Tool::Oval:  return "oval";
    case Tool::Fill:  return "fill the frame";
    default:          return "brush";
  }
}

inline bool toolFromToken(const std::string& s, Tool& out) {
  for (int i = 0; i < static_cast<int>(Tool::Count); ++i) {
    if (s == toolToken(static_cast<Tool>(i))) {
      out = static_cast<Tool>(i);
      return true;
    }
  }
  return false;
}

struct Point {
  float x = 0.0f;         // 0-1 across the picture
  float y = 0.0f;         // 0-1 down
  float pressure = 1.0f;  // 0-1; a mouse is always 1
  float t = 0.0f;         // seconds since the drawing's clock started, for replay
};

struct Stroke {
  Tool tool = Tool::Brush;
  std::uint32_t rgba = 0xE03030FFu;   // 0xRRGGBBAA
  float size = 0.012f;                // diameter, as a fraction of the picture's height
  float opacity = 1.0f;
  std::vector<Point> points;
};

struct Frame {
  std::vector<Stroke> strokes;
};

// How the cue plays what was drawn.
//   Still     the frame being edited, held
//   Flipbook  every frame in turn at `fps`, round and round
//   Replay    the first frame, stroke by stroke as it was drawn, then held
//             for `holdSeconds` and drawn again from nothing
enum class Playback { Still = 0, Flipbook, Replay, Count };

inline const char* playbackToken(Playback p) {
  switch (p) {
    case Playback::Flipbook: return "flipbook";
    case Playback::Replay:   return "replay";
    default:                 return "still";
  }
}

inline bool playbackFromToken(const std::string& s, Playback& out) {
  for (int i = 0; i < static_cast<int>(Playback::Count); ++i) {
    if (s == playbackToken(static_cast<Playback>(i))) {
      out = static_cast<Playback>(i);
      return true;
    }
  }
  return false;
}

struct Drawing {
  std::vector<Frame> frames {Frame {}};
  Playback playback = Playback::Still;
  float fps = 8.0f;
  float holdSeconds = 2.0f;
  int editFrame = 0;          // the frame being drawn on, and what Still shows
  // Bumped on every change, so a cached raster knows when to look again.
  // Not saved.
  std::uint64_t revision = 0;
};

// A gap between strokes longer than this replays as this: the pause while
// someone picked a colour is not part of the drawing.
constexpr float kReplayMaxGap = 0.6f;

// When each point of a replay is reached, with long gaps squeezed out.
// Returns the replay's length in seconds (before the hold).
inline float replayTimes(const Frame& frame, std::vector<std::vector<float>>& out) {
  out.clear();
  float clock = 0.0f;
  float last = -1.0f;
  for (const Stroke& s : frame.strokes) {
    std::vector<float> times;
    times.reserve(s.points.size());
    for (const Point& p : s.points) {
      if (last >= 0.0f) clock += std::clamp(p.t - last, 0.0f, kReplayMaxGap);
      last = p.t;
      times.push_back(clock);
    }
    out.push_back(std::move(times));
  }
  return clock;
}

// ── PIXELS ──────────────────────────────────────────────────────────────────
//
// Straight (not premultiplied) RGBA, 4 bytes a pixel, the layout every CPU
// path in Deckboy uses. A stroke is first drawn into a COVERAGE buffer (the
// most any segment covered each pixel), then laid on once: a half-opaque
// stroke that crosses itself stays half opaque, as ink does, instead of
// darkening at every joint.

namespace detail {

inline float segmentDistance(float px, float py, float ax, float ay, float bx, float by, float& along) {
  const float dx = bx - ax, dy = by - ay;
  const float len2 = dx * dx + dy * dy;
  along = len2 > 1e-12f ? std::clamp(((px - ax) * dx + (py - ay) * dy) / len2, 0.0f, 1.0f) : 0.0f;
  const float cx = ax + dx * along - px, cy = ay + dy * along - py;
  return std::sqrt(cx * cx + cy * cy);
}

// A round-ended segment whose radius runs from ra to rb, into the coverage
// buffer (max), and the pixels it touched into the dirty box.
inline void capsule(std::vector<float>& cov, int w, int h, float ax, float ay, float ra, float bx, float by,
                    float rb, int& x0, int& y0, int& x1, int& y1) {
  const float r = std::max(ra, rb) + 1.0f;
  const int minX = std::max(0, static_cast<int>(std::floor(std::min(ax, bx) - r)));
  const int maxX = std::min(w - 1, static_cast<int>(std::ceil(std::max(ax, bx) + r)));
  const int minY = std::max(0, static_cast<int>(std::floor(std::min(ay, by) - r)));
  const int maxY = std::min(h - 1, static_cast<int>(std::ceil(std::max(ay, by) + r)));
  if (minX > maxX || minY > maxY) return;
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      float along = 0.0f;
      const float d = segmentDistance(x + 0.5f, y + 0.5f, ax, ay, bx, by, along);
      const float rad = ra + (rb - ra) * along;
      const float c = std::clamp(rad + 0.5f - d, 0.0f, 1.0f);
      float& slot = cov[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
      if (c > slot) slot = c;
    }
  }
  x0 = std::min(x0, minX);
  y0 = std::min(y0, minY);
  x1 = std::max(x1, maxX);
  y1 = std::max(y1, maxY);
}

}  // namespace detail

// The radius in pixels of a stroke at a point's pressure. A pen pressed
// lightly draws thinner, but never to nothing.
inline float radiusAt(const Stroke& s, const Point& p, int h) {
  const float pressure = std::clamp(p.pressure, 0.0f, 1.0f);
  return std::max(0.5f, s.size * static_cast<float>(h) * 0.5f * (0.25f + 0.75f * pressure));
}

// Paint into `cov` the part of a stroke from point `from` (exclusive of what
// is already there) up to point `to` (exclusive). A shape (line, box, oval)
// is drawn whole from its first and last points, once `to` reaches its end.
// Returns false when nothing was touched; the dirty box is widened.
inline bool coverStroke(const Stroke& s, std::size_t from, std::size_t to, std::vector<float>& cov, int w, int h,
                        int& x0, int& y0, int& x1, int& y1) {
  const std::size_t n = std::min(to, s.points.size());
  if (n == 0 || w <= 0 || h <= 0) return false;
  auto px = [&](const Point& p) { return p.x * static_cast<float>(w); };
  auto py = [&](const Point& p) { return p.y * static_cast<float>(h); };
  switch (s.tool) {
    case Tool::Fill:
      if (from > 0) return false;
      std::fill(cov.begin(), cov.end(), 1.0f);
      x0 = 0; y0 = 0; x1 = w - 1; y1 = h - 1;
      return true;
    case Tool::Brush:
    case Tool::Erase: {
      if (n == 1 || from == 0) {
        const Point& p = s.points[0];
        const float r = radiusAt(s, p, h);
        detail::capsule(cov, w, h, px(p), py(p), r, px(p), py(p), r, x0, y0, x1, y1);
      }
      for (std::size_t i = std::max<std::size_t>(from, 1); i < n; ++i) {
        const Point& a = s.points[i - 1];
        const Point& b = s.points[i];
        detail::capsule(cov, w, h, px(a), py(a), radiusAt(s, a, h), px(b), py(b), radiusAt(s, b, h), x0, y0, x1, y1);
      }
      break;
    }
    default: {
      // A shape is two points, where the drag began and where it ended,
      // stored when the drag ends (the editor outlines it until then). It is
      // drawn whole once both are reached.
      if (n < s.points.size() || from >= n || n < 2) return false;
      const Point& a = s.points.front();
      const Point& b = s.points.back();
      const float r = radiusAt(s, Point {}, h);
      const float ax = px(a), ay = py(a), bx = px(b), by = py(b);
      auto seg = [&](float x_a, float y_a, float x_b, float y_b) {
        detail::capsule(cov, w, h, x_a, y_a, r, x_b, y_b, r, x0, y0, x1, y1);
      };
      if (s.tool == Tool::Line) {
        seg(ax, ay, bx, by);
      } else if (s.tool == Tool::Box) {
        seg(ax, ay, bx, ay);
        seg(bx, ay, bx, by);
        seg(bx, by, ax, by);
        seg(ax, by, ax, ay);
      } else {
        const float cx = (ax + bx) * 0.5f, cy = (ay + by) * 0.5f;
        const float rx = std::abs(bx - ax) * 0.5f, ry = std::abs(by - ay) * 0.5f;
        const int steps = std::clamp(static_cast<int>((rx + ry) * 0.5f), 24, 256);
        for (int i = 0; i < steps; ++i) {
          const float t0 = 6.2831853f * i / steps, t1 = 6.2831853f * (i + 1) / steps;
          seg(cx + rx * std::cos(t0), cy + ry * std::sin(t0), cx + rx * std::cos(t1), cy + ry * std::sin(t1));
        }
      }
      break;
    }
  }
  return x1 >= x0;
}

// Lay a stroke's coverage, inside the box, over `base` into `out`. The
// stroke's colour goes OVER (straight alpha); the eraser takes alpha away.
inline void composite(const Stroke& s, const std::vector<float>& cov, const std::uint8_t* base, std::uint8_t* out,
                      int w, int x0, int y0, int x1, int y1) {
  const float r = static_cast<float>((s.rgba >> 24) & 0xFF);
  const float g = static_cast<float>((s.rgba >> 16) & 0xFF);
  const float b = static_cast<float>((s.rgba >> 8) & 0xFF);
  const float a = static_cast<float>(s.rgba & 0xFF) / 255.0f * std::clamp(s.opacity, 0.0f, 1.0f);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x);
      const float c = cov[i] * a;
      const std::uint8_t* src = base + i * 4;
      std::uint8_t* dst = out + i * 4;
      if (c <= 0.0f) {
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = src[3];
        continue;
      }
      const float da = src[3] / 255.0f;
      if (s.tool == Tool::Erase) {
        dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
        dst[3] = static_cast<std::uint8_t>(std::lround(255.0f * da * (1.0f - c)));
        continue;
      }
      const float oa = c + da * (1.0f - c);
      auto mix = [&](float ink, std::uint8_t under) {
        return static_cast<std::uint8_t>(std::lround(std::clamp((ink * c + under * da * (1.0f - c)) / oa, 0.0f, 255.0f)));
      };
      dst[0] = mix(r, src[0]);
      dst[1] = mix(g, src[1]);
      dst[2] = mix(b, src[2]);
      dst[3] = static_cast<std::uint8_t>(std::lround(oa * 255.0f));
    }
  }
}

// ── A RASTER THAT KEEPS UP ────────────────────────────────────────────────
//
// What the cue shows, built incrementally: every stroke but the newest is
// baked into `base`; the newest is held as coverage, so a point added to it
// costs only the pixels near that point. Anything else -- a different frame,
// an undo, a new raster size -- starts again from nothing.
class Raster {
 public:
  // Make `pixels` show strokes [0, strokeCount) of `frame`, the last of them
  // only up to `lastPoints` points. Returns true when the pixels changed.
  bool update(const Frame& frame, std::size_t strokeCount, std::size_t lastPoints, int w, int h,
              std::uint64_t revision, int frameIndex) {
    strokeCount = std::min(strokeCount, frame.strokes.size());
    if (w <= 0 || h <= 0) return false;
    const bool sameRaster = w == w_ && h == h_ && frameIndex == frameIndex_;
    // Can it carry on from where it was? Only forward, on the same strokes.
    bool extend = sameRaster && strokeCount >= strokes_ && strokes_ > 0 &&
                  (strokeCount > strokes_ || lastPoints >= points_);
    if (sameRaster && strokeCount == strokes_ && lastPoints == points_ && revision == revision_) return false;
    if (extend && revision != revision_) {
      // The strokes may have changed under us; carry on only if the one we
      // were growing still starts the same way.
      const Stroke& growing = frame.strokes[strokes_ - 1];
      extend = growing.points.size() >= points_ && points_ > 0 &&
               growing.points[points_ - 1].x == lastX_ && growing.points[points_ - 1].y == lastY_ &&
               growing.tool == lastTool_ && growing.rgba == lastRgba_;
    }
    if (!extend) {
      w_ = w;
      h_ = h;
      frameIndex_ = frameIndex;
      pixels_.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, 0);
      base_ = pixels_;
      cov_.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0.0f);
      strokes_ = 0;
      points_ = 0;
    }
    // Finish the stroke in hand, then each later one, baking each finished
    // stroke into the base.
    while (true) {
      if (strokes_ == 0) {
        if (strokeCount == 0) break;
        strokes_ = 1;
        points_ = 0;
        std::fill(cov_.begin(), cov_.end(), 0.0f);
      }
      const Stroke& s = frame.strokes[strokes_ - 1];
      const bool last = strokes_ == strokeCount;
      const std::size_t want = last ? std::min(lastPoints, s.points.size()) : s.points.size();
      int x0 = w, y0 = h, x1 = -1, y1 = -1;
      if (want > points_ && coverStroke(s, points_, want, cov_, w, h, x0, y0, x1, y1) && x1 >= x0) {
        composite(s, cov_, base_.data(), pixels_.data(), w, x0, y0, x1, y1);
      }
      points_ = std::max(points_, want);
      if (last) break;
      // Bake: the base becomes what is shown, and the next stroke starts clean.
      base_ = pixels_;
      std::fill(cov_.begin(), cov_.end(), 0.0f);
      ++strokes_;
      points_ = 0;
    }
    revision_ = revision;
    if (strokes_ > 0 && strokes_ <= frame.strokes.size()) {
      const Stroke& s = frame.strokes[strokes_ - 1];
      lastTool_ = s.tool;
      lastRgba_ = s.rgba;
      if (points_ > 0 && points_ <= s.points.size()) {
        lastX_ = s.points[points_ - 1].x;
        lastY_ = s.points[points_ - 1].y;
      }
    }
    return true;
  }

  const std::vector<std::uint8_t>& pixels() const { return pixels_; }
  int width() const { return w_; }
  int height() const { return h_; }

 private:
  std::vector<std::uint8_t> pixels_, base_;
  std::vector<float> cov_;
  int w_ = 0, h_ = 0, frameIndex_ = -1;
  std::size_t strokes_ = 0, points_ = 0;
  std::uint64_t revision_ = 0;
  float lastX_ = 0.0f, lastY_ = 0.0f;
  Tool lastTool_ = Tool::Brush;
  std::uint32_t lastRgba_ = 0;
};

// What a drawing shows at `seconds` into its cue: which frame, how many
// strokes, and how far into the last. For Still and Flipbook every stroke is
// whole.
struct Moment {
  int frame = 0;
  std::size_t strokes = 0;
  std::size_t lastPoints = 0;
};

inline Moment momentAt(const Drawing& d, double seconds) {
  Moment m;
  if (d.frames.empty()) return m;
  const int frames = static_cast<int>(d.frames.size());
  if (d.playback == Playback::Flipbook && frames > 1) {
    const double fps = std::clamp(static_cast<double>(d.fps), 1.0, 60.0);
    m.frame = static_cast<int>(std::floor(std::max(0.0, seconds) * fps)) % frames;
  } else if (d.playback == Playback::Replay) {
    m.frame = 0;
  } else {
    m.frame = std::clamp(d.editFrame, 0, frames - 1);
  }
  const Frame& f = d.frames[static_cast<std::size_t>(m.frame)];
  m.strokes = f.strokes.size();
  m.lastPoints = f.strokes.empty() ? 0 : f.strokes.back().points.size();
  if (d.playback != Playback::Replay || f.strokes.empty()) return m;
  std::vector<std::vector<float>> times;
  const float length = replayTimes(f, times);
  const double cycle = length + std::max(0.0f, d.holdSeconds);
  if (cycle <= 0.0) return m;
  const double t = std::fmod(std::max(0.0, seconds), cycle);
  // The strokes reached so far; the last of them as far as its points go.
  m.strokes = 0;
  m.lastPoints = 0;
  for (std::size_t s = 0; s < times.size(); ++s) {
    if (times[s].empty() || times[s].front() > t) break;
    m.strokes = s + 1;
    std::size_t n = 0;
    while (n < times[s].size() && times[s][n] <= t) ++n;
    m.lastPoints = n;
  }
  return m;
}

// ── THE SHOW FILE ─────────────────────────────────────────────────────────
//
//   v1|playback|fps|hold|editFrame|frame#frame...
//   frame:  stroke;stroke...
//   stroke: tool,rrggbbaa,size,opacity:x y p t,x y p t,...
// Positions in ten-thousandths, pressure in hundredths, time in
// hundredths of a second, so a point is a dozen characters. No '|' '\t' or
// newline anywhere inside, so it sits in one field of a cue line.

inline std::string serialize(const Drawing& d) {
  std::string out = "v1|";
  out += playbackToken(d.playback);
  char buf[64];
  std::snprintf(buf, sizeof buf, "|%.3g|%.3g|%d|", static_cast<double>(d.fps), static_cast<double>(d.holdSeconds),
                d.editFrame);
  out += buf;
  for (std::size_t f = 0; f < d.frames.size(); ++f) {
    if (f) out += '#';
    const auto& strokes = d.frames[f].strokes;
    for (std::size_t s = 0; s < strokes.size(); ++s) {
      const Stroke& k = strokes[s];
      if (s) out += ';';
      std::snprintf(buf, sizeof buf, "%s,%08x,%.5g,%.3g:", toolToken(k.tool), static_cast<unsigned>(k.rgba),
                    static_cast<double>(k.size), static_cast<double>(k.opacity));
      out += buf;
      for (std::size_t i = 0; i < k.points.size(); ++i) {
        const Point& p = k.points[i];
        std::snprintf(buf, sizeof buf, "%s%d %d %d %d", i ? "," : "",
                      static_cast<int>(std::lround(std::clamp(p.x, -1.0f, 2.0f) * 10000.0f)),
                      static_cast<int>(std::lround(std::clamp(p.y, -1.0f, 2.0f) * 10000.0f)),
                      static_cast<int>(std::lround(std::clamp(p.pressure, 0.0f, 1.0f) * 100.0f)),
                      static_cast<int>(std::lround(std::max(0.0f, p.t) * 100.0f)));
        out += buf;
      }
    }
  }
  return out;
}

namespace detail {
inline std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::size_t at = 0;
  while (true) {
    const std::size_t end = s.find(sep, at);
    out.push_back(s.substr(at, end == std::string::npos ? std::string::npos : end - at));
    if (end == std::string::npos) break;
    at = end + 1;
  }
  return out;
}
}  // namespace detail

// False for text that is not a drawing; `d` is then left as it was.
inline bool parse(const std::string& text, Drawing& d) {
  const auto head = detail::split(text, '|');
  if (head.size() < 6 || head[0] != "v1") return false;
  Drawing out;
  playbackFromToken(head[1], out.playback);
  out.fps = std::clamp(static_cast<float>(std::atof(head[2].c_str())), 1.0f, 60.0f);
  out.holdSeconds = std::clamp(static_cast<float>(std::atof(head[3].c_str())), 0.0f, 60.0f);
  out.editFrame = std::max(0, std::atoi(head[4].c_str()));
  out.frames.clear();
  for (const std::string& frameText : detail::split(head[5], '#')) {
    Frame frame;
    if (!frameText.empty()) {
      for (const std::string& strokeText : detail::split(frameText, ';')) {
        const std::size_t colon = strokeText.find(':');
        if (colon == std::string::npos) continue;
        const auto props = detail::split(strokeText.substr(0, colon), ',');
        if (props.size() < 4) continue;
        Stroke s;
        toolFromToken(props[0], s.tool);
        s.rgba = static_cast<std::uint32_t>(std::strtoul(props[1].c_str(), nullptr, 16));
        s.size = std::clamp(static_cast<float>(std::atof(props[2].c_str())), 0.0005f, 0.5f);
        s.opacity = std::clamp(static_cast<float>(std::atof(props[3].c_str())), 0.0f, 1.0f);
        for (const std::string& pt : detail::split(strokeText.substr(colon + 1), ',')) {
          const char* c = pt.c_str();
          char* stop = nullptr;
          long v[4] = {0, 0, 100, 0};
          int got = 0;
          for (; got < 4; ++got) {
            v[got] = std::strtol(c, &stop, 10);
            if (stop == c) break;
            c = stop;
          }
          if (got < 2) continue;
          s.points.push_back(Point {v[0] / 10000.0f, v[1] / 10000.0f, std::clamp(v[2] / 100.0f, 0.0f, 1.0f),
                                    v[3] / 100.0f});
        }
        if (!s.points.empty() || s.tool == Tool::Fill) frame.strokes.push_back(std::move(s));
      }
    }
    out.frames.push_back(std::move(frame));
  }
  if (out.frames.empty()) out.frames.emplace_back();
  out.editFrame = std::min(out.editFrame, static_cast<int>(out.frames.size()) - 1);
  out.revision = d.revision + 1;
  d = std::move(out);
  return true;
}

// What the next stroke is drawn with. The show's, not a cue's: the brush in
// your hand stays the same as you move from one Draw cue to another.
struct Brush {
  Tool tool = Tool::Brush;
  std::uint32_t rgba = 0xE03030FFu;
  float size = 0.012f;      // fraction of the picture's height
  float opacity = 1.0f;
  bool onion = true;        // show the frame before, faintly, while drawing
};

inline constexpr float kMinBrushSize = 0.001f;
inline constexpr float kMaxBrushSize = 0.2f;

// The eight colours on the palette, as on the board.
constexpr std::uint32_t kPalette[8] = {
  0xFFFFFFFFu, 0x101010FFu, 0xE03030FFu, 0xF08020FFu, 0xF0E040FFu, 0x30C060FFu, 0x3080F0FFu, 0xB050E0FFu,
};

}  // namespace deckboy::draw
