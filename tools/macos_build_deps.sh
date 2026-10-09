#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build every library the Mac bundle carries, from source, for the macOS the
# bundle CLAIMS -- instead of taking Homebrew's, which are built for whatever
# macOS the build machine runs.
#
# WHY. v0.99.405's Info.plist said macOS 11, Deckboy itself was built for 13,
# and all 32 bundled libraries (Homebrew bottles) for 14 -- 15 for some on
# Intel. On Ventura the bundled ffmpeg and ffprobe could not even start
# (libavdevice hard-links camera features new in macOS 14), so the desk came up
# with no icons and nothing could be imported. Built here with
# MACOSX_DEPLOYMENT_TARGET, anything newer than the target is linked weakly and
# guarded by the libraries themselves, and tools/audit_macos_minos.py then
# proves every file's minimum before a bundle can ship.
#
# Also closes two gaps the Homebrew ffmpeg had on the Mac only: no libsnappy
# (so no HAP encoder) and no libsrt (so no SRT streams).
#
#   tools/macos_build_deps.sh <prefix> [deployment-target]
#
# Build tools (cmake, ninja, meson, nasm, pkg-config) come from Homebrew; they
# never reach the bundle. Everything under <prefix> is what gets bundled.

set -euo pipefail

PREFIX="${1:?usage: macos_build_deps.sh <prefix> [deployment-target]}"
TARGET="${2:-12.0}"
mkdir -p "$PREFIX"
PREFIX="$(cd "$PREFIX" && pwd)"
SRC="${DEPS_SRC_DIR:-$PREFIX/../deps-src}"
mkdir -p "$SRC"
SRC="$(cd "$SRC" && pwd)"
JOBS="$(sysctl -n hw.ncpu)"
ARCH="$(uname -m)"   # arm64 or x86_64 -- one architecture per runner

export MACOSX_DEPLOYMENT_TARGET="$TARGET"
export CFLAGS="-mmacosx-version-min=$TARGET -O2"
export CXXFLAGS="-mmacosx-version-min=$TARGET -O2"
export LDFLAGS="-mmacosx-version-min=$TARGET -L$PREFIX/lib -Wl,-headerpad_max_install_names"
export CPPFLAGS="-I$PREFIX/include"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
# Never let a configure script find a Homebrew library by accident: only the
# prefix is on the pkg-config path, and Homebrew's is explicitly left off.
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
CMAKE_COMMON=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
              -DCMAKE_OSX_DEPLOYMENT_TARGET="$TARGET" -DCMAKE_OSX_ARCHITECTURES="$ARCH"
              -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_INSTALL_NAME_DIR="$PREFIX/lib"
              -DBUILD_SHARED_LIBS=ON
              # CMake 4 refuses projects declaring a minimum below 3.5, which
              # x265 4.1, snappy 1.2.1 and libsrt 1.5.4 all do.
              -DCMAKE_POLICY_VERSION_MINIMUM=3.5)

step() { echo; echo "=== $* ($(date +%H:%M:%S))"; }

fetch() {   # fetch <dir> <url> -- a release tarball, unpacked once
  local dir="$1" url="$2"
  if [ ! -d "$SRC/$dir" ]; then
    curl -fsSL --retry 3 "$url" -o "$SRC/$dir.tar.gz"
    mkdir -p "$SRC/$dir"
    tar -xzf "$SRC/$dir.tar.gz" -C "$SRC/$dir" --strip-components=1
  fi
}

clone() {   # clone <dir> <url> <tag>
  local dir="$1" url="$2" tag="$3"
  [ -d "$SRC/$dir" ] || git clone -q --depth 1 --branch "$tag" "$url" "$SRC/$dir"
}

# ── Codecs ──────────────────────────────────────────────────────────────────
step "x264"
clone x264 https://code.videolan.org/videolan/x264.git stable
( cd "$SRC/x264" && ./configure --prefix="$PREFIX" --enable-shared --disable-cli \
    --extra-cflags="$CFLAGS" --extra-ldflags="$LDFLAGS" && make -j"$JOBS" && make install )

