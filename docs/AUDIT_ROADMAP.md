# Deckboy 0.60 — Audit Roadmap

This document is a task map for agents working on cleanup, optimization, and
portability in the `deckboy-0.60` branch. Each section is a self-contained work
item with ownership boundaries, dependencies, and acceptance criteria.

## Completed

### [DONE] Companion race condition fix
- **What:** Added `companionClientsMutex_` protecting `companionClients_` and
  `companionClientBuffers_` in both `companionLoop()` (network thread) and
  `stopCompanionControl()` (main thread). Select timeout reduced from 200ms to
  100ms.
- **Files:** `native/main.cpp` (member decl ~35348, companionLoop ~16532,
  stopCompanionControl ~15323)

### [DONE] Pre-converted color palette + visual overhaul
- **What:** Added `Palette pal` struct with `rebuildPalette()` converting from
  `kConstant` uint32s. Fixed critical bug where `rebuildPalette()` was a no-op
  (self-assigned). Differentiated 10 palette roles (shell vs LCD). Added beveled
  `drawUIPanel()`/`drawFramedPanel()` (raised/inset auto-detected from luma).
  Added scanline overlay. Created 5 Game Boy generation themes.
- **Files:** `native/main.cpp` (palette ~962, drawUIPanel ~34406, scanline init
  ~6924), `native/core/constants.hpp`, `native/render/primitives.cpp`,
  `data/themes/{gameboy,pocket,color,advance,sp}/theme.txt`

### [DONE] Subprocess layer refactor
- **What:** Unified `spawnProcess()` API with `SpawnOptions` / `StdioMode`.
  Legacy wrappers preserved. `spawnDetachedProcess()` moved out of main.cpp.
- **Files:** `native/core/subprocess.hpp`, `native/core/subprocess.cpp`

---

## In Progress / Ready to Pick Up

### [DONE] Migrate colorFromRgba() call sites to pal.*
- **What:** Replaced all ~1247 `colorFromRgba(kConstant)` calls with `pal.*`
  members. 15 non-palette `colorFromRgba()` calls remain (function definition +
  dynamic cue colors from project data).
- **Files:** `native/main.cpp`

### [DONE] Extract duplicated inspector lambdas + snprintf formatting
- **What:** Created `InspectorCtx` struct + 15 shared `insp*()` member
  functions on App (~line 34529). Both docked and floating inspector paths now
  use thin wrapper lambdas that delegate to these shared helpers. Layout
  differences (inset, fonts, ellipsize, gap sizes) are parameterized through
  InspectorCtx. `fmtFloat()`/`fmtPercent()` use `snprintf` instead of
  `std::ostringstream`, eliminating per-frame heap allocations for float
  formatting.
- **Removed:** ~400 lines of duplicated lambda implementations across both
  inspector render paths.
- **Files:** `native/main.cpp` (shared helpers ~34529, docked wrappers ~20720,
  floating wrappers ~23240)

### [DONE] Eliminate per-frame ostringstream allocations
- **What:** Converted `formatSeconds()` and `formatTimecode()` from
  `std::ostringstream` to `snprintf`. Replaced inspector `spdSS` and
  `doubleMixedLabel` ostringstream patterns with `fmtFloat()`. Eliminates
  heap allocations in render hot paths.
- **Files:** `native/main.cpp` (~line 228 formatSeconds, ~line 240
  formatTimecode, inspector helpers)

### [DONE] Async ffprobe for cue loading
- **What:** `importPaths()` now creates placeholder cues immediately and
  launches `probeCue()` via `std::async`. Probe futures polled in `update()`
  with `wait_for(0ms)`. Cue metadata filled in when probe completes. Cue rows
  show "probing..." indicator while pending (`width==0 && height==0`).
- **Files:** `native/main.cpp` (~line 33579 importPaths, ~line 18853 update
  polling, ~line 20405 probing indicator, ~line 35165 PendingProbe struct)

### [DONE] Cache cue row display strings
- **What:** Added `CueRowDisplayCache` struct + `cueRowDisplayCache_`
  (unordered_map by cue ID). Caches `cueDisplayToken`, `toUpper(cueKindLabel)`,
  `ellipsizeToPixelWidth(name)`, and metadata line per cue row. Self-
  invalidating: compares input fields (name, nameW, cueId, cueNumber, index,
  kind, duration, stillDurationSeconds, endAction, width, height, path) and
  recomputes only on change. Cleared on project load/new.
