"""The live text probe reports what really reached the control window.

WHY THIS EXISTS. Issue #6/#7: on a Mac mini (M2, Ventura, two 1080p screens)
every startup text probe passed while the operator saw almost no labels, so
the startup probes are not evidence about real frames. The live probe reads
labels back from a real frame a few seconds in. This checks it runs, finds
ink where labels are drawn on a machine that draws them, and saves the frame
-- so the line it writes on a broken machine can be trusted as a contrast.

    python tools/check_live_text_probe.py
"""

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, parse_args  # noqa: E402


def main():
    args = parse_args(__doc__, 5893)
    fails = []

    def note(name, ok, info=""):
        print("  %-50s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append(name)

    with Deckboy(args, prefix="deckboy-live-text-") as db:
        print("check: the live text probe")
        print()
        log = os.path.join(db.root, "data", "deckboy-render.log")
        line = ""
        for _ in range(40):
            if os.path.exists(log):
                found = [l for l in open(log, encoding="utf-8", errors="replace") if "live text probe 1" in l]
                if found:
                    line = found[-1]
                    break
            time.sleep(0.5)
        note("a live probe line is written a few seconds in", bool(line), "no line in %s" % log)
        print("   " + line.strip())
        inks = [int(x) for x in re.findall(r" ink=(-?\d+)", line)]
        note("three labels sampled (first, middle, last)", len(inks) == 3, line.strip())
        note("every sampled label has ink on this machine", inks and all(i > 0 for i in inks), line.strip())
        note("no 'labels with no ink' warning here", "NO INK" not in line, line.strip())
        note("window size, density and display count recorded",
             all(k in line for k in ("window=", "density=", "displays=")), line.strip())
        frame = os.path.join(db.root, "data", "deckboy-live-frame.bmp")
        note("the frame as drawn is saved beside the log", os.path.exists(frame) and os.path.getsize(frame) > 10000, frame)
    print()
    print("%d FAILED" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
