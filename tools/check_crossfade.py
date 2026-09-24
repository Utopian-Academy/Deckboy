#!/usr/bin/env python3
"""The output's crossfader actually mixes the picture.

WHY THIS EXISTS. VJ mode was a global that claimed the programme output and
knew about exactly two decks. It is now a property of an output: a fader
between any two entries of that output's own stack, with the blend taken from
the layer. An old show migrates onto it on load.

A fader is the easiest control in the world to ship inert -- it stores a number,
the verb answers OK, and the picture never moves. So this records the programme
and reads the actual brightness at each position:

    base = full black, layer = full white, dissolve

      mix 0    -> the picture is the BASE          (dark)
      mix 50   -> between the two, and strictly between
      mix 100  -> the picture is the LAYER         (bright)

The middle reading is the one that matters. A fader wired to a boolean passes
"0 is dark, 100 is bright" perfectly and fails here, and so does one whose ends
are swapped.

Recording needs a presented output window, so this runs on a desktop session,
not on a CI runner. Needs ffmpeg to pull a frame from the file.

    python3 tools/check_crossfade.py
"""

import io
import os
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, finish, parse_args  # noqa: E402


def read_ppm(path):
    with open(path, "rb") as handle:
        data = handle.read()
    fields, at = [], 2
    while len(fields) < 3:
        while at < len(data) and data[at:at + 1].isspace():
            at += 1
        start = at
        while at < len(data) and not data[at:at + 1].isspace():
            at += 1
        fields.append(int(data[start:at]))
    return fields[0], fields[1], data[at + 1:]


def centre_mean(path, frac=0.4):
    """Mean brightness of the middle of the frame, away from any furniture."""
    w, h, px = read_ppm(path)
    pw, ph = max(1, int(w * frac)), max(1, int(h * frac))
    x0, y0 = (w - pw) // 2, (h - ph) // 2
    total = count = 0
    for row in range(y0, y0 + ph):
        base = (row * w + x0) * 3
        for i in range(base, base + pw * 3):
            total += px[i]
            count += 1
    return total / max(1, count)


def frame_at(db, tag):
    """Record briefly and pull one frame out as a PPM, or None."""
    path = db.record(seconds=2.5)
    if not path:
        return None
    out = os.path.join(db.root, "xf-%s.ppm" % tag)
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-sseof", "-1", "-i", path,
                    "-frames:v", "1", "-pix_fmt", "rgb24", out],
                   capture_output=True)
    return out if os.path.exists(out) else None


def main():
    args = parse_args(__doc__, 5725)
    fails = []

    def note(name, ok, detail=""):
        print("  %-46s %s" % (name, "ok" if ok else "FAIL  " + detail))
        if not ok:
            fails.append("%s -- %s" % (name, detail))

    with Deckboy(args, prefix="deckboy-xfade-") as db:
        db.send("MASTERVOL 0")

        print("check: the output's crossfader")
        print()
        print("  what it refuses before there is anything to fade:")
        note("  a fader needs two stack entries",
             db.send("CROSSFADE 50").upper().startswith("ERR"),
             db.send("CROSSFADE 50"))
        print()

        print("  a black base with a white layer over it:")
        db.send("DECK 1")
        db.send("PATTERN ADD full-black")
        db.send("TAKE")
        db.send("DECKADD")
        db.send("DECK 2")
        db.send("PATTERN ADD full-white")
        db.send("TAKE")
        db.send("DECK 2")
        print("    assign as a layer: %s" % db.send("VIDEO OUTPUT ASSIGN"))
        print("    stack:             %s" % db.send("VIDEO OUTPUT LAYER"))
        print()

        print("  what it refuses once it can fade:")
        note("  two of the same position",
             db.send("CROSSFADE 1 1").upper().startswith("ERR"),
             db.send("CROSSFADE 1 1"))
        note("  a position the stack does not have",
             db.send("CROSSFADE 1 9").upper().startswith("ERR"),
             db.send("CROSSFADE 1 9"))
        print()

        print("    %s" % db.send("CROSSFADE 1 2 0"))
        time.sleep(1.2)
        readings = {}
        for mix in (0, 50, 100):
            db.send("CROSSFADE %d" % mix)
            time.sleep(1.2)
            frame = frame_at(db, str(mix))
            if not frame:
                print()
                print("  no recording was produced, so the pixel half could not")
                print("  run. On a machine with no display session that is")
                print("  expected: an output needs a window, and without one the")
                print("  encoder never receives a frame. The refusals above ran.")
                return finish("crossfade (refusals only)", fails)
            readings[mix] = centre_mean(frame)
            print("    mix %3d%%   centre %.1f" % (mix, readings[mix]))

        print()
        note("0% shows the base", readings[0] < 40.0,
             "centre %.1f, expected near black" % readings[0])
        note("100% shows the layer", readings[100] > 215.0,
             "centre %.1f, expected near white" % readings[100])
        # THE ONE THAT CATCHES A FADER WIRED TO A BOOLEAN.
        note("50% is strictly between the two",
             readings[0] + 25.0 < readings[50] < readings[100] - 25.0,
             "centre %.1f, with ends %.1f and %.1f"
             % (readings[50], readings[0], readings[100]))

        print()
        print("  and switching it off leaves the stack composited plain:")
        print("    %s" % db.send("CROSSFADE OFF"))
        time.sleep(1.2)
        frame = frame_at(db, "off")
        if frame:
            off = centre_mean(frame)
            print("    off        centre %.1f" % off)
            note("  the layer is fully over the base again", off > 215.0,
                 "centre %.1f, expected the layer at full" % off)

    return finish("crossfade", fails)


if __name__ == "__main__":
    sys.exit(main())
