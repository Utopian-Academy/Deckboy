#!/usr/bin/env python3
"""Measure whether a recording actually contains the frames it promises.

A recording that runs short looks perfectly healthy until an editor opens it,
so the only honest test is to record a known duration and count what landed in
the file. This drives a real Deckboy over the Companion control port, records
for N seconds at a given standard, and reports frames delivered against frames
owed -- plus whether the app's own dropped-frame alarm fired.

It runs against an ISOLATED project root, so it never touches the operator's
show file or their recordings.

    python3 tools/record_rate_check.py --exe ./build/Deckboy --media clip.mp4
    python3 tools/record_rate_check.py --exe ./build/Deckboy --media clip.mp4 \
        --standard 3840x2160@60 --standard 1920x1080@59.94 --seconds 20

Add --renderer gpu (or direct3d11, metal, opengl) to pin the output window's
backend, and --readback sync to force the portable synchronous readback -- that
is how one platform's behaviour gets measured from another platform's desk.

Run each standard twice if a number looks marginal: the first take on a cold
machine pays for decoder and encoder start-up, and that lands in the shortfall
column rather than in the app's alarm.
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

CONTROL_TIMEOUT = 5.0


def send(port, command, timeout=CONTROL_TIMEOUT):
    """One Companion command. Each opens its own connection, as Companion does."""
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall((command + "\n").encode())
        try:
            return sock.recv(65536).decode(errors="replace").strip()
        except OSError:
            return ""


def wait_for_control(port, seconds=60.0):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            send(port, "HELP", timeout=1.0)
            return True
        except OSError:
            time.sleep(0.5)
    return False


def parse_standard(text):
    """"3840x2160@59.94" -> (3840, 2160, "59.94")."""
    raster, _, rate = text.partition("@")
    width, _, height = raster.lower().partition("x")
    return int(width), int(height), (rate or "program")


def probe(path):
    out = subprocess.run(
        ["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
         "-show_entries", "stream=width,height,r_frame_rate,nb_read_frames",
         "-of", "json", path],
        capture_output=True, text=True).stdout
    try:
        stream = json.loads(out)["streams"][0]
    except (ValueError, KeyError, IndexError):
        return None
    num, _, den = stream.get("r_frame_rate", "0/1").partition("/")
    return {
        "width": stream.get("width"),
        "height": stream.get("height"),
        "fps": float(num) / float(den or 1),
        "frames": int(stream.get("nb_read_frames", 0)),
    }


def run_case(args, standard):
    width, height, rate = parse_standard(standard)
    root = args.root
    recordings = os.path.join(root, "data", "recordings")
    shutil.rmtree(recordings, ignore_errors=True)

    env = dict(os.environ)
    env["DECKBOY_ROOT"] = root
    env["DECKBOY_COMPANION_PORT"] = str(args.port)
    if args.renderer:
        env["DECKBOY_OUTPUT_RENDERER"] = args.renderer
    if args.readback:
        env["DECKBOY_EGRESS_READBACK"] = args.readback
    if args.bench:
        env["DECKBOY_EGRESS_BENCH"] = "1"

    log_path = os.path.join(root, "case.log")
    log = open(log_path, "w")
    # Media arrives as an argument -- there is no IMPORT verb on the control
    # port, and the usage line takes media paths directly.
    proc = subprocess.Popen([args.exe, args.media], env=env,
                            cwd=os.path.dirname(args.exe) or ".",
                            stdout=log, stderr=subprocess.STDOUT)
    try:
        if not wait_for_control(args.port):
            return {"standard": standard, "error": "control port never came up"}
        # Silence first: this drives a real show application.
        send(args.port, "MASTERVOL 0")
        send(args.port, "OUT on")
        send(args.port, "RECFORMAT %dx%d %s" % (width, height, rate))
        send(args.port, "SELECT 1")
        send(args.port, "TAKE")
        time.sleep(args.settle)
        send(args.port, "RECORD on")
        started = time.time()
        time.sleep(args.seconds)
        send(args.port, "RECORD off")
        elapsed = time.time() - started
        time.sleep(3.0)   # let the muxer finalise
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()

    text = open(log_path, errors="replace").read()
    # "record-drop:" is the stderr line. Counting only the operator-facing
    # wording was a mistake that made this column ALWAYS zero -- the toast and
    # the show log never touch stdout, so the harness was reporting "no alarms"
    # for takes that were plainly short. Count both, and treat the show log in
    # the isolated root as a third witness.
    alarms = text.count("record-drop:")
    show_log = os.path.join(root, "deckboy-show.log")
    if os.path.exists(show_log):
        alarms += open(show_log, errors="replace").read().count("RECORD DROP")
    files = sorted(glob.glob(os.path.join(recordings, "*.*")), key=os.path.getmtime)
    if not files:
        return {"standard": standard, "error": "no file written", "alarms": alarms}

    info = probe(files[-1])
    if not info:
        return {"standard": standard, "error": "unreadable file", "alarms": alarms}
    owed = int(round(info["fps"] * elapsed))
    return {
        "standard": standard,
        "file": os.path.basename(files[-1]),
        "raster": "%dx%d" % (info["width"], info["height"]),
        "fps": round(info["fps"], 3),
        "frames": info["frames"],
        "owed": owed,
        "shortfall": owed - info["frames"],
        "alarms": alarms,
        "seconds": round(elapsed, 2),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="path to the Deckboy binary")
    parser.add_argument("--media", required=True, help="clip to record (use a 4K one for 4K tests)")
    parser.add_argument("--standard", action="append", default=[],
                        help="WxH@fps, repeatable (default: a 4K/HD sweep)")
    parser.add_argument("--seconds", type=float, default=20.0, help="take length")
    parser.add_argument("--settle", type=float, default=3.0,
                        help="seconds of playback before RECORD, so decode is warm")
    parser.add_argument("--port", type=int, default=5599)
    parser.add_argument("--renderer", default="", help="DECKBOY_OUTPUT_RENDERER value")
    parser.add_argument("--readback", default="", choices=["", "sync"],
                        help="force the portable synchronous readback")
    parser.add_argument("--bench", action="store_true", help="print per-frame readback costs")
    parser.add_argument("--root", default="", help="isolated project root (default: a temp dir)")
    args = parser.parse_args()

    if not args.standard:
        args.standard = ["3840x2160@60", "3840x2160@50", "3840x2160@30",
                         "1920x1080@60", "1920x1080@59.94"]
    args.exe = os.path.abspath(args.exe)
    args.media = os.path.abspath(args.media)
    if not os.path.exists(args.exe):
        sys.exit("no binary at %s" % args.exe)
    if not os.path.exists(args.media):
        sys.exit("no media at %s" % args.media)
    if not shutil.which("ffprobe"):
        sys.exit("ffprobe is not on PATH; it is what counts the frames")

    temporary = not args.root
    args.root = args.root or tempfile.mkdtemp(prefix="deckboy-ratecheck-")
    os.makedirs(os.path.join(args.root, "data"), exist_ok=True)
    print("project root: %s" % args.root)

    results = []
    try:
        for standard in args.standard:
            result = run_case(args, standard)
            results.append(result)
            print(json.dumps(result), flush=True)
    finally:
        if temporary:
            shutil.rmtree(args.root, ignore_errors=True)

    print()
    print("%-16s %-12s %8s %8s %9s %7s" %
          ("standard", "raster", "frames", "owed", "shortfall", "alarms"))
    failures = 0
    for r in results:
        if "error" in r:
            print("%-16s %s" % (r["standard"], r["error"]))
            failures += 1
            continue
        # Two different questions, deliberately kept apart:
        #
        #   alarms    -- the APP's verdict. Its pacer counts from the first
        #                frame it wrote, so this is "did the capture keep up".
        #   shortfall -- the OPERATOR's. This counts from RECORD on to RECORD
        #                off, so it also carries the encoder's start-up, which
        #                at 4K is a chunk of a second before any frame exists.
        #
        # An alarm is always a failure. A shortfall on its own is only a
        # failure when it is too big to be start-up and round-trip.
        bad = r["alarms"] > 0 or r["shortfall"] > max(8, 0.05 * r["owed"])
        failures += bad
        print("%-16s %-12s %8d %8d %9d %7d  %s" %
              (r["standard"], r["raster"], r["frames"], r["owed"],
               r["shortfall"], r["alarms"], "FAIL" if bad else "ok"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
