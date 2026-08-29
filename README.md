Deckboy

Open-source media playback and show control for live video workflows.

Deckboy is a native desktop application built for video engineers, AV
technicians, and live operators who need a reliable way to organize media,
trigger cues, loop content, and send video to production displays. That is the
job it is built around and the job the interface is optimised for: when the
lights go down, playback has to be right.

It also happens to be a very good video Swiss army knife. The same box speaks
NDI, SDI, SMPTE ST 2110, SRT, RTMP, Spout, LTC timecode, OSC, Art-Net and NMOS,
converts awkward media in place, captures cameras and screens, and generates
test patterns — so the machine you brought for playback usually solves the other
five problems on the day as well.

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

Many video workflows need something between a basic media player and a full
production suite. Deckboy is built for that gap, and playback is the point of
it. The everyday playback workflow is the core of the app:

- Prepare media cues ahead of time
- Trigger clips instantly during a show
- Keep loops running reliably
- Send video to dedicated fullscreen outputs
- Control playback remotely from production tools

Around that core sits a toolkit that means one laptop usually covers the rest of
the day as well:

- Convert media that will not play well, in place, without leaving the app
  (NVENC where available, libx264 otherwise)
- Inspect any file — codec, raster, frame rate, channels, duration — by
  importing it
- Generate test patterns and a test card to prove a chain end to end
- Capture a camera, a window or a screen and treat it as a cue
- Bridge formats: take NDI in and send SDI, ST 2110, SRT or RTMP out, in any
  combination, simultaneously
- Normalize loudness to EBU R128 when a client sends a clip mastered too quiet
- Read the audio honestly with a content-authoritative stereo waveform
- Generate LTC timecode on its own routable output

Built as a native SDL3 application, Deckboy prioritizes predictable performance,
simple deployment, and operator-focused controls. No installer and no account.

Deckboy collects nothing and sends nothing to its developers: there is no
analytics, no usage reporting, no crash upload and no update check. Crash logs
are written to a file next to the app for you to read or forward, and stay
there.

It is, however, deliberately network-active — NDI discovery, PTP, NMOS
registration, OSC, Companion control and streaming all talk to the network by
design. Every one of those goes to your own LAN or to a destination you
configured. The only outbound links in the app are vendor download pages
(NDI, Blackmagic, WebView2), opened in your browser when you click the button
in a missing-dependency prompt.

Deckboy is fully open source. Windows is the primary development and release
platform. macOS and Linux build from the same codebase and are verified on every
commit by CI; macOS additionally has a portable, self-contained app bundle.

Built For

Environments where reliable media playback matters:

- Live events
- Corporate presentations
- Churches
- Schools and universities
- Museums and installations
- Digital signage
- Projection systems
- LED walls
- Streaming productions

And the jobs in between, which is where a Swiss army knife earns its keep:

- Bench-testing a screen, projector or LED wall before anyone arrives
- Proving a cable, converter or switcher input with a real test card
- Getting an NDI source onto SDI, or an SDI-shaped workflow onto the network
- Restreaming a local source to SRT and RTMP at the same time
- Making a client's unplayable file playable, on site, minutes before doors
- Checking what a file actually is before trusting it in a show
- Putting timecode on a spare pair without disturbing the programme mix
- Standing in as a playout source while the real system is being built

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
- Browser sources (Windows and Linux)
- Camera sources
- Window capture
- SRT, RTMP, RTSP and UDP stream input
-
 
N
D
I
 
s
o
u
r
c
e
 
i
n
p
u
t


-
 
T
e
s
t
 
p
a
t
t
e
r
n
 
g
e
n
e
r
a
t
i
o
n
,
 
i
n
c
l
u
d
i
n
g
 
a
 
b
u
i
l
t
-
i
n
 
t
e
s
t
 
c
a
r
d


-
 
S
l
i
d
e
 
d
e
c
k
s
:
 
a
 
P
D
F
 
i
m
p
o
r
t
s
 
a
s
 
o
n
e
 
i
m
a
g
e
 
c
u
e
 
p
e
r
 
p
a
g
e
,
 
r
e
n
d
e
r
e
d
 
a
t
 
i
m
p
o
r
t
 
b
y


 
 
t
h
e
 
p
l
a
t
f
o
r
m
'
s
 
o
w
n
 
e
n
g
i
n
e
 
s
o
 
n
o
t
h
i
n
g
 
d
u
r
i
n
g
 
a
 
s
h
o
w
 
d
e
p
e
n
d
s
 
o
n
 
a
 
d
o
c
u
m
e
n
t


 
 
r
e
n
d
e
r
e
r


-
 
A
 
c
o
d
e
 
s
o
u
r
c
e
:
 
a
 
l
i
v
e
-
c
o
d
e
d
 
e
x
p
r
e
s
s
i
o
n
 
e
v
a
l
u
a
t
e
d
 
p
e
r
 
p
i
x
e
l
,
 
e
d
i
t
e
d
 
w
h
i
l
e
 
i
t


 
 
r
u
n
s
,
 
w
i
t
h
 
s
y
n
t
a
x
 
c
o
l
o
u
r
i
n
g
 
a
n
d
 
a
 
c
o
m
p
i
l
e
 
e
r
r
o
r
 
t
h
a
t
 
n
e
v
e
r
 
b
l
a
c
k
s
 
t
h
e
 
o
u
t
p
u
t




E
f
f
e
c
t
s
 
a
n
d
 
M
i
x
i
n
g




-
 
A
 
p
e
r
-
c
u
e
 
e
f
f
e
c
t
 
s
t
a
c
k
 
o
n
 
