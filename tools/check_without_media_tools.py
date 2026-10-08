"""The desk keeps its artwork, and says so plainly, when ffmpeg cannot run.

WHY THIS EXISTS. 2026-10-08, a Mac on Ventura: the bundled ffmpeg and ffprobe
were built for macOS 14 and could not start there. Deckboy opened with its
text but every icon blank -- the desk's own artwork was decoded by running
those tools -- and nothing said why. Now the artwork decodes in-process and a
startup check reports tools that cannot run.

This starts Deckboy with ffmpeg and ffprobe present but unable to run, then checks the
render log says they cannot run, and that the desk's artwork still loaded
(counted by the app: UIASSETS).

    python tools/check_without_media_tools.py
"""

import os
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, parse_args  # noqa: E402


def main():
    args = parse_args(__doc__, 5897)
    fails = []

    def note(name, ok, info=""):
        print("  %-52s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append(name)

    # PRESENT BUT UNABLE TO RUN, which is what the Ventura Mac had: a file
    # named for each tool that is not a program. On Windows the pinned-tool
    # variables win over every search location; elsewhere PATH is searched,
    # so the broken copies go first on it.
    broken = tempfile.mkdtemp(prefix="deckboy-broken-tools-")
    for name in ("ffmpeg", "ffprobe", "ffmpeg.exe", "ffprobe.exe"):
        path = os.path.join(broken, name)
        with open(path, "w") as f:
            f.write("#!/bin/sh\necho 'dyld: Symbol not found: _AVCaptureDeviceTypeExternal' >&2\nexit 134\n")
        os.chmod(path, 0o755)
    saved_path = os.environ.get("PATH", "")
    os.environ["PATH"] = broken + os.pathsep + saved_path
    os.environ["DECKBOY_FFMPEG"] = os.path.join(broken, "ffmpeg.exe" if os.name == "nt" else "ffmpeg")
    os.environ["DECKBOY_FFPROBE"] = os.path.join(broken, "ffprobe.exe" if os.name == "nt" else "ffprobe")
    # NO DECKBOY_TEST_NO_ALERTS: the warning must be up while the desk keeps
    # answering. A modal system dialog here once froze the whole app.
    print("check: media tools present but unable to run")
    print()
    try:
        with Deckboy(args, prefix="deckboy-no-ffmpeg-") as db:
            log = os.path.join(db.root, "data", "deckboy-render.log")
            line = ""
            for _ in range(40):
                if os.path.exists(log):
                    found = [l for l in open(log, encoding="utf-8", errors="replace") if "media tools:" in l]
                    if found:
                        line = found[-1].strip()
                        break
                time.sleep(0.25)
            note("the startup check reports the tools cannot run", "CANNOT RUN" in line, line[:160])
            snap = os.path.join(db.root, "desk.bmp")
            db.send("UISNAP " + snap)
            for _ in range(40):
                if os.path.exists(snap) and os.path.getsize(snap) > 1000:
                    break
                time.sleep(0.1)
            status = db.send("STATUS")
            note("the desk answers with the warning up", status.startswith("DECKBOY"), status[:80])
            # The artwork: the in-process decoder logs nothing, so ask the app
            # how many of its UI images loaded (the --self-check style count).
            reply = db.send("UIASSETS")
            loaded = int(reply.split("loaded=")[1].split()[0]) if "loaded=" in reply else -1
            note("the desk's artwork loaded without ffmpeg", loaded > 0, reply[:120])
    finally:
        os.environ["PATH"] = saved_path
        for var in ("DECKBOY_FFMPEG", "DECKBOY_FFPROBE"):
            os.environ.pop(var, None)
    print()
    print("%d FAILED" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
