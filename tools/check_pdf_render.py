#!/usr/bin/env python3
"""Slides and PDFs render at full size, the right way up, with nothing round them.

James, 2026-10-28: a PowerPoint "came in too small" on a Mac. Since v0.89.0
the Mac renderer fitted each page with CoreGraphics' own drawing transform,
which only ever scales a page DOWN. Every slide is smaller in points than the
3840 pixels the import renders at, so each one was drawn at its own size in the
middle of a white frame -- a 960x540 slide was a quarter of the width. Nothing
checked the rendered pages on any platform, which is how it shipped for two
months.

This builds a PDF whose pages are filled edge to edge with one colour and carry
a red mark top left and a green one top right, renders it the way slide import
does (--pdf-probe), and reads the pages back. A page drawn small shows white at
its edges; one turned or mirrored moves a mark. The pages are a quarter the
size of real ones and render 960 wide, so the 4x scale-up is the one a 960x540
slide gets at 3840, and the PNGs stay small enough to read without Pillow.

    py -3 tools/check_pdf_render.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WIDTH = 960

BACKGROUND = (0.20, 0.45, 0.80)
RED = (0.90, 0.10, 0.10)
GREEN = (0.10, 0.75, 0.25)
MAGENTA = (1.0, 0.0, 1.0)          # outside the crop box: must never show
TOLERANCE = 24

# name, media box, crop box (or None), /Rotate
PAGES = [
    ("a 16:9 slide (960x540 at a quarter)", (0, 0, 240, 135), None, 0),
    ("a 3840x1520 LED-wall slide (at a quarter)", (0, 0, 720, 285), None, 0),
    ("a portrait letter page", (0, 0, 153, 198), None, 0),
    ("a landscape slide stored portrait, /Rotate 90", (0, 0, 135, 240), None, 90),
    ("a crop box inside a bigger media box", (0, 0, 300, 180), (30, 20, 270, 155), 0),
    ("a page bigger than the render (scaled down)", (0, 0, 1920, 1080), None, 0),
]

fails = []


def note(name, ok, info=""):
    print("  %-62s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def displayed_size(media, crop, rotate):
    x0, y0, x1, y1 = crop or media
    w, h = x1 - x0, y1 - y0
    return (h, w) if rotate in (90, 270) else (w, h)


def page_rect(media, crop, rotate, u0, v0, u1, v1):
    """A rectangle given as it is SEEN (u right, v down) in the page's own space."""
    x0, y0, x1, y1 = crop or media
    if rotate == 90:
        # Turned clockwise to be seen: the stored bottom-left is the seen top-left.
        return x0 + v0, y0 + u0, v1 - v0, u1 - u0
    return x0 + u0, y1 - v1, u1 - u0, v1 - v0


def content(media, crop, rotate):
    w, h = displayed_size(media, crop, rotate)
    ops = []

    def fill(colour, rect):
        ops.append("%.3f %.3f %.3f rg %.3f %.3f %.3f %.3f re f" % (colour + rect))

    if crop:
        mx0, my0, mx1, my1 = media
        fill(MAGENTA, (mx0, my0, mx1 - mx0, my1 - my0))
    fill(BACKGROUND, page_rect(media, crop, rotate, 0, 0, w, h))
    fill(RED, page_rect(media, crop, rotate, 0, 0, 0.12 * w, 0.12 * h))
    fill(GREEN, page_rect(media, crop, rotate, 0.88 * w, 0, w, 0.12 * h))
    return "\n".join(ops).encode("ascii")


def write_pdf(path):
    objects = []
    kids = []
    for _, media, crop, rotate in PAGES:
        stream = content(media, crop, rotate)
        objects.append(b"<< /Length %d >>\nstream\n%s\nendstream" % (len(stream), stream))
        contents_id = len(objects) + 2
        entries = "/Type /Page /Parent 2 0 R /MediaBox [%g %g %g %g]" % media
        if crop:
            entries += " /CropBox [%g %g %g %g]" % crop
        if rotate:
            entries += " /Rotate %d" % rotate
        entries += " /Resources << >> /Contents %d 0 R" % contents_id
        objects.append(("<< %s >>" % entries).encode("ascii"))
        kids.append(len(objects) + 2)
    head = [b"<< /Type /Catalog /Pages 2 0 R >>",
            ("<< /Type /Pages /Kids [%s] /Count %d >>" % (
                " ".join("%d 0 R" % k for k in kids), len(kids))).encode("ascii")]
    objects = head + objects
    out = bytearray(b"%PDF-1.4\n")
    offsets = []
    for number, body in enumerate(objects, 1):
        offsets.append(len(out))
        out += b"%d 0 obj\n%s\nendobj\n" % (number, body)
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    for offset in offsets:
        out += b"%010d 00000 n \n" % offset
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objects) + 1, xref)
    path.write_bytes(bytes(out))


