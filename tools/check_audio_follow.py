#!/usr/bin/env python3
"""A playlist on the system output follows it, even when SDL misses the move.

James, 2026-10-28: on a Mac, plugging in headphones left Deckboy playing into
the speakers until a restart. SDL moves streams opened on the default to each
new default, but on a headphone plug the Mac can announce the new default
before the new device, and SDL drops the change; or it fails to open the new
device and leaves everything where it was. Deckboy now asks the OS which output
is the default (CoreAudio, on the Mac) and moves anything SDL left behind.

That fault needs a Mac and a headphone jack. This check makes the same
disagreement on any machine with two outputs: DECKBOY_TEST_SYSTEM_OUTPUT_FILE
stands in for the OS's answer, so the "system output" can be moved under a
running app while SDL's own default stays where it was -- which is exactly what
SDL's view looks like when it has missed the change. Nothing is played; the
streams only open, paused.

    py -3 tools/check_audio_follow.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import os
import re
import subprocess
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


def devices(exe):
    """The playback devices and SDL's default, as --devices prints them."""
    out = subprocess.run([str(exe), "--devices"], capture_output=True, text=True,
                         encoding="utf8", errors="replace", timeout=60).stdout
    names, default, reading = [], "", False
    for line in out.splitlines():
        if line.startswith("audio out:"):
            reading = True
            continue
        if line.startswith("audio out default:"):
            default = line.split(":", 1)[1].strip()
            default = re.sub(r"\s+\(the OS says .*\)$", "", default)
            reading = False
            continue
        if reading:
            if not line.startswith("  "):
                reading = False
                continue
            names.append(re.sub(r"\s+\[[^\]]*\]$", "", line.strip()))
    return names, default


def audio(db):
    """AUDIO's three answers: what is asked for, where it is, the system output."""
    reply = db.send("AUDIO")
    parts = dict(re.findall(r"(asks|on|system output|desk sounds on) (.*?)(?= \| |$)",
                            reply.split(":", 1)[-1].strip()))
    return reply, parts


def wait_on(db, device, seconds):
    deadline = time.monotonic() + seconds
    while True:
        reply, parts = audio(db)
        if parts.get("on") == device or time.monotonic() > deadline:
            return reply, parts
        time.sleep(0.25)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: a playlist on the system output follows it\n")

    names, sdl_default = devices(exe)
    others = [n for n in names if n and n != sdl_default]
    # A virtual output if there is one, so nothing real is even opened on.
    others.sort(key=lambda n: 0 if re.search(r"steam streaming|virtual|null|dummy", n, re.I) else 1)
    if not sdl_default or not others:
        print("NOT CHECKED: this machine has %d output(s); the check needs two." % len(names))
        return 0
    target = others[0]
    print("  outputs: %d; SDL's default %r; standing in for the OS: %r\n" % (len(names), sdl_default, target))

    work = Path(tempfile.mkdtemp(prefix="deckboy-audio-follow-"))
    show = work / "show.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\nui_scale\t1.0\n", encoding="utf8")
    os_answer = work / "system-output.txt"
    os_answer.write_text("", encoding="utf8")
    # The real audio driver: the dummy one has a single device and nothing to move to.
    env = {"SDL_AUDIODRIVER": None, "DECKBOY_TEST_SYSTEM_OUTPUT_FILE": str(os_answer)}
    with Runtime(exe, work / "run", show, env=env) as db:
        reply, parts = audio(db)
        print("  " + reply)
        note("asks for the system output", parts.get("asks") == "system output", reply)
        note("starts on SDL's default, which is the system output", parts.get("on") == sdl_default, reply)
        note("nothing to fix: desk sounds there too", parts.get("desk sounds on") == sdl_default, reply)

        # The OS moves its output; SDL's default does not (it "missed" it).
        time.sleep(2.5)                   # past the first 2 s check, so the move is a change
        os_answer.write_text(target + "\n", encoding="utf8")
        reply, parts = wait_on(db, target, 6)
        print("  " + reply)
        note("system output reads the OS's answer", parts.get("system output") == target, reply)
        note("the playlist followed it within the 2 s check", parts.get("on") == target, reply)
        note("the desk sounds followed it", parts.get("desk sounds on") == target, reply)
        note("the request is unchanged (still the system output)", parts.get("asks") == "system output", reply)

        # Headphones out: the OS goes back to where SDL still is.
        os_answer.write_text(sdl_default + "\n", encoding="utf8")
        reply, parts = wait_on(db, sdl_default, 6)
        print("  " + reply)
        note("back on SDL's default when the OS goes back", parts.get("on") == sdl_default, reply)
        note("the desk sounds came back too", parts.get("desk sounds on") == sdl_default, reply)

        # And with no disagreement at all, nothing moves.
        os_answer.write_text("", encoding="utf8")
        time.sleep(4.5)
        reply, parts = audio(db)
        note("no OS answer: stays on SDL's default", parts.get("on") == sdl_default, reply)

    log = (work / "run" / "app.log").read_text(encoding="utf8", errors="replace")
    moved = [l for l in log.splitlines() if "after the system output moved to" in l]
    for line in moved:
        print("  log: " + line)
    note("each move logged once (no retry storm)", len(moved) == 2, "%d move lines" % len(moved))
    said = [l for l in log.splitlines() if l.startswith("audio: now on ")]
    note("the desk said where the sound went, once a move",
         said == ["audio: now on " + target, "audio: now on " + sdl_default], said)

    print()
    if fails:
        print("FAIL: %d case(s): %s" % (len(fails), ", ".join(fails)))
        return 1
    print("audio follow: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
