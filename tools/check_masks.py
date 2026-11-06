#!/usr/bin/env python3
"""Masks cut where they are put, read off the frame the output PRESENTS.

James, 2026-11-04 (board X2N9qJe1, approved): masks from a library of shapes
and pen paths, invertible, exportable, on a cue, a layer, an output or in the
matte slot. Over a white picture and a red layer:

  - on the OUTPUT a circle keeps the middle and cuts the corner to black;
    inverted, the other way round; feathered, half-way on its edge; at 25%
    opacity the corner is three-quarters bright
  - in the MATTE slot it paints the corner black, and NONE takes it away
  - on a LAYER it cuts that playlist only: the red is a star, white round it
  - on a CUE it travels with the cue; on the BASE playlist it cuts the white
  - an oscillator moves it; export writes a PNG and an SVG; delete leaves no
    cut behind, and out-of-range values are refused rather than clamped

Every window hidden. OUTSNAP is a local test input.

    py -3 tools/check_masks.py --exe build/windows/Release/Deckboy.exe
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

    def __call__(self, settle=0.5):
        self.n += 1
        path = self.work / ("out%03d.bmp" % self.n)
        time.sleep(settle)
        self.db.send("OUTSNAP %s 1" % path)
        for _ in range(100):
            if path.exists() and path.stat().st_size > 1000:
                time.sleep(0.1)
                return read_bmp(path)
            time.sleep(0.05)
        return None


def probe(shot, fx, fy):
    """The pixel at a fraction of the frame."""
    if not shot:
        return None
    width, height, pixel = shot
    return pixel(min(width - 1, int(fx * width)), min(height - 1, int(fy * height)))


def near(rgb, want, tolerance=14):
    return rgb is not None and all(abs(a - b) <= tolerance for a, b in zip(rgb, want))


WHITE, BLACK, RED = (255, 255, 255), (0, 0, 0), (255, 0, 0)
MIDDLE, CORNER = (0.5, 0.5), (0.04, 0.06)


def run(exe, work, show, renderer=""):
    env = {"DECKBOY_OUTPUT_RENDERER": renderer} if renderer else None
    with Runtime(exe, work / "run", show, env=env) as db:
        time.sleep(1.0)
        db.send("DECK 1")
        db.send("PATTERN ADD full-white")
        db.send("TAKE")
        db.send("OUTPUT SELECT 1")
        db.send("OUTPUT ON")
        time.sleep(2.5)
        snap = Snaps(db, work)

        print("  on the output")
        shot = snap()
        note("before any mask the corner is white", near(probe(shot, *CORNER), WHITE), probe(shot, *CORNER))
        mask = db.send("MASK NEW circle").split()[-1]
        note("MASK NEW answers with an id (%s)" % mask, mask.startswith("m"), mask)
        db.send("MASK ASSIGN %s OUTPUT 1" % mask)
        shot = snap()
        note("a circle on the output keeps the middle", near(probe(shot, *MIDDLE), WHITE), probe(shot, *MIDDLE))
        note("...and cuts the corner to black", near(probe(shot, *CORNER), BLACK), probe(shot, *CORNER))
        if shot:
            # Round on the screen: as far across as it is down, in pixels.
            width, height, pixel = shot
            across = sum(1 for x in range(width) if pixel(x, height // 2)[0] > 127)
            down = sum(1 for y in range(height) if pixel(width // 2, y)[0] > 127)
            note("...and is round, not an egg (%d x %d px)" % (across, down), abs(across - down) <= 4,
                 (across, down))
            note("...0.6 of the height across", abs(down - 0.6 * height) <= 4, down)
        db.send("MASK SET %s INVERT on" % mask)
        shot = snap()
        note("inverted: the middle is cut", near(probe(shot, *MIDDLE), BLACK), probe(shot, *MIDDLE))
        note("...and the corner kept", near(probe(shot, *CORNER), WHITE), probe(shot, *CORNER))
        db.send("MASK SET %s INVERT off" % mask)
        db.send("MASK SET %s FEATHER 10" % mask)
        shot = snap()
        edge = probe(shot, 0.5 + 0.3 * 9 / 16, 0.5)
        note("feathered 10%%: half-way on the circle's edge (%s)" % (edge,),
             edge is not None and 90 <= edge[0] <= 165, edge)
        db.send("MASK SET %s FEATHER 0" % mask)
        db.send("MASK SET %s OPACITY 25" % mask)
        shot = snap()
        note("at 25% opacity the corner is three-quarters bright", near(probe(shot, *CORNER), (191, 191, 191), 10),
             probe(shot, *CORNER))
        db.send("MASK SET %s OPACITY 100" % mask)
        try:
            db.send("MASK SET %s FEATHER 90" % mask)
            refused = "OK"
        except RuntimeError as e:
            refused = str(e)
        note("a feather beyond 25% is refused, not clamped", "ERR" in refused, refused)

        print("  in the matte slot")
        db.send("MASK ASSIGN NONE OUTPUT 1")
        db.send("MASK ASSIGN %s MATTE 1" % mask)
        shot = snap()
        note("the matte paints the corner black", near(probe(shot, *CORNER), BLACK), probe(shot, *CORNER))
        note("...and leaves the middle", near(probe(shot, *MIDDLE), WHITE), probe(shot, *MIDDLE))
        db.send("MASK ASSIGN NONE MATTE 1")
        shot = snap()
        note("NONE takes the matte away", near(probe(shot, *CORNER), WHITE), probe(shot, *CORNER))

        print("  on a layer")
        db.send("DECKADD")
        db.send("DECK 2")
        db.send("PATTERN ADD full-red")
        db.send("TAKE")
        db.send("DECK 2")
        db.send("VIDEO OUTPUT ASSIGN")
        shot = snap(1.0)
        note("the red playlist covers the white one", near(probe(shot, *CORNER), RED), probe(shot, *CORNER))
        star = db.send("MASK NEW star").split()[-1]
        db.send("MASK ASSIGN %s LAYER 1 2" % star)
        shot = snap()
        note("a star on the red layer keeps red in the middle", near(probe(shot, *MIDDLE), RED),
             probe(shot, *MIDDLE))
        note("...and shows the white playlist round it", near(probe(shot, *CORNER), WHITE), probe(shot, *CORNER))
        note("...between the star's arms too", near(probe(shot, 0.5, 0.5 + 0.30), WHITE), probe(shot, 0.5, 0.8))

        print("  on a cue, and on the base playlist")
        db.send("MASK ASSIGN NONE LAYER 1 2")
        db.send("DECK 2")
        db.send("MASK ASSIGN %s CUE" % mask)
        shot = snap()
        note("the red cue's own circle keeps the middle red", near(probe(shot, *MIDDLE), RED), probe(shot, *MIDDLE))
        note("...and the corner shows white", near(probe(shot, *CORNER), WHITE), probe(shot, *CORNER))
        db.send("MASK ASSIGN %s LAYER 1 1" % star)
        shot = snap()
        note("a star on the base playlist cuts the white in the corner", near(probe(shot, *CORNER), BLACK),
             probe(shot, *CORNER))
        note("...under the red circle, which stays", near(probe(shot, *MIDDLE), RED), probe(shot, *MIDDLE))
        db.send("MASK ASSIGN NONE LAYER 1 1")

        print("  moving, exported, deleted")
        db.send("MASK LFO %s X ON 1 100" % mask)
        seen = set()
        # Sampled until both have been seen: a 1 Hz sweep read at whatever
        # moments the snapshots land can show one side six times running.
        for _ in range(16):
            p = probe(snap(0.13), 0.5, 0.5)
            if p:
                seen.add(p[1] < 127)   # red has no green; the white under it does
            if len(seen) == 2:
                break
        note("an oscillator on X moves the cue's circle (middle both red and white)", seen == {True, False}, seen)
        db.send("MASK LFO %s X OFF" % mask)
        png, svg = work / "mask.png", work / "mask.svg"
        db.send("MASK EXPORT %s %s 320x180" % (mask, png))
        db.send("MASK EXPORT %s %s 320x180" % (mask, svg))
        data = png.read_bytes() if png.exists() else b""
        note("EXPORT writes a 320x180 PNG", data[:8] == b"\x89PNG\r\n\x1a\n" and
             struct.unpack(">II", data[16:24]) == (320, 180), data[:24])
        text = svg.read_text() if svg.exists() else ""
        note("...and an SVG with the circle's path", "<path" in text and " C" in text, text[:80])
        db.send("MASK DELETE %s" % mask)
        listing = db.send("MASK LIST")
        note("DELETE takes it out of the library", mask + "\t" not in listing, listing)
        shot = snap()
        note("...and the cue it was on is whole again", near(probe(shot, *CORNER), RED), probe(shot, *CORNER))
        try:
            db.send("MASK ASSIGN %s CUE" % mask)
            gone = "OK"
        except RuntimeError as e:
            gone = str(e)
        note("assigning a deleted mask is refused", "ERR" in gone, gone)

        # SVG in: the exported circle comes back, and a drawing's shapes become paths.
        reply = db.send("MASK IMPORT %s round trip" % svg)
        note("IMPORT reads the exported SVG back as one path", "paths=1" in reply, reply)
        drawing = work / "drawing.svg"
        drawing.write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 160 90">'
                           '<rect width="160" height="90" fill="#000"/>'
                           '<circle cx="80" cy="45" r="40" fill="#fff"/>'
                           '<rect x="70" y="35" width="20" height="20" fill="black"/></svg>')
        reply = db.send("MASK IMPORT %s" % drawing)
        note("...and a drawing: a circle with a black hole, on a black ground", "paths=2" in reply
             and "invert" not in reply and " drawing " in reply, reply)
        shot = snap()
        note("...which cuts the cue's corner away", not near(probe(shot, *CORNER), RED), probe(shot, *CORNER))
        try:
            db.send("MASK IMPORT %s" % (work / "missing.svg"))
            missing = "OK"
        except RuntimeError as e:
            missing = str(e)
        note("a file that is not there is refused, and says so", "cannot read" in missing, missing)
        note("the desk still answers", db.send("PING").startswith("OK"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--renderer", default="",
                    help="pin the output renderer (software has no custom blends: the processor cuts)")
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: masks%s\n" % (" on the " + args.renderer + " renderer" if args.renderer else ""))
    work = Path(tempfile.mkdtemp(prefix="deckboy-masks-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "02-running-a-show.deckboy"
    try:
        run(exe, work, show, args.renderer)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\nmasks: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\nmasks: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
