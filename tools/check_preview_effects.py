#!/usr/bin/env python3
"""Sweep every effect through the CONTROL PREVIEW, with no output armed.

`check_effects.py` proves an effect reaches the recording, which is the output's
composite. It says nothing about the preview, and the preview had its own bug:
with no output window up it uploaded the raw decoded frame, so the operator saw
the cue ungraded until they armed an output ("the effects didn't show up in
preview until output was activated"). A sweep that arms an output cannot see
that class of fault at all -- so this one never arms one.

Method: open the show, TAKE, SEEK to a fixed time, PAUSE. Every case is then the
SAME frame of the SAME clip, so the screenshots differ only by the effect. The
freeze matters: comparing two runs at slightly different positions makes moving
video read as a working effect.

Comparison is a per-pixel difference, not a mean. Mean brightness is worthless
here -- inverting a picture whose mean is 127 gives a mean of 128, which reads
as "no change" while the picture is plainly inverted.

    python3 tools/check_preview_effects.py --exe build/windows/Release/Deckboy.exe

Windows-only for now: the capture is GDI PrintWindow. The app path it exercises
is platform-independent.
"""

import argparse
import io
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deckboy_testroot  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Serialised tokens from cue_effects.hpp, with the amount:paramA each one wants.
# Kept here rather than parsed, so adding an effect and forgetting this list
# fails loudly instead of quietly shrinking the sweep.
# These are written straight into the SAVE format, whose fields are
#   token:amount:paramA:paramB:BYPASSED:paramC:paramD
#
# Note the fourth one. It is NOT paramC -- that is the --effect-dump CLI order,
# and putting a parameter there instead sets "bypassed" to a non-zero value and
# switches the effect off, which then reports as the effect doing nothing. Any
# entry that needs a C or D has to write the 0 for bypassed explicitly.
EFFECTS = [
    ("invert",          "1:0.5"),
    ("posterise",       "1:0.15"),
    ("solarise",        "1:0.5"),
    ("threshold",       "1:0.5"),
    ("vignette",        "1:0.5"),
    ("grain",           "1:0.5"),
    ("scanlines",       "1:0.5"),
    ("channel_offset",  "1:0.5"),
    ("temporal_dither", "1:0.5"),
    ("pixel_sort",      "1:0.5"),
    ("block_glitch",    "1:0.5"),
    ("polar_warp",      "1:0.5"),
    ("luma_displace",   "1:0.5"),
    ("ripple",          "1:0.5"),
    ("kaleidoscope",    "1:0.5"),
    ("dye_advect",      "1:0.2"),
    ("reaction_bloom",  "1:0.45"),
    ("relativistic",    "0.9:0.6"),
    ("caustics",        "0.8:0.45:0.4:0:0.7"),
    ("feedback",        "0.9:0.62:0.55"),
    ("schlieren",       "0.95:0.12:0.55"),
    ("chladni",         "0.9:0.45:0.7"),
    ("wavefront",       "1.0:0.7:0.8:0:0.15:0.6"),
    ("crystallise",     "0.95:0.22:0.6"),
    ("scotopic",        "0.95:0.7:0.6"),
    ("grain_flow",      "1.0:0.95:0.0:0:0.15"),
    # The character grid, on whatever the cue happens to be. This one goes
    # through the app rather than the offline dump, because the renderer it
    # needs lives on the media engine -- so this sweep is the only automated
    # check it gets.
    ("text_mode",       "1.0:0.25:0.0:0:0.2:0.0"),
]

# Both need something a paused frame cannot give them.
SKIP = [("motion_puppet", "needs a motion driver and a moving picture"),
        ("datamosh", "works at decode; needs a background transcode first")]

