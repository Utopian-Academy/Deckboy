# Deckboy vs Mitti Parity Matrix

Date: 2026-03-05
Scope: practical parity planning for Deckboy_0.01 (native SDL2/FFmpeg app)
Sources: `MANUAL.md` (Deckboy), Mitti docs (Getting Started, Cues, Outputs, External Controls)

## Baseline Summary (Deckboy today)

Deckboy currently ships the core live workflow: media import, cue lists, deck runtimes, output targets, layer assignments, live take/go/stop/clear, master cues, output arming/recovery, and triple-ESC panic disarm. It supports multi-deck compositing to outputs, per-output NDI/streaming, OSC + Companion command control, timecode run/chase/trigger/jam/freewheel, patterns/browser/lower-third/source cues, and backward-compatible `.playboy` show file loading.

## Parity Table

| Category | Mitti behavior (reference) | Deckboy today | Implementation notes / file area |
|---|---|---|---|
| UI / Playlist | Playlist-first workflow with clear live/next/preview state | Partial | Keep deck-column UI; continue clarity work in `native/main.cpp` deck/monitor render blocks. |
| UI / Playlist | Playlist Preferences (FPS/timebase, defaults for new cues, still duration) | Partial+ | Deck-level playlist preferences implemented in System settings (timebase/start/fade/still + default toggles) with persistence + new-cue apply. Remaining gap: full dedicated dialog UX and show-level/global preset options. |
| Cues | Cue ID search / jump by ID | Yes (operator ID) | Added short operator cue IDs (`cueId`), typed ID search buffer, and token lookup by `cueId`/number/name in `native/main.cpp`. |
| Cues | Color tags + notes | Yes (extended) | Already implemented (`Cue.colorTag`, `Cue.notes`) with UI + persistence. Extend to multi-edit path. |
| Cues | Multi-select cue edit | Partial+ | Multi-select inspector now shows shared/common controls with mixed-value display and applies quick edits across selection for playback/geometry/key/metadata fields. Remaining gap: richer per-field “set absolute” dialogs for every numeric control. |
| Cues | Pause at beginning | Yes | Added `Cue.pauseAtBeginning` and take path integration (`takeSelected`) with load-without-autoplay behavior. |
| Cues | Pause at end | Yes | Already implemented (`pauseOnLastFrame`, endAction). |
| Cues | Per-cue audio enable/disable | Yes | Added `Cue.audioEnabled`; decode path now honors per-cue audio enable and persists/commands/UI toggle exist. |
| Cues | Per-cue transition-to-next toggle | Yes | Added `Cue.transitionToNext`; auto-advance/take path now respects cue transition toggle. |
| Cues | Per-cue goto target on end | Yes | Added `Cue.gotoTarget` token; end-of-cue path resolves via cue token lookup and jumps accordingly. |
| Playback | JUMP trigger vs load | Yes | Already implemented (`Project.jumpMode`, `jumpTransitionEnabled`). |
| Playback | Panic/Kill profile options | Yes | Already implemented (`panic_profile`, fade_pause/rewind/load_next + outputs_off). |
| Playback | Main playlist fader + auto fade | Yes (deck-level) | Added per-deck playlist opacity + optional auto-fade with fade-time target animation into compositing path. |
| Outputs / Rendering | Multiple outputs with layer stacking | Yes | Implemented via `OutputTarget` + `LayerAssignment`. |
| Outputs / Rendering | NDI per output | Yes | Implemented at `OutputTarget` level + migration from legacy deck NDI. |
| Outputs / Rendering | Stream output (SRT/RTMP) | Yes | Implemented per output. |
| Outputs / Rendering | Canvas + view panning / span semantics | Partial+ | Added explicit per-output `span` vs `duplicate` mode in Video Outputs + command surface; still uses current canvas/view architecture under the hood. |
| Outputs / Rendering | Edge blend + warp/corner pin | Partial+ | Warp/blend implemented with explicit deck warp modes (`linear` and `perspective`) plus mode command/UI controls; advanced Mitti-style warp tooling still incomplete. |
| Outputs / Rendering | Per-output test card toggle | Yes | Added `outputTestCardEnabled` with focused/all-output UI toggles and command support. |
| Outputs / Rendering | Per-output orientation 0/90/180/270 | Yes | Added per-output orientation model + window present and egress transform. |
| Outputs / Rendering | Blackmagic output | Partial (scaffold) | Backend stubs exist in `native/platform/decklink.*`; integrate behind feature flag + output backend abstraction usage. |
| Outputs / Rendering | Syphon/Spout output transport | Partial (scaffold) | Cue/type + sender stubs exist (`native/platform/siphon_spout.*`); runtime output backend path still pending. |
| Outputs / Rendering | Capture backend abstraction (window/camera/app-texture) | Partial+ | Source cue FFmpeg argument planning now delegates to `native/platform/capture_backend.*` via `planSourceCapture(...)`; Linux backends are live, macOS/Windows remain scaffolded. |
| Outputs / Rendering | Output backend route planning abstraction | Partial+ | `native/platform/output_backend.*` now exposes `planOutputBackendRoute(...)` and runtime egress dispatch now gates stream/NDI sends via route support. Full backend execution extraction remains incremental. |
| Integrations / Remote | OSC command control + feedback | Yes (extended) | Added optional canonical OSC feedback mirror mode with configurable rate limit (`oscFeedbackRateMs`) while preserving `/playboy/state` JSON feedback. |
| Integrations / Remote | OSC Query server | Yes (baseline) | Optional HTTP OSC Query server now exposes endpoint docs + live state (`/oscquery.json`, `/state.json`) and lightweight browser page. |
| Integrations / Remote | Stream Deck direct story | Yes (Companion profile) | Published official Companion/Stream Deck mapping bundle in `docs/streamdeck/` (JSON manifest + CSV + setup notes), no proprietary plugin required. |
| Integrations / Remote | Trigger from ATEM | Partial (scaffold) | Integration backend planner + project/settings/command/OSC surface added (`native/platform/integration_backend.*`, Network tab). Runtime ATEM bridge still pending. |
| Integrations / Remote | Trigger by NDI metadata | Partial (scaffold) | Integration adapter toggle + route planning present; runtime metadata listener backend still pending. |
| Integrations / Remote | NMC transport sync | Partial (scaffold) | Integration adapter toggle + route planning present; runtime sync backend still pending. |
| Integrations / Remote | MTC/LTC ingest | Partial (scaffold) | Timecode chase/run exists and integration adapter toggles/routes now persist; dedicated ingest decoders/backends still pending. |
| Integrations / Remote | DMX / Art-Net | Partial (scaffold) | Integration adapter toggle + configurable Art-Net port now persist; runtime DMX/Art-Net trigger bridge still pending. |
| Integrations / Remote | MIDI cross-platform parity | Partial | ALSA-focused implementation exists; abstract MIDI backends for macOS/Windows parity. |
| Timecode | Chase / run / jam / freewheel / cue trigger | Yes | Implemented and documented in manual/commands. |
| Show Files | Backward-compatible load/save and unknown field tolerance | Yes (for current schema) | Continue append-only schema evolution in `saveProject/loadProject` with defaults and no destructive migrations. |
| Safety | Output arming, fullscreen recovery, panic disarm | Yes | Must remain unchanged during parity work (`handleKeyDown`, output recovery paths). |

## Portability Architecture Targets

- Keep Linux backends functional while introducing interface-first backends for:
  - `WindowCaptureBackend` (Linux X11/PipeWire, Windows DXGI, macOS ScreenCaptureKit)
  - `CameraBackend` (Linux v4l2, Windows MF, macOS AVFoundation)
  - `OutputBackend` (SDL window, stream encoder, NDI, DeckLink)
- Keep stubs compiling cross-platform (`native/platform/*`) and gate platform-specific implementations behind compile options.

## Immediate Implementation Order

1. Continue backend extraction from planning APIs into runtime backend execution wrappers (especially output egress path) for Linux/macOS/Windows parity.
2. Implement runtime adapter backends behind integration planner toggles (ATEM, NDI metadata trigger, NMC, MTC/LTC ingest, DMX/Art-Net).
