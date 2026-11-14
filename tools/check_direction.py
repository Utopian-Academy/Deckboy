"""DIRECTION: does a reversed or bounced cue play its frames backwards?

1. A 3-second clip at 30 fps whose every frame carries its own number as a
   bar code (check_loop_seam.build_clip).
2. In the app: DIRECTION REVERSE, then BOUNCE, then a TRIMMED reverse (IN 1,
   OUT 2). Each waits for its copy and reads the copy's frames back: they must
   be exactly the source's frames, backwards (bounce: forwards, then
   backwards without the two turnaround frames).
3. The reversed cue is taken and recorded: the numbers on the output must run
   downwards.

No window covers the desktop (DECKBOY_HIDE_WINDOWS) and the volume is 0.

    py -3 tools/check_direction.py [--exe build/windows/Release/Deckboy.exe]
"""
import argparse
import glob
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import deckboy_testroot  # noqa: E402
from check_loop_seam import build_clip, frame_numbers, send  # noqa: E402

RATE = 30
FRAMES = 90

failures = 0


def check(ok, name, detail=""):
    global failures
    if not ok:
        failures += 1
    print(("ok    " if ok else "FAIL  ") + name + ("" if ok else "   " + str(detail)[:300]), flush=True)


def wait_for_copy(port, timeout=180):
    end = time.time() + timeout
    last = ""
    while time.time() < end:
        last = send(port, "DIRECTION STATUS")
        if "copy=ready" in last:
            return last
        time.sleep(1.0)
    return last


def newest(root, pattern):
    files = sorted(glob.glob(os.path.join(root, "**", pattern), recursive=True), key=os.path.getmtime)
    return files[-1] if files else None


def frame_hashes(path):
    """One MD5 per decoded frame, in file order (ffmpeg's framemd5)."""
    out = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-map", "0:v:0", "-f", "framemd5", "-"],
                         capture_output=True, text=True).stdout
    return [line.split(",")[-1].strip() for line in out.splitlines() if line and not line.startswith("#")]


