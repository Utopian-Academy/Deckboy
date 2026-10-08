"""Does playback hold its frame rate while something else hammers the machine?

WHY THIS EXISTS. "If I'm playing a game like Noita simultaneously, I begin to
see some stuttering in Deckboy's playback." An average fps cannot show that: an
output that drops one frame in twenty still reads 57. So this reads the
output's own hitch counter (a present more than 1.5 frames after the last) from
STATUS, first on a quiet machine, then with every core held busy by ordinary
processes -- a game's threads, near enough, minus the GPU.

WHAT IT FOUND (2026-10-07). With every core busy the hitch rate went from
0.6% to 1.7-3.1%. Raising thread priority moved nothing. DECKBOY_UI_PROFILE=25
showed the time inside the output pass, in the control monitor's synchronous
readback (15-57ms of a 16.7ms frame); taking it through the staging ring
brought load down to 0.6-1.2%. The remaining 0.56% "quiet" floor on that
machine was the Windows compositor itself missing ~61ms every 3.02s with
Deckboy closed (DwmFlush in a loop shows it) -- a fault of the machine, so
compare against the quiet number, never against zero.

    python tools/check_playback_under_load.py [--seconds 15]

It arms an output window, so it takes over that display for the run.
"""

import multiprocessing
import os
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, parse_args  # noqa: E402


def burn(stop_at):
    x = 0
    while time.time() < stop_at:
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF


def output_counters(db):
    for line in db.send("STATUS").splitlines():
        if line.startswith("OUTPUT 1") or " presents=" in line:
            p = re.search(r"presents=(\d+)", line)
            h = re.search(r"hitches=(\d+)", line)
            w = re.search(r"worst_gap_ms=(\d+)", line)
            f = re.search(r"output_fps=([\d.]+)", line)
            if p and h:
                return (int(p.group(1)), int(h.group(1)),
                        int(w.group(1)) if w else -1, f.group(1) if f else "?")
    return None


def window(db, seconds, label):
    a = output_counters(db)
    time.sleep(seconds)
    b = output_counters(db)
    if not a or not b:
        print("  %-26s no output counters in STATUS" % label)
        return None
    presents = b[0] - a[0]
    hitches = b[1] - a[1]
    pct = 100.0 * hitches / presents if presents else 0.0
    print("  %-26s %5d presents  %4d hitches (%.2f%%)  fps %s" %
          (label, presents, hitches, pct, b[3]))
    return pct


def main():
    argv = list(sys.argv)
    seconds = 15.0
    if "--seconds" in argv:
        i = argv.index("--seconds")
        seconds = float(argv[i + 1])
        del sys.argv[i:i + 2]
    args = parse_args(__doc__, 5761)
    clip = os.path.join(tempfile.mkdtemp(prefix="deckboy-load-"), "motion.mp4")
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi",
                    "-i", "testsrc2=size=1920x1080:rate=60:duration=120",
                    "-pix_fmt", "yuv420p", "-c:v", "libx264", "-preset", "veryfast", clip],
                   capture_output=True)
    if not os.path.exists(clip):
        sys.exit("needs ffmpeg on PATH to make the test clip")
    with Deckboy(args, prefix="deckboy-load-", extra_args=["--import", clip]) as db:
        db.send("SELECT 1")
        db.send("LOOP ON")
        db.send("TAKE")
        print("output:", db.send("OUTPUT ON"))
        time.sleep(4.0)   # let the rate settle before anything is judged
        print()
        quiet = window(db, seconds, "quiet machine")
        cores = os.cpu_count() or 4
        stop_at = time.time() + seconds + 3.0
        burners = [multiprocessing.Process(target=burn, args=(stop_at,)) for _ in range(cores)]
        for p in burners:
            p.start()
        time.sleep(1.5)
        loaded = window(db, seconds, "%d cores held busy" % cores)
        for p in burners:
            p.join()
        db.send("OUTPUT OFF")
    print()
    if quiet is None or loaded is None:
        return 1
    print("hitch rate quiet %.2f%%, under load %.2f%%" % (quiet, loaded))
    return 0


if __name__ == "__main__":
    sys.exit(main())
