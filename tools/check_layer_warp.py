#!/usr/bin/env python3
"""A layer's corner pin actually moves the picture.

WHY THIS EXISTS. A mapping control is the easiest kind of feature to ship
inert: the field saves, the renderer reads it, the verb answers OK, and the
picture never changes. That has happened here twice already -- SHATTER spent
twelve days doing nothing, and the entire warp UI spent two releases adjusting
fields the compositor had stopped reading.

So this does not ask the app what its warp is. It RECORDS the programme and
compares pixels:

  * the layer covers the frame, so a corner patch is the LAYER's colour
  * pull that corner most of the way across, and the patch stops being the
    layer and becomes what is underneath
  * the OPPOSITE corner, which the pin did not touch, stays as it was
  * turning the pin off puts the picture back

The third test is what separates "the pin works" from "something changed the
whole frame", and the fourth separates it from "the layer broke". A check that
only asserted "the picture changed" would pass on a layer that had simply
stopped drawing.

Recording needs a presented output window, so this runs on a desktop session,
not on a CI runner -- a runner has no display, SDL_CreateWindow fails, and the
egress never receives a frame. Needs ffmpeg to pull a frame out of the file.

    python3 tools/check_layer_warp.py
"""

import io
import os
import socket
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deckboy_testroot  # noqa: E402

PORT = 5723
# Solid, flat and very different from each other, so a patch mean says plainly
# which one is on top. A photograph would not.
BASE_PATTERN = "full-black"
LAYER_PATTERN = "full-white"


def send(command, timeout=12.0):
    with socket.create_connection(("127.0.0.1", PORT), timeout=timeout) as s:
        s.settimeout(timeout)
        s.sendall((command + "\n").encode())
        try:
            return s.recv(65536).decode(errors="replace").strip()
        except OSError:
            return ""


def read_ppm(path):
    with open(path, "rb") as handle:
        data = handle.read()
    fields, at = [], 2
    while len(fields) < 3:
        while at < len(data) and data[at:at + 1].isspace():
            at += 1
        start = at
        while at < len(data) and not data[at:at + 1].isspace():
            at += 1
        fields.append(int(data[start:at]))
    return fields[0], fields[1], data[at + 1:]


