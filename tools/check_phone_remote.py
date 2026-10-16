#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
"""Exercise /remote on the real app, isolated and with no visible windows.

Optional --playwright and --browser run the phone/tablet browser checks too.
All evidence is retained under build/phone-remote-*.
"""
import argparse
import concurrent.futures
from http.client import HTTPConnection
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
from urllib.parse import quote

from deckboy_testroot import populate

ROOT = Path(__file__).resolve().parent.parent
PIN = "remote-test-4826"


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def command(port, line):
    with socket.create_connection(("127.0.0.1", port), timeout=8) as sock:
        sock.sendall((line + "\n").encode())
        result = b""
        while b"\n" not in result:
            chunk = sock.recv(65536)
            if not chunk:
                raise ConnectionError("Control socket closed without a reply")
            result += chunk
        return result.decode().strip()


# Phone control listens on the private LAN address alone (never a VPN's), so
# once it is on the check follows the server there: PHONE STATUS names it.
HOST = {"name": "127.0.0.1"}


def http(port, path="/remote/state", body="", pin=PIN, method="POST", headers=None):
    conn = HTTPConnection(HOST["name"], port, timeout=8)
    try:
        fields = {"X-Deckboy-Pin": quote(pin, safe=""), "Content-Type": "text/plain"}
        fields.update(headers or {})
        conn.request(method, path, body, fields)
        response = conn.getresponse()
        return response.status, response.read().decode(), dict(response.getheaders())
    finally:
        conn.close()


def wait_for(fn, seconds=15):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        try:
            result = fn()
            if result:
                return result
        except (OSError, ValueError):
            pass
        time.sleep(.15)
    raise AssertionError("Timed out waiting for app state")


