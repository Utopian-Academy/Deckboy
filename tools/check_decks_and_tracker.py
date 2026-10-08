"""Removing a playlist can be taken back, and the tracker keeps its own steps.

WHY THIS EXISTS. Two things an operator lost on a real show:

  1. The - beside the playlist tabs removes the FOCUSED playlist, which with
     two side by side is whichever was last touched -- and it took a deck of
     nearly two thousand cues when the nearly empty one beside it was meant.
     Removing a playlist now pushes an undo, so UNDO must bring every cue back.
  2. Every "+ step" in the tracker added a master cue to a playlist. A step now
     lives in the tracker alone: adding one must not change any playlist, it
     must be able to fire EVERY playlist, and it must survive a save.

Driven over the socket, read back from the replies and the saved show.

    python tools/check_decks_and_tracker.py
"""

import os
import socket
import subprocess
import tempfile
import time
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, parse_args  # noqa: E402


def detail(reply, key):
    """'OK UNDO: decks: 2 | cues: 4 | tracker steps: 0' -> the number after key."""
    body = reply.split(": ", 1)[1] if reply.startswith("OK") and ": " in reply else reply
    for part in body.split("|"):
        part = part.strip()
        if part.startswith(key + ":"):
            try:
                return int(part.split(":", 1)[1])
            except ValueError:
                return -1
    return -1


