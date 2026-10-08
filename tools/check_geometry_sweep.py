"""Does every geometry control change what actually leaves the output?

WHY THIS EXISTS. "If you fixed AOI, were any other geometry parameters broken,
or non-functioning?" The AOI had no width or height control at all, and
nothing noticed, because every existing check reads a recording -- taken from
the composite BEFORE AOI, warp, blend and orientation are applied -- or the
reply to a setter, which proves only that a setter ran.

This reads the frame the output PRESENTS (the OUTSNAP dev verb), so it sees
exactly what a projector or an LED processor would. Two halves:

  - OUTPUT geometry, through the real network verbs: area of interest (fill,
    pixel for pixel, position), warp corners, perspective, the grid, each edge
    blend, orientation. Each case starts from a reset output, snaps, applies
    the change, snaps again.
  - CUE geometry, through the inspector's own buttons (QUICKLIST / QUICK):
    every +/- pair is pressed twenty times up, then twenty times down. Up must
    change the picture; down must bring it back.

A control that changes nothing is reported. It arms an output window, so it
takes over that display for a minute.

    python tools/check_geometry_sweep.py
"""

import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, parse_args  # noqa: E402


# Presses each way. Position moves a pixel a press, so five was 5px -- which
# on a picture with few horizontal edges reads as "Y does nothing".
PRESSES = 20


def read_bmp(path):
    """(w, h, bytes per pixel, rows) from an uncompressed 24/32-bit BMP."""
    data = open(path, "rb").read()
    offset = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0] // 8
    stride = (w * bpp + 3) & ~3
    return w, abs(h), bpp, stride, data[offset:]


def differing(a, b):
    """How many sampled pixels differ by more than a little, of how many."""
    wa, ha, bpa, sa, pa = a
    wb, hb, bpb, sb, pb = b
    if (wa, ha) != (wb, hb):
        return 10 ** 9, 1
    changed = 0
    samples = 0
    for y in range(0, ha, 4):
        for x in range(0, wa, 4):
            i = y * sa + x * bpa
            j = y * sb + x * bpb
            samples += 1
            if (abs(pa[i] - pb[j]) + abs(pa[i + 1] - pb[j + 1]) +
                    abs(pa[i + 2] - pb[j + 2])) > 24:
                changed += 1
    return changed, samples


