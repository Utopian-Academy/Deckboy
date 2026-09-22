"""Kawaii critter sprites for Deckboy.

Drawn pixel by pixel into a 32x32 grid with no antialiasing anywhere, so they
stay pixel art rather than shrunken vector shapes. Same cast as the creature
simulation in core/creatures.hpp, so the ambient critters already in the chrome
and these are the same animals.
"""
import math
import os
import sys

from PIL import Image

S = 32


def new():
    return Image.new("RGBA", (S, S), (0, 0, 0, 0))


def px(im, x, y, c):
    x = int(round(x))
    y = int(round(y))
    if not (0 <= x < S and 0 <= y < S):
        return
    if len(c) == 3:
        c = (c[0], c[1], c[2], 255)
    if c[3] >= 255:
        im.putpixel((x, y), c)
        return
    r, g, b, a = im.getpixel((x, y))
    na = c[3] / 255.0
    im.putpixel((x, y), (int(c[0] * na + r * (1 - na)),
                         int(c[1] * na + g * (1 - na)),
                         int(c[2] * na + b * (1 - na)),
                         max(a, c[3])))


def disc(im, cx, cy, rx, ry, c):
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            dx = (x - cx) / max(0.5, rx)
            dy = (y - cy) / max(0.5, ry)
            if dx * dx + dy * dy <= 1.0:
                px(im, x, y, c)


def ring(im, cx, cy, rx, ry, c):
    inner = set()
    for y in range(int(cy - ry) - 2, int(cy + ry) + 3):
        for x in range(int(cx - rx) - 2, int(cx + rx) + 3):
            dx = (x - cx) / max(0.5, rx)
            dy = (y - cy) / max(0.5, ry)
            if dx * dx + dy * dy <= 1.0:
                inner.add((x, y))
    for (x, y) in sorted(inner):
        for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if (x + ox, y + oy) not in inner:
                px(im, x + ox, y + oy, c)


INK = (32, 28, 40)
WHITE = (255, 255, 255)
BLUSH = (255, 150, 170, 150)
NOSE = (255, 150, 170)


# ── Silhouette helpers ──────────────────────────────────────────────────────
#
# An animal whose parts are drawn AND OUTLINED one at a time comes out lumpy:
# each outline cuts straight through the part next to it, so a body and a head
# read as two blobs stuck together rather than one creature. These accumulate
# the parts into a single mask, fill it once and outline only its outer edge,
# which is what makes a snout or a fin read as belonging to the animal.

def m_disc(mask, cx, cy, rx, ry):
    for y in range(int(cy - ry) - 1, int(cy + ry) + 2):
        for x in range(int(cx - rx) - 1, int(cx + rx) + 2):
            dx = (x - cx) / max(0.5, rx)
            dy = (y - cy) / max(0.5, ry)
            if dx * dx + dy * dy <= 1.0:
                mask.add((int(x), int(y)))


def m_taper(mask, x0, x1, y, half0, half1):
    """A wedge from x0 to x1: half-height half0 shrinking to half1."""
    span = max(1, abs(x1 - x0))
    step = 1 if x1 >= x0 else -1
    for i, x in enumerate(range(x0, x1 + step, step)):
        t = i / span
        h = half0 + (half1 - half0) * t
        for j in range(-int(round(h)), int(round(h)) + 1):
            mask.add((int(x), int(round(y + j))))


def m_tri(mask, apex, base_a, base_b):
    """Filled triangle, by scanning its bounding box."""
    (ax, ay), (bx, by), (cx_, cy_) = apex, base_a, base_b
    minx = int(min(ax, bx, cx_)); maxx = int(max(ax, bx, cx_))
    miny = int(min(ay, by, cy_)); maxy = int(max(ay, by, cy_))

    def sign(px_, py_, qx, qy, rx_, ry_):
        return (px_ - rx_) * (qy - ry_) - (qx - rx_) * (py_ - ry_)

    for y in range(miny, maxy + 1):
        for x in range(minx, maxx + 1):
            d1 = sign(x, y, ax, ay, bx, by)
            d2 = sign(x, y, bx, by, cx_, cy_)
            d3 = sign(x, y, cx_, cy_, ax, ay)
            neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
            pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
            if not (neg and pos):
                mask.add((x, y))


def m_paint(im, mask, fill):
    for (x, y) in mask:
        px(im, x, y, fill)


def m_outline(im, mask, ink=INK):
    for (x, y) in mask:
        for ox, oy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if (x + ox, y + oy) not in mask:
                px(im, x + ox, y + oy, ink)


def m_shade(im, mask, fill, below_y):
    """Belly shading: only the part of the silhouette under a line."""
    for (x, y) in mask:
        if y >= below_y:
            px(im, x, y, fill)


