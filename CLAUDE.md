# CLAUDE.md — Deckboy Quick-Reference for AI Sessions

This file gives Claude Code the architectural context it needs to work on Deckboy efficiently without re-reading everything each session. Keep it updated when significant new systems land.

---

## Build

**Windows (primary dev platform)**
```
cd native
cmake -B ../build/windows -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build ../build/windows --config Release
```
Binary lands at `build/windows/Release/Deckboy.exe`.
Run from `build/windows/Release/` (needs `data/` in the working dir, resolved via walk-up).

**Quick rebuild (no reconfigure)**
```
cmake --build ../build/windows --config Release
```

---

## Version Flow

- Single source of truth: `VERSION` file (root)
- CMake reads it, generates `native/core/deckboy_version.hpp`
- CHANGES.md must have a dated entry for each version
- DEVNOTES.md must be updated when architectural decisions are made
- Tag CI validates VERSION matches git tag before builds run
- **Never hardcode version strings** in source code

---

## Key File Map

| Path | Purpose |
|------|---------|
| `native/main.cpp` | Everything: UI, input, render, OSC, companion, timecode, settings |
| `native/engine/media_engine.cpp/hpp` | Core playback: decode, transport, fade, transition |
| `native/core/types.hpp` | All domain types: `Cue`, `Deck`, `OutputTarget`, `Project` |
| `native/core/constants.hpp` | `kOutputWidth/Height`, `kMaxVideoFrames`, `kAppTitle`, etc. |
| `native/platform/ndi_api.hpp` | NDI send runtime (dynamic load) |
| `native/platform/ndi_trigger_api.hpp` | NDI recv + find runtime (dynamic load) |
| `native/platform/integration_backend.cpp` | Backend catalog for ATEM/NDI/MTC/LTC/Art-Net |
| `native/platform/capture_backend.cpp/hpp` | Source capture (camera/window/screen) |
| `native/app/app_render_settings.ipp` | Settings modal render + action handler |
| `native/app/app_render_output.ipp` | Output compositor → window/NDI/DeckLink/Spout blit |
| `native/app/app_output_mgmt.ipp` | Output lifecycle: windows, streams, NDI, DeckLink, Spout |
| `native/platform/output_backend.hpp/cpp` | Output backend catalog + route planning |
| `native/platform/nmos_node.hpp/cpp` | NMOS IS-04 registration + Node API and IS-05 Connection API for the ST 2110 senders. Own HTTP server + registration threads. Tear down ONLY via `shutdownNmosNode()` — see DEVNOTES |
| `native/platform/siphon_spout.hpp/cpp` | Spout (Windows) / Syphon (macOS) texture sharing |
| `CHANGES.md` | User-facing changelog |
| `DEVNOTES.md` | Internal architectural decisions (must be kept updated) |
| `docs/CODEMAP.md` | Full structural code map: file inventory, data flow, threading model |
| `docs/VERSION_FLOW.md` | Version flow doc |
| `tools/package_windows.ps1` | Build portable `dist\Deckboy-<VERSION>-windows-x64.zip`. Defaults to `build\windows\Release` — pass `-BuildDir` if you built elsewhere. It verifies the binary reports the VERSION it is named after and refuses otherwise |
| `tools/linux_build.sh` | Dependencies + build on a fresh Debian/Ubuntu box, including building SDL 3.4 from source where the distro's is older |
| `tools/record_rate_check.py` | Record a known duration and count what landed in the file: frames delivered vs owed, plus the app's own dropped-frame alarm. `--renderer` / `--readback` pin the backend so one platform's behaviour can be measured from another |
| `tools/deckboy.iss` | Inno Setup Windows installer (Start Menu, `.deckboy` assoc, uninstaller). Needs the zip packager's staging dir. `iscc /DDeckboyVersion=<ver> tools\deckboy.iss` |
| `tools/package_macos.sh` | macOS `.app` bundle → `.zip` AND `.dmg` (drag-to-Applications). Relocates dylibs to Frameworks, re-signs ad-hoc AFTER `install_name_tool` (order is load-bearing), bundles libltc + builds `.icns` |
| `tools/package_linux.sh` | Portable Linux `.tar.gz` (bin/lib/data + `$ORIGIN` RPATH). Host provides GPU/X11/audio/glibc/libstdc++ — see comments |
| `tools/package_linux_appimage.sh` | Single-file `.AppImage` (wraps the portable tree; fetches appimagetool) |
| `docs/PACKAGING.md` | The full per-platform packaging/installer guide — read this first for anything build-distribution |
| `native/core/system_browser.hpp` | Cross-platform `openExternalUrl()` for dep prompts |
| `native/core/sdl_compat.hpp` | SDL3 compat layer: int-rect draw overloads, display-index helpers, `deckboyCreateTexture*` (nearest scale), audio pause helper |
| `native/engine/libav_decoder.hpp/.cpp` | In-process libav decode pipelines (v0.78.0): d3d11va zero-copy video, audio→s16/48k, D3D11 interop helpers. Behind `DECKBOY_INPROC_DECODE` |
| `native/app/app_overlays.ipp` | `renderDependencyPrompt()` + detection helpers (`ndiRuntimeAvailable` etc.) |
| `companion-module-deckboy/` | Bitfocus Companion module (Node/ESM). Polls `STATUS` for tally/feedbacks; `npm test` covers the parser against a captured reply |

