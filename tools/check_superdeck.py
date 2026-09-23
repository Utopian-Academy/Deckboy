"""Does a second playlist actually composite onto an output?

MEASURED IN PIXELS, not asked over the socket. This area was once "verified"
by reading back the value that had just been written: deck 2 decoded, played,
appeared nowhere, and the report said it worked.

So: a known picture on deck 1, a DIFFERENT known picture on deck 2, deck 2
layered over deck 1 on output 1, and the recording looked at. Layer B on top
means the frame must change.

    python3 tools/check_superdeck.py [--exe PATH] [--port N]
"""

import sys
import time

from deckboy_harness import Deckboy, finish, mean_rgb, parse_args


def text_card(db, deck, body, size):
    """A full-frame text card: its colour is entirely ours, no media involved."""
    db.send("DECK %d" % deck)
    db.send("TEXTCUE NEW")
    db.send("TEXTCUE BODY " + body)
    db.send("TEXTCUE CARD 255")
    db.send("TEXTCUE SIZE %d" % size)


def saved_layers(saved, output_name):
    """The zero-based deck indices in an output's saved layer stack.

    The stack is field 77 of its output_target line: one comma-separated field
    of deck:x:y:w:h:blend tokens. It used to be the LAST field and read as a
    bare "1"; fields have been appended after it since, so find it by position,
    never by the end of the line."""
    for line in saved.splitlines():
        fields = line.split("\t")
        if fields[0] == "output_target" and len(fields) > 77 and fields[2] == output_name:
            return [int(tok.split(":")[0]) for tok in fields[77].split(",") if tok.strip()]
    return []


def main():
    args = parse_args(__doc__, 5620)
    fails = []
    with Deckboy(args, "dbsuper-") as db:
        print("1  add a playlist:  %s" % db.send("DECKADD")[:70])
        time.sleep(2.0)
        text_card(db, 1, "AAAA", 60)
        text_card(db, 2, "BBBBBBBB", 90)
        db.send("OUTPUT ON")
        time.sleep(2.0)

        # Baseline: only deck 1 on output 1.
        db.send("DECK 1")
        db.send("TAKE")
        time.sleep(2.0)
        base = mean_rgb(db.frame(db.record()))
        print("2  deck 1 alone:   %s" % base)
        if not base:
            fails.append("no recording produced for the baseline")

        db.send("OUTPUT SELECT 1")
        print("3  stack before:   %s" % db.send("VIDEO OUTPUT LAYER")[:90])
        db.send("DECK 2")
        r = db.send("VIDEO OUTPUT ASSIGN")
        print("4  assign deck 2:  %s" % r[:70])
        if r.startswith("ERR"):
            fails.append("could not assign the second playlist to the output: %s" % r)
        print("5  stack after:    %s" % db.send("VIDEO OUTPUT LAYER")[:90])
        db.send("TAKE")
        time.sleep(2.5)
        both = mean_rgb(db.frame(db.record()))
        print("6  both layered:   %s" % both)

        # Deck 2's card is much bigger, so with it composited on top the frame
        # must differ. Identical frames are exactly the failure this exists to
        # catch: two decks, one picture.
        if base and both:
            delta = sum(abs(base[c] - both[c]) for c in "rgb")
            print("7  delta:          %.2f" % delta)
            if delta < 1.0:
                fails.append("layering a second playlist changed nothing (delta %.2f) "
                             "-- it is not compositing" % delta)
        else:
            fails.append("no recording produced with both layers")

        r = db.send("VIDEO OUTPUT UNASSIGN")
        print("8  unassign:       %s" % r[:70])
        if r.startswith("ERR"):
            fails.append("could not take the second playlist off the output: %s" % r)
        print("9  stack final:    %s" % db.send("VIDEO OUTPUT LAYER")[:90])

        # The stack has to survive a save.
        db.send("DECK 2")
        db.send("VIDEO OUTPUT ASSIGN")
        layers = saved_layers(db.saved_show(), "Output 1")
        print("10 saved stack:    %s" % layers)
        if 1 not in layers:
            fails.append("deck 2 is not in Output 1's saved layer stack (%s)" % layers)
    return finish("super deckboy", fails)


if __name__ == "__main__":
    sys.exit(main())
