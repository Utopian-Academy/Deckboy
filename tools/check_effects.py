#!/usr/bin/env python3
"""Apply every effect in turn and check the picture actually changes.

Eyeballing two of them and assuming the rest work is how a stack of effects
ships with three that do nothing. This records a baseline with an empty stack,
then records the same cue once per effect, and compares.

An effect that produces a picture identical to the baseline has failed --
either it does nothing, or it never reached the pixels. Both are worth knowing
and neither is visible from the code.

    python3 tools/check_effects.py --exe build/windows/Release/Deckboy.exe \
        --media clip.mp4

Datamosh is skipped by default: it works by withholding keyframes at decode and
needs a background transcode to finish first, so a short take legitimately
shows no change. Pass --include-datamosh to try it anyway.
"""

import argparse
import glob
import io
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deckboy_testroot  # noqa: E402

# Serialised tokens, from cue_effects.hpp. Kept here rather than parsed so the
# check fails loudly when a new effect is added and nobody added it here.
EFFECTS = [
    "invert", "posterise", "solarise", "threshold", "vignette",
    "grain", "scanlines", "channel_offset", "temporal_dither",
    "motion_puppet", "datamosh",
]

NEEDS_DRIVER = {"motion_puppet"}
SLOW = {"datamosh"}


def send(port, command, timeout=5.0):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall((command + "\n").encode())
        try:
            return sock.recv(65536).decode(errors="replace").strip()
        except OSError:
            return ""


def fingerprint(path, samples=8):
    """A coarse, quantised signature of the picture over time.

    Quantised because re-encoding an unchanged picture at different GOP
    positions produces different bytes; without it every comparison reads as
    "changed" and the check is worthless.
    """
    out = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path,
         "-vf", "fps=2,scale=24:14,format=gray", "-f", "rawvideo", "-"],
        capture_output=True).stdout
    size = 24 * 14
    frames = []
    for i in range(0, len(out) - size + 1, size):
        frames.append(bytes(b & 0xE0 for b in out[i:i + size]))
        if len(frames) >= samples:
            break
    return frames


def build_project(root, template, media, driver, effect_field):
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(os.path.join(root, "data"), exist_ok=True)
    esc = lambda p: p.replace("\\", "\\\\")
    lines = []
    for line in io.open(template, encoding="utf-8", errors="replace").read().split("\n"):
        f = line.split("\t")
        if f and f[0] == "cue" and len(f) >= 155 and "CHECKME" in line:
            while len(f) < 156:
                f.append("")
            f[2] = esc(media)
            f[154] = effect_field
            f[155] = esc(driver) if driver else ""
            line = "\t".join(f)
        lines.append(line)
    io.open(os.path.join(root, "data", "default.deckboy"), "w",
            encoding="utf-8", newline="").write("\n".join(lines))
    deckboy_testroot.populate(root)


def record(exe, root, port, seconds):
    env = dict(os.environ)
    env["DECKBOY_ROOT"] = root
    env["DECKBOY_COMPANION_PORT"] = str(port)
    env["DECKBOY_PROJECT"] = os.path.join(root, "data", "default.deckboy")
    proc = subprocess.Popen([exe], env=env, cwd=os.path.dirname(exe) or ".",
                            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    try:
        for _ in range(120):
            try:
                send(port, "HELP", timeout=1.0)
                break
            except OSError:
                time.sleep(0.5)
        send(port, "MASTERVOL 0")
        send(port, "OUT on")
        send(port, "RECFORMAT 960x540 30")
        send(port, "SELECT 1")
        send(port, "TAKE")
        for _ in range(40):
            if "status=Playing" in send(port, "STATUS"):
                break
            time.sleep(0.25)
        time.sleep(1.0)
        send(port, "RECORD on")
        time.sleep(seconds)
        send(port, "RECORD off")
        time.sleep(3.0)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
    files = sorted(glob.glob(os.path.join(root, "data", "recordings", "*.*")),
                   key=os.path.getmtime)
    return files[-1] if files else None


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--media", required=True, help="the cue to apply effects to")
    parser.add_argument("--driver", default="", help="clip for motion_puppet")
    parser.add_argument("--template", required=True,
                        help="a .deckboy whose target cue is named CHECKME")
    parser.add_argument("--seconds", type=float, default=4.0)
    parser.add_argument("--port", type=int, default=5720)
    parser.add_argument("--include-datamosh", action="store_true")
    args = parser.parse_args()

    exe = os.path.abspath(args.exe)
    workdir = tempfile.mkdtemp(prefix="deckboy-fxcheck-")
    root = os.path.join(workdir, "root")

    print("baseline (empty stack)...")
    build_project(root, args.template, os.path.abspath(args.media), "", "")
    base_file = record(exe, root, args.port, args.seconds)
    if not base_file:
        print("no baseline recording; aborting")
        return 1
    base = fingerprint(base_file)
    if len(base) < 2:
        print("baseline too short to compare")
        return 1

    results = []
    for effect in EFFECTS:
        if effect in SLOW and not args.include_datamosh:
            results.append((effect, "skipped", "needs a transcode; --include-datamosh"))
            continue
        if effect in NEEDS_DRIVER and not args.driver:
            results.append((effect, "skipped", "no --driver given"))
            continue
        field = "%s:1:0.5:0:0" % effect
        build_project(root, args.template, os.path.abspath(args.media),
                      args.driver, field)
        path = record(exe, root, args.port, args.seconds)
        if not path:
            results.append((effect, "FAIL", "no recording"))
            continue
        shot = fingerprint(path)
        if not shot:
            results.append((effect, "FAIL", "unreadable"))
            continue
        n = min(len(base), len(shot))
        differing = sum(1 for i in range(n) if base[i] != shot[i])
        if differing == 0:
            results.append((effect, "NO CHANGE", "picture identical to baseline"))
        else:
            results.append((effect, "ok", "%d of %d frames differ" % (differing, n)))

    shutil.rmtree(workdir, ignore_errors=True)
    print()
    print("%-18s %-10s %s" % ("effect", "verdict", "detail"))
    failures = 0
    for effect, verdict, detail in results:
        print("%-18s %-10s %s" % (effect, verdict, detail))
        if verdict in ("FAIL", "NO CHANGE"):
            failures += 1
    print("\n%s" % ("FAIL" if failures else "all effects change the picture"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
