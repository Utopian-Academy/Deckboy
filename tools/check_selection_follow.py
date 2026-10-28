#!/usr/bin/env python3
"""Playback moves the selection to ONE cue, and an off output reports no rate.

ENDACTION, too: it changes only video and audio cues, and says so for anything
else, where it used to answer OK and change nothing.

When a cue ends and the deck moves to the next one, the next cue is selected.
It used to be selected ALONGSIDE the one that ended: the inspector read
"2 cues", and the next gain change landed on the cue that had just finished.
Every path that moves the selection for the operator is walked here, and the
edit selection (STATUS JSON `editSelection`) must be that one cue each time.

And an output that is turned off kept reporting its last measured rate, so the
programme monitor read OUTPUT 60.0 with nothing on screen.

    py -3 tools/check_selection_follow.py --exe build/windows/Release/Deckboy.exe
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

fails = []


def note(name, ok, info=""):
    print("  %-62s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def deck(db):
    return json.loads(db.send("STATUS JSON"))["decks"][0]


def wait_for(db, predicate, seconds):
    deadline = time.monotonic() + seconds
    while True:
        d = deck(db)
        if predicate(d) or time.monotonic() > deadline:
            return d
        time.sleep(0.2)


def clips(folder):
    out = []
    for i, colour in enumerate(("red", "lime", "blue", "white")):
        path = folder / f"{colour}.mp4"
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i", f"color=c={colour}:s=320x180:r=25:d=1.5",
                        "-f", "lavfi", "-i", f"sine=f={440 * (i + 1)}:r=48000:d=1.5", "-c:v", "libx264",
                        "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", str(path)], check=True)
        out.append(path)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="deckboy-selection-"))
    show = work / "show.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\nui_scale\t1.0\n", encoding="utf8")
    print("check: selection follows playback to one cue\n")
    media = clips(work)
    extra = tuple(x for p in media for x in ("--import", str(p)))
    with Runtime(Path(args.exe).resolve(), work / "run", show, extra) as db:
        deadline = time.monotonic() + 20
        while True:                       # the imports land one by one
            try:
                db.send(f"SELECT {len(media)}")
                break
            except RuntimeError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.3)
        db.send("SELECT 1")
        db.send("ENDACTION NEXT")
        d = deck(db)
        note("SELECT 1 edits cue 1 alone", d["editSelection"] == [1], d["editSelection"])

        db.send("TAKE")
        d = wait_for(db, lambda d: d["active"] == 2, 6)
        note("cue 1 ends and cue 2 goes live", d["active"] == 2, d["active"])
        note("...and the edit selection is cue 2 alone", d["editSelection"] == [2], d["editSelection"])

        db.send("SKIP")
        d = wait_for(db, lambda d: d["active"] == 3, 4)
        note("SKIP: the edit selection is the cue it took", d["editSelection"] == [3], d["editSelection"])

        db.send("PREV")
        d = wait_for(db, lambda d: d["active"] == 2, 4)
        note("PREV: the edit selection is the cue it took", d["editSelection"] == [2], d["editSelection"])

        def refused(command):
            try:
                db.send(command)
            except RuntimeError as reply:
                return str(reply)
            return ""

        why = refused("ENDACTION SOMETIMES")
        note("ENDACTION refuses a word it does not know", "expected" in why, why or "answered OK")
        db.send("PATTERN ADD checkerboard")
        db.send(f"SELECT {len(media) + 1}")
        why = refused("ENDACTION NEXT")
        note("...and a cue that has no end action", "only video and audio" in why, why or "answered OK")

        db.send("STOP")
        db.send("OUTPUT ON")
        status = {}
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            status = json.loads(db.send("STATUS JSON"))
            if status["outputs"][0].get("outputFps", 0) > 1:
                break
            time.sleep(0.3)
        on_fps = status["outputs"][0].get("outputFps", 0)
        note("an output that is on reports a rate", on_fps > 1, on_fps)
        db.send("OUTPUT OFF")
        time.sleep(0.5)
        off_fps = json.loads(db.send("STATUS JSON"))["outputs"][0].get("outputFps", -1)
        note("...and once off, reports none", off_fps == 0, off_fps)

    print()
    if fails:
        print("FAILED: " + ", ".join(fails))
        return 1
    print("all ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
