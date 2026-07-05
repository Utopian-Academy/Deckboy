# Deckboy Code Map

A structural map of the codebase: where things live, how data flows, and which
threads touch what. Companion documents: `CLAUDE.md` (AI session quick-reference),
`DEVNOTES.md` (architectural decision log), `docs/AUDIT_ROADMAP.md` (cleanup task map).

Line counts are approximate as of v0.76.18.

---

## Top-level layout

```
deckboy/
├── VERSION              # single source of truth for the app version
├── CHANGES.md           # user-facing changelog (dated entry per version)
├── DEVNOTES.md          # internal architectural decision log
├── MANUAL.md            # operator manual
├── PORTABILITY.md       # cross-platform notes and env vars
├── CLAUDE.md            # AI session quick-reference
├── docs/                # specs, roadmap, this map, streamdeck profile
├── native/              # ALL native C++ source (see below)
├── data/                # fonts, themes, UI art, default/last project files
├── art/                 # icon sources + Windows .rc template
├── tools/               # package_windows.ps1 → dist zip
├── scripts/             # smoke.sh, demo show generators (Linux)
└── companion/           # Bitfocus Companion module bits
```

## native/ — source tree

One executable target (`Deckboy`). `main.cpp` defines the `App` class and
`#include`s every `app/*.ipp` file into it — the `.ipp` files are **method
bodies of App**, not separate translation units. The whole app plus engine is
~49k lines across the files below.

### App core (main.cpp + app/*.ipp — one class, one TU)

| File | ~Lines | Owns |
|------|-------:|------|
| `main.cpp` | 5,700 | App class decl, settings action constants, palette/theme, fonts, save/load field helpers (`escapeField`), `run()` loop, `WinMain`, all member state |
| `app/app_output_mgmt.ipp` | 4,200 | Output runtime lifecycle: windows, fullscreen recovery, display selection/hot-plug, stream/NDI/DeckLink/Spout egress start/stop, `OutputBackendRuntimeRoute` |
| `app/app_cue_transport.ipp` | 3,000 | Take/go/stop/seek, deck transitions, auto-advance, overlay (PIP/lower-third) runtimes, panic profiles |
| `app/app_render_main.ipp` | 2,800 | Control-window Program/Transport panel, timeline, inspector host, loading animations |
| `app/app_project_state.ipp` | 2,600 | `saveProject`/`loadProject` (tab-delimited `.deckboy`), STATUS/STATE JSON snapshot builders, undo snapshots, workspace persistence |
| `app/app_cue_mgmt.ipp` | 2,200 | Import (async ffprobe), add/delete/reorder cues, cue editing actions, clipboard |
| `app/app_remote_command.ipp` | 2,100 | `handleRemoteCommand()` text-command dispatcher (Companion/OSC/HyperDeck all funnel here) |
| `app/app_render_settings.ipp` | 2,100 | Settings modal render + `handleSettingsClick` Part1/2/3 |
| `app/app_network.ipp` | 2,000 | Companion TCP/UDP loop, OSC parse/feedback, OSC Query HTTP, HyperDeck server, ATEM/Art-Net/NMC/TSL bridges |
| `app/app_render_inspector.ipp` | 1,600 | Cue inspector sections (uses shared `insp*()` helpers) |
| `app/app_render_control.ipp` | 1,400 | Playlist column, cue rows, header/footer chrome |
| `app/app_input.ipp` | 1,100 | Keyboard + mouse dispatch, hotkeys |
| `app/app_ui_widgets.ipp` | 1,000 | Dropdowns, inline text editor, toasts, context menus |
| `app/app_render_output.ipp` | 900 | Per-output compositor: deck layers → output texture → window present + egress capture (`SDL_RenderReadPixels`) |
| `app/app_accessors.ipp` | 700 | Focused deck/output/cue accessors, small state helpers |
| `app/app_smoke.ipp` | 700 | `--smoke` / `--self-check` harness |
| `app/app_update.ipp` | 650 | Per-tick `update()`: event pump, async future polling, display poll, output recovery poll |
| `app/app_quick_action.ipp` | 550 | `QuickAction` enum dispatch (inspector row actions) |
| `app/app_overlays.ipp` | 470 | Splash, startup dialog, dependency prompt + runtime detection |
| `app/app_geometry.ipp` | 220 | Cue geometry math (scale modes, crop, offsets) |

### Engine

| File | ~Lines | Owns |
|------|-------:|------|
| `engine/media_engine.cpp/.hpp` | 3,150 | One instance per deck. ffmpeg subprocess decode (video pipe → `frameQueue_`, audio pipe → SDL audio), stills/patterns/browser/source frames, transport, fades, transitions. Threading model documented in the .hpp header |

### Core (freestanding utilities)

| File | Owns |
|------|------|
| `core/types.hpp` | `Cue`, `Deck`, `OutputTarget`, `LayerAssignment`, `Project`, `DecodedFrame`, enums (`CueKind`, `TransportState`, …) |
| `core/constants.hpp` | Output raster constants, palette constants, mutable `kLayout*` metrics (rescaled by UI scale) |
| `core/subprocess.hpp/.cpp` | `spawnProcess()` unified API — POSIX fork/exec + Windows `CreateProcessW` backends |
| `core/io_utils.hpp` | `readExact` / `readSome` cross-platform pipe reads |
| `core/paths.cpp` | Project-root/data/font resolution (see PORTABILITY.md) |
| `core/utils.cpp` | trim/toLower/split string helpers |
| `core/single_instance_guard.hpp` | flock (POSIX) / named mutex (Windows) instance lock |
| `core/subtitle_parser.hpp`, `core/expression_parser.hpp`, `core/pixel_effects.hpp` | SRT parse, calculator-entry math, chroma key + color controls (CPU, RGBA path only) |
| `core/system_browser.hpp` | `openExternalUrl()` for dependency prompts |

