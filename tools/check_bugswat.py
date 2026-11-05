#!/usr/bin/env python3
"""Bug Swat, the Motion lesson's game, run through the desk.

James, 2026-11-05 (tutorials board Cihkd2Se, "i dig"). No camera needed:
round 1's SHOW ME PAW puts the waving paw on a playlist and Motion reads it,
which is the game's own no-camera fallback.

  - it starts in a practice show and hands the operator's show back as it
    was, Motion settings and all
  - each briefing waits for its control (PLAY is refused until it is set),
    and SHOW ME sets it: Motion on, two zones, sensitivity 5%, a longer
    fall, a tone with Motion X on its pitch
  - the paw swats bugs in round 1, the clock runs, the score is kept, and
    the high score is written when the game ends
  - the handheld is drawn: the Game Boy greens fill the middle of the desk

Every window hidden, sound to the dummy driver.

    py -3 tools/check_bugswat.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
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

SEED = "update_check\t0\nui_sounds\t0\nui_scale\t1.0\n"
GREENS = {(155, 188, 15), (139, 172, 15), (48, 98, 48), (15, 56, 15)}
fails = []


def note(name, ok, info=""):
    print("  %-66s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def status(db):
    reply = db.send("BUGSWAT STATUS")
    return dict(re.findall(r"(\w+)=([\w/]+)", reply)), reply


def wait_for(db, test, seconds):
    deadline = time.monotonic() + seconds
    st, reply = status(db)
    while time.monotonic() < deadline and not test(st):
        time.sleep(0.25)
        st, reply = status(db)
    return st, reply


def refused(db, command):
    try:
        reply = db.send(command)
    except RuntimeError as e:
        reply = str(e)
    return "ERR" in reply, reply


def read_bmp(path):
    data = Path(path).read_bytes()
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0] // 8
    stride = (width * bpp + 3) & ~3
    flipped = height > 0
    height = abs(height)

    def pixel(x, y):
        row = (height - 1 - y) if flipped else y
        i = offset + row * stride + x * bpp
        return data[i + 2], data[i + 1], data[i]
    return width, height, pixel


def ui_snap(db, path):
    if path.exists():
        path.unlink()
    db.send("UISNAP %s" % path)
    for _ in range(100):
        if path.exists() and path.stat().st_size > 1000:
            time.sleep(0.2)
            return read_bmp(path)
        time.sleep(0.05)
    return None


def green_share(shot):
    """The share of the window's middle third drawn in the four greens."""
    if not shot:
        return 0.0
    width, height, pixel = shot
    hits = total = 0
    for y in range(height // 3, height * 2 // 3, 6):
        for x in range(width // 3, width * 2 // 3, 6):
            total += 1
            hits += pixel(x, y) in GREENS
    return hits / max(1, total)


def run(exe, work, keep):
    folder = work / "show"
    folder.mkdir()
    show = folder / "show.deckboy"
    show.write_text(SEED, encoding="utf8")
    with Runtime(exe, folder / "run", show) as db:
        time.sleep(1.0)
        # The operator's own Motion settings, to be handed back untouched.
        db.send("MOTION SENSITIVITY 30")
        db.send("MOTION FALL 0.5")
        before = db.saved()
        motion_before = next((l for l in before.splitlines() if l.startswith("motion\t")), "")

        print("  starting")
        db.send("BUGSWAT")
        st, reply = status(db)
        note("it starts on round 1's briefing", st.get("phase") == "brief" and st.get("round") == "1", reply)
        note("...not ready until Motion is on", st.get("ready") == "0", reply)
        ok, reply = refused(db, "BUGSWAT PLAY")
        note("PLAY is refused until the round's control is set", ok, reply)
        state = folder / "run" / "state"
        note("it plays in a practice show", (state / "bugswat-practice.deckboy").exists(),
             sorted(p.name for p in state.glob("*")) if state.exists() else "no state dir")

        print("  round 1: Motion on, the paw")
        db.send("BUGSWAT SHOWME PAW")
        st, reply = wait_for(db, lambda s: s.get("ready") == "1", 8)
        note("SHOW ME turns Motion on and it reads the paw", st.get("ready") == "1", reply)
        db.send("BUGSWAT PLAY")
        time.sleep(3.0)
        st, reply = status(db)
        note("PLAY starts the round and its clock runs", st.get("phase") == "play" and int(st.get("time", 0)) >= 2,
             reply)
        st, reply = status(db)
        found = re.search(r"screen=(\d+),(\d+),(\d+),(\d+)", reply)
        if found:
            x, y, w, h = map(int, found.groups())
            db.send("CLICK %d %d" % (x + w // 2, y + h // 3))
            st, reply = status(db)
        note("clicking the screen at a bug gets told to use a hand", "says:" in reply and
             st.get("phase") == "play", reply)
        shot = ui_snap(db, work / "play.bmp")
        share = green_share(shot)
        note("the handheld is drawn in the Game Boy greens (%.0f%% of the middle)" % (share * 100), share > 0.5,
             share)
        if keep:
            shutil.copy(work / "play.bmp", keep / "bugswat_play.bmp")
        st, reply = wait_for(db, lambda s: s.get("phase") != "play", 70)
        score = int(re.search(r"score=(\d+)", reply).group(1)) if "score=" in reply else 0
        note("round 1 ends, by fixes or the clock, onto round 2's briefing",
             st.get("phase") == "brief" and st.get("round") == "2", reply)
        note("the waving paw swatted something (score %d)" % score, score > 0, reply)

        print("  rounds 2-5: each briefing's control")
        note("round 2 waits for two zones", st.get("ready") == "0", reply)
        shot = ui_snap(db, work / "brief.bmp")
        if keep:
            shutil.copy(work / "brief.bmp", keep / "bugswat_brief.bmp")
        db.send("BUGSWAT SHOWME")
        zones = db.send("MOTION ZONE")
        note("SHOW ME draws two zones (%s)" % zones.split(":")[-1].strip(),
             len(re.findall(r"[\d.]+,[\d.]+,[\d.]+,[\d.]+", zones)) == 2, zones)
        st, reply = status(db)
        note("...and round 2 is ready", st.get("ready") == "1", reply)
        db.send("BUGSWAT SKIP")
        st, reply = status(db)
        note("SKIP goes to round 3's briefing, not ready at 30% sensitivity... (practice show: 12%)",
             st.get("round") == "3" and st.get("ready") == "0", reply)
        db.send("BUGSWAT SHOWME")
        st, reply = status(db)
        note("SHOW ME sets sensitivity 5% and round 3 is ready", st.get("ready") == "1", reply)
        db.send("BUGSWAT SKIP")
        st, reply = status(db)
        note("round 4 waits for a longer fall", st.get("round") == "4" and st.get("ready") == "0", reply)
        db.send("BUGSWAT SHOWME")
        st, reply = status(db)
        note("SHOW ME lengthens the fall and round 4 is ready", st.get("ready") == "1", reply)
        db.send("BUGSWAT SKIP")
        st, reply = status(db)
        note("round 5 waits for a tone on Motion X", st.get("round") == "5" and st.get("ready") == "0", reply)
        db.send("BUGSWAT SHOWME")
        st, reply = wait_for(db, lambda s: s.get("ready") == "1", 5)
        note("SHOW ME plays a tone with Motion X on its pitch, and round 5 is ready", st.get("ready") == "1", reply)
        db.send("BUGSWAT PLAY")
        time.sleep(2.0)
        st, reply = status(db)
        note("the boss round plays", st.get("phase") == "play" and st.get("round") == "5", reply)
        db.send("BUGSWAT SKIP")
        st, reply = status(db)
        note("skipping the last round ends the game", st.get("phase") == "over", reply)
        shot = ui_snap(db, work / "over.bmp")
        if keep:
            shutil.copy(work / "over.bmp", keep / "bugswat_over.bmp")
        high = (state / "bugswat-highscore.txt")
        written = high.read_text().strip() if high.exists() else ""
        note("the high score is written (%s)" % written, written == str(score) and score > 0, written)

        print("  handing the desk back")
        db.send("BUGSWAT EXIT")
        st, reply = status(db)
        note("EXIT leaves the game", reply.split()[-2:] and "off" in reply, reply)
        after = db.saved()
        motion_after = next((l for l in after.splitlines() if l.startswith("motion\t")), "")
        note("the operator's Motion settings are as they were", motion_after == motion_before,
             (motion_before, motion_after))
        note("the operator's show did not get the game's cues", "Waving paw" not in after and
             "Heisenbug finder" not in after)
        ok, reply = refused(db, "BUGSWAT SKIP")
        note("refused: BUGSWAT SKIP with no game running", ok, reply)
        ok, reply = refused(db, "BUGSWAT BANANA")
        note("refused: BUGSWAT BANANA", ok, reply)
        note("the desk still answers", db.send("PING").startswith("OK"))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    parser.add_argument("--keep", help="copy the snapshots here")
    args = parser.parse_args()
    keep = Path(args.keep) if args.keep else None
    if keep:
        keep.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="bugswat-"))
    try:
        print("bug swat:")
        run(Path(args.exe), work, keep)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print("FAILED: %d" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
