// The multiview's layouts: every window placed, inside the area, none
// overlapping, the big ones big, and the even grid the same as it always was.
//
//   deckboy-multiview-layout-check
#include "core/multiview_layout.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using deckboy::multiview::Layout;
using deckboy::multiview::Rect;
using deckboy::multiview::layoutWindows;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %-70s %s\n", what.c_str(), ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

static bool overlaps(const Rect& a, const Rect& b) {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

int main() {
  std::printf("check: multiview layouts\n\n");
  const Rect area {10, 20, 1920, 1080};
  const int gap = 8;

  for (int li = 0; li < static_cast<int>(Layout::Count); ++li) {
    const auto layout = static_cast<Layout>(li);
    const std::string name = deckboy::multiview::layoutToken(layout);
    bool placed = true, inside = true, apart = true, sized = true;
    for (int n = 1; n <= 24; ++n) {
      const auto rects = layoutWindows(layout, n, area, gap);
      placed = placed && static_cast<int>(rects.size()) == n;
      for (std::size_t i = 0; i < rects.size(); ++i) {
        const Rect& r = rects[i];
        sized = sized && r.w > 20 && r.h > 20;
        inside = inside && r.x >= area.x && r.y >= area.y &&
                 r.x + r.w <= area.x + area.w && r.y + r.h <= area.y + area.h;
        for (std::size_t j = i + 1; j < rects.size(); ++j) apart = apart && !overlaps(r, rects[j]);
      }
    }
    check(placed, name + ": 1 to 24 windows, every one placed");
    check(inside, name + ": all inside the area");
    check(apart, name + ": none overlapping");
    check(sized, name + ": none smaller than 20 px at 1080p");
    Layout parsed = Layout::Grid;
    check(deckboy::multiview::layoutFromToken(name, parsed) && parsed == layout, name + ": token round-trips");
  }

  // The even grid is the one shows were saved with: 6 windows in 3 x 2.
  const auto six = layoutWindows(Layout::Grid, 6, area, gap);
  check(six[2].y == six[0].y && six[3].y > six[0].y && six[3].x == six[0].x, "grid: 6 windows are 3 across, 2 down");

  const auto big = layoutWindows(Layout::OneBig, 5, area, gap);
  check(big[0].h == area.h && big[0].w > area.w / 2, "big: the first window is full height and most of the width");
  check(big[1].w * big[1].h * 4 < big[0].w * big[0].h, "big: the others are much smaller");

  const auto top = layoutWindows(Layout::OneBigTop, 5, area, gap);
  check(top[0].w == area.w && top[1].y > top[0].y, "bigtop: the first window spans the top");
  check(top[1].y == top[4].y, "bigtop: four small ones share one row");

  const auto two = layoutWindows(Layout::TwoBigTop, 6, area, gap);
  check(std::abs(two[0].w - two[1].w) <= 1 && two[0].y == two[1].y && two[2].y > two[0].y,
        "twotop: two side by side on top, the rest below");
  const auto pair = layoutWindows(Layout::TwoBigTop, 2, area, gap);
  check(pair[0].h == area.h, "twotop: with two windows, each is full height");

  check(layoutWindows(Layout::Grid, 0, area, gap).empty(), "no windows, no rectangles");
  Layout junk = Layout::OneBig;
  check(!deckboy::multiview::layoutFromToken("wall", junk), "an unknown token is refused");

  if (failures) {
    std::printf("\nmultiview layouts: %d FAILED\n", failures);
    return 1;
  }
  std::printf("\nmultiview layouts: all cases pass\n");
  return 0;
}
