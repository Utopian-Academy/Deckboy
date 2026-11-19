#!/usr/bin/env python3
"""The Illusion source: every control moves the picture, and it keeps up.

Offline, through --pattern-dump (seconds, exactly repeatable):
  - every control a shape shows changes that shape's picture (a control that
    moves nothing is the "dead control" bug the --params sweep exists for)
  - with speed at 0 the picture holds still; with speed up it moves
  - the drift field keeps black and white edges on every palette: they are
    what makes it drift

Speed: each shape is drawn in under a 60 fps frame at 4K (--pattern-bench).

In the app (windows hidden, local test inputs only):
  - PATTERN ADD illusion, then the ILLUSION verb sets and reports a control
    and refuses a bad one
  - it reaches the output: a snapshot of a 1080p output shows the drift
    field's colours

    py -3 tools/check_illusion.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

fails = []
SIZE = "480x270"

# Each shape, and the controls it shows on the desk (the inspector's rows).
CONTROLS = {
    "drift": ["count=0.9", "size=0.1", "edge=1", "cx=0.1", "flip", "ring", "wave", "palette=2,hue1=0.6",
              "palette=2,hue2=0.8"],
    "ouchi": ["size=0.9", "count=0.9", "edge=0.9", "cx=0.2", "cy=0.2", "palette=2"],
    "cafe": ["size=0.9", "count=1", "edge=1", "grey=0.1", "palette=2"],
}


def note(name, ok, info=""):
    print("  %-62s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def read_ppm(path):
    data = Path(path).read_bytes()
    parts = data.split(b"\n", 3)
    w, h = (int(v) for v in parts[1].split())
    return w, h, parts[3]


def dump(exe, work, variant, t=1.0, size=SIZE):
    name = "%s_%s_%s.ppm" % (variant.replace(",", "_").replace("=", "-").replace(".", ""),
                             str(t).replace(".", ""), size)
    out = work / name
    env = dict(os.environ, SDL_AUDIODRIVER="dummy")
    subprocess.run([exe, "--pattern-dump", "illusion:" + variant, str(out), size, str(t)],
                   env=env, capture_output=True, timeout=120)
    return read_ppm(out)[2] if out.exists() else None


def mean_diff(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / max(1, len(a))


def offline(exe, work):
    print("offline: every control moves the picture")
    for shape, controls in CONTROLS.items():
        base = dump(exe, work, shape)
        note("%s draws" % shape, base is not None and len(base) > 0)
        if base is None:
            continue
        for c in controls:
            other = dump(exe, work, shape + "," + c)
            d = mean_diff(base, other) if other else 0
            note("%s: %s changes the picture" % (shape, c), d > 0.5, "mean change %.3f" % d)
        still_a = dump(exe, work, shape, 0.5)
        still_b = dump(exe, work, shape, 2.5)
        note("%s: at speed 0 it holds still" % shape, still_a == still_b, "it moved")
        moving_a = dump(exe, work, shape + ",speed=0.6", 0.5)
        moving_b = dump(exe, work, shape + ",speed=0.6", 2.5)
        d = mean_diff(moving_a, moving_b) if moving_a and moving_b else 0
        note("%s: with speed up it moves" % shape, d > 0.5, "mean change %.3f" % d)
    for variant in ("drift", "drift,palette=2,hue1=0.6,hue2=0.9"):
        # At 960x540 the crescents are a few pixels wide: their middles reach
        # black and white (smaller, the anti-aliasing never quite does).
        px = dump(exe, work, variant, size="960x540") or b""
        dark = light = 0
        for i in range(0, len(px) - 2, 3):
            lo, hi = min(px[i:i + 3]), max(px[i:i + 3])
            dark += hi <= 8
            light += lo >= 247
        note("%s keeps black and white edges" % variant, dark > 500 and light > 500,
             "near-black %d, near-white %d pixels" % (dark, light))


def bench(exe):
    print("speed: each shape at 4K (--pattern-bench, build only)")
    # The best of three: a desk with a game or a build running beside it
    # measured the same code 11.7 to 18.6 ms (Moire 12.7 to 14.1 alongside).
    for shape in CONTROLS:
        best, line = 1e9, ""
        for _ in range(3):
            r = subprocess.run([exe, "--pattern-bench", "illusion:" + shape, "3840x2160", "30"],
                               capture_output=True, text=True, timeout=300)
            line = (r.stdout.strip().splitlines() or [""])[-1]
            try:
                best = min(best, float(line.split("avg ")[1].split(" ms")[0]))
            except (IndexError, ValueError):
                pass
        note("%s at 4K: %.1f ms a frame (best of 3)" % (shape, best), best < 16.7, line)


def in_app(exe, work):
    print("in the app: the ILLUSION verb, and an output")
    show = work / "show.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\nui_scale\t1.0\n", encoding="utf-8")
    with Runtime(exe, work / "run", show) as db:
        db.send("MASTERVOL 0")
        db.send("DECK 1")
        note("PATTERN ADD illusion", db.send("PATTERN ADD illusion").startswith("OK"))
        db.send("SELECT 1")
        reply = db.send("ILLUSION SHAPE cafe")
        status = db.send("ILLUSION STATUS")
        note("ILLUSION SHAPE cafe sets the shape", reply.startswith("OK") and "shape=cafe" in status, status)
        db.send("ILLUSION GREY 20")
        note("ILLUSION GREY 20 sets the mortar's grey", "grey=0.2" in db.send("ILLUSION STATUS"))
        db.send("ILLUSION LFO SPEED ON")
        note("ILLUSION LFO SPEED ON puts speed on an oscillator", "moving=1" in db.send("ILLUSION STATUS"))
        db.send("ILLUSION LFO SPEED OFF")
        try:
            bad = db.send("ILLUSION SHAPE banana")
        except RuntimeError as e:
            bad = str(e)
        note("ILLUSION SHAPE banana is refused", "ERR" in bad, bad)
        db.send("ILLUSION SHAPE drift")
        db.send("OUTPUT SELECT 1")
        db.send("OUTPUT ON")
        db.send("VIDEO 1920X1080")
        time.sleep(1.5)
        db.send("SELECT 1")
        db.send("TAKE")
        time.sleep(3)
        snap = work / "out.bmp"
        db.send("OUTSNAP %s" % snap)
        time.sleep(2)
        orange = green = 0
        if snap.exists():
            data = snap.read_bytes()
            off = int.from_bytes(data[10:14], "little")
            bpp = int.from_bytes(data[28:30], "little") // 8 or 4
            for i in range(off, len(data) - bpp, bpp * 97):   # BGR(A), sampled
                b, g, r = data[i], data[i + 1], data[i + 2]
                orange += r > 180 and 60 < g < 140 and b < 90
                green += g > 140 and r < 160 and b < 100
        note("the drift field reaches the output", orange > 50 and green > 50,
             "orange %d, green %d samples (%s)" % (orange, green, "captured" if snap.exists() else "no snapshot"))
    # Drawn by the card (illusionMain) when the output can: the log says so.
    log = (work / "run" / "app.log")
    text = log.read_text(errors="replace") if log.exists() else ""
    if "no GPU effects" in text or "CPU effects" in text:
        print("  %-62s %s" % ("the output drew it on the card", "n/a (this renderer has no GPU effects)"))
    else:
        note("the output drew it on the card", "sources made on the card on" in text,
             "no 'sources made on the card' in app.log")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--skip-app", action="store_true", help="the offline checks only")
    ap.add_argument("--skip-bench", action="store_true", help="no 4K timing (a slow or shared machine)")
    args = ap.parse_args()
    exe = str(Path(args.exe).resolve())
    work = Path(tempfile.mkdtemp(prefix="deckboy-illusion-"))
    print("check: the Illusion source\n")
    try:
        offline(exe, work)
        if not args.skip_bench:
            bench(exe)
        if not args.skip_app:
            in_app(exe, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\nillusion: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\nillusion: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
