#!/usr/bin/env python3
"""Every GPU effect draws what its CPU original draws.

engine/gpu_effects runs a stack on the graphics card only when every effect in
it has a GPU version, and the CPU version is the reference. This runs each one
both ways (--effect-dump and --gpu-effect-dump) on the same picture, at the
defaults and at three other settings, and compares:

    mean   the average difference, in levels of 255
    off    the share of pixels more than 2 levels out
    worst  the largest single difference

Float on the card against double on the CPU leaves a level here and there (a
rounding that lands the other side of .5); a wrong formula shows as a large
mean. Runs on Direct3D 11 by default; DECKBOY_OUTPUT_RENDERER=gpu runs the
SDL_GPU path (Direct3D 12 or Vulkan on Windows, Metal on a Mac).

    py -3 tools/check_gpu_effects.py [--exe build/windows/Release/Deckboy.exe]
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

EFFECTS = ["invert", "posterise", "solarise", "levels", "curves", "strobe", "scanlines",
           "threshold", "colour", "colour_replace", "vignette", "grain", "channel_offset",
           "mirror", "luma_key", "edges", "polar_warp", "luma_displace", "relativistic",
           "ripple", "kaleidoscope", "mosaic", "blur", "glow", "sharpen", "stereogram"]
# amount:a:b:c:d
SETTINGS = ["1", "0.7:0.2:0.6:0.4:0.3", "1:0.9:0.3:0.8:0.1", "0.4:0.05:1:0.2:0.9"]


def read_ppm(path):
    data = Path(path).read_bytes()
    parts = data.split(maxsplit=4)
    w, h = int(parts[1]), int(parts[2])
    return w, h, parts[4][-w * h * 3:]


def compare(a, b):
    wa, ha, pa = read_ppm(a)
    wb, hb, pb = read_ppm(b)
    if (wa, ha) != (wb, hb):
        return None
    total = 0
    off = 0
    worst = 0
    n = len(pa)
    for i in range(n):
        d = abs(pa[i] - pb[i])
        total += d
        if d > 2:
            off += 1
        if d > worst:
            worst = d
    return total / n, 100.0 * off / n, worst


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--size", default="640x360")
    ap.add_argument("--mean", type=float, default=0.6, help="largest mean difference allowed")
    ap.add_argument("--off", type=float, default=1.0, help="largest share of pixels >2 out, percent")
    ap.add_argument("--only", action="append", default=[])
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="gpu-fx-"))
    src = work / "in.ppm"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                    "testsrc2=s=%s:d=1" % args.size, "-frames:v", "1", str(src)], check=True)
    fails = []
    backend = ""
    for token in EFFECTS:
        if args.only and token not in args.only:
            continue
        for i, setting in enumerate(SETTINGS):
            spec = token + ":" + setting
            cpu, gpu = work / ("%s-%d-cpu.ppm" % (token, i)), work / ("%s-%d-gpu.ppm" % (token, i))
            subprocess.run([args.exe, "--effect-dump", spec, str(src), str(cpu), "12"], capture_output=True)
            r = subprocess.run([args.exe, "--gpu-effect-dump", spec, str(src), str(gpu), "12"],
                               capture_output=True, text=True)
            if "on " in r.stdout and not backend:
                backend = r.stdout.split("on ", 1)[1].split()[0]
            if r.returncode != 0 or not gpu.exists() or not cpu.exists():
                print("  %-15s %-22s GPU RUN FAILED (%d) %s" % (token, setting, r.returncode,
                                                               (r.stderr or "").strip()[:120]), flush=True)
                fails.append(spec)
                continue
            res = compare(cpu, gpu)
            if res is None:
                print("  %-15s %-22s SIZE MISMATCH" % (token, setting))
                fails.append(spec)
                continue
            mean, off, worst = res
            ok = mean <= args.mean and off <= args.off
            if not ok:
                fails.append(spec)
            print("  %-15s %-22s mean %5.2f  off %5.2f%%  worst %3d  %s"
                  % (token, setting, mean, off, worst, "ok" if ok else "DIFFERS"), flush=True)
    print("backend:", backend or "?")
    print("all match" if not fails else "%d DIFFER: %s" % (len(fails), " ".join(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
