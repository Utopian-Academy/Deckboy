#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Install the common desktop resources into a package's share directory.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHARE="${1:?destination share directory required}"
mkdir -p "$SHARE/applications" "$SHARE/metainfo" "$SHARE/icons" "$SHARE/mime/packages"
cp "$ROOT/tools/flatpak/io.github.utopian_academy.Deckboy.desktop" "$SHARE/applications/"
cp "$ROOT/tools/flatpak/io.github.utopian_academy.Deckboy.Mini.desktop" "$SHARE/applications/"
cp "$ROOT/tools/flatpak/io.github.utopian_academy.Deckboy.metainfo.xml" "$SHARE/metainfo/"
cp -R "$ROOT/art/linux/hicolor" "$SHARE/icons/"
cp "$ROOT/art/linux/io.github.utopian_academy.Deckboy.xml" "$SHARE/mime/packages/"