step "x265"
fetch x265 https://bitbucket.org/multicoreware/x265_git/downloads/x265_4.1.tar.gz
# 4.1 also forces CMP0025 and CMP0054 to OLD, which CMake 4 no longer allows at
# all. CMP0025 OLD was there to make Apple's compiler report itself as plain
# "Clang", so with it gone the Clang test has to accept "AppleClang" too, or
# x265 builds without its Clang flags.
sed -i '' -E '/cmake_policy\(SET CMP00(25|54) OLD\)/d' "$SRC/x265/source/CMakeLists.txt"
sed -i '' 's/if(${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")/if(${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")/'   "$SRC/x265/source/CMakeLists.txt"
cmake -S "$SRC/x265/source" -B "$SRC/x265/build" "${CMAKE_COMMON[@]}" -DENABLE_CLI=OFF \
  -DENABLE_SHARED=ON $( [ "$ARCH" = arm64 ] && echo "-DENABLE_ASSEMBLY=OFF" )
cmake --build "$SRC/x265/build" && cmake --install "$SRC/x265/build"

step "libvpx"
clone libvpx https://github.com/webmproject/libvpx.git v1.15.0
# Named, not guessed: 1.15 knows darwin20-23, and on a darwin24 runner (macOS
# 15) it fell back to a generic target and linked with GNU ld's options.
# darwin21 is macOS 12, the floor this bundle claims.
( cd "$SRC/libvpx" && ./configure --target="$ARCH-darwin21-gcc" --prefix="$PREFIX" --enable-shared --disable-static \
    --disable-examples --disable-tools --disable-docs --disable-unit-tests \
    --enable-vp9-highbitdepth --extra-cflags="$CFLAGS" && make -j"$JOBS" && make install )
# libvpx names itself "libvpx.9.dylib" with no path, so everything linked to it
# (ffmpeg, ffprobe, four of the libav libraries) looked for a file of that name
# and the packager could not find it to bundle. Give it the full path the
# others use, before ffmpeg links against it.
for vpx in "$PREFIX"/lib/libvpx.*.dylib; do
  [ -L "$vpx" ] && continue
  install_name_tool -id "$PREFIX/lib/$(basename "$vpx")" "$vpx"
done

step "dav1d"
clone dav1d https://code.videolan.org/videolan/dav1d.git 1.5.1
meson setup "$SRC/dav1d/build" "$SRC/dav1d" --prefix="$PREFIX" --libdir=lib --buildtype=release \
  -Denable_tools=false -Denable_tests=false --default-library=shared
ninja -C "$SRC/dav1d/build" install

step "lame"
fetch lame https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz
# 3.100 exports a symbol it does not define; the one-line fix every packager carries.
sed -i '' '/lame_init_old/d' "$SRC/lame/include/libmp3lame.sym"
( cd "$SRC/lame" && ./configure --prefix="$PREFIX" --enable-shared --disable-static \
    --disable-frontend --disable-dependency-tracking && make -j"$JOBS" && make install )

step "opus"
fetch opus https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz
( cd "$SRC/opus" && ./configure --prefix="$PREFIX" --enable-shared --disable-static \
    --disable-doc --disable-extra-programs && make -j"$JOBS" && make install )

step "snappy (HAP)"
clone snappy https://github.com/google/snappy.git 1.2.1
cmake -S "$SRC/snappy" -B "$SRC/snappy/build" "${CMAKE_COMMON[@]}" \
  -DSNAPPY_BUILD_TESTS=OFF -DSNAPPY_BUILD_BENCHMARKS=OFF
cmake --build "$SRC/snappy/build" && cmake --install "$SRC/snappy/build"

