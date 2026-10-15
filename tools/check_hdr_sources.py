"""What an SDR output does with HDR10 (PQ) and HLG sources, measured.

Makes two clips whose signal is known exactly, plays each on a real output
(D3D11 by default, every window hidden: DECKBOY_HIDE_WINDOWS) and compares the
output's own frame (OUTSNAP) with the correct SDR rendition:

  - a grey ladder at known luminances: 0.5, 10, 50, 100, 203, 400, 1000, 4000 nits
  - the EBU 75% bars placed at the BT.2408 reference white (203 nits)

The correct SDR rendition of reference white is SDR white, so the bars must
come back as the plain 75% bars (191/0) and everything up to 203 nits on its
BT.1886 curve. Above 203 nits an SDR output has to clip or compress: the
report says which it did. A measurement, not a gate: it prints a table and
keeps the clips, snapshots and numbers in build/hdr-sources-*/.
"""
import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import numpy as np
from PIL import Image

from deckboy_testroot import populate

ROOT = Path(__file__).resolve().parent.parent
W, H = 1280, 720
NITS = [0.5, 10, 50, 100, 203, 400, 1000, 4000]
EBU75 = [(191, 191, 191), (191, 191, 0), (0, 191, 191), (0, 191, 0),
         (191, 0, 191), (191, 0, 0), (0, 0, 191), (0, 0, 0)]
REF_WHITE = 203.0
# BT.709 -> BT.2020 primaries, linear light (ITU-R BT.2087)
M709_2020 = np.array([[0.6274, 0.3293, 0.0433], [0.0691, 0.9195, 0.0114], [0.0164, 0.0880, 0.8956]])
K2020 = (0.2627, 0.6780, 0.0593)


def pq_encode(nits):
    m1, m2, c1, c2, c3 = 2610 / 16384, 2523 / 4096 * 128, 3424 / 4096, 2413 / 4096 * 32, 2392 / 4096 * 32
    y = np.clip(np.asarray(nits, dtype=float) / 10000.0, 0, 1) ** m1
    return ((c1 + c2 * y) / (1 + c3 * y)) ** m2


def hlg_encode_display(rgb_nits):
    """Display light (nits, BT.2020) on a 1000-nit HLG display -> HLG signal."""
    fd = np.clip(np.asarray(rgb_nits, dtype=float) / 1000.0, 0, 1)
    yd = max(1e-9, K2020[0] * fd[0] + K2020[1] * fd[1] + K2020[2] * fd[2])
    ys = yd ** (1 / 1.2)
    scene = fd * (ys ** -0.2) if yd > 1e-9 else fd  # inverse OOTF: Fd = Ys^(1.2-1) * Es
    a, b, c = 0.17883277, 0.28466892, 0.55991073
    scene = np.clip(scene, 0, 1)
    return np.where(scene <= 1 / 12, np.sqrt(3 * scene), a * np.log(np.maximum(12 * scene - b, 1e-9)) + c)


def rolloff(x, knee=0.9):
    """core/hdr_to_sdr.hpp's highlight roll-off, in units of reference white."""
    if x <= knee:
        return x
    room = 1.0 - knee
    return knee + room * (1.0 - np.exp(-(x - knee) / room))


def patches():
    """(name, BT.2020 linear display light in nits, expected SDR 8-bit RGB)."""
    out = []
    for n in NITS:
        rel = rolloff(n / REF_WHITE)
        out.append((f"grey {n:g} nits", np.array([n, n, n], dtype=float), tuple([round(255 * rel ** (1 / 2.4))] * 3)))
    for rgb in EBU75:
        lin709 = (np.array(rgb) / 255.0) ** 2.4
        nits2020 = M709_2020 @ lin709 * REF_WHITE
        out.append((f"bar {rgb}", nits2020, rgb))
    return out


def ycbcr10(signal_rgb):
    r, g, b = signal_rgb
    y = K2020[0] * r + K2020[1] * g + K2020[2] * b
    cb = (b - y) / (2 * (1 - K2020[2]))
    cr = (r - y) / (2 * (1 - K2020[0]))
    return (round(64 + 876 * y), round(512 + 896 * cb), round(512 + 896 * cr))


