"""Draw the busy critters that used to be only heads: mouse, cat and frog.

James, 2026-10-14: the critters "look weird as just heads bobbing back and
forth". The style follows the original inspiration he collected (a tardigrade,
a lizard, a jellyfish and a rat): whole animals seen from the side, soft
shading, no black outline -- the edge is a darker shade of the animal itself.

Each animal is built from simple shapes at 4x, reduced to 32x32 with hard
alpha (pixel art has no soft edges), then shaded: lighter on top, darker
underneath, and a one-pixel rim in the body's own shadow colour. Four frames,
the size the desk already loads, so nothing on screen grows.

    python tools/make_critters.py [--sheet out.png]
"""
import argparse
import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "data/sprites/critters"
S = 4  # supersample
N = 32


def shade(color, factor):
    return tuple(max(0, min(255, int(c * factor))) for c in color[:3]) + (255,)


class Canvas:
    def __init__(self):
        self.im = Image.new("RGBA", (N * S, N * S), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.im)

    def oval(self, x0, y0, x1, y1, color):
        self.d.ellipse([x0 * S, y0 * S, x1 * S, y1 * S], fill=color)

    def poly(self, points, color):
        self.d.polygon([(x * S, y * S) for x, y in points], fill=color)

    def line(self, points, color, width):
        self.d.line([(x * S, y * S) for x, y in points], fill=color, width=int(width * S), joint="curve")

    def finish(self, rim=0.62):
        small = self.im.resize((N, N), Image.BOX)
        px = small.load()
        out = Image.new("RGBA", (N, N), (0, 0, 0, 0))
        po = out.load()
        for y in range(N):
            for x in range(N):
                r, g, b, a = px[x, y]
                if a >= 120:
                    po[x, y] = (r, g, b, 255)
        # Rim: an opaque pixel touching transparency takes its own shadow.
        rimmed = out.copy()
        pr = rimmed.load()
        for y in range(N):
            for x in range(N):
                if po[x, y][3] == 0:
                    continue
                edge = any(not (0 <= x + dx < N and 0 <= y + dy < N) or po[x + dx, y + dy][3] == 0
                           for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
                if edge:
                    pr[x, y] = shade(po[x, y], rim)
        return rimmed


def body_oval(c, x0, y0, x1, y1, base):
    """An oval lit from above: shadow, base, then a highlight band."""
    c.oval(x0, y0, x1, y1, shade(base, 0.82))
    c.oval(x0 + 0.4, y0, x1 - 0.4, y1 - (y1 - y0) * 0.22, base)
    c.oval(x0 + (x1 - x0) * 0.18, y0 + 0.6, x1 - (x1 - x0) * 0.3, y0 + (y1 - y0) * 0.42, shade(base, 1.12))


def eye(c, x, y):
    c.oval(x - 1.3, y - 1.3, x + 1.3, y + 1.3, (34, 30, 40, 255))
    c.oval(x - 0.6, y - 1.0, x + 0.2, y - 0.2, (255, 255, 255, 255))


def legs(c, xs, top, phase, color, length=3.2, width=2.0):
    for i, x in enumerate(xs):
        swing = math.sin(phase + i * math.pi) * 1.1
        c.oval(x + swing - width / 2, top, x + swing + width / 2, top + length, color)


def mouse(frame):
    c = Canvas()
    grey, pink = (178, 176, 186, 255), (238, 164, 176, 255)
    phase = frame * math.pi / 2
    bob = 0.5 if frame % 2 else 0.0
    c.line([(6, 22 - bob), (3, 21), (2, 18), (3.5, 16)], pink, 1.4)          # tail
    legs(c, [9.5, 13, 18.5, 22], 22 - bob, phase, shade(grey, 0.85), 4.6, 2.4)
    body_oval(c, 6, 14 - bob, 24, 25 - bob, grey)                            # body
    body_oval(c, 18, 12 - bob, 29, 21 - bob, grey)                           # head
    c.oval(17, 7 - bob, 24, 14 - bob, shade(grey, 1.08))                     # ear
    c.oval(18.6, 8.6 - bob, 22.4, 12.4 - bob, pink)
    c.oval(27.6, 16 - bob, 30, 18.2 - bob, pink)                             # nose
    eye(c, 24.5, 15.2 - bob)
    return c.finish()


def cat(frame):
    c = Canvas()
    orange, stripe, cream = (230, 152, 78, 255), (196, 112, 52, 255), (248, 214, 168, 255)
    phase = frame * math.pi / 2
    bob = 0.5 if frame % 2 else 0.0
    sway = math.sin(phase) * 1.0
    c.line([(6, 18 - bob), (3.5, 14), (3 + sway, 9), (5 + sway, 6.5)], orange, 2.2)   # tail up
    legs(c, [9, 12.5, 18, 21.5], 23 - bob, phase, shade(orange, 0.9), 4.0, 2.0)
    body_oval(c, 5, 14 - bob, 24, 25 - bob, orange)
    for sx in (11, 15, 19):
        c.line([(sx, 14.6 - bob), (sx - 0.6, 18.5 - bob)], stripe, 1.3)
    c.oval(9, 20 - bob, 20, 24.6 - bob, cream)                               # belly
    body_oval(c, 19, 10 - bob, 29.5, 20 - bob, orange)                       # head
    c.poly([(19.8, 12 - bob), (21, 6.5 - bob), (24, 11 - bob)], orange)      # ears
    c.poly([(25, 11 - bob), (28, 6.5 - bob), (29, 12.5 - bob)], orange)
    c.poly([(21.2, 11 - bob), (21.6, 8.4 - bob), (23, 10.8 - bob)], (238, 150, 160, 255))
    c.oval(23, 16 - bob, 29, 19.6 - bob, cream)                              # muzzle
    c.oval(28.2, 15.4 - bob, 29.8, 16.8 - bob, (220, 110, 120, 255))         # nose
    eye(c, 25.6, 14.0 - bob)
    return c.finish()


def frog(frame):
    c = Canvas()
    green, belly = (122, 196, 118, 255), (206, 232, 166, 255)
    lift = [0.0, 1.0, 4.5, 1.0][frame]
    stretch = frame == 2
    back = (shade(green, 0.86))
    if stretch:
        c.line([(9, 21 - lift), (4, 24 - lift), (1.5, 25.5 - lift)], back, 2.6)  # legs out behind
        c.line([(22, 23 - lift), (25, 26 - lift)], back, 2.0)
    else:
        c.oval(5, 18 - lift, 14, 27 - lift, back)                               # folded thigh
        c.oval(3, 25 - lift, 12, 28 - lift, back)                               # foot
        c.oval(20, 24 - lift, 25, 28 - lift, back)                              # front foot
    body_oval(c, 6, 13 - lift, 27, 26 - lift, green)
    c.oval(10, 20 - lift, 25, 26 - lift, belly)
    body_oval(c, 18, 8.5 - lift, 25.5, 15.5 - lift, green)                     # eye bump
    eye(c, 22, 12 - lift)
    c.line([(23.5, 19.5 - lift), (27.5, 18.5 - lift)], shade(green, 0.6), 0.8)  # mouth
    return c.finish()


DRAW = {"mouse": mouse, "cat": cat, "frog": frog}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sheet", help="also write a contact sheet, 6x scale")
    args = parser.parse_args()
    drawn = {name: [fn(i) for i in range(4)] for name, fn in DRAW.items()}
    for name, images in drawn.items():
        for i, image in enumerate(images):
            image.save(OUT / f"{name}-{i + 1}.png")
    if args.sheet:
        sheet = Image.new("RGBA", (32 * 4 * 6, 32 * len(drawn) * 6), (30, 34, 40, 255))
        for row, images in enumerate(drawn.values()):
            for col, image in enumerate(images):
                sheet.alpha_composite(image.resize((192, 192), Image.NEAREST), (col * 192, row * 192))
        sheet.save(args.sheet)
    print("wrote", sum(len(v) for v in drawn.values()), "frames to", OUT)


if __name__ == "__main__":
    main()
