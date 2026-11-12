"""Which actions are slow the FIRST time and fine after.

James, 2026-11-11: "Several things in deckboy cause a lag the first time they
are engaged." Each action below runs twice, a few seconds apart, with the
app's own slow-frame log on (DECKBOY_UI_PROFILE). A frame over the threshold
during the first run and not the second is a first-use lag: something loaded,
compiled or opened on the main thread on demand.

Windows hidden, so the desk paces itself without a screen; the threshold is
set above that pace.

    py -3 tools/check_first_use.py --exe build/windows/Release/Deckboy.exe [--ms 60]
"""
import argparse
import re
import shutil
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

SHOW = ROOT / "data/demos/starter-pack/02-running-a-show.deckboy"

# (name, commands for one run, commands that undo it so the second run repeats it)
ACTIONS = [
    ("take a text cue", ["DECK 1", "GOTO 1", "TAKE"], []),
    ("take a video cue", ["GOTO 2", "TAKE"], ["GOTO 1", "TAKE"]),
    ("arm output 1", ["OUTPUT SELECT 1", "OUTPUT ON"], ["OUTPUT OFF"]),
    ("arm output 1 (again, live)", ["OUTPUT ON"], []),
    ("take video with output on", ["GOTO 3", "TAKE"], ["GOTO 1", "TAKE"]),
    ("scopes on", ["SCOPES SCOPES"], ["SCOPES PROGRAM"]),
    ("multiview on", ["MULTIVIEW ON"], ["MULTIVIEW OFF"]),
    ("lessons window", ["LESSONS OPEN"], ["LESSONS CLOSE"]),
    ("new mask", ["GOTO 2", "MASK NEW circle"], ["MASK ASSIGN NONE CUE"]),
    ("new text cue", ["TEXTCUE NEW", "TAKE"], []),
    ("new lower third", ["TEXTCUE LOWERNEW", "TAKE"], []),
    ("language change", ["LANG de"], ["LANG en"]),
    ("record", ["RECORD ON"], ["RECORD OFF"]),
    ("transition crossfade", ["TRANSITION 1", "TRANSITIONSTYLE crossfade", "GOTO 2"], ["TRANSITION 0", "GOTO 3", "TAKE"]),
    ("transition pushleft", ["TRANSITION 1", "TRANSITIONSTYLE pushleft", "GOTO 2"], ["TRANSITION 0", "GOTO 3", "TAKE"]),
    ("transition wipeleft", ["TRANSITION 1", "TRANSITIONSTYLE wipeleft", "GOTO 2"], ["TRANSITION 0", "GOTO 3", "TAKE"]),
    ("transition iris", ["TRANSITION 1", "TRANSITIONSTYLE iris", "GOTO 2"], ["TRANSITION 0", "GOTO 3", "TAKE"]),
    ("transition portal", ["TRANSITION 1", "TRANSITIONSTYLE portal", "GOTO 2"], ["TRANSITION 0", "GOTO 3", "TAKE"]),
    ("transition shatter", ["TRANSITION 1", "TRANSITIONSTYLE shatter", "GOTO 2"], ["TRANSITION 0", "GOTO 3", "TAKE"]),
    ("transition clouds", ["TRANSITION 1", "TRANSITIONSTYLE clouds", "GOTO 2"], ["TRANSITION 0", "GOTO 3", "TAKE"]),
    ("transition ghastly", ["TRANSITION 1", "TRANSITIONSTYLE ghastly", "GOTO 2"], ["TRANSITION 0", "GOTO 3", "TAKE"]),
    ("transition dip", ["TRANSITION 1", "TRANSITIONSTYLE dip", "GOTO 2"], ["TRANSITION 0", "GOTO 3", "TAKE"]),
    ("effect blur", ["GOTO 2", "FX ADD blur"], ["FX CLEAR"]),
    ("effect glow", ["GOTO 2", "FX ADD glow"], ["FX CLEAR"]),
    ("effect kaleidoscope", ["GOTO 2", "FX ADD kaleidoscope"], ["FX CLEAR"]),
    ("effect feedback", ["GOTO 2", "FX ADD feedback"], ["FX CLEAR"]),
    ("effect caustics", ["GOTO 2", "FX ADD caustics"], ["FX CLEAR"]),
    ("effect crystallise", ["GOTO 2", "FX ADD crystallise"], ["FX CLEAR"]),
    ("effect ferrofluid", ["GOTO 2", "FX ADD ferrofluid"], ["FX CLEAR"]),
    ("effect relight", ["GOTO 2", "FX ADD relight"], ["FX CLEAR"]),
    ("effect curves", ["GOTO 2", "FX ADD curves"], ["FX CLEAR"]),
    ("settings window", ["@SETTINGS|SETUP|MENU"], ["KEY Escape"]),
    ("settings: audio tab", ["@SETTINGS|SETUP|MENU", "@AUDIO"], ["KEY Escape"]),
    ("settings: network tab", ["@SETTINGS|SETUP|MENU", "@NETWORK"], ["KEY Escape"]),
]

