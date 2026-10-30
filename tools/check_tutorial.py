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
        # ONE SCREEN (every CI runner): the output fills the desk's screen and
        # the tour holds until a key or a click gives the desk back. Where the
        # output went to a screen of its own, the tour simply moves on.
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            said = status(db)
            if said.startswith("step 3 of") or "has the desk" in said:
                break
            time.sleep(.15)
        if "has the desk" in said:
            time.sleep(1.0)
            assert status(db).startswith("step 2 of"), "the tour moved on behind a screen-filling output"
            db.send(f"OUTSNAP {shots / '02b-desk-eaten.bmp'}")
            time.sleep(1.0)
            db.send("TUTORIAL SHOWME")           # what a key or a click does
            wait_step(db, 3)
            state = json.loads(db.send("STATUS JSON"))
            assert state["outputs"][0]["enabled"], "giving the desk back turned the output off"
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

        # Resume: leave at step 3, come back at step 3. Opening the practice
        # show turns the output off, so the tour goes back for it first: it
        # used to carry on, and the BLACK lesson passed with nothing to see.
        db.send("TUTORIAL START")
        wait_step(db, 1)
        db.send("TUTORIAL SKIP")
        db.send("TUTORIAL SKIP")
        wait_step(db, 3)
        db.send("TUTORIAL EXIT")
        progress = (work / "run/state/tutorial-progress.txt").read_text().strip()
        assert progress == "2", progress
        db.send("TUTORIAL START")
        said = wait_step(db, 2)
        assert "then back to step 3" in said, said
        db.send("TUTORIAL EXIT")                  # left during the detour...
        progress = (work / "run/state/tutorial-progress.txt").read_text().strip()
        assert progress == "2", f"leaving the detour lost the saved step: {progress}"
        db.send("TUTORIAL START")                 # ...and still comes back to it
        wait_step(db, 2)
        db.send("OUTPUT ON")
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            said = status(db)
            if said.startswith("step 3 of") or "has the desk" in said:
                break
            time.sleep(.15)
        if "has the desk" in said:
            db.send("TUTORIAL SHOWME")           # what a key or a click does
        wait_step(db, 3)
        db.snapshot(shots / "11-resumed.bmp")
        db.send("TUTORIAL EXIT")

    after = hashlib.sha256(show.read_bytes()).hexdigest()
    assert before == after, "the operator's show was changed by the tour"
    print("tutorial: 9 steps walked, real actions noticed, autosave ignored, "
          "resume works, the operator's show and desk handed back untouched")
    print("screenshots:", shots)
    handover_checks(args.exe, work)
    screens_checks(args.exe, work)
    crossfade_checks(args.exe, work)


def says(db):
    """What Dr. Pixel is saying now."""
    reply = status(db)
    return reply.split(" says: ", 1)[1] if " says: " in reply else ""


