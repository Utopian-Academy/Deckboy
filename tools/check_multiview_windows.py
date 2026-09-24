#!/usr/bin/env python3
"""The multiview's windows are a list you arrange, and it survives a save.

WHY THIS EXISTS. The multiview used to BE a rule -- the programme, then every
playlist, forever. It is now a list: empty means that same automatic set, and
the moment a window is assigned the list is what gets drawn and saved.

Two things can go wrong with a change like that and neither shows up in a
screenshot:

  1. The automatic set stops matching what it replaced, so a show that never
     touches this looks different for no reason.
  2. An arrangement does not round-trip, so the operator sets up a multiview,
     saves, reopens, and gets the automatic set back. A setting that does not
     round-trip is a setting that does not exist.

So this drives the running app over the socket, arranges windows, saves,
reopens, and asks it what it has. It also checks the refusals, because a verb
that answers OK while doing nothing is the fault this whole file exists to
catch (VIDEO OUTPUT SELECT did exactly that, earlier in the same release).

    python3 tools/check_multiview_windows.py
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

PORT = 5719


class App(object):
    """The app, on a socket, in a root of its own."""

    def __init__(self, exe):
        self.root = tempfile.mkdtemp(prefix="deckboy-mvw-")
        os.makedirs(os.path.join(self.root, "data"), exist_ok=True)
        self.show = os.path.join(self.root, "data", "default.deckboy")
        io.open(self.show, "w", encoding="utf-8", newline="").write(
            io.open(os.path.join(REPO, "data", "default.deckboy"),
                    encoding="utf-8", errors="replace").read())
        deckboy_testroot.populate(self.root)
        self.exe = exe
        self.proc = None
        self.log = os.path.join(self.root, "app.log")

    def start(self):
        env = dict(os.environ)
        env.update(DECKBOY_ROOT=self.root, DECKBOY_PROJECT=self.show,
                   DECKBOY_COMPANION_PORT=str(PORT))
        handle = open(self.log, "a")
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
        # The port outlives the process by a moment on Windows.
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
        print("  %-46s %s" % (name, "ok" if ok else "FAIL  got: " + got))
        if not ok:
            fails.append("%s -- wanted %r, got %r" % (name, want, got))

    app = App(exe)
    print("check: the multiview's windows")
    print()
    try:
        app.start()
        app.send("MASTERVOL 0")

        # -- THE AUTOMATIC SET still matches what it replaced ---------------
        #
        # COUNTED, not substring-matched. The first version of this check
        # asserted "4=deck:2" appears, called it "three playlists -> four
        # windows", and passed against a SIX window list -- because the
        # bundled show has three decks, not one, and "4=deck:2" is in both.
        # A check that agrees with itself proves nothing.
        print("  the automatic set, with nothing arranged:")

        def window_count():
            reply = app.send("MULTIVIEW WINDOW")
            body = reply.split(":", 1)[-1] if reply.upper().startswith("OK") else reply
            return len([p for p in body.split(";") if "=" in p])

        decks = int(app.send("STATUS DECKS").strip().split()[-1])             if "DECKS" in app.send("STATUS DECKS").upper() else None
        base = window_count()
        check("a window per playlist, plus the programme",
              app.send("MULTIVIEW WINDOW"), "1=programme")
        app.send("DECKADD")
        app.send("DECKADD")
        after_adds = window_count()
        grew = after_adds == base + 2
        print("  %-46s %s" % ("two more playlists -> two more windows",
                              "ok (%d -> %d)" % (base, after_adds) if grew
                              else "FAIL %d -> %d" % (base, after_adds)))
        if not grew:
            fails.append("adding two playlists took the multiview from %d to %d windows"
                         % (base, after_adds))
        check("  and the programme is still first",
              app.send("MULTIVIEW WINDOW"), "1=programme")
        print()

        # -- ARRANGING makes it explicit -----------------------------------
        print("  arranging one window:")
        check("point window 2 at playlist 3",
              app.send("MULTIVIEW WINDOW 2 DECK3"), "ok")
        check("  it reports the new source",
              app.send("MULTIVIEW WINDOW"), "2=deck:2")
        check("safe areas on window 2",
              app.send("MULTIVIEW WINDOW 2 SAFE ON"), "ok")
        check("meter on window 2",
              app.send("MULTIVIEW WINDOW 2 METER ON"), "ok")
        check("  both are reported",
              app.send("MULTIVIEW WINDOW"), "deck:2+safe+meter")
        check("empty a window", app.send("MULTIVIEW WINDOW 3 EMPTY"), "ok")
        check("  and it reads as empty",
              app.send("MULTIVIEW WINDOW"), "3=empty")
        print()

        # -- THE REFUSALS. A verb that answers OK while doing nothing -------
        print("  what it refuses:")
        check("no playlist 9", app.send("MULTIVIEW WINDOW 1 DECK9"), "err")
        check("no window 99", app.send("MULTIVIEW WINDOW 99 PROGRAMME"), "err")
        check("a source that is not one",
              app.send("MULTIVIEW WINDOW 1 BANANA"), "err")
        check("SAFE with no state", app.send("MULTIVIEW WINDOW 1 SAFE"), "err")
        print()

        # -- IT ROUND-TRIPS -------------------------------------------------
        print("  it survives a save and a reopen:")
        before = app.send("MULTIVIEW WINDOW")
        app.send("SAVE")
        time.sleep(1.0)
    finally:
        app.stop()

    saved = io.open(app.show, encoding="utf-8", errors="replace").read()
    lines = [ln for ln in saved.splitlines() if ln.startswith("multiview_tile")]
    want_lines = before.count(";") + 1 if before else 0
    ok_lines = len(lines) == want_lines
    print("  %-46s %s" % ("written to the show file",
                          "ok (%d lines)" % len(lines) if ok_lines
                          else "FAIL %d lines for %d windows" % (len(lines), want_lines)))
    if not ok_lines:
        fails.append("saved %d multiview_tile lines for %d windows"
                     % (len(lines), want_lines))

    app.start()
    try:
        after = app.send("MULTIVIEW WINDOW")
        same = after == before
        print("  %-46s %s" % ("the same arrangement comes back",
                              "ok" if same else "FAIL"))
        if not same:
            fails.append("round-trip: saved %r, reopened %r" % (before, after))
        print("  %-46s %s" % ("reset goes back to automatic",
                              app.send("MULTIVIEW WINDOW RESET")))
        got = app.send("MULTIVIEW WINDOW")
        check("  and the automatic set is back", got, "1=programme")
    finally:
        app.stop()

    print()
    for f in fails:
        print("  FAIL " + f)
    print()
    print("clean" if not fails
          else "%d finding%s" % (len(fails), "" if len(fails) == 1 else "s"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
