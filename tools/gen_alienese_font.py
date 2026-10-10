#!/usr/bin/env python3
"""Draw data/fonts/Alienese.ttf, the symbols of the Alienese II cypher.

The cypher (native/core/i18n.cpp) writes the desk in the second alien alphabet
from Futurama, whose 26 symbols stand for the values 0 to 25. No Unicode block
and no registry of invented scripts has them, so they live in the private use
area, U+EE00 for 0 to U+EE19 for 25, and this font draws them there and
nothing else: digits and punctuation in a cypher label come from the desk's
own faces, so a timecode or a port number stays readable. Deckboy reaches the
font through its fallback chain, at the size of whichever face it stands in.

Every symbol is a few strokes of one width -- the alphabet is drawn that way --
written here as centre lines in units of the capital height and stroked to
outlines with Skia. Edit a glyph by editing its strokes and run this again:

    python -m pip install fonttools skia-pathops
    python tools/gen_alienese_font.py

The output is the same file for the same script: the timestamps are fixed.
"""
import math
import os

import pathops
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.ttGlyphPen import TTGlyphPen

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "data", "fonts", "Alienese.ttf")

UPM = 1000
FIRST = 0xEE00  # the symbol for 0; must match kAlieneseFirst in native/core/i18n.hpp
# The capital height. The desk's text shaper matches a borrowed face to the
# face it stands in for by the height of a capital, assuming an ordinary
# sans's 0.716 em, so a symbol drawn this tall is exactly a capital's height
# beside any of the desk's faces.
H = 716
STROKE = 84
BEARING = 60
# The symbols are drawn about as wide as they are tall. Condensed to this, a
# label in them is as wide as the same label in capitals, so it fits the
# buttons English was laid out for.
CONDENSE = 0.78
ASCENT = 820   # the tallest symbol (22) reaches 802
DESCENT = 120  # and its lowest point is -87
SPACE = 278
# 2026-10-10 00:00 UTC in the font format's own epoch (seconds since 1904),
# so a rebuild with no change to a stroke is byte-identical.
FIXED_TIME = 3842985600


# ── Strokes ────────────────────────────────────────────────────────────────
#
# A glyph is a list of strokes. Each stroke is a path of commands in
# capital-height units -- x across, y up from the baseline:
#   ("M", x, y)  ("L", x, y)  ("Q", cx, cy, x, y)  ("C", c1x, c1y, c2x, c2y, x, y)
# and a trailing "Z" closes it. ("dot", x, y, r) is a filled disc.

def line(*points):
    cmds = [("M",) + points[0]]
    cmds += [("L",) + p for p in points[1:]]
    return cmds


def closed(*points):
    return line(*points) + ["Z"]


def dot(x, y, r=85):
    return ("dot", x, y, r)


R = 120  # the radius of a rounded corner

