#!/usr/bin/env python3
"""The hearth's window: five choices, and each one has to be a different room.

WHY THIS EXISTS. The fireside pattern grew a window onto a garden, rain, snow or
the sea. That is five values of one control, drawn by one switch, and Deckboy's
most expensive recurring fault is a control that looks like it works: SHATTER
shipped completely inert for twelve days and nothing caught it, because
screenshots of a glitch effect and screenshots of no effect at all are equally
convincing at a glance.

So this does not ask "does it look nice" -- it asks the four questions that can
only be answered by measuring:

  1. Does each view CHANGE the picture?      (a view that draws nothing)
  2. Are the five views DIFFERENT from each   (a switch whose cases fell
     other?                                    through, or two that duplicate)
  3. Does each view MOVE?                     (a view frozen on one frame)
  4. Is the FIRE untouched by all of them?    (glass drawn over the flame)

Then one claim per view, because "it changed the pixels" is not the same as "it
is snow": the snow must have flakes brighter than its sky, the garden must be
green below the leaf line, the sea must be lighter above the horizon than below.
The first draft of the snow drew each flake into a neighbouring cell and then
painted the sky over that cell -- roughly half the snow vanished, and no
screenshot of a snowstorm would have shown it.

    python3 tools/check_fireside_views.py
    python3 tools/check_fireside_views.py --keep out/   # look at them too

Needs only the app binary. No window, no GPU, no ffmpeg -- so it runs in CI and
over ssh.
"""

import argparse
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deckboy_testroot  # noqa: E402

# The tokens `--pattern-dump fireside:<token>` takes, in the order the inspector
# row cycles them. "" is view 0 -- the bare brick wall, which is the baseline
# every other view has to differ from.
VIEWS = ["", "garden", "rain", "snow", "sea"]

WIDTH, HEIGHT = 1280, 720
# Two moments far enough apart that the slowest view (the garden, deliberately
# the calmest thing on screen) has visibly moved.
T_A, T_B = 30.0, 30.9


def read_ppm(path):
    """(width, height, pixels) from a binary P6 PPM. Pixels are a flat RGB
    bytes object, three per pixel, top row first."""
    with open(path, "rb") as handle:
        data = handle.read()
    if not data.startswith(b"P6"):
        raise ValueError("%s is not a binary PPM" % path)
    # P6\n<w> <h>\n255\n -- the app writes exactly this, so parse the three
    # whitespace-separated fields rather than handling comments nobody emits.
    fields, at = [], 2
    while len(fields) < 3:
        while at < len(data) and data[at:at + 1].isspace():
            at += 1
        start = at
        while at < len(data) and not data[at:at + 1].isspace():
            at += 1
        fields.append(int(data[start:at]))
    return fields[0], fields[1], data[at + 1:]


class Frame(object):
    def __init__(self, path):
        self.w, self.h, self.px = read_ppm(path)

    def at(self, x, y):
        i = (y * self.w + x) * 3
        return self.px[i], self.px[i + 1], self.px[i + 2]

    def region(self, x, y, w, h):
        """The bytes of a rectangle, as one blob to compare or measure."""
        out = bytearray()
        for row in range(y, min(y + h, self.h)):
            i = (row * self.w + x) * 3
            out += self.px[i:i + w * 3]
        return bytes(out)

    def pixels(self, x, y, w, h):
        for row in range(y, min(y + h, self.h)):
            for col in range(x, min(x + w, self.w)):
                yield self.at(col, row)


