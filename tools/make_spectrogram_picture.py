#!/usr/bin/env python3
"""Write a WAV whose SPECTROGRAM draws a picture.

Deckboy's Sound Scopes (SOUND, then SPECTROGRAM) draw frequency up the side
on a log scale, time across, and level as colour. This script turns that round:
it takes a picture and writes the sound that draws it. Each image column
becomes a short stretch of time. Each image row becomes one sine partial at a
log-spaced frequency, and the pixel's brightness sets that partial's level.

The picture is your own: give a PNG, or let it write a word.

    py -3 tools/make_spectrogram_picture.py --text DECKBOY out.wav
    py -3 tools/make_spectrogram_picture.py logo.png out.wav --seconds 6

Row 0 of the picture (the top) is the highest frequency; column 0 (the left)
is the start. The last 8 seconds are on screen, so the default length is 7 s.
The sound is quiet noise-free sines, so it is a tone chord, not music.

Needs Pillow and numpy. Writes 16-bit, 48 kHz, stereo (both sides the same).
"""
import argparse
import sys
import wave
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

RATE = 48000


def text_image(text, height):
    """White letters on black, as wide as they need to be."""
    font = None
    for name in ("arialbd.ttf", "DejaVuSans-Bold.ttf", "arial.ttf"):
        try:
            font = ImageFont.truetype(name, height)
            break
        except OSError:
            pass
    if font is None:
        font = ImageFont.load_default(size=height)
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    box = probe.textbbox((0, 0), text, font=font)
    img = Image.new("L", (box[2] - box[0] + 8, box[3] - box[1] + 8), 0)
    ImageDraw.Draw(img).text((4 - box[0], 4 - box[1]), text, fill=255, font=font)
    return img


def levels_from_image(img, rows, columns, floor_db, top_db):
    """Brightness 0..1 per (row, column) -> level in dB, or None for black."""
    img = img.convert("L").resize((columns, rows), Image.LANCZOS)
    b = np.asarray(img, dtype=np.float64) / 255.0
    db = floor_db + (top_db - floor_db) * b
    db[b < 0.04] = -300.0
    return db


def synthesise(db, low_hz, high_hz, seconds, seed=1):
    rows, columns = db.shape
    freqs = high_hz * (low_hz / high_hz) ** (np.arange(rows) / (rows - 1))   # row 0 = highest
    amps = np.where(db > -250.0, 10.0 ** (db / 20.0), 0.0).T                  # (columns, rows)
    phases = np.random.default_rng(seed).uniform(0, 2 * np.pi, rows)         # random: no spike in the waveform
    n = int(round(seconds * RATE))
    out = np.zeros(n)
    chunk = RATE // 2
    for start in range(0, n, chunk):
        t = (np.arange(start, min(n, start + chunk)) / RATE)
        pos = np.clip(t / seconds * columns - 0.5, 0, columns - 1)            # smooth between columns
        i0 = np.minimum(pos.astype(int), columns - 2)
        f = (pos - i0)[:, None]
        a = amps[i0] * (1 - f) + amps[i0 + 1] * f
        out[start:start + len(t)] = (a * np.sin(2 * np.pi * np.outer(t, freqs) + phases)).sum(axis=1)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("picture", nargs="?", help="a PNG (omit with --text)")
    ap.add_argument("wav", help="the WAV to write")
    ap.add_argument("--text", help="draw this word instead of reading a picture")
    ap.add_argument("--seconds", type=float, default=7.0, help="length; the panel shows the last 8 s")
    ap.add_argument("--low", type=float, default=300.0, help="lowest frequency, Hz (the picture's bottom edge)")
    ap.add_argument("--high", type=float, default=16000.0, help="highest frequency, Hz (the top edge)")
    ap.add_argument("--rows", type=int, default=140, help="partials: image rows")
    ap.add_argument("--peak", type=float, default=0.9, help="loudest sample, 0-1 of full scale")
    ap.add_argument("--invert", action="store_true", help="dark picture on a light ground becomes light on dark")
    ap.add_argument("--preview", help="also write the picture as it was read (PNG)")
    args = ap.parse_args()

    if args.text:
        img = text_image(args.text, 160)
    elif args.picture:
        img = Image.open(args.picture)
        if img.mode in ("RGBA", "LA", "P"):
            rgba = img.convert("RGBA")
            ground = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
            img = Image.alpha_composite(ground, rgba)
        img = img.convert("L")
    else:
        ap.error("give a picture or --text")
    if args.invert:
        img = Image.eval(img, lambda v: 255 - v)

    columns = max(32, int(args.seconds * 48))        # a column every 21 ms, like the panel's
    db = levels_from_image(img, args.rows, columns, -62.0, -4.0)
    if args.preview:
        Image.fromarray(np.clip((db + 62.0) / 58.0 * 255.0, 0, 255).astype(np.uint8)).save(args.preview)
    mono = synthesise(db, args.low, args.high, args.seconds)
    mono *= args.peak / max(1e-9, np.abs(mono).max())
    # A short fade in and out, so the WAV starts and ends without a click.
    fade = int(0.02 * RATE)
    mono[:fade] *= np.linspace(0, 1, fade)
    mono[-fade:] *= np.linspace(1, 0, fade)
    pcm = np.repeat(np.round(mono * 32767.0).astype("<i2")[:, None], 2, axis=1)
    with wave.open(args.wav, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())
    print("%s: %.1f s, %d partials %g-%g Hz, peak %.2f of full scale, %d KB" % (
        args.wav, args.seconds, args.rows, args.low, args.high, np.abs(mono).max(),
        Path(args.wav).stat().st_size // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
