#!/usr/bin/env bash
# Build Deckboy on a fresh Debian/Ubuntu machine.
#
# The only awkward part is SDL: 3.4 is the floor (the recording readback uses
# the SDL_GPU renderer's texture property, which does not exist in 3.2) and most
# distributions do not package it yet. This installs the dependencies, builds
# SDL from source when the packaged one is too old, then builds Deckboy.
#
#   ./tools/linux_build.sh              # deps if needed, then build
#   ./tools/linux_build.sh --deps-only  # just the dependencies
#   ./tools/linux_build.sh --skip-deps  # I already have them
#
# The dependency list is SDL's own documented set. It is not the minimum that
# happens to work: 3.4 hard-fails on each missing X11 dev package where 3.2
# quietly turned the feature off, and finding them one build at a time is not a
# method.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/linux"
SKIP_DEPS=0
DEPS_ONLY=0

for arg in "$@"; do
  case "$arg" in
    --skip-deps) SKIP_DEPS=1 ;;
    --deps-only) DEPS_ONLY=1 ;;
    -h|--help) sed -n '2,18p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'; exit 0 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

install_deps() {
  echo "== dependencies =="
  sudo apt-get update
  sudo apt-get install -y \
    build-essential cmake pkg-config git \
    libfreetype-dev libharfbuzz-dev \
    libavcodec-dev libavformat-dev libavutil-dev \
    libavfilter-dev libswscale-dev libswresample-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
    libxkbcommon-dev libwayland-dev wayland-protocols \
    libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev \
    libasound2-dev libpulse-dev libudev-dev libdrm-dev libgbm-dev \
    libxss-dev libxfixes-dev libxinerama-dev libxtst-dev \
    libdbus-1-dev libibus-1.0-dev libdecor-0-dev \
    ffmpeg

  if sudo apt-get install -y libsdl3-dev libsdl3-ttf-dev 2>/dev/null \
     && pkg-config --atleast-version=3.4.0 sdl3; then
    echo "SDL3 $(pkg-config --modversion sdl3) from apt"
    return
  fi

  echo "== SDL3 >= 3.4 not packaged here, building from source =="
  rm -rf /tmp/SDL /tmp/SDL_ttf
  git clone --depth 1 --branch release-3.4.x https://github.com/libsdl-org/SDL.git /tmp/SDL
  cmake -S /tmp/SDL -B /tmp/SDL/build -DCMAKE_BUILD_TYPE=Release -DSDL_STATIC=OFF
  cmake --build /tmp/SDL/build -j"$(nproc)"
  sudo cmake --install /tmp/SDL/build
  # SDL_ttf has no 3.4 branch; 3.2 builds against SDL 3.4 unchanged.
  git clone --depth 1 --branch release-3.2.x --recurse-submodules \
    https://github.com/libsdl-org/SDL_ttf.git /tmp/SDL_ttf
  cmake -S /tmp/SDL_ttf -B /tmp/SDL_ttf/build -DCMAKE_BUILD_TYPE=Release -DSDLTTF_VENDORED=ON
  cmake --build /tmp/SDL_ttf/build -j"$(nproc)"
  sudo cmake --install /tmp/SDL_ttf/build
  sudo ldconfig
}

need_deps() {
  command -v cmake >/dev/null 2>&1 || return 0
  pkg-config --atleast-version=3.4.0 sdl3 2>/dev/null || return 0
  return 1
}

if [ "$SKIP_DEPS" -eq 0 ] && { [ "$DEPS_ONLY" -eq 1 ] || need_deps; }; then
  install_deps
fi
[ "$DEPS_ONLY" -eq 1 ] && exit 0

echo "== build =="
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo
echo "built: ${BUILD_DIR}/Deckboy"
"${BUILD_DIR}/Deckboy" --version || true
echo
echo "check it:      ${BUILD_DIR}/Deckboy --self-check"
echo "smoke it:      ${BUILD_DIR}/Deckboy --smoke"
echo "recording:     python3 ${REPO_ROOT}/tools/record_rate_check.py \\"
echo "                 --exe ${BUILD_DIR}/Deckboy --media <a 4K clip>"
