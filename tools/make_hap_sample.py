#!/usr/bin/env python3
"""Write a HAP-encoded QuickTime file, for testing Deckboy's HAP playback.

Deckboy needs HAP media to develop against, and ffmpeg only encodes HAP when it
is built with --enable-libsnappy, which the common Windows/macOS builds are not.
Rather than make everyone rebuild ffmpeg, this produces a small valid HAP file
directly: DXT1 block compression, Snappy second stage, minimal MOV container.

It is a TEST GENERATOR, not a production encoder -- the DXT1 compressor picks
block endpoints by min/max luminance, which is fast and correct but not
rate-distortion optimal. Do not ship its output as deliverable media.

Usage:  python tools/make_hap_sample.py out.mov [frames] [width] [height]
"""

import struct
import sys

try:
    import snappy
except ImportError:
    sys.exit("needs python-snappy:  pip install python-snappy")


# --- DXT1 -------------------------------------------------------------------
# One 4x4 block -> 8 bytes: two RGB565 endpoints then 16 2-bit indices. Endpoint
# selection is by luminance extremes, which is the standard cheap heuristic.

def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def unpack565(c):
    r = ((c >> 11) & 0x1F) * 255 // 31
    g = ((c >> 5) & 0x3F) * 255 // 63
    b = (c & 0x1F) * 255 // 31
    return r, g, b


