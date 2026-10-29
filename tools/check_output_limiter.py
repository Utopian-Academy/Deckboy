#!/usr/bin/env python3
"""The output limiter is a ceiling on the MIX a device plays, not on each playlist.

Two playlists play tone cues to the system output at -18 dBFS each, one at
1 kHz and one a third of an octave up, so their peaks line up hundreds of times
a second and the sum reaches about -12 dBFS. The limiter's ceiling is -16:
neither playlist alone goes past it, so only a limiter that hears the sum takes
anything off. DECKBOY_LIMITER_CAPTURE writes exactly what the device was given
(after the limiter), and its peak must not pass the ceiling.

Then OFF must take the playlists back off the shared device, and ON again put
them back, with the sound playing throughout.

With --real-audio and two or more playback devices, it also opens a headphone
monitor and an extra send on two devices and asks SDL whether they run: both
sat paused, and silent, from v0.99.374/376 to 0.99.422. No sound is played for
that part, and it uses whatever devices are there, so it is not run in CI.

    py -3 tools/check_output_limiter.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import math
import re
import shutil
import struct
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


def quick_action(name):
    """A QuickAction's number, read from the enum the way the compiler counts it."""
    text = (ROOT / "native/core/types.hpp").read_text(encoding="utf-8")
    body = re.search(r"enum class QuickAction[^{]*\{(.*?)\n\};", text, re.S).group(1)
    value = -1
    for piece in re.sub(r"//[^\n]*", "", body).split(","):
        ident, _, explicit = piece.strip().partition("=")
        if not ident.strip():
            continue
        value = int(explicit.strip(), 0) if explicit.strip() else value + 1
        if ident.strip() == name:
            return value
    raise SystemExit("no QuickAction " + name)


def fields(reply):
    return dict(re.findall(r"(\w+)=(\S+)", reply))


