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
outlines here, with nothing but fontTools (Debian packages it as
python3-fonttools). Edit a glyph by editing its strokes and run this again:

    python -m pip install fonttools
    python tools/gen_alienese_font.py

Each stroke becomes its own closed outline and the outlines of one glyph are
left overlapping, wound the same way round so the overlaps add rather than
cancel. TrueType fills by the non-zero rule, so that draws the same as merging
them; the glyphs carry the OVERLAP_SIMPLE flag that says so.

The output is the same file for the same script: the timestamps are fixed.
"""
import math
import os

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables._g_l_y_f import flagOverlapSimple

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


# ── Stroking ───────────────────────────────────────────────────────────────
#
# Curves are flattened into short straight runs and every run is stroked as a
# polygon: square ends, mitred corners, and a corner sharper than about 60
# degrees bevelled rather than left as a spike reaching below the baseline (a
# miter limit of 2, as SVG and Skia measure it).

MITER_LIMIT = 2.0
FLAT_STEP = 6.0  # the longest straight run a curve is cut into, in font units


def _bezier(p0, controls, p3, t):
    pts = [p0] + list(controls) + [p3]
    while len(pts) > 1:
        pts = [(a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t) for a, b in zip(pts, pts[1:])]
    return pts[0]


def flatten(stroke):
    """A stroke's centre line as points, condensed, and whether it is closed."""
    pts = []
    closed_path = False
    for cmd in stroke:
        if cmd == "Z":
            closed_path = True
            continue
        op, *xy = cmd
        given = [(xy[i] * CONDENSE, xy[i + 1]) for i in range(0, len(xy), 2)]
        if op in ("M", "L"):
            pts.append(given[0])
            continue
        start, controls, end = pts[-1], given[:-1], given[-1]
        hull = [start] + given
        length = sum(math.dist(a, b) for a, b in zip(hull, hull[1:]))
        steps = max(4, math.ceil(length / FLAT_STEP))
        pts += [_bezier(start, controls, end, i / steps) for i in range(1, steps + 1)]
    out = [pts[0]]
    for p in pts[1:]:
        if math.dist(p, out[-1]) > 1e-6:
            out.append(p)
    if closed_path and math.dist(out[0], out[-1]) < 1e-6:
        out.pop()
    return out, closed_path


def _offset_side(pts, closed_path, side):
    """One side of a stroke: the centre line moved half a stroke to the left
    (side +1) or right (side -1), joined at every corner."""
    hw = STROKE / 2.0 * side
    n = len(pts)
    segs = range(n) if closed_path else range(n - 1)
    normals = []
    for i in segs:
        (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
        d = math.hypot(x1 - x0, y1 - y0)
        normals.append((-(y1 - y0) / d, (x1 - x0) / d))
    out = []
    if not closed_path:
        out.append((pts[0][0] + hw * normals[0][0], pts[0][1] + hw * normals[0][1]))
    corners = range(n) if closed_path else range(1, n - 1)
    for j in corners:
        na, nb = normals[j - 1], normals[j % len(normals)]
        px, py = pts[j]
        dot_ab = na[0] * nb[0] + na[1] * nb[1]
        # Turning away from this side makes it the outside of the corner.
        turn = na[0] * nb[1] - na[1] * nb[0]
        outside = turn * side < 0
        cos_half = math.sqrt(max(0.0, (1.0 + dot_ab) / 2.0))
        if outside and (cos_half < 1e-9 or 1.0 / cos_half > MITER_LIMIT):
            out.append((px + hw * na[0], py + hw * na[1]))
            out.append((px + hw * nb[0], py + hw * nb[1]))
        else:
            k = hw / (1.0 + dot_ab)
            out.append((px + k * (na[0] + nb[0]), py + k * (na[1] + nb[1])))
    if not closed_path:
        out.append((pts[-1][0] + hw * normals[-1][0], pts[-1][1] + hw * normals[-1][1]))
    return out


def _area(poly):
    return sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(poly, poly[1:] + poly[:1])) / 2.0


def _clockwise(poly, want=True):
    """TrueType fills clockwise outlines; a counter-clockwise one is a hole."""
    return poly if (_area(poly) < 0) == want else poly[::-1]


def stroke_outlines(stroke):
    pts, closed_path = flatten(stroke)
    left = _offset_side(pts, closed_path, +1)
    right = _offset_side(pts, closed_path, -1)
    if not closed_path:
        return [("poly", _clockwise(left + right[::-1]))]
    # A closed stroke is a ring: the bigger side is the outline, the smaller
    # the hole through it.
    outer, inner = (left, right) if abs(_area(left)) > abs(_area(right)) else (right, left)
    return [("poly", _clockwise(outer)), ("poly", _clockwise(inner, want=False))]


def disc(x, y, r):
    """A circle, as eight quadratic arcs, clockwise: (on, off, on, off, ...)."""
    k = r / math.cos(math.pi / 8)
    pts = []
    for i in range(8):
        a = -i * math.pi / 4
        c = a - math.pi / 8
        pts.append((x + r * math.cos(a), y + r * math.sin(a)))
        pts.append((x + k * math.cos(c), y + k * math.sin(c)))
    return ("disc", pts)


def outline(strokes):
    """Every stroke of a glyph as its own outlines, overlapping where they cross."""
    parts = []
    for s in strokes:
        if isinstance(s, tuple) and s[0] == "dot":
            _, x, y, r = s
            parts.append(disc(x * CONDENSE, y, r))
        else:
            parts += stroke_outlines(s)
    return parts


def glyph_from(parts):
    """The outlines as a TrueType glyph, moved to sit BEARING from the origin."""
    xs = []
    for kind, pts in parts:
        if kind == "poly":
            xs += [p[0] for p in pts]
        else:  # a disc reaches exactly its radius either side of its centre
            cx = (pts[0][0] + pts[8][0]) / 2.0
            r = (pts[0][0] - pts[8][0]) / 2.0
            xs += [cx - r, cx + r]
    xmin, xmax = min(xs), max(xs)
    dx = BEARING - xmin

    def at(p):
        return (round(p[0] + dx), round(p[1]))

    pen = TTGlyphPen(glyphSet=None)
    for kind, pts in parts:
        pen.moveTo(at(pts[0]))
        if kind == "poly":
            for p in pts[1:]:
                pen.lineTo(at(p))
        else:
            for i in range(1, len(pts), 2):
                pen.qCurveTo(at(pts[i]), at(pts[(i + 1) % len(pts)]))
        pen.closePath()
    glyph = pen.glyph()
    glyph.recalcBounds(None)
    # The outlines overlap on purpose. The flag tells a renderer that merges
    # nothing (Apple's) to fill them by the non-zero rule, which everything
    # else does anyway.
    glyph.flags[0] |= flagOverlapSimple
    return glyph, int(round(xmax - xmin)) + 2 * BEARING


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
