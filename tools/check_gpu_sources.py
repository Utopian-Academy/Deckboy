#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
"""Generated sources made on the graphics card match the CPU reference.

A source with a GPU version (Lumia, 2026-11-13) traces its small picture of
light on the CPU and lets a kernel (gpu_effects.hlsl sourceMain) do the
full-size per-pixel part (MediaEngine::planGpuSource, runGpuSource). The CPU
builder stays the reference. Two parts:

  1. Offline: --gpu-source-dump against --pattern-dump, frame for frame, on
     several settings. Both start from a fresh trace at the same moment, so
     they must agree to within the CPU's lookup tables (a level or so).
  2. In the app (a private desktop, silent): a Lumia cue on a 1080p output.
     The log must say the source moved to the card, the layer profile must
     show it drawn on the GPU every frame, and the output must look like the
     CPU path's (DECKBOY_CPU_SOURCES=1): dark round the lens, light inside,
     the same overall brightness.

    py -3 tools/check_gpu_sources.py [--exe ...] [--skip-app]
    DECKBOY_OUTPUT_RENDERER=gpu SDL_GPU_DRIVER=direct3d12 py -3 tools/check_gpu_sources.py --skip-app
"""
import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

# pattern[:variant], seconds. Each mirror mode, a palette with six beams,
# spin, a strong lens and many tilted panels.
CASES = [
    ("lumia", 7),
    ("lumia:mandala,palette=1", 7),
    ("lumia:none,spin=0.7", 7),
    ("lumia:palette=2,mirrors=0.9,angle=0.9,lens=1", 7),
    ("lumia:topbottom=0,jet=1,water=1", 3),
]
MEAN_MAX = 1.0      # the CPU reads its rim and glow from tables
FAR_MAX = 0.01      # % of pixels more than 8 levels apart (panel edges)


def read_ppm(path):
    data = Path(path).read_bytes()
    parts = data.split(maxsplit=4)
    w, h = int(parts[1]), int(parts[2])
    return w, h, parts[4][: w * h * 3]


def read_bmp(path):
    data = Path(path).read_bytes()
    off = int.from_bytes(data[10:14], "little")
    w = int.from_bytes(data[18:22], "little", signed=True)
    h = int.from_bytes(data[22:26], "little", signed=True)
    bpp = int.from_bytes(data[28:30], "little")
    step = bpp // 8
    stride = (w * step + 3) & ~3
    rows = []
    for y in range(abs(h)):
        src = (abs(h) - 1 - y) if h > 0 else y
        row = data[off + src * stride: off + src * stride + w * step]
        rows.append(bytes(b for i in range(0, len(row), step) for b in (row[i + 2], row[i + 1], row[i])))
    return w, abs(h), b"".join(rows)


def compare(a, b):
    total = far = 0
    n = len(a) // 3
    for i in range(n):
        d = max(abs(a[i * 3] - b[i * 3]), abs(a[i * 3 + 1] - b[i * 3 + 1]), abs(a[i * 3 + 2] - b[i * 3 + 2]))
        total += abs(a[i * 3] - b[i * 3]) + abs(a[i * 3 + 1] - b[i * 3 + 1]) + abs(a[i * 3 + 2] - b[i * 3 + 2])
        far += d > 8
    return total / (n * 3), far * 100.0 / n


def offline(exe, work):
    failures = 0
    for pattern, t in CASES:
        tag = re.sub(r"[^a-z0-9]+", "_", pattern)
        cpu, gpu = work / (tag + "-cpu.ppm"), work / (tag + "-gpu.ppm")
        env = dict(os.environ, SDL_AUDIODRIVER="dummy")
        subprocess.run([exe, "--pattern-dump", pattern, str(cpu), "960x540", str(t)], env=env,
                       capture_output=True, timeout=120)
        r = subprocess.run([exe, "--gpu-source-dump", pattern, str(gpu), "960x540", str(t)], env=env,
                           capture_output=True, text=True, timeout=120)
        if r.returncode == 3:
            print("SKIP  no GPU effects on this renderer")
            return 0
        backend = next((l.split("on ", 1)[1] for l in r.stdout.splitlines() if "gpu-source-dump: on" in l), "?")
        if r.returncode != 0 or not cpu.exists() or not gpu.exists():
            print("FAIL  %-48s the dump failed (%d) %s" % (pattern, r.returncode, r.stderr.strip()[-200:]))
            failures += 1
            continue
        _, _, a = read_ppm(cpu)
        _, _, b = read_ppm(gpu)
        mean, far = compare(a, b)
        ok = mean <= MEAN_MAX and far <= FAR_MAX
        failures += not ok
        print("%s  %-48s %s  mean %.3f  >8 levels %.4f%%" % ("ok  " if ok else "FAIL", pattern, backend, mean, far))
    return failures


