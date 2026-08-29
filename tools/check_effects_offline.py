#!/usr/bin/env python3
"""Render every effect through `--effect-dump` and check it changed the picture.

The other two sweeps drive the running app. They are worth having -- one proves
an effect reaches the recording, the other proves it reaches the preview -- but
neither is any use while WRITING an effect, because each case launches the app,
takes a cue and screenshots it, so a run costs minutes and the comparison is at
the mercy of which frame the seek landed on. Two effects were called working on
that evidence when they were doing nothing at all.

This calls the effect maths directly on one picture. A run is a couple of
seconds, it is exactly repeatable, and it can also write a contact sheet so the
question "does it change the picture" can be followed by the one that matters,
which is "does it look like anything".

    python3 tools/check_effects_offline.py
    python3 tools/check_effects_offline.py --sheet sheet.png --source my.png

Needs ffmpeg only to make the source and the sheet; the effects themselves run
in the app binary.
"""

import argparse
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deckboy_testroot  # noqa: E402

# token: the amount:paramA:paramB to exercise it at. Kept here rather than
# derived, so adding an effect and forgetting this list fails loudly instead of
# quietly shrinking the sweep.
EFFECTS = [
    ("invert",          "0.9:0.5:0.5"),
    ("posterise",       "0.9:0.5:0.5"),
    ("solarise",        "0.9:0.5:0.5"),
    ("threshold",       "0.9:0.5:0.5"),
    ("vignette",        "0.9:0.5:0.5"),
    ("grain",           "0.9:0.5:0.5"),
    ("scanlines",       "0.9:0.5:0.5"),
    ("channel_offset",  "0.9:0.5:0.5"),
    ("temporal_dither", "0.9:0.5:0.5"),
    ("pixel_sort",      "0.9:0.5:0.5"),
    ("block_glitch",    "0.9:0.5:0.5"),
    ("polar_warp",      "0.9:0.5:0.5"),
    ("luma_displace",   "0.9:0.5:0.5"),
    ("ripple",          "0.9:0.5:0.5"),
    ("kaleidoscope",    "0.9:0.5:0.5"),
    ("dye_advect",      "0.9:0.0:0.6"),
    ("reaction_bloom",  "0.85:0.5:0.6"),
    ("relativistic",    "0.85:0.55:0.8"),
    ("caustics",        "0.8:0.45:0.4:0.7"),
    ("feedback",        "1.0:0.85:0.62:0.62:0.4"),
    ("schlieren",       "0.95:0.12:0.55:0.5"),
    ("chladni",         "0.9:0.45:0.7:0.6:0.5"),
    ("wavefront",       "0.9:0.5:0.45:0.3:0.4"),
    ("crystallise",     "0.95:0.22:0.6:0.7:0.5"),
    ("scotopic",        "0.95:0.7:0.6:0.6"),
    ("grain_flow",      "0.95:0.5:0.0:0.4"),
]

# Effects whose whole subject is what happens ACROSS frames need more than one
# application before there is anything to look at: feedback's first pass only
# fills the buffer it will later echo. --effect-dump takes a pass count for
# exactly this, and running the rest more than once would only measure them
# chewing on their own output.
PASSES = {"feedback": 12, "scotopic": 6}

# Effects that look different from one frame to the next on an unchanging
# picture -- mirroring cueEffectKindAnimates in cue_effects.hpp, which the
# render paths use to decide whether a STILL cue must re-run its stack. Get
# this wrong in the header and a time-based effect freezes on a still (or every
# still pays for a full-raster re-render it does not need), and neither shows up
# in any other check. --animation renders each effect at nine frame indices and
# fails if the pixels disagree with this list.
#
# feedback and motion_puppet move by carrying STATE rather than by reading the
# frame index, so they animate in the app while rendering identically here.
# They are listed as state-driven rather than measured.
ANIMATES_BY_INDEX = {"grain", "temporal_dither", "block_glitch", "ripple",
                     "caustics"}
