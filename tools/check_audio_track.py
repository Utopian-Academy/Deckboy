"""The desk plays the sound track it is asked for, and keeps the choice.

WHY THIS EXISTS. A file can carry more than one sound track -- a second
language, a commentary, a clean feed. The engine could play any of them, but
the desk had no control for it and the show file did not keep it, so the
choice existed only in the code. The clip here has silence as its first track
and a tone as its second, so the RECORDED programme says which one played.

    python tools/check_audio_track.py
"""

import os
import re
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, find_ffmpeg, parse_args  # noqa: E402


def mean_volume(ffmpeg, path):
    run = subprocess.run([ffmpeg, "-nostdin", "-hide_banner", "-i", path, "-map", "0:a:0?",
                          "-af", "volumedetect", "-f", "null", "-"],
                         capture_output=True, text=True, errors="replace")
    m = re.search(r"mean_volume:\s*(-?[\d.]+|-inf) dB", run.stderr)
    if not m or m.group(1) == "-inf":
        return -120.0
    return float(m.group(1))


def main():
    args = parse_args(__doc__, 5881)
    ffmpeg = find_ffmpeg(args.exe)
    fails = []

    def note(name, ok, info=""):
        print("  %-50s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append(name)

    # The harness mutes the master, which silences the recording too. Turn it
    # up on SDL's dummy driver instead: the recording hears it, the room does not.
    os.environ["SDL_AUDIO_DRIVER"] = "dummy"
    work = tempfile.mkdtemp(prefix="deckboy-audio-track-")
    clip = os.path.join(work, "two tracks.mp4")
    subprocess.run([ffmpeg, "-nostdin", "-v", "error", "-y",
                    "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=25:duration=30",
                    "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo",
                    "-f", "lavfi", "-i", "sine=frequency=440:duration=30:sample_rate=48000",
                    "-map", "0:v", "-map", "1:a", "-map", "2:a", "-t", "30",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", clip], check=True)

    with Deckboy(args, prefix="deckboy-audio-track-", extra_args=["--import", clip]) as db:
        print("check: the desk's choice of sound track")
        print()
        db.send("MASTERVOL 100")
        db.send("SELECT 1")
        db.send("CUEAUDIO ON")
        time.sleep(1.0)
        note("the cue knows the file has two", db.send("AUDIOTRACK").endswith("1 of 2"),
             db.send("AUDIOTRACK"))
        db.send("TAKE")
        time.sleep(1.5)
        first = db.record(4.0)
        quiet = mean_volume(ffmpeg, first) if first else 0.0
        note("track 1 (silence) is what plays", quiet < -60, "%.1f dB" % quiet)
        reply = db.send("AUDIOTRACK 2")
        note("AUDIOTRACK 2 is accepted", reply.endswith("2 of 2"), reply)
        time.sleep(1.5)
        second = db.record(4.0)
        loud = mean_volume(ffmpeg, second) if second else -120.0
        note("...and the live cue changes to the tone", loud > -40, "%.1f dB" % loud)
        reply = db.send("AUDIOTRACK 3")
        note("a track the file does not have is refused", reply.startswith("ERR"), reply)
        show = db.saved_show()
        cue = next((l for l in show.splitlines() if l.startswith("cue\t")), "")
        tail = cue.split("\t")[-2:]
        note("the show file keeps track 2 of 2", tail == ["1", "2"], repr(tail))
    print()
    print("%d FAILED" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
