# SDL2 → SDL3 Migration — Readiness Spec

Status: **PLANNING (no code changes yet).** This is **Session 1** of Option B in
`docs/GPU_DECODE_PLAN.md` (§3, §4.1, §11): migrate to SDL3 first, then implement
in-process GPU decode on the SDL3 base straight to zero-copy. Keep the two in
**separate sessions** — never bundled.

Drafted 2026-07-04 against v0.76.29, branch `codex/final-name-deckboy`.
Motivation carried from the GPU-decode plan: SDL2 is maintenance-only, and SDL3's
`SDL_CreateTextureWithProperties` (`SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER`)
is what makes decode zero-copy trivial afterward.

---

## 1. Prerequisites

- [x] **DONE (2026-07-04):** `vcpkg install sdl3 sdl3-ttf:x64-windows` →
      **SDL3 3.4.0** + SDL3_ttf installed at `C:\Users\user\vcpkg\installed\x64-windows`
      (headers, `SDL3.lib`/`SDL3_ttf.lib`, `SDL3Config.cmake`/`SDL3_ttfConfig.cmake`).
      **Verified against the installed headers** — all three plan-critical symbols
      are present:
      - `SDL_CreateTextureWithProperties` + `SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER`
        (`"SDL.texture.create.d3d11.texture"`) → **zero-copy decode is real**, not assumed.
      - `SDL_SetRenderVSync` → per-renderer vsync decouple.
      - `SDL_PIXELFORMAT_NV12` → decode-upload / texture-import format.
- Keep SDL2/SDL2_ttf installed until the migration builds green (parallel, then
  remove). CMake `find_package(SDL3 CONFIG)` / `SDL3::SDL3`, `SDL3_ttf::SDL3_ttf`.
- libav dev libs already installed (for the *next* session) — no action here.

---

## 2. Call-surface inventory (measured, v0.76.29)

Real counts across `native/**` (terrarium.cpp excluded — it's a companion exe,
migrate separately or leave on SDL2):

| Area | Volume | Nature | Where |
|------|-------:|--------|-------|
| `SDL_Rect` refs | 959 | float-coord (`SDL_FRect`) churn | concentrated in render `.ipp`s (settings 197, main.ipp 187, main.cpp 133, control 131, inspector 114) |
| Draw calls | ~270 | rename + signature | `SetRenderDrawColor`×60, `SetRenderDrawBlendMode`×80, `RenderFillRect`×26, `RenderCopy`×19, `RenderDrawLine`×38, `RenderSetClipRect`×35 — nearly all via `Primitives::*`/`drawText`/`drawUIPanel` |
| Audio | ~30 | **semantic redesign** | `SDL_PauseAudioDevice`×16, `SDL_AudioDeviceID`×13, `SDL_AudioSpec` — `media_engine.cpp` |
| TTF | ~50 | rename | `TTF_SizeUTF8`×28, `TTF_RenderUTF8_Blended`×8, `TTF_OpenFont`×6, `TTF_Init`/`TTF_FontHeight`/`TTF_CloseFont` |
| Events | ~40 | field renames | single `SDL_PollEvent` loop in `app_input.ipp`; `event.button`×19/`key`×7/`wheel`×6/`motion`×5/`window`×2 |
| Window/init/hints | ~small | signature + model change | `SDL_Init` (main.cpp:3733), `SDL_CreateWindow` (:3754/:3790), `SDL_CreateRenderer` (:3769/:3801), 3 hints (:3697/:3704/:3742) |

**Unchanged / free:** `SDL_Color` (554 refs) is identical in SDL3. That whole
chunk needs no edits.

---

## 3. Migration strategy by area

### 3.1 Render layer — bulk, mostly mechanical
- `SDL_Rect` → `SDL_FRect`; ints → floats at draw time. Most geometry stays int
  in layout structs (`render/layout.hpp`) and converts at the draw boundary —
  decide: convert helpers to take `SDL_FRect`, or keep int layout + cast in
  `Primitives::*`. **Leaning: keep int layout, convert inside the helper layer**
  so the 959 sites don't all change — only the handful of helpers do.
