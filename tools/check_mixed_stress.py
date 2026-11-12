#!/usr/bin/env python3
"""Many formats at once: a 4K base under a grid of scaled decks, with effects.

Every deck plays a different kind of file -- H.264 4K60, HEVC 10-bit 4K, AV1,
ProRes, DNxHR, HAP, CineForm, FFV1, VP9 with alpha, QuickTime Animation with
alpha, MPEG-2 interlaced, MJPEG, a portrait phone clip, an animated GIF and a
PNG with alpha -- each scaled down and placed over the base, the way a second
deck makes a picture-in-picture. Then it adds effects to every layer, a full
stack of eight on the base, and transitions on the base while all of it runs.

It times every frame (DECKBOY_UI_PROFILE) and prints, per stage, the frame
times and where the output pass spent them, so a slow stage names its cost.
It also fails a stage where a deck stops playing.

    py -3 tools/check_mixed_stress.py [--visible] [--seconds 15]

--visible takes over the screen (hidden windows are throttled; ask first).
Sound always goes to the dummy driver.
"""
import argparse
import json
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

SEED = "update_check\t0\nui_sounds\t0\nui_scale\t1.0\n"
SECONDS = 6
SRC = "testsrc2=s={w}x{h}:r={r}:d=%d" % SECONDS
TONE = ["-f", "lavfi", "-i", "sine=f=440:d=%d" % SECONDS]

