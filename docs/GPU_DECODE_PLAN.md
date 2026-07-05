# In-Process GPU Decode — Design & Readiness Plan

Status: **PLANNING (no code changes yet)**. Execution deferred to a later
session (Fable). This document is the readiness spec so that session is pure
execution.

Author context: drafted with Opus, 2026-07-04, against v0.76.29 on branch
`codex/final-name-deckboy`. Motivation: Deckboy is "barely stable" on the GPD
Pocket 3 (fanless Tiger Lake, Iris Xe iGPU, 4 threads, 720p HDMI dongle output).

---

## 1. Objective & non-goals

**Objective:** Replace the per-deck `ffmpeg.exe` *subprocess pipe* decode path
with an *in-process* libav\* decoder, and keep decoded video frames on the GPU
as far as SDL allows — eliminating the GPU→CPU→pipe→CPU→GPU round trip that
dominates the per-frame budget on the Pocket.

**Non-goals (this project):**
- Not removing FFmpeg. We are *embedding* it (libav\* libraries) instead of
  shelling out. See §5.
- Not porting encode-out / ffprobe / waveform / dshow enumeration off the CLI
  (they stay on `ffmpeg.exe` for now — see §5).
- Not the cheap in-pipe wins (thread cap, skip no-op scale). Those are a
  separate small diff, bundled into the same execution session but tracked
  independently. See §6.

---

## 2. Baseline — what we are killing

Current path (`native/engine/media_engine.cpp:startDecoderThreads`,
video args at `:1941`, audio at `:2021`):

```
iGPU decode (ffmpeg -hwaccel auto picks d3d11va)
  → ffmpeg auto hwdownload: GPU surface → CPU NV12 buffer     [readback stall]
  → CPU swscale fast_bilinear → decodeW×decodeH               [per-frame CPU]
  → raw NV12 bytes over stdout pipe (~1.4 MB/720p frame)      [memcpy+syscalls]
  → parent readExact → frameQueue_ (kMaxVideoFrames=6)
  → SDL_UpdateNVTexture: CPU NV12 → GPU texture               [re-upload]
  → composite (app_render_output.ipp) → present
```

Cost drivers on the Pocket, in order:
1. **Two ffmpeg processes per active deck** (video + audio), each with its own
   thread pool competing with the render loop on a 4-thread CPU.
2. **The GPU→CPU download** ffmpeg inserts automatically (no
   `-hwaccel_output_format` set, so frames come back to system memory).
3. **CPU swscale** every frame.
4. **Pipe bandwidth** — ~41 MB/s at 30fps, ~83 MB/s at 60fps, per deck.
5. **CPU→GPU re-upload** via `SDL_UpdateNVTexture`.

Decode itself is already on the GPU today (`-hwaccel auto`). The *transport*
is the waste.

---

## 3. Decision — chosen architecture

**Full in-process libav decode** (operator's choice, accepting the loss of
subprocess crash isolation — see §9 for mitigations).

> **SEQUENCING DECISION (2026-07-04): Option B — SDL3 first, then decode.**
> Migrate Deckboy to SDL3 as its own effort, *then* implement in-process decode
> once on the SDL3 base, going **straight to zero-copy** via SDL3's D3D11
> texture-import (§4.1). Under B, the two-phase split below collapses: there is
> **no interim CPU-bridge (old Phase 1)** and **no hand-rolled D3D11 compositor
> (old Phase 2)** — the single decode implementation wraps the d3d11va texture
> as an SDL_Texture. The libav dev libs still install now (needed either way).
> SDL3 migration and decode remain **separate sessions** — never bundled.

The phased framing below is retained as background / fallback (it's what Option
A would have looked like on SDL2), because of the SDL2 interop constraint (§4):

### Phase 1 — In-process decode, SDL_Texture upload (LOW–MED risk)
Replace the two subprocesses with in-process libav decode/demux/resample.
Video frames are decoded via d3d11va, transferred to a CPU NV12 buffer we
control (`av_hwframe_transfer_data`), and uploaded via the existing
`SDL_UpdateNVTexture` path. **No SDL interop blocker.**

Wins captured: kills both subprocesses, kills the OS pipe, gives us
frame-accurate control of seek/EOF/timestamps, one decode stack. Still pays a
GPU→CPU→GPU trip, but a *controlled* one (no pipe, no second process, no
implicit swscale if we scale on GPU).

### Phase 2 — True GPU-resident video (HIGH risk/effort, optional)
Take over D3D11 compositing for the **output window(s)** so decoded d3d11va
NV12 textures never leave the GPU. See §4 Path B. Only pursue if Phase 1
telemetry on the Pocket still shows decode/upload as the bottleneck.

