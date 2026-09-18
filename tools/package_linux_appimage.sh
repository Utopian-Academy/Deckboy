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
    # CHECK THE SIZE BEFORE ANYTHING TRUSTS THE BYTES.
    #
    # curl -f rejects an error STATUS, so a plain 504 never reaches the
    # disk. It cannot catch a 200 that carries an error page under the
    # requested filename, and it cannot catch a truncated body either.
    #
    # A sibling session downloading a release asset during a GitHub wobble
    # ended up with a 92-byte file containing a 504 page. Their curl had no
    # -f and did not capture the status, so WHICH of those two cases it was
    # is unknown and now unknowable -- do not let anyone tell you otherwise.
    # The guard is worth having for either: 92 bytes of HTML chmods +x
    # happily and fails several steps later as "exec format error", which
    # names appimagetool, the AppDir and the runner long before it names
    # the network.
    #
    # The release API knows how big the asset is meant to be, so ask it and
    # compare exactly. A threshold needs a number somebody guessed; an
    # exact match does not. If the API is unreachable -- which is likely to
    # be true in exactly the conditions this guards against -- fall back to
    # a floor, because appimagetool is megabytes and an error page is not.
    expected=$(curl -fsSL --max-time 30 \
      "https://api.github.com/repos/AppImage/appimagetool/releases/tags/continuous" \
      2>/dev/null | python3 -c 'import json,sys
try:
    for a in json.load(sys.stdin).get("assets", []):
        if a["name"] == "appimagetool-x86_64.AppImage":
            print(a["size"]); break
except Exception:
    pass' 2>/dev/null || true)
    downloaded=$(wc -c < "$APPIMAGETOOL")
    if [ -n "$expected" ] && [ "$downloaded" != "$expected" ]; then
      echo "error: appimagetool is ${downloaded} bytes, the release says ${expected}." >&2
      bad=1
    elif [ -z "$expected" ] && [ "$downloaded" -lt 1000000 ]; then
      echo "error: appimagetool download is only ${downloaded} bytes, and the" >&2
      echo "       release API was unreachable to confirm the real size." >&2
      bad=1
    else
      bad=0
    fi
    if [ "$bad" = 1 ]; then
      echo "       This is not a binary. What arrived:" >&2
      head -c 200 "$APPIMAGETOOL" >&2; echo >&2
      # Delete it: the download is skipped when an executable copy is
      # already here, so a kept error page would poison every later run.
      rm -f "$APPIMAGETOOL"
      exit 1
    fi
    chmod +x "$APPIMAGETOOL"
  fi
fi

APPDIR="$OUTPUT_DIR/staging/Deckboy.AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/deckboy"

cp -a "$STAGE_SRC/bin/." "$APPDIR/usr/bin/"
cp -a "$STAGE_SRC/lib/." "$APPDIR/usr/lib/"
cp -a "$STAGE_SRC/data"  "$APPDIR/usr/share/deckboy/data"

# ── Libraries the AppImage MUST NOT carry ───────────────────────────────────
#
# AppImage keeps an excludelist of libraries that have to come from the host,
# and bundling one does not just fail their catalogue check -- it pins the
# AppImage to the GLIBC OF THE MACHINE THAT BUILT IT. Reported from outside,
# which is the only place it could have been: the AppImageHub test rig ran our
# AppImage and got
#
#   WARNING: Blacklisted file libgpg-error.so.0 found
#   .../libc.so.6: version `GLIBC_2.38' not found (required by libgpg-error.so.0)
#
# so the build would not START on Ubuntu 22.04, which is still supported and is
# what a lot of venue machines run. Our own CI builds and runs it on a newer
# runner, so nothing we test could ever have shown this.
#
# These arrive as transitive dependencies of things we do want (ffmpeg's TLS
# stack pulls the gcrypt family in). Every one of them is present on any desktop
# Linux and is ABI-stable, so dropping them is safe in a way that dropping, say,
# SDL or the FFmpeg libraries would not be. The portable tarball keeps them: it
# is not subject to AppImage's rules and is not claimed to run on an older
# glibc than it was built on.
#
# Source: https://github.com/AppImage/pkg2appimage/blob/master/excludelist
APPIMAGE_EXCLUDED="
libgpg-error.so.0
libgcrypt.so.20
libp11-kit.so.0
libtasn1.so.6
libcom_err.so.2
libkrb5.so.3
libkrb5support.so.0
libk5crypto.so.3
libgssapi_krb5.so.2
libkeyutils.so.1
libglib-2.0.so.0
libgobject-2.0.so.0
libgmodule-2.0.so.0
libgio-2.0.so.0
libgthread-2.0.so.0
"
for soname in $APPIMAGE_EXCLUDED; do
  if [ -e "$APPDIR/usr/lib/$soname" ]; then
    rm -f "$APPDIR/usr/lib/$soname"
    echo "  - usr/lib/$soname (host provides it; bundling raises the glibc floor)"
  fi
done

# AppStream metainfo, which is what gives a catalogue entry its description and
# its screenshots. The same validated file the Flatpak uses -- a copy, not a
# second thing to keep in step.
METAINFO="$REPO_ROOT/tools/flatpak/io.github.utopian_academy.Deckboy.metainfo.xml"
if [ -f "$METAINFO" ]; then
  mkdir -p "$APPDIR/usr/share/metainfo"
  cp "$METAINFO" "$APPDIR/usr/share/metainfo/"
  echo "  + usr/share/metainfo/$(basename "$METAINFO")"
else
  echo "  ! metainfo not found at $METAINFO - the catalogue entry will have no description" >&2
fi

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
