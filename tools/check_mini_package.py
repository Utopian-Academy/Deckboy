#!/usr/bin/env python3
"""Unpack a Deckboy Mini download and play a clip with it, headless.

The packagers check that Mini STARTS and reports its version; this checks it
PLAYS -- that the libraries in the download can decode -- which is the thing a
slimmed-down ffmpeg could quietly lose.

    python3 tools/check_mini_package.py <Deckboy-Mini-*.tar.gz or .zip> [--ffmpeg ffmpeg]
"""
import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("archive")
ap.add_argument("--ffmpeg", default=shutil.which("ffmpeg") or "ffmpeg", help="makes the test clip")
ap.add_argument("--port", type=int, default=5851)
args = ap.parse_args()

work = tempfile.mkdtemp(prefix="mini-package-")
if args.archive.endswith(".zip") and shutil.which("ditto"):
    # As a Mac unzips: permissions and links kept, which zipfile drops.
    subprocess.run(["ditto", "-x", "-k", args.archive, work], check=True)
elif args.archive.endswith(".zip"):
    zipfile.ZipFile(args.archive).extractall(work)
else:
    tarfile.open(args.archive).extractall(work)
top = os.path.join(work, os.listdir(work)[0])
exe = None
for cand in ("deckboy-mini", "deckboy-mini.exe", os.path.join("bin", "deckboy-mini")):
    p = os.path.join(top, cand)
    if os.path.isfile(p):
        exe = p
        break
if not exe:
    sys.exit("no deckboy-mini in %s" % top)
if os.name != "nt":
    for root, _dirs, files in os.walk(top):
        for f in files:
            if f.startswith("deckboy-mini") or root.endswith("bin"):
                os.chmod(os.path.join(root, f), 0o755)

clip = os.path.join(work, "clip.mp4")
subprocess.run([args.ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=s=640x360:r=25:d=8",
                "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "8",
                "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", clip], check=True)

# Off-screen only where there is no display at all (a Linux build machine);
# macOS needs its own driver for the Metal renderer, and Mini exited there.
env = dict(os.environ, SDL_AUDIO_DRIVER="dummy")
if sys.platform.startswith("linux") and "SDL_VIDEO_DRIVER" not in os.environ:
    env["SDL_VIDEO_DRIVER"] = "offscreen"
log = open(os.path.join(work, "mini.log"), "w")
proc = subprocess.Popen([exe, "--plain", "--window", "--volume", "0", "--port", str(args.port), clip],
                        env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)


def send(cmd, wait=6.0):
    # The WHOLE reply, however slowly it comes: an ARM build machine took
    # longer than a second and a half to answer STATUS, and a check that gave
    # up reported a Mini that was playing as one that was not.
    with socket.create_connection(("127.0.0.1", args.port), timeout=5) as s:
        s.sendall((cmd + "\n").encode())
        s.settimeout(wait)
        data = b""
        try:
            while True:
                chunk = s.recv(65536)
                if not chunk:
                    break
                data += chunk
                if cmd != "STATUS" or data.count(b"\n") >= 3:
                    break
        except socket.timeout:
            pass
        return data.decode(errors="replace")


ok = False
try:
    answered = False
    for _ in range(120):
        try:
            send("PING", 2.0)
            answered = True
            break
        except OSError:
            if proc.poll() is not None:
                break
            time.sleep(0.5)
    if not answered:
        log.flush()
        print(open(os.path.join(work, "mini.log"), errors="replace").read()[-2000:])
        sys.exit("FAIL: the Mini download never answered (exit %s)" % proc.poll())
    send("TAKE 1")
    time.sleep(2.0)
    status = send("STATUS")
    shown = int((re.search(r"frames_shown=(\d+)", status) or [0, "0"])[1])
    playing = "status=Playing" in status
    print("Mini from %s: %s, %d frames shown" % (os.path.basename(args.archive), "playing" if playing else "NOT playing", shown))
    # PROOF IT PLAYS, not a speed test: a build machine with no GPU ran this
    # at half a frame a second. Playing, frames decoded and at least one on
    # screen says the download's libraries work; speed is measured elsewhere.
    decoded = int((re.search(r"decoded=(\d+)", status) or [0, "0"])[1])
    ok = playing and decoded >= 5 and shown >= 1
    if not ok:
        # dur says whether the probe found the clip, decoded whether the
        # decoder produced anything, frames_shown whether any reached the screen.
        print("STATUS:", " ".join(f for f in status.split() if f.split("=")[0] in
              ("status", "pos", "dur", "decoded", "decode_fps", "frames_shown", "media_fps", "raster", "window")))
    send("QUIT")
finally:
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
    log.close()
if not ok:
    print(open(os.path.join(work, "mini.log"), errors="replace").read()[-2000:])
    # WHICH HALF FAILED: the download's own ffmpeg decoding the clip says
    # whether the libraries can decode at all; a second run with hardware
    # decoding off says whether Mini's fall-back from a missing hardware
    # decoder is what broke.
    bundled = [os.path.join(r, f) for r, _d, fs in os.walk(top) for f in fs if f in ("ffmpeg", "ffmpeg.exe")]
    if bundled:
        dec = subprocess.run([bundled[0], "-v", "error", "-i", clip, "-f", "null", "-"],
                             capture_output=True, text=True)
        print("the download's ffmpeg decoding the clip: exit %d %s" % (dec.returncode, dec.stderr[-600:]))
    env2 = dict(env, DECKBOY_NO_HW_DECODE="1")
    log2 = open(os.path.join(work, "mini-nohw.log"), "w")
    proc2 = subprocess.Popen([exe, "--plain", "--window", "--volume", "0", "--port", str(args.port + 1), clip],
                             env=env2, stdin=subprocess.DEVNULL, stdout=log2, stderr=subprocess.STDOUT)
    args.port += 1
    try:
        for _ in range(60):
            try:
                send("PING")
                break
            except OSError:
                time.sleep(0.25)
        send("TAKE 1")
        time.sleep(2.5)
        st2 = send("STATUS")
        print("with hardware decoding off: %s, %s frames shown" %
              ("playing" if "status=Playing" in st2 else "NOT playing",
               (re.search(r"frames_shown=(\d+)", st2) or [0, "0"])[1]))
        send("QUIT")
    except OSError as e:
        print("with hardware decoding off: no answer (%s)" % e)
    finally:
        try:
            proc2.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc2.kill()
        log2.close()
    print(open(os.path.join(work, "mini-nohw.log"), errors="replace").read()[-1200:])
    sys.exit("FAIL: the Mini download did not play a clip")
print("ok")
