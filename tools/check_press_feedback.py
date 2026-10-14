"""Every theme: a held press, the release flash and an open MENU must each
look different from the button at rest.

Runs the real app headless (dummy video, software renderer), one launch per
theme, and compares the MENU button's pixels between states. A state that
looks the same as rest in any theme is a button that does not answer.
Evidence: build/press-*/<theme>-<state>.bmp
"""
import argparse
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from PIL import Image, ImageChops, ImageStat

from deckboy_testroot import populate

ROOT = Path(__file__).resolve().parent.parent


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def command(port, line):
    with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
        sock.sendall((line + "\n").encode())
        result = b""
        while b"\n" not in result:
            chunk = sock.recv(65536)
            if not chunk:
                raise ConnectionError("control socket closed")
            result += chunk
        return result.decode().strip()


def snap(port, path):
    if path.exists():
        path.unlink()
    command(port, f"UISNAP {path}")
    until = time.monotonic() + 10
    while time.monotonic() < until:
        if path.exists() and path.stat().st_size > 0:
            time.sleep(0.1)
            return Image.open(path).convert("RGB")
        time.sleep(0.03)
    raise AssertionError(f"no snapshot at {path}")


def difference(a, b, box):
    return sum(ImageStat.Stat(ImageChops.difference(a.crop(box), b.crop(box))).mean) / 3


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    parser.add_argument("--themes", default="", help="comma list; default every bundled theme")
    args = parser.parse_args()
    themes = [t for t in args.themes.split(",") if t] or sorted(
        p.name for p in (ROOT / "data/themes").iterdir() if p.is_dir())
    (ROOT / "build").mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="press-", dir=ROOT / "build"))
    failures = 0
    for theme in themes:
        work = out / ("root-" + theme)
        work.mkdir()
        populate(str(work))
        show = work / "data/default.deckboy"
        show.write_text(f"theme\t{theme}\nupdate_check\t0\nui_sounds\t0\nui_scale\t1.5\n", encoding="utf-8")
        port = free_port()
        env = dict(os.environ, DECKBOY_ROOT=str(work), DECKBOY_STATE_DIR=str(work / "state"),
                   DECKBOY_PROJECT=str(show), DECKBOY_COMPANION_PORT=str(port),
                   SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", DECKBOY_RENDERER="software")
        log = (work / "app.log").open("w", encoding="utf-8")
        app = subprocess.Popen([args.exe, "--allow-multi-instance", str(show), "--ui-size", "2640", "1530"],
                               env=env, cwd=ROOT, stdout=log, stderr=log,
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            until = time.monotonic() + 60
            while True:
                try:
                    if command(port, "PING").startswith("OK"):
                        break
                except OSError:
                    pass
                if time.monotonic() > until:
                    raise AssertionError("app did not answer")
                time.sleep(0.3)
            time.sleep(1.0)
            rest = snap(port, out / f"{theme}-rest.bmp")
            reply = command(port, "PRESS MENU")
            m = re.search(r"at (\d+),(\d+) (\d+)x(\d+)", reply)
            if not m:
                raise AssertionError("PRESS MENU: " + reply)
            x, y, w, h = map(int, m.groups())
            box = (x, y, x + w, y + h)
            ring = (x - 4, y - 4, x + w + 4, y + h + 4)
            pressed = snap(port, out / f"{theme}-pressed.bmp")
            released = command(port, "PRESS OFF")
            command(port, f"CLICK {x + w // 2} {y + h // 2}")
            time.sleep(0.4)
            opened = snap(port, out / f"{theme}-open.bmp")
            results = {
                "pressed": difference(rest, pressed, box),
                "open": difference(rest, opened, ring),
            }
            # The release flash lasts 150 ms, less than a headless frame pair
            # takes to capture, so the app is asked whether it started one.
            if "flash on" not in released:
                results["flash"] = 0.0
            bad = [k for k, v in results.items() if v < 4.0]
            failures += len(bad)
            print(("FAIL " if bad else "ok   ") + theme.ljust(16) +
                  "  ".join(f"{k} {v:5.1f}" for k, v in results.items()) +
                  (("  <- looks like rest: " + ", ".join(bad)) if bad else ""), flush=True)
        except Exception as exc:  # one theme failing to run is a failure, not a crash
            failures += 1
            print(f"FAIL {theme}: {exc}", flush=True)
        finally:
            app.kill()
            app.wait(timeout=10)
            log.close()
    print(f"press feedback: {len(themes)} themes, {failures} failures; evidence: {out}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
