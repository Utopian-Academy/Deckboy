#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Usage: bash tools/build_macos_icons.sh output-dir [--modern-only]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:?output directory required}"
MODE="${2:-all}"
[ "$(uname -s)" = Darwin ] || { echo "macOS is required" >&2; exit 1; }
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
TEMP="$(mktemp -d)"
trap 'rm -rf "$TEMP"' EXIT
if [ "$MODE" != --modern-only ]; then
  for kind in deckboy show; do
    SET="$TEMP/$kind.iconset"
    mkdir -p "$SET"
    for points in 16 32 128 256 512; do
      cp "$ROOT/art/macos/icons/$kind-$points.png" "$SET/icon_${points}x$points.png"
      cp "$ROOT/art/macos/icons/$kind-$((points * 2)).png" "$SET/icon_${points}x$points@2x.png"
    done
    case "$kind" in deckboy) name=Deckboy ;; show) name=DeckboyShow ;; esac
    iconutil -c icns "$SET" -o "$OUT/$name.icns"
    test -s "$OUT/$name.icns"
  done
fi
if [ -n "${DECKBOY_MACOS_ASSETS_CAR:-}" ]; then
  test -s "$DECKBOY_MACOS_ASSETS_CAR"
  cp "$DECKBOY_MACOS_ASSETS_CAR" "$OUT/Assets.car"
else
  XCODE_MAJOR="$(xcodebuild -version 2>/dev/null | awk '/^Xcode / {split($2,v,".");print v[1]}' || true)"
  OS_MAJOR="$(sw_vers -productVersion | cut -d. -f1)"
  if [ "${XCODE_MAJOR:-0}" -ge 26 ] && [ "$OS_MAJOR" -ge 26 ]; then
    mkdir -p "$TEMP/compiled"
    xcrun actool "$ROOT/art/macos/Deckboy.icon" \
      --compile "$TEMP/compiled" --app-icon Deckboy \
      --output-format human-readable-text --notices --warnings --errors \
      --output-partial-info-plist "$TEMP/compiled/partial.plist" \
      --include-all-app-icons --enable-on-demand-resources NO \
      --development-region en --target-device mac \
      --minimum-deployment-target 26.0 --platform macosx
    test -s "$TEMP/compiled/Assets.car"
    cp "$TEMP/compiled/Assets.car" "$OUT/Assets.car"
  elif [ "$MODE" = --modern-only ] || [ "${DECKBOY_REQUIRE_MODERN_ICON:-0}" = 1 ]; then
    echo "Xcode 26+ on macOS 26+ or DECKBOY_MACOS_ASSETS_CAR is required" >&2
    exit 1
  else
    echo "Using legacy ICNS; layered icon needs Xcode 26+ on macOS 26+." >&2
  fi
fi