- **Files:** `native/main.cpp` (~line 20376 renderCueRow, ~line 35217
  CueRowDisplayCache struct)

### [DONE] Reorder Cue struct members for cache efficiency
- **What:** Grouped Cue struct members by alignment: strings (20), vectors (2),
  doubles+uint64 (11), floats (15), ints/enums (9), SDL_Color (3), bools (7).
  Eliminates ~40 bytes of inter-member padding per Cue instance.
- **Files:** `native/core/types.hpp`
- **Verified:** No `offsetof` usage in codebase. Serialization uses explicit
  field names, not layout. Build clean.

### [DONE] Consolidate duplicate trim/toLower utilities
- **What:** Replaced local `trim()`/`toLower()` definitions in
  `capture_backend.cpp` and `output_backend.cpp` with `using` declarations
  from `core/utils.hpp`.
- **Files:** `native/platform/capture_backend.cpp`,
  `native/platform/output_backend.cpp`

### [DONE] Waveform cache lookup helper
- **What:** Added `getWaveformPeaks(path, pending)` method. Replaced 8
  lock-guard + find + count blocks across render paths with single helper call.
- **Files:** `native/main.cpp` (~line 13741)

---

### [DONE] Windows subprocess implementation
- **What:** `spawnProcess()` fully implemented for `_WIN32` using
  `CreateProcessW` + `_open_osfhandle` to expose the pipe read end as a
  POSIX-style fd. FFmpeg/FFprobe resolution via `DECKBOY_FFMPEG` /
  `DECKBOY_FFMPEG_DIR` env vars, plus `paths::` fallback walk.
- **Files:** `native/core/subprocess.cpp`

---

## Future / Roadmap

### Settings click dispatch table
- **Scope:** `native/main.cpp` (action constants), `app_render_settings.ipp`
  (`handleSettingsClick` Part1/2/3)
- **What:** Replace the three chained if-else functions (split only to dodge
  MSVC C1061) and the 636+ integer action constants with a registration
  table (`unordered_map<int, handler>` or command objects). Kills the C1061
  hazard and the "allocate next id from N+" bookkeeping in one move.
- **Risk:** large mechanical refactor; do it in one dedicated pass with
  smoke + manual settings sweep.

### Key–value record serialization
- **Scope:** `saveProject`/`loadProject` Deck/Cue/OutputTarget records
- **What:** Records are positional tab-delimited with `fields.size() >= N`
  guards; one mis-ordered append silently shifts every later field. The
  project header is already key–value (`title\t...`) — migrate records to
  the same style with a legacy-positional read path for old files.

### Async egress readback
- **Scope:** `app_render_output.ipp` egress capture
- **What:** `SDL_RenderReadPixels` per output per frame stalls the GPU
  pipeline; fine at 1080p, won't scale to 4K multi-output. Needs
  double-buffered async readback (or a CPU-side compositor for egress).
  Note AOI still doesn't apply to NDI/DeckLink egress (see DEVNOTES AOI
  note) — fold that in here.

### Standardize namespace (deckboy:: vs deckboy::)
- **Scope:** All `native/platform/*.cpp`, `native/core/utils.*`
- **What:** Platform backends use `deckboy::platform`, core uses
  `deckboy::core::utils`. Pick one.

### Include guard standardization
- **Scope:** All headers in `native/`
- **What:** Mixed `#pragma once` and `#ifndef` guards. Standardize on
  `#pragma once`.

---

## Ownership Rules for Parallel Agents

- **main.cpp is a shared resource.** If two agents need it, coordinate by
  function/line-range ownership. Prefer disjoint edits.
- **Headers in native/core/ and native/platform/ are safe for one agent at a
  time** unless edits are to different files.
- **CHANGES.md, DEVNOTES.md, PORTABILITY.md** — append-only during parallel
  work; merge conflicts are trivial.
- **Build check:** Every agent must `cmake --build build -j4` and
  `--self-check` before declaring done.
