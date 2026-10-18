"""Graphics over an HDR feed, out over ST 2110-20 as HDR, measured on the wire.

check_hdr_feed.py's chain with the output leaving as ST 2110-20 instead of an
SRT stream: an HDR SRT feed (PQ, or HLG with --hlg) into a stream cue, a white
graphic on a playlist layered over it, Output 1 running HDR with ST 2110 aimed
at a UDP socket here. The RTP is depacketised (one-SRD 10-bit 4:2:2 pgroups,
as St2110Output sends them) and read back:
  - the SDP the app gives says TCS=PQ (or HLG) and colorimetry=BT2100, 10-bit
  - every grey of the feed's ladder at the code it was sent with (within 4)
  - the graphic's white on the 203-nit code: PQ 573, HLG 721
Every window hidden: this checks the signal, not the frame rate.
Evidence: build/hdr-2110-*/
"""
import shutil
import socket
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
W, H = 1920, 1080
BOX = (0.625, 0.60, 0.875, 0.90)


def main():
    hlg = "--hlg" in sys.argv
    transfer = "hlg" if hlg else "pq"
    ffmpeg = shutil.which("ffmpeg")
    work = Path(tempfile.mkdtemp(prefix="hdr-2110-", dir=ROOT / "build"))
    clip = work / f"{transfer}.mov"
    ps = c.make_clip(ffmpeg, clip, transfer)
    graphic = work / "graphic.png"
    from PIL import Image
    rgba = np.zeros((720, 1280, 4), np.uint8)
    rgba[int(BOX[1] * 720):int(BOX[3] * 720), int(BOX[0] * 1280):int(BOX[2] * 1280)] = 255
    Image.fromarray(rgba, "RGBA").save(graphic)
    root = work / "root"
    populate(str(root))
    state = work / "state"
    show = root / "data/default.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\n", encoding="utf-8")
    failures = []

    def expect(ok, label):
        print(("ok   " if ok else "FAIL ") + label, flush=True)
        if not ok:
            failures.append(label)

    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    receiver.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 64 * 1024 * 1024)
    receiver.bind(("127.0.0.1", 0))
    rtp_port = receiver.getsockname()[1]
    in_port = c.free_port()
    sender = subprocess.Popen([ffmpeg, "-v", "error", "-re", "-stream_loop", "-1", "-i", str(clip), "-c", "copy",
                               "-f", "mpegts", f"srt://127.0.0.1:{in_port}?mode=listener"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    port = c.free_port()
    log = (work / "app.log").open("w")
    app = launch(root, state, show, port, log)
    rows = {}
    want_rows = {int(0.25 * H), int((BOX[1] + BOX[3]) / 2 * H)}
    sdp = ""
    try:
        say = lambda line: print(f"  {line} -> {c.command(port, line)}")
        say(f"STREAM ADD srt://127.0.0.1:{in_port}?mode=caller")
        say(f"STREAM HDR {'HLG' if hlg else 'PQ'}")
        say("OUTPUT SELECT 1")
        say("OUTPUT HDR ON")
        if hlg:
            say("OUTPUT HDR SIGNAL HLG")
        say("OUTPUT ON")
        say("VIDEO 1080P")
        say("TAKE 1")
        time.sleep(3)
        say("DECKADD")
        say("DECK 2")
        listing = work / "drop.txt"
        listing.write_text(f"{graphic}\n")
        say(f"DROPTEST {listing}")
        time.sleep(2)
        say("VIDEO OUTPUT ASSIGN")
        say("TAKE 1")
        say("OUTPUT SELECT 1")
        say("ST2110 ADDR 127.0.0.1")
        say(f"ST2110 PORT {rtp_port}")
        say("ST2110 DEPTH 10")
        say("ST2110 ON")
        time.sleep(4)
        sdp = c.command(port, "ST2110 SDP")
        # Two markers apart is one whole frame; keep the rows that are measured.
        receiver.settimeout(2.0)
        markers = 0
        deadline = time.time() + 20
        while markers < 2 and time.time() < deadline:
            try:
                p = receiver.recv(2048)
            except socket.timeout:
                break
            if len(p) < 20:
                continue
            if markers == 1:
                row = ((p[16] & 0x7F) << 8) | p[17]
                if row in want_rows:
                    length = (p[14] << 8) | p[15]
                    offset = ((p[18] & 0x7F) << 8) | p[19]
                    rows.setdefault(row, {})[offset] = bytes(p[20:20 + length])
            if p[1] & 0x80:
                markers += 1
    finally:
        app.kill()
        app.wait(timeout=10)
        sender.kill()
        log.close()
        receiver.close()

    # ST2110 SDP prints the SDP to the app's stdout, for a scripted receiver.
    sdp = "\n".join(l for l in (work / "app.log").read_text(errors="replace").splitlines()
                    if l.startswith("a=fmtp"))
    print("   SDP: " + sdp[:220])
    tcs = "TCS=HLG" if hlg else "TCS=PQ"
    expect(tcs in sdp and "colorimetry=BT2100" in sdp and "depth=10" in sdp,
           f"SDP: {tcs}, colorimetry=BT2100, depth=10")
    expect(all(r in rows for r in want_rows), f"a whole frame's measured rows arrived ({len(rows)} of {len(want_rows)})")

    def luma_row(row):
        segments = rows.get(row, {})
        y = np.zeros(W, float)
        for offset, data in segments.items():
            b = np.frombuffer(data, np.uint8).reshape(-1, 5).astype(np.uint64)
            bits = (b[:, 0] << 32) | (b[:, 1] << 24) | (b[:, 2] << 16) | (b[:, 3] << 8) | b[:, 4]
            y0 = (bits >> np.uint64(20)) & np.uint64(1023)
            y1 = bits & np.uint64(1023)
            n = len(bits)
            y[offset:offset + 2 * n:2] = y0[:max(0, min(n, (W - offset + 1) // 2))]
            y[offset + 1:offset + 2 * n:2] = y1[:max(0, min(n, (W - offset) // 2))]
        return y

    encode = (lambda n: c.ycbcr10(c.hlg_encode_display(np.asarray(n, dtype=float)))[0]) if hlg \
        else (lambda n: c.ycbcr10(c.pq_encode(n))[0])
    if int(0.25 * H) in rows:
        y = luma_row(int(0.25 * H))
        for i, (label, nits, _) in enumerate(ps[:8]):
            cx = int((i + 0.5) / 8 * W)
            got = float(y[cx - 4:cx + 4].mean())
            want = encode(nits)
            expect(abs(got - want) <= 4, f"feed {label}: code {got:.1f}, sent {want}")
    box_row = int((BOX[1] + BOX[3]) / 2 * H)
    if box_row in rows:
        y = luma_row(box_row)
        cx = int((BOX[0] + BOX[2]) / 2 * W)
        got = float(y[cx - 4:cx + 4].mean())
        white = encode(np.full(3, 203.0))
        expect(abs(got - white) <= 4, f"graphic white at 203 nits: code {got:.1f}, should be {white}")
    print(f"hdr 2110: {len(failures)} failures; evidence: {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