def main():
    args = parse_args(__doc__, 5741)
    fails = []

    def note(name, ok, info=""):
        print("  %-56s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append("%s -- %s" % (name, info))

    with Deckboy(args, prefix="deckboy-decks-") as db:
        print("check: removing a playlist is undoable; tracker steps are the tracker's")
        print()
        db.send("DECK 1")
        for pattern in ("smpte-bars", "full-black", "full-white"):
            db.send("PATTERN ADD " + pattern)
        db.send("DECKADD")
        db.send("DECK 2")
        db.send("PATTERN ADD smpte-bars")

        # 1. REMOVE THE BIG ONE, THEN TAKE IT BACK.
        reply = db.send("DECKREMOVE 1")
        note("DECKREMOVE 1 removes it", "decks: 1" in reply, reply)
        reply = db.send("UNDO")
        note("UNDO brings the playlist back", detail(reply, "decks") == 2, reply)
        note("...with every one of its cues", detail(reply, "cues") == 4, reply)
        reply = db.send("REDO")
        note("REDO removes it again", detail(reply, "decks") == 1, reply)
        db.send("UNDO")

        # 1b. TRIGGERS: what fires a cue besides GO. Deck 1 holds three cues.
        db.send("DECK 1")
        db.send("SELECT 3")
        reply = db.send("TRIGGER MIDI 60")
        note("TRIGGER MIDI sets a cue's note", "midi 60" in reply, reply)
        db.send("SELECT 2")
        reply = db.send("TRIGGER OSC stage/doors")
        note("TRIGGER OSC sets an address (slash added)", "osc /stage/doors" in reply, reply)
        reply = db.send("TRIGGER KEY F5")
        note("TRIGGER KEY takes a free key", "key F5" in reply, reply)
        reply = db.send("TRIGGER KEY A")
        note("...and refuses one the desk uses", reply.startswith("ERR"), reply)
        db.send("SELECT 1")
        db.send("MIDINOTE 60")
        time.sleep(0.3)
        note("a claimed MIDI note fires its cue", db.status("DECK 1 ", "active") == "3",
             "active=%r" % db.status("DECK 1 ", "active"))
        db.send("OSCTRIGGER /STAGE/DOORS")
        time.sleep(0.3)
        note("an OSC address fires its cue, any case", db.status("DECK 1 ", "active") == "2",
             "active=%r" % db.status("DECK 1 ", "active"))
        # And as a real OSC packet over UDP, which is the path a controller
        # takes: the network thread must hand an unknown address on.
        db.send("SELECT 1")
        db.send("TAKE")
        time.sleep(0.3)

        def osc_string(text):
            raw = text.encode() + b"\0"
            return raw + b"\0" * ((4 - len(raw) % 4) % 4)
        packet = osc_string("/Stage/Doors") + osc_string(",")
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.sendto(packet, ("127.0.0.1", db.port))
        udp.close()
        time.sleep(0.6)
        note("a real OSC packet fires the cue that claims it",
             db.status("DECK 1 ", "active") == "2",
             "active=%r" % db.status("DECK 1 ", "active"))
        db.send("MIDINOTE 0")
        time.sleep(0.3)
        note("an unclaimed note still goes to cue N+1", db.status("DECK 1 ", "active") == "1",
             "active=%r" % db.status("DECK 1 ", "active"))
        reply = db.send("OSCTRIGGER /nobody/home")
        note("OSC nobody claims is quiet, not an error", reply.startswith("OK"), reply)
        db.send("STOP")

        # 1c. A MEMO SAYS ITS PIECE AND CHANGES NOTHING.
        db.send("SELECT 1")
        db.send("TAKE")
        time.sleep(0.3)
        reply = db.send("MEMOCUE stand by sound")
        note("MEMOCUE puts a memo after the selection", "memo at 2" in reply, reply)
        db.send("SELECT 2")
        db.send("TAKE")
        time.sleep(0.3)
        note("GO on a memo leaves the playing cue alone",
             db.status("DECK 1 ", "active") == "1",
             "active=%r" % db.status("DECK 1 ", "active"))
        db.send("DELETE 2")
        db.send("STOP")

        # 1d. STANDBY FOLLOWS ITS CUE when the list changes under it. Deck 1:
        # bars, black, white. Arm white, delete bars: standby must still be
        # white (now cue 2), not whatever slid into slot 3.
        db.send("DECK 1")
        db.send("STANDBY 3")
        before = db.send("STANDBY")
        db.send("SELECT 1")
        db.send("DELETE 1")
        db.send("DELETE 1")   # a non-live cue may want the press-again guard
        after = db.send("STANDBY")
        note("standby keeps its cue after a delete above it",
             "full-white" in after.lower() or "white" in after.lower(),
             "before %r after %r" % (before, after))
        db.send("UNDO")

        # 2. A STEP IS NOT A CUE IN ANY PLAYLIST.
        reply = db.send("TRACKER ADD")
        note("TRACKER ADD makes a step", "1 steps" in reply, reply)
        reply = db.send("MASTER NEW")
        reply = db.send("TRACKER")
        note("MASTER NEW makes a tracker step too", "2 steps" in reply, reply)

        # It can fire BOTH playlists -- a step that lived in deck 1 could not.
        reply = db.send("TRACKER SET 1 1 3")
        note("a step can fire deck 1", reply.startswith("OK"), reply)
        reply = db.send("TRACKER SET 1 2 1")
        note("...and deck 2 in the same step", reply.startswith("OK"), reply)
        reply = db.send("TRACKER SET 1 2 9")
        note("a cue that does not exist is refused", reply.startswith("ERR"), reply)
        db.send("TRACKER STEP 1")
        db.send("DECK 1")
        cue1 = db.status("DECK 1 ", "active")
        cue2 = db.status("DECK 2 ", "active")
        note("firing the step took cue 3 on deck 1", cue1 == "3", "DECK 1 active=%r" % cue1)
        note("...and cue 1 on deck 2", cue2 == "1", "DECK 2 active=%r" % cue2)
        # Step 2 was made empty. CHECK walked only the playlists, so once the
        # masters moved into the tracker it could see none of them.
        reply = db.send("CHECK")
        note("CHECK sees an empty tracker step", "tracker step 2: master fires nothing" in reply, reply)
        note("...and not the step that is set up", "tracker step 1:" not in reply, reply)

        # 3. ONE LED TILE. 256x256 could not be set at all: no width or height
        # control, presets from 640x480 up, and a 5%-of-raster minimum.
        reply = db.send("OUTPUT AOI 256x256")
        note("OUTPUT AOI 256x256 takes", reply.startswith("OK") and "256x256 @" in reply, reply)
        reply = db.send("OUTPUT AOI 128*128 0 0")
        note("a 128x128 tile at the corner takes", "128x128 @ 0,0" in reply, reply)
        raster = reply.rsplit(" of ", 1)[-1].split(" |")[0] if " of " in reply else ""
        reply = db.send("OUTPUT AOI 99999x99999")
        note("a size past the raster clamps to it", ("%s @ 0,0" % raster) in reply, reply)
        db.send("OUTPUT AOI 256x256 16 32")
        reply = db.send("OUTPUT AOI")
        note("a placed tile reads back where it was put", "256x256 @ 16,32" in reply, reply)

        # 4. A WARP POINT TO THE PIXEL, not only by dragging.
        db.send("OUTPUT AOI FULL")
        db.send("WARP EDIT")
        time.sleep(1.0)   # the editor needs one drawn frame to know its monitor
        db.send("WARP SELECT TR")
        reply = db.send("WARP AT 1907 4")
        note("WARP AT puts a corner on the pixel", reply.endswith("1907.0 4.0"), reply)
        db.send("WARP GRID 3")
        db.send("WARP SELECT 2 2")
        reply = db.send("WARP AT 1000 600")
        note("...and a grid point", reply.endswith("1000.0 600.0"), reply)
        reply = db.send("WARP SELECT 9 9")
        note("a grid point that does not exist is refused", reply.startswith("ERR"), reply)
        db.send("WARP EDIT OFF")

        show = db.saved_show()
        lines = show.splitlines()
        steps = [l for l in lines if l.startswith("cue\t-1\t")]
        cues = [l for l in lines if l.startswith("cue\t") and not l.startswith("cue\t-1\t")]
        note("the show saves both steps in the tracker", len(steps) == 2, "%d" % len(steps))
        note("and no playlist gained a cue", len(cues) == 4, "%d playlist cues" % len(cues))
        note("a cue's hotkey and OSC address are saved",
             any(l.split("\t")[-5:-2] == ["F5", "-1", "/stage/doors"] for l in cues),
             "no cue record carries F5 / -1 / /stage/doors before its audio-track fields")
        note("a cue's MIDI note is saved",
             any(l.split("\t")[-4] == "60" for l in cues), "no cue record carries note 60")

    # 3. FORTY CUES DOWN TOGETHER. Each selected cue moves by the same dB from
    # where it was; the old - / + wrote the first cue's value to all of them.
    clips = []
    tmp = tempfile.mkdtemp(prefix="deckboy-gain-media-")
    for n in range(3):
        path = os.path.join(tmp, "tone%d.wav" % n)
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi",
                        "-i", "sine=frequency=%d:duration=2" % (300 + n * 100), path],
                       capture_output=True)
        if os.path.exists(path):
            clips.append(path)
    if len(clips) != 3:
        note("ffmpeg made three test tones", False, "need ffmpeg on PATH")
    else:
        extra = []
        for clip in clips:
            extra += ["--import", clip]
        with Deckboy(args, prefix="deckboy-gain-", extra_args=extra, port=args.port + 1) as db:
            print()
            print("check: gain on many cues moves each by the same dB")
            print()
            # The probe decides hasAudio; give it a moment to land.
            for _ in range(40):
                db.send("SELECT 3")
                if db.send("AUDIOGAIN").startswith("OK"):
                    break
                time.sleep(0.25)
            trims = {1: -2.0, 2: 0.0, 3: 3.0}
            for index, db_value in trims.items():
                db.send("SELECT %d" % index)
                db.send("AUDIOGAIN %g" % db_value)
            db.send("SELECTALL")
            reply = db.send("AUDIOGAIN BY -6")
            note("AUDIOGAIN BY -6 reaches all three", "3 cues moved" in reply, reply)
            for index, db_value in trims.items():
                db.send("SELECT %d" % index)
                reply = db.send("AUDIOGAIN")
                want = "%+.1f dB" % (db_value - 6.0)
                note("cue %d keeps its own trim (%s)" % (index, want), want in reply, reply)

            # DEVAMP: a 2s tone on loop keeps going; devamped, it plays out its
            # pass and ends -- so within a pass it is no longer the one playing.
            db.send("SELECT 1")
            db.send("LOOP ON")
            db.send("HOLD OFF")
            db.send("TAKE")
            time.sleep(4.5)
            looping = db.status("DECK 1 ", "active") == "1" and \
                db.status("DECK 1 ", "status").lower().startswith("play")
            note("a looping cue is still playing after two passes", looping,
                 "status=%r active=%r" % (db.status("DECK 1 ", "status"),
                                          db.status("DECK 1 ", "active")))
            reply = db.send("DEVAMP")
            note("DEVAMP is accepted while it plays", reply.startswith("OK"), reply)
            time.sleep(3.0)
            ended = not (db.status("DECK 1 ", "active") == "1" and
                         db.status("DECK 1 ", "status").lower().startswith("play"))
            note("...and within a pass it has stopped looping", ended,
                 "status=%r active=%r" % (db.status("DECK 1 ", "status"),
                                          db.status("DECK 1 ", "active")))
            reply = db.send("STOP")
            reply = db.send("DEVAMP 9")
            note("DEVAMP on a deck with nothing playing is refused", reply.startswith("ERR"), reply)

    print()
    if fails:
        print("%d FAILED" % len(fails))
        for f in fails:
            print("  - " + f)
        return 1
    print("all ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
