"""The scopes, measured on known signals through the real output.

Runs the real app headless with an output armed and the monitor on SCOPES,
then reads SCOPES STATS for signals whose readings are known exactly:
  - EBU 75% bars made at the output raster: six vectors in the 75% boxes and
    none at 100%, and luma at 75, 69.6, 59.1, 53.6, 21.5, 15.9, 5.4 and 0 %
  - a grey ramp over every code: 0 to 100 % and a flat histogram
  - the built-in SMPTE bars pattern
  - red/black single-pixel stripes, legal as an R'G'B' frame and OUT of
    R 103 gamut as the Y'CbCr 4:2:2 the output sends once ST 2110 is open
    (sent to 127.0.0.1: nothing leaves the machine; SDI uses the same encoder)
and then real clips (--clips), whose screens are kept for a person to look at.
Every reading must cover the FULL output raster, not a preview.
Evidence: build/scopes-*/
"""
import argparse
import glob
import os
import re
import random
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
    # Deckboy binds both protocols; Windows can exclude high ephemeral ranges.
    for _ in range(100):
        port = random.SystemRandom().randrange(12000, 30000)
        try:
            with socket.socket() as tcp, socket.socket(type=socket.SOCK_DGRAM) as udp:
                tcp.bind(("127.0.0.1", port))
                udp.bind(("127.0.0.1", port))
                return port
        except OSError:
            continue
    raise RuntimeError("No free local TCP/UDP control port")


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
    reply, fields = stats(port)
    # The R 103 verdict is the filtered count's, which lands within two seconds.
    while time.monotonic() < until + 4 and "R 103 measuring" in reply:
        time.sleep(0.3)
        reply, fields = stats(port)
    return reply, fields


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
    parser.add_argument("--performance", action="store_true", help="Measure output pacing and scope stages")
    parser.add_argument("--gpu", action="store_true",
                        help="run on the real renderer (hidden windows), so the GPU analyser measures the R'G'B' frame")
    parser.add_argument("--seconds", type=float, default=12)
    args = parser.parse_args()
    if args.performance:
        return performance(args)
    (ROOT / "build").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="scopes-", dir=ROOT / "build"))
    populate(str(work))
    port = free_port()
    show = work / "data/default.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\nui_scale\t1.5\n", encoding="utf-8")
    env = dict(os.environ, DECKBOY_ROOT=str(work), DECKBOY_STATE_DIR=str(work / "state"),
               DECKBOY_PROJECT=str(show), DECKBOY_COMPANION_PORT=str(port), SDL_AUDIODRIVER="dummy")
    # The software renderer has no GPU analyser, so by default every reading
    # here is the CPU's. --gpu keeps the real renderer and hides the windows.
    if args.gpu:
        env["DECKBOY_HIDE_WINDOWS"] = "1"
    else:
        env.update(SDL_VIDEODRIVER="dummy", DECKBOY_RENDERER="software")
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
        expect(f.get("analyser") == ("gpu" if args.gpu else "cpu"),
               "measured by the " + ("GPU" if args.gpu else "CPU") + " analyser", reply)
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
        stripes = Image.new("RGB", (width, height))
        stripes.putdata([((255, 0, 0) if x % 2 == 0 else (0, 0, 0)) for _ in range(height) for x in range(width)])
        stripes_path = work / "stripes.png"
        stripes.save(stripes_path)

        expect(import_paths(port, work, [bars_path, ramp_path, stripes_path]) == 4,
               "import the bars, the ramp and the stripes")

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

        command(port, "SCOPES BIG waveform")
        expect(command(port, "TAKE 3").startswith("OK"), "TAKE the ramp")
        reply, f = fresh_stats(port, reply)
        luma = f.get("luma", "")
        expect(luma.startswith("0.0..100.0"), "ramp: levels span 0 to 100%", reply)
        expect(int(f.get("hist_bins", 0)) >= 250 and float(f.get("hist_ratio", 9)) <= 1.25,
               "ramp: every level present and the histogram flat", reply)
        snap("ramp")

        # FREEZE holds the reading while the picture changes.
        command(port, "SCOPES FREEZE ON")
        frozen, frozen_fields = stats(port)
        expect(command(port, "TAKE 2").startswith("OK"), "TAKE the bars while frozen")
        time.sleep(2.0)
        held, held_fields = stats(port)
        expect(held_fields['serial'] == frozen_fields['serial'] and
               held.split(' state=')[0] == frozen.split(' state=')[0],
               "FREEZE holds the last reading", held)
        expect(held_fields['state'] == 'frozen' and float(held_fields['hz']) == 0,
               "FREEZE reports frozen and zero refresh", held)
        command(port, "SCOPES BIG parade")
        snap("frozen-ramp-parade")
        command(port, "SCOPES BIG vector")
        snap("frozen-ramp-vector")
        expect(stats(port)[1]['serial'] == frozen_fields['serial'],
               "switching frozen panels keeps the same full-raster reading")
        command(port, "SCOPES BIG off")
        command(port, "SCOPES FREEZE OFF")
        reply, f = fresh_stats(port, frozen)
        expect(f.get("targets75") == "6/6" and int(f.get("hist_bins", 0)) < 50,
               "live again after FREEZE OFF (the bars, not the ramp)", reply)

        # The signal SDI sends. The same stripes read legal as the frame and
        # out of gamut as 4:2:2: every black pixel carries half the red's chroma.
        expect(command(port, "TAKE 4").startswith("OK"), "TAKE the stripes")
        reply, f = fresh_stats(port, reply)
        expect(f.get("signal") == "rgb8" and "R 103 OK" in reply,
               "stripes as the R'G'B' frame: legal (it cannot be otherwise)", reply)
        for line in ("ST2110 ADDR 127.0.0.1", "ST2110 DEPTH 10", "ST2110 ON"):
            command(port, line)
        reply, f = fresh_stats(port, reply)
        # Raw: every black pixel overshoots. Filtered (the R 103 verdict): the
        # standard's spatial filter exists to ignore exactly this single-pixel
        # chroma-subsampling overshoot, so the verdict stays OK.
        expect(f.get("signal") == "ycbcr422-10" and "raw 50.00%" in reply and "R 103 OK" in reply,
               "stripes as ST 2110 10-bit 4:2:2: raw 50% out, R 103 filtered verdict legal", reply)
        snap("stripes-2110")
        command(port, "ST2110 DEPTH 8")
        reply, f = fresh_stats(port, reply, seconds=20)
        expect(f.get("signal") == "ycbcr422-8" and "raw 50.00%" in reply, "...and as 8-bit", reply)
        command(port, "ST2110 DEPTH 10")
        expect(command(port, "TAKE 2").startswith("OK"), "TAKE the bars on ST 2110")
        reply, f = fresh_stats(port, reply, seconds=20)
        expect(f.get("signal") == "ycbcr422-10" and f.get("targets75") == "6/6" and "R 103 OK" in reply,
               "EBU 75% bars as 4:2:2: on target and legal", reply)
        snap("ebu75-2110")
        command(port, "ST2110 OFF")
        reply, f = fresh_stats(port, reply)
        expect(f.get("signal") == "rgb8", "ST2110 OFF: back to the R'G'B' frame", reply)

        command(port, "SCOPES BOTH")
        snap("both")
        command(port, "SCOPES SCOPES")
        # Last (its prompt covers the desk): no card here, DECKLINK ON must say so, and the scopes must not
        # claim to measure an SDI signal nothing is sending.
        refused = command(port, "DECKLINK ON")
        expect(refused.startswith("ERR") and "driver" in refused, "DECKLINK ON with no driver says why", refused)
        reply, f = fresh_stats(port, reply)
        expect(f.get("signal") == "rgb8", "...and the scopes stay on the R'G'B' frame", reply)

        # Real clips: the numbers must be sane and the screens are kept to look at.
        clips = [c for pattern in args.clips for c in sorted(glob.glob(pattern))]
        if clips:
            expect(import_paths(port, work, clips) == 4 + len(clips), f"import {len(clips)} real clips")
        for n, clip in enumerate(clips):
            expect(command(port, f"TAKE {5 + n}").startswith("OK"), f"TAKE {Path(clip).name}")
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


