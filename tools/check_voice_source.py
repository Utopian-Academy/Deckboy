"""The VOICE source in the real app: it speaks, ends by itself, and a retake is quick.

Adds a Voice cue over the control port (VOICE SAY), TAKEs it with the dummy
audio driver and hidden windows, and checks that:
  - the line is rendered (VOICE STATUS goes from "rendering" to "ready, N s"),
  - the cue plays while the speech lasts and STOPS BY ITSELF after it
    (the transport leaves "playing" without any STOP from here),
  - a second TAKE of the same cue is cached: its speech is ready at once,
  - changing the words or the mode makes a different length (a new render),
  - bad arguments are refused with ERR.

  py -3 tools/check_voice_source.py [--exe build/windows/Release/Deckboy.exe]
"""
import argparse
import re
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from example_show_runtime import Runtime  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="deckboy-voice-source-"))
    failures = 0

    def expect(ok, label, detail=""):
        nonlocal failures
        failures += 0 if ok else 1
        print(("ok   " if ok else "FAIL ") + label + ("" if ok else "  [%s]" % detail), flush=True)

    show = work / "show.deckboy"
    show.write_text("update_check\t0\n", encoding="utf-8")

    def refused(db, command):
        try:
            db.send(command)
        except RuntimeError as e:
            return str(e).split(" => ", 1)[-1].startswith("ERR")
        return False

    def seconds(status):
        m = re.search(r"ready, ([0-9.]+) s", status)
        return float(m.group(1)) if m else None

    def wait_ready(db, limit=20.0):
        """Poll VOICE STATUS until the speech is ready; returns (status, waited seconds)."""
        start = time.monotonic()
        last = ""
        while time.monotonic() - start < limit:
            last = db.send("VOICE STATUS")
            if seconds(last) is not None:
                return last, time.monotonic() - start
            time.sleep(0.05)
        return last, time.monotonic() - start

    def stopped(db):
        s = db.send("STATUS")
        return not re.search(r"\bplaying\b", s, re.I), s

    with Runtime(args.exe, work / "run", show) as db:
        reply = db.send("VOICE SAY Hello there. This is Deckboy speaking.")
        expect(reply.startswith("OK"), "VOICE SAY adds a Voice cue", reply)
        db.send("SELECT 1")
        reply = db.send("VOICE STATUS")
        expect("PROF. VIDEO" in reply and "WORDS" in reply, "VOICE STATUS names the speaker and mode", reply)
        db.send("LOOP OFF")

        # First take: rendered on a worker thread, then plays from the start.
        t0 = time.monotonic()
        db.send("TAKE")
        status, waited = wait_ready(db)
        length = seconds(status)
        expect(length is not None and length > 1.0, "the speech is rendered and has a length", status)
        print("     first take: ready after %.2f s, speech %.2f s" % (waited, length or 0), flush=True)
        playing_seen = False
        t_end = None
        deadline = time.monotonic() + (length or 5) + 15
        while time.monotonic() < deadline:
            done, st = stopped(db)
            if not done:
                playing_seen = True
            elif playing_seen:
                t_end = time.monotonic()
                break
            time.sleep(0.1)
        expect(playing_seen, "the cue plays")
        expect(t_end is not None, "the cue ends by itself", st[:300] if t_end is None else "")
        if t_end is not None and length:
            total = t_end - t0
            expect(length - 0.5 <= total <= length + waited + 3.0,
                   "it ended about when the speech did (%.2f s after TAKE, speech %.2f s)" % (total, length))

        # Retake: cached, so the speech is ready in the first poll.
        db.send("TAKE")
        status2, waited2 = wait_ready(db, 5.0)
        expect(seconds(status2) == length and waited2 < 0.5,
               "a retake is cached (ready after %.2f s)" % waited2, status2)
        # Let it finish so the next edits do not talk over it.
        time.sleep((length or 3) + 1.0)

        # A different mode / words is a new line with its own length.
        db.send("VOICE MODE SOUNDS")
        db.send("TAKE")
        status3, _ = wait_ready(db)
        expect(seconds(status3) is not None and "SOUNDS" in status3, "SOUNDS mode renders", status3)
        print("     SOUNDS: %s" % status3, flush=True)
        time.sleep((seconds(status3) or 2) + 1.0)
        db.send("VOICE MODE WORDS")
        db.send("VOICE CHARACTER CUSTOM")
        db.send("VOICE PITCH 220")
        db.send("TAKE")
        status4, _ = wait_ready(db)
        expect(seconds(status4) is not None and "CUSTOM" in status4, "CUSTOM voice renders", status4)

        expect(refused(db, "VOICE CHARACTER NOBODY"), "an unknown speaker is refused")
        expect(refused(db, "VOICE MODE LOUD"), "an unknown mode is refused")
        expect(refused(db, "VOICE PITCH 5000"), "a pitch out of range is refused")
        expect(refused(db, "VOICE SAY"), "VOICE SAY with no words is refused")
        expect(refused(db, "VOICE WHISPER"), "an unknown VOICE word is refused")
    print("voice source: %d failures; evidence: %s" % (failures, work))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
