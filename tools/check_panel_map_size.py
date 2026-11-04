#!/usr/bin/env python3
"""A panel map's tile is whatever size the wall's tile really is.

The rows stepped in sixteens and could not be typed into, so a 161 px tile was
out of reach. Read off the frame the output PRESENTS (OUTSNAP), with the cue on
air and never retaken, because a panel map is built once and an edit has to ask
for the redraw itself:

  - the map starts on the 128 px grid
  - clicking the number and typing 161 moves the seam to exactly 161
  - + moves it one pixel, to 162, and - moves it back
  - the height row does the same, on its own axis
  - a value below the floor lands on the floor rather than on the typed number

Every window hidden. KEY, TYPE and QUICK are local test inputs.

    py -3 tools/check_panel_map_size.py --exe build/windows/Release/Deckboy.exe
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
SEAM = (90, 90, 110)


def note(name, ok, info=""):
    print("  %-66s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


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


class Snaps:
    def __init__(self, db, work):
        self.db, self.work, self.n = db, work, 0

    def __call__(self):
        self.n += 1
        path = self.work / ("out%03d.bmp" % self.n)
        time.sleep(0.5)
        self.db.send("OUTSNAP %s 1" % path)
        for _ in range(100):
            if path.exists() and path.stat().st_size > 1000:
                time.sleep(0.1)
                return read_bmp(path)
            time.sleep(0.05)
        return None


def first_seam_x(shot):
    """Where the second tile begins, along a row with no label or marker on it."""
    if not shot:
        return None
    width, height, pixel = shot
    for x in range(8, width):
        if pixel(x, 5) == SEAM and pixel(x, 6) == SEAM:
            return x
    return None


def first_seam_y(shot):
    if not shot:
        return None
    width, height, pixel = shot
    for y in range(8, height):
        if pixel(5, y) == SEAM and pixel(6, y) == SEAM:
            return y
    return None


def rows(db):
    """(tip fragment) -> (action, param), read from the inspector as drawn."""
    found = []
    for line in db.send("QUICKLIST").splitlines():
        bits = line.split(" ", 2)
        if len(bits) == 3 and bits[0].lstrip("-").isdigit():
            found.append((int(bits[0]), int(bits[1]), bits[2]))
    return found


def buttons(db, tip):
    """The dec, inc and edit actions of the row with this tip, in that order."""
    return [(a, p) for a, p, t in rows(db) if tip in t]


def type_into(db, row, text):
    # A row's buttons are listed left to right: -, the value, +.
    db.send("QUICK %d %d" % row[1])
    time.sleep(0.2)
    try:
        db.send("TYPE " + text)      # refused unless the value opened an editor
    except RuntimeError:
        return False
    time.sleep(0.1)
    db.send("KEY Return")
    time.sleep(0.3)
    return True


def run(exe, work, show):
    with Runtime(exe, work / "run", show) as db:
        time.sleep(1.0)
        db.send("DECK 1")
        reply = db.send("PATTERN ADD panel-map")
        note("a panel map can be added", reply.startswith("OK"), reply)
        time.sleep(0.4)
        db.send("TAKE")
        db.send("OUTPUT SELECT 1")
        db.send("OUTPUT ON")
        time.sleep(2.5)
        snap = Snaps(db, work)

        x = first_seam_x(snap())
        note("it starts on the 128 px grid (%s)" % x, x == 128, x)

        wide = buttons(db, "LED panel width")
        tall = buttons(db, "LED panel height")
        note("the inspector draws both rows, on one parameter each",
             len(wide) >= 3 and len(tall) >= 3 and wide[0][1] != tall[0][1], (wide, tall))
        if not wide or not tall:
            return

        note("the width can be typed", type_into(db, wide, "161"))
        x = first_seam_x(snap())
        note("typing 161 moves the seam to exactly 161, on air (%s)" % x, x == 161, x)
        y = first_seam_y(snap())
        note("...and leaves the height alone (%s)" % y, y == 128, y)

        dec, inc = wide[0], wide[2]
        db.send("QUICK %d %d" % inc)
        x = first_seam_x(snap())
        note("+ moves it one pixel, to 162 (%s)" % x, x == 162, x)
        db.send("QUICK %d %d" % dec)
        x = first_seam_x(snap())
        note("- moves it back to 161 (%s)" % x, x == 161, x)

        note("the height can be typed", type_into(db, tall, "97"))
        y = first_seam_y(snap())
        note("typing 97 moves the row seam to exactly 97 (%s)" % y, y == 97, y)
        x = first_seam_x(snap())
        note("...and leaves the width alone (%s)" % x, x == 161, x)

        type_into(db, wide, "2")
        # The corner marker covers the first seams on a grid this fine, so read
        # the spacing between two seams rather than where the first one is.
        shot = snap()
        x = first_seam_x(shot)
        spacing = None
        if x is not None:
            width, _, pixel = shot
            spacing = next((n - x for n in range(x + 1, width)
                            if pixel(n, 5) == SEAM and pixel(n, 6) == SEAM), None)
        note("a size below the floor lands on 8 px (%s)" % spacing, spacing == 8, (x, spacing))
        note("the desk still answers", db.send("PING").startswith("OK"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: a panel map's tile size\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-panelmap-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "02-running-a-show.deckboy"
    try:
        run(exe, work, show)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\npanel map: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\npanel map: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
