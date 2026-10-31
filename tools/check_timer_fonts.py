#!/usr/bin/env python3
"""The stage timer's font list, driven the way an operator drives it.

James, 2026-10-30: "when trying to load a font, it doent open a picker of all
available fonts, it asks to load a file, which seems wrong". The list he
approved (board H856G24w):

  - the font row opens a list, not a file dialog
  - Deckboy's own face first, every font on this computer, a font file last
  - typing searches it; Backspace takes the search back
  - a click on a font sets the timer's font and switches the face to it
  - Deckboy (built in) puts the bundled face back

Every window hidden. CLICK and KEY are local-only test inputs. --snap writes
a picture of the open list.

    py -3 tools/check_timer_fonts.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import json
import shutil
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

fails = []


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


def centre(rect):
    x, y, w, h = rect
    return x + w // 2, y + h // 2


def font_row_in_view(db):
    """Wheel the inspector until the font row is in view."""
    for _ in range(60):
        st = status(db)
        view = st.get("inspectorViewport", [0, 0, 0, 0])
        row = st.get("timerFontRow", [0, 0, 0, 0])
        if row[2] <= 0 or view[3] <= 0:
            return None
        if row[1] < view[1]:
            way = 1
        elif row[1] + row[3] > view[1] + view[3]:
            way = -1
        else:
            return row
        vx, vy = centre(view)
        db.send("HOVER %d %d WHEEL %d" % (vx, vy, way))
        time.sleep(0.15)
    return None


def dropdown(db):
    return status(db).get("dropdown", {})


def click_row(db, index):
    """Click the index'th visible row of the open list."""
    d = dropdown(db)
    x = d["rect"][0] + d["rect"][2] // 2
    y = d["listY"] + d["rowHeight"] * index + d["rowHeight"] // 2
    db.send("CLICK %d %d" % (x, y))
    time.sleep(0.4)


def type_text(db, text):
    for ch in text:
        db.send("KEY %s" % ("Space" if ch == " " else ch.upper()))
    time.sleep(0.2)


def run(exe, work, show, snap):
    with Runtime(exe, work / "fonts", show) as db:
        time.sleep(1.0)
        db.send("ADDTIMER")
        db.send("TIMER FACE typeface")
        time.sleep(0.6)
        row = font_row_in_view(db)
        note("a typeface timer has a font row", row is not None, status(db).get("timerFontRow"))
        if not row:
            return
        db.send("CLICK %d %d" % centre(row))
        note("the font row opens a list",
             wait_for(lambda: dropdown(db).get("open") and dropdown(db).get("owner") == "timer.font", 3.0),
             dropdown(db))
        d = dropdown(db)
        if not d.get("open"):
            return
        rows = d.get("rows", [])
        note("Deckboy's own face is first", rows[:1] == ["Deckboy (built in)"], rows[:3])
        note("the list holds this computer's fonts (%d entries)" % d.get("total", 0), d.get("total", 0) >= 3, d)
        if snap:
            db.send("UISNAP " + str(Path(snap).resolve()))
            time.sleep(0.8)

        type_text(db, "deckboy")
        d = dropdown(db)
        note("typing searches the list", d.get("filter") == "deckboy" and d.get("rows") == ["Deckboy (built in)"], d)
        type_text(db, "zzqx")
        note("a search with no match shows nothing to pick", dropdown(db).get("shown") == 0, dropdown(db))
        for _ in range(len("deckboyzzqx")):
            db.send("KEY Backspace")
        time.sleep(0.2)
        d = dropdown(db)
        note("Backspace takes the search back", d.get("filter") == "" and d.get("shown") == d.get("total"), d)
        if d.get("total", 0) < 3:
            return
        pick = d["rows"][1]
        click_row(db, 1)
        st = status(db)
        note("a click on a font closes the list", not st["dropdown"]["open"], st["dropdown"])
        note("...and the timer uses it (%s)" % pick, st.get("timerFont", "") != "", st.get("timerFont"))
        note("...in the typeface face", "typeface" in db.send("TIMER FACE"), db.send("TIMER FACE"))
        if snap:
            db.send("UISNAP " + str(Path(snap).with_name(Path(snap).stem + "-picked.bmp").resolve()))
            time.sleep(0.8)

        # Back to the built-in face, from the list.
        row = font_row_in_view(db)
        if row:
            db.send("CLICK %d %d" % centre(row))
            wait_for(lambda: dropdown(db).get("open"), 3.0)
            # The list opens scrolled to the font in use; Home goes to the top.
            db.send("KEY Home")
            time.sleep(0.2)
            click_row(db, 0)
        note("Deckboy (built in) puts the bundled face back", status(db).get("timerFont", "x") == "",
             status(db).get("timerFont"))
        db.send("KEY Escape")
        note("the desk still answers", db.send("PING").startswith("OK"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--snap", help="write a picture of the open list here (.bmp)")
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: the stage timer's font list\n")
    work = Path(tempfile.mkdtemp(prefix="deckboy-timer-fonts-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "02-running-a-show.deckboy"
    try:
        run(exe, work, show, args.snap)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if fails:
        print("\ntimer fonts: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("\ntimer fonts: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
