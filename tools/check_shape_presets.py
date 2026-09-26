#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
# This file is part of Deckboy, a cue deck for live events.
# See LICENSE for details.
"""Render every shape preset and assert it is still a shape.

WHAT THIS CATCHES THAT --code-check DOES NOT. --code-check asserts the
language: that a named value reads the one above it, that mix blends, that a
bad call is refused. It compiles every preset, so a preset that stops
compiling fails there. What it cannot see is a preset that compiles perfectly
and draws nothing -- or draws everything.

Those are the two ways a shape dies:

  CLEAR EVERYWHERE   the alpha is zero across the frame. Nothing is on screen
                     and no error is raised, because an empty shape is a
                     legal shape. This is how the gem preset shipped its
                     first draft: the arithmetic was fine and the picture
                     was blank.

  OPAQUE EVERYWHERE  the alpha is 255 across the frame, which means the
                     preset has quietly become a BACKGROUND. It looks correct
                     in a dump -- the dump composites over black -- and it is
                     wrong the moment anybody puts it over a camera, which is
                     the entire point of these five.

So the assertion is on COVERAGE, not on pixels: a shape must have some of the
frame and not all of it. Exact pixels are deliberately not pinned; these are
meant to be edited.

Usage:
    python tools/check_shape_presets.py [--exe PATH] [--sheet OUT.png]
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EDITOR = os.path.join(ROOT, "native", "app", "app_code_editor.ipp")

# A shape has to cover at least this much of the frame to count as drawn, and
# leave at least this much uncovered to count as a shape rather than a wash.
MIN_DRAWN = 1      # per cent
MIN_CLEAR = 5      # per cent

BACKSLASH = chr(92)
QUOTE = chr(34)


def default_exe():
    names = ["Deckboy.exe", "Deckboy", "deckboy"]
    roots = [
        os.path.join(ROOT, "build", "windows", "Release"),
        os.path.join(ROOT, "build", "Release"),
        os.path.join(ROOT, "build"),
        os.path.join(ROOT, "build", "linux"),
    ]
    for r in roots:
        for n in names:
            p = os.path.join(r, n)
            if os.path.isfile(p):
                return p
    return None


def unescape(literal):
    """Turn one C++ string-literal body into the text it denotes."""
    out = []
    i = 0
    while i < len(literal):
        c = literal[i]
        if c == BACKSLASH and i + 1 < len(literal):
            nxt = literal[i + 1]
            out.append({"n": chr(10), "t": chr(9), BACKSLASH: BACKSLASH,
                        QUOTE: QUOTE}.get(nxt, nxt))
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def shape_presets():
    """Every preset below the SHAPES marker, as (name, expression)."""
    text = open(EDITOR, encoding="utf-8", newline="").read()
    marker = text.find("SHAPES")
    if marker < 0:
        sys.exit("check_shape_presets: no SHAPES section in " + EDITOR)
    end = text.find(chr(10) + "  };", marker)
    body = text[marker:end]

    literal = r'"(?:[^"\\]|\\.)*"'
    entry = re.compile(r"\{\s*(" + literal + r")\s*,\s*((?:\s*" + literal + r")+)\s*\}")
    found = []
    for m in entry.finditer(body):
        name = unescape(m.group(1)[1:-1])
        parts = re.findall(literal, m.group(2))
        expression = "".join(unescape(p[1:-1]) for p in parts)
        found.append((name, expression))
    return found


def coverage(exe, expression, tmpdir, name, seconds, size):
    src = os.path.join(tmpdir, "expr.txt")
    with open(src, "w", encoding="utf-8", newline="") as f:
        f.write(expression)
    out = os.path.join(tmpdir, name.replace(" ", "_") + ".ppm")
    run = subprocess.run(
        [exe, "--code-dump", "@" + src, out, size, str(seconds)],
        capture_output=True, text=True)
    if run.returncode != 0:
        return None, out, (run.stdout + run.stderr).strip()
    m = re.search(r"opaque (\d+)% clear (\d+)%", run.stdout)
    if not m:
        return None, out, "no coverage in: " + run.stdout.strip()
    return (int(m.group(1)), int(m.group(2))), out, run.stdout.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=default_exe())
    ap.add_argument("--size", default="480x480")
    ap.add_argument("--seconds", type=float, default=1.3,
                    help="a moment after the cue is taken, so animated "
                         "presets are mid-move rather than at their seed")
    ap.add_argument("--sheet", help="write a contact sheet here (needs ffmpeg)")
    args = ap.parse_args()

    if not args.exe or not os.path.isfile(args.exe):
        sys.exit("check_shape_presets: no Deckboy binary; pass --exe")

    presets = shape_presets()
    if not presets:
        sys.exit("check_shape_presets: found no shape presets to check")

    failures = []
    rendered = []
    with tempfile.TemporaryDirectory() as tmpdir:
        for name, expression in presets:
            # A shape ends in four expressions. Checked in the TEXT as well as
            # in the picture, because the two fail differently: a preset that
            # loses its fourth expression still draws, it just draws opaque,
            # and saying "this one has three" is a better message than "this
            # one covers 100%".
            last = expression.strip().split(";")[-1]
            depth = 0
            commas = 0
            for ch in last:
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                elif ch == "," and depth == 0:
                    commas += 1
            if commas != 3:
                failures.append((name, "ends in %d expressions, not 4 "
                                       "(no alpha means no shape)" % (commas + 1)))
                continue

            cov, ppm, message = coverage(args.exe, expression, tmpdir, name,
                                         args.seconds, args.size)
            if cov is None:
                failures.append((name, message))
                continue
            opaque, clear = cov
            drawn = 100 - clear
            print("  %-14s opaque %3d%%  clear %3d%%" % (name, opaque, clear))
            if drawn < MIN_DRAWN:
                failures.append((name, "draws nothing: %d%% of the frame has "
                                       "any alpha at all" % drawn))
            elif clear < MIN_CLEAR:
                failures.append((name, "covers the whole frame (%d%% clear): "
                                       "this is a background, not a shape" % clear))
            if args.sheet:
                keep = os.path.join(tempfile.gettempdir(),
                                    "deckboy_shape_" + name.replace(" ", "_") + ".ppm")
                with open(ppm, "rb") as a, open(keep, "wb") as b:
                    b.write(a.read())
                rendered.append(keep)

        if args.sheet and rendered:
            listing = os.path.join(tempfile.gettempdir(), "deckboy_shapes.txt")
            with open(listing, "w", encoding="utf-8") as f:
                for p in rendered:
                    f.write("file '" + p.replace(chr(92), "/") + "'" + chr(10))
            subprocess.run(
                ["ffmpeg", "-y", "-v", "error", "-f", "concat", "-safe", "0",
                 "-i", listing, "-vf", "tile=%dx1" % len(rendered), args.sheet],
                check=False)
            print("sheet: " + args.sheet)

    if failures:
        print()
        for name, why in failures:
            print("FAIL %s: %s" % (name, why))
        print("check_shape_presets: %d of %d failed" % (len(failures), len(presets)))
        return 1
    print("check_shape_presets: ok, %d shapes" % len(presets))
    return 0


if __name__ == "__main__":
    sys.exit(main())
