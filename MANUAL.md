# Deckboy — User Manual

> dot-matrix cue deck

Deckboy is a cue deck for live events. It plays video, stills, live sources and
generated patterns from a keyboard-driven playlist, and sends the result to
fullscreen displays, SRT, RTMP, Blackmagic SDI, SMPTE ST 2110 and Spout — the
same programme to more than one of them at a time — and to NDI. A Stream Deck, Bitfocus
Companion, OSC, MIDI, MIDI Show Control from a lighting desk, Art-Net or LTC
timecode can drive it.

Windows, macOS and Linux run the same core and open the same show file, so the
spare machine in the flight case runs it too. It is a native application on
SDL3 decoding in process through FFmpeg, and on Windows hardware-decoded frames
stay on the GPU and are composited there.

---

## Contents

1. [Concepts](#1-concepts)
2. [Running Deckboy](#2-running-deckboy)
3. [Startup](#3-startup)
4. [Interface Layout](#4-interface-layout)
5. [Cue Types](#5-cue-types)
6. [Importing & Adding Cues](#6-importing--adding-cues)
6a. [Slide Decks, Presenter View & Prompter](#6a-slide-decks-presenter-view--prompter)
7. [The Cue Inspector](#7-the-cue-inspector)
8. [Transport](#8-transport)
9. [Playlists, Loop, Shuffle & Cue Endings](#9-playlists-loop-shuffle--cue-endings)
9a. [The Running Order: Standby, Waits & Continue](#9a-the-running-order-standby-waits--continue)
9b. [Show-Control Cues](#9b-show-control-cues)
10. [Transitions](#10-transitions)
11. [Multi-Deck Operation](#11-multi-deck-operation)
12. [Outputs & Routing](#12-outputs--routing)
13. [Recording](#13-recording)
14. [Per-Cue Effects](#14-per-cue-effects)
14a. [The Code Source](#14a-the-code-source)
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
- **Deck** — an ordered playlist of cues with its own transport, loop and
  shuffle mode, and default cue behaviour. A show starts with one and can have
  up to 16. VJ mode mixes two into one programme, and from v0.99.373 each
  output can show a different deck.
- **Output** — a destination with its own window/compositor: a fullscreen
  display, a DeckLink/Spout/NDI sender, a network stream, or a presenter or
  prompter screen. Every output shows the programme (or, for presenter and
  prompter, a view of it), so one show can drive several outputs at once.
- **Program** and **Preview** — the program monitor shows what is live on the
  focused deck. The cue list selection is what you are *about* to take.

The operating loop is: select a cue → **Take** it (Enter) → it goes live on the
deck's output(s), honouring its fade/transition → it ends per its end action
(stop, hold, loop, or auto-advance to the next cue).

---

## 2. Running Deckboy

Install from the release for your platform, or unpack the portable build and
run it in place. Both carry everything they need. Deckboy finds its `data/`
directory by walking up from the executable, so keep the two together.

To build from source, follow the repository's README.

Three dependencies are loaded only when you use the feature that wants them,
and absent ones cost nothing: the Blackmagic DeckLink driver for DeckLink, Spout
for texture sharing, and WebView2 for browser cues.

**NDI needs the NDI runtime on the machine, not in the download.** NDI input
and output are in every published build; what they load at run time is the
runtime that comes with **NDI Tools**, which is free from the same people who
make NDI. Install that and NDI sources appear; leave it out and Deckboy says
NDI is unavailable rather than failing quietly. `--self-check` reports what it
found: `ndi-sdk: headers detected` means NDI is in the build. The NDI *tally
trigger* works whether or not the runtime is there.

---

## 3. Startup

Deckboy opens on a startup card — the wordmark, the version, and a boot log
while the backends come up. **Enter** dismisses it.

It then reopens the last show. To open a different one instead, pass it on the
command line or set `DECKBOY_PROJECT`; `DECKBOY_THEME` forces a colourway.
Either of those skips the startup card as well.

Deckboy refuses to start twice, so a stray double-click cannot take a second
copy of the show live. `--allow-multi-instance` lifts that, for debugging.

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
- **Cue Inspector** (right): every setting for the selected cue, in collapsible
  sections.
- **Monitors window** (separate): per-output preview and routing.

Two dividers rebalance the layout. The vertical splitter sits between the
program area and the inspector. The horizontal grip sits in the gap under the
program monitor: drag it up to shrink the preview and grow the timeline lanes,
down to give the height back.

---

## 5. Cue Types

| Type | Source |
|------|--------|
| **Video** | A video file (any FFmpeg-readable container/codec, incl. HAP, ProRes, H.264/265 hardware-decoded) |
| **Image** | A still (held for a set duration or until taken away) |
| **Pattern** | A generated test pattern (see §18) |
| **Browser** | A live web page, rendered inside the programme rather than in a browser window on your desktop |
| **Window / Screen** | A window on this machine, or a whole screen. A window arrives at full resolution whatever the display scaling is set to, follows the window as it moves and resizes, and is not interrupted by anything in front of it |
| **Camera** | A capture device |
| **Syphon / Spout** | A shared GPU texture from another app, through Spout on Windows. Syphon receive on macOS is not built yet |
| **Stream (SRT)** | A live network input — `cue.path` is the full URL (`srt://`, `rtmp://`, `rtsp://`, `udp://`) |
| **NDI Source** | An NDI receive input — `ndi://SOURCE_NAME` |
| **DeckLink Source** | A Blackmagic card's SDI or HDMI input, captured through the DeckLink SDK rather than through FFmpeg |
| **PiP** | Picture-in-picture overlay of another cue/source |
| **Lower Third** | Text overlay bar |
| **Composite** | A multi-slot scene (2-up, quad, 70/30, etc.) |
| **Audio** | An audio-only file with a waveform lane |
| **Tone** | A generated audio test tone, with optional on-screen diagnostics — and, with a chip selected, a playable 2A03 or FDS voice driven from MIDI or the computer keyboard |
| **Timer** | A stage/speaker countdown with its own clock, thresholds, chimes and messages |
| **Video Synth** | Generated picture — oscillators, feedback, glitch stack, text mode, sprite sets |
| **Code** | A live-coded picture: an expression evaluated per pixel, edited while it runs (see §14a). It is a Pattern cue underneath, so anything true of patterns is true of it |

These cues play nothing themselves. They act on other cues, or send a message
to other equipment, when they are taken (*v0.99.373 and later.*; see §9b):

| Type | What it does on GO |
|------|--------------------|
| **Target** | Starts, stops, pauses, resumes, loads, arms or disarms another cue |
| **Fade** | Ramps a deck's picture, a deck's volume or the master dimmer to a level over time |
| **Master** | Fires an assigned cue on each of several decks at once |
| **MIDI** | Sends a note, control change, program change, MIDI Show Control GO / STOP / RESUME, or raw bytes |
| **Network** | Sends an OSC message, a UDP datagram or a line of TCP |
| **Timecode** | Starts, stops or jams the LTC generator |
| **DMX** | Sends Art-Net channel levels, with a fade time |
| **Script** | Runs lines of Deckboy's own control protocol |

All of them are on the `SOURCE` menu.

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

### A playlist that fills itself

**Settings → System → WATCH FOLDER** points the focused playlist at a folder.
Anything playable dropped in there becomes a cue, by itself, while the show is
running — which is what you want when somebody keeps putting new VTs on the
NAS during rehearsal.

Each playlist can watch its own folder, so the VT drop and the stings can be
different places. The card names the playlist it is about; switch playlists
with the tabs and set another.

Three things it deliberately does:

- **It waits for the copy to finish.** A large file appears in the folder the
  moment the copy starts, and a cue built from a half-written file fails on
  air. Nothing is taken until its size has stopped changing, which costs a
  couple of seconds and removes the whole problem.
- **It never takes the same file twice**, including across reopening the show.
- **Deleting a cue is final.** Removing something from the playlist does not
  bring it back on the next scan; you deleting it was a decision.

Over the wire: `WATCH` to see what is being watched and whether the scan is
running, `WATCH <deck> <folder>` to set one, `WATCH <deck> OFF` to stop.

---

## 6a. Slide Decks, Presenter View & Prompter

A talk is a show like any other, and Deckboy runs one without a second
application on the machine. A deck imports as cues, the speaker gets their own
screen, and the reader gets a prompter — all three out of the one show file.

### Importing a deck

A PDF imports as **one image cue per page**, rasterised once at import and never
touched again. That is the point rather than a shortcut: nothing in a live show
should depend on a document renderer being fast, being installed, or deciding to
reflow a page halfway through the keynote. Once the pages are stills they behave
like every other cue — they take, they fade, they carry effects, they crossfade
to the next one — and a presenter's clicker walks them with Page Down.

Each platform uses the renderer it already has, so nothing is bundled:

| Platform | Engine | Also used by |
|---|---|---|
| Windows | `Windows.Data.Pdf` | Edge |
| macOS | CoreGraphics `CGPDFDocument` | Preview |
| Linux | `pdftoppm`, from `poppler-utils` | the desktop's own PDF viewer |

Linux is the one platform where this is a separate tool, and therefore the one
where it has to be installed; Deckboy says so plainly if it is missing rather
than refusing the file without a reason.

Pages are rendered to a fixed **target width**, chosen for the largest output
the application supports rather than the output currently configured. The three
engines measure a page in three different units, so the same "2x" would produce
a different raster on each platform, and an operator may change the output after
importing anyway. The same deck therefore imports identically on every machine.

### PowerPoint, Keynote and OpenDocument

`.pptx`, `.ppt`, `.key` and `.odp` are not rasterised directly. Deckboy asks
whatever already owns the format on that machine to export a PDF, then
rasterises that:

| Platform | Preference order |
|---|---|
| Windows | PowerPoint itself, then LibreOffice |
| macOS | Keynote for `.key`, then LibreOffice, then PowerPoint if present |
| Linux | LibreOffice |

The format's owner goes first because it is the authority on its own format. A
half-right renderer that puts a slide's type in the wrong place is worse on a
show day than an honest refusal, so where none of them is installed the operator
is told which one to install.

**Exporting to PDF flattens builds and drops transitions.** That is a property
of the export, not of Deckboy, and no PDF-based route avoids it: a PowerPoint
deck that animates arrives as static slides. Keynote can export one page per
build stage, and those come through as one cue per stage, which is usually what
an operator wants. For a PowerPoint deck that genuinely has to animate, capture
it live with a **window-source cue** instead and drive it in PowerPoint.

**Speaker notes** are read out of a `.pptx` — the file is a ZIP with the notes
as XML inside — one entry per slide, so they arrive attached to the cue that
shows the slide.

### Presenter view

Presenter view is an **output type**, not a window: it is assigned to a display
the way a programme output is, so the speaker's laptop screen or the confidence
monitor at the lectern is simply another output of the show.

Four layouts:

| Layout | Shape |
|---|---|
| `wide` | current large, previous and next stacked beside it, notes below |
| `filmstrip` | previous / current / next across the top, notes large below |
| `notes` | notes dominate, the three pictures on a thin strip above |
| `custom` | wherever the operator put the panels |

The three named layouts **reflow**: switch a panel off and the others take its
room. A custom layout does not, because it is an arrangement somebody chose and
rearranging it under them would be a bug rather than a courtesy.

Every panel can be switched off independently — live picture, previous, next,
notes, clock and timers. Turning the three pictures off gives the notes the
whole screen, which is what somebody reading a long script from a lectern
actually wants. **Notes share** scales how much of the screen the notes take
relative to the pictures, on top of whatever the chosen layout already thinks is
sensible, and **notes scale** sets the type size — `1.0` is the size the rest of
the interface uses, and the default is `1.4`, because notes are read from a
lectern rather than from a desk. Background, ink and accent are set as hex
colours, since a presenter screen is often somebody else's laptop in somebody
else's room and "make it readable in here" is a real request.

Panel positions in a custom layout are stored as **fractions** of the area
between the header and the footer, so a layout arranged on a 1080 laptop is the
same shape on the 4K screen it ends up on.

### Note builds

A cue's notes split on a line that is exactly `---`, and the presenter advances
through those parts **without changing the slide** — so a long note is read at
the speaker's pace instead of arriving all at once.

There is an option to make the ordinary NEXT action spend the remaining builds
before it advances the cue, which is how a slide clicker behaves in every other
deck a presenter has used. It is off by default, because it changes what the
transport does.

### Prompter

The prompter is the talent's screen, and it is a different job from the
presenter's: one person reading out loud under a piece of glass.

- **The script.** Left empty, the prompter follows the live cue's notes, which
  is what a deck-driven show wants. A filled-in script is for a talk with no
  slides, or one whose slides are somebody else's problem.
- **Mirroring.** On by default horizontally, because a teleprompter's glass
  reverses the picture on its way to the reader. Vertical mirroring exists as
  well, since the beamsplitter can sit above or below the lens.
- **Pace** is set in **lines per minute** rather than pixels per second — a pace
  belongs to the reader, and it has to mean the same thing when the type size or
  the screen changes. The default is 140.
- **The reading line** sits a fraction of the way down the screen (0.42 by
  default) and the text scrolls up *through* it, so the words being spoken are
  always in the same place. That is the whole ergonomic point of a prompter, and
  the line itself can be drawn or hidden.
- Type is much larger than a presenter's — the default scale is 2.6 — and
  background, ink and accent are hex colours as above.

Whether the prompter is running is saved with the show, so a talk reopens armed
the way it was left.

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

## 9a. The Running Order: Standby, Waits & Continue

*v0.99.373 and later.*

Everything in this chapter defaults to what Deckboy always did, so a show made
before it behaves exactly as before: no waits, nothing follows anything, and GO
takes the selected cue. The controls are in the inspector's **SEQUENCE**
section, on every cue type.

### Standby

The **standby** is the cue GO will fire, kept apart from the selection. The
selection is where you are looking: what the inspector shows and what the
arrow keys move. The standby is the running order: what happens next. Keeping
them apart means clicking a cue to check it during a show does not change what
the next GO does.

Set it with the **standby** row (it reads `THIS CUE` on the cue that has it).
While a standby is set, **Space is GO**: it takes the standby cue and moves the
standby down to the next armed cue. At the end of the list the standby clears
rather than wrapping to the top, so the last GO cannot restart the show. With
no standby set, Enter and Space work as they always have.

### Pre-wait, post-wait and continue

- **pre-wait** — how long after GO this cue starts.
- **continue** — whether the next cue goes by itself, and from when:
  - **off** — the operator fires the next one.
  - **from start** — the post-wait is counted from when this cue *starts*, so a
    sequence is laid out in time from a single GO.
  - **from end** — the post-wait is counted from when this cue *finishes*, for
    "and then the next thing", however long this one took.
- **post-wait** — the delay before the continue fires.

A continue is about the next cue and fires whether or not this cue has ended.
That is what makes it different from the **Auto-Next** end action in §9, which
is about what this cue does when its media runs out.

### Armed

A cue that is not **armed** stays in the list with all its settings, does
nothing, and is stepped over by GO and by the standby. The row is washed out so
the list shows it. Disarm a cue to skip it tonight without deleting it.

### Audition and preload

- **audition** plays the selected cue to the control window's preview only.
  The outputs keep what they have. Use it to check a cue during a show without
  the room seeing it.
- **preload** racks the selected cue on its deck, paused and held off the
  outputs, with decoding already running, so taking it starts with no spin-up.

### CHECK: everything wrong, before doors

When a show has problems, a **CHECK** button beside `RELINK` shows how many.
Click it to walk to each one in turn. It finds:

- media that is missing
- a goto that points at a cue that no longer exists
- a master or target cue whose destination has gone, or that names a deck
  that does not exist
- a MIDI cue with nothing to send, a DMX cue with no channels or with a
  channel list that cannot be read, and a script cue with no lines
- a network cue with no host, a host that is not an IPv4 address, or an OSC
  address that does not start with `/`

---

## 9b. Show-Control Cues

*v0.99.373 and later.*

These cues carry no media. Each one does its job when it is taken, which means
it can be fired by GO, by the standby, by a continue, by a master cue or by
another controller. Every one has a **fire** (or **send**) row in the inspector
to try it without taking the cue. Add them from the `SOURCE` menu.

### Target cue

Acts on another cue: **Start**, **Stop**, **Pause**, **Resume**, **Load**
(stand it by without firing it), **Arm** or **Disarm**. Pick the **deck** and
the **cue**. Stop, pause and resume act only when the named cue is the one on
air, so a target cannot stop something else by mistake.

### Fade cue

Ramps a level over time: a deck's **opacity**, a deck's **volume**, or the
**master dimmer**. Set where it ends (**to**), how long it takes (**over**;
zero is a snap), and the **curve**: linear, ease in, ease out or S-curve. Turn
on **then stop** to stop the deck when the ramp lands, which is the usual
"take it down and stop it".

### Master cue

Fires an assigned cue on each of several decks at once. For each deck, pick
the cue to fire, or bypass that deck. A master cannot fire another master.

### MIDI cue

Sends a **note**, a **control change**, a **program change**, **MIDI Show
Control** GO, STOP or RESUME, or your own **bytes** in hex (for example
`90 3C 7F`). Choose the output **port**. If a named port is missing, Deckboy
reports it rather than sending to whatever port it finds. **channel** counts
1–16, as a desk does. For MSC, set the **device** (127 addresses every device
on the line) and the **cue number** on the other desk, for example `12.5`.

### Network cue

Sends **OSC**, a **UDP** datagram or a line of **TCP** to an IPv4 address and
port. OSC and UDP are fire and forget. TCP has to connect, so it runs off the
show thread and GO never waits for it. Hostnames are refused on purpose: a DNS
lookup can block, and GO must not.

### Timecode cue

Starts the LTC generator, stops it, or **jams** it to a time, so the cue list
can run a timecode sequence (see §19).

### DMX cue

Sends Art-Net channel levels with a **fade** time. Write the levels as
`channel=level`, separated by commas, with ranges allowed:
`1=255, 10-14=64`. Channels count from 1. Set the **universe** (Art-Net port
address, 0–32767) and the destination: an IPv4 address, or
`255.255.255.255` to broadcast. **blackout** zeroes every channel on every
universe the session has touched. Deckboy is not a lighting console: this is
for house lights and practicals, not a rig.

### Script cue

Runs lines of Deckboy's own control protocol (§22), one command per line, with
`#` for comments. One script cue can fire a master, send MIDI, jam timecode and
ping a media server. Scripts can run other script cues, up to four deep, and
then Deckboy stops and says so rather than looping forever.

---

## 10. Transitions

Cue-to-cue transitions are set at the deck level and can be overridden per
cue. There are **thirteen** styles:

| Style | Token | What happens |
|-------|-------|--------------|
| Cut | `cut` | Instant |
| Crossfade | `crossfade` | Alpha blend over the transition time |
| Dip to black | `dipblack` | Down to black, then up |
| Dip to white | `dipwhite` | The same through white — a flash rather than a breath |
| Push left / right / up / down | `pushleft` … | The incoming cue shoves the outgoing one off the raster |
| Wipe left / right / up / down | `wipeleft` … | A hard edge travels across, revealing the incoming cue |
| Iris | `iris` | The incoming cue opens from the centre |
| Portal | `portal` | The Portal's blobs melt open through the old picture, with the neon rim on every edge, until the new cue is all that is left |

A **push** moves both pictures; a **wipe** moves only the boundary. They look
alike in a still and nothing alike in motion.

Set the deck default in the playlist settings; override on a cue in its
PLAYBACK section. Over the wire, `TRANSITIONSTYLE <token>` sets the style and
`TRANSITION <seconds>` sets the time. The incoming cue's fade-in is the visible ramp on the output
path. `next xfade` on a cue toggles whether a transition is used when
auto-advancing into the next cue.

---

## 11. Multi-Deck Operation

A show starts with **one deck** and can have up to **16**. VJ mode, below, mixes
two of them into a single programme with a crossfader. From v0.99.373 **each
output can show a different deck**: choose the deck on the output's card in
`Settings → Video Outputs` (§12). While a show has more than one deck, the
window is titled **Super Deckboy**. The focused deck is the one the keyboard and
transport act on, and selecting a deck also moves the focused output to the one
that deck plays on.

Two decks cover the great majority of shows: a programme, and something held
ready behind it.

### VJ mode

A toggle. Off, Deckboy is a cue deck and every show behaves exactly as it always
has. On, **two decks run at once and a crossfader decides what the audience
sees** — and it is impossible to enter by accident: the whole window is edged in
a colour used nowhere else, breathing on the beat, and a bar across the program
column carries the controls.

- **Crossfader** between deck A and deck B, folded into the opacity each deck
  already had — so a deck faded down or mid cue-fade stays faded down.
- **Blend**: dissolve, add, screen, multiply, lighten, darken, subtract,
  undercut, infiltrate or ember. On a dissolve both decks fade (they are
  drawn over black, so holding A up until B covered it would be a wipe); on the
  others the base stays at full and only the incoming deck rides the fader.
- **Tap tempo**, averaged over recent taps rather than the last interval —
  nobody taps evenly. Taps more than two seconds apart start again.
- **Quantised takes** hold until the next beat. The point of tempo in a video
  mixer is not that anything moves by itself, it is that **what you do lands on
  the music**.
- Both playlists are on screen side by side, each headed with which side of the
  crossfader it is, and A, the mix, and B each get their own monitor — a
  crossfader you cannot see both sides of is a blind control.

`VJ ON|OFF | MIX <0-1> | BLEND [mode] | TAP | BPM <n> | CLOCK <on|off> |
QUANTISE <on|off> | DECKS <a> <b> | STATUS` over the wire, because a fader is
the one control nobody wants to reach for with a mouse.

---

## 12. Outputs & Routing

Outputs are managed in the Monitors window and `Settings → Video Outputs`. A
show can drive several at once, each with its own settings. From v0.99.373 each
output has a **Source deck**, the deck whose picture it carries, and
**ADD OUTPUT** on the Video Outputs card adds one and lets you choose its deck.
Before that, every output carried the first deck. Each output is one of:

- **Window** — a fullscreen (or windowed) display. Toggle the output window
  with `N`, fullscreen with `F`. Fullscreen recovery automatically re-raises a
  program output that gets minimised or lost, with strike-based backoff.
- **Stream** — push SRT/RTMP to a URL. It can run while the programme is
  recorded.
- **Presenter** — the speaker's screen: live slide, previous, next, notes and
  clock ([Presenter view](#presenter-view)).
- **Prompter** — the talent's scrolling script ([Prompter](#prompter)).
- **DeckLink** — SDI/HDMI out via a Blackmagic card.
- **Spout** (Windows) — share the output as a GPU texture to another app. Syphon
  on macOS is not built yet.
- **NDI** — network video send, optionally with a separate key/alpha source.
  Needs the NDI runtime on the machine (see *Running Deckboy*).

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
or click the value to type an exact number), the effect's own parameters
underneath, and a row for changing the effect, moving it up or down, and
removing it. **copy chain** / **paste chain** move a whole look between cues
without dragging geometry, fades or crop along with it.

Effects work on **every kind of cue** — video, stills, patterns, cameras, NDI,
streams and the code source. Everything costs nothing at amount zero, and
**bypass** is not the same as amount zero: turning an effect down loses the
setting you spent time on, bypass takes it out of the chain and gives it back.

### The stack

| Effect | What it does |
|--------|--------------|
| invert, posterise, solarise, threshold | Level shaping, each with a pivot and a channel skew |
| vignette, scanlines, grain | The classic framing and texture set |
| RGB split | Channel offset with an angle |
| temporal dither | See below |
| pixel sort, block glitch, datamosh | Glitch: sorted runs, torn bands, and real codec smear |
| polar warp, luma displace, ripple, kaleidoscope | Geometry |
| lightspeed | Relativistic aberration: field-of-view compression and Doppler shift |
| dye advect, reaction bloom | Fluid: curl-noise advection, and Gray–Scott growth |
| caustics | Refraction *and* the light gathering — see below |
| feedback | A controlled camera-into-monitor loop |
| feedback bloom | The same loop, but the echo turns colour and is warped on every pass, so both compound: iridescent trails that melt. Bounded the same way feedback is |
| motion puppet | Driven by another clip's movement |
| slit scan | One open slit crosses the frame, smearing a long moment across it. Narrow is a scanner; wide is barely an effect |
| motion mosh | Holds the previous frame and the smear between them, so a held cue smears into itself instead of sitting still |
| ferrofluid | The highlights lift away from the surface into spikes, as iron filings do in a field |
| shatter | The picture breaks into shards that slide and turn. Small is frosted glass; large is a dropped plate |
| edge ignite | Edges catch and burn, the flame guttering frame to frame. Low sets the whole picture alight, high only the hardest lines |
| relight | Brightness is treated as height and lit from the side, with the light walking around the frame |
| depth split | Brightness is read as nearness and the two eyes disagree, with a slow rock that makes the depth read without glasses |
| schlieren, chladni, wavefront, crystallise, night eyes, grain flow | See below |

There are **36** of them. Timed one at a time at 1080p on a laptop processor,
the heaviest measured — wavefront — takes about three-quarters of a 60fps frame,
and most take under half. `--effect-bench <token> [WxH]` reports what any of
them costs on your own machine and at your own raster.

### The six that are not in anything else

**Schlieren** is the instrument physicists photograph air with. You cannot see
a shockwave or the heat off a road, but light bent by a density gradient can be
passed or blocked by a knife edge at the focus, which turns an invisible
gradient into brightness. Here the picture is the density field. Rotating the
knife changes *which features exist at all* — gradients along the edge miss it
entirely — and that is what makes it read as an instrument rather than a filter.

**Chladni** is the shape a sound makes. Sand on a bowed metal plate runs away
from everything that is moving and piles up on the lines standing still. Your
picture is the sand. The two mode numbers are the note: whole numbers give the
clean classical figures, and between them the plate is being driven at a
frequency it does not want.

**Wavefront** solves the actual wave equation, seeded from the picture's own
brightness — so unlike every sine-based ripple it has *inertia*. Waves leave
their source and keep going, pass through each other and interfere, and reflect
off the edges of the frame and come back.

**Crystallise** is grain growth, not a mosaic. Crystals nucleate at scattered
points and grow until they collide, so the cell a pixel lands in is the one
whose seed reached it first — and because the seeds grow at *different speeds*
the result is the irregular shard structure of a polished metal section rather
than a honeycomb. Each grain gets a facet normal, so the light catches it.

**Night eyes** is your own retina. Rods are fast and colour-blind, cones are
slow and need light, so in the dark the brightness runs at full speed and the
**colour lags behind it**: move something and it goes grey as it moves, its
colour catching up a moment later. The purkinje control is the other half — as
the rods take over, sensitivity slides toward blue, which is the real reason
night looks blue.

**Grain flow** smears the picture along its own grain. The direction comes from
the structure tensor — the direction in which each neighbourhood changes least,
which is *along* a feature rather than across it — so strokes run along a hair,
around a jaw, down the length of a shadow. Flat areas are left alone; turn
"across the grain" up and it combs the picture apart instead.

### How many effects is too many

A cue holds up to **twelve** effects, but the count is the wrong thing to
watch: a dozen cheap ones are free and four expensive ones at 4K are not. So
the EFFECTS section shows what the chain **actually costs per frame**, measured
on your machine at your raster, once the cue has been live. A 60fps frame is
16.7ms; over that, it says so.

If you do go over, nothing breaks and nothing drifts. **Audio is the master
clock**, so sound continues in real time and the picture slaves to it — you
lose frames, not sync, and the show stays where it should be. The output fps
counter (toggle it on the output bar) and `output_fps` in a `STATUS` reply both
show it happening.

Cheapest ways back under budget, in order: drop the output raster (almost
everything here scales with pixel count), bypass rather than delete while you
find the culprit, and check the expensive ones first — `--effect-bench <token>
3840x2160` will tell you what any of them costs on your hardware.

### An LFO on any parameter

Every parameter — and the effect's amount — has a **`~`** at the right of its
row. Switch it on and that parameter moves on its own, with shape, rate and
depth on the line underneath.

- **Shapes**: sine, triangle, saw, ramp, square, and sample-and-hold (one random
  value per cycle, held — and repeatable, so the same moment of the show always
  gives the same value).
- **Locked to the tempo** or free-running. Locked, the cycle is measured in
  beats and follows the VJ tap tempo, so what moves is on the music.
- The swing is **centred on the value you set**, so switching an LFO on never
  jumps the picture — it starts from where the parameter already was and
  averages back to it.

`FX LFO <n> <A-E> on|off|shape|rate|depth|phase|sync|beats [value]` does the
same over the wire, where `E` is the amount.

### Two worth trying on their own

**Temporal dither** quantises hard to a tiny palette but advances the dither
pattern every frame, so at 60Hz your eye integrates shades that are not in the
palette at all — and it freezes into a visible checkerboard the moment you pause
the deck. The still and the moving image are deliberately different pictures.

**Caustics** computes what water does to the *light*, not just how it bends the
picture. Where neighbouring rays are pushed together the brightness piles up,
and those bright filaments are the moving net you see on the floor of a pool.
Displacement alone is a wobble; the focusing is what the eye reads as water.

### Motion puppet

**Motion puppet** drives this cue's pixels with a *different* clip's movement.
Choose a driver in the EFFECTS section: that clip is decoded only for the
per-macroblock motion vectors its codec already measured — its pictures are
never shown — and those vectors displace this cue. A camera feed can be
puppeteered by a crowd scene.

Its **memory** and **spring** decide how the displacement accumulates: memory is
how much each frame's motion adds to what is already there, spring how fast it
returns to rest. Both are needed — memory alone runs away, a return alone never
builds. At memory 0 it follows a single frame's vectors, which is what it did
before it had the control.

A driver is only as good as its motion. `Deckboy --motion-probe <file>` reports
what a clip offers before you commit to it: a mostly static clip moves a couple
of percent of its cells and will do nothing visible, while something with
whole-frame movement moves half of them and is violent. A keyframe carries no
vectors at all, so the picture is briefly left alone — that is the codec, not
a fault.

---

## 14a. The Code Source

**SOURCE → Code (live expression)** makes a cue whose picture *is* an
expression, evaluated once per pixel and edited while it runs.

    sin(x*8+t)*0.5+0.5, sin(y*8+t*1.3)*0.5+0.5, sin((x+y)*8-t)*0.5+0.5

One expression, or three separated by commas for red, green and blue. The
values available are `x` `y` (0-1 across the frame), `cx` `cy` (-1..1 from the
centre), `r` (distance from the centre), `a` (angle) and `t` (seconds), with
`sin cos tan abs floor fract sqrt min max mod pow atan2 step clamp mix length
smoothstep sign exp log atan if noise` and `pi` to build from.

**Name a value and reuse it.** Any line before the last one names something,
and the lines below it can read that name. A distance used three times is then
computed once:

    d = length(cx, cy);
    fall = exp(-d*d*4);
    fall, fall*0.7, fall*0.35

**A fourth expression is alpha, and that makes the source a *shape*.** With
three expressions the cue fills the frame, the way it always has. With four,
the last one says how opaque each pixel is — so the cue draws *over* whatever
is beneath it. Put one on a layer above a camera and it is an overlay, not a
background:

    d = length(cx, cy);
    1, 0.9, 0.3, smoothstep(0.42, 0.38, d)

That is a soft-edged amber disc over the picture, and nothing else.

`noise(x, y)` is a smooth random field, for the shapes algebra cannot reach.
Stack a few at doubling frequencies and you have cloud:

    f = noise(x*4+t*0.06, y*4) + noise(x*9, y*9)*0.5 + noise(x*18, y*18)*0.25;
    f/1.75, f/1.75, 1, smoothstep(0.55, 0.75, f/1.75)

The cue inspector's **CODE** section opens the editor. It is syntax coloured —
functions, values, numbers, brackets, operators and the commas that split the
channels each have their own colour, and **a name the compiler will refuse is
red while you type it**. Click into the text to place the cursor; click any
value or function to insert it (a function arrives with its brackets and the
cursor already inside). Twenty-one worked examples are one click each — the
last five (**gem**, **gem cluster**, **flash**, **cloud**, **ghast**) are
shapes with alpha, meant to sit over a picture — and a friend in the corner
tells you what the name under your pointer does.

**A compile error never blacks the output.** The cue keeps drawing the last
expression that worked and the error appears in the editor. Someone editing
live is mid-keystroke most of the time.

Division by zero, mod by zero and the square root of a negative are all bounded
rather than producing infinities, because an operator typing at speed will
produce all three.

Not GLSL, deliberately: Deckboy draws through SDL_Renderer, whose backend is
D3D11, D3D12, Metal or OpenGL depending on the machine, and accepting GLSL at
runtime everywhere would mean bundling a shader compiler to run arithmetic that
fits in a few hundred lines. It is evaluated on the CPU, which is viable for
the same reason the effect stack is: the frame splits across cores.

`CODE GET | CODE SET <expression> | CODE EDIT` over the wire.

Two command-line tools go with it, neither of which needs a window or a GPU:
`--code-dump "<expression>" out.ppm [WxH] [seconds]` renders one frame (or
`@file` to read the expression from a file, which avoids arguing with a shell
about semicolons), and reports how much of the frame the shape covers.
`--code-check` asserts the language itself against expected values.

### Panels built for a vMix rig

**Settings → Network → vMix API** makes Deckboy answer the two APIs vMix
speaks: the HTTP one on **8088** and the text protocol on **8099**. Those are
vMix's own ports, so a Stream Deck plugin, Companion module, touch panel or
show-control system already set up for a vMix rig drives Deckboy with nothing
changed and nothing written.

How the two desks line up:

| vMix | Deckboy |
|------|---------|
| Input | a **cue**, numbered straight through every playlist in order |
| Active | the live cue of the focused playlist |
| Preview | the selected cue of the focused playlist |
| Mix *n* | playlist *n*. vMix has four; Deckboy reports all of its own, and a panel that only knows mix1–mix4 reads the first four |

Inputs are cues rather than playlists because a panel's tally light wants to
say *this clip is on air*, which is what every vMix surface is built around.

**HTTP:** `GET /api` returns the state document. `GET /api?Function=Cut`, and
so on, does the thing and returns the same document.

**TCP:** `TALLY`, `FUNCTION`, `XML`, `XMLTEXT`, `SUBSCRIBE`, `UNSUBSCRIBE`,
`ACTS`, `VERSION`, `QUIT`. `SUBSCRIBE TALLY` pushes a new tally string
whenever it changes, which is what a panel should use instead of polling.

Functions understood: Cut, Fade, Play, Pause, Stop, PreviewInput,
ActiveInput, FadeToBlack, StartRecording, StopRecording, SetMasterVolume,
SetVolume, NextItem, PreviousItem, Restart.

**A Function this desk has no equivalent for is refused** — 404 over HTTP,
`FUNCTION ER` over TCP. vMix itself answers success to a Function that
failed; Deckboy does not, because a surface reporting that a cue was taken
when it was not is how a show goes dark with every light green.

**There is no password on it**, and like the control port it binds to
localhost unless **Listen on** is set to all interfaces. vMix's own HTTP API
offers BasicAuth and this does not yet. On a venue or hotel network, put
Deckboy behind a firewall rule.

Over the wire: `VMIX`, `VMIX ON|OFF|TOGGLE`, `VMIX PORTS <http> <tcp>`.

### Caption formats

Captions load from **SubRip** (`.srt`), **WebVTT** (`.vtt`), **SCC**
(`.scc`) and **TTML/DFXP** (`.ttml`, `.dfxp`) — whichever a job arrives in.

SCC is the broadcast one: not text with timestamps but the CEA-608 byte pairs
an encoder would put on line 21, written as hex against drop-frame timecode.
Deckboy decodes it, including the distinction between drop-frame and non-drop
— the two differ by 3.6 seconds an hour, which is a caption on the wrong shot.

`SUBTITLE CONVERT <path>` writes the cue's captions out again as `.srt` or
`.vtt`, so moving between formats needs nothing else installed.

### Text mode

Available two ways: as the video synth's own mode, and as the **TEXT MODE**
effect, which puts the same character grid on any cue at all -- a clip, a
capture card, a camera, a browser cue or a still. Adding the effect gives the
cue its own TEXT MODE section in the inspector with the same rows the synth
has, and the effect's four parameters (columns, corruption, glyph set, ink)
ride on top of them so the useful ones are on faders and can take an LFO.
Its amount is a MIX rather than a switch: at 1.0 the grid replaces the
picture, and part way it sits over the original.

The video synth can render as a character grid instead of as pixels, with a
16-colour indexed palette and its own corruption. Alongside the built-in glyph
sets, two settings make the field yours:

- **custom glyphs** — the characters the picture is built from, darkest first.
  Two characters gives binary rain; a word gives that word as texture;
  box-drawing pieces read as a schematic. Empty uses the chosen glyph set.
- **phrases** — words separated by `|`, one showing at a time, landing
  somewhere new each time it moves. **phrase hold** is how long each one stays;
  zero hides them without losing the list.

The corruption still overwrites a phrase when it lands on that row, which is
the intent: a terminal that can be corrupted can be corrupted mid-sentence.

`ASCII ON|OFF|TOGGLE | ASCII GLYPHS <chars> | ASCII PHRASES <a|b|c> |
ASCII HOLD <seconds>` over the wire; `GLYPHS` and `PHRASES` with no argument
clear them.

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

### Per-cue audio effects (inspector → AUDIO FX)

Each cue carries an ordered chain of up to **eight** audio effects, arranged
like the picture effects and saved with the show. It runs per sample on the
audio thread, between the cue's gain and the deck limiter.

**Amount always means "more of this, less of the original."** Dry/wet for the
shaping effects, gain reduction for the dynamics, and a *send* for the delay and
the reverb — a delay treated as dry/wet would play silence on a cue shorter than
its own delay time.

| Effect | What it does |
|--------|--------------|
| High pass, Low pass | Corner-frequency filters |
| Tilt EQ | One control from dark to bright, pivoting in the middle |
| EQ band | One parametric band: frequency, gain, width, and a shape that makes it a bell or either shelf. It arrives **flat**, the way a band on any desk does, so adding one changes nothing until you move the gain. Stack three or four for a full strip — the order is the order you drag them into |
| Compressor | Threshold, ratio, attack, release. The backward-compatible ratio is 1:1 — a compressor that does not compress — so a cue saved before the control existed still sounds the way it did |
| Gate | Shuts the tail off below a threshold |
| Delay | A send, with time and feedback |
| Reverb | A send, with size and damping |
| Width | Narrows or widens the stereo image |
| Binaural | Places the source around the listener's head |

#### Your own plugins

Deckboy hosts **VST3** effects and instruments, so a reverb you already own or
the channel strip your mix is built around can sit in the same chain as
Deckboy's own effects, in whatever order you put them — a gate before your
reverb is a different sound from a reverb before your gate.

Add a **Plugin** effect and pick from what is installed on the machine. The row
names the plugin, and the four rows beneath it carry that plugin's own first
four automatable controls, under the plugin's names for them. Everything else
the plugin was set to is saved with the show and comes back with it.

Plugins are opened when the cue is prepared, never between two buffers, and each
one runs against a time budget: a plugin that cannot keep up with the block it
was handed is taken out of the chain and the operator is told, rather than
clicking through the PA. A show that opens on a machine missing one of its
plugins keeps the slot, the settings and the name, and says which plugin it
cannot find.

`--plugins` lists what the machine has and the folders searched — run it before
believing "Deckboy cannot see my reverb". `--plugin-chain-check` runs one in a
real chain and measures that it, and its controls, change the sound.

Deckboy's releases host VST3. VST2 is not included: Steinberg withdrew that SDK
in 2018 and licenses it to no new host. The source carries an optional VST2
backend for anyone who holds a licence of their own and builds it themselves.

**Instruments can be played.** An instrument in a cue's chain receives notes
from the computer keyboard, from a MIDI keyboard, and from a controller over the
network — the same three ways Deckboy plays its own chip synth. Put a Plugin
effect on a tone cue, choose an instrument, and the keyboard plays it.

Companion: `AUDIOFX <slot> PLUGIN <name>`, and `TONECUE` to make the cue to put
it on.

#### The five that need the deck

A hosted plug-in receives a buffer of samples and nothing else. That is not a
limitation anyone chose; it is what the interface *is*. Deckboy holds the
picture and the sound in one object, so five of its own effects can read the
frame they are playing under — which nothing hosted in a mixing desk can do.

| Effect | What it reads |
|--------|---------------|
| **Picture** | The cue's own video drives the filter: the shot itself becomes the control signal |
| **Placement** | Where the picture sits on the raster is where the sound sits in the room — move the shot, the sound moves with it |
| **Seam** | The approaching end of the cue resolves the tail, so the outgoing sound lands *with* the cut instead of being chopped by it |
| **Frame lock** | Granular stutter quantised to the video frame period rather than to a tempo you guessed |
| **Suspend** | A held cue keeps its room tone instead of stopping dead |

Every one of them has a neutral setting that passes audio through unchanged, and
when a cue cannot supply what an effect needs — an audio-only cue has no picture
— the inspector row says so rather than passing through in silence.

`--audio-fx-check` runs the whole chain headlessly and reports what each effect
does to a known signal. Its first run found four real faults, including a delay
whose output was silence.

### A/V delay offset

`Settings → AUDIO OUTPUT → A/V delay` holds all audio back 0–1000 ms before the
device, to line Deckboy up with a lagging display or PA DSP. Video anchors to
the undelayed timeline so the offset is a real skew at the output.

### Multichannel output routing

`Settings → AUDIO OUTPUT → Outs` opens the deck device with 2, 4, 6 or 8
channels, and from v0.99.373 with 16, 32 or 64, which is what makes a Dante
Virtual Soundcard or a large ASIO interface worth having.
Each cue routes its processed stereo onto a pair of those outs via the
inspector's **outs** row (1-2, 3-4, 5-6, 7-8) — e.g. programme to the PA on
1-2, click to monitors on 3-4. The pipeline stays stereo end to end; expansion
happens at the final write with silence on unused outs. On a device with fewer
physical outputs, SDL folds extra pairs down, so you can prep on a laptop and
route at the venue.

Companion: `AUDIOGAIN`, `AUDIOPAN`, `AUDIOMONO`, `AUDIONORM`, `AUDIOOUTS`.

### The crosspoint matrix

*v0.99.373 and later.*

For routing a pair cannot express, the inspector's **MATRIX** section sets, per
cue, how much of its left and right reaches each channel of the audio device.
Click **routing** to open the matrix, starting from the pair the cue already
uses. Each cell steps through off, full, −3, −6 and −12 dB. Two sources sent to
one channel add together, so a mono fold-down is just both sources onto the same
out. The matrix reaches every channel the device was opened with (see **Outs**
above). A cue with no matrix routes exactly as it always did. `MATRIX` over the
remote protocol does the same.

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

- **Fireside** — a hearth that burns, for a fireside chat. Set dressing rather
  than a test card: an arched stone fireplace with a mantel, sconces, andirons
  and embers riding the draught. The fire is simulated rather than looped, so
  it never repeats and adds nothing to the download. It is always animated.
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
- **Frame Count** — every frame carries its own number, so a dropped frame is a
  gap in a sequence rather than a judgement about whether the motion looked
  smooth. Photograph it at both ends of a chain and the difference is the
  latency, in frames, with no stopwatch involved. There is an **emoji** variant
  for when the far end is a phone camera and small digits will not survive it.
- **SMPTE 75% colour bars**, **crosshatch**, **checkerboard**, and full-frame
  **white, black, red, green and blue**.

The engineering set (*v0.99.373 and later.*), the same patterns the site's LED page generates:

- **Panel Map** — every LED tile numbered, with its pixel origin printed inside
  it. Set **tile w** and **tile h** to the wall's real panel size, because a
  168px panel mapped as 128 puts every label in the wrong place.
- **Moire 1:1** — shows at once whether anything in the chain is scaling.
- **Dark Detail** (0–12%) and **PLUGE** — black level.
- **Uniformity** — flat fields for colour shift and dead or dim tiles.
- **Banding Ramps** — the chain's real bit depth.
- **Safe Areas** — 90%, 80% and thirds.
- **Boresight** — projector and camera alignment.
- **Greyscale Steps** — gamma.
- **Convergence** — a fine grid for colour registration.
- **Multiburst** — bandwidth and sharpness.
- **Window 10%** and **Window 50%** — peak brightness at low and mid picture
  level.

Pocket Test comes in four times of day — **day, sunset, night and storm** — for
checking that a display's picture processing is not crushing shadows or clipping
highlights at one end of its range.

Most of the static cards also have a **(motion)** variant. Motion is the point:
a still card cannot show you tearing, judder, or a deinterlacer making things
up, and those are the faults that only appear once the picture moves.

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

**NEW asks twice while you are live.** Starting an empty show empties both decks
and disarms every output, so if an output is armed or a deck is running, the
first `NEW` (or `Ctrl+N`) only arms the warning banner — press it again within
2.5 seconds to go through. With nothing on air it goes straight through. The
show you were on is left on disk either way, and `Ctrl+Z` brings it back (with
outputs left disarmed, so you re-arm them yourself).

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

### Creatures

Some themes have things living in them. They occupy the empty part of the
playlist below your last cue: a moth that drifts toward the program monitor,
fireflies that breathe, fish, a crab that scuttles and stops, a cat asleep in
the corner.

They never sit over a control, and **they disappear the moment any output goes
live** — during a show the only thing moving should be the show. They come back
when the outputs go down.

Twenty-eight themes have their own cast, most of them two species. **Game Boy
and the plain dark terminal deliberately have none** — the signature look and
the theme a fresh install lands on stay perfectly still, so if you want a
machine that does not move, pick one of those. `Settings → CREATURES` turns
them off, and only appears when the theme you are using has any.

**Writing your own.** Add lines to a theme's `theme.txt`:

    creature	firefly	4
    creature	cat	1

The species are `moth`, `crab`, `fish`, `firefly`, `cat`, `snail`, `spider`,
`mouse`, `frog`, `jellyfish` and `bird`, up to twelve of
each. A species this build does not know is ignored rather than refused, so a
theme written for a later version still loads.

---

## 22. Remote Control

All remote inputs normalise to plain-text commands.

- **Companion** — port **5510** by default. Use the Deckboy module in
  `companion-module-utopianacademy-deckboy/`: as well as sending commands it polls Deckboy's
  state, so Stream Deck keys show cue tally, transport colour, output health and
  a countdown. The module is built on `@companion-module/base` 2.x and
  **requires Companion 5**; Companion 4.2 and earlier cannot load it, so stay
  on the module from an earlier Deckboy release until you have upgraded.
  A one-way *Generic TCP/UDP* mapping is still available in
  `docs/streamdeck/` for setups that can't install a module.
  **Deckboy listens on localhost only until Settings → Network → REMOTE is on** —
  leave it off and only Companion on the same machine can connect.
- **OSC** — messages/bundles on the same port, plus an OSC Query HTTP endpoint
  and mirrored `/deckboy/state` feedback.
- **MIDI** — on Windows and Linux; the current macOS download does not
  include MIDI. Turn **MIDI input** on in Settings and pick the port. A note
  0–127 takes that cue in the focused playlist, CC 7 is master volume and CC 20
  playback speed, and MIDI Machine Control PLAY / STOP / PAUSE / LOCATE drive
  the transport.
- **MIDI Show Control** — how a lighting desk fires the video. The desk sends
  **GO** with a cue number and Deckboy takes the cue with that number; a GO
  with no number takes the next cue, which is how a desk runs a straight
  rundown. MSC cue numbers are text, so `12.4.1` and `A` both work. **LOAD**
  selects a cue without firing it, **STOP** stops, **RESUME** continues,
  **RESET** reracks the current cue to its first frame, and **ALL_OFF** is
  the panic button: every output off, playback stopped. Set
  **MSC device id** under the MIDI settings to match the desk's patch for
  this machine, or **127** to answer the all-call. Any other ID must match
  exactly, so Deckboy never acts on a GO meant for another device. Every
  message is written to the show log, so "did the desk's GO reach this
  machine?" always has an answer.
- **HyperDeck** — Deckboy answers the HyperDeck protocol for decks that speak
  it.
- **Tally / triggers** — TSL tally out, ATEM and NDI-metadata triggers,
  Art-Net channel map, NMC sync.

Commands are case-insensitive. There are **over 260 of them**, and rather than
list them here — where they would go stale — **send `HELP` over the socket and
Deckboy prints the protocol it is actually running.** That reply is generated
from the same code that handles the commands, so it cannot drift.

Examples: `TAKE`, `STOP`, `VOLUME 75`, `AUDIOGAIN -6`, `AUDIOOUTS 2`,
`AUDIO NEXT`, `TRANSITIONSTYLE wipeleft`, `NOTESTEP NEXT`, `FX ADD ripple`.

The running order has verbs too (*v0.99.373 and later.*): `STANDBY <n>`, `STANDBY NEXT`,
`STANDBY CLEAR`, `PREWAIT <s>`, `POSTWAIT <s>`,
`CONTINUE OFF|AUTO|FOLLOW`, `ARM`, `DISARM`, `AUDITION`, `AUDITION OFF`,
`PRELOAD`, `PRELOAD OFF`, and `CHECK` (with `CHECK <n>` to jump to a problem).
Each show-control cue type has its own verb (`TARGET`, `FADE`, `MIDICUE`,
`NETCUE`, `TCCUE`, `DMXCUE`, `SCRIPTCUE`); `HELP` lists their arguments.

**Every command answers.** You get `OK <VERB>`, `ERR unknown command: <VERB>`,
or `ERR <VERB>: <reason>` — so a controller can tell a typo from a refusal from
a success, and a verb that was understood but could not act says why. Nothing is
silently swallowed and nothing is silently clamped into range: an out-of-range
value is an error, because a clamp is what makes a units mistake invisible.

Toggle adapters in `Settings → Network`.

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

Deckboy runs with no arguments. These are for the times it does not do what you
expect, or you want to prove it will before the doors open.

### Before a show

```
Deckboy.exe --devices               # every audio device, display, MIDI port and render driver
                                    #   the machine can actually see, with real rates and names
Deckboy.exe --self-check            # dependencies and backend wiring
Deckboy.exe --smoke                 # automated smoke test (exit 0 = pass)
Deckboy.exe --soak [minutes]        # long-run stability harness; logs memory and stalls
Deckboy.exe --version               # the version this binary reports
Deckboy.exe --check-update          # ask whether a newer release exists
```

`--devices` is the one to run first when somebody reports no sound, the wrong
controller or a soft picture. It separates a Deckboy fault from a machine that
cannot see its own hardware, and it spells device names the way a show file has
to.

### Opening something directly

```
Deckboy.exe show.deckboy            # open a show, skipping the splash
Deckboy.exe --import FILE           # import a file at launch, skipping the splash
Deckboy.exe --settings [tab[.subtab]]   # open the settings modal at a given tab
Deckboy.exe --code-editor           # open the code editor at boot
```

### When something will not play

```
Deckboy.exe --no-hw-decode          # decode in software: the A/B for the hardware path
Deckboy.exe --no-inproc-decode      # use the FFmpeg subprocess path instead of in-process
Deckboy.exe --decode-bench FILE [seconds] [cli]   # decode rate, and GPU vs CPU frame counts
Deckboy.exe --motion-probe FILE [frames]          # is this clip usable as a motion driver?
Deckboy.exe --hap-probe FILE        # report a HAP file's variant and chunking
Deckboy.exe --pdf-probe FILE        # page count and raster of a PDF before importing it
Deckboy.exe --pptx-notes FILE       # the speaker notes a PowerPoint deck would import
Deckboy.exe --sync-pop-test         # verify the audio-sync beacon path
```

If a clip plays with `--no-hw-decode` and not without it, the fault is the
hardware decoder on that machine, not the file.

### Proving what a look costs

```
Deckboy.exe --effect-bench TOKEN[:amount[:a[:b]]] [WxH] [frames]  # what one effect costs
Deckboy.exe --effect-dump TOKEN IN.ppm OUT.ppm [frame] [passes]   # one effect, headless
Deckboy.exe --pattern-dump ID OUT.ppm [WxH] [t]                   # one pattern, headless
Deckboy.exe --audio-fx-check        # run the audio chain against a known signal
```

`--effect-dump` renders without a window, so two builds can be compared frame
for frame and byte for byte.

### Other

```
Deckboy.exe --allow-multi-instance  # bypass the single-instance lock
Deckboy.exe --inspector-scroll PX   # scroll the inspector at boot (a big number means the bottom)
Deckboy.exe --help                  # the list this section is drawn from
```

### Environment

| Variable | Effect |
|----------|--------|
| `DECKBOY_PROJECT` | Open a specific show |
| `DECKBOY_STATE_DIR` | Where Deckboy writes: the show, the last-opened pointer, logs, converted media |
| `DECKBOY_ROOT` | Where Deckboy reads `data/` from — themes, fonts, sounds |
| `DECKBOY_THEME` | Force a colourway |
| `DECKBOY_COMPANION_PORT` | The control port (see §22) |
| `DECKBOY_NO_HW_DECODE` | As `--no-hw-decode` |
| `DECKBOY_OUTPUT_RENDERER` | Choose the output window's renderer: `gpu`, `direct3d11`, `metal`, `opengl` |
| `DECKBOY_EGRESS_READBACK=sync` | Force the plain synchronous recording readback |
| `DECKBOY_EGRESS_BENCH=1` | Print per-frame readback costs |
| `DECKBOY_UI_PROFILE=1` | UI timing and watchdog logs |

`DECKBOY_ROOT` and `DECKBOY_STATE_DIR` together give a completely isolated
instance, which is how to try something out without touching the show on the
machine.
