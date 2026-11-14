#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
"""RENDER TO FILE draws frame by frame, and draws the same frames every time.

Runs the app on a private desktop (silent, nothing on the operator's screen),
puts a short generated cue and a short video file on a playlist, and renders
with the remote verb RENDER START. For each case it checks:

  - the frame count is exact (seconds x frames a second, no more, no fewer)
  - rendering the same cue twice gives files whose DECODED FRAMES are identical
    (ffmpeg -f framemd5), and the frames really change over time
  - ProRes 4444 keeps its alpha (pix_fmt yuva...), H.264 is yuv420p, PNG frames
    number as many as the plan said
  - a cue range renders as one file of the summed length
  - a live input is refused, with a reason, and no file is made

    py -3 tools/check_render_to_file.py [--exe ...] [--work ...]
"""
import argparse
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

failures = 0


def check(what, ok, detail=""):
    global failures
    if not ok:
        failures += 1
    print("%s  %s%s" % ("ok  " if ok else "FAIL", what, ("  (%s)" % detail) if detail and not ok else ""))
    return ok


def frame_hashes(path):
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", str(path), "-f", "framemd5", "-"],
                         capture_output=True, text=True).stdout
    return [ln.split(",")[-1].strip() for ln in out.splitlines() if ln and not ln.startswith("#")]


def probe(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0", "-show_entries",
                          "stream=pix_fmt,nb_read_frames,width,height,r_frame_rate,codec_name", "-of", "json", str(path)],
                         capture_output=True, text=True).stdout
    return json.loads(out)["streams"][0]


