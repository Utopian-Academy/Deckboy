"""Do two blended projectors add up to one picture?

WHY THIS EXISTS. Two projectors overlapping add LIGHT, and a projector's light
is its signal raised to its gamma. The blend used to be a straight ramp in
signal, and two of those crossing give 2 x 0.5^2.2 = 44% of the light at the
middle of the seam: a dark stripe down the table. Nothing caught it, because
every check asked whether the blend CHANGED the picture, not whether two of
them made a whole one.

This pairs two outputs (VIDEO BLEND PAIR), shows a left-to-right grey ramp,
reads the frame each output PRESENTS (OUTSNAP), and across the overlap adds
up what each projector would emit at a gamma of 2.2. Where the two overlap
they must show the same strip of the picture, and the sum must be the light
that strip would have from one projector alone. It checks:

  - PROJECTOR curve: the sum is within 3% of one projector, everywhere in the
    seam (the curve, the gamma, and the two areas lining up);
  - LINEAR curve: the old ramp, still there for shows that used it, and
    still dark in the middle (so this check can tell the two apart);
  - BLACK LIFT: raises the single-projector part and not the overlap.

Windows are hidden (DECKBOY_HIDE_WINDOWS), so it takes over no screen.

    python tools/check_edge_blend.py
"""

import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, parse_args  # noqa: E402
from check_geometry_sweep import read_bmp  # noqa: E402

GAMMA = 2.2
OVERLAP = 240


def column(snap, x, y0, y1):
    """Mean of the three channels of column x over rows y0..y1, 0-1."""
    w, h, bpp, stride, px = snap
    total = 0
    count = 0
    for y in range(y0, y1):
        row = h - 1 - y  # BMP rows are stored bottom up
        i = row * stride + x * bpp
        total += px[i] + px[i + 1] + px[i + 2]
        count += 3
    return total / count / 255.0


