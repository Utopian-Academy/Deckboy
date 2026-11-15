"""Datamosh: every hard cut gets a keyframe in the moshable copy.

Playback withholds keyframes, so a cut smears only where the copy has one. The
encoders' own scene detection used to keep one at some cuts and not others (6
of the 11 cuts in the Datamosh lesson's reel smeared). Deckboy now finds the
cuts first (findHardCuts) and forces a keyframe on each.

By default it uses the Datamosh lesson's own reel (data/lessons/mosh_reel.mp4,
eleven cuts on the bars of an 84 BPM theme): a clip made with ffmpeg's lavfi
sources had cuts the encoder keyed on its own, so it passed even before the
fix. It turns DATAMOSH on in the real app, waits for the moshable copy, and
checks that the copy has a keyframe on every cut.

  py -3 tools/check_datamosh_cuts.py [--exe build/windows/Release/Deckboy.exe]
  py -3 tools/check_datamosh_cuts.py --clip reel.mp4 --cuts 2.857,5.714,...
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from example_show_runtime import Runtime  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def keyframes(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                          "packet=pts_time,flags", "-of", "csv=p=0", str(path)],
                         capture_output=True, text=True).stdout
    times = []
    for line in out.splitlines():
        parts = line.split(",")
        if len(parts) >= 2 and "K" in parts[1]:
            try:
                times.append(float(parts[0]))
            except ValueError:
                pass
    return times


def copy_cuts(path):
    """The copy's own hard cuts, the way Deckboy finds them (findHardCuts)."""
    out = subprocess.run(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-i", str(path),
                          "-an", "-vf", "scale=160:-2,select=gt(scene\,0.20),metadata=print:file=-",
                          "-f", "null", "-"], capture_output=True, text=True).stdout
    found = []
    for line in out.splitlines():
        if line.startswith("frame:") and "pts_time:" in line:
            found.append(float(line.split("pts_time:")[1].split()[0]))
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--clip", help="a clip of your own (needs --cuts)")
    ap.add_argument("--cuts", help="its cut times in seconds, comma-separated")
    ap.add_argument("--keep", help="copy the moshable copy here")
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="deckboy-mosh-cuts-"))
    if args.clip:
        clip = Path(args.clip).resolve()
        cuts = [float(t) for t in args.cuts.split(",")]
    else:
        clip = ROOT / "data" / "lessons" / "mosh_reel.mp4"
        cuts = [k * 4 * 60.0 / 84.0 for k in range(1, 12)]   # one cut per bar, 84 BPM
    show = work / "show.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\n", encoding="utf-8")
    env = {"SDL_AUDIO_DRIVER": "dummy", "DECKBOY_HIDE_WINDOWS": "1"}
    copy = None
    with Runtime(args.exe, work / "run", show, extra=("--import", str(clip)), env=env) as db:
        time.sleep(3)
        db.send("SELECT 1")
        db.send("DATAMOSH")
        deadline = time.monotonic() + 240
        size = -1
        while time.monotonic() < deadline:
            time.sleep(2)
            found = sorted(work.rglob("*_mosh.*"), key=os.path.getmtime)
            if found:
                now = found[-1].stat().st_size
                if now == size and now > 1024:
                    copy = found[-1]
                    break
                size = now
    failures = 0
    if copy is None:
        print("FAIL no moshable copy appeared in 240 s")
        return 1
    keys = keyframes(copy)
    # Where each cut sits IN THE COPY. The copy's timestamps can run a frame
    # behind the source's (measured: +1 frame on the lesson reels, before and
    # after this check existed), so a cut is looked for in the copy itself,
    # just after its time in the source, and must have a keyframe ON it.
    seen = copy_cuts(copy)
    frame = 1.0 / 24.0
    for t in cuts:
        here = [c for c in seen if t - 0.5 * frame <= c <= t + 2.5 * frame]
        if not here:
            failures += 1
            print("FAIL cut at %6.3f s: not found in the copy" % t)
            continue
        keyed = [k for k in keys if abs(k - here[0]) < 0.5 * frame]
        failures += 0 if keyed else 1
        print("%-4s cut at %6.3f s (copy %.3f s): %s" % ("ok" if keyed else "FAIL", t, here[0],
              "keyframe on it" if keyed else "NO keyframe on it: this cut will not smear"))
    print("copy: %s, %d keyframes" % (copy.name, len(keys)))
    if args.keep:
        Path(args.keep).write_bytes(copy.read_bytes())
    print("datamosh cuts: %s" % ("ok" if failures == 0 else "%d cut(s) without a keyframe" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
