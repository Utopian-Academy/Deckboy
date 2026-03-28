# Deckboy Code Map

This document is a fast navigation guide for humans and coding agents.
It is intentionally practical: where the real app lives, where startup happens,
which files own which responsibilities, and where to go for common tasks.

## Source Of Truth

- `native/` is the current product code. This is the SDL2 desktop app.
- `native/main.cpp` is the spine of the native app:
  - top-level helpers
  - `App` class declaration/state
  - `App::init()`, `run()`, and `shutdown()`
  - `#include "app/*.ipp"` partial implementation files
- `native/app/*.ipp` are not separate mini-programs. They are grouped slices of
  one large `App` class.
- `native/engine/` owns playback, decode, probing, and FFmpeg-driven media work.
- `native/platform/` owns OS/backend integrations and low-level external APIs.
- `native/render/` owns drawing primitives, layout helpers, and texture/text utilities.
- `native/core/` owns shared types, paths, subprocess launching, parsers, and helpers.

## Secondary / Ancillary Areas

- `companion/companion-module-deckboy/` is the Bitfocus Companion module.
  It is related, but separate from the native app runtime.
- `src/server.js` and `public/` are older Node/web-era code paths and support assets.
  Useful for historical context, but not the main product path now.
- `docs/` contains design briefs, specs, and audit notes.
- `build/` and `bin/` are outputs and launcher/build artifacts, not source of truth.
- `data/` holds default project data, demos, assets, and runtime-adjacent files.

## 60-Second Orientation

If you are brand new, read in this order:

1. `docs/CODE_MAP.md`
2. `native/main.cpp`
3. The relevant `native/app/*.ipp` file for your task
4. Only then drop into `native/engine/`, `native/platform/`, or `native/render/`

The key structural idea is:

- `main.cpp` owns app-wide state
- `app/*.ipp` owns feature slices
- `engine/` owns media behavior
- `platform/` owns external systems
- `render/` owns drawing
- `core/` owns shared glue

## Fast Task Index

Use this when you already know the kind of task and just want the first files.

- UI layout, spacing, labels, panel composition
  - `native/app/app_render_main.ipp`
  - `native/app/app_render_control.ipp`
  - `native/app/app_render_inspector.ipp`
  - `native/app/app_render_settings.ipp`
- Buttons, widgets, inline editors, reusable UI bits
  - `native/app/app_ui_widgets.ipp`
  - `native/app/app_quick_action.ipp`
- Keyboard/mouse behavior, focus, shortcuts, drag interactions
  - `native/app/app_input.ipp`
- Cue add/import/edit/delete flows
  - `native/app/app_cue_mgmt.ipp`
  - `native/main.cpp` for probe/import helpers
- Transport behavior: take, go, pause, stop, seek, trim, loop, hold
  - `native/app/app_cue_transport.ipp`
  - `native/app/app_remote_command.ipp` if remote-triggered
  - `native/engine/media_engine.cpp` if decode/playback timing is involved
- Selection changes, thumbnails, timeline strip, waveform, preview caches
  - `native/app/app_project_state.ipp`
  - `native/app/app_update.ipp`
- Output routing, output windows, stream egress, browser cue runtime
  - `native/app/app_output_mgmt.ipp`
  - `native/platform/output_backend.cpp`
  - `native/engine/media_engine.cpp`
- Companion, OSC, HyperDeck, network listeners, bridge integrations
  - `native/app/app_network.ipp`
  - `native/app/app_remote_command.ipp`
  - `native/platform/network.hpp`
- Live sources: window capture, camera capture, Syphon/Spout
  - `native/platform/capture_backend.cpp`
  - `native/app/app_cue_mgmt.ipp`
  - `native/engine/media_engine.cpp`
- Paths, project files, subprocesses, temp files, ffmpeg/ffprobe launch
  - `native/main.cpp`
  - `native/core/subprocess.cpp`
  - `native/core/paths.cpp`
- Rendering primitives, texture sync, waveform drawing
  - `native/render/primitives.cpp`
  - `native/render/texture_helpers.hpp`
  - `native/render/waveform_renderer.cpp`
- Broadcast / SDK integrations
  - `native/platform/ndi_api.hpp`
  - `native/platform/ndi_trigger_api.hpp`
  - `native/platform/decklink.cpp`
  - `native/platform/ltc_api.hpp`

## Search Seeds

When you do not know the exact file, these search terms are usually good entry points.

- Startup / teardown
  - `bool init(`
  - `void shutdown(`
  - `void run(`
