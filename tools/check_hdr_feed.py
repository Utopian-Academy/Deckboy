"""Graphics over an HDR feed, HDR in and HDR out, measured end to end.

The job a broadcast engineer asked for ("how do I add graphics to an HDR
feed?"), done by Deckboy and read back at the far end:

  1. ffmpeg sends an HDR feed over SRT: check_hdr_sources' exact ladder of
     greys (0.5-4000 nits), HEVC 10-bit, tagged PQ -- or HLG with --hlg.
  2. Deckboy takes it as a stream cue marked STREAM HDR PQ (or HLG), on an
     output running HDR, with a second playlist layered over it carrying a
     graphic: a white box on a transparent picture.
  3. That output goes out as an HDR SRT stream (OUTPUT HDR SIGNAL to match)
     to a second ffmpeg, which keeps what arrives.

Then, in what arrived:
  - HEVC Main10, BT.2020, tagged PQ (HLG)
  - every grey of the feed at the code it was sent with: the feed's light
    went through Deckboy untouched, highlights and all
  - the graphic's white at 203 nits (BT.2408 graphics white): PQ code 573,
    HLG 75% (721)
Two lossy encodes sit in the chain, so a code within 4 of 1023 is a pass.
Every window hidden. Evidence: build/hdr-feed-*/
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import numpy as np

import check_hdr_sources as c
from check_hdr_stream import launch
from deckboy_testroot import populate

ROOT = Path(__file__).resolve().parent.parent
W, H = 1280, 720
BOX = (0.625, 0.60, 0.875, 0.90)   # the graphic: x0, y0, x1, y1 as fractions


def main():
    hlg = "--hlg" in sys.argv
    transfer = "hlg" if hlg else "pq"
    ffmpeg = shutil.which("ffmpeg")
    ffprobe = shutil.which("ffprobe")
    work = Path(tempfile.mkdtemp(prefix="hdr-feed-", dir=ROOT / "build"))
    clip = work / f"{transfer}.mov"
    ps = c.make_clip(ffmpeg, clip, transfer)
    # The graphic: white where the box is, transparent everywhere else.
    graphic = work / "graphic.png"
    x0, y0, x1, y1 = (int(BOX[0] * W), int(BOX[1] * H), int(BOX[2] * W), int(BOX[3] * H))
    # (Drawn here, not with ffmpeg's drawbox, which leaves the box's alpha at 0.)
    rgba = np.zeros((H, W, 4), np.uint8)
    rgba[y0:y1, x0:x1] = 255
    from PIL import Image
    Image.fromarray(rgba, "RGBA").save(graphic)
    root = work / "root"
    populate(str(root))
    state = work / "state"
    show = root / "data/default.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\noutput_follow_display\t0\n"
                    f"output_render_width\t{W}\noutput_render_height\t{H}\n", encoding="utf-8")
    failures = []

    def expect(ok, label):
        print(("ok   " if ok else "FAIL ") + label, flush=True)
        if not ok:
            failures.append(label)

    # First run: make the recording output and save it, to be turned into an
    # SRT stream in the file (no verb sets a stream's destination).
    port = c.free_port()
    log = (work / "app.log").open("w")
    app = launch(root, state, show, port, log)
    try:
        c.command(port, "RECORD ON")
        time.sleep(2)
        c.command(port, "RECORD OFF")
        time.sleep(1)
        c.command(port, "SAVE")
        time.sleep(1)
    finally:
        app.kill()
        app.wait(timeout=10)
    out_port = c.free_port()
    rows = show.read_text(encoding="utf-8").splitlines()
    for i, row in enumerate(rows):
        f = row.split("\t")
        if f[0] == "output_target" and f[1] == "1":
            f[5], f[6], f[7] = "1", "1", "srt"
            f[8] = f"srt://127.0.0.1:{out_port}?mode=caller"
            rows[i] = "\t".join(f)
    show.write_text("\n".join(rows) + "\n", encoding="utf-8")

    # The feed, and the far end.
    in_port = c.free_port()
    sender = subprocess.Popen([ffmpeg, "-v", "error", "-re", "-stream_loop", "-1", "-i", str(clip), "-c", "copy",
                               "-f", "mpegts", f"srt://127.0.0.1:{in_port}?mode=listener"],
                              stdout=subprocess.DEVNULL, stderr=(work / "sender.log").open("w"))
    received = work / "received.ts"
    listener = subprocess.Popen([ffmpeg, "-v", "error", "-y", "-i", f"srt://127.0.0.1:{out_port}?mode=listener",
                                 "-c", "copy", "-t", "8", str(received)],
                                stdout=subprocess.DEVNULL, stderr=(work / "listener.log").open("w"))
    port = c.free_port()
    app = launch(root, state, show, port, log)
    try:
        say = lambda line: print(f"  {line} -> {c.command(port, line)}")
        say(f"STREAM ADD srt://127.0.0.1:{in_port}?mode=caller")
        sdr = "--sdr" in sys.argv   # the same chain with no HDR, for its frame rates only
        say(f"STREAM HDR {'SDR' if sdr else 'HLG' if hlg else 'PQ'}")
        say("OUTPUT SELECT 1")
        say("OUTPUT HDR " + ("OFF" if sdr else "ON"))
        if hlg:
            say("OUTPUT HDR SIGNAL HLG")
        say("OUTPUT ON")
        if "--1080" in sys.argv:
            # OUTPUT ON follows the display; this pins the broadcast raster.
            say("VIDEO 1080P")
        say("TAKE 1")
        time.sleep(3)
        # The graphic, on a second playlist layered over the feed.
        say("DECKADD")
        say("DECK 2")
        listing = work / "drop.txt"
        listing.write_text(f"{graphic}\n")
        say(f"DROPTEST {listing}")
        time.sleep(2)
        say("VIDEO OUTPUT ASSIGN")
        say("TAKE 1")
        say("VIDEO OUTPUT LAYER")
        time.sleep(2)
        say("OUTPUT SELECT 2")
        say("OUTPUT ON")
        for _ in range(3):
            time.sleep(3)
            status = c.command(port, "STATUS")
            for line in status.splitlines():
                if line.startswith("OUTPUT 2 "):
                    print("  output 2: " + " ".join(w for w in line.split()
                                                     if w.startswith(("health=", "stream", "output_fps="))))
        try:
            listener.wait(timeout=40)
        except subprocess.TimeoutExpired:
            listener.kill()
    finally:
        app.kill()
        app.wait(timeout=10)
        sender.kill()
        log.close()

    arrived = received.exists() and received.stat().st_size > 10000
    expect(arrived, "the output stream arrived")
    if arrived:
        probe = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                                "stream=codec_name,profile,pix_fmt,color_space,color_transfer,color_primaries",
                                "-of", "default=nw=1", str(received)], capture_output=True, text=True).stdout
        print("   " + probe.strip().splitlines()[0:6].__str__())
        trc = "arib-std-b67" if hlg else "smpte2084"
        expect("codec_name=hevc" in probe and f"color_transfer={trc}" in probe and
               "color_primaries=bt2020" in probe and "10le" in probe, f"HEVC Main10, BT.2020, {trc}")
        dims = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                               "stream=width,height", "-of", "csv=p=0", str(received)],
                              capture_output=True, text=True).stdout
        w, h = map(int, dims.split()[0].split(",")[:2])
        raw = subprocess.run([ffmpeg, "-v", "error", "-i", str(received), "-f", "rawvideo",
                              "-pix_fmt", "yuv420p10le", "-"], capture_output=True).stdout
        per_frame = w * h * 3 // 2
        frames = len(raw) // (per_frame * 2)
        expect(frames > 0, f"it decodes ({frames} frames)")
        if frames:
            last = np.frombuffer(raw, np.uint16)[(frames - 1) * per_frame:(frames - 1) * per_frame + w * h]
            y = last.reshape(h, w).astype(float)

            def at(fx, fy):
                cx, cy = int(fx * w), int(fy * h)
                return float(y[cy - 4:cy + 4, cx - 4:cx + 4].mean())

            encode = (lambda n: c.ycbcr10(c.hlg_encode_display(np.asarray(n, dtype=float)))[0]) if hlg \
                else (lambda n: c.ycbcr10(c.pq_encode(n))[0])
            for i, (label, nits, _) in enumerate(ps[:8]):
                want = encode(nits)
                got = at((i + 0.5) / 8, 0.25)
                expect(abs(got - want) <= 4, f"feed {label}: code {got:.1f}, sent {want}")
            white = encode(np.full(3, 203.0))
            got = at((BOX[0] + BOX[2]) / 2, (BOX[1] + BOX[3]) / 2)
            expect(abs(got - white) <= 4, f"graphic white at 203 nits: code {got:.1f}, should be {white}")
    print(f"hdr feed: {len(failures)} failures; evidence: {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