def capture_peak(path):
    data = Path(path).read_bytes()
    if len(data) < 12 or data[:4] != b"DBLC":
        return None, 0, 0
    channels, rate = struct.unpack("<ii", data[4:12])
    samples = struct.unpack("<%df" % ((len(data) - 12) // 4), data[12:12 + ((len(data) - 12) // 4) * 4])
    return max((abs(s) for s in samples), default=0.0), channels, rate


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--real-audio", action="store_true",
                    help="also check the monitor and extra sends on this machine's devices")
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: the output limiter\n")

    work = Path(tempfile.mkdtemp(prefix="deckboy-limiter-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "01-picture-on-screen.deckboy"
    capture = work / "limiter.raw"
    freq_up = quick_action("ToneFreqInc")
    with Runtime(exe, work / "run", show, env={"DECKBOY_LIMITER_CAPTURE": str(capture)}) as db:
        db.send("DECKADD")
        db.send("DECK 2")
        db.send("TONECUE")
        db.send("SELECT LAST")
        db.send("QUICK %d" % freq_up)       # 1 kHz up a third of an octave
        db.send("DECK 1")
        db.send("TONECUE")
        db.send("SELECT LAST")
        reply = db.send("LIMITER ON CEILING -16")
        print("    " + reply)
        v = fields(reply)
        note("on, at -16 dBFS, on the system output", v.get("on") == "1" and v.get("ceiling") == "-16.0", reply)
        db.send("TAKE")
        db.send("DECK 2")
        db.send("TAKE")
        time.sleep(3.0)
        reply = db.send("LIMITER")
        print("    " + reply)
        v = fields(reply)
        note("both playlists on the limited device", v.get("playlists") == "2", reply)
        note("taking off about 4 dB: the SUM passed the ceiling",
             -6.0 < float(v.get("reduction", "0")) < -2.0, v.get("reduction"))
        note("the device is being fed through it", int(v.get("callbacks", "0")) > 20, v.get("callbacks"))
        note("no clipping: the gain did all of it", v.get("clamped") == "0", v.get("clamped"))

        db.send("DECK 1")
        reply = db.send("LIMITER OFF")
        time.sleep(0.8)
        v = fields(db.send("LIMITER"))
        note("OFF takes the playlists off the shared device", v.get("on") == "0" and v.get("playlists") == "0", v)
        db.send("LIMITER ON")
        time.sleep(0.8)
        v = fields(db.send("LIMITER"))
        note("ON again puts them back", v.get("on") == "1" and v.get("playlists") == "2", v)
        status = db.send("STATUS")
        playing = len(re.findall(r"^DECK \d+ .*status=Playing", status, re.M))
        note("both still playing through the switches", playing == 2, playing)

    # OUTPUT EFFECTS, before the limiter: a high pass on the device's mix. A
    # -18 dBFS 1 kHz tone passes an 80 Hz high pass whole; moved to 2 kHz, a
    # second-order filter one octave above the tone takes about 12 dB off it.
    # BYPASS gives it back. Effects alone put the device on the shared bus;
    # CLEAR with the limiter off takes it off again.
    print()
    work3 = Path(tempfile.mkdtemp(prefix="deckboy-output-fx-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work3 / "show")
    show3 = work3 / "show" / "01-picture-on-screen.deckboy"
    with Runtime(exe, work3 / "run", show3) as db:
        db.send("TONECUE")
        db.send("SELECT LAST")
        db.send("TAKE")

        def out(settle=1.2):
            time.sleep(settle)
            reply = db.send("OUTPUTFX")
            return reply, fields(reply)

        db.send("OUTPUTFX ADD hpf")
        reply, v = out()
        print("    " + reply)
        note("a high pass on the device: shared, limiter off", v.get("playlists") == "1" and v.get("limiter") == "0", v)
        note("at 80 Hz the 1 kHz tone passes (-18 dBFS)", abs(float(v.get("out", "0")) + 18.0) < 0.6, v.get("out"))
        db.send("OUTPUTFX SET 1 100")
        reply, v = out()
        print("    " + reply)
        note("at 2 kHz it takes about 12 dB off", -32.0 < float(v.get("out", "0")) < -28.5, v.get("out"))
        db.send("OUTPUTFX BYPASS ON")
        reply, v = out()
        note("BYPASS gives the tone back whole", abs(float(v.get("out", "0")) + 18.0) < 0.6, reply)
        db.send("OUTPUTFX BYPASS OFF")
        reply, v = out()
        note("and back in, it filters again", float(v.get("out", "0")) < -28.5, reply)
        db.send("OUTPUTFX CLEAR")
        reply, v = out(0.8)
        note("CLEAR, limiter off: off the shared device", v.get("playlists") == "0" and v.get("chain") == "none", v)

        # Kept with the show: a chain built, saved, and the show opened again.
        db.send("OUTPUTFX ADD hpf")
        db.send("OUTPUTFX ADD tilt")
        db.send("OUTPUTFX SET 2 25")
        db.send("LIMITER ON CEILING -6")
        built = fields(db.send("OUTPUTFX"))
        time.sleep(1.5)   # the autosave
    saved = [line for line in show3.read_text(encoding="utf-8", errors="replace").splitlines()
             if line.startswith("output_limiter")]
    print("    saved: " + (saved[0] if saved else "(nothing)"))
    with Runtime(exe, work3 / "run2", show3) as db:
        time.sleep(1.0)
        again = fields(db.send("OUTPUTFX"))
        limiter = fields(db.send("LIMITER"))
        note("the chain comes back with the show",
             again.get("chain") == built.get("chain") and again.get("chain", "").startswith("hpf:80"), (built, again))
        note("and the limiter with its ceiling", limiter.get("on") == "1" and limiter.get("ceiling") == "-6.0", limiter)
    shutil.rmtree(work3, ignore_errors=True)

    peak, channels, rate = capture_peak(capture)
    ceiling = 10 ** (-16 / 20)
    if peak is None:
        note("the device's sound was captured", False, "no capture written")
    else:
        print("    captured %d ch at %d Hz: peak %.5f (%.2f dBFS), ceiling %.5f" %
              (channels, rate, peak, 20 * math.log10(max(peak, 1e-9)), ceiling))
        note("nothing the device was given passed the ceiling", peak <= ceiling * 1.0001, peak)
        note("and it was loud enough to need it", peak > ceiling * 0.95, peak)

    if args.real_audio:
        print()
        # The playback devices as `--devices` lists them, the default left out;
        # virtual ones first, since nothing here should come out of a speaker
        # (nothing is played either way).
        import subprocess
        listing = subprocess.run([str(exe), "--devices"], capture_output=True, text=True, timeout=60).stdout
        lines = listing.splitlines()
        start = next((i for i, l in enumerate(lines) if l.startswith("audio out:")), None)
        default = next((l.split(":", 1)[1].strip() for l in lines if l.startswith("audio out default:")), "")
        names = []
        if start is not None:
            for l in lines[start + 1:]:
                if not l.startswith("  "):
                    break
                names.append(l.strip().split("  [")[0].strip())
        others = [n for n in names if n and n != default]
        others.sort(key=lambda n: 0 if re.search(r"steam|virtual|cable|dummy", n, re.I) else 1)
        if len(others) < 2:
            print("    fewer than two other playback devices here: monitor part skipped")
        else:
            work2 = Path(tempfile.mkdtemp(prefix="deckboy-monitor-run-"))
            shutil.copytree(ROOT / "data/demos/starter-pack", work2 / "show")
            show2 = work2 / "show" / "01-picture-on-screen.deckboy"
            with Runtime(exe, work2 / "run", show2, env={"SDL_AUDIODRIVER": None}) as db:
                db.send("AUDIOALSO ADD " + others[0])
                db.send("MONITOR DEVICE " + others[1])
                time.sleep(1.0)
                reply = db.send("MONITOR")
                print("    extra send to %s, monitor on %s" % (others[0], others[1]))
                print("    " + reply)
                note("the monitor and the extra send are running, not paused",
                     "monitor streams 1 (0 paused)" in reply and "extra sends 1 (0 paused)" in reply, reply)
            shutil.rmtree(work2, ignore_errors=True)

    shutil.rmtree(work, ignore_errors=True)
    print()
    if fails:
        print("output limiter: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("output limiter: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
