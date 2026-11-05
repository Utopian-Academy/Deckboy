#!/usr/bin/env python3
"""Slices: pieces of the programme placed on an output, read off the frame
the output presents.

James, 2026-11-04 (Screens board 5Dsz7KuL): Resolume's Advanced Output. The
programme is red on its left half and blue on its right:

  - with no slices the output shows it as it is
  - two slices swap the halves: blue on the left, red on the right
  - a soft edge fades a slice toward black at that edge
  - a pinned corner pulls the picture off it, leaving black
  - a mask on a slice cuts that slice only
  - an off slice is not drawn; CLEAR goes back to the one picture
  - the slices survive the save; bad values are refused

Every window hidden. OUTSNAP is a local test input.

    py -3 tools/check_slices.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
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

    def __call__(self, settle=0.7):
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
    if not shot:
        return None
    width, height, pixel = shot
    return pixel(min(width - 1, int(fx * width)), min(height - 1, int(fy * height)))


def red(p):
    return p is not None and p[0] > 180 and p[1] < 60 and p[2] < 60


def blue(p):
    return p is not None and p[2] > 180 and p[0] < 60 and p[1] < 60


def dark(p):
    return p is not None and max(p) < 40


def refused(db, command):
    try:
        reply = db.send(command)
    except RuntimeError as e:
        reply = str(e)
    return "ERR" in reply, reply


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
        snap = Snaps(db, work)
        db.commands("SELECT 1", "TAKE", "OUTPUT SELECT 1", "OUTPUT ON")
        shot = snap(2.0)
        note("with no slices: red left, blue right", red(probe(shot, 0.25, 0.5)) and blue(probe(shot, 0.75, 0.5)),
             (probe(shot, 0.25, 0.5), probe(shot, 0.75, 0.5)))

        print("  two slices, swapped")
        db.commands("SLICE ADD right half", "SLICE 1 CROP 50 0 50 100", "SLICE 1 PLACE 0 0 50 100",
                    "SLICE ADD left half", "SLICE 2 CROP 0 0 50 100", "SLICE 2 PLACE 50 0 50 100")
        shot = snap()
        note("blue on the left now", blue(probe(shot, 0.25, 0.5)), probe(shot, 0.25, 0.5))
        note("...and red on the right", red(probe(shot, 0.75, 0.5)), probe(shot, 0.75, 0.5))

        print("  edges, corners, masks")
        db.send("SLICE 1 EDGE R 30")
        shot = snap()
        edge, inner = probe(shot, 0.49, 0.5), probe(shot, 0.2, 0.5)
        note("a soft right edge fades slice 1 toward its edge (%s, inside %s)" % (edge, inner),
             edge is not None and inner is not None and edge[2] < inner[2] - 60, (edge, inner))
        db.send("SLICE 1 EDGE R 0")
        db.send("SLICE 1 CORNER TL 20 30")
        shot = snap()
        note("a pinned corner pulls the picture off it (black there)", dark(probe(shot, 0.04, 0.06)),
             probe(shot, 0.04, 0.06))
        note("...and the slice is still blue in its middle", blue(probe(shot, 0.3, 0.6)), probe(shot, 0.3, 0.6))
        db.send("SLICE 1 CORNER TL 0 0")
        mask = db.send("MASK NEW circle").split()[-1]
        db.send("SLICE 2 MASK %s" % mask)
        shot = snap()
        note("a mask on slice 2 cuts its corner", dark(probe(shot, 0.52, 0.06)), probe(shot, 0.52, 0.06))
        note("...keeps its middle", red(probe(shot, 0.75, 0.5)), probe(shot, 0.75, 0.5))
        note("...and leaves slice 1 whole", blue(probe(shot, 0.04, 0.06)), probe(shot, 0.04, 0.06))
        db.send("SLICE 2 OFF")
        shot = snap()
        note("an off slice is not drawn", dark(probe(shot, 0.75, 0.5)), probe(shot, 0.75, 0.5))
        db.send("SLICE 2 ON")

        print("  saved, refused, cleared")
        saved = db.saved()
        line = next((l for l in saved.splitlines() if l.startswith("output") and "right half" in l), "")
        note("the slices are saved with the output", "right half,1,0.5,0,0.5,1" in line and mask in line,
             line[-160:])
        for bad in ("SLICE 9 CROP 0 0 50 50", "SLICE 1 CROP 60 0 50 100", "SLICE 1 EDGE X 10",
                    "SLICE 1 CORNER MIDDLE 1 1", "SLICE 1 MASK nosuchmask", "SLICE 1 WOBBLE"):
            ok, reply = refused(db, bad)
            note("refused: %s" % bad, ok, reply)
        db.send("SLICE CLEAR")
        shot = snap()
        note("CLEAR goes back to the one picture", red(probe(shot, 0.25, 0.5)) and blue(probe(shot, 0.75, 0.5)),
             (probe(shot, 0.25, 0.5), probe(shot, 0.75, 0.5)))

        print("  flip, turn, opacity, an oscillator, a playlist as the source")
        db.commands("SLICE ADD whole", "SLICE 1 FLIP H ON")
        shot = snap()
        note("flipped across: blue left, red right", blue(probe(shot, 0.25, 0.5)) and red(probe(shot, 0.75, 0.5)),
             (probe(shot, 0.25, 0.5), probe(shot, 0.75, 0.5)))
        db.commands("SLICE 1 FLIP H OFF", "SLICE 1 PLACE 25 25 50 50", "SLICE 1 ROTATE OUT 180")
        shot = snap()
        note("turned half round: the small picture's left is blue", blue(probe(shot, 0.35, 0.5)),
             probe(shot, 0.35, 0.5))
        note("...and its right is red", red(probe(shot, 0.65, 0.5)), probe(shot, 0.65, 0.5))
        db.commands("SLICE 1 ROTATE OUT 0", "SLICE 1 PLACE 0 0 100 100", "SLICE 1 OPACITY 50")
        shot = snap()
        p = probe(shot, 0.25, 0.5)
        note("at 50%% opacity the red is half as bright (%s)" % (p,),
             p is not None and 100 < p[0] < 160 and p[1] < 40, p)
        db.commands("SLICE 1 OPACITY 100", "SLICE 1 LFO X DEPTH 1", "SLICE 1 LFO X RATE 1", "SLICE 1 LFO X ON")
        seen = set()
        for _ in range(16):
            q = probe(snap(0.13), 0.5, 0.5)
            if red(q):
                seen.add("red")
            if blue(q):
                seen.add("blue")
            if len(seen) == 2:
                break
        note("an oscillator on X slides the slice (the middle is red, then blue)", seen == {"red", "blue"}, seen)
        db.commands("SLICE 1 LFO X OFF", "DECKADD", "DECK 2", "PATTERN ADD full-white", "TAKE",
                    "OUTPUT SELECT 1", "SLICE 1 SOURCE DECK 2")
        shot = snap(1.0)
        note("a slice can take one playlist: playlist 2's white, not the programme",
             probe(shot, 0.25, 0.5) is not None and min(probe(shot, 0.25, 0.5)) > 200, probe(shot, 0.25, 0.5))
        saved = db.saved()
        line = next((l for l in saved.splitlines() if l.startswith("output") and "whole," in l), "")
        note("the source, turn, flip and opacity are saved", ",deck:1,0,0,0,0,1" in line, line[-200:])
        print("  a mesh on a slice")
        db.commands("SLICE 1 SOURCE PROGRAMME", "SLICE 1 GRID 3 3", "SLICE 1 GRID POINT 2 2 25 0")
        shot = snap()
        note("pulling the middle point right pushes the red into the blue there",
             red(probe(shot, 0.62, 0.5)), probe(shot, 0.62, 0.5))
        note("...but not at the top edge, whose points stay put", blue(probe(shot, 0.62, 0.02)),
             probe(shot, 0.62, 0.02))
        saved = db.saved()
        line = next((l for l in saved.splitlines() if l.startswith("output") and "whole," in l), "")
        note("the mesh is saved", "3 3 1 0 0 0 0 0 0 0 0 0.25 0" in line, line[-120:])
        for bad in ("SLICE 1 SOURCE DECK 9", "SLICE 1 ROTATE SIDEWAYS 10", "SLICE 1 FLIP Z ON",
                    "SLICE 1 OPACITY 150", "SLICE 1 LFO SIZE ON", "SLICE 1 GRID 12 3",
                    "SLICE 1 GRID POINT 9 9 1 1"):
            ok, reply = refused(db, bad)
            note("refused: %s" % bad, ok, reply)
        note("the desk still answers", db.send("PING").startswith("OK"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: slices\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-slices-"))
    try:
        run(exe, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\nslices: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\nslices: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
