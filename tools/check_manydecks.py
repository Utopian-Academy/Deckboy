"""Four playlists plus a master deck, layered onto an output, then one removed
from the MIDDLE -- the case that silently repoints everything at its neighbour
if the remap is wrong.

    python3 tools/check_manydecks.py [--exe PATH] [--port N]
"""

import sys
import time

from deckboy_harness import Deckboy, finish, parse_args


def main():
    args = parse_args(__doc__, 5625)
    fails = []
    with Deckboy(args, "dbmany-") as db:
        for _ in range(4):
            db.send("DECKADD")
            time.sleep(1.2)
        decks = 0
        for line in db.send("STATUS").splitlines():
            if line.startswith("DECKBOY"):
                for token in line.split():
                    if token.startswith("decks="):
                        decks = int(token[6:])
        print("1  playlists:      %d" % decks)
        if decks != 5:
            fails.append("expected 5 playlists, got %d" % decks)

        # A master cue on deck 5 firing cues on decks 2 and 3: references that
        # must survive a removal below them.
        db.send("DECK 5")
        db.send("MASTER NEW")
        for deck, body in ((2, "TWO"), (3, "THREE")):
            db.send("DECK %d" % deck)
            db.send("TEXTCUE NEW")
            db.send("TEXTCUE BODY " + body)
        db.send("DECK 5")
        print("2  master d2:      %s" % db.send("MASTER DECK 2 1")[:50])
        print("3  master d3:      %s" % db.send("MASTER DECK 3 1")[:50])
        print("4  master holds:   %s" % db.send("MASTER")[:110])

        # Decks 3 and 4 layered onto output 1, over deck 1.
        db.send("OUTPUT SELECT 1")
        for deck in (3, 4):
            db.send("DECK %d" % deck)
            db.send("VIDEO OUTPUT ASSIGN")
        stack = db.send("VIDEO OUTPUT LAYER")
        print("5  stack:          %s" % stack[:110])
        if "Deck 3" not in stack or "Deck 4" not in stack:
            fails.append("the layer stack did not take both playlists: %s" % stack)

        print("6  remove deck 2:  %s" % db.send("DECKREMOVE 2")[:70])
        time.sleep(2.0)

        # Decks 3 and 4 are now in slots 2 and 3, and default names follow
        # position, so the stack must read Deck 2 and Deck 3.
        after_stack = db.send("VIDEO OUTPUT LAYER")
        print("7  stack after:    %s" % after_stack[:110])
        if "Deck 2" not in after_stack or "Deck 3" not in after_stack:
            fails.append("the layer stack was not remapped: %s" % after_stack)
        if "Deck 4" in after_stack:
            fails.append("the stack still names a playlist that no longer exists")

        db.send("DECK 4")   # was deck 5, the master deck
        after = db.send("MASTER")
        print("8  master after:   %s" % after[:110])
        # The assignment to the removed deck must be gone; the other survives.
        if "assignment" not in after:
            fails.append("the master cue lost its report: %s" % after)
        if "UNRESOLVED" in after:
            fails.append("a master assignment survived as a broken reference")

        saved = db.saved_show()
        deck_lines = [line for line in saved.splitlines() if line.startswith("deck\t")]
        print("9  saved decks:    %d" % len(deck_lines))
        if len(deck_lines) != 4:
            fails.append("expected 4 playlists in the saved show, found %d" % len(deck_lines))

        print("10 remove more:    %s" % db.send("DECKREMOVE 1")[:70])
        db.send("DECKREMOVE 1")
        db.send("DECKREMOVE 1")
        r = db.send("DECKREMOVE 1")
        print("11 refuse last:    %s" % r[:70])
        if not r.startswith("ERR"):
            fails.append("removing the last playlist was allowed: %s" % r)
    return finish("many playlists", fails)


if __name__ == "__main__":
    sys.exit(main())
