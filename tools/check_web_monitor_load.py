#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
"""The web server under load: what a show's network actually does to it.

Phone control must keep answering while the room watches the Web Monitor,
while a browser holds idle preconnections, after garbage and a thousand quick
connections; the desk must keep answering Companion meanwhile, must not leak
threads, and turning the server off must not freeze the desk behind a viewer
that stopped reading. Runs the real app, isolated, with no visible windows.
"""
import argparse
import os
from pathlib import Path
import random
import socket
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from deckboy_testroot import populate  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
PIN = "load-test-48261"


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def command(port, line, timeout=8):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.sendall((line + "\n").encode())
        result = b""
        while b"\n" not in result:
            chunk = sock.recv(65536)
            if not chunk:
                raise ConnectionError("no reply")
            result += chunk
        return result.decode().strip()


def http(host, port, path, method="POST", body="", timeout=5):
    """One request; returns (status, seconds) or (None, seconds) on failure."""
    t0 = time.time()
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            head = (f"{method} {path} HTTP/1.1\r\nHost: {host}:{port}\r\nX-Deckboy-Pin: {PIN}\r\n"
                    f"Content-Type: text/plain\r\nContent-Length: {len(body)}\r\nConnection: close\r\n\r\n")
            sock.sendall(head.encode() + body.encode())
            sock.settimeout(timeout)
            data = b""
            while b"\r\n" not in data:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                data += chunk
            line = data.split(b"\r\n", 1)[0].decode(errors="replace")
            return (int(line.split()[1]) if len(line.split()) > 1 else None), time.time() - t0
    except OSError:
        return None, time.time() - t0


