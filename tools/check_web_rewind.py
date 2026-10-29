#!/usr/bin/env python3
"""The Web Monitor's PAUSE, BACK 10 S and LIVE: two minutes kept, and a way back.

The server keeps two minutes of the programme (James, 2026-10-29: as much
memory as that needs) and lists all of it in dvr.m3u8, the monitor page's own
playlist. index.m3u8, the OPEN STREAM link, stays the short live window an
outside player expects. Both must end at the same fragment, and the oldest one
dvr.m3u8 names must still be there to fetch.

With --browser it also drives the real page in headless Edge (or Chrome) over
the DevTools protocol: PAUSE leaves live and holds, BACK 10 S goes ten seconds
back and stays there (hls.js must not pull it forward), LIVE returns to the
edge. That needs the websocket-client package and a browser, so CI runs only
the first part.

    py -3 tools/check_web_rewind.py --exe build/windows/Release/Deckboy.exe [--browser]
"""
import argparse
import json
import random
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

fails = []


def note(name, ok, info=""):
    print("  %-62s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def segments(text):
    return [line for line in text.splitlines() if line and not line.startswith("#")]


def fetch(url, timeout=10):
    with urlopen(url, timeout=timeout) as reply:
        return reply.status, reply.read()


class Browser:
    """Just enough of the DevTools protocol to load a page and run script in it."""

    def __init__(self, url):
        import websocket  # websocket-client
        candidates = [
            r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
            r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
            r"C:\Program Files\Google\Chrome\Application\chrome.exe",
            "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser",
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        ]
        exe = next((c for c in candidates if Path(c).exists()), None)
        if not exe:
            raise RuntimeError("no Edge or Chrome here")
        self.port = free_port()
        self.profile = tempfile.mkdtemp(prefix="deckboy-rewind-browser-")
        self.proc = subprocess.Popen([exe, "--headless=new", "--mute-audio", "--no-first-run",
                                      "--autoplay-policy=no-user-gesture-required",
                                      "--remote-debugging-port=%d" % self.port,
                                      "--user-data-dir=" + self.profile, url],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.monotonic() + 20
        target = None
        while time.monotonic() < deadline and not target:
            try:
                pages = json.loads(fetch("http://127.0.0.1:%d/json/list" % self.port)[1])
                target = next((p for p in pages if p.get("type") == "page"), None)
            except OSError:
                time.sleep(0.3)
        if not target:
            raise RuntimeError("the browser did not open its page")
        self.ws = websocket.create_connection(target["webSocketDebuggerUrl"], timeout=20)
        self.next_id = 0

    def eval(self, expression):
        self.next_id += 1
        self.ws.send(json.dumps({"id": self.next_id, "method": "Runtime.evaluate",
                                 "params": {"expression": expression, "returnByValue": True}}))
        while True:
            reply = json.loads(self.ws.recv())
            if reply.get("id") == self.next_id:
                return reply.get("result", {}).get("result", {}).get("value")

    def close(self):
        try:
            self.ws.close()
        finally:
            self.proc.terminate()
            shutil.rmtree(self.profile, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--browser", action="store_true", help="also drive the page in headless Edge/Chrome")
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: the Web Monitor's pause and rewind\n")

    work = Path(tempfile.mkdtemp(prefix="deckboy-web-rewind-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "02-running-a-show.deckboy"
    web = random.SystemRandom().randrange(31000, 39000)
    with Runtime(exe, work / "run", show) as db:
        for line in ("OUTPUT ON", "VIDEO 640x360", "WEBMONITOR PORT %d" % web, "WEBMONITOR ON",
                     "SELECT 2", "TAKE"):
            db.send(line)
        base = "http://127.0.0.1:%d" % web
        page = ""
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            try:
                page = fetch(base + "/")[1].decode("utf-8", "replace")
                if "data-source" in page:
                    break
            except OSError:
                pass
            time.sleep(0.5)
        match = re.search(r"data-source='(/hls/\d+/)dvr\.m3u8([^']*)'", page)
        note("the page's own player reads dvr.m3u8", bool(match), page[:200])
        note("the page has PAUSE, BACK 10 S and LIVE",
             all(b in page for b in ("id='pause'", "id='back'", "id='live'")))
        if not match:
            return 1
        prefix, query = match.group(1), match.group(2)
        time.sleep(20.0)   # let some programme pile up behind the live edge
        index = fetch(base + prefix + "index.m3u8" + query)[1].decode()
        dvr = fetch(base + prefix + "dvr.m3u8" + query)[1].decode()
        short, long = segments(index), segments(dvr)
        print("    index.m3u8 lists %d fragments, dvr.m3u8 %d" % (len(short), len(long)))
        note("index.m3u8 stays the short live window", 1 <= len(short) <= 12, len(short))
        note("dvr.m3u8 keeps what came before it", len(long) >= 30, len(long))
        note("both end at the same live edge", short and long and short[-1] == long[-1],
             (short[-1:], long[-1:]))
        status, body = fetch(base + prefix + long[0].split("/")[-1] if not long[0].startswith("/") else base + long[0])
        note("the oldest fragment dvr.m3u8 names is still there", status == 200 and len(body) > 1000, status)

        if args.browser:
            print()
            browser = Browser(base + "/")
            try:
                def video(expression):
                    return browser.eval("(function(){var v=document.getElementById('v');return %s;})()" % expression)
                playing = False
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline and not playing:
                    time.sleep(0.5)
                    playing = bool(video("!v.paused && v.readyState >= 3 && v.currentTime > 1"))
                note("the page plays the programme", playing, video("[v.paused, v.readyState, v.currentTime]"))
                time.sleep(2.0)
                browser.eval("document.getElementById('pause').click()")
                time.sleep(0.5)
                held = video("v.currentTime")
                time.sleep(3.0)
                note("PAUSE holds the picture and leaves live",
                     video("v.paused") and abs(video("v.currentTime") - held) < 0.05 and
                     browser.eval("window.deckboyMonitorAtLive()") is False,
                     (video("v.paused"), held, video("v.currentTime")))
                browser.eval("document.getElementById('live').click()")
                time.sleep(3.0)
                behind = browser.eval("(function(){var v=document.getElementById('v');var r=v.seekable;"
                                      "return r.length ? r.end(r.length-1) - v.currentTime : -1;})()")
                note("LIVE goes back to the edge, playing",
                     not video("v.paused") and browser.eval("window.deckboyMonitorAtLive()") is True and
                     0 <= behind < 6, (video("v.paused"), behind))
                before = video("v.currentTime")
                browser.eval("document.getElementById('back').click()")
                time.sleep(4.0)
                behind = browser.eval("(function(){var v=document.getElementById('v');var r=v.seekable;"
                                      "return r.length ? r.end(r.length-1) - v.currentTime : -1;})()")
                print("    after BACK 10 S and four seconds: %.1f s behind the edge" % behind)
                note("BACK 10 S goes back and stays back, playing",
                     not video("v.paused") and 9 <= behind <= 16 and
                     browser.eval("window.deckboyMonitorAtLive()") is False, (before, behind))
                label = browser.eval("document.getElementById('live').textContent")
                note("LIVE says how far behind it is", label.startswith("GO LIVE  -0:1"), label)
                browser.eval("document.getElementById('live').click()")
                time.sleep(3.0)
                behind = browser.eval("(function(){var v=document.getElementById('v');var r=v.seekable;"
                                      "return r.length ? r.end(r.length-1) - v.currentTime : -1;})()")
                note("and LIVE returns to the edge again", 0 <= behind < 6 and not video("v.paused"), behind)
            finally:
                browser.close()

    shutil.rmtree(work, ignore_errors=True)
    print()
    if fails:
        print("web rewind: %d FAILED: %s" % (len(fails), ", ".join(fails)))
        return 1
    print("web rewind: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
