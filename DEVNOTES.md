# DEVNOTES

## Layout Component Map (native)
- Main control layout entry: `native/main.cpp` -> `renderControlWindow()`.
- Global header + output strip: `renderControlWindow()` (header block and `OUTPUT ARM` chip strip).
- Deck cue column + rows: `renderPlaylistColumn()` and `renderCueRow()`.
- Program monitor + stack view: `renderMainPanel()` (`program monitor` and `STACK VIEW (Output X)`).
- Cue settings panel (group headers + rows): `renderMainPanel()` (`drawSectionHeader`, `drawQuickRow`, `drawCueRoutingRows`).
- Master Cue / scene sidebar: `renderDeckSidebar()`.
- Settings modal + Video Outputs routing table: `renderSettingsModal()` (`settingsTab_ == 3`).
- Splash overlay and startup dialog: `renderSplashOverlay()` and `renderStartupDialog()`.

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
