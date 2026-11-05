#!/usr/bin/env python3
"""The Draw source: strokes land on the output, drawn over the wire and with
the mouse on the programme monitor; erase, frames, playback, and the saves.

James, 2026-11-04 (board X2N9qJe1): "a source which gives you tools and a
pallete to draw on the output, and save what you drew as stills or even
possibly animations" -- a flipbook and stroke replay both.

A Draw cue on a playlist over black, read off the frame the output presents:

  - a red stroke over the wire is red on the output, and transparent around it
  - dragging on the monitor with DRAW open draws there too
  - the eraser takes ink back; UNDO and CLEAR
  - a flipbook of two frames shows each in turn; replay draws, then holds
  - the drawing and the brush survive the save
  - SAVE STILL writes a PNG with transparency and adds it as a cue;
    SAVE ANIMATION writes a video with alpha and adds it as a cue

Every window hidden. OUTSNAP is a local test input.

    py -3 tools/check_draw.py --exe build/windows/Release/Deckboy.exe
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

    def __call__(self, settle=0.6):
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
    return p is not None and p[0] > 180 and p[1] < 80 and p[2] < 80


def dark(p):
    return p is not None and max(p) < 40


def white(p):
    return p is not None and min(p) > 200


def refused(db, command):
    try:
        reply = db.send(command)
    except RuntimeError as e:
        reply = str(e)
    return "ERR" in reply, reply


def run(exe, work):
    folder = work / "show"
    folder.mkdir()
    show = folder / "show.deckboy"
    show.write_text(SEED, encoding="utf8")
    with Runtime(exe, folder / "run", show) as db:
        time.sleep(1.0)
        snap = Snaps(db, work)
        ok, reply = refused(db, "DRAW STROKE 10,10 20,20")
        note("with no Draw cue selected, a stroke is refused", ok, reply)
        db.send("PATTERN ADD draw")
        db.commands("DRAW TOOL brush", "DRAW COLOUR 3", "DRAW SIZE 4", "OUTPUT ON")
        print("  over the wire")
        note("DRAW STROKE adds a stroke", "strokes here: 1" in db.send("DRAW STROKE 10,50 50,50 90,50"))
        db.send("TAKE")
        shot = snap(1.0)
        note("the stroke is red on the output", red(probe(shot, 0.5, 0.5)), probe(shot, 0.5, 0.5))
        note("...and the picture around it is clear (black)", dark(probe(shot, 0.5, 0.2)), probe(shot, 0.5, 0.2))
        db.commands("DRAW TOOL erase", "DRAW SIZE 10")
        db.send("DRAW STROKE 50,20 50,80")
        shot = snap()
        note("the eraser takes the ink back where it goes", dark(probe(shot, 0.5, 0.5)), probe(shot, 0.5, 0.5))
        note("...and only there", red(probe(shot, 0.25, 0.5)), probe(shot, 0.25, 0.5))
        db.send("DRAW UNDO")
        shot = snap()
        note("UNDO brings it back", red(probe(shot, 0.5, 0.5)), probe(shot, 0.5, 0.5))
        db.commands("DRAW TOOL box", "DRAW COLOUR white", "DRAW SIZE 1")
        db.send("DRAW STROKE 20,20 80,80")
        shot = snap()
        note("a box is an outline", white(probe(shot, 0.2, 0.35)) and not white(probe(shot, 0.35, 0.35)),
             (probe(shot, 0.2, 0.35), probe(shot, 0.35, 0.35)))

        print("  with the mouse on the monitor")
        db.commands("DRAW TOOL brush", "DRAW COLOUR 6", "DRAW SIZE 4")
        db.send("DRAW EDITOR ON")
        status = db.send("DRAW")
        area = re.search(r"area=(\d+),(\d+),(\d+),(\d+)", status)
        note("DRAW EDITOR ON opens the editor on the monitor", "editor=on" in status and area is not None, status)
        if area:
            ax, ay, aw, ah = map(int, area.groups())
            y = ay + ah * 3 // 4
            db.send("CLICK %d %d HOLD" % (ax + aw // 10, y))
            for i in range(1, 13):
                db.send("HOVER %d %d" % (ax + aw // 10 + (aw * 8 // 10) * i // 12, y))
                time.sleep(0.02)
            db.send("CLICK UP")
            shot = snap()
            p = probe(shot, 0.5, 0.75)
            note("dragging on the monitor draws a green line there (%s)" % (p,),
                 p is not None and p[1] > 150 and p[0] < 100, p)
        db.send("DRAW EDITOR OFF")

        print("  frames and playback")
        db.send("DRAW CLEAR")
        db.commands("DRAW TOOL fill", "DRAW COLOUR red", "DRAW STROKE 50,50")
        db.send("DRAW FRAME ADD")
        db.commands("DRAW COLOUR white", "DRAW STROKE 50,50")
        status = db.send("DRAW")
        note("two frames, one fill on each", "frames=2" in status and "here=1" in status, status)
        db.commands("DRAW PLAYBACK flipbook", "DRAW FPS 2", "TAKE")
        seen = set()
        for _ in range(12):
            p = probe(snap(0.2), 0.5, 0.5)
            if red(p):
                seen.add("red")
            if white(p):
                seen.add("white")
            if len(seen) == 2:
                break
        note("the flipbook shows each frame in turn", seen == {"red", "white"}, seen)
        db.commands("DRAW FRAME 1", "DRAW FRAME DELETE", "DRAW CLEAR", "DRAW TOOL brush", "DRAW COLOUR red",
                    "DRAW SIZE 4", "DRAW PLAYBACK replay", "DRAW HOLD 3")
        db.send("DRAW STROKE " + " ".join("%d,50" % x for x in range(5, 96, 2)))
        db.send("TAKE")
        early = snap(0.25)
        late = snap(1.2)
        note("replay draws the line from its start (left first)",
             red(probe(early, 0.08, 0.5)) and not red(probe(early, 0.9, 0.5)),
             (probe(early, 0.08, 0.5), probe(early, 0.9, 0.5)))
        note("...then holds it whole", red(probe(late, 0.9, 0.5)), probe(late, 0.9, 0.5))

        print("  saved, and saving")
        saved = db.saved()
        line = next((l for l in saved.splitlines() if l.startswith("draw_brush\t")), "")
        note("the brush is saved (%s)" % line.replace("\t", " | "), line.startswith("draw_brush\tbrush\te03030ff"), line)
        note("the drawing is saved with its cue", "v1|replay|" in saved)
        still = work / "still.png"
        reply = db.send("DRAW SAVE STILL %s" % still)
        data = still.read_bytes() if still.exists() else b""
        note("SAVE STILL writes a PNG with transparency", data[:8] == b"\x89PNG\r\n\x1a\n" and data[25] == 6,
             (reply, data[:26]))
        note("...and adds it as a cue", "still.png" in db.saved(), "")
        movie = work / "anim.mov"
        db.send("DRAW SAVE ANIMATION %s" % movie)
        for _ in range(120):
            status = db.send("DRAW")
            if "saving=1" not in status:
                break
            time.sleep(0.25)
        note("SAVE ANIMATION finishes (%s)" % status.split("last_save=")[-1][:60], "last_save=\"saved" in status,
             status)
        probe_out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                                    "stream=codec_name,pix_fmt", "-of", "csv=p=0", str(movie)],
                                   capture_output=True, text=True).stdout.strip()
        note("...as a ProRes 4444 video with alpha (%s)" % probe_out, "prores" in probe_out and "yuva" in probe_out,
             probe_out)
        for bad in ("DRAW TOOL crayon", "DRAW COLOUR mauve-ish", "DRAW SIZE 90", "DRAW PLAYBACK backwards",
                    "DRAW FPS 0", "DRAW STROKE banana"):
            ok, reply = refused(db, bad)
            note("refused: %s" % bad, ok, reply)
        note("the desk still answers", db.send("PING").startswith("OK"))


def run_layers(exe, work):
    """Transparent on a layer: the Draw cue itself, and its saved animation
    played back as a video, both over a white playlist."""
    folder = work / "layers"
    folder.mkdir()
    show = folder / "show.deckboy"
    show.write_text(SEED, encoding="utf8")
    movie = work / "layer.mov"
    with Runtime(exe, folder / "run", show) as db:
        time.sleep(1.0)
        snap = Snaps(db, work)
        print("  on a layer over white")
        db.commands("DECK 1", "PATTERN ADD full-white", "TAKE", "OUTPUT SELECT 1", "OUTPUT ON", "DECKADD", "DECK 2",
                    "PATTERN ADD draw", "DRAW TOOL brush", "DRAW COLOUR red", "DRAW SIZE 6",
                    "DRAW STROKE 10,50 50,50 90,50", "TAKE", "DECK 2", "VIDEO OUTPUT ASSIGN")
        shot = snap(2.0)
        note("the Draw cue's ink is on the white", red(probe(shot, 0.5, 0.5)), probe(shot, 0.5, 0.5))
        note("...and the white shows round it", white(probe(shot, 0.05, 0.1)), probe(shot, 0.05, 0.1))
        db.send("DRAW SAVE ANIMATION %s" % movie)
        for _ in range(240):
            if "saving=1" not in db.send("DRAW"):
                break
            time.sleep(0.25)
        db.send("SELECT 2")
        db.send("TAKE")
        shot = snap(2.5)
        note("the saved animation plays with its ink", red(probe(shot, 0.5, 0.5)), probe(shot, 0.5, 0.5))
        note("...and its transparency: the white shows round it", white(probe(shot, 0.05, 0.1)),
             probe(shot, 0.05, 0.1))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: draw\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-draw-"))
    try:
        run(exe, work)
        run_layers(exe, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\ndraw: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\ndraw: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
