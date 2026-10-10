#!/usr/bin/env bash
# Deckboy Mini on its own, for Linux: a small portable .tar.gz with Mini, the
# ffmpeg tools it uses for subtitles inside files, its font and the libraries
# they need. One script for both architectures -- x86_64 PCs and 64-bit ARM
# (a Raspberry Pi 4 or 5, or a Pi 3 on the 64-bit system) -- named after the
# machine it runs on.
#
#   tools/package_mini_linux.sh <build dir with deckboy-mini> [out dir]
#
# The library list and the rules for what NOT to bundle (glibc, the GPU
# driver, the display and sound servers) are the main packager's, kept in
# step with tools/package_linux.sh: the host provides those.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:?usage: package_mini_linux.sh <build dir> [out dir]}"
OUT_DIR="${2:-$REPO_ROOT/dist}"
VERSION="$(tr -d '[:space:]' < "$REPO_ROOT/VERSION")"
case "$(uname -m)" in
  x86_64) ARCH=x86_64 ;;
  aarch64|arm64) ARCH=arm64 ;;
  *) echo "error: unsupported architecture $(uname -m)" >&2; exit 1 ;;
esac
NAME="Deckboy-Mini-${VERSION}-linux-${ARCH}"
STAGE="$OUT_DIR/staging/$NAME"

command -v patchelf >/dev/null || { echo "error: patchelf is required (apt install patchelf)" >&2; exit 1; }
[ -x "$BUILD_DIR/deckboy-mini" ] || { echo "error: $BUILD_DIR/deckboy-mini not found" >&2; exit 1; }
FFMPEG="${DECKBOY_FFMPEG:-$(command -v ffmpeg || true)}"
FFPROBE="${DECKBOY_FFPROBE:-$(command -v ffprobe || true)}"
[ -x "$FFMPEG" ] && [ -x "$FFPROBE" ] || { echo "error: ffmpeg and ffprobe are required" >&2; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/lib" "$STAGE/data/fonts"
cp "$BUILD_DIR/deckboy-mini" "$STAGE/bin/deckboy-mini"
cp -L "$FFMPEG" "$STAGE/bin/ffmpeg"
cp -L "$FFPROBE" "$STAGE/bin/ffprobe"
# The subtitle face, and the two faces that draw Arabic, Persian and Tamazight
# on a machine without them, each with the licence that has to travel with it.
for f in LiberationSans-Regular.ttf LICENSE-Liberation.txt NotoSansArabic-Regular.ttf \
         NotoSansTifinagh-Regular.ttf LICENSE-Noto.txt; do
  cp "$REPO_ROOT/data/fonts/$f" "$STAGE/data/fonts/"
done
cp "$REPO_ROOT/LICENSE" "$STAGE/"
chmod u+w "$STAGE/bin/"*

is_excluded() {
  case "$1" in
    ld-linux*|libc.so.*|libm.so.*|libdl.so.*|librt.so.*|libpthread.so.*) return 0 ;;
    libresolv.so.*|libnsl.so.*|libutil.so.*|libcrypt.so.*|libanl.so.*)   return 0 ;;
    libstdc++*|libgcc_s*)                                                return 0 ;;
    libGL*|libEGL*|libOpenGL*|libGLdispatch*|libglapi*|libgbm*|libdrm*)  return 0 ;;
    libX*|libxcb*|libwayland*|libxkbcommon*|libxshmfence*)               return 0 ;;
    libasound*|libpulse*|libjack*|libpipewire*|libsndio*|libasyncns*)    return 0 ;;
    libdbus*|libudev*|libsystemd*|libselinux*|libapparmor*|libcap.so.*)  return 0 ;;
    *) return 1 ;;
  esac
}

WORKLIST=("$STAGE/bin/deckboy-mini" "$STAGE/bin/ffmpeg" "$STAGE/bin/ffprobe")
idx=0
while [ "$idx" -lt "${#WORKLIST[@]}" ]; do
  current="${WORKLIST[$idx]}"
  idx=$((idx + 1))
  while read -r soname arrow target _rest; do
    [ "$arrow" = "=>" ] || continue
    [ -n "$target" ] && [ -f "$target" ] || continue
    is_excluded "$soname" && continue
    if [ ! -f "$STAGE/lib/$soname" ]; then
      cp -L "$target" "$STAGE/lib/$soname"
      chmod u+w "$STAGE/lib/$soname"
      WORKLIST+=("$STAGE/lib/$soname")
    fi
  done < <(ldd "$current" 2>/dev/null | sed 's/^[[:space:]]*//' || true)
done
for exe in "$STAGE/bin"/*; do patchelf --set-rpath '$ORIGIN/../lib' "$exe" 2>/dev/null || true; done
for lib in "$STAGE/lib"/*; do patchelf --set-rpath '$ORIGIN' "$lib" 2>/dev/null || true; done

# A launcher at the top, so it is ./deckboy-mini from the folder you unpacked.
cat > "$STAGE/deckboy-mini" <<'LAUNCH'
#!/bin/sh
here="$(cd "$(dirname "$0")" && pwd)"
PATH="$here/bin:$PATH" exec "$here/bin/deckboy-mini" "$@"
LAUNCH
chmod +x "$STAGE/deckboy-mini"

cat > "$STAGE/README.txt" <<README
Deckboy Mini ${VERSION} for Linux (${ARCH})

One deck, one output, run from the keyboard. Unpack anywhere and run:

  ./deckboy-mini ~/Videos            a folder plays in name order
  ./deckboy-mini clip.mp4 logo.png   files in the order given
  ./deckboy-mini --help              every option

Press ? in the terminal for the keys. The manual has a miniature one:
https://utopian-academy.github.io/Deckboy/manual.html#deckboy-mini
README

# It has to run from the staged copy and say the version it is named after.
got="$("$STAGE/deckboy-mini" --version 2>/dev/null | head -1 | sed 's/^deckboy-mini[[:space:]]*v\{0,1\}//')"
if [ "$got" != "$VERSION" ]; then
  echo "error: the staged deckboy-mini reports '$got', not $VERSION" >&2
  exit 1
fi
echo "staged deckboy-mini reports v$got ($ARCH, $(ls "$STAGE/lib" | wc -l) libraries)"

mkdir -p "$OUT_DIR"
tar -C "$OUT_DIR/staging" -czf "$OUT_DIR/$NAME.tar.gz" "$NAME"
echo "wrote $OUT_DIR/$NAME.tar.gz ($(du -h "$OUT_DIR/$NAME.tar.gz" | cut -f1))"
