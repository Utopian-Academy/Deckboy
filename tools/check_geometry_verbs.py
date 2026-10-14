"""Exercise geometry verbs, persisted cue fields and inspector mode arrows.

Uses SDL's dummy display and software renderer: no physical output or control
window appears. Each run has isolated state. Run against a freshly built app:
    python tools/check_geometry_verbs.py --exe build/windows/Release/Deckboy.exe
"""

import math
import os
from pathlib import Path
import re
import sys
import time

from deckboy_harness import Deckboy, finish, parse_args
from check_geometry_sweep import differing, read_bmp


def quick_ids():
    source = (Path(__file__).resolve().parents[1] / "native/core/types.hpp").read_text(encoding="utf-8")
    body = source.split("enum class QuickAction {", 1)[1].split("};", 1)[0]
    body = re.sub(r"//[^\n]*|/\*.*?\*/", "", body, flags=re.S)
    return {name.strip(): index for index, name in enumerate(body.split(",")) if name.strip()}


def main():
    args = parse_args(__doc__, 5796)
    os.environ.update(SDL_VIDEO_DRIVER="dummy", SDL_VIDEODRIVER="dummy",
                      SDL_RENDER_DRIVER="software", SDL_AUDIO_DRIVER="dummy",
                      SDL_AUDIODRIVER="dummy", SDL_DISPLAY_USABLE_BOUNDS="0,0,3840,2160")
    fails = []
    checks = 0

    def note(name, ok, detail=""):
        nonlocal checks
        checks += 1
        print("  %-58s %s" % (name, "ok" if ok else "FAIL " + detail), flush=True)
        if not ok:
            fails.append(name + ": " + detail)

    with Deckboy(args, prefix="deckboy-geometry-verbs-") as db:
        print("isolated state:", db.root, flush=True)

        def send(command, error=False):
            reply = db.send(command)
            note(command, reply.startswith("ERR " if error else "OK "), reply)
            return reply

        def numbers(command, expected):
            reply = db.send(command)
            tail = reply.split(":", 1)[-1] if ":" in reply else reply[len("OK " + command):]
            values = [float(n) for n in re.findall(r"-?\d+(?:\.\d+)?", tail)]
            note(command + " reads " + str(expected), reply.startswith("OK ") and
                 len(values) == len(expected) and all(math.isclose(a, b, abs_tol=0.02)
                 for a, b in zip(values, expected)), reply)
            return values

        def selected(index):
            send("SELECT %d" % index)

        def scale(x, y):
            numbers("SCALE", [x, y])

        def read_number(command):
            reply = db.send(command)
            if not reply.startswith("OK "):
                raise RuntimeError(reply)
            return float(re.search(r"-?\d+(?:\.\d+)?", reply).group())

        geometry = ("SCALE", "SCALEX", "SCALEY", "WIDTH", "HEIGHT", "SCALEMODE",
                    "OFFSET", "OFFSETX", "OFFSETY", "ROTATION", "CROP", "CROPLEFT",
                    "CROPRIGHT", "CROPTOP", "CROPBOTTOM")
        for verb in geometry:
            send(verb, error=True)
        for command in ("SCALE 1", "SCALEMODE fit", "OFFSET 10 20", "ROTATION 20", "CROP 0 0 0 0"):
            send(command, error=True)
        help_reply = db.send("HELP")
        help_all = db.send("HELP ALL")
        note("geometry syntax and SYNTH ADD in HELP", all(v in help_reply for v in geometry) and
             "SYNTH ADD" in help_reply, help_reply)
        note("geometry verbs in HELP ALL", all(v in help_all.split() for v in geometry), help_all)

        send("PATTERN ADD smpte-bars")
        send("PATTERN ADD smpte-bars")
        send("MEMOCUE Geometry note")
        for verb in geometry:
            send(verb, error=True)
        selected(1)
        base_w, base_h = read_number("WIDTH"), read_number("HEIGHT")
        send("SELECTALL")
        reply = send("SCALE 0.5")
        note("mixed selection reports eligible and skipped", "2 cues, 1 skipped" in reply, reply)
        for i in (1, 2):
            selected(i)
            scale(0.5, 0.5)
            numbers("WIDTH", [base_w / 2])
            numbers("HEIGHT", [base_h / 2])
        send("SELECTALL")
        send("SCALEX 1")
        for i in (1, 2):
            selected(i)
            scale(1, 1)
        send("SELECTALL")
        send("SCALEY 0.75")
        for i in (1, 2):
            selected(i)
            scale(0.75, 0.75)
        send("SET geometry_aspect_link 0")
        send("SELECTALL")
        send("SCALEX 1")
        send("SCALEY 2")
        for i in (1, 2):
            selected(i)
            scale(1, 2)
        send("SELECTALL")
        send("WIDTH %s" % (base_w / 2))
        send("HEIGHT %s" % base_h)
        for i in (1, 2):
            selected(i)
            scale(0.5, 1)
        send("SET geometry_aspect_link 1")
        send("SELECTALL")
        send("WIDTH %s" % base_w)
        for i in (1, 2):
            selected(i)
            scale(1, 2)
        send("SELECTALL")
        send("HEIGHT %s" % base_h)
        for i in (1, 2):
            selected(i)
            scale(0.5, 1)

        # One cue fits, another linked axis would exceed 4: neither may change.
        selected(1)
        send("SCALE 1")
        selected(2)
        send("SET geometry_aspect_link 0")
        send("SCALEX 1")
        send("SCALEY 2")
        send("SET geometry_aspect_link 1")
        send("SELECTALL")
        send("SCALEX 4", error=True)
        send("WIDTH %s" % (4 * base_w), error=True)
        selected(1)
        scale(1, 1)
        selected(2)
        scale(1, 2)

        for command in ("SCALE 0", "SCALE -1", "SCALE 4.1", "SCALEX 0.2", "SCALEY 5",
                        "SCALE nan", "SCALE inf", "SCALE 1e999", "SCALE 2junk", "SCALE 1 2",
                        "WIDTH 0", "HEIGHT -1", "WIDTH 1", "HEIGHT 999999999",
                        "WIDTH nan", "HEIGHT 25px", "SCALEMODE sideways", "SCALEMODE fit extra",
                        "OFFSET 1", "OFFSET 1 2 3", "OFFSETX inf", "OFFSETY 1000001",
                        "ROTATION 181", "ROTATION -181", "CROP 1 2", "CROP 90 10 0 0",
                        "CROPLEFT -1", "CROPTOP 91", "CROPBOTTOM NaN"):
            send(command, error=True)
        scale(1, 2)
        send("SELECTALL")
        send("SCALE 1")
        send("OFFSET -80 40")
        send("OFFSETX 125.5")
        send("OFFSETY -64")
        send("ROTATION -45")
        send("CROP 10 20 15 25")
        for command in ("CROPLEFT 12", "CROPRIGHT 18", "CROPTOP 14", "CROPBOTTOM 24"):
            send(command)
        for i in (1, 2):
            selected(i)
            numbers("OFFSET", [125.5, -64])
            numbers("OFFSETX", [125.5])
            numbers("OFFSETY", [-64])
            numbers("ROTATION", [-45])
            numbers("CROP", [12, 18, 14, 24])
            for verb, value in zip(geometry[-4:], (12, 18, 14, 24)):
                numbers(verb, [value])

        selected(2)
        send("CROP 10 80 0 0")
        send("SELECTALL")
        send("CROPLEFT 20", error=True)
        selected(1)
        numbers("CROP", [12, 18, 14, 24])
        selected(2)
        numbers("CROP", [10, 80, 0, 0])
        send("CROPLEFT 15")
        numbers("CROP", [15, 80, 0, 0])

        send("SELECTALL")
        send("SCALEMODE fit")
        ids = quick_ids()
        send("QUICK %d" % ids["CycleScaleModePrevious"])
        for i in (1, 2):
            selected(i)
            note("previous mode wraps fit to unscaled", db.send("SCALEMODE").endswith("unscaled"))
        send("SELECTALL")
        send("QUICK %d" % ids["CycleScaleMode"])
        for i in (1, 2):
            selected(i)
            note("next mode wraps unscaled to fit", db.send("SCALEMODE").endswith("fit"))
        for mode in ("fill", "stretch", "unscaled", "fit"):
            send("QUICK %d" % ids["CycleScaleMode"])
            note("next mode " + mode, db.send("SCALEMODE").endswith(mode))
        for mode in ("unscaled", "stretch", "fill", "fit"):
            send("QUICK %d" % ids["CycleScaleModePrevious"])
            note("previous mode " + mode, db.send("SCALEMODE").endswith(mode))
        listing = db.send("QUICKLIST")
        if "Fit/Fill/Stretch/Unscaled" not in listing:
            send("SECTION GEOMETRY")
            time.sleep(0.3)
            listing = db.send("QUICKLIST")
        mode_actions = {int(line.split()[0]) for line in listing.splitlines()
                        if "Fit/Fill/Stretch/Unscaled" in line and line.split()[0].isdigit()}
        note("inspector draws distinct previous and next actions",
             {ids["CycleScaleMode"], ids["CycleScaleModePrevious"]} <= mode_actions, listing)

        # Let earlier geometry edits finish saving first, so SYNTH ADD must
        # schedule its own save rather than ride an already-pending one.
        before_synth = db.saved_show()
        note("prior edits saved before synth creation", len([
            line for line in before_synth.splitlines() if line.startswith("cue\t")]) == 3
            and "\tvideo_synth\t" not in before_synth)
        send("SYNTH ADD extra", error=True)
        send("SYNTH ADD")
        note("SYNTH ADD selects editable synth", "shape=" in db.send("SYNTH"))
        note("new synth saves without a later edit", "\tvideo_synth\t" in db.saved_show())
        send("SYNTH SHAPE rings")
        records = [line.split("\t") for line in db.saved_show().splitlines() if line.startswith("cue\t")]
        note("saved show contains two patterns, memo and synth", len(records) == 4 and
             [r[4] for r in records] == ["pattern", "pattern", "memo", "video_synth"],
             str([r[4] for r in records]))
        if len(records) == 4:
            for i, crop in ((0, [.12, .18, .14, .24]), (1, [.15, .80, 0, 0])):
                row = records[i]
                actual = [float(row[j]) for j in (34, 35, 37, 38, 41, 42, 43, 44, 45)]
                expected = [1, 1, 125.5, -64, -45] + crop
                note("persisted geometry cue %d" % (i + 1), all(math.isclose(a, b, abs_tol=1e-5)
                     for a, b in zip(actual, expected)), str(actual))
            note("unsupported memo geometry unchanged", [float(records[2][j]) for j in
                 (34, 35, 37, 38, 41, 42, 43, 44, 45)] == [1, 1, 0, 0, 0, 0, 0, 0, 0])

        # Read actual presented pixels, on the dummy display only. The saved
        # fields above prove selection semantics; this proves the edit reaches
        # a cue that is already playing, through the engine snapshot path.
        selected(1)
        for command in ("SET geometry_aspect_link 0", "SCALEMODE stretch", "SCALE 0.75",
                        "CROP 0 0 0 0", "OFFSET 0 0", "ROTATION 0", "TAKE", "OUTPUT ON"):
            send(command)
        time.sleep(1)
        shot_index = 0

        def snap():
            nonlocal shot_index
            shot_index += 1
            path = Path(db.root) / ("geometry-%02d.bmp" % shot_index)
            time.sleep(0.4)
            reply = db.send("OUTSNAP " + str(path))
            if not reply.startswith("OK "):
                raise RuntimeError(reply)
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if path.exists() and path.stat().st_size > 1000:
                    time.sleep(0.1)
                    return read_bmp(path)
                time.sleep(0.05)
            raise RuntimeError("no output snapshot: " + str(path))

        for change, reset in (("SCALE 0.5", "SCALE 0.75"),
                              ("SCALEX 0.5", "SCALEX 0.75"),
                              ("SCALEY 0.5", "SCALEY 0.75"),
                              ("OFFSETX 240", "OFFSETX 0"),
                              ("OFFSETY 120", "OFFSETY 0"),
                              ("ROTATION 30", "ROTATION 0"),
                              ("CROP 20 10 0 0", "CROP 0 0 0 0")):
            before = snap()
            send(change)
            after = snap()
            changed, samples = differing(before, after)
            note(change + " changes live pixels", changed > samples * .01,
                 "%d / %d pixels" % (changed, samples))
            send(reset)
            returned, samples = differing(before, snap())
            note(reset + " restores live pixels", returned <= samples * .01,
                 "%d / %d pixels" % (returned, samples))
        send("OUTPUT OFF")
        print("checks:", checks, "failures:", len(fails), flush=True)
    return finish("geometry verbs", fails)


if __name__ == "__main__":
    sys.exit(main())
