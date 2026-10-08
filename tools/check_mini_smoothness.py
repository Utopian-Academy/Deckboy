"""How smoothly Deckboy Mini plays a file on THIS machine.

WHY THIS EXISTS. James, 2026-10-07, on Mini: "I'd like it to be comparable to
mpv" -- and smoothness on weak hardware was half of what he meant. This plays
one file, silently, for a fixed time and reports what reached the screen: new
frames shown, frames the file had that never got there (skipped), the decode
rate and the CPU it cost. Run mpv on the same file on the same machine for the
number to compare with (its frame-drop-count).

    python3 tools/check_mini_smoothness.py <file> [--seconds 50] [--start 600] [--exe path]
             [--max-skipped-pct 1.0]

No sound is made: Mini runs at volume 0 (and SDL_AUDIO_DRIVER=dummy unless
--real-audio is given).
"""

import argparse
import os
import shutil
import re
import socket
import subprocess
import sys
import tempfile
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def default_exe():
    for rel in (("build", "windows", "Release", "deckboy-mini.exe"), ("build", "deckboy-mini")):
        path = os.path.join(REPO, *rel)
        if os.path.isfile(path):
            return path
    return "deckboy-mini"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("--seconds", type=float, default=50.0)
    ap.add_argument("--start", type=float, default=0.0, help="seek here first (seconds)")
    ap.add_argument("--exe", default=default_exe())
    ap.add_argument("--port", type=int, default=5845)
    ap.add_argument("--window", action="store_true", help="windowed instead of fullscreen")
    ap.add_argument("--real-audio", action="store_true")
    ap.add_argument("--max-skipped-pct", type=float, default=1.0)
    ap.add_argument("--log", default="", help="write Mini's own output here (and a backtrace if it hangs)")
    args = ap.parse_args()

    def send(command):
        with socket.create_connection(("127.0.0.1", args.port), timeout=5) as s:
            s.sendall((command + "\n").encode())
            return s.recv(65536).decode(errors="replace").strip()

    def field(text, name):
        m = re.search(r'(?:^|\s)' + name + r'=("([^"]*)"|\S+)', text)
        return "" if not m else (m.group(2) if m.group(2) is not None else m.group(1))

    env = dict(os.environ)
    if not args.real_audio:
        env["SDL_AUDIO_DRIVER"] = "dummy"
    cmd = [args.exe, "--plain", "--volume", "0", "--port", str(args.port), args.file]
    if args.window:
        cmd.insert(1, "--window")
    log = open(args.log, "w") if args.log else subprocess.DEVNULL
    proc = subprocess.Popen(cmd, env=env, stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(100):
            try:
                send("PING")
                break
            except OSError:
                time.sleep(0.3)
        else:
            # A HANG says where it is stuck only while it is still stuck: take
            # every thread's stack before it is killed.
            if args.log and shutil.which("gdb") and proc.poll() is None:
                with open(args.log, "a") as out:
                    out.write("\n--- no answer: backtraces ---\n")
                    out.flush()
                    subprocess.run(["gdb", "-p", str(proc.pid), "-batch", "-ex", "thread apply all bt 25"],
                                   stdout=out, stderr=subprocess.STDOUT, timeout=60)
            state = "still running" if proc.poll() is None else "exited with %s" % proc.returncode
            sys.exit("deckboy-mini did not answer on port %d (%s)" % (args.port, state))
        send("TAKE 1")
        if args.start > 0:
            time.sleep(1.0)
            send("SEEKPOS %g" % args.start)
        time.sleep(6.0)   # past the first decode and any seek
        before = send("STATUS")
        snap0 = snap(send, "a")
        cpu0 = cpu_seconds(proc.pid)
        t0 = time.time()
        time.sleep(args.seconds)
        after = send("STATUS")
        cpu1 = cpu_seconds(proc.pid)
        wall = time.time() - t0
        snap1 = snap(send, "b")
    finally:
        try:
            send("QUIT")
        except OSError:
            pass
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    shown = int(field(after, "frames_shown") or 0) - int(field(before, "frames_shown") or 0)
    skipped = int(field(after, "frames_skipped") or 0) - int(field(before, "frames_skipped") or 0)
    # WHAT THE FILM OWES, not what the decoder produced. frames_skipped counts
    # only frames the engine threw away; a decoder that delivers too few
    # throws none away and shows a slow picture. Measured on a Pi 3: 19.8 new
    # frames a second from a 23.976 fps film, zero skipped -- and this tool
    # said "ok" until it compared against the film's own rate.
    fps = float(field(after, "media_fps") or 0)
    owed = int(round(fps * wall)) if fps > 0 else shown + skipped
    missing = max(0, owed - shown)
    pct = 100.0 * missing / owed if owed else 100.0
    print("file      %s" % os.path.basename(args.file))
    print("played    %.1f s   raster %s" % (wall, field(after, "raster")))
    print("shown     %d new frames (%.2f/s)" % (shown, shown / wall if wall else 0))
    print("film      %.3f fps -> %d frames owed in that time" % (fps, owed))
    print("missing   %d (%.2f%% of the frames owed; %d of them thrown away as late)" % (missing, pct, skipped))
    print("decode    %s fps" % field(after, "decode_fps"))
    # Where each frame's time went, from the engine's running totals.
    def per(total, count):
        dn = int(field(after, count) or 0) - int(field(before, count) or 0)
        dt = int(field(after, total) or 0) - int(field(before, total) or 0)
        return (dt / dn / 1000.0, dn) if dn > 0 else (None, 0)
    dec, n = per("decode_us", "decoded")
    conv, _ = per("convert_us", "decoded")
    # Per NEW frame shown, not per loop: the loop spins far faster than the
    # film, and the cost that matters is what each picture took to put up.
    dd = int(field(after, "draw_us") or 0) - int(field(before, "draw_us") or 0)
    nu = shown
    up = dd / shown / 1000.0 if shown > 0 else None
    budget = 1000.0 / fps if fps > 0 else None
    if dec is not None:
        print("stages    decode %.1f ms (of which convert %.1f) per frame, %d frames; upload+draw %s ms per new frame (%d); budget %s ms"
              % (dec, conv or 0.0, n, "%.1f" % up if up is not None else "?", nu,
                 "%.1f" % budget if budget else "?"))
    if cpu0 is not None and cpu1 is not None:
        print("cpu       %.1f%% of one core" % (100.0 * (cpu1 - cpu0) / wall))
    # THE PICTURE HAS TO MOVE. Every count above is about frames PRESENTED,
    # and on the Pi's zero-copy path those were one stale import shown over
    # and over: 24.08 fps, nothing missing, and the film frozen on its title
    # card. Two snapshots args.seconds apart must differ.
    changed = picture_change(snap0, snap1)
    print("picture   %s" % ("no snapshot" if changed is None else "%.1f%% of the frame changed" % changed))
    moving = changed is not None and changed >= 2.0
    ok = owed > 0 and pct <= args.max_skipped_pct and moving
    if not moving:
        print("FAIL: the picture did not change -- frames were counted but not seen")
    else:
        print("ok" if ok else "FAIL: missing more than %.2f%% of the frames owed" % args.max_skipped_pct)
    return 0 if ok else 1


def snap(send, tag):
    """The output frame as it leaves, via OUTSNAP; the BMP's bytes or None."""
    path = os.path.join(tempfile.gettempdir(), "mini-smooth-%d-%s.bmp" % (os.getpid(), tag))
    if os.path.exists(path):
        os.remove(path)
    send("OUTSNAP " + path)
    for _ in range(50):
        if os.path.exists(path) and os.path.getsize(path) > 1000:
            time.sleep(0.2)
            data = open(path, "rb").read()
            os.remove(path)
            return data
        time.sleep(0.1)
    return None


def picture_change(a, b):
    """Percent of pixel bytes that differ by more than a little noise."""
    if not a or not b or len(a) != len(b):
        return None
    off = int.from_bytes(a[10:14], "little")   # BMP pixel-data offset
    pa, pb = a[off:], b[off:]
    step = max(1, len(pa) // 200000)            # a sample is plenty
    idx = range(0, len(pa), step)
    differ = sum(1 for i in idx if abs(pa[i] - pb[i]) > 8)
    return 100.0 * differ / len(idx)


def cpu_seconds(pid):
    """User+system CPU seconds of a process (Linux /proc, or psutil if present)."""
    stat = "/proc/%d/stat" % pid
    if os.path.exists(stat):
        fields = open(stat).read().rsplit(")", 1)[1].split()
        return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")
    try:
        import psutil
        t = psutil.Process(pid).cpu_times()
        return t.user + t.system
    except Exception:
        return None


if __name__ == "__main__":
    sys.exit(main())
