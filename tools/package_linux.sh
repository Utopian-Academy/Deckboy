#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
#
# package_linux.sh — build a portable Deckboy tree, the Linux counterpart to
# tools/package_windows.ps1 and tools/package_macos.sh.
#
# Output: dist/Deckboy-<VERSION>-linux-<arch>.tar.gz
#
# LAYOUT
# ------
#   Deckboy-<VERSION>-linux-x86_64/
#     deckboy            launcher (thin; the RPATH does the real work)
#     bin/Deckboy        RPATH=$ORIGIN/../lib
#     bin/ffmpeg         bundled, and REACHABLE: the app prepends its own
#     bin/ffprobe        directory to PATH at startup
#     lib/*.so*          bundled non-system libraries
#     data/              resources
#     LICENSE, README-linux.txt
#
# `bin` is in Paths::resolveProjectRoot()'s skip list, so the walk-up steps out
# of bin/ and finds the sibling data/ on the first try. No code change needed.
#
# WHAT TO BUNDLE, AND WHY THE LINE IS WHERE IT IS
# -----------------------------------------------
# The instinct is "bundle everything so it runs anywhere". On Linux that is
# actively wrong, and it is the single thing that makes portable builds fragile.
#
# Anything that talks to the kernel, the GPU driver, the display server or the
# sound server MUST come from the host:
#   * libGL / libEGL / libGLdispatch — these load the machine's actual DRI
#     driver. A bundled libGL against a host Mesa is a crash, not a fallback.
#   * libX11 / libxcb / libwayland — must match the running display server.
#   * libasound / libpulse / libjack — must match the running sound server.
#   * libdrm / libgbm — kernel-coupled.
#   * glibc (libc/libm/libpthread/libdl/librt) and the dynamic loader — bundling
#     glibc is how you get "symbol lookup error" on every second machine.
#   * libstdc++ / libgcc_s — deliberately NOT bundled. The host's Mesa driver is
#     loaded into our process and links against the host libstdc++; forcing ours
#     in front of it is a classic way to break GL on newer distros. The cost is
#     that this build needs a reasonably current distro, which is the right
#     trade for a tool that must not fall over mid-show.
#
# Everything else — SDL3, the FFmpeg libraries, codecs — travels with us.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
OUTPUT_DIR="$REPO_ROOT/dist"

while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    -h|--help) sed -n '2,45p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [ "$(uname -s)" != "Linux" ]; then
  echo "error: this packager only runs on Linux (uname is $(uname -s))" >&2
  exit 1
fi
command -v patchelf >/dev/null || {
  echo "error: patchelf is required (apt install patchelf)" >&2; exit 1; }

VERSION="$(tr -d ' \t\r\n' < "$REPO_ROOT/VERSION")"
[ -n "$VERSION" ] || { echo "error: VERSION file is empty" >&2; exit 1; }

ARCH="$(uname -m)"
STAGE_NAME="Deckboy-${VERSION}-linux-${ARCH}"
STAGE_DIR="$OUTPUT_DIR/staging/$STAGE_NAME"

BINARY="$BUILD_DIR/Deckboy"
[ -f "$BINARY" ] || { echo "error: $BINARY not found - build first" >&2; exit 1; }

echo "Staging Deckboy $VERSION ($ARCH) -> $STAGE_DIR"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/bin" "$STAGE_DIR/lib" "$OUTPUT_DIR"

cp "$BINARY" "$STAGE_DIR/bin/Deckboy"
chmod +x "$STAGE_DIR/bin/Deckboy"

for tool in ffmpeg ffprobe; do
  if src="$(command -v "$tool" 2>/dev/null)"; then
    cp "$src" "$STAGE_DIR/bin/$tool"
    chmod +x "$STAGE_DIR/bin/$tool"
    echo "  + bin/$tool"
  else
    echo "  ! $tool not found on PATH - the bundle will not decode media" >&2
  fi
done

if [ -d "$REPO_ROOT/data" ]; then
  cp -R "$REPO_ROOT/data" "$STAGE_DIR/data"
  # Per-machine state, exactly as the Windows and macOS packagers strip it:
  # last_project.txt holds the packager's own absolute paths, and
  # default.deckboy is gitignored scratch state (a release once shipped the
  # packager's webcam cue).
  for stale in last_project.txt default.deckboy deckboy-crash.log deckboy-soak.log; do
    if [ -e "$STAGE_DIR/data/$stale" ]; then
      rm -f "$STAGE_DIR/data/$stale"
      echo "  - stripped data/$stale (build-machine state)"
    fi
  done
fi

[ -f "$REPO_ROOT/LICENSE" ] && cp "$REPO_ROOT/LICENSE" "$STAGE_DIR/LICENSE"

# --- Which libraries stay on the host -------------------------------------
# Matched against the SONAME. See the rationale at the top of this file.
# Patterns are PREFIX-based, not ".so.N"-based. The first version of this list
# required a numeric suffix and so happily bundled libpulsecommon-16.1.so —
# PulseAudio's own internals — because that name ends in a bare ".so". Shipping
# a sound server's guts alongside a different host build of it is precisely the
# class of breakage this list exists to prevent.
is_excluded() {
  case "$1" in
    # loader + glibc
    ld-linux*|libc.so.*|libm.so.*|libdl.so.*|librt.so.*|libpthread.so.*) return 0 ;;
    libresolv.so.*|libnsl.so.*|libutil.so.*|libcrypt.so.*|libanl.so.*)   return 0 ;;
    # C++ runtime — must not sit in front of the host's Mesa
    libstdc++*|libgcc_s*)                                                return 0 ;;
    # GPU / graphics: these load the machine's own driver
    libGL*|libEGL*|libOpenGL*|libGLdispatch*|libglapi*|libgbm*|libdrm*)  return 0 ;;
    # display server
    libX*|libxcb*|libwayland*|libxkbcommon*|libxshmfence*)               return 0 ;;
    # sound servers — prefix form catches libpulsecommon-16.1.so
    libasound*|libpulse*|libjack*|libpipewire*|libsndio*|libasyncns*)    return 0 ;;
    # system policy / init / device management
    libdbus*|libudev*|libsystemd*|libselinux*|libapparmor*|libcap.so.*)  return 0 ;;
    *) return 1 ;;
  esac
}

