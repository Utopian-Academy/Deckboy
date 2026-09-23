"""Does a multiview OUTPUT actually put every playlist on the screen?

Measured in pixels. Two playlists, each showing a bright text card, on an
output set to MULTIVIEW: the recorded frame must carry picture in BOTH halves,
which a programme output showing one deck does not.

    python3 tools/check_multiview_out.py [--exe PATH] [--port N]
"""

import sys
import time

from deckboy_harness import Deckboy, finish, parse_args


def halves(frame):
    """Mean red-channel level of the left and right halves, sampled every 4 px."""
    if not frame:
        return None
    w, h, px = frame
    left = right = ln = rn = 0
    for y in range(0, h, 4):
        row = y * w * 3
        for x in range(0, w, 4):
            v = px[row + x * 3]
            if x < w // 2:
                left += v
                ln += 1
            else:
                right += v
                rn += 1
    return {"left": left / max(1, ln), "right": right / max(1, rn), "w": w, "h": h}


def main():
    args = parse_args(__doc__, 5630)
    fails = []
    with Deckboy(args, "dbmvout-") as db:
        db.send("DECKADD")
        time.sleep(2.0)
        for deck, body in ((1, "AAAAAAAA"), (2, "BBBBBBBB")):
            db.send("DECK %d" % deck)
            db.send("TEXTCUE NEW")
            db.send("TEXTCUE BODY " + body)
            db.send("TEXTCUE CARD 255")
            db.send("TEXTCUE SIZE 90")
        db.send("OUTPUT ON")
        time.sleep(2.0)
        for deck in (1, 2):
            db.send("DECK %d" % deck)
            db.send("TAKE")
        time.sleep(2.0)

        base = halves(db.frame(db.record()))
        print("1  programme:     %s" % base)
        print("2  set multiview: %s" % db.send("VIDEO OUTPUT TYPE MULTIVIEW")[:60])
        time.sleep(3.0)
        mv = halves(db.frame(db.record()))
        print("3  multiview:     %s" % mv)

        if not base or not mv:
            fails.append("no recording produced")
        else:
            lit_both = mv["left"] > 8.0 and mv["right"] > 8.0
            print("4  both halves lit: %s  (L %.1f  R %.1f)" % (lit_both, mv["left"], mv["right"]))
            if not lit_both:
                fails.append("a multiview output did not light both halves of the frame "
                             "(L %.1f R %.1f)" % (mv["left"], mv["right"]))
            if abs(mv["left"] - base["left"]) < 0.5 and abs(mv["right"] - base["right"]) < 0.5:
                fails.append("switching the output to multiview changed nothing")

        print("5  back:          %s" % db.send("VIDEO OUTPUT TYPE WINDOW")[:50])
    return finish("multiview output", fails)


if __name__ == "__main__":
    sys.exit(main())
