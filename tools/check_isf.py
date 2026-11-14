#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
"""Shaders as a source (ISF) run on the graphics card.

The CPU half (every shipped shader parses, wraps, compiles to SPIR-V and
cross-compiles to HLSL and MSL; a refused shader gives its reason; the input
values survive a save) is part of `Deckboy --smoke`. This is the card's half:

  1. --gpu-source-dump on a test shader whose colour is known: every pixel
     must be exactly 255, 0, 0, 255 (it is dumped over black, so 255, 0, 0).
  2. The orientation shader: green on top, blue below, a red first column.
  3. Every shipped shader draws a picture that is not black and not flat.
  4. In the app (a private desktop, silent): SHADER LOAD makes a cue, SHADER
     SET changes an input, STATUS reads it back, and an OUTSNAP of a 1080p
     output shows the shader; a UISNAP shows the inspector with its rows.

    py -3 tools/check_isf.py [--exe ...] [--skip-app]
    DECKBOY_OUTPUT_RENDERER=gpu SDL_GPU_DRIVER=vulkan py -3 tools/check_isf.py --skip-app   (Linux)
"""
import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))


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
    step = int.from_bytes(data[28:30], "little") // 8
    stride = (w * step + 3) & ~3
    rows = []
    for y in range(abs(h)):
        src = (abs(h) - 1 - y) if h > 0 else y
        row = data[off + src * stride: off + src * stride + w * step]
        rows.append(bytes(b for i in range(0, len(row), step) for b in (row[i + 2], row[i + 1], row[i])))
    return w, abs(h), b"".join(rows)


def dump(exe, shader, out, size, seconds=2):
    env = dict(os.environ, SDL_AUDIODRIVER="dummy")
    r = subprocess.run([exe, "--gpu-source-dump", "isf:" + str(shader), str(out), size, str(seconds)], env=env,
                       capture_output=True, text=True, timeout=180)
    backend = next((l.split("on ", 1)[1] for l in r.stdout.splitlines() if "gpu-source-dump: on" in l), "?")
    return r, backend


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--work", default=str(ROOT / "build/isf"))
    ap.add_argument("--skip-app", action="store_true", help="the offline dumps only")
    args = ap.parse_args()
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    exe = str(Path(args.exe).resolve())
    failures = 0

    def expect(cond, label, detail=""):
        nonlocal failures
        failures += not cond
        print(("ok    " if cond else "FAIL  ") + label + ("" if cond or not detail else "  [%s]" % detail))

    # 1. a known colour, exactly
    r, backend = dump(exe, ROOT / "tools/isf_tests/solid_red.fs", work / "red.ppm", "64x48")
    if r.returncode == 3 or "no video" in r.stderr:
        print("SKIP  no GPU effects on this renderer (the CPU half is in --smoke)")
        return 0
    expect(r.returncode == 0, "the solid red shader ran on " + backend, r.stderr.strip()[-200:])
    if r.returncode == 0:
        w, h, px = read_ppm(work / "red.ppm")
        expect(px == bytes([255, 0, 0]) * (w * h), "every pixel is exactly 255, 0, 0")
    # 2. which way is up
    r, _ = dump(exe, ROOT / "tools/isf_tests/corners.fs", work / "corners.ppm", "64x48")
    if r.returncode == 0:
        w, h, px = read_ppm(work / "corners.ppm")
        at = lambda x, y: tuple(px[(y * w + x) * 3:(y * w + x) * 3 + 3])
        expect(at(32, 2) == (0, 255, 0) and at(32, h - 3) == (0, 0, 255) and at(0, 5) == (255, 0, 0) and at(1, 5) != (255, 0, 0),
               "green on top, blue below, a red first column", "%s %s %s" % (at(32, 2), at(32, h - 3), at(0, 5)))
    else:
        expect(False, "the orientation shader ran", r.stderr.strip()[-200:])
    # 3. the shipped shaders
    shipped = sorted((ROOT / "data/shaders").glob("*.fs"))
    expect(len(shipped) >= 4, "Deckboy ships shaders", str(len(shipped)))
    for shader in shipped:
        out = work / (shader.stem + ".ppm")
        r, _ = dump(exe, shader, out, "640x360", 3)
        if r.returncode != 0:
            expect(False, shader.name + " ran on the card", r.stderr.strip()[-200:])
            continue
        w, h, px = read_ppm(out)
        sample = px[:: 3 * 37]
        spread = max(sample) - min(sample)
        expect(spread > 40 and any(px), shader.name + " draws a picture (not black, not flat)", "spread %d" % spread)

    if not args.skip_app:
        from example_show_runtime import Runtime
        show = work / "show.deckboy"
        show.write_text("update_check\t0\nui_sounds\t0\nui_scale\t1.5\n", encoding="utf-8")
        plasma = ROOT / "data/shaders/deckboy_plasma.fs"
        with Runtime(exe, work / "run", show) as db:
            db.send("MASTERVOL 0")
            db.send("DECK 1")
            reply = db.send("SHADER LOAD " + str(plasma))
            db.send("SELECT 1")
            expect("loaded" in reply, "SHADER LOAD makes a cue", reply)
            status = db.send("SHADER STATUS")
            expect("inputs=6" in status and "speed=0.5" in status, "SHADER STATUS lists the inputs", status)
            db.send("SHADER SET speed 2")
            db.send("SHADER SET colourOne #00ff00")
            db.send("SHADER SET bands on")
            db.send("SHADER LFO zoom ON")
            status = db.send("SHADER STATUS")
            expect("speed=2" in status and "colourOne=#00ff00" in status and "bands=on" in status,
                   "SHADER SET reads back", status)
            for bad in ("SHADER SET speed 99", "SHADER SET nothing 1", "SHADER SET colourOne 2 0 0"):
                try:
                    db.send(bad)
                    expect(False, bad + " is refused")
                except RuntimeError:
                    expect(True, bad + " is refused")
            db.send("OUTPUT SELECT 1")
            db.send("OUTPUT ON")
            db.send("VIDEO 1920X1080")
            time.sleep(1.5)
            db.send("DECK 1")
            db.send("SELECT 1")
            db.send("TAKE")
            time.sleep(5)
            out = db.snapshot(work / "output.bmp", output=True)
            ui = db.snapshot(work / "inspector.bmp")
            w, h, px = read_bmp(out)
            sample = px[:: 3 * 997 + 1]
            expect(w == 1920 and h == 1080 and max(sample) - min(sample) > 60, "the 1080p output shows the shader",
                   "%dx%d" % (w, h))
            expect(ui.exists(), "the inspector was captured: " + str(ui))
            saved = db.saved()
            expect("pattern://isf:" in saved and "colourOne|0,1,0,1|" in saved.replace("\t", "|").replace(" ", "")
                   or "colourOne|" in saved, "the shader's values are in the saved show")
        # The inputs, below the fold: scroll the inspector (the lava lamp has
        # a float, a choice and three colours).
        with Runtime(exe, work / "run-inputs", show, extra=("--inspector-scroll", "560")) as db:
            db.send("MASTERVOL 0")
            db.send("DECK 1")
            db.send("SHADER LOAD " + str(ROOT / "data/shaders/deckboy_lava_lamp.fs"))
            db.send("SELECT 1")
            time.sleep(1.5)
            inputs = db.snapshot(work / "inspector-inputs.bmp")
            expect(inputs.exists(), "the shader's input rows were captured: " + str(inputs))
    print("shaders: %s" % ("FAIL (%d)" % failures if failures else "ok"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
