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
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# Only the styles built on the CPU. Cut, the dissolves, the pushes and the
# wipes are drawn by the renderer with SDL calls and have no frame to dump;
# --transition-dump says so rather than pretending.
STYLES = ["portal", "shatter", "clouds", "ghastly"]
PLAYOUT_STYLES = ["cut", "crossfade", "dip", "dipwhite", "pushleft", "pushright",
                 "pushup", "pushdown", "wipeleft", "wiperight", "wipeup", "wipedown",
                 "iris"] + STYLES

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


def check_playout(args):
    """Measure placement in the complete output, which frame dumps cannot see."""
    from deckboy_harness import Deckboy
    args.keep = False
    args.ffmpeg = ""
    failures = []
    with tempfile.TemporaryDirectory(prefix="deckboy-transition-media-") as media:
        white, black, portrait = [os.path.join(media, name + ".ppm")
                                  for name in ("white", "black", "portrait")]
        write_ppm(white, 300, 100, lambda x, y: (255, 255, 255))
        write_ppm(black, 300, 100, lambda x, y: (0, 0, 0))
        write_ppm(portrait, 100, 300, lambda x, y: (0, 255, 0))
        with Deckboy(args, "deckboy-transition-playout-",
                     extra_args=["--import", white, "--import", black, "--import", portrait]) as db:
            db.send("OUTPUT ON")
            db.send("RECFORMAT 640x360 25")
            for style in PLAYOUT_STYLES:
                db.send("TRANSITION 0")
                db.send("SELECT 1")
                db.send("SCALEMODE FIT")
                db.send("TAKE")
                time.sleep(0.5)
                db.send("TRANSITION 1.2")
                db.send("TRANSITIONSTYLE " + style)
                def take_black():
                    time.sleep(1.0)
                    db.send("SELECT 2")
                    db.send("TAKE")
                take = threading.Thread(target=take_black)
                take.start()
                path = db.record(seconds=3.0)
                take.join()
                if not path:
                    failures.append(style + ": no programme recording")
                    continue
                pixels = subprocess.run([
                    "ffmpeg", "-v", "error", "-i", path, "-vf", "scale=96:54,format=gray",
                    "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
                frames = [pixels[i:i + 96 * 54] for i in range(0, len(pixels), 96 * 54)]
                frames = [f for f in frames if len(f) == 96 * 54]
                # A 3:1 source fitted to 16:9 leaves bars above and below.
                # They must remain black for EVERY frame of the transition.
                bars = [max(f[:96*8] + f[96*46:]) for f in frames]
                centres = [sum(f[96*20:96*34]) / (96 * 14) for f in frames]
                litRows = [sum(max(f[y*96:(y+1)*96]) > 15 for y in range(54)) for f in frames]
                litPixels = [sum(v > 40 for v in f) for f in frames]
                # Pushes move the picture into a bar but preserve its height.
                # Dip to white deliberately flashes the complete output.
                if style == "dipwhite":
                    geometry_ok = True
                elif style.startswith("push"):
                    geometry_ok = max(litRows, default=54) <= 34
                else:
                    geometry_ok = max(bars, default=255) < 8
                # A wipe/iris must expose part of the incoming picture before
                # it finishes. Correct endpoints alone also pass a silent cut.
                reveal_ok = True
                if style.startswith("wipe") or style == "iris":
                    full = max(litPixels[:15], default=0)
                    reveal_ok = full > 0 and any(full * 0.1 < n < full * 0.9 for n in litPixels)
                ok = (len(frames) > 40 and geometry_ok and
                      reveal_ok and
                      max(centres[:15], default=0) > 230 and
                      max(centres[-10:], default=255) < 8)
                print("playout %s: frames=%d bar-maximum=%d first=%.1f last=%.1f %s" %
                      (style, len(frames), max(bars, default=255),
                       max(centres[:15], default=0), max(centres[-10:], default=255),
                       "ok" if ok else "FAIL"))
                if not ok:
                    failures.append(style + ": fitted placement or intermediate reveal failed")
            # The outgoing frame must hold its processed look. Inverting white
            # makes this a black-to-black take: raw-frame leakage turns it white.
            for style in ["crossfade"] + STYLES:
                db.send("TRANSITION 0")
                db.send("SELECT 1")
                db.send("FX CLEAR")
                db.send("FX ADD invert")
                db.send("FX AMOUNT 1 1")
                db.send("TAKE")
                time.sleep(0.5)
                db.send("TRANSITION 1.2")
                db.send("TRANSITIONSTYLE " + style)
                take = threading.Thread(target=take_black)
                take.start()
                path = db.record(seconds=3.0)
                take.join()
                if not path:
                    failures.append(style + ": no look recording")
                    continue
                pixels = subprocess.run([
                    "ffmpeg", "-v", "error", "-i", path, "-vf", "scale=96:54,format=gray",
                    "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
                frames = [pixels[i:i+5184] for i in range(0, len(pixels), 5184)]
                frames = [f for f in frames if len(f) == 5184]
                peak = max((sum(f) / len(f) for f in frames), default=255)
                # Portal adds a coloured rim; it must not restore raw white.
                ok = len(frames) > 40 and peak < 40
                print("playout look %s: frames=%d peak-mean=%.1f %s" %
                      (style, len(frames), peak, "ok" if ok else "FAIL"))
                if not ok:
                    failures.append(style + ": outgoing processed look was lost")
            # The layer fader must apply to both held and incoming pictures.
            # This also repeats a white cue after its inversion was removed,
            # exposing a stale processed-look cache from an earlier take.
            for style, opacity in [("crossfade", 50), ("portal", 50), ("dipwhite", 0)]:
                db.send("TRANSITION 0")
                db.send("SELECT 1")
                db.send("FX CLEAR")
                db.send("DECKOPACITY " + str(opacity))
                db.send("TAKE")
                time.sleep(0.5)
                db.send("TRANSITION 1.2")
                db.send("TRANSITIONSTYLE " + style)
                take = threading.Thread(target=take_black)
                take.start()
                path = db.record(seconds=3.0)
                take.join()
                pixels = subprocess.run([
                    "ffmpeg", "-v", "error", "-i", path, "-vf", "scale=96:54,format=gray",
                    "-f", "rawvideo", "-"], capture_output=True, check=True).stdout if path else b""
                frames = [pixels[i:i+5184] for i in range(0, len(pixels), 5184)]
                frames = [f for f in frames if len(f) == 5184]
                peak = max((max(f) for f in frames), default=255)
                centres = [sum(f[96*20:96*34]) / (96*14) for f in frames]
                expected = 255 * opacity / 100
                initial = max(centres[:15], default=255)
                # Lossy YUV recording can ring a few levels above a sharp mask
                # edge. Eight levels permits that; the doubled hold reached 195.
                ok = len(frames) > 40 and peak <= expected + 8 and abs(initial-expected) < 5
                print("playout opacity %s: opacity=%d peak=%d initial=%.1f %s" %
                      (style, opacity, peak, initial, "ok" if ok else "FAIL"))
                if not ok:
                    failures.append(style + ": layer opacity changed during transition")
            db.send("DECKOPACITY 100")
            # Change geometry AFTER taking the outgoing cue, then take again.
            # This catches a snapshot that reads legacy engine geometry rather
            # than the live cue used by the actual compositor.
            for style in ["crossfade"] + STYLES:
                db.send("TRANSITION 0")
                db.send("SELECT 1")
                db.send("FX CLEAR")
                db.send("SCALEX 1")
                db.send("SCALEY 1")
                db.send("TAKE")
                db.send("SCALEX 0.5")
                db.send("SCALEY 0.5")
                time.sleep(0.5)
                db.send("TRANSITION 1.2")
                db.send("TRANSITIONSTYLE " + style)
                take = threading.Thread(target=take_black)
                take.start()
                path = db.record(seconds=3.0)
                take.join()
                pixels = subprocess.run([
                    "ffmpeg", "-v", "error", "-i", path, "-vf", "scale=96:54,format=gray",
                    "-f", "rawvideo", "-"], capture_output=True, check=True).stdout if path else b""
                frames = [pixels[i:i+5184] for i in range(0, len(pixels), 5184)]
                frames = [f for f in frames if len(f) == 5184]
                rows = [sum(max(f[y*96:(y+1)*96]) > 15 for y in range(54)) for f in frames]
                peak = max(rows, default=54)
                initial = max(rows[:15], default=0)
                ok = len(frames) > 40 and 15 <= initial <= 18 and peak <= 18
                print("playout live geometry %s: initial-rows=%d peak-rows=%d %s" %
                      (style, initial, peak, "ok" if ok else "FAIL"))
                if not ok:
                    failures.append(style + ": live geometry edit was lost during transition")
            # A portrait cue pushed in after a wide one must enter from the
            # edge, retaining its own shape. An extra draw at rest exposes it
            # early in the wide cue's bars and can resemble a size jump.
            for style in ["pushleft", "pushright", "pushup", "pushdown"]:
                db.send("TRANSITION 0")
                db.send("SELECT 1")
                db.send("SCALE 1")
                db.send("TAKE")
                time.sleep(0.5)
                db.send("TRANSITION 1.2")
                db.send("TRANSITIONSTYLE " + style)
                def take_portrait():
                    time.sleep(1.0)
                    reply = db.send("TAKE 3")
                    if reply.startswith("ERR"):
                        failures.append(style + ": " + reply)
                take = threading.Thread(target=take_portrait)
                take.start()
                path = db.record(seconds=3.0)
                take.join()
                pixels = subprocess.run([
                    "ffmpeg", "-v", "error", "-i", path, "-vf", "scale=96:54,format=rgb24",
                    "-f", "rawvideo", "-"], capture_output=True, check=True).stdout if path else b""
                frames = [pixels[i:i+15552] for i in range(0, len(pixels), 15552)
                          if len(pixels[i:i+15552]) == 15552]
                violations = coexist = 0
                for f in frames:
                    white_points, green_points = [], []
                    for pos in range(0, len(f), 3):
                        r, g, b = f[pos:pos+3]
                        xy = (pos // 3 % 96, pos // 3 // 96)
                        if min(r, g, b) > 200: white_points.append(xy)
                        if g > 100 and r < 50 and b < 50: green_points.append(xy)
                    if not white_points or not green_points: continue
                    coexist += 1
                    if style == "pushleft":
                        bad = min(p[0] for p in green_points) < max(p[0] for p in white_points) + 37
                    elif style == "pushright":
                        bad = max(p[0] for p in green_points) > min(p[0] for p in white_points) - 37
                    elif style == "pushup":
                        bad = min(p[1] for p in green_points) < max(p[1] for p in white_points) + 9
                    else:
                        bad = max(p[1] for p in green_points) > min(p[1] for p in white_points) - 9
                    violations += bad
                ok = len(frames) > 40 and coexist > 2 and violations == 0
                print("playout mixed shapes %s: coexist=%d misplaced=%d %s" %
                      (style, coexist, violations, "ok" if ok else "FAIL"))
                if not ok:
                    failures.append(style + ": portrait incoming cue appeared at rest or changed shape")
    for failure in failures:
        print("FAIL: " + failure)
    return 1 if failures else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=default_exe())
    ap.add_argument("--playout", action="store_true", help="also measure geometry in programme recordings")
    ap.add_argument("--port", type=int, default=5631, help="isolated playout control port")
    args = ap.parse_args()
    if not args.exe or not os.path.isfile(args.exe):
        sys.exit("check_transitions: no Deckboy binary; pass --exe")
    # The playout harness launches from the binary's directory, so resolve
    # --exe against the invocation directory before that working-dir change.
    args.exe = os.path.abspath(args.exe)

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
    return check_playout(args) if args.playout else 0


if __name__ == "__main__":
    sys.exit(main())
