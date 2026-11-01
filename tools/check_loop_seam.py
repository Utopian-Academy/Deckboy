#!/usr/bin/env python3
"""Is a looping clip seamless? Picture and sound, measured at the seam.

WHY THIS EXISTS. A loop used to restart the decoders: every pass ended with
the queued sound thrown away and the file opened again, so a looping clip had
a held picture and a gap of silence at every seam. Nothing measured it -- the
pacing checks never look at the moment of the loop, and a cadence histogram
counts it as one long frame among thousands.

This builds a clip in which every frame carries its own number as a 16-bit
bar code across the top, with a 1 kHz tone that fits the pass to the sample
(an exact number of cycles), loops it, records the output, and reads the
recording back:

  picture  every recorded frame's number. Seamless is ..., last, 0, 1, ...
           with one recorded frame per clip frame: no repeat, no skip at the
           seam beyond what the rest of the recording shows.
  sound    an unbroken sine. A second-order predictor of a sine is exact, so
           any cut, pad, gap or click is a spike in its residual.

    python3 tools/check_loop_seam.py --exe ./build/Deckboy [--rate 60]
        [--pass-seconds 2] [--passes 5] [--keep]

Records at the clip's rate. Exit 0 when every seam is clean.
"""
import argparse
import glob
import math
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deckboy_testroot  # noqa: E402

WIDTH, HEIGHT = 640, 360
BITS = 16
BAR_H = 40
TONE_HZ = 1000.0
RATE = 48000


def send(port, command, timeout=5.0):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall((command + "\n").encode())
        try:
            return sock.recv(65536).decode(errors="replace").strip()
        except OSError:
            return ""


