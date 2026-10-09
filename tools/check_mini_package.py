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
if args.archive.endswith(".zip"):
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
subprocess.run([args.ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=s=640x360:r=25:d=4",
                "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "4",
                "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", clip], check=True)

env = dict(os.environ, SDL_VIDEO_DRIVER=os.environ.get("SDL_VIDEO_DRIVER", "offscreen"), SDL_AUDIO_DRIVER="dummy")
log = open(os.path.join(work, "mini.log"), "w")
proc = subprocess.Popen([exe, "--plain", "--window", "--volume", "0", "--port", str(args.port), clip],
                        env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)


def send(cmd):
    with socket.create_connection(("127.0.0.1", args.port), timeout=5) as s:
        s.sendall((cmd + "\n").encode())
        s.settimeout(1.5)
        try:
            return s.recv(65536).decode(errors="replace")
        except socket.timeout:
            return ""


ok = False
try:
    for _ in range(60):
        try:
            send("PING")
            break
        except OSError:
            time.sleep(0.25)
    send("TAKE 1")
    time.sleep(2.5)
    status = send("STATUS")
    shown = int((re.search(r"frames_shown=(\d+)", status) or [0, "0"])[1])
    playing = "status=Playing" in status
    print("Mini from %s: %s, %d frames shown" % (os.path.basename(args.archive), "playing" if playing else "NOT playing", shown))
    ok = playing and shown > 10
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
