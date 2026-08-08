#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
#
# package_macos.sh — build a portable Deckboy.app, the macOS counterpart to
# tools/package_windows.ps1.
#
# Output: dist/Deckboy-<VERSION>-macos-<arch>.zip
#
# WHAT "PORTABLE" MEANS HERE
# --------------------------
# Everything the app needs travels with it. Homebrew installs SDL3, FFmpeg and
# friends under /opt/homebrew (Apple Silicon) or /usr/local (Intel); a bundle
# that merely links against those paths runs on the BUILD machine and nowhere
# else. So every non-system dylib is copied into Contents/Frameworks and every
# reference to it is rewritten to @rpath, recursively — dependencies have
# dependencies.
#
# THE TRAP THAT BREAKS NAIVE BUNDLES
# ----------------------------------
# install_name_tool INVALIDATES A CODE SIGNATURE. On Apple Silicon every binary
# must carry at least an ad-hoc signature or the kernel refuses to execute it —
# so a bundle that is rewritten and not re-signed dies instantly with "killed:
# 9", which looks nothing like a linking problem. Everything is therefore
# re-signed AFTER rewriting, inside-out: dylibs, then executables, then the
# bundle itself.
#
# WHAT THIS DOES NOT DO
# ---------------------
#   * No Developer ID signing and no notarisation. Gatekeeper will quarantine a
#     zip downloaded from the internet; see README-macOS.txt in the output for
#     the one-line fix. Building locally, or transferring by USB/AirDrop, avoids
#     it entirely.
#   * No universal binary. The bundle is whatever architecture it was built on.
#   * Browser cues (WebView2), Spout and d3d11va zero-copy decode are Windows
#     features and are absent by construction, not broken.
#
# Usage:
#   ./tools/package_macos.sh                     # expects an existing build/
#   ./tools/package_macos.sh --build-dir build   # explicit
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build"
OUTPUT_DIR="$REPO_ROOT/dist"

while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    -h|--help) sed -n '2,40p' "$0"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [ "$(uname -s)" != "Darwin" ]; then
  echo "error: this packager only runs on macOS (uname is $(uname -s))" >&2
  exit 1
fi

VERSION="$(tr -d ' \t\r\n' < "$REPO_ROOT/VERSION")"
[ -n "$VERSION" ] || { echo "error: VERSION file is empty" >&2; exit 1; }

ARCH="$(uname -m)"          # arm64 on Apple Silicon, x86_64 on Intel
STAGE_NAME="Deckboy-${VERSION}-macos-${ARCH}"
STAGE_DIR="$OUTPUT_DIR/staging/$STAGE_NAME"
APP="$STAGE_DIR/Deckboy.app"
MACOS_DIR="$APP/Contents/MacOS"
FRAMEWORKS_DIR="$APP/Contents/Frameworks"
RESOURCES_DIR="$APP/Contents/Resources"

BINARY="$BUILD_DIR/Deckboy"
[ -f "$BINARY" ] || { echo "error: $BINARY not found - build first" >&2; exit 1; }

echo "Staging Deckboy $VERSION ($ARCH) -> $STAGE_DIR"
rm -rf "$STAGE_DIR"
mkdir -p "$MACOS_DIR" "$FRAMEWORKS_DIR" "$RESOURCES_DIR" "$OUTPUT_DIR"

# --- App binary -------------------------------------------------------------
cp "$BINARY" "$MACOS_DIR/Deckboy"
chmod +x "$MACOS_DIR/Deckboy"

# --- ffmpeg / ffprobe -------------------------------------------------------
# Deckboy shells out to these for probing and for the fallback decode path, so a
# bundle without them plays nothing on a machine that has no Homebrew.
for tool in ffmpeg ffprobe; do
  if src="$(command -v "$tool" 2>/dev/null)"; then
    cp "$src" "$MACOS_DIR/$tool"
    chmod +x "$MACOS_DIR/$tool"
    echo "  + $tool"
  else
    echo "  ! $tool not found on PATH - the bundle will not decode media" >&2
  fi
done