CAPTURE_PS1 = '''
param([int]$ProcId, [string]$Out)
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
[StructLayout(LayoutKind.Sequential)]
public struct DbRect { public int L; public int T; public int R; public int B; }
public static class DbShot {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out DbRect r);
}
"@ -ErrorAction SilentlyContinue
# DPI-aware FIRST: without it GetWindowRect returns virtualised coordinates and
# every capture comes out cropped, which looks like the app failing to render.
[void][DbShot]::SetProcessDPIAware()
$p = Get-Process -Id $ProcId -ErrorAction Stop
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Output "NOWINDOW"; exit 1 }
$r = New-Object DbRect
[void][DbShot]::GetWindowRect($h, [ref]$r)
$w = $r.R - $r.L; $ht = $r.B - $r.T
if ($w -le 0 -or $ht -le 0) { Write-Output "BADRECT"; exit 1 }
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
# PW_RENDERFULLCONTENT (2): captures the SDL window without stealing focus.
[void][DbShot]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc); $g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "OK"
'''


def send(port, command, timeout=5.0):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall((command + "\n").encode())
        try:
            return sock.recv(65536).decode(errors="replace").strip()
        except OSError:
            return ""


def build_root(root, template, media, effect_field):
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(os.path.join(root, "data"), exist_ok=True)
    lines, kept = [], False
    for line in io.open(template, encoding="utf-8", errors="replace").read().split("\n"):
        f = line.split("\t")
        if f and f[0] == "cue":
            if kept:
                continue                       # one cue in the show: ours
            kept = True
            idx = deckboy_testroot.cue_effects_field_index(f)
            while len(f) <= idx + 1:
                f.append("")
            f[2] = media.replace("\\", "\\\\")
            f[3] = "FXCHECK"
            # The KIND, too.
            #
            # Rewriting only the path left the cue whatever the template's first
            # cue happened to be. On a machine whose saved show starts with a
            # pattern cue, the path was simply ignored and every effect was
            # being checked against a generated test card instead of the clip --
            # which passed for most of them and made "night eyes" look dead,
            # because an effect whose job is removing colour does nothing
            # visible to a near-monochrome card. The template is an ordinary
            # local show file and its first cue is not ours to assume.
            if len(f) > 4:
                f[4] = "video"
            f[idx] = effect_field
            f[idx + 1] = ""                    # no motion driver
            line = "\t".join(f)
        lines.append(line)
    io.open(os.path.join(root, "data", "default.deckboy"), "w",
            encoding="utf-8", newline="").write("\n".join(lines))
    deckboy_testroot.populate(root)