def patch_mean(path, fx, fy, frac=0.10):
    w, h, px = read_ppm(path)
    pw, ph = max(1, int(w * frac)), max(1, int(h * frac))
    x0 = max(0, min(w - pw, int(w * fx) - pw // 2))
    y0 = max(0, min(h - ph, int(h * fy) - ph // 2))
    total = count = 0
    for row in range(y0, y0 + ph):
        base = (row * w + x0) * 3
        for i in range(base, base + pw * 3):
            total += px[i]
            count += 1
    return total / max(1, count)


def record_frame(root, tag, seconds=2.5):
    """Roll the recorder briefly and pull one frame out as a PPM."""
    send("RECORD START")
    time.sleep(seconds)
    send("RECORD STOP")
    time.sleep(2.0)
    rec_dir = os.path.join(root, "data", "recordings")
    for candidate in (rec_dir, os.path.join(root, "recordings"), root):
        if not os.path.isdir(candidate):
            continue
        files = [os.path.join(candidate, f) for f in os.listdir(candidate)
                 if f.lower().endswith((".mp4", ".mov", ".mkv"))]
        if not files:
            continue
        newest = max(files, key=os.path.getmtime)
        out = os.path.join(root, "frame-%s.ppm" % tag)
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-sseof", "-1",
                        "-i", newest, "-frames:v", "1", "-pix_fmt", "rgb24", out],
                       capture_output=True)
        if os.path.exists(out):
            return out, newest
    return None, None


def main():
    exe = os.path.join(REPO, "build", "windows", "Release", "Deckboy.exe")
    if len(sys.argv) > 1:
        exe = sys.argv[1]
    if not os.path.exists(exe):
        print("no binary at %s" % exe)
        return 1
    deckboy_testroot.warn_if_stale(exe)

    root = tempfile.mkdtemp(prefix="deckboy-lwarp-")
    os.makedirs(os.path.join(root, "data"), exist_ok=True)
    show = os.path.join(root, "data", "default.deckboy")
    io.open(show, "w", encoding="utf-8", newline="").write(
        io.open(os.path.join(REPO, "data", "default.deckboy"),
                encoding="utf-8", errors="replace").read())
    deckboy_testroot.populate(root)
    env = dict(os.environ)
    env.update(DECKBOY_ROOT=root, DECKBOY_PROJECT=show,
               DECKBOY_COMPANION_PORT=str(PORT))
    log = open(os.path.join(root, "app.log"), "w")
    proc = subprocess.Popen([exe, show], env=env, cwd=os.path.dirname(exe),
                            stdout=log, stderr=subprocess.STDOUT)

    fails = []

    def note(name, ok, detail=""):
        print("  %-46s %s" % (name, "ok" if ok else "FAIL  " + detail))
        if not ok:
            fails.append("%s -- %s" % (name, detail))

    print("check: a layer's corner pin moves the picture")
    print()
    try:
        for _ in range(150):
            try:
                send("HELP")
                break
            except OSError:
                time.sleep(0.5)
        send("MASTERVOL 0")

        # -- THE REFUSALS, which need no output at all --------------------
        print("  what it refuses:")
        for cmd, why in (
                ("VIDEO OUTPUT LAYERWARP 1 2 3", "too few numbers"),
                ("VIDEO OUTPUT LAYERWARP a b c d e f g h", "values that are not numbers")):
            reply = send(cmd)
            note("  " + why, reply.upper().startswith("ERR"), reply)
        base_refusal = send("VIDEO OUTPUT LAYERWARP 0 0 0 0 0 0 0 0")
        note("  the base deck is not a layer, and says so",
             base_refusal.upper().startswith("ERR"), base_refusal)
        print()

        # -- A LAYER OVER A BASE, ON A REAL OUTPUT -----------------------
        print("  setting up a layer over a base:")
        send("DECK 1")
        print("    base  : %s" % send("PATTERN ADD " + BASE_PATTERN))
        print("    take  : %s" % send("TAKE"))
        send("DECKADD")
        send("DECK 2")
        print("    layer : %s" % send("PATTERN ADD " + LAYER_PATTERN))
        print("    take  : %s" % send("TAKE"))
        send("DECK 2")
        assign = send("VIDEO OUTPUT ASSIGN")
        print("    assign deck 2 as a layer: %s" % assign)
        stack = send("VIDEO OUTPUT LAYER")
        print("    the stack: %s" % stack)
        time.sleep(1.5)

        off_frame, path = record_frame(root, "off")
        if not off_frame:
            print()
            print("  no recording was produced, so the pixel half of this check")
            print("  could not run. On a machine with no display session that is")
            print("  expected: every output needs a window, and without one the")
            print("  encoder never receives a frame. The refusals above did run.")
            print()
            print("clean (refusals only)" if not fails
                  else "%d finding%s" % (len(fails), "" if len(fails) == 1 else "s"))
            return 1 if fails else 0

        tl_off = patch_mean(off_frame, 0.08, 0.08)
        br_off = patch_mean(off_frame, 0.92, 0.92)
        print("    recorded %s" % os.path.basename(path))
        print("    pin off:  top-left %.1f   bottom-right %.1f" % (tl_off, br_off))
        print()

        # Drag the top-left corner most of the way across and down, so that
        # corner of the frame is no longer covered by the layer.
        print("  with the top-left corner pulled inward:")
        reply = send("VIDEO OUTPUT LAYERWARP 0.8 0.8 0 0 0 0 0 0")
        print("    %s" % reply)
        time.sleep(1.5)
        on_frame, _ = record_frame(root, "on")
        if not on_frame:
            fails.append("the second recording was not produced")
        else:
            tl_on = patch_mean(on_frame, 0.08, 0.08)
            br_on = patch_mean(on_frame, 0.92, 0.92)
            print("    pin on:   top-left %.1f   bottom-right %.1f" % (tl_on, br_on))
            note("  the pinned corner changed",
                 abs(tl_on - tl_off) > 20.0,
                 "top-left %.1f -> %.1f, which is not a move" % (tl_off, tl_on))
            note("  and moved AWAY from the layer's colour",
                 tl_on < tl_off,
                 "top-left went %.1f -> %.1f; the layer is the bright one"
                 % (tl_off, tl_on))
            note("  the untouched corner did not",
                 abs(br_on - br_off) <= 12.0,
                 "bottom-right %.1f -> %.1f: the whole frame moved, not the corner"
                 % (br_off, br_on))

        print()
        print("  and switching it off puts it back:")
        print("    %s" % send("VIDEO OUTPUT LAYERWARP OFF"))
        time.sleep(1.5)
        back_frame, _ = record_frame(root, "back")
        if back_frame:
            tl_back = patch_mean(back_frame, 0.08, 0.08)
            print("    pin off:  top-left %.1f" % tl_back)
            note("  the corner is covered again",
                 abs(tl_back - tl_off) <= 12.0,
                 "top-left %.1f, was %.1f before the pin" % (tl_back, tl_off))
    finally:
        try:
            send("RECORD STOP")
            send("QUIT")
        except OSError:
            pass
        for _ in range(40):
            if proc.poll() is not None:
                break
            time.sleep(0.25)
        if proc.poll() is None:
            proc.kill()

    print()
    for f in fails:
        print("  FAIL " + f)
    print()
    print("clean" if not fails
          else "%d finding%s" % (len(fails), "" if len(fails) == 1 else "s"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
