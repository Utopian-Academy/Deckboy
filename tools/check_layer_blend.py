#!/usr/bin/env python3
"""Every layer blend mode reaches the output, and does what its formula says.

WHY THIS EXISTS. Until 0.99.426 no blend mode reached an output. VJ BLEND set
a label that only the mix monitor read, and the output read a per-layer blend
that nothing could set. Every mode looked like dissolve, and the crossfade
check, which tests only dissolve, passed. A fader check cannot see that.

So this records the programme with a grey ramp as the base and colour bars as
the layer over it, and for every mode compares the picture with the mode's
formula, written out again here rather than shared with the app, at points
across the frame:

    base alone    the layer's fader at 0
    layer alone   dissolve at 100 (dissolve takes the base away)
    each mode     at 100, against formula(base, layer)
    pixel modes   at 50 too, against halfway between the base and the result

Run it on the default renderer AND on the software renderer
(DECKBOY_OUTPUT_RENDERER=software), which has no custom blends and takes the
processor path for every mode.

Recording needs a presented output window, so this runs on a desktop session.
Needs ffmpeg to pull a frame from the file.

    python3 tools/check_layer_blend.py
"""

import colorsys
import math
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, finish, parse_args  # noqa: E402
from check_crossfade import frame_at, read_ppm  # noqa: E402


