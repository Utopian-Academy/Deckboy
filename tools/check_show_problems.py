"""CHECK sees what is wrong with the MACHINE, not just the show.

WHY THIS EXISTS. The broken-cue panel's plan listed missing media, missing
plugins, missing devices and dangling references. Media and references were
built; a playlist set to a sound device that is not plugged in, and a plugin
a cue uses that is not installed, were not on the list -- the show played on
(the default device; a silent slot) and the operator found out at the cue.

This saves a show, points its deck at a sound device that does not exist,
opens it again, and asks CHECK.

    python tools/check_show_problems.py
"""

import os
import shutil
import time
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, parse_args  # noqa: E402

GHOST = "Deckboy Test Ghost Speaker"


def main():
    keep = ""
    if "--keep" in sys.argv:
        i = sys.argv.index("--keep")
        keep = sys.argv[i + 1]
        del sys.argv[i:i + 2]
    args = parse_args(__doc__, 5891)
    fails = []

    def note(name, ok, info=""):
        print("  %-50s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append(name)

    print("check: CHECK lists a sound device that is not connected")
    print()
    with Deckboy(args, prefix="deckboy-problems-") as db:
        db.send("PATTERN")
        show = db.saved_show()
    lines = show.splitlines()
    for i, line in enumerate(lines):
        if line.startswith("deck\t"):
            fields = line.split("\t")
            fields[7] = GHOST            # audioOutputDeviceName (see saveProject)
            lines[i] = "\t".join(fields)
            break
    else:
        sys.exit("the saved show has no deck line")

    second = Deckboy(args, prefix="deckboy-problems2-")
    with open(second.show, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    with second as db:
        reply = db.send("CHECK")
        note("CHECK names the missing sound device", GHOST in reply and "not connected" in reply, reply)
        reply = db.send("CHECK 1")
        note("CHECK 1 goes to it", reply.startswith("OK") and "deck 1" in reply, reply)
        # The button's list, drawn: open it the way the button does and keep a
        # picture of the control window for a person to look at.
        reply = db.send("CHECK LIST")
        note("CHECK LIST opens the list", reply.startswith("OK") and "listed" in reply, reply)
        snap = os.path.join(db.root, "problems.bmp")
        db.send("UISNAP " + snap)
        for _ in range(40):
            if os.path.exists(snap) and os.path.getsize(snap) > 1000:
                break
            time.sleep(0.1)
        note("the control window was captured", os.path.exists(snap), snap)
        if keep and os.path.exists(snap):
            shutil.copy(snap, keep)
    print()
    print("%d FAILED" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