def capture(exe, root, port, shot, seek, script):
    env = dict(os.environ)
    env["DECKBOY_ROOT"] = root
    show = os.path.join(root, "data", "default.deckboy")
    env["DECKBOY_PROJECT"] = show
    env["DECKBOY_COMPANION_PORT"] = str(port)
    log = open(os.path.join(root, "app.log"), "w")
    # The show goes on the command line as well as in the environment: a bare
    # .deckboy argument opens it AND skips the splash. Without that the app sits
    # on the splash overlay (scripted keys do not reach SDL3) and every capture
    # is of the boot screen -- which reads as "the effect did nothing".
    proc = subprocess.Popen([exe, show], env=env, cwd=os.path.dirname(exe),
                            stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(120):
            try:
                send(port, "HELP", timeout=1.0)
                break
            except OSError:
                time.sleep(0.5)
        else:
            raise RuntimeError("control port %d never came up" % port)
        send(port, "MASTERVOL 0")
        # There is deliberately no "OUT on" in this file: the no-output preview
        # path is the thing under test.
        send(port, "SELECT 1")
        send(port, "TAKE")
        for _ in range(40):
            if "status=Playing" in send(port, "STATUS"):
                break
            time.sleep(0.25)
        send(port, "SEEK %g" % seek)
        time.sleep(1.0)
        send(port, "PAUSE")
        time.sleep(1.5)
        out = subprocess.run(["powershell", "-NoProfile", "-File", script,
                              "-ProcId", str(proc.pid), "-Out", shot],
                             capture_output=True, text=True)
        if "OK" not in out.stdout:
            print("   capture: %s %s" % (out.stdout.strip(),
                                         out.stderr.strip()[:160]))
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()


MONITOR_W = 140          # the width monitor_pixels() scales to
MONITOR_H = 68


def edge_energy(pixels, width):
    """Total absolute horizontal + vertical gradient across the monitor.

    A smear removes detail without moving the local average, which a per-pixel
    difference cannot see. This can: softening the picture drops this number,
    sharpening raises it.
    """
    rows = len(pixels) // width
    total = 0
    for y in range(rows - 1):
        row = y * width
        nxt = row + width
        for x in range(width - 1):
            here = pixels[row + x]
            total += abs(here - pixels[row + x + 1])
            total += abs(here - pixels[nxt + x])
    return total


def monitor_pixels(path):
    """The program-monitor region, greyscale, as raw bytes.

    Cropped to the monitor so the surrounding chrome -- which never changes --
    cannot dilute the difference, and so the burned-in timecode in the pattern's
    top-left corner stays out of the comparison.
    """
    return subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path,
         "-vf", "crop=700:340:390:250,scale=140:68,format=gray",
         "-f", "rawvideo", "-"], capture_output=True).stdout


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default=os.path.join(
        REPO, "build", "windows", "Release", "Deckboy.exe"))
    parser.add_argument("--media", help="clip to apply effects to "
                                        "(default: a generated test pattern)")
    parser.add_argument("--template", default=os.path.join(REPO, "data", "default.deckboy"),
                        help="a show to borrow a cue record from")
    parser.add_argument("--seek", type=float, default=4.0)
    parser.add_argument("--port", type=int, default=5840)
    parser.add_argument("--keep", action="store_true",
                        help="keep the screenshots for eyeballing")
    # This sweep launches the app once per effect, so a full run is minutes.
    # When one effect is in question -- a regression, or a new one being
    # written -- running just that one is the difference between a workflow
    # and a coffee break.
    parser.add_argument("--only", metavar="TOKEN", action="append",
                        help="check only these effects (repeatable)")
    args = parser.parse_args()

    exe = os.path.abspath(args.exe)
    if deckboy_testroot.warn_if_stale(exe):
        return 1

    work = tempfile.mkdtemp(prefix="deckboy-preview-fx-")
    script = os.path.join(work, "capture.ps1")
    io.open(script, "w", encoding="utf-8", newline="").write(CAPTURE_PS1)

    media = args.media
    if not media:
        media = os.path.join(work, "pattern.mp4")
        # ONE frame, held for the whole clip, with BOTH kinds of content in it.
        #
        # No single source suits every effect, and swapping between them only
        # moves which ones go quiet — measured both ways round. testsrc2 is
        # mostly large flat fields, so anything acting on EDGES (caustics
        # displacing by a surface slope, grain flow stroking along the grain)
        # barely moves it. A fractal is the opposite: all structure and no flat
        # colour, which is where the colour effects went quiet instead. So the
        # frame is half of each.
        #
        # And it is a single frame repeated, so the comparison no longer depends
        # on where in the clip the seek happened to land. That dependence was
        # never worth having in a check.
        still = os.path.join(work, "still.png")
        subprocess.run(["ffmpeg", "-y", "-v", "error",
                        "-f", "lavfi", "-i", "mandelbrot=size=640x720:rate=1",
                        "-f", "lavfi", "-i", "testsrc2=size=640x720:rate=1",
                        "-filter_complex", "[0:v][1:v]hstack=2",
                        "-frames:v", "1", still], check=True)
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-loop", "1",
                        "-i", still, "-t", "12", "-r", "30",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", media],
                       check=True)
    media = os.path.abspath(media)

    print("baseline (empty stack), no output armed ...")
    root = os.path.join(work, "baseline")
    build_root(root, args.template, media, "")
    base_shot = os.path.join(work, "baseline.png")
    capture(exe, root, args.port, base_shot, args.seek, script)
    base = monitor_pixels(base_shot) if os.path.exists(base_shot) else b""
    if not base:
        print("no baseline capture; aborting")
        return 1

    # THE SAME PICTURE, TWICE. Everything measured below is a difference from
    # the baseline, and a difference has a floor: two captures of one paused
    # frame still disagree slightly, because decode timing, the compositor and
    # the screen grab all wobble. Measuring that here means the thresholds can
    # be stated as multiples of what an UNCHANGED picture actually scores on
    # this machine, instead of constants picked by feel -- which is how an
    # 8% edge-energy threshold chosen for a smear came to fail grain at +7%.
    print("noise floor (the same baseline, captured again) ...")
    root = os.path.join(work, "baseline2")
    build_root(root, args.template, media, "")
    floor_shot = os.path.join(work, "baseline2.png")
    capture(exe, root, args.port + 900, floor_shot, args.seek, script)
    floor_px = monitor_pixels(floor_shot) if os.path.exists(floor_shot) else b""
    floor_pct = 0.0
    floor_edge = 0.0
    if floor_px and len(floor_px) == len(base):
        same = sum(1 for a, b in zip(base, floor_px) if abs(a - b) > 16)
        floor_pct = 100.0 * same / len(base)
        e0 = edge_energy(base, MONITOR_W)
        e1 = edge_energy(floor_px, MONITOR_W)
        if e0 > 0:
            floor_edge = 100.0 * abs(e1 - e0) / e0
    # Three times the floor, and never below the old hand-picked values' floor
    # of usefulness. A dead effect scores the floor; a real one clears it by a
    # wide margin, and the margin is now visible instead of assumed.
    pct_gate = max(1.0, floor_pct * 3.0)
    edge_gate = max(3.0, floor_edge * 3.0)
    print("floor: %.2f%% of pixels, %.2f%% edge energy  ->  "
          "gates %.2f%% / %.2f%%" % (floor_pct, floor_edge, pct_gate, edge_gate))

    results = []
    wanted = set(args.only) if args.only else None
    for i, (token, params) in enumerate(EFFECTS):
        if wanted is not None and token not in wanted:
            continue
        print("%s ..." % token)
        root = os.path.join(work, token)
        build_root(root, args.template, media, "%s:%s:0:0" % (token, params))
        shot = os.path.join(work, token + ".png")
        # A fresh port per case: reusing one leaves the previous app's socket
        # lingering, the next app fails to bind, and the harness then talks to
        # nothing and blames the effect.
        capture(exe, root, args.port + 1 + i, shot, args.seek, script)
        if not os.path.exists(shot):
            results.append((token, "FAIL", "no capture"))
            continue
        shot_px = monitor_pixels(shot)
        if not shot_px or len(shot_px) != len(base):
            results.append((token, "FAIL", "unreadable capture"))
            continue
        changed = sum(1 for a, b in zip(base, shot_px) if abs(a - b) > 16)
        pct = 100.0 * changed / len(base)
        # A SECOND measure, because the first one is structurally blind to a
        # whole class of effect.
        #
        # Counting differing pixels cannot see a SMEAR. Blurring or stroking
        # along a feature preserves the local average almost exactly, so grain
        # flow reported 0.8% while visibly softening the picture. What a smear
        # unmistakably does is remove high-frequency energy -- so this also
        # measures the total edge energy in the monitor and treats a big move in
        # EITHER as the effect having arrived.
        detail = "%.1f%% of the monitor differs" % pct
        arrived = pct >= pct_gate
        if not arrived:
            sharp_base = edge_energy(base, MONITOR_W)
            sharp_shot = edge_energy(shot_px, MONITOR_W)
            if sharp_base > 0:
                delta = 100.0 * abs(sharp_shot - sharp_base) / sharp_base
                if delta >= edge_gate:
                    arrived = True
                detail = "%s, edge energy %+.0f%%" % (
                    detail, 100.0 * (sharp_shot - sharp_base) / sharp_base)
        results.append((token, "ok" if arrived else "NO CHANGE", detail))

    if args.keep:
        print("\nscreenshots kept in %s" % work)
    else:
        shutil.rmtree(work, ignore_errors=True)

    print()
    print("%-18s %-10s %s" % ("effect", "verdict", "detail"))
    failures = 0
    for token, verdict, detail in results:
        print("%-18s %-10s %s" % (token, verdict, detail))
        if verdict in ("FAIL", "NO CHANGE"):
            failures += 1
    for token, why in SKIP:
        print("%-18s %-10s %s" % (token, "skipped", why))
    print("\n%s" % ("FAIL" if failures else
                    "every effect reaches the preview with no output armed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
