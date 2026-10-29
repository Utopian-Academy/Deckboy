#!/usr/bin/env python3
"""The cue's matrix, the patch and speed like tape, through an ASIO driver.

Needs Windows and an installed ASIO driver (ASIO4ALL is enough; --driver picks
another), and plays quietly through it (MASTERVOL 20). Everything is judged
from DECKBOY_ASIO_CAPTURE, the WAV of exactly what the driver was given, two
channels wide here.

One cue, a 440 Hz tone the same on both sides:

  1. a channel matrix, both sides onto out 2 (it was ignored through ASIO,
     which only ever took the pair): out 1 silent, out 2 at twice the tone
  2. the playlist into the patch: RIGHT, out 2 of the ASIO driver, at -6;
     SPEAKERS, on the system output, is unreachable while ASIO plays the show
     and must say so. A one-channel name takes half of each side, so out 2
     is a quarter of what it was in 1, and out 1 stays silent
  3. a SPEED fade to 0.5x: the driver is given the tone an octave down,
     220 Hz, at the level it had, and no 440 Hz left

    python tools/check_asio_patch.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import math
import os
import re
import struct
import subprocess
import sys
import tempfile
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from deckboy_harness import Deckboy  # noqa: E402
from check_asio import make_clip, close_gracefully, tone  # noqa: E402


def playhead(status):
    """The first deck's pos=MM:SS.s from STATUS, in seconds."""
    match = re.search(r"pos=(\d+):(\d+(?:\.\d+)?)", status)
    return int(match.group(1)) * 60 + float(match.group(2)) if match else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--driver", default="ASIO4ALL v2")
    ap.add_argument("--port", type=int, default=5702)
    cli = ap.parse_args()
    if sys.platform != "win32":
        print("ASIO is Windows only")
        return 0

    work = tempfile.mkdtemp(prefix="check-asio-patch-")
    capture = os.path.join(work, "asio.wav")
    make_clip(os.path.join(work, "a440.mp4"), 440)
    os.environ["DECKBOY_ASIO_CAPTURE"] = capture
    args = argparse.Namespace(exe=os.path.abspath(cli.exe), port=cli.port, keep=True, ffmpeg="")
    db = Deckboy(args, "check-asio-patch-", extra_args=["--import", os.path.join(work, "a440.mp4")])
    with open(db.show, "w") as f:
        f.write("asio_driver\t%s\nasio_channels\t2\n" % cli.driver)

    fails = []

    def check(ok, what, detail=""):
        print("  %-60s %s  %s" % (what, "ok  " if ok else "FAIL", detail))
        if not ok:
            fails.append(what)

    marks = {}
    exit_code = None
    with db:
        db.send("MASTERVOL 20")
        db.send("TRANSITION 0")
        db.send("SELECT 1")
        print("   ", db.send("MATRIX SET 1 2 100").strip())
        print("   ", db.send("MATRIX SET 2 2 100").strip())
        db.send("TAKE")
        marks["take"] = time.monotonic()
        time.sleep(2.0)

        asio = "ASIO: " + cli.driver
        for line in ("MATRIX CLEAR", "OUTPUTS ADD LEFT CH 1 DEVICE " + asio,
                     "OUTPUTS ADD RIGHT CH 2 DEVICE " + asio, "OUTPUTS ADD SPEAKERS CH 1-2",
                     "PATCH 1 RIGHT -6", "PATCH 1 SPEAKERS 0"):
            db.send(line)
        marks["patch"] = time.monotonic()
        outputs = db.send("OUTPUTS").strip()
        print("   ", outputs)
        check("LEFT: " + asio + " 1, ok" in outputs and "RIGHT: " + asio + " 2, ok" in outputs,
              "names on the ASIO driver are reachable", "")
        check("SPEAKERS: system output 1-2, unpatched: the show plays through ASIO" in outputs,
              "a name on the system output says ASIO has the show", "")
        time.sleep(2.0)

        made = db.send("FADE NEW")
        for line in ("FADE WHAT speed", "FADE TO 0.5", "FADE OVER 0"):
            db.send(line)
        reply = db.send("FADE FIRE")   # this harness hands back ERR replies as they are
        marks["speed"] = time.monotonic()
        print("   ", reply.strip())
        check(reply.startswith("OK"), "a SPEED fade fires through ASIO", reply.strip())
        # The picture slows with it: the playhead moves one second in two.
        time.sleep(0.3)
        before = playhead(db.send("STATUS"))
        time.sleep(2.0)
        after = playhead(db.send("STATUS"))
        moved = after - before if before is not None and after is not None else None
        check(moved is not None and 0.8 <= moved <= 1.2, "and the picture with it: one second in two",
              "%s -> %s" % (before, after))
        close_gracefully(db.proc.pid)
        try:
            exit_code = db.proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            pass
        del made

    if not os.path.exists(capture):
        print("FAIL: no capture -- ASIO never opened (is '%s' installed?)" % cli.driver)
        return 1
    w = wave.open(capture, "rb")
    ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
    frames = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    out1, out2 = frames[0::ch], frames[1::ch]
    print("capture: %d ch, %d Hz, %.2f s" % (ch, rate, n / rate))

    # The first sound is the TAKE; everything else is timed from it.
    start = next((i for i in range(n) if abs(out2[i]) >= 4 or abs(out1[i]) >= 4), None)
    if start is None:
        check(False, "the cue is heard at all")
        return 1
    win = rate // 2   # half a second: 440 Hz sits on a bin

    def level(channel, t, hz=440):
        i = start + int(t * rate)
        seg = channel[i:i + win]
        return tone(seg, rate, hz) if len(seg) == win else 0.0

    patched_at = marks["patch"] - marks["take"]
    matrix1, matrix2 = level(out1, 0.8), level(out2, 0.8)
    patch1, patch2 = level(out1, patched_at + 1.0), level(out2, patched_at + 1.0)
    check(matrix2 > 20 and matrix1 < 0.01 * matrix2,
          "the cue's matrix through ASIO: both sides on out 2, out 1 silent",
          "out 1 %.1f, out 2 %.1f" % (matrix1, matrix2))
    ratio = patch2 / matrix2 if matrix2 else 0.0
    check(abs(ratio - 0.25) < 0.03 and patch1 < 0.01 * matrix2,
          "in the patch: RIGHT at -6, one channel, out 1 silent",
          "out 2 at %.3f of the matrix's (0.25 wanted), out 1 %.1f" % (ratio, patch1))
    sped_at = marks["speed"] - marks["take"]
    slow220, slow440 = level(out2, sped_at + 0.9, 220), level(out2, sped_at + 0.9, 440)
    check(patch2 > 0 and abs(slow220 / patch2 - 1.0) < 0.1 and slow440 < 0.05 * patch2,
          "at 0.5x through ASIO: 220 Hz at the level it had, no 440 left",
          "220 Hz %.1f, 440 Hz %.1f, level before %.1f" % (slow220, slow440, patch2))
    check(exit_code == 0, "the app closes cleanly", "exit code %s" % exit_code)
    print()
    if fails:
        print("asio patch: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("asio patch: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
