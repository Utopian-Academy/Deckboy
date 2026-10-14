"""Exercise Mini's terminal-only containment while a real clip keeps playing.

All state, media and logs stay in a new build/ directory. SDL dummy drivers
prevent windows or sound on the operator's desktop. No third-party Python
packages are required. See the private handoff for the deliberately unlisted
test override; it never appears in HELP or published documentation.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def wait_for(predicate, timeout=12):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(0.05)
    raise AssertionError("timed out waiting for " + repr(predicate))


def complete_bmp(path):
    try:
        with path.open("rb") as source:
            header = source.read(6)
        return len(header) == 6 and header[:2] == b"BM" and path.stat().st_size == struct.unpack("<I", header[2:])[0]
    except OSError:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=ROOT / "build/windows/Release/deckboy-mini.exe", type=Path)
    args = ap.parse_args()
    exe = args.exe.resolve()
    work = Path(tempfile.mkdtemp(prefix="mini-hud-", dir=ROOT / "build"))
    state = work / "state"
    state.mkdir()
    video = work / "bars.avi"
    creation = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    subprocess.run([shutil.which("ffmpeg") or "ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                    "smptebars=size=320x180:rate=24", "-f", "lavfi", "-i",
                    "sine=frequency=440:sample_rate=48000", "-t", "20", "-c:v", "mpeg4",
                    "-q:v", "4", "-c:a", "pcm_s16le", str(video)],
                   check=True, timeout=60, **creation)
    checks = 0

    def check(condition, name):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(name)
        print("PASS " + name, flush=True)

    class Player:
        def __init__(self, name, plain=False, panel=True):
            with socket.socket() as probe:
                probe.bind(("127.0.0.1", 0))
                self.port = probe.getsockname()[1]
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                       SDL_RENDER_DRIVER="software", DECKBOY_ROOT=str(ROOT),
                       DECKBOY_STATE_DIR=str(state), DECKBOY_MINI_GREMLIN_ODDS="1")
            env.pop("DECKBOY_MINI_PANEL", None)
            if panel:
                env["DECKBOY_MINI_PANEL"] = "1"
            self.path = work / (name + ".log")
            self.log = self.path.open("wb")
            command = [str(exe), "--window", "--loop", "--port", str(self.port)]
            if plain:
                command.append("--plain")
            self.proc = subprocess.Popen(command + [str(video)], cwd=ROOT, env=env,
                                         stdin=subprocess.DEVNULL, stdout=self.log,
                                         stderr=subprocess.STDOUT, **creation)

        def send(self, command):
            with socket.create_connection(("127.0.0.1", self.port), timeout=2) as client:
                client.sendall((command + "\n").encode())
                data = b""
                while True:
                    block = client.recv(65536)
                    if not block:
                        break
                    data += block
                    if data.endswith(b"\n"):
                        break
            return data.decode("utf-8", errors="replace")

        def ready(self):
            if self.proc.poll() is not None:
                raise AssertionError(self.text())
            try:
                return self.send("PING").startswith("OK")
            except OSError:
                return False

        def text(self):
            return self.path.read_text(encoding="utf-8", errors="replace")

        def close(self):
            try:
                self.send("QUIT")
                self.proc.wait(timeout=15)
                check(self.proc.returncode == 0, "clean Mini exit")
            finally:
                if self.proc.poll() is None:
                    self.proc.kill()
                    self.proc.wait(timeout=10)
                self.log.close()

    player = Player("escape")
    try:
        wait_for(player.ready)
        wait_for(lambda: "gremlin containment: BREACHED" in player.text())
        check("\x1b[" in player.text(), "forced panel captured without a visible window")
        initial = player.send("STATUS")
        check("status=Playing" in initial, "playing during breach")
        check("master_vol=100 blackout=off" in initial, "volume and blackout unchanged")
        before = work / "before.bmp"
        check(player.send("OUTSNAP " + str(before)).startswith("OK"), "snapshot during escape")
        wait_for(lambda: complete_bmp(before))
        wait_for(lambda: "[o_o]" in player.text())
        after = work / "after.bmp"
        check(player.send("OUTSNAP " + str(after)).startswith("OK"), "snapshot at catch")
        wait_for(lambda: complete_bmp(after))
        check(hashlib.sha256(before.read_bytes()).digest() == hashlib.sha256(after.read_bytes()).digest(),
              "output pixels unchanged by terminal escape")
        final = player.send("STATUS")
        frame = lambda reply: int(re.search(r"frames_shown=(\d+)", reply)[1])
        check(frame(final) > frame(initial) + 10, "video frames continue through the chase")
        check(re.search(r" pos=([^ ]+)", final)[1] != re.search(r" pos=([^ ]+)", initial)[1],
              "playback clock advances")
        check(int(re.search(r" level=(\d+)", final)[1]) > 0, "audio analysis remains active")
        check("active=1" in final and "status=Playing" in final and "blackout=off" in final,
              "cue and transport unchanged")
        wait_for(lambda: "gremlin containment: re-contained by Deck" in player.text())
        check("1 escapes on record" in player.text(), "completion visible")
        record = state / "mini-containment"
        wait_for(lambda: record.exists() and record.read_text().strip() == "1")
        check(True, "count persisted during playback")
        check("GREMLIN" not in player.send("HELP").upper(), "test hook absent from HELP")
        check(player.send("PAUSE").startswith("OK") and "status=Paused" in player.send("STATUS"),
              "pause still works")
        check(player.send("PLAY").startswith("OK") and "status=Playing" in player.send("STATUS"),
              "resume still works")
        check(player.send("STOP").startswith("OK") and "status=Stopped" in player.send("STATUS"),
              "stop still works")
    finally:
        player.close()

    # Restart loads the count and another forced event increments it.
    player = Player("restart")
    try:
        wait_for(player.ready)
        wait_for(lambda: record.read_text().strip() == "2")
        check("gremlin containment: BREACHED" in player.text(), "restart continues from saved count")
    finally:
        player.close()
    for name, plain, panel in [("plain", True, True), ("pipe", False, False)]:
        saved = record.read_bytes()
        player = Player(name, plain, panel)
        try:
            wait_for(player.ready)
            time.sleep(1.5)
            check("BREACHED" not in player.text() and "\x1b[" not in player.text(),
                  name + " mode has no animation or ANSI")
            check(record.read_bytes() == saved, name + " mode does not roll or write state")
        finally:
            player.close()

    # Extract actual captured terminal frames for review, preserving text.
    raw = (work / "escape.log").read_text(encoding="utf-8", errors="replace")
    frames = re.split(r"\x1b\[\d+F", raw)
    selected = []
    for marker in ["nominal", "BREACHED", "[o_o]", "re-contained by"]:
        match = next((f for f in frames if marker in f and "DECKBOY MINI " in ANSI.sub("", f)), None)
        if match:
            lines = ANSI.sub("", match).splitlines()
            start = next(i for i, line in enumerate(lines) if "DECKBOY MINI " in line and "booting" not in line)
            visible = lines[start:start + 22]
            check(len(visible) == 22, marker + " panel has stable height")
            check(any("VOL " in line for line in visible) and any("SPC" in line for line in visible),
                  marker + " keeps controls visible")
            selected.append("\n".join(visible))
    (work / "terminal-frames.txt").write_text("\n\n".join(selected), encoding="utf-8")
    print(f"Mini HUD integration: {checks} checks passed; evidence: {work}", flush=True)


if __name__ == "__main__":
    main()
