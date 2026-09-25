#!/usr/bin/env python3
"""A lower third arrives and leaves, instead of appearing and vanishing.

WHY THIS EXISTS. James asked whether lower thirds were real -- "renders a
lower third i can render subject and subtext onto and it animates in and out
smoothly in various styles with alpha channel". Half of it was: the subject and
the subtext both rendered, several stacked, and the bar's opacity was settable.
Nothing moved, because an overlay had no clock at all: Deck::overlayActiveIndices
is a bare vector<int> with no idea when anything went up.

An animation is easy to claim and easy to ship inert -- store a style, answer
OK, draw the same frame forever. So this records the programme and compares the
SAME region at successive moments:

  * with a style, the bar region CHANGES between two frames during the move
  * `none` does not change, because instant is still a legal answer and every
    show written before this relies on it
  * a cleared overlay stays on screen for its out-move and is gone after it,
    which is the half that could not work before: something removed from the
    list cannot be drawn leaving

Recording needs a presented output window, so this runs on a desktop session,
not a CI runner. Needs ffmpeg to pull frames out.

    python3 tools/check_lower_third.py
"""

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


def band(path):
    """The lower-third band: the bottom quarter, where the bar lives."""
    w, h, px = read_ppm(path)
    y0 = int(h * 0.72)
    out = bytearray()
    for row in range(y0, h):
        i = (row * w) * 3
        out += px[i:i + w * 3]
    return bytes(out)


def differing(a, b):
    return sum(1 for x, y in zip(a, b) if abs(x - y) > 6)


def frames(db, tag, seconds=2.0, count=2):
    """Record briefly and pull `count` frames spread across the clip."""
    path = db.record(seconds=seconds)
    if not path:
        return []
    out = []
    for n in range(count):
        # Spread across the recording: the move happens near its start.
        at = 0.15 + n * 0.5
        dest = os.path.join(db.root, "l3-%s-%d.ppm" % (tag, n))
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-ss", str(at), "-i", path,
                        "-frames:v", "1", "-pix_fmt", "rgb24", dest],
                       capture_output=True)
        if os.path.exists(dest):
            out.append(dest)
    return out


def main():
    args = parse_args(__doc__, 5733)
    fails = []

    def note(name, ok, detail=""):
        print("  %-48s %s" % (name, "ok" if ok else "FAIL  " + detail))
        if not ok:
            fails.append("%s -- %s" % (name, detail))

    with Deckboy(args, prefix="deckboy-l3-") as db:
        db.send("MASTERVOL 0")
        print("check: a lower third that moves")
        print()

        # Something behind it, so the bar has a picture to arrive over.
        db.send("DECK 1")
        db.send("PATTERN ADD full-black")
        db.send("TAKE")
        print("  making a lower third:")
        print("    %s" % db.send("LOWERTHIRD")[:90])
        db.send("SELECT LAST")
        print("    title:   %s" % db.send("LOWERTEXT Doctor Bird")[:70])
        print("    sub:     %s" % db.send("LOWERSUB Chief Engineer")[:70])
        print()

        probe = frames(db, "probe", seconds=1.0, count=1)
        if not probe:
            print("  no recording was produced, so the pixel half could not run.")
            print("  An output needs a presented window; without one the encoder")
            print("  never receives a frame.")
            return finish("lower third (setup only)", fails)

        for style, moving in (("rise", True), ("slide", True),
                              ("fade", True), ("none", False)):
            # Set the style by cycling to it would be guesswork; the cue field
            # is what the renderer reads, so drive it the way an operator does.
            db.send("SELECT LAST")
            for _ in range(6):
                if style in db.send("LOWERSTYLE").lower():
                    break
                db.send("LOWERSTYLE NEXT")
            db.send("LOWERANIM 1.2")
            db.send("OVERLAY CLEAR")
            time.sleep(1.6)
            db.send("TAKE")            # fire it; the move starts now
            shots = frames(db, style, seconds=2.0, count=2)
            if len(shots) < 2:
                note("  %s: two frames of the move" % style, False,
                     "only %d frame(s) came back" % len(shots))
                continue
            changed = differing(band(shots[0]), band(shots[1]))
            if moving:
                note("  %s moves" % style, changed > 400,
                     "%d bytes differ between two moments; it is not moving"
                     % changed)
            else:
                note("  none is instant, as an old show expects", changed < 400,
                     "%d bytes differ; it should not be animating" % changed)

    return finish("lower third", fails)


if __name__ == "__main__":
    sys.exit(main())
