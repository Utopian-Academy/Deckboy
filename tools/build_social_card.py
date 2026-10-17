#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
"""Build both 1280 x 640 social cards with Pillow and the bundled fonts.

    python tools/build_social_card.py
    python tools/build_social_card.py --capture --exe build/windows/Release/Deckboy.exe

The default uses art/readme/social-card-desk.png. --capture starts a separate,
hidden Deckboy with fresh assets/state under build/social-card-*, adds only
built-in generators over TCP, and captures the actual desk with UISNAP.
Use a build of this checkout, without private extras. No desktop input or
screen capture is used. Install Pillow with: python -m pip install Pillow
"""

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time

from PIL import Image, ImageDraw, ImageFont

from deckboy_testroot import populate

ROOT = Path(__file__).resolve().parent.parent
DESK = ROOT / "art/readme/social-card-desk.png"
OUTPUTS = (ROOT / "docs/images/social-card.png", ROOT / "art/readme/social-card.png")
SIZE = (1280, 640)
SAFE = 40
# The established social-card palette, separate from the application's UI.
GROUND = "#091809"
ACCENT = "#b0cf28"
INK = "#e0e8cf"
MUTED = "#a1bb51"
FRAME = "#53643d"


def command(port, line):
    with socket.create_connection(("127.0.0.1", port), timeout=10) as sock:
        sock.sendall((line + "\n").encode("utf-8"))
        reply = b""
        while b"\n" not in reply:
            chunk = sock.recv(65536)
            if not chunk:
                raise ConnectionError("Deckboy closed the control connection")
            reply += chunk
    result = reply.decode("utf-8").strip()
    if not result.startswith("OK "):
        raise RuntimeError(f"{line}: {result}")
    return result


def wait_for(check, seconds=60):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            result = check()
            if result:
                return result
        except (OSError, ValueError):
            pass
        time.sleep(0.15)
    raise TimeoutError("Timed out waiting for the hidden Deckboy capture")


