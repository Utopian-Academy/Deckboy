"""Draw the busy critters that used to be only heads: mouse, cat and frog.

James, 2026-10-14: the critters "look weird as just heads bobbing back and
forth". These are whole animals seen from the side, with legs that move, in
four frames at the same 32x32 the desk already loads, so their size on screen
does not change. Each map is 16 pixels wide, drawn at 2x.

    python tools/make_critters.py [--sheet out.png]
"""
import argparse
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "data/sprites/critters"

INK = {
    "k": (32, 28, 40, 255),     # outline, the same ink as the other critters
    "w": (255, 255, 255, 255),
    "p": (255, 150, 170, 255),  # pink: ears, nose
}
SPECIES = {
    "mouse": {"b": (196, 190, 205, 255), "l": (230, 226, 236, 255)},
    "cat": {"b": (226, 160, 92, 255), "l": (246, 214, 170, 255), "s": (190, 118, 60, 255)},
    "frog": {"b": (150, 210, 110, 255), "l": (214, 236, 170, 255), "s": (110, 170, 80, 255)},
}

MOUSE = [
    "................",
    "..........kk....",
    ".........kppk...",
    "....kkkkkkbbkk..",
    "...kbbbbbbbbwkk.",
    "..kbbbbbbbbbbbbk",
    ".kbbbbbbbbbbbbpk",
    "kkbllllllbbbbbk.",
    "k.kbllllllbbkk..",
    "...kkkkkkkkk....",
]
CAT = [
    "...........k..k.",
    ".k........kbkkbk",
    ".kk.......kbbbbk",
    "..k.kkkkkkkbkbkk",
    "..kkbbsbbsbbbbpk",
    "...kbbsbbsbbbbk.",
    "...kbllbbbbbbbk.",
    "...kbllllbbbbk..",
    "....kkkkkkkkk...",
]
WALK = [  # two leg rows per frame; frames 2 and 4 are lifted a pixel
    ["...kbk...kbk....", "...kk....kk....."],
    ["....kbk.kbk.....", "....kk..kk......"],
    ["....kbk..kbk....", ".....kk...kk...."],
    ["...kbk..kbk.....", "...kk...kk......"],
]
FROG_SIT = [
    "..........kkk...",
    ".........kwkkk..",
    "....kkkkkkbbbbk.",
    "...kbbsbbbbbbbbk",
    "..kbbbbbbsbbbbk.",
    "..kbllllllllbk..",
    ".kkbbkllllkbk...",
    ".kbbbk.kkk.kbk..",
    "kkkkk.....kkkk..",
]
FROG_LEAP = [
    "...........kkk..",
    "..........kwkkk.",
    ".....kkkkkkbbbbk",
    "...kkbbsbbbbbbbk",
    ".kkbbbbbbsbbbbk.",
    "kbbkllllllllbk..",
    "kkk.kkkkkkkkkk..",
    "................",
    "................",
]


def render(rows, palette, lift):
    ink = dict(INK, **palette)
    image = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    top = 32 - len(rows) * 2 - lift * 2
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch in ink:
                for dy in range(2):
                    for dx in range(2):
                        image.putpixel((x * 2 + dx, top + y * 2 + dy), ink[ch])
    return image


def frames():
    out = {}
    for name, body in (("mouse", MOUSE), ("cat", CAT)):
        out[name] = [render(body + legs, SPECIES[name], i % 2) for i, legs in enumerate(WALK)]
    frog = SPECIES["frog"]
    out["frog"] = [render(FROG_SIT, frog, 0), render(FROG_SIT, frog, 1),
                   render(FROG_LEAP, frog, 3), render(FROG_SIT, frog, 1)]
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sheet", help="also write a contact sheet, 6x scale")
    args = parser.parse_args()
    drawn = frames()
    for name, images in drawn.items():
        for i, image in enumerate(images):
            image.save(OUT / f"{name}-{i + 1}.png")
    if args.sheet:
        sheet = Image.new("RGBA", (32 * 4 * 6, 32 * len(drawn) * 6), (196, 207, 161, 255))
        for row, images in enumerate(drawn.values()):
            for col, image in enumerate(images):
                big = image.resize((192, 192), Image.NEAREST)
                sheet.alpha_composite(big, (col * 192, row * 192))
        sheet.save(args.sheet)
    print("wrote", sum(len(v) for v in drawn.values()), "frames to", OUT)


if __name__ == "__main__":
    main()
