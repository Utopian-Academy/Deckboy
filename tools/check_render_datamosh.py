#!/usr/bin/env python3
"""Render to file draws DATAMOSH and Direction, as the deck plays them.

Before 0.99.428 a render read the cue's original file forwards whatever the
cue was set to. This renders the Datamosh lesson's reel three ways -- plain,
with DATAMOSH, and reversed -- and checks:
  - plain and moshed match before the first cut and differ after it (the smear)
  - the reversed render's first frame is the plain render's last
Every window hidden, sound to the dummy driver.

    py -3 tools/check_render_datamosh.py [--exe build/windows/Release/Deckboy.exe]
"""
import argparse
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

REEL = ROOT / "data" / "lessons" / "mosh_reel.mp4"
FIRST_CUT = 4 * 60.0 / 84.0    # one bar at 84 BPM


def frame_at(path, seconds, out):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", "%.3f" % seconds, "-i", str(path), "-frames:v", "1",
                    "-vf", "scale=160:-2,format=gray", "-f", "rawvideo", str(out)], check=True)
    return Path(out).read_bytes()


def last_frame(path, out):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-sseof", "-0.2", "-i", str(path), "-update", "1",
                    "-vf", "scale=160:-2,format=gray", "-f", "rawvideo", str(out)], check=True)
    return Path(out).read_bytes()


def diff(a, b):
    n = min(len(a), len(b))
    return sum(abs(a[i] - b[i]) for i in range(n)) / max(1, n)


def render(db, out, seconds=60):
    db.send("RENDER START CUE 1080P 25 H264 %s" % out)
    deadline = time.monotonic() + seconds * 4
    while time.monotonic() < deadline:
        reply = db.send("RENDER STATUS")
        m = re.search(r'done frames=(\d+) of (\d+) result="([^"]*)"', reply)
        if m:
            if m.group(1) != m.group(2) or not Path(out).exists():
                print("  render:", reply[:240])
                return False
            return True
        time.sleep(1.0)
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="deckboy-render-mosh-"))
    show = work / "show.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\n", encoding="utf-8")
    failures = []

    def expect(ok, what, detail=""):
        print(("ok   " if ok else "FAIL ") + what + ("" if ok or not detail else "  (" + detail + ")"))
        if not ok:
            failures.append(what)

    env = {"SDL_AUDIO_DRIVER": "dummy", "DECKBOY_HIDE_WINDOWS": "1"}
    plain, moshed, back = work / "plain.mp4", work / "moshed.mp4", work / "reversed.mp4"
    with Runtime(args.exe, work / "run", show, extra=("--import", str(REEL)), env=env) as db:
        time.sleep(3)
        db.send("SELECT 1")
        expect(render(db, plain), "the plain render finishes")
        db.send("DATAMOSH")
        deadline = time.monotonic() + 240
        while time.monotonic() < deadline and not list(work.rglob("*_mosh.*")):
            time.sleep(2)
        time.sleep(3)
        expect(render(db, moshed), "the DATAMOSH render finishes")
        db.send("DATAMOSH")   # off again
        db.send("DIRECTION REVERSE")
        deadline = time.monotonic() + 240
        while time.monotonic() < deadline:
            if "copy=ready" in db.send("DIRECTION STATUS") or "ready" in db.send("DIRECTION STATUS"):
                break
            time.sleep(2)
        expect(render(db, back), "the reversed render finishes")
    if failures:
        print("render datamosh: %d failure(s)" % len(failures))
        return 1
    before = diff(frame_at(plain, FIRST_CUT - 0.5, work / "a"), frame_at(moshed, FIRST_CUT - 0.5, work / "b"))
    after = diff(frame_at(plain, FIRST_CUT + 0.6, work / "c"), frame_at(moshed, FIRST_CUT + 0.6, work / "d"))
    expect(before < 12.0, "before the first cut, moshed matches plain (mean grey diff %.1f)" % before)
    expect(after > 2 * before + 8.0, "after the first cut, moshed smears (mean grey diff %.1f)" % after)
    turned = diff(frame_at(back, 0.05, work / "e"), last_frame(plain, work / "f"))
    expect(turned < 12.0, "the reversed render starts on the plain one's last frame (mean grey diff %.1f)" % turned)
    print("render datamosh: %s" % ("ok" if not failures else "%d failure(s)" % len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
