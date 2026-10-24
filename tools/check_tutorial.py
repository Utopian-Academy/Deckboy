"""Walk LEARN THE BASICS from start to finish and check it never touches the
operator's own show.

Drives the tour over the control port (TUTORIAL START/SHOWME/SKIP/EXIT/STATUS)
and, where a step can be done for real from the wire, does the REAL action
(OUTPUT ON, SELECT + TAKE, TRANSITION + TAKE), so the step has to notice it by
itself. Writes a screenshot of every step to build/tutorial-check/.

python tools/check_tutorial.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import time

from example_show_runtime import ROOT, Runtime


def status(db):
    reply = db.send("TUTORIAL STATUS")
    return reply.split(" ", 2)[2] if reply.count(" ") >= 2 else reply


def wait_step(db, number, timeout=8.0):
    deadline = time.monotonic() + timeout
    last = ""
    while time.monotonic() < deadline:
        last = status(db)
        if last.startswith(f"step {number} of"):
            return last
        time.sleep(.15)
    raise AssertionError(f"expected step {number}, tour says: {last}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True)
    args = parser.parse_args()
    work = ROOT / "build/tutorial-check"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    # The operator's own show: an example show copied somewhere of its own.
    pack = ROOT / "data/demos/starter-pack"
    mine = work / "My show"
    shutil.copytree(pack, mine)
    show = mine / "01-picture-on-screen.deckboy"
    before = hashlib.sha256(show.read_bytes()).hexdigest()
    shots = work / "shots"

    with Runtime(args.exe, work / "run", show) as db:
        db.send("TUTORIAL START")
        wait_step(db, 1)
        db.snapshot(shots / "01-hello.bmp")
        db.send("TUTORIAL SHOWME")               # START
        wait_step(db, 2)
        db.snapshot(shots / "02-output-menu.bmp")
        db.send("OUTPUT ON")                      # the real thing
        wait_step(db, 3)
        db.snapshot(shots / "03-import.bmp")
        db.send("TUTORIAL SHOWME")               # lends a clip
        wait_step(db, 4)
        db.snapshot(shots / "04-take.bmp")
        db.commands("SELECT 1", "TAKE")           # the real thing
        wait_step(db, 5)
        db.snapshot(shots / "05-next.bmp")
        db.send("TUTORIAL SHOWME")               # a second cue, selected
        wait_step(db, 6)
        db.snapshot(shots / "06-crossfade.bmp")
        db.commands("TRANSITION 1", "TAKE")       # the real thing
        wait_step(db, 7)
        db.snapshot(shots / "07-black.bmp")
        db.send("TUTORIAL SHOWME")               # BLACK
        wait_step(db, 8)
        db.snapshot(shots / "08-save.bmp")
        # An autosave must NOT count as the operator's save.
        db.send("CUENOTES 1 autosave should not finish the save step")
        time.sleep(2.5)
        assert status(db).startswith("step 8 of"), "an autosave passed the save step"
        db.send("TUTORIAL SHOWME")               # Ctrl+S
        wait_step(db, 9)
        db.snapshot(shots / "09-finish.bmp")
        practice = work / "run/state/tutorial-practice.deckboy"
        assert practice.is_file(), "no practice show"
        text = practice.read_text(encoding="utf8")
        assert "pattern://swirl" in text and "pattern://fireside" in text, "practice show lacks its cues"
        db.send("TUTORIAL EXIT")                  # CLOSE
        assert status(db) == "off"
        time.sleep(1)
        db.snapshot(shots / "10-back-home.bmp")
        # The desk is handed back as it was lent: output off, BLACK off.
        state = json.loads(db.send("STATUS JSON"))
        assert not state["blackout"], "the tour left BLACK on"
        assert not any(o["enabled"] for o in state["outputs"]), "the tour left an output on"
        assert not (work / "run/state/tutorial-progress.txt").exists(), "finished tour left progress"

        # Resume: leave at step 3, come back at step 3.
        db.send("TUTORIAL START")
        wait_step(db, 1)
        db.send("TUTORIAL SKIP")
        db.send("TUTORIAL SKIP")
        wait_step(db, 3)
        db.send("TUTORIAL EXIT")
        progress = (work / "run/state/tutorial-progress.txt").read_text().strip()
        assert progress == "2", progress
        db.send("TUTORIAL START")
        wait_step(db, 3)
        db.snapshot(shots / "11-resumed.bmp")
        db.send("TUTORIAL EXIT")

    after = hashlib.sha256(show.read_bytes()).hexdigest()
    assert before == after, "the operator's show was changed by the tour"
    print("tutorial: 9 steps walked, real actions noticed, autosave ignored, "
          "resume works, the operator's show and desk handed back untouched")
    print("screenshots:", shots)
    handover_checks(args.exe, work)


def refused(db, command):
    """True when the app answered ERR (Runtime.send raises on ERR)."""
    try:
        db.send(command)
    except RuntimeError as error:
        return "ERR" in str(error)
    return False


def handover_checks(exe, work):
    """The three cases the 2026-10-24 audit found: an armed output must come
    back armed, a playing show must not be taken over, and a show that could
    not be saved must keep its edit on the desk instead of starting the tour."""
    pack = ROOT / "data/demos/starter-pack"
    mine = work / "Desk show"
    shutil.copytree(pack, mine)
    show = mine / "01-picture-on-screen.deckboy"
    with Runtime(exe, work / "run2", show) as db:
        def output_on():
            return json.loads(db.send("STATUS JSON"))["outputs"][0]["enabled"]
        # 1. Armed, nothing playing: the tour may run, and hands it back armed.
        db.send("OUTPUT ON")
        time.sleep(1)
        assert output_on(), "fixture: output did not arm"
        db.send("TUTORIAL START")
        wait_step(db, 1)
        db.send("TUTORIAL EXIT")
        time.sleep(1.5)
        assert output_on(), "an output that was on came back off"
        db.send("OUTPUT OFF")
        # 2. Playing: refused, and nothing changes.
        db.commands("DECK 1", "SELECT 1", "TAKE")
        time.sleep(1)
        assert refused(db, "TUTORIAL START"), "the tour took over a playing show"
        assert status(db) == "off"
        db.send("STOP")
        time.sleep(0.5)
        # 3. The show's folder becomes unavailable, then an edit: refused,
        #    and the edit is still on the desk.
        aside = work / "Desk show (moved aside)"
        mine.rename(aside)
        mine.write_text("not a folder", encoding="utf8")
        try:
            db.commands("SELECT 1", "RENAME UNSAVED OFFLINE MARKER")
            time.sleep(2.5)
            assert refused(db, "TUTORIAL START"), "the tour started although the show could not be saved"
            assert status(db) == "off"
            assert "UNSAVED OFFLINE MARKER" in db.send("STATUS CUES"), "the unsaved edit left the desk"
        finally:
            mine.unlink()
            aside.rename(mine)
    print("tutorial hand-over: armed output restored, playing show refused, unsaved edit kept")


if __name__ == "__main__":
    main()