def bitmap(path):
    pixels = bytes([64, 128, 192] * 16 * 16)
    path.write_bytes(b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54) +
                     struct.pack("<IiiHHIIiiII", 40, 16, 16, 1, 24, 0, len(pixels), 0, 0, 0, 0) + pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    parser.add_argument("--playwright", help="Path to installed playwright index.mjs")
    parser.add_argument("--browser", help="Headless Chromium executable")
    parser.add_argument("--native-ui", action="store_true", help="Also expose Network settings at 150% for screenshots")
    args = parser.parse_args()
    (ROOT / "build").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="phone-remote-", dir=ROOT / "build"))
    populate(str(work))
    media = work / "media"
    media.mkdir()
    for name in ("Alpha", "Beta"):
        bitmap(media / (name + ".bmp"))
    show = work / "data/default.deckboy"
    show.write_text("web_monitor_low_latency\t1\nupdate_check\t0\nui_scale\t1.5\n", encoding="utf-8")
    port, web = free_port(), free_port()
    env = dict(os.environ, DECKBOY_ROOT=str(work), DECKBOY_STATE_DIR=str(work / "state"),
               DECKBOY_PROJECT=str(show), DECKBOY_COMPANION_PORT=str(port),
               SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", DECKBOY_RENDERER="software")
    log = (work / "app.log").open("w", encoding="utf-8")
    ui_args = ["--settings", "2", "--ui-size", "2640", "1530", "--settings-scroll", "1100",
               "--audit-settings-layout"] if args.native_ui else []
    app = subprocess.Popen([str(Path(args.exe).resolve()), "--allow-multi-instance", str(show),
                            "--import", str(media), *ui_args], env=env, cwd=ROOT,
                           stdout=log, stderr=log, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    checks = 0

    def expect(condition, message):
        nonlocal checks
        assert condition, message
        checks += 1
        print("PASS", message, flush=True)

    def control(line, ok=True):
        result = command(port, line)
        expect(result.startswith("OK " if ok else "ERR "), line.split(" PIN")[0] + " reply")
        return result

    def state():
        code, body, _ = http(web)
        assert code == 200, (code, body)
        return json.loads(body)

    def send(line, code=200):
        status, body, _ = http(web, "/remote/command", line)
        expect(status == code and body.startswith("OK " if code == 200 else "ERR "), line + " HTTP reply")
        return body

    try:
        wait_for(lambda: command(port, "PING").startswith("OK"), 60)
        control("OUTPUT OFF")
        control("WEBMONITOR CONTROL ON", ok=False)
        control(f"WEBMONITOR PORT {web}")
        control("WEBMONITOR ON")
        wait_for(lambda: http(web, "/remote", method="GET")[0] == 200)
        expect(http(web)[0] == 403, "control disabled by default")
        control("WEBMONITOR PIN " + PIN)
        control("WEBMONITOR CONTROL ON")
        phone = command(port, "PHONE STATUS")
        if "http://" in phone:
            HOST["name"] = phone.split("http://", 1)[1].split(":", 1)[0]
        if args.native_ui:
            # Centre of Phone control in the fixed 2640x1530 / 150% viewport,
            # visually checked against the native screenshots below.
            # Off takes the server off the network, so ask it from this computer.
            lan = HOST["name"]
            control("CLICK 2001 971")
            HOST["name"] = "127.0.0.1"
            wait_for(lambda: http(web, "/remote", method="GET")[0] == 200)
            expect(http(web)[0] == 403, "native Phone control click revokes access")
            control("CLICK 2001 971")
            HOST["name"] = lan
            wait_for(lambda: http(web, "/remote", method="GET")[0] == 200)
            expect(http(web)[0] == 200, "native Phone control click restores access")
        expect(http(web, pin="wrong")[0] == 401, "wrong PIN refused")
        expect(http(web, pin="")[0] == 401, "missing PIN refused")
        expect(http(web, pin="", path="/remote/state?pin=" + PIN)[0] != 200, "URL PIN never authenticates remote")
        expect(http(web, method="GET")[0] == 405, "GET cannot read control state")
        expect(http(web, method="OPTIONS")[0] == 405, "cross-origin preflight refused")
        expect(http(web, headers={"Origin": "http://example.invalid"})[0] == 403, "foreign Origin refused")
        expect(http(web, headers={"Host": "example.invalid"})[0] == 403, "rebound DNS host refused")
        expect(http(web, headers={"Sec-Fetch-Site": "cross-site"})[0] == 403, "cross-site browser request refused")
        expect(http(web, headers={"Origin": f"http://{HOST['name']}:{web}"})[0] == 200, "same-origin request accepted")
        expect(http(web, "/remote/command", "DECK 1 GO", pin="wrong")[0] == 401,
               "unauthenticated command receives an error without a connection reset")
        for line in ("GO", "DECK 1 GO\nSTOP", "DECK 1 SELECT 1junk", "DASH SET 1 x | GO", "SET theme dark"):
            send(line, 400)
        expect(http(web, "/remote/command", "x" * 129)[0] == 413, "oversized command refused")
        # Duplicate headers and incomplete bodies are received through the real socket parser.
        with socket.create_connection((HOST["name"], web), timeout=8) as sock:
            sock.sendall((f"POST /remote/command HTTP/1.1\r\nHost: {HOST['name']}:{web}\r\n"
                          f"X-Deckboy-Pin: {PIN}\r\nContent-Length: 9\r\nContent-Length: 9\r\n\r\nDECK 1 GO").encode())
            expect(b"400 Bad Request" in sock.recv(4096), "duplicate Content-Length refused")
        with socket.create_connection((HOST["name"], web), timeout=8) as sock:
            sock.sendall((f"POST /remote/command HTTP/1.1\r\nHost: {HOST['name']}:{web}\r\n"
                          f"X-Deckboy-Pin: {PIN}\r\nContent-Length: 9\r\n\r\nDECK").encode())
            started = time.monotonic()
            expect(b"400 Bad Request" in sock.recv(4096) and time.monotonic() - started < 4,
                   "incomplete body times out without executing")
        control("DECK 1 SELECT 1")
        awkward = 'Alpha <script>alert(1)</script> & "quoted"'
        control("RENAME " + awkward)
        wait_for(lambda: len(state()["decks"][0]["cues"]) == 2)
        wait_for(lambda: state()["decks"][0]["cues"][0]["name"] == awkward)
        expect(awkward in [c["name"] for c in state()["decks"][0]["cues"]], "cue names round-trip as JSON")
        send("DECK 1 SELECT 1")
        send("DECK 1 NEXT")
        wait_for(lambda: state()["decks"][0]["selected"] == 2)
        expect(True, "NEXT changes the real selected cue")
        send("DECK 1 GO")
        wait_for(lambda: state()["decks"][0]["active"] == 2)
        expect(True, "GO takes the real selected cue")
        send("DECK 1 STOP")
        wait_for(lambda: state()["decks"][0]["status"].lower() != "playing")
        expect(True, "STOP stops the real transport")
        send("DECK 99 GO", 409)
        send("DECK 1 SELECT 999", 409)
        control("NEWDECK")
        send("DECK 2 STOP")
        wait_for(lambda: state()["focus"] == 2 and len(state()["decks"]) == 2)
        expect(True, "explicit deck commands update the intended desk focus")
        send("DECK 1 SELECT 2")
        control("DASH SET 1 Select Alpha | DECK 1 SELECT 1")
        control("DASH SET 2 Invalid deck | DECK 99 GO")
        control("DASH SET 3 Invalid verb | NOT_A_DECKBOY_COMMAND")
        send("DASH 1")
        wait_for(lambda: state()["decks"][0]["selected"] == 1)
        send("DASH 2", 409)
        send("DASH 3", 409)
        expect("remote-test" not in json.dumps(state()), "state contains no PIN")
        control("WEBMONITOR PIN changed-5937")
        expect(http(web)[0] == 401, "PIN rotation revokes old PIN immediately")
        expect(http(web, pin="changed-5937")[0] == 200, "new PIN accepted")
        control("WEBMONITOR PIN " + PIN)
        control("WEBMONITOR PIN accent-caf\u00e9-%25")
        expect(http(web, pin="accent-caf\u00e9-%25")[0] == 200, "Unicode and percent passphrase round-trip")
        control("WEBMONITOR PIN " + PIN)
        control("WEBMONITOR PIN OFF", ok=False)
        lan = HOST["name"]
        control("WEBMONITOR CONTROL OFF")
        # Off, phone control takes the server off the network (the monitor here
        # is "this computer"), and the desk itself is refused control.
        HOST["name"] = "127.0.0.1"
        wait_for(lambda: http(web, "/remote", method="GET")[0] == 200)
        expect(http(web)[0] == 403, "disabling control revokes access")
        control("WEBMONITOR CONTROL ON")
        HOST["name"] = lan
        wait_for(lambda: http(web, "/remote", method="GET")[0] == 200)
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            results = list(pool.map(lambda _: http(web), range(9)))
        expect(all(r[0] == 200 for r in results), "concurrent tablet state requests")
        for _ in range(5):
            http(web, pin="incorrect")
        expect(http(web)[0] == 429, "repeated incorrect PINs are throttled")
        time.sleep(2.1)
        for _ in range(5):
            http(web, pin="incorrect")
        time.sleep(2.1)
        expect(http(web)[0] == 429, "a second lockout lasts longer than the first")
        time.sleep(2.1)
        expect(http(web)[0] == 200, "PIN throttle recovers")
        if args.playwright and args.browser:
            subprocess.run(["node", str(ROOT / "tools/check_phone_remote_ui.mjs"),
                            args.playwright, args.browser, str(web), str(port), str(work), HOST["name"]],
                           cwd=ROOT, check=True, timeout=150)
            expect(True, "headless phone and tablet UI checks")
        # A DASH action may stop its own listener. The waiting HTTP thread must
        # let shutdown join it, and the app must remain responsive afterwards.
        control("DASH SET 4 Disable monitor | WEBMONITOR OFF")
        started = time.monotonic()
        http(web, "/remote/command", "DASH 4")
        control("PING")
        expect(time.monotonic() - started < 6, "DASH can stop its own listener without deadlock")
        control("WEBMONITOR ON")
        wait_for(lambda: http(web)[0] == 200)
        expect(True, "remote reconnects after listener restart")
        print(f"phone-remote: {checks} checks, 0 failures; evidence: {work}", flush=True)
    finally:
        app.terminate()
        try:
            app.wait(timeout=10)
        except subprocess.TimeoutExpired:
            app.kill()
            app.wait()
        log.close()


if __name__ == "__main__":
    main()