## Program-Monitor Tap (v0.81.0)

The control window's preview is sampled from the **output's** finished composite,
not the decoder. `captureOutputPreviewTap()` (`app_output_mgmt.ipp`) renders
`compositorTexture` into a small target on the output renderer and reads it back
once per presented frame; `app_update.ipp` uploads it to `controlPreviewTex_`.
Two invariants: the tap is taken **before** warp/AOI (so the warp editor's
handles still make sense), and `controlPreviewIsComposite_` tells
`app_render_main.ipp` to draw it **without** re-applying cue geometry — the
composite already has it baked in. When no window output is armed it falls back
to the old decoder-frame path. Don't restore the ~10fps hwframe-download path as
the primary source; that was the "preview lags the output" bug.

---

## Settings Action Constants

Settings button actions are integer constants defined at the top of `main.cpp`. The 600s range runs to **655** (`kSettingsActionOutputAoiHEdit`; 652–655 are the AOI typed-entry chips), so 656–701 are free. A 700s block is also in use: **702–706** LTC generator, **710–714** NMOS. Allocate next from **715+** (or from the 656–701 gap). WARNING: ids 634–637 were once double-allocated, which silently killed whichever button's handler ran second (the "Processing sub-tab does nothing" bug, v0.76.24). Before allocating, grep the value: `grep "= <id>;" native/main.cpp`. High ranges in use: 800+ (display select), 20000+ (routing tables).

Pattern: define constants → add UI in `app_render_settings.ipp` → handle in settings action handler.

Recent additions (v0.76.14):
- `kSettingsActionMascotToggle = 634` — flips `Project::splashCharacter` between `deckbot` and `deckgirl`; calls `refreshSplashAsset`.
- `kSettingsActionUiScaleDropdown = 635` — picks a `Project::uiScale` from a fixed list; calls `applyUiScale` to reload fonts.
- `kSettingsActionPocket3Preset = 636` — toggles `Project::uiScale` between 1.0 and 2.0.

---

## Serialization (Save/Load)

Tab-delimited `.deckboy` project files. Fields appended at end of record; backward-compat guard: `if (fields.size() >= N)`. Helpers: `safeDouble`, `safeInt`, `safeBool`, `safeString`.

Current field counts:
- **OutputTarget**: 28 base fields + 4 AOI (28–31) + 2 Spout (32–33) + streamKey (34) + displayName (35) → guard `>= 36`
- **Cue**: check existing guard indices in saveProject/loadProject in `main.cpp`
- Careful: `app_smoke.ipp` constructs `OutputTarget` with positional aggregate init — adding a struct member mid-struct breaks those sites (prefer appending or update them)
- **Project scalars** serialize as `key\tvalue` lines (not positional): e.g. `splash_character`, `ui_scale`, `theme` (the saved colorway dir under `data/themes/`, applied on open + at boot unless empty). Add new ones as a `<<` write in saveProject + an `else if (fields[0] == "...")` branch in loadProject.

