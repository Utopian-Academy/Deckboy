#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
# This file is part of Deckboy, a cue deck for live events.
# See LICENSE for details.
"""Prove a watched folder actually fills a playlist.

Nothing cheaper can. Scripted mouse and keyboard input does not reach SDL3, so
the only way to drive a running Deckboy from a script is the control port --
which is why WATCH exists as a verb at all. And what is being tested is a race
between a background scan, a file appearing on disk and an import on the main
thread; a unit test of the scan function would prove none of that.

So this drives the real application, and three details of HOW it does that
were each found by getting them wrong first:

  AN ISOLATED STATE DIRECTORY. Deckboy reopens the last show at boot. Without
  DECKBOY_STATE_DIR the second run of this test opens the playlist the first
  run filled, so every assertion about "did it arrive" is answered by
  yesterday's cue. The first version of this test passed for that reason.

  FIND, NOT `STATUS CUES`. The cue snapshot reports one SELECTED cue per deck,
  not the whole playlist -- so a second file arriving is invisible in it, and
  a name left selected from an earlier step reads as a pass. FIND searches the
  playlist and FINDSTATUS says how many it matched, which is the actual
  question.

  A FILE THAT IS REALLY STILL GROWING. The service takes a path once its size
  has stopped changing between scans, so a test that writes a third of a file
  and then SLEEPS for longer than the scan interval has written a file that
  has, correctly, stopped changing. It has to keep growing across several
  scans to be the thing it is imitating.

Usage:
    python tools/check_watch_folder.py [--exe PATH] [--port 5510]
"""
import argparse
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# The service scans every 2.5s and a path must measure the same on two
# consecutive scans, so anything under about six seconds is measuring the scan
# interval rather than the feature.
SETTLE = 9.0


def default_exe():
    for rel in ("build/windows/Release/Deckboy.exe", "build/Release/Deckboy",
                "build/Deckboy", "build/linux/Deckboy"):
        p = os.path.join(ROOT, rel.replace("/", os.sep))
        if os.path.isfile(p):
            return p
    return None


