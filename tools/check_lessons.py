#!/usr/bin/env python3
"""Learn the basics, part two: the lesson list and the lessons.

James, 2026-11-05 (tutorials board Cihkd2Se). Each lesson is walked from its
first card to its last with SHOW ME, and every step must be passed by what
SHOW ME did to the desk (the step's own test seeing it), not by the button
moving the card on. Then:

  - the lesson list opens, follows Dr. Pixel or Prof. Video, and ticks a
    finished lesson DONE
  - each lesson ran in its own practice show, and the operator's show is
    handed back as it was
  - a lesson left half way picks up at the same step, unless an older
    Deckboy built its practice show (practiceVersion): then it starts again

Every window hidden, sound to the dummy driver.

    py -3 tools/check_lessons.py --exe build/windows/Release/Deckboy.exe [--keep dir]
"""
import argparse
import json
import re
import shutil
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

SEED = "update_check\t0\nui_sounds\t0\nui_scale\t1.0\n"
LESSONS = ("effects", "datamosh", "draw", "masks", "warp", "multiview", "presets", "go", "words", "timers", "layers", "wrong", "transitions", "sound", "road", "scopes", "outside")
fails = []


def note(name, ok, info=""):
    print("  %-66s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def step_of(db):
    reply = db.send("TUTORIAL STATUS")
    found = re.search(r"step (\d+) of (\d+) (.*?)(?: says:|$)", reply)
    if not found:
        return None, None, reply
    return int(found.group(1)), int(found.group(2)), reply


def lessons(db):
    reply = db.send("LESSONS STATUS")
    return dict(re.findall(r"(\w+):(done|started|new)", reply)), reply


def ndi_names(text):
    return [(row[11], row[13]) for line in text.splitlines()
            if (row := line.split("\t"))[0] == "output_target" and len(row) > 13]


def walk(db, lesson, work, keep, stop_at=None):
    """SHOW ME through every step; returns the steps that would not pass."""
    stuck = []
    step, count, reply = step_of(db)
    while step is not None and step < count:
        if stop_at is not None and step == stop_at:
            return stuck
        if step > 1 and keep and step == count // 2:
            path = work / ("%s.bmp" % lesson)
            db.send("UISNAP %s" % path)
            time.sleep(0.6)
            if path.exists():
                shutil.copy(path, keep / path.name)
        db.send("TUTORIAL SHOWME")
        # Datamosh waits for its moshable copy to be encoded.
        deadline = time.monotonic() + (90 if lesson == "datamosh" else 6)
        now = step
        while time.monotonic() < deadline and now == step:
            time.sleep(0.2)
            now, _, reply = step_of(db)
        if now == step:
            stuck.append("%d (%s)" % (step, reply))
            db.send("TUTORIAL SKIP")
            now, _, reply = step_of(db)
        step = now
    return stuck


def run(exe, work, keep):
    folder = work / "show"
    folder.mkdir()
    show = folder / "show.deckboy"
    show.write_text(SEED, encoding="utf8")
    with Runtime(exe, folder / "run", show) as db:
        time.sleep(1.0)
        db.send("MOTION SENSITIVITY 30")   # something of the operator's to hand back
        before = db.saved()
        state = folder / "run" / "state"

        print("  the list")
        db.send("LESSONS")
        reply = db.send("LESSONS STATUS")
        note("LESSONS opens the list", "open=1" in reply, reply)
        db.send("LESSONS TRACK PIXEL")
        note("following Dr. Pixel is remembered", "track=pixel" in db.send("LESSONS STATUS") and
             "track pixel" in (state / "lessons.txt").read_text())
        db.send("LESSONS TRACK VIDEO")
        if keep:
            path = work / "list.bmp"
            db.send("UISNAP %s" % path)
            time.sleep(0.6)
            if path.exists():
                shutil.copy(path, keep / "lessons_list.bmp")
        db.send("LESSONS CLOSE")
        status, reply = lessons(db)
        note("every lesson is listed, all new", all(status.get(l) == "new" for l in LESSONS + ("bugswat",)), reply)

        for lesson in LESSONS:
            print("  lesson: %s" % lesson)
            db.send("LESSONS START %s" % lesson)
            step, count, reply = step_of(db)
            note("%s starts on step 1 of %s" % (lesson, count), step == 1 and count and count >= 4, reply)
            note("...in its own practice show", (state / ("lesson-%s.deckboy" % lesson)).exists())
            if lesson == "outside":
                practice_names = ndi_names((state / "lesson-outside.deckboy").read_text())
                note("practice NDI names are separate from the operator's", bool(practice_names) and
                     all(fill.startswith("Deckboy Lesson - outside - ") and key == fill + " Key"
                         for fill, key in practice_names) and
                     set(practice_names).isdisjoint(ndi_names(before)), practice_names)
            stuck = walk(db, lesson, work, keep)
            note("every step passed by what SHOW ME did (%d steps)" % count, not stuck, stuck)
            step, count, reply = step_of(db)
            note("...ending on its last card", step == count, reply)
            db.send("TUTORIAL SHOWME")          # LESSON LIST
            status, reply = lessons(db)
            note("finishing ticks it DONE and opens the list", status.get(lesson) == "done" and "open=1" in reply,
                 reply)
            db.send("LESSONS CLOSE")
            after = db.saved()
            if lesson == "outside":
                note("the operator's NDI names are handed back unchanged", ndi_names(after) == ndi_names(before))
            motion = lambda t: next((l for l in t.splitlines() if l.startswith("motion\t")), "")
            note("the operator's show is handed back as it was", motion(after) == motion(before) and
                 "Nebula ahead" not in after and "Look two" not in after and "Contestant" not in after and "Weather map" not in after and "The trunk" not in after and "souffle" not in after and "The planet" not in after and "The beat" not in after and "office PC" not in after and "evidence #2" not in after and "The replay" not in after and "The rocket" not in after and "capsule" not in after and "Saucer" not in after and "Student work" not in after and "STAIRLIFT" not in after and "ghost" not in after.lower(),
                 (motion(before), motion(after)))

        print("  NDI practice names survive resume and older shows are rebuilt")
        db.send("LESSONS START outside")
        walk(db, "outside", work, None, stop_at=3)
        outside_file = state / "lesson-outside.deckboy"
        practice_names = ndi_names(outside_file.read_text())
        db.send("TUTORIAL EXIT")
        db.send("LESSONS START outside")
        step, _, reply = step_of(db)
        note("NDI practice resumes with the same sender names", step == 3 and
             ndi_names(outside_file.read_text()) == practice_names, reply)
        db.send("TUTORIAL EXIT")
        progress = (state / "lessons.txt").read_text().splitlines()
        (state / "lessons.txt").write_text("\n".join(
            "practice outside 1" if line.startswith("practice outside ") else line
            for line in progress) + "\n")
        db.send("LESSONS START outside")
        step, _, reply = step_of(db)
        note("an older NDI practice show starts fresh", step == 1, reply)
        note("...and its sender names remain separate", ndi_names(outside_file.read_text()) == practice_names)
        db.send("TUTORIAL EXIT")

        # James, 2026-11-07: the lessons never said to select the new
        # playlist. The + leaves playlist 1 focused, so a cue added straight
        # after it landed in playlist 1 and the step passed anyway.
        print("  a new playlist must be selected before adding to it")
        db.send("LESSONS START layers")
        time.sleep(0.8)
        walk(db, "layers", work, None, stop_at=2)
        db.send("DECKADD")
        time.sleep(0.6)
        step, _, reply = step_of(db)
        note("the + moves on to SELECT DECK 2", "SELECT DECK 2" in reply, reply)
        db.send("PATTERN ADD checkerboard")
        time.sleep(0.8)
        step2, _, reply = step_of(db)
        note("a cue added to playlist 1 does not pass it", step2 == step, reply)
        db.send("DECK 2")
        time.sleep(0.8)
        step3, _, reply = step_of(db)
        note("clicking Deck 2 passes it", step3 == step + 1, reply)
        # TRICK ONE (2026-11-13): both TAKEs pass it, but only once the
        # chicken has been on screen a moment -- the next step resets the
        # trick, and moving on at once hid the chicken as it appeared.
        note("the chicken is in playlist 2", "chicken" in db.send("TUTORIAL STATUS").lower() and
             "TRICK ONE" in db.send("TUTORIAL STATUS"))
        db.send("TUTORIAL SHOWME")
        time.sleep(0.5)
        step4, _, reply = step_of(db)
        note("the chicken holds before the trick resets", step4 == step3, reply)
        time.sleep(2.0)
        step5, _, reply = step_of(db)
        note("...then TA-DA", step5 == step3 + 1 and "TA-DA" in reply, reply)
        db.send("TUTORIAL EXIT")
        time.sleep(1.0)
        db.send("LESSONS CLOSE")
        time.sleep(0.4)

        # James, 2026-11-07: "Close the DASH (Esc)" in the presets lesson
        # closed the lesson. Esc goes to whatever is open first; only an Esc
        # nothing else wanted leaves the lesson.
        print("  Esc closes what is open before it leaves a lesson")
        db.send("LESSONS START presets")
        time.sleep(0.8)
        db.send("DASH SHOW")
        time.sleep(0.4)
        db.send("KEY Escape")
        time.sleep(0.6)
        step, _, treply = step_of(db)
        reply = db.send("DASH TOGGLE")    # "shown" means Esc had closed it
        db.send("DASH HIDE")
        note("Esc with DASH open closes DASH", "dashboard shown" in reply, reply)
        note("...and the lesson is still running", step is not None, treply)
        db.send("KEY Escape")
        time.sleep(0.8)
        step, _, treply = step_of(db)
        note("the next Esc leaves the lesson", step is None, treply)
        db.send("LESSONS CLOSE")
        time.sleep(0.4)

        print("  picking up where it was left")
        (state / "lessons.txt").write_text("track video\n")
        db.send("LESSONS START masks")
        walk(db, "masks", work, None, stop_at=3)
        db.send("TUTORIAL EXIT")
        status, reply = lessons(db)
        note("a lesson left half way is marked started", status.get("masks") == "started", reply)
        db.send("LESSONS START masks")
        step, _, reply = step_of(db)
        note("...and starts again on the step it was left on (3)", step == 3, reply)
        db.send("TUTORIAL EXIT")

        # A practice show from an older Deckboy is built again: 0.99.427's
        # Sound lesson played a tone, and its practice show has no song.
        print("  an older practice show is built again")
        db.send("LESSONS START sound")
        walk(db, "sound", work, None, stop_at=2)
        db.send("TUTORIAL EXIT")
        db.send("LESSONS START sound")
        step, _, reply = step_of(db)
        note("the Sound lesson picks up on step 2 in its own practice show", step == 2, reply)
        db.send("TUTORIAL EXIT")
        old = [line for line in (state / "lessons.txt").read_text().splitlines()
               if not line.startswith("practice sound")]
        (state / "lessons.txt").write_text("".join(line + "\n" for line in old))
        db.send("LESSONS START sound")
        step, _, reply = step_of(db)
        note("...but one built by an older Deckboy starts again on step 1", step == 1, reply)
        db.send("TUTORIAL EXIT")

        # Caroline sings with the music video, in every voice type but OFF.
        print("  Caroline sings with the music video")
        db.send("LESSONS START datamosh")
        walk(db, "datamosh", work, None, stop_at=3)   # WATCH THE CUTS: the reel is taken
        db.send("TUTORIAL SHOWME")
        for voice in ("CHIP", "WORDS", "SOUNDS"):
            db.send("LESSONS VOICE " + voice)
            reply = ""
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                reply = db.send("LESSONS SONG")
                if "song=playing" in reply:
                    break
                time.sleep(0.5)
            note("she sings in %s" % voice, "song=playing" in reply, reply)
            # Her mouth moves with the song, on the card (it stayed shut in 0.99.429).
            # The song opens with a bar of music, mouth shut: wait for her words.
            seen = set()
            deadline = time.monotonic() + 15
            while time.monotonic() < deadline and len(seen) < 2:
                r = db.send("LESSONS SONG")
                for state in ("open", "shut"):
                    if "drawn=" + state in r:
                        seen.add(state)
                time.sleep(0.05)
            note("her mouth opens and shuts on the card in %s" % voice, seen == {"open", "shut"},
                 "drawn: %s" % sorted(seen))
        db.send("LESSONS VOICE OFF")
        time.sleep(0.6)
        reply = db.send("LESSONS SONG")
        note("...and is quiet with lesson voices OFF", "OFF" in reply and "song=off" in reply, reply)
        db.send("LESSONS VOICE CHIP")
        db.send("TUTORIAL EXIT")

        # RESTART is on every lesson card after the first, Draw's twelve dots
        # included (it was left out there), and a click starts it again.
        print("  RESTART on the card")
        for lesson in ("draw", "sound"):
            db.send("LESSONS START " + lesson)
            walk(db, lesson, work, None, stop_at=2)
            time.sleep(0.5)
            s = json.loads(db.send("STATUS JSON"))
            x, y, w, h = s.get("lessonRestartRect", [0, 0, 0, 0])
            vx, vy, vw, vh = s.get("lessonVoiceRect", [0, 0, 0, 0])
            note("%s: RESTART is on the card" % lesson, w > 0 and h > 0, s.get("lessonRestartRect"))
            apart = x >= vx + vw or y >= vy + vh or vx >= x + w or vy >= y + h
            note("%s: ...clear of VOICE" % lesson, w > 0 and vw > 0 and apart,
                 "%s %s" % (s.get("lessonVoiceRect"), s.get("lessonRestartRect")))
            if w > 0:
                db.send("CLICK %d %d" % (x + w // 2, y + h // 2))
                deadline = time.monotonic() + 5
                step = None
                while time.monotonic() < deadline:
                    step, _, reply = step_of(db)
                    if step == 1:
                        break
                    time.sleep(0.2)
                note("%s: ...and a click starts the lesson again" % lesson, step == 1, step)
            db.send("TUTORIAL EXIT")

        for bad in ("LESSONS START banana", "LESSONS TRACK DOG", "LESSONS WOBBLE"):
            try:
                reply = db.send(bad)
            except RuntimeError as e:
                reply = str(e)
            note("refused: %s" % bad, "ERR" in reply, reply)
        note("the desk still answers", db.send("PING").startswith("OK"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    parser.add_argument("--keep", help="copy the snapshots here")
    args = parser.parse_args()
    keep = Path(args.keep) if args.keep else None
    if keep:
        keep.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="lessons-"))
    try:
        print("lessons:")
        run(Path(args.exe), work, keep)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("FAILED: %d" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
