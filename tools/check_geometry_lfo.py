#!/usr/bin/env python3
"""An oscillator on a cue's geometry actually moves the picture.

WHY THIS EXISTS. James asked for oscillators on the geometry rows. The feature
was built in another session (7b5483c) and covers more than this check was
first written against -- all nine rows including the four crops. What it did not
come with was a measurement, and this is the half that cannot be done by
watching.

A slow oscillator at low depth looks EXACTLY like nothing happening until it
has had time to move, which makes this the worst kind of feature to verify by
watching. So this records the programme and compares frames:

  * with an oscillator armed on X, the picture is in a different place at two
    moments
  * with it off, the same two moments are identical -- a cue that is not
    modulated must be rock steady, and a "moving" picture that moves when
    nothing is armed is worse than one that never moves
  * the operator's own number is NEVER changed: the swing is a copy applied at
    draw time, so the cue still reports the offset that was typed

That last one is the one a person cannot see at all. An LFO that edited the cue
would ratchet its value away and there would be no way back to what was set.

Recording needs a presented output window, so this runs on a desktop session,
not a CI runner.

    python3 tools/check_geometry_lfo.py
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


def differing(a, b):
    _, _, pa = read_ppm(a)
    _, _, pb = read_ppm(b)
    return sum(1 for x, y in zip(pa, pb) if abs(x - y) > 8)


def two_frames(db, tag):
    """Two moments from one short recording, far enough apart to have moved."""
    path = db.record(seconds=3.0)
    if not path:
        return []
    out = []
    for n, at in enumerate((0.3, 1.8)):
        dest = os.path.join(db.root, "geo-%s-%d.ppm" % (tag, n))
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-ss", str(at), "-i", path,
                        "-frames:v", "1", "-pix_fmt", "rgb24", dest],
                       capture_output=True)
        if os.path.exists(dest):
            out.append(dest)
    return out


def main():
    args = parse_args(__doc__, 5737)
    fails = []

    def note(name, ok, detail=""):
        print("  %-50s %s" % (name, "ok" if ok else "FAIL  " + detail))
        if not ok:
            fails.append("%s -- %s" % (name, detail))

    with Deckboy(args, prefix="deckboy-geolfo-") as db:
        db.send("MASTERVOL 0")
        print("check: an oscillator on the geometry")
        print()

        # Bars, not a flat colour: a picture that is the same everywhere cannot
        # be seen to move, and would make every reading below zero.
        db.send("DECK 1")
        db.send("PATTERN ADD smpte-bars")
        db.send("TAKE")
        time.sleep(1.0)

        print("  what it refuses:")
        note("  a parameter that is not one",
             db.send("GEOLFO banana on").upper().startswith("ERR"),
             db.send("GEOLFO banana on"))
        note("  a shape that does not exist",
             db.send("GEOLFO X shape wobbleflop").upper().startswith("ERR"),
             db.send("GEOLFO X shape wobbleflop"))
        print()

        print("  with nothing armed:")
        print("    %s" % db.send("GEOLFO"))
        still = two_frames(db, "still")
        if len(still) < 2:
            print()
            print("  no recording was produced, so the pixel half could not run.")
            print("  An output needs a presented window; without one the encoder")
            print("  never receives a frame. The refusals above did run.")
            return finish("geometry lfo (refusals only)", fails)
        steady = differing(still[0], still[1])
        note("  the picture is steady", steady < 2000,
             "%d bytes differ with no oscillator armed" % steady)
        print("    %d bytes differ" % steady)
        print()

        db.send("SAVE")
        time.sleep(1.0)
        before_cue = [ln for ln in io.open(db.show, encoding="utf-8",
                                           errors="replace").read().splitlines()
                      if ln.startswith("cue")]

        print("  with an oscillator on X:")
        # ARM IT EXPLICITLY. Setting a rate does not arm an oscillator here,
        # and depth is 0-1 rather than a percent -- both learned by reading the
        # replies rather than assuming, after this check drove it wrong and
        # reported the feature as dead when it was simply never switched on.
        print("    %s" % db.send("GEOLFO X on"))
        print("    %s" % db.send("GEOLFO X rate 0.6"))
        print("    %s" % db.send("GEOLFO X depth 0.8"))
        time.sleep(1.0)
        moved = two_frames(db, "moving")
        if len(moved) < 2:
            note("  two frames of the move", False, "recording did not come back")
        else:
            changed = differing(moved[0], moved[1])
            print("    %d bytes differ" % changed)
            note("  the picture moves", changed > steady + 20000,
                 "%d bytes differ, against %d when still" % (changed, steady))

        # THE INVISIBLE ONE. The swing is applied to a copy at draw time, so
        # the cue's own numbers must be exactly what they were. Checked through
        # the SAVED SHOW rather than a verb: what persists is what matters, and
        # a reporting verb could be reading the modulated copy too.
        print()
        print("  and the operator's own numbers are untouched:")
        db.send("SAVE")
        time.sleep(1.0)
        after = [ln for ln in io.open(db.show, encoding="utf-8",
                                      errors="replace").read().splitlines()
                 if ln.startswith("cue")]
        same = False
        if before_cue and after:
            # Everything up to the geometry oscillator field, which is the only
            # thing that legitimately changed.
            same = before_cue[0].split("	")[:100] == after[0].split("	")[:100]
        note("  the cue record's geometry is unchanged", same,
             "the cue's own numbers moved; an oscillator must never write back")

    return finish("geometry lfo", fails)


if __name__ == "__main__":
    sys.exit(main())