def build_clip(path, rate, seconds):
    """Frame number as 16 bars of 40 px (white = 1, LSB on the left) over a
    moving test card; a 1 kHz tone, PCM so the pass is exact to the sample."""
    lum = ("if(lt(Y,%d),255*mod(floor(N/pow(2,floor(X/%d))),2),"
           "128+60*sin(2*PI*(X/160+N/%s)))" % (BAR_H, WIDTH // BITS, rate))
    subprocess.run([
        "ffmpeg", "-v", "error", "-y",
        "-f", "lavfi", "-i", "color=c=gray:s=%dx%d:r=%s:d=%s" % (WIDTH, HEIGHT, rate, seconds),
        "-f", "lavfi", "-i", "sine=frequency=%g:sample_rate=%d:duration=%s" % (TONE_HZ, RATE, seconds),
        "-filter_complex", "[0:v]format=gray,geq=lum='%s',format=yuv420p[v]" % lum,
        "-map", "[v]", "-map", "1:a",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "12", "-g", str(int(round(float(rate)))),
        "-c:a", "pcm_s16le", path,
    ], check=True)


def frame_numbers(path):
    """The number in each recorded frame (None where the bars do not read)."""
    raw = subprocess.run([
        "ffmpeg", "-v", "error", "-i", path, "-vf",
        "scale=%d:%d:flags=neighbor,format=gray" % (WIDTH, HEIGHT),
        "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
    size = WIDTH * HEIGHT
    numbers = []
    for off in range(0, len(raw) - size + 1, size):
        frame = raw[off:off + size]
        row = (BAR_H // 2) * WIDTH
        value, ok = 0, True
        for bit in range(BITS):
            x = bit * (WIDTH // BITS) + WIDTH // BITS // 2
            px = frame[row + x]
            if px > 190:
                value |= 1 << bit
            elif px > 64:
                ok = False
        numbers.append(value if ok else None)
    return numbers


def audio_samples(path):
    raw = subprocess.run([
        "ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(RATE),
        "-f", "s16le", "-"], capture_output=True, check=True).stdout
    return struct.unpack("<%dh" % (len(raw) // 2), raw)


def sound_breaks(samples):
    """Where the sine is broken: spikes in the residual of x[n] - 2cos(w)x[n-1]
    + x[n-2], which is zero for an unbroken sine of frequency w. Also runs of
    near-silence. Returns (spikes, silent_ms, typical_amplitude)."""
    if len(samples) < RATE // 2:
        return [], 0.0, 0.0
    c = 2.0 * math.cos(2.0 * math.pi * TONE_HZ / RATE)
    amp = sorted(abs(s) for s in samples[len(samples) // 4: len(samples) // 4 + RATE])[int(RATE * 0.99)]
    resid = [abs(samples[n] - c * samples[n - 1] + samples[n - 2]) for n in range(2, len(samples))]
    floor = sorted(resid)[int(len(resid) * 0.99)]
    threshold = max(floor * 4.0, amp * 0.08)
    spikes = []
    last = -RATE
    for n, r in enumerate(resid):
        if r > threshold and n - last > RATE // 100:
            spikes.append((n + 2) / RATE)
            last = n
    window = RATE // 500  # 2 ms
    silent = 0
    for w in range(0, len(samples) - window, window):
        peak = max(abs(s) for s in samples[w:w + window])
        if peak < amp * 0.25:
            silent += 1
    return spikes, silent * 2.0, amp


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--rate", default="60", help="clip and recording frame rate")
    parser.add_argument("--pass-seconds", type=float, default=2.0)
    parser.add_argument("--passes", type=int, default=5, help="seams to record, about")
    parser.add_argument("--port", type=int, default=5612)
    parser.add_argument("--keep", action="store_true")
    parser.add_argument("--in", dest="trim_in", type=float, default=0.0,
                        help="cue in-point, seconds: the loop goes round from here")
    parser.add_argument("--out", dest="trim_out", type=float, default=0.0,
                        help="cue out-point, seconds (0 = the end of the clip)")
    parser.add_argument("--loop-count", type=int, default=0,
                        help="repeats: the clip must play this many times, then stop")
    parser.add_argument("--audio-only", action="store_true",
                        help="a sound file with no picture (a music bed)")
    parser.add_argument("--app-arg", action="append", default=[],
                        help="extra Deckboy argument (repeatable), e.g. --no-inproc-decode")
    args = parser.parse_args()
    args.exe = os.path.abspath(args.exe)
    for tool in ("ffmpeg", "ffprobe"):
        if not shutil.which(tool):
            sys.exit("%s is not on PATH" % tool)

    rate = float(args.rate)
    clip_frames = int(round(args.pass_seconds * rate))
    first_frame = int(round(args.trim_in * rate))
    last_end = int(round(args.trim_out * rate)) if args.trim_out > 0 else clip_frames
    pass_frames = last_end - first_frame
    root = tempfile.mkdtemp(prefix="deckboy-loopseam-")
    os.makedirs(os.path.join(root, "data"), exist_ok=True)
    deckboy_testroot.populate(root)
    clip = os.path.join(root, "loopclip.wav" if args.audio_only else "loopclip.mkv")
    if args.audio_only:
        print("building a %.2f s sound loop..." % args.pass_seconds)
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                        "sine=frequency=%g:sample_rate=%d:duration=%s" % (TONE_HZ, RATE, args.pass_seconds),
                        "-c:a", "pcm_s16le", clip], check=True)
    else:
        print("building a %d-frame loop at %s fps (pass: frames %d-%d)..."
              % (clip_frames, args.rate, first_frame, last_end - 1))
        build_clip(clip, args.rate, args.pass_seconds)

    env = dict(os.environ)
    env["DECKBOY_ROOT"] = root
    env["DECKBOY_COMPANION_PORT"] = str(args.port)
    log = open(os.path.join(root, "log.txt"), "w")
    proc = subprocess.Popen([args.exe, *args.app_arg, "--import", clip], env=env,
                            cwd=os.path.dirname(args.exe) or ".",
                            stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(120):
            try:
                send(args.port, "HELP", timeout=1.0)
                break
            except OSError:
                time.sleep(0.5)
        send(args.port, "OUT on")
        send(args.port, "RECFORMAT %dx%d %s" % (WIDTH, HEIGHT, args.rate))
        send(args.port, "SELECT 1")
        if args.trim_in > 0:
            send(args.port, "IN %g" % args.trim_in)
        if args.trim_out > 0:
            send(args.port, "OUT %g" % args.trim_out)
        send(args.port, "LOOP ON")
        if args.loop_count > 0:
            send(args.port, "LOOPCOUNT %d" % args.loop_count)
        if args.loop_count > 0:
            # Every pass on the recording, the first included, to count them.
            send(args.port, "RECORD on")
            time.sleep(1.0)
            send(args.port, "TAKE")
            time.sleep(pass_frames / rate * (args.loop_count + 1.5))
        else:
            send(args.port, "TAKE")
            time.sleep(2.5)
            send(args.port, "RECORD on")
            time.sleep(args.pass_seconds * args.passes + 0.5)
        send(args.port, "RECORD off")
        time.sleep(4.0)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()

    files = sorted(glob.glob(os.path.join(root, "data", "recordings", "*.*")), key=os.path.getmtime)
    if not files:
        print("no recording was written -- see %s" % os.path.join(root, "log.txt"))
        return 1
    rec = files[-1]

    if args.audio_only:
        numbers = []
    else:
        numbers = frame_numbers(rec)
    unread = sum(1 for n in numbers if n is None)
    seq = [n for n in numbers if n is not None]
    print("recorded frames: %d (bar code unreadable in %d)" % (len(numbers), unread))
    if not args.audio_only and len(seq) < pass_frames:
        print("too few readable frames to judge -- check the recording: %s" % rec)
        return 1

    # JUDGED AGAINST THE RECORDER ITSELF. The recorder samples the output on
    # its own clock, so even a perfect picture records with the odd repeat and
    # skip: a recorded frame landing twice on one output frame, then missing
    # the next. So each seam is judged by the longest run of one repeated frame
    # and the biggest skip within four recorded frames of it, against the same
    # two numbers everywhere away from the seams. A restart -- the last frame
    # held while the file reopens, then a jump past the first frames -- stands
    # far outside what the recorder does on its own.
    def window_stats(window):
        longest, run, skip = 1, 1, 0
        for j in range(1, len(window)):
            step = (window[j] - window[j - 1]) % pass_frames
            run = run + 1 if step == 0 else 1
            longest = max(longest, run)
            if step > 1:
                skip = max(skip, step - 1)
        return longest, skip

    seams = [i for i in range(1, len(seq)) if seq[i] < seq[i - 1] - pass_frames // 2]
    # Repeats: exactly N passes, so N-1 seams, and then the picture stops.
    count_ok = True
    if args.loop_count > 0 and not args.audio_only:
        moving_after = 0
        if seams:
            tail = seq[seams[-1]:]
            for j in range(1, len(tail)):
                if tail[j] != tail[j - 1]:
                    moving_after += 1
        played = len(seams) + 1
        count_ok = played == args.loop_count and moving_after <= pass_frames + 2
        print("repeats: played %d of %d passes, then %s" % (
            played, args.loop_count, "stopped" if moving_after <= pass_frames + 2 else "kept going"))
    near_seam = set()
    for i in seams:
        near_seam.update(range(i - 8, i + 9))
    base_run, base_skip = 1, 0
    for i in range(4, len(seq) - 4):
        if i in near_seam:
            continue
        run, skip = window_stats(seq[i - 4:i + 5])
        base_run, base_skip = max(base_run, run), max(base_skip, skip)
    print("recorder on its own (away from seams): longest repeat %d, biggest skip %d"
          % (base_run, base_skip))
    seam_reports = []
    for i in seams:
        if i < 4 or i + 4 >= len(seq):
            continue   # too near the start or end of the recording to judge
        window = seq[i - 4:i + 5]
        run, skip = window_stats(window)
        clean = run <= base_run and skip <= base_skip
        seam_reports.append((seq[i - 1], seq[i], run, skip, clean, window))
    print("seams judged: %d" % len(seam_reports))
    for last, first, run, skip, clean, window in seam_reports:
        print("  %4d -> %-4d longest repeat %d, biggest skip %d: %s   %s"
              % (last, first, run, skip, "clean" if clean else "GLITCH", window))

    samples = audio_samples(rec)
    # Not the first or last tenth of a second: the recording starting and
    # stopping is not the loop.
    edge = RATE // 10
    spikes, silent_ms, amp = sound_breaks(samples[edge:len(samples) - edge])
    print("sound: %.1f s, tone amplitude %d, breaks in the sine: %d, near-silent: %.0f ms"
          % (len(samples) / RATE, amp, len(spikes), silent_ms))
    if spikes:
        print("  breaks at: %s" % ", ".join("%.3f s" % (t + 0.1) for t in spikes[:12]))

    picture_ok = args.audio_only or (bool(seam_reports) and all(r[4] for r in seam_reports))
    picture_ok = picture_ok and count_ok
    sound_ok = amp > 1000 and not spikes and silent_ms < 2.0
    if args.loop_count > 0:
        sound_ok = True   # silence before the take and after the last repeat is the point
    print("\npicture at the seam: %s" % ("SEAMLESS" if picture_ok else "NOT SEAMLESS"))
    print("sound at the seam:   %s" % ("SEAMLESS" if sound_ok else "NOT SEAMLESS"))

    if args.keep:
        kept = os.path.join(os.getcwd(), os.path.basename(rec))
        shutil.copy2(rec, kept)
        print("kept: %s" % kept)
    shutil.rmtree(root, ignore_errors=True)
    return 0 if picture_ok and sound_ok else 1


if __name__ == "__main__":
    sys.exit(main())
