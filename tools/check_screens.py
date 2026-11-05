#!/usr/bin/env python3
"""The Screens window, through its own presses and drags: slices made,
moved, sized, pinned and bent by hand, and what that does to the output.

James, 2026-11-04 (Screens board 5Dsz7KuL, stage 3). The window is driven
with SCREENS CLICK / HOVER / UP in its own pixels (scripted input does not
reach SDL), and checked by SLICE, which reads the model, and OUTSNAP, which
reads the frame the output presents.

Every window hidden. SCREENS CLICK and OUTSNAP are local test inputs.

    py -3 tools/check_screens.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

SEED = "update_check\t0\nui_sounds\t0\nui_scale\t1.0\n"
fails = []
ADD, REMOVE, PIN, CLOSE, SELECT, TOGGLE, DEC, INC, SOURCE, FLIP, MASK, MESH = 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
F_CROP_X, F_OPACITY = 0, 10


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


def snap(db, work, n):
    path = work / ("out%03d.bmp" % n)
    time.sleep(0.6)
    db.send("OUTSNAP %s 1" % path)
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


def red(p):
    return p is not None and p[0] > 180 and p[1] < 60 and p[2] < 60


def blue(p):
    return p is not None and p[2] > 180 and p[0] < 60 and p[1] < 60


class Window:
    def __init__(self, db):
        self.db = db

    def state(self):
        reply = self.db.send("SCREENS")
        rect = lambda key: tuple(map(int, re.search(key + r"=(\d+),(\d+),(\d+),(\d+)", reply).groups()))
        return reply, rect("input"), rect("output_stage")

    def buttons(self):
        reply = self.db.send("SCREENS BUTTONS")
        found = []
        for a, p, x, y, w, h in re.findall(r"(\d+),(-?\d+)@(-?\d+),(-?\d+),(\d+),(\d+)", reply):
            found.append((int(a), int(p), int(x), int(y), int(w), int(h)))
        return found

    def press(self, action, param=None):
        time.sleep(0.15)   # a frame, so the buttons are this frame's
        for a, p, x, y, w, h in self.buttons():
            if a == action and (param is None or p == param):
                self.db.send("SCREENS CLICK %d %d" % (x + w // 2, y + h // 2))
                time.sleep(0.15)
                return True
        return False

    def drag(self, x0, y0, x1, y1):
        self.db.send("SCREENS CLICK %d %d HOLD" % (x0, y0))
        for i in range(1, 7):
            self.db.send("SCREENS HOVER %d %d" % (x0 + (x1 - x0) * i // 6, y0 + (y1 - y0) * i // 6))
        self.db.send("SCREENS UP")
        time.sleep(0.2)


def slices(db):
    return db.send("SLICE")


def numbers(listing, which, label):
    """The four numbers after `label` ("crop" or "place") of slice `which`."""
    for part in listing.split(";"):
        part = part.replace("OK SLICE:", "").strip()
        if part.startswith("%d=" % which):
            m = re.search(label + r" (-?[\d.]+),(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)", part)
            return tuple(float(v) for v in m.groups()) if m else None
    return None


def run(exe, work):
    picture = work / "halves.png"
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i", "color=c=red:s=640x360",
                    "-f", "lavfi", "-i", "color=c=blue:s=320x360",
                    "-filter_complex", "[0][1]overlay=x=320:y=0", "-frames:v", "1", str(picture)], check=True)
    folder = work / "show"
    folder.mkdir()
    show = folder / "show.deckboy"
    show.write_text(SEED, encoding="utf8")
    with Runtime(exe, folder / "run", show, extra=("--import", str(picture))) as db:
        time.sleep(1.0)
        db.commands("SELECT 1", "TAKE", "OUTPUT SELECT 1", "OUTPUT ON")
        w = Window(db)
        note("SCREENS ON opens the window", "open" in db.send("SCREENS ON"))
        time.sleep(1.5)
        reply, inp, outp = w.state()
        note("it has an input and an output stage (%s)" % reply.split("open ")[-1][:80],
             inp[2] > 100 and outp[2] > 100, reply)
        note("...and the programme's picture", "picture=yes" in reply, reply)

        print("  making slices")
        note("+ SLICE makes one", w.press(ADD) and "1=" in slices(db), slices(db))
        w.press(ADD)
        listing = slices(db)
        note("a second arrives as the right half of everything",
             numbers(listing, 2, "crop") == (50.0, 0.0, 50.0, 100.0) and
             numbers(listing, 2, "place") == (50.0, 0.0, 50.0, 100.0), listing)

        print("  by hand, on the input")
        # Slice 2's crop is the right half: drag it left by a tenth of the picture.
        w.press(SELECT, 1)
        ix, iy, iw, ih = inp
        w.drag(ix + iw * 3 // 4, iy + ih // 2, ix + iw * 3 // 4 - iw // 10, iy + ih // 2)
        crop = numbers(slices(db), 2, "crop")
        note("dragging slice 2's crop moves it left by a tenth (%s)" % (crop,),
             crop is not None and 38.0 < crop[0] < 42.0 and crop[2] == 50.0, crop)
        w.press(INC, F_CROP_X)
        crop2 = numbers(slices(db), 2, "crop")
        note("the + beside crop x moves it a percent more (%s)" % (crop2,),
             crop is not None and crop2 is not None and abs(crop2[0] - crop[0] - 1.0) < 0.2, (crop, crop2))

        print("  by hand, on the output")
        ox, oy, ow, oh = outp
        before = numbers(slices(db), 1, "place")
        # Slice 1's bottom-right corner, sized in (CORNERS: SIZE).
        w.press(SELECT, 0)
        bx = ox + int(ow * (before[0] + before[2]) / 100)
        by = oy + int(oh * (before[1] + before[3]) / 100)
        w.drag(bx - 1, by - 1, bx - ow // 4, by - oh // 4)
        after = numbers(slices(db), 1, "place")
        note("dragging a corner sizes the slice (%s -> %s)" % (before, after),
             after is not None and after[2] < before[2] - 15 and after[3] < before[3] - 15, (before, after))
        w.press(PIN)
        tx, ty = ox + int(ow * after[0] / 100), oy + int(oh * after[1] / 100)
        w.drag(tx + 1, ty + 1, tx + ow // 10, ty + oh // 10)
        note("with CORNERS: PIN the corner is pinned instead", "pinned" in slices(db), slices(db))
        w.press(PIN)
        mid = (ox + int(ow * (after[0] + after[2] / 2) / 100), oy + int(oh * (after[1] + after[3] / 2) / 100))
        w.drag(mid[0], mid[1], mid[0] + ow // 5, mid[1])
        moved = numbers(slices(db), 1, "place")
        note("dragging the slice's body moves it (%s)" % (moved,),
             moved is not None and moved[0] > after[0] + 15, (after, moved))

        print("  the rows")
        w.press(SOURCE)
        note("source cycles to a playlist", "source deck:0" in slices(db), slices(db))
        w.press(SOURCE)
        note("...and back to the programme", "source" not in slices(db).split(";")[0], slices(db))
        w.press(MESH)
        note("mesh turns on at 3 x 3", "mesh 3x3" in slices(db), slices(db))
        w.press(DEC, F_OPACITY)
        note("the - beside opacity fades it", "opacity 95" in slices(db), slices(db))
        w.press(TOGGLE, 1)
        note("a slice's on chip switches it off", "(off)" in slices(db), slices(db))

        print("  what the output shows, and saving")
        db.commands("SLICE 1 CORNER TL 0 0", "SLICE 1 GRID OFF", "SLICE 1 OPACITY 100", "SLICE 1 CROP 50 0 50 100",
                    "SLICE 1 PLACE 0 0 50 100", "SLICE 2 ON", "SLICE 2 CROP 0 0 50 100", "SLICE 2 PLACE 50 0 50 100")
        shot = snap(db, work, 1)
        note("the output shows what the window arranged: blue left, red right",
             blue(probe(shot, 0.25, 0.5)) and red(probe(shot, 0.75, 0.5)),
             (probe(shot, 0.25, 0.5), probe(shot, 0.75, 0.5)))
        w.press(REMOVE)
        note("- SLICE takes the selected one away", "2=" not in slices(db), slices(db))
        note("CLOSE closes it", w.press(CLOSE) and "closed" in db.send("SCREENS"), db.send("SCREENS"))
        note("the desk still answers", db.send("PING").startswith("OK"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: the Screens window\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-screens-"))
    try:
        run(exe, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\nscreens: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\nscreens: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