> Rationale for staging: Phase 1 delivers most of the process/pipe savings at a
> fraction of the risk and reuses the entire existing compositor. Phase 2 is
> where the last GPU→CPU→GPU copy dies, but it requires hand-written D3D11 and
> is the riskiest part for a live-show tool.

---

## 4. CRITICAL FINDING — SDL2 cannot ingest an external GPU texture

Verified against installed **SDL 2.32.10** (vcpkg, `C:\Users\user\vcpkg`).

- SDL2 exposes `SDL_RenderGetD3D11Device()` (get the renderer's `ID3D11Device`).
- SDL2 has **no** public API to (a) create an `SDL_Texture` from an existing
  `ID3D11Texture2D`, nor (b) fetch the backing `ID3D11Texture2D` of an
  `SDL_Texture` to `CopyResource` into it.

Consequence — the three candidate paths and the verdict:

| Path | Description | Verdict |
|------|-------------|---------|
| **A** | GPU-copy decoded NV12 into an existing SDL_Texture on the shared device | ✗ blocked — no getter for SDL_Texture's backing resource |
| **B** | Own D3D11 compositor for output windows: decode d3d11va → composite/present in our own D3D11 (NV12→RGB shader, layer blend, geometry), bypassing SDL_Renderer for output; keep SDL for control UI | ✓ true zero-copy, **Phase 2**, biggest effort |
| **C** | In-process libav decode, `av_hwframe_transfer_data` → CPU NV12 → `SDL_UpdateNVTexture` | ✓ no interop blocker, **Phase 1** |

> If we ever want zero-copy without hand-rolling D3D11, the other lever is
> **migrating to SDL3** — which turns Phase 2 from a bespoke D3D11 compositor
> into a texture-wrap. See §4.1.

### 4.1 SDL3 as the Phase 2 vehicle — sequencing

SDL3 (stable since 3.2.0, Jan 2025; SDL2 is now maintenance-only, no new
features) **fixes the exact SDL2 dead end above**:
`SDL_CreateTextureWithProperties` accepts an existing backend resource via
`SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER`. A d3d11va-decoded NV12 texture
can be **wrapped as an SDL_Texture and composited by SDL's own renderer** — no
CPU download, no hand-written D3D11 compositor. On SDL3, **Phase 2 collapses
from "write a D3D11 output compositor" into "wrap the decoded texture."** (SDL3
also ships the low-level `SDL_GPU` API, but that is a *separate* abstraction —
we stay on `SDL_Renderer`, not rewrite into SDL_GPU.)

**Migration surface in Deckboy** (whole-app; a dedicated sprint, not a weekend):
1. **Render draw calls** — float coords (`SDL_Rect`→`SDL_FRect`) + near-universal
   renames (`SDL_RenderCopy`→`SDL_RenderTexture`, etc.). Thousands of call sites,
   but funneled through `Primitives::*` / `drawText` / `drawUIPanel` /
   `syncFrameTexture()` — concentrated, largely scriptable via SDL's migration guide.
2. **Audio — semantic redesign.** SDL3 replaces the device/callback model with
   `SDL_AudioStream`; the A/V-master audio clock (DEVNOTES "Audio-Master A/V
   Clock") must be re-established on it. Trickiest part, and it **overlaps** the
   in-process decode audio rework.
3. **SDL2_ttf → SDL3_ttf** — font load/render call changes (`fontSmall_`,
   `fontMono_`, `fontPixelSmall_`, `applyUiScale`). Moderate.
4. **Events / init / types** — `SDL_Event` field changes (ns timestamps),
   `SDL_bool`→`bool`, property-based APIs, hint renames incl. ones we rely on
   (`SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS`, `SDL_HINT_RENDER_DRIVER`). Pervasive
   but mechanical; combs through `app_input.ipp` / `processEvents`.

**Sequencing options:**
- **A — Decode-first (this plan's default):** in-process decode on SDL2 now →
  SDL3 later as its own migration → Phase 2 zero-copy becomes a tiny
  texture-import change. Isolates risk, fastest Pocket relief. Cost: Phase 1's
  CPU-bridged upload is interim scaffolding SDL3 would let us skip.
- **B — SDL3-first:** migrate to SDL3 → do the decode rewrite once on the new
  base, straight to zero-copy, never build the D3D11 compositor. Cleaner end
  state, less rework; but big upfront churn before any Pocket win, front-loading
  the riskiest changes.
- **C — Never migrate:** hand-roll D3D11 for Phase 2 on frozen SDL2. Viable
  short-term; bespoke GPU code, swimming against the SDL current.

**CHOSEN: Option B (2026-07-04).** Migrate to SDL3 first, then implement decode
once on SDL3 straight to zero-copy — no interim CPU bridge, no D3D11 compositor.
The two efforts stay in **separate sessions** (SDL3 migration, then decode);
never bundled — two playback-path rewrites at once is how a live tool breaks.
Accepted tradeoff: bigger upfront migration before any Pocket win, in exchange
for the cleanest end state and zero rework.

**Immediate consequence:** the *next* body of work is the **SDL3 migration**, not
the decode rewrite. That migration deserves its own readiness spec (enumerate the
real SDL call surface across `Primitives::*`/`drawText`/audio/ttf/events, script
the mechanical renames, plan the audio A/V-clock re-establishment). Decode work
(§7–§11) follows on the SDL3 base.

**Readiness spike (do before Phase 2, not blocking Phase 1):** confirm we can
create an `ID3D11Device` compatible with (or reuse) SDL's device, decode one
file via d3d11va, and present one NV12 frame through our own D3D11 path onto
the output window. Small standalone spike; gates Phase 2 only.

---

## 5. Environment — verified state

| Thing | State |
|-------|-------|
| vcpkg root | `C:\Users\user\vcpkg` (classic mode, `VCPKG_MANIFEST_MODE=OFF`) |
| SDL2 | 2.32.10 (has `SDL_RenderGetD3D11Device`) |
| SDL2_ttf, Spout2 | vcpkg `x64-windows` |
| **libav\* dev libs** | **NOT installed** — must add (see §6) |
| ffmpeg CLI | `C:\ffmpeg\bin\ffmpeg.exe` — stays (see below) |

**FFmpeg is still needed after this project**, in two senses:
1. The libav\* *libraries* become the in-process decode engine — more tightly
   coupled than today.
2. `ffmpeg.exe` does **not** disappear, because decode is only ~half its jobs:
   - video decode — `media_engine.cpp:1941`  ← migrated in this project
   - audio decode — `media_engine.cpp:2021`  ← migrated in this project
   - single still-frame decode — `media_engine.cpp:1599`  ← candidate to migrate
   - ffprobe metadata on import — `media_engine.cpp:1877`  ← stays on CLI (for now)
   - waveform generation — `media_engine.cpp:2132`  ← stays on CLI
   - stream **encode-out** SRT/RTMP — `app_output_mgmt.ipp:1128`  ← stays on CLI
   - DirectShow device enumeration — `app_cue_mgmt.ipp:1280`  ← stays on CLI

So post-project we maintain **both** an in-process decode path and the CLI for
the rest. That dual stack is an accepted maintenance cost.

**Codec/format coverage:** identical to today (same libavformat demuxers, same
libavcodec decoders — the CLI is just a frontend over these). Zero-copy /
hwaccel applies only to GPU-decodable codecs (H.264/HEVC/VP9/AV1/MPEG-2 per the
Tiger Lake gen); everything else (ProRes, DNxHD, image seqs, exotic) falls back
to software decode + CPU upload — i.e. no worse than today. We lose no formats.

**Licensing:** libav\* is LGPL-2.1+ (GPL only with `--enable-gpl`). Deckboy is
GPL-3.0-or-later → compatible with linking either. We must bundle the `libav*`
DLLs and carry their license notices. Nothing becomes proprietary; stays open.

---

## 6. Readiness checklist (things to do BEFORE the execution session)

These prep the ground without rewriting decode. `[ ]` = todo.

- [ ] **Install libav dev libs** (the gating prerequisite). Classic-mode vcpkg:
      `vcpkg install ffmpeg[avcodec,avformat,avdevice,swscale,swresample]:x64-windows`
      — confirm the port includes d3d11va (Windows default). Large build/download;
      run when convenient. **Ask the owner before running** (changes his toolchain).
- [ ] **CMake option scaffolding** (design only in this doc; code lands in
      execution session): a `DECKBOY_INPROC_DECODE` option that, when OFF, keeps
      the current CLI path. Lets us ship a fallback build. → §8.
- [ ] **Baseline benchmark harness** (execution-session task, but design now):
      a `--decode-bench <file>` mode logging avg decode+upload ms/frame, dropped
      frames, and process count, so Pocket before/after is measurable. Without
      this we can't prove Phase 1 helped or that Phase 2 is warranted.
- [ ] **Phase 2 D3D11 spike** (§4) — standalone, gates Phase 2 only.
- [ ] **Codec test corpus** — collect on the Pocket: H.264, HEVC, VP9, AV1 (if
      supported), plus a ProRes/DNxHD file to exercise the software fallback,
      plus a deliberately corrupt file for the crash-resilience test (§9).

---

## 7. Phase 1 component design (in-process libav)

New in-process decoder to replace `startDecoderThreads`'s two subprocesses.
Suggested home: extend `media_engine.cpp` (per §CLAUDE.md "no new files unless
strictly necessary") or a focused `engine/libav_decoder.hpp/.cpp` if it keeps
`media_engine.cpp` readable — decide at execution time.

Responsibilities, mapping 1:1 to what the CLI did for us for free:

1. **Demux** — `avformat_open_input` / `avformat_find_stream_info`; pick best
   video + audio streams (`av_find_best_stream`). Replaces `-i path`, `-map`.
2. **HW decode** — set up an `AVBufferRef` d3d11va `hw_device_ctx`, negotiate
   `get_format` to `AV_PIX_FMT_D3D11`. Software fallback when unsupported.
   Replaces `-hwaccel auto`.
3. **Scale** — Phase 1: after `av_hwframe_transfer_data` to CPU, use libswscale
   (mirror current `fast_bilinear`); **skip entirely when src size == output
   size** (this also fixes the current always-on no-op scale). Phase 2 moves
   scale to GPU.
4. **Pixel format** — preserve the per-cue NV12-vs-RGBA decision (chroma key /
   color controls → RGBA for the CPU effects path). `DecodedFrame::format` and
   all six upload sites stay as documented in DEVNOTES. NV12 stays the fast path.
5. **Audio** — decode audio packets, resample with libswresample to the SDL
   audio device format. Replaces the audio ffmpeg subprocess (`:2021`). The
   audio-device clock stays the A/V master (DEVNOTES "Audio-Master A/V Clock").
6. **Transport** — seek via `av_seek_frame` + codec flush (replaces `-ss`);
   accurate frame stepping; EOF handling drives `decoderEof_`.
7. **Threading** — one demux/decode worker per deck feeding the existing
   `frameQueue_` (keep `kMaxVideoFrames` backpressure). Preserve the atomic
   fade mirrors the audio thread reads (`syncAudioFadeParams`).
8. **Live sources** — SRT/NDI cues: libavformat can open these too, but Phase 1
   may keep live streams on the CLI to avoid latency/format surprises (they
   already skip `-ss`/hwaccel). Decide per §13.

Everything downstream of `frameQueue_` (compositor, effects, egress, texture
upload) is **unchanged in Phase 1**.

---

## 8. Exact code map

Migrated in this project:
- `media_engine.cpp:startDecoderThreads` (video `:1941`, audio `:2021`) →
  in-process decoder. Biggest change.
- `media_engine.cpp:1599` still-frame decode → candidate (nice-to-have).
- Frame pipeline `readExact → frameQueue_` → replaced by decoder pushing frames
  directly into `frameQueue_` (same queue, same `DecodedFrame`/`format` tag).
- `core/subprocess.*` / `io_utils.readExact` — no longer used by *decode* (still
  used by the CLI paths that remain).

Unchanged:
- `app_render_output.ipp` compositor, all six `SDL_*Texture` upload sites,
  `syncFrameTexture()`, egress readback, effects (`applyCueVisualEffectsToPixels`).
- All remaining CLI callers in §5.

Build:
- `CMakeLists.txt` — `find_package`/link libav\*; new `DECKBOY_INPROC_DECODE`
  option (default? decide — likely ON once stable, with CLI fallback build).
- `tools/package_windows.ps1` — bundle `avcodec-*.dll`, `avformat-*.dll`,
  `avutil-*.dll`, `swscale-*.dll`, `swresample-*.dll` + license notices.

---

## 9. Crash-resilience mitigations (we are giving up process isolation)

This is the single biggest downside of the in-process choice for a live tool:
a libav segfault on a bad frame now takes the **whole app** black on stage. We
cannot `try/catch` a native crash. Mitigations to build in from day one:

1. **Validate before decode** — probe container/streams, reject clearly-bad
   inputs at import (we already ffprobe on import; tighten it).
2. **Decode watchdog** — a monitor that flags a stalled/looping decode thread
   and forces the deck to RERACK (dark hold) rather than hang. Deckboy already
   has STOP=rerack semantics to build on.
3. **Guard the hot path** — bounds-check every buffer size against
   `frameBufferSize()`; never trust decoder-reported dimensions blindly.
4. **Windows SEH wrapper (evaluate)** — `__try/__except` around the decode call
   can convert some access violations into recoverable errors. Fragile and
   compiler-specific; prototype and measure, don't assume.
5. **Corrupt-file test in CI/smoke** — the deliberate corrupt file from §6 must
   *not* crash the app; it must rerack the deck and toast an error.
6. **Keep the CLI-fallback build** (`DECKBOY_INPROC_DECODE=OFF`) as the
   break-glass option for a show where robustness outranks Pocket performance.

---

## 10. Testing & validation plan

- `--smoke` / `--self-check` still green (headless-tolerant engine).
- **Codec matrix** on the Pocket: H.264/HEVC/VP9/AV1 hw path + ProRes/DNxHD sw
  fallback all play, seek, loop, and A/V-sync correctly.
- **A/V sync** unchanged (audio-master clock) — measure drift over 10 min.
- **Seek accuracy** vs current CLI `-ss` behaviour.
- **Corrupt/edge-case file** → rerack, no crash (§9).
- **Leak/handle audit** — libav refcounted frames/packets; run a soak.
- **Thermal/stability on the Pocket** — the actual goal: sustained multi-cue
  playback without throttle-to-instability; compare to baseline (§6 harness).

---

## 11. Execution sequence (Option B — SDL3 first)

**Session 0 (readiness, now):** install libav dev libs (§6, in progress); write
the dedicated **SDL3 migration readiness spec** (separate doc).

**Session 1 — SDL3 migration (its own effort):**
1. SDL3 + SDL3_ttf via vcpkg; `find_package`/link.
2. Mechanical render renames (float coords, `SDL_RenderTexture`, etc.), mostly
   scripted, concentrated in `Primitives::*`/`drawText`/`drawUIPanel`/`syncFrameTexture()`.
3. Audio port to `SDL_AudioStream`; **re-establish the A/V-master clock** (DEVNOTES).
4. SDL3_ttf font path (`fontSmall_`/`fontMono_`/`fontPixelSmall_`/`applyUiScale`).
5. Events/init/hints (incl. `MINIMIZE_ON_FOCUS_LOSS`, `RENDER_DRIVER`); `app_input.ipp`.
6. Full smoke + visual pass; ship a stable SDL3 build before touching decode.

**Session 2 — in-process decode on SDL3 (straight to zero-copy):**
7. `--decode-bench` harness; capture Pocket baseline on the SDL3 build.
8. In-process libav **video** decode via d3d11va → wrap the NV12 texture with
   `SDL_CreateTextureWithProperties`/`SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER`
   (zero-copy — no CPU download, no compositor rewrite). Software+CPU-upload
   fallback for non-hwaccel codecs.
9. In-process **audio** decode + libswresample; wire to the SDL3 audio clock.
10. Transport (seek/EOF/flush); preserve NV12/RGBA per-cue decision.
11. Crash-resilience: watchdog, validation, corrupt-file smoke (§9).
12. Bench on Pocket vs baseline; packaging (bundle libav + SDL3 DLLs + licenses);
    update DEVNOTES/CODEMAP/CLAUDE.md.
13. Also land the cheap wins (thread cap; skip-no-op-scale absorbed into §7.3).

---

## 12. Bundled cheap wins (do in the same session)

Independent of the rewrite, low-risk, immediate Pocket relief:
- **Cap ffmpeg/libav threads** on the Pocket so decode doesn't starve the
  render loop (counterintuitively improves smoothness on 4 threads).
- **Skip the scale filter when src size == output size** — absorbed into §7.3
  for the in-process path; also trivially applies to the CLI path if we keep it.

---

## 13. Open questions / decisions still needed

1. **Live sources (SRT/NDI)** — migrate to libavformat in Phase 1, or keep on
   CLI? Leaning: keep on CLI initially (latency/format risk).
2. **`DECKBOY_INPROC_DECODE` default** — ON with CLI fallback, or opt-in until
   soak-tested?
3. **Still-frame decode (`:1599`)** — migrate now or leave on CLI?
4. **Phase 2 vehicle — RESOLVED: Option B (SDL3-first).** Migrate to SDL3, then
   decode once on it straight to zero-copy via texture-import. Separate sessions.
   Full analysis in §4.1. Next artifact: a dedicated SDL3 migration readiness spec.
5. **SEH wrapper** — worth the fragility for partial crash recovery? Prototype
   before deciding.
