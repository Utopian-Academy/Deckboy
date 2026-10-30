#!/usr/bin/env python3
"""Removing a playlist stops that playlist, and only that one.

Four playlists each play a tone at their own pitch -- 1000, 2000, 1260 and
1587 Hz for playlists 1 to 4 -- and output 1, showing playlist 4, is recording.
Then playlist 2 is removed, in the middle of the show:

  - playlists 1, 3 and 4 play on, as 1, 2 and 3, their playheads carrying on
    from where they were rather than starting again (removing used to rebuild
    every playlist, which stopped them all)
  - "Deck 3" and "Deck 4" become "Deck 2" and "Deck 3", and output 1 still
    shows playlist 4, now number 3
  - the recording keeps playlist 4's tone through the removal with no gap: the
    sound feed moved to its new number with it (it used to be captured once,
    so a playlist that moved up recorded as its old number)

UNDO then puts playlist 2 back where it was, stopped, with the other three
still playing and output 1 back on playlist 4. REDO takes it out again, the
same way.

    py -3 tools/check_deck_remove.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402
from check_output_limiter import quick_action  # noqa: E402

PITCH = {1: 1000.0, 2: 2000.0, 3: 1000.0 * 2 ** (1 / 3), 4: 1000.0 * 2 ** (2 / 3)}
STEPS = {1: 0, 2: 3, 3: 1, 4: 2}

fails = []


def note(name, ok, info=""):
    print("  %-66s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def decks(db):
    """[(name, status, position seconds)] for every playlist, in order."""
    out = []
    for line in db.send("STATUS").splitlines():
        m = re.match(r'DECK (\d+) .*?name="([^"]*)".*?status=(\w+)', line)
        if not m:
            continue
        pos = re.search(r"pos=(\d+):(\d+(?:\.\d+)?)", line)
        seconds = int(pos.group(1)) * 60 + float(pos.group(2)) if pos else None
        out.append((m.group(2), m.group(3), seconds))
    return out


def tone_db(samples, rate, hz):
    n = len(samples)
    if n == 0:
        return -200.0
    coeff = 2.0 * math.cos(2.0 * math.pi * hz / rate)
    s1 = s2 = 0.0
    for x in samples:
        s1, s2 = x + coeff * s1 - s2, s1
    power = max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)
    amplitude = 2.0 * math.sqrt(power) / n
    return 20.0 * math.log10(amplitude) if amplitude > 1e-10 else -200.0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: removing one playlist of four\n")

    work = Path(tempfile.mkdtemp(prefix="deckboy-deck-remove-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "01-picture-on-screen.deckboy"
    recordings = work / "show" / "recordings"
    freq_up = quick_action("ToneFreqInc")
    with Runtime(exe, work / "run", show) as db:
        for _ in range(3):
            db.send("DECKADD")
        for d in (1, 2, 3, 4):
            db.send("DECK %d" % d)
            db.send("TONECUE")
            db.send("SELECT LAST")
            for _ in range(STEPS[d]):
                db.send("QUICK %d" % freq_up)
            db.send("TAKE")
        db.send("OUTPUT on")
        db.send("RECFORMAT 640x360 25")
        print("    " + db.send("OUTPUT DECK 4"))
        time.sleep(1.5)
        before = decks(db)
        print("    before: %s" % before)
        note("four playlists, all playing", len(before) == 4 and all(s == "Playing" for _, s, _ in before),
             before)

        db.send("RECORD on")
        # How long the take runs before the removal: the stretch before it is
        # the control, sound from four playlists with nothing being removed.
        pre = float(os.environ.get("DECKBOY_CHECK_PRE_REMOVE", "2.5"))
        time.sleep(pre)
        at_removal = decks(db)
        reply = db.send("DECKREMOVE 2")
        print("    " + reply)
        time.sleep(2.5)
        after = decks(db)
        print("    after:  %s" % after)
        note("three left, and all three still playing",
             len(after) == 3 and all(s == "Playing" for _, s, _ in after), after)
        kept = [at_removal[0], at_removal[2], at_removal[3]]
        carried = len(after) == 3 and all(
            k[2] is not None and a[2] is not None and a[2] >= k[2] + 2.0 for k, a in zip(kept, after))
        note("their playheads carried on: none started again", carried, (kept, after))
        note("the names follow the places: Deck 3 is now Deck 2, Deck 4 Deck 3",
             len(after) == 3 and after[1][0] == "Deck 2" and after[2][0] == "Deck 3", after)
        db.send("OUTPUT SELECT 1")             # recording adds an output of its own
        reply = db.send("OUTPUT DECK")
        note("output 1 still shows playlist 4, now number 3", "deck 3" in reply, reply)
        time.sleep(1.0)
        db.send("RECORD off")
        time.sleep(3.0)

        db.send("UNDO")
        time.sleep(1.2)
        undone = decks(db)
        print("    undone: %s" % undone)
        note("UNDO puts playlist 2 back where it was, stopped",
             len(undone) == 4 and undone[1][0] == "Deck 2" and undone[1][1] != "Playing", undone)
        note("and the other three play on through the undo",
             len(undone) == 4 and all(undone[i][1] == "Playing" for i in (0, 2, 3)) and
             all(undone[i][2] is not None and undone[i][2] >= after[j][2] for i, j in ((0, 0), (2, 1), (3, 2))),
             (after, undone))
        print("    " + db.send("OUTPUT LIST"))
        db.send("OUTPUT SELECT 1")
        reply = db.send("OUTPUT DECK")
        note("output 1 is back on playlist 4", "deck 4" in reply, reply)

        db.send("REDO")
        time.sleep(1.2)
        redone = decks(db)
        print("    redone: %s" % redone)
        note("REDO takes it out again, the others still playing",
             len(redone) == 3 and all(s == "Playing" for _, s, _ in redone), redone)

    files = sorted(recordings.glob("*.*"), key=lambda p: p.stat().st_mtime)
    if not files:
        note("the recording was written", False, "nothing in " + str(recordings))
    else:
        pcm = subprocess.run(["ffmpeg", "-v", "error", "-i", str(files[-1]), "-map", "0:a:0", "-ac", "1",
                              "-ar", "48000", "-f", "s16le", "-"], capture_output=True).stdout
        samples = [v / 32768.0 for v in struct.unpack("<%dh" % (len(pcm) // 2), pcm[:len(pcm) // 2 * 2])]
        step = 12000                            # a quarter of a second
        windows = [samples[i:i + step] for i in range(step, len(samples) - step, step)]
        own = [tone_db(w, 48000, PITCH[4]) for w in windows]
        others = [max(tone_db(w, 48000, PITCH[d]) for d in (1, 2, 3)) for w in windows]
        median = sorted(own)[len(own) // 2] if own else -200.0
        print("    recording: %d quarter-seconds, playlist 4's tone %.1f dB (lowest %.1f), others at most %.1f" %
              (len(own), median, min(own) if own else -200.0, max(others) if others else -200.0))
        # Where any dip is, against the removal (2.5 s after RECORD on): a gap
        # at the head of the take is a different fault from one at the removal.
        dips_before = dips_after = 0
        for i, level in enumerate(own):
            if level < median - 3.0:
                at = (i + 1) * 0.25
                if at + 0.25 <= pre:
                    dips_before += 1
                else:
                    dips_after += 1
                print("    dip: %.1f dB at %.2f-%.2f s into the take (the removal was at about %.1f s)" %
                      (level, at, at + 0.25, pre))
        print("    dips: %d in the %.1f s before the removal, %d after" % (dips_before, pre, dips_after))
        note("the recording keeps playlist 4's tone through the removal, no gap",
             len(own) >= 12 and min(own) > median - 3.0, (min(own) if own else None, median))
        note("and nothing of the other playlists", others and max(others) < median - 40.0,
             max(others) if others else None)

    shutil.rmtree(work, ignore_errors=True)
    print()
    if fails:
        print("deck remove: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("deck remove: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
