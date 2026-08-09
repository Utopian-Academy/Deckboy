# Packaging & Installers

Deckboy ships two things per platform: a **portable** build (unzip and run) and a
**proper installer**. Both are built from the same staged tree, so their payloads
are identical — the installer is just a friendlier wrapper around the portable
bundle.

Everything a user needs is bundled: the Deckboy binary, `ffmpeg`/`ffprobe`, the
UI `data/`, and the runtime libraries that are safe to carry. What is
*deliberately* left to the host is documented per platform below — bundling
those makes a build **less** portable, not more.

---

## What each script produces

| Script | Runs on | Portable output | Installer output |
|---|---|---|---|
| `tools/package_windows.ps1` | Windows | `Deckboy-<ver>-windows-x64.zip` | — |
| `tools/deckboy.iss` (Inno Setup) | Windows | — | `Deckboy-<ver>-windows-x64-setup.exe` |
| `tools/package_macos.sh` | macOS | `Deckboy-<ver>-macos-<arch>.zip` | `Deckboy-<ver>-macos-<arch>.dmg` |
| `tools/package_linux.sh` | Linux | `Deckboy-<ver>-linux-<arch>.tar.gz` | — |
| `tools/package_linux_appimage.sh` | Linux | — | `Deckboy-<ver>-<arch>.AppImage` |

The AppImage packager calls `package_linux.sh` internally and reuses its staged
tree, so you never run both by hand.

---

## Windows

**Build the portable zip:**
```
cmake --build build\windows --config Release
powershell -File tools\package_windows.ps1 -RepoRoot .
```
It copies `Deckboy.exe` and every DLL CMake placed beside it, `ffmpeg.exe` /
`ffprobe.exe` (from `C:\ffmpeg\bin`, override with `-FfmpegDir`), the app-local
MSVC runtime DLLs, and `data\`. It **strips** `last_project.txt` and
`default.deckboy` — per-machine state that must never ship (a release once
carried the packager's own webcam cue).

**Build the installer** (needs [Inno Setup 6](https://jrsoftware.org/isdl.php)):
```
iscc /DDeckboyVersion=<ver> tools\deckboy.iss
```
`deckboy.iss` reads the staged folder the zip packager made, so run the zip
packager first. The installer gives a Start Menu entry, an optional desktop
shortcut, an opt-in `.deckboy` file association, and a real uninstaller. It
installs per-machine with admin, or falls back to per-user without.

**Host-provided:** nothing — Windows binaries are fully self-contained.

---

## macOS

**Build both outputs at once** (macOS only — uses `install_name_tool`,
`codesign`, `iconutil`, `hdiutil`):
```
brew install cmake pkg-config sdl3 sdl3_ttf ffmpeg libltc
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(sysctl -n hw.ncpu)"
./tools/package_macos.sh          # writes both the .zip and the .dmg
```
The packager relocates every Homebrew dylib into `Contents/Frameworks`, rewrites
load paths to `@rpath`, copies `libltc.dylib` (dlopen'd at runtime, so the
dependency walk cannot find it), builds `Deckboy.icns`, and **re-signs ad-hoc
after rewriting** — Apple Silicon kills a binary whose signature was invalidated
by `install_name_tool`, so this order is load-bearing.

**Install:** open the `.dmg`, drag Deckboy onto the Applications shortcut.
Installing to `/Applications` also avoids **App Translocation** — an app run from
`~/Downloads` is executed by Gatekeeper from a read-only random path, which broke
the file picker on the first field test.

**Gatekeeper:** these builds are ad-hoc signed, not notarized. A download is
quarantined; clear it once:
```
xattr -dr com.apple.quarantine /Applications/Deckboy.app
```
Notarization (removing that step) needs an Apple Developer account and is not
wired up.

**Host-provided:** browser cues (WebView2 is Windows-only), Spout/Syphon, and
GPU zero-copy decode (D3D11VA). These are absent by construction, not broken;
the app reports them as unavailable.

---

## Linux

**Build the portable tarball:**
```
sudo apt-get install -y build-essential cmake pkg-config patchelf \
  libavcodec-dev libavformat-dev libavutil-dev libavfilter-dev \
  libswscale-dev libswresample-dev libfreetype-dev libharfbuzz-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxkbcommon-dev libwayland-dev libasound2-dev libpulse-dev \
  libudev-dev libdrm-dev libgbm-dev ffmpeg libltc-dev
# SDL3 from source if your distro does not package it (see .github/workflows/build.yml)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./tools/package_linux.sh
```

**Build the AppImage** (the single-file installer):
```
./tools/package_linux_appimage.sh   # fetches appimagetool automatically
```
Runs on any current distro with no extraction and no root: `chmod +x` it and
run it. `AppRun` points `DECKBOY_ROOT` at the bundled `data/`; the binary's
`$ORIGIN/../lib` RPATH resolves the libraries and its startup PATH-prepend finds
the bundled `ffmpeg`.

**Host-provided (NOT bundled — this is the important bit):** the GPU stack
(libGL/Mesa — a bundled libGL cannot load the machine's driver), the display
server (X11/Wayland), the sound server (ALSA/PulseAudio/JACK/PipeWire), and the
C/C++ runtime (glibc, `libstdc++` — the host's Mesa is loaded into our process
and links against the host `libstdc++`, so forcing ours in front breaks GL).
This means the build targets a **reasonably current** distribution (built on
Ubuntu 24.04 / Mint 22); it is not meant to run on a decade-old box.

---

## Verifying a portable build actually is portable

The test that matters is running it with a **cleared environment**, which proves
nothing leaked in from the build host's `PATH` or installed libraries:
```
# Linux / macOS
env -i HOME=/tmp PATH=/nonexistent /path/to/Deckboy --self-check   # ffmpeg: ok, ltc-runtime: ok
```
`ffmpeg: ok` with an empty `PATH` proves the bundled ffmpeg is found (Deckboy
prepends its own directory to `PATH` at startup). `ltc-runtime: ok` proves the
bundled libltc was found relative to the executable. CI runs this exact check
for the macOS bundle.

---

## Signing & notarization (not done)

- **macOS notarization** — needs an Apple Developer ID ($99/yr). Without it the
  one-time `xattr` clear above is required on downloaded builds.
- **Windows code signing** — needs a code-signing certificate. Without it
  SmartScreen shows an "unknown publisher" prompt on first run.

Both are credential/account decisions, not code. When the accounts exist, the
signing steps slot into `package_macos.sh` (a `codesign` with the Developer ID +
`notarytool`) and the Inno Setup build (a `signtool` pass), respectively.
