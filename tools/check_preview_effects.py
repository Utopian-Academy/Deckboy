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
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i",
                        "testsrc2=size=1280x720:rate=30:duration=12",
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

    results = []
    for i, (token, params) in enumerate(EFFECTS):
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
        # 1% of the monitor is well clear of the noise on a paused picture, and
        # low enough to catch the subtle ones (grain, scanlines).
        results.append((token, "ok" if pct >= 1.0 else "NO CHANGE",
                        "%.1f%% of the monitor differs" % pct))

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
