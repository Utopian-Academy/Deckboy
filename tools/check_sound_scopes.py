#!/usr/bin/env python3
"""The sound scopes, read off a tone cue the desk itself plays.

A tone cue is a 1 kHz sine at -18 dBFS on every channel, so every reading has
an answer known in advance: correlation +1 (the same sine both sides), the
loudest third octave 1k at -18 dB, momentary, short-term and integrated
loudness -18 LUFS (a stereo 1 kHz sine reads its own level, EBU Tech 3341),
and a true peak of -18 dBTP. SCOPES SOUND returns them; --snap also photographs
the panel. Then the cue stops, and the scopes must say so.

The arithmetic is checked on its own in --smoke; this is the path to it: the
playlist's audio tap, the queue the desk drains, and the panel.

    py -3 tools/check_sound_scopes.py --exe build/windows/Release/Deckboy.exe
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

fails = []


def note(name, ok, info=""):
    print("  %-62s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def readings(db):
    reply = db.send("SCOPES SOUND")
    values = dict(re.findall(r"(\w+)=(\S+)", reply))
    return reply, values


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--snap", help="write a picture of the panel here (.bmp)")
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: the sound scopes on a tone cue\n")

    work = Path(tempfile.mkdtemp(prefix="deckboy-sound-scopes-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "01-picture-on-screen.deckboy"
    with Runtime(exe, work / "run", show) as db:
        closed = db.send("SCOPES SOUND")
        note("closed, it says how to open it", "closed" in closed, closed)
        db.send("TONECUE")
        db.send("SELECT LAST")
        db.send("SCOPES ON")
        db.send("SCOPES BIG sound")
        time.sleep(0.5)
        db.send("TAKE")
        time.sleep(5.0)        # short-term needs three seconds of it
        reply, v = readings(db)
        print("    " + reply)
        def num(key):
            try:
                return float(v.get(key, "nan"))
            except ValueError:
                return float("nan")
        note("sounding", v.get("sounding") == "1", reply)
        note("correlation +1: the same sine both sides", num("corr") > 0.98, v.get("corr"))
        note("loudest band 1k, at -18 dB", v.get("loudest", "").startswith("1k/") and
             abs(float(v.get("loudest", "x/nan").split("/")[1]) + 18.0) < 0.5, v.get("loudest"))
        for key in ("M", "S", "I"):
            note("%s loudness -18 LUFS" % key, abs(num(key) + 18.0) < 0.3, v.get(key))
        note("true peak -18 dBTP", abs(num("TP") + 18.0) < 0.5, v.get("TP"))
        if args.snap:
            db.send("UISNAP " + str(Path(args.snap).resolve()))
            time.sleep(0.5)

        # The tone itself: PAUSE and STOP used to leave it sounding (the deck
        # changed state and the generator went on topping its device up).
        db.send("PAUSE")
        time.sleep(1.0)
        reply, v = readings(db)
        note("PAUSE silences the tone", v.get("sounding") == "0", reply)
        db.send("PLAY")
        time.sleep(1.0)
        reply, v = readings(db)
        note("PLAY brings it back", v.get("sounding") == "1" and abs(num("M") + 18.0) < 0.5, reply)

        db.send("STOP")
        time.sleep(1.5)
        reply, v = readings(db)
        print("    " + reply)
        note("STOP silences the tone, and momentary falls away", v.get("sounding") == "0" and num("M") < -60.0,
             reply)
        # Within 0.6 LU, not 0.3: the 400 ms blocks that straddle the pause and
        # the stop are part tone, part silence, and R 128 rightly counts them.
        # Silence itself counting would put it lower still.
        note("stopped: the integrated reading stays (the gate keeps silence out)",
             abs(num("I") + 18.0) < 0.6, v.get("I"))
        note("silence reads as nothing, not as a number", num("M") >= -120.0, v.get("M"))
        db.send("SCOPES SOUND RESET")
        reply, v = readings(db)
        note("RESET starts the integrated reading again", num("I") < -100.0, reply)

    shutil.rmtree(work, ignore_errors=True)
    print()
    if fails:
        print("sound scopes: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("sound scopes: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