# --- data/ ------------------------------------------------------------------
# Contents/Resources, per Apple's layout. codesign treats Contents/MacOS as a
# code-only directory, so a data/ tree next to the executable risks failing
# signature validation — which on Apple Silicon means the app will not launch at
# all. Paths::resolveProjectRoot() has a matching __APPLE__ branch that detects
# the …/Contents/MacOS/<exe> structure and resolves the root to Contents/Resources.
if [ -d "$REPO_ROOT/data" ]; then
  cp -R "$REPO_ROOT/data" "$RESOURCES_DIR/data"
  # Same per-machine state the Windows packager strips: last_project.txt holds
  # the packager's own absolute paths, and default.deckboy is gitignored scratch
  # state (a release once shipped the packager's webcam cue). Neither ships.
  for stale in last_project.txt default.deckboy; do
    if [ -e "$RESOURCES_DIR/data/$stale" ]; then
      rm -f "$RESOURCES_DIR/data/$stale"
      echo "  - stripped data/$stale (build-machine state)"
    fi
  done
fi

[ -f "$REPO_ROOT/LICENSE" ] && cp "$REPO_ROOT/LICENSE" "$STAGE_DIR/LICENSE"

# --- Info.plist -------------------------------------------------------------
# The usage descriptions are load-bearing, not boilerplate: macOS TERMINATES a
# process that touches the camera or microphone without a matching
# NS*UsageDescription. Deckboy has Camera cues (avfoundation) and audio input,
# so omitting these turns "add a camera cue" into an unexplained crash.
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>Deckboy</string>
  <key>CFBundleDisplayName</key><string>Deckboy</string>
  <key>CFBundleExecutable</key><string>Deckboy</string>
  <key>CFBundleIdentifier</key><string>org.utopianacademy.deckboy</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${VERSION}</string>
  <key>CFBundleVersion</key><string>${VERSION}</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSCameraUsageDescription</key>
  <string>Deckboy uses the camera for live Camera cues.</string>
  <key>NSMicrophoneUsageDescription</key>
  <string>Deckboy uses audio input for live capture cues.</string>
  <key>NSLocalNetworkUsageDescription</key>
  <string>Deckboy uses the local network for NDI, NMOS, OSC and remote control.</string>
</dict>
</plist>
PLIST

