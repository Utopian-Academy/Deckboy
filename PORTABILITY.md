# Deckboy — Portability

This document describes how to run Deckboy on different machines and install layouts without hardcoded paths.

## Project root and data directory

- **Project root** is where show files and the `data/` folder are resolved from.
- **Resolution order**
  1. **`DECKBOY_ROOT`** — if set, used as project root (absolute or relative to cwd).
  2. **Executable location** — if the binary path is known (Linux, macOS, Windows):
     - If the executable lies in a directory named `bin`, `build`, or `native`, the **parent** of that directory is used as project root.
     - Otherwise the **directory containing the executable** is project root.
  3. **Current working directory** — if nothing else applies.

So you can run from anywhere, e.g.:

```bash
# From project directory (typical)
./build/deckboy-native
# → project root = current dir

# From elsewhere, using env
DECKBOY_ROOT=/opt/deckboy ./usr/local/bin/deckboy-native
# → project root = /opt/deckboy, data = /opt/deckboy/data
```

- **Data directory** is always `project_root/data`. It is created on startup if missing.
- **Default show file** is `data/default.deckboy`. Override with **`DECKBOY_PROJECT`** (path to a `.deckboy` file).

## Fonts

Fonts are looked up in this order:

1. **Environment override** (exact path):
   - `DECKBOY_FONT_SANS`
   - `DECKBOY_FONT_MONO`
   - `DECKBOY_FONT_PIXEL`
2. **Project-local**: `data/` and `data/fonts/` (e.g. `data/PressStart2P.ttf`).
3. **XDG**: if **`XDG_DATA_HOME`** is set, `$XDG_DATA_HOME/deckboy/fonts/`.
4. **System** paths:
   - Linux: typical `/usr/share/fonts/...` locations
   - macOS: `/System/Library/Fonts`, `/System/Library/Fonts/Supplemental`, `/Library/Fonts`, `~/Library/Fonts`
   - Windows: `%WINDIR%/Fonts`

Ship **PressStart2P.ttf** in `data/` for the pixel font. Sans/mono can come from the system or from the same `data/` or `data/fonts/` if you bundle them.

## Build discovery

- CMake now tries **config packages first** for `SDL3` and `SDL3_ttf` (good fit for Homebrew, vcpkg, system packages with exported configs).
- If config packages are unavailable, it falls back to **pkg-config** and then plain include/library lookup.
- macOS-only feature gates (`ENABLE_SIPHON`, `ENABLE_WEBVIEW`) now resolve frameworks through `find_library(...)` instead of relying on a non-standard CMake command.

## NDI

- **Build**: NDI SDK include path via **`DECKBOY_NDI_SDK`** (for example the SDK install root). CMake probes common include roots as fallback.
- **Runtime**:
  - Linux loader candidates: `libndi.so.6`, `libndi.so`, `/usr/local/lib/libndi.so.6`, `/usr/lib/libndi.so.6`
  - macOS loader candidate: `libndi.dylib`
  - Override any platform by setting **`DECKBOY_NDI_LIB`** to an absolute library path.
  - Windows loader candidate: `Processing.NDI.Lib.x64.dll` (NDI SDK / NDI Runtime — both send and receive paths are live on Windows).

## Streaming outputs (SRT / RTMP)

- Deckboy can publish per-output network streams through ffmpeg (`VIDEO STREAM ...` controls).
- Your ffmpeg build must include the relevant protocol support:
  - **SRT** (`libsrt`) for `srt://...` URLs
  - **RTMP** support for `rtmp://...` URLs
- Quick verification:

```bash
ffmpeg -protocols | rg "srt|rtmp"
```

- Stream path muxes H.264 video + AAC stereo audio.
- Audio follows output assignment stack (host deck fallback when no assignments are present).
- `VIDEO OUTPUT DELAY` currently applies to NDI/stream egress frames.
- `VIDEO OUTPUT COLORSPACE` maps to stream encoder color metadata flags (`AUTO`/`BT709`/`SRGB`).
- Stream egress works on all platforms: Unix feeds ffmpeg via FIFO; Windows feeds ffmpeg via an stdin pipe (no named FIFO needed).

