#!/usr/bin/env python3
"""Check the ~ shapes that follow the sound: audio-low, -mid, -high, and LISTEN.

James, 2026-11-15, from Ponderosa: "i love 1 2 and 3" ... "1 and 2 this
release". A tone cue is stepped to 125 Hz, 1 kHz and 8 kHz with an effect
whose ~ follows each band in turn, and STATUS JSON's audioHeard must put the
tone in that band and not the others. Then the ~ is told to listen to the live
input: a listen-only input opens (the dummy driver's, silent) while Settings'
own input stays off, and it closes again a few seconds after nothing listens.
FX LFO's phase, sync and beats, which it refused until this change, answer OK.

Silent: the dummy audio driver plays nothing and records nothing.
"""
import argparse
import json
import re
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

failures = []


def note(what, ok, detail=""):
    print("  [%s] %s%s" % ("ok" if ok else "FAIL", what, (": " + str(detail)) if detail else ""))
    if not ok:
        failures.append(what)


def quick_actions():
    src = (ROOT / "native/core/types.hpp").read_text(encoding="utf-8")
    body = src[src.index("enum class QuickAction"):]
    body = body[body.index("{") + 1:body.index("};")]
    body = re.sub(r"//[^\n]*", "", body)
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    value, ids = -1, {}
    for entry in [e.strip() for e in body.split(",") if e.strip()]:
        if "=" in entry:
            name, num = [s.strip() for s in entry.split("=", 1)]
            value = int(num, 0)
        else:
            name = entry
            value += 1
        ids[name] = value
    return ids


def heard(db):
    reply = db.send("STATUS JSON")
    return json.loads(reply[reply.index("{"):])["audioHeard"]


def disk_input(exe):
    """The live input with a real signal: SDL's file-backed driver plays an
    8 kHz tone into the "microphone" (and writes the speakers to a file), so
    nothing is heard. The playlist plays 125 Hz meanwhile: each source must
    hear only its own."""
    import math
    import struct
    print("check: a ~ listening to the live input hears it, not the playlist\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-audio-bands-disk-"))
    raw = work / "input.raw"
    second = b"".join(struct.pack("<hh", s, s) for s in
                      (int(8000 * math.sin(2 * math.pi * 8000 * i / 48000)) for i in range(48000)))
    raw.write_bytes(second * 40)
    show = work / "show.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\n", encoding="utf-8")
    env = {"SDL_AUDIO_DRIVER": "disk", "SDL_AUDIODRIVER": "disk",
           "SDL_AUDIO_DISK_INPUT_FILE": str(raw), "SDL_AUDIO_DISK_OUTPUT_FILE": str(work / "speakers.raw"),
           "SDL_AUDIO_FORMAT": "S16", "SDL_AUDIO_FREQUENCY": "48000", "SDL_AUDIO_CHANNELS": "2"}
    ids = quick_actions()
    with Runtime(exe, work / "run", show, env=env) as db:
        time.sleep(1.5)
        db.send("TONECUE")
        db.send("SELECT LAST")
        db.send("FX ADD mosaic")
        for _ in range(9):
            db.send("QUICK %d" % ids["ToneFreqDec"])
        db.send("FX LFO 1 A shape audio-high")
        db.send("FX LFO 1 A listen input")
        db.send("FX ADD blur")
        db.send("FX LFO 2 A shape audio-low")
        db.send("FX LFO 2 A listen playlist")
        db.send("TAKE")
        time.sleep(3.0)
        h = heard(db)
        print("    " + json.dumps(h))
        note("the input is open to listen only", h["inputOpen"] == "listening", h["inputOpen"])
        i, p = h["input"], h["playlist"]
        note("the input's 8 kHz is in its high band", i["high"] > 0.8 and i["low"] < 0.3 and i["mid"] < 0.3, i)
        note("the input's level is the tone's (-12 dBFS)", abs(i["level"] - 0.244) < 0.05, i["level"])
        note("the playlist's 125 Hz is in its own low band", p["low"] > 0.8 and p["high"] < 0.3, p)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    ids = quick_actions()
    down, up = ids["ToneFreqDec"], ids["ToneFreqInc"]
    print("check: the ~ shapes that follow the sound\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-audio-bands-"))
    show = work / "show.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\n", encoding="utf-8")
    env = {"SDL_AUDIO_DRIVER": "dummy", "SDL_AUDIODRIVER": "dummy"}
    with Runtime(ROOT / args.exe if not Path(args.exe).is_absolute() else args.exe, work / "run", show, env=env) as db:
        time.sleep(1.5)
        db.send("TONECUE")
        db.send("SELECT LAST")
        db.send("FX ADD mosaic")
        # Third-octave steps: nine down from 1 kHz is 125 Hz.
        for _ in range(9):
            db.send("QUICK %d" % down)
        db.send("FX LFO 1 A shape audio-low")
        db.send("TAKE")
        time.sleep(2.0)
        for shape, band, steps in (("audio-low", "low", 0), ("audio-mid", "mid", 9), ("audio-high", "high", 9)):
            for _ in range(steps):
                db.send("QUICK %d" % up)
            db.send("FX LFO 1 A shape " + shape)
            time.sleep(2.0)
            h = heard(db)
            p = h["playlist"]
            others = [b for b in ("low", "mid", "high") if b != band]
            note("%s: the tone is in %s, not %s" % (shape, band, " or ".join(others)),
                 p[band] > 0.8 and all(p[o] < 0.3 for o in others), p)
        note("auto hears the playlist while Settings has no input open", h["auto"] == "playlist", h["auto"])
        note("no input is open while nothing listens to one", h["inputOpen"] == "no", h["inputOpen"])

        db.send("FX LFO 1 A listen input")
        time.sleep(1.5)
        h = heard(db)
        note("a ~ on the live input opens one to listen only", h["inputOpen"] == "listening", h["inputOpen"])
        status = db.send("STATUS JSON")
        settings = json.loads(status[status.index("{"):])
        note("Settings' own input stays off", h["auto"] == "playlist", h["auto"])
        note("the dummy input is silent, and is heard as silence",
             h["input"]["level"] < 0.01 and h["input"]["high"] < 0.05, h["input"])
        for what in ("listen playlist", "phase 0.25", "sync on", "beats 2", "sync off", "listen auto"):
            try:
                reply = db.send("FX LFO 1 A " + what)
                note("FX LFO ... %s answers OK" % what, reply.startswith("OK"), reply)
            except RuntimeError as e:
                note("FX LFO ... %s answers OK" % what, False, e)
        try:
            db.send("FX LFO 1 A listen everywhere")
            note("FX LFO ... listen everywhere is refused", False, "answered OK")
        except RuntimeError as e:
            note("FX LFO ... listen everywhere is refused", "auto, playlist or input" in str(e), e)
        db.send("FX LFO 1 A shape sine")
        time.sleep(4.5)
        h = heard(db)
        note("the listen-only input closes once nothing listens", h["inputOpen"] == "no", h["inputOpen"])
        del settings
    print()
    disk_input(args.exe)
    print()
    if failures:
        print("FAILED: %d" % len(failures))
        for f in failures:
            print("  - " + f)
        return 1
    print("all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
