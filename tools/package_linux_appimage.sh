#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
#
# package_linux_appimage.sh — build Deckboy-<VERSION>-x86_64.AppImage.
#
# An AppImage is Linux's "proper installer": one executable file that carries
# everything and runs on any reasonably current distribution, no extraction, no
# root, no package manager. It reuses the exact staged tree the portable
# packager builds (tools/package_linux.sh), rearranged into the AppDir layout
# appimagetool expects, plus a .desktop entry, an icon, and an AppRun launcher.
#
# What still comes from the host is the SAME short list as the portable build
# (see package_linux.sh): GPU/Mesa, X11/Wayland, the sound server, glibc and
# libstdc++. An AppImage that bundled those would be LESS portable, not more.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
OUTPUT_DIR="$REPO_ROOT/dist"

while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

[ "$(uname -s)" = "Linux" ] || { echo "error: Linux only" >&2; exit 1; }

VERSION="$(tr -d ' \t\r\n' < "$REPO_ROOT/VERSION")"
ARCH="$(uname -m)"

# Build the portable staging tree first — it does the library bundling, RPATHs
# and state-stripping we depend on. We consume its staging dir, not its tarball.
"$REPO_ROOT/tools/package_linux.sh" --build-dir "$BUILD_DIR" --output-dir "$OUTPUT_DIR"
STAGE_SRC="$OUTPUT_DIR/staging/Deckboy-${VERSION}-linux-${ARCH}"
[ -d "$STAGE_SRC" ] || { echo "error: portable staging tree not found at $STAGE_SRC" >&2; exit 1; }

# appimagetool: download a pinned copy next to the build if not on PATH. It is a
# self-contained AppImage itself, so there is nothing to install.
APPIMAGETOOL="$(command -v appimagetool || true)"
if [ -z "$APPIMAGETOOL" ]; then
  APPIMAGETOOL="$OUTPUT_DIR/appimagetool-x86_64.AppImage"
  if [ ! -x "$APPIMAGETOOL" ]; then
    echo "Fetching appimagetool"
    curl -fsSL -o "$APPIMAGETOOL" \
      "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage"
    chmod +x "$APPIMAGETOOL"
  fi
fi

APPDIR="$OUTPUT_DIR/staging/Deckboy.AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/deckboy"

cp -a "$STAGE_SRC/bin/." "$APPDIR/usr/bin/"
cp -a "$STAGE_SRC/lib/." "$APPDIR/usr/lib/"
cp -a "$STAGE_SRC/data"  "$APPDIR/usr/share/deckboy/data"
[ -f "$STAGE_SRC/LICENSE" ] && cp "$STAGE_SRC/LICENSE" "$APPDIR/"

# Icon: AppImage wants a top-level <name>.png. Convert the master with whatever
# is available; fall back to a 1x1 so the build never fails on a missing tool.
ICON_MASTER="$REPO_ROOT/art/windows/icons/deckboy_app_master.png"
if command -v convert >/dev/null && [ -f "$ICON_MASTER" ]; then
  convert "$ICON_MASTER" -resize 256x256 "$APPDIR/deckboy.png"
elif [ -f "$ICON_MASTER" ]; then
  cp "$ICON_MASTER" "$APPDIR/deckboy.png"
else
  printf 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==' \
    | base64 -d > "$APPDIR/deckboy.png"
fi
cp "$APPDIR/deckboy.png" "$APPDIR/.DirIcon"

cat > "$APPDIR/deckboy.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Deckboy
Comment=Cue deck for live video
Exec=Deckboy
Icon=deckboy
Categories=AudioVideo;Video;Player;
Terminal=false
DESKTOP

# AppRun: point DECKBOY_ROOT at the bundled data (the walk-up cannot find a
# sibling data/ from usr/bin), then exec the real binary. The binary's own
# $ORIGIN/../lib RPATH resolves the libraries, and its startup PATH-prepend
# finds usr/bin/ffmpeg, so AppRun stays deliberately thin.
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export DECKBOY_ROOT="$HERE/usr/share/deckboy"
exec "$HERE/usr/bin/Deckboy" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

OUT="$OUTPUT_DIR/Deckboy-${VERSION}-${ARCH}.AppImage"
rm -f "$OUT"
# --appimage-extract-and-run avoids needing FUSE on the build host (CI/servers).
ARCH="$ARCH" "$APPIMAGETOOL" --appimage-extract-and-run "$APPDIR" "$OUT" >/dev/null 2>&1 \
  || ARCH="$ARCH" "$APPIMAGETOOL" "$APPDIR" "$OUT"

echo
echo "Wrote $OUT"
du -h "$OUT" | awk '{print "  size: " $1}'
