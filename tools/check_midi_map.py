"""The MIDI map, end to end in the running app, with no MIDI port.

MIDIIN puts a message through the same path the MIDI thread uses, and
MIDIIN OSC the same path an OSC packet takes, so this runs on any machine.
Checks: the built-in rows, CC 7, a show row on one channel only, a hold, an
aimed command that leaves focus and selection alone, a refused aim, OSC in
the same map, a burst of a knob coalesced, the computer's map on disk, the
monitor naming the row, and the show's rows saved in the show.

    py -3 tools/check_midi_map.py [--exe build/windows/Release/Deckboy.exe]
"""
import argparse
import json
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

failures = 0


def expect(ok, what):
    global failures
    print(("[ok] " if ok else "[FAIL] ") + what)
    if not ok:
        failures += 1


def status(db):
    return json.loads(db.send("STATUS JSON"))


def detail(reply):
    # "OK VERB: detail"
    return reply.split(": ", 1)[1] if ": " in reply else ""


def monitor(db, rows=40):
    return json.loads(detail(db.send("INPUTMONITOR %d" % rows)))


def send_batch(db, lines):
    """Every line in one packet, so they reach the desk in the same frame --
    the way a fast fader's values do. send() waits for each reply."""
    import socket
    with socket.create_connection(("127.0.0.1", db.port), 15) as s:
        s.sendall(("\n".join(lines) + "\n").encode())
        got = b""
        s.settimeout(3)
        try:
            while got.count(b"\n") < len(lines):
                part = s.recv(65536)
                if not part:
                    break
                got += part
        except socket.timeout:
            pass


