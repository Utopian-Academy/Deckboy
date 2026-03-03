# Playboy — Portability

This document describes how to run Playboy on different machines and install layouts without hardcoded paths.

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

- **Build**: NDI SDK include path via **`PLAYBOY_NDI_SDK`** (e.g. `PLAYBOY_NDI_SDK=/opt/NDI\ SDK\ for\ Linux`). CMake also checks `/opt/NDI SDK for Linux`, `/usr/local`, `/usr`.
- **Runtime**: NDI library is loaded dynamically. **`PLAYBOY_NDI_LIB`** can point at the exact `.so` path. Otherwise the loader tries `libndi.so.6`, `libndi.so`, `/usr/local/lib/libndi.so.6`, `/usr/lib/libndi.so.6`.

## Other environment variables

| Variable | Purpose |
|----------|---------|
| `PLAYBOY_ROOT` | Project root directory (overrides executable/cwd). |
| `PLAYBOY_PROJECT` | Path to show file to open at launch (default: `data/default.playboy`). |
| `PLAYBOY_FONT_SANS` / `PLAYBOY_FONT_MONO` / `PLAYBOY_FONT_PIXEL` | Override font paths. |
| `PLAYBOY_NDI_LIB` | Path to NDI runtime library (e.g. `libndi.so.6`). |
| `PLAYBOY_COMPANION_PORT` | Companion TCP/UDP port (default 5510). |
| `PLAYBOY_HYPERDECK_PORT` | HyperDeck server port. |
| `XDG_DATA_HOME` | Optional; used for font search under `playboy/fonts/`. |

## Install layout

For an installed binary (e.g. under `/usr/local`):

- Set **`PLAYBOY_ROOT`** to the desired config/data root (e.g. `/usr/local/share/playboy` or `~/.local/share/playboy`), or
- Run the binary from that root so the executable’s directory (or parent of `bin`/`build`) is the project root.

No paths are hardcoded to a specific machine or home directory.
