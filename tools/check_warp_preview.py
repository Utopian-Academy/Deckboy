#!/usr/bin/env python3
"""The control window's monitor shows the warp while the WARP editor is open.

The monitor is the programme, tapped before warp (captureOutputPreviewTap) --
except while the warp editor is open on the output, when it is drawn the way
the window presents it, so the handles sit on the warped picture's corners.
Before, it stayed flat under the handles and only the projector showed the
warp.

A white picture, its top-left corner pulled in on the output (VIDEO WARP TL):

  editor closed  -> the monitor is all white       (the flat programme)
  editor open    -> a wedge of it goes black        (the picture as presented)
  editor closed  -> white again

Judged from the control window as drawn (UISNAP), by counting pixels that are
white in one snapshot and black in the other -- no screen coordinates, so it
holds at any UI scale or layout. The overlay the editor draws (banner,
handles, wireframe, toolbar) is never that black, so a flat monitor under the
editor counts close to nothing.

Needs a presented output window, so a desktop session (hidden windows are
fine: DECKBOY_HIDE_WINDOWS=1).

    python tools/check_warp_preview.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from deckboy_harness import Deckboy  # noqa: E402


def read_bmp(path):
    """Grey levels of a 24/32-bit BMP, top row first."""
    data = open(path, "rb").read()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0] // 8
    stride = (width * bpp + 3) & ~3
    rows = abs(height)
    grey = bytearray(width * rows)
    for y in range(rows):
        src = offset + ((rows - 1 - y) if height > 0 else y) * stride
        line = data[src:src + width * bpp]
        for x in range(width):
            o = x * bpp
            grey[y * width + x] = (line[o] + line[o + 1] + line[o + 2]) // 3
    return width, rows, grey


def white_to_black(a, b):
    return sum(1 for p, q in zip(a, b) if p > 235 and q < 16)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--port", type=int, default=5707)
    ap.add_argument("--keep", action="store_true")
    cli = ap.parse_args()

    work = tempfile.mkdtemp(prefix="check-warp-preview-")
    clip = os.path.join(work, "white.mp4")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi",
                    "-i", "color=c=white:s=1920x1080:r=25:d=30",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", clip], check=True)
    args = argparse.Namespace(exe=os.path.abspath(cli.exe), port=cli.port, keep=cli.keep, ffmpeg="")
    shots = {}

    with Deckboy(args, "check-warp-preview-", extra_args=["--import", clip]) as db:

        def snap(name):
            path = os.path.join(work, name + ".bmp")
            db.send("UISNAP " + path)
            for _ in range(100):
                if os.path.exists(path) and os.path.getsize(path) > 1000:
                    time.sleep(0.2)
                    shots[name] = read_bmp(path)
                    return
                time.sleep(0.05)
            raise RuntimeError("no UISNAP for " + name)

        db.send("OUTPUT ON")
        db.send("SELECT 1")
        db.send("TAKE")
        time.sleep(2.0)
        db.send("VIDEO WARP ON")
        db.send("VIDEO WARP TL 600 300")
        time.sleep(1.0)
        snap("closed")
        db.send("WARP EDIT")
        time.sleep(1.0)
        snap("editing")
        db.send("WARP EDIT OFF")
        time.sleep(1.0)
        snap("closed-again")
        db.send("OUTPUT OFF")

    w, h, closed = shots["closed"]
    area = w * h
    fails = []

    def check(ok, what, detail):
        print("  %-48s %s  %s" % (what, "ok  " if ok else "FAIL", detail))
        if not ok:
            fails.append(what)

    whites = sum(1 for p in closed if p > 235)
    check(whites > area * 0.03, "the programme is on the monitor",
          "%.1f%% of the window white" % (100.0 * whites / area))
    opened = white_to_black(closed, shots["editing"][2])
    check(opened > area * 0.005, "editing: the monitor shows the warp",
          "%.2f%% of the window went white to black" % (100.0 * opened / area))
    back = white_to_black(closed, shots["closed-again"][2])
    check(back < area * 0.0005, "closed: the monitor is flat again",
          "%.3f%% white to black" % (100.0 * back / area))

    if fails:
        print("%d FAILED (shots in %s)" % (len(fails), work))
        return 1
    print("warp preview: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