# name, file, input filter, encoder args. The base is first.
CLIPS = [
    ("h264 4K60", "base_h264_4k60.mp4", SRC.format(w=3840, h=2160, r=60),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p", "-c:a", "aac"]),
    ("hevc 10-bit 4K30", "hevc10_4k.mkv", SRC.format(w=3840, h=2160, r=30),
     ["-c:v", "libx265", "-preset", "ultrafast", "-pix_fmt", "yuv420p10le", "-x265-params", "log-level=error"]),
    ("av1 1080p60", "av1_1080p60.mkv", SRC.format(w=1920, h=1080, r=60),
     ["-c:v", "libsvtav1", "-preset", "12", "-pix_fmt", "yuv420p"]),
    ("prores 422hq 1080p25", "prores_1080p25.mov", SRC.format(w=1920, h=1080, r=25),
     ["-c:v", "prores_ks", "-profile:v", "3", "-c:a", "pcm_s16le"]),
    ("dnxhr hq 1080p29.97", "dnxhr_1080p.mov", SRC.format(w=1920, h=1080, r="30000/1001"),
     ["-c:v", "dnxhd", "-profile:v", "dnxhr_hq", "-pix_fmt", "yuv422p"]),
    ("hap 1080p30", "hap_1080p.mov", SRC.format(w=1920, h=1080, r=30),
     ["-c:v", "hap", "-format", "hap"]),
    ("cineform 1080p24", "cfhd_1080p.mov", SRC.format(w=1920, h=1080, r=24),
     ["-c:v", "cfhd", "-pix_fmt", "yuv422p10le"]),
    ("ffv1 720p50", "ffv1_720p.mkv", SRC.format(w=1280, h=720, r=50),
     ["-c:v", "ffv1"]),
    ("vp9 alpha 720p30", "vp9_alpha.webm",
     SRC.format(w=1280, h=720, r=30) + ",format=yuva420p,geq=lum='lum(X,Y)':a='if(lt(hypot(X-640,Y-360),300),255,0)'",
     ["-c:v", "libvpx-vp9", "-pix_fmt", "yuva420p", "-deadline", "realtime", "-b:v", "2M"]),
    ("qtrle alpha 640x360", "qtrle_alpha.mov",
     SRC.format(w=640, h=360, r=30) + ",format=argb,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':a='if(lt(X,320),255,96)'",
     ["-c:v", "qtrle", "-pix_fmt", "argb"]),
    ("mpeg2 480i29.97", "mpeg2_480i.ts", SRC.format(w=720, h=480, r="30000/1001"),
     ["-c:v", "mpeg2video", "-flags", "+ilme+ildct", "-top", "1", "-b:v", "6M", "-c:a", "mp2"]),
    ("mjpeg 1080p29.97", "mjpeg_1080p.avi", SRC.format(w=1920, h=1080, r="30000/1001"),
     ["-c:v", "mjpeg", "-q:v", "4", "-pix_fmt", "yuvj420p"]),
    ("h264 portrait 1080x1920 23.976", "portrait_2398.mp4", SRC.format(w=1080, h=1920, r="24000/1001"),
     ["-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p", "-c:a", "aac"]),
    ("gif 480x270", "anim.gif", SRC.format(w=480, h=270, r=15),
     ["-c:v", "gif"]),
    ("png alpha still 2048", "still_alpha.png", None, None),
]

# Two effects for each layer, cycling, and the base's eight.
LAYER_FX = [("blur", "glow"), ("kaleidoscope", "curves"), ("colour", "vignette"),
            ("posterise", "scanlines"), ("feedback", "levels"), ("ripple", "sharpen"),
            ("mirror", "grain"), ("edges", "invert"), ("mosaic", "solarise"),
            ("channel_offset", "colour"), ("polar_warp", "glow"), ("caustics", "levels"),
            ("crystallise", "curves"), ("relight", "vignette")]
BASE_FX = ["curves", "glow", "blur", "ripple", "vignette", "grain", "levels", "sharpen"]

FRAME = re.compile(r"\[DECKBOY_UI_PROFILE (\d+)ms\] frame dt=([\d.]+)ms .*?render\+outputs=([\d.]+)ms")
OUTPASS = re.compile(r"\[DECKBOY_UI_PROFILE (\d+)ms\] output (\d+) pass=([\d.]+)ms "
                     r"composite\+egress=([\d.]+)ms tap\+sends=([\d.]+)ms present=([\d.]+)ms")


def make_clips(work):
    work.mkdir(parents=True, exist_ok=True)
    paths = []
    for name, file, src, enc in CLIPS:
        out = work / file
        if not out.exists():
            if src is None:
                cmd = ["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                       "testsrc2=s=2048x2048:d=1,format=rgba,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':"
                       "a='if(lt(hypot(X-1024,Y-1024),900),255,0)'", "-frames:v", "1", str(out)]
            else:
                audio = TONE if any(a in enc for a in ("aac", "pcm_s16le", "mp2")) else []
                cmd = ["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", src, *audio,
                       "-shortest", *enc, str(out)]
            t = time.time()
            subprocess.run(cmd, check=True)
            print("  made %-32s %5.1fs" % (name, time.time() - t), flush=True)
        paths.append((name, out))
    return paths


def import_clip(db, work, clip):
    drop = work / "drop.txt"
    drop.write_text(str(clip) + "\n", encoding="utf8")
    db.send("DROPTEST " + str(drop))
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        reply = db.send("DROPTEST")
        if "pending=0" in reply and "probes=0" in reply:
            time.sleep(.3)
            return
        time.sleep(.25)
    raise RuntimeError("import did not finish: " + str(clip))


def grid_offsets(n, w=1920, h=1080):
    """Centre offsets in pixels for n tiles in a 4-wide grid over the frame."""
    cols = 4
    rows = (n + cols - 1) // cols
    out = []
    for i in range(n):
        c, r = i % cols, i // cols
        x = (c + 0.5) * w / cols - w / 2
        y = (r + 0.5) * h / max(rows, 1) - h / 2
        out.append((int(x), int(y)))
    return out


def summarise(log, start, end):
    text = Path(log).read_text(encoding="utf-8", errors="replace")[start:end]
    dts = [float(m.group(2)) for m in FRAME.finditer(text)]
    passes = {}
    for m in OUTPASS.finditer(text):
        passes.setdefault(int(m.group(2)), []).append(tuple(float(m.group(i)) for i in range(3, 7)))
    return dts, passes


def pct(values, p):
    if not values:
        return 0.0
    s = sorted(values)
    return s[min(len(s) - 1, int(len(s) * p))]


def report(stage, dts, passes):
    if not dts:
        print("  %-26s no frames logged" % stage)
        return
    late = sum(1 for d in dts if d > 20.0)
    print("  %-26s frames %5d  mean %5.2f  p95 %5.2f  p99 %6.2f  max %6.2f ms  late(>20ms) %4d (%.1f%%)"
          % (stage, len(dts), statistics.mean(dts), pct(dts, .95), pct(dts, .99), max(dts),
             late, 100.0 * late / len(dts)), flush=True)
    for out, rows in sorted(passes.items()):
        cols = list(zip(*rows))
        print("      output %d pass mean %5.2f p99 %6.2f | composite+egress %5.2f | tap+sends %5.2f | present %5.2f"
              % (out, statistics.mean(cols[0]), pct(cols[0], .99), statistics.mean(cols[1]),
                 statistics.mean(cols[2]), statistics.mean(cols[3])), flush=True)


def deck_states(db):
    st = json.loads(db.send("STATUS JSON"))
    return [(d.get("status"), d.get("active")) for d in st.get("decks", [])]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--work", default=str(ROOT / "build/mixed-stress"))
    ap.add_argument("--seconds", type=float, default=15.0)
    ap.add_argument("--visible", action="store_true")
    ap.add_argument("--layers", type=int, default=len(CLIPS) - 1, help="decks over the base")
    args = ap.parse_args()

    work = Path(args.work)
    print("making clips", flush=True)
    clips = make_clips(work / "media")
    base, layers = clips[0], clips[1:1 + args.layers]
    show = work / "stress.deckboy"
    show.write_text(SEED, encoding="utf8")
    env = {"DECKBOY_UI_PROFILE": "2"}
    fails = []
    with Runtime(args.exe, work / "run", show, env=env, visible=args.visible) as db:
        log = db.log.name
        db.send("MASTERVOL 0")
        print("building %d decks" % (1 + len(layers)), flush=True)
        db.send("DECK 1")
        import_clip(db, work, base[1])
        import_clip(db, work, clips[2][1])   # a second cue for the transitions
        db.send("SELECT 1")
        db.send("LOOP ON")
        db.send("SELECT 2")
        db.send("LOOP ON")
        offs = grid_offsets(len(layers))
        for i, (name, path) in enumerate(layers):
            db.send("DECKADD")
            db.send("DECK %d" % (i + 2))
            import_clip(db, work, path)
            for c in ("SELECT 1", "LOOP ON", "SCALE 0.25",
                      "OFFSETX %d" % offs[i][0], "OFFSETY %d" % offs[i][1]):
                try:
                    db.send(c)
                except RuntimeError as e:
                    fails.append("%s: %s -> %s" % (name, c, e))
        db.send("OUTPUT SELECT 1")
        db.send("OUTPUT ON")
        time.sleep(1.5)
        # Every layer onto output 1, over the base -- what makes a scaled
        # deck a picture-in-picture rather than a deck nobody sees.
        for d in range(2, len(layers) + 2):
            db.send("DECK %d" % d)
            try:
                db.send("VIDEO OUTPUT ASSIGN")
            except RuntimeError as e:
                fails.append("deck %d: assign -> %s" % (d, e))
        for d in range(1, len(layers) + 2):
            db.send("DECK %d" % d)
            db.send("SELECT 1")
            db.send("TAKE")
        time.sleep(4.0)   # every open settles

        def stage(name, during=None):
            mark = len(Path(log).read_text(encoding="utf-8", errors="replace"))
            t0 = time.monotonic()
            while time.monotonic() - t0 < args.seconds:
                if during:
                    during(time.monotonic() - t0)
                time.sleep(0.1)
            end = len(Path(log).read_text(encoding="utf-8", errors="replace"))
            snap = work / ("stage-%s.bmp" % re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-"))
            db.send("OUTSNAP %s" % snap)
            dts, passes = summarise(log, mark, end)
            report(name, dts, passes)
            st = json.loads(db.send("STATUS JSON"))
            costs = ["%d:%.0f/%.0f" % (i + 1, d.get("fxMs", 0), d.get("decodeFps", 0))
                     for i, d in enumerate(st.get("decks", []))]
            print("      deck fx ms / decode fps: " + "  ".join(costs), flush=True)
            states = deck_states(db)
            stopped = [i + 1 for i, s in enumerate(states) if s[0] != "Playing"]
            if stopped:
                fails.append("%s: decks not playing: %s" % (name, stopped))
                print("      NOT PLAYING: decks %s %s" % (stopped, [states[i - 1] for i in stopped]), flush=True)

        print("stages (%.0f s each)" % args.seconds, flush=True)
        stage("layers, no effects")
        for i in range(len(layers)):
            db.send("DECK %d" % (i + 2))
            db.send("SELECT 1")
            for fx in LAYER_FX[i % len(LAYER_FX)]:
                db.send("FX ADD " + fx)
        time.sleep(1.0)
        stage("+ 2 effects per layer")
        db.send("DECK 1")
        db.send("SELECT 1")
        for fx in BASE_FX:
            db.send("FX ADD " + fx)
        time.sleep(1.0)
        stage("+ 8 effects on the base")
        # LFOs: a parameter and the amount on every effect, and the layers' own
        # position and rotation moving.
        for d in range(1, len(layers) + 2):
            db.send("DECK %d" % d)
            db.send("SELECT 1")
            n = len(BASE_FX) if d == 1 else 2
            for k in range(1, n + 1):
                for c in ("FX LFO %d A on" % k, "FX LFO %d A rate %.2f" % (k, 0.3 + 0.17 * k),
                          "FX LFO %d E on" % k):
                    try:
                        db.send(c)
                    except RuntimeError as e:
                        fails.append("lfo: %s -> %s" % (c, e))
            if d > 1:
                for c in ("GEOLFO X on", "GEOLFO X rate %.2f" % (0.2 + 0.05 * d), "GEOLFO ROTATION on"):
                    try:
                        db.send(c)
                    except RuntimeError as e:
                        fails.append("geolfo: %s -> %s" % (c, e))
        time.sleep(1.0)
        stage("+ LFOs everywhere")
        db.send("DECK 1")
        db.send("TRANSITION 1")
        db.send("TRANSITIONSTYLE crossfade")
        state = {"next": 0.0, "cue": 1}

        def flip(t):
            if t >= state["next"]:
                state["cue"] = 2 if state["cue"] == 1 else 1
                db.send("DECK 1")
                db.send("GOTO %d" % state["cue"])
                db.send("TAKE")
                state["next"] = t + 2.5
        stage("+ transitions on the base", flip)
        db.send("DECK 1")
        tail = db.send("STATUS")
    for line in tail.splitlines():
        if re.search(r"paced_hz|dropped|late|output", line, re.I):
            print("   ", line[:220])
    if fails:
        print("FAILED:")
        for f in fails:
            print("  -", f)
        return 1
    print("all stages ran")
    return 0


if __name__ == "__main__":
    sys.exit(main())