class Remote:
    def __init__(self, port, timeout=3.0):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout)
        self.sock.settimeout(timeout)

    def send(self, line, seconds=0.8):
        self.sock.sendall((line + "\n").encode("utf-8"))
        deadline = time.time() + seconds
        buf = b""
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            buf += chunk
        return buf.decode("utf-8", "replace").strip()

    def has_cue(self, token):
        """Is there a cue whose name matches? FIND searches the playlist;
        FINDSTATUS reports how many it hit."""
        self.send("FIND " + token)
        status = self.send("FINDSTATUS")
        return "find: none" not in status and "none" not in status.split(":")[-1]

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def wait_for_port(port, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            socket.create_connection(("127.0.0.1", port), 0.5).close()
            return True
        except OSError:
            time.sleep(0.3)
    return False


def make_media(path):
    """A real clip, so the importer accepts it."""
    run = subprocess.run(
        ["ffmpeg", "-y", "-v", "error", "-f", "lavfi",
         "-i", "testsrc=size=160x120:rate=10:duration=2",
         "-pix_fmt", "yuv420p", path],
        capture_output=True, text=True)
    if run.returncode != 0:
        sys.exit("check_watch_folder: ffmpeg could not make a test clip:\n" +
                 run.stderr.strip())


def dribble(source, target, seconds, stop):
    """Copy a file slowly, so it is genuinely mid-write across several scans."""
    payload = open(source, "rb").read()
    chunks = 24
    step = max(1, len(payload) // chunks)
    with open(target, "wb") as f:
        for at in range(0, len(payload), step):
            if stop.is_set():
                break
            f.write(payload[at:at + step])
            f.flush()
            os.fsync(f.fileno())
            time.sleep(seconds / chunks)


def counters(reply):
    """scans/seen/taken out of a WATCH reply."""
    out = {}
    for part in reply.replace(";", " ").split():
        if "=" in part:
            k, _, v = part.partition("=")
            try:
                out[k] = int(v)
            except ValueError:
                pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=default_exe())
    ap.add_argument("--port", type=int, default=5510)
    ap.add_argument("--keep", action="store_true", help="leave the temp dirs")
    args = ap.parse_args()

    if not args.exe or not os.path.isfile(args.exe):
        sys.exit("check_watch_folder: no Deckboy binary; pass --exe")

    work = tempfile.mkdtemp(prefix="deckboy_watch_")
    watched = os.path.join(work, "drop")
    state = os.path.join(work, "state")
    os.makedirs(watched)
    os.makedirs(state)
    source = os.path.join(work, "clip.mp4")
    make_media(source)

    env = dict(os.environ)
    # A SHOW OF ITS OWN. Deckboy reopens the last show at boot, and without
    # this the run inherits whatever the previous run imported.
    env["DECKBOY_STATE_DIR"] = state

    failures = []
    app = subprocess.Popen([args.exe, "--allow-multi-instance"], env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    remote = None
    try:
        if not wait_for_port(args.port, 45):
            sys.exit("check_watch_folder: the control port never opened")
        remote = Remote(args.port)

        # ── 0. NOTHING IS WATCHED TO BEGIN WITH ─────────────────────────────
        first = remote.send("WATCH")
        if counters(first).get("scans", -1) != 0:
            failures.append("scanning before any folder was set: " + first)
        print("  at the start: " + first)

        reply = remote.send("WATCH 1 " + watched)
        if not reply.startswith("OK"):
            failures.append("WATCH refused a real folder: " + reply)
        print("  WATCH 1 <folder>: " + reply)

        # ── 1. A FILE STILL BEING WRITTEN MUST NOT BE TAKEN ─────────────────
        # The real hazard: a large VT shows up in the listing the instant the
        # copy starts, and a cue made from a half-written file fails on air.
        stop = threading.Event()
        slow = threading.Thread(target=dribble,
                                args=(source, os.path.join(watched, "copying.mp4"),
                                      14.0, stop))
        slow.start()
        time.sleep(8.0)                      # at least three scans, all different sizes
        mid = remote.send("WATCH")
        if counters(mid).get("taken", 0) != 0:
            failures.append("imported a file that was still being written: " + mid)
        print("  while genuinely still growing: " +
              ("left alone" if counters(mid).get("taken", 0) == 0 else "IMPORTED (wrong)"))
        slow.join()

        # Now it has stopped growing, so it must arrive.
        time.sleep(SETTLE)
        if not remote.has_cue("copying"):
            failures.append("a finished file never arrived")
        print("  once the copy finished: " +
              ("imported" if remote.has_cue("copying") else "STILL MISSING"))

        # ── 2. AN ORDINARY DROP ─────────────────────────────────────────────
        shutil.copy(source, os.path.join(watched, "dropped.mp4"))
        time.sleep(SETTLE)
        if not remote.has_cue("dropped"):
            failures.append("a dropped file never arrived")
        print("  a dropped file: " +
              ("imported" if remote.has_cue("dropped") else "STILL MISSING"))

        # ── 3. NEVER THE SAME FILE TWICE ────────────────────────────────────
        before = counters(remote.send("WATCH")).get("taken", 0)
        time.sleep(SETTLE)
        after = counters(remote.send("WATCH")).get("taken", 0)
        if after != before:
            failures.append("kept importing with nothing new in the folder "
                            "(%d then %d)" % (before, after))
        print("  another pass over the same folder: " +
              ("nothing new" if after == before else "RE-IMPORTED"))

        # ── 4. OFF MEANS OFF ────────────────────────────────────────────────
        reply = remote.send("WATCH 1 OFF")
        if not reply.startswith("OK"):
            failures.append("WATCH OFF was refused: " + reply)
        shutil.copy(source, os.path.join(watched, "afteroff.mp4"))
        time.sleep(SETTLE)
        if remote.has_cue("afteroff"):
            failures.append("kept importing after WATCH OFF")
        print("  after WATCH OFF: " +
              ("ignored" if not remote.has_cue("afteroff") else "STILL IMPORTING"))

        # ── 5. A FOLDER THAT IS NOT THERE MUST BE REFUSED ───────────────────
        reply = remote.send("WATCH 1 " + os.path.join(work, "no_such_folder"))
        if not reply.startswith("ERR"):
            failures.append("a folder that does not exist was accepted: " + reply)
        print("  a folder that does not exist: " +
              ("refused" if reply.startswith("ERR") else "ACCEPTED (wrong)"))

        # ── 6. AND A PLAYLIST THAT DOES NOT EXIST ───────────────────────────
        reply = remote.send("WATCH 99 " + watched)
        if not reply.startswith("ERR"):
            failures.append("a playlist that does not exist was accepted: " + reply)
        print("  playlist 99: " +
              ("refused" if reply.startswith("ERR") else "ACCEPTED (wrong)"))
    finally:
        if remote:
            try:
                remote.send("QUIT", 0.3)
            except OSError:
                pass
            remote.close()
        app.terminate()
        try:
            app.wait(timeout=15)
        except subprocess.TimeoutExpired:
            app.kill()
        if not args.keep:
            shutil.rmtree(work, ignore_errors=True)

    if failures:
        print()
        for f in failures:
            print("FAIL " + f)
        print("check_watch_folder: %d failures" % len(failures))
        return 1
    print("check_watch_folder: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