ANIMATES_BY_STATE = {"feedback", "motion_puppet", "scotopic"}

# Every named parameter slot, mirroring cueEffectParamLabel in cue_effects.hpp.
# Kept here rather than parsed, so the two diverging fails loudly: --params
# checks that each one actually moves the picture, which is the dead-control
# bug this codebase keeps producing, applied to effect parameters.
PARAM_SLOTS = {
    "invert":          ["pivot", "channel spread"],
    "posterise":       ["band curve", "channel skew"],
    "solarise":        ["fold point", "knee"],
    "threshold":       ["pivot", "softness"],
    "vignette":        ["size", "falloff"],
    "grain":           ["grain size", "colour"],
    "scanlines":       ["pitch", "darkness"],
    "channel_offset":  ["angle", "green split"],
    "temporal_dither": ["palette", "hold"],
    "pixel_sort":      ["threshold", "reverse"],
    "block_glitch":    ["bands", "tear width"],
    "polar_warp":      ["twist", "radial zoom"],
    "luma_displace":   ["vertical bias", "pivot"],
    "ripple":          ["frequency", "speed"],
    "kaleidoscope":    ["wedges", "rotation"],
    "dye_advect":      ["bleed", "curl detail", "swirl"],
    "reaction_bloom":  ["feed rate", "growth", "seed density", "glow"],
    "relativistic":    ["field of view", "doppler", "off-axis"],
    "caustics":        ["chop", "swell speed", "focus"],
    "feedback":        ["zoom", "spin", "drift", "colour bleed"],
    "schlieren":       ["knife angle", "sensitivity", "colour"],
    "chladni":         ["mode", "second mode", "gather", "line glow"],
    "wavefront":       ["stiffness", "steps", "damping", "relief"],
    "crystallise":     ["grain size", "facet light", "irregularity", "edges"],
    "scotopic":        ["colour lag", "rod bias", "purkinje"],
    "grain_flow":      ["stroke", "across the grain", "coherence"],
    "motion_puppet":   ["spring", "memory"],
}

# What a show saved before a parameter existed carries, and what it must still
# mean. Moving one of these breaks every show that already used the effect.
NEUTRAL = [0.5, 0.0, 0.0, 0.0]

# Where a parameter can only do anything once ANOTHER one is engaged, the check
# starts from here instead of from neutral.
#
# scotopic's purkinje shift only acts where the rods have taken over, and at
# neutral rod bias almost nothing in a normal picture is dark enough for that.
# Testing it from neutral asks whether a night-vision tint works in daylight,
# gets "no", and calls a working control dead.
PARAM_BASE = {
    "scotopic": [0.5, 0.85, 0.0, 0.0],
}

# Parameters whose whole subject is what happens BETWEEN frames, on a picture
# that is CHANGING. This harness renders one still picture repeatedly, so a
# value that controls how fast the effect catches up with a new picture has
# nothing to catch up with: every pass converges to the same place whatever the
# rate. Covered instead by the oscillator checks in --smoke and by watching a
# moving clip.
TEMPORAL_PARAMS = {
    ("scotopic", "colour lag"),
}
MOVED = [0.9, 0.8, 0.8, 0.8]

# Neither is a pixel operation, so neither can be dumped. Datamosh happens at
# the DECODER and motion puppet needs a driver clip's vectors.
NOT_PIXEL_EFFECTS = [
    ("datamosh", "happens at decode, not on the pixels"),
    ("motion_puppet", "needs a driver clip's motion vectors"),
]


def raster(path):
    """The pixel bytes of a binary PPM, without its header."""
    with open(path, "rb") as handle:
        data = handle.read()
    return data[data.index(b"255\n") + 4:]


