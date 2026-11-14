"""A sped-up soak of Deckboy Mini: does looping for days leak?

Two Minis play a ONE-second clip, so a minute here is sixty loops: one loops
the cue (Shift+L, the engine's restart from the top), one loops the list
(--loop, the cue taken again each pass). Every 15 seconds the script reads
each process's private memory, handles and threads, and at the end fits a
line through the second half of the samples (after the start-up allocations
settle) and reports the growth per thousand loops.

No window and no sound: SDL's dummy video and audio drivers. That leaves the
GPU decode path out; what it measures is the decoder restart, the engine and
the Mini around them.

    py -3 tools/check_mini_soak.py [--minutes 20] [--exe build/windows/Release/deckboy-mini.exe]

Exit status 1 if anything grows faster than the limits below.
"""
import argparse
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Allowed growth per 1000 loops, after warm-up. A week of a 10-second clip is
# ~60,000 loops, so 1 MB per 1000 would be 60 MB a week: noticeable but not
# fatal. Handles and threads must not grow at all beyond noise.
LIMIT_MB = 1.0
LIMIT_HANDLES = 2.0
LIMIT_THREADS = 0.5


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def send(port, line):
    with socket.create_connection(("127.0.0.1", port), timeout=3) as c:
        c.sendall((line + "\n").encode())
        data = b""
        while not data.endswith(b"\n"):
            block = c.recv(65536)
            if not block:
                break
            data += block
    return data.decode("utf-8", "replace").strip()


def sample(pids):
    """Private MB, handles, threads for each pid (Windows: one PowerShell call)."""
    if os.name == "nt":
        ids = ",".join(str(p) for p in pids)
        ps = ("Get-Process -Id %s | ForEach-Object { '{0} {1} {2} {3}' -f $_.Id, "
              "$_.PrivateMemorySize64, $_.HandleCount, $_.Threads.Count }" % ids)
        out = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True).stdout
        got = {}
        for line in out.split():
            pass
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 4:
                got[int(parts[0])] = (int(parts[1]) / 1048576.0, int(parts[2]), int(parts[3]))
        return got
    got = {}
    for p in pids:
        status = Path("/proc/%d/status" % p).read_text()
        rss = int(next(l for l in status.splitlines() if l.startswith("VmRSS")).split()[1]) / 1024.0
        threads = int(next(l for l in status.splitlines() if l.startswith("Threads")).split()[1])
        fds = len(os.listdir("/proc/%d/fd" % p))
        got[p] = (rss, fds, threads)
    return got


def slope(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    den = sum((x - mx) ** 2 for x in xs) or 1.0
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / den


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--minutes", type=float, default=20.0)
    ap.add_argument("--exe", type=Path, default=ROOT / "build/windows/Release/deckboy-mini.exe")
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="mini-soak-", dir=ROOT / "build"))
    clip = work / "one-second.mp4"
    quiet = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    subprocess.run([shutil.which("ffmpeg") or "ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                    "testsrc2=size=1280x720:rate=30", "-f", "lavfi", "-i", "sine=frequency=330:sample_rate=48000",
                    "-t", "1", "-c:v", "libx264", "-g", "15", "-pix_fmt", "yuv420p", "-c:a", "aac", str(clip)],
                   check=True, timeout=120, **quiet)

    runs = {}
    for name, extra in (("cue-loop", []), ("list-loop", ["--loop"])):
        state = work / name
        state.mkdir()
        port = free_port()
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software",
                   DECKBOY_ROOT=str(ROOT), DECKBOY_STATE_DIR=str(state))
        log = (work / (name + ".log")).open("wb")
        proc = subprocess.Popen([str(args.exe.resolve()), "--window", "--plain", "--port", str(port)] + extra +
                                [str(clip)], cwd=ROOT, env=env, stdin=subprocess.DEVNULL, stdout=log,
                                stderr=subprocess.STDOUT, **quiet)
        runs[name] = {"proc": proc, "port": port, "log": log, "samples": []}
    try:
        for name, r in runs.items():
            end = time.time() + 15
            while True:
                try:
                    if send(r["port"], "PING").startswith("OK"):
                        break
                except OSError:
                    pass
                if time.time() > end:
                    raise SystemExit(name + ": Mini did not answer")
                time.sleep(0.2)
        reply = send(runs["cue-loop"]["port"], "CUELOOP 1 ON")
        if not reply.startswith("OK"):
            raise SystemExit("CUELOOP refused: " + reply)
        start = time.time()
        stop = start + args.minutes * 60.0
        while time.time() < stop:
            got = sample([r["proc"].pid for r in runs.values()])
            t = time.time() - start
            for name, r in runs.items():
                if r["proc"].poll() is not None:
                    raise SystemExit("%s exited during the soak (code %s)" % (name, r["proc"].returncode))
                if r["proc"].pid in got:
                    r["samples"].append((t,) + got[r["proc"].pid])
            line = "  ".join("%s %.1f MB %d h %d t" % ((n,) + r["samples"][-1][1:]) for n, r in runs.items()
                             if r["samples"])
            print("t+%5.0fs  %s" % (t, line), flush=True)
            time.sleep(15)
        for name, r in runs.items():
            print(name, "status:", send(r["port"], "STATUS").splitlines()[0][:160])
    finally:
        for r in runs.values():
            try:
                send(r["port"], "QUIT")
                r["proc"].wait(timeout=15)
            except Exception:
                r["proc"].kill()
            r["log"].close()

    failures = 0
    for name, r in runs.items():
        s = r["samples"]
        half = s[len(s) // 2:]
        if len(half) < 4:
            print(name, ": too few samples")
            failures += 1
            continue
        xs = [p[0] for p in half]                     # seconds = loops for a 1 s clip
        per_k = [slope(xs, [p[i] for p in half]) * 1000.0 for i in (1, 2, 3)]
        verdict = per_k[0] <= LIMIT_MB and per_k[1] <= LIMIT_HANDLES and per_k[2] <= LIMIT_THREADS
        failures += 0 if verdict else 1
        print("%s %-9s per 1000 loops: memory %+.2f MB, handles %+.1f, threads %+.2f  "
              "(start %.1f MB / %d h / %d t, end %.1f MB / %d h / %d t)" %
              ("ok  " if verdict else "FAIL", name, per_k[0], per_k[1], per_k[2],
               s[0][1], s[0][2], s[0][3], s[-1][1], s[-1][2], s[-1][3]))
    print("evidence:", work)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