### Platform (backend seams)

| File | Owns |
|------|------|
| `platform/output_backend.hpp/.cpp` | Output backend catalog + `planOutputBackendRoute()` (window/stream/NDI/DeckLink/Spout availability) |
| `platform/capture_backend.hpp/.cpp` | Source-capture catalog + `planSourceCapture()` (x11grab/v4l2 on Linux, gdigrab on Windows, scaffolds elsewhere) |
| `platform/integration_backend.hpp/.cpp` | Integration adapter catalog (ATEM/NDI-trigger/NMC/MTC/LTC/Art-Net) |
| `platform/ndi_api.hpp` | NDI **send** runtime (dynamic load) |
| `platform/ndi_trigger_api.hpp` | NDI **receive + find** runtime (dynamic load) |
| `platform/decklink.cpp` | Blackmagic DeckLink SDK 16.0 output (COM on Windows) |
| `platform/siphon_spout.hpp/.cpp` | Spout (Windows) / Syphon (macOS) texture sharing |
| `platform/browser.cpp` | Browser cue session lifecycle (WebView2 on Windows, external Chromium on Linux) |
| `platform/midi.cpp` | RtMidi input (cross-platform) + MTC decode |
| `platform/ltc_api.hpp` | libltc dynamic load |
| `platform/network.hpp` | Socket helpers (winsock/BSD) |

### Render helpers

| File | Owns |
|------|------|
| `render/layout.hpp` | `VerticalLayout`/`HorizontalLayout`/`GridLayout`/`UITable`, font-derived row spacing helpers |
| `render/primitives.cpp` | `drawFramedPanel` beveling and low-level draws |
| `render/texture_helpers.hpp` | `syncFrameTexture()` — RGBA32/NV12 texture create+upload |

---

## Data flow: cue → glass

```
Operator TAKE
  └─ app_cue_transport.ipp → MediaEngine::loadCue(&deck.cues[i])
       └─ startDecoderThreads(): 2 ffmpeg subprocesses (video pipe, audio pipe)
            video thread:  readExact → frameQueue_ (frameMutex_)
            audio thread:  readSome → gain/fade scale → SDL_QueueAudio
  main loop each frame:
    MediaEngine::update()  — pops frames ≤ targetFrame (wall-clock × fps), uploads SDL_Texture
    app_render_output.ipp  — renderDeckLayerIntoOutput per layer assignment:
                             frame texture → bridge texture (alpha = deckOpacity × fadeGain)
                             → output compositor texture (AOI/warp/edge blend)
      ├─ presentOutputCompositorToWindow  (window outputs, vsync)
      └─ SDL_RenderReadPixels → sendOutputNdiFrame / DeckLink / Spout / ffmpeg stream stdin
```

Pipe pixel format is per-cue, frozen at TAKE: `nv12` normally, `rgba` when
chroma key/color controls are active (CPU effects path). See DEVNOTES
"GPU Hardware Decode + NV12 Upload Path".

Clocking: video position runs on the wall clock but re-anchors to the audio
device clock (frames queued − frames buffered) when they drift > 60 ms —
audio is the master for cues that have it. The engine owns a snapshot of the
loaded cue (`activeCueSnapshot_`); it never keeps pointers into `Deck::cues`.
See DEVNOTES "Engine Cue Snapshot Ownership" / "Audio-Master A/V Clock".

## Threading model

**Main thread** owns: all SDL windows/renderers/textures, `project_` and all
cue/deck/output state, the settings/UI, `update()`/`render()` loop (240 Hz
anti-spin floor, vsync-governed normally).

**Per-deck MediaEngine threads**: video decode reader, audio decode reader,
still-image decoder. Share `frameQueue_` (via `frameMutex_`) and atomics
(`decoderStop_`, `decoderEof_`, `volume_`) with the main thread.

**Network threads** (started per enabled integration): Companion TCP/UDP loop,
OSC Query HTTP, HyperDeck TCP, ATEM/Art-Net/NMC/NDI-trigger/TSL/LTC listeners.
They never touch app state directly: inbound commands are enqueued under
`remoteCommandMutex_` and drained on the main thread by
`processRemoteCommands()`; outbound STATUS/STATE is served from a snapshot
string rebuilt on the main thread under `statusSnapshotMutex_`.

**std::async pools**: ffprobe metadata (`probeFutures_`), waveform peaks
(`waveformMutex_`), thumbnails/timeline strips (`thumbnailMutex_`,
`timelineStripMutex_`). All polled non-blocking in `update()`.

## Serialization

`.deckboy` project files are tab-delimited text. The project header uses
key–value lines (`title\t...`); Deck/Cue/OutputTarget records are positional
with fields appended at the end and backward-compat guards
(`if (fields.size() >= N)`). Special characters escaped via `escapeField()` /
`unescapeField()` (main.cpp). Workspace layout persists separately in
`data/deckboy.workspace`; last project path in `data/last_project.txt`.

## Remote-control surface

All remote inputs normalize to text commands handled by
`handleRemoteCommand()` (app_remote_command.ipp): Companion TCP/UDP (port
5510), OSC messages/bundles (same port, mapped via `mapOscToRemoteCommand`),
HyperDeck protocol, Art-Net channel map, NDI metadata triggers, NMC sync.
Feedback: OSC `/deckboy/ack` + `/deckboy/state`, STATUS/STATE (plain + JSON),
TSL tally out, OSC Query HTTP.