- Per-frame behavior
  - `update(`
  - `processRemoteCommands(`
  - `onSelectionChanged(`
- Cue operations
  - `addBrowserCue`
  - `addSourceCue`
  - `jumpSelectedCue`
  - `takeSelected`
  - `setSelectedTrimIn`
  - `setSelectedTrimOut`
- Preview / analysis
  - `requestThumbnail`
  - `requestTimelineStrip`
  - `triggerWaveformAnalysis`
  - `probeCue`
- Output / routing
  - `rebuildOutputRuntimes`
  - `destroyOutputRuntime`
  - `startBrowserCue`
  - `buildOutputStreamArgs`
- Network / remote control
  - `startCompanionControl`
  - `startHyperDeckServer`
  - `handleRemoteCommand`
  - `enqueueRemoteCommand`
- Security-sensitive surfaces
  - `spawnProcess`
  - `readAllText`
  - `CreateProcessW`
  - `createBoundSocket`
  - `temp_directory_path`

## Startup / Runtime Spine

The runtime path is straightforward once you know where to look:

1. `main()` in `native/main.cpp`
2. `App::init()`
3. `App::run()`
4. main loop:
   - event/input handling
   - remote command processing
   - update/state machines
   - render
5. `App::shutdown()`

Important init responsibilities:

- SDL/window/renderer/font startup
- theme/palette/UI asset preload
- project load + normalize
- deck runtime rebuild
- output runtime rebuild
- startup selection/thumbnail state
- remote/network bridge startup

Important shutdown responsibilities:

- stop network/integration bridges
- destroy deck/output runtimes
- stop thumbnail/timeline threads
- destroy textures/windows/fonts
- close SDL subsystems

## `native/` Folder Map

### `native/main.cpp`

Go here when you need:

- app startup/shutdown flow
- global helper functions used across `App`
- project load/save helpers
- media probe helpers
- subtitle/waveform helpers
- the master list of `App` state members
- the include order for `app/*.ipp`

This file matters even when your change feels "feature-local", because many
shared helpers live above the `App` class and many state members live inside it.

### `native/app/`

This folder is the fastest way into a task. Open the matching slice first.

- `app_project_state.ipp`
  - project state helpers
  - selection changes
  - thumbnail / timeline strip generation
  - waveform cache
  - save/load-adjacent runtime state
  - bundle export
- `app_cue_mgmt.ipp`
  - adding/importing/editing cues
  - project open/save pickers
  - browser/source/pattern cue creation
- `app_cue_transport.ipp`
  - cue transport rules
  - play/pause/stop/take/jump behavior
  - trim, seek, cue-level playback semantics
- `app_update.ipp`
  - per-frame state machine work
  - async future polling
  - animation/runtime progression
  - periodic thumbnail/timeline/waveform requests
- `app_output_mgmt.ipp`
  - output runtime lifecycle
  - window/stream/browser output handling
  - output routing and external egress setup
- `app_network.ipp`
  - Companion/OSC/HyperDeck/network listeners
  - integration bridges
  - status snapshots and network feedback
- `app_remote_command.ipp`
  - text command grammar
  - mapping remote commands into app actions
- `app_input.ipp`
  - keyboard/mouse input
  - event handling
  - hotkeys and gesture routing
- `app_render_main.ipp`
  - main control window drawing
  - top-level UI composition
- `app_render_control.ipp`
  - deck/control workspace drawing
  - major control panels and shell layout
- `app_render_inspector.ipp`
  - cue inspector and settings UI
- `app_render_output.ipp`
  - output-side rendering glue
- `app_render_settings.ipp`
  - settings/preferences rendering
- `app_ui_widgets.ipp`
  - reusable UI widgets and small controls
- `app_overlays.ipp`
  - overlays, lower-third/composite-adjacent presentation helpers
- `app_geometry.ipp`
  - geometry editing and warp/grid logic
- `app_quick_action.ipp`
  - inline editors and quick-edit flows
- `app_accessors.ipp`
  - small state lookup/access helpers
- `app_smoke.ipp`
  - smoke harness and broad regression checks

### `native/engine/`

- `media_engine.*` is the main media runtime.
- Go here for:
  - FFmpeg playback pipelines
  - audio/video decode threads
  - browser capture hookup
  - probing-derived playback behavior
  - frame/audio timing issues

Rule of thumb:

- If the bug is "media looked/played wrong", check `media_engine.*`
- If the bug is "UI requested the wrong media thing", check `app/*.ipp`

### `native/platform/`