def compress_block(pixels):
    """pixels: 16 (r,g,b) tuples in row-major 4x4 order."""
    lum = [0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2] for p in pixels]
    lo_i = lum.index(min(lum))
    hi_i = lum.index(max(lum))
    c0 = rgb565(*pixels[hi_i])
    c1 = rgb565(*pixels[lo_i])
    # DXT1 uses c0 > c1 to signal the 4-colour (opaque) mode.
    if c0 < c1:
        c0, c1 = c1, c0
    elif c0 == c1:
        pass  # degenerate block: both endpoints equal, all indices 0 is correct

    e0 = unpack565(c0)
    e1 = unpack565(c1)
    palette = [
        e0,
        e1,
        tuple((2 * e0[i] + e1[i]) // 3 for i in range(3)),
        tuple((e0[i] + 2 * e1[i]) // 3 for i in range(3)),
    ]

    indices = 0
    for i, px in enumerate(pixels):
        best, best_d = 0, None
        for j, pal in enumerate(palette):
            d = sum((px[k] - pal[k]) ** 2 for k in range(3))
            if best_d is None or d < best_d:
                best, best_d = j, d
        indices |= best << (2 * i)
    return struct.pack("<HHI", c0, c1, indices)


def compress_dxt1(rgb, width, height):
    out = bytearray()
    for by in range(0, height, 4):
        for bx in range(0, width, 4):
            block = []
            for y in range(4):
                for x in range(4):
                    px = min(bx + x, width - 1)
                    py = min(by + y, height - 1)
                    o = (py * width + px) * 3
                    block.append((rgb[o], rgb[o + 1], rgb[o + 2]))
            out += compress_block(block)
    return bytes(out)


# --- HAP frame --------------------------------------------------------------
# Section header: 3-byte little-endian length + 1 type byte. The type byte is
# (compressor << 4) | texture format: 0xB_ = Snappy, 0xA_ = none; _B = RGB_DXT1.

def hap_frame(dxt, compressed=True):
    if compressed:
        body = snappy.compress(dxt)
        type_byte = 0xBB          # Snappy | RGB_DXT1
    else:
        body = dxt
        type_byte = 0xAB          # none | RGB_DXT1
    n = len(body)
    if n < (1 << 24):
        header = bytes([n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF, type_byte])
    else:
        header = bytes([0, 0, 0, type_byte]) + struct.pack("<I", n)
    return header + body


# --- Minimal MOV ------------------------------------------------------------

def atom(kind, payload):
    return struct.pack(">I", len(payload) + 8) + kind + payload


def build_mov(frames, width, height, fps):
    mdat = atom(b"mdat", b"".join(frames))
    sizes = [len(f) for f in frames]
    n = len(frames)
    timescale = 1000
    dur_per = timescale // fps
    duration = dur_per * n

    # Sample description: 'Hap1' is the FourCC for HAP (DXT1).
    vis = (b"\x00" * 6 + struct.pack(">H", 1) +          # reserved, data-ref index
           # EXACTLY 16 bytes here (pre_defined, reserved, pre_defined[3])
           # before width/height. Being 4 over is what made ffmpeg report
           # "Invalid video size 0x0" while still recognising the codec.
           struct.pack(">HH", 0, 0) +
           struct.pack(">III", 0, 0, 0) +
           struct.pack(">HH", width, height) +
           struct.pack(">II", 0x00480000, 0x00480000) +  # 72 dpi
           struct.pack(">I", 0) + struct.pack(">H", 1) + # data size, frame count
           bytes([4]) + b"Hap " + b"\x00" * 27 +         # 32-byte pascal name
           struct.pack(">H", 24) + struct.pack(">h", -1))
    stsd = atom(b"stsd", struct.pack(">II", 0, 1) + atom(b"Hap1", vis))
    stts = atom(b"stts", struct.pack(">III", 0, 1, n) + struct.pack(">I", dur_per))
    stsc = atom(b"stsc", struct.pack(">II", 0, 1) + struct.pack(">III", 1, 1, 1))
    stsz = atom(b"stsz", struct.pack(">III", 0, 0, n) +
                b"".join(struct.pack(">I", s) for s in sizes))

    offsets = []
    running = 0
    for s in sizes:
        offsets.append(running)
        running += s
    # Patched below once the header length is known.
    stco_payload = struct.pack(">II", 0, n) + b"".join(
        struct.pack(">I", o) for o in offsets)
    stco = atom(b"stco", stco_payload)

    stbl = atom(b"stbl", stsd + stts + stsc + stsz + stco)
    dinf = atom(b"dinf", atom(b"dref", struct.pack(">II", 0, 1) +
                              atom(b"alis", struct.pack(">I", 1))))
    vmhd = atom(b"vmhd", struct.pack(">I", 1) + struct.pack(">HHHH", 0, 0, 0, 0))
    minf = atom(b"minf", vmhd + dinf + stbl)
    hdlr = atom(b"hdlr", struct.pack(">I", 0) + b"mhlr" + b"vide" +
                b"\x00" * 12 + bytes([0]))
    # mdhd v0 is 24 bytes; duration is a uint32, not a uint16.
    mdhd = atom(b"mdhd", struct.pack(">IIIII", 0, 0, 0, timescale, duration) +
                struct.pack(">HH", 0, 0))
    mdia = atom(b"mdia", mdhd + hdlr + minf)

    unity = struct.pack(">9i", 0x10000, 0, 0, 0, 0x10000, 0, 0, 0, 0x40000000)
    tkhd = atom(b"tkhd", struct.pack(">I", 0x0F) +
                struct.pack(">IIIII", 0, 0, 1, 0, duration) +
                b"\x00" * 8 + struct.pack(">HHHH", 0, 0, 0, 0) + unity +
                struct.pack(">II", width << 16, height << 16))
    trak = atom(b"trak", tkhd + mdia)
    mvhd = atom(b"mvhd", struct.pack(">I", 0) +
                struct.pack(">IIII", 0, 0, timescale, duration) +
                struct.pack(">IHH", 0x10000, 0x100, 0) + b"\x00" * 8 + unity +
                b"\x00" * 24 + struct.pack(">I", 2))
    moov = atom(b"moov", mvhd + trak)

    ftyp = atom(b"ftyp", b"qt  " + struct.pack(">I", 0x20050300) + b"qt  ")

    # mdat payload begins after ftyp + moov + the mdat header.
    base = len(ftyp) + len(moov) + 8
    fixed = struct.pack(">II", 0, n) + b"".join(
        struct.pack(">I", base + o) for o in offsets)
    moov = moov.replace(stco_payload, fixed)
    assert len(moov) == len(atom(b"moov", mvhd + trak)), "stco patch changed size"
    return ftyp + moov + mdat


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "hap_sample.mov"
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 30
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 256
    h = int(sys.argv[4]) if len(sys.argv) > 4 else 256
    fps = 30

    frames = []
    for f in range(n):
        rgb = bytearray(w * h * 3)
        for y in range(h):
            for x in range(w):
                o = (y * w + x) * 3
                # A moving diagonal band over a colour ramp: obvious motion, and
                # obvious if blocks land in the wrong order.
                band = 255 if ((x + y + f * 8) % 64) < 16 else 0
                rgb[o] = min(255, x * 255 // max(1, w - 1))
                rgb[o + 1] = min(255, y * 255 // max(1, h - 1))
                rgb[o + 2] = band
        frames.append(hap_frame(compress_dxt1(rgb, w, h)))

    data = build_mov(frames, w, h, fps)
    with open(out, "wb") as fh:
        fh.write(data)
    print("wrote %s: %d frames, %dx%d, %d bytes" % (out, n, w, h, len(data)))


if __name__ == "__main__":
    main()
