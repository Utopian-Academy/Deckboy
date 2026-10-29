#!/usr/bin/env python3
"""Named outputs and the patch: playlists send to names, and the names land
on the right channels of the right interface, at the crosspoint's level.

Three playlists play tone cues at -18 dBFS, each at its own pitch (1000, 1260
and 1587 Hz), so every channel of the device's feed can be taken apart into
"how much of each playlist is here". The named outputs are the board's own
example on one interface, eight channels wide:

    FOH 1-2, SUB 3, MON 4, PRESS FEED 5-6, and BOOTH on an interface
    that is not there

and the patch is

    1  FOH 0   SUB -6  PRESS FEED 0   BOOTH 0
    2  FOH 0   SUB 0   PRESS FEED 0
    3  FOH -3  MON 0

A one-channel output takes half of each side, so a centred tone arrives at
the level it has on a pair: SUB carries playlist 1 at -24 and playlist 2 at
-18. BOOTH must be reported unpatched and send nowhere. Then playlist 3's tone
cue gets a matrix of its own, by name, onto PRESS FEED only; then playlist 3
leaves the patch and plays to its own device again, on outs 1-2.

What each playlist hands each interface is recorded by the engine itself
(DECKBOY_PATCH_CAPTURE: one file per playlist per interface, at the width it
was opened with), because a test machine's device cannot show it: SDL keeps a
device at the width of whoever opened it first, and the desk's own sounds open
the dummy device in stereo before any playlist. DECKBOY_TEST_DEVICE_CHANNELS
makes that device count as eight channels. Where the device's own feed is
clean -- a playlist out of the patch, in stereo -- it is read too
(DECKBOY_LIMITER_CAPTURE, with the limiter on at 0 dBFS, which nothing reaches).

    py -3 tools/check_audio_patch.py --exe build/windows/Release/Deckboy.exe
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
from check_output_limiter import quick_action  # noqa: E402

TONES = [1000.0, 1000.0 * 2 ** (1 / 3), 1000.0 * 2 ** (2 / 3)]
ABSENT = -60.0

fails = []


def note(name, ok, info=""):
    print("  %-66s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def read_capture(path, magic, sample_bytes):
    """(channels, rate, data) of a capture file, or (0, 0, b"")."""
    data = Path(path).read_bytes() if Path(path).exists() else b""
    if len(data) < 12 or data[:4] != magic:
        return 0, 0, b""
    channels, rate = struct.unpack("<ii", data[4:12])
    return channels, rate, data


def channel_samples(path, magic, sample_bytes, start_byte, seconds):
    """`seconds` of frames from start_byte on, one list per channel, in -1..1."""
    channels, rate, data = read_capture(path, magic, sample_bytes)
    if not channels:
        return 0, rate, []
    frame_bytes = sample_bytes * channels
    start = max(12, start_byte)
    start += (frame_bytes - (start - 12) % frame_bytes) % frame_bytes
    count = min(int(seconds * rate), (len(data) - start) // frame_bytes)
    if sample_bytes == 2:
        values = [v / 32768.0 for v in struct.unpack("<%dh" % (count * channels),
                                                       data[start:start + count * frame_bytes])]
    else:
        values = struct.unpack("<%df" % (count * channels), data[start:start + count * frame_bytes])
    return channels, rate, [values[c::channels] for c in range(channels)]


def tone_db(samples, rate, hz):
    """The level of one pure tone in a channel, dBFS of its peak (Goertzel)."""
    n = len(samples)
    if n == 0:
        return -200.0
    coeff = 2.0 * math.cos(2.0 * math.pi * hz / rate)
    s1 = s2 = 0.0
    for x in samples:
        s1, s2 = x + coeff * s1 - s2, s1
    power = max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)
    amplitude = 2.0 * math.sqrt(power) / n
    return 20.0 * math.log10(amplitude) if amplitude > 1e-10 else -200.0


def patch_file(folder, deck, dest=0):
    return folder / ("deck%d-dest%d.raw" % (deck, dest))


def patch_table(folder, marks, skip=0.4, seconds=1.0):
    """Each playlist's own tone, channel by channel, in what it handed the
    interface after its mark: rows are outs 1-8, columns playlists 1-3."""
    table = [[-200.0] * 3 for _ in range(8)]
    for deck in (1, 2, 3):
        path = patch_file(folder, deck)
        channels, rate, _ = read_capture(path, b"DBPC", 2)
        if not channels:
            continue
        start = marks.get(deck, 12) + int(skip * rate) * 2 * channels
        _, _, per = channel_samples(path, b"DBPC", 2, start, seconds)
        for c in range(min(8, channels)):
            table[c][deck - 1] = tone_db(per[c], rate, TONES[deck - 1])
    return table


def sizes(folder):
    return {deck: patch_file(folder, deck).stat().st_size if patch_file(folder, deck).exists() else 12
            for deck in (1, 2, 3)}


def show_table(table):
    for c, row in enumerate(table):
        print("    out %d: %s" % (c + 1, "  ".join(
            ("%6.1f" % db if db > ABSENT else "     -") for db in row)))


def expect(table, label, wanted):
    """wanted: {channel (1-based): [dB or None per playlist]}"""
    bad = []
    for channel, row in wanted.items():
        for p, want in enumerate(row):
            got = table[channel - 1][p]
            if want is None:
                if got > ABSENT:
                    bad.append("out %d has playlist %d at %.1f" % (channel, p + 1, got))
            elif abs(got - want) > 0.5:
                bad.append("out %d playlist %d %.1f, wanted %.1f" % (channel, p + 1, got, want))
    note(label, not bad, "; ".join(bad))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: named outputs and the patch\n")

    work = Path(tempfile.mkdtemp(prefix="deckboy-patch-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "01-picture-on-screen.deckboy"
    capture = work / "device.raw"
    freq_up = quick_action("ToneFreqInc")
    # The dummy device opened eight wide (SDL_AUDIO_CHANNELS is the width SDL
    # gives a device opened in its own format) and counted as eight.
    wide = {"DECKBOY_TEST_DEVICE_CHANNELS": "8", "SDL_AUDIO_CHANNELS": "8"}

    with Runtime(exe, work / "run", show, env=wide) as db:
        db.send("DECKADD")
        db.send("DECKADD")
        for deck in (1, 2, 3):
            db.send("DECK %d" % deck)
            db.send("TONECUE")
            db.send("SELECT LAST")
            for _ in range(deck - 1):
                db.send("QUICK %d" % freq_up)
        for line in ("OUTPUTS ADD FOH CH 1-2", "OUTPUTS ADD SUB CH 3", "OUTPUTS ADD MON CH 4",
                     "OUTPUTS ADD PRESS FEED CH 5-6",
                     "OUTPUTS ADD BOOTH CH 1-2 DEVICE No Such Interface",
                     "PATCH 1 FOH 0", "PATCH 1 SUB -6", "PATCH 1 PRESS FEED 0", "PATCH 1 BOOTH 0",
                     "PATCH 2 FOH 0", "PATCH 2 SUB 0", "PATCH 2 PRESS FEED 0",
                     "PATCH 3 FOH -3", "PATCH 3 MON 0", "LIMITER ON CEILING 0"):
            db.send(line)
        outputs = db.send("OUTPUTS")
        print("    " + outputs)
        note("FOH, SUB, MON and PRESS FEED reachable on the system output",
             all(re.search(name + r": system output [0-9-]+, ok", outputs)
                 for name in ("FOH", "SUB", "MON", "PRESS FEED")), outputs)
        note("BOOTH on a missing interface: unpatched, and it says why",
             "BOOTH: No Such Interface 1-2, unpatched: not connected" in outputs, outputs)
        print("    " + db.send("PATCH"))
        try:
            db.send("PATCH 1 NOBODY 0")
            refused = ""
        except RuntimeError as error:
            refused = str(error)
        note("a crosspoint to a name that does not exist is refused", "no output called NOBODY" in refused,
             refused)
        time.sleep(1.5)                       # the autosave

    folder = work / "patch"
    folder.mkdir()
    env = dict(wide, DECKBOY_PATCH_CAPTURE=str(folder), DECKBOY_LIMITER_CAPTURE=str(capture))
    with Runtime(exe, work / "run2", show, env=env) as db:
        time.sleep(1.0)
        route = db.send("PATCH ROUTE 1")
        print("    " + route)
        note("playlist 1 opened on one interface at eight channels", "sdl|#8" in route, route)
        note("its gains: FOH pair, SUB both sides at -6 and half, PRESS FEED pair",
             all(cell in route for cell in ("0:1=1.000,0.000", "0:2=0.000,1.000", "0:3=0.251,0.251",
                                            "0:5=1.000,0.000", "0:6=0.000,1.000", "BOOTH=unreached")),
             route)
        for deck in (1, 2, 3):
            db.send("DECK %d" % deck)
            db.send("SELECT LAST")
            db.send("TAKE")
        time.sleep(2.0)
        widths = [read_capture(patch_file(folder, deck), b"DBPC", 2)[0] for deck in (1, 2, 3)]
        note("each playlist hands the interface eight channels", widths == [8, 8, 8], widths)
        table = patch_table(folder, {}, skip=0.8)
        print("    playlist:  1       2       3")
        show_table(table)
        expect(table, "FOH: 1 and 2 at 0 dB, 3 at -3",
               {1: [-18, -18, -21], 2: [-18, -18, -21]})
        expect(table, "SUB (one channel): 1 at -6, 2 at 0, not 3", {3: [-24, -18, None]})
        expect(table, "MON (one channel): 3 alone", {4: [None, None, -18]})
        expect(table, "PRESS FEED: 1 and 2, not 3", {5: [-18, -18, None], 6: [-18, -18, None]})
        expect(table, "nothing on outs 7-8, and nothing for BOOTH",
               {7: [None, None, None], 8: [None, None, None]})

        db.send("DECK 3")
        db.send("SELECT LAST")
        db.send("MATRIX SET 1 PRESS FEED L 100")
        reply = db.send("MATRIX SET 2 PRESS FEED R 100")
        print("    " + reply)
        marks = sizes(folder)
        time.sleep(1.6)
        table = patch_table(folder, marks)
        show_table(table)
        expect(table, "a cue's own matrix, by name: playlist 3 on PRESS FEED only",
               {1: [-18, -18, None], 4: [None, None, None], 5: [-18, -18, -18], 6: [-18, -18, -18]})

        reply = db.send("PATCH 3 CLEAR")
        print("    " + reply)
        time.sleep(0.5)
        before = sizes(folder)[3]
        mark = capture.stat().st_size
        time.sleep(1.6)
        note("out of the patch, playlist 3 hands the patch nothing more",
             sizes(folder)[3] == before, (before, sizes(folder)[3]))
        channels, rate, per = channel_samples(capture, b"DBLC", 4, mark + int(0.4 * 48000) * 8, 1.0)
        own = [tone_db(per[c], rate, TONES[2]) for c in range(min(2, channels))]
        note("and plays to its own device again: -18 on outs 1-2 of the device's feed",
             len(own) == 2 and all(abs(db + 18.0) <= 0.5 for db in own), own)
        route = db.send("PATCH ROUTE 3")
        note("its engine says it is not in the patch", "not in the patch" in route, route)
        status = db.send("STATUS")
        playing = len(re.findall(r"^DECK \d+ .*status=Playing", status, re.M))
        note("all three still playing through the reopens", playing == 3, playing)
        time.sleep(1.5)                       # the autosave

    with Runtime(exe, work / "run3", show, env=wide) as db:
        time.sleep(1.0)
        patch = db.send("PATCH")
        print("    " + patch)
        note("the patch comes back with the show",
             "deck 1: FOH 0 SUB -6 PRESS FEED 0 BOOTH 0" in patch and "deck 3: not in the patch" in patch,
             patch)
        db.send("DECK 3")
        db.send("SELECT LAST")
        matrix = db.send("MATRIX")
        note("and the cue's matrix by name", "L->PRESS FEED L 100%" in matrix and
             "R->PRESS FEED R 100%" in matrix, matrix)
        db.send("OUTPUTS RENAME PRESS FEED TO PRESS")
        patch = db.send("PATCH")
        matrix = db.send("MATRIX")
        note("a rename carries the patch and the matrix",
             "PRESS 0" in patch and "PRESS FEED" not in patch and "L->PRESS L 100%" in matrix,
             (patch, matrix))
        time.sleep(1.5)                       # the autosave

    # THE CONTROLS: Settings > Audio scrolled to the bottom, where OUTPUTS and
    # PATCH are. A click walks a crosspoint, a drag sets it, + ADD AN OUTPUT
    # adds the next pair on the focused playlist's interface.
    with Runtime(exe, work / "run4", show, env=wide,
                 extra=("--ui-size", "1512", "982", "--settings", "1", "--settings-scroll", "5000")) as db:
        time.sleep(1.5)

        def centre(label):
            reply = db.send("PRESS " + label)
            db.send("PRESS OFF")
            x, y, w, h = (int(v) for v in re.search(r" at (-?\d+),(-?\d+) (\d+)x(\d+)", reply).groups())
            return x + w // 2, y + h // 2

        x, y = centre("patch_3_SUB")
        db.send("CLICK %d %d" % (x, y))
        patch = db.send("PATCH")
        note("a click on an off crosspoint turns it on at 0 dB (and playlist 3 joins)",
             re.search(r"deck 3: [^|]*SUB 0\b", patch) is not None, patch)
        db.send("CLICK %d %d" % (x, y))
        patch = db.send("PATCH")
        note("the next click walks it to -3", re.search(r"deck 3: [^|]*SUB -3\b", patch) is not None, patch)
        db.send("CLICK %d %d HOLD" % (x, y))
        db.send("HOVER %d %d" % (x + 12, y))
        db.send("HOVER %d %d" % (x + 24, y))
        db.send("CLICK UP")
        patch = db.send("PATCH")
        note("a drag sets it: 24 px right from -3 is +3 dB",
             re.search(r"deck 3: [^|]*SUB 3\b", patch) is not None, patch)
        db.send("CLICK %d %d HOLD" % (x, y))
        db.send("HOVER %d %d" % (x - 400, y))
        db.send("CLICK UP")
        patch = db.send("PATCH")
        note("dragged all the way down it stops at -48, still on",
             re.search(r"deck 3: [^|]*SUB -48\b", patch) is not None, patch)
        x, y = centre("named_output_add")
        db.send("CLICK %d %d" % (x, y))
        outputs = db.send("OUTPUTS")
        note("+ ADD AN OUTPUT adds the next pair on the playlist's interface",
             "OUT 6: system output 7-8" in outputs, outputs)
    return finish(work)


def finish(work):
    shutil.rmtree(work, ignore_errors=True)
    print()
    if fails:
        print("audio patch: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("audio patch: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
