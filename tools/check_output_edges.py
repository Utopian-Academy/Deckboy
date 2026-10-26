#!/usr/bin/env python3
"""The edges of the live show: outputs, selection and transitions under stress.

Each case is a fault the 24 October edges audit found in 0.99.419, run against
the real app on a private desktop:

- F11: taking a third cue during a dissolve snapped to the second before
  fading to the third.
- F14: removing one output restarted the recording on another.
- F15: STOP during a dissolve cleared the incoming cue but left the outgoing
  picture fading out on its own.
- F16: adding a cue left the previous one selected too, so the next LOOP or
  DELETE hit both.

    py -3 tools/check_output_edges.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import json
import re
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
fails = []


def note(name, ok, info=""):
    print("  %-62s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def show(work, name):
    folder = work / name
    folder.mkdir(parents=True)
    path = folder / "show.deckboy"
    path.write_text(SEED, encoding="utf8")
    return folder, path


def state(db):
    return json.loads(db.send("STATUS JSON"))


def settled_recordings(folder):
    last, steady = None, 0
    for _ in range(60):
        files = sorted((folder / "recordings").glob("*.mp4"))
        sizes = [(f.name, f.stat().st_size) for f in files]
        steady = steady + 1 if sizes == last and sizes else 0
        if steady >= 4:
            return files
        last = sizes
        time.sleep(0.25)
    return sorted((folder / "recordings").glob("*.mp4"))


def remove_output_keeps_recording(exe, work):
    folder, path = show(work, "output-remove")
    # The settings modal open on Video Outputs. REMOVE OUTPUT is found by its
    # label: a fixed coordinate broke the day a section moved above it.
    with Runtime(exe, folder / "run", path, extra=("--settings", "3.0")) as db:
        db.commands("PATTERN ADD full-white", "TAKE", "OUTPUT ON", "VIDEO 640x360",
                    "OUTPUT ADD 1", "OUTPUT ON", "VIDEO 640x360",
                    "RECFORMAT 640x360 25", "RECORD START")
        db.send("OUTPUT SELECT 2")
        time.sleep(1.5)
        before = state(db)
        recorder = before["outputs"][-1]["outputId"]
        found = re.search(r"at (\d+),(\d+) (\d+)x(\d+)", db.send("PRESS REMOVE OUTPUT"))
        db.send("PRESS OFF")
        if found:                              # REMOVE OUTPUT, on output 2
            x, y, w, h = map(int, found.groups())
            db.send(f"CLICK {x + w // 2} {y + h // 2}")
        time.sleep(2.5)
        after = state(db)
        db.send("RECORD STOP")
        time.sleep(0.5)
    files = settled_recordings(path.parent)
    ids = [o["outputId"] for o in after["outputs"]]
    note("REMOVE OUTPUT took output 2 away (3 -> 2)", len(before["outputs"]) == 3 and len(ids) == 2,
         (len(before["outputs"]), len(ids)))
    note("...the recording output is still there", recorder in ids, ids)
    note("...and the take is one file, not two", len(files) == 1, [f.name for f in files])


def insert_selects_only_the_new_cue(exe, work):
    folder, path = show(work, "selection")
    clip = work / "red.mp4"
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i", "color=c=red:s=640x360:r=25:d=8",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", str(clip)], check=True)
    with Runtime(exe, folder / "run", path, extra=("--import", str(clip))) as db:
        db.commands("SELECT 1", "LOOP OFF", "TAKE")
        db.send("PATTERN ADD full-white")
        db.send("SELECT 2")
        db.send("LOOP ON")                      # meant for the new cue only
        saved = db.saved()
    cues = [line.split("\t") for line in saved.splitlines() if line.startswith("cue\t0\t")]
    loops = [c[CUE_FIELDS["loop"]] for c in cues]
    note("two cues on the playlist", len(cues) == 2, len(cues))
    note("LOOP after adding a cue changes only the new cue", loops == ["0", "1"], loops)


def mean_rgb(path):
    """Mean R, G, B of an image, measured by ffmpeg (no Pillow on CI)."""
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-vf", "scale=1:1:flags=area",
                          "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], capture_output=True).stdout
    return list(raw[:3]) if len(raw) >= 3 else [255, 255, 255]


def stop_mid_dissolve_goes_black(exe, work):
    folder, path = show(work, "stop-dissolve")
    clips = []
    for colour in ("red", "lime"):
        clip = work / f"{colour}-24.mp4"
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i", f"color=c={colour}:s=640x360:r=25:d=24",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", str(clip)], check=True)
        clips += ["--import", str(clip)]
    with Runtime(exe, folder / "run", path, extra=tuple(clips)) as db:
        db.commands("SELECT 1", "LOOP ON", "TAKE", "OUTPUT ON", "VIDEO 640x360")
        time.sleep(0.8)
        db.commands("TRANSITION 4", "TAKE 2")
        time.sleep(1.0)                         # a quarter of the way through
        db.send("STOP")
        time.sleep(0.7)
        db.snapshot(folder / "stopped.bmp", output=True)
        level = mean_rgb(folder / "stopped.bmp")
    note("STOP during a dissolve leaves the output black", max(level) < 10,
         [round(v) for v in level])


def interrupted_dissolve_is_smooth(exe, work):
    folder, path = show(work, "interrupt")
    clips = []
    for colour in ("red", "lime", "blue"):
        clip = work / f"{colour}-i.mp4"
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i", f"color=c={colour}:s=640x360:r=25:d=24",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", str(clip)], check=True)
        clips += ["--import", str(clip)]
    with Runtime(exe, folder / "run", path, extra=tuple(clips)) as db:
        db.commands("SELECT 1", "LOOP ON", "TAKE", "OUTPUT ON", "VIDEO 640x360", "RECFORMAT 640x360 25")
        db.send("RECORD START")
        time.sleep(0.5)
        db.commands("TRANSITION 4", "TAKE 2")
        time.sleep(1.0)                         # red to green, a quarter done
        db.send("TAKE 3")                       # now to blue
        time.sleep(4.5)
        db.send("RECORD STOP")
        time.sleep(0.5)
    files = settled_recordings(path.parent)
    if len(files) != 1:
        note("the interrupted dissolve was recorded", False, [f.name for f in files])
        return
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(files[0]), "-vf", "scale=1:1:flags=area",
                          "-f", "rawvideo", "-pix_fmt", "rgb24", "-"], capture_output=True).stdout
    frames = [raw[i:i + 3] for i in range(0, len(raw) - 2, 3)]
    jumps = [sum(abs(a - b) for a, b in zip(frames[i], frames[i + 1])) for i in range(len(frames) - 1)]
    worst = max(jumps) if jumps else 0
    # One frame of a 4 s dissolve moves about 16; the old snap moved 365.
    note("taking a third cue mid-dissolve has no jump", 0 < len(jumps) and worst < 60, f"worst step {worst}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    work = Path(tempfile.mkdtemp(prefix="deckboy-output-edges-"))
    print("check: outputs, selection and transitions at the edges\n")
    interrupted_dissolve_is_smooth(exe, work)
    remove_output_keeps_recording(exe, work)
    insert_selects_only_the_new_cue(exe, work)
    stop_mid_dissolve_goes_black(exe, work)
    print()
    print("%d FAILED" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
