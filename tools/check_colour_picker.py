#!/usr/bin/env python3
"""The effect colour picker, driven the way an operator drives it.

James, 2026-10-30: colour pickers for the effects that take a colour, picking
from any window, and typed hex/named values. The headless half (typed colours,
the hue maths, the saved colour) is `--colour-check`; this is the running app:

  - FX COLOUR sets a whole colour (strobe) and a hue slot (colour replace), and
    refuses what cannot work: a grey for a hue, a colour on a slot that has none
  - the inspector draws a chip on exactly the rows that take a colour
  - a click on a chip opens the picker on that row; a click on the hue strip,
    or on a swatch, sets the colour
  - PICK FROM SCREEN arms, Enter takes, Esc cancels (DECKBOY_TEST_SCREEN_PICK
    stands in for the screen, which a CI runner does not have to read)
  - Esc closes the picker, and a second click on its chip does too

Every window hidden. CLICK and KEY are local-only test inputs.

    py -3 tools/check_colour_picker.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import json
import os
import re
import shutil
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

fails = []

# ColourPickerHit in native/app/app_colour_picker.ipp, in that order.
HIT_CLOSE, HIT_SCREEN, HIT_HUE, HIT_HEX, HIT_RGB, HIT_DEGREES, HIT_NAMED, HIT_SWATCH, HIT_PANEL = range(9)


def note(name, ok, info=""):
    print("  %-66s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def status(db):
    return json.loads(db.send("STATUS JSON"))


def wait_for(test, seconds=5.0):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if test():
            return True
        time.sleep(0.1)
    return test()


def answer(db, command):
    try:
        return db.send(command)
    except RuntimeError as exc:   # an ERR reply
        return "ERR " + str(exc)


def fx_get(db, n):
    return answer(db, "FX GET %d" % n)


def param(reply, slot):
    m = re.search(r"\b%s=(\d+)" % slot, reply)
    return int(m.group(1)) if m else None


def colour(reply):
    m = re.search(r"colour=#([0-9a-f]{6})", reply)
    return m.group(1) if m else None


def centre(rect):
    x, y, w, h = rect
    return x + w // 2, y + h // 2


def click(db, rect, dx=None):
    x, y = centre(rect)
    if dx is not None:
        x = rect[0] + dx
    db.send("CLICK %d %d" % (x, y))
    time.sleep(0.4)


def chips(db):
    return {c[0]: c[1:] for c in status(db).get("colourChips", [])}


def picker(db):
    return status(db).get("colourPicker", {})


def hit(db, what):
    for h in picker(db).get("hits", []):
        if h[0] == what:
            return h[1:]
    return None


def scroll_to_chip(db, packed):
    """Wheel the inspector until a chip is in view, the way an operator gets
    there. Its rect, or None if it never comes into view."""
    for _ in range(60):
        view = status(db).get("inspectorViewport", [0, 0, 0, 0])
        chip = chips(db).get(packed)
        if not chip or view[3] <= 0:
            return None
        if chip[1] < view[1]:
            way = 1
        elif chip[1] + chip[3] > view[1] + view[3]:
            way = -1
        else:
            return chip
        vx, vy = centre(view)
        db.send("HOVER %d %d WHEEL %d" % (vx, vy, way))
        time.sleep(0.15)
    return None


def run(exe, work, show):
    with Runtime(exe, work / "picker", show, env={"DECKBOY_TEST_SCREEN_PICK": "#00c040"}) as db:
        time.sleep(1.0)
        db.send("FX CLEAR")
        db.send("FX ADD colour_replace")
        db.send("FX ADD strobe")
        db.send("FX ADD blur")

        print("  over the wire")
        reply = answer(db, "FX COLOUR 1 C #0000ff")
        note("FX COLOUR 1 C #0000ff answers OK", reply.startswith("OK"), reply)
        note("...and colour replace's 'to' is blue (240 deg = 67%)", param(fx_get(db, 1), "c") == 67, fx_get(db, 1))
        reply = answer(db, "FX COLOUR 2 #ff2040")
        note("FX COLOUR 2 #ff2040 answers OK", reply.startswith("OK"), reply)
        note("...and the strobe flashes #ff2040", colour(fx_get(db, 2)) == "ff2040", fx_get(db, 2))
        note("a grey for a hue is refused", answer(db, "FX COLOUR 1 A #808080").startswith("ERR"))
        note("a whole colour on colour replace is refused", answer(db, "FX COLOUR 1 #ff0000").startswith("ERR"))
        note("a hue on the strobe is refused", answer(db, "FX COLOUR 2 A #ff0000").startswith("ERR"))
        note("blur takes no colour at all", answer(db, "FX COLOUR 3 #ff0000").startswith("ERR"))
        note("a colour that is not one is refused", answer(db, "FX COLOUR 2 #zz0000").startswith("ERR"))

        print("  the chips")
        time.sleep(0.5)
        have = chips(db)
        # effect index * 8 + slot: colour replace's from (A) and to (C), the
        # strobe's flash to (B), and nothing on blur.
        note("a chip on exactly from, to and flash to", sorted(have) == [0, 2, 9], sorted(have))
        to_chip = scroll_to_chip(db, 2)
        note("the 'to' chip is in view to click", to_chip is not None, chips(db).get(2))
        if not to_chip:
            return

        print("  the picker")
        click(db, to_chip)
        p = picker(db)
        note("a click on the chip opens the picker on that row",
             p.get("open") and p.get("effect") == 0 and p.get("slot") == 2 and not p.get("whole"), p)
        strip = hit(db, HIT_HUE)
        note("the picker has a hue strip", strip is not None, p.get("hits"))
        if strip:
            # A third of the way along is green (120 deg = 33%).
            click(db, strip, dx=strip[2] // 3)
            got = param(fx_get(db, 1), "c")
            note("a click a third along the strip makes 'to' green", got is not None and abs(got - 33) <= 1,
                 fx_get(db, 1))
        swatch = hit(db, HIT_SWATCH)
        note("the picks so far are offered as swatches", swatch is not None, p.get("hits"))
        if swatch:
            click(db, swatch)
            note("a swatch sets the colour (the last pick was green)",
                 abs((param(fx_get(db, 1), "c") or 0) - 33) <= 1, fx_get(db, 1))

        print("  pick from screen")
        screen = hit(db, HIT_SCREEN)
        if screen:
            click(db, screen)
            note("PICK FROM SCREEN arms", wait_for(lambda: picker(db).get("sampling"), 2.0), picker(db))
            db.send("KEY Escape")
            note("Esc cancels the pick and leaves the picker open",
                 wait_for(lambda: not picker(db).get("sampling"), 2.0) and picker(db).get("open"), picker(db))
            note("...and the colour is as it was", abs((param(fx_get(db, 1), "c") or 0) - 33) <= 1, fx_get(db, 1))
            click(db, hit(db, HIT_SCREEN))
            db.send("KEY Return")
            # #00c040 is 140 deg: 39%.
            note("Enter takes the colour under the cursor (#00c040, 140 deg)",
                 wait_for(lambda: param(fx_get(db, 1), "c") == 39, 2.0), fx_get(db, 1))
        else:
            note("the picker has PICK FROM SCREEN", False, p.get("hits"))

        print("  putting it away")
        db.send("KEY Escape")
        note("Esc closes the picker", wait_for(lambda: not picker(db).get("open"), 2.0), picker(db))
        flash = scroll_to_chip(db, 9)
        note("the strobe's chip is in view to click", flash is not None, chips(db).get(9))
        if flash:
            click(db, flash)
            p = picker(db)
            note("the strobe's chip opens a WHOLE-colour picker", p.get("open") and p.get("whole"), p)
            click(db, flash)
            note("a second click on the same chip closes it", not picker(db).get("open"), picker(db))
        note("the desk still answers", db.send("PING").startswith("OK"))


def real_pick(exe, work, show):
    """The real screen, where a check can read one: X11 under xvfb. Windows'
    hook and the Mac's sampler want a person at a real desktop, so there it is
    said, not skipped silently."""
    print("  pick from the real screen")
    if not sys.platform.startswith("linux") or os.environ.get("WAYLAND_DISPLAY"):
        print("  %-66s %s" % ("the real screen is read", "n/a (X11 only here; the others need a person)"))
        return
    with Runtime(exe, work / "real", show) as db:
        time.sleep(1.0)
        db.send("FX CLEAR")
        db.send("FX ADD strobe")
        db.send("FX COLOUR 1 #ff2040")
        flash = scroll_to_chip(db, 1)
        note("the strobe's chip is in view", flash is not None, chips(db).get(1))
        if not flash:
            return
        click(db, flash)
        screen = hit(db, HIT_SCREEN)
        note("the picker opens with PICK FROM SCREEN", screen is not None, picker(db))
        if not screen:
            return
        click(db, screen)
        note("PICK FROM SCREEN grabs the pointer and arms", wait_for(lambda: picker(db).get("sampling"), 3.0),
             picker(db))
        db.send("KEY Right")   # warps the real pointer one pixel
        db.send("KEY Return")
        note("Enter takes the pixel under the real pointer",
             wait_for(lambda: not picker(db).get("sampling"), 3.0), picker(db))
        # The windows are hidden, so the root is whatever xvfb fills it with:
        # anything but the colour that was there proves the screen was read.
        got = colour(fx_get(db, 1))
        note("...and the strobe took the screen's colour (#%s)" % got, got is not None and got != "ff2040",
             fx_get(db, 1))
        note("the desk still answers", db.send("PING").startswith("OK"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: the effect colour picker\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-colour-picker-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "02-running-a-show.deckboy"
    try:
        run(exe, work, show)
        real_pick(exe, work, show)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\ncolour picker: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\ncolour picker: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