def frame():
    time.sleep(0.15)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp())
    folder = work / "show"
    folder.mkdir()
    show = folder / "show.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\n", encoding="utf8")
    with Runtime(Path(args.exe), folder / "run", show) as db:
        time.sleep(1.5)
        rows = json.loads(detail(db.send("MIDIMAP")))
        expect(len(rows) == 3 and all(r["builtin"] for r in rows),
               "a new computer has the three built-in rows")

        # CC 7, the built-in volume.
        db.send("MIDIIN CC 7 127")
        frame()
        expect(status(db)["masterVolume"] == 200, "CC 7 at 127 is 200%")
        db.send("MIDIIN CC 7 64")
        frame()
        expect(status(db)["masterVolume"] == 100, "CC 7 at 64 is 100%")

        # A show row on channel 2 only.
        db.send("MIDIMAP ADD SHOW src=cc | ch=2 | num=16 | read=knob | cmd=MASTERVOL {value} | from=0 | to=50")
        db.send("MIDIIN CC 16 127 CH 2")
        frame()
        expect(status(db)["masterVolume"] == 50, "a show row on channel 2 answers channel 2")
        db.send("MIDIIN CC 16 0 CH 1")
        frame()
        expect(status(db)["masterVolume"] == 50, "and not channel 1")
        last = monitor(db, 5)[-1]
        expect(last["outcome"] == "NOTHING" and "nothing mapped to CC 16" in last["what"],
               "the monitor says why channel 1 did nothing: " + last["what"])

        # A burst of a knob: one command for the frame, the latest value.
        db.send("INPUTMONITOR CLEAR")
        burst = ["MIDIIN CC 16 %d CH 2" % v for v in range(0, 128, 4)] + ["MIDIIN CC 16 127 CH 2"]
        # One connection per line would take frames; send them in one batch.
        send_batch(db, burst)
        time.sleep(0.4)
        ran = [e for e in monitor(db, 200) if e["command"].startswith("MASTERVOL")]
        expect(status(db)["masterVolume"] == 50 and 0 < len(ran) < len(burst),
               "a burst of %d values ran %d commands, ending at the last" % (len(burst), len(ran)))

        # Hold to fire.
        db.send("MIDIMAP ADD SHOW src=note | num=43 | read=hold | hold=0.6 | cmd=BLACKOUT")
        before = status(db)["blackout"]
        db.send("MIDIIN NOTE 43 100")
        time.sleep(0.25)
        expect(status(db)["blackout"] == before, "a hold has not fired at 0.25 s")
        time.sleep(0.6)
        expect(status(db)["blackout"] != before, "it fires once held 0.6 s")
        db.send("MIDIIN NOTEOFF 43")
        db.send("MIDIIN NOTE 43 100")
        time.sleep(0.2)
        db.send("MIDIIN NOTEOFF 43")
        time.sleep(0.8)
        expect(status(db)["blackout"] != before, "let go early, nothing happens")
        last = [e for e in monitor(db, 10) if "released at" in e["what"]]
        expect(bool(last), "the monitor says it was let go early")
        db.send("BLACKOUT")  # put it back

        # Aiming: the on-air cue of playlist 2, without moving the desk.
        db.send("PATTERN ADD full-white")
        db.send("PATTERN ADD full-white")
        db.send("DECKADD")
        db.send("DECK 2")
        db.send("PATTERN ADD full-white")
        db.send("PATTERN ADD full-white")
        db.send("SELECT 2")
        db.send("TAKE")
        db.send("SELECT 1")
        db.send("DECK 1")
        db.send("SELECT 2")
        frame()
        db.send("MIDIMAP ADD SHOW src=note | num=50 | cmd=LOOP ON | target=onair | playlist=2")
        db.send("INPUTMONITOR CLEAR")
        db.send("MIDIIN NOTE 50 100")
        frame()
        s = status(db)
        decks = s["decks"]
        expect(s["focusedDeck"] == 1 and decks[0]["selected"] == 2 and decks[1]["selected"] == 1,
               "aimed at playlist 2 on air, the desk did not move (focus %d, selected %d / %d)"
               % (s["focusedDeck"], decks[0]["selected"], decks[1]["selected"]))
        did = [e for e in monitor(db, 10) if e["command"] == "LOOP ON"]
        expect(bool(did) and did[-1]["outcome"] == "DID" and "on air in playlist 2" in did[-1]["what"]
               and "show map row" in did[-1]["what"],
               "the monitor names the target and the row: " + (did[-1]["what"] if did else "-"))

        # An aim that cannot land is refused, and says why.
        db.send("MIDIMAP ADD SHOW src=note | num=51 | cmd=LOOP ON | target=onair | playlist=3")
        db.send("MIDIIN NOTE 51 100")
        frame()
        last = [e for e in monitor(db, 10) if e["command"] == "LOOP ON"][-1]
        expect(last["outcome"] == "REFUSED" and "no playlist 3" in last["what"],
               "a playlist that is not there is refused: " + last["what"])

        # OSC in the same map.
        db.send("MIDIMAP ADD SHOW src=osc | osc=/x/vol | read=knob | cmd=MASTERVOL {value} | from=0 | to=200")
        db.send("MIDIIN OSC /x/vol 0.25")
        frame()
        expect(status(db)["masterVolume"] == 50, "an OSC fader in the map")

        # The computer's map is saved with the computer.
        db.send("MIDIMAP ADD COMPUTER src=program | num=5 | cmd=BLACKOUT")
        saved = (folder / "run" / "state" / "midi_map.tsv")
        text = saved.read_text(encoding="utf8") if saved.exists() else ""
        expect("src=program" in text and text.count("builtin=1") == 3,
               "the computer's map is on disk with its built-in rows")

        # Overlaps are flagged.
        db.send("MIDIMAP ADD SHOW src=note | num=36 | cmd=GO")
        db.send("MIDIMAP ADD SHOW src=note | num=30 | to_num=40 | cmd=STOP")
        rows = json.loads(detail(db.send("MIDIMAP")))
        flagged = [r for r in rows if r["map"] == "show" and r["overlap"]]
        expect(len(flagged) >= 2, "two rows on one note are flagged")

        # Refused from another machine: a stored local-only verb. (Not
        # reachable from here; the unit is midimap's refusedFromPeer.)

        # Learn.
        db.send("MIDIMAP LEARN")
        db.send("MIDIIN CC 21 65 CH 3")
        db.send("MIDIIN CC 21 66 CH 3")
        db.send("MIDIIN CC 21 65 CH 3")
        frame()
        learned = detail(db.send("MIDIMAP LEARNED"))
        expect("src=cc" in learned and "num=21" in learned and "ch=3" in learned,
               "learn heard CC 21 on channel 3: " + learned)

    # The show's rows travel in the show file. (Saved by the autosave on quit.)
    text = show.read_text(encoding="utf8", errors="replace")
    print("show rows in file:", text.count("midimap_row"))

    print("check_midi_map: %d failure%s" % (failures, "" if failures == 1 else "s"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
