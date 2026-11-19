#!/bin/bash
# FFmpeg 7.1.2 for Android arm64 (API 29 = Android 10, the Pocket floor), shared
# libraries with the phone's hardware codecs (MediaCodec) beside the software ones.
# Runs in WSL; everything stays under X:\android.
set -euo pipefail
ROOT=/mnt/x/android
NDK=$ROOT/ndk-linux/android-ndk-r27d
API=29
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64
PREFIX=$ROOT/prefix/arm64-v8a
SRC=$ROOT/src/ffmpeg
BUILD=$SRC   # in-tree: an out-of-tree configure links "src", and X: refuses symlinks from WSL
mkdir -p "$BUILD" "$PREFIX"
cd "$BUILD"
"$SRC/configure" \
  --prefix="$PREFIX" \
  --target-os=android --arch=aarch64 --cpu=armv8-a \
  --enable-cross-compile \
  --cc="$TC/bin/aarch64-linux-android$API-clang" \
  --cxx="$TC/bin/aarch64-linux-android$API-clang++" \
  --nm="$TC/bin/llvm-nm" --ar="$TC/bin/llvm-ar" --ranlib="$TC/bin/llvm-ranlib" \
  --strip="$TC/bin/llvm-strip" \
  --sysroot="$TC/sysroot" \
  --enable-shared --disable-static \
  --enable-pic --enable-neon --enable-asm --enable-inline-asm \
  --enable-gpl --enable-version3 \
  --enable-jni --enable-mediacodec \
  --disable-programs --disable-doc --disable-avdevice --disable-postproc \
  --disable-debug --disable-symver \
  --extra-cflags="-O3 -fPIC" \
  --extra-ldflags="-Wl,-z,max-page-size=16384"
make -j"$(nproc)"
make install
ls -la "$PREFIX/lib"