def performance(args):
    """Measure output pacing and scope stages in isolated processes."""
    import ctypes
    import json
    from example_show_runtime import Runtime
    work = ROOT / 'build' / ('scopes-performance-' + time.strftime('%Y%m%d-%H%M%S'))
    work.mkdir(parents=True)
    clip = ROOT / 'build/scopes-performance-60.mp4'
    if not clip.exists():
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
                        'testsrc2=size=1920x1080:rate=60', '-t', '20', '-an',
                        '-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '23',
                        '-pix_fmt', 'yuv420p', str(clip)], check=True)
    def cpu_seconds(db):
        if os.name != 'nt':
            return None
        stamps = [ctypes.c_uint64() for _ in range(4)]
        fn = ctypes.windll.kernel32.GetProcessTimes
        fn.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(ctypes.c_uint64)] * 4
        if not fn(ctypes.c_void_p(int(db.proc._handle)), *[ctypes.byref(t) for t in stamps]):
            raise ctypes.WinError()
        return (stamps[2].value + stamps[3].value) / 1e7
    def output_status(db):
        reply = db.send('STATUS')
        line = next(s for s in reply.splitlines() if s.startswith('OUTPUT 1 '))
        return reply, {k: float(v) for k, v in re.findall(r'(presents|hitches|worst_gap_ms|output_fps)=([\d.]+)', line)}
    results = {'exe': str(Path(args.exe).resolve()), 'seconds': args.seconds,
               'worst_gap_note': 'worst gap is cumulative for the process, hitches are phase deltas',
               'samples': []}
    for width, height in [(1920, 1080), (3840, 2160)]:
        case = work / str(height)
        case.mkdir()
        show = case / 'test.deckboy'
        show.write_text('update_check\t0\nui_sounds\t0\nui_scale\t1.5\n', encoding='utf-8')
        with Runtime(args.exe, case / 'runtime', show, extra=('--import', str(clip))) as db:
            db.commands('SELECT 1', 'LOOP ON', 'TAKE', 'OUTPUT ON', f'VIDEO {width}x{height}', 'VIDEO HZ 60')
            time.sleep(4)
            for mode, commands in [
                ('program-before', ['SCOPES PROGRAM']),
                ('all', ['SCOPES SCOPES', 'SCOPES BIG off']),
                ('both', ['SCOPES BOTH']),
                ('waveform', ['SCOPES SCOPES', 'SCOPES BIG waveform']),
                ('sound', ['SCOPES BIG sound']),
                ('program-after', ['SCOPES PROGRAM']),
            ]:
                db.commands(*commands)
                time.sleep(3)
                scope_start = db.send('SCOPES STATS')
                _, before = output_status(db)
                cpu_start, started = cpu_seconds(db), time.monotonic()
                time.sleep(args.seconds)
                status_text, after = output_status(db)
                elapsed, cpu_end = time.monotonic() - started, cpu_seconds(db)
                scope_end = db.send('SCOPES STATS')
                serial = lambda text: int(re.search(r'serial=(\d+)', text)[1])
                row = dict(raster=f'{width}x{height}', mode=mode, elapsed=elapsed,
                           output_fps=(after['presents'] - before['presents']) / elapsed,
                           hitches=after['hitches'] - before['hitches'], worst_gap_ms=after['worst_gap_ms'],
                           decode_fps=float(re.search(r'decode_fps=([\d.]+)', status_text)[1]),
                           scope_hz=(serial(scope_end) - serial(scope_start)) / elapsed,
                           cpu_cores=None if cpu_start is None else (cpu_end - cpu_start) / elapsed,
                           scope_start=scope_start, scope_end=scope_end, status=status_text)
                results['samples'].append(row)
                print(json.dumps({k: v for k, v in row.items() if k not in ('scope_start', 'scope_end', 'status')}), flush=True)
                (work / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
            db.snapshot(case / 'program.bmp')
    print('performance evidence:', work, flush=True)



if __name__ == "__main__":
    sys.exit(main())
