# Deckboy Pocket for Android

The phone app. Its native code is Deckboy's own: `native/pocket/pocket_main.cpp`
around the desk's MediaEngine, built from the repository's `CMakeLists.txt`
(the `DECKBOY_BUILD_POCKET` target, which takes the desk's sources and settings
the way Deckboy Mini does). This folder is the Android side: SDL3's activity,
Pocket's own (`PocketActivity`, `PocketBridge` for the picker and PDF decks),
and the Gradle build.

## What it needs, outside the repository

Everything lives under one folder, `POCKET_DEPS` (default `X:/android`), so it
can be deleted in one go:

| Path under `POCKET_DEPS` | What |
|---|---|
| `jdk17/` | JDK 17 (Temurin) for Gradle |
| `sdk/` | Android command-line tools, platform-tools (adb), platform 35, build-tools 35.0.0, NDK 27.3.13750724 |
| `src/SDL`, `src/SDL_ttf` | SDL `release-3.4.18`, SDL_ttf `release-3.2.2` (with submodules): the desk's versions |
| `src/ffmpeg` | FFmpeg `n7.1.2` |
| `ndk-linux/` | The Linux NDK r27d, for building FFmpeg in WSL |
| `prefix/arm64-v8a/` | FFmpeg built for Android by `build_ffmpeg_android.sh` |
| `gradle/` | Gradle's cache (`GRADLE_USER_HOME`) |

## Building

FFmpeg first (in WSL, once):

    wsl -d <distro> -u root -- bash /mnt/x/android/build_ffmpeg_android.sh

Without it Pocket still builds and plays Deckboy's generators, but no files.

Then the app (Git Bash):

    export JAVA_HOME='X:\android\jdk17' ANDROID_HOME='X:\android\sdk' \
           GRADLE_USER_HOME='X:\android\gradle' ANDROID_USER_HOME='X:\android\user-home'
    cd android && ./gradlew.bat assembleDebug
    X:/android/sdk/platform-tools/adb.exe install -r app/build/outputs/apk/debug/app-debug.apk

`local.properties` holds `sdk.dir=X\:/android/sdk` (forward slashes: a
backslash is an escape in that file).

## Traps found on the way

- **X: is case-insensitive and refuses symlinks from WSL.** The Linux NDK has
  kernel headers differing only in case, and its compilers are symlinks.
  Unzip with `-n < /dev/null` (otherwise it stops to ask "replace?" with
  nobody there), then replace each symlink with a launcher that keeps its
  name (`exec -a "$0"`): `clang++` and `ld.lld` pick their mode from it.
- **Android has no ffmpeg program to run**, so the desk's subprocess paths do
  not exist here; everything goes through in-process libav. The phone's
  decoders are ffmpeg's `*_mediacodec` decoders, which need the Java VM
  (`av_jni_set_java_vm`), and files from the picker are `content://` links,
  which need the app context (`av_jni_set_android_app_ctx`).
- **16 KB pages**: libraries are linked with `-z max-page-size=16384`, which
  Android 15 devices with 16 KB pages require.
