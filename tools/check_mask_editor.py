#!/usr/bin/env python3
"""The mask editor, driven the way an operator drives it.

James, 2026-11-04 (board X2N9qJe1, approved): pen-tool masks with points to
add and remove, a library of shapes, invert, and the controls each on an
oscillator. Every step goes through the desk's own input -- the MASK button,
the inspector's MASK rows, the library dropdown, presses and drags on the
programme monitor -- and is read back from the mask (MASK INFO, the editor's
own report in STATUS JSON) and from the frame the output presents:

  - MASK in the monitor's header opens the editor; the inspector's MASK
    section picks "new heart" from the library and the output is heart-cut
  - dragging the heart's point moves it, on the monitor and on the output
  - a click on the outline adds a point; Delete takes the selected one away
  - the pen, set to subtract, draws a closed path that cuts the middle out
  - feather is typed into its row; its ~ chip puts it on an oscillator
  - invert from its row turns the cut inside out; MASK closes the editor

Every window hidden. CLICK, HOVER, KEY, TYPE, QUICK and OUTSNAP are local
test inputs.

    py -3 tools/check_mask_editor.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import json
import re
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


class Desk:
    def __init__(self, db, work):
        self.db, self.work, self.n = db, work, 0

    def status(self):
        return json.loads(self.db.send("STATUS JSON"))

    def editor(self):
        return self.status().get("maskEditor", {})

    def info(self, mask):
        return dict(kv.split("=", 1) for kv in self.db.send("MASK INFO " + mask).split("OK MASK", 1)[-1].split()
                    if "=" in kv)

    def rows(self):
        found = []
        for line in self.db.send("QUICKLIST").splitlines():
            bits = line.split(" ", 2)
            if len(bits) == 3 and bits[0].lstrip("-").isdigit():
                found.append((int(bits[0]), int(bits[1]), bits[2]))
        return found

    def row(self, tip):
        """The buttons of the row whose tip starts like this: dec, value, inc (or one)."""
        return [(a, p) for a, p, t in self.rows() if t.startswith(tip)]

    def press(self, tip, which=0):
        hits = self.row(tip)
        if not hits:
            return False
        self.db.send("QUICK %d %d" % hits[min(which, len(hits) - 1)])
        time.sleep(0.25)
        return True

    def click(self, x, y):
        self.db.send("CLICK %d %d" % (x, y))
        time.sleep(0.15)

    def drag(self, x0, y0, x1, y1):
        self.db.send("CLICK %d %d HOLD" % (x0, y0))
        time.sleep(0.1)
        steps = 6
        for i in range(1, steps + 1):
            self.db.send("HOVER %d %d" % (x0 + (x1 - x0) * i // steps, y0 + (y1 - y0) * i // steps))
            time.sleep(0.04)
        self.db.send("CLICK UP")
        time.sleep(0.2)

    def choose(self, label):
        """Click a row of the open dropdown by its label."""
        dd = self.status().get("dropdown", {})
        rows = dd.get("rows", [])
        if not dd.get("open") or label not in rows:
            return False
        i = rows.index(label)
        r = dd["rect"]
        self.click(r[0] + r[2] // 2, dd["listY"] + dd["rowHeight"] * i + dd["rowHeight"] // 2)
        time.sleep(0.2)
        return True

    def snap(self):
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


def probe(shot, fx, fy):
    if not shot:
        return None
    width, height, pixel = shot
    return pixel(min(width - 1, int(fx * width)), min(height - 1, int(fy * height)))


def white(p):
    return p is not None and min(p) > 200


def black(p):
    return p is not None and max(p) < 50


def run(exe, work, show):
    with Runtime(exe, work / "run", show) as db:
        desk = Desk(db, work)
        time.sleep(1.0)
        db.send("DECK 1")
        db.send("PATTERN ADD full-white")
        db.send("TAKE")
        db.send("OUTPUT SELECT 1")
        db.send("OUTPUT ON")
        time.sleep(2.5)

        print("  opening it")
        ed = desk.editor()
        note("the editor starts closed", ed.get("open") is False, ed)
        b = ed.get("button", [0, 0, 0, 0])
        note("MASK sits in the monitor's header", b[2] > 0 and b[3] > 0, b)
        desk.click(b[0] + b[2] // 2, b[1] + b[3] // 2)
        note("MASK opens the editor", desk.editor().get("open") is True, desk.editor())
        note("the inspector has a mask row", desk.press("Choose a mask from this show"), desk.rows()[:3])
        note("...which opens the library", desk.choose("new heart"), desk.status().get("dropdown"))
        ed = desk.editor()
        mask = ed.get("mask", "")
        paths = ed.get("paths", [])
        note("\"new heart\" puts a heart on the cue (%s)" % mask,
             mask.startswith("m") and len(paths) == 1 and len(paths[0]["points"]) == 4, ed)
        shot = desk.snap()
        note("...and the output is heart-cut: middle kept, corner black",
             white(probe(shot, 0.5, 0.5)) and black(probe(shot, 0.04, 0.06)), (probe(shot, 0.5, 0.5),
                                                                                probe(shot, 0.04, 0.06)))
        if not paths:
            return

        print("  points")
        tip = paths[0]["points"][2]          # the heart's point, at the bottom
        area = ed["area"]
        desk.drag(tip[0], tip[1], tip[0], tip[1] + 40)
        moved = desk.editor()["paths"][0]["points"][2]
        note("dragging the heart's point moves it 40 px down the monitor (%d)" % (moved[1] - tip[1]),
             abs(moved[1] - tip[1] - 40) <= 2, (tip, moved))
        below = (tip[1] + 25 - area[1]) / area[3]
        shot = desk.snap()
        note("...and on the output, where the point now reaches", white(probe(shot, 0.5, below)),
             probe(shot, 0.5, below))
        # Half-way along the edge from the right lobe to the point: on the outline.
        pts = desk.editor()["paths"][0]["points"]
        mx, my = (pts[2][0] + pts[3][0]) // 2, (pts[2][1] + pts[3][1]) // 2
        seen = None
        for dx in range(0, 30, 2):           # walk out from the chord to the curve
            desk.click(mx + dx, my)
            count = len(desk.editor()["paths"][0]["points"])
            if count == 5:
                seen = dx
                break
        note("a click on the outline adds a point", seen is not None, desk.editor()["paths"][0]["points"])
        db.send("KEY Delete")
        time.sleep(0.2)
        note("Delete takes the selected point away again", len(desk.editor()["paths"][0]["points"]) == 4,
             desk.editor()["paths"][0]["points"])

        print("  the pen")
        note("the tool row switches to the pen", desk.press("Pen: click for corners"), desk.rows()[:3])
        note("...and the editor says so", desk.editor().get("tool") == "pen", desk.editor())
        desk.press("Whether the next shape or pen path adds or cuts")
        cx, cy = area[0] + area[2] // 2, area[1] + area[3] // 2
        corners = [(cx - 30, cy - 30), (cx + 30, cy - 30), (cx + 30, cy + 30), (cx - 30, cy + 30)]
        for x, y in corners:
            desk.click(x, y)
        note("four clicks draw an open path", desk.editor().get("drawing") is True, desk.editor())
        desk.click(*corners[0])
        ed = desk.editor()
        cut = ed["paths"][-1] if ed.get("paths") else {}
        note("clicking the first point closes it", ed.get("drawing") is False and cut.get("closed") is True
             and len(cut.get("points", [])) == 4, ed)
        note("...as a subtract, since the row said so", cut.get("subtract") is True, cut)
        shot = desk.snap()
        note("...and it cuts the middle out of the heart", black(probe(shot, 0.5, 0.5)), probe(shot, 0.5, 0.5))
        desk.press("Pen: click for corners")

        print("  its controls")
        feather = desk.row("How soft the edge is")
        # The ~ chip is its own button, addressed by the oscillator it arms:
        # the mask range (1 << 23) plus feather's slot (4).
        chip = [(a, p) for a, p, t in desk.rows() if p == (1 << 23) + 4]
        note("feather has a row with - value + and a ~ chip", len(feather) == 3 and len(chip) == 1,
             (feather, chip))
        if len(feather) == 3 and chip:
            feather = feather + chip
            db.send("QUICK %d %d" % feather[1])
            time.sleep(0.2)
            db.send("TYPE 5")
            db.send("KEY Return")
            time.sleep(0.3)
            note("feather typed as 5 reads back 5", abs(float(desk.info(mask).get("feather", -1)) - 5) < 0.01,
                 desk.info(mask))
            db.send("QUICK %d %d" % feather[-1])
            time.sleep(0.2)
            note("its ~ chip puts it on an oscillator", "feather" in desk.info(mask).get("lfo", ""), desk.info(mask))
            db.send("QUICK %d %d" % feather[-1])
        desk.press("Keep the outside and cut the inside")
        note("invert from its row", desk.info(mask).get("invert") == "on", desk.info(mask))
        shot = desk.snap()
        note("...turns the cut inside out: the corner is kept", white(probe(shot, 0.04, 0.06)),
             probe(shot, 0.04, 0.06))

        print("  closing")
        b = desk.editor()["button"]
        desk.click(b[0] + b[2] // 2, b[1] + b[3] // 2)
        note("MASK closes the editor", desk.editor().get("open") is False, desk.editor())

        print("  taking it off, and deleting it")
        name = desk.info(mask).get("name", "")
        note("REMOVE has its own row", desk.press("Stop masking this"), desk.rows()[:6])
        note("...which takes the mask off the cue", not desk.editor().get("mask"), desk.editor())
        note("...and leaves it in the library", mask in db.send("MASK LIST"), db.send("MASK LIST"))
        shot = desk.snap()
        note("...so the corner is no longer cut", not black(probe(shot, 0.04, 0.06)), probe(shot, 0.04, 0.06))
        desk.press("Choose a mask from this show's library")
        note("it can be put back from the library", desk.choose(name), desk.status().get("dropdown"))
        note("...and is on the cue again", desk.editor().get("mask") == mask, desk.editor())
        note("DELETE has its own row", desk.press("Delete this mask from the show"), desk.rows()[:8])
        note("...which takes it out of the library", mask not in db.send("MASK LIST"), db.send("MASK LIST"))
        note("...and off the cue", not desk.editor().get("mask"), desk.editor())
        db.send("UNDO")
        time.sleep(0.3)
        note("undo brings it back", mask in db.send("MASK LIST"), db.send("MASK LIST"))
        note("the desk still answers", db.send("PING").startswith("OK"))


def run_settings(exe, work, show):
    """Settings -> Video Outputs -> Matte & Overlay has an Output mask row
    that opens the same library, with no cue selected."""
    with Runtime(exe, work / "run-settings", show, extra=("--settings", "3.1")) as db:
        desk = Desk(db, work)
        time.sleep(1.0)
        db.send("DECK 1")
        db.send("PATTERN ADD full-white")
        db.send("TAKE")
        db.send("OUTPUT SELECT 1")
        db.send("OUTPUT ON")
        time.sleep(2.5)

        print("  from the output's settings")
        found = re.search(r"at (\d+),(\d+) (\d+)x(\d+)", db.send("PRESS output_mask"))
        db.send("PRESS OFF")
        note("Matte & Overlay has an Output mask row", found is not None, found)
        if not found:
            return
        x, y, w, h = map(int, found.groups())
        desk.click(x + w // 2, y + h // 2)
        dd = desk.status().get("dropdown", {})
        note("...which opens the mask library", dd.get("open") is True and "new circle" in dd.get("rows", []), dd)
        note("...and picking new circle", desk.choose("new circle"), desk.status().get("dropdown"))
        ed = desk.editor()
        note("...puts it on the output (MASK ON follows)", ed.get("target") == 2 and ed.get("mask"), ed)
        shot = desk.snap()
        note("...which cuts the corner to black", black(probe(shot, 0.04, 0.06)), probe(shot, 0.04, 0.06))
        note("...and keeps the middle", white(probe(shot, 0.5, 0.5)), probe(shot, 0.5, 0.5))
        desk.click(x + w // 2, y + h // 2)
        note("none takes it off again", desk.choose("none"), desk.status().get("dropdown"))
        shot = desk.snap()
        note("...and the corner is white", white(probe(shot, 0.04, 0.06)), probe(shot, 0.04, 0.06))
        note("the desk still answers", db.send("PING").startswith("OK"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: the mask editor\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-mask-editor-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "02-running-a-show.deckboy"
    try:
        run(exe, work, show)
        run_settings(exe, work, show)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\nmask editor: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\nmask editor: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
