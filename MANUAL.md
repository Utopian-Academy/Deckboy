# Deckboy — User Manual

> dot-matrix cue deck

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
13. [Recording](#13-recording)
14. [Per-Cue Effects](#14-per-cue-effects)
15. [Output Geometry: AOI, Warp, Edge Blend](#15-output-geometry-aoi-warp-edge-blend)
16. [Overlays: PiP & Lower Thirds](#16-overlays-pip--lower-thirds)
17. [Audio](#17-audio)
18. [Test Patterns](#18-test-patterns)
19. [Timecode & Chase](#19-timecode--chase)
20. [Show Files, Bundling & Missing Media](#20-show-files-bundling--missing-media)
21. [Themes](#21-themes)
22. [Remote Control](#22-remote-control)
23. [Reliability & Soak Testing](#23-reliability--soak-testing)
24. [Keyboard Reference](#24-keyboard-reference)
25. [Command-Line Flags](#25-command-line-flags)

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

Two draggable dividers let you rebalance the layout: the vertical splitter
between the program area and the inspector, and a horizontal grip in the gap
under the program monitor — drag it up to shrink the preview and enlarge the
timeline lanes, down to give the height back.
- **Monitors window** (separate): per-output preview and routing.

---

## 5. Cue Types

| Type | Source |
|------|--------|
| **Video** | A video file (any FFmpeg-readable container/codec, incl. HAP, ProRes, H.264/265 hardware-decoded) |
| **Image** | A still (held for a set duration or until taken away) |
| **Pattern** | A generated test pattern (see §18) |
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
| **Tone** | A generated audio test tone, with optional on-screen diagnostics |
| **Timer** | A stage/speaker countdown with its own clock, thresholds, chimes and messages |
| **Video Synth** | Generated picture — oscillators, feedback, glitch stack, text mode, sprite sets |
| **Synth** | A playable chip voice (2A03 / FDS), driven from MIDI or the computer keyboard |

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
  normalize, and output-pair routing (see §17).
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
the show (see §20).

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

## 13. Recording

The program output can be written to a file while the show runs. `RECORD` sits
on the button bar in the OUTPUT group; it pulses while armed and shows the
running file size. Recordings land in `Settings → Recording → Destination`,
which is deliberately separate from the encode queue's output folder.

**The recording is its own standard.** Raster and rate are set independently of
the programme, and both default to *following the input* — a recording should
look like what went in unless you say otherwise. Ask for something smaller and
the composite is scaled on the GPU before it is read back, so a 1080 recording
off a 4K programme moves a quarter of the bytes.

Rates are exact where broadcast says they are exact: 23.976 is 24000/1001, and
the file carries it that way.

**Timecode.** Start at a value, at time of day, or at zero. Drop-frame,
non-drop, or auto — auto picks DF at 29.97 and 59.94 and NDF everywhere else,
which is the correct answer. Drop-frame skips two timecode *numbers* a minute
(except every tenth minute) so the count keeps pace with the wall clock; it
never drops a picture.

**Codecs.** H.264 and HEVC for a viewing copy; ProRes (LT, 422, HQ, 4444) and
DNxHR (LB, SQ, HQ, HQX) for delivery, written to `.mov` at the right pixel
format.

**Segmentation.** Roll to a new file every N minutes or N megabytes. A 3.8 GB
ceiling always applies, so a FAT32 card cannot silently truncate a take.

**If it cannot keep up, it says so.** The file must contain exactly
`rate × elapsed` frames — an encoder stamps by arrival order, so a shortfall
does not slow the file down, it *shortens* it. Deckboy counts what is owed,
repeats the last picture to cover a gap, and raises `RECORDING DROPPING FRAMES`
on the output health state, as a toast, and in the show log if it falls behind.
A recording that runs short will never look healthy.

**On stop**, a fragmented recording is remuxed into a normal MP4. A power cut
therefore leaves a playable file, and a clean stop leaves a tidy one.

**Platform note.** The frame leaves the GPU asynchronously on every platform:
Windows through a D3D11 staging ring, macOS and Linux through SDL_GPU's texture
download (Metal and Vulkan underneath), which is why output windows there ask
for the `gpu` renderer. Set `DECKBOY_OUTPUT_RENDERER=<driver>` to override that
choice if a driver misbehaves.

Whether a given machine sustains 4K60 then comes down to its decoder and
encoder, not to the recording path — and the dropped-frame alarm will say so if
it does not.

Over the wire: `RECORD [on|off|toggle]`, `RECFORMAT <WxH|program> [fps|program]`,
`RECCODEC <token>`, `RECTC <hh:mm:ss:ff|timeofday> [df|ndf|auto]`,
`RECSEGMENT <minutes> [megabytes]`.

---

## 14. Per-Cue Effects

Each cue carries an ordered **effect stack**, built in the inspector's EFFECTS
section and saved with the show. Effects run in the order you arrange them, and
order is part of the effect — posterise then invert is not invert then
posterise. Each row has the amount (nudge, drag to scrub, hold shift for fine,
or click the value to type an exact number) and a second row for changing the
effect, moving it up or down, and removing it.

Available: invert, posterise, solarise, threshold, vignette, grain, scanlines,
RGB split, temporal dither and motion puppet. Every one costs nothing at zero.

**Temporal dither** is worth trying on its own. It quantises hard to a tiny
palette but advances the dither pattern every frame, so at 60Hz your eye
integrates shades that are not in the palette at all — and it freezes into
visible checkerboard the moment you pause the deck. The still and the moving
image are deliberately different pictures.

**Motion puppet** drives this cue's pixels with a *different* clip's movement.
Choose a driver in the EFFECTS section: that clip is decoded only for the
per-macroblock motion vectors its codec already measured — its pictures are
never shown — and those vectors displace this cue. A camera feed can be
puppeteered by a crowd scene.

A driver is only as good as its motion. `Deckboy --motion-probe <file>` reports
what a clip offers before you commit to it: a mostly static clip moves a couple
of percent of its cells and will do nothing visible, while something with
whole-frame movement moves half of them and is violent. A keyframe carries no
vectors at all, so the picture is briefly left alone — that is the codec, not
a fault.

---

## 15. Output Geometry: AOI, Warp, Edge Blend

Applied per output (not per cue):

- **Area of Interest (AOI)** — crop the rendered output to a sub-region
  (fractions from each edge) for multi-display slicing.
- **Warp** — corner-pin the output (drag the four corners; `Shift+drag` snaps
  to a grid). Copy/paste warp with `Ctrl+Shift+C` / `Ctrl+Shift+V`.
- **Edge blend** — feather each edge for projector soft-edge blending.

Per-*cue* geometry (scale/crop/rotation/offset/keying/colour) lives in the cue
inspector instead (§7).

---

## 16. Overlays: PiP & Lower Thirds

Lower-third and PiP cues fire into an overlay slot independently of the main
program cue, so you can bring a name strap or inset up over whatever is live.
Lower thirds carry two text lines and a background-bar opacity; PiP insets
another cue/camera/NDI source. `G` adds the selected cue as a graphic overlay;
`Backspace` clears all overlays.

---

## 17. Audio

Video, audio, and browser cues play through the focused deck's selected audio
device (`Settings → AUDIO OUTPUT`). UI click sounds use a separate device so
they never touch the programme bus.

### Per-cue audio (inspector → AUDIO)

| Control | Range | Notes |
|---------|-------|-------|
| Gain | −40 … +40 dB | Live trim in the audio thread |
| Pan | full L … full R | Constant-power balance; snaps to centre |
| Mono | on/off | Downmix for mono sources / mono PA |
| Audio fade in / out | follow / none / seconds | `follow` tracks the visual fade; set a length to duck audio independently |
| Normalize (R128) | button | Measures EBU R128 loudness and sets gain for −16 LUFS. Target is always reached; peaks are handled by the deck limiter, not by backing the gain off |
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

### Audio input

`Settings → AUDIO INPUT` opens a microphone or line input by device, with gain,
a clip indicator, mono folding, and a recording bitrate. Routed to the programme
it reaches both the stream and the recording, so a presenter's mic or a desk
feed can be laid against the playback.

### ASIO

On Windows, an ASIO interface can be opened directly for cue audio, with a
real-time callback and a ring buffer. A device whose sample rate does not match
the material is converted rather than refused.

---

## 18. Test Patterns

Pattern cues generate their pixels live and auto-scale to the selected output
raster and refresh rate (unless the project overrides it). All motion is slow,
smooth, and diagonal; full-frame solid colours have no motion option.

- **Pocket Test** — a PM5544-style test card with a bouncing scene-porthole
  ball, a sync beacon, and an audio sync pop at the top of each second (use it
  to dial the A/V delay offset).
- **Test Bars** — motion diagnostics: saturated bars, a bouncing rainbow
  diagonal for tearing, a dissolving checker patch that provokes scaler and
  deinterlace artefacts, a sliding grey block for judder, and a clock.
- **Test Clock** — the sync and latency card. A large seconds counter over a
  frame counter, plus exact timecode in the corner: put it up and photograph two
  screens (or a screen and a downstream recorder) to see whether they agree. The
  circle is drawn from a true pixel radius, so it reads as an egg the moment a
  stretch mode or pixel aspect is wrong, and the scrolling hue band gives
  sub-second phase between captures a few frames apart.
- Standard bars, crosshatch, solids, and gradients.
- **Terrarium** — a hidden ecosystem simulation, unlocked per-save as a secret
  (not selectable until unlocked).

---

## 19. Timecode & Chase

Each deck can **chase** incoming timecode (follow an external master),
**run/generate** timecode, and **trigger** cues at set SMPTE times. MTC and LTC
ingest are available as integration backends (`Settings → Network`). Set the
deck's frame rate and freewheel behaviour in the timecode controls.

---

## 20. Show Files, Bundling & Missing Media

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

## 21. Themes

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

## 22. Remote Control

All remote inputs normalise to plain-text commands.

- **Companion** — port **5510** by default. Use the Deckboy module in
  `companion-module-deckboy/`: as well as sending commands it polls Deckboy's
  state, so Stream Deck keys show cue tally, transport colour, output health and
  a countdown. A one-way *Generic TCP/UDP* mapping is still available in
  `docs/streamdeck/` for setups that can't install a module.
  **Deckboy listens on localhost only until Settings → Network → REMOTE is on** —
  leave it off and only Companion on the same machine can connect.
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

## 23. Reliability & Soak Testing

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

## 24. Keyboard Reference

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

## 25. Command-Line Flags

```
Deckboy.exe --self-check            # verify dependencies + backend wiring
Deckboy.exe --smoke                 # automated smoke test (exit 0 = pass)
Deckboy.exe --soak [minutes]        # long-run stability harness (default 24h)
Deckboy.exe --decode-bench FILE [seconds] [cli]  # decode benchmark; 'cli' forces the subprocess path
Deckboy.exe --sync-pop-test         # verify the pocket-test audio sync path
Deckboy.exe --motion-probe FILE [frames]  # is this clip a usable motion driver?
Deckboy.exe --no-inproc-decode      # force the FFmpeg subprocess decode path
Deckboy.exe --allow-multi-instance  # bypass the single-instance lock (debug)
```

Environment: `DECKBOY_PROJECT` (open a specific show), `DECKBOY_THEME` (force a
colourway), `DECKBOY_COMPANION_PORT` (control port), `DECKBOY_UI_PROFILE=1`
(UI timing + watchdog logs), `DECKBOY_EGRESS_READBACK=sync` (force the plain
synchronous recording readback), `DECKBOY_EGRESS_BENCH=1` (print readback costs),
`DECKBOY_OUTPUT_RENDERER=<driver>` (choose the output window's renderer, e.g.
`gpu`, `direct3d11`, `metal`, `opengl`).
