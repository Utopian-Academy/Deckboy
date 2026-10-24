# Deckboy features

[Back to the README](README.md)

The long version. It is long because the cue list absorbs so much — but it is
all one object with one set of controls, not fifty separate things to learn.

<details>
<summary><b>Playback &amp; cues</b></summary>

- Cue-based video playback with playlist management
- Drag-and-drop media import
- Play, pause, stop and seek; CLEAR immediately stops every deck, cancels queued
  takes, drops held pictures and overlays, and disarms output
- Looping and hold-last-frame behaviour
- **Captions made on this computer**: a clip with none gets them from its own
  sound, written beside it as a subtitle file and switched on, with nothing
  leaving the machine. English or any language; the speech engine comes with
  Deckboy on all three platforms
- Memo cues (a note in the running order) and devamp (a looping cue finishes
  its pass, then ends)
- Fade in/out, per cue and per deck
- Cue trimming, and per-cue transition overrides
- Seventeen transitions: cut, crossfade, dip to black or white, four pushes,
  four wipes, an iris and four procedural styles — set per deck, overridden
  per cue, timed in seconds, keeping each cue's fit, crop, geometry and look
- Compact cue rows and a full-width focused deck with tabs when several
  comfortable columns cannot fit
- Keying, grading and picture effects apply immediately to a playing or paused
  cue, without another TAKE

</details>

<details>
<summary><b>Sources — everything that can be a cue</b></summary>

- Video clips, images and audio files
- **Slide decks** — a PDF, PowerPoint or Keynote deck imports as one image cue
  per page, rendered at import by the platform's own engine, so nothing during
  a show depends on a document renderer
- Browser cues, rendered inside the programme rather than by a browser
  window on your desktop — a scoreboard, a dashboard or a lyric page is a
  cue like any other, with the same fades and effects
- Camera, window and screen capture, on all three platforms. A window cue
  arrives at the window's own full resolution whatever your display scaling
  is set to, scaled to fit the frame, follows the window as it moves and
  resizes, and anything in front of it stays out of shot. It runs at sixty
  frames a second where the window can afford it, and costs about a fifth of
  what it did: on a 960x540 window with a 4K output, processor use across the
  capture and the application fell from roughly 225% of a core to 45%
- Stream cues take `srt://`, `rtmp://`, `rtsp://`, `udp://` and http HLS
- NDI receive in every download, and Blackmagic DeckLink capture through the
  SDK rather than through a pipe
- Spout shared textures from another application on the machine (Windows)
- **IPTV channel lists**: import an `.m3u` and every channel becomes a cue,
  named and grouped from the list
- Test patterns, a built-in test card, and generated line-up tone
- **Swirl**, a generated pixel spiral with palettes, colour bands, speed (forward
  or reverse), twist, flow, zoom, a movable centre and density, all live, saved
  with the cue and available over the control protocol
- Timer cues — a stage or speaker countdown with its own clock, thresholds and
  chimes. Its backdrop can go transparent and it has its own size and position,
  so the clock can sit over the programme instead of replacing it
- Lower thirds, cued like anything else, in 52 looks: branded straps, paper
  cards, cloth banners, phosphor terminals, split-flap cells and **arcade**.
  Themed looks bring their own materials and entrances; cassette reels turn,
  marquee bulbs chase and Equalizer follows the programme's sound. Any look
  can use Arcade's slow bob and tilt with Wiggle. Colours, size and Wiggle
  can be set from a Companion button
- Picture in picture, side by side or a quad: lay a second playlist over the
  first as a layer and scale and place it, with a whole playlist behind each
  window instead of one fixed box

![A lower third reading Lower Third, Arcade look pops onto a generated pixel beach scene in a pixel font with a hard drop shadow, keeps a slow bob and tilt, then goes](docs/images/readme/light/arcade-lower-third.gif)

- A **video synth** — oscillators with feedback, a glitch stack, text mode and
  sprite sets, among them the pickle set by Garrick Folderouy — and a **code source**, a live-coded expression evaluated per
  pixel and edited while it runs. Its own render width and height set the work
  it does independently of its placement on the output; an incomplete edit
  holds the last good picture

</details>

<details>
<summary><b>Outputs — everywhere a cue can go</b></summary>

- Fullscreen output windows, display selection, and the same programme on more
  than one output at once
- Display-native and fixed raster modes
- Area of interest, warp / keystone correction, gamma-correct edge blending,
  paired overlap, projector gamma and black lift
