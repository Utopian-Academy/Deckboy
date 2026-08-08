Deckboy

Open-source media playback and show control for live video workflows.

Deckboy is a native desktop application built for video engineers, AV technicians, and live operators who need a reliable way to organize media, trigger cues, loop content, and send video to production displays.

Load your media. Build your playlist. Take it live.

+--------------------------+
| DECKBOY                  |
|--------------------------|
| 01  opener.mp4           |
| 02  background_loop.mov  |
| 03  stinger.mp4          |
|                          |
|             >> TAKE      |
+--------------------------+

Why Deckboy?

Many video workflows need something between a basic media player and a full production suite.

Deckboy focuses on the everyday playback workflow:

- Prepare media cues ahead of time
- Trigger clips instantly during a show
- Keep loops running reliably
- Send video to dedicated fullscreen outputs
- Control playback remotely from production tools

Built as a native SDL3 application, Deckboy prioritizes predictable performance, simple deployment, and operator-focused controls.

Deckboy is fully open source. Windows is the primary development and release
platform. macOS and Linux build from the same codebase and are verified on every
commit by CI; macOS additionally has a portable, self-contained app bundle.

Built For

Deckboy is designed for environments where reliable media playback matters:

- Live events
- Corporate presentations
- Churches
- Schools and universities
- Museums and installations
- Digital signage
- Projection systems
- LED walls
- Streaming productions

Current Features

Playback

- Cue-based video playback
- Playlist management
- Drag-and-drop media import
- Play, pause, stop, seek, and clear controls
- Looping and hold-last-frame behavior
- Fade in/out controls
- Cue trimming

Video Output

- Dedicated fullscreen output windows
- Multiple display support
- Display selection
- Display-native and fixed raster modes
- Output recovery and fullscreen safety tools
- Area of interest, edge feathering, and warp/keystone correction
- NDI output support
- DeckLink (SDI) output (Windows builds)
- Spout texture sharing (Windows)
- SRT and RTMP streaming outputs, configurable independently and live at once

Broadcast / IP Video

- SMPTE ST 2110-20 uncompressed video output
- SMPTE ST 2110-30 (AES67) audio output
- PTP (IEEE 1588 / SMPTE ST 2059) media clock slaving
- AMWA NMOS IS-04 registration and Node API
- AMWA NMOS IS-05 connection management, so a broadcast controller can
  discover and route Deckboy's senders

ST 2110 output is marked EXPERIMENTAL in the interface, and honestly so: it is a
conformant packetiser, but it is not narrow-model paced (that needs hardware
pacing), and NMOS discovery is by configured registry URL rather than mDNS. See
docs/ST2110_FEASIBILITY.md.

Audio

- Per-cue gain trim, pan, and mono fold-down
- EBU R128 loudness normalization
- Independent audio fade in/out, separate from video fades
- Content-authoritative stereo waveform display
- Audio-only cues
- Per-cue audio mute

Control

- Bitfocus Companion integration (module included)
- OSC input and OSC Query
- TCP command control
- HyperDeck protocol emulation
- LTC timecode input
- LTC timecode generator, individually routable to its own device and channel
- MIDI input
- Art-Net / DMX
- ATEM tally
- Remote operation workflows

Sources

- Video clips
- Images
- Audio files
- Browser sources (Windows)
- Camera sources
- Window capture
- SRT, RTMP, RTSP and UDP stream input
- NDI source input
- Test pattern generation, including a built-in test card

Interface

- Themeable, including high-contrast terminal themes suited to OLED panels
- Timeline with filmstrip thumbnails
- Resizable program monitor and timeline
- UI scaling

Download

The fastest way to get started:

1. Download the latest release
2. Extract the ZIP
3. Run Deckboy.exe
4. Load your media
5. Take your first cue

Windows builds are portable and include the required runtime components.

macOS builds are portable too: a self-contained Deckboy.app that carries its own
libraries, built by tools/package_macos.sh. It is ad-hoc signed rather than
notarized, so a zip downloaded through a browser needs its quarantine flag
cleared once:

    xattr -dr com.apple.quarantine Deckboy.app

Browser cues and Spout output are Windows-only and are absent from the macOS
build rather than broken in it.

No installer required.

Documentation

- "Code Map" (docs/CODEMAP.md)
- "Build Instructions" (CLAUDE.md)
- "Changelog" (CHANGES.md)
- "ST 2110 Feasibility" (docs/ST2110_FEASIBILITY.md)

The changelog is the authoritative record of what landed and, just as
importantly, what each feature deliberately does not do.

Roadmap

Deckboy is actively evolving. Future development is focused on expanding its capabilities for larger and more complex production workflows, including:

- Multi-output video workflows
- Layer-based compositing
- Picture-in-picture (PIP) layouts
- Expanded show-control features
- Additional production integrations
- NMOS discovery over mDNS, so a registry no longer has to be configured by URL
- Hardware-paced ST 2110 output for narrow-model compliance
- Linux packaging to match the Windows and macOS portable builds
- Developer ID signing and notarization for macOS releases
- Expanded cross-platform testing and release automation

The goal is to continue building a flexible, open-source playback platform that remains simple to operate while supporting increasingly advanced workflows across platforms.

Contributing

Deckboy is built in the open, and contributions are welcome.

Whether you are a developer interested in improving the codebase, a video engineer testing Deckboy in real production environments, or someone with ideas for better workflows, your input can help shape the future of the project.

New features, bug reports, documentation improvements, testing, and production feedback are all valuable.

The best open-source tools are built by the communities that use them — and Deckboy is better when more people help make it better.


## Project Status

Deckboy is actively developed and currently Windows-first.

The core playback workflow is functional, including cue playlists, fullscreen outputs, remote control, and live production integrations.

macOS and Linux build from the same source and are checked by CI on every commit. macOS has a portable, self-contained app bundle; Linux packaging is in progress.

As the project grows, additional workflows and platform improvements are being developed.
