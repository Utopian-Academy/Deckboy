"""Nested commands keep the caller's limits, and a command loop is an error, not a crash.

The 2026-10-19 audit (F1, F2) found the local-only check applied to the outer
verb only -- `DECK 1 TRUST x` from another machine changed the trust list, and
`DASH SET 1 Label|UISNAP <path>` (no spaces round the bar) stored and ran a
screenshot -- and that a dashboard slot reaching itself through `DECK 1 DASH 3`
overflowed the stack and closed the app mid-show.

Runs an isolated, hidden instance with dummy video and audio, trusts this
machine's LAN address, and talks to it from that address (a "network peer") and
from 127.0.0.1 (this computer).

    python tools/check_dispatch_guard.py [--exe build/windows/Release/Deckboy.exe]
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from check_control_trust import free_port, lan_address, session  # noqa: E402
from deckboy_testroot import populate  # noqa: E402

failures = 0


def expect(ok, what, reply=""):
    global failures
    print(("ok   " if ok else "FAIL ") + what + ("" if ok else f"  [{reply}]"), flush=True)
    if not ok:
        failures += 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = parser.parse_args()
    (ROOT / "build").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="dispatch-", dir=ROOT / "build"))
    populate(str(work))
    port = free_port()
    show = work / "data/default.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\nallow_remote_network\t1\n", encoding="utf-8")
    env = dict(os.environ, DECKBOY_ROOT=str(work), DECKBOY_STATE_DIR=str(work / "state"),
               DECKBOY_PROJECT=str(show), DECKBOY_COMPANION_PORT=str(port),
               SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", DECKBOY_RENDERER="software",
               DECKBOY_HIDE_WINDOWS="1")
    log = (work / "app.log").open("w", encoding="utf-8")
    proc = subprocess.Popen([args.exe, "--allow-multi-instance", str(show)], cwd=work, env=env,
                            stdout=log, stderr=log,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

    def send(host, text):
        return (session(host, port, [text], timeout=4) or ["NO_REPLY"])[0]

    try:
        until = time.monotonic() + 45
        while not send("127.0.0.1", "PING").startswith("OK"):
            if time.monotonic() > until:
                raise RuntimeError("app did not answer")
            time.sleep(0.2)
        local = lambda text: send("127.0.0.1", text)  # noqa: E731
        lan = lan_address()
        if not lan:
            print("SKIP no LAN address to act as another machine")
            return 0
        peer = lambda text: send(lan, text)  # noqa: E731
        expect(local("TRUST " + lan).startswith("OK"), "trust this machine's LAN address")

        # F1: local-only verbs, however they are wrapped.
        for command in ["TRUST 192.0.2.77", "DECK 1 TRUST 192.0.2.77", "DECK 1 DECK 1 TRUST 192.0.2.77"]:
            reply = peer(command)
            expect(reply.startswith("ERR") and "this computer" in reply,
                   f"another machine is refused: {command}", reply)
        trusted = local("TRUST LIST")
        expect("192.0.2.77" not in trusted, "the trust list did not change", trusted)

        snap = work / "compact-dash.bmp"
        for spacing in ["|", " | "]:
            reply = peer(f"DASH SET 1 Probe{spacing}UISNAP {snap}")
            expect(reply.startswith("ERR"), f"another machine cannot store UISNAP ('{spacing}')", reply)
        reply = peer(f"SCRIPTCUE ADD DECK 1 UISNAP {snap}")
        expect(reply.startswith("ERR"), "another machine cannot store a wrapped UISNAP in a script", reply)

        # Stored by this computer, fired from another: checked when it runs.
        expect(local(f"DASH SET 2 Snap | UISNAP {snap}").startswith("OK"),
               "this computer may store UISNAP in a slot")
        reply = peer("DASH 2")
        expect(reply.startswith("ERR"), "another machine firing that slot is refused", reply)
        time.sleep(0.5)
        expect(not snap.exists(), "no screenshot was written for another machine")

        # The ordinary things a controller does still work through wrappers.
        for command in ["MASTERVOL 80", "DECK 1 MASTERVOL 80"]:
            reply = peer(command)
            expect(reply.startswith("OK"), f"another machine may still: {command}", reply)

        # F2: loops are an error the app survives.
        reply = local("DASH SET 3 Loop | DASH 3")
        expect(reply.startswith("ERR"), "a slot that fires the dashboard directly is refused", reply)
        expect(local("DASH SET 3 Loop | DECK 1 DASH 3").startswith("OK"),
               "a slot that reaches itself through DECK can be stored (caught when run)")
        reply = local("DASH 3")
        expect(reply.startswith("ERR") and "nested" in reply, "firing that loop returns an error", reply)
        time.sleep(1.0)
        expect(proc.poll() is None, "the app is still running after the loop", proc.poll())
        reply = local("PING")
        expect(reply.startswith("OK"), "and still answers", reply)
        expect(local("DASH SET 4 Ok | DECK 1 MASTERVOL 90").startswith("OK") and local("DASH 4").startswith("OK"),
               "a slot wrapping DECK still works")
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=10)
        log.close()
    print(f"dispatch guard: {failures} failure(s); evidence: {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
