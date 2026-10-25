#!/usr/bin/env python3
"""Undo puts back edits, never the live show.

Three things Undo used to do to a running show (the 24 October edges audit,
F12 and F13), each checked against the real app on a private desktop:

1. A cue waiting out its pre-wait fired the WRONG cue after an Undo that put
   a deleted cue back above it: the wait remembered a row, not the cue.
2. An Undo stopped a running recording (starting one adds an output, and the
   restored step had no such output); Redo then began a second file.
3. A deck layered onto an output after the restored step dropped off it.

    py -3 tools/check_undo_live.py --exe build/windows/Release/Deckboy.exe
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

SEED = "update_check\t0\nui_sounds\t0\nui_scale\t1.0\n"
fails = []


def note(name, ok, info=""):
    print("  %-62s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def clips(folder):
    out = []
    for colour, freq in (("red", 440), ("lime", 880), ("blue", 1320)):
        path = folder / f"{colour}.mp4"
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i", f"color=c={colour}:s=640x360:r=25:d=24",
                        "-f", "lavfi", "-i", f"sine=f={freq}:r=48000:d=24", "-c:v", "libx264",
                        "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", str(path)], check=True)
        out.append(path)
    return out


def show(work, name):
    folder = work / name
    folder.mkdir(parents=True)
    path = folder / "show.deckboy"
    path.write_text(SEED, encoding="utf8")
    return folder, path


def state(db):
    return json.loads(db.send("STATUS JSON"))


def pending_keeps_its_cue(exe, work, media):
    folder, path = show(work, "pending")
    extra = tuple(x for p in media for x in ("--import", str(p)))
    with Runtime(exe, folder / "run", path, extra=extra) as db:
        db.commands("SELECT 1", "LOOP ON", "TAKE", "OUTPUT ON", "VIDEO 640x360", "SELECT 2")
        db.send("DELETE")                       # lime goes; blue becomes row 2
        db.commands("SELECT 2", "PREWAIT 2.5")
        db.send("TAKE")                         # blue waits 2.5 s
        undo = db.send("UNDO")                  # lime comes back above it
        time.sleep(3.2)
        deck = state(db)["decks"][0]
    note("Undo during a pre-wait is accepted", undo.startswith("OK"), undo)
    note("...and the cue asked for (blue) is what goes live", deck.get("cue") == "blue", deck.get("cue"))


def recording_survives_undo(exe, work, media):
    folder, path = show(work, "recording")
    with Runtime(exe, folder / "run", path, extra=("--import", str(media[0]))) as db:
        db.commands("SELECT 1", "LOOP ON", "TAKE", "OUTPUT ON", "VIDEO 640x360", "RECFORMAT 640x360 25")
        db.send("PATTERN ADD full-white")
        db.commands("SELECT 1", "SELECT 2")
        deleted = db.send("DELETE")             # the edit Undo will restore
        db.send("RECORD START")
        time.sleep(2)
        before = state(db)["outputCount"]
        undo = db.send("UNDO")
        time.sleep(1.5)
        after = state(db)["outputCount"]
        redo = db.send("REDO")
        time.sleep(1.5)
        redone = state(db)["outputCount"]
        db.send("RECORD STOP")
        time.sleep(1.0)
    files = sorted((path.parent / "recordings").glob("*.mp4"))
    note("the setup edit and Undo/Redo are accepted",
         deleted.startswith("OK") and undo.startswith("OK") and redo.startswith("OK"), (deleted, undo, redo))
    note("Undo and Redo leave the recording output in place", before == after == redone, (before, after, redone))
    note("...and the take is one file, not two", len(files) == 1, [f.name for f in files])
    if len(files) == 1:
        seconds = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration",
                                        "-of", "csv=p=0", str(files[0])], capture_output=True,
                                       text=True).stdout.strip() or 0)
        note("...covering the whole take (about 5 s)", seconds >= 4.5, f"{seconds:.2f} s")


def brightness(db, path):
    """Mean brightness (0-255) of what output 1 is showing."""
    from PIL import Image, ImageStat
    db.snapshot(path, output=True)
    return ImageStat.Stat(Image.open(path).convert("L")).mean[0]


def layer_survives_undo(exe, work):
    folder, path = show(work, "routing")
    with Runtime(exe, folder / "run", path) as db:
        db.commands("PATTERN ADD full-white", "TAKE", "DECKADD", "DECK 2", "PATTERN ADD full-black", "TAKE")
        db.send("PATTERN ADD smpte-bars")
        db.commands("SELECT 1", "SELECT 2")
        db.send("DELETE")                       # the edit Undo will restore
        # Black from deck 2 layered over deck 1's white, after that edit.
        db.commands("OUTPUT SELECT 1", "VIDEO OUTPUT ASSIGN", "OUTPUT ON", "VIDEO 640x360")
        time.sleep(0.8)
        before = brightness(db, folder / "before.bmp")
        undo = db.send("UNDO")
        time.sleep(0.8)
        after = brightness(db, folder / "after.bmp")
    note("the layer (black over white) is on the output to begin with", before < 20, f"{before:.0f}")
    note("...and Undo leaves it there", undo.startswith("OK") and after < 20, (undo, f"{after:.0f}"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    work = Path(tempfile.mkdtemp(prefix="deckboy-undo-live-"))
    media = clips(work)
    print("check: Undo puts back edits, not the live show\n")
    pending_keeps_its_cue(exe, work, media)
    recording_survives_undo(exe, work, media)
    layer_survives_undo(exe, work)
    print()
    print("%d FAILED" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