- **Projection mapping per layer**: drag a layer's four corners onto a surface
  on the programme monitor, in **perspective**, so a flat board stays flat with
  its far end properly smaller, or linear. The programme, the recording and the
  stream all carry it

![A test grid pinned onto a steep board: drawn linear it folds along the diagonal, then in perspective it becomes a flat board](docs/images/readme/light/perspective-pin.gif)

- Per-output matte and still overlay, composited into the output's own picture
- NDI output, in every download
- DeckLink (SDI) output, wherever the Blackmagic SDK is present, with scheduled
  video and audio, limited-range BT.709 conversion and eight- or ten-bit packing
- SRT and RTMP streaming, each output with its own destination, running while
  the programme is recorded
- Recording that keeps its sound, continuous for the whole take
- Web Monitor shows programme -- or the multiview, a presenter view, any
  output -- with stereo sound in any browser. Screens watching together stay
  in step, a low-latency mode sends the picture alone, and a QR code opens it on
  a phone
- Picture and sound stay in sync through a long show, following the sound
  card's own clock
- Eight- and ten-bit BT.709 SDR video retains its source range on programme
  output. HDR10 and HLG are converted automatically for SDR outputs
- Floating-point HDR window outputs, with SDR graphics at 203 nits by default;
  PQ or HLG recording, SRT/enhanced RTMP and ST 2110 output. See the
  [HDR specifications](https://utopian-academy.github.io/Deckboy/specs.html#hdr)
  for platform, encoder and receiver requirements
- A test card per output, drawn at its own raster, with moving checks, safe
  areas, frame counter and export to H.264 movies
- Programme picture scopes: waveform, parade, vectorscope, histogram and gamut
  checks, including HDR measurements in nits and the SDI/ST 2110 4:2:2 signal

</details>

<details>
<summary><b>Presenting from slides</b></summary>

- Import a PDF, PowerPoint or Keynote deck as one cue per slide, rendered once
  at import by the platform's own engine -- nothing during the show depends on
  a document renderer
- A cat accompanies slide rendering, with measured page progress as the
  slides arrive; timeline loading keeps its filmstrip and audio-meter displays
- Speaker notes come with the deck, so a team working a master deck elsewhere
  keeps its fonts *and* its notes
- **Presenter view** as an output type, so it takes its own display: the live
  slide, the one before it, the one after it, the notes and the clock. Every
  panel switches off on its own, the colours are yours, and the panels are
  dragged into place rather than chosen from a list of layouts
- The clicker walks long notes a part at a time, at the speaker's pace — and
  Page Down and Page Up just work, because that is what every clicker sends

![The presenter view mid-build: the live slide large on the left, the previous and next slides stacked on the right, and the speaker's notes below, the part already spoken greyed out](art/readme/presenter.png)

- **Teleprompter view**, also an output of its own: the script large, scrolling
  through a fixed reading line, mirrored for a beamsplitter, and driven from a
  hand controller over the control protocol

</details>

<details>
<summary><b>Broadcast / IP video</b></summary>

- SMPTE ST 2110-20 uncompressed video output
- SMPTE ST 2110-30 (AES67) audio output
- PTP (IEEE 1588 / SMPTE ST 2059) media clock slaving
- AMWA NMOS IS-04 registration and Node API
- AMWA NMOS IS-05 connection management, so a broadcast controller can
  discover and route Deckboy's senders
- MediaMTX both ways: its streams appear as one-click stream cues, and the
  programme publishes to it for any number of viewers

Video and 48 kHz stereo audio have independent sender controls for activation,
source and destination addresses, ports and interface binding. PTP acquisition
and reacquisition update the media clock. See
[docs/ST2110_FEASIBILITY.md](docs/ST2110_FEASIBILITY.md).

</details>

<details>
<summary><b>Audio</b></summary>

- Per-cue gain trim, pan and mono fold-down
- Loudness normalization with selectable −16, −23 (EBU R128) and −24 LUFS targets
- Programme metering with momentary, short-term and integrated LUFS, true peak,
  and a true-peak guard on the mixed output
- Independent audio fades, separate from video fades
- Content-authoritative stereo waveform display
- Audio-only cues, and per-cue mute
- A file with more than one sound track (a second language, a commentary)
  lets you choose which one plays, even on the cue that is on air
- Up to 128 output channels per deck, with a crosspoint matrix routing each cue
  onto any of them: enough for a full Dante or MADI rig
- **Your own VST3 effects and instruments, in the cue's chain.** The reverb you
  already own or the channel strip your mix is built around, sitting beside
  Deckboy's own effects in whatever order you put them, with its settings saved
  in the show. Instruments play from a MIDI keyboard, a controller over the
  network, or the computer's own keys
- **A per-cue effect chain** -- high pass, low pass, tilt EQ, compressor, gate,
  delay, reverb, width and binaural placement, in the order you put them in
- **Five effects that use what the deck knows**, which no plugin is ever told:
  the cue's own picture driving a filter, its position on the output becoming
  the sound's position in the room, the end of the cue resolving the tail,
  stutter locked to the video frame, and a held cue keeping its room tone
- **Eight bends**, for when the point is damage rather than polish — bit
  crushing, a CD skipping, corrupted filter coefficients, and *ouroboros*,
  where the finished picture drives the bend that is driving the picture

</details>

<details>
<summary><b>Show control</b></summary>

- Bitfocus Companion integration (module included)
- OSC input and OSC Query
- TCP command control — every verb answers `OK` or `ERR`
- **Triggers on every cue**: a hotkey, a MIDI note, an OSC address, a time of
  day or a timecode point fires it through the same take as GO
- **CHECK**: everything wrong with the show in one list (missing media, a
  device that is not connected, a plugin that is not installed); pick a line to
  go straight to it
- HyperDeck protocol emulation, so a deck controller can drive it
- Tally-driven playback: roll when an ATEM or an NDI receiver puts you on air
- LTC timecode in, and an LTC generator routable to its own device and channel
- MIDI input, Art-Net / sACN DMX, NMC transport sync in and out
- Phone and tablet control, paired by QR or passphrase: TAKE, transport, fader,
  cue list, editable DASH, tracker and show controls on one page
- Master tracker in pattern or classic view, with keyboard editing, stop/fade/
  volume/opacity cells, column mute/solo, jumps and named sections
- A fixture patch: lights named once, set by name and colour with LFOs, and dipped
  automatically while a video rolls
- Mic cues: a live microphone or line input as a cue, with its own fades, effects and routing

</details>

<details>
<summary><b>Effects &amp; mixing</b></summary>

- A per-cue effect stack on every kind of cue, ordered, with copy/paste of a
  whole chain between cues
- **Levels and curves** first in the list: black and white point, midtones and
  lift; an S-curve per channel with shadow lift and highlight roll-off. Both
  arrive neutral and cost under a millisecond a frame at 1080p
- Effects go on and off a live cue without interrupting it

![Effects switched on live while a clip plays: kaleidoscope, posterise, ripple and crystallise, then back to the clean picture](docs/images/readme/light/live-effects.gif)

- Forty-two effects, each with named parameters. `--effect-bench` measures
  the cost on your machine; resolution and the chosen stack determine the frame budget
- Six that exist nowhere else: schlieren gradient imaging, Chladni nodal
  figures, a true wave equation with inertia, crystal grain growth, retinal
  rod/cone persistence, and structure tensor grain flow
![The databend effect brought up on a generated beach scene: the colours tear into horizontal bands, blocks repeat down the frame, and the picture settles back](docs/images/readme/light/databend.gif)

- **Two that cross between picture and sound.** *Databend* reads the frame out
  as a signal and plays it through an audio chain — a delay with feedback, a
  filter, a wavefolder — then paints what comes back, which is the look people
  chase by opening a picture in an audio editor, on a live cue and on a knob.
  *Audioprint* goes the other way and draws the audio the deck has just played
  through the picture, a slice of sound per row. Put one of each on the same
  cue and they bend each other
- An LFO on any parameter — six shapes, free running or locked to a tap tempo
- VJ mode: a second deck live, a crossfader with ten blend modes — dissolve,
  add, screen, multiply, lighten, darken, subtract, undercut, infiltrate and
  ember — tap tempo, and takes quantised to the beat

</details>

<details>
<summary><b>Interface</b></summary>

- **Thirty bundled themes**, switchable live and from a controller, including
  high-contrast terminal themes suited to OLED panels
- Timeline with filmstrip thumbnails; resizable program monitor and timeline
- UI scale that follows the desktop's own scaling
- Save the current setup as your defaults for new shows: outputs, routing,
  decks, DASH, fixtures, theme and settings, without the cues
- A language picker with 43 languages, including Arabic and Persian right to
  left, Kurdish, Kabyle and Tamazight, Cubano, Klingon and a few written in
  cypher
- Every script on any desk: a cue named in Japanese, Chinese, Hindi or Arabic
  reads on an English desk, from the fonts the computer already has
- Missing-media detection with folder relink, so a moved drive does not cost you
  a rebuild

</details>

---

[Back to the README](README.md)
