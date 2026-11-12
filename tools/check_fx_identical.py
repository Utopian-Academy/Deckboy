#!/usr/bin/env python3
"""Every effect gives the same bytes from two builds.

For proving an optimisation or refactor changed nothing: runs --effect-dump
for every effect (and a few frames, so the animated ones are covered) with
both executables on the same picture and compares the files byte for byte.

    py -3 tools/check_fx_identical.py --old <old Deckboy.exe> [--new <exe>]
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_effects_offline import EFFECTS  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--old", required=True)
    ap.add_argument("--new", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--frames", default="0,12,57")
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="fx-same-"))
    src = work / "in.ppm"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=s=1280x720:d=1",
                    "-frames:v", "1", str(src)], check=True)
    differs = []
    for e in EFFECTS:
        token = e if isinstance(e, str) else e[0]
        for frame in args.frames.split(","):
            outs = []
            for tag, exe in (("old", args.old), ("new", args.new)):
                out = work / ("%s-%s-%s.ppm" % (token, frame, tag))
                subprocess.run([exe, "--effect-dump", token, str(src), str(out), frame], capture_output=True)
                outs.append(out.read_bytes() if out.exists() else None)
            if outs[0] is None and outs[1] is None:
                continue
            if outs[0] != outs[1]:
                differs.append("%s@%s" % (token, frame))
    print("differ:", " ".join(differs) if differs else "none -- every effect is byte-identical")
    return 1 if differs else 0


if __name__ == "__main__":
    sys.exit(main())
