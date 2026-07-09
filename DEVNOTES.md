# DEVNOTES

## Pocket Test Card (v0.78.1, diegetic rework v0.78.2)

`pocket-test` (the auto-cycling default pattern) is Deckboy's working test
card: `drawPocketTestCardOverlay` in media_engine.cpp draws the instruments
over the island scene. the owner's direction: instruments must be DIEGETIC —
scene objects (billboard = color bars, staircase = grayscale, sky banner =
banding ramp, cave eyes = 2%/4% black-crush, cloud lumps = 98%/96% white
clip, flashing ? block = cadence, beach TV static = fine detail, runner past
10%-spaced fence posts = judder), never chart furniture. The Pokémon-style
dialog box (chunky `gbBox` chrome, cream/ink/red palette, encounter text,
blinking continue-cursor) is the game-UI layer. The forced-scene variants
(`pocket-day` etc.) deliberately do NOT get the overlay — operators use them
as backgrounds. Measurement values must stay EXACT (75% = 191, 2% = 5, etc.)
and the pixel-precision elements (border checkerboard, TV static) must never
be scaled by the proportional unit `u` — single pixels are the point. Text
uses a built-in 3x5 pixel font (`glyphRows` lambda) since patterns are raw
CPU pixels with no TTF access. `--pattern-dump <id> <out.ppm> [WxH] [t]`
renders any pattern for visual inspection; smoke scans for the exact
diegetic values (75% red, 2% eyes, 96% lump) + the border checker, and
asserts scene variants stay clean.

## In-Process GPU Decode (v0.78.0)

Session 2 of `docs/GPU_DECODE_PLAN.md`: file-backed Video/Audio cues decode
in-process via libav\* (`native/engine/libav_decoder.hpp/.cpp`,
`DECKBOY_INPROC_DECODE`), replacing the two ffmpeg subprocess pipes per deck.
Architecture and traps:

- **Zero-copy path (Windows/D3D11).** Deck engines get a
  `DecodeDeviceProvider` returning the program output renderer's
  `ID3D11Device` (`primaryOutputDecodeDevice()`); the decoder adopts that
  device into an ffmpeg hw ctx and decodes d3d11va straight onto it. Decoded
  frames ride `DecodedFrame::gpu*` (texture-array slice + AVFrame ref;
  `pixels` empty). `renderDeckLayerIntoOutput` GPU-copies the slice into a
  per-deck SDL-owned NV12 texture (`ensureLayerGpuTexture` →
  `createWrappedNV12Texture`, which pulls SDL's backing texture out via
  `SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER`) — video never touches the CPU.
- **`SDL_HINT_RENDER_DIRECT3D_THREADSAFE = "1"` at init is LOAD-BEARING.**
  SDL otherwise creates its D3D11 devices `D3D11_CREATE_DEVICE_SINGLETHREADED`;
  the ID3D10/11Multithread QI then fails and a shared device crashes or
  deadlocks at random (we hit both). `adoptD3D11Device` refuses devices where
  multithread protection can't be enabled — zero-copy silently degrades to
  in-process CPU output, which is the symptom to check first if
  `--decode-bench` reports `inproc-cpu` unexpectedly.
