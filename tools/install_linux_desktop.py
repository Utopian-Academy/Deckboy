#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install/remove a portable folder's desktop resources for the current user."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def exec_argument(path):
    # Desktop Entry string escaping is decoded before Exec's quoted arguments.
    text = str(path)
    if any(c in text for c in "\r\n\x00"):
        raise ValueError("The portable folder path must not contain line breaks")
    text = text.replace("%", "%%")
    for char in ("\\", '"', chr(96), '$'):
        text = text.replace(char, "\\" + char)
    return '"' + text.replace("\\", "\\\\") + '"'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def refresh(data):
    for command, relative in (("update-desktop-database", "applications"),
                              ("update-mime-database", "mime"),
                              ("gtk-update-icon-cache", "icons/hicolor")):
        if shutil.which(command) and (data / relative).is_dir():
            args = [command]
            if command == "gtk-update-icon-cache":
                args += ["--force", "--ignore-theme-index"]
            result = subprocess.run(args + [str(data / relative)], check=False)
            if result.returncode:
                print(f"{command}: refresh returned {result.returncode}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uninstall", action="store_true")
    args = parser.parse_args()
    bundle = Path(__file__).resolve().parent
    data = Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share"))).resolve()
    state = data / "deckboy/desktop-install.json"
    previous = json.loads(state.read_text()) if state.exists() else None
    if args.uninstall:
        if previous is None:
            print("No portable desktop integration is installed.")
            return
        if previous["bundle"] != str(bundle):
            raise SystemExit("Integration belongs to another portable folder; run its installer to remove it.")
        for relative, expected in previous["files"].items():
            path = (data / relative).resolve()
            if not path.is_relative_to(data):
                raise SystemExit("Invalid desktop integration manifest")
            if path.is_file() and digest(path) == expected:
                path.unlink()
        state.unlink()
        refresh(data)
        print("Removed this portable folder's desktop integration.")
        return
    source = bundle / "share"
    if not source.is_dir() or not (bundle / "deckboy").is_file():
        raise SystemExit("Run this copy from the extracted Deckboy portable folder.")
    installed = {}
    for original in sorted(source.rglob("*")):
        if not original.is_file():
            continue
        relative = original.relative_to(source)
        destination = data / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if original.suffix == ".desktop":
            launcher = "deckboy-mini" if original.stem.endswith(".Mini") else "deckboy"
            field = "%F" if launcher == "deckboy-mini" else "%f"
            text = original.read_text(encoding="utf-8")
            text = "\n".join("Exec=" + exec_argument(bundle / launcher) + " " + field
                             if line.startswith("Exec=") else line for line in text.splitlines()) + "\n"
            destination.write_text(text, encoding="utf-8")
        else:
            shutil.copyfile(original, destination)
        installed[relative.as_posix()] = digest(destination)
    state.parent.mkdir(parents=True, exist_ok=True)
    state.write_text(json.dumps({"bundle": str(bundle), "files": installed}, indent=2) + "\n")
    refresh(data)
    print("Installed Deckboy's menu entry, icons and show-file type for this user.")


if __name__ == "__main__":
    main()