def check_params(exe, src, base, work):
    """Every named parameter should change the picture, and the neutral
    position should leave it exactly as it was."""
    def base_for(token):
        return list(PARAM_BASE.get(token, NEUTRAL))

    def render(token, params, frame=7):
        out = os.path.join(work, "param.ppm")
        spec = "%s:0.9:%g:%g:%g:%g" % ((token,) + tuple(params))
        subprocess.run([exe, "--effect-dump", spec, src, out, str(frame),
                        str(PASSES.get(token, 1))], capture_output=True)
        return raster(out) if os.path.exists(out) else b""

    dead = 0
    # The same exemption the main sweep uses. Without it this check reported
    # motion puppet's two parameters as dead on every run -- they are not, it
    # simply has no motion vectors to work from when it is called directly --
    # and a gate that always fails is a gate nobody reads.
    undumpable = dict(NOT_PIXEL_EFFECTS)
    print("%-16s %-15s %-6s %s" % ("effect", "parameter", "verdict", "detail"))
    for token, names in PARAM_SLOTS.items():
        if token in undumpable:
            print("%-16s %-15s %-6s %s"
                  % (token, "-", "n/a", undumpable[token]))
            continue
        base = base_for(token)
        neutral = render(token, base)
        if not neutral:
            print("%-16s %-15s %-6s %s" % (token, "-", "FAIL", "no render"))
            dead += 1
            continue
        for slot, name in enumerate(names):
            if (token, name) in TEMPORAL_PARAMS:
                print("%-16s %-15s %-6s %s"
                      % (token, name, "n/a",
                         "only shows on a picture that is changing"))
                continue
            params = base_for(token)
            params[slot] = MOVED[slot]
            shot = render(token, params)
            if len(shot) != len(neutral):
                print("%-16s %-15s %-6s %s" % (token, name, "FAIL", "bad render"))
                dead += 1
                continue
            changed = sum(1 for a, b in zip(neutral[::53], shot[::53])
                          if abs(a - b) > 6)
            pct = 100.0 * changed / len(neutral[::53])
            verdict = "ok" if pct >= 0.5 else "DEAD"
            if verdict == "DEAD":
                dead += 1
            print("%-16s %-15s %-6s %.1f%% of the picture moved"
                  % (token, name, verdict, pct))
    summary = ("FAIL: %d parameter(s) do nothing" % dead if dead else
               "every named parameter changes the picture")
    print()
    print(summary)
    return 1 if dead else 0


