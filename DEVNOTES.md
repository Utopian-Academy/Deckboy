# DEVNOTES

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
- Cue inspector grouping in `renderMainPanel()` now uses local section helpers:
  - `beginInspectorSection(...)`
  - `finishInspectorSection(...)`
- These wrap existing cue-control logic in boxed section cards without changing
  playback or cue data behavior.
- Shared metadata/action row helpers also live in the same `renderMainPanel()`
  scope:
  - `drawInspectorMessageRow(...)`
  - `drawInspectorActionRow(...)`
  - `drawInspectorEditableRow(...)`
  - `drawInspectorStatusRow(...)`
  - `drawCueTagRow(...)`
  - `drawPausePointsRow(...)`
- If you add a new inspector group, follow the same pattern:
  1. begin section
  2. render rows
  3. finish section with final body Y.
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
  - `.playboy`, `PLAYBOY_*`, `/playboy/*`, and `playboy-native`

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
  - tab-delimited, same escape rules as `.playboy`
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
  - `startArtNetBridgeListener()` / `artNetBridgeLoop()`
  - bridge lifecycle wrappers: `startIntegrationBridges()` / `stopIntegrationBridges()`.
- MTC ingest runtime is decoded in `midiLoop()`:
  - `SND_SEQ_EVENT_QFRAME` -> `decodeMidiMtcQuarterFrame(...)`
  - internal command ingress `MTCEXT <seconds> <fps>`
  - applied via `ingestIntegrationTimecode(...)`.
- Art-Net runtime command mapping is centralized in `handleArtNetEvent(...)`
  (ch1-10 mapping to transport/master cue commands).