def lum(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def clip(c):
    l = lum(c)
    n, x = min(c), max(c)
    if n < 0:
        c = [l + (v - l) * l / max(1e-6, l - n) for v in c]
    if x > 1:
        c = [l + (v - l) * (1 - l) / max(1e-6, x - l) for v in c]
    return c


def set_lum(c, l):
    d = l - lum(c)
    return clip([v + d for v in c])


def sat(c):
    return max(c) - min(c)


def set_sat(c, s):
    order = sorted(range(3), key=lambda i: c[i])
    lo, mid, hi = order
    out = [0.0, 0.0, 0.0]
    if c[hi] > c[lo]:
        out[mid] = (c[mid] - c[lo]) * s / (c[hi] - c[lo])
        out[hi] = s
    return out


def dodge(d, s):
    return 0.0 if d <= 0 else 1.0 if s >= 1 else min(1.0, d / (1 - s))


def burn(d, s):
    return 1.0 if d >= 1 else 0.0 if s <= 0 else 1 - min(1.0, (1 - d) / s)


def soft(d, s):
    if s <= 0.5:
        return d - (1 - 2 * s) * d * (1 - d)
    dd = ((16 * d - 12) * d + 4) * d if d <= 0.25 else math.sqrt(d)
    return d + (2 * s - 1) * (dd - d)


PER_CHANNEL = {
    "add": lambda d, s: min(1.0, d + s),
    "screen": lambda d, s: d + s - d * s,
    "multiply": lambda d, s: d * s,
    "lighten": max,
    "darken": min,
    "subtract": lambda d, s: max(0.0, s - d),
    "undercut": lambda d, s: max(0.0, d - s),
    "infiltrate": lambda d, s: s * (1 - d),
    "ember": lambda d, s: min(1.0, d + s * d),
    "difference": lambda d, s: abs(d - s),
    "exclusion": lambda d, s: d + s - 2 * d * s,
    "overlay": lambda d, s: 2 * d * s if d <= 0.5 else 1 - 2 * (1 - d) * (1 - s),
    "hard-light": lambda d, s: 2 * d * s if s <= 0.5 else 1 - 2 * (1 - d) * (1 - s),
    "soft-light": soft,
    "vivid-light": lambda d, s: burn(d, 2 * s) if s <= 0.5 else dodge(d, 2 * s - 1),
    "linear-light": lambda d, s: min(1.0, max(0.0, d + 2 * s - 1)),
    "pin-light": lambda d, s: min(d, 2 * s) if s <= 0.5 else max(d, 2 * s - 1),
    "hard-mix": lambda d, s: 1.0 if d + s >= 1 else 0.0,
    "dodge": dodge,
    "burn": burn,
    "linear-burn": lambda d, s: max(0.0, d + s - 1),
    "divide": lambda d, s: (1.0 if d > 0 else 0.0) if s <= 0 else min(1.0, d / s),
    "average": lambda d, s: 0.5 * (d + s),
    "xor": lambda d, s: (round(d * 255) ^ round(s * 255)) / 255,
    "wraparound": lambda d, s: d + s - 1 if d + s > 1 else d + s,
    "solarise": lambda d, s: 1 - d if s > 0.5 else d,
}

# Modes with a hard edge in them: a point within this much of the edge is
# skipped, because one code value either side is a different answer.
EDGES = {"hard-mix": lambda d, s: [d[i] + s[i] - 1 for i in range(3)],
         "solarise": lambda d, s: [v - 0.5 for v in s],
         "overlay": lambda d, s: [v - 0.5 for v in d],
         "hard-light": lambda d, s: [v - 0.5 for v in s],
         "vivid-light": lambda d, s: [v - 0.5 for v in s],
         "pin-light": lambda d, s: [v - 0.5 for v in s],
         "wraparound": lambda d, s: [d[i] + s[i] - 1 for i in range(3)]}


def heat(t):
    stops = [(0, 0, 0), (0.1, 0, 0.6), (0.8, 0, 0.6), (1, 0.45, 0), (1, 0.9, 0.1), (1, 1, 1)]
    t = min(1.0, max(0.0, t)) * 5
    i = min(4, int(t))
    f = t - i
    return [stops[i][k] + (stops[i + 1][k] - stops[i][k]) * f for k in range(3)]


def expected(mode, d, s):
    if mode in PER_CHANNEL:
        return [PER_CHANNEL[mode](d[i], s[i]) for i in range(3)]
    if mode == "hue":
        return set_lum(set_sat(s, sat(d)), lum(d))
    if mode == "saturation":
        return set_lum(set_sat(d, sat(s)), lum(d))
    if mode == "colour":
        return set_lum(s, lum(d))
    if mode == "luminosity":
        return set_lum(d, lum(s))
    if mode == "luma-key":
        a = lum(s)
        return [d[i] + (s[i] - d[i]) * a for i in range(3)]
    if mode == "luma-key-inv":
        a = 1 - lum(s)
        return [d[i] + (s[i] - d[i]) * a for i in range(3)]
    if mode == "stencil":
        return [v * lum(s) for v in d]
    if mode == "take-red":
        return [s[0], d[1], d[2]]
    if mode == "take-green":
        return [d[0], s[1], d[2]]
    if mode == "take-blue":
        return [d[0], d[1], s[2]]
    if mode == "hue-spin":
        t = lum(s) * 2 * math.pi
        c, k, q = math.cos(t), (1 - math.cos(t)) / 3, math.sin(t) / math.sqrt(3)
        return clip([d[0] * (c + k) + d[1] * (k - q) + d[2] * (k + q),
                     d[0] * (k + q) + d[1] * (c + k) + d[2] * (k - q),
                     d[0] * (k - q) + d[1] * (k + q) + d[2] * (c + k)])
    if mode == "thermal":
        return heat(abs(lum(d) - lum(s)) * 1.6)
    if mode == "crush":
        levels = 2 + round(min(1.0, max(0.0, lum(s))) * 14)
        return [round(v * (levels - 1)) / (levels - 1) for v in d]
    return None


# The modes the GPU lays on with blend factors. At a part fader they follow
# the factors (lighten, for one, rises from the base rather than fading), so
# only their full-fader result is checked.
FACTOR = set()

def spread(mode, d, s, amount):
    """The lowest and highest result for inputs within two code values of what
    was measured. The sample is 8-bit and the steep modes (dodge, vivid light,
    thermal, xor) turn one code value of rounding into ten."""
    lo, hi = [1.0] * 3, [0.0] * 3
    step = 2 / 255
    for dd in (-step, 0, step):
        for ds in (-step, 0, step):
            d2 = [min(1.0, max(0.0, v + dd)) for v in d]
            s2 = [min(1.0, max(0.0, v + ds)) for v in s]
            r = expected(mode, d2, s2)
            for k in range(3):
                v = d2[k] + (r[k] - d2[k]) * amount
                lo[k], hi[k] = min(lo[k], v), max(hi[k], v)
    return lo, hi


ROWS = (0.30, 0.70)
COLUMNS = [0.04 + i * 0.92 / 23 for i in range(24)]


def sample(path):
    w, h, px = read_ppm(path)
    out = []
    for fy in ROWS:
        for fx in COLUMNS:
            x, y = int(w * fx), int(h * fy)
            i = (y * w + x) * 3
            out.append([px[i + k] / 255 for k in range(3)])
    return out


def main():
    args = parse_args(__doc__, 5741)
    fails = []
    tol = 10 / 255

    def note(name, ok, detail=""):
        print("  %-46s %s" % (name, "ok" if ok else "FAIL  " + detail))
        if not ok:
            fails.append("%s -- %s" % (name, detail))

    with Deckboy(args, prefix="deckboy-layerblend-") as db:
        db.send("MASTERVOL 0")
        print("check: layer blend modes reach the output")
        db.send("DECK 1")
        db.send("PATTERN ADD greyscale")
        db.send("TAKE")
        db.send("DECKADD")
        db.send("DECK 2")
        db.send("PATTERN ADD smpte-bars")
        db.send("TAKE")
        db.send("DECK 2")
        db.send("VIDEO OUTPUT ASSIGN")
        listing = db.send("VIDEO OUTPUT LAYERBLEND LIST")
        modes = listing.split(": ", 2)[-1].split()
        print("    %d modes" % len(modes))
        note("at least 30 modes", len(modes) >= 30, listing[:80])
        note("the base refuses a blend",
             db.send("DECK 1") and db.send("VIDEO OUTPUT LAYERBLEND add").startswith("ERR"))
        db.send("DECK 2")
        note("an unknown mode is refused",
             db.send("VIDEO OUTPUT LAYERBLEND sparkle").startswith("ERR"))

        def grab(tag):
            time.sleep(1.0)
            f = frame_at(db, tag)
            return sample(f) if f else None

        db.send("VIDEO OUTPUT LAYERBLEND difference")
        db.send("CROSSFADE 1 2 0")
        base = grab("base")
        db.send("VIDEO OUTPUT LAYERBLEND dissolve")
        db.send("CROSSFADE 1 2 100")
        layer = grab("layer")
        if not base or not layer:
            print("  no recording was produced; an output needs a window.")
            return finish("layer blend (refusals only)", fails)

        for mode in modes:
            if mode == "dissolve":
                continue
            reply = db.send("VIDEO OUTPUT LAYERBLEND " + mode)
            db.send("CROSSFADE 100")
            full = grab(mode + "-100")
            worst, where, checked = 0.0, "", 0
            for i, (d, s) in enumerate(zip(base, layer)):
                if mode in EDGES and any(abs(e) < 0.04 for e in EDGES[mode](d, s)):
                    continue
                want = expected(mode, d, s)
                if want is None:
                    continue
                checked += 1
                lo, hi = spread(mode, d, s, 1.0)
                err = max(max(0.0, lo[k] - full[i][k], full[i][k] - hi[k]) for k in range(3))
                if err > worst:
                    worst, where = err, "d=%s s=%s got=%s want=%s" % (
                        [round(v * 255) for v in d], [round(v * 255) for v in s],
                        [round(v * 255) for v in full[i]], [round(v * 255) for v in want])
            note("%s at 100 (%d points)" % (mode, checked),
                 checked > 0 and worst <= tol and reply.startswith("OK"),
                 "worst %d: %s %s" % (round(worst * 255), where, reply))
            if mode in FACTOR:
                continue
            db.send("CROSSFADE 50")
            half = grab(mode + "-50")
            worst, where = 0.0, ""
            for i, (d, s) in enumerate(zip(base, layer)):
                if mode in EDGES and any(abs(e) < 0.04 for e in EDGES[mode](d, s)):
                    continue
                want = expected(mode, d, s)
                if want is None:
                    continue
                want = [d[k] + (want[k] - d[k]) * 0.5 for k in range(3)]
                lo, hi = spread(mode, d, s, 0.5)
                err = max(max(0.0, lo[k] - half[i][k], half[i][k] - hi[k]) for k in range(3))
                if err > worst:
                    worst, where = err, "got=%s want=%s" % (
                        [round(v * 255) for v in half[i]], [round(v * 255) for v in want])
            note("%s at 50" % mode, worst <= tol, "worst %d: %s" % (round(worst * 255), where))

    return finish("layer blend", fails)


if __name__ == "__main__":
    sys.exit(main())
