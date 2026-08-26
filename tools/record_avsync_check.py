#!/usr/bin/env python3
"""Measure audio/video sync in a recording, in frames.

The recording readback is a pipeline: the frame handed to the encoder is a few
frames behind the one just rendered, while audio arrives on its own path. If
nothing compensates, the file has video lagging audio by that much. Three frames
at 25p is 120ms, which is outside EBU R37 (audio may lead by 40ms, lag by 60ms)
and is the difference between a deliverable and a re-lay.

Nobody can eyeball 120ms reliably, so this measures it. It builds a clip whose
video flashes white and whose audio beeps together for 100ms, once a second,
records it, then finds both edges in the recorded file and reports the gap.

    python3 tools/record_avsync_check.py --exe ./build/Deckboy

A positive offset means VIDEO IS LATE (the flash arrives after the beep), which
is the direction the readback pipeline would push it.
"""

import argparse
import glob
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deckboy_testroot  # noqa: E402


def send(port, command, timeout=5.0):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall((command + "\n").encode())
        try:
            return sock.recv(65536).decode(errors="replace").strip()
        except OSError:
            return ""


def build_sync_clip(path, seconds, rate, width, height):
    """A MOVING base with a full-frame white flash and a beep on the same frame,
    once a second.

    The base is testsrc2 rather than black on purpose: a static base cannot tell
    a correct recording from one frozen on its first frame, and an early version
    of this tool was fooled by exactly that -- drawbox evaluated its position
    once at init, so the "moving" box never moved and the recording looked
    frozen when it was faithful.
    """
    subprocess.run([
        "ffmpeg", "-v", "error", "-y",
        "-f", "lavfi", "-i", "testsrc2=s=%dx%d:r=%s:d=%s" % (width, height, rate, seconds),
        "-f", "lavfi", "-i", "sine=frequency=1000:sample_rate=48000:duration=%s" % seconds,
        "-filter_complex",
        "[0:v]drawbox=x=0:y=0:w=iw:h=ih:c=white:t=fill:enable='lt(mod(t,1),0.1)'[v];"
        "[1:a]volume=0:enable='gte(mod(t,1),0.1)'[a]",
        "-map", "[v]", "-map", "[a]",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "18", "-pix_fmt", "yuv420p",
        "-c:a", "aac", "-shortest", path,
    ], check=True)


def edges(path, kind):
    """Start times of each flash (video) or beep (audio)."""
    if kind == "video":
        # NEGATE first: the flash is a white frame, and blackdetect is the
        # detector ffmpeg gives us. Inverted, a flash becomes a short black run,
        # and black_start is its leading edge -- the same edge silencedetect
        # reports for the beep, so neither measurement is privileged.
        filt = "negate,blackdetect=d=0.02:pic_th=0.85"
        pattern = r"black_start:(\d+\.?\d*)"
        flag = "-vf"
    else:
        filt = "silencedetect=n=-40dB:d=0.05"
        pattern = r"silence_end: (\d+\.?\d*)"
        flag = "-af"
    out = subprocess.run(["ffmpeg", "-v", "info", "-i", path, flag, filt,
                          "-f", "null", "-"],
                         capture_output=True, text=True).stderr
    return [float(m) for m in re.findall(pattern, out)]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--standard", default="1920x1080@30", help="WxH@fps to record at")
    parser.add_argument("--seconds", type=float, default=20.0)
    parser.add_argument("--port", type=int, default=5602)
    parser.add_argument("--keep", action="store_true", help="keep the recording")
    args = parser.parse_args()

    args.exe = os.path.abspath(args.exe)
    for tool in ("ffmpeg", "ffprobe"):
        if not shutil.which(tool):
            sys.exit("%s is not on PATH" % tool)

    raster, _, rate_text = args.standard.partition("@")
    width, _, height = raster.lower().partition("x")
    rate = float(rate_text)

    root = tempfile.mkdtemp(prefix="deckboy-avsync-")
    os.makedirs(os.path.join(root, "data"), exist_ok=True)
    deckboy_testroot.populate(root)
    clip = os.path.join(root, "syncclip.mp4")
    print("building sync clip...")
    build_sync_clip(clip, args.seconds + 12, rate, int(width), int(height))

    env = dict(os.environ)
    env["DECKBOY_ROOT"] = root
    env["DECKBOY_COMPANION_PORT"] = str(args.port)
    log = open(os.path.join(root, "log.txt"), "w")
    # --import, not a bare path: bare media leaves the splash/startup menu up
    # and TAKE never reaches the deck, so the recording is the empty monitor.
    proc = subprocess.Popen([args.exe, "--import", clip], env=env,
                            cwd=os.path.dirname(args.exe) or ".",
                            stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(120):
            try:
                send(args.port, "HELP", timeout=1.0)
                break
            except OSError:
                time.sleep(0.5)
        # Master volume must stay UP: the beep has to reach the recording.
        send(args.port, "OUT on")
        send(args.port, "RECFORMAT %s %s" % (raster, rate_text))
        send(args.port, "SELECT 1")
        send(args.port, "TAKE")
        time.sleep(3.0)
        send(args.port, "RECORD on")
        time.sleep(args.seconds)
        send(args.port, "RECORD off")
        time.sleep(4.0)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()

    files = sorted(glob.glob(os.path.join(root, "data", "recordings", "*.*")),
                   key=os.path.getmtime)
    if not files:
        print("no recording was written")
        return 1
    rec = files[-1]

    flashes = edges(rec, "video")
    beeps = edges(rec, "audio")
    print("flashes: %d   beeps: %d" % (len(flashes), len(beeps)))
    if not flashes or not beeps:
        print("could not find both edges in the recording -- check it by hand: %s" % rec)
        return 1

    # Pair each beep with its nearest flash. They are a second apart, so the
    # nearest is unambiguous unless the offset is already catastrophic.
    offsets = []
    for beep in beeps:
        nearest = min(flashes, key=lambda f: abs(f - beep))
        if abs(nearest - beep) < 0.5:
            offsets.append(nearest - beep)
    if not offsets:
        print("no flash/beep pairs within half a second -- sync is badly wrong")
        return 1

    offsets.sort()
    median = offsets[len(offsets) // 2]
    print("\npairs: %d" % len(offsets))
    print("offset: %+.1f ms  (%+.2f frames at %s)" % (median * 1000.0, median * rate, rate_text))
    print("range:  %+.1f ms to %+.1f ms" % (offsets[0] * 1000.0, offsets[-1] * 1000.0))
    print("\npositive = video late. EBU R37 allows audio to lead by 40ms and lag by 60ms.")
    verdict = "within EBU R37" if -0.060 <= median <= 0.040 else "OUTSIDE EBU R37"
    print("verdict: %s" % verdict)

    if args.keep:
        kept = os.path.join(os.getcwd(), os.path.basename(rec))
        shutil.copy2(rec, kept)
        print("kept: %s" % kept)
    shutil.rmtree(root, ignore_errors=True)
    return 0 if verdict.startswith("within") else 1


if __name__ == "__main__":
    sys.exit(main())