def check_animation(exe, src, work):
    """Does each effect move over time exactly when the header says it does?"""
    frames = (0, 1, 2, 3, 5, 8, 13, 40, 97)
    wrong = 0
    print("%-16s %-10s %s" % ("effect", "verdict", "detail"))
    for token, params in EFFECTS:
        if token in ANIMATES_BY_STATE:
            print("%-16s %-10s %s"
                  % (token, "state", "moves by carrying state, not by the index"))
            continue
        seen = set()
        for frame in frames:
            out = os.path.join(work, "anim.ppm")
            subprocess.run([exe, "--effect-dump", "%s:%s" % (token, params),
                            src, out, str(frame), str(PASSES.get(token, 1))],
                           capture_output=True)
            seen.add(raster(out) if os.path.exists(out) else b"")
        moves = len(seen) > 1
        listed = token in ANIMATES_BY_INDEX
        if moves == listed:
            print("%-16s %-10s %s"
                  % (token, "ok", "moves" if moves else "still, as listed"))
        else:
            wrong += 1
            print("%-16s %-10s %s"
                  % (token, "WRONG",
                     "the pixels move and the header says they do not"
                     if moves else
                     "the header says it animates and the pixels do not"))
    print()
    print("FAIL: %d effect(s) misfiled in cueEffectKindAnimates" % wrong if wrong
          else "the animating effects are exactly the ones the header lists")
    return 1 if wrong else 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default=os.path.join(
        REPO, "build", "windows", "Release", "Deckboy.exe"))
    parser.add_argument("--source", help="picture to run the effects on "
                                         "(default: a generated fractal)")
    parser.add_argument("--frame", type=int, default=7,
                        help="frame index, for the effects that advance with time")
    parser.add_argument("--sheet", help="write a contact sheet here (PNG)")
    parser.add_argument("--params", action="store_true",
                        help="check every named parameter moves the picture")
    parser.add_argument("--animation", action="store_true",
                        help="check cueEffectKindAnimates matches the pixels")
    args = parser.parse_args()

    exe = os.path.abspath(args.exe)
    if not os.path.exists(exe):
        print("no binary at %s" % exe)
        return 1
    deckboy_testroot.warn_if_stale(exe)

    work = tempfile.mkdtemp(prefix="deckboy-fx-offline-")
    src = os.path.join(work, "src.ppm")
    if args.source:
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", args.source,
                        "-frames:v", "1", "-pix_fmt", "rgb24", src], check=True)
    else:
        # Something with structure at every scale. Flat colour bars are a poor
        # test for anything that reacts to image content, and they made a dead
        # reaction-diffusion look identical to a live one.
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-f", "lavfi",
                        "-i", "mandelbrot=size=640x360:rate=1",
                        "-frames:v", "1", "-pix_fmt", "rgb24", src], check=True)
    base = raster(src)

    if args.animation:
        return check_animation(exe, src, work)
    if args.params:
        return check_params(exe, src, base, work)

    rows, outputs, failures = [], [], 0
    for token, params in EFFECTS:
        out = os.path.join(work, token + ".ppm")
        proc = subprocess.run([exe, "--effect-dump", "%s:%s" % (token, params),
                               src, out, str(args.frame),
                               str(PASSES.get(token, 1))],
                              capture_output=True, text=True)
        if proc.returncode != 0 or not os.path.exists(out):
            rows.append((token, "FAIL", (proc.stderr or proc.stdout).strip()[:70]))
            failures += 1
            continue
        shot = raster(out)
        if len(shot) != len(base):
            rows.append((token, "FAIL", "wrong size out"))
            failures += 1
            continue
        # Sampled rather than exhaustive: a stride keeps this instant on a 4K
        # source and no real effect changes only the bytes a stride skips.
        changed = sum(1 for a, b in zip(base[::97], shot[::97]) if abs(a - b) > 8)
        pct = 100.0 * changed / len(base[::97])
        ms = ""
        for word in proc.stdout.split():
            if word.endswith("ms"):
                ms = " " + word
        if pct < 0.5:
            rows.append((token, "NO CHANGE", "identical to the source"))
            failures += 1
        else:
            rows.append((token, "ok", "%.1f%% of bytes changed%s" % (pct, ms)))
        outputs.append(out)

    if args.sheet and outputs:
        cols = 3
        inputs = []
        for path in [src] + outputs:
            inputs += ["-i", path]
        scaled = "".join("[%d]scale=426:-1[s%d];" % (i, i)
                         for i in range(len(outputs) + 1))
        stacks, row_labels = "", []
        for start in range(0, len(outputs) + 1, cols):
            group = list(range(start, min(start + cols, len(outputs) + 1)))
            if len(group) < cols:
                break                      # drop a ragged last row
            name = "r%d" % start
            stacks += "".join("[s%d]" % i for i in group) + "hstack=%d[%s];" % (cols, name)
            row_labels.append(name)
        graph = scaled + stacks + "".join("[%s]" % n for n in row_labels) + \
            "vstack=%d" % len(row_labels)
        subprocess.run(["ffmpeg", "-y", "-v", "error"] + inputs +
                       ["-filter_complex", graph, args.sheet], check=False)
        print("contact sheet: %s (source first, then %s)"
              % (args.sheet, ", ".join(t for t, _ in EFFECTS)))

    print()
    print("%-18s %-10s %s" % ("effect", "verdict", "detail"))
    for token, verdict, detail in rows:
        print("%-18s %-10s %s" % (token, verdict, detail))
    for token, why in NOT_PIXEL_EFFECTS:
        print("%-18s %-10s %s" % (token, "n/a", why))
    print("\n%s" % ("FAIL" if failures else
                    "every effect changes the picture"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
