#!/usr/bin/env python3
"""Switch an output in and out of fullscreen, fast and at awkward moments.

macOS animates every fullscreen switch, and a request that lands while one is
still running made AppKit end the app ("NSWindowStyleMaskFullScreen cleared on
a window outside of a full screen transition"). It showed up about once in six
CI runs, inside the A/V timing check, a few seconds after launch -- straight
after `OUTPUT ON` and then `VIDEO 960x540` with no gap, so the raster change
rebuilt the output window while its first fullscreen animation was running.

Phase 1 relaunches Deckboy and sends exactly that pair, many times over. Phase
2 runs the fullscreen toggle, output off and on, and raster changes with random
gaps from none to longer than the animation. After every step Deckboy must
still be running and answering.

    python tools/check_fullscreen_stress.py [--exe build/Deckboy]
    (DECKBOY_STRESS_LAUNCHES, 15 by default; DECKBOY_STRESS_ROUNDS, 40)
"""

import os
import random
import sys
import time

from deckboy_harness import Deckboy, parse_args

STEPS = ["FULLSCREEN", "FULLSCREEN", "OUTPUT OFF", "OUTPUT ON",
         "VIDEO 1280x720", "VIDEO 960x540", "FULLSCREEN"]


def alive(db, what):
    if db.proc.poll() is not None:
        print("FAIL: Deckboy ended %s (exit %s); test root: %s"
              % (what, db.proc.returncode, db.root))
        return False
    status = db.send("STATUS")
    if not status.startswith("DECKBOY"):
        print("FAIL: no answer %s: %r" % (what, status[:80]))
        return False
    return True


def main():
    args = parse_args(__doc__, 5790)
    launches = int(os.environ.get("DECKBOY_STRESS_LAUNCHES", "15"))
    rounds = int(os.environ.get("DECKBOY_STRESS_ROUNDS", "40"))
    rng = random.Random(406)          # the same sequence every run

    # Phase 1: the crash's own sequence, from a fresh launch each time.
    for n in range(launches):
        with Deckboy(args, "dbfull-") as db:
            db.send("OUTPUT ON")
            db.send("VIDEO %s" % ("960x540" if n % 2 == 0 else "1280x720"))
            for _ in range(8):
                time.sleep(0.4)
                if not alive(db, "after launch %d: OUTPUT ON, then VIDEO at once" % (n + 1)):
                    return 1
    print("launch, OUTPUT ON, VIDEO at once: %d launches survived" % launches)

    # Phase 2: switching at random moments within one session.
    with Deckboy(args, "dbfull-") as db:
        db.send("OUTPUT ON")
        done = 0
        for n in range(rounds):
            step = rng.choice(STEPS)
            db.send(step)
            time.sleep(rng.uniform(0.0, 0.7))
            if not alive(db, "after step %d (%s)" % (n + 1, step)):
                return 1
            done += 1
        db.send("OUTPUT ON")
        time.sleep(1.5)
        if not alive(db, "settling back to fullscreen"):
            return 1
    print("fullscreen stress: %d launches and %d switches, Deckboy running and answering "
          "throughout -- ok" % (launches, done))
    return 0


if __name__ == "__main__":
    sys.exit(main())
