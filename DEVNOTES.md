# DEVNOTES

## Deckboy 0.60 Cleanup + Portability Audit
- Browser cue backend note:
  - `native/platform/browser.*` is now the real owner of browser session
    lifecycle and phased startup state
  - the old Linux Chromium/Xvfb launch logic has been pulled out of
    `native/app/app_output_mgmt.ipp` and behind `BrowserRenderer`
  - current behavior is still an external-browser backend on Linux, but future
    native WebView / owned-renderer work now has an actual swap point
- Windows portable packaging note:
  - Deckboy's current Windows shipping shape is a portable folder, not a lone
    `.exe`
  - `scripts/package_windows_portable.ps1` now assembles
    `dist/windows/Deckboy/` plus `dist/windows/Deckboy-windows-portable.zip`
  - the package includes release DLLs, repo `data/`, and bundled
    `ffmpeg.exe` / `ffprobe.exe` under `tools/ffmpeg/bin`
- Windows icon integration note:
  - final shipped Windows icon assets now live in `art/windows/icons/`
  - `deckboy_app.ico` is now embedded into the Windows executable via
    `native/platform/windows/deckboy.rc.in` configured from `CMakeLists.txt`
  - an earlier sheet-extraction experiment was discarded once the clean final
    icon pack arrived; the current PNG / `.ico` files are the source of truth
  - `deckboy_project.ico` ships as the deferred file-association icon asset;
    registry / installer association wiring is still intentionally out of scope
    for this pass
- Animated pattern compositor note:
  - regenerated software frames now get a fresh `DecodedFrame.index` when they
    are published from `native/engine/media_engine.cpp`
  - reason: output bridge textures in `native/app/app_render_output.ipp` only
    upload when the cue key or frame index changes
  - before this fix, animated pattern frames kept reusing index `0`, so
    motion-enabled engineering patterns and Pocket Test variants could look
    frozen in output windows / PIP compositing even while their pixels were
    changing
  - follow-up: crosshatch and checkerboard motion now use seam-safe phase math
    so they wrap on equivalent board/grid states instead of visibly snapping at
    the loop point
- Windows security hardening note:
  - `native/core/subprocess.cpp` now pins bare `ffmpeg` / `ffprobe` launches
    to trusted absolute paths on Windows before calling `CreateProcessW`
  - supported overrides: `DECKBOY_FFMPEG`, `DECKBOY_FFPROBE`,
    `DECKBOY_FFMPEG_DIR`
  - trusted defaults now include repo-local `tools/` directories, the
    executable directory, and `C:/ffmpeg/bin`
  - Deckboy now refuses bare Windows `ffmpeg` / `ffprobe` launches if they
    cannot be resolved to one of those trusted locations
- Subtitle extraction hardening note:
  - embedded subtitle SRT text is now parsed directly in memory via
    `parseSrtText(...)`
  - this removes the old predictable `deckboy_sub_extract.srt` temp file
- Timeline strip note:
  - the final filmstrip tile now uses a stricter pre-EOF guard in
    `native/app/app_project_state.ipp`
  - reason: some clips appear to report a nominal duration slightly longer than
    the last frame FFmpeg can actually decode, which was surfacing as a black
    last tile on the cue timeline
  - the target sample now stays near the cue end from that safe decode point,
    and retries walk backward from there instead of sampling on or near the
    duration boundary
  - a second bug was also present in the UI publish path: the completed strip
    could be cached with all 5 tiles ready, but the pending upload was being
    cleared before `app_update.ipp` refreshed the SDL texture, so the screen
    kept showing the stale 4-tile texture with a black final tile
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
