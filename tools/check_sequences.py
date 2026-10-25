#!/usr/bin/env python3
"""Sequences: several commands on one button, with waits between them.

    SEQ <step> ; <step> ; WAIT <seconds> ; WAIT GO ; <step>

Run against the real app on a private desktop. The master volume is the
marker each step leaves, so every check reads one number from STATUS.

    py -3 tools/check_sequences.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import json
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

fails = []


def note(name, ok, info=""):
    print("  %-58s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def send(db, command):
    """The reply, refusals included (the runtime raises on ERR)."""
    try:
        return db.send(command)
    except RuntimeError as e:
        return str(e).split(" => ", 1)[-1]


def volume(db):
    status = json.loads(db.send("STATUS JSON"))
    return int(round(float(status.get("masterVolume", -1))))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="deckboy-sequences-"))
    show = work / "show.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\nui_scale\t1.0\n", encoding="utf8")
    print("check: sequences\n")
    with Runtime(Path(args.exe).resolve(), work / "run", show) as db:
        reply = db.send("SEQ MASTERVOL 50 ; WAIT 1.0 ; MASTERVOL 80")
        first = volume(db)
        time.sleep(0.4)
        early = volume(db)
        time.sleep(1.0)
        late = volume(db)
        note("SEQ is accepted", reply.startswith("OK"), reply)
        note("the first step runs at once", first == 50, first)
        note("...the next waits its WAIT", early == 50, early)
        note("...and then runs", late == 80, late)

        db.send("DASH SET 1 Two parts | SEQ MASTERVOL 20 ; WAIT GO ; MASTERVOL 90")
        db.send("DASH 1")
        held = volume(db)
        waiting = db.send("SEQ")
        time.sleep(0.6)
        still = volume(db)
        db.send("DASH 1")
        time.sleep(0.2)
        gone_on = volume(db)
        note("a tile with WAIT GO runs up to the wait", held == 20, held)
        note("...says it is waiting", "waiting for GO" in waiting, waiting)
        note("...holds there", still == 20, still)
        note("...and carries on when pressed again", gone_on == 90, gone_on)

        db.send("DASH SET 2 Long | SEQ MASTERVOL 30 ; WAIT 1.5 ; MASTERVOL 70")
        db.send("DASH 2")
        db.send("DASH 2")                       # pressed again while it runs: stop
        time.sleep(1.9)
        stopped = volume(db)
        note("pressing a running tile stops it", stopped == 30, stopped)

        nested = send(db, "SEQ MASTERVOL 10 ; SEQ MASTERVOL 20")
        note("a sequence cannot start a sequence", nested.startswith("ERR"), nested)
        bad = send(db, "SEQ MASTERVOL 10 ; WAIT soon")
        note("WAIT needs seconds or GO", bad.startswith("ERR"), bad)
        note("a refused sequence runs none of its steps", volume(db) == 30, volume(db))
    print()
    print("%d FAILED" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