def main():
    args = parse_args(__doc__, 5797)
    shots = tempfile.mkdtemp(prefix="deckboy-blend-shots-")
    # A grey ramp across the width: where the two projectors overlap they
    # must show the SAME grey, so a misplaced area shows up as a wrong sum.
    picture = os.path.join(shots, "ramp.png")
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i",
                    "nullsrc=size=1920x1080,geq=lum='64+160*X/W':cb=128:cr=128,format=gray",
                    "-frames:v", "1", picture], capture_output=True)
    if not os.path.exists(picture):
        sys.exit("needs ffmpeg on PATH to make the test picture")
    fails = []

    def note(name, ok, info=""):
        print("  %-58s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append("%s -- %s" % (name, info))

    os.environ["DECKBOY_HIDE_WINDOWS"] = "1"
    with Deckboy(args, prefix="deckboy-blend-", extra_args=["--import", picture]) as db:
        counter = [0]

        def snap(output):
            """The frame output <output> (counted from 1) presents next."""
            counter[0] += 1
            path = os.path.join(shots, "s%04d.bmp" % counter[0])
            db.send("OUTSNAP %s %d" % (path, output))
            for _ in range(100):
                if os.path.exists(path) and os.path.getsize(path) > 1000:
                    time.sleep(0.05)
                    return read_bmp(path)
                time.sleep(0.05)
            return None

        db.send("SELECT 1")
        db.send("TAKE")
        db.send("OUTPUT SELECT 1")
        print("output 1:", db.send("OUTPUT ON"))
        print("add:", db.send("OUTPUT ADD 1"))
        print("output 2:", db.send("OUTPUT ON"))
        db.send("OUTPUT SELECT 1")
        time.sleep(3.0)

        def measure(label):
            a, b = snap(1), snap(2)
            if not a or not b:
                note(label + ": both outputs snapped", False, "no snapshot")
                return None
            w, h = a[0], a[1]
            y0, y1 = h // 3, 2 * h // 3
            sums = []
            for i in range(0, OVERLAP, 4):
                la = column(a, w - OVERLAP + i, y0, y1) ** GAMMA
                lb = column(b, i, y0, y1) ** GAMMA
                # What one projector shows of that strip, unblended: just
                # inside each one's own part, where nothing fades.
                sums.append((i, la + lb))
            # The reference: the same strip of picture, from one projector,
            # at the inner edge of each side's blend (alpha 1 there).
            ref_a = column(a, w - OVERLAP - 2, y0, y1) ** GAMMA
            ref_b = column(b, OVERLAP + 2, y0, y1) ** GAMMA
            return w, sums, ref_a, ref_b, a, b

        def both(*commands):
            for output in (1, 2):
                db.send("OUTPUT SELECT %d" % output)
                for c in commands:
                    db.send(c)
            db.send("OUTPUT SELECT 1")

        # -- PROJECTOR curve (what a new output starts with), through each of
        # the three meshes the blend is drawn on: plain, the perspective
        # corner pin, and the grid warp. A warp with its points at rest moves
        # nothing, so the seam must still add up.
        print("pair:", db.send("VIDEO BLEND PAIR 2 %d" % OVERLAP))
        print("curve:", db.send("VIDEO BLEND CURVE PROJECTOR"))
        for path, commands in (("no warp", ()),
                               ("perspective warp", ("VIDEO WARP ON", "VIDEO WARP MODE PERSPECTIVE")),
                               ("grid warp", ("VIDEO WARP ON", "VIDEO WARP GRID 5"))):
            both(*commands)
            # WARP RESET clears the blends too, so pair again each time.
            db.send("VIDEO BLEND PAIR 2 %d" % OVERLAP)
            time.sleep(1.0)
            got = measure("projector")
            if got:
                w, sums, ref_a, ref_b, _, _ = got
                worst = 0.0
                for i, total in sums:
                    # The ramp rises across the seam, so the reference does too:
                    # interpolate between the two edges' single-projector light.
                    ref = ref_a + (ref_b - ref_a) * (i + 0.5) / OVERLAP
                    worst = max(worst, abs(total / ref - 1.0))
                mid = sums[len(sums) // 2][1] / ((ref_a + ref_b) / 2)
                note("projector curve, %s: sums to one (within 3%%)" % path, worst <= 0.03,
                     "worst %.1f%% off" % (worst * 100))
                print("    middle of the seam: %.1f%% of one projector" % (mid * 100))
            both("VIDEO WARP GRID OFF", "VIDEO WARP RESET", "VIDEO WARP OFF")

        # -- LINEAR (the old feather): must still be the old, dark-middled one
        db.send("VIDEO BLEND PAIR 2 %d" % OVERLAP)
        db.send("VIDEO BLEND CURVE LINEAR")
        db.send("OUTPUT SELECT 2")
        db.send("VIDEO BLEND CURVE LINEAR")
        db.send("OUTPUT SELECT 1")
        time.sleep(1.0)
        got = measure("linear")
        if got:
            w, sums, ref_a, ref_b, _, _ = got
            mid = sums[len(sums) // 2][1] / ((ref_a + ref_b) / 2)
            note("linear curve: the old ramp, dark in the middle (< 60%)", mid < 0.60,
                 "middle %.1f%%" % (mid * 100))

        # -- BLACK LIFT on a black picture: up outside the seam, not in it
        db.send("VIDEO BLEND CURVE PROJECTOR")
        db.send("OUTPUT SELECT 2")
        db.send("VIDEO BLEND CURVE PROJECTOR")
        db.send("OUTPUT SELECT 1")
        print("lift:", db.send("VIDEO BLEND LIFT 5"))
        db.send("VIDEO BLEND PAIR 2 %d" % OVERLAP)   # carries the lift to output 2
        time.sleep(1.0)
        got = measure("lift")
        if got:
            w, sums, ref_a, ref_b, a, b = got
            h = a[1]
            outside = column(a, w // 4, h // 3, 2 * h // 3)
            inside = column(a, w - OVERLAP // 2, h // 3, 2 * h // 3)
            db.send("VIDEO BLEND LIFT 0")
            db.send("VIDEO BLEND PAIR 2 %d" % OVERLAP)
            time.sleep(1.0)
            a0 = snap(1)
            outside0 = column(a0, w // 4, h // 3, 2 * h // 3)
            inside0 = column(a0, w - OVERLAP // 2, h // 3, 2 * h // 3)
            note("black lift raises the single-projector part",
                 outside - outside0 > 0.03, "%.3f -> %.3f" % (outside0, outside))
            note("black lift leaves the overlap alone", abs(inside - inside0) < 0.01,
                 "%.3f -> %.3f" % (inside0, inside))

        # -- the show file keeps all three
        db.send("VIDEO BLEND GAMMA 2.4")
        db.send("VIDEO BLEND LIFT 3")
        db.send("SAVE")
        time.sleep(1.0)
        saved = open(db.show, encoding="utf-8", errors="replace").read()
        rows = [r.split("\t") for r in saved.splitlines() if r.startswith("output_target\t")]
        ok = bool(rows) and len(rows[0]) >= 112 and rows[0][109] == "1" and \
            abs(float(rows[0][110]) - 2.4) < 1e-3 and abs(float(rows[0][111]) - 0.03) < 1e-3
        note("curve, gamma and lift saved with the output", ok,
             str(rows[0][109:112]) if rows and len(rows[0]) >= 112 else "short record")

        old_show = "\n".join(
            "\t".join(r.split("\t")[:109]) if r.startswith("output_target\t") else r
            for r in saved.splitlines()) + "\n"

    # -- a show saved BEFORE the curve existed must keep its straight ramp:
    # the same record cut back to its 109 fields, opened fresh.
    old = Deckboy(args, prefix="deckboy-blend-old-")
    with open(old.show, "w", encoding="utf-8") as f:
        f.write(old_show)
    with old as db:
        time.sleep(2.0)
        reply = db.send("VIDEO BLEND LIFT 0")
        note("a show saved before the curve opens LINEAR", "blend linear" in reply, reply)

    print()
    if fails:
        print("FAILED (%d):" % len(fails))
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print("edge blend: all checks passed")


if __name__ == "__main__":
    main()
