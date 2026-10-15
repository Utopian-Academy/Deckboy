"""Who may control Deckboy over the network: the trusted-controller list, the
Companion passphrase and the verbs only this computer may use.

Runs the real app headless with "Listen on: all interfaces" and every control
port on, then connects to THIS machine's own LAN address, which arrives with a
non-loopback source exactly as another machine's connection would.
Evidence: build/control-trust-*/
"""
import argparse
import re
import os
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

from deckboy_testroot import populate

ROOT = Path(__file__).resolve().parent.parent


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def lan_address():
    # No packet is sent: connecting a UDP socket only picks the route.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        try:
            sock.connect(("192.0.2.1", 9))
            address = sock.getsockname()[0]
        except OSError:
            return None
    return None if address.startswith("127.") else address


def session(host, port, lines, timeout=4):
    """Send lines on one connection; return the reply lines, or None if refused."""
    try:
        sock = socket.create_connection((host, port), timeout=timeout)
    except OSError:
        return None
    replies = []
    with sock:
        sock.settimeout(timeout)
        buffer = b""
        for line in lines:
            try:
                sock.sendall((line + "\n").encode())
            except OSError:
                return replies or None
            while b"\n" not in buffer:
                try:
                    chunk = sock.recv(65536)
                except OSError:
                    chunk = b""
                if not chunk:
                    return replies or None
                buffer += chunk
            reply, buffer = buffer.split(b"\n", 1)
            replies.append(reply.decode(errors="replace").strip())
    return replies


def first_bytes(host, port, timeout=3):
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            sock.settimeout(timeout)
            return sock.recv(200)
    except OSError:
        return b""


def http_status(url, timeout=3):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return response.status
    except Exception:
        return None


