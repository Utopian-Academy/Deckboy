#!/usr/bin/env python3
"""Portal, shatter, clouds and ghastly draw on the GPU what they draw on the CPU.

The four transitions that build the outgoing picture pixel by pixel run their
per-pixel pass on the graphics card (engine/gpu_effects, transMain) when the
output can; the small per-frame inputs come from the same code the CPU builders
run (MediaEngine::planGpuTransition), and the CPU builders are the reference.
This runs each style both ways (--transition-dump and --gpu-transition-dump) on
the same pictures at several points of the transition, and compares the
composites:

    mean   the average difference, in levels of 255
    off    the share of pixels more than 2 levels out
    worst  the largest single difference

Float on the card against float and double on the CPU leaves a level here and
there, and a shard's sample can round to its neighbour pixel; a wrong formula
shows as a large mean. Runs on Direct3D 11 by default; DECKBOY_OUTPUT_RENDERER=gpu
runs the SDL_GPU path (Direct3D 12 or Vulkan on Windows, Metal on a Mac).

    py -3 tools/check_gpu_transitions.py [--exe build/windows/Release/Deckboy.exe]
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STYLES = ["portal", "shatter", "clouds", "ghastly"]
PROGRESS = [0.0, 0.2, 0.45, 0.7, 0.9, 1.0]


def write_ppm(path, w, h, seed):
    """A picture with fine detail, gradients and hard edges, so a sample one
    pixel off shows."""
    rnd = random.Random(seed)
    body = bytearray(w * h * 3)
    i = 0
    for y in range(h):
        for x in range(w):
            body[i] = (x * 255 // max(1, w - 1)) ^ rnd.randrange(32)
            body[i + 1] = y * 255 // max(1, h - 1)
            body[i + 2] = ((x // 16 + y // 16) & 1) * 200 + rnd.randrange(40)
            i += 3
    Path(path).write_bytes(b"P6\n%d %d\n255\n" % (w, h) + bytes(body))


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
    total = off = worst = 0
    for i in range(len(pa)):
        d = abs(pa[i] - pb[i])
        total += d
        if d > 2:
            off += 1
        if d > worst:
            worst = d
    return total / len(pa), 100.0 * off / len(pa), worst


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--size", action="append", default=[],
                    help="WxH; repeat for several (default 640x360 and 360x640)")
    ap.add_argument("--mean", type=float, default=0.5, help="largest mean difference allowed")
    ap.add_argument("--off", type=float, default=0.5, help="largest share of pixels >2 out, percent")
    ap.add_argument("--only", action="append", default=[])
    args = ap.parse_args()
    sizes = [tuple(int(v) for v in s.lower().split("x")) for s in (args.size or ["640x360", "360x640"])]
    styles = args.only or STYLES
    failures = 0
    backend = None
    with tempfile.TemporaryDirectory(prefix="deckboy-gpu-transitions-") as work:
        for w, h in sizes:
            out = os.path.join(work, "out-%dx%d.ppm" % (w, h))
            inc = os.path.join(work, "in-%dx%d.ppm" % (w, h))
            write_ppm(out, w, h, 1)
            write_ppm(inc, w, h, 2)
            for style in styles:
                for p in PROGRESS:
                    cpu = os.path.join(work, "cpu.ppm")
                    gpu = os.path.join(work, "gpu.ppm")
                    a = subprocess.run([args.exe, "--transition-dump", style, out, inc, cpu, str(p), "1.5"],
                                       capture_output=True, text=True)
                    b = subprocess.run([args.exe, "--gpu-transition-dump", style, out, inc, gpu, str(p), "1.5"],
                                       capture_output=True, text=True)
                    if a.returncode != 0 or b.returncode != 0:
                        print("FAIL %-8s %dx%d p=%.2f: the dump did not run (%s)" %
                              (style, w, h, p, (a.stderr + b.stderr).strip()))
                        failures += 1
                        continue
                    for line in b.stdout.splitlines():
                        if line.startswith("gpu-transition-dump: on "):
                            backend = line.split(" on ", 1)[1]
                    result = compare(cpu, gpu)
                    if result is None:
                        print("FAIL %-8s %dx%d p=%.2f: different sizes" % (style, w, h, p))
                        failures += 1
                        continue
                    mean, off, worst = result
                    bad = mean > args.mean or off > args.off
                    failures += bad
                    print("%s %-8s %4dx%-4d p=%.2f  mean %.3f  off %.3f%%  worst %d" %
                          ("FAIL" if bad else "ok  ", style, w, h, p, mean, off, worst))
    print("backend: %s" % backend)
    print("%d failure(s)" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