def render(db, spec, path, timeout=180):
    """RENDER START ...; wait for the job; return the final status line."""
    db.send("RENDER START %s %s" % (spec, path))
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = db.send("RENDER STATUS")
        if "running" not in status:
            return status
        time.sleep(0.25)
    raise TimeoutError("render did not finish: " + status)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--work", default=str(ROOT / "build/render-to-file"))
    args = ap.parse_args()
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    from example_show_runtime import Runtime  # noqa: E402

    # A two-second video file with known content to be the "file-backed" case.
    clip = work / "clip.mp4"
    subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30:duration=2",
                    "-pix_fmt", "yuv420p", str(clip)], check=True)

    show = work / "show.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\nui_scale\t1.0\n", encoding="utf-8")
    with Runtime(args.exe, work / "run", show, extra=("--import", str(clip))) as db:
        time.sleep(4)   # the probe
        db.send("MASTERVOL 0")
        db.send("DECK 1")
        db.send("PATTERN ADD moire-field")
        db.send("SELECT 2")
        db.send("STILLDUR 2")
        db.send("PATTERN ADD lumia")
        db.send("SELECT 3")
        db.send("STILLDUR 1")

        # 1. a generated source, 2 s at 640x360... the 1080p size is the smallest the dialog offers, so
        #    the check sizes the OUTPUT instead and asks for that.
        db.send("VIDEO 640X360")
        db.send("SELECT 2")
        files = []
        for n in (1, 2):
            f = work / ("moire-%d.mov" % n)
            f.unlink(missing_ok=True)
            status = render(db, "CUE OUTPUT 30 PRORES", f)
            check("moire render %d finished" % n, "done" in status and "saved" in status, status)
            files.append(f)
        a, b = (frame_hashes(f) for f in files)
        info = probe(files[0])
        check("moire: 60 frames exactly (2 s at 30)", len(a) == 60 and info.get("nb_read_frames") == "60",
              "%d frames, probe says %s" % (len(a), info.get("nb_read_frames")))
        check("moire: size is the output's 640x360", (info["width"], info["height"]) == (640, 360),
              "%sx%s" % (info["width"], info["height"]))
        check("moire: two renders decode to identical frames", a == b and len(a) > 0)
        check("moire: the frames change over time", len(set(a)) > 30, "%d distinct of %d" % (len(set(a)), len(a)))
        check("moire: ProRes 4444 keeps alpha", info.get("pix_fmt", "").startswith("yuva"), info.get("pix_fmt"))
        check("moire: the two files are byte for byte the same",
              hashlib.sha256(files[0].read_bytes()).digest() == hashlib.sha256(files[1].read_bytes()).digest())

        # 2. Lumia keeps a trail from frame to frame: the state must start clean each render.
        db.send("SELECT 3")
        lum = []
        for n in (1, 2):
            f = work / ("lumia-%d.mov" % n)
            f.unlink(missing_ok=True)
            status = render(db, "CUE OUTPUT 30 PRORES", f)
            check("lumia render %d finished" % n, "done" in status and "saved" in status, status)
            lum.append(f)
        a, b = (frame_hashes(f) for f in lum)
        check("lumia: 30 frames, identical across two renders", len(a) == 30 and a == b, "%d vs %d" % (len(a), len(b)))

        # 3. a file-backed video, and a range of three cues (2 s + 1 s + 2 s at 25).
        db.send("SELECT 1")
        vids = []
        for n in (1, 2):
            f = work / ("clip-%d.mov" % n)
            f.unlink(missing_ok=True)
            status = render(db, "CUE OUTPUT 25 PRORES", f)
            check("clip render %d finished" % n, "done" in status and "saved" in status, status)
            vids.append(f)
        a, b = (frame_hashes(f) for f in vids)
        check("clip: 50 frames (2 s at 25), identical across two renders", len(a) == 50 and a == b,
              "%d vs %d" % (len(a), len(b)))
        check("clip: the picture is not blank", len(set(a)) > 10, "%d distinct" % len(set(a)))
        rng = work / "range.mov"
        rng.unlink(missing_ok=True)
        status = render(db, "CUES 1-3 OUTPUT 25 PRORES", rng)
        n = len(frame_hashes(rng))
        check("range of three cues: 50 + 50 + 25 = 125 frames", n == 125, "%d (%s)" % (n, status))

        # 4. the other containers
        h264 = work / "moire.mp4"
        h264.unlink(missing_ok=True)
        db.send("SELECT 2")
        render(db, "CUE OUTPUT 30 H264", h264)
        info = probe(h264) if h264.exists() else {}
        check("H.264: 60 frames, yuv420p", info.get("nb_read_frames") == "60" and info.get("pix_fmt") == "yuv420p",
              str(info))
        pngs = work / "moire-png"
        for old in pngs.glob("*.png"):
            old.unlink()
        render(db, "CUE OUTPUT 30 PNG", pngs)
        count = len(list(pngs.glob("*.png")))
        check("PNG frames: 60 files", count == 60, str(count))

        # 5. a refusal: a Timer has no frame to step, so a range that holds one is refused with a reason.
        db.send("ADDTIMER 60")
        refused = ""
        nope = work / "nope.mov"
        nope.unlink(missing_ok=True)
        try:
            db.send("RENDER START CUES 1-4 OUTPUT 30 PRORES " + str(nope))
        except RuntimeError as err:
            refused = str(err)
        check("a live cue is refused, naming the cue and the reason", "Cue 4" in refused and "cannot be drawn" in refused, refused)
        check("and no file is made", not nope.exists())
        try:
            db.send("RENDER START CUES 1-9 OUTPUT 30 PRORES " + str(nope))
            check("a range past the end is refused", False)
        except RuntimeError as err:
            check("a range past the end is refused", "has 4 cues" in str(err), str(err))

        # 5b. a long one, watched while it runs, then cancelled: the partial file goes away.
        db.send("SELECT 2")
        big = work / "big.mov"
        big.unlink(missing_ok=True)
        db.send("RENDER START CUE 4K 60 PRORES " + str(big))
        db.send("RENDER DIALOG")
        time.sleep(2.5)
        mid = db.send("RENDER STATUS")
        check("a 4K render reports its frames while it runs", "running frame" in mid, mid)
        db.snapshot(work / "dialog-running.bmp")
        db.send("RENDER CANCEL")
        time.sleep(2)
        after = db.send("RENDER STATUS")
        check("cancel stops it and removes the partial file", "cancelled" in after and not big.exists(), after)
        db.send("RENDER CLOSE")

        # 6. the dialog, for the screenshot
        db.send("RENDER DIALOG")
        time.sleep(0.6)
        shot = db.snapshot(work / "dialog.bmp")
        check("the dialog draws (snapshot written)", shot.exists() and shot.stat().st_size > 1000)
    print("render to file: %s" % ("FAIL (%d)" % failures if failures else "all ok"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
