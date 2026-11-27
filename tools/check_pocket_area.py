#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check an already running Pocket. Do not launch an app or change playback.

Run with exclusive use of Pocket's remote controls and state folder. The script
backs up the whole state folder, restores AREA in memory, then restores files.
Backups and screenshots remain under --out, outside the state folder.
Screenshots need human review; this does not prove delay timing or touch input.
"""

import argparse
import math
import os
from pathlib import Path
import shutil
import socket
import stat
import time


CONFIG = ("left", "top", "right", "bottom", "mirror_h", "mirror_v", "turn",
          "matte", "opacity", "delay_ms")
MATTES = ("off", "16:9", "4:3", "2.39:1", "1.85:1", "1:1", "9:16")


def request(command):
    with socket.create_connection(("127.0.0.1", 5510), timeout=10) as connection:
        connection.sendall((command + "\n").encode("utf-8"))
        chunks = []
        while True:
            try:
                data = connection.recv(65536)
            except socket.timeout:
                if chunks:
                    break
                raise
            if not data:
                break
            chunks.append(data)
            connection.settimeout(0.3)
    response = b"".join(chunks).decode("utf-8")
    if not response:
        raise AssertionError(f"No answer to {command}")
    return response


def ok(command):
    response = request(command)
    if not response.startswith("OK "):
        raise AssertionError(f"{command}: {response}")
    return response


def area():
    response = ok("AREA")
    return dict(part.split("=", 1) for part in response.split() if "=" in part)


def same_config(actual, expected):
    for key in CONFIG:
        if key in ("left", "top", "right", "bottom", "opacity"):
            assert math.isclose(float(actual[key]), float(expected[key]), abs_tol=0.002), (key, actual, expected)
        else:
            assert actual[key] == expected[key], (key, actual, expected)


def restore_area(saved):
    ok("AREA " + " ".join(saved[key] for key in ("left", "top", "right", "bottom")))
    for axis in ("h", "v"):
        ok(f"AREA MIRROR {axis} {saved['mirror_' + axis]}")
    ok("AREA TURN " + saved["turn"])
    ok(f"AREA MATTE {saved['matte']} {saved['opacity']}")
    ok("AREA DELAY " + saved["delay_ms"])
    same_config(area(), saved)


def check_tree(root):
    # Do not follow a link out of the state folder during backup or restore.
    for path in (root, *root.rglob("*")):
        if path.is_symlink() or getattr(path.lstat(), "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise RuntimeError(f"State contains a link: {path}")


def restore_files(state, backup):
    check_tree(state)
    # Check every resolved target before removing a file created by the test.
    for path in sorted(state.rglob("*"), key=lambda item: len(item.parts), reverse=True):
        path.resolve().relative_to(state)
        relative = path.relative_to(state)
        if not (backup / relative).exists():
            if path.is_dir():
                path.rmdir()
            else:
                path.unlink()
    shutil.copytree(backup, state, dirs_exist_ok=True)


def snapshot(state, output, name):
    filename = f"area-check-{time.time_ns()}-{name}.bmp"
    source = state / filename
    ok("SNAP " + filename)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if source.exists():
            data = source.read_bytes()
            if len(data) >= 54 and data[:2] == b"BM" and len(data) == int.from_bytes(data[2:6], "little"):
                (output / (name + ".bmp")).write_bytes(data)
                return
        time.sleep(0.05)
    raise AssertionError(f"SNAP did not complete: {source}")


def run_checks(state, output):
    assert "AREA [" in ok("HELP")
    ok("AREA RESET")
    defaults = area()
    assert all(float(defaults[key]) == 0 for key in ("left", "top", "right", "bottom", "turn", "delay_ms"))
    assert defaults["mirror_h"] == defaults["mirror_v"] == "off"
    assert defaults["matte"] == "off" and float(defaults["opacity"]) == 1
    status = request("STATUS")
    assert "app=pocket" in status and "\nAREA " in status
    (output / "status-before.txt").write_text(status, encoding="utf-8")
    ok("VIEW MENU OUTPUT")
    snapshot(state, output, "output-menu")
    ok("VIEW MENU AREA")
    snapshot(state, output, "area-reset")
    width, height = float(defaults["width"]), float(defaults["height"])
    values = [round(width * .1), round(height * .1), round(width * .2), round(height * .2)]
    ok("AREA " + " ".join(map(str, values)))
    for key, value in zip(("left", "top", "right", "bottom"), values):
        assert abs(float(area()[key]) - value) < .002
    ok("VIEW FULL")
    snapshot(state, output, "program-crop")
    for axis in ("H", "V"):
        ok(f"AREA MIRROR {axis} ON")
        assert area()["mirror_" + axis.lower()] == "on"
        snapshot(state, output, "program-mirror-" + axis.lower())
    for turn in (0, 90, 180, 270):
        ok(f"AREA TURN {turn}")
        assert int(area()["turn"]) == turn
        snapshot(state, output, f"program-turn-{turn}")
    for matte in MATTES:
        ok(f"AREA MATTE {matte} 0.5")
        current = area()
        assert current["matte"] == matte and float(current["opacity"]) == .5
        snapshot(state, output, "program-matte-" + matte.replace(":", "-"))
    for delay in (0, 1, 5000, 250):
        ok(f"AREA DELAY {delay}")
        assert int(area()["delay_ms"]) == delay
    time.sleep(.4)
    assert area()["delay_error"] == "off"
    for bad in ("-1 0 0 0", "1 2 3", "1 2 3 4 extra", "nan 0 0 0", "inf 0 0 0",
                f"{width} 0 0 0", f"{width * .6} 0 {width * .6} 0", "MIRROR X ON",
                "MIRROR H MAYBE", "MIRROR H ON extra", "TURN 45", "TURN 90.5", "TURN 360",
                "DELAY -1", "DELAY 5001", "DELAY 1 extra", "MATTE banana", "MATTE 4:3 2",
                "MATTE 4:3 nan", "RESET extra"):
        before = area()
        assert request("AREA " + bad).startswith("ERR AREA:"), bad
        same_config(area(), before)
    ok("VIEW MENU AREA")
    snapshot(state, output, "area-crop")
    ok("VIEW HOME")
    snapshot(state, output, "program-area")
    (output / "status-area.txt").write_text(request("STATUS"), encoding="utf-8")
    # Pocket autosaves every four seconds. Check the new fields on disk.
    deadline = time.monotonic() + 7
    while time.monotonic() < deadline:
        saved = state / "pocket_list.deckboy"
        if saved.exists():
            records = [line.split("\t") for line in saved.read_text(encoding="utf-8").splitlines()]
            if any(row[0] == "output_target" and len(row) >= 119 and row[117:119] == ["1", "1"] for row in records):
                break
        time.sleep(.1)
    else:
        raise AssertionError("Autosave did not persist output mirrors")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("build/pocket-area-check") / time.strftime("%Y%m%d-%H%M%S"))
    args = parser.parse_args()
    state = (Path(os.environ["APPDATA"]) / "Deckboy/Pocket/state").resolve(strict=True)
    output = args.out.resolve()
    if output == state or state in output.parents or output in state.parents:
        raise RuntimeError("--out must be outside the state folder and its parents")
    output.mkdir(parents=True, exist_ok=False)
    check_tree(state)
    saved = area()
    if saved["matte"] not in MATTES:
        raise RuntimeError("This test cannot restore a custom mask matte; no settings changed")
    backup = output / "state-backup"
    shutil.copytree(state, backup)
    print(f"State backup: {backup}", flush=True)
    try:
        run_checks(state, output)
    finally:
        try:
            restore_area(saved)
            ok("VIEW HOME")
        finally:
            restore_files(state, backup)
    print(f"Protocol and persistence checks passed. Review screenshots in {output}.")
    print("Not tested: touch edges, themes/scale, MAP geometry, connected screen, or measured delay timing.")


if __name__ == "__main__":
    main()
