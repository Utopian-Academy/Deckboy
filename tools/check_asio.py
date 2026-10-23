#!/usr/bin/env python3
"""ASIO in the full app, judged from what was handed to the driver.

Needs Windows and an installed ASIO driver (ASIO4ALL is enough; --driver picks
another). The app writes the exact mix it gave the driver to a WAV when
DECKBOY_ASIO_CAPTURE is set, and everything here is read from that -- a room
cannot be measured from a script.

Two cues, 440 Hz and 660 Hz, through one deck:

  TAKE 440 (cut) ... TAKE 660 (2 s crossfade) ... PAUSE ... PLAY ... STOP
  ... TAKE 440 (cut)

and the checks, with the fault each one is there for:

  the first cue plays without a gap      (it went silent half a second in)
  the old cue fades out under the new    (two engines in the mix at once)
  no sample jump in the crossfade        (a click)
  PAUSE is silent, for as long as asked  (a paused cue kept sounding)
  STOP is as prompt as PAUSE             (the queue played out after it)
  nothing sounds after STOP
  the next take starts clean             (the stopped cue's queue came first)
  the app closes cleanly, exit code 0

The sound plays through the driver's device at low volume (MASTERVOL 20).

--rate runs the device at another rate (DECKBOY_ASIO_RATE), so the rate
converter is in the path: a driver that can run at 48 kHz otherwise never
converts, and every word-clocked interface at 44.1 kHz does.

  python tools/check_asio.py --exe build/windows/Release/Deckboy.exe
  python tools/check_asio.py --exe build/windows/Release/Deckboy.exe --rate 44100
"""
import argparse
import ctypes
import math
import os
import struct
import subprocess
import sys
import tempfile
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from deckboy_harness import Deckboy  # noqa: E402


def make_clip(path, hz, seconds=12):
    subprocess.run(["ffmpeg", "-v", "error", "-y",
                    "-f", "lavfi", "-i", "testsrc2=s=320x180:r=25:d=%d" % seconds,
                    "-f", "lavfi", "-i", "sine=frequency=%d:sample_rate=48000:duration=%d" % (hz, seconds),
                    "-af", "volume=0.25", "-ac", "2", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                    "-c:a", "aac", "-shortest", path], check=True)