- **Who falls back where:** live streams (SRT/NDI), stills, capture,
  waveform, ffprobe, encode-out → CLI (unchanged). Rotation-metadata files →
  CLI (libav doesn't autorotate). RGBA effects cues + non-hw codecs +
  engines without a device provider (preview/PiP) → in-process CPU frames.
  In-process open failure → CLI pipe path automatically. Runtime
  break-glass: `--no-inproc-decode`; build-time: `-DDECKBOY_INPROC_DECODE=OFF`.
- **Semantics preserved:** the decode threads keep the same
  `frameQueue_`/`kMaxVideoFrames` backpressure and frame indexing; audio
  decodes to the same s16/stereo/48k stream through the shared
  `applyGainAndQueueAudio` tail, so the audio-master A/V clock (v0.76.19) is
  untouched. Audio speed uses an avfilter atempo chain identical to the old
  CLI args; in-point seeks trim to the sample in the output domain.
- **Crash resilience (no more subprocess isolation):** `VideoPipeline::open`
  primes the first frame (validation gate + hw→sw retry), corrupt packets
  degrade to EOF after `kMaxConsecutiveErrors`, and `consumeDecodeStall()`
  (4 s watchdog in `update()`) makes the app rerack the deck dark + toast.
  `stopDecoderThreads` trips the pipelines' AVIO interrupt callback before
  joining — the in-process replacement for killing the child to unblock a
  pipe read (dead network shares can't hang a TAKE).
- **Device lifecycle:** output-runtime create/destroy calls
  `scheduleDecodeDeviceReconcile()`; the app tick restarts decode on decks
  whose `activeDecodeDevice()` no longer matches (`reconcileDecodeDevices`).
  Frames referencing a destroyed output's device stay valid (COM refs);
  consumers on a different device CPU-download per frame advance
  (`downloadGpuFrameNV12`), control preview throttles that to ~10 fps.
- **Bench:** `--decode-bench <file> [seconds] [cli]` — prints
  `mode=inproc-zerocopy | inproc-cpu | cli-pipe` and sustained decode fps
  (consumer drains the queue; pacing removed). Desktop reference (RTX-class,
  1080x1920 h264): cli-pipe 228, inproc-cpu 236, inproc-zerocopy 240 —
  desktop is decode-bound; the Pocket is transport-bound, which is where the
  win lives. Capture Pocket numbers before the next show.

## SDL2 → SDL3 Migration (v0.77.0)

Whole-app port, executed per `docs/SDL3_MIGRATION_PLAN.md` (Session 1 of the
GPU-decode Option B sequencing — decode rewrite follows in a separate session
on this base). Design decisions and the traps future edits must respect:

- **Compat layer: `native/core/sdl_compat.hpp`.** Every `#include <SDL.h>` was
  rewritten to include this header. It provides:
  - C++ overloads of `SDL_RenderFillRect` / `SDL_RenderRect` /
    `SDL_RenderTexture` / `SDL_RenderTextureRotated` that accept the int
    `SDL_Rect` the entire layout system uses and convert to `SDL_FRect` at the
    draw boundary. Layout stays integer; only the boundary converts. A literal
    `nullptr` rect needs no cast (dedicated `std::nullptr_t` overloads).
  - `deckboyCreateTexture` / `deckboyCreateTextureFromSurface` — ALWAYS use
    these instead of raw `SDL_CreateTexture*`: they apply
    `SDL_SCALEMODE_NEAREST` per texture, preserving the SDL2-era global
    `SDL_HINT_RENDER_SCALE_QUALITY="0"` look (the hint is gone in SDL3, default
    is linear — a raw create call would silently blur pixel-art UI).
  - SDL2-style display indices (`deckboyGetNumVideoDisplays`,
    `deckboyDisplayIdFromIndex`, `deckboyGetWindowDisplayIndex`,
    `deckboyGetDisplayBounds`, `deckboyGetDesktopDisplayMode`, …) mapping index
    ↔ `SDL_DisplayID` through `SDL_GetDisplays()` order. Projects keep
    persisting display *indices*; hot-plug revalidation works as before.
  - `deckboySetAudioPaused(stream, paused)` — SDL2 `SDL_PauseAudioDevice(dev,
    0/1)` semantics over a device-bound stream.
- **Audio = SDL3 streams, queue model preserved.** Every SDL2
  `SDL_AudioDeviceID` became an `SDL_AudioStream*` from
  `SDL_OpenAudioDeviceStream` (one logical device per consumer: per-deck main
  out, UI sounds, LTC recording). `SDL_QueueAudio` → `SDL_PutAudioStreamData`,
  `SDL_GetQueuedAudioSize` → `SDL_GetAudioStreamQueued`, `SDL_ClearQueuedAudio`
  → `SDL_ClearAudioStream`, close → `SDL_DestroyAudioStream` (closes the
  logical device). Streams open PAUSED; the engine resumes per transport state.
  - **A/V audio-master clock re-anchored** on `SDL_GetAudioStreamQueued`:
    "queued frames minus stream-buffered bytes" as before. The physical device
    buffer beyond the stream adds a small constant offset (~one buffer,
    5–20 ms) that sits inside the 60 ms drift threshold — do not tighten that
    threshold below ~2 device buffers.
  - The operator buffer-size setting (`audioBufferSamples`) now applies via
    `SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES` set immediately before each device
    open (`applyAudioBufferSizeHint()`); SDL3 specs have no `samples` field.
  - Device pickers enumerate `SDL_GetAudioPlaybackDevices` and resolve
    persisted device *names* to ids at open time; a missing name falls back to
    the system default (same policy as SDL2).
- **Fullscreen model.** `SDL_WINDOW_FULLSCREEN_DESKTOP` is gone. Policy
  mapping: borderless desktop = `SDL_SetWindowFullscreenMode(win, nullptr)` +
  `SDL_SetWindowFullscreen(win, true)`; exclusive (only when the operator picked
  a fixed raster/refresh) = `SDL_SetWindowFullscreenMode(win, &mode)` first.
  Flag checks are just `flags & SDL_WINDOW_FULLSCREEN` now.
  `SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS="0"` still exists in SDL3 and REMAINS
  SET — never remove (see "output frozen" saga below). The recovery/backoff
  policy in app_output_mgmt.ipp is unchanged; only the SDL calls under it are
  new. Re-verify taskbar/focus/hot-plug in the field.
- **Windows are shown by default** in SDL3 (`SDL_WINDOW_SHOWN` gone); hidden
  decode/monitor windows keep `SDL_WINDOW_HIDDEN`. Visibility checks use
  `!(flags & SDL_WINDOW_HIDDEN)`. `SDL_CreateWindow` lost the x/y args —
  position is set after create via `SDL_SetWindowPosition`.
- **Renderers**: no creation flags. vsync is per-renderer at runtime
  (`SDL_SetRenderVSync`): control window ON, visible program outputs ON,
  stream-only outputs and hidden per-deck decode renderers OFF (never set).
  Software fallback = `SDL_CreateRenderer(win, SDL_SOFTWARE_RENDERER)`.
- **Events**: window events are top-level (`SDL_EVENT_WINDOW_CLOSE_REQUESTED`
  etc.), display hot-plug is `SDL_EVENT_DISPLAY_ADDED/REMOVED`. Mouse/wheel
  coords are float — the UI truncates via `static_cast<int>` at the dispatch
  boundary in app_update.ipp. `event.drop.data` is OWNED BY SDL now (freeing it
  is a heap corruption); `event.key.key`/`.mod` replace `.keysym.*`; letter
  keycodes are uppercase (`SDLK_A`); `SDL_StartTextInput` takes the window.
- **Bool returns**: most SDL3 calls return `bool` (true = success). All the old
  `== 0` / `!= 0` int-return checks were flipped; when adding code, never write
  `SDL_X(...) == 0` for success.
- **`SDL_RenderReadPixels` returns a new `SDL_Surface*`** (no in-place buffer
  fill). Egress capture converts via `SDL_ConvertPixels` into the persistent
  BGRA buffer; the key-color picker reads via `SDL_ReadSurfacePixel`.
- **`SDL_Vertex` carries `SDL_FColor`** (floats) — warp/edge-blend vertex
  alphas convert `Uint8 → a/255.0f`.
- **Renderer format probe** moved from `SDL_RendererInfo` to the
  `SDL_PROP_RENDERER_TEXTURE_FORMATS_POINTER` property (UNKNOWN-terminated
  array) — see `rendererSupportsTextureFormat()`.
- **HWND** for WM_SETICON comes from
  `SDL_PROP_WINDOW_WIN32_HWND_POINTER` (SDL_syswm.h is gone).
- **Not migrated / unchanged**: `SDL_Color` (554 refs), `SDL_SetRenderClipRect`
  still takes int `SDL_Rect`, blend modes, `SDL_UpdateNVTexture` (NV12 upload
  path intact for the decode rewrite), render targets.
- **Next session**: in-process libav decode straight to zero-copy via
  `SDL_CreateTextureWithProperties` + `SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER`
  (GPU_DECODE_PLAN §7–§11, Session 2). Keep it separate from any other
  playback-path work.

## Media converter + ENCODER tab (v0.76.31)
- `cueConvertReason()` flags file-backed Video cues that ffprobe can't read
  (tracked in the transient `unreadablePaths_` set) or that carry a heavy codec
  (HEVC/H265/AV1/ProRes/DNxHD) or a >1080p frame. Surfaced three ways: a toast on
  import, a contextual CONVERT button in the SELECTED CUE panel (only when
  flagged/converting), and the Settings → Encoder tab (`settingsTab_ == 5`).
- `convertCueMedia()` runs an async ffmpeg job in `conversionJobs_`: CPU decode +
  `h264_nvenc` (libx264 fallback) → `<show>/_converted/<stem>.mp4`. The per-frame
  poll in `update()` swaps the cue's path to the copy and pushes a re-probe on
  success. Original untouched; `_converted/` is git-ignored.

## Audio device hot-swap (v0.76.31)
- `MediaEngine::setAudioDevice()` redirects the SDL output device in place (the
  engine never owns it — it only queues PCM via SDL_QueueAudio). `reopenDeckAudioOutput()`
  now hot-swaps on the existing engine instead of `stopAll()` + recreate, so
  changing a deck's audio output mid-cue no longer stops playback. First-time
  setup (null engine) still constructs it.

## Deck-list scroll + splash system (v0.76.31)
- The MAIN deck cue list (renderCueRow path, not the compact panel) clamps
  `deckScrolls_` to `[0, deckScrollMax_]` before drawing, with a bottom-only
  rubber-band: the wheel may push to `max + kDeckScrollOverscroll`, then a
  time-based exponential settle (τ≈71ms, gated on `lastDeckScrollMs_` idle)
  springs it back. Top is hard-clamped at 0.
- Splash: grayscale masters in `data/ui/.../splash/cycle/` are picked at random
  per boot (`SDL_GetPerformanceCounter`) and tinted to `pal.light` via
  `splashTintable_`; the default (gameboy) theme instead boots the branded
  character splash untinted. The splash background follows `pal.deep` (was a
  hardcoded green).

## Save / Theme / Output / UX batch (v0.76.30)
- **SAVE always prompts.** The toolbar SAVE button and Ctrl+S both call
  `saveProjectAsFromPicker` (file picker every time, project only — no media);
  there is deliberately no separate Save As button (it would be identical).
  BUNDLE (`exportProjectBundleTo`) is the export-with-media path. The old SAVE
  silently overwrote `data/default.deckboy` with no filename feedback, which
  read as "saving a default state."
- **Theme persists per project.** `Project::theme` (types.hpp) holds the saved
  colorway (a `data/themes/<name>` dir), serialized as a key-value line
  `theme\t<name>` like `splash_character`. Applied on open in
  `openProjectFromPath` and at boot after project load — only when non-empty,
  so an older theme-less show never stomps the operator's current pick.
  `DECKBOY_THEME` still hard-overrides at boot. The Appearance dropdown scans
  `data/themes/` for any dir containing `theme.txt` (24 shipped: dark sci-fi +
  Nintendo colorways).
- **Output flushes black instead of freezing.** A disabled-but-visible output
  window is painted black exactly once, latched by
  `OutputRuntime::blackedWhileDisabled` (reset when it renders again) via
  `clearDisabledOutputWindow` in the render loop — this covers New Show, where
  `ensureOutputRuntimesSynced` reuses the window and just stops rendering it.
  `destroyOutputRuntime` also presents two black frames before tearing a
  visible window down (exit / display switch / capture dongles that latch the
  last received frame). Never let a disabled output silently hold the last
  frame.
- **Inline editor type-to-replace.** `InlineTextEditorState::freshEntry` is set
  on open; the first character (or backspace) clears the pre-filled value
  first, mimicking select-all-on-focus. Submitting with no edit keeps the
  original value.
- **Recursive folder import.** `importPaths` expands directories via
  `recursive_directory_iterator` (skip_permission_denied, name-sorted),
  filtered by `isAcceptableMediaPath` (video/image/audio ext lists beside
  `isImagePath`). Cue kind is Image/Audio/Video; `probeCue` now also returns
  Audio for audio extensions so the async probe doesn't relabel audio as Video.
- **Cue-list over-scroll** is clamped BEFORE the draw loop (deck panel list and
  overlay bin) so the wheel can't push past the last row into empty space; the
  old after-draw clamp flickered.
- **Audio timeline seek.** The audio lane rect is stored in
  `audioProgressBarRect_` and is a click-to-seek target alongside
  `progressBarRect_` (same x-mapping, shared scrub/resume path).

## Pixel-Based Geometry Editing (v0.76.21)
- Operator-facing size unit is output pixels; `Cue::outputScaleX/Y`
  multipliers are an implementation detail. `cueBaseRenderSize(cue)`
  (app_geometry.ipp) returns the rendered px size at multiplier 1.0 — the
  same scale-mode + crop math as the compositor path; keep them in step.
  `finalPx = base × outputScale`; typed px → `scale = px / base`, clamped to
  the historical 0.25–4.0 range (so extreme px requests saturate).
- `Project::geometryAspectLinked` (default true, header line
  `geometry_aspect_link`) drives the link: any scale change on one axis
  multiplies the other axis by the same relative factor
  (`newOther = other × newX/oldX`). Applied in `adjustSelectedScaleX/Y`
  (nudge buttons) and `editSelectedScaleX/Y` (typed px). Note the clamp can
  bend aspect at range extremes — same tradeoff as other media software.
- UI: `link aspect` toggle row (QuickAction::ToggleAspectLink) in the
  GEOMETRY section above width/height. If a new surface edits cue scale,
  route it through the adjust/edit helpers — do not write outputScaleX/Y
  directly, or the link silently won't apply.

## Fullscreen Minimize-On-Focus-Loss Root Cause (v0.76.20)
- The deepest layer of the fullscreen fight: SDL2 minimizes EXCLUSIVE
  fullscreen windows on focus loss by default. Operator clicks the control
  window → program output minimizes ("output frozen while preview plays")
  → recovery re-raises + re-fullscreens it (stealing focus) → operator
  clicks again → loop. `SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS = "0"` is now
  set in init, right after the DPI hint. Never remove it — a playout output
  must survive the operator working in the control surface.
- `enableOutputFullscreen` now uses exclusive `SDL_WINDOW_FULLSCREEN` (real
  mode switch) ONLY when `!outputFollowDisplay || outputRefreshRateHz > 0`
  (operator explicitly chose a fixed raster or refresh). Display-native
  outputs use `SDL_WINDOW_FULLSCREEN_DESKTOP` — no mode switch, no
  blanking, reliable placement on mixed-DPI multi-monitor. The custom-EDID /
  high-refresh feature is unchanged when explicitly requested.

## Fullscreen Recovery Fight Postmortem (v0.76.20)
- v0.76.19 widened `recoverWindowOutputIfNeeded`'s `wrongDisplay` check to
  apply while the window was fullscreen (intended to migrate windows left on
  the wrong display after hot-plug). Field result: on a mixed-DPI
  multi-monitor setup, `SDL_GetWindowDisplayIndex` persistently disagreed
  with the target for a fullscreen window → recovery exited fullscreen,
  moved, re-entered, and raised the output every 1.2 s. Each
  `SDL_RaiseWindow` moved keyboard focus to the output window (which
  ignores all keys except Esc by design) and ate in-flight clicks. Operator
  experience: "typing becomes difficult", "controls seem like they weren't
  happening", "trying to take over the wrong screen".
- **Rules now enforced:**
  1. `wrongDisplay` is only evaluated for NON-fullscreen windows (original
     behavior). A stable fullscreen window is never repositioned
     automatically; wrong placement is the operator's explicit call.
  2. Strike backoff: >3 recovery attempts within 15 s → recovery pauses 30 s,
     health `Error("output unstable - recovery paused 30s")` + toast. A
     healthy output needs recovery rarely; repetition means recovery itself
     is the problem.
  3. Any path that raises an output window (recovery, deferred display-move
     tick, display identify) captures whether the control window had
     keyboard focus first and re-raises it after.
- The audio-master clock gained a stall guard in the same release: the
  correction only runs while the audio clock advanced within the last
  400 ms. A frozen clock (dead endpoint, audio pipe death mid-file) would
  otherwise pin video at the freeze position forever.

## Display Identify Overlay (v0.76.20)
- `showDisplayIdentify()` / `renderDisplayIdentify()` / `closeDisplayIdentify()`
  in `app_output_mgmt.ipp`; state is `identifyWindows_` + `identifyUntilMs_`.
  One borderless always-on-top window per display, auto-closed after 2.5 s
  from the render loop. Text is blitted straight from TTF surfaces per
  identify renderer — the `drawText*` helpers are bound to
  `controlRenderer_` textures and must not be used for other renderers.
- Triggered by `kSettingsActionDisplayIdentify` (637) — the IDENTIFY button
  in Video Outputs → Display → CONNECTED DISPLAYS. The list itself now sizes
  for every connected display (clamped to modal space) instead of collapsing
  to two rows.

## Engine Cue Snapshot Ownership (v0.76.19)
- `MediaEngine` no longer holds a raw pointer into `Deck::cues`.
  `loadCue()` copies the cue into `activeCueSnapshot_` (a
  `std::optional<Cue>`) and `activeCue_` points at that owned storage.
  Rationale: `deck.cues.push_back()` (import) reallocates and
  `deck.cues.erase()` (delete) shifts elements while the render path reads
  `activeCue_->fadeInSeconds` every frame and the audio thread reads it per
  sample pair — the old pointer dangled (reallocation) or silently retargeted
  the *next* cue's parameters (erase-shift above the live cue).
- **Live-edit contract**: the output compositor reads geometry/effects from
  the app's cue (`activeCuePtr(deckIndex)`), NOT the engine snapshot — so
  geometry/color edits were never affected. Fields the ENGINE reads mid-play
  (fade in/out, still duration) stay fresh because `markProjectDirty()` sets
  `engineCueSyncPending_`, and `App::update()` drains it by calling
  `syncEngineCueSnapshots()` → `engine->syncActiveCueSnapshot(cue)` for every
  deck with a valid activeIndex. One frame of lag, no per-frame copying.
- `refreshActiveCueRuntime(const Cue* updatedCue)` now takes the app's
  current cue and refreshes the snapshot before re-reading runtime params.
- When adding engine reads of new cue fields, nothing extra is needed — the
  snapshot sync covers all fields. When adding new app-side mutation paths,
  make sure they call `markProjectDirty()` (they all should anyway).

## Audio-Thread Fade Atomics (v0.76.19)
- The audio decode thread previously called `fadeGainAt()`, which reads
  `activeCue_`, `duration_`, and the two suppress flags — plain members
  mutated by the main thread (data race, torn-double risk on non-x86).
- Now: `syncAudioFadeParams()` (main thread) publishes fade-in/out seconds,
  duration, and suppress flags to relaxed atomics; the audio thread uses
  `audioFadeGainAt()` which reads ONLY those mirrors. Publish points: before
  the audio thread spawns in `startDecoderThreads`, in `stopAll`, in
  `syncActiveCueSnapshot`, and once per `update()` tick as a catch-all
  (covers `handlePlaybackEnd` loop re-assert, browser duration restore).
- Rule: never add a read of a non-atomic engine member inside the audio
  thread lambda. Mirror it through an atomic and publish in
  `syncAudioFadeParams()` (or a sibling).

## Audio-Master A/V Clock (v0.76.19)
- `position()` runs on the wall clock (steady_clock); the audio pipeline
  free-runs into the SDL queue throttled only by queue depth. The two clocks
  drift (audio device crystal ≠ steady_clock; VFR sources drift immediately
  because frame index × probed fps is wrong for them).
- The audio thread counts stereo frames it queues (`audioFramesQueued_`,
  atomic). `update()` derives the audio playback clock:
  `audioClockStartSeconds_ + (queued − SDL_GetQueuedAudioSize buffered) ×
  playbackSpeed_` (atempo re-times the pipe to wall rate, position space is
  speed × wall). When |video − audio| > 60 ms it re-anchors
  `playbackClockStart_`/`playbackStartPosition_` to the audio clock.
- Guards: needs ≥100 ms of queued audio before trusting the clock; skipped
  when `decoderEof_` (audio drains before video ends), within 250 ms of the
  cue end, and for live streams (`audioClockValid_` false — no deterministic
  sample→position mapping). `seek()`/`refreshActiveCueRuntime()` re-base the
  clock automatically because they restart the pipes with a new
  `cueStartSeconds`.
- Cues without audio keep the wall clock — nothing to slave to.

## Display Identity By Name + Hot-Plug Safety (v0.76.19)
- `OutputTarget::displayName` (serialized field 35, guard ≥ 36) records the
  SDL display name whenever the operator explicitly picks a display
  (`setOutputDisplayIndex`, `cycleOutputDisplay` → `recordOutputDisplayName`).
  SDL display indices are enumeration-order-dependent and shuffle across
  hot-plug/reboot/driver updates — a bare persisted index could silently
  retarget program output to the operator's monitor.
- `resolveOutputDisplayIndex()` is the single resolution choke point (called
  from `applyOutputDisplaySelection` and `refreshDisplayTopology`): keep the
  current index if its name still matches, else find the display carrying the
  recorded name, else clamp — WITHOUT erasing `displayName`, so the intended
  display re-matches when it comes back.
- `refreshDisplayTopology` no longer calls
  `applyOutputDisplaySelectionAllOutputs(true, true)`. That forced every
  enabled output through a fullscreen exit/re-enter on ANY display connect/
  disconnect and — critically — re-fullscreened outputs the operator had
  escaped to windowed (bypassing `recoveryPausedByEscape`). It now resolves
  indices and heals via `recoverWindowOutputIfNeeded`, which honors the
  escape flag and `fullscreenIntended`. `wrongDisplay` detection now also
  applies to windows still fullscreen on the wrong display (post-hot-plug),
  and recovery passes `allowFullscreenTransition=true` only for display
  moves. Zero-display scans (RDP handoff, driver reset) no longer mutate
  persisted display targets.
- `tickPendingOutputDisplayTransitions` cancels a pending re-fullscreen when
  `recoveryPausedByEscape` is set. Explicit operator display changes re-arm
  (the runtime-rebuild path clears the escape flag).

## HyperDeck Snapshot (v0.76.19)
- `hyperDeckHandleCommand` runs on the HyperDeck TCP thread. It previously
  read `focusedDeck()` (a race against main-thread cue mutations) and
  inferred transport by substring-matching the human-readable status text —
  a cue named "playing" would corrupt replies.
- Now: `App::HyperDeckSnapshot` (transport token, 1-based clip id, loop,
  {name, duration} clip list) is rebuilt on the main thread in
  `updateStatusSnapshot()` and read under `statusSnapshotMutex_`. Rule for
  new HyperDeck (or any network-thread) handlers: reply from a snapshot
  field; if one doesn't exist, add it to the snapshot — never touch
  `project_` from the network thread.

## Still Cue Hold / Fade Interaction (v0.76.17)
- **Symptom:** an Image (or other still-type) cue with hold / pause-on-last set
  vanished the instant its `stillDurationSeconds` elapsed, even though the frame
  was still resident. Animated Pattern cues masked the bug because they re-render
  every tick.
- **Root cause chain:**
  1. The output composites `MediaEngine::currentFrame()` (the `DecodedFrame`),
     and its alpha = `currentVisualFadeGain()` = `visualFadeGainAt(position())`.
  2. `position()` returns `currentPosition_` whenever the engine isn't `Playing`.
  3. `handlePlaybackEnd()` → `PauseOnLast` correctly froze at
     `currentPosition_ = pausedPosition_ = duration_` and set state `Paused`.
  4. BUT the very next `update()` tick took the non-video `else` branch and
     reset `currentPosition_ = 0.0`. So `position()` returned 0, and
     `visualFadeGainAt(0)` applied the **fade-IN** ramp at t=0 → gain 0 → the
     held frame drew fully transparent.
- **Fix:** the still `else` branch now holds `currentPosition_ = pausedPosition_`
  instead of snapping to 0. `pausedPosition_` is the authoritative freeze point
  (0 at start, mid-cue for a manual pause, `duration_` after pause-on-last).
- **Fade-out policy:** `visualFadeGainAt` does NOT special-case hold cues — a
  configured fade-out always ramps. Instead, still-type cues default to
  `fadeOutSeconds = 0` in `applyDeckDefaultsToCue` (a static graphic that holds
  shouldn't dip to black), but the operator can re-enable fade-out per cue and it
  will then run normally. An earlier attempt that suppressed fade-out for
  PauseOnLast in `visualFadeGainAt` was reverted because it broke deliberately
  configured fade-outs.
- **Render loop refresh:** `run()`'s anti-spin floor is `1/240s`, not `1/120s`,
  so vsync (not the floor) governs on 144/165/240 Hz displays.

## Output Backend Route Architecture (v0.76.11)
- Output destinations use a catalog → route-plan → runtime-route
  pipeline. `OutputBackendCatalog` (output_backend.cpp) reports all
  backends with platform availability at compile time via feature
  gates (`DECKBOY_HAS_NDI_SDK`, `DECKBOY_HAS_DECKLINK`,
  `DECKBOY_HAS_SPOUT`). `planOutputBackendRoute()` converts an
  `OutputBackendRouteRequest` (built from per-output `OutputTarget`
  enable flags) into an ordered list of steps, each annotated with
  whether the backend is supported on this build.
- At runtime, `resolveOutputBackendRuntimeRoute()` in
  `app_output_mgmt.ipp` maps the plan into `*Supported` bools on an
  `OutputBackendRuntimeRoute` struct. The render loop in
  `app_render_output.ipp` computes `*RouteActive` (enabled AND
  supported) and gates send/shutdown calls accordingly.
- Adding a new output backend: add enum to `OutputRouteKind`, add
  catalog entry (with platform `#if` guards), add enable flag to
  `OutputBackendRouteRequest` and `OutputTarget`, add case to
  `planOutputBackendRoute()` and `resolveOutputBackendRuntimeRoute()`,
  implement send/shutdown functions in `app_output_mgmt.ipp`, add
  `*RouteActive` check in render loop, add settings UI, add
  serialization fields.

## Spout Integration Notes (v0.76.11)
- Spout2 is Windows-only interprocess texture sharing (analogous to
  Syphon on macOS). The `siphon_spout.hpp` interface is shared; the
  `.cpp` has `#if defined(DECKBOY_HAS_SPOUT)` for the Windows
  implementation and a stub for other platforms.
- SpoutLibrary provides a COM-like interface via `GetSpout()` factory.
  Lifecycle: `GetSpout()` → `SetSenderName()` → `SendImage(pixels,
  w, h, GL_BGRA, true)` per frame → `ReleaseSender()` → `Release()`.
  `SendImage()` accepts raw CPU pixel buffers and handles
  DirectX/OpenGL internally — no GL context from the caller.
- The sender name is user-configurable via settings. Changing the
  name requires `ReleaseSender()` then `SetSenderName()` to re-create
  the shared texture with the new identity.
- Installed via vcpkg (`spout2:x64-windows`). CMake auto-detects via
  `find_package(Spout2 CONFIG QUIET)` and sets `DECKBOY_HAS_SPOUT`.
  Falls back to manual `find_path` if `ENABLE_SPOUT` is explicitly
  set.

## MSVC Block-Nesting Limit and handleSettingsClick (v0.76.11)
- MSVC has a hard limit on block nesting depth (C1061, ~128 levels).
  The settings click handler's if-else-if chain can exceed this when
  enough handlers accumulate. The function is split across three
  methods: `handleSettingsClick` → `handleSettingsClickPart2` →
  `handleSettingsClickPart3`. Each ends with
  `else { handleSettingsClickPartN+1(sb); }` to chain to the next.
- When adding new settings handlers, add them to the last Part
  function. If the build starts failing with C2628 / C2065 cascades
  (members reported as undeclared), split again by ending the current
  chain with an `else { handleSettingsClickPartN(sb); }` and starting
  a new function.

## Timeline Loading Animations (v0.76.10)
- The timeline has two lanes — video (`progressBarRect_`) and audio
  (`audioLaneRect`) — each of which can be waiting on background work
  before it has anything meaningful to draw. Video waits on
  `timelineStripTex_` to be rendered from the cue's thumbnail grid;
  audio waits on `computeWaveformPeaks` to finish on a background
  `std::async`. Both are gated by kind-specific state: the video
  loading branch fires when `timelineStripLoading_` is true for the
  current cue's cache key, the audio branch fires when
  `getWaveformPeaks` returns `pending == true` and an empty peaks
  struct.
- Both animations use the same widget: a `drawUIPanel`-framed box
  centered in its lane, sized proportionally to the lane
  (`std::min(188, std::max(124, laneRect.w - 28))` x
  `std::min(46, std::max(34, laneRect.h - 16))`), over a translucent
  `{7, 12, 7, 148}` dimming overlay, with a pulsing "LOADING..."
  label at the bottom whose dot count advances on a 180ms clock.
  This is deliberate: they should read as sibling animations so the
  operator recognizes "timeline is fetching" instantly regardless of
  which lane is loading.
- Iconography differs to distinguish lanes. Video uses 5 filmstrip
  cells with sprocket holes (`drawTimelineLoadingAnimation`); audio
  uses a 9-bar EQ meter where each bar's height is a squared sine
  envelope with a per-bar phase offset
  (`drawAudioTimelineLoadingAnimation`). Both animations run on
  `animationNow_` as their time base, so the idle/fps clamp
  behavior for UI animations applies uniformly.
- If you add a third lane that can load asynchronously, follow the
  same pattern: build a lambda in `app_render_main.ipp` next to the
  existing two, reuse the widget frame + dimming + LOADING label,
  and pick iconography that's unambiguously different from the
  other two. Do not factor the shared chrome into a helper until
  there is a third site — the duplication is cheap and the lambdas
  have to capture animation state from the enclosing render
  function.

## Settings Card Row Spacing Note (v0.76.9)
- The System Settings modal used hand-picked absolute y-offsets
  (`safetyRect.y + 56`, `+ 74`, `+ 108`, ...) sized for the stock font
  load (`fontSmall_` @ 15pt, `TTF_FontHeight ≈ 18`). On retina / HiDPI
  the loader switches to a 17pt face (`TTF_FontHeight ≈ 21`), and label
  rows started overlapping the button rows directly below them by
  2–4px. Because the buttons call `drawFramedPanel` *after* the label
  was drawn, the panel fill overpainted the descenders — reported as
  "panic text is so low it's cutoff".
- The fix is ergonomic rather than structural: a trio of helpers in
  `render/layout.hpp` — `textLineHeight(font)`, `rowYBelowLabel(labelY,
  font, gap)`, and `rowYBelowLines(startY, font, lines, gap)` — derive
  row Y and multi-line spacing from `TTF_FontHeight` at runtime. They
  are pure functions with a null-safe fallback (returns 18 when font
  is null), so callsites can use them interchangeably with literal
  offsets during incremental migration.
- All settings cards that mixed a single-line label and a control row
  were migrated (SAFETY / TIMECODE, SHOW FLOW, CUE TOOLS, AUDIO, MIDI,
  REMOTE, OSC, NOTES, INTEGRATION, About/RUNTIME, Edge Blending, AOI).
  The inspector (`app_render_inspector.ipp`) and its four primitive
  drawers in `app_cue_mgmt.ipp` were left untouched because the user's
  acceptance criteria explicitly called out that the inspector already
  rendered correctly in scrolling menus and must not regress. The new
  helpers are additive; they don't change any existing behavior at the
  stock font size.
- The four text-drawing primitives (`drawText`, `drawTextSafe`,
  `drawCenteredText`, `drawCenteredTextSafe`) remain the canonical
  way to render text in the app. If a future pass wants to unify
  optical centering across button surfaces, that lives inside those
  primitives — don't introduce a parallel drawer.

## FFprobe Parser Stream Boundary Note (v0.76.8)
- `probeCue()` in `main.cpp` parses ffprobe's `default=noprint_wrappers=1`
  output line-by-line. It requests `stream=codec_type,codec_name,...` and
  expects these fields in either order within a stream (the order varies
  by ffprobe version and container format).
- The original buffering logic stored the most recent `codec_name` as
  `pendingCodecName` and the most recent `codec_type` as `lastCodecType`,
  applying the pair via `tryApplyCodec()` whenever both were known. A
  `pendingApplied` flag prevented double-application.
- **The bug:** it treated codec_name and codec_type as independent fields
  without recognizing that a NEW stream resets both. For an mp4 with the
  audio stream emitted first:
  ```
  codec_name=aac
  codec_type=audio       ← pair applied, pendingApplied=true
  sample_rate=48000
  channels=2
  r_frame_rate=0/0
  codec_name=h264        ← new stream, but lastCodecType still "audio"
  codec_type=video
  ...
  ```
  On `codec_name=h264`, tryApplyCodec ran with `lastCodecType="audio"` and
  saw `cue.audioCodec` already filled, so the "h264" was silently
  discarded. The subsequent `codec_type=video` set `lastCodecType="video"`
  but `pendingApplied` was already true from the audio pair, so
  tryApplyCodec early-returned. Result: `cue.videoCodec` stayed empty,
  triggering the end-of-function audio-only detection.
- **Fix:** when `codec_type` arrives AND `pendingApplied` is already true,
  that codec_type marks a new stream — clear `pendingCodecName` and reset
  `pendingApplied = false` before assigning the new type. Symmetric handling
  on `codec_name` (clear `lastCodecType`). Both within-stream orderings
  continue to work; new-stream boundaries are now detected.
- **Testing:** verified against `H:\Missa X\Missa X - Bachelorette Pt 2
  (1080p).mp4` which emits audio-first. Traced step-by-step through both
  the audio-first case and the conventional video-first case to confirm
  no regression in the normal path.

## Duplicate Cue Allowance Note (v0.76.8)
- `importPaths()` in `app_cue_mgmt.ipp` used to silently skip any path
  that already existed in the deck's cue list. This blocked a legitimate
  operator workflow: using the same asset twice (e.g., as two cues with
  different in/out trim, or a playlist that loops back through an asset).
- Removed the dedup check. Library-level dedup (avoiding re-ingesting the
  same file into the media library) is a separate concern that belongs in
  the media library layer, not the cue list.

## Video Cue Fade Suppression Removal Note (v0.76.7)
- **Context:** this is a follow-on to the v0.76.4 output fade gain fix.
  That release moved per-cue fade ramps from the (dead) `MediaEngine::render()`
  path into the live `renderDeckLayerIntoOutput` path via
  `currentVisualFadeGain()` → bridge texture alpha. It made fades work on
  output for the first time. But it left a stale cue-kind gate in
  `loadCue()` intact, which silently killed fades for video/source cues in
  playlists.
- **The stale gate (now removed):**
  ```cpp
  bool isStillTypeCue = cue && (cue->kind == Image || Pattern || Browser || Composite);
  suppressFadeInForCurrentCue_ = suppressFadeIn && !isStillTypeCue;
  suppressVisualFadeOutForCurrentCue_ =
    cue && !isStillTypeCue && (cueAdvancesWhenFinished(*cue) || Loop);
  ```
  The comment said: "Video/source cues suppress these during auto-advance
  because the crossfade handles the outgoing/incoming visual, avoiding a
  double-fade effect." This was true pre-v0.76.4 — but the "crossfade" it
  was protecting lives inside `MediaEngine::render()`'s transition overlay
  path, which writes to a hidden per-deck `SDL_WINDOW_HIDDEN` that the
  output compositor never reads. The crossfade is invisible on output, so
  there's no double-fade hazard for the suppression to guard against —
  removing it just means the per-cue fade ramp actually runs on video cues.
- **Symptom before fix:** playlist of video+browser cues, same
  `fadeInSeconds`/`fadeOutSeconds` set on both. Browser cue fades correctly
  (isStillTypeCue=true → suppression skipped). Video cue pops in and out
  (suppression active → `visualFadeGainAt` returns 1.0 throughout).
- **Fix surface:**
  - `engine/media_engine.cpp:loadCue` — `suppressFadeInForCurrentCue_`
    honors the caller hint only, no cue-kind override;
    `suppressVisualFadeOutForCurrentCue_` always starts false. Loop
    suppression still re-asserts itself in `handlePlaybackEnd` (correct).
  - `app/app_update.ipp:416` — auto-advance no longer passes
    `suppressIncomingFadeIn=true`; the per-cue fade-in IS the visible
    transition.
- **Audio side effect:** `fadeGainAt()` used by the audio thread respects
  the same suppression flags, so audio also now fades in/out for
  auto-advancing video cues. This is the expected behavior — a cue with a
  visible fade should also have an audible fade. The fade ramp happens
  before `stopDecoderThreads` kills the outgoing audio pipe, so there's no
  click or abrupt termination.
- **If a visible crossfade mechanism is re-introduced in a future release**:
  wire it into the output path (bridge texture or a second bridge layer),
  NOT back into `MediaEngine::render()`. At that point, reconsider whether
  the crossfade should multiply OR replace the per-cue fade ramp. Do not
  resurrect the `isStillTypeCue` gate — use a more targeted mechanism
  (e.g., flag the outgoing frame explicitly during the crossfade window).

## Audio Thread Byte Alignment Note (v0.76.6)
- `MediaEngine::startDecoderThreads` spawns an audio reader thread that pulls
  s16le stereo @ 48 kHz from an FFmpeg pipe via `readSome()`. The previous
  code sized its `scaled` vector at `bytesRead / 2` (elements) and then
  `memcpy`'d `bytesRead` raw bytes in. If `readSome` ever returned an odd
  byte count (possible on EINTR / short read / EOF boundary), this wrote one
  byte past the end of the vector's backing storage — classic off-by-one,
  invisible in release but UB under sanitizers.
- Fix: mask `bytesRead` to an even count (`bytesRead & ~size_t{1}`) before
  sizing and copying. Any stray trailing byte is dropped; the pipe will
  return it on the next read, so sample alignment is preserved across the
  boundary. Sub-2-byte reads are skipped via `continue` rather than breaking
  the loop.
- This is not a rewrite of the audio path — the sample interleave, volume
  ramp, tap callback, and SDL queue are unchanged. Purely a boundary fix.

## Capture Backend Factory Platform Guard Note (v0.76.6)
- `platform/capture_backend.cpp` exposes three factories:
  `createWindowCaptureBackend()`, `createCameraCaptureBackend()`, and
  `createAppTextureCaptureBackend()`. The window factory already had a
  `_WIN32` guard that returns `WindowsGdigrabCaptureBackend` on Windows.
- Camera factory did not — it unconditionally returned
  `LinuxCameraCaptureBackend`, which on Windows produced a plan with
  `supported=false` and `backendId="v4l2"`. This contradicts the catalog,
  which advertises Windows camera capture as `mediafoundation`
  ("backend scaffold only").
- Added a small `UnsupportedCameraCaptureBackend` scaffold class (guarded
  `#if !defined(__linux__)`) that reports the correct platform backend id
  (`mediafoundation` on Windows, `avfoundation` on macOS) and
  `reasonUnavailable = "camera capture backend scaffold only"`. Factory is
  now `__linux__`-guarded to pick the right implementation.
- When a real Windows or macOS camera backend lands (Media Foundation,
  AVFoundation), replace the scaffold with the real class; the factory guard
  is the single switch point.

## Output Fade Gain Fix Note (v0.76.4)
- `MediaEngine::render()` applies fade-in/out via `visualFadeGainAt` to a hidden
  per-deck SDL window (DeckRuntime::outputWindow, SDL_WINDOW_HIDDEN). This window
  is never read by the output compositor — `MediaEngine::render()` is effectively
  dead code for output purposes.
- The actual output path is `renderDeckLayerIntoOutput` (app_render_output.ipp):
  reads `currentFrame()` pixels, uploads to a bridge texture, applies alpha via
  `SDL_SetTextureAlphaMod`. Before this fix, only `playlistOpacity` (deck-level
  opacity) was applied — the per-cue fade ramp was completely ignored.
- Fix: added public `currentVisualFadeGain() const` to `MediaEngine` (wraps the
  private `visualFadeGainAt(position())`). `renderDeckLayerIntoOutput` now sets
  `alpha = deckOpacity × fadeGain × 255`.
- `suppressVisualFadeOutForCurrentCue_` is respected by `visualFadeGainAt` —
  auto-advancing cues (which use crossfade for the outgoing visual) correctly
  return 1.0 from `currentVisualFadeGain()` so the outgoing frame is not double-faded.

## Browser Cue Duration Fix Note
- `startBrowserFrameMode` was unconditionally setting `duration_ = 0.0` on every first
  frame arrival from the browser capture pipeline.
- `loadCue` calls `initStillTimer` (which sets `duration_` from `stillDurationSeconds`)
  before starting browser capture, but `startBrowserFrameMode` overwrote it.
- Fix: `startBrowserFrameMode` now reads `activeCue_->stillDurationSeconds` and restores
  the duration if it is > 0 — otherwise leaves `duration_` at 0 (infinite still).
- This is why fade-out and auto-advance were silently broken for all browser cues.

## Area of Interest (AOI) Output Crop Note
- Per-output fractional edge crop, stored as `aoiLeft/Right/Top/Bottom` float fields
  on `OutputTarget` (0 = no crop, 1 = full crop from that edge; max 0.95 per edge).
- Applied in `presentOutputCompositorToWindow` (app_render_output.ipp) when computing
  the SDL source rect for the compositor→window blit.
- When AOI is active, canvas view pan (`canvasViewX/Y`) is intentionally skipped —
  the two modes are mutually exclusive to avoid confusing double-offset behavior.
- NDI and DeckLink outputs read the compositor texture via `SDL_RenderReadPixels` using
  a separate `captureRect` — AOI does NOT apply to those paths currently; a future
  improvement would composite to an intermediate scaled texture first.
- Settings panel: 4 dec/inc controls at 5% step + RESET button. Panel header highlights
  when any AOI edge is active.
- Serialized as fields 28–31 of the OutputTarget record (guard: `fields.size() >= 32`).

## Dependency Prompt (v0.76.14)
- Three optional Windows backends — NDI runtime, Blackmagic Desktop Video,
  Microsoft WebView2 — are not redistributable but Deckboy can detect them
  at runtime and route the operator to the official vendor download page.
- Detection lives next to the modal in `app/app_overlays.ipp`:
  - `ndiRuntimeAvailable()` calls `NdiApi::ensureLoaded()` (clears
    `attempted` on failure so a fresh attempt after installing works).
  - `deckLinkRuntimeAvailable()` returns true iff `DeckLinkOutput::listDevices()`
    finds at least one device. Empty means either Desktop Video missing or
    no card connected; the prompt copy addresses both.
  - `webView2RuntimeAvailable()` dynamically loads `WebView2Loader.dll` and
    calls `GetAvailableCoreWebView2BrowserVersionString` — the official
    runtime-availability check. Frees the COM-allocated version string
    via `CoTaskMemFree` (objbase.h, included from `main.cpp`).
- Prompt state is `App::depPrompt_` (a `DependencyPromptState` struct).
  `showDependencyPrompt()` populates it; `renderDependencyPrompt()` draws
  the modal in `renderControlWindow()`'s overlay stack; click handling
  lives in `processMouseDown` next to the existing confirmQuit_ block.
  CTA opens the vendor URL via `deckboy::platform::openExternalUrl()`
  (`core/system_browser.hpp` — ShellExecuteW on Windows, xdg-open / open
  elsewhere). Either button dismisses the prompt.
- Hook points must check at the UI-action site, NOT at the underlying
  setter, so a project file opened on a machine without the dep can still
  load with the flag respected (the setter then silently degrades).
  Current hooks: NDI toggle in settings + the `N` hotkey; DeckLink toggle
  in settings; `addBrowserCue` (Windows-only branch).

## UI Scale (v0.76.14, layout-chrome pass v0.76.15)
- `Project::uiScale` (double, default 1.0, clamped to [0.75, 3.0]) is the
  operator-facing scale multiplier. Persisted in the show file as
  `ui_scale` so a project authored on a 4K monitor keeps its scale.
- Fonts: `loadFonts(scale)` reopens all six TTF faces at `base × scale ×
  platformNudge` point sizes (Windows keeps the historical 0.9 nudge for
  its DPI baseline). Six base sizes: large 32, base 21, small 17, mono 18,
  pixel 24, pixel-small 12.
- Layout chrome: the `kLayout*` identifiers in `core/constants.hpp` are
  now mutable `inline int` globals (C++17), seeded from immutable
  `*Base` constexprs and rewritten by `App::rebuildLayoutMetrics(scale)`.
  `applyUiScale()` calls both `loadFonts` and `rebuildLayoutMetrics` so
  the next frame draws against a consistent set of metrics. Every prior
  callsite that read `kLayoutHeaderHeight` etc. picks up the scaled
  value without edits — the rename strategy is in-place rather than
  per-callsite.
- Default arguments in `render/layout.hpp` (VerticalLayout/HorizontalLayout/
  GridLayout/UITable) read `kLayoutPanelGap` / `kLayoutButtonGap` at call
  time, so they auto-scale too. Callers that pass explicit gaps still
  need to feed scaled values themselves.
- Pocket 3 / Touch preset is a single pill that bundles `uiScale = 2.0`
  AND `interactionMode = "touch"` (see Touch Mode note). The "active"
  state on the pill requires both fields to match the preset, so a
  half-applied state (e.g. scale 2.0 but mouse mode) renders as inactive.

## Touch Interaction Mode (v0.76.15)
- `Project::interactionMode` (`"mouse"` default, `"touch"` alternative)
  persisted as `interaction_mode` in the show file.
- `App::inTouchMode()` is the only accessor render code should consult.
  Three suppression sites today:
  - playlist splitter hover (`app_render_control.ipp`)
  - inspector splitter hover (`app_render_main.ipp`)
  - context menu item hover (`app_ui_widgets.ipp`)
- The Pocket 3 preset sets `interactionMode = "touch"` and `uiScale = 2.0`
  in lockstep. Add new touch-only ergonomic behavior here — never split
  the touch surface across a second flag.
- Right-click menus, drag-resize splitters, and other mouse-only gestures
  are intentionally not yet rewired for touch. The preset is an
  ergonomic improvement, not a full touch-input redesign — that's
  product work.

## Splash Mascot Swap (v0.76.14)
- `Project::splashCharacter` ("deckbot" default | "deckgirl") names the
  splash art. `pickSplashCandidates(character)` in `main.cpp` returns a
  fallback chain: `deckboy_splash_<name>.{mp4,gif,png}` then the legacy v2
  filename, then the v074 plain art. `refreshSplashAsset()` re-resolves
  and reloads the texture.
- Both assets live in `data/ui/deckboy_ui_pack_v3/splash/`. To add a third
  character: drop `deckboy_splash_<name>.png` into that folder and either
  extend the toggle in settings or set the value via a saved project.
- For an animated mascot: drop `deckboy_splash_<name>.mp4` (or `.gif`)
  next to the PNG and teach `UiImageAsset` to render video frames. The
  pickSplashCandidates chain already tries those extensions first, so the
  upgrade lands without touching the splash overlay code path.

## GPU Hardware Decode + NV12 Upload Path (v0.76.13)
- `startDecoderThreads` passes `-hwaccel auto` before `-i`. FFmpeg picks the
  best hardware decoder (DXVA2/D3D11VA on Windows, NVDEC/VAAPI/VDPAU on
  Linux/macOS) and inserts a `hwdownload + format=...` filter automatically
  when the downstream filter chain needs CPU frames. Falls back to software
  decode silently when no hardware backend is available.
- **Pipe pixel format is per-cue.** A cue with `chromaKeyEnabled` or active
  color controls (`brightness`/`contrast`/`saturation`/`hueShift` ≠ unity)
  decodes as `rawvideo rgba`, because the CPU effects path
  (`applyCueVisualEffectsToPixels`) mutates interleaved RGBA bytes. Every
  other cue decodes as `rawvideo nv12` — planar Y plane followed by
  interleaved UV, 12 bpp instead of 32, cutting pipe + system-RAM traffic
  by ~62%. Decision is frozen at TAKE time; the engine reads the cue once
  in `startDecoderThreads`.
- **Frame shape is tagged.** `DecodedFrame::format` carries the layout
  (`FramePixelFormat::RGBA32` or `NV12`). All six SDL_Texture upload sites
  branch on it:
  - `MediaEngine::uploadFrame` — main deck texture
  - `renderDeckLayerIntoOutput` — per-output layer bridge
  - `renderOverlayFrameIntoOutput` — per-overlay bridge
  - `uploadPreviewCueTexture` — preview-cue texture
  - `update()` focused-engine block — control-window preview texture
  Each tracks `Uint32` cached SDL pixel format alongside cached
  width/height and recreates the texture when format OR dimensions
  change. `syncFrameTexture()` in `render/texture_helpers.hpp` is the
  shared helper for the create/upload pair; callers that need
  CPU-effects pre-processing (the bridge and main-deck paths) inline the
  branch instead so they can apply effects only on the RGBA arm.
- **NV12 requires even dimensions** because the chroma plane is at half
  resolution. `startDecoderThreads` rounds `decodeW`/`decodeH` down to
  even before building the scale filter; `frameBufferSize()` does the
  same trim when computing the byte count. Sources virtually always have
  even dimensions, but the trim avoids a partial-UV-row hazard.
- **CPU scaler is fast_bilinear**, not bicubic. Bicubic costs ~3–4× more
  per frame and is indistinguishable on moving video at deck-output
  sizes. Still-image and thumbnail paths (`loadStillFrame`,
  `decodeSingleFrame`) keep `flags=neighbor` — that decision is
  deliberate for pixel-art and diagram stills.
- **Live effect-toggle limitation.** If the operator enables chroma key
  or color controls on a cue already decoded as NV12, the effect will
  not appear visually until the next TAKE — the upload path takes the
  NV12 branch and skips the RGBA effects scratch. If you need
  live-toggleable effects, enable at least one effect parameter on the
  cue before TAKE so the decoder picks RGBA up front. A real fix would
  push effects to a fragment shader so they run on the GPU regardless
  of pipe format; that's left as future work.

## TSL/Tally Protocol Note
- UDP listener on port 5800 (configurable). Supports TSL 3.1 (20-byte packets) and
  TSL 5.0 (variable-length). Sends tally state on every deck active-status change.
- `tslTallyEnabled` / `tslTallyPort` added to `Project`. Tally thread started/stopped
  alongside other integration adapters in `applyIntegrationRoute`.
- Each active deck maps to a TSL address (deck 0 → address 1, etc.). PGM bit set when
  deck is active (playing/paused with active cue); PVW bit set when deck is the
  currently focused/selected deck in standby.

## SRT Input Source Note
- `CueKind::SrtStream` is a dedicated cue kind for live stream input (srt://, rtmp://,
  rtsp://, udp://). The cue path stores the full stream URL.
- Added via SOURCE menu → "Stream Cue (SRT / RTMP / RTSP)" → URL prompt.
- `startDecoderThreads` detects `cue.kind == CueKind::SrtStream` and skips ffprobe and
  the `-ss` seek flag. FFmpeg receives the URL directly as `-i URL`.
- The ffmpeg build shipped with Deckboy must be compiled with `--enable-libsrt`.
- Inspector shows a URL editor row (edit path via `QuickAction::EditBrowserUrl`).

## NDI Receive Input Note
- `CueKind::NdiSource` is a dedicated cue kind for NDI receive input.
- `cue.path` stores `ndi://SOURCE_NAME`. The engine strips the prefix and passes the
  name to ffmpeg as `-f libndi_newtek -i SOURCE_NAME`.
- Added via SOURCE menu → "NDI Source Cue" → source name prompt.
- `startDecoderThreads` detects `cue.kind == CueKind::NdiSource`: sets `isNdiSource=true`,
  `isLiveStream=true`; skips ffprobe and `-ss`.
- Inspector shows a source name editor row (edit via `QuickAction::EditBrowserUrl`).
- Windows DLL candidates for NDI SDK in `ndi_api.hpp` / `ndi_trigger_api.hpp`.

## Audio Buffer Size Tuning Note
- `Project::audioBufferSamples` (256/512/1024/2048, default 1024) controls the SDL
  audio buffer size passed to `SDL_OpenAudioDevice` in `openMainAudioDevice` and the
  UI audio device open call.
- Smaller buffers reduce audio-to-video sync latency. Larger buffers improve stability
  on slower/loaded systems.
- On Windows, SDL2 uses WASAPI in shared mode. There is no mechanism to switch to
  WASAPI exclusive mode or ASIO via SDL2. True ASIO support would require PortAudio
  with the Steinberg ASIO SDK — deferred pending SDK licensing review.
- Buffer size changes take effect on the next app restart (audio devices are opened
  during init, not on-the-fly).

## Startup Project Restore Note
- Startup no longer assumes `data/default.deckboy` is synonymous with “previous
  show.”
- Deckboy now remembers the actual last opened/saved project path in
  `data/last_project.txt` and uses that to seed the startup dialog/load path on
  the next launch.

## Saved Show Path Repair Note
- The current `data/default.deckboy` file had two collapsed Windows media paths
  (`G:...`) that prevented the previous-show flow from finding its clips.
- Those saved cue paths were repaired directly in the project file to valid
  `G:\\...` paths.
- The more aggressive auto-repair-at-startup experiment was removed after it
  proved too risky for startup stability.

## Cue Inspector Text Clip Note
- The inspector scroll viewport clip alone was not enough; text could still
  render outside its own label/value rect when a row was only partially visible.
- `drawTextSafe()` and `drawCenteredTextSafe()` now intersect the active
  renderer clip with the real control bounds before drawing, which keeps
  scrolled parameter text visually locked inside its box without clipping text
  to an overly shrunken inner rect.
- The shared inspector row renderer now also uses slightly taller rows, wider
  horizontal gaps, and slightly roomier internal text spacing so the cue
  inspector feels cleaner without squeezing labels and values into unreadable
  widths.

## Timeline Scrub Note
- The old timeline input path only sought once on mouse-down.
- There is now a dedicated `timelineScrubActive_` state so left-button hold +
  drag keeps sending clamped timeline seeks on mouse motion until button-up.
- The click path and drag path now share the same timeline-fraction helper, so
  trim-relative timeline views and normal full-duration views seek consistently.

## Cue Row Readability Note
- Playlist / overlay cue rows use the shared `kRowHeight`, which is now a bit
  taller to give the three-line row layout more breathing room.
- The cue name line in `renderCueRow()` now uses the smaller sans face instead
  of the larger base face, which gives long cue names more usable width before
  ellipsizing.

## Windows Live Icon Note
- The Deckboy executable and the live SDL windows are not the same icon path on
  Windows.
- Embedding an `.ico` in the executable helps Explorer/shortcuts, but the
  actual running control/output windows still need explicit `WM_SETICON`
  handling if we want the taskbar/titlebar identity to stay reliable.
- `applyDeckboyWindowIcon()` now loads `IDI_DECKBOY_APP_ICON` from the current
  module and applies both big and small icons to the control window, monitors
  window, and output windows.

## Program Monitor Layout Note
- The old right-side `NEXT` preview panel has been removed from the main
  control-window monitor area.
- `app_update.ipp` now clears/stops the corresponding preview runtime instead
  of continuing to decode a hidden next-cue monitor surface.
- Program-monitor telemetry badges now compute against the remaining header
  width after reserving space for the `WARP` button and title label, so they
  shrink/drop cleanly instead of overlapping the header controls.

## Async Media Task Note
- The main update loop now treats media-probe and waveform futures as fallible
  background work instead of assuming `future.get()` can never throw.
- This is important for operator robustness: a bad probe/decode should degrade
  to a failed asset analysis state, not terminate the whole app.
- Windows waveform analysis now mirrors the Unix code path by draining ffmpeg
  output with `_read()`, which was previously skipped under `_WIN32`.

## Seek Frame Hold Note
- `MediaEngine::seek()` now defaults `clearVisualFrame` to `false`.
- The main reason is operator-facing transport behavior: jumps, scrubs, and
  quick seek actions should keep the last good frame visible until the decoder
  publishes replacement pixels.
- This prevents preview/output flashes to black during routine navigation while
  still leaving `seek(..., true)` available for any path that truly wants a
  hard visual clear.

## Output Display Switch Note
- `applyOutputDisplaySelection()` now treats fullscreen exit, geometry update,
  and fullscreen re-entry as separate steps.
- Manual output-display changes (`setOutputDisplayIndex`,
  `cycleOutputDisplay`, and `sizeFocusedOutputToSelectedDisplay`) now route
  through one fullscreen restore path instead of two.
- Geometry updates only occur after SDL reports the output window is no longer
  fullscreen, which avoids the worst multi-monitor thrash during display moves.
- For enabled window outputs, display reassignment now prefers recreating that
  one output runtime on the target display, which is more robust on Windows
  than asking the same fullscreen window to migrate in place.
- That recreation is now queued onto the next update tick instead of happening
  directly inside the display-picker action, so SDL gets one deliberate teardown
  and rebuild instead of overlapping the user's click with recovery/fullscreen
  churn.
- `destroyOutputRuntime()` now clears the pending display-transition flags and
  timers so a rebuilt output starts from a clean state.

## Windows Launch Note
- The Windows target now sets `WIN32_EXECUTABLE`, so `Deckboy.exe` launches as
  a GUI app rather than spawning a blank console window.
- `native/main.cpp` now shares startup through `runDeckboyMain()` and adds a
  Windows `WinMain` wrapper that reconstructs UTF-8 argv values with
  `CommandLineToArgvW`, keeping the CLI code paths aligned with the normal app
  launch path.

## Keyboard Focus Note
- `processEvents()` now forwards `SDL_KEYDOWN` into `handleKeyDown()` only when
  the event came from the main control window, plus `Esc` from output windows
  for fullscreen safety handling.
- This avoids transport/editor shortcuts firing from secondary Deckboy windows,
  which was especially confusing when the control window was not the active
  place receiving text input.
- `openInlineTextEditor()` now raises the control window before
  `SDL_StartTextInput()` so token-entry tools such as `Ctrl+G` behave more
  predictably on multi-window setups.

## Final Naming Notes
- The internal CMake target remains `deckboy-native` for now, but the release-
  facing output name is now `Deckboy`.
- This keeps existing target references stable in CMake while making the built
  app, workflow artifacts, and user-facing docs match the real product name.
- Because `v0.75.0` was already tagged before this rename landed, the repo
  version advances to `0.75.1` for the first release that ships the corrected
  final output name.
- Startup dialog, splash overlay, and About/settings branding now derive their
  visible version line from generated version metadata instead of stale
  hardcoded `0.74` constants.

## Version Flow Notes
- `VERSION` is now the single source of truth for Deckboy's SemVer version.
- CMake reads `VERSION`, parses the numeric core into `project(... VERSION ...)`,
  and generates `deckboy_version.hpp` so native code can print the same version.
- `Deckboy --version` is now the quickest sanity check when a local build
  or GitHub artifact feels ambiguous.
- GitHub Actions now guard `v*` tags against `VERSION` before running
  Linux/macOS/Windows build jobs, so a mistyped tag cannot silently create a
  mismatched release candidate build.

## Deckboy 0.60 Cleanup + Portability Audit
- Shared runtime fix note:
  - timeline strip EOF sampling and strip publish behavior are now safer in the
    shared native path, which fixes the black-final-tile issue seen on long
    clips
  - animated engineering patterns now republish with a fresh frame serial, so
    output compositors no longer hold stale still frames
  - crosshatch and checkerboard pattern loops now wrap on seam-safe phase math
- Browser cue backend note:
  - `native/platform/browser.*` now owns browser session lifecycle and phased
    startup state instead of keeping the Linux external-browser runtime smeared
    through `native/app/app_output_mgmt.ipp`
  - current behavior is still an external-browser Linux backend; native webview
    or more owned rendering remains future work
- `deckboy-0.60` is now in an audit / cleanup phase rather than a keep-adding-
  features phase.
- **Audit roadmap:** see `docs/AUDIT_ROADMAP.md` for the full task map covering
  remaining optimization and cleanup work.
- **Companion thread safety (fixed):** `companionClientsMutex_` now protects
  `companionClients_` + `companionClientBuffers_` in both the network thread
  (`companionLoop`) and main-thread shutdown (`stopCompanionControl`).
- **Pre-converted palette (migrated + fixed):** `Palette pal` struct holds
  `SDL_Color` versions of all 10 theme colors + `scanlineAlpha`. Rebuilt on
  theme load via `rebuildPalette()`. All ~1247 `colorFromRgba(kConstant)` calls
  migrated to `pal.*` members. (Bug fix: `rebuildPalette()` was a no-op — now
  converts from kConstants.)
- **Beveled panel rendering:** `drawUIPanel()` and `Primitives::drawFramedPanel()`
  draw beveled edges. Accent-vs-fill luma comparison determines raised/inset.
  No signature or call-site changes.
- **Scanline overlay:** Procedural 1×4 texture rendered before each present.
  `scanline_alpha` theme key (0=off, default 18).
- **Theme system:** 7 themes in `data/themes/`: gameboy (default), dark, pocket,
  color, advance, sp. Set via `DECKBOY_THEME=name` env var. Users create custom
  themes by adding `data/themes/mytheme/theme.txt`.
- **Inspector helpers (shared):** `InspectorCtx` struct + 15 `insp*()` member
  functions (~line 34529) provide shared implementations for both docked and
  floating cue inspector paths. Both render paths use thin wrapper lambdas.
  `fmtFloat()`/`fmtPercent()` use `snprintf` (zero heap alloc).
- Immediate operational priority:
  - remove the last active modal text-entry flows from the live UI
  - park half-finished overlay/scene authoring surfaces until the core app is
    steadier
- Active UI rule for this phase:
  - prefer `openInlineTextEditor(...)` everywhere the operator is already inside
    Deckboy
  - do not reintroduce ad-hoc `zenity` / modal prompt text entry for normal
    show-control editing
- The old deck-level auto-advance flag is now treated as legacy:
  - cue endings are per-cue only
  - save/load still tolerates old `auto_advance` fields for compatibility
  - do not build new UI/state on top of `Deck::autoAdvance`
- Lower Third / PIP / Composite current stance:
  - existing cues still load, inspect, save, and render
  - new cue creation from the bottom bar / hotkeys / remote add commands is
    intentionally parked for now
  - this reduces UI clutter while keeping forward-compatibility work on the
    branch
- Portability audit conclusion:
  - no major product or runtime-ownership rewrite is required to make
    portability realistic
  - the real blockers are backend/runtime seams:
    - Unix-first child-process execution (`fork/execvp`, FIFO-based stream feed)
    - Linux-only browser/source capture (`Xvfb`, `x11grab`, `v4l2`)
    - Windows/macOS backend completion for capture, stream egress, and runtime
      loading

## Phase 4 Inline Editing + Floating Panels
- Panel presentation/visibility is now a real persisted part of
  `UiWorkspaceState`, not just a computed summary.
- The main helpers added/extended in `native/main.cpp`:
  - `panelIsVisible(...)`
  - `panelPresentation(...)`
  - `panelIsDockedVisible(...)`
  - `setPanelVisible(...)`
  - `setPanelPresentation(...)`
  - `cyclePanelWorkspaceMode(...)`
  - `panelHasLocalFocus(...)`
  - `panelFocusBadge(...)`
- `Deckboy Panels` is a secondary floating workspace window used for popped-out
  singleton operational panels. Current behavior:
  - renders floating `Program / Transport` as a live summary panel
  - renders floating `Cue Inspector` as a live summary panel
  - renders floating `Routing`, `Master Scene`, and `Output Panels`
  - `DOCK` returns a floating panel to the main control workspace
- Important limitation:
  - floating `Program / Transport` and floating `Cue Inspector` are mirrored
    summaries in this pass, not full independent interactive clones of the main
    control workspace render path
  - this avoids renderer-specific texture duplication bugs while keeping the
    pop-out workflow real and safe
- Inline operational editing should prefer `openInlineTextEditor(...)` over
  `pickTextInput(...)` whenever the operator is already in the live control UI.

## Phase 3 Workflow Polish
- Shared dropdown scaffolding is now the standard selector path for operational
  UI selection surfaces. Active dropdown-based settings selectors include:
  - audio output device
  - output display
  - stream protocol
  - mirror source
- The old blocking list-picker path was removed from active UI flows.
- `nextCueIndexForDeck(...)` is now the canonical UI helper for `what is next`.
  It is used by:
  - `renderMainPanel()` summaries
  - `renderCueRow()` deck playlist rows
- Text-safe cleanup in this pass focused on the highest-density views:
  - `renderDecksPanel()`
  - `renderDeckSidebar()`
  - `renderMainPanel()`
  - `renderSettingsModal()`
  - `renderDropdownPopover()`
- The default control workspace was rebalanced to favor the center
  `Program / Transport` region over the right-side operational column.

## Phase 2 Operational Panel Split
- The control workspace is no longer treated as one render block conceptually.
- Current operational panel functions in `native/main.cpp`:
  - `renderPlaylistColumn()` -> docked `Deck Playlist`
  - `renderDecksPanel()` -> floating/repeating `Deck Playlist` views
  - `renderMainPanel()` -> `Program / Transport` + `Cue Inspector`
  - `renderOutputPanelsPanel()` -> repeating `Output` panels
  - `renderRoutingMatrixPanel()` -> singleton `Routing` panel
  - `renderDeckSidebar()` -> singleton `Master Scene` panel
- Shared panel chrome helper:
  - `drawOperationalPanel(...)`
- Shared rendered-frame sync helper:
  - `recordRenderedPanelFrame(...)`
- Current Phase 2 limitation:
  - only `Deck Playlist` has a true floating window surface today (`Decks window`)
  - `Program / Transport`, `Cue Inspector`, `Routing`, `Master Scene`, and `Output`
    are modular/persisted panels but still render docked inside the control window
  - panel persistence is ready for future pop-out/docking work, but that behavior
    is not fully implemented yet
- Scrollable operational regions added in Phase 2:
  - `outputPanelsViewportRect_`
  - `routingMatrixViewportRect_`
  - wheel scrolling is state-driven in `processEvents()`

## Layout System (March 2026 cleanup pass)
- Grid/layout primitives live in `native/main.cpp` near the shared rect helpers:
  - `VerticalLayout`
  - `HorizontalLayout`
  - `GridLayout`
  - `UITable`
- Shared layout constants also live there:
  - `kLayoutSpacingUnit`
  - `kLayoutPanelPadding`
  - `kLayoutPanelGap`
  - `kLayoutPanelBorder`
  - `kLayoutHeaderHeight`
  - `kLayoutBottomBarHeight`
  - `kLayoutButtonHeight`
- Shared drawing helpers used by the live control window:
  - `drawTextSafe(...)`
  - `drawCenteredTextSafe(...)`
  - `drawUIPanel(...)`
  - `drawUIButton(...)`
  - `drawUIDropdown(...)`

### Inspector Section Scopes
- Cue inspector implementations live in shared `insp*()` member functions
  (~line 34529) parameterized by `InspectorCtx` struct. Both docked
  (`renderMainPanel()`) and floating inspector paths use thin wrapper lambdas.
- Key shared functions: `inspDrawQuickRow`, `inspDrawMessageRow`,
  `inspDrawActionRow`, `inspDrawEditableRow`, `inspDrawStatusRow`,
  `inspDrawKeyColorRow`, `inspDrawGeometryRows`, `inspDrawColorRows`,
  `inspDrawKeyRows`, `inspBeginSection`, `inspFinishSection`.
- Format helpers: `fmtFloat()`, `fmtPercent()`, `fmtScaleMode()` (static,
  snprintf-based — no heap alloc).
- `InspectorCtx` fields: `ctrl`, `ctrlW`, `inset`, `rowH`, `rowStep`,
  `sectionHeaderH`, `sectionGap`, `headerFont`, `valueFont`, `labelFont`,
  `ellipsize`.
- If you add a new inspector group, follow the same pattern:
  1. begin section (`inspBeginSection`)
  2. render rows (use `inspDraw*` helpers or thin wrappers)
  3. finish section with final body Y (`inspFinishSection`).
- Current section set used in the live inspector:
  - `PLAYBACK`
  - `METADATA`
  - `GEOMETRY`
  - `KEY`
  - `ROUTING`

When adjusting control-window layout, change these helpers/constants first
instead of reintroducing local pixel offsets inside render functions.

## Layout Component Map (native)
- Main control layout entry: `native/main.cpp` -> `renderControlWindow()`.
- Global header + workspace/focus strip: `renderControlWindow()`.
- Deck Playlist panel + cue rows: `renderPlaylistColumn()` and `renderCueRow()`.
- Program / Transport panel: `renderMainPanel()` (`Program monitor`, `Preview monitor`, `STACK VIEW`, timeline, summaries).
- Cue Inspector panel: `renderMainPanel()` (section helpers + row helpers).
- Master Scene panel: `renderDeckSidebar()`.
- Output panels: `renderOutputPanelsPanel()`.
- Routing Matrix panel: `renderRoutingMatrixPanel()`.
- Preferences modal remains in `renderSettingsModal()`.
- Splash overlay and startup dialog: `renderSplashOverlay()` and `renderStartupDialog()`.

## Terminology Policy
- Operator-facing UI/docs should use:
  - `Master Scene`
  - `Decks window`
  - `Window Output` / `Stream Output` / `NDI Output`
  - `Window Source` / `Camera Source` / `Syphon/Spout Source`
  - `Lower Third`
- Compatibility aliases stay in place unless there is an explicit migration plan:
  - `GROUP` / `SCENE` command aliases for Master Scene control
  - `.deckboy`, `DECKBOY_*`, `/deckboy/*`, and `deckboy-native`

## Workspace Foundation
- Runtime panel/workspace scaffolding lives in `native/main.cpp` inside `App`:
  - `UiPanelCategory`
  - `UiPanelPresentation`
  - `UiPanelKind`
  - `UiPanelKey`
  - `UiPanelDefinition`
  - `UiPanelState`
  - `UiPanelManager`
  - `UiWorkspaceState`
  - `UiFocusState`
- Sync/persistence helpers:
  - `syncUiWorkspaceState()`
  - `uiWorkspaceSummaryLine()`
  - `uiFocusSummaryLine()`
  - `saveUiWorkspaceNow()`
  - `loadUiWorkspaceFromDisk()`
  - `applyUiWorkspaceState()`
  - `flushDirtyUiWorkspace()`
- Current mapping is intentionally conservative:
  - singleton modules: Program / Transport, Preview, Cue Inspector, Routing, Master Scene, Preferences
  - repeating modules: Deck Playlist, Output
- Workspace persistence file:
  - `data/deckboy.workspace`
  - tab-delimited, same escape rules as `.deckboy`
  - separate from show files on purpose so Phase 1 does not mutate project serialization
- Persisted in Phase 1:
  - panel visibility
  - panel presentation
  - panel frames
  - control window frame
  - Decks window frame
  - focused panel
  - focused Deck / Output / Cue
  - Decks window manual-open state
  - Master Scene sidebar visible/expanded state
  - last settings tab
- Not implemented yet:
  - docking
  - multiple named workspaces
  - real pop-out singleton panels
  - restoring modal panels open on launch
- Selector pattern for future migrations:
  - `DropdownState`
  - `openDropdown(...)`
  - `renderDropdownPopover()`
  - current proof path: bottom-bar Pattern selector / cue Pattern selector

## DMG Palette Tuning
Palette constants live in `native/core/constants.hpp`:
- `kScreenDeepColor` (`#0f380f`)
- `kScreenDarkColor` (`#306230`)
- `kScreenMidColor` (`#8bac0f`)
- `kScreenLightColor` (`#9bbc0f`)

For readability tuning, prefer changing only these constants first so all framed panels/text inherit consistently.

## Adding Cue-Type Icons
Cue list type tokens are defined in `renderCueRow()` (`typeIcon` switch on `CueKind`).
- Update that switch to add or adjust tokens.
- Keep tokens short (3-4 chars) so fixed columns remain stable.
- If adding a new `CueKind`, update both:
  - `native/core/types.hpp` (`enum class CueKind`)
  - `renderCueRow()` type switch.

## Routing Table Wiring
Video Outputs routing rows use per-deck action ranges in `native/main.cpp`:
- `kSettingsActionRoutingTableOutputPrevBase`
- `kSettingsActionRoutingTableOutputNextBase`
- `kSettingsActionRoutingTableLayerDecBase`
- `kSettingsActionRoutingTableLayerIncBase`
- `kSettingsActionRoutingTableAssignToggleBase`

Click handling lives in `handleSettingsClick()`.

## Warp Mode Implementation
- Deck warp state now includes `Deck.warpMode` (`linear` | `perspective`) in `native/core/types.hpp`.
- Normalize/save/load wiring lives in:
  - `normalizeWarpMode(...)`
  - `saveProject(...)` / `loadProject(...)` deck row handling in `native/main.cpp`.
- UI control lives in Video Outputs -> Advanced row:
  - action id `kSettingsActionOutputWarpModeCycle`
  - handled in `handleSettingsClick()`.
- Command control lives in `handleRemoteCommand(...)`:
  - `VIDEO WARP MODE LINEAR|PERSPECTIVE|NEXT|PREV`
  - direct aliases: `VIDEO WARP LINEAR|PERSPECTIVE`.
- Render behavior:
  - `linear`: existing quad geometry path
  - `perspective`: tessellated projective UV mapping via `renderPerspectiveWarp(...)`.
  - Mesh density is controlled by `kCols` / `kRows` inside `renderPerspectiveWarp(...)`.

## Portability Backends
- Capture backend interfaces now live in:
  - `native/platform/capture_backend.hpp/.cpp`
  - Catalog API: `createCaptureBackendCatalog()`
  - Runtime planning API: `planSourceCapture(const SourceCaptureRequest&)`
- `MediaEngine::buildSourceCaptureArgs(...)` now delegates source cue FFmpeg arg
  planning to `planSourceCapture(...)` (Linux backends active, other OSes stubbed).
- Output backend interfaces now live in:
  - `native/platform/output_backend.hpp/.cpp`
  - Catalog API: `createOutputBackendCatalog()`
  - Route planning API: `planOutputBackendRoute(const OutputBackendRouteRequest&)`
- Runtime egress dispatch now uses backend route planning in `renderOutputWindow()`:
  - stream send is gated by `route.streamSupported`
  - NDI send is gated by `route.ndiSupported`
  - stream runtime is stopped automatically when stream route is not available.
- `--self-check` prints backend introspection lines:
  - `capture-plan-defaults: ...`
  - `output-route-defaults: ...`
  - `integration-route-defaults: ...`
- Top-level CMake now prefers exported `SDL2` / `SDL2_ttf` config packages,
  then falls back to pkg-config/manual lookup. macOS framework feature gates
  use `deckboy_target_link_frameworks(...)`.
- `native/core/paths.cpp` now resolves executable paths on Linux/macOS/Windows
  and expands sans/mono font lookup to macOS + Windows system font locations.
- `native/core/subprocess.*` now provides a unified `spawnProcess()` entry point
  with `SpawnOptions` (StdioMode for stdin/stdout/stderr, detached flag). Legacy
  wrappers `spawnPipeProcess()` / `spawnDetachedProcess()` / `readAllText()` are
  thin forwards so existing call sites need no changes. The old inline
  `spawnDetachedProcess()` definition was removed from `native/main.cpp`.
  Windows builds stub all paths safely; macOS builds do not hard-require
  `MSG_NOSIGNAL` on socket sends.

## Integration Adapter Foundation
- Integration backend planning APIs now live in:
  - `native/platform/integration_backend.hpp/.cpp`
  - Catalog API: `createIntegrationBackendCatalog()`
  - Route planning API: `planIntegrationBackendRoute(const IntegrationBackendRouteRequest&)`
- Network tab integration controls are rendered in `renderSettingsModal()`
  (`settingsTab_ == 2`, `INTEGRATION ADAPTERS` block).
- Actions are handled in `handleSettingsClick()`:
  - `kSettingsActionIntegrationAtemToggle`
  - `kSettingsActionIntegrationNdiTriggerToggle`
  - `kSettingsActionIntegrationNmcToggle`
  - `kSettingsActionIntegrationMtcToggle`
  - `kSettingsActionIntegrationLtcToggle`
  - `kSettingsActionIntegrationArtNetToggle`
  - `kSettingsActionIntegrationArtNetPortPrompt`
  - `kSettingsActionIntegrationAllToggle`
- Companion/OSC command wiring is in `handleRemoteCommand(...)` and
  `mapOscToRemoteCommand(...)` for:
  - `ATEM`, `NDITRIGGER`, `NMC`, `MTC`, `LTC`, `ARTNET`, `ARTNETPORT`, `INTEGRATIONS`.
- Runtime listeners (Linux/macOS path) live in `native/main.cpp`:
  - `startAtemBridgeListener()` / `atemBridgeLoop()`
  - `startNdiTriggerBridge()` / `ndiTriggerLoop()`
  - `startArtNetBridgeListener()` / `artNetBridgeLoop()`
  - bridge lifecycle wrappers: `startIntegrationBridges()` / `stopIntegrationBridges()`.
- NDI metadata trigger runtime details:
  - dynamically loads `libndi` with `NdiTriggerApi`
  - metadata frames are enqueued as `NDIEVENT ...`
  - payload parsing is centralized in `handleNdiTriggerPayload(...)`
  - optional source selection currently uses `DECKBOY_NDI_TRIGGER_SOURCE`
    until a proper UI picker exists.
- NMC sync runtime details:
  - lifecycle/state lives in `refreshNmcSyncState()` / `startNmcSyncBridge()` /
    `stopNmcSyncBridge()`
  - input mode is a UDP listener thread (`nmcSyncLoop()`) that enqueues
    `NMCEVENT ...`
  - payload application is centralized in `handleNmcSyncPayload(...)`
  - output mode is polled from `tickNmcSyncOutput()` inside `update()`
  - current config is env-driven: `DECKBOY_NMC_MODE`, `DECKBOY_NMC_PORT`,
    `DECKBOY_NMC_HOST`, `DECKBOY_NMC_SOURCE`, `DECKBOY_NMC_LOCATE_MS`

## UI cleanup notes (March 2026)

- Composite cue first implementation cut (`deckboy-0.60`):
  - cue kind + serialization are live in `native/core/types.hpp` and
    `saveProject(...)` / `loadProject(...)`
  - add flows:
    - `addCompositeCue()`
    - bottom `SCENE` media button
    - `M` keyboard shortcut
    - remote aliases: `COMPOSITE`, `SCENE`, `MULTIVIEW`
  - this first pass intentionally uses a scene placeholder renderer:
    - `renderCompositeCuePlaceholder(...)`
    - used in Program monitor, Preview monitor, and output render path
    - avoids black / invalid runtime behavior while the slot-runtime phase is
      still pending
  - inspector path currently supports:
    - layout presets `2-UP`, `70/30`, `QUAD`
    - up to 4 saved slot sources
    - cycling a designated audio slot
    - attached overlays from the overlay bin
  - next phase should replace placeholder slot cards with real per-slot source
    runtimes, most likely by adapting the existing `PIP` source-resolution
    pattern to a per-slot runtime map
- Composite cue planning notes:
  - see `docs/COMPOSITE_CUE_SPEC.md`
  - recommendation is to add a first-class `Composite` cue rather than a
    generic live layer system
  - reasoning is architectural, not aesthetic:
    - Deckboy currently has one primary live cue (`Deck::activeIndex`)
    - overlays are sidecar items (`Deck::overlayActiveIndices`)
    - output rendering is main scene first, then overlays
  - the spec proposes reusing the existing source-resolution pattern pioneered
    by `PIP`, but moving multi-source authored layouts into a main-cue runtime
    instead of the overlay system
- Cue / warp clipboard notes:
  - cue settings copy/paste intentionally preserves cue identity and source
    media (`name`, `id`, `path`, probed metadata), and only copies the
    inspector-facing playback / geometry / key / color / overlay-attachment
    settings
  - warp copy/paste is deck-scoped and currently copies the 4-corner warp plus
    edge blends
  - warp preset naming now uses `openInlineTextEditor("warp.preset", ...)`
    instead of the older blocking picker path
- Fade defaults:
  - `Deck::playlistDefaultCueFadeSeconds` now defaults to `1.5`
  - normalization / load fallbacks in `native/main.cpp` were updated to the
    same default so new/empty projects inherit the longer fade
- Overlay subdeck/bin operator model:
  - for compatibility, overlays still live in `Deck::cues` internally, but the
    active control-surface layout now splits them into a dedicated `OVERLAY BIN`
    instead of mixing them into the main playback rundown
  - the overlay bin is conditional: if there are no overlay-only cues, the main
    rundown expands to fill the space
  - `cueIsOverlayOnly(...)` is the gate for `Lower Third` / `PIP`
  - `nextCueIndexForDeck(...)`, `selectRelative(...)`, and cue-end
    auto-advance now skip overlay-only cues so looping/next logic stays about
    the primary playback sequence
  - playlist mouse hit-testing now has separate primary-list vs overlay-bin
    regions; drag reorder is intentionally limited to main cues for now
  - main rundown and overlay bin maintain separate wheel-scroll offsets
- PIP operator controls:
  - `PIP` is now source-driven rather than cue-target-driven:
    - supported inspector source types are `media`, `browser`, `window`,
      `camera`, and `syphon/spout`
    - legacy cue-linked PIP cues are still loadable and editable as
      `Legacy Cue Link`
  - live PIP overlay runtimes now build a resolved runtime cue from the chosen
    source type before loading the overlay media engine
  - corner presets and size presets are rendered inline in the `PLAYBACK`
    section, with exact geometry still handled below in `GEOMETRY`
  - `anchorPipCueToCorner(...)` uses current output size plus the cue's scale
    to keep the preset inset visible with a consistent margin
- Primary cue overlay attachments:
  - non-overlay cues now expose an `OVERLAYS` section in the cue inspector
  - each cue can attach one `Lower Third` and one `PIP` by overlay-bin
    cue token/id/number/name
  - attachments fire on `TAKE` only and intentionally do not retrigger on loop
- Cue-row playback state controls now live in `renderCueRow(...)`:
  - icon-only buttons are rendered directly on each cue row for fade in,
    fade out, loop, hold, and cue audio
  - click handling is routed through `cueRowActionHits_` in
    `handleMouseDown(...)`, then dispatched with existing `QuickAction` wiring
- The footer `MEDIA / TRANSPORT / OUTPUT` strip is now cleaner:
  - section labels are drawn inside their panel groups in `renderButtons()`
  - old footer `Source` / `Pattern` selectors were removed from
    `renderButtons()` / `handleMouseDown()`
  - the media group now includes a dedicated `LOWER 3RD` button
- Footer tiles were later resized back up after the first cleanup pass:
  - `kLayoutBottomBarHeight` / `kLayoutButtonHeight` were increased again so
    bottom-bar labels fit without clipping
  - telemetry pills beside the program monitor now render `label + value`
    separately instead of squeezing everything into one clipped string
- Source cue type selection moved into the cue inspector:
  - `cueSourceTypeDropdownRect_` anchors the dropdown
  - `setSelectedSourceCueKind(...)` swaps source cue kind in-place while
    preserving/re-normalizing the source reference where possible
- Lower Third cues now have direct inspector-side editing:
  - `QuickAction::EditLowerThirdText`
  - `QuickAction::EditLowerThirdSubtext`
  - both actions use `openInlineTextEditor(...)` and update focused selected
    lower-third cues in place
- `clearOutput()` now clears `overlayActiveIndices` immediately so output clear
  removes live lower-third overlays during the fade rather than waiting for the
  deferred cleanup callback
- Settings modal layout was reorganized in `renderSettingsModal()`:
  - `System` now emphasizes `Appearance`, `Safety / Timecode`, `Show Flow`,
    `Cue Tools`, and `Playlist Prefs`
  - audio device selection moved to the `Audio` tab
  - `Network` now uses larger cards for Companion/OSC, OSC Query/Feedback, and
    integration adapters
- `PIP` cue implementation notes:
  - `CueKind::Pip` uses the existing overlay stack (`overlayActiveIndices`)
    instead of inventing a second deck/sub-deck
  - each live `PIP` cue owns a separate silent `MediaEngine` keyed by
    `deckIndex:cueIndex`, so the inset can play independently of the main deck
  - output rendering reuses normal cue geometry/color/key controls, so PIP
    placement is just the standard cue geometry path applied to the overlay
  - `setSelectedPipCueTarget(...)` updates the selected cue, reloads its
    target thumbnail, and refreshes any live overlay runtime
- UI motion policy changed:
  - `project.uiTransitionsEnabled` is normalized back to `true`
  - `System -> Appearance` now treats motion as always-on feedback rather than
    exposing an operator-facing `ANIM OFF` state
- Playback flag helpers were broadened so inspector/cue-row toggles behave
  consistently for still/source/browser/pattern/lower-third cues:
  - `toggleSelectedLoop()`
  - `toggleSelectedPauseOnLastFrame()`
  - `adjustSelectedFade(...)`
  - `setSelectedFade(...)`
- MTC ingest runtime is decoded in `midiLoop()`:
  - `SND_SEQ_EVENT_QFRAME` -> `decodeMidiMtcQuarterFrame(...)`
  - internal command ingress `MTCEXT <seconds> <fps>`
  - applied via `ingestIntegrationTimecode(...)`.
- Art-Net runtime command mapping is centralized in `handleArtNetEvent(...)`
  (ch1-10 mapping to transport/master cue commands).
