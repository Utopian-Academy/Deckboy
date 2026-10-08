"""Deckboy Mini runs a whole show from the keyboard: list editing, playlists,
displays, the output and the sound device.

WHY THIS EXISTS. James, 2026-10-07: "Deckboy Mini also needs a way to load
cues and edit playlists, and all the basic commands needed to run a show.
Also selecting and activating outputs etc, all from a keyboard." Every key
Mini has is also a command (the : line and the remote port use one handler),
so this drives the commands and reads STATUS back -- the LIST line carries
the order, so a move that only answered OK is caught.

    python tools/check_mini_editing.py [--exe build/windows/Release/deckboy-mini.exe]
"""

import argparse
import os
import socket
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = 5821


def send(command, timeout=5.0):
    with socket.create_connection(("127.0.0.1", PORT), timeout=timeout) as s:
        s.settimeout(timeout)
        s.sendall((command + "\n").encode())
        data = b""
        end = time.time() + timeout
        while time.time() < end:
            try:
                chunk = s.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            data += chunk
            if data.endswith(b"\n"):
                time.sleep(0.05)
                s.settimeout(0.2)
        return data.decode(errors="replace").strip()


def list_line():
    for line in send("STATUS").splitlines():
        if line.startswith("LIST "):
            return line
    return ""


def order():
    line = list_line()
    at = line.find("order=")
    return line[at + 6:].split("|") if at >= 0 else []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(REPO, "build", "windows", "Release", "deckboy-mini.exe"))
    args = ap.parse_args()
    work = tempfile.mkdtemp(prefix="deckboy-mini-edit-")
    clips = []
    for n, name in enumerate(["alpha", "bravo", "charlie"]):
        path = os.path.join(work, "%s.wav" % name)
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi", "-i",
                        "sine=frequency=%d:duration=8" % (300 + 100 * n), path], capture_output=True)
        clips.append(path)
    with open(os.path.join(work, "alpha.srt"), "w", encoding="utf-8") as srt:
        srt.write("1\n00:00:00,000 --> 00:00:05,000\nHello from the sidecar\n\n")
    fails = []

    def note(name, ok, info=""):
        print("  %-52s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append("%s -- %s" % (name, info))

    env = dict(os.environ)
    env.pop("NO_COLOR", None)
    proc = subprocess.Popen([args.exe, "--window", "--paused", "--plain", "--port", str(PORT)] + clips,
                            cwd=os.path.dirname(args.exe), env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, stdin=subprocess.DEVNULL)
    try:
        for _ in range(60):
            try:
                send("PING", 1.0)
                break
            except OSError:
                time.sleep(0.5)
        print("check: Deckboy Mini, the show from the keyboard")
        print()
        note("three cues in", order() == ["alpha", "bravo", "charlie"], repr(order()))
        send("MOVE 3 1")
        note("MOVE 3 1 puts charlie first", order() == ["charlie", "alpha", "bravo"], repr(order()))
        send("RENAME 2 Opening titles")
        note("RENAME keeps spaces", order()[1:2] == ["Opening titles"], repr(order()))
        send("CUELOOP 3 ON")
        note("CUELOOP marks a cue", order()[2:3] == ["bravo(loop)"], repr(order()))
        note("an edited list is marked unsaved", "dirty=yes" in list_line(), list_line())
        show = os.path.join(work, "my show.m3u8")
        reply = send('SAVE "%s"' % show)
        note("SAVE writes a playlist", reply.startswith("OK") and os.path.exists(show), reply)
        note("...and the list is clean", "dirty=no" in list_line(), list_line())
        text = open(show, encoding="utf-8").read() if os.path.exists(show) else ""
        note("it is a readable M3U8 with relative paths",
             text.startswith("#EXTM3U") and "charlie.wav" in text and work not in text, text[:120])
        send("REMOVE 1")
        note("REMOVE takes a cue out", order() == ["Opening titles", "bravo(loop)"], repr(order()))
        reply = send('OPEN "%s"' % show)
        note("OPEN over unsaved edits is refused once", reply.startswith("ERR") and "unsaved" in reply, reply)
        reply = send('OPEN "%s"' % show)
        note("OPEN brings the saved list back", reply.startswith("OK") and
             order() == ["charlie", "Opening titles", "bravo(loop)"], "%s %r" % (reply, order()))
        reply = send("DISPLAYS")
        note("DISPLAYS lists the screens", reply.startswith("OK DISPLAYS") and "* 1" in reply, reply[:120])
        count = reply.count("\n")
        if count >= 2:
            reply = send("DISPLAY 2")
            note("DISPLAY 2 moves the output", reply.startswith("OK") and "display=2" in send("STATUS"), reply)
            send("DISPLAY 1")
        reply = send("DISPLAY 99")
        note("a display that is not there is refused", reply.startswith("ERR"), reply)
        send("OUTPUT OFF")
        note("OUTPUT OFF hides the output", "window=hidden" in send("STATUS"), "")
        send("OUTPUT ON")
        note("OUTPUT ON shows it again", "window=shown" in send("STATUS"), "")
        reply = send("AUDIO LIST")
        note("AUDIO LIST names the devices", reply.startswith("OK AUDIO"), reply[:120])
        send("TAKE 1")
        time.sleep(0.5)
        reply = send("AUDIO NEXT")
        status = send("STATUS")
        note("AUDIO NEXT moves the sound and keeps the cue on air",
             reply.startswith("OK") and "active=1" in status, reply + " | " + status[:160])
        reply = send("AUDIO no-such-device-anywhere")
        note("a sound device that is not there is refused", reply.startswith("ERR"), reply)
        # PLAYING A FILE, as mpv does. The open list is charlie, Opening
        # titles (alpha), bravo; alpha has a subtitle file beside it.
        reply = send("TAKE 2")
        time.sleep(0.6)
        reply = send("SUBS ON")
        note("subtitles beside the file are found", reply.startswith("OK"), reply)
        note("...and named in STATUS", 'subs="alpha.srt"' in send("STATUS"), send("STATUS")[:200])
        send("SPEED 1.5")
        time.sleep(0.4)
        status = send("STATUS")
        note("SPEED takes and the cue stays on air", "speed=1.5" in status and "active=2" in status, status[:200])
        send("SPEED 1")
        send("MUTE ON")
        note("MUTE", "mute=on" in send("STATUS"))
        send("MUTE OFF")
        send("TAKE 2")
        time.sleep(0.3)
        reply = send("ABLOOP 0.5 2.0")
        seen = []
        for _ in range(8):
            time.sleep(0.35)
            st = send("STATUS")
            seen.append(st.split(" pos=")[1].split()[0] if " pos=" in st else "?")
        status = send("STATUS")
        inside = all("00:00.4" <= p <= "00:02.1" for p in seen)
        moving = len(set(seen)) > 2
        note("ABLOOP plays between A and B, round and round",
             reply.startswith("OK") and inside and moving and "active=2" in status,
             "%s seen=%s" % (reply, seen))
        send("ABLOOP OFF")
        reply = send("FRAME")
        note("FRAME steps (and pauses)", reply.startswith("OK") and "status=Paused" in send("STATUS"), reply)
        reply = send("SPEED 9")
        note("a speed out of range is refused", reply.startswith("ERR"), reply)
        reply = send("STILL 1 4")
        note("STILL refuses a cue that is not a still", reply.startswith("ERR"), reply)
    finally:
        try:
            send("QUIT", 2.0)
        except OSError:
            pass
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
    print()
    if fails:
        print("%d FAILED" % len(fails))
        for f in fails:
            print("  - " + f)
        return 1
    print("all ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
