"""Drop a .mid into a show and see whether Deckboy makes a playable cue of it.

The parse itself is covered in --smoke, on bytes. This is the other half, the
half that is easy to get wrong: does IMPORT recognise the file, does the cue
come out with a LENGTH, does it survive a save, does taking it run a transport
-- and is a file that only wears the extension refused.

It cannot check that notes reach an instrument: there is no MIDI hardware on a
CI runner. It says so rather than pretending.

    python3 tools/check_midifile.py [--exe PATH] [--port N]
"""

import io
import os
import shutil
import struct
import sys
import tempfile
import time

from deckboy_harness import Deckboy, finish, parse_args


def varlen(n):
    out = bytearray([n & 0x7F])
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(out)


def write_midi(path):
    """Two seconds of quarter notes at 120 bpm, 96 ticks per quarter."""
    track = bytearray()
    for i in range(8):
        track += varlen(0) + bytes([0x90, 60 + i, 0x40])
        track += varlen(96) + bytes([0x80, 60 + i, 0x40])
    track += varlen(0) + bytes([0xFF, 0x2F, 0x00])
    data = b"MThd" + struct.pack(">IHHH", 6, 0, 1, 96)
    data += b"MTrk" + struct.pack(">I", len(track)) + bytes(track)
    io.open(path, "wb").write(data)
    return 8 * 0.5


def main():
    args = parse_args(__doc__, 5635)
    fails = []
    media = tempfile.mkdtemp(prefix="dbmidi-media-")
    midi_path = os.path.join(media, "stinger.mid")
    expected = write_midi(midi_path)
    impostor = os.path.join(media, "notreally.mid")
    io.open(impostor, "wb").write(b"RIFF....this is not a midi file at all........")

    # Imported on the command line: there is no remote verb that adds media.
    with Deckboy(args, "dbmidi-", ["--import", midi_path]) as db:
        time.sleep(2.0)
        print("1  imported at launch: %s" % os.path.basename(midi_path))
        db.send("SELECT LAST")
        time.sleep(0.5)

        report = db.send("MIDIFILE")
        print("2  reports:     %s" % report[:110])
        if "UNREADABLE" in report or report.startswith("ERR"):
            fails.append("the imported cue does not report as a readable MIDI file: %s" % report)
        if "8 event" not in report and "16 event" not in report:
            fails.append("the event count is not what the file contains: %s" % report)
        print("3  port:        %s" % db.send("MIDIFILE PORT")[:60])

        # Nothing is heard without MIDI hardware, but the cue must still run a
        # transport rather than sit at zero.
        db.send("TAKE")
        time.sleep(1.5)
        pos = db.status("DECK 1", "pos")
        print("4  after TAKE:  pos=%s" % pos)
        if pos in ("", "00:00.0"):
            fails.append("taking the cue did not start a transport (pos stayed at %r)" % pos)

        # Read AFTER the take: STATUS describes the active cue.
        dur = db.status("DECK 1", "dur")
        print("5  duration:    %s   (the file is %.1fs)" % (dur, expected))
        if dur in ("", "00:00.0"):
            fails.append("the cue has no duration, so it never ends and never advances")
        db.send("STOP")

        # The kind has to survive a save, or it comes back as a video cue
        # pointed at a .mid, which racks and never plays.
        has_kind = "\tmidifile\t" in db.saved_show()
        print("6  saved kind:  %s" % has_kind)
        if not has_kind:
            fails.append("the cue was not saved as a midifile cue")

    # The impostor, in its own launch: refused at import, not racked as a cue
    # that does nothing when taken.
    with Deckboy(args, "dbmidi2-", ["--import", impostor], port=args.port + 1) as db:
        time.sleep(4.0)
        made_a_cue = "notreally" in db.send("STATUS")
        print("7  impostor:    made a cue: %s  (it must not)" % made_a_cue)
        if made_a_cue:
            fails.append("a file that is not a MIDI file was imported as one anyway")

    shutil.rmtree(media, ignore_errors=True)
    return finish("midi file source", fails, notes=(
        "NOT CHECKED: that notes reach an instrument. There is no MIDI output here, so",
        "the send path is covered only by the parse tests in --smoke and by the port-open",
        "code being shared with the MIDI cue.",
        ""))


if __name__ == "__main__":
    sys.exit(main())
