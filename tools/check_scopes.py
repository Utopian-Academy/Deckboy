"""The scopes, measured on known signals through the real output.

Runs the real app headless with an output armed and the monitor on SCOPES,
then reads SCOPES STATS for signals whose readings are known exactly:
  - EBU 75% bars made at the output raster: six vectors in the 75% boxes and
    none at 100%, and luma at 75, 69.6, 59.1, 53.6, 21.5, 15.9, 5.4 and 0 %
  - a grey ramp over every code: 0 to 100 % and a flat histogram
  - the built-in SMPTE bars pattern
and then real clips (--clips), whose screens are kept for a person to look at.
Every reading must cover the FULL output raster, not a preview.
Evidence: build/scopes-*/
"""
import argparse
import glob
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from PIL import Image

from deckboy_testroot import populate

ROOT = Path(__file__).resolve().parent.parent

EBU75 = [(191, 191, 191), (191, 191, 0), (0, 191, 191), (0, 191, 0),
         (191, 0, 191), (191, 0, 0), (0, 0, 191), (0, 0, 0)]
# BT.709 luma of each bar, in %: 0.75 * (0.2126 R + 0.7152 G + 0.0722 B)
EBU75_LUMA = [round(100 * (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255, 1) for r, g, b in EBU75]


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def command(port, line, timeout=15):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.sendall((line + "\n").encode())
        result = b""
        while b"\n" not in result:
            chunk = sock.recv(65536)
            if not chunk:
                raise ConnectionError("control socket closed")
            result += chunk
        return result.decode().strip()


def stats(port):
    reply = command(port, "SCOPES STATS")
    fields = dict(re.findall(r"(\w+)=(\S+)", reply))
    return reply, fields


def fresh_stats(port, previous=None, seconds=12):
    """Wait for a reading that differs from `previous` (a new frame analysed)."""
    until = time.monotonic() + seconds
    reply, fields = stats(port)
    while time.monotonic() < until and ("frame" not in fields or reply == previous):
        time.sleep(0.3)
        reply, fields = stats(port)
    time.sleep(1.2)  # let a couple more frames land so the reading is settled
    return stats(port)


def import_paths(port, work, paths):
    """Import through the real drop path; returns the cue count once it settles."""
    listing = work / f"drop-{time.monotonic_ns()}.txt"
    listing.write_text("".join(f"{p}\n" for p in paths), encoding="utf-8")
    before = int(re.search(r"cues=(\d+)", command(port, "DROPTEST")).group(1))
    if not command(port, f"DROPTEST {listing}").startswith("OK"):
        return before
    until = time.monotonic() + 60
    count = before
    while time.monotonic() < until:
        reply = command(port, "DROPTEST")
        count = int(re.search(r"cues=(\d+)", reply).group(1))
        if count >= before + len(paths) and "pending=0" in reply:
            break
        time.sleep(0.5)
    time.sleep(1.0)
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    parser.add_argument("--clips", nargs="*", default=[], help="real media to scope (globs allowed)")
    args = parser.parse_args()
    (ROOT / "build").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="scopes-", dir=ROOT / "build"))
    populate(str(work))
    port = free_port()
    show = work / "data/default.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\nui_scale\t1.5\n", encoding="utf-8")
    env = dict(os.environ, DECKBOY_ROOT=str(work), DECKBOY_STATE_DIR=str(work / "state"),
               DECKBOY_PROJECT=str(show), DECKBOY_COMPANION_PORT=str(port),
               SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", DECKBOY_RENDERER="software")
    log = (work / "app.log").open("w", encoding="utf-8")
    app = subprocess.Popen([args.exe, "--allow-multi-instance", str(show), "--ui-size", "2640", "1530"],
                           env=env, cwd=ROOT, stdout=log, stderr=log,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    checks = failures = 0

    def expect(condition, label, detail=""):
        nonlocal checks, failures
        checks += 1
        failures += 0 if condition else 1
        print(("ok   " if condition else "FAIL ") + label + ("" if condition else f"  [{detail}]"), flush=True)

    def snap(name):
        path = work / f"{name}.bmp"
        command(port, f"UISNAP {path}")
        time.sleep(1.5)
        return path

    try:
        until = time.monotonic() + 60
        while True:
            try:
                if command(port, "PING").startswith("OK"):
                    break
            except OSError:
                pass
            if time.monotonic() > until:
                raise AssertionError("app did not answer")
            time.sleep(0.3)
        time.sleep(1.0)
        expect(command(port, "SCOPES STATS").endswith("no frame yet"), "no reading before anything is scoped")
        command(port, "OUTPUT ON")
        command(port, "PATTERN ADD smpte-bars")
        command(port, "TAKE 1")
        expect(command(port, "SCOPES SCOPES").startswith("OK"), "SCOPES SCOPES")
        reply, f = fresh_stats(port)
        expect("frame" in f, "a reading arrives once the output is scoped", reply)
        if "frame" not in f:
            raise AssertionError("no scope reading: " + reply)
        width, height = map(int, f["frame"].split("x"))
        expect(int(f["pixels"]) == width * height,
               f"every pixel of the {width}x{height} output is counted", reply)
        expect(f["targets75"] == "6/6", "SMPTE bars: all six vectors in the 75% boxes", reply)
        snap("smpte-bars")

        # Known signals at the output raster, so nothing is scaled on the way.
        bars = Image.new("RGB", (width, height))
        for i, colour in enumerate(EBU75):
            x0, x1 = width * i // 8, width * (i + 1) // 8
            bars.paste(colour, (x0, 0, x1, height))
        bars_path = work / "ebu75.png"
        bars.save(bars_path)
        ramp = Image.new("RGB", (width, height))
        ramp.putdata([(v, v, v) for _ in range(height) for v in [x * 256 // width for x in range(width)]])
        ramp_path = work / "ramp.png"
        ramp.save(ramp_path)

        expect(import_paths(port, work, [bars_path, ramp_path]) == 3, "import the bars and the ramp")

        expect(command(port, "TAKE 2").startswith("OK"), "TAKE the bars")
        reply, f = fresh_stats(port, reply)
        expect(f.get("targets75") == "6/6" and f.get("targets100") == "0/6",
               "EBU 75% bars: six vectors in the 75% boxes, none at 100%", reply)
        peaks = [float(p) for p in f.get("luma_peaks", "-").split(",") if p not in ("-", "")]
        for want in EBU75_LUMA:
            expect(any(abs(p - want) <= 0.6 for p in peaks), f"EBU 75% bars: a luma level at {want}%", reply)
        expect(not any(p > 90 for p in peaks), "EBU 75% bars: no 100% white (this is not the SMPTE pattern)", reply)
        expect(f.get("diamond_out") == "0", "EBU 75% bars: inside both diamonds", reply)
        expect("R 103 OK" in reply, "EBU 75% bars: in gamut", reply)
        snap("ebu75")
        command(port, "SCOPES BIG diamond")
        snap("ebu75-diamond")
        command(port, "SCOPES BIG vector")
        snap("ebu75-vector")
        command(port, "SCOPES BIG off")

        expect(command(port, "TAKE 3").startswith("OK"), "TAKE the ramp")
        reply, f = fresh_stats(port, reply)
        luma = f.get("luma", "")
        expect(luma.startswith("0.0..100.0"), "ramp: levels span 0 to 100%", reply)
        expect(int(f.get("hist_bins", 0)) >= 250 and float(f.get("hist_ratio", 9)) <= 1.25,
               "ramp: every level present and the histogram flat", reply)
        snap("ramp")

        # FREEZE holds the reading while the picture changes.
        command(port, "SCOPES FREEZE ON")
        frozen, _ = stats(port)
        expect(command(port, "TAKE 2").startswith("OK"), "TAKE the bars while frozen")
        time.sleep(2.0)
        expect(stats(port)[0] == frozen, "FREEZE holds the last reading", stats(port)[0])
        command(port, "SCOPES FREEZE OFF")
        reply, f = fresh_stats(port, frozen)
        expect(f.get("targets75") == "6/6" and int(f.get("hist_bins", 0)) < 50,
               "live again after FREEZE OFF (the bars, not the ramp)", reply)

        command(port, "SCOPES BOTH")
        snap("both")
        command(port, "SCOPES SCOPES")

        # Real clips: the numbers must be sane and the screens are kept to look at.
        clips = [c for pattern in args.clips for c in sorted(glob.glob(pattern))]
        if clips:
            expect(import_paths(port, work, clips) == 3 + len(clips), f"import {len(clips)} real clips")
        for n, clip in enumerate(clips):
            expect(command(port, f"TAKE {4 + n}").startswith("OK"), f"TAKE {Path(clip).name}")
            time.sleep(4.0)
            reply, f = fresh_stats(port, reply)
            expect(int(f.get("pixels", 0)) == width * height,
                   f"{Path(clip).name}: full raster scoped ({f.get('ms')} ms)", reply)
            print("     " + reply, flush=True)
            snap(f"clip{n + 1}")
    finally:
        app.kill()
        app.wait(timeout=10)
        log.close()
    print(f"scopes: {checks} checks, {failures} failures; evidence: {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