# --- Dependency bundling ----------------------------------------------------
# Recursively pull in every non-system dylib. A worklist, not recursion in the
# shell: SDL3 depends on things that depend on things, and FFmpeg's libavfilter
# alone drags in a long tail.
is_system_lib() {
  case "$1" in
    /usr/lib/*|/System/Library/*|@rpath/*|@loader_path/*|@executable_path/*) return 0 ;;
    *) return 1 ;;
  esac
}

WORKLIST=()
for f in "$MACOS_DIR/Deckboy" "$MACOS_DIR/ffmpeg" "$MACOS_DIR/ffprobe"; do
  [ -f "$f" ] && WORKLIST+=("$f")
done

# Walk the list with an index instead of shifting off the front. macOS ships
# bash 3.2, where expanding an EMPTY array as "${arr[@]}" under `set -u` is an
# unbound-variable error — so the obvious `WORKLIST=("${WORKLIST[@]:1}")` blows
# up on the last element rather than terminating cleanly.
idx=0
while [ "$idx" -lt "${#WORKLIST[@]}" ]; do
  current="${WORKLIST[$idx]}"
  idx=$((idx + 1))
  # Skip the Mach-O header line (ends with ':') and the library's own id line.
  # `|| true`: otool exits non-zero on anything that is not Mach-O, and under
  # `set -e` a failing command substitution in an assignment aborts the whole
  # packaging run halfway through a bundle.
  deps="$(otool -L "$current" 2>/dev/null | tail -n +2 | awk '{print $1}' || true)"
  for dep in $deps; do
    is_system_lib "$dep" && continue
    base="$(basename "$dep")"
    if [ ! -f "$FRAMEWORKS_DIR/$base" ]; then
      if [ -f "$dep" ]; then
        cp "$dep" "$FRAMEWORKS_DIR/$base"
        chmod u+w "$FRAMEWORKS_DIR/$base"
        WORKLIST+=("$FRAMEWORKS_DIR/$base")
        echo "  + Frameworks/$base"
      else
        echo "  ! missing dependency: $dep (referenced by $(basename "$current"))" >&2
      fi
    fi
  done
done

# --- Rewrite install names --------------------------------------------------
# Every bundled dylib gets an @rpath id, every reference to it is repointed, and
# the executables gain an rpath that resolves @rpath to Contents/Frameworks.
for lib in "$FRAMEWORKS_DIR"/*.dylib; do
  [ -e "$lib" ] || continue
  install_name_tool -id "@rpath/$(basename "$lib")" "$lib" 2>/dev/null || true
done

retarget() {
  local target="$1"
  local deps
  deps="$(otool -L "$target" 2>/dev/null | tail -n +2 | awk '{print $1}' || true)"
  for dep in $deps; do
    is_system_lib "$dep" && continue
    local base
    base="$(basename "$dep")"
    if [ -f "$FRAMEWORKS_DIR/$base" ]; then
      install_name_tool -change "$dep" "@rpath/$base" "$target" 2>/dev/null || true
    fi
  done
}

for lib in "$FRAMEWORKS_DIR"/*.dylib; do
  [ -e "$lib" ] || continue
  retarget "$lib"
  # A dylib in Frameworks/ resolves @rpath relative to itself.
  install_name_tool -add_rpath "@loader_path" "$lib" 2>/dev/null || true
done

for exe in "$MACOS_DIR/Deckboy" "$MACOS_DIR/ffmpeg" "$MACOS_DIR/ffprobe"; do
  [ -f "$exe" ] || continue
  retarget "$exe"
  install_name_tool -add_rpath "@executable_path/../Frameworks" "$exe" 2>/dev/null || true
done

# --- Re-sign ----------------------------------------------------------------
# MUST come after install_name_tool. Ad-hoc ("-") is enough to execute locally;
# it is not Developer ID and does not survive Gatekeeper on a downloaded zip.
# Inside-out: dependencies before the things that load them.
echo "Re-signing (ad-hoc)"
for lib in "$FRAMEWORKS_DIR"/*.dylib; do
  [ -e "$lib" ] || continue
  codesign --force --timestamp=none --sign - "$lib" >/dev/null 2>&1 || true
done
for exe in "$MACOS_DIR/ffmpeg" "$MACOS_DIR/ffprobe" "$MACOS_DIR/Deckboy"; do
  [ -f "$exe" ] && codesign --force --timestamp=none --sign - "$exe" >/dev/null 2>&1 || true
done
codesign --force --deep --timestamp=none --sign - "$APP" >/dev/null 2>&1 || true

# --- Operator notes ---------------------------------------------------------
cat > "$STAGE_DIR/README-macOS.txt" <<NOTE
Deckboy ${VERSION} (macOS ${ARCH})

Run it
------
Double-click Deckboy.app.

macOS says the app "is damaged" or "cannot be opened"
-----------------------------------------------------
This build is ad-hoc signed, not notarised, so Gatekeeper quarantines it when it
arrives via a browser download. Clear the quarantine flag:

    xattr -dr com.apple.quarantine /path/to/Deckboy.app

Transferring by USB or AirDrop, or building on the machine itself, avoids this.

What is not in this build
-------------------------
  * Browser cues        - the embedded browser is Windows-only (WebView2).
  * Spout output        - Windows-only; the macOS equivalent (Syphon) is scaffold only.
  * GPU zero-copy decode - the d3d11va fast path is Windows/D3D11. Decoding falls
                          back to CPU, which Apple Silicon handles comfortably.

Everything else - playback, audio, themes, NDI, ST 2110, PTP, NMOS, LTC, OSC and
the Companion control surface - is present.
NOTE

# --- Zip --------------------------------------------------------------------
# ditto, not zip: it preserves the bundle structure and the signatures. A plain
# `zip` can mangle symlinks inside frameworks.
ZIP_PATH="$OUTPUT_DIR/$STAGE_NAME.zip"
rm -f "$ZIP_PATH"
( cd "$OUTPUT_DIR/staging" && ditto -c -k --sequesterRsrc --keepParent "$STAGE_NAME" "$ZIP_PATH" )

echo
echo "Wrote $ZIP_PATH"
du -h "$ZIP_PATH" | awk '{print "  size: " $1}'