---

## MediaEngine Internals

- `loadCue()` → sets `activeCue_`, geometry, calls `initStillTimer` for still-type cues, then `startDecoderThreads` or `loadStillFrame`/`loadPatternFrame`
- `startDecoderThreads()` → **in-process libav decode first** (v0.78.0, file-backed cues, `startInprocDecoders`): d3d11va zero-copy onto the program output's D3D11 device when the cue takes the NV12 path (frames ride `DecodedFrame::gpu*`, composited via per-deck GPU bridge in `app_render_output.ipp`), CPU frames otherwise. Falls back to the classic ffmpeg subprocess pipes for live streams (SRT/NDI), rotated files, open failures, or `--no-inproc-decode`. CLI path picks `nv12` pipe format when the cue has no chroma key / color controls, otherwise `rgba` (so the CPU effects path stays valid). Scaler is `fast_bilinear` — bicubic is too expensive and visually indistinguishable on moving video.
- Decode watchdog: `consumeDecodeStall()` polled in `app_update.ipp` — a wedged in-process decode reracks the deck dark + toasts. See DEVNOTES "In-Process GPU Decode".
- `startBrowserFrameMode()` → called when first browser frame arrives; must preserve `duration_` from `activeCue_->stillDurationSeconds`
- `visualFadeGainAt()` → returns 1.0 when no duration or fade defined; suppressed for auto-advance cues. Evaluated at `position()`, so paused stills MUST keep a sane `currentPosition_` (held at `pausedPosition_`, not 0) or the fade-in ramp drives the held frame to 0 alpha — see DEVNOTES `Still Cue Hold / Fade Interaction`.
- Still-type cues (`isDefaultStillDurationCueKind`) default to `fadeOutSeconds = 0` in `applyDeckDefaultsToCue` so a held graphic doesn't dip to black; per-cue fade-out still works if enabled.
- `suppressVisualFadeOutForCurrentCue_` → set `true` for auto-advancing cues (crossfade handles the outgoing visual)
- **The engine OWNS its cue** (v0.76.19): `activeCue_` points at `activeCueSnapshot_`, never into `Deck::cues`. App edits reach the engine via `markProjectDirty()` → `syncEngineCueSnapshots()` next tick. The audio thread reads fades only through atomic mirrors (`syncAudioFadeParams`/`audioFadeGainAt`). Video position slaves to the audio device clock when audio is present (see DEVNOTES `Audio-Master A/V Clock`).
- Frame pipeline: ffmpeg stdout → `readExact` → `frameQueue_` → `uploadFrame()` → SDL_Texture; audio thread queues PCM via `SDL_PutAudioStreamData` on the deck's `SDL_AudioStream` (SDL3)
- `DecodedFrame::format` (`FramePixelFormat`) is the canonical flag: every upload site reads it to pick `SDL_UpdateTexture` (RGBA32) vs `SDL_UpdateNVTexture` (NV12), and to recreate the cached texture when the format changes. Helper: `syncFrameTexture()` in `render/texture_helpers.hpp`. See DEVNOTES `GPU Hardware Decode + NV12 Upload Path` for the full architecture.

---

## UI Patterns

- **Text placement is a contract (v0.81.0)** — panels paint the rect they are
  given (`drawUIPanel` no longer grid-snaps) and *all three* label helpers
  (`drawTextSafe`, `drawCenteredTextSafe`, `drawCenteredText`) centre on that
  same rect, ellipsizing and clipping identically. `drawCenteredText` is now a
  thin forward to the Safe variant; `drawCenteredTextUnclipped` is the escape
  hatch if raw overflow is ever needed. Before this, boxes and labels used two
  different coordinate spaces and neighbouring controls disagreed by up to a
  grid unit. **Do not reintroduce per-call-site snapping or hand-computed text
  y-offsets** — pass the container rect and let the helper centre it.
