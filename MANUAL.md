# Playboy_0.01 — User Manual

> dot-matrix cue deck · model pb-001 · v0.01

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

Playboy_0.01 is a native Linux desktop cue deck for live events. It uses SDL2
for the UI and FFmpeg for media decode. Each _deck_ has its own playlist,
output window, audio path, and transport runtime. Multiple decks can run
simultaneously for multi-screen or multi-zone shows.

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
PLAYBOY_COMPANION_PORT=5610 ./bin/playboy    # custom Companion port
PLAYBOY_PROJECT=/path/to/show.playboy ./bin/playboy  # open specific show
```

---

## 3. Startup Dialog

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
│               │                          │  (mascot + tips when     │
│               │                          │   no cue is selected)    │
├───────────────┴──────────────────────────┴──────────────────────────┤
│  Import │ Take │ Go/Pause │ Stop │ Clear │ Fullscreen │ Delete │ ... │
└─────────────────────────────────────────────────────────────────────┘
```

**Hover tips**: hover over any button, cue row, progress bar, or the mascot to
see a contextual tip. The mascot in the cue settings panel shows the full
keyboard cheatsheet on hover.

---

## 5. Cue Types

| Type | Description |
|------|-------------|
| **Video** | Any FFmpeg-readable video file. Audio is decoded alongside. |
| **Image** | Still image (JPEG, PNG, etc.) held until taken off or auto-advanced. |
| **Pattern** | Procedurally generated test pattern — no file required. |
| **Browser** | Web URL rendered via Xvfb + ffmpeg x11grab into the output window. |
| **LowerThird** | Graphic overlay pushed into the 4-slot overlay stack. |

---

## 6. Importing Media

**Drag and drop** files directly onto the control window shell — they are
probed with ffprobe and added to the focused deck's playlist.

**Keyboard import**: press `I` to open the native file picker. Multiple files
can be selected.

**Browser cue**: press `B`, enter a URL in the dialog. The cue is added to the
playlist with a "web" type.

**Pattern cue**: press `P` to add a Kawaii Pocket Test pattern. Use the
Companion command `PATTERN <type>` to add other pattern types.

**Lower-third / graphic**: press `G` to add a blank lower-third overlay cue.
Set the text via the `LOWERTEXT` and `LOWERSUB` Companion commands, or edit
`lowertext` directly in the show file.

---

## 7. Cue Settings

Select a cue (click or `Up`/`Down` arrows) to see its settings in the right panel.
If the settings list is longer than the panel, use the mouse wheel over the
settings area to scroll.

### Video cues

| Control | Keys | Description |
|---------|------|-------------|
| Volume | `+ / -` | Output gain, 0–100 % |
| Fade In | `[ / ]` | Automatic fade-in duration (±0.25 s per press) |
| Fade Out | `Shift+[ / Shift+]` | Automatic fade-out duration |
| In-point | `−/+` buttons in panel | Where playback begins |
| Out-point | `−/+` buttons in panel | Where playback ends (0 = end of file) |
| Transition | `−/+` buttons in panel | Per-cue transition duration/style override |
| Loop | `L` | Loop the cue indefinitely |
| Hold on last frame | `E` | Freeze on the last frame instead of stopping |
| End action | `X` | Cycle: inherit → stop → loop → hold → auto-next |

### Output geometry and keying (video / image / pattern / browser)

| Control | Description |
|---------|-------------|
| Scale | Per-cue output scale (0.25x to 4.0x) |
| Offset X / Y | Per-cue output position offset in pixels |
| Rotation | Per-cue rotation angle (-180 to +180 degrees) |
| Crop L / R / T / B | Per-edge crop percentage |
| Key on/off | Enable/disable per-cue chroma key |
| Key color | Pick chroma key target color (`#RRGGBB`) |
| Key tolerance | RGB distance threshold for removal |
| Key softness | Feather width around the threshold |

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

---

## 10. Multi-Deck Operation

Each deck has its own:
- Playlist and cue selection
- Output window and display assignment
- Audio device
- Transport state (play/pause/stop)
- NDI sender
- Timecode clock

**Add a deck**: `Ctrl+N` or Companion `NEWDECK`

**Switch focused deck**: `Tab` / `Shift+Tab`, or click the deck column header,
or Companion `DECK 2`.

**Focused deck** is the deck that receives keyboard and button actions. Each
deck column header shows the active cue and transport state for that deck.

Commands can be prefixed: `DECK 2 TAKE` switches focus to deck 2 and takes.

### Video Output Mode (Preferences → Video)

The **Video** tab controls output raster sizing for all decks:

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

Press `P` to add a **Kawaii Pocket Test** cue, or use `PATTERN <type>` via
Companion.

| Type ID | Description |
|---------|-------------|
| `pocket-test` | Animated GB-style scene (4-colour Game Boy palette, scrolling tiles, walking girl, HUD) |
| `smpte-bars` | SMPTE 75% HD colour bars with PLUGE strip |
| `crosshatch` | White grid on black with red centre cross and green safe-area markers |
| `checkerboard` | 64 px alternating black/white squares |
| `full-white` | 100 % white field |
| `full-black` | 100 % black field |
| `full-red` | 100 % red field |
| `full-green` | 100 % green field |
| `full-blue` | 100 % blue field |

Pattern cues are generated in-process — no external file needed. Animated
patterns (Pocket Test) rebuild every frame.

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
| Set TC mark on cue | — | `TCMARK NOW` or `TCMARK 00:00:12:10` |
| Clear TC mark | — | `TCMARK CLEAR` |
| Trigger on TC mark | — | `TIMECODE TRIGGER ON` |