def make_clip(ffmpeg, path, transfer):
    ps = patches()
    yp = np.zeros((H, W), np.uint16)
    up = np.zeros((H // 2, W // 2), np.uint16)
    vp = np.zeros((H // 2, W // 2), np.uint16)
    cols = 8
    for i, (_, nits, _) in enumerate(ps):
        row, col = divmod(i, cols)
        sig = pq_encode(nits) if transfer == "pq" else hlg_encode_display(nits)
        y, cb, cr = ycbcr10(sig)
        x0, x1 = col * W // cols, (col + 1) * W // cols
        y0, y1 = row * H // 2, (row + 1) * H // 2
        yp[y0:y1, x0:x1] = y
        up[y0 // 2:y1 // 2, x0 // 2:x1 // 2] = cb
        vp[y0 // 2:y1 // 2, x0 // 2:x1 // 2] = cr
    frame = yp.tobytes() + up.tobytes() + vp.tobytes()
    trc = "smpte2084" if transfer == "pq" else "arib-std-b67"
    # The tags must reach the BITSTREAM (x265's VUI), not only the container:
    # without them the first version of this check measured an untagged clip
    # as if it were HDR. ffprobe below refuses a clip that lost them.
    # NOT lossless: hardware decoders (d3d11va here) mis-decode HEVC lossless
    # mode, and real HDR material is never lossless. qp 2 on flat patches
    # lands within a code value.
    x265 = f"qp=2:log-level=error:colorprim=bt2020:transfer={trc}:colormatrix=bt2020nc:range=limited"
    subprocess.run([ffmpeg, "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "yuv420p10le", "-s", f"{W}x{H}",
                    # The INPUT tagged the same as the output, or ffmpeg takes the raw
                    # planes for BT.601 and converts them to the tagged BT.2020 on the
                    # way in -- which bent every colour patch (measured: Y 498 -> 509).
                    "-r", "25", "-color_primaries", "bt2020", "-color_trc", trc, "-colorspace", "bt2020nc",
                    "-color_range", "tv", "-i", "pipe:0", "-t", "6", "-c:v", "libx265", "-x265-params", x265,
                    "-pix_fmt", "yuv420p10le", "-color_primaries", "bt2020", "-color_trc", trc,
                    "-colorspace", "bt2020nc", "-color_range", "tv", "-tag:v", "hvc1", str(path)],
                   input=frame * 150, check=True)
    probe = subprocess.run([str(Path(ffmpeg).with_name(Path(ffmpeg).name.replace("ffmpeg", "ffprobe"))), "-v", "error",
                            "-select_streams", "v:0", "-show_entries", "stream=color_transfer,color_primaries",
                            "-of", "default=nw=1", str(path)], capture_output=True, text=True).stdout
    if f"color_transfer={trc}" not in probe or "color_primaries=bt2020" not in probe:
        raise RuntimeError(f"{path.name} lost its HDR tags: {probe.strip()}")
    return ps


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def command(port, line, timeout=20):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.sendall((line + "\n").encode())
        data = b""
        while b"\n" not in data:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
        return data.decode().strip()


def measure(snap, ps):
    img = np.asarray(Image.open(snap).convert("RGB"), dtype=float)
    h, w, _ = img.shape
    got = []
    for i in range(len(ps)):
        row, col = divmod(i, 8)
        cx, cy = int((col + 0.5) * w / 8), int((row + 0.5) * h / 2)
        r = max(4, w // 64)
        got.append(tuple(int(round(v)) for v in img[cy - r:cy + r, cx - r:cx + r].reshape(-1, 3).mean(axis=0)))
    return got


def run(exe, renderer, clips, work):
    port = free_port()
    state = work / f"state-{renderer}"
    root = work / f"root-{renderer}"
    populate(str(root))
    show = root / "data/default.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\n", encoding="utf-8")
    env = dict(os.environ, DECKBOY_ROOT=str(root), DECKBOY_STATE_DIR=str(state), DECKBOY_PROJECT=str(show),
               DECKBOY_COMPANION_PORT=str(port), DECKBOY_HIDE_WINDOWS="1", DECKBOY_OUTPUT_RENDERER=renderer,
               DECKBOY_EGRESS_READBACK="sync")
    log = (work / f"app-{renderer}.log").open("w", encoding="utf-8")
    app = subprocess.Popen([exe, "--allow-multi-instance", str(show)], env=env, cwd=ROOT, stdout=log, stderr=log)
    results = {}
    try:
        until = time.monotonic() + 60
        while True:
            try:
                if command(port, "PING").startswith("OK"):
                    break
            except OSError:
                pass
            if time.monotonic() > until:
                raise RuntimeError("Deckboy did not answer")
            time.sleep(0.3)
        listing = work / f"drop-{renderer}.txt"
        listing.write_text("".join(f"{c}\n" for c in clips.values()), encoding="utf-8")
        command(port, f"DROPTEST {listing}")
        time.sleep(4)
        command(port, "VIDEO 720P")
        command(port, "OUTPUT ON")
        time.sleep(2)
        for n, name in enumerate(clips, start=1):
            command(port, f"TAKE {n}")
            time.sleep(3)
            command(port, "PAUSE")
            time.sleep(1)
            snap = work / f"{renderer}-{name}.bmp"
            reply = command(port, f"OUTSNAP {snap} 1")
            time.sleep(2)
            results[name] = (snap, reply)
    finally:
        app.kill()
        app.wait(timeout=10)
        log.close()
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    parser.add_argument("--renderers", default="direct3d11,software")
    args = parser.parse_args()
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        parser.error("ffmpeg is required")
    (ROOT / "build").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="hdr-sources-", dir=ROOT / "build"))
    clips, ps = {}, None
    for transfer in ("pq", "hlg"):
        path = work / f"ladder-{transfer}.mov"
        ps = make_clip(ffmpeg, path, transfer)
        clips[transfer] = path
    failed = 0
    for renderer in args.renderers.split(","):
        results = run(args.exe, renderer, clips, work)
        for name, (snap, reply) in results.items():
            print(f"\n== {renderer} / {name.upper()}  ({reply})")
            if not snap.exists():
                print("   FAIL no snapshot")
                failed += 1
                continue
            got = measure(snap, ps)
            worst = 0
            for (label, _, want), have in zip(ps, got):
                err = max(abs(a - b) for a, b in zip(have, want))
                worst = max(worst, err)
                print(f"   {label:28s} want {str(want):16s} got {str(have):16s} {'' if err <= 6 else ' <-- ' + str(err)}")
            # 14 code values: what 10-bit encoding plus 4:2:0 leave on a channel
            # that should be zero (about 0.05% light, invisible). The faults this
            # found were 60 (HLG dim) and 255 (black).
            ok = worst <= 14
            failed += 0 if ok else 1
            print(f"   {'ok  ' if ok else 'FAIL'} worst error: {worst}")
    print(f"\nevidence: {work}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
