#!/usr/bin/env python3
"""INSTALL & RESTART shows at once that it was pressed, and hands over cleanly.

The installer is a stand-in (DECKBOY_TEST_UPDATE_INSTALLER). On Windows it is
Deckboy itself, which opens nothing when started with /fromupdater=1 and
DECKBOY_TEST_STANDIN_MS set: it lives that long, leaves a mark and exits. On
Linux it is a script put where an AppImage would go (APPIMAGE), which the
install helper moves into place and starts once Deckboy has closed.

  - a stand-in that lives: the reply comes at once, the card is on the desk on
    the next frame, and frames keep coming while the installer starts (the
    press used to sleep the main thread for 1.2 s, so the window froze exactly
    as it was). Then Deckboy closes by itself, and the stand-in outlives it:
    it was let go, not killed with Deckboy
  - a stand-in that dies at once (Windows): Deckboy stays open, says why, and
    the download is still ready to install
  - a deck that starts while the installer is starting (a GO over the
    network): Deckboy stays open and stops the installer before it has
    replaced anything

macOS is not covered: its helper needs an app bundle and a disk image.

    py -3 tools/check_update_install.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import os
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
    print("  %-66s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def show_log(work):
    path = work / "state" / "deckboy-show.log"
    return path.read_text(errors="replace") if path.exists() else ""


def wait_until(test, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if test():
            return True
        time.sleep(0.1)
    return test()


def stand_in(exe, base, lives_ms, exit_code=0):
    """(environment, mark file, target or None) for one stand-in installer."""
    mark = base / "stand-in.mark"
    env = {"DECKBOY_TEST_STANDIN_MS": str(lives_ms), "DECKBOY_TEST_STANDIN_MARK": str(mark),
           "DECKBOY_TEST_STANDIN_EXIT": str(exit_code)}
    if os.name == "nt":
        env["DECKBOY_TEST_UPDATE_INSTALLER"] = str(exe)
        return env, mark, None
    # What the helper will move over the "AppImage" and start. It runs only
    # once Deckboy has gone, so it need not wait: the mark alone says the
    # helper outlived Deckboy and did its job.
    download = base / "download"
    download.mkdir(parents=True, exist_ok=True)
    script = download / "Deckboy-new.AppImage"
    script.write_text("#!/bin/sh\necho relaunched > '%s'\n" % mark)
    script.chmod(0o755)
    target = base / "apps" / "Deckboy.AppImage"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("old\n")
    env["DECKBOY_TEST_UPDATE_INSTALLER"] = str(script)
    env["APPIMAGE"] = str(target)
    return env, mark, target


def run_lives(exe, work, show):
    print("  a stand-in installer that lives")
    base = work / "lives"
    env, mark, target = stand_in(exe, base, 5000)
    with Runtime(exe, base / "run", show, env=env) as db:
        db.snapshot(base / "before.bmp")
        t0 = time.monotonic()
        reply = db.send("UPDATE INSTALL")
        replied = time.monotonic() - t0
        print("    " + reply)
        note("UPDATE INSTALL answers at once", replied < 0.5, "%.2fs" % replied)
        t1 = time.monotonic()
        db.snapshot(base / "during.bmp")      # --keep, to look at
        shot = time.monotonic() - t1
        reply = db.send("UPDATE STATUS")
        print("    " + reply)
        note("UPDATE STATUS says it is installing", "installing" in reply, reply)
        drawn = re.search(r"card drawn (\d+) frames", reply)
        note("the card is drawn from the frame after the press",
             drawn is not None and int(drawn.group(1)) >= 2, reply)
        time.sleep(0.4)
        t2 = time.monotonic()
        db.snapshot(base / "starting.bmp")
        later = time.monotonic() - t2
        note("frames keep coming while the installer starts",
             shot < 0.8 and later < 0.8, "%.2fs, %.2fs" % (shot, later))
        exited = wait_until(lambda: db.proc.poll() is not None, 15)
        quit_at = time.monotonic() - t0
        note("Deckboy closes by itself", exited, "still running after 15 s")
        if exited:
            note("and exits cleanly", db.proc.returncode == 0, db.proc.returncode)
            # Not before the card saying so has been up 0.9 s.
            note("a second or two after the press", 0.9 < quit_at < 9.0, "%.1fs" % quit_at)
        marked_at_exit = mark.exists()
        lived = wait_until(mark.exists, 10)
        note("the installer outlives Deckboy (let go, not killed)", lived and not marked_at_exit,
             "mark at exit=%s, later=%s" % (marked_at_exit, lived))
        if target is not None:
            replaced = target.read_text(errors="replace").startswith("#!/bin/sh")
            note("the helper put the new AppImage in place", replaced, target.read_text(errors="replace")[:40])
        log = show_log(base / "run")
        note("the show log has the hand-over", "UPDATE-ASK" in log and "UPDATE-RUN" in log, log[-400:])


def run_dies(exe, work, show):
    print("  a stand-in installer that dies at once")
    base = work / "dies"
    env, mark, _ = stand_in(exe, base, 0, exit_code=7)
    with Runtime(exe, base / "run", show, env=env) as db:
        db.send("UPDATE INSTALL")
        stopped = wait_until(lambda: "UPDATE-STOP" in show_log(base / "run"), 8)
        log = show_log(base / "run")
        note("Deckboy says the installer closed, with its code", stopped and "code 7" in log, log[-400:])
        time.sleep(1.5)
        note("and stays open", db.proc.poll() is None, db.proc.returncode)
        reply = db.send("UPDATE STATUS")
        note("the download is still ready to install", "ready to install" in reply, reply)
        db.send("PING")


def run_goes_live(exe, work, show):
    print("  a deck that starts while the installer is starting")
    base = work / "live"
    env, mark, target = stand_in(exe, base, 4000)
    with Runtime(exe, base / "run", show, env=env) as db:
        db.send("TONECUE")
        db.send("SELECT LAST")
        db.send("UPDATE INSTALL")
        started = wait_until(lambda: "UPDATE-RUN" in show_log(base / "run"), 5)
        note("the installer was started", started, show_log(base / "run")[-300:])
        db.send("TAKE")
        stopped = wait_until(lambda: "UPDATE-STOP" in show_log(base / "run"), 5)
        log = show_log(base / "run")
        note("the install is called off, naming the deck", stopped and "deck 1" in log, log[-400:])
        time.sleep(2.5)
        note("Deckboy stays open", db.proc.poll() is None, db.proc.returncode)
        reply = db.send("UPDATE STATUS")
        note("the download is still ready to install", "ready to install" in reply, reply)
        db.send("STOP")
    # Deckboy has been closed by now. An installer that had been let go would
    # carry on from here: the stand-in would leave its mark, and the helper
    # would replace the AppImage.
    time.sleep(5.0)
    note("the installer was stopped with the install", not mark.exists(), "it left its mark")
    if target is not None:
        note("and replaced nothing", target.read_text(errors="replace") == "old\n",
             target.read_text(errors="replace")[:40])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--keep", action="store_true", help="keep the work folder and its snapshots")
    args = ap.parse_args()
    if sys.platform == "darwin":
        print("update install: skipped on macOS (its helper needs an app bundle and a disk image)")
        return 0
    exe = Path(args.exe).resolve()
    print("check: INSTALL & RESTART\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-update-install-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "01-picture-on-screen.deckboy"
    try:
        run_lives(exe, work, show)
        if os.name == "nt":
            run_dies(exe, work, show)
        run_goes_live(exe, work, show)
    finally:
        if args.keep:
            print("  kept " + str(work))
        else:
            shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\nupdate install: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\nupdate install: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
