#!/usr/bin/env python3
"""The multiview's layouts and new feeds, read off the frame a multiview
OUTPUT presents.

James, 2026-11-04 (Screens board 5Dsz7KuL, approved "Let's a go"): the
multiview he has, made easier to use and able to show more. Output 1 shows a
white playlist; output 2 is a multiview of three windows:

  - window 1 shows OUTPUT 1: white, with a red on-air border
  - window 2 shows the CLOCK: lit text on black
  - window 3 is empty
  - BIG puts window 1 over the left two thirds; GRID makes it a quarter;
    TWOTOP puts windows 1 and 2 side by side over the top half
  - the layout and an output window survive the save; an unknown feed or
    layout is refused, and an output that is removed reads as missing

Every window hidden. OUTSNAP is a local test input.

    py -3 tools/check_multiview_feeds.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import shutil
import struct
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

fails = []


def note(name, ok, info=""):
    print("  %-66s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def read_bmp(path):
    data = Path(path).read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0] // 8
    stride = (width * bpp + 3) & ~3
    flipped = height > 0
    height = abs(height)

    def pixel(x, y):
        row = (height - 1 - y) if flipped else y
        i = offset + row * stride + x * bpp
        return data[i + 2], data[i + 1], data[i]
    return width, height, pixel


class Snaps:
    def __init__(self, db, work):
        self.db, self.work, self.n = db, work, 0

    def __call__(self, output, settle=0.8):
        self.n += 1
        path = self.work / ("out%03d.bmp" % self.n)
        time.sleep(settle)
        self.db.send("OUTSNAP %s %d" % (path, output))
        for _ in range(100):
            if path.exists() and path.stat().st_size > 1000:
                time.sleep(0.1)
                return read_bmp(path)
            time.sleep(0.05)
        return None


def probe(shot, fx, fy):
    if not shot:
        return None
    width, height, pixel = shot
    return pixel(min(width - 1, int(fx * width)), min(height - 1, int(fy * height)))


def white(p):
    return p is not None and min(p) > 200


def dark(p):
    return p is not None and max(p) < 40


def red(p):
    return p is not None and p[0] > 180 and p[1] < 90 and p[2] < 90


def lit_in(shot, x0, y0, x1, y1):
    """How many sampled pixels in a region of the frame are bright."""
    if not shot:
        return 0
    width, height, pixel = shot
    count = 0
    for y in range(int(y0 * height), int(y1 * height), 3):
        for x in range(int(x0 * width), int(x1 * width), 3):
            if max(pixel(x, y)) > 150:
                count += 1
    return count


def run(exe, work, show):
    with Runtime(exe, work / "run", show) as db:
        time.sleep(1.0)
        snap = Snaps(db, work)
        db.send("DECK 1")
        db.send("PATTERN ADD full-white")
        db.send("TAKE")
        db.send("OUTPUT SELECT 1")
        db.send("OUTPUT ON")
        db.send("OUTPUT ADD 1")
        db.send("OUTPUT SELECT 2")
        db.send("VIDEO OUTPUT TYPE MULTIVIEW")
        db.send("OUTPUT ON")
        time.sleep(2.5)

        print("  three windows")
        db.send("MULTIVIEW WINDOW 1 OUTPUT1")
        db.send("MULTIVIEW WINDOW 2 CLOCK")
        listing = db.send("MULTIVIEW WINDOW")
        if "3=" not in listing:
            db.send("MULTIVIEW WINDOW ADD")
        db.send("MULTIVIEW WINDOW 3 EMPTY")
        listing = db.send("MULTIVIEW WINDOW")
        note("the windows read back by output number (%s)" % listing.split("OK MULTIVIEW")[-1].strip(),
             "1=output1" in listing and "2=clock" in listing and "3=empty" in listing, listing)

        print("  one big")
        note("LAYOUT BIG is accepted", "big" in db.send("MULTIVIEW LAYOUT BIG"))
        shot = snap(2)
        note("output 1's white fills the big window", white(probe(shot, 0.33, 0.5)), probe(shot, 0.33, 0.5))
        note("...with a red on-air border at its left edge", red(probe(shot, 0.002, 0.5)), probe(shot, 0.002, 0.5))
        clock = lit_in(shot, 0.68, 0.0, 1.0, 0.5)
        note("the clock's window has lit text (%d px)" % clock, clock > 20, clock)
        note("the empty window is dark", dark(probe(shot, 0.84, 0.75)), probe(shot, 0.84, 0.75))

        print("  even grid")
        db.send("MULTIVIEW LAYOUT GRID")
        shot = snap(2)
        note("output 1 in the top-left quarter", white(probe(shot, 0.25, 0.25)), probe(shot, 0.25, 0.25))
        note("...and not over the middle-left of the frame", not white(probe(shot, 0.25, 0.75)),
             probe(shot, 0.25, 0.75))

        print("  two big on top")
        db.send("MULTIVIEW LAYOUT TWOTOP")
        shot = snap(2)
        note("output 1 top left", white(probe(shot, 0.25, 0.25)), probe(shot, 0.25, 0.25))
        note("the clock top right", lit_in(shot, 0.5, 0.0, 1.0, 0.5) > 20)
        note("the empty window below is dark", dark(probe(shot, 0.5, 0.75)), probe(shot, 0.5, 0.75))

        print("  saved, refused")
        saved = db.saved()
        note("the layout is saved", "multiview_layout\ttwotop" in saved)
        note("the output window is saved by id", "multiview_tile\toutput:" in saved)
        for bad in ("MULTIVIEW LAYOUT WALL", "MULTIVIEW WINDOW 1 OUTPUT9", "MULTIVIEW WINDOW 1 BANANA"):
            try:
                reply = db.send(bad)
            except RuntimeError as e:
                reply = str(e)
            note("refused: %s" % bad, "ERR" in reply, reply)

        print("  a playlist window, on air")
        db.send("MULTIVIEW WINDOW 3 DECK1")
        shot = snap(2)
        note("the playing playlist's window has the red border", red(probe(shot, 0.002, 0.75)),
             probe(shot, 0.002, 0.75))
        note("the desk still answers", db.send("PING").startswith("OK"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: multiview feeds\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-mvfeeds-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "02-running-a-show.deckboy"
    try:
        run(exe, work, show)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\nmultiview feeds: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\nmultiview feeds: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
