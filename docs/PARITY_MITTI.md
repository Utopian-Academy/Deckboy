# Deckboy vs Mitti Parity Matrix

Date: 2026-03-12
Scope: practical parity planning for Deckboy_0.01 (native SDL2/FFmpeg app)
Sources: `MANUAL.md` (Deckboy), Mitti docs (Getting Started, Cue Settings, Video Output, Audio Output, External Controls)

## Baseline Summary (Deckboy today)

Deckboy currently ships the core live workflow: media import, cue lists, deck runtimes, output targets, layer assignments, live take/go/stop/clear, master cues, output arming/recovery, and triple-ESC panic disarm. It supports per-output NDI/streaming, OSC + Companion command control, HyperDeck emulation, timecode run/chase/trigger/jam/freewheel, patterns/browser/lower-third/source cues, undo/redo, a keyboard shortcuts overlay, theme selection, interactive warp editing with presets/snap, bundled show export, and backward-compatible `.deckboy` show file loading.

## Parity Table

| Category | Mitti behavior (reference) | Deckboy today | Implementation notes / file area |
|---|---|---|---|
| UI / Playlist | Playlist-first workflow with clear live/next/preview state | Partial | Keep deck-column UI; continue clarity work in `native/main.cpp` deck/monitor render blocks. |
| UI / Playlist | Playlist Preferences (FPS/timebase, defaults for new cues, still duration) | Partial+ | Deck-level playlist preferences implemented in System settings (timebase/start/fade/still + default toggles) with persistence + new-cue apply. Remaining gap: full dedicated dialog UX and show-level/global preset options. |
| Cues | Cue ID search / jump by ID | Yes (operator ID) | Added short operator cue IDs (`cueId`), typed ID search buffer, and token lookup by `cueId`/number/name in `native/main.cpp`. |
| Cues | Color tags + notes | Yes (extended) | Already implemented (`Cue.colorTag`, `Cue.notes`) with UI + persistence. Extend to multi-edit path. |
| Cues | Multi-select cue edit | Partial+ | Multi-select now includes range/toggle selection, shared/mixed inspector rendering, and batch mutation helpers across metadata, playback, geometry, keying, and goto/default-toggle edits. Remaining gap: richer dedicated set-value dialogs for every numeric field. |
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
| Outputs / Rendering | Edge blend + warp/corner pin | Partial+ | Deck-level edge blend is live, and the program monitor now has a visible warp editor with drag corners, `Shift` snap-to-grid, linear/perspective warp modes, reset, and preset save/recall. Remaining gap: deeper operator tooling such as richer preset management and more advanced mesh workflows. |
| Outputs / Rendering | Per-output test card toggle | Yes | Added `outputTestCardEnabled` with focused/all-output UI toggles and command support. |
| Outputs / Rendering | Per-output orientation 0/90/180/270 | Yes | Added per-output orientation model + window present and egress transform. |
| Outputs / Rendering | Blackmagic output | Partial (scaffold) | Backend stubs exist in `native/platform/decklink.*`; integrate behind feature flag + output backend abstraction usage. |
| Outputs / Rendering | Syphon/Spout output transport | Partial (scaffold) | Cue/type + sender stubs exist (`native/platform/siphon_spout.*`); runtime output backend path still pending. |
| Outputs / Rendering | Capture backend abstraction (window/camera/app-texture) | Partial+ | Source cue FFmpeg argument planning now delegates to `native/platform/capture_backend.*` via `planSourceCapture(...)`; Linux backends are live, macOS/Windows remain scaffolded. |
| Outputs / Rendering | Output backend route planning abstraction | Partial+ | `native/platform/output_backend.*` now exposes `planOutputBackendRoute(...)` and runtime egress dispatch now gates stream/NDI sends via route support. Full backend execution extraction remains incremental. |
| Integrations / Remote | OSC command control + feedback | Yes (extended) | Added optional canonical OSC feedback mirror mode with configurable rate limit (`oscFeedbackRateMs`) while preserving `/deckboy/state` JSON feedback. |
| Integrations / Remote | OSC Query server | Yes (baseline) | Optional HTTP OSC Query server now exposes endpoint docs + live state (`/oscquery.json`, `/state.json`) and lightweight browser page. |
| Integrations / Remote | Stream Deck direct story | Yes (Companion profile) | Published official Companion/Stream Deck mapping bundle in `docs/streamdeck/` (JSON manifest + CSV + setup notes), no proprietary plugin required. |
| Integrations / Remote | HyperDeck emulation | Yes (baseline) | Built-in HyperDeck TCP server listens on port `9992` (override via `DECKBOY_HYPERDECK_PORT`) and maps the core play/stop/goto/status workflow into the existing remote-command path. |
| Integrations / Remote | Trigger from ATEM | Partial+ | Integration backend planner + settings/command/OSC surface plus live UDP ATEM trigger bridge runtime (`ATEMEVENT` ingress) in `native/main.cpp`. |
| Integrations / Remote | Trigger by NDI metadata | Yes (runtime) | Linux/macOS builds now runtime-load `libndi`, discover an NDI source, receive metadata frames, and map raw/XML payloads into the existing remote-command path. Current source selection is env-filtered (`DECKBOY_NDI_TRIGGER_SOURCE`) rather than UI-driven. |
| Integrations / Remote | NMC transport sync | Partial+ | Linux/macOS builds now ship a live UDP transport/locate backend with Mitti-style input/output mode behavior behind the existing toggle. Remaining gap: third-party interop validation and UI-driven target/source config. |
| Integrations / Remote | MTC/LTC ingest | Partial+ | ALSA MIDI quarter-frame (`SND_SEQ_EVENT_QFRAME`) decodes to the live ingest path, and Linux/macOS builds now runtime-load `libltc` and decode LTC from the default SDL capture input (`LTCEXT` -> chase decks). Remaining gap: capture-device selection UX and Windows parity. |
| Integrations / Remote | DMX / Art-Net | Partial+ | Integration adapter now includes live Art-Net `ArtDMX` trigger bridge runtime with default channel mapping; advanced patching/profile tooling remains pending. |
| Integrations / Remote | MIDI cross-platform parity | Partial | ALSA-focused implementation exists; abstract MIDI backends for macOS/Windows parity. |
| Timecode | Chase / run / jam / freewheel / cue trigger | Yes | Implemented and documented in manual/commands. |
| Show Files | Backward-compatible load/save and unknown field tolerance | Yes (for current schema) | Continue append-only schema evolution in `saveProject/loadProject` with defaults and no destructive migrations. |
| Safety | Output arming, fullscreen recovery, panic disarm | Yes | Must remain unchanged during parity work (`handleKeyDown`, output recovery paths). |

## Remaining High-Value Gaps From Current Mitti Docs

- Multichannel audio output/channel routing is still behind Mitti's audio-output feature set; Deckboy remains stereo-program oriented today.
- Subtitle / closed-caption style output paths are not implemented yet.

## Portability Architecture Targets

- Keep Linux backends functional while introducing interface-first backends for:
  - `WindowCaptureBackend` (Linux X11/PipeWire, Windows DXGI, macOS ScreenCaptureKit)
  - `CameraBackend` (Linux v4l2, Windows MF, macOS AVFoundation)
  - `OutputBackend` (SDL window, stream encoder, NDI, DeckLink)
- Keep stubs compiling cross-platform (`native/platform/*`) and gate platform-specific implementations behind compile options.

## Immediate Implementation Order

1. Continue turning the planning APIs into real cross-platform runtime backends, especially subprocess, capture, and output-egress paths that are still Linux-first.
2. After the portability floor is stable, target the highest-value remaining Mitti feature gaps: multichannel audio routing and subtitle/CC output.
