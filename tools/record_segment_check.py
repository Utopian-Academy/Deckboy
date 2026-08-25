#!/usr/bin/env python3
"""Check that a SEGMENTED recording carries continuous timecode.

Segmentation exists for long takes, and long takes are exactly the ones that get
conformed against a running order. If every segment restarts at the take's start
timecode they cannot be laid end to end, and nothing about the files themselves
says so -- each one looks perfectly correct on its own.

This records one take with a small size cap so it rolls several times, then
reads the tmcd track out of each segment and checks that segment N starts where
segment N-1 ended.

    python3 tools/record_segment_check.py --exe ./build/Deckboy --media clip.mp4
    python3 tools/record_segment_check.py --exe ./build/Deckboy --media clip.mp4 \
        --start 10:00:00:00 --standard 1920x1080@29.97 --seconds 40

Note that the cap is in megabytes of ENCODED video, so static content rolls far
more slowly than moving content -- a take of colour bars can sit under a 3 MB
cap for a minute. If you get one segment, lower --cap-mb rather than assuming
segmentation is broken.
"""

import argparse
import glob
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time


def send(port, command, timeout=5.0):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall((command + "\n").encode())
        try:
            return sock.recv(65536).decode(errors="replace").strip()
        except OSError:
            return ""


def timecode_of(path):
    """The tmcd track's start timecode, or None. It lives in STREAM tags, not
    format tags -- probing the format returns nothing and looks like absence."""
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "stream_tags=timecode",
         "-of", "json", path], capture_output=True, text=True).stdout
    try:
        for stream in json.loads(out).get("streams", []):
            tc = stream.get("tags", {}).get("timecode")
            if tc:
                return tc
    except ValueError:
        pass
    return None


def frames_of(path):
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
         "-show_entries", "stream=nb_read_frames", "-of", "default=nw=1:nk=1",
         path], capture_output=True, text=True).stdout.strip()
    try:
        return int(out)
    except ValueError:
        return 0


def tc_to_frames(tc, rate):
    """SMPTE timecode -> frame count. ';' marks drop-frame."""
    drop = ";" in tc
    hh, mm, ss, ff = (int(x) for x in tc.replace(";", ":").split(":"))
    nominal = int(round(rate))
    frames = hh * nominal * 3600 + mm * nominal * 60 + ss * nominal + ff
    if drop:
        drop_per_minute = 2 * (nominal // 30)
        total_minutes = hh * 60 + mm
        frames -= drop_per_minute * (total_minutes - total_minutes // 10)
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--media", required=True)
    parser.add_argument("--standard", default="1920x1080@30", help="WxH@fps")
    parser.add_argument("--start", default="10:00:00:00", help="start timecode")
    parser.add_argument("--df", default="ndf", choices=["ndf", "df", "auto"])
    parser.add_argument("--cap-mb", type=int, default=1, help="segment size cap")
    parser.add_argument("--seconds", type=float, default=40.0)
    parser.add_argument("--port", type=int, default=5601)
    args = parser.parse_args()

    args.exe = os.path.abspath(args.exe)
    args.media = os.path.abspath(args.media)
    if not shutil.which("ffprobe"):
        sys.exit("ffprobe is not on PATH; it is what reads the timecode back")

    raster, _, rate_text = args.standard.partition("@")
    width, _, height = raster.lower().partition("x")
    rate = float(rate_text)

    root = tempfile.mkdtemp(prefix="deckboy-segcheck-")
    os.makedirs(os.path.join(root, "data"), exist_ok=True)
    env = dict(os.environ)
    env["DECKBOY_ROOT"] = root
    env["DECKBOY_COMPANION_PORT"] = str(args.port)

    log = open(os.path.join(root, "log.txt"), "w")
    proc = subprocess.Popen([args.exe, args.media], env=env,
                            cwd=os.path.dirname(args.exe) or ".",
                            stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(120):
            try:
                send(args.port, "HELP", timeout=1.0)
                break
            except OSError:
                time.sleep(0.5)
        send(args.port, "MASTERVOL 0")
        send(args.port, "OUT on")
        send(args.port, "RECFORMAT %s %s" % (raster, rate_text))
        send(args.port, "RECTC %s %s" % (args.start, args.df))
        send(args.port, "RECSEGMENT 0 %d" % args.cap_mb)
        send(args.port, "SELECT 1")
        send(args.port, "TAKE")
        time.sleep(3.0)
        send(args.port, "RECORD on")
        time.sleep(args.seconds)
        send(args.port, "RECORD off")
        time.sleep(4.0)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()

    files = sorted(glob.glob(os.path.join(root, "data", "recordings", "*.*")),
                   key=os.path.getmtime)
    print("%-46s %-14s %8s %10s" % ("segment", "timecode", "frames", "expected"))
    failures = 0
    expected = None
    for path in files:
        tc = timecode_of(path)
        n = frames_of(path)
        note = ""
        if tc is None:
            note = "  NO TIMECODE"
            failures += 1
        elif expected is not None:
            # One frame of slack: the roll happens between frames, so a segment
            # boundary can legitimately land either side of a single frame.
            if abs(tc_to_frames(tc, rate) - expected) > 1:
                note = "  DISCONTINUOUS"
                failures += 1
        print("%-46s %-14s %8d %10s%s" % (
            os.path.basename(path), tc or "(none)", n,
            "-" if expected is None else str(expected), note))
        if tc is not None:
            expected = tc_to_frames(tc, rate) + n

    shutil.rmtree(root, ignore_errors=True)
    if len(files) < 2:
        print("\nonly %d segment(s): the cap was never reached, so continuity was "
              "not exercised. Lower --cap-mb or use moving content." % len(files))
        return 1
    print("\n%d segments, %s" % (len(files), "FAIL" if failures else "continuous"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
