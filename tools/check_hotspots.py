#!/usr/bin/env python3
"""What each feature costs a 4K output: a survey, one feature at a time.

A 4K clip plays on playlist 1 into a 4K output. Each scenario adds ONE thing
-- a layer of some kind, a look on the base, recording, the multiview, a
transition style -- in a fresh app, and the frames are timed for a few
seconds with DECKBOY_UI_PROFILE. The table says which features push the
frame past 16.7 ms (60 fps), and the profile lines it prints name where the
time went. This is how the GPU work of 2026-11-12 chose what to move next.

    py -3 tools/check_hotspots.py --visible [--only NAME] [--seconds 6]
"""
import argparse
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402
from check_mixed_stress import import_clip, FRAME  # noqa: E402
from build_example_shows import CUE_FIELDS  # noqa: E402

SEED = "update_check\t0\nui_sounds\t0\nui_scale\t1.0\n"
LAYER = ["DECKADD", "DECK 2"]
ON_OUT = ["SELECT 1", "SCALE 0.5", "OFFSETX 400", "OFFSETY 200", "VIDEO OUTPUT ASSIGN", "TAKE"]

# name, commands after the base is up, cue-field edits on the base (show file), flips
SCENARIOS = [
    ("4K video alone", [], {}, None),
    ("+ animated pattern layer", LAYER + ["PATTERN ADD test-bars"] + ON_OUT, {}, None),
    ("+ terrarium layer", LAYER + ["PATTERN ADD terrarium"] + ON_OUT, {}, None),
    ("+ video synth layer", LAYER + ["SYNTH ADD"] + ON_OUT, {}, None),
    ("+ text cue layer", LAYER + ["TEXTCUE NEW"] + ON_OUT, {}, None),
    ("+ lower third layer", LAYER + ["TEXTCUE LOWERNEW"] + ON_OUT, {}, None),
    ("+ recording", ["RECORD ON"], {}, None),
    ("+ multiview", ["MULTIVIEW ON"], {}, None),
    ("+ scopes", ["SCOPES SCOPES"], {}, None),
    ("+ grade on the base", [], {"brightness": "1.25", "contrast": "1.2", "saturation": "1.3"}, None),
    ("+ chroma key on the base", [], {"chromaKeyEnabled": "1"}, None),
    ("+ feedback on the base", ["DECK 1", "SELECT 1", "FX ADD feedback"], {}, None),
    ("+ caustics on the base", ["DECK 1", "SELECT 1", "FX ADD caustics"], {}, None),
    ("+ stereogram on the base", ["DECK 1", "SELECT 1", "FX ADD stereogram"], {}, None),
    ("+ tape on the base", ["DECK 1", "SELECT 1", "FX ADD tape"], {}, None),
    ("+ melt on the base", ["DECK 1", "SELECT 1", "FX ADD melt"], {}, None),
    ("transitions: crossfade", [], {}, "crossfade"),
    ("transitions: portal", [], {}, "portal"),
    ("transitions: shatter", [], {}, "shatter"),
    ("transitions: clouds", [], {}, "clouds"),
    ("transitions: ghastly", [], {}, "ghastly"),
]

PROFILE = re.compile(r"\[DECKBOY_UI_PROFILE \d+ms\] ((?:layer|engine|command|transition|deck|output) .*)")


def frames(log, start):
    text = Path(log).read_text(encoding="utf-8", errors="replace")[start:]
    rows = []
    for line in text.splitlines():
        m = FRAME.search(line)
        if m:
            up = re.search(r"update=([\d.]+)ms", line)
            rows.append((float(m.group(2)), float(up.group(1)) if up else 0.0, float(m.group(3))))
    notes = [m.group(1) for m in PROFILE.finditer(text)]
    return rows, notes


def base_show(exe, work, clip, edits):
    """A show with the clip twice on playlist 1 (for the transitions), with `edits` on cue 1."""
    show = work / "seed.deckboy"
    show.write_text(SEED, encoding="utf8")
    with Runtime(exe, work / "seed-run", show) as db:
        db.send("DECK 1")
        import_clip(db, work, clip)
        import_clip(db, work, clip)
        for c in ("SELECT 1", "LOOP ON", "SELECT 2", "LOOP ON"):
            db.send(c)
        text = db.saved()
    if edits:
        rows = text.splitlines()
        cues = [i for i, line in enumerate(rows) if line.startswith("cue\t")]
        f = rows[cues[0]].split("\t")
        for key, value in edits.items():
            f[CUE_FIELDS[key]] = value
        rows[cues[0]] = "\t".join(f)
        text = "\n".join(rows) + "\n"
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--work", default=str(ROOT / "build/hotspots"))
    ap.add_argument("--seconds", type=float, default=6.0)
    ap.add_argument("--visible", action="store_true")
    ap.add_argument("--only", action="append", default=[])
    args = ap.parse_args()
    work = Path(args.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    clip = ROOT / "build/mixed-stress/media/base_h264_4k60.mp4"
    if not clip.exists():
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=s=3840x2160:r=60:d=6",
                        "-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p", str(clip)], check=True)
    plain = base_show(args.exe, work, clip, {})
    print("%-28s %6s %6s %6s %7s %8s" % ("scenario", "mean", "p95", "late%", "update", "outputs"))
    for name, commands, edits, flip in SCENARIOS:
        if args.only and not any(o in name for o in args.only):
            continue
        show = work / "show.deckboy"
        show.write_text(base_show(args.exe, work, clip, edits) if edits else plain, encoding="utf8")
        with Runtime(args.exe, work / "run", show, env={"DECKBOY_UI_PROFILE": "2"}, visible=args.visible) as db:
            log = db.log.name
            for c in ("MASTERVOL 0", "OUTPUT SELECT 1", "OUTPUT ON", "VIDEO 3840X2160", "DECK 1", "SELECT 1", "TAKE"):
                db.send(c)
            time.sleep(2.0)
            for c in commands:
                try:
                    db.send(c)
                except RuntimeError as e:
                    print("   %s: %s" % (c, e))
            if flip:
                db.send("DECK 1")
                db.send("TRANSITION 1")
                db.send("TRANSITIONSTYLE " + flip)
            time.sleep(2.0)
            mark = len(Path(log).read_text(encoding="utf-8", errors="replace"))
            t0 = time.monotonic()
            cue = 1
            while time.monotonic() - t0 < args.seconds:
                if flip:
                    cue = 2 if cue == 1 else 1
                    db.send("DECK 1")
                    db.send("SELECT %d" % cue)
                    db.send("TAKE")
                    time.sleep(1.5)
                else:
                    time.sleep(0.25)
            rows, notes = frames(log, mark)
        if not rows:
            print("%-28s no frames" % name)
            continue
        dts = [r[0] for r in rows]
        late = 100.0 * sum(1 for d in dts if d > 20.0) / len(dts)
        p95 = sorted(dts)[int(len(dts) * 0.95)]
        print("%-28s %6.1f %6.1f %5.1f%% %7.2f %8.2f" % (name, statistics.mean(dts), p95, late,
                                                       statistics.mean(r[1] for r in rows),
                                                       statistics.mean(r[2] for r in rows)), flush=True)
        seen = set()
        for n in notes:
            key = re.sub(r"[\d.]+ms", "", n)[:40]
            if key in seen:
                continue
            seen.add(key)
            print("      " + n[:140])
            if len(seen) >= 4:
                break
    return 0


if __name__ == "__main__":
    sys.exit(main())
