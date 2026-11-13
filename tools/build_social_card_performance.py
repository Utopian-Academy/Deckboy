#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
"""Build the performance social card (1280 x 640) beside the main one.

Same layout, palette and font as tools/build_social_card.py. The picture is a
real output frame: a portal transition half way out of a cue with a
kaleidoscope and a glow on it, captured with OUTSNAP from a GPU build.

    python tools/build_social_card_performance.py --frame build/perf-card/mid-1.bmp

Every number on the card is measured: 4K portal outgoing draw 46-55 ms a frame
before, 3.4-5.8 ms after (tools/check_hotspots.py --visible --only transitions,
2026-11-12, RTX 4070 Laptop).
"""
import argparse
import io
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "docs/images/social-card-performance.png"
SIZE = (1280, 640)
SAFE = 40
GROUND = "#091809"
ACCENT = "#b0cf28"
INK = "#e0e8cf"
MUTED = "#a1bb51"
FRAME = "#53643d"


def build(frame):
    card = Image.new("RGB", SIZE, GROUND)
    draw = ImageDraw.Draw(card)
    mono = ROOT / "data/fonts/LiberationMono-Regular.ttf"

    def text(x, y, value, size, fill, right=520):
        font = ImageFont.truetype(str(mono), size)
        box = draw.textbbox((x, y), value, font=font, anchor="lt")
        if box[0] < SAFE or box[1] < SAFE or box[2] > right or box[3] > SIZE[1] - SAFE:
            raise ValueError(f"Text outside its safe column: {value}")
        draw.text((x, y), value, font=font, fill=fill, anchor="lt")

    draw.rectangle((40, 120, 45, 520), fill=ACCENT)
    text(64, 144, "DECKBOY", 68, ACCENT)
    text(66, 254, "Effects and transitions", 27, INK)
    text(66, 291, "now run on the GPU.", 27, INK)
    text(66, 374, "4K output at 60 fps.", 21, MUTED)
    text(66, 406, "Portal at 4K: 50 ms -> 4 ms.", 21, MUTED)
    text(66, 438, "Older machines use the CPU.", 21, MUTED)
    text(66, 494, "Windows  macOS  Linux", 23, INK)

    with Image.open(frame) as source:
        picture = source.convert("RGB")
    picture.thumbnail((688, 490), Image.Resampling.LANCZOS)
    x, y = 1232 - picture.width, (SIZE[1] - picture.height) // 2
    border = (x - 6, y - 6, x + picture.width + 5, y + picture.height + 5)
    if border[0] < 530 or border[1] < SAFE or border[2] >= SIZE[0] - SAFE or border[3] >= SIZE[1] - SAFE:
        raise ValueError(f"Picture does not fit the safe area: {border}")
    draw.rectangle(border, fill=FRAME)
    card.paste(picture, (x, y))
    encoded = io.BytesIO()
    card.save(encoded, format="PNG", optimize=True)
    data = encoded.getvalue()
    if len(data) >= 1_000_000:
        raise ValueError(f"Card exceeds 1 MB: {len(data)} bytes")
    OUTPUT.write_bytes(data)
    print(f"{OUTPUT.relative_to(ROOT)}: 1280 x 640, {len(data):,} bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--frame", type=Path, required=True, help="An output frame (OUTSNAP)")
    build(parser.parse_args().frame)


if __name__ == "__main__":
    main()