def lossless_check(exe, port):
    """A ProRes clip: its copy is the same packets backwards, not an encode."""
    root = tempfile.mkdtemp(prefix="deckboy-direction-prores-")
    os.makedirs(os.path.join(root, "data"), exist_ok=True)
    deckboy_testroot.populate(root)
    h264 = os.path.join(root, "numbers.mkv")
    build_clip(h264, str(RATE), FRAMES / RATE)
    clip = os.path.join(root, "numbers_prores.mov")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", h264, "-c:v", "prores_ks", "-profile:v", "3",
                    "-c:a", "pcm_s16le", clip], check=True)
    env = dict(os.environ, DECKBOY_ROOT=root, DECKBOY_COMPANION_PORT=str(port), DECKBOY_HIDE_WINDOWS="1")
    logpath = os.path.join(root, "log.txt")
    log = open(logpath, "w")
    proc = subprocess.Popen([exe, "--import", clip], env=env, cwd=os.path.dirname(exe),
                            stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(120):
            try:
                send(port, "HELP", timeout=1.0)
                break
            except OSError:
                time.sleep(0.5)
        send(port, "MASTERVOL 0")
        send(port, "SELECT 1")
        started = time.time()
        send(port, "DIRECTION REVERSE")
        status = wait_for_copy(port)
        took = time.time() - started
        check("copy=ready" in status, "ProRes: the reversed copy is made", status)
        send(port, "DIRECTION BOUNCE")
        status = wait_for_copy(port)
        check("copy=ready" in status, "ProRes: the bounced copy is made", status)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()
    text = open(logpath, encoding="utf-8", errors="replace").read()
    check(text.count("direction: lossless copy") == 2, "ProRes: both copies took the lossless path",
          [l for l in text.splitlines() if l.startswith("direction:")])
    rev = newest(root, "*_reverse_*.mov")
    bounce = newest(root, "*_bounce_*.mov")
    codec = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=codec_name",
                            "-of", "csv=p=0", rev], capture_output=True, text=True).stdout.strip() if rev else ""
    check(codec == "prores", "ProRes: the copy is still ProRes", codec)
    src = frame_hashes(clip)
    got = frame_hashes(rev) if rev else []
    check(got == src[::-1], "ProRes: the reversed copy is the source's frames, byte for byte, backwards",
          "%d vs %d frames" % (len(got), len(src)))
    got = frame_hashes(bounce) if bounce else []
    check(got == src + src[-2:0:-1], "ProRes: the bounce is the same frames there and back",
          "%d vs %d frames" % (len(got), len(src) * 2 - 2))
    seen = frame_numbers(rev) if rev else []
    check(seen == list(range(FRAMES - 1, -1, -1)), "ProRes: the reversed copy reads 89..0", seen[:6])
    snd = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries",
                          "stream=codec_name,duration", "-of", "csv=p=0", rev], capture_output=True, text=True).stdout.strip() if rev else ""
    check(snd.startswith("pcm_s16le") and abs(float(snd.split(",")[-1]) - FRAMES / RATE) < 0.05,
          "ProRes: the copy carries the reversed sound, the same length", snd)
    print("      the lossless reverse took %.1f s" % took)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(HERE, "..", "build", "windows", "Release", "Deckboy.exe"))
    ap.add_argument("--port", type=int, default=5623)
    args = ap.parse_args()
    root = tempfile.mkdtemp(prefix="deckboy-direction-")
    os.makedirs(os.path.join(root, "data"), exist_ok=True)
    deckboy_testroot.populate(root)
    clip = os.path.join(root, "numbers.mkv")
    build_clip(clip, str(RATE), FRAMES / RATE)
    source = frame_numbers(clip)
    check(source == list(range(FRAMES)), "the source clip reads 0..%d" % (FRAMES - 1), source[:12])

    env = dict(os.environ, DECKBOY_ROOT=root, DECKBOY_COMPANION_PORT=str(args.port), DECKBOY_HIDE_WINDOWS="1")
    log = open(os.path.join(root, "log.txt"), "w")
    exe = os.path.abspath(args.exe)
    proc = subprocess.Popen([exe, "--import", clip], env=env, cwd=os.path.dirname(exe),
                            stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(120):
            try:
                send(args.port, "HELP", timeout=1.0)
                break
            except OSError:
                time.sleep(0.5)
        send(args.port, "MASTERVOL 0")
        send(args.port, "SELECT 1")
        status = send(args.port, "DIRECTION STATUS")
        check("direction=FORWARD copy=none" in status, "a new cue plays forwards", status)
        check("ERR" in send(args.port, "DIRECTION SIDEWAYS"), "a direction that does not exist is refused")

        # Reverse.
        check(send(args.port, "DIRECTION REVERSE").startswith("OK"), "DIRECTION REVERSE is accepted")
        status = wait_for_copy(args.port)
        check("copy=ready" in status, "the reversed copy is made", status)
        copy = newest(root, "*_reverse_*.mov")
        got = frame_numbers(copy) if copy else []
        check(got == list(range(FRAMES - 1, -1, -1)), "the reversed copy is every frame, backwards",
              "%d frames: %s ... %s" % (len(got), got[:6], got[-6:]))

        # Bounce: forwards, then backwards without the two turnaround frames.
        check(send(args.port, "DIRECTION BOUNCE").startswith("OK"), "DIRECTION BOUNCE is accepted")
        status = wait_for_copy(args.port)
        print("      " + status)
        check("copy=ready" in status, "the bounced copy is made", status)
        copy = newest(root, "*_bounce_*.mov")
        got = frame_numbers(copy) if copy else []
        want = list(range(FRAMES)) + list(range(FRAMES - 2, 0, -1))
        check(got == want, "the bounced copy is there and back, no frame twice at a turn",
              "%d frames: %s ... %s" % (len(got), got[:4], got[86:96]))
        check(abs(float(status.split("seconds=")[-1].split()[0]) - len(want) / RATE) < 0.05 if "seconds=" in status else False,
              "the cue's length is the bounce's length", status)

        # A trimmed reverse: frames 30..59, backwards.
        send(args.port, "DIRECTION REVERSE")
        send(args.port, "IN 1")
        send(args.port, "OUT 2")
        time.sleep(0.5)
        status = send(args.port, "DIRECTION STATUS")
        check("copy=ready" not in status, "a new trim makes the old copy stale", status)
        status = wait_for_copy(args.port)
        print("      " + status)
        check("copy=ready" in status, "the trimmed copy is made", status)
        copy = newest(root, "*_reverse_*1000_2000.mov")
        got = frame_numbers(copy) if copy else []
        check(got == list(range(59, 29, -1)), "the trimmed copy is frames 59..30",
              "%s: %d frames: %s ... %s" % (copy, len(got), got[:6], got[-6:]))

        # Played and recorded: the numbers on the output run downwards.
        send(args.port, "OUTPUT ON")
        send(args.port, "VIDEO 640x360")
        send(args.port, "RECFORMAT 640x360 30")
        send(args.port, "RECCODEC prores_proxy")
        send(args.port, "LOOP ON")
        send(args.port, "TAKE")
        time.sleep(2.0)
        print("      " + " ".join(send(args.port, "STATUS").split())[:600])
        send(args.port, "RECORD on")
        time.sleep(3.0)
        send(args.port, "RECORD off")
        time.sleep(4.0)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()
    rec = newest(os.path.join(root, "data", "recordings"), "*.*")
    seen = [n for n in (frame_numbers(rec) if rec else []) if n is not None and 30 <= n <= 59]
    # Looping: down from 59 to 30, a jump back up to the top, and down again.
    # A small step UP anywhere would be the cue playing forwards.
    steps = [b - a for a, b in zip(seen, seen[1:]) if b != a]
    down = sum(1 for s in steps if s < 0)
    forwards = [s for s in steps if 0 < s < 20]
    check(len(seen) >= 30 and down >= 20 and not forwards,
          "on the output the trimmed cue runs 59 down to 30, round and round",
          "%d frames, %d steps down, forward steps %s: %s" % (len(seen), down, forwards[:8], seen[:50]))
    lossless_check(exe, args.port + 1)
    print("direction: %s   (evidence: %s)" % ("FAIL (%d)" % failures if failures else "ok", root))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