def capture_desk(exe, destination, width, height, scale):
    exe = exe.resolve(strict=True)
    (ROOT / "build").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="social-card-", dir=ROOT / "build"))
    root, state = work / "root", work / "state"
    populate(str(root), source_data=str(ROOT / "data"))
    state.mkdir()
    show = root / "data/default.deckboy"
    show.write_text(
        f"theme\tgameboy\nui_scale\t{scale}\nupdate_check\t0\nui_sounds\t0\n"
        "web_monitor\t0\n", encoding="utf-8")
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    # Do not inherit another test's root, project, render or control settings.
    env = {k: v for k, v in os.environ.items() if not k.startswith("DECKBOY_")}
    env.update(DECKBOY_ROOT=str(root), DECKBOY_STATE_DIR=str(state),
               DECKBOY_PROJECT=str(show), DECKBOY_COMPANION_PORT=str(port),
               DECKBOY_HIDE_WINDOWS="1", SDL_AUDIODRIVER="dummy")
    transcript = []
    with (work / "app.log").open("w", encoding="utf-8") as log:
        app = subprocess.Popen(
            [str(exe), "--allow-multi-instance", str(show),
             "--ui-size", str(width), str(height)], cwd=ROOT, env=env,
            stdout=log, stderr=log,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            def ready():
                if app.poll() is not None:
                    raise RuntimeError(f"Deckboy exited: {app.returncode}; see {work / 'app.log'}")
                return command(port, "PING")
            wait_for(ready)
            for line in (
                "OUTPUT OFF", "MASTERVOL 0", "LANGUAGE en",
                "PATTERN ADD swirl", "PATTERN ADD fireside", "PATTERN ADD portal",
                "PATTERN ADD code", "PATTERN ADD smpte-bars", "PATTERN ADD crosshatch",
                "TAKE 5", "SELECT 1",
            ):
                transcript.append({"command": line, "reply": command(port, line)})
            # Let imports, preview generation and transient confirmation toasts settle.
            time.sleep(5)
            bitmap = work / "desk.bmp"
            transcript.append({"command": "UISNAP", "reply": command(port, f"UISNAP {bitmap}")})

            def complete_bitmap():
                with Image.open(bitmap) as picture:
                    picture.load()
                    return picture.convert("RGB")
            picture = wait_for(complete_bitmap)
            if picture.size != (width, height):
                raise RuntimeError(f"Requested {width}x{height}; captured {picture.size}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            picture.save(destination, optimize=True)
            transcript.append({"command": "STATUS", "reply": command(port, "STATUS")})
        finally:
            app.terminate()
            try:
                app.wait(timeout=10)
            except subprocess.TimeoutExpired:
                app.kill()
                app.wait(timeout=10)
    (work / "capture.json").write_text(json.dumps({
        "executable": str(exe), "size": [width, height], "requested_ui_scale": scale,
        "theme": "gameboy", "commands": transcript,
        "capture_sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
    }, indent=2) + "\n", encoding="utf-8")
    print(f"Headless capture evidence: {work}")


def build_card(screenshot):
    card = Image.new("RGB", SIZE, GROUND)
    draw = ImageDraw.Draw(card)
    mono = ROOT / "data/fonts/LiberationMono-Regular.ttf"

    def text(x, y, value, size, fill, right=520):
        font = ImageFont.truetype(str(mono), size)
        box = draw.textbbox((x, y), value, font=font, anchor="lt")
        if box[0] < SAFE or box[1] < SAFE or box[2] > right or box[3] > SIZE[1] - SAFE:
            raise ValueError(f"Text outside its safe column: {value}")
        draw.text((x, y), value, font=font, fill=fill, anchor="lt")

    # Same two-column layout as the original; every mark respects the safe inset.
    draw.rectangle((40, 120, 45, 520), fill=ACCENT)
    text(64, 144, "DECKBOY", 68, ACCENT)
    text(66, 254, "Open-source playback and", 27, INK)
    text(66, 291, "show control for live video.", 27, INK)
    text(66, 390, "NDI / SRT / ST 2110 / DeckLink", 21, MUTED)
    text(66, 422, "Stream Deck / Companion / OSC", 21, MUTED)
    text(66, 494, "Windows  macOS  Linux", 23, INK)

    with Image.open(screenshot) as source:
        desk = source.convert("RGB")
    # Never crop or reconstruct the desk. Fit its whole framebuffer in the frame.
    desk.thumbnail((688, 490), Image.Resampling.LANCZOS)
    x, y = 1232 - desk.width, (SIZE[1] - desk.height) // 2
    border = (x - 6, y - 6, x + desk.width + 5, y + desk.height + 5)
    if border[0] < 530 or border[1] < SAFE or border[2] >= SIZE[0] - SAFE or border[3] >= SIZE[1] - SAFE:
        raise ValueError(f"Screenshot does not fit the safe area: {border}")
    draw.rectangle(border, fill=FRAME)
    card.paste(desk, (x, y))
    encoded = io.BytesIO()
    card.save(encoded, format="PNG", optimize=True)
    result = encoded.getvalue()
    if len(result) >= 1_000_000:
        raise ValueError(f"Card exceeds 1 MB: {len(result)} bytes")
    for output in OUTPUTS:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(result)
        print(f"{output.relative_to(ROOT)}: 1280 x 640, {len(result):,} bytes")
    print("SHA-256:", hashlib.sha256(result).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--capture", action="store_true", help="Refresh the actual desk screenshot headlessly first")
    parser.add_argument("--exe", type=Path, default=ROOT / "build/windows/Release/Deckboy.exe",
                        help="A public-source Deckboy build from this checkout")
    parser.add_argument("--screenshot", type=Path, default=DESK)
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=1000)
    parser.add_argument("--scale", type=float, default=1.5)
    args = parser.parse_args()
    if args.capture:
        capture_desk(args.exe, args.screenshot, args.width, args.height, args.scale)
    build_card(args.screenshot)


if __name__ == "__main__":
    main()
