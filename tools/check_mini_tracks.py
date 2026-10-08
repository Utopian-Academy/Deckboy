"""Deckboy Mini plays the sound track it is asked for.

WHY THIS EXISTS. mpv parity: a file with a second sound track (commentary, a
second language) must be able to play it, and # / AUDIOTRACK must actually
change what is heard -- not just a number in STATUS. The test file's first
track is silence and its second a tone, so the programme level tells them
apart.

    python tools/check_mini_tracks.py
"""

import os
import re
import socket
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(REPO, "build", "windows", "Release", "deckboy-mini.exe")
PORT = 5841


def send(command):
    with socket.create_connection(("127.0.0.1", PORT), timeout=5) as s:
        s.sendall((command + "\n").encode())
        return s.recv(65536).decode(errors="replace").strip()


def field(name):
    """One key from STATUS; a quoted value keeps its spaces."""
    m = re.search(r'(?:^|\s)' + name + r'=("([^"]*)"|\S+)', send("STATUS"))
    if not m:
        return ""
    return m.group(2) if m.group(2) is not None else m.group(1)


def main():
    work = tempfile.mkdtemp(prefix="deckboy-mini-tracks-")
    clip = os.path.join(work, "two tracks.mp4")
    subprocess.run(["ffmpeg", "-y", "-v", "error",
                    "-f", "lavfi", "-i", "testsrc2=size=640x360:rate=25:duration=20",
                    "-f", "lavfi", "-i", "anullsrc=r=48000:cl=stereo",
                    "-f", "lavfi", "-i", "sine=frequency=440:duration=20:sample_rate=48000",
                    "-map", "0:v", "-map", "1:a", "-map", "2:a", "-t", "20",
                    "-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p", "-c:a", "aac", clip],
                   capture_output=True)
    if not os.path.exists(clip):
        sys.exit("needs ffmpeg on PATH")
    fails = []

    def note(name, ok, info=""):
        print("  %-48s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append(name)

    proc = subprocess.Popen([EXE, "--window", "--plain", "--port", str(PORT), clip], cwd=os.path.dirname(EXE),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
    try:
        for _ in range(40):
            try:
                send("PING")
                break
            except OSError:
                time.sleep(0.3)
        print("check: Deckboy Mini, choosing the sound track")
        print()
        time.sleep(2.0)
        note("the file reports two sound tracks", field("track") == "1/2", field("track"))
        quiet = max(int(field("level") or 0) for _ in range(5) if not time.sleep(0.2))
        note("track 1 (silence) is quiet", quiet <= 2, "level %d" % quiet)
        reply = send("AUDIOTRACK 2")
        time.sleep(1.5)
        loud = max(int(field("level") or 0) for _ in range(5) if not time.sleep(0.2))
        note("AUDIOTRACK 2 plays the tone", reply.startswith("OK") and loud >= 10, "%s level %d" % (reply, loud))
        note("...and STATUS says track 2", field("track") == "2/2", field("track"))
        reply = send("AUDIOTRACK 3")
        note("a track the file does not have is refused", reply.startswith("ERR"), reply)

        # Subtitles INSIDE the file (an MKV with an SRT track), read in the
        # background and offered once they arrive.
        srt = os.path.join(work, "inner.srt")
        with open(srt, "w", encoding="utf-8") as f:
            f.write("1\n00:00:00,000 --> 00:00:19,000\nInside the file\n\n")
        mkv = os.path.join(work, "with subs.mkv")
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", clip, "-i", srt, "-map", "0", "-map", "1",
                        "-c", "copy", "-c:s", "srt", mkv], capture_output=True)
        send('ADD "%s"' % mkv)
        send("TAKE 2")
        time.sleep(3.0)
        send("SUBS ON")
        subs = ""
        for _ in range(10):
            subs = field("subs")
            if "in the file" in subs:
                break
            time.sleep(0.5)
        note("a subtitle track inside the file is found", "in the file" in subs, subs)
    finally:
        try:
            send("QUIT")
        except OSError:
            pass
        proc.wait(timeout=10)
    print()
    print("%d FAILED" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