## Live source cues (Window / Camera / Syphon-Spout)

- Source cues now run through native transport in the runtime:
  - `Window Source` uses ffmpeg `x11grab` on Linux.
  - `Camera` uses ffmpeg `v4l2` on Linux.
  - `Syphon/Spout` currently uses desktop-capture fallback on Linux.
- Practical Linux requirements:
  - `DISPLAY` must be set for window/screen capture cues.
  - Camera cues require readable `/dev/video*` devices (user permissions/group access).
- Native Syphon (macOS) and Spout (Windows) capture backends are still roadmap work.

## Current platform readiness

- **Windows**: primary development target. Full media decode (CreateProcessW subprocess pipes), stream/NDI/DeckLink/Spout output, WebView2 browser cues, gdigrab window-source capture, RtMidi input, LTC ingest, and all network integrations (Companion/OSC/ATEM/Art-Net/NMC/HyperDeck/TSL) are live. Camera capture (Media Foundation) remains a scaffold.
- **Linux**: fully supported. Browser cues use the external-Chromium/x11grab path; source capture uses x11grab/v4l2; stream audio uses FIFO handoff.
- **macOS**: executable-root resolution and font lookup are portable and most integrations are shared with Linux, but browser/source capture backends still need native (AVFoundation/Syphon) runtime work for a first-class build.

## Audit Conclusion (March 2026)

Portability is still realistic without a major architectural rewrite.

The current blockers are mostly backend/runtime seams, not the high-level
single-deck control model:

- **Unix-first process execution**
  - `native/core/subprocess.hpp/cpp` now provides a unified `spawnProcess()` API
    with `SpawnOptions` / `StdioMode` that encapsulates the three common patterns
    (piped stdout, detached silent, capture-all) behind a portable interface
  - Windows stubs are in place; the next step is a real `CreateProcessW` backend
  - FIFO-based stream feeding for ffmpeg is still Unix-only
  - remaining inline fork/exec sites in `main.cpp` should migrate to
    `spawnProcess()` over time
- **Linux-only live capture/browser backends**
  - browser runtime still depends on Linux virtual-display / capture plumbing
  - source cues still rely on Linux `x11grab` / `v4l2`
- **Platform backend completion**
  - native Windows/macOS capture/output/runtime backends still need real
    implementation
  - Windows NDI/runtime execution parity still needs completion

So the right sequencing is:

1. audit + cleanup
2. finish runtime/backend abstractions
3. complete macOS/Windows capture/output execution paths

## Other environment variables

| Variable | Purpose |
|----------|---------|
| `DECKBOY_ROOT` | Project root directory (overrides executable/cwd). |
| `DECKBOY_PROJECT` | Path to show file to open at launch (default: `data/default.deckboy`). |
| `DECKBOY_FONT_SANS` / `DECKBOY_FONT_MONO` / `DECKBOY_FONT_PIXEL` | Override font paths. |
| `DECKBOY_NDI_LIB` | Path to NDI runtime library (for example `libndi.so.6` or `libndi.dylib`). |
| `DECKBOY_COMPANION_PORT` | Companion TCP/UDP port (default 5510). |
| `DECKBOY_HYPERDECK_PORT` | HyperDeck server port. |
| `XDG_DATA_HOME` | Optional; used for font search under `deckboy/fonts/`. |

## Install layout

For an installed binary (e.g. under `/usr/local`):

- Set **`DECKBOY_ROOT`** to the desired config/data root (e.g. `/usr/local/share/deckboy` or `~/.local/share/deckboy`), or
- Run the binary from that root so the executable’s directory (or parent of `bin`/`build`) is the project root.

No paths are hardcoded to a specific machine or home directory.
