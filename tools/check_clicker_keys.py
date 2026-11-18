#!/usr/bin/env python3
"""A presenter clicker's keys reach the show.

A clicker is a USB keyboard. A D'San Perfect Cue sends Right arrow (forward),
Left arrow (back) and B (blank) unless it is reprogrammed; others send
Page Down / Page Up. Deckboy listened only for the Page keys, so a Perfect Cue
did nothing. Checked here, through the app's real key path (KEY pushes the
key events the clicker would):

  - Right / Left take the next / previous cue, as Page Down / Page Up do
  - the same keys work when a full-screen output has the keyboard
  - B (the clicker's blank button) toggles blackout from an output too
  - Shift / Alt + arrows still step a paused video and take no cue
  - in the tracker's pattern view, Right / Left are GO / BACK

Every window hidden. The test inputs are local-only.

    py -3 tools/check_clicker_keys.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import json
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
    print("  %-66s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def status(db):
    return json.loads(db.send("STATUS JSON"))


def live(db):
    """The live cue's index on the focused playlist."""
    return status(db)["decks"][0]["active"]


def blackout(db):
    return bool(status(db)["blackout"])


def seconds(stamp):
    """'01:02.3' -> 62.3"""
    minutes, rest = stamp.split(":")
    return int(minutes) * 60 + float(rest)


def wait_for(test, seconds=4.0):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if test():
            return True
        time.sleep(0.1)
    return test()


def press_and_expect(db, key, want, where=""):
    db.send("KEY %s%s" % (key, where))
    return wait_for(lambda: live(db) == want), live(db)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: a clicker's keys reach the show\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-clicker-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "02-running-a-show.deckboy"
    try:
        with Runtime(exe, work / "run", show) as db:
            time.sleep(1.0)
            db.send("GOTO 1")
            start = wait_for(lambda: live(db) is not None and live(db) >= 0) and live(db)
            note("a cue is live to start from", start is not None and start >= 0, live(db))

            print("  on the desk")
            for key, step in (("PageDown", 1), ("PageUp", -1), ("Right", 1), ("Right", 1),
                              ("Left", -1), ("Left", -1)):
                want = live(db) + step
                ok, got = press_and_expect(db, key, want)
                note("%s takes cue %d" % (key, want), ok, "live %s" % got)

            print("  with a full-screen output holding the keyboard")
            db.send("OUTPUT on")
            wait_for(lambda: status(db)["outputs"][0]["enabled"])
            for key, step in (("Right", 1), ("Left", -1), ("PageDown", 1), ("PageUp", -1)):
                want = live(db) + step
                ok, got = press_and_expect(db, key, want, " OUTPUT 1")
                note("%s from the output takes cue %d" % (key, want), ok, "live %s" % got)

            db.send("KEY B OUTPUT 1")
            note("B from the output turns blackout on", wait_for(lambda: blackout(db)), blackout(db))
            db.send("KEY B OUTPUT 1")
            note("a second B turns it off", wait_for(lambda: not blackout(db)), blackout(db))

            # Other keys from the output stay dropped: a stray key on the
            # projector must not fire anything else.
            before = live(db)
            db.send("KEY Return OUTPUT 1")
            time.sleep(0.6)
            note("Enter from the output still does nothing", live(db) == before, live(db))

        with Runtime(exe, work / "step", show) as db:
            print("  a paused video, stepped with modifiers")
            time.sleep(1.0)
            db.send("GOTO 2")   # the starter show's first video
            wait_for(lambda: status(db)["decks"][0]["duration"] != "00:00.0")
            db.send("PAUSE")
            wait_for(lambda: status(db)["decks"][0]["status"] == "Paused")
            deck = status(db)["decks"][0]
            cue, at = deck["active"], seconds(deck["position"])
            db.send("KEY Alt+Right")
            ok = wait_for(lambda: abs(seconds(status(db)["decks"][0]["position"]) - (at + 1.0)) < 0.15)
            deck = status(db)["decks"][0]
            note("Alt+Right steps one second", ok, deck["position"])
            db.send("KEY Shift+Right")
            time.sleep(0.6)
            deck = status(db)["decks"][0]
            note("Shift+Right keeps the cue and stays paused",
                 deck["active"] == cue and deck["status"] == "Paused", "%s %s" % (deck["active"], deck["status"]))
            db.send("KEY Right")
            note("plain Right then takes the next cue", wait_for(lambda: live(db) == cue + 1), live(db))

        with Runtime(exe, work / "tracker", show) as db:
            print("  the tracker's pattern view")
            time.sleep(1.0)
            for _ in range(3):
                db.send("TRACKER ADD")
            db.send("DASH TRACKER")

            def step():
                line = db.send("TRACKER STATUS")
                return int(line.split("| step ")[1].split()[0]) if "| step " in line else 0

            for key, want in (("Right", 1), ("Right", 2), ("Shift+Right", 2), ("Left", 1)):
                db.send("KEY " + key)
                ok = wait_for(lambda: step() == want, 2.0)
                note("%s leaves the sequence on step %d" % (key, want), ok, step())
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\nclicker keys: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\nclicker keys: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