def http_reply(url, timeout=3):
    """(status, body), or (None, why) if nothing answered at all."""
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return response.status, response.read().decode(errors="replace")
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode(errors="replace")
    except Exception as error:
        return None, repr(error)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = parser.parse_args()
    lan = lan_address()
    if not lan:
        print("SKIP: this machine has no non-loopback IPv4 address to test from")
        return 0
    (ROOT / "build").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="control-trust-", dir=ROOT / "build"))
    populate(str(work))
    ports = {name: free_port() for name in ("companion", "hyperdeck", "vmix_http", "vmix_tcp", "osc", "web")}
    show = work / "data/default.deckboy"
    show.write_text(
        "update_check\t0\nui_sounds\t0\nallow_remote_network\t1\nhyperdeck_enabled\t1\n"
        f"vmix_api_enabled\t1\nvmix_http_port\t{ports['vmix_http']}\nvmix_tcp_port\t{ports['vmix_tcp']}\n"
        f"osc_query_enabled\t1\nosc_query_port\t{ports['osc']}\n"
        f"web_monitor_enabled\t1\nweb_monitor_port\t{ports['web']}\nweb_monitor_share\t1\n"
        "web_monitor_pin\tremote-test-4826\nweb_remote_enabled\t1\n", encoding="utf-8")
    env = dict(os.environ, DECKBOY_ROOT=str(work), DECKBOY_STATE_DIR=str(work / "state"),
               DECKBOY_PROJECT=str(show), DECKBOY_COMPANION_PORT=str(ports["companion"]),
               DECKBOY_HYPERDECK_PORT=str(ports["hyperdeck"]),
               SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", DECKBOY_RENDERER="software")
    log = (work / "app.log").open("w", encoding="utf-8")
    app = subprocess.Popen([args.exe, "--allow-multi-instance", str(show)], env=env, cwd=ROOT,
                           stdout=log, stderr=log, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    checks = failures = 0

    def expect(condition, label, detail=""):
        nonlocal checks, failures
        checks += 1
        if not condition:
            failures += 1
        print(("ok   " if condition else "FAIL ") + label + (f"  [{detail}]" if detail and not condition else ""),
              flush=True)

    local = lambda *lines: session("127.0.0.1", ports["companion"], list(lines))
    remote = lambda *lines: session(lan, ports["companion"], list(lines))
    try:
        until = time.monotonic() + 60
        while not (local("PING") or [""])[0].startswith("OK"):
            if time.monotonic() > until:
                raise AssertionError("app did not answer on loopback")
            time.sleep(0.3)
        time.sleep(1.0)
        snap = work / "remote-snap.bmp"

        # 1. Nobody trusted yet: every port refuses the LAN address.
        expect(remote("PING") is None, "Companion TCP refuses an untrusted machine")
        expect(first_bytes(lan, ports["hyperdeck"]) == b"", "HyperDeck refuses an untrusted machine")
        expect(http_status(f"http://{lan}:{ports['vmix_http']}/api") is None, "vMix HTTP refuses an untrusted machine")
        expect(http_status(f"http://{lan}:{ports['osc']}/") is None, "OSC query refuses an untrusted machine")
        # Shared on the LAN, the Web Monitor listens on this PC's LAN address
        # only, so the phone remote is reached there, as a phone would.
        web_on = local("WEBMONITOR ON")[0]
        web_host = (re.search(r"http://([\d.]+):", web_on) or [None, None])[1]
        expect(web_host is not None, "the Web Monitor is shared on the LAN", web_on)
        phone_url = f"http://{web_host}:{ports['web']}/remote"
        until = time.monotonic() + 20
        while http_reply(phone_url)[0] is None and time.monotonic() < until:
            time.sleep(0.3)
        status, body = http_reply(phone_url)
        expect(status == 403 and "not a trusted controller" in body,
               "the phone remote refuses an untrusted machine", f"{status} {body.strip()}")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.sendto(b"COMPANIONPASS changedbyudp\n", (lan, ports["companion"]))
        time.sleep(0.5)
        expect(local("COMPANIONPASS")[0].endswith("off"), "Companion UDP from an untrusted machine is ignored",
               str(local("COMPANIONPASS")))
        expect(first_bytes("127.0.0.1", ports["hyperdeck"]).startswith(b"500"), "this computer is always trusted")
        time.sleep(0.5)
        last = local("TRUST LAST")[0]
        expect(last.startswith("OK") and lan in last, "TRUST LAST trusts the machine just refused", last)

        # 2. Trusted: everything answers, but the local-only verbs do not.
        expect((remote("PING") or [""])[0].startswith("OK"), "a trusted machine is answered")
        expect(first_bytes(lan, ports["hyperdeck"]).startswith(b"500"), "HyperDeck answers a trusted machine")
        expect(http_status(f"http://{lan}:{ports['osc']}/") == 200, "OSC query answers a trusted machine")
        local(f"TRUST {web_host}")
        status, body = http_reply(phone_url)
        expect(status == 200 and "<" in body, "the phone remote answers a trusted machine", f"{status} {body[:80]}")
        vmix = http_status(f"http://{lan}:{ports['vmix_http']}/api")
        expect(vmix is not None, "vMix HTTP answers a trusted machine", str(vmix))
        reply = (remote(f"UISNAP {snap}") or [""])[0]
        time.sleep(1.0)
        expect(reply.startswith("ERR") and "this computer" in reply and not snap.exists(),
               "UISNAP from another machine is refused and writes nothing", reply)
        for line in ("CLICK 10 10", "PRESS OFF", "DROPTEST C:/Windows/win.ini", "TRUST 10.9.9.9",
                     "COMPANIONPASS letmeinplease", "SUBTITLE CONVERT C:/x.srt",
                     "BROWSER file:///C:/Windows/win.ini", "DASH SET 4 Snap | UISNAP C:/x.bmp"):
            reply = (remote(line) or [""])[0]
            expect(reply.startswith("ERR") and "this computer" in reply,
                   f"'{line.split()[0]} {line.split()[1] if len(line.split()) > 1 else ''}' refused from another machine", reply)
        expect("10.9.9.9" not in local("TRUST LIST")[0], "a trusted machine cannot widen the list")
        expect((local(f"UISNAP {snap}") or [""])[0].startswith("OK"), "the same UISNAP works from this computer")

        # 3. Companion passphrase.
        expect(local("COMPANIONPASS short")[0].startswith("ERR"), "a passphrase under 8 characters is refused")
        expect(local("COMPANIONPASS correct-horse")[0].startswith("OK"), "COMPANIONPASS sets it from this computer")
        replies = remote("PING", "STATUS", "AUTH wrong-horse", "AUTH correct-horse", "PING")
        expect(replies is not None and len(replies) == 5, "passphrase session completes", str(replies))
        if replies and len(replies) == 5:
            expect(replies[0].startswith("ERR AUTH"), "commands before AUTH are refused", replies[0])
            expect(replies[1].startswith("ERR AUTH"), "STATUS before AUTH is refused", replies[1])
            expect(replies[2] == "ERR AUTH: wrong passphrase", "a wrong passphrase is refused", replies[2])
            expect(replies[3] == "OK AUTH", "the right passphrase is accepted", replies[3])
            expect(replies[4].startswith("OK"), "commands after AUTH are answered", replies[4])
        expect((local("PING") or [""])[0].startswith("OK"), "this computer needs no passphrase")
        expect((local("AUTH anything") or [""])[0] == "OK AUTH", "AUTH from this computer is accepted harmlessly")
        flood = remote(*["AUTH nope"] * 5, "PING")
        expect(flood is None or len(flood) <= 5, "five wrong passphrases end the session", str(flood))
        expect(local("COMPANIONPASS OFF")[0].startswith("OK"), "COMPANIONPASS OFF")

        # 4. Removing the trust takes effect at once.
        expect(local(f"TRUST REMOVE {lan}")[0].startswith("OK"), "TRUST REMOVE")
        expect(remote("PING") is None, "a removed machine is refused again")
        expect(local("TRUST 10.0.0.0/8")[0].startswith("OK") and local("TRUST 0.0.0.0/0")[0].startswith("ERR"),
               "ranges accepted, the whole internet refused")
        logs = list(work.rglob("deckboy-show.log"))
        text = logs[0].read_text(encoding="utf-8", errors="replace") if logs else ""
        expect("CONTROL-REFUSED" in text and lan in text, "refusals are written to the show log")
    finally:
        app.kill()
        app.wait(timeout=10)
        log.close()
    print(f"control trust: {checks} checks, {failures} failures; evidence: {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