- **Settings headers**: `drawSettingsPlate` / `settingsPlateRect` /
  `settingsHeaderHeight` in `app_render_settings.ipp` are the single source for
  every titled box (System-tab cards and Video-tab sections). Size sections from
  `settingsHeaderHeight(font)`, never a hardcoded 32.
- **Chrome fill role**: structural panels (bottom-bar groups, playlist body, deck
  list, timeline lanes) fill with `pal.tile` and ink with `pal.fg`/`pal.fgSoft`,
  NOT `pal.light`/`pal.deep`. `screen_light` is also the bright ink, so filling
  chrome with it made every theme a wall of colour and dark themes impossible.
  Both roles fall back (`tile`→`screen_light`, `fg`→`screen_deep`), so themes
  without them are unchanged. When adding a large structural panel, use the
  tile/fg pair; use light/deep only for small raised controls.
- **Known gap**: `Project::uiScale` scales fonts and the `kLayout*` metrics, but
  the settings modal's per-control geometry is still authored at 1×, so large
  scales (Pocket 3 / 2.0×) still overlap. Deferred deliberately.
- **Inspector helpers**: `insp*()` member functions in `main.cpp` (~line 34529), use `InspectorCtx`
- **Inline text editing**: always `openInlineTextEditor(token, ...)` — never modal dialogs
- **Dropdown**: `drawUIDropdown()` + `openDropdown()` — share with existing selectors
- **Panels**: `drawOperationalPanel()` chrome; panel visibility in `UiWorkspaceState`
- **Toast**: `triggerToast("message")` for operator feedback
- **Layout**: `VerticalLayout`, `HorizontalLayout`, `GridLayout`, `UITable`; grid unit = `kLayoutSpacingUnit`

---

## Integration Backend Pattern

New protocol integrations (tally, NDI recv, etc.) follow this pattern:
1. Add `IntegrationBackendKind` enum value in `integration_backend.hpp`
2. Add catalog entry in `integration_backend.cpp` (platform guards)
3. Add `enabled` field to `IntegrationBackendRouteRequest` / `Project` struct
4. Add toggle in settings modal (Network tab)
5. Implement runtime in `main.cpp` (thread + loop) using sockets or dynamic-library API
6. Add serialization in save/load

---

## NDI API Pattern

Dynamic library, loaded at runtime. `ndi_api.hpp` = send; `ndi_trigger_api.hpp` = recv + find. Both use `deckboy::platform::DynamicLibrary`. Windows DLL names differ from Linux `.so` — always add `#ifdef _WIN32` candidates.

