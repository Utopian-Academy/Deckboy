"""An HDR stream, measured: what arrives at the far end of the SRT link.

check_hdr_recording.py for a STREAM. Output 1 runs HDR and plays the exact PQ
ladder (check_hdr_sources' greys 0.5-4000 nits); Output 2 is an SRT stream
mirroring it. A local ffmpeg listens, keeps what arrives, and it is read back:
  - HEVC Main10, BT.2020, tagged PQ (or HLG with --hlg)
  - each grey at the code the source carried (PQ) or the BT.2100 HLG code for
    that light (check_hdr_sources.hlg_encode_display, not the app's code),
    within 2 of 1023
Every window hidden. Evidence: build/hdr-stream-*/
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
from deckboy_testroot import populate

ROOT = Path(__file__).resolve().parent.parent


def launch(root, state, show, port, log):
    env = dict(os.environ, DECKBOY_ROOT=str(root), DECKBOY_STATE_DIR=str(state), DECKBOY_PROJECT=str(show),
               DECKBOY_COMPANION_PORT=str(port), DECKBOY_HIDE_WINDOWS="1", DECKBOY_OUTPUT_RENDERER="direct3d11")
    app = subprocess.Popen([str(ROOT / "build/windows/Release/Deckboy.exe"), "--allow-multi-instance", str(show)],
                           env=env, cwd=ROOT, stdout=log, stderr=log)
    for _ in range(200):
        try:
            if c.command(port, "PING").startswith("OK"):
                break
        except OSError:
            time.sleep(0.3)
    return app


def main():
    hlg = "--hlg" in sys.argv
    ffmpeg = shutil.which("ffmpeg")
    ffprobe = shutil.which("ffprobe")
    work = Path(tempfile.mkdtemp(prefix="hdr-stream-", dir=ROOT / "build"))
    clip = work / "pq.mov"
    ps = c.make_clip(ffmpeg, clip, "pq")
    root = work / "root"
    populate(str(root))
    state = work / "state"
    show = root / "data/default.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\noutput_follow_display\t0\noutput_render_width\t1280\n"
                    "output_render_height\t720\n", encoding="utf-8")
    failures = []

    def expect(ok, label):
        print(("ok   " if ok else "FAIL ") + label, flush=True)
        if not ok:
            failures.append(label)

    # 1. Let the app make its recording output (an encoder that mirrors
    # Output 1), save, and turn that output into an SRT stream in the file:
    # there is no control-port verb for a stream destination.
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
    srt_port = c.free_port()
    rows = show.read_text(encoding="utf-8").splitlines()
    for i, row in enumerate(rows):
        f = row.split("\t")
        if f[0] == "output_target" and f[1] == "1":
            f[5], f[6], f[7] = "1", "1", "srt"
            f[8] = f"srt://127.0.0.1:{srt_port}?mode=caller"
            rows[i] = "\t".join(f)
    show.write_text("\n".join(rows) + "\n", encoding="utf-8")

    # 2. The far end: ffmpeg listening, keeping what arrives.
    received = work / "received.ts"
    listener = subprocess.Popen([ffmpeg, "-v", "error", "-y", "-i", f"srt://127.0.0.1:{srt_port}?mode=listener",
                                 "-c", "copy", "-t", "8", str(received)],
                                stdout=subprocess.DEVNULL, stderr=(work / "listener.log").open("w"))
    port = c.free_port()
    app = launch(root, state, show, port, log)
    try:
        listing = work / "drop.txt"
        listing.write_text(f"{clip}\n")
        c.command(port, f"DROPTEST {listing}")
        time.sleep(4)
        c.command(port, "OUTPUT SELECT 1")
        print(c.command(port, "OUTPUT HDR ON"), c.command(port, "OUTPUT ON"))
        if hlg:
            print(c.command(port, "OUTPUT HDR SIGNAL HLG"))
        c.command(port, "TAKE 1")
        c.command(port, "OUTPUT SELECT 2")
        print(c.command(port, "OUTPUT ON"))
        try:
            listener.wait(timeout=40)
        except subprocess.TimeoutExpired:
            listener.kill()
    finally:
        app.kill()
        app.wait(timeout=10)
        log.close()

    expect(received.exists() and received.stat().st_size > 10000, "the stream arrived")
    if received.exists() and received.stat().st_size > 10000:
        probe = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                                "stream=codec_name,profile,pix_fmt,color_range,color_space,color_transfer,color_primaries",
                                "-of", "default=nw=1", str(received)], capture_output=True, text=True).stdout
        print("   " + probe.strip().replace("\n", "  "))
        transfer = "arib-std-b67" if hlg else "smpte2084"
        expect("codec_name=hevc" in probe and f"color_transfer={transfer}" in probe and
               "color_primaries=bt2020" in probe and "10le" in probe,
               f"HEVC Main10, BT.2020, {transfer}")
        raw = subprocess.run([ffmpeg, "-v", "error", "-i", str(received), "-f", "rawvideo",
                              "-pix_fmt", "yuv420p10le", "-"], capture_output=True).stdout
        dims = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                               "stream=width,height", "-of", "csv=p=0", str(received)],
                              capture_output=True, text=True).stdout
        w, h = map(int, dims.split()[0].split(",")[:2])
        per_frame = w * h * 3 // 2
        frames = len(raw) // (per_frame * 2)
        expect(frames > 0, f"it decodes ({frames} frames)")
        if frames:
            last = np.frombuffer(raw, np.uint16)[(frames - 1) * per_frame:(frames - 1) * per_frame + w * h]
            y = last.reshape(h, w).astype(float)
            for i, (label, nits, _) in enumerate(ps[:8]):
                if hlg:
                    want = c.ycbcr10(c.hlg_encode_display(np.asarray(nits, dtype=float)))[0]
                else:
                    want = c.ycbcr10(c.pq_encode(nits))[0]
                cx, cy = int((i + 0.5) * w / 8), int(0.25 * h)
                got = float(y[cy - 4:cy + 4, cx - 4:cx + 4].mean())
                # A stream is lossy at a live bitrate; flat patches survive it.
                expect(abs(got - want) <= 2.5, f"{label}: luma code {got:.1f}, should be {want}")
    print(f"hdr stream: {len(failures)} failures; evidence: {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