def read_png(path):
    """Width, height and rows of RGB tuples, from an 8-bit non-interlaced PNG."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    at, idat, palette = 8, bytearray(), None
    while at < len(data):
        length = int.from_bytes(data[at:at + 4], "big")
        kind = data[at + 4:at + 8]
        body = data[at + 8:at + 8 + length]
        at += 12 + length
        if kind == b"IHDR":
            width, height = int.from_bytes(body[0:4], "big"), int.from_bytes(body[4:8], "big")
            depth, colour, interlace = body[8], body[9], body[12]
        elif kind == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour]
    if depth != 8 or interlace:
        raise ValueError("%d-bit, interlace %d: not read here" % (depth, interlace))
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        if kind == 2:
            line = bytearray((a + b) & 255 for a, b in zip(line, previous))
        elif kind:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                up = previous[i]
                if kind == 1:
                    line[i] = (line[i] + left) & 255
                elif kind == 3:
                    line[i] = (line[i] + ((left + up) >> 1)) & 255
                else:
                    corner = previous[i - channels] if i >= channels else 0
                    p = left + up - corner
                    pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                    line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        rows.append(line)
        previous = line

    def pixel(x, y):
        line = rows[y]
        if colour == 3:
            return palette[line[x]]
        if channels <= 2:
            return (line[x * channels],) * 3
        return tuple(line[x * channels:x * channels + 3])

    return width, height, pixel


def near(seen, want):
    return all(abs(s - round(w * 255)) <= TOLERANCE for s, w in zip(seen, want))


def coverage(width, height, pixel):
    """How much of the frame is not white: the page, if it was drawn small."""
    xs, ys = [], []
    for y in range(0, height, 4):
        for x in range(0, width, 4):
            if any(c < 255 - TOLERANCE for c in pixel(x, y)):
                xs.append(x)
                ys.append(y)
    if not xs:
        return "the frame is all white"
    return "the page fills %.0f%% x %.0f%% of the frame" % (
        100.0 * (max(xs) - min(xs) + 4) / width, 100.0 * (max(ys) - min(ys) + 4) / height)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: slides and PDFs render at full size\n")

    work = Path(tempfile.mkdtemp(prefix="deckboy-pdf-render-"))
    pdf = work / "pages.pdf"
    write_pdf(pdf)
    out = work / "pages"
    run = subprocess.run([str(exe), "--pdf-probe", str(pdf), str(out), str(WIDTH)],
                         capture_output=True, text=True, encoding="utf8", errors="replace",
                         timeout=180)
    said = (run.stdout + run.stderr).strip()
    if "unavailable" in said:
        print("NOT CHECKED: " + said.splitlines()[0])
        return 0
    print("  " + "\n  ".join(line for line in said.splitlines() if line.startswith("pdf-probe")))
    pngs = sorted(out.glob("page*.png"))
    note("every page rendered", run.returncode == 0 and len(pngs) == len(PAGES),
         "%d of %d pages, exit %d" % (len(pngs), len(PAGES), run.returncode))

    for (name, media, crop, rotate), png in zip(PAGES, pngs):
        w, h = displayed_size(media, crop, rotate)
        want = (WIDTH, round(WIDTH * h / w))
        width, height, pixel = read_png(png)
        if abs(width - want[0]) > 1 or abs(height - want[1]) > 1:
            note(name, False, "%dx%d, wanted %dx%d" % (width, height, want[0], want[1]))
            continue

        def at(fx, fy):
            return pixel(min(width - 1, int(fx * width)), min(height - 1, int(fy * height)))

        wrong = []
        for (fx, fy), colour, what in [
            ((0.06, 0.06), RED, "red mark top left"),
            ((0.94, 0.06), GREEN, "green mark top right"),
            ((0.50, 0.50), BACKGROUND, "middle"),
            ((0.02, 0.50), BACKGROUND, "left edge"),
            ((0.98, 0.50), BACKGROUND, "right edge"),
            ((0.50, 0.02), BACKGROUND, "top edge"),
            ((0.50, 0.98), BACKGROUND, "bottom edge"),
            ((0.03, 0.97), BACKGROUND, "bottom-left corner"),
            ((0.97, 0.97), BACKGROUND, "bottom-right corner"),
        ]:
            seen = at(fx, fy)
            if not near(seen, colour):
                wrong.append("%s is %s" % (what, seen))
        info = "; ".join(wrong[:3]) + ("" if not wrong else " -- " + coverage(width, height, pixel))
        note(name, not wrong, info)

    print()
    if fails:
        print("FAIL: %d case(s): %s" % (len(fails), ", ".join(fails)))
        return 1
    print("pdf render: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