- Renames (scriptable via SDL's migration guide table):
  `SDL_RenderCopy`→`SDL_RenderTexture`, `SDL_RenderCopyEx`→`SDL_RenderTextureRotated`,
  `SDL_RenderFillRect`/`SDL_RenderDrawRect`/`SDL_RenderDrawLine`→ `SDL_RenderFillRect`/
  `SDL_RenderRect`/`SDL_RenderLine` (now float), `SDL_RenderSetClipRect`→
  `SDL_SetRenderClipRect`, `SDL_RenderSetScale`→`SDL_SetRenderScale`.
- `SDL_RENDER_SCALE_QUALITY` hint is gone → per-texture `SDL_SetTextureScaleMode`
  (`SDL_SCALEMODE_NEAREST` to preserve the current `"0"` = nearest behaviour).
- **Bonus:** `SDL_CreateRenderer` loses the flags int; vsync becomes
  `SDL_SetRenderVSync(renderer, 1|0)` **per renderer** — which cleanly enables
  the earlier perf idea (decouple control-window vsync from output vsync) as a
  first-class call instead of a creation flag.

### 3.2 Audio — the semantic core (highest risk)
- SDL2 pull-callback / `SDL_OpenAudioDevice`+`SDL_AudioSpec.callback` →
  SDL3 `SDL_AudioStream`: open a logical device, bind an `SDL_AudioStream`, and
  either keep a callback (`SDL_SetAudioStreamGetCallback`) or push with
  `SDL_PutAudioStreamData`.
- **A/V-master clock must be re-established** (DEVNOTES "Audio-Master A/V Clock"):
  the video position slaves to the audio device clock. Determine the SDL3
  equivalent of "frames consumed by the device" (queued vs played) and re-anchor
  `audioClock`/correction logic. This is the part to prototype in isolation first.
- `SDL_PauseAudioDevice(dev, 1|0)` → `SDL_PauseAudioDevice(dev)` /
  `SDL_ResumeAudioDevice(dev)` (16 sites, mechanical once the model is settled).
- **Locate the exact open/callback site** (not surfaced by grep — likely
  `media_engine.cpp` audio init); it's the anchor for this whole section.

### 3.3 TTF — small, mechanical
- `TTF_SizeUTF8`→`TTF_GetStringSize`, `TTF_RenderUTF8_Blended`→
  `TTF_RenderText_Blended` (now takes a length arg), `TTF_FontHeight`→
  `TTF_GetFontHeight`. `TTF_OpenFont`/`TTF_Init`/`TTF_CloseFont` roughly stable.
- Preserve `applyUiScale` font-reload flow (`fontSmall_`/`fontMono_`/`fontPixelSmall_`).
- Optional later: SDL3_ttf text-engine/HarfBuzz — **out of scope**, keep the
  simple render-to-texture path.

### 3.4 Events / init / window / hints
- `SDL_Init` returns **bool** (true=success) not 0 — invert the check
  (main.cpp:3733). `SDL_INIT_EVENTS` is implied by video.
- `SDL_CreateWindow(title, x, y, w, h, flags)` → `SDL_CreateWindow(title, w, h,
  flags)`; **position set separately** (`SDL_SetWindowPosition`). Affects both
  windows (:3754/:3790) and any output-window creation in `app_output_mgmt.ipp`.
- Event fields: `event.button.x/y` are float; `event.key` drops `.keysym`
  (scancode/key are direct fields); window events are now **top-level
  `SDL_EVENT_WINDOW_*` types**, not `event.window.event` subtypes — the
  `app_input.ipp` dispatch and any `SDL_WINDOWEVENT_*` handling get restructured.
- Hints: `SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS` **still exists in SDL3** — keep
  it (the whole fullscreen saga depends on it). `SDL_HINT_WINDOWS_DPI_AWARENESS`
  is largely automatic in SDL3 (verify/remove). `SDL_HINT_RENDER_SCALE_QUALITY`
  → per-texture scale mode (§3.1).

---

## 4. Risk areas / gotchas (Deckboy-specific)

1. **Fullscreen model changed.** SDL3 unifies fullscreen:
   `SDL_WINDOW_FULLSCREEN_DESKTOP` (15 refs) + `SDL_WINDOW_FULLSCREEN` (11) →
   `SDL_SetWindowFullscreenMode(win, NULL)` for borderless-desktop vs a specific
   `SDL_DisplayMode` for exclusive. This **directly touches the hard-won
   fullscreen-recovery logic** in `app_output_mgmt.ipp` (the minimize-on-focus-loss
   / wrong-display / exclusive-vs-borderless saga in DEVNOTES). **Highest-risk
   behavioural area** — re-verify the taskbar/focus/hot-plug behaviour end-to-end
   after migrating (see the 2026-07-04 field incident: borderless output must
   stay above an auto-hide taskbar when focused).
2. **NV12 texture support** — confirm the SDL3 D3D11 renderer still advertises
   `SDL_PIXELFORMAT_NV12` (the decode-upload path and Phase-2 texture-import both
   depend on it). `rendererSupportsTextureFormat()` already probes formats.
3. **Blend modes** — `SDL_SetRenderDrawBlendMode`×80 + `SDL_SetTextureBlendMode`
   ×17 survive with same enum names; verify custom blend (if any) still composes.
4. **Render targets / clip** — `SDL_SetRenderTarget`, `SDL_SetRenderClipRect`
   (now float) used by the compositor + egress capture; re-verify AOI/warp.
5. **Multi-window + per-renderer vsync** — decoupling control vs output vsync
   (perf win) is now `SDL_SetRenderVSync` per renderer; sequence it here.
6. **`SDL_GetTicks64`×70 → `SDL_GetTicks`** (SDL3 `SDL_GetTicks` is already 64-bit).

---

## 5. Validation plan

- `--smoke` / `--self-check` green.
- **Full visual pass** of the control UI (fonts, panels, inspector, timeline,
  settings modal) — the render-layer churn is broad.
- **Fullscreen/taskbar/focus re-verification** on the mixed-DPI two-display
  setup + the 720p dongle — the §4.1 risk. Reproduce the recovery cases in
  DEVNOTES and the 2026-07-04 auto-hide-taskbar incident.
- **Audio A/V sync** soak (10+ min) — the §3.2 clock re-anchor.
- Multi-output (window + NDI/Spout egress) still composites and captures.
- Ship a **stable SDL3 build** before starting the decode session.

---

## 6. Execution sequence

Maps to `GPU_DECODE_PLAN.md` §11 "Session 1":
1. Install SDL3/SDL3_ttf; parallel CMake (`DECKBOY_SDL3` option, or hard swap on a
   branch). Keep SDL2 build until green.
2. Init/window/renderer/hints (§3.4) — get a window on screen first.
3. Render helper layer (`Primitives::*`/`drawText`/`drawUIPanel`) → float +
   renames; script the bulk, hand-fix stragglers. Get the control UI drawing.
4. Events (§3.4) — restore input.
5. TTF (§3.3) — restore text.
6. Audio (§3.2) — the careful one; prototype the clock re-anchor in isolation.
7. Fullscreen/output-window model (§4.1) — re-verify the recovery saga.
8. Per-renderer vsync decouple (perf bonus).
9. Full validation (§5); remove SDL2; update DEVNOTES/CODEMAP/CLAUDE.md.

Then → Session 2: in-process GPU decode on the SDL3 base (GPU_DECODE_PLAN §7–§11).

---

## 7. Open questions

1. **Migrate `extras/terrarium.cpp` too, or leave on SDL2?** It's a separate exe;
   leaving it on SDL2 means shipping both runtimes. Leaning: migrate for a single
   SDL3 runtime, low priority.
2. **Hard swap vs `DECKBOY_SDL3` option build?** A parallel option doubles the
   render-layer `#ifdef` noise; a clean branch swap is likely simpler given the
   render helpers are centralized. Leaning: branch swap, no long-lived option.
3. **Keep pull-callback audio or switch to push (`SDL_PutAudioStreamData`)?**
   Push may simplify the A/V-clock anchor. Decide during the §3.2 prototype.
