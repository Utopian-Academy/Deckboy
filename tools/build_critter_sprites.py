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


SPECIES = {
    "mouse": mouse,
    "crab": crab,
    "frog": frog,
    "snail": snail,
    "moth": moth,
    "cat": cat,
}


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    frames = 0
    for name, fn in SPECIES.items():
        for f in range(4):
            fn(f).save(os.path.join(outdir, "%s-%d.png" % (name, f + 1)))
            frames += 1
        strip = Image.new("RGBA", (S * 4, S), (0, 0, 0, 0))
        for f in range(4):
            strip.paste(fn(f), (S * f, 0))
        strip.save(os.path.join(outdir, "%s-strip.png" % name))
    print("wrote %d frames + %d strips to %s" % (frames, len(SPECIES), outdir))


if __name__ == "__main__":
    main(sys.argv[1])
