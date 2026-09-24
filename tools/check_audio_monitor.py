#!/usr/bin/env python3
"""The monitor is a separate bus, and a playlist can be kept out of the room.

WHY THIS EXISTS. Playing several videos at once put every playlist onto the
same default audio device -- a deck's device has never had any connection to
where its picture goes -- so they all arrived together, with no way to hear one
by itself and no way to keep one out of the PA.

Two independent gates now sit in MediaEngine::queueToDevices: one on the deck's
own device (the room) and one on the monitor stream. Both are atomics, because
choosing a different playlist to listen to must not reopen a device -- it would
click, and with the monitor following the focused playlist it would click on
every arrow key.

WHAT THIS CHECKS, and why each one can go wrong silently:

  * exactly ONE playlist is on the monitor at a time -- a gate that is never
    set leaves every deck audible, which sounds exactly like no monitor at all

MONITOR reports the ENGINE GATES, not the project fields, so what this reads
back is what the audio thread actually obeys. Built from the project it would
have agreed with itself even if the gates were never pushed onto the engines at
all, which is the failure mode that makes a green check worthless.
  * following the focused playlist actually follows it
  * pinning stops it following
  * the room gate is INDEPENDENT of the monitor gate -- the whole point is that
    a deck can be out of the PA and still in your headphones
  * it round-trips, and a show written before any of this opens with every
    playlist in the room (the backward-compatible default is TRUE, not the
    zero value -- a show that came back silent would have lost its sound)

It reads the state back over the socket rather than listening to a device, so
it runs on a machine with no audio hardware at all.

    python3 tools/check_audio_monitor.py
"""

import io
import os
import socket
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deckboy_testroot  # noqa: E402

PORT = 5721


class App(object):
    def __init__(self, exe):
        self.root = tempfile.mkdtemp(prefix="deckboy-mon-")
        os.makedirs(os.path.join(self.root, "data"), exist_ok=True)
        self.show = os.path.join(self.root, "data", "default.deckboy")
        io.open(self.show, "w", encoding="utf-8", newline="").write(
            io.open(os.path.join(REPO, "data", "default.deckboy"),
                    encoding="utf-8", errors="replace").read())
        deckboy_testroot.populate(self.root)
        self.exe = exe
        self.proc = None

    def start(self):
        env = dict(os.environ)
        env.update(DECKBOY_ROOT=self.root, DECKBOY_PROJECT=self.show,
                   DECKBOY_COMPANION_PORT=str(PORT))
        handle = open(os.path.join(self.root, "app.log"), "a")
        self.proc = subprocess.Popen([self.exe, self.show], env=env,
                                     cwd=os.path.dirname(self.exe),
                                     stdout=handle, stderr=subprocess.STDOUT)
        for _ in range(120):
            try:
                self.send("HELP")
                return
            except OSError:
                time.sleep(0.5)
        raise RuntimeError("the app never answered on %d" % PORT)

    def send(self, command, timeout=8.0):
        with socket.create_connection(("127.0.0.1", PORT), timeout=timeout) as s:
            s.settimeout(timeout)
            s.sendall((command + "\n").encode())
            try:
                return s.recv(65536).decode(errors="replace").strip()
            except OSError:
                return ""

    def stop(self):
        try:
            self.send("QUIT")
        except OSError:
            pass
        for _ in range(40):
            if not self.proc or self.proc.poll() is not None:
                break
            time.sleep(0.25)
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait(timeout=10)
        time.sleep(1.0)