GLYPHS = {
    # 0: a stroke falling into a corner, like an arrow into its box.
    0: [line((0, 0), (680, 0), (680, 716)), line((0, 716), (680, 0))],
    # 1: a tall, narrow S.
    1: [[("M", 150, 716), ("C", 20, 620, 20, 470, 95, 380), ("C", 170, 290, 175, 120, 40, 0)]],
    # 2: three uprights on a base, the middle one tall.
    2: [line((0, 400), (0, 0), (820, 0), (820, 400)), line((410, 0), (410, 716))],
    # 3: an upright with an arm from its middle.
    3: [line((0, 0), (0, 716)), line((0, 358), (520, 358))],
    # 4: 0 mirrored.
    4: [line((680, 0), (0, 0), (0, 716)), line((680, 716), (0, 0))],
    # 5: an upright and the diagonal from its foot.
    5: [line((0, 716), (0, 0), (670, 716))],
    # 6: a wave.
    6: [[("M", 0, 330), ("C", 90, 470, 230, 470, 380, 380), ("C", 530, 290, 670, 290, 760, 430)]],
    # 7: a closing bracket with an arm out of its middle.
    7: [[("M", 0, 716), ("C", 287, 560, 287, 156, 0, 0)], line((215, 358), (645, 358))],
    # 8: a box open on the left, its corners rounded.
    8: [[("M", 0, 716), ("L", 680 - R, 716), ("Q", 680, 716, 680, 716 - R), ("L", 680, R),
         ("Q", 680, 0, 680 - R, 0), ("L", 0, 0)]],
    # 9: two brackets back to back, joined across the middle.
    9: [[("M", 110, 716), ("Q", 235, 620, 235, 400), ("Q", 235, 200, 0, 0)],
        [("M", 750, 716), ("Q", 625, 620, 625, 400), ("Q", 625, 200, 860, 0)],
        line((235, 400), (625, 400))],
    # 10: two uprights.
    10: [line((0, 0), (0, 716)), line((170, 0), (170, 716))],
    # 11: a corner with a dot inside it.
    11: [line((0, 0), (0, 716), (560, 716)), dot(280, 372)],
    # 12: a right triangle, the right angle at the top left.
    12: [closed((0, 0), (0, 716), (700, 716))],
    # 13: a U.
    13: [[("M", 0, 716), ("L", 0, R), ("Q", 0, 0, R, 0), ("L", 680 - R, 0), ("Q", 680, 0, 680, R),
          ("L", 680, 716)]],
    # 14: a V holding a dot.
    14: [line((0, 716), (450, 30), (900, 716)), dot(450, 520)],
    # 15: two parallel strokes falling to the right.
    15: [line((0, 640), (560, 0)), line((160, 716), (720, 76))],
    # 16: a rising stroke with a leg from its middle.
    16: [line((0, 0), (660, 716)), line((330, 358), (700, 0))],
    # 17: a step down to the left.
    17: [line((315, 716), (315, 392), (0, 392), (0, 0))],
    # 18: one rising stroke.
    18: [line((0, 0), (680, 716))],
    # 19: an arch.
    19: [[("M", 0, 0), ("L", 0, 716 - R), ("Q", 0, 716, R, 716), ("L", 680 - R, 716),
          ("Q", 680, 716, 680, 716 - R), ("L", 680, 0)]],
    # 20: a rising stroke and the upright from its top.
    20: [line((0, 0), (660, 716), (660, 0))],
    # 21: an upright under a canopy.
    21: [[("M", 0, 501), ("Q", 370, 931, 740, 501)], line((370, 0), (370, 716))],
    # 22: a tall closing bracket with a dot before it.
    22: [[("M", 0, 760), ("C", 387, 600, 387, 115, 0, -45)], dot(0, 358)],
    # 23: two chevrons pointing right.
    23: [line((0, 716), (360, 358), (0, 0)), line((190, 716), (550, 358), (190, 0))],
    # 24: a right triangle, the right angle at the top right.
    24: [closed((0, 716), (700, 716), (700, 0))],
    # 25: 7 mirrored: an arm into an opening bracket.
    25: [[("M", 645, 716), ("C", 358, 560, 358, 156, 645, 0)], line((430, 358), (0, 358))],
}


def disc(x, y, r):
    """A circle, as eight quadratic arcs."""
    p = pathops.Path()
    k = r / math.cos(math.pi / 8)
    p.moveTo(x + r, y)
    for i in range(1, 9):
        a = i * math.pi / 4
        c = a - math.pi / 8
        p.quadTo(x + k * math.cos(c), y + k * math.sin(c), x + r * math.cos(a), y + r * math.sin(a))
    p.close()
    return p


def outline(strokes):
    """Every stroke of a glyph, stroked, condensed and merged into one outline."""
    parts = []
    for s in strokes:
        if isinstance(s, tuple) and s[0] == "dot":
            _, x, y, r = s
            parts.append(disc(x * CONDENSE, y, r))
            continue
        p = pathops.Path()
        for cmd in s:
            if cmd == "Z":
                p.close()
                continue
            op, *xy = cmd
            pts = [(xy[i] * CONDENSE, xy[i + 1]) for i in range(0, len(xy), 2)]
            if op == "M":
                p.moveTo(*pts[0])
            elif op == "L":
                p.lineTo(*pts[0])
            elif op == "Q":
                p.quadTo(*pts[0], *pts[1])
            elif op == "C":
                p.cubicTo(*pts[0], *pts[1], *pts[2])
        # Square ends and mitred corners, as the alphabet is drawn; a corner
        # sharper than about 60 degrees is bevelled rather than left as a spike
        # reaching below the baseline.
        p.stroke(STROKE, pathops.LineCap.BUTT_CAP, pathops.LineJoin.MITER_JOIN, 2.0)
        parts.append(p)
    # Each part turned the same way round first: two overlapping outlines wound
    # in opposite directions cancel where they cross, and the crossing of two
    # strokes would come out as a hole.
    merged = pathops.Path()
    pen = merged.getPen()
    for p in parts:
        pathops.simplify(p, fix_winding=True, keep_starting_points=False, clockwise=True).draw(pen)
    return pathops.simplify(merged, fix_winding=True, keep_starting_points=False, clockwise=True)


