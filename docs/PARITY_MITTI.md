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
| Cues | Multi-select cue edit | Partial | Shift-range/Ctrl-toggle multi-select and batch apply for key cue edits (notes/id/loop/hold/fade/tag + new parity toggles). Remaining inspector rows still single-cue in some areas. |
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
| Outputs / Rendering | Canvas + view panning / span semantics | Partial | Canvas exists; formal duplicate/span UX mode labeling can be improved in settings. |
| Outputs / Rendering | Edge blend + warp/corner pin | Partial | Warp/blend implemented; explicit perspective vs linear mode not yet split. |
| Outputs / Rendering | Per-output test card toggle | Partial | Pattern cues exist; add output-scoped test card toggle for parity convenience. |
| Outputs / Rendering | Per-output orientation 0/90/180/270 | Missing | Add output orientation field and final present transform. |
| Outputs / Rendering | Blackmagic output | Partial (scaffold) | Backend stubs exist in `native/platform/decklink.*`; integrate behind feature flag + output backend abstraction usage. |
| Outputs / Rendering | Syphon/Spout output transport | Partial (scaffold) | Cue/type + sender stubs exist (`native/platform/siphon_spout.*`); runtime output backend path still pending. |
| Integrations / Remote | OSC command control + feedback | Partial+ | Command surface + `/playboy/state` JSON feedback present; add canonical OSC mirror mode + stronger rate controls. |
| Integrations / Remote | OSC Query server | Missing | Add optional HTTP OSC Query endpoint exposing command tree + live values. |
| Integrations / Remote | Stream Deck direct story | Partial | Companion works; add official Companion preset / Stream Deck profile docs package. |
| Integrations / Remote | Trigger from ATEM | Missing | Add command ingress adapter (ATEM tally/trigger bridge) as optional module. |
| Integrations / Remote | Trigger by NDI metadata | Missing | Add optional NDI metadata/trigger listener backend with command mapping. |
| Integrations / Remote | NMC transport sync | Missing | Add NMC ingest adapter and mapping to deck transport. |
| Integrations / Remote | MTC/LTC ingest | Partial | Timecode chase/run exists; explicit MTC/LTC ingest backend parity still incomplete. |
| Integrations / Remote | DMX / Art-Net | Missing | Add optional control adapter backend; keep core app decoupled. |
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

1. Finish remaining multi-select inspector parity (common-control masking in right panel for all fields).
2. Add playlist preferences model/UI (fps/timebase/default cue values/start offset).
3. Implement OSC Query server + optional OSC feedback mirror mode.
4. Output parity UX follow-through (orientation/test-card/span semantics + warp-mode split).
5. Backend interface extraction for capture/output parity across Linux/macOS/Windows.
