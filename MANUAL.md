# Deckboy_0.01 — User Manual

> dot-matrix cue deck · model db-001 · v0.01

---

## Contents

1. [Overview](#1-overview)
2. [Running the App](#2-running-the-app)
3. [Startup Dialog](#3-startup-dialog)
4. [Interface Layout](#4-interface-layout)
5. [Cue Types](#5-cue-types)
6. [Importing Media](#6-importing-media)
7. [Cue Settings](#7-cue-settings)
8. [Transport Controls](#8-transport-controls)
9. [Playlists & Auto-Advance](#9-playlists--auto-advance)
10. [Multi-Deck Operation](#10-multi-deck-operation)
11. [Overlay Compositor](#11-overlay-compositor)
12. [Test Patterns](#12-test-patterns)
13. [Browser Cues](#13-browser-cues)
14. [Timecode & Chase](#14-timecode--chase)
15. [NDI Output](#15-ndi-output)
16. [Audio](#16-audio)
17. [Show Files](#17-show-files)
18. [Companion Control](#18-companion-control)
19. [OSC Input](#19-osc-input)
20. [Keyboard Reference](#20-keyboard-reference)
21. [Companion Command Reference](#21-companion-command-reference)

---

## 1. Overview

Deckboy_0.01 is a native SDL desktop cue deck for live events. Current builds
are Linux-first, with cross-platform parity work in progress for macOS and
Windows. It uses SDL2 for the UI and FFmpeg for media decode. Deck runtimes own playlist/audio/transport,
and output runtimes are separate entities with their own windows/compositor.
Decks are assigned to outputs via layer assignments, so multiple decks can
stack on one output and one deck can be assigned to multiple outputs. Outputs
can be `window` or `stream` targets; stream outputs can also mirror another
output feed.

The UI is styled with a Game Boy–inspired look: monochrome green palette,
chunky framing, and a "cartridge shelf" vocabulary.

---

## 2. Running the App

```bash
cd /home/user/playboy
./bin/playboy          # builds with CMake, then runs
```

Useful flags:

```bash
./build/native/playboy-native --self-check   # verify dependencies
./build/native/playboy-native --smoke        # automated smoke test
./build/native/playboy-native --allow-multi-instance  # bypass safety lock (debug only)
./scripts/generate_demo_shows.sh  # generate demo .playboy show files
PLAYBOY_COMPANION_PORT=5610 ./bin/playboy    # custom Companion port
PLAYBOY_PROJECT=/path/to/show.playboy ./bin/playboy  # open specific show
DECKBOY_UI_PROFILE=1 ./bin/playboy  # UI timing + popup watchdog logs
```

`--self-check` now includes backend wiring diagnostics, including:
- `capture-backends` and `capture-plan-defaults`
- `output-backends` and `output-route-defaults`

By default, Deckboy now enforces a single-instance launch lock to prevent
runaway duplicate app spawns.

Generated demo shows are written to `data/demos/` (for example
`demo_70_30_4pip_bg_5deck.playboy`).

---

## 3. Startup Dialog

Deckboy now opens with a short splash overlay first (`DECKBOY`, boot lines, and
`press ENTER to start`). The splash can be skipped with `Enter`, `Esc`, or a
mouse click.

On every launch a dialog appears in front of the interface with two choices:

| Button | Key | Effect |
|--------|-----|--------|
| **LOAD SHOW** | `Enter` | Keep the last-saved show file and continue |
| **NEW SHOW** | `N` | Clear the project and start with one empty deck |

Press `Esc` to dismiss and keep the loaded show (same as LOAD SHOW).

The dialog shows the filename of the saved show so you know exactly what will
be loaded.

---

## 4. Interface Layout

```
┌─────────────────────────────────────────────────────────────────────┐
│  Global header — title · status bits · companion port · file name   │
├───────────────┬─────────────────────────────────────────────────────┤
│  Deck column  │  Program monitor         │  Cue settings panel      │
│  (cue list)   │  (live preview)          │  (selected cue)          │
│               │                          │                          │
│  Cue 1        │  ┌──────────────────┐    │  Thumbnail               │
│  Cue 2 ◀LIVE  │  │  live frame      │    │  ──────────────────────  │
│  Cue 3 ●SEL   │  └──────────────────┘    │  vol  [ - ]  85%  [ + ] │
│  ...          │                          │  fade in / fade out      │
│               │                          │  in / out points         │
│  footer:      │  ─── progress bar ───    │  transition override     │
│  routing info │  status · timecode       │  loop / hold / end act.  │
│               │                          │                          │
│               │                          │  (text prompts when      │
│               │                          │   no cue is selected)    │
├───────────────┴──────────────────────────┴──────────────────────────┤
│  MEDIA (Import/Source/Pattern) | TRANSPORT (A/B/Start) | OUTPUT (Clear/Output) │
└─────────────────────────────────────────────────────────────────────┘
```

The main control window is now program/output-focused and no longer carries the
deck playlist column. Deck playlists are in the separate **Deckboy Decks**
window for multi-deck operation (click cue rows to select; per-deck `Take`
button to fire selection). The same window also includes a dedicated
multi-deck line view: one row per master cue preset with inline per-deck slot
details (deck, cue number, cue name, or bypass) and direct in-row `Take`.

Main-window output strip quick controls (always visible):
- Top row: output chips (`O1`, `O2`, ...) with target/state labels plus per-output arm switch and `Add Output`.
- Bottom row: focused-route controls (`Link/Unlink`, `Layer-`, `Layer+`) and plain-English status:
  - `Focused Route: Deck N -> Output N  Background/Layer N/Not Linked`
- Main header includes `decks` toggle for the separate deck panel.
  - When a second deck is created, Deckboy auto-opens and raises the deck panel.
- Header also includes compact per-deck live summary (`D1 LIVE ...`) for quick
  cross-deck awareness.
- Program monitor includes `STACK VIEW (Output X)` so deck/layer occupancy for
  the focused output is visible at a glance.
- Bottom control strip is grouped for scanability:
  - `MEDIA`, `TRANSPORT`, `OUTPUT`
  - includes default selectors for `Source` and `Pattern` cue type.

**Hover tips**: hover over any button, cue row, or progress bar to see a
contextual tip.

---

## 5. Cue Types

| Type | Description |
|------|-------------|
| **Video** | Any FFmpeg-readable video file. Audio is decoded alongside. |
| **Image** | Still image (JPEG, PNG, etc.) held until taken off or auto-advanced. |
| **Pattern** | Procedurally generated test pattern — no file required. |
| **Browser** | Web URL rendered via Xvfb + ffmpeg x11grab into the output window. |
| **Window Source** | Live window/screen capture cue (`source://window/...`) using ffmpeg `x11grab` on Linux. |
| **Camera** | Live camera cue (`source://camera/...`) using ffmpeg `v4l2` on Linux. |
| **Syphon/Spout** | Inter-app source cue path (`source://syphon/...`); Linux currently uses desktop capture fallback while native Syphon/Spout backends remain roadmap work. |
| **LowerThird** | Graphic overlay pushed into the 4-slot overlay stack. |

---

## 6. Importing Media

**Drag and drop** files directly onto the control window shell — they are
probed with ffprobe and added to the focused deck's playlist.

**Keyboard import**: press `Shift+I` to open the native file picker. Multiple files
can be selected.

**Browser cue**: press `B`, enter a URL in the dialog. The cue is added to the
playlist with a "web" type.

**Source cue**:
- Use the bottom-bar `Source` selector to choose default source kind (`Window`, `Camera`, `Syphon/Spout`).
- Click `SOURCE` to add immediately with default refs:
  - `window -> active-window`
  - `camera -> default-camera`
  - `syphon/spout -> default-bus`
- For an existing source cue, use the cue panel `source` row and click `edit` to set the source reference directly in-menu (non-blocking inline editor).
- Or use commands (`SOURCE WINDOW ...`, `SOURCE CAMERA ...`, `SOURCE SYPHON ...`).
- Linux source-ref quick examples:
  - `SOURCE WINDOW active-window` (full desktop capture)
  - `SOURCE WINDOW :0.0+100,100` (explicit X11 display+offset spec)
  - `SOURCE WINDOW id:0x3e00007` (capture one X11 window id)
  - `SOURCE CAMERA default-camera` (maps to `/dev/video0`)
  - `SOURCE CAMERA 1` (maps to `/dev/video1`)
  - `SOURCE SYPHON default-bus` (desktop fallback on Linux)

**Pattern cue**:
- Use the bottom-bar `Pattern` selector to choose the default pattern type.
- Click `PATTERN` (or press `P`) to add that default pattern immediately.
- Use Companion `PATTERN <type>` for direct type add.
- For an existing pattern cue, use the cue-settings dropdown:
  - `Pattern Type: ... v` selects type
  - `motion` row toggles motion on/off (for supported types).

**Lower-third / graphic**: press `G` to add a blank lower-third overlay cue.
Set the text via the `LOWERTEXT` and `LOWERSUB` Companion commands, or edit
`lowertext` directly in the show file.

---

## 7. Cue Settings

Select a cue (click or `Up`/`Down` arrows) to see its settings in the right panel.
If the settings list is longer than the panel, use the mouse wheel over the
settings area to scroll.
You can multi-select cues with `Shift+click` (range) and `Ctrl/Cmd+click`
(toggle). Supported inspector actions apply to all selected cues.
When multiple cues are selected, the inspector enters a common-controls view:
- shared controls remain editable
- incompatible control groups are hidden/disabled
- conflicting values are shown as `mixed`.

Cue settings are grouped into collapsible blocks:
- `Playback`
- `Geometry`
- `Key`
- `Routing`

For source cues (`Window Source`, `Camera`, `Syphon/Spout`), the playback area includes:
- `source` value + `edit` button for changing the source reference without leaving the menu.

`Routing` in cue settings edits the focused deck route inline (`Output`,
`Layer`, `Assign/Unassign`) without opening separate route manager popups.

### Video cues

| Control | Keys | Description |
|---------|------|-------------|
| Volume | `+ / -` | Output gain, 0–100 % |
| Fade In | `[ / ]` | Automatic fade-in duration (±0.25 s per press) |
| Fade Out | `Shift+[ / Shift+]` | Automatic fade-out duration |
| In-point | `−/+` buttons in panel | Where playback begins |
| Out-point | `−/+` buttons in panel | Where playback ends (0 = end of file) |
| Transition | `−/+` + style dropdown | Per-cue transition duration override and style (`cut/crossfade/dip`) |
| Loop | `L` | Loop the cue indefinitely |
| Hold on last frame | `E` | Freeze on the last frame instead of stopping |
| Pause at beginning | cue panel toggle / `PAUSEBEGIN` | Load first frame when taken (do not autoplay) |
| Pause at end | cue panel hold toggle / `PAUSEEND` | Pause on last frame at end of cue |
| Cue audio | cue panel toggle / `CUEAUDIO` | Enable/disable cue audio track without removing media |
| Transition to next | cue panel toggle / `NEXTTRANS` | Use transition timing when advancing to the next cue |
| Goto target | cue panel `goto` button / `CUEGOTO` | On cue end, jump to cue token (ID/number/name token) instead of next |
| End action | `X` | Cycle: inherit → stop → loop → hold → auto-next |

### Output geometry, keying, and color controls (video / image / pattern / browser)

| Control | Description |
|---------|-------------|
| Scale Mode | Fit/Fill/Stretch/Unscaled — how to scale to canvas: Fit maintains aspect ratio with letterbox, Fill maintains ratio but crops edges, Stretch ignores aspect ratio to fill completely, Unscaled is 1:1 pixel mapping |
| Scale X / Y | Per-cue independent horizontal and vertical scaling (0.25x to 4.0x each, applied after scale mode) |
| Offset X / Y | Per-cue output position offset in pixels |
| Rotation | Per-cue rotation angle (-180 to +180 degrees) |
| Crop L / R / T / B | Per-edge crop percentage |
| Key on/off | Enable/disable per-cue chroma key |
| Key color | Pick chroma key target color (`#RRGGBB`) |
| Key tolerance | RGB distance threshold for removal |
| Key softness | Feather width around the threshold |
| Brightness | Per-cue brightness gain (0.0 to 2.0) |
| Contrast | Per-cue contrast gain (0.0 to 2.0) |
| Saturation | Per-cue saturation gain (0.0 to 2.0) |
| Hue | Per-cue hue rotation (-180 to +180 degrees) |

Precision input notes:
- `off X` / `off Y` quick buttons now nudge in `1px` steps.
- Click the value cell for `scale X`, `scale Y`, `off X`, `off Y`, or `rot` to type an exact number.
- Numeric input supports simple expressions (`+`, `-`, `*`, `/`, and parentheses).

### Output canvas and edge treatment (deck output)

| Control | Description |
|---------|-------------|
| Canvas span | Project-level compositor size (for example `5760x2160`) |
| View X / Y | Focused deck viewport offset inside the canvas |
| Output layout | Per-output canvas view mode: `span` (uses view offset) or `duplicate` (locks to `0,0`) |
| Output orientation | Per-output final output rotation (`0`, `90`, `180`, `270`) |
| Output test card | Per-output test signal feed toggle (`on`/`off`) |
| Warp | Deck-level 4-corner output warp (`TL/TR/BR/BL` offsets) with mode `linear` (default) or `perspective` |
| Edge blend L/R/T/B | Deck-level per-edge blend softening (0-49%) |
| Output route / layer | Route playlist to an output host deck and set layer order |

Use **Preferences -> Video** for output focus/create/assign, stream controls,
and canvas/view/warp controls, or Companion commands (`VIDEO OUTPUT ...`,
`VIDEO STREAM ...`, `VIDEO CANVAS`, `VIDEO VIEW`, `VIDEO WARP`, `VIDEO BLEND`,
`VIDEO OUTPUT LAYOUT`, `VIDEO OUTPUT ORIENTATION`, `VIDEO OUTPUT TESTCARD`)
for precise values.

### Still / pattern / browser cues

| Control | Description |
|---------|-------------|
| Duration | Seconds to display before auto-advancing. 0 = hold until taken. |

### Lower-third cues

| Control | Description |
|---------|-------------|
| Main text | Set via Companion `LOWERTEXT <text>` |
| Sub text | Set via Companion `LOWERSUB <text>` |
| BG Alpha | Background band opacity (0–255) |
| Duration | 0 = hold until taken |

---

## 8. Transport Controls

| Action | Key | Button |
|--------|-----|--------|
| Take selected cue live | `Enter` | **Take** |
| Play / Pause active cue | `Space` | **Go/Pause** |
| Stop | `S` | **Stop** |
| Cut to black | `C` | **Clear** |
| Toggle fullscreen output | `F` | **Fullscreen** |
| Emergency exit fullscreen takeover | `Esc` | exits fullscreen output safely and pauses auto-recovery for that output (so it stays windowed until re-armed) |
| Panic output disarm | `Esc` x3 (quickly, ~0.9s gaps) | disarms all outputs when output safety context is active |
| Seek | Click/drag progress bar | — |
| Volume up / down | `+ / -` | — |

**GO command** (Companion `GO`): if nothing is playing, takes the selected cue;
if playing, pauses; if paused, resumes.

---

## 9. Playlists & Auto-Advance

| Setting | Key | Description |
|---------|-----|-------------|
| Auto-advance | `3` | When active cue ends, advance and take next cue automatically |
| Playlist loop | `4` | After the last cue, wrap back to cue 1 |
| Shuffle | Companion `SHUFFLE ON` | Randomise playback order |
| Reorder cue | `Shift+Up / Shift+Down` | Move selected cue in the list |
| Playlist timebase | `Prefs -> System -> Playlist Prefs` | Select deck playlist SMPTE base (`24`, `25`, `29.97`, `30`) |
| Playlist start TC | `Prefs -> System -> Playlist Prefs` | Set per-deck start offset (`hh:mm:ss:ff` or seconds) |
| Default cue fade | `Prefs -> System -> Playlist Prefs` | Default fade duration for newly imported/added cues |
| Default non-movie duration | `Prefs -> System -> Playlist Prefs` | Default duration for new image/pattern/browser/lower-third cues (`0` = hold) |
| New-cue toggles | `Prefs -> System -> Playlist Prefs` | Default loop, fade-in/out on/off, audio on/off, pause-begin/end, transition-to-next |

Playlist preferences are per-deck. New cues added by import or cue-creation
tools inherit the focused deck's playlist defaults.

---

## 10. Multi-Deck Operation

Each deck has its own:
- Playlist and cue selection
- Audio device
- Transport state (play/pause/stop)
- Timecode clock

Each output has its own:
- Window and compositor runtime
- Display assignment
- Host deck (for output-level view/warp/blend state)
- Layer stack membership from `LayerAssignment`
- Output target type (`window` or `stream`)
- Optional mirror source output index (stream outputs)
- Optional NDI fill/key senders
- Layout mode (`span`/`duplicate`)
- Orientation (`0/90/180/270`)
- Test card state (`on`/`off`)

Entity model (current architecture):
- `Deck`: media + transport + audio + cue list
- `OutputTarget`: output window/stream target + display target + host deck
- `LayerAssignment`: deck-to-output layer mapping (`deckIndex`, `outputIndex`, `layerIndex`)

**Add a deck**: `Ctrl+N` or Companion `NEWDECK`

New decks auto-route to the currently focused output and are placed at the
top layer for that output.

**Switch focused deck**: `Tab` / `Shift+Tab`, or click the deck column header,
or Companion `DECK 2`.

**Focused deck** is the deck that receives keyboard and button actions. Each
deck column header shows the active cue and transport state for that deck.

Commands can be prefixed: `DECK 2 TAKE` switches focus to deck 2 and takes.

### Output Routing & Layering

Use these Companion commands on the focused deck:

- `ROUTE <deck>`: route this playlist to the output hosted by that deck (e.g. `ROUTE 1`, `ROUTE SELF`)
- `LAYER <n>`: set z-layer index (`0` = bottom)
- `LAYER UP|DOWN|TOP|BOTTOM`: quick layer nudges

Layering lets you run multiple playlists on one output (for example a keyed bug
playlist over a full-screen program playlist).

### Master Cues (Simultaneous Multi-Deck Cues)

Master cues store one slot per deck:
- each slot can point at a specific cue on that deck, or
- be marked as `bypass` (skip that deck when firing).

Current controls:
- Companion/remote `MASTER ...` commands (alias: `GROUP ...`) for add/select/set/capture/bypass/fire.
- Main-window master-cue sidebar programmer:
  - `Sel`, `Act`, `Byp`, `-`, `+` per deck slot
  - row click (outside buttons): assign slot from that deck's selected cue
  - mouse wheel over a programmer row: cycle slot cue up/down
- Decks tracker window shows master cues as one line per preset:
  - each line includes preset index/name plus per-deck slot summaries (`Deck`, `cue #`, `cue name` or `BYPASS`)
  - each line includes direct `Take` trigger for that preset
- Decks tracker window controls:
  - buttons: `<MC`, `MC>`, `New`, `Del`, `Take`
  - click a per-deck slot cell in a master-cue line: assign that deck slot from selected cue
  - `Shift+click` slot cell: assign from active cue
  - middle-click or `Ctrl+click` slot cell: open cue picker
  - right-click slot cell: toggle bypass
- Keyboard:
  - `Ctrl+Shift+G`: fire focused master cue
  - `Ctrl+Shift+N`: add master cue from current selected cues
  - `Ctrl+Shift+[` / `Ctrl+Shift+]`: previous/next master cue

### Video Output Mode (Preferences → Video)

The **Video** tab controls output raster sizing and display targeting for outputs:

- The Video preferences panel now opens in a larger layout (to reduce cramped controls).
- Routing is now directly editable in the same tab via a table:
  - `Deck | Output | Layer | Assigned`
  - inline per-row controls for output prev/next, layer +/- and link/unlink.
- **Prev Out / Next Out**: cycle focused output.
- **Add Output**: create a new output entity/window.
- **Enabled** (toggle): arm/disarm focused output.
  - Implemented as a dedicated toggle switch in the Video tab.
  - Outputs default to `OFF` to avoid immediate screen takeover at startup.
  - Loaded shows are also disarmed at open; operator must explicitly arm outputs in UI.
  - Turning `ON` a `window` output immediately fullscreenes it on the selected display.
  - If fixed raster mode was active, turning `ON` (or re-arming `ON`) auto-switches to **Display Native** for quality.
  - Repeating `ON` while already enabled triggers output recovery (re-apply display, raise, re-fullscreen).
  - After `Esc` windowed escape, repeated `ON` also re-arms fullscreen/recovery for that output.
- **Host Deck**: set the focused output host deck to the currently focused deck.
- **Window / Stream**: toggle focused output target type (`window` / `stream`).
- **Mirror**: open mirror source picker for focused output (`off` or another output).
- **Display target (top-right block)**:
  - `Prev` / `Next` chooses the monitor for the focused output.
  - middle display label shows the currently targeted monitor.
  - `Rescan` refreshes connected-display detection without restarting Deckboy.
  - changing display target auto-switches output sizing to **Display Native** (`auto native`) for quality.
- **Connected Displays** panel:
  - lists currently detected displays (for example HDMI sink / switcher endpoints).
  - click a display row to assign that display to the focused output directly.
  - if the focused output is enabled and type `window`, display change automatically re-fullscreens and raises that output on the selected display.
- **Display Native**: uses the selected display's current desktop mode
  (OS/EDID-negotiated resolution).
- **Fixed presets**: `720p`, `1080p`, `1440p`, `4K UHD (3840x2160)`.
- **Custom WxH**: enter any raster (for custom EDID/timing workflows).
- **Refresh target**: Auto, cycle available rates for the selected display mode,
  or set custom Hz.
- **Bit depth mode**: `Auto`, `8-bit`, or `10-bit` output compositing.
  `Auto` prefers 10-bit when the renderer/GPU supports it.
- **Size To Display**: repositions and resizes the focused output window to
  the selected display immediately.
- **Toggle Fullscreen**: same as `F`, but available in the Video tab.
  - If output is already fullscreen, `F` now re-asserts fullscreen on the selected display (recovery behavior).
- **Background recovery**:
  - Enabled `window` outputs are checked once per second.
  - If an output becomes hidden, minimized, off-display, or leaves fullscreen, Deckboy auto-recovers it.
  - Exception: if operator escaped that output with `Esc`, recovery is paused until operator re-arms (`F` or repeated `VIDEO OUTPUT ON`).
- **Triple-Esc panic safety**:
  - Press `Esc` three times quickly (`~0.9s` gaps) to force output disarm (`outputs off`) when output safety context is active.
- **Display note**: both quick controls (`Prev`, `Next`, `Rescan`) and direct display row-pick are in the Video tab.
- **Stream On/Off**: enable/disable focused-output ffmpeg network stream.
- **Protocol**: switch focused-output stream between `SRT` and `RTMP`.
- **Set Stream URL**: set focused-output stream target URL.
- **Bitrate**: set focused-output stream bitrate (`500-50000` kbps).
- **Output FX row** (per focused output):
  - `Overlay ON/OFF`: output-scoped time/ID overlay toggle.
  - `Alpha`: per-output dimmer (`0-100%`) over composited output.
  - `Delay`: per-output delay (`0-5000 ms`) for NDI/stream egress frame path.
  - `Color`: `AUTO` / `BT709` / `SRGB` color-space mode (stream metadata path).
- **Layout row** (per focused output):
  - `Span` / `Duplicate`: explicit output canvas semantic mode.
  - `Rotate`: cycle `0° -> 90° -> 180° -> 270°`.
  - `Test Card ON/OFF`: force output test signal feed.
  - `All Cards ON/OFF`: batch test-card toggle for all outputs.
- **Egress parity note**:
  - NDI/stream/delayed egress follows focused output view semantics (`span`/`duplicate`) and orientation.
- **Operational Routing Strip (main window, always visible)**:
  - Deck rows are edited inline as: `Deck -> Output -> Layer`.
  - `LINK/UNLINK` toggles assignment for that deck/output pair.
  - Output `<` `>` moves that deck route to previous/next output.
  - Layer `-` `+` adjusts layer for the current assignment.
  - This strip is the live routing surface during operation.
- **Routing Table (Prefs -> Video Outputs)**:
  - same route model, but in a compact table for quick auditing during setup.
  - useful when checking all deck assignments at once before show start.

### Standard vs Stream vs NDI (current operator path)

Use this exact flow:

1. **Standard output (window/display)**  
   - In `Prefs -> Video Outputs`: click `Create Standard` (creates a new `window` output, initially `off`).
   - In `Prefs -> Video Outputs`: select output, keep type as `Window`, assign display, then arm `Enabled`.

2. **Stream output (SRT/RTMP)**  
   - In `Prefs -> Video Outputs`: click `Create Stream` (new output target with type `stream`).
   - If needed, use `Set Stream` on an existing output.
   - Set `Stream URL...`, protocol (`SRT`/`RTMP`), bitrate, then `Stream ON`.
   - Optional: set `Mirror` to mirror another output feed.

3. **NDI output**  
   - NDI is **per output target**.
   - Focus the output (`VIDEO OUTPUT <n>` or `Prev Output` / `Next Output` in Video Outputs), then press `N` (`NDI ON/OFF`).
   - Optional commands:
     - `NDI NAME <name>`
     - `NDI KEY ON|OFF`
     - `NDI KEY NAME <name>`

Current stream implementation notes:
- Streaming is per-output.
- Stream outputs can mirror another output feed, or render their own assignments when mirror is `off`.
- Stream ffmpeg path now muxes H.264 video + AAC stereo audio.
- Audio source follows the output assignment stack (host deck fallback if no assignments are present).
- Output transforms for that output (`layout`, `orientation`, and `test card`) are applied before stream/NDI egress.

---

## 11. Overlay Compositor

Up to 4 lower-third / graphic cues can be stacked simultaneously in z-order.

| Action | Key | Companion |
|--------|-----|-----------|
| Push cue to overlay stack | `Enter` on a LowerThird cue | `OVERLAY PUSH <n>` |
| Pop top overlay | `Backspace` | `OVERLAY POP` |
| Clear all overlays | — | `OVERLAY CLEAR` |

Overlays are rendered above video with independent accent colours per slot.
Each slot fades independently based on the cue's fade-in/fade-out settings.

---

## 12. Test Patterns

Use the main control bar **Pattern** button to pick any test pattern from a
menu, or use `PATTERN <type>` via Companion.

| Type ID | Description |
|---------|-------------|
| `pocket-test` | Animated tropical platform-adventure scene cycle (day/sunset/night/storm + creatures + signal strip) |
| `pocket-day` | Animated day variant of Pocket Test |
| `pocket-sunset` | Animated sunset variant of Pocket Test |
| `pocket-night` | Animated night variant of Pocket Test |
| `pocket-storm` | Animated storm variant of Pocket Test |
| `smpte-bars` | SMPTE 75% HD colour bars with PLUGE strip |
| `smpte-bars-motion` | SMPTE bars with moving scan overlays |
| `crosshatch` | White grid on black with red centre cross and green safe-area markers |
| `crosshatch-motion` | Crosshatch with animated grid phase and marker sweep |
| `checkerboard` | 64 px alternating black/white squares |
| `checkerboard-motion` | Checkerboard with animated phase shift and sweep line |
| `full-white` | 100 % white field |
| `full-black` | 100 % black field |
| `full-red` | 100 % red field |
| `full-green` | 100 % green field |
| `full-blue` | 100 % blue field |
| `full-*-motion` | Pulsing full-field motion variants (`white/black/red/green/blue`) |

Pattern cues are generated in-process — no external file needed. Animated
patterns rebuild every frame.

---

## 13. Browser Cues

Browser cues render a live web page **into** the output window via a virtual
framebuffer (Xvfb) and ffmpeg x11grab capture — the result feeds the normal
video pipeline, so transitions and the program monitor preview work exactly as
with any other cue.

**Requires**: a Chromium-family browser installed on the machine.

**Add**: press `B`, enter a URL. Or Companion `BROWSER https://example.com`.

**Startup phases**: the cue goes through three phases before showing live
content:
1. `WaitXvfb` (400 ms) — virtual display is starting
2. `WaitChrome` (1 200 ms) — Chromium is loading the page
3. `Live` — frames stream into the output window

The output window stays black during startup — no spinners or status overlays
are ever shown on the output.

---

## 14. Timecode & Chase

Each deck maintains an independent timecode clock.

| Feature | Key | Companion |
|---------|-----|-----------|
| Run timecode | `T` | `TIMECODE RUN ON/OFF` |
| Chase mode | `5` | `TIMECODE CHASE ON/OFF` |
| Set timecode | — | `TIMECODE 00:01:02:12` |
| Set FPS | — | `TIMECODE FPS 29.97` |
| Jam sync | — | `TIMECODE JAM ON/OFF` |
| Freewheel window | — | `TIMECODE FREEWHEEL 1.5` |
| Set TC mark on cue | — | `TCMARK NOW` or `TCMARK 00:00:12:10` |
| Clear TC mark | — | `TCMARK CLEAR` |
| Trigger on TC mark | — | `TIMECODE TRIGGER ON` |

When **chase mode** is on, Deckboy reads timecode from OSC (`/timecode`) and
keeps its internal clock aligned. Cues with a TC mark trigger automatically
when the timecode clock reaches the mark.

`TIMECODE FREEWHEEL` controls how long a chase+run deck keeps advancing after
external TC updates stop. `TIMECODE JAM OFF` reduces continuous re-jamming so
the clock can run smoothly between updates.

---

## 15. NDI Output

NDI output is optional and per-output. If NDI SDK headers were present at build
time and a valid NDI runtime library is available (or `PLAYBOY_NDI_LIB` is
set), each output target can publish a network NDI source.

| Action | Key | Companion |
|--------|-----|-----------|
| Toggle NDI for focused output | `N` | `NDI ON / OFF` |
| Rename NDI source | — | `NDI NAME Stage Left Feed` |
| Toggle NDI key output | — | `NDI KEY ON / OFF` |
| Rename NDI key source | — | `NDI KEY NAME Stage Left Key` |

The fill NDI stream carries composited output video + mixed stereo audio for
that output's assigned deck stack.
When key output is enabled, Deckboy also publishes a second NDI stream with
grayscale key matte (white = opaque, black = transparent).
If `VIDEO OUTPUT DELAY` is set, that delay is applied to NDI egress frames.

---

## 16. Audio

Deckboy uses SDL2 for audio output. All video and browser cues play audio
through the focused deck's selected audio device.

UI click sounds use a **separate** SDL audio device so they never interfere
with the programme stream.

| Action | Key | Companion |
|--------|-----|-----------|
| Cycle audio device | `A` | `AUDIO NEXT` |
| Return to system default | — | `AUDIO DEFAULT` |
| Set volume | `+ / -` | `VOLUME 75` |
| Toggle UI sounds | `1` | `SFX ON / OFF` |
| Toggle UI animations | `2` | `ANIM ON / OFF` |

---

## 17. Show Files

Show files use the `.playboy` extension (plain text, tab-delimited).

| Action | Key |
|--------|-----|
| Save current show | `Ctrl+S` |
| Save As | `Ctrl+Shift+S` |
| Open show | `Ctrl+O` |

UI equivalents are always visible in the main header:
- `New` starts a blank show.
- `Open` loads a `.playboy` show from picker.
- `Save` writes the current show file immediately.
- `SaveAs` chooses a new `.playboy` path and writes immediately.

The default show file is `data/default.playboy` in the project directory. Set
`PLAYBOY_PROJECT=/path/to/show.playboy` to use a different path at launch.

---

## 18. Companion Control

Deckboy opens a TCP/UDP control port (default **5510**) for Bitfocus Companion
or any plain-text client.

**Setup in Companion**:
1. Add a `Generic TCP/UDP` connection
2. Host: IP of the Deckboy machine
3. Port: `5510` (or your override)
4. Protocol: TCP or UDP

Official Stream Deck + Companion mapping bundle:
- `docs/streamdeck/deckboy_companion_profile_map.json`
- `docs/streamdeck/deckboy_main_page.csv`
- `docs/streamdeck/README.md`

Each Companion button sends a plain-text command string. Commands are
case-insensitive.

Network tab notes:
- `Settings -> Network -> Change port...` updates Companion/OSC UDP+TCP port live.
- OSC Query HTTP is separate and optional (see section 19).
- Integration adapter toggles (ATEM/NDI trigger/NMC/MTC/LTC/DMX-ArtNet) live in
  `Settings -> Network -> INTEGRATION ADAPTERS`.
- Runtime bridges in this build:
  - ATEM UDP trigger bridge on port `9910` (`PLAYBOY_ATEM_BRIDGE_PORT` override)
  - Art-Net trigger bridge on configured `ARTNETPORT`
  - MTC ingest from ALSA MIDI quarter-frame events when `MTC ON`.

See [Section 21](#21-companion-command-reference) for the full command list.

---

## 19. OSC Input

Deckboy also accepts OSC over UDP on the same port as Companion (`5510`).
Deckboy can optionally run an OSC Query HTTP endpoint (default `5511`) from
`Settings -> Network -> OSC QUERY`.

Supported OSC addresses:

```
/go  /play  /pause  /stop  /clear  /next  /prev
/select i  /take i  /goto s
/cue/id s  /cue/audio i  /cue/pausebegin i  /cue/pauseend i  /cue/nexttrans i  /cue/goto s
/find s  /find/next  /find/prev  /find/take s  /find/clear
/renumber s
/deck i  /deck/next  /deck/prev
/deck/opacity f  /deck/autofade i  /deck/fade f
/playlist/opacity f  /playlist/autofade i  /playlist/fade f
/route s  /layer s|i
/volume f  /seek f
/autonext i  /playlistloop i
/transition f  /transition/style s
/ndi i  /ndi/name s
/video s
/overlay i  /timeoverlay i
/in f  /out f  /trim/clear
/timecode s|f  /timecode/chase i  /timecode/run i
/timecode/fps f  /timecode/jam i  /timecode/freewheel f  /timecode/mark s
/status  /state  /ping
/oscquery i  /oscquery/port i
/osc/feedback i  /osc/feedback/rate f
/atem i  /ndi/trigger i  /nmc i  /mtc i  /ltc i
/artnet i  /artnet/port i
/integration s
```

Integration runtime bridge behavior:
- `ATEM ON`: bridge payload mapping:
  - `CUT`/`AUTO`/`TAKE` -> `TAKE`
  - `PLAY`/`STOP`/`NEXT`/`PREV`/`CLEAR`/`PANIC` -> matching command
  - `SCENE <n>` -> `GROUP <n> FIRE`
  - `DECKBOY <command>` -> forwards the command tail directly.
- `MTC ON`: ALSA MIDI quarter-frame ingest updates Deckboy timecode
  (chase-enabled decks, or focused deck fallback).
- `ARTNET ON`: Art-Net `ArtDMX` default channel map:
  - ch1 `TAKE`, ch2 `PLAY`, ch3 `STOP`, ch4 `GO`
  - ch5 `NEXT`, ch6 `PREV`, ch7 `CLEAR`, ch8 `PANIC`
  - ch9 `TAKE <value>`, ch10 `GROUP <value> FIRE`.

OSC bundles (`#bundle`) are supported. Accepted commands receive a
`/playboy/ack` reply. The `/status` and `/state` addresses return
`/playboy/state` JSON replies. Recent OSC senders also receive periodic
`/playboy/state` feedback broadcasts.

When OSC Query is enabled:
- `http://127.0.0.1:<oscQueryPort>/` shows a simple endpoint browser.
- `http://127.0.0.1:<oscQueryPort>/oscquery.json` returns endpoint docs + live state.
- `http://127.0.0.1:<oscQueryPort>/state.json` returns live status JSON only.

Optional mirror mode (`OSCFEEDBACK ON`) sends canonical value OSC updates
(`/playboy/deck/*`, `/playboy/output/*`, `/playboy/integration/*`) at the configured rate limit.

---

## 20. Keyboard Reference

### Cue navigation
| Key | Action |
|-----|--------|
| `Up / Down` | Move selection |
| `Shift+Up / Shift+Down` | Reorder selected cue |
| `Enter` | Take selected cue |
| `Delete` | Remove selected cue |
| `Type A-Z/0-9/-/_` | Cue ID type-ahead search (find/select matching cue token) |

### Transport
| Key | Action |
|-----|--------|
| `Space` | Play / pause active cue |
| `S` | Stop |
| `C` | Clear output (cut to black) |
| `F` | Toggle output fullscreen |
| `Left / Right` (paused, active video) | Nudge playhead by 1 frame |
| `Shift+Left / Shift+Right` (paused, active video) | Nudge by 5 frames |
| `Ctrl+Left / Ctrl+Right` (paused, active video) | Nudge by 10 frames |
| `Alt+Left / Alt+Right` (paused, active video) | Nudge by 1.0s (frame-snapped) |

### Cue settings
| Key | Action |
|-----|--------|
| `L` | Toggle loop |
| `E` | Toggle hold on last frame |
| `X` | Cycle end action |
| `O` | Set trim-out at current playhead on active video cue (frame-snapped) |
| `Shift+O` | Toggle time overlay for focused deck |
| `[ / ]` | Fade-in −/+ 0.25 s |
| `Shift+[ / Shift+]` | Fade-out −/+ 0.25 s |
| `+ / -` | Volume up / down |
| `Backspace` | Pop active overlay |

### Adding cues
| Key | Action |
|-----|--------|
| `I` | Set trim-in at current playhead on active video cue (frame-snapped) |
| `Shift+I` | Import media (file picker) |
| `B` | Add browser cue |
| `P` | Add Pocket Test pattern |
| `G` | Add lower-third / graphic cue |

### UI toggles
| Key | Action |
|-----|--------|
| `1` | Toggle UI sounds |
| `2` | Toggle UI transitions / animations |
| `3` | Toggle auto-advance |
| `4` | Toggle playlist loop |
| `5` | Toggle timecode chase mode |
| `T` | Toggle timecode run mode |

### Multi-deck
| Key | Action |
|-----|--------|
| `Tab` | Focus next deck |
| `Shift+Tab` | Focus previous deck |
| `Ctrl+N` | Add new deck |
| `Ctrl+Shift+G` | Fire focused master cue |
| `Ctrl+Shift+N` | Add master cue from selected cues |
| `Ctrl+Shift+[` | Focus previous master cue |
| `Ctrl+Shift+]` | Focus next master cue |
| `N` | Toggle NDI for focused output |
| `A` | Cycle audio output device |
| `D` | Cycle output display |

### Show files
| Key | Action |
|-----|--------|
| `Ctrl+S` | Save |
| `Ctrl+Shift+S` | Save As |
| `Ctrl+O` | Open |

---

## 21. Companion Command Reference

Commands are plain text strings. Arguments separated by spaces.

### Core transport

```
GO              play/pause toggle, or take if nothing is active
PLAY            play / resume
PAUSE           pause
STOP            stop and rewind
CLEAR           cut to black
TAKE            take selected cue
TAKE 3          select cue 3 and take it
NEXT            advance selection one cue
PREV            retreat selection one cue
```

### Selection & navigation

```
SELECT 3        select cue 3
SELECTID cue-abc123     select cue by ID
GOTO 3          select cue 3
GOTO cue-abc123 select cue by ID
GOTO opener     select first cue whose name contains "opener"
FIND opener     find by cue number, ID, or name token
FINDNEXT        cycle to next find match
FINDPREV        cycle to previous find match
FINDTAKE intro  find first match and take it
FINDCLEAR       clear stored find token/matches
FINDSTATUS      show current find token + match cursor
RENUMBER Q 1    set cue numbers to Q1, Q2, Q3...
RENUMBER CLEAR  clear all cue numbers in focused deck
```

### Playback settings

```
VOLUME 75       set volume 0–100
SEEK 12.5       seek to 12.5 seconds
IN 2.0          set in-point
OUT 8.5         set out-point
TRIM CLEAR      clear in/out points
LOOP ON|OFF
HOLD ON|OFF
PAUSEBEGIN ON|OFF
PAUSEEND ON|OFF
CUEAUDIO ON|OFF
NEXTTRANS ON|OFF
CUEGOTO Q12
CUEGOTO        show current goto target
CUEIDSHORT A1
FADEIN 1.5      set fade-in duration
FADEOUT 1.0     set fade-out duration
SPEED 1.5       set playback speed (0.25–4.0×)
SCALE 1.5       set output scale X and Y equally (0.25–4.0×)
SCALEX 1.2      set output scale X only (0.25–4.0×)
SCALEY 1.8      set output scale Y only (0.25–4.0×)
```

### Playlist

```
AUTONEXT ON|OFF
PLAYLISTLOOP ON|OFF
SHUFFLE ON|OFF
PLAYLISTOPACITY 75
DECKOPACITY 75
DECKAUTOFADE ON|OFF
DECKFADE 1.2
```

### Master Cues / Simul-Fire

```
MASTER                  show focused master cue summary
MASTER LIST             show master cue count/focus
MASTER ADD Opener       create master cue, capture selected cues for each deck
MASTER ADDEMPTY         create empty (all-bypass) master cue
MASTER SELECT 2         focus master cue 2
MASTER NEXT|PREV        cycle focused master cue
MASTER NAME Main Opener rename focused master cue
MASTER SET 1 12         set deck 1 slot to cue token "12"
MASTER SET 2 SEL        set deck 2 slot to currently selected cue
MASTER SET 3 ACTIVE     set deck 3 slot to currently active cue
MASTER SET 4 BYPASS     bypass deck 4 in this master cue
MASTER BYPASS 4 ON|OFF|TOGGLE
MASTER CAPTURE SEL      capture selected cues from all decks
MASTER CAPTURE ACTIVE   capture active/live cues from all decks
MASTER FIRE             fire focused master cue
MASTER FIRE 2           fire master cue 2
MASTER DELETE           delete focused master cue

# Alias:
GROUP ...               all MASTER commands also accept GROUP
```

### Transitions

```
TRANSITION 0.75              deck transition duration
TRANSITION STYLE DIP         deck transition style
TRANSITIONSTYLE CROSSFADE    (alias)
```

### Patterns

```
PATTERN                 add default pattern type
PATTERN LIST            show pattern type count
PATTERN SET smpte-bars  set default pattern type
PATTERN pocket-test
PATTERN pocket-day
PATTERN pocket-sunset
PATTERN pocket-night
PATTERN pocket-storm
PATTERN smpte-bars
PATTERN smpte-bars-motion
PATTERN crosshatch
PATTERN crosshatch-motion
PATTERN checkerboard
PATTERN checkerboard-motion
PATTERN full-white
PATTERN full-black
PATTERN full-red
PATTERN full-green
PATTERN full-blue
PATTERN full-white-motion
PATTERN full-black-motion
PATTERN full-red-motion
PATTERN full-green-motion
PATTERN full-blue-motion
PATTERN smpte-bars MOTION
```

### Browser

```
BROWSER https://example.com   add browser cue with URL
```

### Overlays

```
OVERLAY ON|OFF          toggle overlay compositor display
OVERLAY PUSH 3          push cue 3 to overlay stack
OVERLAY POP             pop top overlay
OVERLAY CLEAR           clear all overlays
```

### Lower-third content

```
LOWERTEXT Hello world!        set main text line
LOWERSUB Director · Camera    set sub text line
```

### Audio & display

```
AUDIO NEXT          cycle audio output device
AUDIO DEFAULT       return to system default audio
DISPLAY NEXT        cycle output display
DISPLAY 2           use second display
ROUTE 1             route focused playlist to output host deck 1
ROUTE SELF          route focused playlist back to its own output
LAYER 2             place focused playlist on layer 2
LAYER UP            move focused playlist one layer up
LAYERNAME 0 BG      rename layer 0 to "BG"
LAYERNAME "LayerA" "Video1"   rename "LayerA" to "Video1"
VIDEO               show current video output mode
VIDEO NATIVE        follow selected display's desktop mode (EDID path)
VIDEO 4K            fixed 3840x2160 output
VIDEO 1920x1080     fixed custom raster (WIDTHxHEIGHT also accepted)
VIDEO CUSTOM 3440x1440   fixed ultrawide/custom EDID raster
VIDEO 3840x2160@50  set raster + target refresh in one command
VIDEO REFRESH AUTO  follow desktop/default refresh for selected mode
VIDEO REFRESH NEXT  cycle available refresh rates for current raster
VIDEO REFRESH 59.94 set explicit refresh target (Hz)
VIDEO DEPTH AUTO    automatic bit-depth selection (prefer 10-bit)
VIDEO DEPTH 8       force 8-bit compositing
VIDEO DEPTH 10      request 10-bit compositing (falls back if unsupported)
VIDEO SIZE DISPLAY  resize/reposition focused output to selected display
VIDEO OUTPUT NEXT   focus next output
VIDEO OUTPUT PREV   focus previous output
VIDEO OUTPUT 2      focus output index (1-based)
VIDEO OUTPUT ADD    create a new output
VIDEO OUTPUT ADD STREAM   create a new stream output
VIDEO OUTPUT ON     enable focused output (window outputs go fullscreen)
VIDEO OUTPUT OFF    disable focused output
VIDEO OUTPUT TOGGLE toggle focused output on/off
VIDEO OUTPUT ASSIGN assign focused deck to focused output (next layer)
VIDEO OUTPUT ASSIGN 5  assign to explicit layer
VIDEO OUTPUT HOST 1 set focused output host deck
DISPLAY / VIDEO OUTPUT display changes auto-switch to VIDEO NATIVE sizing
VIDEO OUTPUT TYPE WINDOW
VIDEO OUTPUT TYPE STREAM
VIDEO OUTPUT MIRROR 1
VIDEO OUTPUT MIRROR OFF
VIDEO OUTPUT ALPHA 85
VIDEO OUTPUT ALPHA DOWN
VIDEO OUTPUT DELAY 250
VIDEO OUTPUT DELAY OFF
VIDEO OUTPUT OVERLAY ON|OFF|TOGGLE
VIDEO OUTPUT COLORSPACE AUTO|BT709|SRGB
VIDEO OUTPUT LAYOUT SPAN|DUPLICATE|NEXT|PREV
VIDEO OUTPUT ORIENTATION 0|90|180|270|NEXT|PREV|RESET
VIDEO OUTPUT TESTCARD ON|OFF|TOGGLE
VIDEO OUTPUT TESTCARD ALL ON|OFF
VIDEO STREAM ON|OFF
VIDEO STREAM SRT|RTMP
VIDEO STREAM URL srt://127.0.0.1:9000?mode=caller
VIDEO STREAM BITRATE 6500
SOURCE WINDOW active-window
SOURCE CAMERA default-camera
SOURCE SYPHON default-bus
VIDEO CANVAS OFF
VIDEO CANVAS ON
VIDEO CANVAS 5760x2160
VIDEO CANVAS DISPLAY         use focused output raster as canvas size
VIDEO VIEW 320,40            set focused deck viewport inside canvas
VIDEO VIEW LEFT 200          nudge viewport
VIDEO WARP ON|OFF|TOGGLE
VIDEO WARP MODE LINEAR|PERSPECTIVE|NEXT|PREV
VIDEO WARP TL -12 8          move one corner (TL/TR/BR/BL)
VIDEO WARP RESET
VIDEO BLEND L 8              set edge blend (% or 0.0-0.49)
VIDEO BLEND ALL 5
VIDEO BLEND RESET
CANVAS ... / VIEW ... / WARP ... / BLEND ...   aliases for VIDEO subcommands
```

### NDI

```
NDI ON|OFF
NDI NAME Stage Left Feed   rename NDI sender
NDI KEY ON|OFF             enable/disable paired key sender
NDI KEY NAME Stage Left Key  rename key sender
NDI KEY TOGGLE
NDIKEY ON|OFF              alias for NDI KEY ON|OFF
NDIKEYNAME Stage Left Key  set key sender name
VIDEO OUTPUT 2             focus output 2 before running NDI commands
```

### Timecode

```
TIMECODE 00:01:02:12        set timecode value
TIMECODE SET 12.5           set timecode in seconds
TIMECODE CHASE ON|OFF
TIMECODE RUN ON|OFF
TIMECODE FPS 29.97
TIMECODE TRIGGER ON|OFF
TIMECODE JAM ON|OFF
TIMECODE FREEWHEEL 1.5
TCMARK NOW                  set TC mark to current timecode
TCMARK 00:00:12:10          set TC mark at specific time
TCMARK CLEAR                clear TC mark on selected cue
```

### Multi-deck

```
DECK 2              focus deck 2
DECK 2 TAKE         focus deck 2 and take
DECK 2 SELECT 3     focus deck 2 and select cue 3
VIDEO OUTPUT 2      focus output 2
NDI ON              toggle NDI on focused output
DECKNEXT            focus next deck
DECKPREV            focus previous deck
NEWDECK             add a new deck
ROUTE 2             route focused deck to output host deck 2
LAYER TOP           move focused deck to top layer on its routed output
```

### UI toggles

```
SFX ON|OFF
ANIM ON|OFF
FULLSCREEN
```

### Network / OSC Query

```
OSCQUERY ON|OFF           enable/disable OSC Query HTTP server
OSCQUERYPORT 5511         set/query OSC Query HTTP port
OSCFEEDBACK ON|OFF        canonical OSC feedback mirror mode
OSCFEEDBACKRATE 120       mirror feedback rate limit in milliseconds
ATEM ON|OFF               toggle ATEM trigger adapter
NDITRIGGER ON|OFF         toggle NDI metadata trigger adapter
NMC ON|OFF                toggle NMC transport sync adapter
MTC ON|OFF                toggle MTC ingest adapter
LTC ON|OFF                toggle LTC ingest adapter
ARTNET ON|OFF             toggle DMX/Art-Net adapter
ARTNETPORT 6454           set/query Art-Net adapter port
INTEGRATIONS [STATUS]     show adapter route summary
```

Runtime note:
- `ATEM`, `MTC`, and `ARTNET` are live in this Linux build.
- `NDITRIGGER`, `NMC`, and `LTC` remain scaffolded command surfaces.

### Status

```
PING                    returns PONG
STATUS                  multi-line snapshot of all decks and outputs
STATUS 2                snapshot of deck 2 only
STATUS CUES             cue-programming snapshot (selected/active cue number+id per deck)
STATUS FIND             current find token/match cursor summary
STATUS JSON             JSON snapshot (includes outputs, panic, timecode follower, find state, and integrations)
STATE                   alias for STATUS
STATE JSON              alias for STATUS JSON
```

`STATUS`/`STATUS JSON` output entries now include a backend route summary
(`backend` / `backendRoute`) showing the active runtime dispatch chain
(for example `window[ok]+ndi[stub]`), plus integration adapter route state
(`integrations` / `integrationRoute`).

---

*Deckboy_0.01 — dot-matrix cue deck — model db-001*