def stats(rgb, w, h):
    """Mean brightness inside the lens, and in the four corners (dark)."""
    def mean_box(x0, y0, x1, y1):
        s = n = 0
        for y in range(y0, y1, 4):
            for x in range(x0, x1, 4):
                i = (y * w + x) * 3
                s += rgb[i] + rgb[i + 1] + rgb[i + 2]
                n += 3
        return s / max(1, n)
    inside = mean_box(w // 4, h // 4, w * 3 // 4, h * 3 // 4)
    corners = max(mean_box(0, 0, w // 16, h // 16), mean_box(w - w // 16, 0, w, h // 16),
                  mean_box(0, h - h // 16, w // 16, h), mean_box(w - w // 16, h - h // 16, w, h))
    return inside, corners


def in_app(exe, work, cpu_sources):
    from example_show_runtime import Runtime  # noqa: E402
    tag = "cpu" if cpu_sources else "gpu"
    show = work / ("show-%s.deckboy" % tag)
    show.write_text("update_check\t0\nui_sounds\t0\nui_scale\t1.0\n", encoding="utf-8")
    env = {"DECKBOY_UI_PROFILE": "1"}
    if cpu_sources:
        env["DECKBOY_CPU_SOURCES"] = "1"
    snap = work / ("out-%s.bmp" % tag)
    with Runtime(exe, work / ("run-" + tag), show, env=env) as db:
        db.send("MASTERVOL 0")
        db.send("DECK 1")
        db.send("PATTERN ADD lumia")
        db.send("SELECT 1")
        db.send("OUTPUT SELECT 1")
        db.send("OUTPUT ON")
        db.send("VIDEO 1920X1080")
        time.sleep(1.5)
        db.send("DECK 1")
        db.send("SELECT 1")
        db.send("TAKE")
        time.sleep(6)
        db.send("OUTSNAP %s" % snap)
        time.sleep(1.5)
        log = (work / ("run-" + tag) / "app.log").read_text(errors="replace")
    return snap, log


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--work", default=str(ROOT / "build/gpu-sources"))
    ap.add_argument("--skip-app", action="store_true", help="the offline comparison only")
    args = ap.parse_args()
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    exe = str(Path(args.exe).resolve())

    print("offline: --gpu-source-dump against --pattern-dump (960x540)")
    failures = offline(exe, work)
    if args.skip_app:
        print("gpu sources: %s" % ("FAIL (%d)" % failures if failures else "ok"))
        return 1 if failures else 0

    print("in the app: a Lumia cue on a 1080p output")
    gpu_snap, gpu_log = in_app(exe, work, False)
    cpu_snap, cpu_log = in_app(exe, work, True)

    def expect(cond, label, detail=""):
        nonlocal failures
        failures += not cond
        print(("ok    " if cond else "FAIL  ") + label + ("" if cond or not detail else "  [%s]" % detail))

    expect("gpu-sources: playlist 1 sources made on the card on" in gpu_log, "the source moved to the card")
    gpu_frames = re.findall(r"layer 1 \d+x\d+ /\d+ .*?\((\d+)/120 on gpu\)", gpu_log)
    expect(bool(gpu_frames) and all(int(n) >= 110 for n in gpu_frames[1:] or gpu_frames),
           "the layer was drawn on the GPU", "profile: %s" % gpu_frames[-4:])
    sizes = re.findall(r"layer 1 (\d+)x(\d+) ", gpu_log)
    expect(bool(sizes) and int(sizes[-1][0]) <= 640, "the desk picture is the small one", str(sizes[-1:]))
    expect("sources made on the card on" not in cpu_log, "DECKBOY_CPU_SOURCES=1 keeps it on the CPU")
    for name, snap in (("card", gpu_snap), ("cpu", cpu_snap)):
        expect(snap.exists(), "the %s output was captured" % name)
    if gpu_snap.exists() and cpu_snap.exists():
        gw, gh, g = read_bmp(gpu_snap)
        cw, ch, c = read_bmp(cpu_snap)
        g_in, g_corner = stats(g, gw, gh)
        c_in, c_corner = stats(c, cw, ch)
        print("      inside the lens: card %.1f, cpu %.1f;  darkest corner: card %.1f, cpu %.1f"
              % (g_in, c_in, g_corner, c_corner))
        expect(gw == cw == 1920 and gh == ch == 1080, "both outputs are 1920x1080", "%dx%d %dx%d" % (gw, gh, cw, ch))
        expect(g_in > 4.0, "the card's picture has light in it")
        expect(g_corner < 2.0, "the card's lens is dark at the corners")
        expect(abs(g_in - c_in) <= max(3.0, c_in * 0.35), "card and CPU are about as bright")
    print("gpu sources: %s" % ("FAIL (%d)" % failures if failures else "ok"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
