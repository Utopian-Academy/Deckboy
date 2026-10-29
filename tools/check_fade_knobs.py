#!/usr/bin/env python3
"""A fade cue can ramp an effect's knob, picture or audio.

The Audio board's first item, effects half: a fade's targets grow "an
effect's knob" and "an audio effect's knob". The effect is picked by its place
in the stack of the cue the deck is playing when the fade fires, so this puts
a picture effect and an audio effect on a looping video cue, takes it, and
fires fade cues at both. Then a fade at an effect the cue does not have must
say so and move nothing. And the fade cue keeps its effect and knob when the
show is saved.

The read-backs are FX GET (picture) and AUDIOFX (audio), in percent.

    py -3 tools/check_fade_knobs.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import re
import shutil
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

PLAYING = "4"       # "04 Loops until you move on": a video with sound, looping

fails = []


def note(name, ok, info=""):
    print("  %-62s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def knob(reply, name):
    match = re.search(r"\b%s=(-?\d+)" % name, reply)
    return int(match.group(1)) if match else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: fades on effect knobs\n")

    work = Path(tempfile.mkdtemp(prefix="deckboy-fade-knobs-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "02-running-a-show.deckboy"
    with Runtime(exe, work / "run", show) as db:
        db.send("SELECT " + PLAYING)
        db.send("FX ADD kaleidoscope")
        db.send("FX AMOUNT 1 1.0")
        db.send("AUDIOFX ADD hpf")
        db.send("TAKE")
        time.sleep(0.8)
        print("    playing cue, effect 1: " + db.send("FX GET 1"))
        print("    and its audio effect:  " + db.send("AUDIOFX"))

        made = db.send("FADE NEW")
        match = re.search(r"fade cue (\d+)", made)
        fade = match.group(1) if match else ""
        note("FADE NEW makes a fade cue", bool(fade), made)
        for line in ("FADE WHAT effect", "FADE EFFECT 1", "FADE KNOB amount", "FADE TO 0", "FADE OVER 1.0"):
            db.send(line)
        print("    " + db.send("FADE"))
        print("    " + db.send("FADE FIRE"))
        time.sleep(0.5)
        db.send("SELECT " + PLAYING)
        mid = knob(db.send("FX GET 1"), "amount")
        time.sleep(1.0)
        end = knob(db.send("FX GET 1"), "amount")
        note("an effect's amount ramps down through the middle", mid is not None and 15 <= mid <= 85, mid)
        note("and lands on 0", end == 0, end)

        db.send("SELECT " + fade)
        for line in ("FADE WHAT audiofx", "FADE EFFECT 1", "FADE KNOB A", "FADE TO 100", "FADE OVER 0.6"):
            db.send(line)
        print("    " + db.send("FADE FIRE"))
        time.sleep(1.2)
        db.send("SELECT " + PLAYING)
        reply = db.send("AUDIOFX")
        note("an audio effect's knob A ramps to 100", knob(reply, "a") == 100, reply)

        db.send("SELECT " + fade)
        db.send("FADE WHAT effect")
        db.send("FADE EFFECT 7")
        try:
            reply = "OK " + db.send("FADE FIRE")
        except RuntimeError as refused:
            reply = str(refused)
        print("    " + reply)
        db.send("SELECT " + PLAYING)
        after = knob(db.send("FX GET 1"), "amount")
        note("an effect the cue does not have: ERR, says so, moves nothing",
             "ERR" in reply and "nothing to fade" in reply and after == 0, (reply, after))

        # RESTORE AT END. The amount is at 0: a fade up to 100 that restores
        # rises, lands, and is put back to 0.
        db.send("SELECT " + fade)
        for line in ("FADE EFFECT 1", "FADE KNOB amount", "FADE TO 100", "FADE OVER 2", "FADE RESTORE ON"):
            db.send(line)
        print("    " + db.send("FADE"))
        db.send("FADE FIRE")
        time.sleep(0.8)
        db.send("SELECT " + PLAYING)
        mid = knob(db.send("FX GET 1"), "amount")
        time.sleep(1.8)
        end = knob(db.send("FX GET 1"), "amount")
        note("RESTORE AT END: the knob rises, lands, and is put back", mid is not None and mid >= 20 and end == 0,
             (mid, end))
        # With THEN STOP, the usual take-it-out: the deck stops and is left at
        # full opacity for whatever it plays next.
        db.send("SELECT " + fade)
        for line in ("FADE WHAT opacity", "FADE TO 0", "FADE OVER 0.4", "FADE STOP ON"):
            db.send(line)
        db.send("FADE FIRE")
        time.sleep(1.0)
        status = db.send("STATUS")
        deck1 = next((line for line in status.splitlines() if line.startswith("DECK 1 ")), "")
        opacity = re.search(r"opacity=(\d+)", deck1)
        note("with THEN STOP: stopped, and left at full opacity",
             "status=Playing" not in deck1 and opacity is not None and opacity.group(1) == "100", deck1[:240])

        db.send("SELECT " + fade)
        for line in ("FADE WHAT effect", "FADE EFFECT 7", "FADE KNOB C"):
            db.send(line)
        time.sleep(1.5)                         # the autosave

    with Runtime(exe, work / "run2", show) as db:
        time.sleep(1.0)
        db.send("SELECT " + fade)
        reply = db.send("FADE")
        note("the fade cue keeps its effect, knob and restore with the show",
             "effect 7 knob C" in reply and "then restore" in reply, reply)

    shutil.rmtree(work, ignore_errors=True)
    print()
    if fails:
        print("fade knobs: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("fade knobs: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
