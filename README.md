# Deckboy 🎬

[![Release](https://img.shields.io/github/v/release/Utopian-Academy/Deckboy)](https://github.com/Utopian-Academy/Deckboy/releases)
[![License](https://img.shields.io/github/license/Utopian-Academy/Deckboy)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-lightgrey)](#)
[![Built with SDL3](https://img.shields.io/badge/built%20with-SDL3-blue)](#)

**Open-source media playback and show control for live video.**
Load your media. Build your playlist. Take it live.

**[deckboy website](https://utopian-academy.github.io/Deckboy/)** — what it does, and where to download it. · **[How it compares](https://utopian-academy.github.io/Deckboy/compare.html)** to Mitti, PlaybackPro, QLab, Millumin and vMix — including where they win. · **[Manual](https://utopian-academy.github.io/Deckboy/manual.html)** — all of it, twenty-six chapters. · **[FAQ](https://utopian-academy.github.io/Deckboy/faq.html)** · **[Where latency hides](https://utopian-academy.github.io/Deckboy/latency.html)** — the four stages of a live chain, in frames and milliseconds

<!-- GENERATED CONTENT ONLY in this shot -- the built-in pattern generators,
     never a real show file. A client's deck in a public README is a client's
     deck on the internet. -->

![Deckboy taking generated cues to air, then stacking scanlines, ripple and a kaleidoscope onto the live picture, then the built-in video synth in Game Boy green](art/readme/demo.gif)

<sub>Shown with the test patterns Deckboy generates itself, so nothing here is anyone’s show file.</sub>

---

## Download

Every release ships an installer **and** a portable build for each platform.
Both bundle everything they need — binary, ffmpeg, runtime libraries. Nothing
else to install.

| Platform | Installer | Portable |
|---|---|---|
| **Windows** | `…-windows-x64-setup.exe` — Start Menu, uninstaller, `.deckboy` file association | `…-windows-x64.zip` |
| **macOS** (Apple Silicon) | `…-macos-arm64.dmg` — drag to Applications | `…-macos-arm64.zip` |
| **macOS** (Intel) | `…-macos-x86_64.dmg` | `…-macos-x86_64.zip` |
| **Linux** | `…-x86_64.AppImage` — one file, `chmod +x` and run | `…-linux-x86_64.tar.gz` |

Both control surfaces ship with every release, so neither needs a build:
**Stream Deck** (`…streamDeckPlugin` — double-click it) and **Bitfocus
Companion** (`Deckboy-companion-module-….zip` — unzip it, then point
Companion's *developer modules path* at the folder containing it;
`INSTALL.txt` inside has the three steps).

→ **[Latest release](https://github.com/Utopian-Academy/Deckboy/releases/latest)**

<details>
<summary><b>"Unknown developer" warnings — what to do</b></summary>

Deckboy is not code-signed. Signing is a paid developer account with Apple and
with Microsoft, and the project has not bought one -- so the builds are fine and
the OS simply does not recognise the publisher. It is on the roadmap below for
the macOS releases, where the warning is most in the way.

- **macOS** — if it says the app is damaged, clear the quarantine flag once:

      xattr -dr com.apple.quarantine /Applications/Deckboy.app

  Install from the `.dmg` into Applications rather than running from Downloads;
  that is also what avoids Gatekeeper's App Translocation sandbox.
- **Windows** — SmartScreen may say "Windows protected your PC". Click
  **More info → Run anyway**.
- **Linux** — the AppImage runs on any current distribution. Graphics, display
  server, audio and the C/C++ runtime deliberately come from the host: they
  have to match the machine actually running, and a bundled libGL cannot load
  your GPU driver. Built on Ubuntu 24.04 / Mint 22, so target that vintage or
  newer.

</details>

---

## What it is

Deckboy is a native desktop application for video engineers, AV technicians and
live operators who need a reliable way to organise media, trigger cues, loop
content and send video to production displays.

That is the job it is built around, and it is built on a stubborn principle:
**Seize the means of playback ♡**

No subscription ♡ No account ♡ No telemetry ♡ Nothing phones home. And no licence
server that can refuse to start the show at 19:55 because it could not reach the
internet. The machine you carried into the venue is the machine that plays the
show.

- 🎯 **Prepare** media cues ahead of time
- 🚀 **Trigger** clips instantly during a show
- 🔁 **Keep loops running** reliably
- 📺 **Send video** to dedicated fullscreen outputs
- 🎛️ **Control playback** remotely from production tools

### It is also a very good video Swiss army knife

The same box speaks NDI, SDI, SMPTE ST 2110, SRT, RTMP, Spout, LTC timecode,
OSC, Art-Net and NMOS — so the machine you brought for playout usually solves
the other five problems on the day as well.

- **Convert media** that will not play well, in place, without leaving the app
- **Inspect any file** — codec, raster, frame rate, channels, duration — by importing it
- **Generate test patterns** and a test card to prove a chain end to end
- **Capture** a camera, a window or a screen and treat it as a cue
- **Bridge formats** — bring a camera, capture card or stream in and send the
  programme out as SDI, ST 2110 or a stream, and record it
- **Normalize loudness** to EBU R128 when a client sends a clip mastered too quiet
- **Generate LTC timecode** on its own routable output

### Where it gets used

Live events • Corporate presentations • Churches • Schools & universities •
Museums & installations • Digital signage • Projection • LED walls • Streaming

And the jobs in between: bench-testing a screen before anyone arrives, proving a
cable or converter with a real test card, getting a camera or a stream onto
SDI, restreaming to SRT or RTMP, or making a client's unplayable file
playable on site, minutes before doors.

---

## Features

<details>
<summary><b>Playback &amp; cues</b></summary>

- Cue-based video playback with playlist management
- Drag-and-drop media import
- Play, pause, stop, seek and clear
- Looping and hold-last-frame behaviour
- Fade in/out, per cue and per deck
- Cue trimming, and per-cue transition overrides
- Thirteen transitions: cut, crossfade, dip to black or white, four pushes,
  four wipes and an iris — set per deck, overridden per cue, timed in seconds

</details>

<details>
<summary><b>Video output</b></summary>

- Dedicated fullscreen output windows, display selection, and the same programme
  on more than one output at once. A *different* deck on each output — multi-deck,
  multi-output playback — is coming in **Super Deckboy**
- Display-native and fixed raster modes
- Area of interest, edge feathering, warp / keystone correction
- Per-output matte and still overlay, composited into the output's own picture
- NDI output, in every download. The NDI runtime itself comes from your own
  NDI Tools install, so nothing is redistributed and a machine without it
  says so rather than failing quietly
- DeckLink (SDI) output, wherever the Blackmagic SDK is present
- SRT and RTMP streaming, each output with its own destination, running while
  the programme is recorded
- Recording that keeps its sound: the audio is continuous for the whole take,
  and it ends with the picture rather than a moment before it

</details>

<details>
<summary><b>Live sources</b></summary>

- Stream cues take `srt://`, `rtmp://`, `rtsp://`, `udp://` and http HLS
- Blackmagic DeckLink capture through the SDK rather than a pipe, and NDI
  receive in every download
- Camera, desktop window and screen capture
- **IPTV channel lists**: import an `.m3u` and every channel becomes a cue, named
  from the entry with its group in the notes. An HLS media playlist that happens
  to share the extension is spotted by its `#EXT-X-` tags and played as one
  stream instead of imported as one cue per segment

</details>

<details>
<summary><b>Presenting from slides</b></summary>

- Import a PDF, PowerPoint or Keynote deck as one cue per slide, rendered once
  at import by the platform's own engine -- nothing during the show depends on
  a document renderer
- Speaker notes come with the deck: read out of a `.pptx` directly, or from a
  `.pdfpc` / `.notes.txt` sidecar beside a PDF. A team working a master deck in
  Google Slides can export both and keep its fonts *and* its notes
- **Presenter view** as an output type, so it takes its own display: the live
  slide, the one before it, the one after it, the notes and the clock. Every
  panel switches off on its own, the colours are yours, and the panels are
  dragged into place rather than chosen from a list of layouts
- Notes split into parts on a line of `---` and the clicker walks them, scrolling
  a long note at the speaker's pace instead of showing it all at once. Or split
  one cue into one cue per part, if you would rather they were in the playlist
- Page Down and Page Up are a presenter remote, because that is what every
  clicker sends

![The presenter view: previous, live and next slides across the top, the speaker's notes below](art/readme/presenter.png)

- **Teleprompter view**, also an output type, for the person in front of the camera:
  the script very large, scrolling up through a fixed reading line at a pace set
  in lines per minute, and mirrored for a beamsplitter. It prompts from the live
  cue's notes, or from a script of its own. Run, stop, jog, pace, size and
  mirror are all on the control protocol, which is what a hand controller drives

</details>

<details>
<summary><b>Broadcast / IP video</b></summary>

- SMPTE ST 2110-20 uncompressed video output
- SMPTE ST 2110-30 (AES67) audio output
- PTP (IEEE 1588 / SMPTE ST 2059) media clock slaving
- AMWA NMOS IS-04 registration and Node API
- AMWA NMOS IS-05 connection management, so a broadcast controller can
  discover and route Deckboy's senders

ST 2110 output is marked **experimental** in the interface, and honestly so: it
is a conformant packetiser, but it is not narrow-model paced (that needs
hardware pacing), and NMOS discovery is by configured registry URL rather than
mDNS. See [docs/ST2110_FEASIBILITY.md](docs/ST2110_FEASIBILITY.md).

</details>

<details>
<summary><b>Audio</b></summary>

- Per-cue gain trim, pan and mono fold-down
- EBU R128 loudness normalization
- Independent audio fades, separate from video fades
- Content-authoritative stereo waveform display
- Audio-only cues, and per-cue mute
- **A per-cue effect chain** -- high pass, low pass, tilt EQ, compressor, gate,
  delay, reverb, width and binaural placement, in the order you put them in
- **Five effects that use what the deck knows**, which is something no plugin
  is ever told: the cue's own picture driving a filter, its position on the
  output becoming the sound's position in the room, the approaching end of the
  cue resolving the tail, stutter quantised to the video frame period, and a
  held cue keeping its room tone instead of stopping dead
- **Eight bends**, for when the point is damage rather than polish: fewer bits
  and fewer samples, sample words misread, a CD skipping and splicing itself
  clean, a filter with corrupted coefficients, and four that read the deck —
  the picture's size and brightness setting the sound's resolution, its
  brightness becoming *time* so a dark frame reaches backwards, a short circuit
  closing on every cut, and *ouroboros*, where the finished picture drives the
  bend that is driving the picture

</details>

<details>
<summary><b>Show control</b></summary>

- Bitfocus Companion integration (module included)
- OSC input and OSC Query
- TCP command control — every verb answers `OK` or `ERR`
- HyperDeck protocol emulation, so a deck controller can drive it
- Tally-driven playback: roll when an ATEM or an NDI receiver puts you on air
- LTC timecode in, and an LTC generator routable to its own device and channel
- MIDI input, Art-Net / DMX, NMC transport sync in and out

</details>

<details>
<summary><b>Sources</b></summary>

- Video clips, images and audio files
- **Slide decks** — a PDF imports as one image cue per page, rendered at import
  by the platform's own engine, so nothing during a show depends on a document
  renderer
- Browser cues, rendered inside the programme rather than by a browser
  window on your desktop — a scoreboard, a dashboard or a lyric page is a
  cue like any other, with the same fades and effects
- Camera, window and screen capture, on all three platforms. A window cue
  arrives at the window's own full resolution whatever your display scaling
  is set to, scaled to fit the frame, follows the window as it moves and
  resizes, and anything in front of it stays out of shot
- SRT, RTMP, RTSP and UDP stream input, and NDI source input
- Test patterns and a built-in test card
- A **code source** — a live-coded expression evaluated per pixel, edited while
  it runs, with a compile error that never blacks the output

</details>

<details>
<summary><b>Effects &amp; mixing</b></summary>

- A per-cue effect stack on every kind of cue, ordered, with copy/paste of a
  whole chain between cues
- Thirty-seven effects, each with named parameters. Timed one at a time at 1080p
  on a laptop processor, the heaviest measured takes three-quarters of a 60fps
  frame — `--effect-bench` prints what each one costs on yours
- Six that exist nowhere else: schlieren gradient imaging, Chladni nodal
  figures, a true wave equation with inertia, crystal grain growth, retinal
  rod/cone persistence, and structure tensor grain flow
![The databend effect brought up on a generated beach scene: the colours tear into horizontal bands, blocks repeat down the frame, and the picture settles back](docs/images/databend.gif)

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
  ember — tap tempo, and takes quantised to the beat. Two decks mixed into one
  programme is where it stops today: more decks, and a different deck on each
  output, are coming in **Super Deckboy**

</details>

<details>
<summary><b>Interface</b></summary>

- Themeable, including high-contrast terminal themes suited to OLED panels
- Timeline with filmstrip thumbnails; resizable program monitor and timeline
- UI scale that follows the desktop's own scaling
- The interface reads in 38 languages, including Cubano, Klingon and a few
  written in cypher
- Missing-media detection with folder relink, so a moved drive does not cost you
  a rebuild

</details>

---

## Built for operators, not data harvesters

Deckboy collects nothing and sends nothing to its developers. **No telemetry,
no usage reporting, no crash upload, no account.** Crash logs are written to a
file next to the app for you to read or forward, and they stay there.

There is an update check, and it is **off by default**. Switched on, it asks
GitHub's releases API whether a newer version exists — nothing about you goes
with the question, and finding one never installs anything without you saying
so. A machine sitting on a venue's network should do nothing nobody asked it
to.

It is deliberately network-active — NDI discovery, PTP, NMOS registration, OSC,
Companion control and streaming all talk to the network by design. Every one of
those goes to your own LAN or to a destination you configured.

---

## Where the platforms differ

**App texture sharing.** Spout **output** works on Windows — Deckboy's picture
appears as a Spout sender that Resolume, TouchDesigner or OBS can pick up.
Spout *input* (treating another app's texture as a cue) is not built yet, and
**Syphon on macOS is not built at all**. Deckboy reports the missing halves as
unavailable rather than accepting frames and quietly discarding them.

**Hardware decode.** Two separate things. The *decode* runs on hardware where
the platform has a decoder: D3D11VA on Windows, VideoToolbox on macOS, VAAPI on
Linux. The *frame* additionally avoids a copy on Windows and macOS -- on
Windows the decoder can be put on the output renderer's own device, and on macOS
a VideoToolbox frame is an IOSurface that a Metal texture can wrap directly, so
either way the picture never touches system memory.

On an Apple M4, ten seconds of 4K30 costs about half a second of CPU that way
against five and a half in software.

Neither is about making playback possible — software decode is comfortable on
modern hardware for ordinary material — they are about what else the machine can
do at the same time. `--decode-bench` reports which decoder ran, and
`DECKBOY_NO_HW_DECODE=1` forces software so the two can be compared on your own
machine with your own footage, which is the only comparison worth anything: the
saving depends on the codec and on how old the GPU is, and on a sufficiently old
one the hardware decoder can be the slower of the two.

Everything else in the list above runs on all three platforms.

---

## Documentation

- [Code map](docs/CODEMAP.md) — file inventory, data flow, threading model
- [Packaging guide](docs/PACKAGING.md)
- [Changelog](CHANGES.md)
- [ST 2110 feasibility](docs/ST2110_FEASIBILITY.md)

The changelog is the authoritative record of what landed and, just as
importantly, what each feature deliberately does not do.

---

## Roadmap

- **Super Deckboy:** more than two decks, and multi-deck, multi-output playback —
  a different deck on each output
- NMOS registry discovery over mDNS, so there is no registry address to type in
- Hardware-paced ST 2110 output for narrow-model compliance
- Syphon output on macOS, and Spout/Syphon *input* as a cue source
- Developer ID signing and notarization for macOS releases

---

## License

Deckboy is free software under the **GNU General Public License v3.0 or later**.
Copyright © 2026 Deckboy Contributors. The full text is in [LICENSE](LICENSE).

Bundled components keep their own licences — ffmpeg ships with its LGPL/GPL
notice beside the binary, and the NDI and DeckLink SDKs are loaded at runtime
rather than distributed.

NDI® is a registered trademark of Vizrt NDI AB.

---

## Contributing

Deckboy is built in the open and contributions are welcome — code, bug reports,
documentation, testing, or production feedback from a real show.

The best open-source tools are built by the communities that use them.

---

## Project status

Actively developed, and Windows-first. The core playback workflow is complete:
cue playlists, fullscreen outputs, remote control and live production
integrations.

macOS and Linux build from the same source and are checked by CI on every
commit, in both full and reduced-feature configurations. macOS has a portable,
self-contained app bundle; Linux ships a portable tarball and an AppImage.
