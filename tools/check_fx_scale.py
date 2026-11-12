#!/usr/bin/env python3
"""Does each effect look the same run small as run full size?

A layer's effects run on a copy reduced to about the size the output draws
it (layerFxShrinkFor). That is invisible only for an effect that sizes itself
from the frame it is given. This runs every effect two ways on one picture,
headless (--effect-dump), and compares:

    full   the effect at 1920x1080, then reduced to 960x540
    small  the picture reduced to 960x540, then the effect, told it is a
           half-size copy (DECKBOY_FX_PIXEL_SCALE=0.5, ctx.pixelScale)

An effect whose constants are in pixels (a 24-pixel ripple) differs; one
sized from the frame does not. Pitch-in-output-pixel effects (scanlines,
grain) are expected to differ and are listed as such.

    py -3 tools/check_fx_scale.py [--exe build/windows/Release/Deckboy.exe]
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from check_effects_offline import EFFECTS  # noqa: E402

PITCHED = {"scanlines", "grain", "temporal_dither"}


def read_ppm(path):
    data = Path(path).read_bytes()
    parts = data.split(maxsplit=4)
    w, h = int(parts[1]), int(parts[2])
    return w, h, parts[4][-w * h * 3:]


def shrink(src, dst, w, h):
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(src), "-vf",
                    "scale=%d:%d:flags=area" % (w, h), str(dst)], check=True)


def diff(a, b):
    wa, ha, pa = read_ppm(a)
    wb, hb, pb = read_ppm(b)
    assert (wa, ha) == (wb, hb)
    n = len(pa)
    total = sum(abs(pa[i] - pb[i]) for i in range(0, n, 7))
    return total / len(range(0, n, 7))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--limit", type=float, default=4.0, help="mean level difference allowed")
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="fx-scale-"))
    full = work / "full.ppm"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                    "testsrc2=s=1920x1080:d=1", "-frames:v", "1", str(full)], check=True)
    small = work / "small.ppm"
    shrink(full, small, 960, 540)
    worse = []
    for e in EFFECTS:
        token = e if isinstance(e, str) else e[0]
        a, a2, b = work / ("%s-a.ppm" % token), work / ("%s-a2.ppm" % token), work / ("%s-b.ppm" % token)
        r1 = subprocess.run([args.exe, "--effect-dump", token, str(full), str(a), "12"], capture_output=True)
        r2 = subprocess.run([args.exe, "--effect-dump", token, str(small), str(b), "12"], capture_output=True,
                            env=dict(os.environ, DECKBOY_FX_PIXEL_SCALE="0.5"))
        if r1.returncode or r2.returncode or not a.exists() or not b.exists():
            print("  %-16s n/a" % token)
            continue
        shrink(a, a2, 960, 540)
        d = diff(a2, b)
        tag = "pitched (expected)" if token in PITCHED else ("DIFFERS" if d > args.limit else "same")
        if tag == "DIFFERS":
            worse.append(token)
        print("  %-16s mean diff %6.2f  %s" % (token, d, tag), flush=True)
    print("resolution-dependent:", " ".join(worse) if worse else "none")
    return 1 if worse else 0


if __name__ == "__main__":
    sys.exit(main())
