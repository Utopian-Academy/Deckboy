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
    im = new()
    ph = f * math.pi / 2
    cy = 19 + int(round(math.sin(ph)))
    COAT = (150, 140, 148)
    SHADE = (122, 112, 122)
    # Long bare tail, thicker and longer than the mouse's -- that and the
    # snout are what stop the two reading as the same animal.
    for k in range(11):
        tx = 3 + k * 0.5
        ty = cy + 3 + math.sin(ph + k * 0.4) * 2.4
        px(im, tx, ty, (208, 168, 176))
        px(im, tx, ty + 1, INK)
    legs(im, 15, cy + 5, ph, INK, span=5)
    for sx in (-5, 5):                          # small round ears
        disc(im, 15 + sx, cy - 7, 2.8, 2.8, COAT)
        ring(im, 15 + sx, cy - 7, 2.8, 2.8, INK)
        disc(im, 15 + sx, cy - 7, 1.3, 1.3, (238, 176, 188))
    # One body, then a head clearly PAST it. Overlapping two blobs of the same
    # colour made a lump with an eye in the middle of it.
    disc(im, 13, cy, 8.0, 6.2, COAT)
    disc(im, 13, cy + 3, 6.2, 2.6, SHADE)
    ring(im, 13, cy, 8.0, 6.2, INK)
    disc(im, 22, cy + 1, 5.0, 4.4, COAT)        # head
    ring(im, 22, cy + 1, 5.0, 4.4, INK)
    for k in range(4):                          # pointed snout
        px(im, 26 + k, cy + 2, COAT)
        px(im, 26 + k, cy + 1, COAT)
        px(im, 26 + k, cy, INK)
        px(im, 26 + k, cy + 3, INK)
    disc(im, 29, cy + 2, 1.4, 1.2, (240, 130, 150))
    if f == 3:
        for k in range(-1, 2):
            px(im, 22 + k, cy, INK)
    else:
        disc(im, 22, cy, 1.7, 1.9, INK)
        px(im, 22, cy - 1, WHITE)
    disc(im, 19, cy + 3, 1.3, 1.0, BLUSH)
    # The slice, held in FRONT and below the ears rather than balanced on the
    # head, where it covered them.
    sx = 9
    sy = cy - 4 + int(round(math.sin(ph + 1.0)))
    for k in range(5):
        w = k
        for j in range(-w, w + 1):
            px(im, sx + j, sy + k, (248, 206, 126))
        px(im, sx - w - 1, sy + k, INK)
        px(im, sx + w + 1, sy + k, INK)
    for j in range(-4, 5):
        px(im, sx + j, sy + 5, (206, 150, 86))
    for j in range(-5, 6):
        px(im, sx + j, sy + 6, INK)
    for ox, oy in ((-1, 3), (2, 4)):
        disc(im, sx + ox, sy + oy, 1.1, 0.9, (222, 82, 74))
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