def glyph_from(path):
    """The outline as a TrueType glyph, moved to sit BEARING from the origin."""
    xmin, _, xmax, _ = path.bounds
    shifted = path.transform(1, 0, 0, 1, BEARING - xmin, 0)
    rounded = pathops.Path()
    pen = rounded.getPen()
    # Integer points, as the glyf table stores them, then merged again so the
    # rounding cannot leave a sliver of overlap.
    shifted.draw(_RoundPen(pen))
    clean = pathops.simplify(rounded, fix_winding=True, keep_starting_points=False, clockwise=True)
    tt = TTGlyphPen(glyphSet=None)
    clean.draw(Cu2QuPen(tt, max_err=1.0, reverse_direction=False))
    glyph = tt.glyph()
    glyph.recalcBounds(None)
    return glyph, int(round(xmax - xmin)) + 2 * BEARING


class _RoundPen:
    def __init__(self, out):
        self.out = out

    @staticmethod
    def _r(pt):
        return (round(pt[0]), round(pt[1]))

    def moveTo(self, pt):
        self.out.moveTo(self._r(pt))

    def lineTo(self, pt):
        self.out.lineTo(self._r(pt))

    def qCurveTo(self, *pts):
        self.out.qCurveTo(*[self._r(p) if p is not None else None for p in pts])

    def curveTo(self, *pts):
        self.out.curveTo(*[self._r(p) for p in pts])

    def closePath(self):
        self.out.closePath()

    def endPath(self):
        self.out.endPath()


def notdef():
    pen = TTGlyphPen(glyphSet=None)
    for (x0, y0, x1, y1), clockwise in (((60, 0, 460, 716), True), ((120, 60, 400, 656), False)):
        pts = [(x0, y0), (x0, y1), (x1, y1), (x1, y0)]
        if not clockwise:
            pts.reverse()
        pen.moveTo(pts[0])
        for p in pts[1:]:
            pen.lineTo(p)
        pen.closePath()
    return pen.glyph(), 520


def main():
    names = ["uni%04X" % (FIRST + v) for v in range(26)]
    order = [".notdef", "space"] + names
    glyphs = {}
    metrics = {}
    g, adv = notdef()
    glyphs[".notdef"] = g
    metrics[".notdef"] = (adv, 60)
    glyphs["space"] = TTGlyphPen(glyphSet=None).glyph()
    metrics["space"] = (SPACE, 0)
    cmap = {0x20: "space", 0xA0: "space"}
    for v, name in enumerate(names):
        g, adv = glyph_from(outline(GLYPHS[v]))
        glyphs[name] = g
        metrics[name] = (adv, g.xMin)
        cmap[FIRST + v] = name

    fb = FontBuilder(UPM, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics)
    fb.setupHorizontalHeader(ascent=ASCENT, descent=-DESCENT)
    fb.setupNameTable({
        "familyName": "Deckboy Alienese",
        "styleName": "Regular",
        "uniqueFontIdentifier": "Deckboy Alienese Regular 1.0",
        "fullName": "Deckboy Alienese",
        "psName": "DeckboyAlienese-Regular",
        "version": "Version 1.000",
        "copyright": "Copyright (C) 2026 Deckboy Contributors",
        "description": "The second alien alphabet, drawn for the Alienese II cypher in "
                       "Deckboy: the symbols for 0 to 25 at U+EE00 to U+EE19.",
        "licenseDescription": "Part of Deckboy, licensed under the GNU General Public "
                              "License, version 3 or (at your option) any later version.",
        "licenseInfoURL": "https://www.gnu.org/licenses/gpl-3.0.html",
    })
    fb.setupOS2(
        sTypoAscender=ASCENT, sTypoDescender=-DESCENT, sTypoLineGap=0,
        usWinAscent=ASCENT, usWinDescent=DESCENT,
        sxHeight=H, sCapHeight=H, achVendID="NONE", fsType=0,
        ulUnicodeRange1=1,        # Basic Latin: the space
        ulUnicodeRange2=1 << 28,  # Private Use Area: the symbols
    )
    fb.setupPost()
    fb.updateHead(created=FIXED_TIME, modified=FIXED_TIME)
    fb.font.recalcTimestamp = False
    fb.save(OUT)
    print("wrote", os.path.relpath(OUT, ROOT), os.path.getsize(OUT), "bytes,",
          len(order), "glyphs")


if __name__ == "__main__":
    main()