e
v
e
r
y
 
k
i
n
d
 
o
f
 
c
u
e
,
 
o
r
d
e
r
e
d
,
 
w
i
t
h
 
c
o
p
y
/
p
a
s
t
e
 
o
f
 
a


 
 
w
h
o
l
e
 
c
h
a
i
n
 
b
e
t
w
e
e
n
 
c
u
e
s


-
 
T
w
e
n
t
y
-
s
i
x
 
e
f
f
e
c
t
s
,
 
e
a
c
h
 
w
i
t
h
 
i
t
s
 
o
w
n
 
n
a
m
e
d
 
p
a
r
a
m
e
t
e
r
s
,
 
a
l
l
 
m
e
a
s
u
r
e
d
 
t
o
 
f
i
t


 
 
i
n
s
i
d
e
 
a
 
6
0
f
p
s
 
f
r
a
m
e
 
a
t
 
1
0
8
0
p


-
 
S
i
x
 
t
h
a
t
 
e
x
i
s
t
 
n
o
w
h
e
r
e
 
e
l
s
e
:
 
s
c
h
l
i
e
r
e
n
 
g
r
a
d
i
e
n
t
 
i
m
a
g
i
n
g
,
 
C
h
l
a
d
n
i
 
n
o
d
a
l


 
 
f
i
g
u
r
e
s
,
 
a
 
t
r
u
e
 
w
a
v
e
 
e
q
u
a
t
i
o
n
 
w
i
t
h
 
i
n
e
r
t
i
a
,
 
c
r
y
s
t
a
l
 
g
r
a
i
n
 
g
r
o
w
t
h
,
 
r
e
t
i
n
a
l


 
 
r
o
d
/
c
o
n
e
 
p
e
r
s
i
s
t
e
n
c
e
,
 
a
n
d
 
s
t
r
u
c
t
u
r
e
-
t
e
n
s
o
r
 
g
r
a
i
n
 
f
l
o
w


-
 
A
n
 
L
F
O
 
o
n
 
a
n
y
 
p
a
r
a
m
e
t
e
r
 
—
 
s
i
x
 
s
h
a
p
e
s
,
 
f
r
e
e
-
r
u
n
n
i
n
g
 
o
r
 
l
o
c
k
e
d
 
t
o
 
a
 
t
a
p
 
t
e
m
p
o


-
 
V
J
 
m
o
d
e
:
 
a
 
s
e
c
o
n
d
 
d
e
c
k
 
l
i
v
e
,
 
a
 
c
r
o
s
s
f
a
d
e
r
 
w
i
t
h
 
d
i
s
s
o
l
v
e
/
a
d
d
/
m
u
l
t
i
p
l
y
,
 
t
a
p


 
 
t
e
m
p
o
,
 
a
n
d
 
t
a
k
e
s
 
q
u
a
n
t
i
s
e
d
 
t
o
 
t
h
e
 
b
e
a
t

Media Tools

- In-app conversion for files that will not play well, using hardware encoding
  where available and falling back to software
- Media inspection on import: codec, container, raster, frame rate, audio
  channels, sample rate and duration
- Missing-media detection with folder relink, so a moved drive does not cost you
  a rebuild

Interface

- Themeable, including high-contrast terminal themes suited to OLED panels
- Timeline with filmstrip thumbnails
- Resizable program monitor and timeline
- UI scaling

Download

Every release ships both a proper installer and a portable build for each
platform. Grab whichever you prefer from the releases page:

| Platform | Installer | Portable |
|----------|-----------|----------|
| Windows  | `...-windows-x64-setup.exe` (Start Menu, uninstaller, `.deckboy` files) | `...-windows-x64.zip` |
| macOS    | `...-macos-arm64.dmg` (drag to Applications) | `...-macos-arm64.zip` |
| Linux    | `...-x86_64.AppImage` (one file, `chmod +x` and run) | `...-linux-x86_64.tar.gz` |

Every build bundles everything it needs — the binary, ffmpeg, and its runtime
libraries. No separate downloads.

A note on the "unknown developer" warnings: Deckboy is free software and is not
code-signed (that means paying Apple/Microsoft, which rather defeats the point).
The builds are safe; the OS just does not recognise an unpaid publisher. One-time
steps:

- **macOS** — if it says the app is damaged or cannot be opened, clear the
  quarantine flag once:

      xattr -dr com.apple.quarantine /Applications/Deckboy.app

  Installing from the `.dmg` into Applications (rather than running from
  Downloads) is also what avoids Gatekeeper's App Translocation sandbox.
- **Windows** — SmartScreen may show "Windows protected your PC". Click
  **More info → Run anyway**.

On Linux the AppImage runs on any current distribution. Graphics (Mesa), the
display server, audio and the C/C++ runtime deliberately come from the host —
they must match the machine actually running, and a bundled libGL cannot load
your GPU driver. Built on Ubuntu 24.04 / Mint 22, so target something of that
vintage or newer.

Not everything is on every platform. Rather than hide it:

- Browser cues work on Windows (WebView2) and Linux (headless Chromium via
  Xvfb). On macOS the backend is a scaffold and browser cues do not run.
- Spout texture sharing is Windows-only. The macOS equivalent, Syphon, is not
  implemented, and the app reports it as unavailable rather than accepting
  frames and discarding them.
- Camera and window capture work on Windows (DirectShow / gdigrab) and Linux
  (V4L2 / x11grab). The macOS AVFoundation and ScreenCaptureKit backends are
  scaffolds.
- GPU zero-copy decode is Windows-only (D3D11VA). Other platforms decode on the
  CPU, which is comfortable on modern hardware.

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
