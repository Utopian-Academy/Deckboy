"""An HDR recording, measured: what is in the file.

Plays an exact PQ ladder (check_hdr_sources' grey 0.5-4000 nits and EBU bars
at 203 nits) into a recording output with HDR on, every window hidden, then
reads the file back with ffprobe/ffmpeg and checks:
  - it is HEVC Main10 (or the chosen 10-bit codec) tagged BT.2020 / PQ / limited
  - each patch's luma code is the PQ code the source carried (within 2 of 1023)
    -- i.e. 1000 nits in the source is 1000 nits in the recording, not clipped
    to SDR white
Evidence: build/hdr-rec-*/
"""
import glob
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


def main():
    ffmpeg = shutil.which("ffmpeg")
    ffprobe = shutil.which("ffprobe")
    work = Path(tempfile.mkdtemp(prefix="hdr-rec-", dir=ROOT / "build"))
    clip = work / "pq.mov"
    ps = c.make_clip(ffmpeg, clip, "pq")
    port = c.free_port()
    root = work / "root"
    populate(str(root))
    state = work / "state"
    show = root / "data/default.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\noutput_follow_display\t0\noutput_render_width\t1280\n"
                    "output_render_height\t720\nrecording_codec\thevc\n", encoding="utf-8")
    env = dict(os.environ, DECKBOY_ROOT=str(root), DECKBOY_STATE_DIR=str(state), DECKBOY_PROJECT=str(show),
               DECKBOY_COMPANION_PORT=str(port), DECKBOY_HIDE_WINDOWS="1", DECKBOY_OUTPUT_RENDERER="direct3d11")
    log = (work / "app.log").open("w")
    app = subprocess.Popen([str(ROOT / "build/windows/Release/Deckboy.exe"), "--allow-multi-instance", str(show)],
                           env=env, cwd=ROOT, stdout=log, stderr=log)
    cmd = lambda line: c.command(port, line)
    failures = []

    def expect(ok, label):
        print(("ok   " if ok else "FAIL ") + label, flush=True)
        if not ok:
            failures.append(label)

    try:
        for _ in range(200):
            try:
                if cmd("PING").startswith("OK"):
                    break
            except OSError:
                time.sleep(0.3)
        listing = work / "drop.txt"
        listing.write_text(f"{clip}\n")
        cmd(f"DROPTEST {listing}")
        time.sleep(4)
        # The HDR picture is Output 1's; the recording mirrors it.
        print(cmd("OUTPUT HDR ON"), cmd("OUTPUT ON"))
        time.sleep(3)
        print(cmd("OUTPUT HDR STATUS"))
        print(cmd("RECORD ON"))
        time.sleep(2)
        cmd("TAKE 1")
        time.sleep(5)
        print(cmd("RECORD OFF"))
        time.sleep(4)
    finally:
        app.kill()
        app.wait(timeout=10)
        log.close()
    files = sorted(glob.glob(str(state / "recordings" / "*")) + glob.glob(str(root / "data" / "recordings" / "*")),
                   key=os.path.getmtime)
    expect(bool(files), "a recording was written")
    if files:
        rec = files[-1]
        probe = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                                "stream=codec_name,profile,pix_fmt,color_range,color_space,color_transfer,color_primaries",
                                "-of", "default=nw=1", rec], capture_output=True, text=True).stdout
        print("   " + probe.strip().replace("\n", "  "))
        expect("color_transfer=smpte2084" in probe and "color_primaries=bt2020" in probe and
               "color_space=bt2020nc" in probe and "10le" in probe, "tagged HDR10: BT.2020, PQ, 10-bit")
        # The last frame, as 10-bit codes.
        # The RAW 10-bit planes: decoding to a grey or 16-bit format makes
        # ffmpeg expand limited range to full, which read a correct file as
        # wrong (measured: every code off by exactly that stretch).
        raw = subprocess.run([ffmpeg, "-v", "error", "-i", rec, "-f", "rawvideo", "-pix_fmt", "yuv420p10le", "-"],
                             capture_output=True).stdout
        dims = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                               "stream=width,height", "-of", "csv=p=0", rec], capture_output=True, text=True).stdout
        w, h = map(int, dims.strip().split(",")[:2])
        per_frame = w * h * 3 // 2   # 10-bit samples in a 4:2:0 frame
        frames = len(raw) // (per_frame * 2)
        expect(frames > 0, f"the file decodes ({frames} frames)")
        last = np.frombuffer(raw, np.uint16)[(frames - 1) * per_frame:(frames - 1) * per_frame + w * h]
        y = last.reshape(h, w).astype(float)
        for i, (label, nits, _) in enumerate(ps[:8]):
            want = c.ycbcr10(c.pq_encode(nits))[0]
            cx, cy = int((i + 0.5) * w / 8), int(0.25 * h)
            got = float(y[cy - 4:cy + 4, cx - 4:cx + 4].mean())
            expect(abs(got - want) <= 2.5, f"{label}: luma code {got:.1f}, source carried {want}")
    print(f"hdr recording: {len(failures)} failures; evidence: {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
