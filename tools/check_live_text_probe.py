"""The live text probe reports what really reached the control window.

WHY THIS EXISTS. Issue #6/#7: on a Mac mini (M2, Ventura, two 1080p screens)
every startup text probe passed while the operator saw almost no labels, so
the startup probes are not evidence about real frames. The live probe reads
labels back from a real frame a few seconds in. This checks it runs, finds
ink where labels are drawn on a machine that draws them, and saves the frame
-- so the line it writes on a broken machine can be trusted as a contrast.
Also check that an unreadable language font, selected after the startup
probes, cannot discard the fonts that already drew the desk successfully.

    python tools/check_live_text_probe.py
"""

import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, parse_args  # noqa: E402


def probe_line(db):
    log = os.path.join(db.root, "data", "deckboy-render.log")
    for _ in range(40):
        if os.path.exists(log):
            with open(log, encoding="utf-8", errors="replace") as src:
                found = [line for line in src if "live text probe 1" in line]
            if found:
                return found[-1]
        time.sleep(0.5)
    return ""


def main():
    args = parse_args(__doc__, 5893)
    fails = []

    def note(name, ok, info=""):
        print("  %-50s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append(name)

    baseline = Deckboy(args, prefix="deckboy-live-text-")
    with open(baseline.show, "w", encoding="utf-8") as show:
        show.write("ui_scale\t1.0\n")
    with baseline as db:
        print("check: the live text probe")
        print()
        log = os.path.join(db.root, "data", "deckboy-render.log")
        line = probe_line(db)
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
        baseline_labels = re.search(r"labels=(\d+)", line)
        baseline_first = re.search(r'\| #1 "([^"]+)"', line)

    # The first load uses the bundled faces and passes the startup probes.
    # Loading the show then selects this existing but invalid font, and asks
    # for 1.5x. Both that load and the 1x retry must leave the working 1x desk
    # intact. No system font or operator file is changed by this fixture.
    broken = Deckboy(args, prefix="deckboy-font-reload-")
    with open(os.path.join(broken.root, "data", "fonts", "probe-broken.ttf"), "wb") as font:
        font.write(b"not a font\n")
    with open(os.path.join(broken.root, "data", "lang", "probe-font.tsv"), "w", encoding="utf-8") as lang:
        lang.write("#name Font reload probe\n#font probe-broken.ttf\nNEW\tNEW\n")
    with open(broken.show, "w", encoding="utf-8") as show:
        show.write("language\tprobe-font\nui_scale\t1.5\n")
    with broken as db:
        print("\ncheck: a failed font reload keeps the readable desk")
        line = probe_line(db)
        print("   " + line.strip())
        labels = re.search(r"labels=(\d+)", line)
        first = re.search(r'\| #1 "([^"]+)"', line)
        note("failed reload keeps the desk's labels",
             baseline_labels and labels and
             int(labels.group(1)) >= int(baseline_labels.group(1)), line.strip())
        note("the first chrome label still draws",
             baseline_first and first and first.group(1) == baseline_first.group(1), line.strip())
        inks = [int(x) for x in re.findall(r" ink=(-?\d+)", line)]
        note("recovered labels have visible ink", len(inks) == 3 and all(i > 0 for i in inks), line.strip())
        log = os.path.join(db.root, "data", "deckboy-render.log")
        with open(log, encoding="utf-8", errors="replace") as src:
            diagnostic = src.read()
        note("the failed font is identified in the log",
             "font load failed:" in diagnostic and "probe-broken.ttf" in diagnostic)
    print()
    print("%d FAILED" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
