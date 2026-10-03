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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deckboy_testroot  # noqa: E402

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


def distinct_pictures(path):
    """How many visually distinct pictures the recording contains, sampled at
    one frame per second.

    Counting frames is not enough, and this is not hypothetical: the
    asynchronous readback once failed on EVERY frame while the caller served
    the previous picture, so recordings were a single still image with a
    perfect frame count, exact duration and a silent dropped-frame alarm.
    Every counter said the take was healthy; only the pixels disagreed.

    Downscaled and quantised before hashing, so lossy re-encoding of an
    unchanging picture does not read as motion -- comparing raw frames finds
    differences between identical pictures encoded at different GOP positions.
    """
    out = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path,
         "-vf", "fps=1,scale=32:18,format=gray",
         "-f", "rawvideo", "-"],
        capture_output=True).stdout
    size = 32 * 18
    seen = set()
    for i in range(0, len(out) - size + 1, size):
        seen.add(bytes(b & 0xF0 for b in out[i:i + size]))
    return len(seen)


def magenta_frames(path):
    """Inspect every decoded frame for an uninitialised YUV texture.

    Motion and cadence can both look healthy while alternating with a flat
    magenta frame. The generated test pattern never contains a flat magenta
    picture, so that colour is an unambiguous output failure for this fixture.
    """
    decoded = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-vf", "scale=32:18,format=rgb24",
         "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
    size = 32 * 18 * 3
    bad = 0
    for offset in range(0, len(decoded) - size + 1, size):
        frame = decoded[offset:offset + size]
        pink = sum(r > 180 and g < 100 and b > 180
                   for r, g, b in zip(frame[0::3], frame[1::3], frame[2::3]))
        bad += pink > 32 * 18 * 0.95
    return bad


def run_case(args, standard):
    width, height, rate = parse_standard(standard)
    root = args.root
    recordings = os.path.join(root, "data", "recordings")
    if os.path.commonpath([os.path.abspath(root), os.path.abspath(recordings)]) != os.path.abspath(root):
        raise ValueError("recordings must stay inside the isolated project root")
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
    proc = subprocess.Popen([args.exe, "--import", args.media], env=env,
                            cwd=os.path.dirname(args.exe) or ".",
                            stdout=log, stderr=subprocess.STDOUT)
    try:
        if not wait_for_control(args.port):
            return {"standard": standard, "error": "control port never came up"}
        # Silence first: this drives a real show application.
        send(args.port, "MASTERVOL 0")
        send(args.port, "OUTPUT on")
        send(args.port, "RECFORMAT %dx%d %s" % (width, height, rate))
        send(args.port, "SELECT 1")
        send(args.port, "TAKE")
        # WAIT for the deck to actually be playing before recording. A fixed
        # sleep records whatever is on screen at the time, and on a slow boot
        # that is the empty monitor -- which then reports as a frozen recording
        # and sends you hunting a bug that is not there. Seen once; that was
        # enough.
        deadline = time.time() + 20.0
        while time.time() < deadline:
            if 'status=Playing' in send(args.port, "STATUS"):
                break
            time.sleep(0.25)
        else:
            print("warning: deck never reported Playing; recording anyway")
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
        "distinct": distinct_pictures(files[-1]),
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
    parser.add_argument("--media", default="", help="clip to record (use a 4K one for 4K tests)")
    parser.add_argument("--fixture", choices=["main10"], help="generate a Main 10 BT.709 SDR motion clip")
    parser.add_argument("--reject-magenta", action="store_true", help="reject flat magenta frames")
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
    if not os.path.exists(args.exe):
        sys.exit("no binary at %s" % args.exe)
    deckboy_testroot.warn_if_stale(args.exe)
    if bool(args.media) == bool(args.fixture):
        parser.error("choose either --media or --fixture")
    if args.media:
        args.media = os.path.abspath(args.media)
    if args.media and not os.path.exists(args.media):
        sys.exit("no media at %s" % args.media)
    if not shutil.which("ffprobe"):
        sys.exit("ffprobe is not on PATH; it is what counts the frames")

    temporary = not args.root
    args.root = args.root or tempfile.mkdtemp(prefix="deckboy-ratecheck-")
    os.makedirs(os.path.join(args.root, "data"), exist_ok=True)
    deckboy_testroot.populate(args.root, verbose=True)
    print("project root: %s" % args.root)

    if args.fixture:
        args.media = os.path.join(args.root, "data", "main10-sdr.mkv")
        subprocess.run([
            "ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=25",
            "-f", "lavfi", "-i", "sine=frequency=1000:sample_rate=48000",
            "-t", str(args.seconds + args.settle + 20), "-c:v", "libx265", "-preset", "ultrafast",
            "-pix_fmt", "yuv420p10le", "-x265-params", "pools=2:frame-threads=2:log-level=error",
            "-color_range", "tv", "-colorspace", "bt709", "-color_trc", "bt709",
            "-color_primaries", "bt709", "-c:a", "pcm_s16le", args.media], check=True)
        args.reject_magenta = True

    results = []
    try:
        for standard in args.standard:
            result = run_case(args, standard)
            if args.reject_magenta and "error" not in result:
                files = glob.glob(os.path.join(args.root, "data", "recordings", "*"))
                result["magenta_frames"] = magenta_frames(max(files, key=os.path.getmtime))
            results.append(result)
            print(json.dumps(result), flush=True)
    finally:
        if temporary:
            shutil.rmtree(args.root, ignore_errors=True)

    print()
    print("%-16s %-12s %8s %8s %9s %7s %9s" %
          ("standard", "raster", "frames", "owed", "shortfall", "alarms",
           "distinct"))
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
        #   distinct  -- whether the PICTURE moved. A recording can score
        #                perfectly on every count above and still be one still
        #                frame repeated; that is not a hypothetical failure
        #                mode, it is what a broken readback actually produced.
        frozen = r.get("distinct", 99) < 3
        bad = (frozen or r["alarms"] > 0 or r.get("magenta_frames", 0) > 0 or
               r["shortfall"] > max(8, 0.05 * r["owed"]))
        failures += bad
        print("%-16s %-12s %8d %8d %9d %7d %9d  %s" %
              (r["standard"], r["raster"], r["frames"], r["owed"],
               r["shortfall"], r["alarms"], r.get("distinct", -1),
               "FROZEN" if frozen else ("FAIL" if bad else "ok")))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
