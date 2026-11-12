#!/usr/bin/env python3
"""A scaled layer's effects, run at the size it is drawn, look as they did.

Since 2026-11-11 a layer's effect stack runs on a copy reduced to about the
size the output draws it (layerFxShrinkFor, app_render_output.ipp), and the
result is placed as the full frame would be. This draws the same paused frame
both ways -- with DECKBOY_FX_FULL_SIZE=1 and without -- and compares them:

  - the layer lands in the same place (its edges are found in both),
  - the picture inside it matches (mean difference small; the two differ only
    by how the reduction was filtered),
  - during a crossfade out of that cue the outgoing look is still there.

Each case runs on the screen (--visible) or a private desktop, silently.

    py -3 tools/check_fx_shrink.py [--visible]
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402
from check_mixed_stress import import_clip  # noqa: E402

SEED = "update_check\t0\nui_sounds\t0\nui_scale\t1.0\n"
CASES = [
    # name, scale, x, y, effects
    ("quarter, invert+vignette", 0.25, -500, -250, ["invert", "vignette"]),
    ("small, blur+glow", 0.3, 400, 200, ["blur", "glow"]),
    # Scanlines' pitch is in PIXELS of the frame it runs on, so this one is
    # meant to differ: at full size the lines were drawn at 1080 and then
    # shrunk by the GPU into irregular aliased bands; now they land on the
    # output's own pixels. Only the placement is compared (every difference
    # must fall inside where the layer is).
    ("small, scanlines+posterise", 0.3, -400, 220, ["scanlines", "posterise"]),
    ("rotated, kaleidoscope", 0.3, 300, -200, ["kaleidoscope"]),
]


def read_bmp(path):
    data = Path(path).read_bytes()
    off = int.from_bytes(data[10:14], "little")
    w = int.from_bytes(data[18:22], "little", signed=True)
    h = int.from_bytes(data[22:26], "little", signed=True)
    bpp = int.from_bytes(data[28:30], "little")
    step = bpp // 8
    row = (w * step + 3) & ~3
    flip = h > 0
    h = abs(h)

    def px(x, y):
        yy = h - 1 - y if flip else y
        i = off + yy * row + x * step
        return data[i + 2], data[i + 1], data[i]
    return w, h, px


def compare(a, b):
    wa, ha, pa = read_bmp(a)
    wb, hb, pb = read_bmp(b)
    assert (wa, ha) == (wb, hb), ((wa, ha), (wb, hb))
    total = 0
    big = 0
    n = 0
    box = [wa, ha, -1, -1]
    for y in range(0, ha, 3):
        for x in range(0, wa, 3):
            ca, cb = pa(x, y), pb(x, y)
            d = max(abs(ca[i] - cb[i]) for i in range(3))
            total += d
            big += d > 48
            n += 1
            if d > 48:
                box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
    return total / n, 100.0 * big / n, box


def snap(exe, work, tag, clip, case, full, visible, against="full"):
    name, scale, x, y, fx = case
    show = work / ("%s.deckboy" % tag)
    show.write_text(SEED, encoding="utf8")
    if against == "cpu":
        # Both reduced; the first on the CPU, the second wherever it lands (the GPU).
        env = {"DECKBOY_CPU_EFFECTS": "1" if full else None, "DECKBOY_FX_FULL_SIZE": None}
    else:
        env = {"DECKBOY_FX_FULL_SIZE": "1" if full else None}
    out = {}
    with Runtime(exe, work / ("run-" + tag), show, env=env, visible=visible) as db:
        db.send("MASTERVOL 0")
        db.send("DECK 1")
        db.send("PATTERN ADD smpte-bars")
        db.send("SELECT 1")
        db.send("TAKE")
        db.send("DECKADD")
        db.send("DECK 2")
        import_clip(db, work, clip)
        db.send("PATTERN ADD greyscale")   # a still: the incoming picture does not move
        db.send("SELECT 1")
        for c in ("SCALE %g" % scale, "OFFSETX %d" % x, "OFFSETY %d" % y):
            db.send(c)
        if "rotated" in name:
            db.send("ROTATION 20")
        for f in fx:
            db.send("FX ADD " + f)
        db.send("SELECT 2")
        db.send("SCALE %g" % scale)
        db.send("OFFSETX %d" % x)
        db.send("OFFSETY %d" % y)
        db.send("VIDEO OUTPUT ASSIGN")
        db.send("OUTPUT SELECT 1")
        db.send("OUTPUT ON")
        db.send("VIDEO 1920X1080")
        time.sleep(1.5)
        db.send("DECK 2")   # OUTPUT and VIDEO can move the focus
        db.send("SELECT 1")
        db.send("TAKE")
        time.sleep(1.5)
        db.send("SEEK 2")
        db.send("PAUSE")
        time.sleep(1.5)
        still = work / ("%s-still.bmp" % tag)
        db.send("OUTSNAP %s" % still)
        time.sleep(1.0)
        out["still"] = still
        # A slow crossfade to the plain cue: the outgoing look must survive.
        db.send("TRANSITION 4")
        db.send("TRANSITIONSTYLE crossfade")
        db.send("DECK 2")
        db.send("SELECT 2")   # not GOTO: that is a jump, which cuts
        db.send("TAKE")
        time.sleep(1.5)
        mid = work / ("%s-mid.bmp" % tag)
        db.send("OUTSNAP %s" % mid)
        time.sleep(1.0)
        out["mid"] = mid
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--work", default=str(ROOT / "build/fx-shrink"))
    ap.add_argument("--visible", action="store_true")
    ap.add_argument("--against", choices=["full", "cpu"], default="full",
                    help="full: reduced vs full size; cpu: the GPU effects vs the CPU ones")
    args = ap.parse_args()
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    clip = work / "clip.mp4"
    if not clip.exists():
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                        "testsrc2=s=1920x1080:r=30:d=6", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                        str(clip)], check=True)
    fails = 0
    for i, case in enumerate(CASES):
        full = snap(args.exe, work, "c%d-full" % i, clip, case, True, args.visible, args.against)
        small = snap(args.exe, work, "c%d-small" % i, clip, case, False, args.visible, args.against)
        for key, limit_mean in (("still", 6.0), ("mid", 8.0)):
            mean, big, box = compare(full[key], small[key])
            if "scanlines" in case[0] and args.against == "full":
                # The layer: 0.3 of the 1920x1080 frame, centred at the offset.
                cx, cy, w, h = 960 + case[2], 540 + case[3], 1920 * case[1], 1080 * case[1]
                inside = box[2] < 0 or (box[0] >= cx - w / 2 - 6 and box[2] <= cx + w / 2 + 6 and
                                        box[1] >= cy - h / 2 - 6 and box[3] <= cy + h / 2 + 6)
                ok = inside
            else:
                ok = mean <= limit_mean and big <= 2.0
            fails += not ok
            print("  %-28s %-5s mean diff %5.2f  pixels off by >48: %5.2f%%  %s"
                  % (case[0], key, mean, big, "ok" if ok else "FAIL"), flush=True)
    print("all ok" if not fails else "%d FAILED" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