Windows NDI lib: `Processing.NDI.Lib.x64.dll` (from NDI SDK, typically at `%NDI_SDK_DIR%\Bin\x64\`)

---

## Output Backend Pattern

Output destinations (window, stream, NDI, DeckLink, Spout) follow a catalog + route-planning architecture:

1. `OutputBackendCatalog` in `output_backend.cpp` — lists all backends with platform availability (`DECKBOY_HAS_NDI_SDK`, `DECKBOY_HAS_DECKLINK`, `DECKBOY_HAS_SPOUT`)
2. `OutputBackendRouteRequest` in `output_backend.hpp` — built from `OutputTarget` fields (per-output enable flags)
3. `planOutputBackendRoute()` — converts request → `OutputBackendRoutePlan` with ordered steps annotated with support status
4. `OutputBackendRuntimeRoute` in `app_output_mgmt.ipp` — resolved at runtime, drives `*RouteActive` bools in the render loop
5. Send/shutdown functions in `app_output_mgmt.ipp` — `sendOutputNdiFrame()`, `sendOutputDeckLinkFrame()`, `sendOutputSpoutFrame()`, etc.
6. Render loop in `app_render_output.ipp` — checks `*RouteActive`, calls send functions, manages egress capture
7. Settings UI in `app_render_settings.ipp` — toggle + config per backend in the Video Outputs tab
8. Serialization in `main.cpp` — fields appended to OutputTarget record

Spout uses `SpoutLibrary` DLL (vcpkg `spout2:x64-windows`). `SendImage()` accepts raw CPU pixel buffers — no OpenGL context needed from the caller. DeckLink uses the Blackmagic DeckLink SDK 16.0.

### handleSettingsClick split

The settings click handler is split across three functions to stay under MSVC's C1061 block-nesting limit: `handleSettingsClick` → `handleSettingsClickPart2` → `handleSettingsClickPart3`. When adding new handlers, add to Part3 (or create Part4 if needed). The split point is a trailing `else { handleSettingsClickPartN(sb); }`.

---

## Cue Kinds (CueKind enum)

`Video`, `Image`, `Pattern`, `Browser`, `WindowSource`, `Camera`, `Syphon`, `SrtStream`, `NdiSource`, `Pip`, `LowerThird`, `Composite`, `Audio`

- **SrtStream**: live stream input — `cue.path` = full URL (`srt://`, `rtmp://`, `rtsp://`, `udp://`). Skips ffprobe and `-ss`. Added via SOURCE menu → "Stream Cue".
- **NdiSource**: NDI receive input — `cue.path` = `ndi://SOURCE_NAME`. Skips ffprobe; uses `-f libndi_newtek -i NAME`. Added via SOURCE menu → "NDI Source Cue".
- Both use kind checks in `startDecoderThreads` (`isLiveStream`, `isNdiSource`); URL prefix detection no longer used.

---

## SDL3 (v0.77.0+)

The app runs on SDL3 (migrated from SDL2 in v0.77.0). Rules that keep it working:
- Include `"core/sdl_compat.hpp"`, never `<SDL3/SDL.h>` directly.
- Create textures with `deckboyCreateTexture` / `deckboyCreateTextureFromSurface`
  (applies the mandatory nearest-neighbour scale mode) — raw `SDL_CreateTexture`
  silently gives linear filtering.
- Display selection uses SDL2-style indices via `deckboy*Display*` helpers.
- Audio handles are `SDL_AudioStream*` (device-bound logical devices), paused
  via `deckboySetAudioPaused`. Streams open paused.
- SDL3 functions return `bool` (true = success) — never check `== 0`.
- `SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS = "0"` at init is load-bearing; never remove.
- `SDL_HINT_RENDER_DIRECT3D_THREADSAFE = "1"` at init is load-bearing (v0.78.0):
  without it SDL's D3D11 devices are single-threaded and the in-process
  decoder cannot share them (random crash/deadlock); never remove.
- Full migration notes: DEVNOTES "SDL2 → SDL3 Migration".

## File Dialogs (native, async)

All file/folder pickers use SDL3's native dialogs — `showOpenFileDialog` /
`showSaveFileDialog` / `showFolderDialog` in `app_cue_mgmt.ipp`. Do NOT reintroduce
the old `osascript`/`powershell`/`zenity` subprocess pickers: import ran one on a
`std::async` thread, and macOS cannot fork a GUI subprocess off the main thread —
it failed silently. Rules:
- The result callback (`sdlDialogTrampoline`) may fire on **any thread** (SDL's
  contract), so it only marshals a closure into `sdlDialogActions_` under a mutex;
  the real work runs on the main thread via `drainSdlDialogActions()` in the
  update loop. `sdlDialogOpen_` is atomic for the same reason.
- Filters passed to the helpers MUST have static/long lifetime — SDL requires the
  filter array stay valid until the callback fires. Every caller uses a
  `static const` list; never pass a temporary.

## Read-only Resources vs Writable State (v0.83.2)

`Paths::dataDir()` is **read-only**: bundled themes, fonts, sounds, UI packs.
`Paths::stateDir()` is the **only** place the app may write — the show, the
last-opened pointer, crash and soak logs, `_converted` media with no project
open. Inside a macOS `.app` the data dir is `Contents/Resources/data`, sealed by
the code signature, so writing there fails `codesign --verify --deep --strict`
after the very first run and breaks read-only / non-admin installs outright.

stateDir resolves to `DECKBOY_STATE_DIR` → the data dir when it is writable and
we are not in a bundle (portable installs unchanged, and what the isolated-test
`DECKBOY_ROOT` recipe selects) → per-user application data. **When adding
anything the app writes, use `stateDir()`.**

## Remote Protocol: every command answers (v0.83.2)

Companion/TCP commands are queued to the main thread with the client socket
attached (`PendingRemoteCommand`), and `processRemoteCommands` replies with
`OK <VERB>`, `ERR unknown command: <VERB>`, or `ERR <VERB>: <reason>`.

- Unknown verbs are detected by *falling off the end* of `handleRemoteCommand`,
  which clears `remoteCommandRecognized_`. Every recognized branch must
  `return` — a branch that falls through would report a false ERR.
- A verb that was understood but can't act calls `failRemoteCommand(reason)`
  instead of `triggerToast(reason)`: the operator gets the same toast and the
  caller gets the reason. Prefer it for every argument-validation failure.
- `HELP` over the socket lists the protocol. Keep it honest — it advertised
  SAVE/LOAD/RELOAD/FADE for a long time, none of which exist.
- Do NOT silently clamp a remote value into range. `MASTERVOL` clamped a percent
  into a 0–2 multiplier for years and nobody could see it; units are percent,
  out-of-range is an error.

## Media Formats

- Image cues: `isImagePath()` (main.cpp) lists the still extensions, incl.
  `.heic`/`.heif`. HEIC/HEIF are HEIF-container stills reconstructed via ffmpeg's
  INTERNAL complex filtergraph, so `loadStillFrame` scales them with
  `-filter_complex`, not `-vf` (which errors "simple and complex filtering cannot
  be used together" and yields zero bytes). Needs ffmpeg ≥ 7.1 for HEIF demux
  (macOS Homebrew has it; Ubuntu 24.04's 6.1 does NOT).
- A still that decodes to no frame latches `consumeStillDecodeFailure()` so the
  operator is told, instead of a blank cue.
- libltc is dlopen'd at runtime and BUNDLED by the packagers (Frameworks on mac,
  lib/ on Linux); `ltc_api.hpp` looks for it relative to the exe first.

## Key Conventions

- No new files unless strictly necessary — extend existing `.ipp`/`.cpp` instead
- AOI, warp, edge blend: per-output, in `OutputTarget`
- Cue geometry (scale/offset/crop/rotation): per-cue, in `Cue`
- All color/geometry values normalized (0–1 for fractions, degrees for rotation)
- **Operators see pixels, storage keeps fractions**: cue geometry (v0.76.21), AOI rect + edge blend in settings (v0.80.0). Convert at the UI edge only (`focusedOutputAoiRectPx`/`applyFocusedOutputAoiRectPx` pattern)
- Dev/test CLI: `--import <file>` (import at launch, skip splash/startup menu), `--settings [tab[.subtab]]` (open settings modal at boot), `--pattern-dump <id> <out.ppm> [WxH] [t]` — all scriptable for screenshot verification
- **Every change must work on Windows, macOS AND Linux** — same quality, not
  necessarily the same solution. Windows is where the work usually gets done, but
  a feature is not finished until the `#ifdef` structure has been checked and the
  other two either share the path or have a fallback that is *measured*, not
  assumed. Build the DISABLED configurations too (`-DENABLE_ASIO=OFF`,
  `-DDECKBOY_INPROC_DECODE=OFF`): the default local build hides link errors that
  only the other platforms hit. Push before tagging so CI rules on all three.
- SDL 3.4 is the floor (the recording readback needs the SDL_GPU texture
  property, absent in 3.2).
- `#ifndef _WIN32` / `#ifdef _WIN32` guards for platform-specific code
- Copyright year: 2026; license: GPL-3.0-or-later
