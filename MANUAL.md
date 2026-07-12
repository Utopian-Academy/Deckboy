# Deckboy — User Manual

> dot-matrix cue deck · v0.79.x

Deckboy is a native desktop cue deck for live events: a fast, keyboard-driven
player for video, stills, live sources, and generated patterns, with
professional output routing (fullscreen windows, NDI, DeckLink, Spout, and
network streams) and deep remote control (Companion, OSC, HyperDeck, timecode).

It is built on SDL3 with in-process FFmpeg/libav decode. On Windows the
hardware decode path is zero-copy (D3D11VA on the output's device). Windows is
the primary platform; Linux and macOS builds share the same core.

---

## Contents

1. [Concepts](#1-concepts)
2. [Running Deckboy](#2-running-deckboy)
3. [Startup](#3-startup)
4. [Interface Layout](#4-interface-layout)
5. [Cue Types](#5-cue-types)
6. [Importing & Adding Cues](#6-importing--adding-cues)
7. [The Cue Inspector](#7-the-cue-inspector)
8. [Transport](#8-transport)
9. [Playlists, Loop, Shuffle & Cue Endings](#9-playlists-loop-shuffle--cue-endings)
10. [Transitions](#10-transitions)
11. [Multi-Deck Operation](#11-multi-deck-operation)
12. [Outputs & Routing](#12-outputs--routing)
13. [Output Geometry: AOI, Warp, Edge Blend](#13-output-geometry-aoi-warp-edge-blend)
14. [Overlays: PiP & Lower Thirds](#14-overlays-pip--lower-thirds)
15. [Audio](#15-audio)
16. [Test Patterns](#16-test-patterns)
17. [Timecode & Chase](#17-timecode--chase)
18. [Show Files, Bundling & Missing Media](#18-show-files-bundling--missing-media)
19. [Themes](#19-themes)
20. [Remote Control](#20-remote-control)
21. [Reliability & Soak Testing](#21-reliability--soak-testing)
22. [Keyboard Reference](#22-keyboard-reference)
23. [Command-Line Flags](#23-command-line-flags)

---

## 1. Concepts

- **Cue** — one playable item (a video, image, pattern, live source, overlay,
  composite, or audio file) with its own trim, fades, geometry, and audio trim.
- **Deck** — an ordered playlist of cues with its own transport (play/stop/
  seek), loop/shuffle mode, and default cue behaviour. Deckboy supports
  multiple decks.
- **Output** — a destination with its own window/compositor: a fullscreen
  display, an NDI/DeckLink/Spout sender, or a network stream. Outputs are
  separate from decks.
- **Layer assignment** — the mapping of decks onto outputs. Several decks can
  stack on one output; one deck can drive several outputs.
- **Program** vs **Preview** — the program monitor shows what is live on the
  focused deck; the cue list selection is what you are *about* to take.

The operating loop is: select a cue → **Take** it (Enter) → it goes live on the
deck's output(s), honouring its fade/transition → it ends per its end action
(stop, hold, loop, or auto-advance to the next cue).

---

## 2. Running Deckboy

**Windows (primary):** launch `Deckboy.exe`. It resolves its `data/` directory
by walking up from the executable, so run it from the build/dist folder that
contains `data/`.

**Build from source (Windows):**

```
cd native
cmake -B ../build/windows -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build ../build/windows --config Release
```

The binary lands at `build/windows/Release/Deckboy.exe`.

Optional runtime dependencies are loaded dynamically and only needed for the
matching feature: the NDI SDK (`Processing.NDI.Lib.x64.dll`) for NDI in/out,
the Blackmagic DeckLink SDK for DeckLink output, Spout for texture sharing,
and WebView2 for browser cues. Run `Deckboy.exe --self-check` to see what is
detected.

---

## 3. Startup

On launch Deckboy shows a startup card with the **Deckboy** wordmark, a version
line, and a short boot log while backends come up. Press **Enter** to dismiss
it and start.

Deckboy reopens the last show automatically. Set
`DECKBOY_PROJECT=C:\path\show.deckboy` to force a specific show, or
`DECKBOY_THEME=<name>` to force a colourway at launch.

A single-instance lock prevents accidental duplicate launches; use
`--allow-multi-instance` to override it (debugging only).

---

## 4. Interface Layout

The control window is split into:

- **Toolbar** (top): `NEW`, `OPEN`, `SAVE`, `BUNDLE`, and — only when media is
  missing — a red `RELINK` button; loop/shuffle mode toggles; the master
  volume fader; and fullscreen / blackout controls.
- **Playlist column** (left): the focused deck's cue list. Each row shows cue
  number, type, name, duration/end action, a colour-tag chip, and quick action
  icons (fade in/out, loop, hold, audio). The live cue, the queued next cue,
  and the selection are highlighted distinctly.
- **Timeline & transport** (centre): the program monitor, the video/audio
  lanes with the playhead and in/out trim, and the transport buttons.
- **Cue Inspector** (right): all settings for the selected cue, in collapsible
  sections. Drag the splitter between the program area and the inspector to
  resize.
- **Monitors window** (separate): per-output preview and routing.

---

## 5. Cue Types

| Type | Source |
|------|--------|
| **Video** | A video file (any FFmpeg-readable container/codec, incl. HAP, ProRes, H.264/265 hardware-decoded) |
| **Image** | A still (held for a set duration or until taken away) |
| **Pattern** | A generated test pattern (see §16) |
| **Browser** | A live web page rendered via WebView2 (Windows) |
| **Window / Screen** | Desktop window or screen capture |
| **Camera** | A capture device |
| **Syphon / Spout** | A shared GPU texture from another app |
| **Stream (SRT)** | A live network input — `cue.path` is the full URL (`srt://`, `rtmp://`, `rtsp://`, `udp://`) |
| **NDI Source** | An NDI receive input — `ndi://SOURCE_NAME` |
| **PiP** | Picture-in-picture overlay of another cue/source |
| **Lower Third** | Text overlay bar |
| **Composite** | A multi-slot scene (2-up, quad, 70/30, etc.) |
| **Audio** | An audio-only file with a waveform lane |

---

## 6. Importing & Adding Cues

- **Import media:** press `I` or `Import`, or drag files onto the window.
  Metadata is probed asynchronously (`probing…` shows on the row until done).
- **Add a source cue:** the SOURCE menu adds stream, NDI, camera, window,
  browser, PiP, lower-third, and composite cues.
- **Add a pattern:** press `P` or use the pattern menu.

New cues inherit the deck's playlist defaults (fade lengths, loop, pause
behaviour, audio-enabled). Reorder by dragging; multi-select with Shift/Ctrl;
copy/paste cue settings with `Ctrl+C` / `Ctrl+V`.

---

## 7. The Cue Inspector

The inspector edits the selected cue in collapsible sections. Most numeric rows
have `−`/`+` steppers, are drag-to-scrub, and click-to-type an exact value.

- **PLAYBACK** — loop & loop count, hold last frame, pause at start, playback
  speed (0.25–4×, pitch-corrected audio), fade in/out, in/out trim, pause
  points, end action, goto target, next-transition toggle.
- **AUDIO** — per-cue gain, pan, mono, independent audio fades, loudness
  normalize, and output-pair routing (see §15).
- **GEOMETRY** — scale mode (fit/fill/stretch/unscaled), scale, offset, crop,
  rotation, and colour controls (brightness/contrast/saturation/hue).
- **KEY** — chroma key colour, tolerance, and softness.
- Metadata — cue number, name, colour tag, notes.

Values apply live where possible (fades, audio trim, geometry) without
reloading the decode.

---

## 8. Transport

| Action | Key |
|--------|-----|
| Take selected cue live | `Enter` |
| Play / Pause | `Space` |
| Stop active cue | `S` |
| Rerack (rewind to first frame, hold) | `Ctrl+R` |
| Skip ±10 s | `Left` / `Right` |
| Skip to start / end | `Home` / `End` |
| Set in / out point at playhead | `Ctrl+I` / `Ctrl+O` |

Click or drag the timeline lanes to seek. When a cue has audio, the video
position slaves to the audio device clock, so long-form and variable-frame-rate
playback stay in sync.

---

## 9. Playlists, Loop, Shuffle & Cue Endings

Each cue's **end action** decides what happens when it finishes:

- **Stop** — go to black (or hold, if hold-last-frame is set).
- **Loop** — repeat (respecting the loop count, 0 = infinite).
- **Hold** — freeze on the last frame.
- **Auto-Next** — advance to the next playable cue.
- **Goto** — jump to a specific cue number.

Deck modes (toolbar toggles):

- **Loop playlist** — after the last cue, wrap to the first.
- **Shuffle** — auto-advance picks a random other cue. The shuffle generator is
  seeded from a real entropy source at launch, so the order differs every run.

Missing cues are skipped on auto-advance so a single missing file can't stop
the show (see §18).

---

## 10. Transitions

Cue-to-cue transitions are set at the deck level and can be overridden per cue:

- **Cut** — instant.
- **Crossfade** — alpha blend over the transition time.
- **Dip to black** — fade down, then up.

Set the deck default in the playlist settings; override on a cue in its
PLAYBACK section. The incoming cue's fade-in is the visible ramp on the output
path. `next xfade` on a cue toggles whether a transition is used when
auto-advancing into the next cue.

---

## 11. Multi-Deck Operation

Multiple decks each have independent transport and playlists. Assign decks to
outputs via layer assignments; deck opacity and auto-fade let stacked decks mix
on a shared output. The focused deck is the one the keyboard/transport act on.

---

## 12. Outputs & Routing

Outputs are managed in the Monitors window and `Settings → Video Outputs`. Each
output is one of:

- **Window** — a fullscreen (or windowed) display. Toggle the output window
  with `N`, fullscreen with `F`. Fullscreen recovery automatically re-raises a
  program output that gets minimised or lost, with strike-based backoff.
- **NDI** — network video send (optionally with a separate key/alpha source).
- **DeckLink** — SDI/HDMI out via a Blackmagic card.
- **Spout / Syphon** — share the output as a GPU texture to another app.
- **Stream** — push SRT/RTMP to a URL.

Per output you can set alpha, delay, colour space, orientation (0/90/180/270),
a test card, and a time overlay. `Blackout` (`B`) dims all outputs; panic
profiles give a one-key safe state.

---

## 13. Output Geometry: AOI, Warp, Edge Blend

Applied per output (not per cue):

- **Area of Interest (AOI)** — crop the rendered output to a sub-region
  (fractions from each edge) for multi-display slicing.
- **Warp** — corner-pin the output (drag the four corners; `Shift+drag` snaps
  to a grid). Copy/paste warp with `Ctrl+Shift+C` / `Ctrl+Shift+V`.
- **Edge blend** — feather each edge for projector soft-edge blending.

Per-*cue* geometry (scale/crop/rotation/offset/keying/colour) lives in the cue
inspector instead (§7).

---

## 14. Overlays: PiP & Lower Thirds

Lower-third and PiP cues fire into an overlay slot independently of the main
program cue, so you can bring a name strap or inset up over whatever is live.
Lower thirds carry two text lines and a background-bar opacity; PiP insets
another cue/camera/NDI source. `G` adds the selected cue as a graphic overlay;
`Backspace` clears all overlays.

---

## 15. Audio

Video, audio, and browser cues play through the focused deck's selected audio
device (`Settings → AUDIO OUTPUT`). UI click sounds use a separate device so
they never touch the programme bus.

### Per-cue audio (inspector → AUDIO)

| Control | Range | Notes |
|---------|-------|-------|
| Gain | −24 … +12 dB | Live trim in the audio thread |
| Pan | full L … full R | Constant-power balance; snaps to centre |
| Mono | on/off | Downmix for mono sources / mono PA |
| Audio fade in / out | follow / none / seconds | `follow` tracks the visual fade; set a length to duck audio independently |
| Normalize (R128) | button | Measures EBU R128 loudness and sets gain for −16 LUFS |
| Outs | pair 1-2 … 7-8 | Output pair on a multichannel device (below) |

The **deck fader** is a deck-level level on top of each cue's gain (the master
fader in the header rides on top of everything). The effective audio-fade ramp
is drawn over every waveform view — the timeline audio lane, the program strip,
and both inspector thumbs — so what you see is what plays.

### A/V delay offset

`Settings → AUDIO OUTPUT → A/V delay` holds all audio back 0–1000 ms before the
device, to line Deckboy up with a lagging display or PA DSP. Video anchors to
the undelayed timeline so the offset is a real skew at the output.

### Multichannel output routing

`Settings → AUDIO OUTPUT → Outs` opens the deck device with 2/4/6/8 channels.
Each cue routes its processed stereo onto a pair of those outs via the
inspector's **outs** row (1-2, 3-4, 5-6, 7-8) — e.g. programme to the PA on
1-2, click to monitors on 3-4. The pipeline stays stereo end to end; expansion
happens at the final write with silence on unused outs. On a device with fewer
physical outputs, SDL folds extra pairs down, so you can prep on a laptop and
route at the venue.

Companion: `AUDIOGAIN`, `AUDIOPAN`, `AUDIOMONO`, `AUDIONORM`, `AUDIOOUTS`.

---

## 16. Test Patterns

Pattern cues generate their pixels live and auto-scale to the selected output
raster and refresh rate (unless the project overrides it). All motion is slow,
smooth, and diagonal; full-frame solid colours have no motion option.

- **Pocket Test** — a PM5544-style test card with a bouncing scene-porthole
  ball, a sync beacon, and an audio sync pop at the top of each second (use it
  to dial the A/V delay offset).
- Standard bars, crosshatch, solids, and gradients.
- **Terrarium** — a hidden ecosystem simulation, unlocked per-save as a secret
  (not selectable until unlocked).

---

## 17. Timecode & Chase

Each deck can **chase** incoming timecode (follow an external master),
**run/generate** timecode, and **trigger** cues at set SMPTE times. MTC and LTC
ingest are available as integration backends (`Settings → Network`). Set the
deck's frame rate and freewheel behaviour in the timecode controls.

---

## 18. Show Files, Bundling & Missing Media

Shows are `.deckboy` files (plain text, tab-delimited).

| Action | Key |
|--------|-----|
| New show | `Ctrl+N` |
| Open | `Ctrl+O` |
| Save | `Ctrl+S` |
| Export bundle | `Ctrl+Shift+E` |

`BUNDLE` (or `Ctrl+Shift+E`) exports the show plus a copy of every media file
into a portable folder. Deckboy also autosaves with dirty tracking. The default
show is `data/default.deckboy`; override with `DECKBOY_PROJECT`.

### Missing media & relink

On open, Deckboy scans every file-backed cue. Missing files get a red
**MISSING** badge in the cue list, and a red **RELINK n** button appears in the
toolbar (only while something is missing). `RELINK` first re-checks the disk (a
re-mounted drive clears the warning), then opens a folder picker and repoints
missing cues to same-named files found under that folder — an exact file-size
match wins when several share a name.

Missing media is also caught at showtime: taking a cue whose file has vanished
is refused with a `MEDIA MISSING` toast (the output holds), and auto-advance
skips missing cues instead of cascading to black.

---

## 19. Themes

Deckboy ships many console-inspired colourways (`Settings → theme`), from the
default **gameboy** green through famicom, super-famicom, gamecube, n64,
virtual-boy, metroid, and more.

**Terminal / OLED themes.** Some dark themes render as a true-black terminal:
OLED-black backgrounds and tiles with phosphor-bright text and per-theme
accents (e.g. **dark** = green, **virtual-boy** = red). These use extra theme
roles, all of which fall back to older roles so existing themes are unchanged:

| Role | Purpose | Falls back to |
|------|---------|---------------|
| `screen_fg` | Primary on-body text ink (labels, hints, titles) | `screen_deep` |
| `screen_fg_soft` | Secondary on-tile text (cue-row subtext) | `screen_dark` |
| `screen_tile` | Interactive tile fill (buttons, idle rows) | `screen_light` |

Readability is a data contract: every theme must pass
`tools/audit_theme_contrast.ps1`, which checks each ink/fill pair the UI draws
against a WCAG-style minimum. Edit the theme, not the renderer, and re-run the
audit.

---

## 20. Remote Control

All remote inputs normalise to plain-text commands.

- **Companion / TCP-UDP** — port **5510** by default. Add a *Generic TCP/UDP*
  connection in Bitfocus Companion pointed at the Deckboy machine. A ready-made
  Stream Deck profile lives in `docs/streamdeck/`.
- **OSC** — messages/bundles on the same port, plus an OSC Query HTTP endpoint
  and mirrored `/deckboy/state` feedback.
- **HyperDeck** — Deckboy answers the HyperDeck protocol for decks that speak
  it.
- **Tally / triggers** — TSL tally out, ATEM and NDI-metadata triggers,
  Art-Net channel map, NMC sync.

Commands are case-insensitive. Examples: `TAKE`, `STOP`, `VOLUME 75`,
`AUDIOGAIN -6`, `AUDIOOUTS 2`, `AUDIO NEXT`. Toggle adapters in
`Settings → Network`.

---

## 21. Reliability & Soak Testing

- **Decode watchdog** — a wedged decode reracks the deck dark and toasts the
  operator rather than hanging the show; if the file is gone it reports
  `MEDIA LOST` and raises the RELINK state.
- **Fullscreen recovery** — a program output that is minimised or lost is
  automatically re-raised, with strike-based backoff to avoid loops.
- **`--soak [minutes]`** — a long-run stability harness. It loops the loaded
  show (or synthesized patterns) through the real app loop and logs RSS,
  decode-stall, and missing-media counters once a minute to stdout and
  `deckboy-soak.log`, then quits. It never writes the looped state back to the
  show file. Default 24 h; e.g. `Deckboy.exe --soak 720` for a 12-hour run on
  show hardware.

---

## 22. Keyboard Reference

| Key | Action |
|-----|--------|
| `Enter` | Take selected cue live |
| `Space` | Play / Pause |
| `S` | Stop active cue |
| `Ctrl+R` | Rerack (rewind to start) |
| `Up` / `Down` | Navigate cue list |
| `Left` / `Right` | Skip back / forward 10 s |
| `Home` / `End` | Skip to start / end |
| `I` | Import media files |
| `Ctrl+I` / `Ctrl+O` | Set in / out point at playhead |
| `Delete` / `Backspace` | Delete selected cue(s) |
| `Ctrl+C` / `Ctrl+V` | Copy / paste cue settings |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | Copy / paste focused warp settings |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / Redo |
| `Ctrl+G` | GOTO cue number |
| `Ctrl+F` | Find cue by name / number |
| `Ctrl+S` | Save project |
| `Ctrl+Shift+E` | Export bundled project |
| `Ctrl+O` | Open project |
| `Ctrl+N` | New project |
| `L` | Toggle loop |
| `H` | Toggle hold (pause at end) |
| `X` | Cycle end action |
| `K` | Cycle colour tag |
| `G` | Add as graphic overlay |
| `Backspace` | Clear all overlays |
| `N` | Toggle output window |
| `F` | Toggle fullscreen output |
| `B` | Toggle blackout |
| `P` | Open preferences |
| `Ctrl+/` | Shortcut overlay |
| `+` / `-` | Volume up / down |
| `Shift+drag` | Snap warp corners to grid |

---

## 23. Command-Line Flags

```
Deckboy.exe --self-check            # verify dependencies + backend wiring
Deckboy.exe --smoke                 # automated smoke test (exit 0 = pass)
Deckboy.exe --soak [minutes]        # long-run stability harness (default 24h)
Deckboy.exe --decode-bench FILE [seconds] [cli]  # decode benchmark; 'cli' forces the subprocess path
Deckboy.exe --sync-pop-test         # verify the pocket-test audio sync path
Deckboy.exe --no-inproc-decode      # force the FFmpeg subprocess decode path
Deckboy.exe --allow-multi-instance  # bypass the single-instance lock (debug)
```

Environment: `DECKBOY_PROJECT` (open a specific show), `DECKBOY_THEME` (force a
colourway), `DECKBOY_COMPANION_PORT` (control port), `DECKBOY_UI_PROFILE=1`
(UI timing + watchdog logs).