def close_gracefully(pid):
    """WM_CLOSE to each of the app's top-level windows, hidden ones too. SDL
    turns it into a close request and the control window's ends the app --
    which is the only way the driver closes and the capture gets written. The
    harness's own exit terminates the process, and nothing is written."""
    from ctypes import wintypes
    user32 = ctypes.windll.user32
    hwnds = []
    proto = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def each(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            hwnds.append(hwnd)
        return True
    user32.EnumWindows(proto(each), 0)
    for hwnd in hwnds:
        user32.PostMessageW(hwnd, 0x0010, 0, 0)   # WM_CLOSE
    return len(hwnds)


def tone(seg, rate, hz):
    """Amplitude of one frequency in a window (Goertzel)."""
    k = 2 * math.cos(2 * math.pi * hz / rate)
    s1 = s2 = 0.0
    for v in seg:
        s0 = v + k * s1 - s2
        s2, s1 = s1, s0
    return math.sqrt(max(0.0, s1 * s1 + s2 * s2 - k * s1 * s2)) * 2 / len(seg)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--driver", default="ASIO4ALL v2")
    ap.add_argument("--port", type=int, default=5701)
    ap.add_argument("--rate", type=int, default=0, help="device rate to ask for (converter path)")
    ap.add_argument("--keep", action="store_true", help="keep the run's folder and log")
    cli = ap.parse_args()
    if sys.platform != "win32":
        print("ASIO is Windows only")
        return 0

    work = tempfile.mkdtemp(prefix="check-asio-")
    capture = os.path.join(work, "asio.wav")
    make_clip(os.path.join(work, "a440.mp4"), 440)
    make_clip(os.path.join(work, "b660.mp4"), 660)
    os.environ["DECKBOY_ASIO_CAPTURE"] = capture
    if cli.rate:
        os.environ["DECKBOY_ASIO_RATE"] = str(cli.rate)
    args = argparse.Namespace(exe=os.path.abspath(cli.exe), port=cli.port, keep=True, ffmpeg="")
    db = Deckboy(args, "check-asio-", extra_args=["--import", os.path.join(work, "a440.mp4"),
                                                  "--import", os.path.join(work, "b660.mp4")])
    # Armed at boot from the show, the way a saved show arms it.
    with open(db.show, "w") as f:
        f.write("asio_driver\t%s\nasio_channels\t2\n" % cli.driver)

    marks = {}   # taken AFTER each reply: the command has been handled by then
    exit_code = None
    with db:
        db.send("MASTERVOL 20")
        db.send("TRANSITION 0")
        db.send("SELECT 1")
        db.send("TAKE")
        time.sleep(2.5)
        db.send("TRANSITION 2.0")
        db.send("TRANSITIONSTYLE crossfade")
        db.send("SELECT 2")
        db.send("TAKE")
        time.sleep(3.0)
        db.send("PAUSE"); marks["pause"] = time.monotonic()
        time.sleep(1.0)
        db.send("PLAY"); marks["play"] = time.monotonic()
        time.sleep(1.0)
        db.send("STOP"); marks["stop"] = time.monotonic()
        time.sleep(0.7)
        # Back to the first cue on the same deck: whatever the stopped cue
        # left queued must not come out ahead of it.
        db.send("TRANSITION 0")
        db.send("SELECT 1")
        db.send("TAKE")
        time.sleep(1.0)
        # Deck volume to zero part-way through a crossfade: the cue on its way
        # out is still this deck's sound and must go quiet with the incoming
        # one (it kept its old volume until its fade ended -- audit #2).
        db.send("TRANSITION 2.0")
        db.send("SELECT 2")
        db.send("TAKE")
        time.sleep(0.6)
        db.send("VOLUME 0"); marks["vol0"] = time.monotonic()
        time.sleep(1.0)
        close_gracefully(db.proc.pid)
        try:
            exit_code = db.proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            pass

    log = os.path.join(db.root, "app.log")
    if not os.path.exists(capture):
        print("FAIL: no capture -- ASIO never opened (is '%s' installed?), or the app did not "
              "close cleanly. Log: %s" % (cli.driver, log))
        return 1

    w = wave.open(capture, "rb")
    ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
    x = struct.unpack("<%dh" % (n * ch), w.readframes(n))[0::ch]
    print("capture: %d ch, %d Hz, %.2f s" % (ch, rate, n / rate))
    if cli.rate and rate != cli.rate:
        # The driver would not switch: what ran was not the converter path,
        # and a pass here would claim it had been tested.
        print("FAIL: asked the driver for %d Hz and it ran at %d -- the converter was not tested"
              % (cli.rate, rate))
        return 1

    # Sound and silence, in 5 ms steps.
    step = rate // 200
    runs = []
    for i in range(0, n - step, step):
        loud = max(abs(v) for v in x[i:i + step]) >= 4
        if runs and runs[-1][0] == loud:
            runs[-1][2] = (i + step) / rate
        else:
            runs.append([loud, i / rate, (i + step) / rate])
    sounds = [r for r in runs if r[0]]

    # 50 ms windows: both tones then sit exactly on a bin, so neither leaks
    # into the other's reading.
    win = rate // 20

    def at(t, hz):
        i = int(t * rate)
        return tone(x[i:i + win], rate, hz) if 0 <= i and i + win <= n else 0.0

    fails = []

    def check(ok, what, detail=""):
        print("  %-44s %s  %s" % (what, "ok  " if ok else "FAIL", detail))
        if not ok:
            fails.append(what)

    check(len(sounds) == 3, "sound, pause, sound, stop, sound",
          "%d runs of sound" % len(sounds))
    if len(sounds) != 3:
        for loud, a, b in runs:
            print("    %-6s %6.3f -> %6.3f" % ("sound" if loud else "silent", a, b))
        print("%d FAILED (log %s)" % (len(fails), log))
        return 1
    first, second, third = sounds
    start = first[1]
    take2 = next((start + k / 100 for k in range(int((first[2] - start) * 100))
                  if at(start + k / 100, 660) > 20), None)
    check(take2 is not None, "the second cue is heard")
    if take2 is None:
        return 1
    steady = sorted(at(start + 0.2 + k * 0.05, 440) for k in range(int((take2 - start - 0.3) / 0.05)))
    level = steady[len(steady) // 2]
    low = min(steady) if steady else 0.0
    check(low > 0.5 * level, "the first cue plays without a gap",
          "lowest %.0f of %.0f" % (low, level))

    curve = [at(take2 + k * 0.1, 440) / level for k in range(0, 23)]
    rising = max((curve[k + 1] - curve[k] for k in range(len(curve) - 1)), default=0)
    check(0.3 < curve[10] < 0.9 and curve[22] < 0.05 and rising < 0.05,
          "the old cue fades out under the new",
          "440 at 0/1/2.2 s: %.2f %.2f %.2f" % (curve[0], curve[10], curve[22]))
    check(at(take2 + 0.2, 660) > 0.5 * level, "the new cue plays through the fade",
          "660 at 0.2 s: %.0f" % at(take2 + 0.2, 660))

    a, b = int((take2 - 0.5) * rate), int((take2 + 2.5) * rate)
    jump = max(abs(x[i + 1] - x[i]) for i in range(a, b - 1))
    # Two full-level sines move at most this far between samples.
    bound = 2.0 * level * 2 * math.pi * (440 + 660) / rate
    check(jump <= bound, "no sample jump in the crossfade", "largest %d, bound %.0f" % (jump, bound))

    asked = marks["play"] - marks["pause"]
    gap = second[1] - first[2]
    check(abs(gap - asked) < 0.06, "PAUSE is silent, for as long as asked",
          "%.0f ms for %.0f ms" % (gap * 1000, asked * 1000))
    # How much later the STOP went quiet than the PAUSE did, after their own
    # commands. STOP waits for the decoders to finish before it clears, and
    # the sound plays on meanwhile, as it does on any device: tens of ms. A
    # queue playing out after the stop is 85 ms and up.
    late = (second[2] - first[2]) - (marks["stop"] - marks["pause"])
    check(late < 0.075, "STOP is as prompt as PAUSE", "%+.0f ms" % (late * 1000))
    quiet = third[1] - second[2]
    check(quiet > 0.5, "nothing sounds after STOP", "%.0f ms of silence to the next take" % (quiet * 1000))
    stale = max(at(third[1] + k * 0.025, 660) for k in range(6))
    check(stale < 0.1 * level and at(third[1] + 0.2, 440) > 0.5 * level,
          "the next take starts clean",
          "660 in its first 175 ms: %.0f, then 440 at %.0f" % (stale, at(third[1] + 0.2, 440)))
    # Measured the way STOP is: how long after its command the sound ended,
    # against how long after PAUSE the pause went quiet.
    # A volume change applies from the next audio queued, not to what is
    # already queued (~120 ms), so it is slower than STOP. Released 0.99.418:
    # +1198 ms, the outgoing cue sounding to the end of its fade.
    hush = (third[2] - first[2]) - (marks["vol0"] - marks["pause"])
    check(hush < 0.25, "VOLUME 0 mid-crossfade silences both cues",
          "%+.0f ms after the command" % (hush * 1000))
    check(exit_code == 0, "the app closes cleanly", "exit code %s" % exit_code)

    if fails:
        print("%d FAILED (log %s)" % (len(fails), log))
        return 1
    print("asio: all cases pass")
    if not cli.keep:
        import shutil
        shutil.rmtree(db.root, ignore_errors=True)
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
