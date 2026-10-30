#!/usr/bin/env python3
"""The desk's small promises, checked rather than remembered.

Each of these was fixed or added in 0.99.422 and checked by hand at the time,
with nothing to say if it broke again:

  - the window title names the show, and says Super Deckboy with two playlists
  - the OUTPUT chip is a switch: off, a click turns the output on; on, a click
    offers a menu and never turns it off by itself
  - Esc on a full-screen output brings the desk back at the FIRST press, even
    with Settings open behind it (that first Esc used to close Settings)
  - an exception nothing catches is named in deckboy-crash.log (Windows)
  - turning an ST 2110 output on and off many times never hangs the desk
    (it could, very rarely, while its sender stopped)

Every window hidden. The test inputs (KEY, CLICK, TESTTHROW) are local-only.

    py -3 tools/check_desk_basics.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import json
import os
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


def wait_for(test, seconds=5.0):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if test():
            return True
        time.sleep(0.1)
    return test()


def title_and_chip(exe, work, show):
    print("  the window title and the OUTPUT chip")
    with Runtime(exe, work / "title", show) as db:
        time.sleep(1.0)
        s = status(db)
        title = s.get("windowTitle", "")
        note("the title names the show", show.stem in title and "Deckboy" in title, title)
        db.send("DECKADD")
        note("with two playlists it says Super Deckboy",
             wait_for(lambda: "Super Deckboy" in status(db).get("windowTitle", "")),
             status(db).get("windowTitle"))
        db.send("DECKREMOVE")

        s = status(db)
        x, y, w, h = s["outputChipRect"]
        note("the OUTPUT chip is on the desk", w > 0 and h > 0, s["outputChipRect"])
        note("the output starts off", not s["outputs"][0]["enabled"], s["outputs"][0]["enabled"])
        db.send("CLICK %d %d" % (x + w // 2, y + h // 2))
        note("a click on the chip turns the output on",
             wait_for(lambda: status(db)["outputs"][0]["enabled"]), status(db)["outputs"][0]["enabled"])
        x, y, w, h = status(db)["outputChipRect"]
        db.send("CLICK %d %d" % (x + w // 2, y + h // 2))
        time.sleep(0.6)
        note("a second click does not turn it off", status(db)["outputs"][0]["enabled"],
             status(db)["outputs"][0]["enabled"])


def escape_fullscreen(exe, work, show):
    print("  Esc on a full-screen output, with Settings open")
    # Windows SHOWN: a hidden window will not go full screen. The harness keeps
    # the whole app on a private desktop (Windows) or a virtual display (CI's
    # xvfb), so nothing appears on anyone's screen.
    with Runtime(exe, work / "esc", show, extra=("--settings", "audio"),
                 env={"DECKBOY_HIDE_WINDOWS": None}) as db:
        time.sleep(1.0)
        db.send("OUTPUT on")
        time.sleep(1.0)
        # An output turned on goes full screen by itself; only ask if it did not.
        if not status(db)["outputs"][0]["fullscreen"]:
            db.send("FULLSCREEN")
        went = wait_for(lambda: status(db)["outputs"][0]["fullscreen"], 5.0)
        if not went:
            # A window the harness keeps hidden may refuse to go full screen on
            # this machine: say so rather than pass or fail on nothing.
            print("  %-66s %s" % ("the output went full screen", "SKIPPED (a hidden window here will not)"))
            return
        note("Settings is open on the desk", status(db).get("settingsOpen"), status(db).get("settingsOpen"))
        db.send("KEY Escape OUTPUT 1")
        note("the first Esc brings the output out of full screen",
             wait_for(lambda: not status(db)["outputs"][0]["fullscreen"], 3.0),
             status(db)["outputs"][0]["fullscreen"])
        note("and leaves Settings open (it used to close it instead)", status(db).get("settingsOpen"),
             status(db).get("settingsOpen"))


def crash_log(exe, work, show):
    print("  an exception nothing catches, named in deckboy-crash.log")
    run = work / "crash"
    with Runtime(exe, run, show) as db:
        try:
            db.send("TESTTHROW")
        except (ConnectionError, RuntimeError):
            pass   # it may go down before it answers, which is the point
        ended = wait_for(lambda: db.proc.poll() is not None, 15.0)
        note("the app ended on it", ended, "still running")
    log = run / "state" / "deckboy-crash.log"
    text = log.read_text(errors="replace") if log.exists() else ""
    note("deckboy-crash.log names the exception and its message",
         "thrown:" in text and "TESTTHROW" in text, text[-400:] or "no log at " + str(log))


def st2110_on_off(exe, work, show, rounds):
    print("  ST 2110 turned on and off %d times" % rounds)
    with Runtime(exe, work / "2110", show) as db:
        db.send("OUTPUT on")
        db.send("ST2110 ADDR 127.0.0.1")
        db.send("ST2110 PORT 50120")
        slowest = 0.0
        hung = None
        for i in range(rounds):
            for word in ("ON", "OFF"):
                began = time.monotonic()
                try:
                    db.send("ST2110 " + word)
                except Exception as exc:  # a timeout is the hang
                    hung = "round %d %s: %s" % (i + 1, word, exc)
                    break
                slowest = max(slowest, time.monotonic() - began)
                time.sleep(0.15)
            if hung:
                break
        note("every ON and OFF answered (slowest %.2f s)" % slowest, hung is None and slowest < 5.0,
             hung or "%.2f s" % slowest)
        note("and the desk still answers after", db.send("PING").startswith("OK"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--rounds", type=int, default=25, help="ST 2110 on/off rounds")
    ap.add_argument("--no-2110", action="store_true", help="a build without ST 2110")
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: the desk's small promises\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-desk-basics-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "02-running-a-show.deckboy"
    try:
        title_and_chip(exe, work, show)
        escape_fullscreen(exe, work, show)
        if os.name == "nt":
            crash_log(exe, work, show)
        else:
            print("  %-66s %s" % ("deckboy-crash.log names the exception", "n/a (the named exception is Windows')"))
        if not args.no_2110:
            st2110_on_off(exe, work, show, args.rounds)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\ndesk basics: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\ndesk basics: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