echo "Bundling libraries"
WORKLIST=()
for f in "$STAGE_DIR/bin/Deckboy" "$STAGE_DIR/bin/ffmpeg" "$STAGE_DIR/bin/ffprobe"; do
  [ -f "$f" ] && WORKLIST+=("$f")
done

# Index-walk rather than shifting: expanding an empty array under `set -u` is an
# error on older bash, which bites exactly when the list drains.
idx=0
while [ "$idx" -lt "${#WORKLIST[@]}" ]; do
  current="${WORKLIST[$idx]}"
  idx=$((idx + 1))
  # ldd output: "libfoo.so.1 => /path/to/libfoo.so.1 (0x...)". Entries without
  # a "=>" are the loader or linux-vdso and are never copied.
  while read -r soname arrow target _rest; do
    [ "$arrow" = "=>" ] || continue
    [ -n "$target" ] && [ -f "$target" ] || continue
    is_excluded "$soname" && continue
    if [ ! -f "$STAGE_DIR/lib/$soname" ]; then
      cp -L "$target" "$STAGE_DIR/lib/$soname"
      chmod u+w "$STAGE_DIR/lib/$soname"
      WORKLIST+=("$STAGE_DIR/lib/$soname")
      echo "  + lib/$soname"
    fi
  done < <(ldd "$current" 2>/dev/null | sed 's/^[[:space:]]*//' || true)
done

# --- RPATHs ---------------------------------------------------------------
# $ORIGIN is resolved by the loader relative to the object being loaded, which
# is what makes the tree relocatable. Quoted so the shell does not expand it.
for exe in "$STAGE_DIR/bin"/*; do
  [ -f "$exe" ] || continue
  patchelf --set-rpath '$ORIGIN/../lib' "$exe" 2>/dev/null || true
done
for lib in "$STAGE_DIR/lib"/*; do
  [ -f "$lib" ] || continue
  patchelf --set-rpath '$ORIGIN' "$lib" 2>/dev/null || true
done

# --- Launcher -------------------------------------------------------------
# Thin on purpose. The RPATH already resolves libraries and the app prepends its
# own directory to PATH for ffmpeg, so this exists only so users have something
# obvious to double-click or type.
cat > "$STAGE_DIR/deckboy" <<'LAUNCH'
#!/bin/sh
# POSIX sh with an absolute interpreter path, NOT `#!/usr/bin/env bash`.
# `env bash` has to find bash on PATH, so the launcher failed outright in a
# stripped environment while the binary beside it ran fine — a launcher that is
# more fragile than the thing it launches. /bin/sh is always present.
SELF="$0"
# Resolve symlinks so this works via a PATH entry or a desktop shortcut.
while [ -L "$SELF" ]; do
  DIR=$(cd -P "$(dirname "$SELF")" && pwd)
  SELF=$(readlink "$SELF")
  case "$SELF" in
    /*) ;;
    *) SELF="$DIR/$SELF" ;;
  esac
done
HERE=$(cd -P "$(dirname "$SELF")" && pwd)
exec "$HERE/bin/Deckboy" "$@"
LAUNCH
chmod +x "$STAGE_DIR/deckboy"

cat > "$STAGE_DIR/README-linux.txt" <<NOTE
Deckboy ${VERSION} (Linux ${ARCH})

Run it
------
    ./deckboy

Nothing to install. SDL3, the FFmpeg libraries and ffmpeg/ffprobe travel with
the app.

What comes from your system, on purpose
---------------------------------------
Graphics (libGL/Mesa), the display server (X11/Wayland), audio
(ALSA/PulseAudio/PipeWire) and the C/C++ runtime are NOT bundled. Those must
match the machine actually running: a bundled libGL cannot load your GPU driver,
and a bundled libstdc++ in front of your Mesa is a well-known way to break GL.

That means this build wants a reasonably current distribution. It is built on
Ubuntu 24.04 / Mint 22 and should run on anything of that vintage or newer.

Not in this build
-----------------
  * Spout / DeckLink   - Windows features.
  * GPU zero-copy decode - Windows-only (D3D11VA). Decoding falls back to the
                         CPU.
Browser cues, camera capture (V4L2) and window capture (x11grab) DO work here.

If it crashes
-------------
A stack trace is written to data/deckboy-crash.log. Sending that is the single
most useful thing you can do.
NOTE

TARBALL="$OUTPUT_DIR/$STAGE_NAME.tar.gz"
rm -f "$TARBALL"
# --owner/--group 0 so the archive does not carry the packager's uid.
tar -czf "$TARBALL" --owner=0 --group=0 -C "$OUTPUT_DIR/staging" "$STAGE_NAME"

echo
echo "Wrote $TARBALL"
du -h "$TARBALL" | awk '{print "  size: " $1}'
