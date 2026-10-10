"""Deckboy Mini's watch folder fills the list from a folder, and only with files
that have finished arriving.

WHY THIS EXISTS. James, 2026-10-10, relayed: Mini should have the desk's WATCH
FOLDER. What is being tested is a race between a scan on a worker thread, a
file appearing on disk and an add on the main thread, so this drives the real
Mini through its remote port and writes real files beside it:

  * Mini starts EMPTY on --watch alone, and the first arrival plays.
  * Two files landing together arrive in name order.
  * A file that keeps growing across several scans is NOT taken until it
    stops -- it has to keep growing, or it is a finished file by the time
    the scan looks (the desk's check learned that the hard way).
  * Files already in the list are not added twice; non-media is ignored.
  * WATCH OFF stops arrivals and WATCH ON brings them back; a folder that is
    not there is refused, on the command line and over the port.

    python tools/check_mini_watch.py [--exe build/windows/Release/deckboy-mini.exe]
"""

import argparse
import math
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import wave

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = 5823
# A scan every 2.5 s, and a file must measure the same on two of them.
SETTLE = 9.0


def send(command, timeout=5.0):
    with socket.create_connection(("127.0.0.1", PORT), timeout=timeout) as s:
        s.settimeout(timeout)
        s.sendall((command + "\n").encode())
        data = b""
        end = time.time() + timeout
        while time.time() < end:
            try:
                chunk = s.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            data += chunk
            if data.endswith(b"\n"):
                time.sleep(0.05)
                s.settimeout(0.2)
        return data.decode(errors="replace").strip()


def order():
    for line in send("STATUS").splitlines():
        if line.startswith("LIST "):
            at = line.find("order=")
            return line[at + 6:].split("|") if at >= 0 else []
    return []


def deck_status():
    for line in send("STATUS").splitlines():
        if line.startswith("DECK 1 "):
            return line
    return ""


def quit_mini(proc):
    """QUIT, then make sure it is gone. Whether Mini exits promptly is not what
    this checks (a headless Linux box with the dummy audio driver can keep it
    in shutdown for a while, with or without --watch)."""
    try:
        send("QUIT")
        proc.wait(timeout=20)
    except (OSError, subprocess.TimeoutExpired):
        proc.kill()
        proc.wait(timeout=10)


def write_tone(path, seconds=6, hz=440):
    """A real WAV, so Mini can probe and play what arrives."""
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(48000)
        frames = b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * hz * i / 48000)))
                          for i in range(48000 * seconds))
        w.writeframes(frames)


