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
| `native/platform/siphon_spout.hpp/cpp` | Spout (Windows) / Syphon (macOS) texture sharing |
| `CHANGES.md` | User-facing changelog |
| `DEVNOTES.md` | Internal architectural decisions (must be kept updated) |
| `docs/CODEMAP.md` | Full structural code map: file inventory, data flow, threading model |
| `docs/VERSION_FLOW.md` | Version flow doc |
| `tools/package_windows.ps1` | Build portable `dist\Deckboy-<VERSION>-windows-x64.zip` |
| `native/core/system_browser.hpp` | Cross-platform `openExternalUrl()` for dep prompts |
| `native/app/app_overlays.ipp` | `renderDependencyPrompt()` + detection helpers (`ndiRuntimeAvailable` etc.) |

---

## Settings Action Constants

Settings button actions are integer constants defined at the top of `main.cpp`. Current highest in the sequential range: **645** (`kSettingsActionVideoSubTabBase` 642–645). Allocate next from **647+**. WARNING: ids 634–637 were once double-allocated, which silently killed whichever button's handler ran second (the "Processing sub-tab does nothing" bug, v0.76.24). Before allocating, grep the value: `grep "= <id>;" native/main.cpp`. High ranges in use: 800+ (display select), 20000+ (routing tables).

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

---

## MediaEngine Internals

- `loadCue()` → sets `activeCue_`, geometry, calls `initStillTimer` for still-type cues, then `startDecoderThreads` or `loadStillFrame`/`loadPatternFrame`
- `startDecoderThreads()` → spawns ffmpeg video + audio subprocesses as pipe sources. Picks `nv12` pipe format when the cue has no chroma key / color controls, otherwise `rgba` (so the CPU effects path stays valid). Scaler is `fast_bilinear` — bicubic is too expensive and visually indistinguishable on moving video.
- `startBrowserFrameMode()` → called when first browser frame arrives; must preserve `duration_` from `activeCue_->stillDurationSeconds`
- `visualFadeGainAt()` → returns 1.0 when no duration or fade defined; suppressed for auto-advance cues. Evaluated at `position()`, so paused stills MUST keep a sane `currentPosition_` (held at `pausedPosition_`, not 0) or the fade-in ramp drives the held frame to 0 alpha — see DEVNOTES `Still Cue Hold / Fade Interaction`.
- Still-type cues (`isDefaultStillDurationCueKind`) default to `fadeOutSeconds = 0` in `applyDeckDefaultsToCue` so a held graphic doesn't dip to black; per-cue fade-out still works if enabled.
- `suppressVisualFadeOutForCurrentCue_` → set `true` for auto-advancing cues (crossfade handles the outgoing visual)
- **The engine OWNS its cue** (v0.76.19): `activeCue_` points at `activeCueSnapshot_`, never into `Deck::cues`. App edits reach the engine via `markProjectDirty()` → `syncEngineCueSnapshots()` next tick. The audio thread reads fades only through atomic mirrors (`syncAudioFadeParams`/`audioFadeGainAt`). Video position slaves to the audio device clock when audio is present (see DEVNOTES `Audio-Master A/V Clock`).
- Frame pipeline: ffmpeg stdout → `readExact` → `frameQueue_` → `uploadFrame()` → SDL_Texture
- `DecodedFrame::format` (`FramePixelFormat`) is the canonical flag: every upload site reads it to pick `SDL_UpdateTexture` (RGBA32) vs `SDL_UpdateNVTexture` (NV12), and to recreate the cached texture when the format changes. Helper: `syncFrameTexture()` in `render/texture_helpers.hpp`. See DEVNOTES `GPU Hardware Decode + NV12 Upload Path` for the full architecture.

---

## UI Patterns

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

## Key Conventions

- No new files unless strictly necessary — extend existing `.ipp`/`.cpp` instead
- AOI, warp, edge blend: per-output, in `OutputTarget`
- Cue geometry (scale/offset/crop/rotation): per-cue, in `Cue`
- All color/geometry values normalized (0–1 for fractions, degrees for rotation)
- Windows-first for new features (primary dev target)
- `#ifndef _WIN32` / `#ifdef _WIN32` guards for platform-specific code
- Copyright year: 2026; license: GPL-3.0-or-later