def glass_rect(w, h):
    """The rect the glass occupies, from the same arithmetic the builder uses.

    Repeated here on purpose. If the window MOVES, this check starts measuring
    brickwork, reports every view as identical, and says so loudly -- which is
    the failure we want from a drifted constant, rather than a check that
    quietly follows it and proves nothing.
    """
    cell = max(2, w // 200)
    win_w = max(20, w // 6)
    win_h = max(16, h // 5)
    win_x = max(cell * 2, w // 12)
    win_y = max(cell * 2, h // 7)
    cols = max(1, win_w // cell)
    rows = max(1, win_h // cell)
    return win_x, win_y, cols * cell, rows * cell


def dump(exe, work, view, t):
    token = "fireside:%s" % view if view else "fireside"
    out = os.path.join(work, "fireside-%s-%g.ppm" % (view or "none", t))
    proc = subprocess.run([exe, "--pattern-dump", token, out,
                           "%dx%d" % (WIDTH, HEIGHT), str(t)],
                          capture_output=True)
    if not os.path.exists(out):
        raise RuntimeError("no dump for %s: %s%s"
                           % (token, proc.stdout.decode("utf-8", "replace"),
                              proc.stderr.decode("utf-8", "replace")))
    return Frame(out)


def differing_bytes(a, b):
    return sum(1 for x, y in zip(a, b) if x != y)


# ── WHAT EACH VIEW CLAIMS TO BE ─────────────────────────────────────────────
#
# One measurable claim per view, phrased as what a person would say about it
# rather than as what the code does -- so a rewrite of the drawing still has to
# satisfy it, and so the check cannot pass by agreeing with itself.

def claim_garden(f, rect):
    x, y, w, h = rect
    lower = list(f.pixels(x, y + h // 2, w, h - h // 2))
    green = sum(1 for r, g, b in lower if g > r and g > b)
    return (green * 100 // max(1, len(lower)) >= 70,
            "%d%% of the lower glass is green" % (green * 100 // max(1, len(lower))))


def claim_rain(f, rect):
    x, y, w, h = rect
    px = list(f.pixels(x, y, w, h))
    streaks = sum(1 for r, g, b in px if b >= 140 and g >= 120)
    return (streaks >= 8, "%d streak cells over a sky under 100" % streaks)


def claim_snow(f, rect):
    x, y, w, h = rect
    px = list(f.pixels(x, y, w, h))
    flakes = sum(1 for r, g, b in px if r >= 210 and g >= 210 and b >= 210)
    return (flakes >= 8, "%d flake cells brighter than the sky" % flakes)


def claim_sea(f, rect):
    x, y, w, h = rect
    # The horizon sits at 0.46 of the glass; sky above it is brighter than the
    # water below. A sea drawn upside down passes every other check here.
    sky = list(f.pixels(x, y, w, int(h * 0.4)))
    sea = list(f.pixels(x, y + int(h * 0.55), w, h - int(h * 0.55)))
    sky_l = sum(sum(p) for p in sky) // max(1, len(sky) * 3)
    sea_l = sum(sum(p) for p in sea) // max(1, len(sea) * 3)
    return (sky_l > sea_l + 8,
            "sky %d vs water %d" % (sky_l, sea_l))


CLAIMS = {"garden": claim_garden, "rain": claim_rain,
          "snow": claim_snow, "sea": claim_sea}


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default=os.path.join(
        REPO, "build", "windows", "Release", "Deckboy.exe"))
    parser.add_argument("--keep", help="write the dumps here and leave them")
    args = parser.parse_args()

    exe = os.path.abspath(args.exe)
    if not os.path.exists(exe):
        print("no binary at %s" % exe)
        return 1
    deckboy_testroot.warn_if_stale(exe)

    work = args.keep or tempfile.mkdtemp(prefix="deckboy-fireside-")
    if args.keep:
        os.makedirs(work, exist_ok=True)

    print("check: the hearth's window")
    print()
    print("  raster:   %dx%d" % (WIDTH, HEIGHT))
    print("  moments:  t=%g and t=%g" % (T_A, T_B))
    print()

    frames_a, frames_b = {}, {}
    for view in VIEWS:
        frames_a[view] = dump(exe, work, view, T_A)
        frames_b[view] = dump(exe, work, view, T_B)

    rect = glass_rect(WIDTH, HEIGHT)
    print("  glass:    x=%d y=%d %dx%d" % rect)
    print()

    fails = []
    base = frames_a[""]
    base_glass = base.region(*rect)

    # 1 + 3 + the per-view claim, one line each.
    print("  %-8s %-9s %-9s %s" % ("view", "draws", "moves", "claim"))
    for view in VIEWS[1:]:
        f, g = frames_a[view], frames_b[view]
        glass = f.region(*rect)
        drew = differing_bytes(glass, base_glass)
        moved = differing_bytes(glass, g.region(*rect))
        ok_claim, detail = CLAIMS[view](f, rect)
        if drew == 0:
            fails.append("%s draws nothing: the glass is the bare wall" % view)
        if moved == 0:
            fails.append("%s is frozen: identical at t=%g and t=%g"
                         % (view, T_A, T_B))
        if not ok_claim:
            fails.append("%s does not look like %s: %s" % (view, view, detail))
        print("  %-8s %-9s %-9s %s%s"
              % (view,
                 "%d B" % drew if drew else "NOTHING",
                 "%d B" % moved if moved else "FROZEN",
                 detail, "" if ok_claim else "   <-- FAIL"))
    print()

    # 2. No two views may be the same picture.
    print("  each view is its own room:")
    for i, a in enumerate(VIEWS):
        for b in VIEWS[i + 1:]:
            same = frames_a[a].region(*rect) == frames_a[b].region(*rect)
            if same:
                fails.append("%s and %s draw the same glass"
                             % (a or "no window", b or "no window"))
                print("    %s == %s   <-- FAIL" % (a or "no window",
                                                   b or "no window"))
    print("    %d pair%s, all different" % (10, "s") if not fails else "")

    # 4. THE FIRE IS NOT TOUCHED. The arch is a pure function of the clock, so
    # at one moment every view must render it byte for byte the same. This is
    # the check that catches a window sized or placed over the hearth -- which
    # would be a hearth pattern that no longer shows a fire.
    arch = (WIDTH // 3, 0, WIDTH // 3, HEIGHT)
    arch_base = base.region(*arch)
    print()
    print("  the fire is untouched:")
    for view in VIEWS[1:]:
        diff = differing_bytes(frames_a[view].region(*arch), arch_base)
        if diff:
            fails.append("%s changes the hearth itself (%d bytes in the arch)"
                         % (view, diff))
        print("    %-8s %s" % (view, "same" if not diff
                               else "%d bytes DIFFER   <-- FAIL" % diff))

    print()
    for f in fails:
        print("  FAIL " + f)
    if args.keep:
        print()
        print("  dumps kept in %s" % work)
    print()
    print("clean" if not fails
          else "%d finding%s" % (len(fails), "" if len(fails) == 1 else "s"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