def wait_says(db, start, timeout=4.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if says(db).startswith(start):
            return True
        time.sleep(.15)
    return says(db).startswith(start)


def screens_checks(exe, work):
    """The tour fits the screens in front of the operator (James, 2026-10-30:
    "make it adaptable and reactable to whatever hardware config is being
    used"). TUTORIAL SCREENS stands in for the hardware a runner lacks; on a
    machine with two real screens, the picture is moved for real."""
    pack = ROOT / "data/demos/starter-pack"
    mine = work / "Screens show"
    shutil.copytree(pack, mine)
    show = mine / "01-picture-on-screen.deckboy"
    with Runtime(exe, work / "run3", show) as db:
        # Step 1's instruction, for each kind of rig.
        db.send("TUTORIAL SCREENS 1")
        db.send("TUTORIAL START")
        db.send("TUTORIAL SKIP")
        wait_step(db, 2)
        assert says(db).startswith("Skapi, the screen is empty"), says(db)
        db.send("TUTORIAL SCREENS 2")
        assert wait_says(db, "A second screen, skapi. When the output goes on"), says(db)
        db.send("TUTORIAL SCREENS 1")
        assert wait_says(db, "The other screen went away, skapi. The picture will open on this one"), says(db)
        db.send("TUTORIAL SCREENS mirrored")
        assert wait_says(db, "Your screens are mirrored now, skapi"), says(db)
        db.send("TUTORIAL EXIT")
        for rig, start in (("2", "Two screens, skapi. The desk stays on this one, the picture goes on the other: "),
                           ("3", "Three screens, skapi. The desk stays on this one, the picture goes on "),
                           ("mirrored", "Your screens are mirrored, skapi: "),
                           ("1", "Skapi, the screen is empty")):
            db.send(f"TUTORIAL SCREENS {rig}")
            db.send("TUTORIAL START")
            wait_step(db, 2)
            assert says(db).startswith(start), f"{rig}: {says(db)}"
            db.send("TUTORIAL EXIT")
        # Turned on with the desk's screen the only one, the output takes the
        # desk; the caption and Dr. Pixel's joke say why, for the rig there is.
        for rig, joke in (("1", "It ate the desk, skapi. One screen"),
                          ("mirrored", "It ate the desk, skapi. Mirrored screens"),
                          ("2", "It ate the desk, skapi. You have Two screens")):
            db.send(f"TUTORIAL SCREENS {rig}")
            db.send("TUTORIAL START")
            wait_step(db, 2)
            db.send("OUTPUT ON")
            deadline = time.monotonic() + 8
            said = status(db)
            while time.monotonic() < deadline and not ("has the desk" in said or said.startswith("step 3 of")):
                time.sleep(.15)
                said = status(db)
            if "has the desk" in said:
                db.send("TUTORIAL SHOWME")           # a key or a click
                wait_step(db, 3)
                assert says(db).startswith(joke), f"{rig}: {says(db)}"
            else:
                print(f"  ({rig}: the output did not take the desk here; its joke was not shown)")
            db.send("OUTPUT OFF")
            db.send("TUTORIAL EXIT")
        db.send("TUTORIAL SCREENS real")
        real = json.loads(db.send("STATUS JSON")).get("displayCount", 1)
    print("tutorial screens: step 1, the desk-eaten joke and the change notes fit one, two, three and mirrored")
    if real >= 2:
        real_screens_check(exe, work)
    else:
        print("tutorial screens: one real screen here, so the picture was not moved for real")


def wait_for_output_off_desk(db, desk, timeout=6.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if json.loads(db.send("STATUS JSON"))["outputs"][0]["display"] != desk:
            return True
        time.sleep(.2)
    return False


def real_screens_check(exe, work):
    """Two real screens: the tour puts the picture on the one the desk is not
    on, and when a second screen 'arrives' mid-tour, it moves the picture
    there and gives the desk back."""
    pack = ROOT / "data/demos/starter-pack"
    mine = work / "Real screens show"
    shutil.copytree(pack, mine)
    show = mine / "01-picture-on-screen.deckboy"
    with Runtime(exe, work / "run4", show, env={"DECKBOY_HIDE_WINDOWS": None}) as db:
        db.send("TUTORIAL START")
        wait_step(db, 1)
        db.send("TUTORIAL SKIP")
        wait_step(db, 2)
        state = json.loads(db.send("STATUS JSON"))
        desk = state["deskDisplay"]
        assert says(db).startswith("Two screens, skapi") or says(db).startswith("Three screens"), says(db)
        assert state["outputs"][0]["display"] != desk, f"the practice output is on the desk's screen {desk}"
        db.send("TUTORIAL EXIT")
        # Pretend there is one screen, put the output on the desk's and turn
        # it on over the desk, then let the real second screen 'arrive'.
        db.send("TUTORIAL SCREENS 1")
        db.send("TUTORIAL START")
        wait_step(db, 2)
        db.send(f"DISPLAY {desk}")
        db.send("OUTPUT ON")
        time.sleep(1.5)
        db.send("TUTORIAL SCREENS real")
        assert wait_says(db, "A second screen, skapi! The picture is on it now", 4.0), says(db)
        assert wait_for_output_off_desk(db, desk), json.loads(db.send("STATUS JSON"))["outputs"][0]
        assert "has the desk" not in status(db), status(db)
        db.send("OUTPUT OFF")
        db.send("TUTORIAL EXIT")
    print("tutorial screens: on two real screens the picture went on the other one, and moved there when it 'arrived'")


def crossfade_fixture(kind):
    """A practice show with two cues of one kind and a 0 s deck transition."""
    pack = ROOT / "data/demos/starter-pack"
    lines = (pack / "01-picture-on-screen.deckboy").read_text(encoding="utf8").splitlines()
    templates = [ln.split("\t") for ln in lines if ln.startswith("cue\t") and ln.split("\t")[4] == kind]
    assert templates, f"no {kind} cue in the starter pack to copy"
    out = []
    for line in lines:
        if line.startswith("cue\t"):
            continue
        fields = line.split("\t")
        if fields[0] == "deck":
            fields[3:5] = ["1", "-1"]
            fields[12:14] = ["0", "crossfade"]
        out.append("\t".join(fields))
    for i in range(2):
        fields = templates[0].copy()
        if kind == "video":
            name = "tears-of-steel-street.mp4" if i == 0 else "tears-of-steel-workshop.mp4"
            fields[2] = (pack / "media" / name).as_posix()
        else:
            fields[2] = "pattern://swirl" if i == 0 else "pattern://fireside"
        fields[3] = f"Crossfade {kind} {i + 1}"
        fields[19] = f"crossfade-{kind}-{i + 1}"
        fields[39] = str(i + 1).zfill(2)
        fields[22] = "-1"   # the cue's own transition: inherit the deck's
        fields[24] = "-1"   # no timed follow
        out.append("\t".join(fields))
    return "\n".join(out) + "\n"


def crossfade_checks(exe, work):
    """Step 6, CROSSFADE TO IT (audit 2026-10-30): a video crossfade passes it
    (the engine flag the step used to watch never rose for a playing or a
    hardware-decoded video), and a resumed step 6 needs a DIFFERENT cue taken
    with a crossfade: the first TAKE only makes something live, and taking the
    same cue again does not count."""
    for kind in ("video", "pattern"):
        run = work / f"run-xfade-{kind}"
        state = run / "state"
        state.mkdir(parents=True, exist_ok=True)
        (state / "tutorial-practice.deckboy").write_text(crossfade_fixture(kind), encoding="utf8")
        (state / "tutorial-progress.txt").write_text("5", encoding="utf8")
        pack = ROOT / "data/demos/starter-pack"
        mine = work / f"Xfade show {kind}"
        shutil.copytree(pack, mine)
        with Runtime(exe, run, mine / "01-picture-on-screen.deckboy") as db:
            db.send("TUTORIAL START")
            said = wait_step(db, 2)
            assert "then back to step 6" in said, said
            db.send("OUTPUT ON")
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                said = status(db)
                if said.startswith("step 6 of") or "has the desk" in said:
                    break
                time.sleep(.15)
            if "has the desk" in said:
                db.send("TUTORIAL SHOWME")
            wait_step(db, 6)
            assert says(db).startswith("Nothing is live to fade from"), says(db)
            db.commands("SELECT 1", "TAKE")
            time.sleep(1.5)
            assert status(db).startswith("step 6 of"), f"{kind}: the first TAKE after a resume passed the crossfade"
            db.send("TAKE")
            time.sleep(1.5)
            assert status(db).startswith("step 6 of"), f"{kind}: taking the same cue again passed the crossfade"
            db.commands("SELECT 2", "TAKE")            # a cut, at 0 s
            time.sleep(1.5)
            assert status(db).startswith("step 6 of"), f"{kind}: a cut passed the crossfade"
            assert says(db).startswith("That was a cut"), says(db)
            db.commands("SELECT 1", "TRANSITION 1", "TAKE")
            wait_step(db, 7, timeout=6.0)
            db.send("TUTORIAL EXIT")
    print("tutorial crossfade: video and pattern pass on a real crossfade; resume, re-take and cut do not")


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
