#!/usr/bin/env python3
"""A MIDI File cue plays Deckboy's own synth (port DECKBOY SYNTH).

Writes a three-note file (C4, E4, G4, half a second each), imports it as a
MIDI File cue on playlist 1, puts a chip synth cue live on playlist 2, sets
the file's port to DECKBOY SYNTH and takes it. The chip must sound each note
in turn -- MIDIFILE reports the chip's frequency -- and must let go when the
file ends. Every window hidden, sound to the dummy driver.

    py -3 tools/check_midi_file_synth.py [--exe build/windows/Release/Deckboy.exe]
"""
import argparse
import re
import struct
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

NOTES = [(60, 261.63), (64, 329.63), (67, 392.00)]


def write_midi(path):
    """Format 0, 480 ticks a quarter, 120 BPM: each note one quarter (0.5 s)."""
    track = bytearray(b"\x00\xff\x51\x03\x07\xa1\x20")   # tempo 500000 us a quarter
    for note, _ in NOTES:
        track += bytes([0x00, 0x90, note, 100])           # on at once
        track += bytes([0x83, 0x60, 0x80, note, 0])        # off 480 ticks later
    track += b"\x00\xff\x2f\x00"
    path.write_bytes(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) +
                     b"MTrk" + struct.pack(">I", len(track)) + bytes(track))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="deckboy-midi-synth-"))
    mid = work / "three_notes.mid"
    write_midi(mid)
    show = work / "show.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\n", encoding="utf-8")
    failures = []

    def expect(ok, what, detail=""):
        print(("ok   " if ok else "FAIL ") + what + (("  (" + detail + ")") if detail and not ok else ""))
        if not ok:
            failures.append(what)

    env = {"SDL_AUDIO_DRIVER": "dummy", "DECKBOY_HIDE_WINDOWS": "1"}
    with Runtime(args.exe, work / "run", show, extra=("--import", str(mid)), env=env) as db:
        time.sleep(2)
        db.send("DECKADD")
        reply = db.send("DECK 2 CHIP ADD NES")
        expect("added NES synth cue" in reply, "CHIP ADD puts a chip synth cue on playlist 2", reply)
        db.send("TAKE")
        # The import at launch can land after the control port opens: on CI's
        # slower machines it had not when this ran (2026-11-15). Wait for it.
        reply, t0 = "", time.monotonic()
        while time.monotonic() - t0 < 30.0:
            try:
                db.send("DECK 1 SELECT 1")
                reply = db.send("MIDIFILE PORT SYNTH")
                break
            except RuntimeError as e:
                reply = str(e)
                time.sleep(0.5)
        expect("DECKBOY SYNTH" in reply, "MIDIFILE PORT SYNTH names the destination", reply)
        db.send("TAKE")
        seen, last = [], ""
        t0 = time.monotonic()
        while time.monotonic() - t0 < 2.6:
            last = db.send("MIDIFILE")
            m = re.search(r"synth ([\d.]+) Hz", last)
            if m and float(m.group(1)) > 0 and (not seen or abs(seen[-1] - float(m.group(1))) > 1.0):
                seen.append(float(m.group(1)))
            time.sleep(0.08)
        print("  chip notes seen: %s" % ", ".join("%.1f" % hz for hz in seen))
        wanted = [hz for _, hz in NOTES]
        expect(len(seen) == 3 and all(abs(a - b) < 1.0 for a, b in zip(seen, wanted)),
               "the chip sounds C4, E4, G4 in turn", ", ".join("%.1f" % hz for hz in seen))
        m = re.search(r"played (\d+) \| holding (\d+) \| synth ([\d.]+) Hz", last)
        expect(bool(m) and m.group(1) == "3", "three notes played", last)
        expect(bool(m) and m.group(2) == "0" and float(m.group(3)) == 0.0,
               "and every one let go when the file ended", last)
    print("midi file to synth: %s" % ("ok" if not failures else "%d failure(s)" % len(failures)))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
