"""Deckboy and MediaMTX, both directions.

WHY THIS EXISTS. James, 2026-10-08, on MediaMTX: "ok let's try". Level 2:
the router's streams as one-click stream cues, and the programme out through
it to any number of viewers. This needs a MediaMTX running with its API on
(api: true) and something publishing to it; it checks:

  - MEDIAMTX lists what the router carries;
  - MEDIAMTX ADD makes a stream cue that, taken, puts a moving picture on the
    output (read from the presented frame, not from a reply);
  - MEDIAMTX PUBLISH makes the programme appear on the router as a stream.

    python tools/check_mediamtx.py --stream camera1
"""

import os
import struct
import sys
import tempfile
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, parse_args  # noqa: E402


def read_bmp_mean(path):
    data = open(path, "rb").read()
    off = struct.unpack_from("<I", data, 10)[0]
    px = data[off:]
    return sum(px[::97]) / max(1, len(px[::97]))


def main():
    argv = sys.argv
    stream = "camera1"
    if "--stream" in argv:
        i = argv.index("--stream")
        stream = argv[i + 1]
        del argv[i:i + 2]
    args = parse_args(__doc__, 5861)
    fails = []

    def note(name, ok, info=""):
        print("  %-50s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append(name)

    shots = tempfile.mkdtemp(prefix="deckboy-mtx-")
    with Deckboy(args, prefix="deckboy-mtx-") as db:
        print("check: MediaMTX in and out")
        print()
        reply = db.send("MEDIAMTX")
        note("MEDIAMTX lists the router's streams", stream + "(live)" in reply, reply)
        reply = db.send("MEDIAMTX ADD " + stream)
        note("MEDIAMTX ADD makes a stream cue", reply.startswith("OK"), reply)
        db.send("SELECT 1")
        db.send("TAKE")
        print("  output:", db.send("OUTPUT ON"))
        time.sleep(6.0)
        means = []
        for n in range(2):
            path = os.path.join(shots, "f%d.bmp" % n)
            db.send("OUTSNAP " + path)
            for _ in range(40):
                if os.path.exists(path) and os.path.getsize(path) > 1000:
                    break
                time.sleep(0.1)
            time.sleep(0.2)
            means.append(read_bmp_mean(path) if os.path.exists(path) else 0)
            time.sleep(1.0)
        note("the stream's picture is on the output", means and min(means) > 20, "mean levels %r" % means)
        reply = db.send("MEDIAMTX PUBLISH deckboy")
        note("MEDIAMTX PUBLISH answers with where to watch", "8888/deckboy" in reply, reply)
        ready = False
        for _ in range(30):
            try:
                body = urllib.request.urlopen("http://127.0.0.1:9997/v3/paths/get/deckboy", timeout=2).read().decode()
                if '"ready":true' in body:
                    ready = True
                    break
            except Exception:
                pass
            time.sleep(0.5)
        note("the programme is live on MediaMTX", ready)
        db.send("OUTPUT OFF")
    print()
    print("%d FAILED" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
