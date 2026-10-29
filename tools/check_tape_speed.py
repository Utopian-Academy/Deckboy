#!/usr/bin/env python3
"""A fade cue ramps a deck's SPEED like tape: the pitch follows.

The Audio board's first item, speed half. A 1 kHz tone cue is taken and a
fade cue snaps the deck to 0.5x: what the device is given must be 500 Hz at
the same level, with no 1 kHz left. A two-second ramp to 2x must end at
2 kHz, and taking the tone again starts it at its own speed. Then a looping
video at 0.5x: its playhead must move one second in two, which is the picture
clock running at the speed the sound plays at.

What the device is given is read from DECKBOY_LIMITER_CAPTURE, with the
limiter on at 0 dBFS (nothing here reaches it) only for the capture.

    py -3 tools/check_tape_speed.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import math
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

fails = []


def note(name, ok, info=""):
    print("  %-62s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def tone_db(samples, rate, hz):
    """One pure tone's level in a channel, dBFS of its peak (Goertzel)."""
    n = len(samples)
    if n == 0:
        return -200.0
    coeff = 2.0 * math.cos(2.0 * math.pi * hz / rate)
    s1 = s2 = 0.0
    for x in samples:
        s1, s2 = x + coeff * s1 - s2, s1
    power = max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)
    amplitude = 2.0 * math.sqrt(power) / n
    return 20.0 * math.log10(amplitude) if amplitude > 1e-10 else -200.0