When **chase mode** is on, Playboy reads timecode from OSC (`/timecode`) and
keeps its internal clock aligned. Cues with a TC mark trigger automatically
when the timecode clock reaches the mark.

---

## 15. NDI Output

NDI output is optional and per-deck. If NDI SDK headers were present at build
time and `libndi.so.6` is on the library path (or `PLAYBOY_NDI_LIB` is set),
each deck can publish a network NDI source.

| Action | Key | Companion |
|--------|-----|-----------|
| Toggle NDI for focused deck | `N` | `NDI ON / OFF` |
| Rename NDI source | — | `NDI NAME Stage Left Feed` |

The NDI stream carries video + audio for video cues.

---

## 16. Audio

Playboy uses SDL2 for audio output. All video and browser cues play audio
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

The default show file is `data/default.playboy` in the project directory. Set
`PLAYBOY_PROJECT=/path/to/show.playboy` to use a different path at launch.

---

## 18. Companion Control

Playboy opens a TCP/UDP control port (default **5510**) for Bitfocus Companion
or any plain-text client.

**Setup in Companion**:
1. Add a `Generic TCP/UDP` connection
2. Host: IP of the Playboy machine
3. Port: `5510` (or your override)
4. Protocol: TCP or UDP

Each Companion button sends a plain-text command string. Commands are
case-insensitive.

See [Section 21](#21-companion-command-reference) for the full command list.

---

## 19. OSC Input

Playboy also accepts OSC over UDP on the same port as Companion (`5510`).

Supported OSC addresses:

```
/go  /play  /pause  /stop  /clear  /next  /prev
/select i  /take i  /goto s
/deck i  /deck/next  /deck/prev
/volume f  /seek f
/autonext i  /playlistloop i
/transition f  /transition/style s
/ndi i  /ndi/name s
/video s
/overlay i  /timeoverlay i
/in f  /out f  /trim/clear
/timecode s|f  /timecode/chase i  /timecode/run i
/timecode/fps f  /timecode/mark s
/status  /state  /ping
```

OSC bundles (`#bundle`) are supported. Accepted commands receive a
`/playboy/ack` reply. The `/status` and `/state` addresses return
`/playboy/state` JSON replies. Recent OSC senders also receive periodic
`/playboy/state` feedback broadcasts.

---

## 20. Keyboard Reference

### Cue navigation
| Key | Action |
|-----|--------|
| `Up / Down` | Move selection |
| `Shift+Up / Shift+Down` | Reorder selected cue |
| `Enter` | Take selected cue |
| `Delete` | Remove selected cue |

### Transport
| Key | Action |
|-----|--------|
| `Space` | Play / pause active cue |
| `S` | Stop |
| `C` | Clear output (cut to black) |
| `F` | Toggle output fullscreen |

### Cue settings
| Key | Action |
|-----|--------|
| `L` | Toggle loop |
| `E` | Toggle hold on last frame |
| `X` | Cycle end action |
| `[ / ]` | Fade-in −/+ 0.25 s |
| `Shift+[ / Shift+]` | Fade-out −/+ 0.25 s |
| `+ / -` | Volume up / down |
| `Backspace` | Pop active overlay |

### Adding cues
| Key | Action |
|-----|--------|
| `I` | Import media (file picker) |
| `B` | Add browser cue |
| `P` | Add Kawaii Pocket Test pattern |
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
| `N` | Toggle NDI for focused deck |
| `O` | Toggle time overlay for focused deck |
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
FADEIN 1.5      set fade-in duration
FADEOUT 1.0     set fade-out duration
```

### Playlist

```
AUTONEXT ON|OFF
PLAYLISTLOOP ON|OFF
SHUFFLE ON|OFF
```

### Transitions

```
TRANSITION 0.75              deck transition duration
TRANSITION STYLE DIP         deck transition style
TRANSITIONSTYLE CROSSFADE    (alias)
```

### Patterns

```
PATTERN                 add pocket-test pattern
PATTERN pocket-test
PATTERN smpte-bars
PATTERN crosshatch
PATTERN checkerboard
PATTERN full-white
PATTERN full-black
PATTERN full-red
PATTERN full-green
PATTERN full-blue
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
```

### NDI

```
NDI ON|OFF
NDI NAME Stage Left Feed   rename NDI sender
```

### Timecode

```
TIMECODE 00:01:02:12        set timecode value
TIMECODE SET 12.5           set timecode in seconds
TIMECODE CHASE ON|OFF
TIMECODE RUN ON|OFF
TIMECODE FPS 29.97
TIMECODE TRIGGER ON|OFF
TCMARK NOW                  set TC mark to current timecode
TCMARK 00:00:12:10          set TC mark at specific time
TCMARK CLEAR                clear TC mark on selected cue
```

### Multi-deck

```
DECK 2              focus deck 2
DECK 2 TAKE         focus deck 2 and take
DECK 2 SELECT 3     focus deck 2 and select cue 3
DECK 2 NDI ON       toggle NDI on deck 2
DECKNEXT            focus next deck
DECKPREV            focus previous deck
NEWDECK             add a new deck
```

### UI toggles

```
SFX ON|OFF
ANIM ON|OFF
FULLSCREEN
```

### Status

```
PING                    returns PONG
STATUS                  multi-line snapshot of all decks
STATUS 2                snapshot of deck 2 only
STATUS JSON             JSON snapshot
STATE                   alias for STATUS
STATE JSON              alias for STATUS JSON
```

---

*Playboy_0.01 — dot-matrix cue deck — model pb-001*
