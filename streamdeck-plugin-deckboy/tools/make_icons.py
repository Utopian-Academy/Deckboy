#!/usr/bin/env python3
"""Generate the plugin's artwork from one source of truth.

Elgato's Marketplace guidelines fix the sizes, and they are not the sizes a
plugin needs to *work*, which is why the placeholders passed for so long:

  plugin icon        256x256 / 512x512   (the Marketplace listing)
  category icon       28x28  /  56x56
  action list icon    20x20  /  40x40    white stroke, transparent, no fill
  key image           72x72  / 144x144   the button the operator actually sees

The glyphs are drawn on a 24x24 grid and scaled with NEAREST, so they stay
pixel art at every size, which is what the rest of Deckboy looks like.
"""

import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN = os.path.join(HERE, "..", "com.deckboy.streamdeck.sdPlugin")
IMGS = os.path.join(PLUGIN, "imgs")
APP_ICON = os.path.join(HERE, "..", "..", "art", "windows", "icons", "deckboy_app_master.png")

G = 24  # glyph grid

# Key backgrounds. Deckboy's terminal green for the go-keys, red for the ones
# that take the show off air, so a dark rack reads at a glance.
KEYS = {
    "take":     ((26, 58, 36), (126, 217, 87)),
    "stop":     ((58, 26, 27), (232, 122, 124)),
    "pause":    ((58, 46, 22), (233, 196, 96)),
    "panic":    ((72, 18, 19), (255, 108, 96)),
    "blackout": ((26, 26, 26), (196, 196, 196)),
    "command":  ((24, 40, 58), (126, 186, 237)),
}


def glyph(name, draw):
    """Draw one glyph on a 24x24 grid in solid white."""
    w = (255, 255, 255, 255)
    if name == "take":                                  # play
        draw.polygon([(7, 3), (7, 21), (20, 12)], fill=w)
    elif name == "stop":                                # square
        draw.rectangle([5, 5, 18, 18], fill=w)
    elif name == "pause":                               # two bars
        draw.rectangle([6, 4, 10, 19], fill=w)
        draw.rectangle([13, 4, 17, 19], fill=w)
    elif name == "panic":                               # warning triangle
        draw.polygon([(12, 1), (22, 21), (2, 21)], fill=w)
        draw.polygon([(12, 5), (19, 19), (5, 19)], fill=(0, 0, 0, 0))
        draw.rectangle([11, 11, 13, 15], fill=w)
        draw.rectangle([11, 17, 13, 18], fill=w)
    elif name == "blackout":                            # half-dark disc
        draw.ellipse([3, 3, 20, 20], outline=w, width=2)
        draw.pieslice([3, 3, 20, 20], -90, 90, fill=w)
    elif name == "command":                             # prompt: >_
        draw.line([(5, 5), (12, 11)], fill=w, width=3)
        draw.line([(12, 11), (5, 17)], fill=w, width=3)
        draw.rectangle([14, 17, 21, 19], fill=w)
    else:
        raise ValueError(name)


def render_glyph(name, size, colour=None):
    """One glyph, transparent background, NEAREST-scaled to `size`."""
    im = Image.new("RGBA", (G, G), (0, 0, 0, 0))
    glyph(name, ImageDraw.Draw(im))
    if colour is not None:
        tint = Image.new("RGBA", (G, G), colour + (255,))
        tint.putalpha(im.getchannel("A"))
        im = tint
    return im.resize((size, size), Image.NEAREST)


def key_image(name, size):
    """The button: flat field, 1px-equivalent border, glyph inset."""
    bg, fg = KEYS[name]
    im = Image.new("RGBA", (G, G), bg + (255,))
    ImageDraw.Draw(im).rectangle([0, 0, G - 1, G - 1], outline=tuple(
        min(255, c + 48) for c in bg) + (255,))
    im = im.resize((size, size), Image.NEAREST)
    # The glyph sits in the middle 70% so the title still has room underneath.
    inset = int(size * 0.16)
    gl = render_glyph(name, size - inset * 2, fg)
    im.alpha_composite(gl, (inset, inset - int(size * 0.05)))
    return im


def save(im, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    im.save(path, "PNG", optimize=True)
    print("  %-44s %dx%d" % (os.path.relpath(path, PLUGIN), im.width, im.height))


def main():
    print("action icons (list, white on transparent) + key images")
    for name in KEYS:
        save(render_glyph(name, 20), os.path.join(IMGS, "actions", name + "-list.png"))
        save(render_glyph(name, 40), os.path.join(IMGS, "actions", name + "-list@2x.png"))
        save(key_image(name, 72), os.path.join(IMGS, "actions", name + ".png"))
        save(key_image(name, 144), os.path.join(IMGS, "actions", name + "@2x.png"))

    print("plugin icon (Marketplace listing)")
    src = Image.open(APP_ICON).convert("RGBA")
    save(src.resize((256, 256), Image.LANCZOS), os.path.join(IMGS, "plugin.png"))
    save(src.resize((512, 512), Image.LANCZOS), os.path.join(IMGS, "plugin@2x.png"))

    print("category icon")
    save(src.resize((28, 28), Image.LANCZOS), os.path.join(IMGS, "category.png"))
    save(src.resize((56, 56), Image.LANCZOS), os.path.join(IMGS, "category@2x.png"))


if __name__ == "__main__":
    main()
