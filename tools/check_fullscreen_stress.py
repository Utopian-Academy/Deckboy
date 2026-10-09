#!/usr/bin/env python3
"""Switch an output in and out of fullscreen, fast and at awkward moments.

macOS animates every fullscreen switch, and a request that lands while one is
still running made AppKit end the app ("NSWindowStyleMaskFullScreen cleared on
a window outside of a full screen transition"). It showed up about once in six
CI runs, inside a check about something else. This makes the moment happen on
purpose: the fullscreen toggle, output off and on, and a raster change (which
rebuilds the window and re-enters fullscreen through the display-move retry),
each followed by a random gap from none to longer than the animation. After
every step Deckboy must still be running and answering.

    python tools/check_fullscreen_stress.py [--exe build/Deckboy]
    (DECKBOY_STRESS_ROUNDS sets how many switches; 40 by default)
"""

import os
import random
import sys
import time

from deckboy_harness import Deckboy, parse_args

STEPS = ["FULLSCREEN", "FULLSCREEN", "OUTPUT OFF", "OUTPUT ON",
         "VIDEO 1280x720", "VIDEO 960x540", "FULLSCREEN"]


def main():
    args = parse_args(__doc__, 5790)
    rounds = int(os.environ.get("DECKBOY_STRESS_ROUNDS", "40"))
    rng = random.Random(406)          # the same sequence every run
    with Deckboy(args, "dbfull-") as db:
        reply = db.send("OUTPUT ON")
        print("OUTPUT ON ->", reply)
        time.sleep(2.0)
        done = 0
        for n in range(rounds):
            step = rng.choice(STEPS)
            db.send(step)
            time.sleep(rng.uniform(0.0, 0.7))
            if db.proc.poll() is not None:
                print("FAIL: Deckboy ended after step %d (%s), exit %s"
                      % (n + 1, step, db.proc.returncode))
                print("test root (app.log, crash report):", db.root)
                return 1
            status = db.send("STATUS")
            if not status.startswith("DECKBOY"):
                print("FAIL: no answer after step %d (%s): %r" % (n + 1, step, status[:80]))
                return 1
            done += 1
        db.send("OUTPUT ON")
        time.sleep(1.5)
        if db.proc.poll() is not None or not db.send("STATUS").startswith("DECKBOY"):
            print("FAIL: Deckboy did not survive settling back to fullscreen")
            return 1
    print("fullscreen stress: %d switches, Deckboy running and answering throughout -- ok" % done)
    return 0


if __name__ == "__main__":
    sys.exit(main())