def threads_of(pid):
    out = subprocess.run(["powershell", "-NoProfile", "-Command", f"(Get-Process -Id {pid}).Threads.Count"],
                         capture_output=True, text=True).stdout.strip()
    return int(out) if out.isdigit() else -1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    parser.add_argument("--viewers", type=int, default=12)
    args = parser.parse_args()
    (ROOT / "build").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="web-load-", dir=ROOT / "build"))
    populate(str(work))
    show = work / "data/default.deckboy"
    show.write_text("update_check\t0\nui_scale\t1.0\n", encoding="utf-8")
    port, web = free_port(), free_port()
    env = dict(os.environ, DECKBOY_ROOT=str(work), DECKBOY_STATE_DIR=str(work / "state"),
               DECKBOY_PROJECT=str(show), DECKBOY_COMPANION_PORT=str(port),
               SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", DECKBOY_RENDERER="software")
    log = (work / "app.log").open("w", encoding="utf-8")
    app = subprocess.Popen([str(Path(args.exe).resolve()), "--allow-multi-instance", str(show)], env=env, cwd=ROOT,
                           stdout=log, stderr=log, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    results = []
    held = []

    def check(ok, message, detail=""):
        results.append(bool(ok))
        print(("PASS " if ok else "FAIL ") + message + (("  [" + detail + "]") if detail else ""), flush=True)

    try:
        for _ in range(120):
            try:
                if command(port, "PING", 2).startswith("OK"):
                    break
            except OSError:
                time.sleep(0.5)
        command(port, "OUTPUT OFF")
        command(port, f"WEBMONITOR PORT {web}")
        command(port, "WEBMONITOR PIN " + PIN)
        command(port, "WEBMONITOR SHARE ON")
        command(port, "WEBMONITOR ON")
        command(port, "WEBMONITOR CONTROL ON")
        phone = command(port, "PHONE STATUS")
        host = phone.split("http://", 1)[1].split(":", 1)[0] if "http://" in phone else "127.0.0.1"
        for _ in range(40):
            if http(host, web, "/remote/state")[0] == 200:
                break
            time.sleep(0.25)
        check(http(host, web, "/remote/state")[0] == 200, "phone control answers", host)
        threads_before = threads_of(app.pid)

        # 1. The room watches: more viewers than the limit, every one held open.
        def viewer():
            try:
                s = socket.create_connection((host, web), timeout=5)
                s.sendall(f"GET /out/1?pin={PIN} HTTP/1.1\r\nHost: x\r\n\r\n".encode())
                held.append(s)
            except OSError:
                pass
        for _ in range(args.viewers):
            viewer()
        time.sleep(1.5)
        streaming = 0
        for s in held:
            try:
                s.settimeout(3)
                first = s.recv(64)
                streaming += first.startswith(b"HTTP/1.1 200")
            except OSError:
                pass
        check(streaming == min(8, args.viewers), "viewers past the limit (8) are refused", "%d streaming" % streaming)
        code, secs = http(host, web, "/remote/state")
        check(code == 200, "phone control answers while %d viewers watch" % args.viewers, "%s in %.2fs" % (code, secs))
        code, secs = http(host, web, f"/?pin={PIN}", method="GET")
        check(code in (200, 503), "the monitor page answers while viewers watch", "%s in %.2fs" % (code, secs))
        for s in held:
            s.close()
        held.clear()
        time.sleep(1.5)

        # 2. Idle preconnections (a browser opens them and sends nothing).
        for _ in range(24):
            try:
                held.append(socket.create_connection((host, web), timeout=5))
            except OSError:
                pass
        for s in held[:12]:
            try:
                s.sendall(b"GET / HTTP/1.1\r\n")   # half a request, then silence
            except OSError:
                pass
        time.sleep(0.5)
        code, secs = http(host, web, "/remote/state")
        check(code == 200 and secs < 1.5, "phone control answers past 24 idle connections", "%s in %.2fs" % (code, secs))
        for s in held:
            s.close()
        held.clear()

        # 3. Garbage.
        junk = [b"\x00\xff" * 500, b"GET " + b"/" * 20000 + b" HTTP/1.1\r\n\r\n", b"BREW /pot HTTP/1.1\r\n\r\n",
                b"GET /out/99999999999999999999 HTTP/1.1\r\n\r\n", b"GET /out/-1 HTTP/1.1\r\n\r\n",
                b"GET /web/../../VERSION HTTP/1.1\r\n\r\n", b"GET /remote/../outputs HTTP/1.1\r\n\r\n",
                b"POST /remote/command HTTP/1.1\r\nContent-Length: 999999999\r\n\r\nGO",
                b"POST /remote/command HTTP/1.1\r\nContent-Length: -5\r\n\r\n", b"GET /hls/1/../../x HTTP/1.1\r\n\r\n",
                b"\r\n\r\n", b"GET\r\n\r\n", b"GET / HTTP/1.1\r\n" + b"X: " + b"a" * 30000 + b"\r\n\r\n"]
        rng = random.Random(7)
        for _ in range(4):
            for j in junk + [bytes(rng.randrange(256) for _ in range(rng.randrange(1, 3000)))]:
                try:
                    with socket.create_connection((host, web), timeout=3) as s:
                        s.sendall(j)
                        s.settimeout(2)
                        try:
                            s.recv(4096)
                        except OSError:
                            pass
                except OSError:
                    pass
        check(http(host, web, "/remote/state")[0] == 200, "phone control answers after garbage")

        # 4. A thousand quick connections, some cut mid-request.
        def churn(n):
            for k in range(n):
                try:
                    s = socket.create_connection((host, web), timeout=3)
                    if k % 3 == 0:
                        s.sendall(b"GET /snap/1 HTTP/1.1\r\n\r\n")
                    elif k % 3 == 1:
                        s.sendall(b"GET /out/1 HT")
                    s.close()
                except OSError:
                    pass
        workers = [threading.Thread(target=churn, args=(125,)) for _ in range(8)]
        t0 = time.time()
        for w in workers:
            w.start()
        worst = 0.0
        while any(w.is_alive() for w in workers):
            c0 = time.time()
            command(port, "STATUS")
            worst = max(worst, time.time() - c0)
            time.sleep(0.05)
        for w in workers:
            w.join()
        check(worst < 1.0, "Companion keeps answering through 1000 connections", "worst %.2fs, %.1fs total" % (worst, time.time() - t0))
        time.sleep(6)
        code, secs = http(host, web, "/remote/state")
        check(code == 200, "phone control answers after the churn", "%s in %.2fs" % (code, secs))
        threads_after = threads_of(app.pid)
        check(threads_after <= threads_before + 4, "no threads left behind", "%d -> %d" % (threads_before, threads_after))

        # 5. A viewer that stops reading, then the server is turned off.
        stuck = socket.socket()
        stuck.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        stuck.connect((host, web))
        stuck.sendall(f"GET /out/1?pin={PIN} HTTP/1.1\r\nHost: x\r\n\r\n".encode())
        held.append(stuck)
        time.sleep(4)   # its buffers fill with placeholder pictures
        t0 = time.time()
        command(port, "WEBMONITOR OFF")
        command(port, "WEBMONITOR CONTROL OFF")
        off = time.time() - t0
        check(off < 1.0, "turning the server off does not wait on a stuck viewer", "%.2fs" % off)
        command(port, "WEBMONITOR ON")
        command(port, "WEBMONITOR CONTROL ON")
        for _ in range(40):
            if http(host, web, "/remote/state")[0] == 200:
                break
            time.sleep(0.25)
        check(http(host, web, "/remote/state")[0] == 200, "and comes back")

        # 6. Guessing the passphrase on the monitor counts against phone control.
        for k in range(5):
            http(host, web, "/?pin=wrong-%d" % k, method="GET")
        code, _ = http(host, web, "/remote/state")
        check(code == 429, "five wrong passphrases on the monitor lock this address out of phone control", str(code))
        code, _ = http(host, web, f"/?pin={PIN}", method="GET")
        check(code == 429, "and out of the monitor, right passphrase or not", str(code))
        time.sleep(2.5)
        code, _ = http(host, web, "/remote/state")
        check(code == 200, "the lockout passes", str(code))
    finally:
        for s in held:
            try:
                s.close()
            except OSError:
                pass
        try:
            command(port, "QUIT", 3)
        except OSError:
            pass
        try:
            app.wait(10)
        except subprocess.TimeoutExpired:
            app.kill()
        log.close()
    results += moving_address(args.exe)
    print("%d/%d passed" % (sum(results), len(results)))
    return 0 if all(results) else 1


def moving_address(exe):
    """The server follows this computer's address: started before there is a
    network it comes up when one appears, and it moves when the address
    changes. The address comes from a file (DECKBOY_TEST_LAN_ADDRESS_FILE)."""
    work = Path(tempfile.mkdtemp(prefix="web-move-", dir=ROOT / "build"))
    populate(str(work))
    show = work / "data/default.deckboy"
    port, web = free_port(), free_port()
    address_file = work / "lan.txt"
    address_file.write_text("", encoding="utf-8")   # no network yet
    show.write_text("update_check\t0\nweb_remote_enabled\t1\nweb_monitor_port\t%d\nweb_monitor_pin\t%s\n" % (web, PIN),
                    encoding="utf-8")
    env = dict(os.environ, DECKBOY_ROOT=str(work), DECKBOY_STATE_DIR=str(work / "state"),
               DECKBOY_PROJECT=str(show), DECKBOY_COMPANION_PORT=str(port), DECKBOY_TEST_LAN_ADDRESS_FILE=str(address_file),
               SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", DECKBOY_RENDERER="software")
    log = (work / "app.log").open("w", encoding="utf-8")
    app = subprocess.Popen([str(Path(exe).resolve()), "--allow-multi-instance", str(show)], env=env, cwd=ROOT,
                           stdout=log, stderr=log, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    out = []

    def check(ok, message, detail=""):
        out.append(bool(ok))
        print(("PASS " if ok else "FAIL ") + message + (("  [" + detail + "]") if detail else ""), flush=True)

    def answers(host, seconds):
        end = time.time() + seconds
        while time.time() < end:
            if http(host, web, "/remote/state", timeout=1)[0] == 200:
                return True
            time.sleep(0.25)
        return False
    try:
        for _ in range(120):
            try:
                if command(port, "PING", 2).startswith("OK"):
                    break
            except OSError:
                time.sleep(0.5)
        check(not answers("127.0.0.2", 2), "no network: phone control waits")
        address_file.write_text("127.0.0.2", encoding="utf-8")
        check(answers("127.0.0.2", 8), "a network appears: phone control comes up on it")
        address_file.write_text("127.0.0.3", encoding="utf-8")
        check(answers("127.0.0.3", 8), "the address changes: phone control moves to the new one")
        check(not answers("127.0.0.2", 1), "and has left the old one")
        address_file.write_text("", encoding="utf-8")
        time.sleep(4)
        check(answers("127.0.0.3", 2), "the network blinks off: phone control stays where it was")
    finally:
        try:
            command(port, "QUIT", 3)
        except OSError:
            pass
        try:
            app.wait(10)
        except subprocess.TimeoutExpired:
            app.kill()
        log.close()
    return out


if __name__ == "__main__":
    sys.exit(main())
