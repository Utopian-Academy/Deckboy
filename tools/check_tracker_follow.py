#!/usr/bin/env python3
"""A tracker step set to "when its cues end" must wait for the cues it fired.

Step 1 takes a four-second trimmed clip (IN 0 / OUT 4, hold last frame); step 2
takes a blue clip. With step 1 on "when its cues end" and the sequence playing,
the switch to blue has to come when the clip actually finishes -- later at half
speed, never while it is paused -- and a fixed-length step has to keep its
timer. Each case runs the real app on a private desktop and polls STATUS.

    py -3 tools/check_tracker_follow.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402
from build_example_shows import CUE_FIELDS  # noqa: E402

SEED = "update_check\t0\nui_sounds\t0\nui_scale\t1.0\n"


def make_clip(path, colour=None):
    source = f"color=c={colour}:s=640x360:r=30" if colour else "testsrc2=s=640x360:r=30"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", source + ":d=8",
                    "-f", "lavfi", "-i", "sine=f=440:d=8", "-shortest",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", str(path)],
                   check=True)


def import_clip(db, work, clip):
    drop = work / "drop.txt"
    drop.write_text(str(clip) + "\n", encoding="utf8")
    db.send("DROPTEST " + str(drop))
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        reply = db.send("DROPTEST")
        if "pending=0" in reply and "probes=0" in reply:
            time.sleep(.4)
            return
        time.sleep(.25)
    raise RuntimeError("import did not finish: " + str(clip))


def build_show(exe, work, motion, blue, two_decks):
    show = work / "setup.deckboy"
    show.write_text(SEED, encoding="utf8")
    with Runtime(exe, work / "setup-run", show,
                 extra=("--import", str(motion), "--import", str(blue))) as db:
        db.commands("SELECT 1", "IN 0", "OUT 4", "LOOP OFF", "HOLD ON", "CONTINUE OFF")
        if two_decks:
            db.send("DECKADD")
            db.send("DECK 2")
            import_clip(db, work, motion)
            db.commands("SELECT 1", "IN 0", "OUT 6", "LOOP OFF", "HOLD ON", "CONTINUE OFF",
                        "DECK 1")
        db.commands("TRACKER ADD", "TRACKER SET 1 1 1", "TRACKER ADD", "TRACKER SET 2 1 2")
        if two_decks:
            db.send("TRACKER SET 1 2 1")
        return db.saved()


def with_step_length(saved, mode, post_wait):
    """The first step's LEN, written the way the LEN cell saves it."""
    rows = saved.splitlines()
    masters = [i for i, line in enumerate(rows) if line.startswith("cue\t-1\t")]
    assert len(masters) == 2, masters
    f = rows[masters[0]].split("\t")
    f[CUE_FIELDS["continueMode"]] = mode
    f[CUE_FIELDS["postWaitSeconds"]] = str(post_wait)
    rows[masters[0]] = "\t".join(f)
    return "\n".join(rows) + "\n"


# name, mode, post-wait, speed, timed actions, expected switch (None: must not)
CASES = [
    ("normal", "auto-follow", 0, 1, [], 4.0),
    ("half-speed", "auto-follow", 0, .5, [], 8.0),
    ("double-speed", "auto-follow", 0, 2, [], 2.0),
    ("paused", "auto-follow", 0, 1, [(1.5, "PAUSE")], None),
    ("pause-resume", "auto-follow", 0, 1, [(1.0, "PAUSE"), (3.0, "PLAY")], 6.0),
    ("speed-change", "auto-follow", 0, 1, [(1.0, "SPEED 0.5")], 7.0),
    ("post-wait", "auto-follow", 2, 1, [], 6.0),
    ("fixed-length", "auto-continue", 3, .5, [], 3.0),
]


def run_case(exe, work, template, case, late):
    name, mode, post, speed, actions, expected = case
    show = work / f"{name}.deckboy"
    show.write_text(with_step_length(template, mode, post), encoding="utf8")
    samples = []
    switched = None
    with Runtime(exe, work / f"{name}-run", show) as db:
        db.commands("SELECT 1", f"SPEED {speed}", "TRACKER LOOP OFF")
        start = time.monotonic()
        db.send("TRACKER PLAY")
        pending = list(actions)
        limit = (expected or 9.0) + 3.0
        while time.monotonic() - start < limit:
            time.sleep(.1)
            elapsed = time.monotonic() - start
            while pending and elapsed >= pending[0][0]:
                db.send(pending.pop(0)[1])
            deck = json.loads(db.send("STATUS JSON"))["decks"][0]
            samples.append((round(elapsed, 2), deck.get("active"), deck.get("status"),
                            deck.get("position")))
            if deck.get("active") == 2:
                switched = elapsed
                break
        tracker = db.send("TRACKER")
    if expected is None:
        ok = switched is None
    else:
        # Polled every 0.1 s, through a socket round trip, so a little late is
        # observation; early is the fault. Holding seconds too long is a fault
        # too (419 held a double-speed cue two seconds), so --late stays small.
        ok = switched is not None and expected - .3 <= switched <= expected + late
    return dict(name=name, expected=expected, switched=switched and round(switched, 2),
                ok=ok, tracker=tracker, last=samples[-3:])


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--late", type=float, default=.9,
                    help="seconds late a switch may be observed (slow CI runners: 1.5)")
    ap.add_argument("--keep", help="work directory to keep (default: a temp dir)")
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    work = Path(args.keep) if args.keep else Path(tempfile.mkdtemp(prefix="tracker-follow-"))
    work.mkdir(parents=True, exist_ok=True)
    motion, blue = work / "motion.mp4", work / "blue.mp4"
    make_clip(motion)
    make_clip(blue, "blue")

    results = []
    template = build_show(exe, work, motion, blue, two_decks=False)
    for case in CASES:
        results.append(run_case(exe, work, template, case, args.late))
        print(json.dumps(results[-1]), flush=True)
    # Two playlists: deck 1's cue ends at 4 s, deck 2's at 6 s. The step waits
    # for the later one.
    template = build_show(exe, work, motion, blue, two_decks=True)
    results.append(run_case(exe, work, template,
                            ("two-playlists", "auto-follow", 0, 1, [], 6.0), args.late))
    print(json.dumps(results[-1]), flush=True)

    failed = [r["name"] for r in results if not r["ok"]]
    print(("FAIL: " + ", ".join(failed)) if failed else f"PASS: {len(results)} cases")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