def wait_for(predicate, seconds):
    end = time.time() + seconds
    while time.time() < end:
        if predicate():
            return True
        time.sleep(0.5)
    return predicate()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(REPO, "build", "windows", "Release", "deckboy-mini.exe"))
    args = ap.parse_args()
    args.exe = os.path.abspath(args.exe)
    work = tempfile.mkdtemp(prefix="deckboy-mini-watch-")
    drop = os.path.join(work, "drop")
    os.mkdir(drop)
    source = os.path.join(work, "source")
    os.mkdir(source)
    fails = []

    def note(name, ok, info=""):
        print("  %-56s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append("%s -- %s" % (name, info))

    print("check: Deckboy Mini's watch folder")
    print()
    missing = subprocess.run([args.exe, "--watch", os.path.join(work, "not-there")],
                             capture_output=True, text=True, timeout=30)
    note("--watch on a folder that is not there stops Mini", missing.returncode == 2,
         "exit %s: %s" % (missing.returncode, missing.stderr.strip()))

    env = dict(os.environ)
    env.pop("NO_COLOR", None)
    log = open(os.path.join(work, "mini.log"), "w", encoding="utf-8")
    proc = subprocess.Popen([args.exe, "--window", "--plain", "--port", str(PORT), "--watch", drop],
                            cwd=os.path.dirname(args.exe), env=env,
                            stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    try:
        for _ in range(60):
            try:
                send("PING", 1.0)
                break
            except OSError:
                time.sleep(0.5)
        note("--watch alone starts Mini empty", order() == [] and "cues=0" in deck_status(), deck_status())
        note("HELP lists WATCH", "WATCH" in send("HELP"))
        reply = send("WATCH")
        note("WATCH reports the folder", reply.startswith("OK WATCH") and drop in reply, reply)

        # Two at once: written elsewhere, then moved in, as a copy tool does.
        for name in ("bravo", "alpha"):
            write_tone(os.path.join(source, name + ".wav"))
        with open(os.path.join(drop, "notes.txt"), "w") as f:
            f.write("not media")
        with open(os.path.join(drop, "README"), "w") as f:
            f.write("no extension")
        for name in ("bravo", "alpha"):
            shutil.move(os.path.join(source, name + ".wav"), os.path.join(drop, name + ".wav"))
        note("two arrivals come in name order",
             wait_for(lambda: order() == ["alpha", "bravo"], SETTLE * 2), repr(order()))
        note("...and the first one plays", wait_for(lambda: "status=Playing" in deck_status(), 5), deck_status())

        # Still being written: keeps growing across several scans.
        growing = os.path.join(drop, "charlie.wav")
        write_tone(os.path.join(source, "charlie.wav"), seconds=10)
        data = open(os.path.join(source, "charlie.wav"), "rb").read()
        chunk = len(data) // 10
        early = False
        with open(growing, "wb") as f:
            for i in range(10):
                f.write(data[i * chunk:(i + 1) * chunk] if i < 9 else data[i * chunk:])
                f.flush()
                os.fsync(f.fileno())
                if "charlie" in order():
                    early = True
                time.sleep(1.2)
        note("a file still growing is not taken", not early, repr(order()))
        note("...and it arrives once it stops",
             wait_for(lambda: order() == ["alpha", "bravo", "charlie"], SETTLE * 2), repr(order()))
        note("non-media is left alone", order() == ["alpha", "bravo", "charlie"], repr(order()))

        reply = send("WATCH")
        note("WATCH counts what it took", "taken=3" in reply, reply)

        note("WATCH OFF answers OK", send("WATCH OFF").startswith("OK"))
        write_tone(os.path.join(drop, "delta.wav"), seconds=3)
        time.sleep(SETTLE)
        note("...and nothing arrives while off", "delta" not in order(), repr(order()))
        note("WATCH ON answers OK", send("WATCH ON").startswith("OK"))
        note("...and the file arrives", wait_for(lambda: "delta" in order(), SETTLE * 2), repr(order()))

        reply = send("WATCH " + os.path.join(work, "not-there"))
        note("a folder that is not there is refused", reply.startswith("ERR WATCH"), reply)
        reply = send("WATCH 1 " + drop)
        note("the desk's form, WATCH 1 <folder>, works", reply.startswith("OK"), reply)
        time.sleep(SETTLE)
        note("...and re-watching the same folder adds nothing twice",
             order() == ["alpha", "bravo", "charlie", "delta"], repr(order()))
        quit_mini(proc)
    finally:
        if proc.poll() is None:
            proc.kill()
        log.close()

    # The list came from the folder: watching it must not add it again.
    proc = subprocess.Popen([args.exe, "--window", "--paused", "--plain", "--port", str(PORT),
                             "--watch", drop, os.path.join(drop, "alpha.wav"), os.path.join(drop, "bravo.wav")],
                            cwd=os.path.dirname(args.exe), env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
    try:
        for _ in range(60):
            try:
                send("PING", 1.0)
                break
            except OSError:
                time.sleep(0.5)
        time.sleep(SETTLE)
        names = order()
        note("files already in the list are not added twice",
             sorted(names) == ["alpha", "bravo", "charlie", "delta"], repr(names))
        quit_mini(proc)
    finally:
        if proc.poll() is None:
            proc.kill()

    print()
    if fails:
        print("FAILED (%d): log in %s" % (len(fails), work))
        for f in fails:
            print("  " + f)
        return 1
    shutil.rmtree(work, ignore_errors=True)
    print("all good")
    return 0


if __name__ == "__main__":
    sys.exit(main())
