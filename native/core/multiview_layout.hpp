#pragma once
// Where each window of a multiview goes. One function for the desk's monitor
// and for an output carrying the multiview, so the two can never disagree
// (they used to keep a grid each).
//
// Every layout takes any number of windows. The big ones are the first one or
// two in the list; the rest share what is left in the grid that shows them
// largest. Nothing is ever hidden because a layout has too few places.
//
// SDL-free, so tools/multiview_layout_check.cpp can test it alone.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace deckboy::multiview {

enum class Layout { Grid = 0, OneBig, OneBigTop, TwoBigTop, Count };

inline const char* layoutToken(Layout layout) {
  switch (layout) {
    case Layout::OneBig:    return "big";
    case Layout::OneBigTop: return "bigtop";
    case Layout::TwoBigTop: return "twotop";
    default:                return "grid";
  }
}

inline const char* layoutLabel(Layout layout) {
  switch (layout) {
    case Layout::OneBig:    return "one big, the rest beside it";
    case Layout::OneBigTop: return "one big on top, the rest below";
    case Layout::TwoBigTop: return "two big on top, the rest below";
    default:                return "even grid";
  }
}

inline bool layoutFromToken(const std::string& token, Layout& out) {
  for (int i = 0; i < static_cast<int>(Layout::Count); ++i) {
    const auto layout = static_cast<Layout>(i);
    if (token == layoutToken(layout)) {
      out = layout;
      return true;
    }
  }
  return false;
}

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

namespace detail {

// `count` cells over `area` in `cols` columns, row by row.
inline void gridInto(std::vector<Rect>& out, const Rect& area, int count, int cols, int gap) {
  if (count <= 0 || cols <= 0) return;
  const int rows = (count + cols - 1) / cols;
  const int cellW = (area.w - gap * (cols - 1)) / cols;
  const int cellH = (area.h - gap * (rows - 1)) / rows;
  for (int i = 0; i < count; ++i) {
    out.push_back({area.x + (i % cols) * (cellW + gap), area.y + (i / cols) * (cellH + gap),
                   std::max(0, cellW), std::max(0, cellH)});
  }
}

// The column count that shows `count` pictures of this aspect largest.
inline int bestColumns(const Rect& area, int count, int gap, double aspect) {
  int best = 1;
  double bestArea = -1.0;
  for (int cols = 1; cols <= count; ++cols) {
    const int rows = (count + cols - 1) / cols;
    const double cellW = static_cast<double>(area.w - gap * (cols - 1)) / cols;
    const double cellH = static_cast<double>(area.h - gap * (rows - 1)) / rows;
    if (cellW <= 0 || cellH <= 0) continue;
    const double picW = std::min(cellW, cellH * aspect);
    const double pic = picW * (picW / aspect);
    if (pic > bestArea + 0.5) {
      bestArea = pic;
      best = cols;
    }
  }
  return best;
}

}  // namespace detail

// The rectangle of each of `count` windows inside `area`, in list order.
// `aspect` is the pictures' width over height, used to size the small ones.
inline std::vector<Rect> layoutWindows(Layout layout, int count, const Rect& area, int gap,
                                       double aspect = 16.0 / 9.0) {
  std::vector<Rect> out;
  if (count <= 0 || area.w <= 0 || area.h <= 0) return out;
  gap = std::max(0, gap);
  aspect = aspect > 0.01 ? aspect : 16.0 / 9.0;
  out.reserve(static_cast<std::size_t>(count));

  if (layout == Layout::Grid || count == 1) {
    // The grid the multiview has always used, so a show opens as it was:
    // the smallest square that holds them, then only the rows it needs.
    int cols = 1;
    while (cols * cols < count) ++cols;
    detail::gridInto(out, area, count, cols, gap);
    return out;
  }

  if (layout == Layout::OneBig) {
    const int bigW = (area.w - gap) * 2 / 3;
    out.push_back({area.x, area.y, bigW, area.h});
    const Rect rest {area.x + bigW + gap, area.y, area.w - bigW - gap, area.h};
    detail::gridInto(out, rest, count - 1, detail::bestColumns(rest, count - 1, gap, aspect), gap);
    return out;
  }

  if (layout == Layout::OneBigTop) {
    const int bigH = (area.h - gap) * 2 / 3;
    out.push_back({area.x, area.y, area.w, bigH});
    const Rect rest {area.x, area.y + bigH + gap, area.w, area.h - bigH - gap};
    detail::gridInto(out, rest, count - 1, detail::bestColumns(rest, count - 1, gap, aspect), gap);
    return out;
  }

  // Two big on top: the broadcast wall, programme and preview side by side.
  const int halfW = (area.w - gap) / 2;
  if (count == 2) {
    out.push_back({area.x, area.y, halfW, area.h});
    out.push_back({area.x + halfW + gap, area.y, area.w - halfW - gap, area.h});
    return out;
  }
  const int bigH = (area.h - gap) / 2;
  out.push_back({area.x, area.y, halfW, bigH});
  out.push_back({area.x + halfW + gap, area.y, area.w - halfW - gap, bigH});
  const Rect rest {area.x, area.y + bigH + gap, area.w, area.h - bigH - gap};
  detail::gridInto(out, rest, count - 2, detail::bestColumns(rest, count - 2, gap, aspect), gap);
  return out;
}

}  // namespace deckboy::multiview
