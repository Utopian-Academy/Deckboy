#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
# This file is part of Deckboy, a cue deck for live events.
# See LICENSE for details.
"""Hold every CPU-built transition to the three rules it promises.

A transition is the one piece of the show nobody gets to look at twice. It runs
for half a second in front of an audience, and the two ways it fails are both
invisible in a screenshot of the running app, because the capture lands on
whatever frame the seek happened to reach:

  1. IT DOES NOT START CLEAN. The first frame of a transition has to be the
     last frame of the cue. Anything that moves at progress 0 is a twitch the
     instant the take lands.
  2. IT DOES NOT FINISH. A shard, a wisp or one stubborn pixel of the outgoing
     cue still on screen at progress 1 is a transition that has to be cut out
     of the show.

Both were real. Written against three new transitions, this found the ghastly
one inverted -- the picture gone at progress 0 and still there at progress 1 --
the shatter shards already turning before the take had landed, and, in the
Portal that had already shipped, corners still holding the old picture at a
third opacity on the very last frame, against the promise in its own comment.

The third rule is the one that catches a control that does nothing:

  3. IT HAS TO CHANGE THE PICTURE. Something visibly different at a quarter,
     a half and three quarters, and less of the old cue at each step.

And, because two outputs can show one deck, a fourth:

  4. THE SAME INPUTS GIVE THE SAME PICTURE, or the two screens disagree.

    python3 tools/check_transitions.py [--exe PATH]
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Only the styles built on the CPU. Cut, the dissolves, the pushes and the
# wipes are drawn by the renderer with SDL calls and have no frame to dump;
# --transition-dump says so rather than pretending.
STYLES = ["portal", "shatter", "clouds", "ghastly"]

# Portal's corners are the reason this number is not 0: they are the last thing
# it opens, and they have to clear the alpha ramp as well as the geometry.
# Anything above a couple of levels is visible on a dark incoming picture.
MAX_ALPHA_AT_END = 0


def default_exe():
    for rel in (
        os.path.join("build", "windows", "Release", "Deckboy.exe"),
        os.path.join("build", "Deckboy"),
        os.path.join("build", "deckboy"),
    ):
        path = os.path.join(ROOT, rel)
        if os.path.isfile(path):
            return path
    return None


def write_ppm(path, width, height, pixel):
    body = bytearray()
    for y in range(height):
        for x in range(width):
            body += bytes(pixel(x, y))
    with open(path, "wb") as handle:
        handle.write(b"P6\n%d %d\n255\n" % (width, height))
        handle.write(bytes(body))


def dump(exe, style, outgoing, incoming, dst, progress):
    """Run one dump and return its reported numbers as a dict."""
    result = subprocess.run(
        [exe, "--transition-dump", style, outgoing, incoming, dst,
         str(progress), "1.0"],
        capture_output=True, text=True)
    if result.returncode != 0:
        return None, (result.stdout + result.stderr).strip()
    numbers = {}
    for key, value in re.findall(r"(\w+)=([-\d.e+]+)", result.stdout):
        try:
            numbers[key] = float(value)
        except ValueError:
            pass
    return numbers, result.stdout.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=default_exe())
    args = ap.parse_args()
    if not args.exe or not os.path.isfile(args.exe):
        sys.exit("check_transitions: no Deckboy binary; pass --exe")

    work = tempfile.mkdtemp(prefix="deckboy_transitions_")
    W, H = 320, 180
    outgoing = os.path.join(work, "outgoing.ppm")
    incoming = os.path.join(work, "incoming.ppm")
    # THE OUTGOING PICTURE HAS STRUCTURE EVERYWHERE, so a transition cannot
    # accidentally pass rule 3 by moving a flat colour around; the incoming one
    # is flat and unlike it, so "how far from each" is unambiguous.
    write_ppm(outgoing, W, H,
              lambda x, y: (((x // 20) + (y // 20)) % 2 and 230 or 30,
                            40 + (y * 180) // H,
                            ((x // 20) % 3) * 90 + 20))
    write_ppm(incoming, W, H, lambda x, y: (0, 255, 0))

    failures = []
    for style in STYLES:
        print("%s:" % style)
        seen = {}
        for progress in (0.0, 0.25, 0.5, 0.75, 1.0):
            dst = os.path.join(work, "%s_%s.ppm" % (style, progress))
            numbers, text = dump(args.exe, style, outgoing, incoming, dst, progress)
            if numbers is None:
                failures.append("%s: the dump failed at p=%s: %s"
                                % (style, progress, text))
                break
            seen[progress] = (numbers, dst)
            print("   p=%-5s meanAlpha=%-9.5f maxAlpha=%-4d fromOutgoing=%-9.4f "
                  "fromIncoming=%.4f"
                  % (progress, numbers.get("meanAlpha", -1),
                     int(numbers.get("maxAlpha", -1)),
                     numbers.get("fromOutgoing", -1),
                     numbers.get("fromIncoming", -1)))
        if len(seen) < 5:
            continue

        # 1. Starts clean: at progress 0 the composite IS the outgoing picture.
        start = seen[0.0][0]
        if start.get("fromOutgoing", 1.0) != 0.0:
            failures.append(
                "%s: at progress 0 the picture has already changed "
                "(fromOutgoing=%.4f, should be 0). The first frame of a "
                "transition must be the last frame of the cue."
                % (style, start.get("fromOutgoing", -1)))

        # 2. Finishes: at progress 1 nothing of the outgoing cue is left.
        end = seen[1.0][0]
        if end.get("fromIncoming", 1.0) != 0.0 or \
                end.get("maxAlpha", 255) > MAX_ALPHA_AT_END:
            failures.append(
                "%s: at progress 1 the outgoing cue is still there "
                "(maxAlpha=%d, fromIncoming=%.4f). A transition that ends on an "
                "island of the old picture has to be cut out of a show."
                % (style, int(end.get("maxAlpha", -1)),
                   end.get("fromIncoming", -1)))

        # 3. It does something, and does more of it as it goes.
        alphas = [seen[p][0].get("meanAlpha", -1) for p in (0.0, 0.25, 0.5, 0.75, 1.0)]
        for a, b in zip(alphas, alphas[1:]):
            if b > a + 1e-9:
                failures.append(
                    "%s: more of the outgoing cue is showing later than earlier "
                    "(%s). A transition should only ever reveal."
                    % (style, " -> ".join("%.4f" % v for v in alphas)))
                break
        if alphas[2] > 0.995:
            failures.append(
                "%s: halfway through, %.1f%% of the outgoing cue is still fully "
                "there -- the style is doing nothing."
                % (style, alphas[2] * 100.0))

        # 4. Deterministic: two outputs on one deck must draw the same frame.
        again = os.path.join(work, "%s_again.ppm" % style)
        numbers, text = dump(args.exe, style, outgoing, incoming, again, 0.5)
        if numbers is None:
            failures.append("%s: the repeat dump failed: %s" % (style, text))
        else:
            with open(seen[0.5][1], "rb") as a, open(again, "rb") as b:
                if a.read() != b.read():
                    failures.append(
                        "%s: the same inputs produced a different picture, so two "
                        "outputs showing one deck would disagree." % style)

    print()
    if failures:
        print("check_transitions: %d problem(s)" % len(failures))
        for line in failures:
            print("  - " + line)
        return 1
    print("check_transitions: ok -- %d styles start clean, finish clean, "
          "reveal steadily and repeat exactly." % len(STYLES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