Go here for platform-specific or backend-specific behavior:

- `network.hpp`
  - socket helpers and bind/listen primitives
- `capture_backend.*`
  - live source capture plans and OS-specific capture routing
- `output_backend.*`
  - output backend planning
- `dynamic_library.hpp`
  - runtime library loading
- `ndi_api.hpp`, `ndi_trigger_api.hpp`, `ltc_api.hpp`
  - optional broadcast/timecode integrations
- `decklink.*`
  - DeckLink integration
- `siphon_spout.*`
  - Syphon/Spout scaffolding/fallbacks
- `integration_backend.*`
  - integration capability routing

### `native/render/`

Low-level drawing utilities live here:

- `primitives.*`
  - basic shapes and pixel-ish drawing helpers
- `layout.hpp`
  - layout constants/helpers
- `texture_helpers.hpp`
  - texture sync/upload helpers
- `text_renderer.*`
  - text draw helpers
- `waveform_renderer.*`
  - waveform rendering
- `output_renderer.*`
  - output renderer support

### `native/core/`

Cross-cutting helpers:

- `types.hpp`
  - core app/domain structs
- `constants.hpp`
  - shared constants
- `paths.*`
  - data/project/font path resolution
- `subprocess.*`
  - child process launching and capture
- `io_utils.hpp`
  - low-level read helpers
- `utils.*`
  - shared string/collection helpers
- `cue_helpers.hpp`
  - cue/path utility logic
- `subtitle_parser.hpp`
  - subtitle parsing
- `expression_parser.hpp`
  - inline math entry parsing
- `single_instance_guard.hpp`
  - single-instance lock behavior
- `pattern_helpers.hpp`, `pixel_effects.hpp`, `palette.hpp`
  - visual helpers

## Common Task Routing

If the task is...

- "a UI label, button, panel, or inspector looks wrong"
  - start in `app_render_main.ipp`, `app_render_control.ipp`, or `app_render_inspector.ipp`
- "a shortcut, click, drag, or keybind behaves wrong"
  - start in `app_input.ipp`
- "adding/importing/editing cues is wrong"
  - start in `app_cue_mgmt.ipp`
- "take/play/stop/seek/trim logic is wrong"
  - start in `app_cue_transport.ipp`
- "timeline strip, thumbnail, waveform, or project cache is wrong"
  - start in `app_project_state.ipp`
- "the bug only appears every frame / over time / after async work"
  - start in `app_update.ipp`
- "the output window / stream / browser output is wrong"
  - start in `app_output_mgmt.ipp`
- "OSC / Companion / HyperDeck / remote control is wrong"
  - start in `app_network.ipp`, then `app_remote_command.ipp`
- "video/audio decode or playback is wrong"
  - start in `engine/media_engine.*`
- "capture / NDI / DeckLink / LTC / sockets are wrong"
  - start in `platform/`
- "paths, subprocesses, shared structs, or parsing are wrong"
  - start in `core/`

## Important Architectural Surprises

- The app is modular by `#include`-sliced `.ipp` files, not by many separate classes.
- A lot of "shared" logic still lives in `main.cpp`.
- `App` owns a large amount of state directly.
- Linux-first code paths are often more feature-complete than Windows paths.
- FFmpeg/FFprobe are central to many features:
  - media probe
  - playback
  - thumbnails
  - timeline strips
  - waveform extraction
  - stream output
  - browser capture glue
- Outputs are intentionally disarmed on startup/open, so startup behavior is safer
  than the rest of the media stack might suggest.

## Security / Audit Hotspots

If the task is security-related, start here first:

- `native/core/subprocess.*`
- `native/platform/network.hpp`
- `native/app/app_network.ipp`
- `native/app/app_output_mgmt.ipp`
- `native/main.cpp`
  - media probe helpers
  - subtitle extraction
  - waveform extraction

These files cover most of the risky surfaces:

- process launching
- external tool discovery
- sockets/listeners
- browser launch/capture
- temp-file behavior
- media path handling

## Suggested Agent Workflow

For small tasks:

1. read this file
2. open the likely `app/*.ipp` slice
3. open `main.cpp` only if you need shared state or helper context

For medium/large tasks:

1. read this file
2. locate the feature slice in `app/`
3. check its dependencies in `engine/`, `platform/`, or `core/`
4. check `main.cpp` for shared state and startup/shutdown implications
5. only then widen the search

This usually cuts exploration time a lot, because most tasks are local to one
`app/*.ipp` slice plus one supporting module.
