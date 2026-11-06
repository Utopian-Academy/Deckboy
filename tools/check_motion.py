#!/usr/bin/env python3
"""Motion: a camera's movement drives the controls, checked on a playlist's
picture so it runs without a camera.

James, 2026-11-04 (board DGrGWwFS, "I say build it"), after his Max patch.
A clip of a white box crossing black from left to right plays on playlist 1,
and Motion reads that playlist:

  - something moving reads as motion, and nothing does with Motion off
  - Motion X follows the box to the right; mirrored, it goes the other way
  - two zones, left and right halves: the box is zone 1's, then zone 2's
  - an invert effect whose amount follows Motion turns the black background
    light while the box moves, and black again with Motion off
  - the settings and an oscillator's zone are saved; bad values are refused

Every window hidden. OUTSNAP is a local test input.

    py -3 tools/check_motion.py --exe build/windows/Release/Deckboy.exe
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


def snap(db, work, n):
    path = work / ("out%03d.bmp" % n)
    db.send("OUTSNAP %s 1" % path)
    for _ in range(100):
        if path.exists() and path.stat().st_size > 1000:
            time.sleep(0.1)
            return read_bmp(path)
        time.sleep(0.05)
    return None


def corner(shot):
    if not shot:
        return None
    width, height, pixel = shot
    return pixel(int(0.04 * width), int(0.9 * height))


def levels(db):
    """MOTION LEVEL -> {'whole': (amount, x, y), 'zone1': ...}"""
    reply = db.send("MOTION LEVEL")
    out = {}
    for name, a, x, y in re.findall(r"(\w+)=([\d.]+)@([\d.]+),([\d.]+)", reply):
        out[name] = (float(a), float(x), float(y))
    return out


def refused(db, command):
    try:
        reply = db.send(command)
    except RuntimeError as e:
        reply = str(e)
    return "ERR" in reply, reply


def run(exe, work):
    clip = work / "box.mp4"
    # Eight seconds: a 120 px box whose left edge goes from 0 to 520 px.
    subprocess.run(["ffmpeg", "-y", "-v", "error",
                    "-f", "lavfi", "-i", "color=c=black:s=640x360:r=30:d=8",
                    "-f", "lavfi", "-i", "color=c=white:s=120x120:r=30:d=8",
                    "-filter_complex", "[0][1]overlay=x='t*65':y=120:shortest=1,format=yuv420p",
                    "-c:v", "libx264", str(clip)], check=True)
    folder = work / "show"
    folder.mkdir()
    show = folder / "show.deckboy"
    show.write_text(SEED, encoding="utf8")
    with Runtime(exe, folder / "run", show, extra=("--import", str(clip))) as db:
        time.sleep(1.0)
        db.commands("SELECT 1", "LOOP ON", "OUTPUT ON")
        db.send("MOTION SOURCE DECK 1")
        db.send("MOTION MIRROR OFF")
        db.send("MOTION ZONE ADD 0 0 50 100")
        db.send("MOTION ZONE ADD 50 0 50 100")

        print("  off")
        db.send("TAKE")
        time.sleep(1.5)
        lv = levels(db)
        note("with Motion off nothing is read", lv.get("whole", (0,))[0] == 0.0, lv)

        print("  reading a playlist")
        db.send("TAKE")            # back to the start of the crossing
        db.send("MOTION ON")
        time.sleep(1.6)            # box near x = 110 of 640
        early = levels(db)
        time.sleep(4.2)            # box near x = 400
        late = levels(db)
        note("the moving box reads as motion (%.2f)" % early.get("whole", (0,))[0],
             early.get("whole", (0,))[0] > 0.05, early)
        note("Motion X follows it to the right (%.2f -> %.2f)" % (early.get("whole", (0, 0))[1],
                                                                  late.get("whole", (0, 0))[1]),
             late.get("whole", (0, 0))[1] - early.get("whole", (0, 1))[1] > 0.25, (early, late))
        note("zone 1 (left half) has it first", early.get("zone1", (0,))[0] > early.get("zone2", (0,))[0], early)
        note("zone 2 (right half) has it later", late.get("zone2", (0,))[0] > late.get("zone1", (0,))[0], late)

        db.send("MOTION MIRROR ON")
        db.send("TAKE")
        time.sleep(1.6)
        mirrored = levels(db)
        note("mirrored, the box on the left reads on the right (%.2f)" % mirrored.get("whole", (0, 0))[1],
             mirrored.get("whole", (0, 0))[1] > 0.6, mirrored)
        db.send("MOTION MIRROR OFF")

        print("  driving a control")
        db.send("FX ADD invert 0")
        db.send("FX LFO 1 E SHAPE motion")
        db.send("FX LFO 1 E DEPTH 1")
        db.send("FX LFO 1 E ON")
        # From the start of the crossing, as the reading above: on a slow
        # machine the box can have finished crossing by now, and a still
        # picture is no motion. The background is exactly black without the
        # invert, so any clear lift is the invert following Motion; how far
        # it lifts is the motion level (about 0.1 here), not a fixed number.
        db.send("TAKE")
        time.sleep(1.6)
        level = levels(db).get("whole", (0,))[0]
        moving = corner(snap(db, work, 1))
        note("an invert following Motion lifts the black background (%s, motion %.2f)" % (moving, level),
             moving is not None and moving[0] > 6, moving)
        db.send("MOTION OFF")
        time.sleep(1.0)
        still = corner(snap(db, work, 2))
        note("...and with Motion off it is black again (%s)" % (still,),
             still is not None and moving is not None and still[0] < moving[0] - 4, still)
        db.send("FX LFO 1 E ZONE 2")

        print("  saved, refused")
        saved = db.saved()
        line = next((l for l in saved.splitlines() if l.startswith("motion\t")), "")
        note("the Motion line is saved (%s)" % line.replace("\t", " | "),
             line.startswith("motion\t0\tdeck:0\t") and "0.5000,0.0000,0.5000,1.0000" in line, line)
        # The effect's amount oscillator: on, shape 8 (motion), five more
        # fields, then the zone.
        found = re.search(r"invert:[^|\t]*?1,8(,[^,;:|\t]*){5},2(?=[;:|\t]|$)", saved)
        note("the oscillator's shape and zone are saved", found is not None,
             re.findall(r"invert:[^|\t]*", saved)[:1])
        for bad in ("MOTION SENSITIVITY 200", "MOTION ZONE ADD 60 0 50 100", "MOTION SOURCE DECK 9",
                    "FX LFO 1 E ZONE 9", "MOTION BANANA"):
            ok, reply = refused(db, bad)
            note("refused: %s" % bad, ok, reply)

        print("  the theremin: a tone's level follows Motion")
        db.send("FX LFO 1 E ZONE 0")
        db.send("MOTION ON")
        db.commands("DECKADD", "DECK 2", "TONECUE", "TONECUE LFO LEVEL SHAPE motion",
                    "TONECUE LFO LEVEL DEPTH 1", "TONECUE LFO LEVEL ON", "TAKE")
        time.sleep(2.0)

        def tone_level():
            found = re.findall(r"audio_level=(\d+)", db.send("STATUS"))
            return int(found[1]) if len(found) > 1 else -1
        moving = max(tone_level() for _ in range(3))
        db.send("MOTION OFF")
        time.sleep(1.5)
        still = tone_level()
        note("the tone is louder while the box moves (%d) than with Motion off (%d)" % (moving, still),
             moving >= still + 6 and still > 0, (moving, still))
        ok, reply = refused(db, "TONECUE LFO PITCH ON")
        note("refused: TONECUE LFO PITCH (FREQ or LEVEL)", ok, reply)
        note("the desk still answers", db.send("PING").startswith("OK"))


def run_card(exe, work):
    """The MOTION CONTROL card, through the desk's own input: its switch,
    and a zone drawn by dragging on the picture and removed by a click."""
    folder = work / "card"
    folder.mkdir()
    show = folder / "show.deckboy"
    show.write_text(SEED, encoding="utf8")
    with Runtime(exe, folder / "run", show, extra=("--settings", "1", "--settings-scroll", "4000")) as db:
        time.sleep(1.5)
        print("  the settings card")
        found = re.search(r"at (\d+),(\d+) (\d+)x(\d+)", db.send("PRESS motion_on"))
        db.send("PRESS OFF")
        note("the card has a Motion switch", found is not None)
        if found:
            x, y, w, h = map(int, found.groups())
            db.send("CLICK %d %d" % (x + w // 2, y + h // 2))
            note("...which turns Motion on", db.send("MOTION").split()[2] == "on", db.send("MOTION"))
            db.send("CLICK %d %d" % (x + w // 2, y + h // 2))
            note("...and off again", db.send("MOTION").split()[2] == "off", db.send("MOTION"))
        pic = re.search(r"picture=(\d+),(\d+),(\d+),(\d+)", db.send("MOTION"))
        note("the card draws the motion picture", pic is not None and int(pic.group(3)) > 100,
             db.send("MOTION"))
        if not pic:
            return
        px, py, pw, ph = map(int, pic.groups())
        db.send("CLICK %d %d HOLD" % (px + pw // 10, py + ph // 10))
        for i in range(1, 7):
            db.send("HOVER %d %d" % (px + pw // 10 + (pw * 4 // 10) * i // 6, py + ph // 10 + (ph * 8 // 10) * i // 6))
            time.sleep(0.03)
        db.send("CLICK UP")
        zones = db.send("MOTION ZONE")
        found = re.search(r"([\d.]+),([\d.]+),([\d.]+),([\d.]+)", zones)
        note("dragging on the picture draws a zone (%s)" % zones.split(":")[-1].strip(),
             found is not None and abs(float(found.group(1)) - 0.1) < 0.03 and
             abs(float(found.group(3)) - 0.4) < 0.03 and abs(float(found.group(4)) - 0.8) < 0.03, zones)
        db.send("CLICK %d %d" % (px + pw * 3 // 10, py + ph // 2))
        note("clicking the zone removes it", "no zones" in db.send("MOTION ZONE"), db.send("MOTION ZONE"))
        db.send("CLICK %d %d" % (px + pw * 3 // 10, py + ph // 2))
        note("a click with no zone under it changes nothing", "no zones" in db.send("MOTION ZONE"))


def run_trails(exe, work):
    """Motion trails (the patch's melter) on the moving box: where the box
    has been stays white after it has gone; without the effect it is black."""
    clip = work / "trails.mp4"
    subprocess.run(["ffmpeg", "-y", "-v", "error",
                    "-f", "lavfi", "-i", "color=c=black:s=640x360:r=30:d=8",
                    "-f", "lavfi", "-i", "color=c=white:s=120x120:r=30:d=8",
                    "-filter_complex", "[0][1]overlay=x='t*65':y=120:shortest=1,format=yuv420p",
                    "-c:v", "libx264", str(clip)], check=True)
    folder = work / "trails"
    folder.mkdir()
    show = folder / "show.deckboy"
    show.write_text(SEED, encoding="utf8")
    with Runtime(exe, folder / "run", show, extra=("--import", str(clip))) as db:
        time.sleep(1.0)
        print("  motion trails")
        db.commands("SELECT 1", "LOOP ON", "OUTPUT ON", "TAKE")
        time.sleep(4.2)            # the box has passed x = 160 (it is near 270-390)
        plain = corner_at(snap(db, work, 21), 0.25, 0.5)
        note("without the effect, where the box was is black (%s)" % (plain,),
             plain is not None and max(plain) < 40, plain)
        db.send("FX ADD motion_trails 1")
        db.send("TAKE")
        time.sleep(4.2)
        trail = corner_at(snap(db, work, 22), 0.25, 0.5)
        note("with motion trails it stays white behind the box (%s)" % (trail,),
             trail is not None and min(trail) > 180, trail)
        ahead = corner_at(snap(db, work, 23), 0.9, 0.5)
        note("...and where the box has not been yet is still black (%s)" % (ahead,),
             ahead is not None and max(ahead) < 40, ahead)


def corner_at(shot, fx, fy):
    if not shot:
        return None
    width, height, pixel = shot
    return pixel(min(width - 1, int(fx * width)), min(height - 1, int(fy * height)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: motion\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-motion-"))
    try:
        run(exe, work)
        run_card(exe, work)
        run_trails(exe, work)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\nmotion: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\nmotion: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