LINE = re.compile(r"\[DECKBOY_UI_PROFILE (\d+)ms\] frame dt=([\d.]+)ms(.*)")


def slow_frames(log, start):
    text = Path(log).read_text(encoding="utf-8", errors="replace")[start:]
    return [(float(m.group(2)), m.group(3).strip()) for m in LINE.finditer(text)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--work", default=str(ROOT / "build/first-use"))
    ap.add_argument("--ms", type=float, default=60.0, help="slow-frame threshold")
    ap.add_argument("--settle", type=float, default=2.5, help="seconds to wait after each run")
    args = ap.parse_args()

    env = {"DECKBOY_UI_PROFILE": str(args.ms)}
    # A COPY of the demo, media and all: the app autosaves the show it has
    # open and records beside it.
    work = Path(args.work)
    copy = work / "show"
    shutil.rmtree(copy, ignore_errors=True)
    shutil.copytree(SHOW.parent, copy)
    show = copy / SHOW.name
    results = []
    with Runtime(args.exe, work / "run", show, env=env) as db:
        log = db.log.name
        time.sleep(4.0)    # boot work is not what this measures
        for name, run, undo in ACTIONS:
            worst = []
            for attempt in range(2):
                mark = len(Path(log).read_text(encoding="utf-8", errors="replace"))
                answers = []
                for c in run:
                    try:
                        if c.startswith("@"):
                            # A button by its label: PRESS finds it, CLICK presses it.
                            found = None
                            for label in c[1:].split("|"):   # the first label that exists
                                try:
                                    found = re.search(r"at (-?\d+),(-?\d+) (\d+)x(\d+)", db.send("PRESS " + label))
                                except RuntimeError:
                                    continue
                                if found:
                                    break
                            db.send("PRESS OFF")
                            if not found:
                                raise RuntimeError("no button " + c[1:])
                            x, y, w, h = map(int, found.groups())
                            answers.append(db.send("CLICK %d %d" % (x + w // 2, y + h // 2)))
                            time.sleep(0.6)
                            continue
                        answers.append(db.send(c))
                    except RuntimeError as e:
                        answers.append("ERR " + str(e))
                time.sleep(args.settle)
                frames = slow_frames(log, mark)
                worst.append(max(frames, default=(0.0, ""), key=lambda f: f[0]))
                if any(a.startswith("ERR") for a in answers):
                    worst[-1] = (-1.0, " ".join(a for a in answers if a.startswith("ERR")))
                for c in undo:
                    try:
                        db.send(c)
                    except RuntimeError:
                        pass
                time.sleep(1.0)
            results.append((name, worst))
            first, second = worst
            flag = "FIRST-USE LAG" if first[0] > args.ms and first[0] > 2 * max(second[0], args.ms / 2) else ""
            print("  %-30s first %7.1f ms   second %7.1f ms   %s" % (name, first[0], second[0], flag), flush=True)
            if first[0] < 0:
                print("      %s" % first[1][:200], flush=True)
            if flag:
                print("      %s" % first[1][:200], flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
