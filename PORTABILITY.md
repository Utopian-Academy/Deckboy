# Deckboy — Portability

This document describes how to run Deckboy on different machines and install layouts without hardcoded paths.

## Project root and data directory

- **Project root** is where show files and the `data/` folder are resolved from.
- **Resolution order**
  1. **`PLAYBOY_ROOT`** — if set, used as project root (absolute or relative to cwd).
  2. **Executable location** — if the binary path is known (Linux/BSD):
     - If the executable lies in a directory named `bin`, `build`, or `native`, the **parent** of that directory is used as project root.
     - Otherwise the **directory containing the executable** is project root.
  3. **Current working directory** — if nothing else applies.

So you can run from anywhere, e.g.:

```bash
# From project directory (typical)
./build/playboy-native
# → project root = current dir

# From elsewhere, using env
PLAYBOY_ROOT=/opt/playboy ./usr/local/bin/playboy-native
# → project root = /opt/playboy, data = /opt/playboy/data
```

- **Data directory** is always `project_root/data`. It is created on startup if missing.
- **Default show file** is `data/default.playboy`. Override with **`PLAYBOY_PROJECT`** (path to a `.playboy` file).

## Fonts

Fonts are looked up in this order:

1. **Environment override** (exact path):
   - `PLAYBOY_FONT_SANS`
   - `PLAYBOY_FONT_MONO`
   - `PLAYBOY_FONT_PIXEL`
2. **Project-local**: `data/` and `data/fonts/` (e.g. `data/PressStart2P.ttf`).
3. **XDG**: if **`XDG_DATA_HOME`** is set, `$XDG_DATA_HOME/playboy/fonts/`.
4. **System** paths (e.g. `/usr/share/fonts/...`).

Ship **PressStart2P.ttf** in `data/` for the pixel font. Sans/mono can come from the system or from the same `data/` or `data/fonts/` if you bundle them.

## NDI

- **Build**: NDI SDK include path via **`PLAYBOY_NDI_SDK`** (for example the SDK install root). CMake probes common include roots as fallback.
- **Runtime**:
  - Linux loader candidates: `libndi.so.6`, `libndi.so`, `/usr/local/lib/libndi.so.6`, `/usr/lib/libndi.so.6`
  - macOS loader candidate: `libndi.dylib`
  - Override any platform by setting **`PLAYBOY_NDI_LIB`** to an absolute library path.
- Current runtime dynamic-loader path is implemented for Linux/macOS builds; Windows loader parity is part of the cross-platform roadmap.

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

## Live source cues (Window / Camera / Syphon-Spout)

- Source cues now run through native transport in the runtime:
  - `Window Source` uses ffmpeg `x11grab` on Linux.
  - `Camera` uses ffmpeg `v4l2` on Linux.
  - `Syphon/Spout` currently uses desktop-capture fallback on Linux.
- Practical Linux requirements:
  - `DISPLAY` must be set for window/screen capture cues.
  - Camera cues require readable `/dev/video*` devices (user permissions/group access).
- Native Syphon (macOS) and Spout (Windows) capture backends are still roadmap work.

## Other environment variables

| Variable | Purpose |
|----------|---------|
| `PLAYBOY_ROOT` | Project root directory (overrides executable/cwd). |
| `PLAYBOY_PROJECT` | Path to show file to open at launch (default: `data/default.playboy`). |
| `PLAYBOY_FONT_SANS` / `PLAYBOY_FONT_MONO` / `PLAYBOY_FONT_PIXEL` | Override font paths. |
| `PLAYBOY_NDI_LIB` | Path to NDI runtime library (for example `libndi.so.6` or `libndi.dylib`). |
| `PLAYBOY_COMPANION_PORT` | Companion TCP/UDP port (default 5510). |
| `PLAYBOY_HYPERDECK_PORT` | HyperDeck server port. |
| `XDG_DATA_HOME` | Optional; used for font search under `playboy/fonts/`. |

## Install layout

For an installed binary (e.g. under `/usr/local`):

- Set **`PLAYBOY_ROOT`** to the desired config/data root (e.g. `/usr/local/share/playboy` or `~/.local/share/playboy`), or
- Run the binary from that root so the executable’s directory (or parent of `bin`/`build`) is the project root.

No paths are hardcoded to a specific machine or home directory.