def left_channel(path, start_byte, seconds):
    data = Path(path).read_bytes()
    if len(data) < 12 or data[:4] != b"DBLC":
        return [], 0
    channels, rate = struct.unpack("<ii", data[4:12])
    frame = 4 * channels
    start = max(12, start_byte)
    start += (frame - (start - 12) % frame) % frame
    count = min(int(seconds * rate), (len(data) - start) // frame)
    values = struct.unpack("<%df" % (count * channels), data[start:start + count * frame])
    return values[0::channels], rate


def measure(capture, mark, rate_guess, skip, *tones):
    samples, rate = left_channel(capture, mark + int(skip * rate_guess) * 8, 1.0)
    return [tone_db(samples, rate, hz) for hz in tones]


def seconds(text):
    match = re.search(r"pos=(\d+):(\d+(?:\.\d+)?)", text)
    return int(match.group(1)) * 60 + float(match.group(2)) if match else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: speed, like tape\n")

    work = Path(tempfile.mkdtemp(prefix="deckboy-tape-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    capture = work / "device.raw"
    show = work / "show" / "01-picture-on-screen.deckboy"
    with Runtime(exe, work / "run", show, env={"DECKBOY_LIMITER_CAPTURE": str(capture)}) as db:
        db.send("LIMITER ON CEILING 0")
        db.send("TONECUE")
        db.send("SELECT LAST")
        db.send("TAKE")
        time.sleep(1.2)
        mark = capture.stat().st_size
        time.sleep(1.2)
        at1, at05 = measure(capture, mark, 48000, 0.0, 1000.0, 500.0)
        note("1x: the tone at 1 kHz, -18 dBFS", abs(at1 + 18.0) <= 0.5 and at05 < -50, (at1, at05))

        made = db.send("FADE NEW")
        fade = re.search(r"fade cue (\d+)", made).group(1)
        for line in ("FADE WHAT speed", "FADE TO 0.5", "FADE OVER 0"):
            db.send(line)
        print("    " + db.send("FADE"))
        print("    " + db.send("FADE FIRE"))
        mark = capture.stat().st_size
        time.sleep(1.6)
        at05, at1 = measure(capture, mark, 48000, 0.4, 500.0, 1000.0)
        note("0.5x: an octave down, 500 Hz at the same level, no 1 kHz left",
             abs(at05 + 18.0) <= 0.5 and at1 < -50, (at05, at1))

        db.send("SELECT " + fade)
        db.send("FADE TO 2x")
        db.send("FADE OVER 2")
        db.send("FADE FIRE")
        time.sleep(1.0)
        mid = measure(capture, capture.stat().st_size - 9600 * 8, 48000, 0.0, 500.0, 1000.0, 2000.0)
        time.sleep(1.6)
        mark = capture.stat().st_size
        time.sleep(1.4)
        at2, at1 = measure(capture, mark, 48000, 0.2, 2000.0, 1000.0)
        note("a ramp to 2x passes between and ends at 2 kHz",
             abs(at2 + 18.0) <= 0.5 and at1 < -50 and max(mid) < -24, (at2, at1, mid))

        db.send("SELECT %d" % (int(fade) - 1))
        db.send("TAKE")
        mark = capture.stat().st_size
        time.sleep(1.6)
        at1, at2 = measure(capture, mark, 48000, 0.5, 1000.0, 2000.0)
        note("taken again, the tone starts at its own speed", abs(at1 + 18.0) <= 0.5 and at2 < -50, (at1, at2))

    show = work / "show" / "02-running-a-show.deckboy"
    with Runtime(exe, work / "run2", show) as db:
        db.send("SELECT 4")                  # "04 Loops until you move on"
        db.send("TAKE")
        time.sleep(1.5)
        made = db.send("FADE NEW")
        for line in ("FADE WHAT speed", "FADE TO 0.5", "FADE OVER 0", "FADE FIRE"):
            db.send(line)
        time.sleep(0.5)
        before = seconds(db.send("STATUS"))
        time.sleep(2.0)
        after = seconds(db.send("STATUS"))
        moved = (after - before) if before is not None and after is not None else None
        note("a video at 0.5x: its playhead moves about one second in two",
             moved is not None and 0.8 <= moved <= 1.2, (before, after))
        status = db.send("STATUS")
        note("and it is still playing", "status=Playing" in status, status[:200])

    # A RECORDING HEARS WHAT THE ROOM HEARS. The recorder's sound is handed on
    # as the device plays it, so at 0.5x it would get half a second a second
    # unless it is resampled the same way: the file must carry the tone an
    # octave down, as much sound as picture. Once through the decoder's path
    # (a clip with a 440 Hz sine) and once through the tone generator's.
    clip = work / "sine440.mp4"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=s=320x180:r=25:d=30",
                    "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=30",
                    "-af", "volume=0.25", "-ac", "2", "-c:v", "libx264", "-pix_fmt", "yuv420p",
                    "-c:a", "aac", "-shortest", str(clip)], check=True)
    show = work / "show" / "01-picture-on-screen.deckboy"
    recordings = work / "show" / "recordings"
    with Runtime(exe, work / "run3", show, extra=("--import", str(clip))) as db:
        db.send("OUTPUT on")
        db.send("RECFORMAT 640x360 25")
        for cue, label in (("SELECT LAST", "clip"), ("TONECUE", "tone")):
            if label == "tone":
                db.send("TONECUE")
                db.send("SELECT LAST")
            else:
                db.send(cue)
            db.send("TAKE")
            time.sleep(1.5)
            made = db.send("FADE NEW")
            for line in ("FADE WHAT speed", "FADE TO 0.5", "FADE OVER 0", "FADE FIRE"):
                db.send(line)
            time.sleep(0.8)
            db.send("RECORD on")
            time.sleep(4.0)
            db.send("RECORD off")
            time.sleep(3.0)                      # the muxer finishes the file
            files = sorted(recordings.glob("*.*"), key=lambda p: p.stat().st_mtime)
            if not files:
                note("a recording at 0.5x (%s) was written" % label, False, "nothing in " + str(recordings))
                continue
            take = files[-1]
            pcm = subprocess.run(["ffmpeg", "-v", "error", "-i", str(take), "-map", "0:a:0", "-ac", "1",
                                  "-ar", "48000", "-f", "s16le", "-"], capture_output=True).stdout
            probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                                    "stream=duration", "-of", "csv=p=0", str(take)],
                                   capture_output=True, text=True).stdout.strip()
            picture = float(probe) if probe and probe != "N/A" else None
            samples = [v / 32768.0 for v in struct.unpack("<%dh" % (len(pcm) // 2), pcm[:len(pcm) // 2 * 2])]
            sound = len(samples) / 48000.0
            want = 220.0 if label == "clip" else 500.0
            middle = samples[len(samples) // 2 - 24000:len(samples) // 2 + 24000]
            slowed, original = tone_db(middle, 48000, want), tone_db(middle, 48000, want * 2)
            note("a recording at 0.5x (%s): an octave down, %d Hz" % (label, want),
                 slowed > original + 20, ("%.1f dB at %d, %.1f at %d" % (slowed, want, original, want * 2)))
            note("a recording at 0.5x (%s): as much sound as picture" % label,
                 picture is not None and abs(sound - picture) <= 0.35, (sound, picture))
            del made
            take.unlink()

    shutil.rmtree(work, ignore_errors=True)
    print()
    if fails:
        print("tape speed: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("tape speed: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