def main():
    exe = os.path.join(REPO, "build", "windows", "Release", "Deckboy.exe")
    if len(sys.argv) > 1:
        exe = sys.argv[1]
    if not os.path.exists(exe):
        print("no binary at %s" % exe)
        return 1
    deckboy_testroot.warn_if_stale(exe)

    fails = []

    def check(name, got, want):
        ok = want.lower() in got.lower()
        print("  %-48s %s" % (name, "ok" if ok else "FAIL  got: " + got))
        if not ok:
            fails.append("%s -- wanted %r, got %r" % (name, want, got))

    app = App(exe)
    print("check: the monitor bus")
    print()
    try:
        app.start()
        app.send("MASTERVOL 0")
        app.send("DECKADD")

        print("  before anything is set:")
        check("no monitor device", app.send("MONITOR"), "no monitor device")
        check("  and every playlist is in the room",
              app.send("MONITOR"), "out of the room: none")
        print()

        print("  choosing what you hear:")
        check("it follows the focused playlist by default",
              app.send("MONITOR"), "follows focus")
        app.send("DECK 2")
        got = app.send("MONITOR")
        check("  focusing playlist 2 moves the monitor to it", got, "hearing deck 2")
        # ONE, not "deck 2 among others": the report lists every engine whose
        # monitor gate is open, so a comma here means two decks are audible.
        heard = got.split("hearing", 1)[1].split("(")[0]
        one = "," not in heard
        print("  %-48s %s" % ("    and nothing else is on the monitor",
                              "ok" if one else "FAIL heard:" + heard))
        if not one:
            fails.append("more than one playlist on the monitor: %r" % heard)
        app.send("DECK 1")
        check("  and back", app.send("MONITOR"), "hearing deck 1")
        check("pinning a playlist", app.send("MONITOR DECK 2"), "ok")
        app.send("DECK 1")
        check("  a pinned monitor does NOT follow focus",
              app.send("MONITOR"), "hearing deck 2")
        check("  and says it is pinned", app.send("MONITOR"), "pinned")
        check("back to following", app.send("MONITOR DECK FOLLOW"), "ok")
        check("  which follows again", app.send("MONITOR"), "hearing deck 1")
        print()

        print("  the room, which is a separate thing:")
        check("take playlist 2 out of the room",
              app.send("MONITOR ROOM 2 ON") and app.send("MONITOR ROOM 2 OFF"), "ok")
        check("  it is reported out of the room",
              app.send("MONITOR"), "out of the room: deck 2")
        check("  and the monitor is unaffected by that",
              app.send("MONITOR"), "hearing deck 1")
        check("put it back", app.send("MONITOR ROOM 2 ON"), "ok")
        check("  and the room is full again",
              app.send("MONITOR"), "out of the room: none")
        print()

        print("  what it refuses:")
        check("no playlist 9", app.send("MONITOR DECK 9"), "err")
        check("ROOM with no state", app.send("MONITOR ROOM 1"), "err")
        check("ROOM with a bad state", app.send("MONITOR ROOM 1 MAYBE"), "err")
        check("a subcommand that is not one", app.send("MONITOR BANANA"), "err")
        print()

        print("  it survives a save and a reopen:")
        app.send("MONITOR DECK 2")
        app.send("MONITOR ROOM 1 OFF")
        app.send("MONITOR DEVICE Some Device That Is Not Here")
        before = app.send("MONITOR")
        app.send("SAVE")
        time.sleep(1.0)
    finally:
        app.stop()

    saved = io.open(app.show, encoding="utf-8", errors="replace").read()
    for key in ("monitor_device", "monitor_deck"):
        present = any(ln.startswith(key) for ln in saved.splitlines())
        print("  %-48s %s" % ("%s written" % key, "ok" if present else "FAIL"))
        if not present:
            fails.append("%s missing from the saved show" % key)

    app.start()
    try:
        after = app.send("MONITOR")
        same = after == before
        print("  %-48s %s" % ("the same monitor comes back", "ok" if same else "FAIL"))
        if not same:
            fails.append("round-trip: saved %r, reopened %r" % (before, after))
        # An absent device must be REPORTED, never silently swapped for the
        # system default -- a monitor that fell back to the default would put
        # what you are auditioning into the room.
        check("  an absent monitor device is kept, not swapped",
              after, "some device that is not here")
    finally:
        app.stop()

    # A show written before any of this: no monitor lines, no audioToProgram
    # field on the deck records.
    print()
    print("  a show written before the monitor existed:")
    older = os.path.join(app.root, "data", "older.deckboy")
    trimmed = []
    for line in saved.splitlines():
        if line.startswith("monitor_device") or line.startswith("monitor_deck"):
            continue
        if line.startswith("deck\t"):
            line = line.rsplit("\t", 1)[0]     # drop audioToProgram
        trimmed.append(line)
    io.open(older, "w", encoding="utf-8", newline="\n").write("\n".join(trimmed) + "\n")

    app2 = App(exe)
    app2.show = older
    try:
        app2.start()
        got = app2.send("MONITOR")
        check("every playlist is in the room", got, "out of the room: none")
        check("  and there is no monitor device", got, "no monitor device")
    finally:
        app2.stop()

    print()
    for f in fails:
        print("  FAIL " + f)
    print()
    print("clean" if not fails
          else "%d finding%s" % (len(fails), "" if len(fails) == 1 else "s"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