def main():
    args = parse_args(__doc__, 5793)
    shots = tempfile.mkdtemp(prefix="deckboy-geo-shots-")
    # A 4:3 still with detail in BOTH directions and to every edge. Colour
    # bars hide a vertical move (they are vertical stripes), hide a bottom
    # blend (their bottom row is black), and look identical under every scale
    # mode because a pattern is generated at the box's own size.
    picture = os.path.join(shots, "testsrc-4x3.png")
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i",
                    "testsrc=size=1280x960:rate=1:duration=1", "-frames:v", "1", picture],
                   capture_output=True)
    if not os.path.exists(picture):
        sys.exit("needs ffmpeg on PATH to make the test picture")
    fails = []
    counter = [0]

    def note(name, ok, info=""):
        print("  %-52s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append("%s -- %s" % (name, info))

    with Deckboy(args, prefix="deckboy-geo-", extra_args=["--import", picture]) as db:
        def snap():
            counter[0] += 1
            path = os.path.join(shots, "s%04d.bmp" % counter[0])
            db.send("OUTSNAP " + path)
            for _ in range(100):
                if os.path.exists(path) and os.path.getsize(path) > 1000:
                    time.sleep(0.05)
                    return read_bmp(path)
                time.sleep(0.05)
            return None

        def settle():
            time.sleep(0.35)

        db.send("SET ui_scale 0.6")
        db.send("SELECT 1")
        db.send("TAKE")
        print("output:", db.send("OUTPUT ON"))
        time.sleep(3.0)

        def reset_output():
            db.send("OUTPUT AOI FULL")
            db.send("OUTPUT AOI MODE FILL")
            db.send("VIDEO WARP GRID OFF")
            db.send("VIDEO WARP RESET")
            db.send("VIDEO WARP OFF")
            db.send("VIDEO BLEND RESET")
            db.send("VIDEO OUTPUT ORIENTATION 0")
            settle()

        def case(name, *commands, against=None, expect_change=True):
            reset_output()
            base = against() if against else snap()
            for c in commands:
                db.send(c)
            settle()
            after = snap()
            if base is None or after is None:
                note(name, False, "no frame came back from the output")
                return
            changed, samples = differing(base, after)
            pct = 100.0 * changed / max(1, samples)
            if expect_change:
                note(name, changed > samples * 0.01, "changed %.2f%% of the picture" % pct)
            else:
                note(name, changed <= samples * 0.01, "changed %.2f%% (should not)" % pct)

        print()
        print("output geometry")
        case("AOI 960x540, scaled to fill", "OUTPUT AOI 960x540 0 0")
        case("AOI 960x540, pixel for pixel", "OUTPUT AOI 960x540 0 0", "OUTPUT AOI MODE PIXEL")
        case("AOI moved across the raster", "OUTPUT AOI 960x540 1800 900")
        case("warp: TL corner pulled in", "VIDEO WARP ON", "VIDEO WARP TL 300 200")
        case("warp: BR corner pulled in", "VIDEO WARP ON", "VIDEO WARP BR -300 -200")
        case("warp: perspective vs linear", "VIDEO WARP ON", "VIDEO WARP TL 600 0",
             "VIDEO WARP MODE PERSPECTIVE")
        case("warp: a grid point moved", "VIDEO WARP ON", "VIDEO WARP GRID 3",
             "WARP EDIT", "WARP SELECT 2 2", "WARP AT 1200 700", "WARP EDIT OFF")
        for edge in ("L", "R", "T", "B"):
            case("edge blend %s" % edge, "VIDEO BLEND %s 30" % edge)
        case("orientation 180", "VIDEO OUTPUT ORIENTATION 180")
        reset_output()

        print()
        print("cue geometry (the inspector's own buttons)")
        listing = db.send("QUICKLIST")
        present = set()
        for line in listing.splitlines():
            bits = line.replace("OK QUICKLIST:", "").strip().split(" ", 2)
            if len(bits) >= 2 and bits[0].lstrip("-").isdigit():
                present.add(int(bits[0]))
        pairs = [
            ("width", 60, 61), ("height", 62, 63),
            ("position X", 66, 67), ("position Y", 68, 69),
            ("rotation", 72, 73),
            ("crop left", 141, 142), ("crop right", 143, 144),
            ("crop top", 145, 146), ("crop bottom", 147, 148),
            ("brightness", 163, 164), ("contrast", 165, 166),
            ("saturation", 167, 168), ("hue", 169, 170),
        ]
        for name, dec, inc in pairs:
            if dec not in present or inc not in present:
                note("cue %s: buttons on screen" % name, False,
                     "action %d/%d not in the inspector" % (dec, inc))
                continue
            base = snap()
            for _ in range(PRESSES):
                db.send("QUICK %d" % inc)
            settle()
            up = snap()
            for _ in range(PRESSES):
                db.send("QUICK %d" % dec)
            settle()
            back = snap()
            if not (base and up and back):
                note("cue " + name, False, "no frame came back")
                continue
            moved, samples = differing(base, up)
            returned, _ = differing(base, back)
            note("cue %s: + changes the output" % name, moved > samples * 0.01,
                 "%.2f%% changed" % (100.0 * moved / samples))
            note("cue %s: - brings it back" % name, returned <= samples * 0.01,
                 "%.2f%% still different" % (100.0 * returned / samples))
        # Scale mode is a cycle, and bars at the output's own raster look the
        # same fitted or filled -- so it is checked on a cue made smaller first.
        for _ in range(8):
            db.send("QUICK 60")
        settle()
        base = snap()
        db.send("QUICK 58")
        settle()
        cycled = snap()
        if base and cycled:
            moved, samples = differing(base, cycled)
            note("cue scale mode: next mode changes the output", moved > samples * 0.01,
                 "%.2f%% changed" % (100.0 * moved / samples))
        db.send("OUTPUT OFF")

    print()
    if fails:
        print("%d FAILED" % len(fails))
        for f in fails:
            print("  - " + f)
        return 1
    print("all ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
