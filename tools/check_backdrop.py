#!/usr/bin/env python3
"""A solid backdrop behind the cues made to sit over another playlist.

James, 2026-10-30: the stage timer "as well as any other source meant to be
composited with another source potentially should have the option to set the
background to a solid colour to key out". Board H856G24w: the timer's
backdrop row (none, black, key green, key blue, and a colour chip) on Text,
Lower Third and image cues, transparent by default as before.

Read off the frame the output PRESENTS (OUTSNAP), on a Lower Third:

  - by default the frame around it is what it was: nothing filled in
  - BACKDROP #00b140 fills it key green, and the Lower Third still draws on top
  - BACKDROP transparent takes it away again
  - the inspector draws the backdrop row on the cue, and its chip opens the
    colour picker aimed at the cue's backdrop

Every window hidden. CLICK is a local-only test input.

    py -3 tools/check_backdrop.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import json
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


def status(db):
    return json.loads(db.send("STATUS JSON"))


def read_bmp(path):
    """(width, height, pixel(x, y) -> (r, g, b)) for a 24- or 32-bit BMP."""
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


def snap(db, work, n):
    path = work / ("out%03d.bmp" % n)
    db.send("OUTSNAP %s 1" % path)
    for _ in range(100):
        if path.exists() and path.stat().st_size > 1000:
            time.sleep(0.1)
            return read_bmp(path)
        time.sleep(0.05)
    return None


def near(rgb, want, tolerance=12):
    return rgb is not None and all(abs(a - b) <= tolerance for a, b in zip(rgb, want))


def corner(shot):
    """A pixel well away from the Lower Third, which sits low on the frame."""
    if not shot:
        return None
    width, height, pixel = shot
    return pixel(width // 10, height // 10)


def run(exe, work, show):
    with Runtime(exe, work / "run", show) as db:
        time.sleep(1.0)
        db.send("DECK 1")
        db.send("LOWERTHIRD")
        time.sleep(0.4)
        # LOWERTHIRD selects the cue it makes.
        print("  backdrop now:", db.send("BACKDROP"))
        db.send("TAKE")
        db.send("OUTPUT SELECT 1")
        db.send("OUTPUT ON")
        time.sleep(2.5)

        print("  on the output")
        before = corner(snap(db, work, 1))
        note("by default nothing is filled behind it (%s)" % (before,), near(before, (0, 0, 0)), before)
        reply = db.send("BACKDROP #00b140")
        note("BACKDROP #00b140 answers", "00B140" in reply.upper() or "green" in reply.lower(), reply)
        time.sleep(0.6)
        shot = snap(db, work, 2)
        green = corner(shot)
        note("...and the frame around the Lower Third is key green (%s)" % (green,), near(green, (0, 177, 64)), green)
        if shot:
            width, height, pixel = shot
            # The Lower Third still draws: some pixel in its band is not green.
            band = [pixel(x, int(height * y)) for y in (0.78, 0.82, 0.86) for x in range(0, width, max(1, width // 64))]
            note("...with the Lower Third still on top of it", any(not near(p, (0, 177, 64), 30) for p in band),
                 band[:4])
        db.send("BACKDROP transparent")
        time.sleep(0.6)
        after = corner(snap(db, work, 3))
        note("BACKDROP transparent takes it away (%s)" % (after,), near(after, (0, 0, 0)), after)

        print("  in the inspector")
        chips = {c[0]: c[1:] for c in status(db).get("colourChips", [])}
        chip = chips.get(-1)
        note("the cue has a backdrop chip", chip is not None, chips)
        if chip:
            view = status(db).get("inspectorViewport", [0, 0, 0, 0])
            for _ in range(60):
                chip = {c[0]: c[1:] for c in status(db).get("colourChips", [])}.get(-1)
                if not chip or view[3] <= 0 or view[1] <= chip[1] <= view[1] + view[3] - chip[3]:
                    break
                way = 1 if chip[1] < view[1] else -1
                db.send("HOVER %d %d WHEEL %d" % (view[0] + view[2] // 2, view[1] + view[3] // 2, way))
                time.sleep(0.15)
            if chip:
                db.send("CLICK %d %d" % (chip[0] + chip[2] // 2, chip[1] + chip[3] // 2))
                time.sleep(0.4)
                picker = status(db).get("colourPicker", {})
                note("its chip opens the colour picker on the backdrop", picker.get("open") and picker.get("backdrop"),
                     picker)
                db.send("KEY Escape")
        note("the desk still answers", db.send("PING").startswith("OK"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: backdrops behind overlay cues\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-backdrop-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "02-running-a-show.deckboy"
    try:
        run(exe, work, show)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\nbackdrop: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\nbackdrop: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