def face(im, cx, cy, blink=False, spread=3, happy=False):
    for sx in (-spread, spread):
        if blink:
            for k in range(-1, 2):
                px(im, cx + sx + k, cy, INK)
        elif happy:
            for k in range(-1, 2):
                px(im, cx + sx + k, cy + abs(k), INK)
        else:
            disc(im, cx + sx, cy, 1.6, 1.9, INK)
            px(im, cx + sx, cy - 1, WHITE)
    disc(im, cx - spread - 2, cy + 3, 1.4, 1.0, BLUSH)
    disc(im, cx + spread + 2, cy + 3, 1.4, 1.0, BLUSH)


def legs(im, cx, cy, phase, c, span=5, n=2):
    for i in range(n):
        side = -1 if i % 2 == 0 else 1
        off = math.sin(phase + i * math.pi) * 1.6
        for k in range(3):
            px(im, cx + side * span, cy + k + off, c)


def body(im, cx, cy, rx, ry, fill, shade):
    disc(im, cx, cy, rx, ry, fill)
    disc(im, cx, cy + ry * 0.45, rx * 0.8, ry * 0.45, shade)
    ring(im, cx, cy, rx, ry, INK)


def mouse(f):
    im = new()
    ph = f * math.pi / 2
    cy = 18 + int(round(math.sin(ph)))
    for sx in (-6, 6):
        disc(im, 16 + sx, cy - 8, 4.0, 4.0, (198, 190, 205))
        ring(im, 16 + sx, cy - 8, 4.0, 4.0, INK)
        disc(im, 16 + sx, cy - 8, 2.2, 2.2, (255, 175, 190))
    legs(im, 16, cy + 5, ph, INK)
    body(im, 16, cy, 9.0, 7.5, (222, 216, 228), (196, 190, 205))
    face(im, 16, cy - 1, blink=(f == 2))
    disc(im, 16, cy + 3, 1.6, 1.2, NOSE)
    for k in range(7):
        px(im, 25 + k // 2, cy + 4 - math.sin(ph + k * 0.5) * 2, INK)
    return im


def crab(f):
    im = new()
    ph = f * math.pi / 2
    sway = math.sin(ph)
    cy = 20
    # Everything has to fit inside 32px INCLUDING the claws, so the shell is
    # narrow and the claws tuck in beside it rather than reaching out.
    for side in (-1, 1):
        for i in range(2):
            lx = 16 + side * (6 + i * 2)
            for k in range(2):
                px(im, lx, cy + 4 + k + i, INK)
    for side in (-1, 1):
        arm_y = cy - 1 + sway * side
        px(im, 16 + side * 7, arm_y, INK)
        cxo = 16 + side * 10
        disc(im, cxo, arm_y - 1, 2.6, 2.8, (250, 130, 110))
        ring(im, cxo, arm_y - 1, 2.6, 2.8, INK)
        px(im, cxo, arm_y - 3, INK)
    body(im, 16, cy, 6.8, 5.2, (250, 130, 110), (222, 96, 80))
    for sx in (-3, 3):
        for k in range(2):
            px(im, 16 + sx, cy - 5 - k, INK)
        if f == 3:
            for k in range(-1, 2):
                px(im, 16 + sx + k, cy - 8, INK)
        else:
            disc(im, 16 + sx, cy - 8, 1.6, 1.6, WHITE)
            ring(im, 16 + sx, cy - 8, 1.6, 1.6, INK)
            px(im, 16 + sx, cy - 8, INK)
    disc(im, 11, cy + 1, 1.3, 1.0, BLUSH)
    disc(im, 21, cy + 1, 1.3, 1.0, BLUSH)
    for k in range(-1, 2):
        px(im, 16 + k, cy + 1, INK)
    return im


def frog(f):
    im = new()
    ph = f * math.pi / 2
    cy = 20 - int(round(max(0.0, math.sin(ph)) * 2))
    for sx in (-7, 7):
        disc(im, 16 + sx, cy + 4, 3.2, 2.4, (150, 205, 120))
        ring(im, 16 + sx, cy + 4, 3.2, 2.4, INK)
    body(im, 16, cy, 9.5, 7.2, (168, 222, 134), (138, 196, 108))
    for sx in (-4, 4):
        disc(im, 16 + sx, cy - 7, 3.4, 3.2, (186, 232, 150))
        ring(im, 16 + sx, cy - 7, 3.4, 3.2, INK)
    face(im, 16, cy - 7, blink=(f == 2), spread=4)
    for k in range(-2, 3):
        px(im, 16 + k, cy + 2, INK)
    return im


def snail(f):
    im = new()
    ph = f * math.pi / 2
    cy = 20 + int(round(math.sin(ph) * 0.5))
    for k in range(11):
        px(im, 6 + k, cy + 6, INK)
    disc(im, 13, cy + 3, 8.0, 3.6, (240, 222, 190))
    ring(im, 13, cy + 3, 8.0, 3.6, INK)
    disc(im, 19, cy - 1, 8.0, 7.6, (226, 158, 96))
    ring(im, 19, cy - 1, 8.0, 7.6, INK)
    t = 0
    while t < 720:
        a = math.radians(t)
        r = 1.0 + t / 170.0
        px(im, 19 + math.cos(a) * r, cy - 1 + math.sin(a) * r, (196, 120, 70))
        t += 12
    for sx in (-2, 2):
        for k in range(4):
            px(im, 9 + sx, cy - 1 - k, INK)
        disc(im, 9 + sx, cy - 6, 1.6, 1.6, INK)
        px(im, 9 + sx, cy - 7, WHITE)
    disc(im, 7, cy + 3, 1.3, 1.0, BLUSH)
    return im


def moth(f):
    im = new()
    ph = f * math.pi / 2
    flap = abs(math.sin(ph))
    cy = 17 - int(round(math.sin(ph) * 1.5))
    wr = 3.0 + flap * 4.5
    for side in (-1, 1):
        wx = 16 + side * (5 + flap * 2)
        disc(im, wx, cy - 2, wr, 6.5 - flap * 1.5, (226, 206, 240))
        ring(im, wx, cy - 2, wr, 6.5 - flap * 1.5, INK)
        disc(im, wx, cy - 3, wr * 0.45, 2.0, (188, 160, 220))
    body(im, 16, cy, 4.2, 7.0, (152, 140, 176), (126, 114, 150))
    for side in (-1, 1):
        for k in range(4):
            px(im, 16 + side * (1 + k), cy - 8 - k, INK)
    face(im, 16, cy - 3, blink=(f == 1), spread=2)
    return im


def cat(f):
    im = new()
    ph = f * math.pi / 2
    cy = 19 + int(round(math.sin(ph)))
    FUR = (252, 216, 170)
    legs(im, 16, cy + 5, ph, INK, span=6)
    # Tail behind the body: a two-pixel stroke, not a row of outlined discs --
    # ringing every disc turned it into a solid black bar.
    for k in range(9):
        tx = 25 + math.sin(ph + k * 0.32) * 2.0
        ty = cy + 2 - k
        px(im, tx, ty, FUR)
        px(im, tx + 1, ty, FUR)
        px(im, tx - 1, ty, INK)
        px(im, tx + 2, ty, INK)
    body(im, 16, cy, 9.0, 7.2, FUR, (226, 186, 140))
    # Ears AFTER the body, or the body paints over them. Proper triangles,
    # sitting on the crown rather than poking out of the sides.
    for side in (-1, 1):
        ex = 16 + side * 5
        for k in range(5):
            w = 4 - k
            for j in range(-w, w + 1):
                px(im, ex + j, cy - 6 - k, FUR)
        for k in range(5):
            w = 4 - k
            px(im, ex - w, cy - 6 - k, INK)
            px(im, ex + w, cy - 6 - k, INK)
        for k in range(3):
            px(im, ex, cy - 7 - k, (255, 170, 185))
    face(im, 16, cy - 1, blink=(f == 3), happy=(f == 1))
    disc(im, 16, cy + 2, 1.4, 1.0, NOSE)
    for side in (-1, 1):
        for k in range(3):
            px(im, 16 + side * (7 + k), cy + 1 + (k // 2) * side, INK)
    return im


def dolphin(f):
    im = new()
    ph = f * math.pi / 2
    cy = 17 + int(round(math.sin(ph) * 2))      # porpoising
    SKIN = (140, 178, 214)
    BELLY = (226, 238, 248)
    # Flukes as a proper horizontal V behind the body -- the old diagonal pair
    # crossed into an X and read as a propeller.
    ty = cy + 1 + int(round(math.sin(ph + 1.0) * 2))
    for k in range(5):
        for j in range(k // 2 + 1):
            px(im, 3 + k, ty - j, SKIN)
            px(im, 3 + k, ty + j, SKIN)
        px(im, 3 + k, ty - k // 2 - 1, INK)
        px(im, 3 + k, ty + k // 2 + 1, INK)
    disc(im, 16, cy, 9.5, 5.2, SKIN)            # body
    disc(im, 16, cy + 2, 7.5, 2.6, BELLY)
    for k in range(5):                          # dorsal fin, leaning back
        w = 3 - k // 2
        for j in range(w):
            px(im, 14 - k // 2 + j, cy - 4 - k, SKIN)
        px(im, 14 - k // 2 - 1, cy - 4 - k, INK)
        px(im, 14 - k // 2 + w, cy - 4 - k, INK)
    ring(im, 16, cy, 9.5, 5.2, INK)
    # The beak is what makes it a dolphin, so it is long, thin and separate.
    disc(im, 23, cy, 4.4, 4.0, SKIN)
    ring(im, 23, cy, 4.4, 4.0, INK)
    for k in range(6):
        px(im, 26 + k, cy + 2, SKIN)
        px(im, 26 + k, cy + 1, INK)
        px(im, 26 + k, cy + 3, INK)
    if f == 2:
        for k in range(-1, 2):
            px(im, 23 + k, cy - 1, INK)
    else:
        disc(im, 23, cy - 1, 1.7, 1.9, INK)
        px(im, 23, cy - 2, WHITE)
    for k in range(3):                          # the permanent grin
        px(im, 25 + k, cy + 1, INK)
    disc(im, 20, cy + 2, 1.4, 1.0, BLUSH)
    return im


def lizard(f):
    im = new()
    ph = f * math.pi / 2
    cy = 19
    SKIN = (126, 206, 138)
    SPOT = (96, 172, 108)
    for k in range(10):                         # tail, curling
        tx = 5 + k * 0.6
        ty = cy + 2 + math.sin(ph + k * 0.45) * 2.2
        px(im, tx, ty, SKIN)
        px(im, tx, ty + 1, INK)
        px(im, tx, ty - 1, INK)
    for side, i in ((-1, 0), (1, 1)):           # legs, alternating
        off = math.sin(ph + i * math.pi) * 1.6
        for lx in (13, 21):
            for k in range(3):
                px(im, lx + side, cy + 4 + k + off, INK)
    disc(im, 15, cy, 8.0, 5.0, SKIN)            # body, shorter and deeper
    for sx in (-4, 0, 4):                       # spots
        px(im, 15 + sx, cy - 2, SPOT)
        px(im, 15 + sx + 1, cy - 2, SPOT)
    ring(im, 15, cy, 8.0, 5.0, INK)
    # A head that is clearly a head: rounder, bigger, and set above the spine
    # so there is a neck. The old one was the same depth as the body and the
    # whole animal read as one sausage.
    disc(im, 24, cy - 2, 5.6, 4.8, SKIN)
    ring(im, 24, cy - 2, 5.6, 4.8, INK)
    if f == 3:
        for k in range(-2, 3):
            px(im, 25 + k, cy - 3, INK)
    else:
        disc(im, 25, cy - 3, 2.1, 2.3, WHITE)
        ring(im, 25, cy - 3, 2.1, 2.3, INK)
        px(im, 25, cy - 3, INK)
    for k in range(4):                          # mouth line
        px(im, 25 + k, cy, INK)
    if f % 2 == 0:                              # tongue flick
        for k in range(3):
            px(im, 29 + k, cy, (240, 110, 130))
    disc(im, 21, cy, 1.3, 1.0, BLUSH)
    return im


def hedgehog(f):
    im = new()
    ph = f * math.pi / 2
    cy = 19 + int(round(math.sin(ph)))
    FACE = (232, 200, 168)
    COAT = (150, 116, 88)
    QUILL = (104, 78, 60)
    legs(im, 14, cy + 5, ph, INK, span=4)
    disc(im, 14, cy, 9.5, 7.0, COAT)            # spiky dome
    for a in range(-170, 10, 14):               # quills round the back
        r = math.radians(a)
        bx = 14 + math.cos(r) * 8.5
        by = cy + math.sin(r) * 6.2
        for k in range(4):
            px(im, bx + math.cos(r) * k, by + math.sin(r) * k,
               QUILL if k < 3 else INK)
    ring(im, 14, cy, 9.5, 7.0, INK)
    disc(im, 23, cy + 2, 5.2, 4.4, FACE)        # snout
    ring(im, 23, cy + 2, 5.2, 4.4, INK)
    if f == 2:
        for k in range(-1, 2):
            px(im, 23 + k, cy + 1, INK)
    else:
        disc(im, 23, cy + 1, 1.6, 1.8, INK)
        px(im, 23, cy, WHITE)
    disc(im, 27, cy + 3, 1.6, 1.3, INK)         # nose
    disc(im, 20, cy + 4, 1.4, 1.0, BLUSH)
    return im


def clownfish(f):
    im = new()
    ph = f * math.pi / 2
    cy = 17 + int(round(math.sin(ph)))
    ORANGE = (252, 150, 70)
    for k in range(5):                          # tail, fanning
        spread = 1 + int(abs(math.sin(ph)) * 2) + k // 2
        for j in range(-spread, spread + 1):
            px(im, 5 + k, cy + j, ORANGE)
        px(im, 5 + k, cy - spread - 1, INK)
        px(im, 5 + k, cy + spread + 1, INK)
    disc(im, 18, cy, 10.0, 7.0, ORANGE)         # body
    for bx in (13, 19, 25):                     # the three white bands
        for y in range(cy - 8, cy + 9):
            dx = (bx - 18) / 10.0
            dy = (y - cy) / 7.0
            if dx * dx + dy * dy <= 1.0:
                for w in range(2):
                    px(im, bx + w, y, WHITE)
                px(im, bx - 1, y, INK)
                px(im, bx + 2, y, INK)
    ring(im, 18, cy, 10.0, 7.0, INK)
    for k in range(4):                          # top fin
        px(im, 17 + k, cy - 7 - k // 2, ORANGE)
        px(im, 17 + k, cy - 8 - k // 2, INK)
    if f == 1:
        for k in range(-1, 2):
            px(im, 23 + k, cy - 1, INK)
    else:
        disc(im, 23, cy - 1, 2.0, 2.2, WHITE)
        ring(im, 23, cy - 1, 2.0, 2.2, INK)
        px(im, 23, cy - 1, INK)
    for k in range(2):                          # lips
        px(im, 27 + k, cy + 2, INK)
    return im


def eel(f):
    im = new()
    ph = f * math.pi / 2
    SKIN = (112, 134, 96)
    BELLY = (184, 196, 150)
    # One travelling wave, but a LONGER one -- at 0.42 radians a pixel it made
    # three sharp peaks and read as a zigzag rather than an animal. A gentler
    # period and a thicker body make it swim.
    for k in range(25):
        x = 4 + k
        y = 16 + math.sin(ph + k * 0.22) * 6.0
        thick = 4 if k < 16 else max(1, 4 - (k - 16) // 2)
        for j in range(-thick, thick + 1):
            px(im, x, y + j, SKIN if j < 1 else BELLY)
        px(im, x, y - thick - 1, INK)
        px(im, x, y + thick + 1, INK)
    # Head at the LEADING end, big enough to find.
    hy = 16 + math.sin(ph) * 6.0
    disc(im, 6, hy, 4.6, 4.2, SKIN)
    ring(im, 6, hy, 4.6, 4.2, INK)
    if f == 3:
        for k in range(-1, 2):
            px(im, 5 + k, hy - 1, INK)
    else:
        disc(im, 5, hy - 1, 1.8, 1.9, WHITE)
        ring(im, 5, hy - 1, 1.8, 1.9, INK)
        px(im, 5, hy - 1, INK)
    for k in range(3):                          # mouth
        px(im, 3 + k, hy + 2, INK)
    return im


def rat(f):
    """Pizza rat, in profile. One silhouette: body, head, snout and ear are a
    single mask, so the outline goes round the animal instead of through it.

    The rat sits LEFT of centre on purpose. Centred, its snout reached x=25 and
    the slice had six pixels to exist in, which is not a slice -- it is a yellow
    smudge. The animal is the smaller half of this picture; the slice is the
    joke."""
    im = new()
    ph = f * math.pi / 2
    cy = 18 + int(round(math.sin(ph)))
    COAT = (152, 142, 150)
    SHADE = (120, 110, 120)
    EAR = (236, 176, 188)
    TAIL = (214, 174, 182)

    for k in range(8):                        # tail, behind everything
        tx = 0 + k * 0.6
        ty = cy + 2 + math.sin(ph + k * 0.42) * 2.4
        px(im, tx, ty, TAIL)
        px(im, tx, ty + 1, INK)
        px(im, tx, ty - 1, INK)

    for i, lx in enumerate((6, 11)):           # feet, alternating
        off = math.sin(ph + i * math.pi) * 1.5
        for k in range(3):
            px(im, lx, cy + 5 + k + off, INK)
        px(im, lx + 1, cy + 7 + off, INK)

    # The slice goes down FIRST, crust where the mouth will be, so the snout is
    # painted over it and the rat is GRIPPING it rather than beside it.
    sy = cy + 2 + int(round(math.sin(ph + 1.0)))
    for k in range(11):
        h = int(round(5.0 - k * 0.45))
        for j in range(-h, h + 1):
            px(im, 21 + k, sy + j, (250, 208, 128))
        px(im, 21 + k, sy - h - 1, INK)
        px(im, 21 + k, sy + h + 1, INK)
    for j in range(-5, 6):                     # crust edge, at the mouth
        px(im, 20, sy + j, (214, 158, 92))
    disc(im, 25, sy - 1, 1.3, 1.1, (222, 82, 74))
    disc(im, 27, sy + 2, 1.1, 0.9, (222, 82, 74))
    disc(im, 24, sy + 2, 1.0, 0.8, (222, 82, 74))

    body = set()
    m_disc(body, 9, cy, 7.0, 5.4)              # body
    m_disc(body, 15, cy + 1, 4.6, 4.2)         # head
    m_taper(body, 19, 21, cy + 2, 2.8, 1.2)    # snout, onto the crust
    m_disc(body, 14, cy - 4, 3.0, 3.0)         # the one visible ear
    m_paint(im, body, COAT)
    m_shade(im, body, SHADE, cy + 3)
    m_outline(im, body)

    disc(im, 14, cy - 4, 1.5, 1.5, EAR)        # inner ear, after the outline
    if f == 3:
        for k in range(-1, 2):
            px(im, 16 + k, cy, INK)
    else:
        disc(im, 16, cy, 1.7, 1.9, INK)
        px(im, 16, cy - 1, WHITE)
    disc(im, 12, cy + 3, 1.4, 1.0, BLUSH)
    return im


def dolphin(f):
    im = new()
    ph = f * math.pi / 2
    cy = 17 + int(round(math.sin(ph) * 2))      # porpoising
    SKIN = (140, 178, 214)
    BELLY = (226, 238, 248)
    # Flukes as a proper horizontal V behind the body -- the old diagonal pair
    # crossed into an X and read as a propeller.
    ty = cy + 1 + int(round(math.sin(ph + 1.0) * 2))
    for k in range(5):
        for j in range(k // 2 + 1):
            px(im, 3 + k, ty - j, SKIN)
            px(im, 3 + k, ty + j, SKIN)
        px(im, 3 + k, ty - k // 2 - 1, INK)
        px(im, 3 + k, ty + k // 2 + 1, INK)
    disc(im, 16, cy, 9.5, 5.2, SKIN)            # body
    disc(im, 16, cy + 2, 7.5, 2.6, BELLY)
    for k in range(5):                          # dorsal fin, leaning back
        w = 3 - k // 2
        for j in range(w):
            px(im, 14 - k // 2 + j, cy - 4 - k, SKIN)
        px(im, 14 - k // 2 - 1, cy - 4 - k, INK)
        px(im, 14 - k // 2 + w, cy - 4 - k, INK)
    ring(im, 16, cy, 9.5, 5.2, INK)
    # The beak is what makes it a dolphin, so it is long, thin and separate.
    disc(im, 23, cy, 4.4, 4.0, SKIN)
    ring(im, 23, cy, 4.4, 4.0, INK)
    for k in range(6):
        px(im, 26 + k, cy + 2, SKIN)
        px(im, 26 + k, cy + 1, INK)
        px(im, 26 + k, cy + 3, INK)
    if f == 2:
        for k in range(-1, 2):
            px(im, 23 + k, cy - 1, INK)
    else:
        disc(im, 23, cy - 1, 1.7, 1.9, INK)
        px(im, 23, cy - 2, WHITE)
    for k in range(3):                          # the permanent grin
        px(im, 25 + k, cy + 1, INK)
    disc(im, 20, cy + 2, 1.4, 1.0, BLUSH)
    return im


def lizard(f):
    im = new()
    ph = f * math.pi / 2
    cy = 19
    SKIN = (126, 206, 138)
    SPOT = (96, 172, 108)
    for k in range(10):                         # tail, curling
        tx = 5 + k * 0.6
        ty = cy + 2 + math.sin(ph + k * 0.45) * 2.2
        px(im, tx, ty, SKIN)
        px(im, tx, ty + 1, INK)
        px(im, tx, ty - 1, INK)
    for side, i in ((-1, 0), (1, 1)):           # legs, alternating
        off = math.sin(ph + i * math.pi) * 1.6
        for lx in (13, 21):
            for k in range(3):
                px(im, lx + side, cy + 4 + k + off, INK)
    disc(im, 15, cy, 8.0, 5.0, SKIN)            # body, shorter and deeper
    for sx in (-4, 0, 4):                       # spots
        px(im, 15 + sx, cy - 2, SPOT)
        px(im, 15 + sx + 1, cy - 2, SPOT)
    ring(im, 15, cy, 8.0, 5.0, INK)
    # A head that is clearly a head: rounder, bigger, and set above the spine
    # so there is a neck. The old one was the same depth as the body and the
    # whole animal read as one sausage.
    disc(im, 24, cy - 2, 5.6, 4.8, SKIN)
    ring(im, 24, cy - 2, 5.6, 4.8, INK)
    if f == 3:
        for k in range(-2, 3):
            px(im, 25 + k, cy - 3, INK)
    else:
        disc(im, 25, cy - 3, 2.1, 2.3, WHITE)
        ring(im, 25, cy - 3, 2.1, 2.3, INK)
        px(im, 25, cy - 3, INK)
    for k in range(4):                          # mouth line
        px(im, 25 + k, cy, INK)
    if f % 2 == 0:                              # tongue flick
        for k in range(3):
            px(im, 29 + k, cy, (240, 110, 130))
    disc(im, 21, cy, 1.3, 1.0, BLUSH)
    return im


def hedgehog(f):
    im = new()
    ph = f * math.pi / 2
    cy = 19 + int(round(math.sin(ph)))
    FACE = (232, 200, 168)
    COAT = (150, 116, 88)
    QUILL = (104, 78, 60)
    legs(im, 14, cy + 5, ph, INK, span=4)
    disc(im, 14, cy, 9.5, 7.0, COAT)            # spiky dome
    for a in range(-170, 10, 14):               # quills round the back
        r = math.radians(a)
        bx = 14 + math.cos(r) * 8.5
        by = cy + math.sin(r) * 6.2
        for k in range(4):
            px(im, bx + math.cos(r) * k, by + math.sin(r) * k,
               QUILL if k < 3 else INK)
    ring(im, 14, cy, 9.5, 7.0, INK)
    disc(im, 23, cy + 2, 5.2, 4.4, FACE)        # snout
    ring(im, 23, cy + 2, 5.2, 4.4, INK)
    if f == 2:
        for k in range(-1, 2):
            px(im, 23 + k, cy + 1, INK)
    else:
        disc(im, 23, cy + 1, 1.6, 1.8, INK)
        px(im, 23, cy, WHITE)
    disc(im, 27, cy + 3, 1.6, 1.3, INK)         # nose
    disc(im, 20, cy + 4, 1.4, 1.0, BLUSH)
    return im


def clownfish(f):
    im = new()
    ph = f * math.pi / 2
    cy = 17 + int(round(math.sin(ph)))
    ORANGE = (252, 150, 70)
    for k in range(5):                          # tail, fanning
        spread = 1 + int(abs(math.sin(ph)) * 2) + k // 2
        for j in range(-spread, spread + 1):
            px(im, 5 + k, cy + j, ORANGE)
        px(im, 5 + k, cy - spread - 1, INK)
        px(im, 5 + k, cy + spread + 1, INK)
    disc(im, 18, cy, 10.0, 7.0, ORANGE)         # body
    for bx in (13, 19, 25):                     # the three white bands
        for y in range(cy - 8, cy + 9):
            dx = (bx - 18) / 10.0
            dy = (y - cy) / 7.0
            if dx * dx + dy * dy <= 1.0:
                for w in range(2):
                    px(im, bx + w, y, WHITE)
                px(im, bx - 1, y, INK)
                px(im, bx + 2, y, INK)
    ring(im, 18, cy, 10.0, 7.0, INK)
    for k in range(4):                          # top fin
        px(im, 17 + k, cy - 7 - k // 2, ORANGE)
        px(im, 17 + k, cy - 8 - k // 2, INK)
    if f == 1:
        for k in range(-1, 2):
            px(im, 23 + k, cy - 1, INK)
    else:
        disc(im, 23, cy - 1, 2.0, 2.2, WHITE)
        ring(im, 23, cy - 1, 2.0, 2.2, INK)
        px(im, 23, cy - 1, INK)
    for k in range(2):                          # lips
        px(im, 27 + k, cy + 2, INK)
    return im


def eel(f):
    im = new()
    ph = f * math.pi / 2
    SKIN = (112, 134, 96)
    BELLY = (184, 196, 150)
    # One travelling wave, but a LONGER one -- at 0.42 radians a pixel it made
    # three sharp peaks and read as a zigzag rather than an animal. A gentler
    # period and a thicker body make it swim.
    for k in range(25):
        x = 4 + k
        y = 16 + math.sin(ph + k * 0.22) * 6.0
        thick = 4 if k < 16 else max(1, 4 - (k - 16) // 2)
        for j in range(-thick, thick + 1):
            px(im, x, y + j, SKIN if j < 1 else BELLY)
        px(im, x, y - thick - 1, INK)
        px(im, x, y + thick + 1, INK)
    # Head at the LEADING end, big enough to find.
    hy = 16 + math.sin(ph) * 6.0
    disc(im, 6, hy, 4.6, 4.2, SKIN)
    ring(im, 6, hy, 4.6, 4.2, INK)
    if f == 3:
        for k in range(-1, 2):
            px(im, 5 + k, hy - 1, INK)
    else:
        disc(im, 5, hy - 1, 1.8, 1.9, WHITE)
        ring(im, 5, hy - 1, 1.8, 1.9, INK)
        px(im, 5, hy - 1, INK)
    for k in range(3):                          # mouth
        px(im, 3 + k, hy + 2, INK)
    return im


def rat(f):
    """Pizza rat, in profile. One silhouette: body, head, snout and ear are a
    single mask, so the outline goes round the animal instead of through it."""
    im = new()
    ph = f * math.pi / 2
    cy = 18 + int(round(math.sin(ph)))
    COAT = (152, 142, 150)
    SHADE = (120, 110, 120)
    EAR = (236, 176, 188)
    TAIL = (214, 174, 182)

    # Tail first, behind everything.
    for k in range(9):
        tx = 1 + k * 0.7
        ty = cy + 2 + math.sin(ph + k * 0.42) * 2.6
        px(im, tx, ty, TAIL)
        px(im, tx, ty + 1, INK)
        px(im, tx, ty - 1, INK)

    # Feet, under the body, alternating.
    for i, lx in enumerate((8, 14)):
        off = math.sin(ph + i * math.pi) * 1.5
        for k in range(3):
            px(im, lx, cy + 5 + k + off, INK)
        px(im, lx + 1, cy + 7 + off, INK)

    # The slice goes down FIRST, with its crust where the mouth will be: the
    # snout is painted over it a moment later, so the rat is GRIPPING it. Drawn
    # afterwards it sat in the air beside the animal with a gap, which is what
    # it looked like -- a rat and, separately, a slice.
    # Crust at x=24, where the snout ends, so the mouth just overlaps it -- and
    # the rest of the wedge OUTSIDE the silhouette where it can be seen. Tucked
    # any further in and the body ate it, which read as a yellow smudge.
    sy = cy + 3 + int(round(math.sin(ph + 1.0)))
    for k in range(8):
        h = int(round(4.0 - k * 0.55))
        for j in range(-h, h + 1):
            px(im, 24 + k, sy + j, (250, 208, 128))
        px(im, 24 + k, sy - h - 1, INK)
        px(im, 24 + k, sy + h + 1, INK)
    for j in range(-4, 5):                    # the crust edge, at the mouth
        px(im, 23, sy + j, (214, 158, 92))
    disc(im, 27, sy - 1, 1.2, 1.0, (222, 82, 74))
    disc(im, 29, sy + 1, 1.0, 0.8, (222, 82, 74))

    body = set()
    m_disc(body, 11, cy, 7.2, 5.6)            # body
    m_disc(body, 18, cy + 1, 5.0, 4.4)        # head, overlapping it
    m_taper(body, 22, 25, cy + 2, 3.0, 1.0)   # snout, tapering to a point
    m_disc(body, 17, cy - 4, 3.2, 3.2)        # the one visible ear
    m_paint(im, body, COAT)
    m_shade(im, body, SHADE, cy + 3)
    m_outline(im, body)

    disc(im, 17, cy - 4, 1.6, 1.6, EAR)       # inner ear, after the outline
    if f == 3:
        for k in range(-1, 2):
            px(im, 19 + k, cy, INK)
    else:
        disc(im, 19, cy, 1.7, 1.9, INK)
        px(im, 19, cy - 1, WHITE)
    disc(im, 15, cy + 3, 1.4, 1.0, BLUSH)
    for k in range(3):                        # whiskers off the snout
        px(im, 24 + k, cy + 4 + k // 2, INK)
        px(im, 24 + k, cy, INK)

    return im


def dolphin(f):
    """Also one silhouette -- body, melon, beak, dorsal and flukes together.
    Outlining the beak separately was what made it read as a submarine."""
    im = new()
    ph = f * math.pi / 2
    cy = 17 + int(round(math.sin(ph) * 2))
    SKIN = (132, 170, 210)
    BELLY = (222, 236, 248)

    shape = set()
    m_disc(shape, 14, cy, 9.0, 4.8)                       # body
    m_disc(shape, 21, cy - 1, 4.6, 4.0)                   # melon
    m_taper(shape, 25, 30, cy + 1, 2.4, 0.8)              # beak
    # Dorsal fin, swept back, and a pectoral below. Part of the mask, so the
    # outline wraps them instead of drawing a fence between them and the body.
    m_tri(shape, (11, cy - 10), (16, cy - 3), (9, cy - 3))
    m_tri(shape, (14, cy + 8), (19, cy + 2), (13, cy + 2))
    # Flukes: a horizontal V at the tail.
    fy = cy + int(round(math.sin(ph + 1.0) * 2))
    m_tri(shape, (8, fy), (2, fy - 5), (2, fy - 1))
    m_tri(shape, (8, fy), (2, fy + 5), (2, fy + 1))
    m_paint(im, shape, SKIN)
    m_shade(im, shape, BELLY, cy + 2)
    m_outline(im, shape)

    if f == 2:
        for k in range(-1, 2):
            px(im, 21 + k, cy - 2, INK)
    else:
        disc(im, 21, cy - 2, 1.7, 1.9, INK)
        px(im, 21, cy - 3, WHITE)
    for k in range(4):                                    # the permanent grin
        px(im, 23 + k, cy + 1, INK)
    disc(im, 18, cy + 2, 1.4, 1.0, BLUSH)
    return im


SPECIES = {
    "mouse": mouse,
    "rat": rat,
    "crab": crab,
    "frog": frog,
    "snail": snail,
    "moth": moth,
    "cat": cat,
    # Tropical London: things that would and would not be in the Thames.
    "dolphin": dolphin,
    "lizard": lizard,
    "hedgehog": hedgehog,
    "clownfish": clownfish,
    "eel": eel,
}


def main(outdir, preview=None):
    os.makedirs(outdir, exist_ok=True)
    if preview:
        os.makedirs(preview, exist_ok=True)
    frames = 0
    for name, fn in SPECIES.items():
        for f in range(4):
            fn(f).save(os.path.join(outdir, "%s-%d.png" % (name, f + 1)))
            frames += 1
        # Filmstrips go to a preview dir, NOT into the set: data/sprites is
        # scanned by the video synth, and a 128x32 strip would be offered there
        # as a glyph alongside the frames.
        if preview:
            strip = Image.new("RGBA", (S * 4, S), (0, 0, 0, 0))
            for f in range(4):
                strip.paste(fn(f), (S * f, 0))
            strip.save(os.path.join(preview, "%s-strip.png" % name))
    print("wrote %d frames + %d strips to %s" % (frames, len(SPECIES), outdir))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
