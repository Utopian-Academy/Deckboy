"""The tracker's pattern features, through the real verbs and the app's own state.

What James's FT2-style tracker design (2026-10-15) adds to a step, each fired
and then read back from STATUS rather than from the setter's reply:
  - a cell's actions: G take a cue, S stop the deck, F fade it out and stop
    it (and put its levels back), V set its volume, O set its opacity
  - mute and solo on a column
  - a step's JUMP to another row, and a step that starts a SECTION
  - all of it saved: a show reloaded fires the same way
Every window hidden; nothing here measures a frame rate. STOP leaves a deck
dark and reracked (state Paused on its first frame), so "stopped" is read as
blank frames counting, which is what the desk's own STOP does.

    python tools/check_tracker_pattern.py
"""

import os
import shutil
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, parse_args  # noqa: E402


def main():
    args = parse_args(__doc__, 5761)
    fails = []

    def note(name, ok, info=""):
        print("  %-60s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append("%s -- %s" % (name, info))

    os.environ["DECKBOY_HIDE_WINDOWS"] = "1"
    saved = None
    with Deckboy(args, prefix="deckboy-trackerpat-") as db:
        db.send("DECK 1")
        for pattern in ("smpte-bars", "full-black", "full-white"):
            db.send("PATTERN ADD " + pattern)
        db.send("DECKADD")
        db.send("DECK 2")
        db.send("PATTERN ADD smpte-bars")
        db.send("PATTERN ADD full-white")
        # An output on: a deck nothing shows does not count blank frames, so a
        # stop on an unfocused deck reads as nothing (measured: with Output 1 on,
        # deck 1 goes black after S and F whichever deck has focus).
        db.send("DECK 1")
        db.send("OUTPUT ON")
        time.sleep(2.0)
        db.send("DECK 2")
        for _ in range(4):
            db.send("TRACKER ADD")

        note("a cell takes a cue (G)", db.send("TRACKER SET 1 1 1").startswith("OK"))
        db.send("TRACKER SET 1 2 1")
        note("...volume (V)", "volume 30%" in db.send("TRACKER SET 2 2 VOLUME 30"))
        note("...opacity (O)", "opacity 40%" in db.send("TRACKER SET 2 1 OPACITY 40"))
        note("...stop (S)", "stop" in db.send("TRACKER SET 3 1 STOP"))
        db.send("TRACKER SET 3 2 STOP")
        note("...fade out (F, tenths)", "fade out 0.5s" in db.send("TRACKER SET 4 2 FADE 5"))

        db.send("TRACKER STEP 1")
        time.sleep(0.6)
        note("G took cue 1 on both decks",
             db.status("DECK 1 ", "active") == "1" and db.status("DECK 2 ", "active") == "1",
             "active %s/%s" % (db.status("DECK 1 ", "active"), db.status("DECK 2 ", "active")))
        db.send("TRACKER STEP 2")
        time.sleep(0.3)
        note("V set deck 2's volume to 30", db.status("DECK 2 ", "vol") == "30", "vol=%s" % db.status("DECK 2 ", "vol"))
        note("O set deck 1's opacity to 40", db.status("DECK 1 ", "opacity") == "40",
             "opacity=%s" % db.status("DECK 1 ", "opacity"))

        # MUTE: step 3 stops both decks -- but not a muted one.
        db.send("TRACKER MUTE 1 ON")
        db.send("TRACKER STEP 3")
        time.sleep(0.4)
        note("a muted column fires nothing (deck 1 not stopped)",
             int(db.status("DECK 1 ", "blank_frames") or 0) == 0, "blank_frames=%s" % db.status("DECK 1 ", "blank_frames"))
        note("...an unmuted one does (deck 2 stopped)",
             int(db.status("DECK 2 ", "blank_frames") or 0) > 0, "blank_frames=%s" % db.status("DECK 2 ", "blank_frames"))
        db.send("TRACKER MUTE 1 OFF")

        # SOLO: only the soloed column fires.
        db.send("TRACKER STEP 1")
        time.sleep(0.4)
        db.send("TRACKER SOLO 2 ON")
        db.send("TRACKER STEP 3")
        time.sleep(0.4)
        note("solo: the soloed column fires (deck 2 stopped)",
             int(db.status("DECK 2 ", "blank_frames") or 0) > 0, "blank_frames=%s" % db.status("DECK 2 ", "blank_frames"))
        note("...the others do not (deck 1 not stopped)",
             int(db.status("DECK 1 ", "blank_frames") or 0) == 0, "blank_frames=%s" % db.status("DECK 1 ", "blank_frames"))
        db.send("TRACKER SOLO 2 OFF")

        # FADE: out over half a second, stopped, levels back for the next cue.
        # On deck 2, which an output is compositing: blank_frames only counts
        # when an output asks a deck for a picture and gets none.
        db.send("TRACKER STEP 1")
        time.sleep(0.4)
        db.send("TRACKER STEP 2")   # volume 30 on deck 2
        time.sleep(0.2)
        db.send("TRACKER STEP 4")
        time.sleep(0.2)
        mid = db.status("DECK 2 ", "vol")
        time.sleep(1.0)
        note("F fades deck 2 (part way after 0.2 s)", mid not in ("", "30") and int(mid or 0) < 30, "vol=%s" % mid)
        note("...then stops it (the deck goes dark)", int(db.status("DECK 2 ", "blank_frames") or 0) > 0,
             "blank_frames=%s" % db.status("DECK 2 ", "blank_frames"))
        note("...and puts its volume back", db.status("DECK 2 ", "vol") == "30",
             "vol=%s" % db.status("DECK 2 ", "vol"))

        # JUMP and SECTION.
        note("JUMP sets a step's next row", "jumps to step 1" in db.send("TRACKER JUMP 4 1"))
        db.send("TRACKER STEP 4")
        time.sleep(0.2)
        reply = db.send("TRACKER GO")
        note("GO after a jumping step goes to that row", "step 1" in reply, reply)
        note("SECTION names a step", "starts section Intro" in db.send("TRACKER SECTION 1 Intro"))
        note("VIEW switches to the classic tracker", "classic" in db.send("TRACKER VIEW CLASSIC"))
        db.send("TRACKER VIEW PATTERN")
        db.send("TRACKER MUTE 2 ON")

        db.send("SAVE")
        time.sleep(1.0)
        saved = open(db.show, encoding="utf-8", errors="replace").read()

    note("saved: the actions", ":V:30" in saved and ":O:40" in saved and ":S:" in saved and ":F:5" in saved,
         "no action fields in the show")
    note("saved: a muted column", "tracker_muted\t2" in saved, "tracker_muted not 2")

    # A show reloaded fires the same way.
    reloaded = Deckboy(args, prefix="deckboy-trackerpat-reload-")
    with open(reloaded.show, "w", encoding="utf-8") as f:
        f.write(saved)
    with reloaded as db:
        time.sleep(1.5)
        db.send("TRACKER MUTE 2 OFF")
        db.send("TRACKER STEP 1")
        time.sleep(0.4)
        db.send("TRACKER STEP 2")
        time.sleep(0.3)
        note("reloaded: V still sets deck 2's volume to 30", db.status("DECK 2 ", "vol") == "30",
             "vol=%s" % db.status("DECK 2 ", "vol"))
        db.send("TRACKER STEP 4")
        time.sleep(1.2)
        reply = db.send("TRACKER GO")
        note("reloaded: the jump survives", "step 1" in reply, reply)

    print()
    if fails:
        print("FAILED (%d):" % len(fails))
        for f in fails:
            print("  " + f)
        sys.exit(1)
    print("tracker pattern: all checks passed")


if __name__ == "__main__":
    main()
