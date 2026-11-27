#!/usr/bin/env python3
"""Check Pocket's phone LFOs over 127.0.0.1:5510.

Run only after the other Pocket session has stopped. This script owns the
Pocket process it starts, backs up APPDATA/Deckboy/Pocket/state before launch,
and restores that directory after the process exits, including on failure.
It never connects to an already running Pocket. Hardware and permission
dialogs still need an Android check; injection deliberately avoids the mic.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile
import time


ADDRESS = ("127.0.0.1", 5510)
ROOT = Path(__file__).resolve().parents[1]


def command(text):
    with socket.create_connection(ADDRESS, timeout=3) as client:
        client.sendall((text + "\n").encode())
        data = b""
        while b"\n" not in data:
            block = client.recv(65536)
            if not block:
                raise RuntimeError("Pocket closed the connection")
            data += block
        result = data.decode()
        if result.startswith("ERR"):
            raise RuntimeError(f"{text}: {result.strip()}")
        return result


def check():
    command("SENSORS TILT 0.5")
    command("VOLUME 0")
    command("ADD swirl")
    # A fresh state has no cues; use STATUS's count even if a media folder
    # supplied cues at startup. ADD selects and appends the new generator.
    status = command("STATUS")
    if "cues=" not in status:
        # STATUS spans several lines, unlike every other reply we use.
        with socket.create_connection(ADDRESS, timeout=3) as client:
            client.sendall(b"STATUS\n")
            status = ""
            while "\nOUTPUT " not in status:
                block = client.recv(65536)
                if not block:
                    raise RuntimeError("Incomplete STATUS")
                status += block.decode()
    cue = int(re.search(r"\bcues=(\d+)", status)[1])
    command(f"SET {cue} SPEED 0")
    command(f"SET {cue} END HOLD 0")
    command(f"TAKE {cue}")
    command("BLACK OFF")
    for source in ("TILT", "TURN", "SHAKE", "MIC"):
        command(f"LFO {cue} SPEED ON {source} 0.25 50")
        for value in (0, 0.25, 0.75, 1):
            reply = command(f"SENSORS {source} {value}")
            assert f"{source.lower()}={value}" in reply, reply
            time.sleep(0.1)
            reply = command(f"VALUE {cue} SPEED")
            actual = float(reply.split("=", 1)[1])
            # Speed spans -3..3. Tilt/turn center on the set number;
            # shake/mic rise from it, like the sound envelope.
            expected = 3 * (value - 0.5 if source in ("TILT", "TURN") else value)
            assert abs(actual - expected) < 0.001, (source, value, actual, expected)
        command(f"LFO {cue} SPEED OFF")
        assert abs(float(command(f"VALUE {cue} SPEED").split("=", 1)[1])) < 0.001
        print(f"PASS {source}: four injected values and disabled LFO")
    assert "recording=0" in command("SENSORS"), "Injection opened the microphone"
    for invalid in ("TILT -1", "MIC 1.1", "TURN nan", "SHAKE 0.5 extra"):
        try:
            command("SENSORS " + invalid)
        except RuntimeError:
            pass
        else:
            raise AssertionError(f"Accepted invalid injection: {invalid}")
    command("STOP")
    assert "mode=live" in command("SENSORS LIVE")
    print("PASS validation, microphone isolation and LIVE reset")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/windows/Release/deckboy-pocket.exe")
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    try:
        with socket.create_connection(ADDRESS, timeout=0.3):
            raise RuntimeError("Port 5510 is occupied. Stop the other session before running this check.")
    except (ConnectionRefusedError, TimeoutError):
        pass
    state = (Path(os.environ["APPDATA"]) / "Deckboy/Pocket/state").resolve()
    state.parent.mkdir(parents=True, exist_ok=True)
    # Moving within the same parent preserves every original file, including
    # settings and credentials. Restore only after our child can no longer save.
    backup_root = Path(tempfile.mkdtemp(prefix="sensor-check-", dir=state.parent))
    backup = backup_root / "state"
    existed = state.exists()
    process = None
    moved = False
    created = False
    try:
        if existed:
            state.rename(backup)
            moved = True
        state.mkdir()
        created = True
        env = os.environ.copy()
        env["DECKBOY_STATE_DIR"] = str(state)
        with (backup_root / "pocket.log").open("w") as log:
            process = subprocess.Popen([str(exe)], cwd=ROOT, env=env, stdout=log, stderr=log)
            deadline = time.monotonic() + 45
            while True:
                if process.poll() is not None:
                    raise RuntimeError("Pocket exited before opening its remote port")
                try:
                    command("PING")
                    break
                except OSError:
                    if time.monotonic() >= deadline:
                        raise RuntimeError("Pocket did not open port 5510")
                    time.sleep(0.2)
            check()
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        # Never remove any other path, and keep the backup if restore fails.
        if created and state.exists():
            shutil.rmtree(state)
        if moved and backup.exists():
            backup.rename(state)
        print(f"State restored. Test log: {backup_root / 'pocket.log'}")


if __name__ == "__main__":
    main()