step "srt (SRT streams)"
clone srt https://github.com/Haivision/srt.git v1.5.4
# No encryption library: passphrase-protected SRT needs one, plain SRT (which
# is what MediaMTX and Deckboy's stream cues use) does not. A TLS stack would
# add OpenSSL to the bundle for one option.
cmake -S "$SRC/srt" -B "$SRC/srt/build" "${CMAKE_COMMON[@]}" -DENABLE_ENCRYPTION=OFF \
  -DENABLE_APPS=OFF -DENABLE_STATIC=OFF -DENABLE_SHARED=ON
cmake --build "$SRC/srt/build" && cmake --install "$SRC/srt/build"

# ── ffmpeg ──────────────────────────────────────────────────────────────────
step "ffmpeg"
clone ffmpeg https://git.ffmpeg.org/ffmpeg.git n8.1
( cd "$SRC/ffmpeg" && ./configure --prefix="$PREFIX" --enable-shared --disable-static \
    --enable-gpl --enable-version3 --disable-doc --disable-debug \
    --enable-videotoolbox --enable-audiotoolbox \
    --enable-libx264 --enable-libx265 --enable-libvpx --enable-libdav1d \
    --enable-libmp3lame --enable-libopus --enable-libsnappy --enable-libsrt \
    --disable-openssl --disable-gnutls --disable-libxml2 --disable-sdl2 \
    --disable-lzma --disable-bzlib --disable-iconv \
    `# X11 is a Linux desktop; a Mac captures through ScreenCaptureKit. ffmpeg` \
    `# looks for Xlib by compiling against it, and an Intel Mac's compiler` \
    `# searches /usr/local -- Homebrew's -- by default, so it bundled four X11` \
    `# libraries built for macOS 14.` \
    --disable-xlib --disable-libxcb \
    --extra-cflags="$CFLAGS" --extra-ldflags="$LDFLAGS" \
  && make -j"$JOBS" && make install )

# ── SDL and the desk's text ─────────────────────────────────────────────────
step "SDL3"
clone SDL https://github.com/libsdl-org/SDL.git release-3.4.0
cmake -S "$SRC/SDL" -B "$SRC/SDL/build" "${CMAKE_COMMON[@]}" -DSDL_STATIC=OFF -DSDL_TESTS=OFF \
  -DSDL_EXAMPLES=OFF
cmake --build "$SRC/SDL/build" && cmake --install "$SRC/SDL/build"

step "SDL3_ttf (vendored FreeType + HarfBuzz)"
clone SDL_ttf https://github.com/libsdl-org/SDL_ttf.git release-3.2.2
( cd "$SRC/SDL_ttf" && git submodule update --init --depth 1 )
cmake -S "$SRC/SDL_ttf" -B "$SRC/SDL_ttf/build" "${CMAKE_COMMON[@]}" -DSDLTTF_VENDORED=ON \
  -DSDLTTF_SAMPLES=OFF -DSDLTTF_PLUTOSVG=OFF
cmake --build "$SRC/SDL_ttf/build" && cmake --install "$SRC/SDL_ttf/build"

# ── Small ones ──────────────────────────────────────────────────────────────
step "libltc"
fetch libltc https://github.com/x42/libltc/releases/download/v1.3.2/libltc-1.3.2.tar.gz
( cd "$SRC/libltc" && ./configure --prefix="$PREFIX" --enable-shared --disable-static \
  && make -j"$JOBS" && make install )

step "rtmidi"
clone rtmidi https://github.com/thestk/rtmidi.git 6.0.0
cmake -S "$SRC/rtmidi" -B "$SRC/rtmidi/build" "${CMAKE_COMMON[@]}" -DRTMIDI_BUILD_TESTING=OFF \
  -DRTMIDI_API_JACK=OFF
cmake --build "$SRC/rtmidi/build" && cmake --install "$SRC/rtmidi/build"

step "done: $PREFIX for macOS $TARGET ($ARCH)"
ls "$PREFIX/lib"/*.dylib | sed "s|$PREFIX/lib/||" | tr '\n' ' '; echo
