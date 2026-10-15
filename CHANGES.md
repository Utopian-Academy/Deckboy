# CHANGES - Incremental Updates (March-October 2026)

## 2026-10-15 - v0.99.412 (in progress)

### HDR video
- Fixed an issue where an HDR video (HDR10 or HLG) played black on an output using the software
  renderer, and an HLG video played dim and washed out on Windows. HDR video on an SDR output now
  has its reference white (203 nits) at SDR white, brighter highlights eased into white rather than
  clipped, and its BT.2020 colours converted to BT.709.
- Added `--devices` reporting whether each display is in HDR, with its SDR white level and headroom.
- Added HDR window outputs: `OUTPUT HDR ON|OFF|STATUS` and `OUTPUT HDR WHITE <nits>`. SDR material sits
  at 203 nits (BT.2408) and HDR video keeps its highlights; NDI, DeckLink and recordings from that
  output stay SDR. **Settings > Video Outputs > Screen > HDR** and **SDR material at**. Measured on
  Windows; macOS and Linux untested.
- Recording an HDR output now records HDR10 (HEVC Main10, BT.2020, PQ), highlights at their own
  brightness; ProRes and DNxHR HQX are tagged PQ.

### Test card
- Added a test card that checks a screen: drawn at the raster the output sends, never scaled, with
  1-pixel corner patches and a 1:1 / SCALED readout, a round circle, edge arrows, action and title
  safe, 75% bars, a grey scale and PLUGE, HDR patches from 203 to 2000 nits on an HDR output, a
  motion lane, phase blocks and a frame counter run from one clock across every output, edge-blend
  lines, and the output's number, raster, refresh, destinations and measured frame rate.
  **Settings > Video Outputs > TEST CARD** and **CARD ON ALL**; `OUTPUT TESTCARD ON|OFF|TOGGLE|ALL`.
- Added **EXPORT CARD** and `OUTPUT TESTCARD EXPORT`: the moving card as H.264 files, one per output
  of a canvas plus the whole canvas.
- A test card is no longer saved with the show, and a recording keeps recording programme while one
  is up.

### Desk
- Fixed an issue where Deckboy opened slightly too tall on a 1080p Windows screen, its bottom edge
  under the taskbar. The first window now fits inside the usable area, title bar included.

### Live captions
- Added live captions: what is being said, on the chosen outputs, about two seconds behind, made on
  this computer. Listens to the live input, a deck or an output's mix; one to three rolling lines,
  top or bottom; unsure words grey until they are sure; clears after six seconds of silence; an
  optional `.srt` transcript. **Settings > Audio > LIVE CAPTIONS**, and `CAPTIONS LIVE`,
  `CAPTIONS SOURCE`, `CAPTIONS SHOW`, `CAPTIONS LINES`, `CAPTIONS POSITION`, `CAPTIONS SIZE` and
  `CAPTIONS TRANSCRIPT`.
- The Audio tab of Settings scrolls when its cards do not fit.

### Picture scopes
- When an output is sending SDI or ST 2110, the scopes now measure the Y'CbCr 4:2:2 signal it puts on
  the wire, at the depth it opened, including the overshoot at saturated edges that 4:2:2 creates and
  R 103 looks for. The footer names the signal measured. Other outputs are measured as before.
- Fixed an issue where `DECKLINK ON` answered `OK` on a computer without Blackmagic Desktop Video and
  left DeckLink off. It now says why.
- DeckLink and ST 2110 now share one Y'CbCr encoder.

### Network control
- Fixed an issue where the phone remote answered any machine on the local network with the
  passphrase, even when that machine was not in **Trusted controllers**. It now answers only this
  computer and trusted machines, like every other control port; a refused phone is named in a warning
  with **ALLOW**.
- `WEBMONITOR SHARE`, `WEBMONITOR PIN` and `WEBMONITOR CONTROL` now work only from this computer,
  like `TRUST`: another machine could share the picture on the network under a passphrase of its own
  choosing.

## 2026-10-15 - v0.99.411 (picture scopes, network control locked down, a phone remote, 52 lower thirds)

Broadcast picture scopes measured on the output itself, network control that answers only the
machines you trust, a phone remote, 52 lower-third looks, and every edit reaches the show file even
when another program is holding it.

### Web Monitor
- Added PIN-protected phone and tablet controls at `/remote`, with `GO`, `STOP`, `NEXT`, cue
  selection and `DASH` buttons.
- Added **Phone control** in **Settings > Network > Web Monitor** and `WEBMONITOR CONTROL ON|OFF`.
  Control is off by default, requires a passphrase of 8 or more characters, and accepts private
  IPv4 LAN and loopback clients. Repeated wrong passphrases lock the page out for longer each time.
- Fixed an issue where `DASH <n>` reported `OK` when its assigned command failed or was unknown.

### macOS
- Fixed an issue on macOS where clicking anywhere away from a full-screen output (the desk,
  another app) showed the menu bar over it and could take it out of full screen. The output now
  stays full screen above the menu bar wherever focus goes.

### Picture scopes
- Added picture scopes in the programme monitor: the **PROGRAM** button switches it to **SCOPES**
  or **BOTH**. Diamond, luma waveform (% and mV), RGB parade, vectorscope with 75% and 100% targets
  and the skin-tone line, and a luma histogram, with the EBU R 103 gamut verdict and **FREEZE**.
- The scopes measure the focused output's own frame at its full raster, the frame NDI, DeckLink
  and ST 2110 send.
- Added `SCOPES`.

### Network control
- Fixed an issue where, with **Listen on** set to all interfaces, Companion, OSC, OSC Query, the
  HyperDeck emulation and the vMix API accepted commands from any machine on the network. Only
  this computer and the machines in **Settings → Network → Trusted controllers** are answered
  now; anyone else is refused and named, with an **ALLOW** button beside **Last refused**.
- Added **Companion passphrase**: a Companion client on another machine must send
  `AUTH <passphrase>` first. The Companion module 1.0.5 has a **Passphrase** field for it.
- Commands that write files at a given path, fake mouse input, open `file:` addresses or change
  who is trusted are refused from other machines, including inside `DASH` buttons and script
  cues sent from them.
- Added `TRUST` and `COMPANIONPASS`.

### Lower thirds
- Added 39 lower-third looks to the inspector's look picker and `TEXTCUE LOOK`, bringing the
  catalogue to 52 looks with individual entrances and exits.
- Added a saved Wiggle option in the inspector and `TEXTCUE WIGGLE ON|OFF|TOGGLE`; Arcade keeps its
  continuous bob and tilt.
- Added right-to-left layouts and joined-letter reveals for Arabic and Persian names across the new
  looks.
- The Equalizer look follows the programme's sound when the lower third plays on a separate
  graphics playlist.
- Fixed an issue where a lower third was blank at cue time zero, or stayed visible after
  `TEXTCUE OUT`, when its entrance or exit was set to `cut`.
- Fixed an issue where rounded lower-third edges looked faceted at 4K.
- Fixed an issue where Equalizer kept its last audio level when the programme host was paused or
  stopped.

### Cue geometry
- Fixed an issue where SCALE, SCALEX and SCALEY edited only one cue when several were selected.
- Fixed an issue where SCALEX and SCALEY ignored Link aspect.
- Fixed an issue where WIDTH and HEIGHT returned nothing when called without a value.
- Fixed an issue where the left geometry mode button stepped forwards instead of back.
- Added OFFSET, OFFSETX and OFFSETY in pixels, ROTATION in degrees, and CROP, CROPLEFT, CROPRIGHT,
  CROPTOP and CROPBOTTOM in percent. Each answers with the current value when called without one.
- Geometry setters report the applied value and how many selected cues were changed or skipped;
  an invalid value answers `ERR` and changes nothing.

### Video Synth
- Added `SYNTH ADD` to create and select a Video Synth cue in the focused playlist.
- Fixed an issue where a newly added Video Synth cue could be missing from the saved show when no
  later edit was made.

### Interface
- The bottom-bar buttons and other raised controls now show they were pressed: they sink the
  moment the mouse goes down, and a ring flashes on them when you let go.
- The mouse, cat and frog busy critters are whole animals now, walking or hopping, instead of
  heads bobbing side to side. Busy critters grow out of a single cell when a job starts and
  shrink back into one when it ends.
- Fixed an issue where the loudness critter could draw over the header when its cue row was
  scrolled partly out of view.
- MENU and DASH stay pressed in, with a ring, while the settings or the dashboard they opened are
  showing.
- Added `PRESS <x> <y>|<button>|OFF` to hold the pointer down without clicking, for checking a
  control's pressed look.

### Import
- Fixed an issue where **Import** hid PDF, PowerPoint, Keynote and OpenDocument slide files until
  **All files** was chosen. They are now in the media list and under their own **Slides** filter.

### UI checks
- Added `--ui-size <width> <height>` and `--settings-scroll <pixels>` for screenshot checks at a
  set window size and settings position.

### Show files
- Fixed an issue where autosaved edits could be lost when another application briefly held the
  `.deckboy` file open. Deckboy retries saving and reports a warning if it still fails; during a
  live show it never pauses the outputs to wait.
- Fixed an issue where recent edits could be lost when Deckboy closed before the autosave delay
  expired. Closing now waits for saving and keeps the desk open if saving fails.
- Added RECOVER and KEEP SAVED when opening a show with a newer `.deckboy.saving` copy. RECOVER
  opens the copy with outputs disarmed; Enter selects RECOVER and Escape selects KEEP SAVED.
- Added `--autosave-check <dir>` to check saving, retry, closing and recovery without visible
  windows. Use a new directory and set `DECKBOY_STATE_DIR=<dir>/state`.

## 2026-10-10 - v0.99.410 (Arabic, Persian and Sorani on Windows)

Arabic, Persian and Sorani now appear in the language picker on Windows.

### Interface and localisation
- Fixed an issue where Arabic, Persian and Sorani (Kurdish) were missing
  from the language picker on Windows. `--self-check` now reports all 43
  languages on every platform.

## 2026-10-10 - v0.99.409 (right-to-left languages, script fonts everywhere, Mini watch folder, NMOS discovery)

Deckboy now reads right to left, borrows a font for any script on any Linux, keeps your outputs exactly where you put them, and Mini learns to watch a folder.

### Interface and localisation
- New languages: Persian (right to left). New, marked unreviewed in the
  picker: Kurdish (Sorani, Kurmanji), Kabyle, Standard Moroccan Tamazight
  (Tifinagh). Kabyle and Tamazight fall back to French for untranslated
  strings, not English.
- Right-to-left text (Arabic, Persian, Sorani) is laid out with the Unicode
  Bidirectional Algorithm (UAX #9), checked against Unicode's conformance
  test data on every CI build. Numbers, ports and Latin words inside a label
  keep their own direction.
- Labels in Arabic script are shaped (joined letter forms).
- Reviewed line by line: Arabic, Chinese (Simplified, Traditional), Korean,
  Brazilian Portuguese. Turkish now uses ç, ğ, ı, İ, ö, ş, ü throughout.
- Dropdown and picker items are now translated: the stream deck button
  menu, tracker step actions, output and source names, the UI scale list.
  46 strings added in 26 languages.
- A label of the form "Name (qualifier)" with no catalogue entry of its own
  is translated as two parts, e.g. "Window (full screen)".
- Alienese II cypher: the whole desk, buttons and headers included, draws in
  the cypher's own symbols from a bundled font. Digits are unchanged; output
  text is never altered.
- Presenter notes and the prompter script are always shown as written,
  whatever the desk language or cypher.
- Remote: `LANGUAGE <code>` sets the desk language; `LANGUAGE` alone replies
  with the current one.
- `tools/audit_i18n.py` counts dropdown and picker labels; coverage figures
  are lower than before because these were previously not counted.

### Fonts
- Text in scripts the bundled fonts lack (Japanese, Chinese, Korean, Hindi
  and other Indic scripts, Arabic, Thai, Amharic, Tifinagh) draws on a desk
  in any language: cue names, file names, lower thirds, subtitles. Missing
  characters come from the computer's installed fonts, ordered by interface
  language for Han characters (Japanese vs Chinese forms).
- Bundled: Noto Sans Arabic, Noto Sans Tifinagh.
- Linux: fallback fonts are found through fontconfig (loaded at run time;
  no new build dependency), so these scripts draw on Fedora, Arch, openSUSE
  and others, not only Debian, Ubuntu and Raspberry Pi OS. Without
  fontconfig, the Debian font paths are used.
- `--self-check` prints `font-discovery: fontconfig` or `fixed list`, the
  fallback font chain, and which scripts this computer can draw.

### Output (lower thirds, text, subtitles)
- Arabic and Persian lower thirds are laid out from the right of the strap;
  the Typewriter move reveals from the right; Wobble keeps joined letters
  joined. Deckboy Mini's subtitles follow the same rules.

### Sources and patterns
- SOURCE > MADE BY DECKBOY: Fireside, Portal, Swirl, Terrarium and Terrarium
  Pico (once unlocked), Video Synth, Code, Text, Lower Third, Stage Timer,
  2A03 Synth, FDS Synth.
- New SOURCE > TEST SIGNALS: Test Pattern (cards and charts), Test Tone
  (line-up, pink noise, channel walk).
- PATTERN (P) lists test cards and charts only; Fireside and Swirl moved to
  SOURCE.
- Generated cues are named Fireside, Portal, Swirl, Code, Terrarium or
  Terrarium Pico. The inspector's source line reads "<name>  made by
  Deckboy", and the PLAYBACK pattern picker is hidden for these cues. The
  motion row appears only for patterns that have a motion variant.
- Bottom bar: DASH and MENU moved from OUTPUT into a new DESK group.

### Outputs
- Changed: Undo (Ctrl+Z) no longer reverts output settings: display, NDI,
  stream, DeckLink, Spout and ST 2110 senders, follow display, fixed raster,
  refresh, bit depth, canvas, recording size. Cue and warp edits remain
  undoable.
- Changed: inspector pixel values refer to the output the selected cue's
  playlist plays on, not the focused output.
- Changed: the toast after choosing a display reports the new display's
  raster.
- Fixed an issue where an output's display assignment reverted to an
  earlier display after Undo, while its window stayed on the chosen screen.
- Fixed an issue where the inspector's width/height (px), fit/fill, and the
  size of new Text, Browser and PiP cues used the assigned display's
  resolution instead of the resolution the output window draws at (e.g.
  3840x2160 shown for a 1280x720 output).
- Fixed an issue where undoing OPEN or NEW wrote the previous show into the
  newly opened show's file.
- Fixed an issue where the program monitor placed Unscaled cues and
  position offsets differently from the output.

### Desk
- The watch folder (Settings > WATCH FOLDER, remote `WATCH`) now also
  requires the modification time, not only the size, to hold still between
  scans before a file is taken.
- Fixed an issue where files with no extension (README, LICENSE) in a
  dropped or watched folder were imported as image cues that could not be
  read.

### Deckboy Mini
- New: watch folder. `--watch <dir>`, Shift+W (toggle; asks for a folder the
  first time) and remote `WATCH [<dir>|ON|OFF|TOGGLE]` (the desk's
  `WATCH 1 <dir>` form is accepted). New files are appended in name order;
  a file is taken once its size and modification time are unchanged across
  two scans (2.5 s apart). Not recursive. Files already in the list are
  skipped.
- `--watch` with no files starts Mini with an empty list; the first arrival
  plays unless `--paused` is set.
- `WATCH` with no argument replies with the folder and the counters
  `scans=`, `seen=` and `taken=`.

### NMOS (IS-04 / IS-05)
- New: registry discovery over IPv4 mDNS (`_nmos-register._tcp` and
  `_nmos-registration._tcp`) when the registry URL is blank and REMOTE ON is
  set. Accepts HTTP, IS-04 v1.3, `api_auth=false`; lowest TXT `pri` first,
  ties chosen at random. A registry that stops answering is avoided for 30 s
  and discovery runs again.
- Settings shows the registry in use as `typed: <url>` or
  `found: <host:port> (mDNS)`. `NMOS STATUS` adds `registry=` and
  `registry_source=typed|mdns|searching|none`.
- With LOCAL ONLY set, neither discovery nor registration to a typed
  registry takes place.
- Not supported: unicast DNS-SD, IPv6, HTTPS, authorization, peer-to-peer.

### Known issues
- Some of the interface is still English in every language: the main
  catalogues cover about three quarters of the drawn labels, and the PATTERN
  (test card) picker is not translated yet.
- On a desk in another language, a cue named exactly like an interface label
  (e.g. "Camera", "STOP") is shown translated, and so is the name part of a
  cue named "Name (qualifier)". The cue's real name is unchanged; the English
  desk always shows it as typed.
- NMOS registry discovery has been tested against a local test registry, not
  yet against vendor registries. IPv4 only.
- On a Linux machine with no display (offscreen video driver), Deckboy Mini
  can keep running for some seconds after `QUIT`.
- On Windows, an autosave can occasionally fail to replace the show file, most
  often as Deckboy closes; the newest state is then left beside the show as
  `<show>.deckboy.saving`. If that file is newer than the show, close Deckboy
  and rename it over the show file.

## 2026-10-09 - v0.99.408 (fullscreen on macOS, Deckboy Mini runs the order your way)

### Deckboy Mini

- New: cue search. Press `/` and type to filter the list, best match first;
  `Enter` takes the selected cue.
- New: `N` queues the selected cue after the cue on air, ahead of the list.
  `E` stops at the end of the cue on air. The panel's NEXT line shows what
  will happen next.
- New: `Z` shuffles, playing every cue once before repeating; `G` enables
  crossfades from 0.5 to 2 seconds; `C` shows time remaining. Each also has
  a remote command.
- New: opens `.pls` playlists and saves them beside the original as `.m3u8`.
- Changed: `W` saves a playlist opened at launch back to that playlist.
- Fixed an issue where text did not fit on pages of the Mini booklet. Added
  pages for the new keys.

### Desk

- Changed: playlist tabs show default names without a number prefix
  (`Deck 1`, rather than `1 Deck 1`). Renamed playlists retain the prefix.

### macOS

- Changed: fullscreen output and desk windows fill the screen immediately
  without moving into a separate Space.
- Switching an output between following the display and a fixed raster or
  refresh rate is safe, including immediately after entering fullscreen.

### Packaging

- New: separate Mini downloads for Windows, macOS (Apple Silicon and Intel),
  Linux and Raspberry Pi (64-bit). Mini remains included in every full download.

## 2026-10-09 - v0.99.407 (colour checked on every platform, the desk keeps its words)

### Fonts

- Fixed an issue where interface text disappeared when a font could not be
  opened during a resize or language change. Existing lettering is retained;
  headers and buttons fall back to the plain face. The render log names the
  font that could not be opened.

### Lower thirds

- Changed: Scroll unrolls from both ends on entry and rolls up on exit, timed
  to its move.

### Desk

- Changed: after a short cue such as a lower third, the idle monitor face
  waits 10 seconds before returning gradually.

### Outputs

- Every build compares programme colour against independently decoded
  references for 8- and 10-bit SDR, in limited and full range, on Metal
  (macOS), OpenGL (Linux) and Direct3D 11 (Windows).

### Linux

- `tools/linux_build.sh --mini` builds Mini alone and limits parallel
  compilation to available memory, including on Raspberry Pi 3 while other
  software is running.

## 2026-10-08 - v0.99.406 (captions made here, triggers, MediaMTX, CHECK as one list, Mini runs a show from the keyboard)

### Captions

- New: inspector > CAPTIONS > make captions here generates captions locally,
  writes `<name>.auto.srt` beside the media, attaches it and enables it.
- Choose English or any language, with quick or more accurate recognition.
  The first use shows the speech model's download size and asks before
  fetching it; subsequent use works offline.
- whisper.cpp is bundled on Windows, macOS and Linux.
- Output captions scale to the output and use a box fitted to the line.

### Cues

- New: TRIGGERS on every cue. A hotkey (`F1`-`F10`, `F12` or number pad),
  MIDI note, OSC address, time of day or timecode point takes the cue through
  the same action as GO without moving focus. Press a key or note to learn it.
- New: memo cues show a running-order note on GO without changing playback.
- New: devamp ends a looping cue after its current pass, as if looping were
  disabled. Available from a target cue or remote `DEVAMP`.
- Changed: with multiple cues selected, gain applies the same relative change
  to each cue; normalize measures each cue separately.

### MediaMTX

- New: MediaMTX streams appear in SOURCE as one-click stream cues.
- `MEDIAMTX PUBLISH` sends the programme to MediaMTX for any number of viewers
  in browsers, vMix or recorders.

### Desk

- Changed: CHECK opens one list of show issues; selecting a line goes to the
  affected item. Added checks for disconnected playlist audio devices,
  missing cue plugins and tracker steps that fire nothing.
- Fixed an issue where clicking a cue could start a drag before the pointer
  moved.
- Changed: faders move relative to where they are grabbed; `Alt` jumps to
  the pointer.
- Changed: while the show is live, keys for audio device, display, NDI,
  timecode, clear and fullscreen require a second press.
- Undo and redo preserve playback on air.
- Fixed an issue where the idle monitor face's smirk separated over time.

### Outputs

- Area of interest width, height and position accept pixel values.
  `pixel for pixel, top-left` sends the area at its native size to the
  corner mapped by an LED processor.
- Click a grid-warp handle to select it, then type its position or move it
  one pixel at a time with the arrow keys.

### Lower thirds

- New: Split, Card, Hairline, Sparkle, Neon, Comic and Scroll. The picker
  groups clean and playful looks.

### Audio

- New: AUDIO > track selects among a file's audio tracks, including alternate
  languages and commentary. Remote: `AUDIOTRACK`. Changes to the cue on air
  take effect immediately.

### Deckboy Mini

- New: keyboard controls to load and save M3U8 playlists, move, remove and
  rename cues, select and switch outputs, and select the audio device.
  `?` shows four help pages.
- New: speed, mute, A-B loop, frame step, audio track and subtitle controls,
  including embedded subtitles.
- Mini builds independently on Raspberry Pi and uses the board's hardware
  video decoder, reducing processor use by about 40%.

### macOS

- Every bundled library targets macOS 12 Monterey or later; releases check
  compatibility file by file.
- Bundled ffmpeg gains HAP and SRT support, matching Windows.

### Interface

- Icons and buttons no longer depend on the media tools. If ffmpeg or ffprobe
  cannot start, an in-app notice names the tool and reason; the desk keeps
  running.
- Fixed an issue where prompts could cut off instructions. NDI, Blackmagic,
  WebView2, macOS first-run and HAP suggestion prompts wrap and size to fit
  their text.
- A few seconds after startup, Deckboy reads back labels from its window and
  records whether they reached the frame, plus window size and density, in
  the render log.

## 2026-10-05 - v0.99.405 (the monitor shows the show, loading at every scale)

### Desk

- Fixed an issue where the program monitor did not show a live picture with
  some decoders. It shows the picture as soon as it is available; the idle
  face returns only when nothing is live.
- Fixed an issue where timeline filmstrip and waveform loading animations
  overlapped at 150%, 200% and larger UI scales. Film cells, meter bars and
  the LOADING label now scale with the interface.

### Outputs

- Fixed an issue where Video Outputs > Geometry could not scroll down to
  Default Transition.

## 2026-10-05 - v0.99.404 (Deckboy Mini, remote takes name a real cue, first save asks where)

### Deckboy Mini

- New: `deckboy-mini`, a one-deck player bundled with the desk. Plays clips,
  stills and folders fullscreen through the same playback engine, in name
  order, with still holds and optional looping.
- Accepts the desk's remote commands on the desk's port; Companion controls
  it as deck 1.
- The terminal shows a Game Boy-green status panel with audio-reactive text.
  `H` shows a status bar on the output.
- Drop files on the output or send `ADD` remotely to append cues during play.
- Terminal keyboard controls work over SSH: select cues, type cue numbers,
  seek, change volume, add files with `Tab` path completion, or enter remote
  commands. Manual section 25.

### Remote protocol

- Changed: `TAKE <cue>` and `SELECT <cue>` from Companion, OSC, MIDI or
  Art-Net act only on existing cues. An invalid cue returns `ERR` with the
  deck and cue count, leaving the deck unchanged.

### Show files

- Changed: the first `SAVE` or `Ctrl+S` on a new show opens the save dialog;
  subsequent saves write to that file.
- Changed: `NEW` asks twice before replacing a never-saved show with cues.

### Companion

- Module 1.0.3: buttons follow Deckboy smoothly across networks, indicate a
  lost connection and reconnect automatically.
- Fixed an issue where a button naming its deck changed the focused deck.
- Fixed an issue where cue names containing quotes were displayed incorrectly.

## 2026-10-04 - v0.99.403 (lip sync for long shows, Web Monitor sources, readable themes)

### Web Monitor

- Changed: the browser player uses HLS with a shared picture/audio timeline,
  a short network buffer and automatic reconnection. Supports portrait and
  landscape phones, fullscreen controls and keyboard navigation.
- Uses native browser HLS where available, otherwise the bundled player;
  playback makes no CDN requests.
- New: Video Outputs > Source routes the programme, multiview, presenter view
  or another output to Web Monitor. Its card shows the current source.
  Remote: `WEBMONITOR SOURCE PROGRAMME|<output>`.
- Programme viewers target the same wall-clock moment, keeping phones,
  laptops and TVs within a few milliseconds while retaining sound.
- New: Low latency sends picture only, without buffering.
- New: Connections at once limits simultaneous browser requests. Default: 8,
  serving roughly 10-20 viewers.
- New: QR CODE in Web Monitor settings shows the address as a large QR code.
  Remote: `WEBMONITOR QR`. The code contains only the address; the phone
  still asks for the passphrase.
- When encoding cannot keep up, Web Monitor switches to a lighter encoder,
  then 720p, then 540p, and reports the change.

### Outputs

- Every stream and recording output gains the same Source choice.
- The multiview output follows the desk's arrangement, including windows,
  programme tile, labels and level meters, scaled to the output.
- Fixed an issue where streams, recordings and Web Monitor lacked deck audio
  on macOS and Linux. They now carry it as on Windows.
- Changed: Orientation uses a full-height control in Video Outputs.
- `VIDEO OUTPUT ADD` accepts `MULTIVIEW`, `PRESENTER` and `PROMPTER`.

### Playback

- Fixed an issue where picture and sound drifted apart during long shows,
  then caught up in a jump. Video now follows the sound card clock
  continuously, correcting differences smoothly.

### Patterns

- Swirl gains palettes (`classic`, `fire`, `ice`, `neon`, `mono`), colour
  bands, zoom, flow, a movable centre and reverse speed, in the inspector and
  remotely. Existing Swirl cues retain their appearance.

### Interface

- New: 8 splash scenes, shown in their own colours in every theme.
- Labels enforce readable contrast against the surface behind them in all
  30 bundled themes.
- Help notices use the theme highlight; warnings use its danger colour with
  a brief pulse. Notices also appear with interface animations disabled.
- `--contrast-check` renders every bundled theme with both notice kinds and
  reports insufficient contrast.
- Changed: narrow cue rows use one heading line and a full bottom line for
  their 5 toggles, growing taller as needed. Cue kind uses its icon when the
  word does not fit.
- Fixed an issue where the idle face returned only before the session's first
  cue. It now returns whenever nothing is live.
- Changed: timeline and show-loading animations use their earlier designs
  and critter positions.

## 2026-10-03 - v0.99.402 (Web Monitor network access and quality)

### Web Monitor

- Changed: network sharing selects an active private Ethernet or Wi-Fi
  address, binds only to it and reports when none is available. It no longer
  relies on the default route, which could select a VPN.
- Network sharing requires a passphrase of at least 8 characters; letters
  and symbols are accepted. Concurrent browser connections are limited.
- The firewall must allow the selected port on the private network.
  Web Monitor uses unencrypted HTTP: use a trusted LAN and do not forward
  the port to the public internet.
- New: maximum resolution of 720p, 1080p, 1440p or 2160p. Default: 1080p.
  Smaller programme pictures are not upscaled.

## 2026-10-03 - v0.99.401 (SDR colour and programme monitoring)

### Windows

- Fixed an issue where SDR colour or video range was not preserved on
  programme output. 8- and 10-bit SDR use source colour metadata while
  playing and paused; Main 10 SDR uses the cached NV12 path on D3D11 with
  its SDR transfer instead of an HDR transfer.

### Web Monitor

- Enabling Web Monitor includes programme picture and stereo sound. The
  main address opens the programme player; SOUND ON enables audio when a
  browser requires a click.
- Disabling Web Monitor also disarms the programme encoder.
- Individual output views remain at `/outputs`.

### Outputs

- Colour checks compare primary programme pixels against independently
  decoded 8- and 10-bit BT.709 SDR references, in limited and full range,
  while playing and paused.
- Checks also verify video and stereo 48 kHz audio in the web programme
  stream.
- The compositor remains SDR. Convert PQ/HLG sources to SDR before playout.

## 2026-10-02 - v0.99.400 (live looks, steady transitions, and a Swirl you can shape)

### Effects and transitions

- Changed: keying, grading and picture effects update the playing or paused
  cue immediately.
- Every transition preserves the outgoing cue's processed look, opacity,
  fit, crop, scale, rotation and layer corner pins.
- Push transitions move each picture once; wipes and iris reveal the next
  cue throughout the transition.

### Playback

- Main 10 SDR holds a steady picture between decoded frames.

### Sources and patterns

- Swirl gains live inspector controls for speed, twist, pixel density and
  colour. All 4 are saved with the show.
- Remote: `SWIRL` reports the settings; `SWIRL SPEED`, `TWIST`, `PIXELS`
  and `COLOUR` set them.
- Code sources use their editable render width and height independently of
  output resolution, and retain the last good picture during incomplete edits.

### Desk

- Changed: narrow cue rows give names and timers more room. When several
  usable columns cannot fit, decks use a full-width focused list.
- Changed: CLEAR immediately stops every deck, cancels queued takes and
  disarms output.
- Animated critters accompany timeline video/audio loading, previews, show
  opening, file conversion, imports, slide rendering and updates. Labels,
  page counts and measured progress remain visible.

### Audio and outputs

- Recordings and network outputs receive independently routed live-input
  audio.
- Programme metering shows integrated LUFS and true peak. Normalization
  offers -16, -23 and -24 LUFS targets.
- SDI sends scheduled video and audio with BT.709 conversion and 8- or
  10-bit packing.
- ST 2110 senders apply independent video and audio transport settings,
  bounded audio queues, source binding and clock reacquisition.

### Companion

- Traffic is bounded so a stalled controller cannot hold up playout.

### Release checks

- Required checks cover programme recording, every transition type, Main 10
  SDR playback, transport lifecycle and malformed clock packets.

## 2026-10-02 - v0.99.399 (text on every Mac, Windows decodes in-process, the web monitor, and a clicker that takes)

### Audio

- New: audio devices up to 128 channels per deck, with crosspoint routing
  to every channel. ASIO interfaces can be armed at 32, 64 or 128 outputs.
- Fixed an issue where deck output widths above 8 channels were not retained
  after saving and reopening a show.

### macOS

- Startup checks whether interface text reaches the screen and changes the
  drawing path automatically before showing the window if needed.
- Results are written to `deckboy-render.log`.
  `DECKBOY_RENDERER=software` forces software rendering from a terminal.

### Transitions

- Fixed an issue where a letterboxed cue snapped to full frame when a
  crossfade started. Letterbox bars now fade with the cue.
- Fixed an issue where transitions became cuts when output video used GPU
  decoding. All transition types now draw on that path.

### Desk

- Changed: `Page Down` and `Page Up` take the next and previous cues with the
  deck's transition, as TAKE does. They also work when a fullscreen output
  takes focus on the same screen.

### Web Monitor

- New: Settings > Network > Web Monitor shows any output live in a browser
  on a laptop, tablet or phone, with nothing to install.
- Starts locally; a switch enables network sharing and an optional PIN
  restricts access.
- Programme with sound provides picture and sound about 1 second behind
  the show. Any output can also stream to the `WEB` destination.
- Outputs consume no monitoring resources until someone watches.
- Remote: `WEBMONITOR`, `WEBMONITOR ON|OFF`, `SOUND ON|OFF`, `SHARE ON|OFF`,
  `PORT <n>`, `PIN <pin>|OFF`.

## 2026-10-01 - v0.99.398 (SRT feeds with their sound, grid warp, and warp you can find)

### Sources

- Fixed an issue where SRT cues played without sound. Picture and audio now
  use one connection and play together at the feed's incoming size. Channels
  imported from `.m3u` playlists also carry audio.
- New: Swirl pattern, after Pocket Test in the pattern picker. A slow spiral
  of melting pixel greens with a yellow crest, matching Deckboy's trailer,
  for holding screens or backgrounds behind lower thirds.
- Source commands accept full names with or without quotes:
  `NDICUE "WINDY (Graphics PC)"` and `NDICUE WINDY (Graphics PC)` both find
  the sender. This applies to every source verb that takes a name.

### Cues

- Fixed an issue where `CONTINUE FOLLOW` set to `from end` did not take
  the next cue when the current cue held its last frame. Hold remains the
  default.
- Fixed an issue where the desk stuttered when a MIDI file cue's
  port was held by another program. Deckboy retries every couple of seconds
  and opens the port when it becomes free.

### Remote protocol

- `STATUS` adds `audio_level=` for each deck: `0-100` on the VU scale.

### Outputs

- New: `GRID` in the warp toolbar, with `3x3` to `9x9` points. Drag points
  to map curved surfaces with smooth interpolation or folded surfaces with
  straight lines. Grid warp applies on top of the existing corner pin.
- Remote: `WARP GRID <n>`, `WARP GRID SMOOTH|STRAIGHT`,
  `WARP POINT <col> <row> <dx> <dy>`.
- The programme monitor's `WARP` button is sized for every interface scale.
  Settings > Video Outputs > Geometry adds a WARP section that opens the
  editor.
- The warp editor identifies whether it is editing the whole programme on
  an output or one layer's corner pin.

## 2026-10-01 - v0.99.397 (perspective layer mapping, levels and curves, and an arcade lower third)

### Desk

- New: Settings > About > REPORT A PROBLEM opens a GitHub issue prefilled
  with the version, platform and renderer backend. The operator can review
  and edit it; nothing is sent until they press Submit on GitHub.
- Fixed an issue where NMC IN & OUT and TALLY on Settings > Network, and
  WATCH FOLDER on Settings > System, could fall below the window with no way
  to reach them. Both tabs now scroll, as Video Outputs already did.
- Fixed an issue where the update-download indicator disappeared when
  switching away from Settings > System. It remains visible on every tab.
- Changed: File > New warns before discarding unsaved changes, including
  when the show is not live.
- Recent-show rows identify the containing folder as `in <folder>`.
- New splash scenes: beach bonfire and rooftop party, in the cycling rotation.

### Cues

- VideoSynth cues now have brightness, contrast, saturation and hue controls,
  matching Video, Image, Pattern, Browser, PiP and source cues.
- Fade cues can ramp brightness, contrast, saturation and hue, in addition
  to opacity, volume and the master dimmer.
- Adding a master cue from ADD A CUE opens the Dashboard's Tracker view
  to assign its targets.
- New: MOVE TO DECK... in a cue's right-click menu. Dragging still reorders
  cues within a deck.
- The preset picker adds TAKE DECK... and STOP DECK..., each with a picker
  of the show's playlists.
- Changed: ADD A CUE and the `PIP` remote command no longer create
  picture-in-picture cues. Existing PiP cues still load and play. A scaled
  layer from a second deck provides picture-in-picture with a full playlist.
- Fixed an issue where a master cue added remotely went to the focused deck
  when a tracker sequence was running. It now joins that sequence, as
  ADD STEP does.
- Fixed an issue where long text exceeded its available width on outputs
  and the main control-window preview. Text now fits both width and height.
- Fixed an issue where Fireside rain and snow did not fall downwards.

### Outputs

- New: layer corner-pin handles in the programme monitor. Focus a playlist
  layered on the output, press `WARP` and drag its four corners. Handles
  follow the cue's size and offset; `EDIT` switches between layer and output
  warp.
- New: Perspective layer mapping keeps straight lines straight and applies
  foreshortening without folding along the diagonal. New pins use
  Perspective; previously saved shows retain Linear mapping.
- Remote: `VIDEO OUTPUT LAYERWARP MODE linear|perspective`.
- Fixed an issue where selecting a layered playlist changed the focused
  output and programme monitor. Both remain on the layer's output, including
  while recording or streaming.

### Lower thirds and text

- New: Arcade lower third, with a pixel-font name, monospaced role, bordered
  box and hard drop shadow. A slow bob and slight tilt continue while it is
  visible, follow the cue's clock, support scrubbing and stay synchronised
  across outputs.
- New colour: `forest`, paired with a yellow accent for the Arcade look.
- Remote: `TEXTCUE BAR <colour>`, `TEXTCUE ACCENT <colour>`,
  `TEXTCUE SCALE <0.5-2>`. `LOWERTEXT` and `LOWERSUB` set the two lines
  of the lower third created by `LOWERTHIRD`.
- Fixed an issue where text edges were not clean on transparent frames,
  including lower thirds.

### Effects

- Fixed an issue where adding the first effect, grade or key to a live cue,
  or removing the last one, interrupted playback or delayed the change.
  The look now changes on the next frame; paused cues update immediately.
- New: Levels and Curves, the first two entries in the effect list. Levels
  provides midtones displayed as gamma, black point, white point and lift.
  Curves provides an S-curve or flattening on all channels or red, green or
  blue individually, with shadow lift and highlight roll-off.
- Both effects start neutral and support LFO modulation.

### Stage timer

- New: transparent backdrop and a GEOMETRY section to size and position
  the timer over another playlist's programme.
- Remote: `TIMER BACKDROP transparent | default | #rrggbb`.

## 2026-09-30 - v0.99.396 (text survives a second Mac, and a transition keeps its shape)

### macOS

- Fixed an issue where the entire interface could have no text on macOS,
  including Tahoe 26.6.1 on Apple Silicon (issues #7 and #6). Text now uses
  streaming textures, as image and video frames do. The issue persisted on
  a second Mac after the earlier Metal preference and label-cache changes.

### Diagnostics

- The GPU backend used by each window and any text-texture creation failure
  are always logged to `deckboy-render.log` in the state directory.
  No diagnostic flag or terminal is required.

### Transitions

- Fixed an issue where an outgoing cue adopted the incoming cue's scale,
  crop and offset as soon as a transition started. Each cue keeps its own
  geometry throughout; letterboxed cues no longer flash to full frame or
  the incoming cue's geometry.

### Interface

- Increased the brightness of soft text colours on nine themes: Virtual
  Boy, Famicom, Metroid, Ganon, Midna, GameCube, Super Famicom, Star Fox and
  Switch Neon. Soft labels now have greater contrast against panel tiles.

## 2026-09-27 - v0.99.393 (thirty-one languages, three new transitions, and the tracker you already owned)

### Sources

- Live-coded sources accept a fourth expression for alpha after red, green
  and blue, allowing transparent shapes over another picture.
- New editable presets: gem, gem cluster, flash, cloud and ghast.
  New expression: `noise(x, y)`.

### Effects and transitions

- New: Feedback Bloom, with hue turn, melt, zoom and swirl controls.
  Colour shifts and warping accumulate on each feedback pass, producing
  iridescent trails; best used over dark images.
- New transitions, selectable per deck or per cue: Shatter, Clouds and
  Ghastly. Shatter sends rotating shards out in waves with lit edges;
  `gems` and `glass` are aliases. Clouds rises through the frame, replacing
  the picture in its densest areas with a bright fringe. Ghastly desaturates
  to cold blue-grey, smears upwards and replaces the picture with rising wisps.

### Media

- New: SOURCE > Browse media... lists folders first, then playable files.
  Selecting a file adds it to the focused playlist and keeps the browser in
  the same folder. Type to filter.
- New: Settings > System > WATCH FOLDER. Each playlist can watch its own
  folder and import arriving files while the show runs. Imports wait for
  copying to finish; files are taken only once, and deleted cues stay deleted.

### Audio

- New: per-cue EQ band with frequency, gain, width and bell or either shelf shape.
  New bands start flat; stack three or four for a full EQ strip.
- Settings > Audio offers `44100`, `48000`, `88200`, `96000` and
  `192000 Hz`. The device, decoder, cue-chain filters and delays, and
  video clock all follow the selected rate.

### Remote protocol

- New: Settings > Network > vMix API serves the vMix HTTP API on port
  `8088` and text protocol on port `8099`, for existing Stream Deck
  plugins, Companion modules and touch panels.
- Each vMix input maps to a Deckboy cue, numbered across all playlists,
  including per-cue tally.
- New: `AVJUMP` on the control port counts stalls and playhead jumps.

### Outputs

- New: Settings > Video Outputs > Devices > KEY+FILL sends picture on one
  DeckLink card and a greyscale alpha matte on a second card selected as
  Key out. The output composites over transparency with premultiplied fill
  for hardware keyers.

### Windows

- LTC generation, LTC chase and timecode cues are now available; the required
  library is bundled.
- The encoder now offers HAP, HAP Alpha and HAP Q, with an ffmpeg build
  that supports encoding them.

### Playback

- Fixed an issue where heavy media held briefly and then rushed at the start
  of a cue. The playback clock now waits for a few decoded frames of headroom;
  the picture itself is not delayed.
- The pointer hides after `3` seconds without movement over the program
  monitor or an output window. Moving it makes it visible again.

### Interface and localisation

- Expanded the catalogues in the 31-language release to cover around 90% of
  drawn text: German, French, Spanish, Cuban Spanish, Italian, Dutch,
  Brazilian Portuguese, Polish, Danish, Finnish, Icelandic, Scots, Gaelic,
  Greek, Bulgarian, Russian, Ukrainian, Turkish, Japanese, Simplified and
  Traditional Chinese, Korean, Arabic, Hindi, Zulu, Lumeni, Klingon and
  Sindarin.
- Missing scripts use fonts installed on the machine.
  `--font-check <language>` reports whether a suitable font was found.
- Broadcast terms `TAKE`, `PROGRAM`, `NDI`, `SRT` and `OSC` remain
  English in every language.
- Navajo, Mohawk and Muscogee are marked `(unreviewed)` in the picker.
  No speaker has reviewed them; untranslated text remains English.
  Contributors can edit the plain-text language catalogues.
- Fixed an issue where language names were unreadable when the current
  language's font lacked their script. Entries now include an English name,
  for example `Japanese - 日本語`.
- Fixed an issue where the transition-style button showed garbled
  characters instead of its arrow.
- Fixed an issue where the activity indicator was hidden underneath its
  panel.

### Dashboard and documentation

- Documented the existing master tracker in manual section 11a:
  `Ctrl+D` opens the dashboard and TILES / TRACKER switches views.
  Steps run down the grid and playlists across it; each step can fire a cue
  on every playlist, with its own length, countdown and LOOP.
  `Page Up` / `Page Down` support a presenter's clicker.
- Added nine existing shortcuts to the `Ctrl+/` overlay, including
  `Ctrl+Enter` (take all decks), `Ctrl+Space` (play/pause all decks),
  `Ctrl+Shift+Space` (stop all decks) and `Ctrl+A` (select the playlist).
  The manual's key table is generated from the same list.

### Known issues

- DeckLink key-and-fill output has not yet been tested on hardware.

## 2026-09-25 - v0.99.379 (Companion module 1.0.2, and the animals stay off the picture)

### Companion

- Updated the module to `1.0.2`.
- Fixed an issue where variables were blank for playlists and outputs above
  `4`, although actions could address `16`. Every addressable playlist
  and output now has variables.
- Fixed an issue where tally and countdowns froze after a status request
  went unanswered. Polling now continues.
- Action text, including text from another module's variable, is sent as
  exactly one command.
- Added an in-module help page with connection troubleshooting.

### Interface

- Fixed an issue where theme creatures remained over the program monitor
  when a cue went live. They leave the idle monitor as soon as it is in use.

## 2026-09-25 - v0.99.378 (Presets, portals, and a lower third you can watch yourself make)

### Presets

- New: save the desk's current state and recall the whole show or selected
  parts.

### Sources and effects

- New: Portal source and transition. Neon-rimmed space blobs have transparent
  gaps for layering over another playlist; the transition opens them through
  the outgoing picture.
- New: Time cube stacks recent clip frames into a rotating volume. The front
  shows the current frame and the sides show time streaks. See-through
  renders the whole volume.
- New: geometry oscillators on width, height, X/Y offsets, rotation and all
  four crops. Controls match effect modulation: shape, rate or beats, depth,
  tempo lock and a drawable curve. Stored values stay unchanged; modulation
  applies when drawing.

### Lower thirds

- Name and role on a transparent frame, played from a separate playlist over
  another. Includes five looks, nine colours and eight entrance/exit styles.
- Fixed an issue where lower thirds were not visible in the program monitor
  or multiview when no output was armed. Both now preview them while editing.

### Outputs and audio

- New: per-output crossfader between any two entries in that output's stack.
  It uses the layer's blend mode: Dissolve fades out the outgoing side;
  other blends bring the incoming side over it.
- New: monitor one playlist on a chosen device while the room hears the
  show. A playlist can be excluded from the room and remain on the monitor.

### Dashboard

- The master tracker plays steps down the page across playlists, with
  editable dashboard buttons.

### Desk

- Fixed an issue where toggling NMC sync froze the app.
- Fixed an issue where a typo in a live-code cue closed the app.
- Fixed an issue where reading encoder status remotely conflicted with
  updates to the encode queue.
- Small windows now reduce the interface scale to fit.

### Linux

- Fixed an issue where builds in `build/linux` could not find their data
  folder.

## 2026-09-24 - v0.99.377 (Master cues as a tracker, and every button says what it is)

### Outputs

- New: `VIDEO OUTPUT LAYERWARP` sets a layer's corner pin with eight
  fractions clockwise from the top left, or disables it with `OFF`.
  Coordinates are relative to the layer's own rectangle and remain valid
  when it moves or resizes.
- Layer mapping applies to programme output, recordings, streams, NDI
  senders and secondary programme screens. Mirrored destinations composite
  the same mapped layers as their source.
- Added a programme-recording check that compares corner patches to verify
  the rendered pin.

### Dashboard

- New: Tracker view shows master-cue steps down the page and playlists
  across it. Each cell selects the cue that step fires from a list, exposing
  the sequence's existing per-playlist instructions.
- Open with `Ctrl+D`; switch TILES / TRACKER in the dashboard header.

### Interface and diagnostics

- Icon-only buttons fall back to a short label when their image is
  unavailable, including playlist `LOOP` and `ONCE`.
- New: `--image-check` checks whether the machine can draw interface icons
  and splash art. It verifies ffprobe and ffmpeg are reachable, checks that
  decoding returns a picture, and identifies the failing step.
  Complements `--font-check`.

## 2026-09-24 - v0.99.376 (Projection mapping end to end, and a monitor of your own)

### Outputs

- New: per-layer corner pins in addition to output warp, allowing several
  surfaces to be mapped from one projector. Layer corners are relative to
  the layer and retain their alignment when it moves or resizes.
- Fixed an issue where mapping controls could address a different output
  from the one showing the picture. Corner dragging, RESET, presets,
  copy/paste, edge blend, the WARP button's lit state, remote `WARP` and
  `BLEND` replies, and control-surface mapping status now use the
  destination output.

### Audio

- New: listen to one playlist on a separate monitor device while the room
  hears the show. Monitoring can follow the focused playlist or stay pinned
  to one. Switching is immediate without reopening audio devices or clicks.
- A missing named monitor device stays silent and reports its absence;
  it does not fall back to the room's device.
- Playlists can be excluded from the PA while remaining audible on the
  monitor, including playlists feeding a second screen.
- Both playlist switches are in the routing menu; choose the monitor device
  in Settings > Audio.
- Remote: `MONITOR` reads and sets monitoring and reports the audio engines'
  actual state.

## 2026-09-24 - v0.99.375 (A multiview you arrange yourself, and a window by the hearth)

### Multiview

- Changed: multiview windows can be assigned individually. Right-click a
  window to show the programme, any playlist or nothing, and to add safe
  areas (action `90%`, title `80%`) and an audio meter.
- Add or remove windows, or reset to one per playlist. Existing shows keep
  their layout until it is changed.
- Remote: `MULTIVIEW WINDOW` reads and arranges windows.

### Outputs

- The source-deck row now opens a playlist menu for direct selection.

### Sources

- Fireside adds garden, rain, snow and sea views through a window on the
  hearth wall. Choose one in the cue's FIRESIDE row; it uses the fire's
  coarse grid and leaves the fire unchanged.
- `--pattern-dump fireside:snow` renders a hearth view to a file without
  opening a window; the other views can also be dumped.

### Desk

- Changed: the `-` beside the playlist tabs arms in red on the first press
  and removes the playlist on the second.
- Fixed an issue where playlist play mode and layer level became hard to
  read in narrow columns. Each now has its own footer space.

## 2026-09-23 - v0.99.374 (Super Deckboy properly: every playlist on its own output, a walk-up sting)

### Show files

- Changed: projection mapping is stored per output, and each layer stores
  its position and blend mode. Existing shows migrate on load and retain
  their alignment and layers.
- Older Deckboy versions cannot read either change from shows saved by
  this version.

### Outputs

- Each output has its own playlist layer stack. A camera and lower thirds
  can share one output while another shows the camera clean.
- The routing menu shows each output's full stack, with an insertion point
  between each pair of layers.
- Warp and edge blend apply per output, so a playlist can feed a keystoned
  projector and a clean stream simultaneously.
- New: multiview in the control window and as a separate output type for
  another screen. Each playlist tile shows its routing and layer fader.
- Fixed an issue where recordings did not capture the programme.

### Audio and MIDI

- A cue can send the same audio to several devices simultaneously, in
  addition to the crosspoint matrix's channel routing within one interface.
- New: MIDI file source. Drop a `.mid` file into a playlist and select an
  output port. Playback follows the cue transport, including scrubbing,
  pause and loop.
- New: generated walk-up sting with pitch, length, sweep and body sliders.
  It ends automatically and can run in a cue sequence like a clip.

### Sources

- New: Text source for title cards, scrolls, crawls and wobble, animated
  from the cue transport.
- Added thirteen engineering test patterns and a free downloadable kit.

### Desk

- Playlists can be added and removed from the playlist strip, each with
  independent audio.
- The dashboard has a bar button; tiles can be deleted, and a master cue
  can be assigned to a tile in one press.
- Inspector rows accept presses across their full width.
- `show in explorer` selects the media file.
- Fixed an issue where a cold file did not start cleanly on its first play.
- `Space` controls play/pause; GO advances through standby cues.
- Restored animals in the playlist.

## 2026-09-22 - v0.99.373 (EXPERIMENTAL - a cue list that is a program, and a deck that speaks)

Pre-release: these additions have not been tested in a real show; run alongside your trusted version.

### Cues
- New: per-cue pre-wait, post-wait and continue mode. A cue can wait before
  starting and start the next cue when it begins or ends.
- New: STANDBY pointer, independent of selection. `GO` fires the standby cue
  and advances the pointer; selection determines which cue you inspect.
- New: TARGET cues start, stop, pause, resume, load, arm or disarm another
  cue on any deck.
- New: FADE cues ramp a deck's picture level, deck volume or master dimmer
  to a value over a duration. Linear, eased and S curves; optional stop at
  the end of the fade.
- New: MASTER cues fire an assigned cue on several decks at once.
- New: disarm any cue without removing it or its settings. `GO` skips
  disarmed cues; their rows are dimmed.
- New: MIDI cues send notes, control changes, program changes, MIDI Show
  Control `GO`/`STOP`/`RESUME`, or custom bytes. These complement the
  existing MIDI, MSC, OSC, Art-Net and timecode inputs.
- New: Network cues send OSC, a UDP datagram or a TCP line.
- New: Timecode cues start, stop or jam the LTC generator.
- New: DMX cues send Art-Net channel levels with a fade time, e.g. house
  lights to 20% on cue 14.
- New: Script cues run Deckboy protocol lines. One cue can fire a master,
  send MIDI, jam timecode and send a message to a media server.
- New: AUDITION plays a cue in the control window without changing outputs.
- New: PRELOAD racks a cue paused with decode running and keeps it off air,
  ready to take without decoder startup.

### Desk
- New: CHECK, beside RELINK, counts show faults and steps through them:
  missing media, goto destinations pointing to deleted cues, missing master
  or target destinations, MIDI or DMX cues unable to build a message, and
  network cues without a destination.
- Added loading indicators for jobs that previously appeared frozen.
- The playlist scrollbar can be dragged; the mouse wheel scrolls 3 rows.
- The update check reports what is blocking it.

### Outputs
- Fixed an issue where every output showed deck 1 regardless of its
  assigned deck. Outputs now show their host deck.
- Add an output and choose its deck from the Video Outputs card.

### Audio
- New: per-cue crosspoint matrix in the inspector. Route left and right
  audio to up to 64 device channels, with gain at each crosspoint.
- Sources routed to the same channel sum, allowing mono fold-down.
  Supports routing to Dante Virtual Soundcard and large ASIO interfaces.
- Cues without a matrix retain their existing routing.

### Sources and patterns
- New: TEXT source for title cards, holding slides and crawls as the deck's
  picture, rather than an overlay. Fade-in, typewriter, upward credits,
  ticker crawl and breathing motion follow cue transport, including scrub
  and pause. Text size is a percentage of the raster, preserving relative
  size between 1080 and 2160 outputs.
- Changed: FIRESIDE moves from test cards to its own SOURCE entry.
  Controls set fire intensity from embers to roaring and how much it throws
  off; the fire continues to surge and settle at every setting.
- Added 13 test patterns, also available on the LED page and in the site's
  free kit: numbered panel map with tile pixel origins, moire 1:1, dark
  detail, uniformity, banding ramps, safe areas, boresight, PLUGE, greyscale
  steps, convergence, multiburst, and 10% and 50% windows.

### Companion
- Updated the module to base 2.x.

## 2026-09-20 - v0.99.372 (a second press before the show goes dark, and Companion 5)

### Desk
- Changed: NEW requires a second press when an output is armed or a deck
  is running. The first press displays a warning banner, matching the guard
  for deleting a live cue; with nothing on air, one press still opens an
  empty show.
- NEW remains beside OPEN on the toolbar and empties both decks and disarms
  every output. `Ctrl+Z` restores the show with outputs left disarmed.

### Companion
- Updated to `@companion-module/base` 2.x and Node 22 for Companion 5.
- Presets are grouped into sections in the preset browser. Actions,
  feedbacks, variables and transport retain their existing behaviour.
- Companion 4.2 and earlier cannot load this module; use the previous
  module zip with those versions.

### Remote protocol
- Changed: `PATTERN ADD` and `PATTERN SET` return an error naming an
  unrecognised pattern ID, rather than reporting success without making
  the cue.

## 2026-09-19 - v0.99.371 (plugins at the right level, and instruments you can play)

### Audio
- Fixed an issue where hosted effects distorted and hosted instruments were
  inaudible because plugin input was above full scale and output was too
  quiet. Plugins and the cue chain now use matching full-scale levels.
- New: play hosted instruments from the computer keyboard, a MIDI keyboard
  or a network controller, using the same inputs as the built-in chip
  synth. Add a Plugin effect to a tone cue and select an instrument.
- Fixed an issue where effects, bends and plugin slots appeared in a tone
  cue's inspector but did not process its generated audio.

### Remote protocol
- New: `TONECUE` creates a chip-synth cue from a controller.

## 2026-09-19 - v0.99.370 (your own plugins, in the cue's chain)

### Audio
- New: VST3 hosting in the per-cue audio chain. Add a Plugin effect, choose
  an installed plugin and arrange it alongside built-in effects.
- Plugin parameter rows use the plugin's own names. Other plugin settings
  are saved with the show.
- Plugins open when a cue is prepared, outside audio processing. A plugin
  that exceeds its audio-block time budget is removed from the chain and
  reported to the operator.
- When a plugin is missing, its slot, name and settings are retained and
  the missing plugin is reported.
- New: `--plugins` lists installed plugins and search locations.
  `--plugin-chain-check` runs a plugin in a cue chain and measures whether
  it and its controls change the sound.
- Fixed an issue where Deckboy could not start without an audio device.
  Cues and video outputs now run; the missing device is reported once.
- Tone cues carry an audio chain. Audioprint and the Audio LFO can read
  generated cues as well as files, allowing generators to drive picture
  effects and pass through the audio chain.

### Remote protocol
- New: `AUDIOFX <slot> PLUGIN <name>` for Companion control.

### Effects
- Optimised Kaleidoscope by reusing its fold map and Pixel Sort by computing
  each sorting key once per pixel. Picture unchanged; more headroom at 4K.

### Sources
- Fixed an issue where a window cue remained black when the source window
  stopped redrawing. Capture requests a redraw, then falls back and reports
  the fallback if no picture arrives.

## 2026-09-18 - v0.99.369 (a desk you can read at a glance)

### Interface
- Fixed an issue where inspector and settings toggles used the same colour
  for on and off in half the bundled themes. Loop, hold, fade, mute and
  other two-state controls now show distinct states in every theme.
- Enlarged the playlist scrollbar and its click target to follow interface
  scale. The loop icon stays inside its button; the transition style row
  aligns with adjacent rows.
- Changed: the initial window size follows display scaling, including 125%
  and 150%, keeping the bottom bar and MENU visible without resizing.
- The MENU button retains its gear icon. The wall clock is named Eddie's
  Clock.
- Pattern creation errors now name the requested pattern instead of only
  reporting `pattern: invalid`.

### Linux
- Fixed an issue where the AppImage could not run on Ubuntu 22.04.
  All bundled libraries now target that baseline, checked on every build.

## 2026-09-18 - v0.99.368 (window cues, tidied and quickened)

### Sources
- Fixed an issue where captured windows' rounded corners and unpainted web
  page edges left white or grey pixels around dark cues. Unpainted page
  areas are black.
- Changed: window capture runs at 60 fps instead of 30 fps when the window
  is small enough to sustain the output rate, avoiding repeated frames.
- Small windows are captured at their own size rather than enlarged before
  reaching the deck. For a 960x540 window on a 4K output, combined capture
  and app CPU use fell from about 225% of one core to 45%.

### Remote protocol
- Fixed an issue where requesting a camera or window without a name returned
  `OK` but added nothing while waiting for a picker. The command now returns
  the available sources for selection from a controller or script.

### Outputs
- Windows: Spout send and receive verified end to end, both sending the
  programme to another app and taking another app's Spout output as a cue.
- macOS: Syphon sends only. Receive is explicitly described as unsupported,
  rather than pending.

### Linux
- Removed host-provided libraries from the AppImage.
- Added an application description and screenshots for software catalogues.

### Interface
- Fixed an issue where MENU appeared as an ellipsis at interface scales
  above 1x, including 150%.

## 2026-09-17 - v0.99.367 (cues that stay where they belong, and sound that bends the picture)

Includes the changes since v0.99.362.

### Sources
- Windows: browser cues render directly into the programme output without
  opening a window over the desk. Page size is independent of display
  scaling. Use the inspector to scroll, click through a cookie banner or
  open the hands-on window for login.
- `SOURCE > Window` opens the window picker beside the button.
- Windows: window capture uses the window's full-resolution picture,
  independent of display scaling. Capture follows moves and resizes,
  excludes overlapping windows and captures static pages.
- Missing source windows are reported by name instead of producing a
  dark cue.

### Effects
- New: DATABEND processes picture data through an audio delay with feedback,
  filter and wavefolder, then draws the result. Live controls require no
  media preparation.
- New: AUDIOPRINT draws the deck's waveform down the picture and displaces
  each line horizontally with the same audio signal. Combine it with
  DATABEND and an audio-following parameter for interacting sound and
  picture effects.
- Added 8 audio bends, including one driven by the picture.
- Fixed an issue where SHATTER did not break the picture immediately after
  being added. Shard size now has an immediate visible effect.

### Outputs
- All downloads include the NDI sender. Install the free NDI runtime to
  send programme output over the network.

### Recording
- Fixed an issue where recorded audio did not stay aligned with its picture
  across recording duration and frame rates.
- Recordings retain every sample played by the decks, including audio
  between video frames and the final audio when a take is stopped.
- Pocket Test recordings include the sync beep with the flash, allowing
  end-to-end sync measurement from the recording as well as live output.

### Interface
- Reduced repeated interface drawing. Idle control-window CPU use is about
  one third of its previous level.
- Renamed the settings button to MENU.
- Empty playlists show shortcuts: `I` to import, `P` for a pattern, or
  drag media into the list.

### Companion and remote protocol
- Fixed an issue where Companion LFO buttons did not drive the selected
  parameter.
- Commands that cannot run, including requests for nonexistent decks or
  features absent from the build, return an error and reason instead of
  `OK`.
- macOS and Linux: connected controllers receive command replies immediately.

### Fonts
- `--font-check` accepts a language, e.g. `--font-check ja`, and reports
  whether this machine can draw it.

## 2026-09-14 - v0.99.366 (the font check can tell letters from boxes)

### Fonts
- Fixed an issue where `--font-check` passed fonts that drew only missing
  character boxes. It now compares two equal-length strings; identical
  boxed output reports `GLYPHS NONE` and fails the check.
- New: `--font-check <language>` selects the language and the font the
  interface would use before testing. Accepts any offered language code,
  including `ja`, `ar`, `hi`, `ko` and `zh-Hans`; previously only English
  was tested.
- The check uses each language's name in its own script as the sample.
  Languages absent from bundled fonts use the system font named by their
  catalogue; Latin coverage alone no longer passes a check for another script.
- Verification caught every tested bundled face lacking the requested
  script, despite all of them drawing boxes. All 6 languages requiring a
  system face passed on a machine with those fonts installed.
- The timecode face is exempt from language checks: it keeps its counter
  and readout font instead of switching to the language's prose font.

## 2026-09-14 - v0.99.365 (a recording hears what the room hears)

### Recording and streaming
- Fixed an issue where recorded audio led the picture by the sound device's
  queued-audio duration. The lead was constant, independent of source format
  and recording start time, at roughly 7 frames before this change.
- Audio supplied to recording, streams and NDI now follows samples played
  by the device, using its reported queue depth without a fixed or
  machine-specific delay.
- At 25 fps, a clip with simultaneous once-per-second white flashes and
  tone bursts measured audio offsets of -275, -307 and -275 ms before
  (mean -286 ms), and -189, -171 and -164 ms after (mean -175 ms).
  Negative values mean audio is early.
- A second machine, using a different clip and measurement method, recovered
  157 ms versus 111 ms on the first. The correction varies with the device
  and driver queue.
- Live output is unchanged: its picture already follows played audio.
  During verification it measured 20 ms closer to sync than another player
  through the same capture rig.
- Fixed an issue where a stopped take lost its final fraction of a second
  of audio. Pending audio is released when playback ends.

### Known issues
- About 4.5 frames of recorded A/V offset remain: 4.51 frames across 9 runs
  on one machine and 4.4 on another, steady from 25 to 60 fps. GPU frame
  readback arrives several frames after drawing; recorded audio is not yet
  delayed to match.

## 2026-09-14 - v0.99.364 (a recording keeps time with itself)

### Recording
- Fixed an issue where recorded audio ran about 3.3% slow when late video
  frames were repeated to maintain constant frame rate. Each repeated
  picture also repeated its audio, causing over 2 seconds of drift per
  minute at about 1 repeated frame in 30.
- Each written frame now receives fresh, consecutive audio. Sample counts,
  frame counts, durations and dropped-frame checks had appeared correct
  despite the duplicated audio content.
- Marker-clip timing ratios before and after, where 1.0000 is exact sync:
  25 fps at 640x360, 1.0332 to 1.0025; 50 fps at 640x360, 1.0340 to 0.9990;
  60 fps at 640x360, 1.0387 to 1.0015; 25 fps at 1080p with Grain Flow and
  Caustics, 1.0331 to 1.0025. Remaining variation reflects measurement
  quantisation to frame boundaries.
- The effect was worse at 60 fps than at 25 or 30 fps. The label cache in
  v0.99.363 had already reduced late frames and repeats by lowering
  main-thread work.

### Streaming
- Streams were unaffected because they do not repeat pictures. Streaming
  now uses the same audio calculation as recording.

## 2026-09-14 - v0.99.363 (the interface stops re-drawing what has not changed)

### Interface
- Cached rendered labels by window renderer, font, text and colour.
  Unchanged labels reuse their textures; unused labels and reloaded fonts
  release the cache.
- Previously, 312 text-drawing sites recreated labels each frame, often in
  cue and settings loops at 60 fps: roughly 20,000 allocations per second.
  Text measurement was already cached; rendered text now is too.
- `--ui-profile` reports label-cache entries, memory use, hit rate and
  rasterisation failures every 5 seconds.
- Fixed right-to-left splash font setup after interface-scale changes.
  The headline font now loads with the other fonts before direction is
  configured, avoiding use of the previous, freed font. Left-to-right
  interfaces were unaffected.

### Known issues
- Missing interface text on one Mac (issue #6) remains unverified on the
  affected machine. Panels, icons, images and colours drew correctly.
  Reduced text allocation addresses a suspected cause; it is not a confirmed
  fix. macOS already switched to the supported Metal backend in v0.99.361.

## 2026-09-14 - v0.99.362 (an audio cue makes a sound)

### Audio
- Fixed an issue where AUDIO cues played silently through speakers and
  recordings while showing Playing, advancing position and valid duration,
  with a dead VU meter and no error. Audio-only cues with 1x1 probe
  dimensions are no longer rejected by video dimension rounding.
  Audio cues of every format now play and record.
- Added a 35 Hz high-pass filter to chip-synth pulse voices for DC blocking,
  matching the original chip's output stage. The offset previously consumed
  about 8% of headroom and caused thumps when notes started or stopped.
- A 12.5% duty pulse's measured mean changed from -2630 to 1.9; triangle
  bass is unchanged.

### Recording
- Fixed an issue where recording a slide, graphic, audio cue or paused clip
  repeatedly reported `RECORDING PICTURE STALLED` and put the output into
  an error state.
- The stall check now requires a picture expected to move, waits 3 seconds
  instead of 0.5 seconds, and reports once instead of every second.

## 2026-09-14 - v0.99.361 (the cue you took is the cue you see)

### macOS
- Fixed an issue where interface text was missing on macOS 26 while panels,
  icons, theme colours and the splash drew correctly. macOS now uses Metal
  instead of deprecated OpenGL; the previous backend worked on macOS 14
  and 15.

### Fonts
- New: `--font-check` opens every font at interface sizes, checks rendered
  ink and agreement between measured and drawn text, and names failed steps.
- Added per-window renderer reporting. Failed text textures are retried
  and logged.

### Playback
- Fixed an issue where taking a still cue after another live cue left the
  previous picture on the output and presenter view, while the cue name,
  notes, slide dots and both timers updated. First takes, repeated takes
  of the same cue and video playback were unaffected.
- Fixed an issue where older AVI files with packed B-frames displayed video
  about 170 ms behind audio because the first frame timestamp was 4 frames
  late.
- Fixed an issue where a bad timestamp intermittently froze the picture
  for 1–2 seconds.

### Cues
- PowerPoint `.pptx` imports retain per-slide fades, pushes, wipes, dips
  and irises with their durations, plus timed slide advances. These were
  previously lost during PDF conversion.
- Keynote decks exported as `.pptx` use the same import.

### Interface
- New: playlist scrollbar with draggable rail and thumb. Thumb size shows
  the visible proportion of the list; previously navigation was by mouse
  wheel in 36-pixel steps.
- Changed: the output's Shows control is a dropdown, exposing presenter
  view and prompter choices.
- Removed the minus sign from the timeline's REMAINING readout; playlist
  countdowns retain it to indicate counting direction.
- Fixed an issue where the first timeline ruler timestamp overlapped VIDEO.

### MIDI
- Fixed an issue where MIDI timecode quarter-frame messages were discarded
  on Windows and macOS and MTC was described as Linux-only. Any MIDI input
  can now chase timecode.

### Companion and Stream Deck
- The Companion module is included in release downloads.
- Changed: Stream Deck plugin ID from `com.deckboy.streamdeck` to
  `com.utopian-academy.deckboy` for Elgato Marketplace requirements.
  Published Marketplace IDs cannot change.
- Reassign Stream Deck keys once: install the new plugin, drag its actions
  onto the keys and remove the old plugin. Other behaviour is unchanged.

### Documentation and checks
- Added a screen check usable from a phone at a venue.
- The manual now lists all 13 existing transitions: cut, crossfade, dip to
  black, dip to white, push left/right/up/down, wipe left/right/up/down,
  and iris. No transition behaviour changed.
- Added 7 existing manual audits to CI, including 2 checks whose accepted
  issue counts can only decrease, plus a check for saved values changing
  after reload.

## 2026-09-13 - v0.99.359 (the rings come back)

### Video Synth
- Fixed an issue where Rings became a flat colour a few seconds after take:
  white on Vapor, pastel on NES and orange on Fire. Bands now persist at
  every palette, mirror and speed setting.
- Fixed an issue where the brightest posterised band clipped towards white
  instead of using the palette's top colour. Most visible on Rings,
  Diamond and Grid.

## 2026-09-13 - v0.99.358 (a synth stays a synth)

### Cues
- Fixed an issue where reopening a show changed a chip-synth cue into the
  channel-walking line-up tone, retaining its name and settings but leaving
  it unable to play notes.
- Fixed an issue where reopening a show changed Video Synth palettes EGA,
  C64, Game Boy, CGA, NES and Vapor to Mono.
- Added a source check to verify saved choices remain supported as option
  lists grow; the palette list currently has 11 entries.

### Audio
- Fixed an issue where notes always reached the first live deck when two
  decks were running. Notes now go to the focused deck.

### Remote protocol
- New: `CHIP` controls 2A03 timbre, complementing `SYNTH` for Video Synth.
  `CHIP VOICE pulse|triangle|noise`;
  `CHIP DUTY eighth|quarter|half|threequarter`;
  `CHIP NOISE short|long`; `CHIP ENV <attack> <release>`;
  `CHIP QUANTISE on|off`.
- Bare `CHIP` returns the current timbre. Changes apply during held notes.

### Interface
- Added 4 splash images, bringing the colour rotation to 27.

## 2026-09-12 - v0.99.357 (the audio chain reaches Companion)

### Companion
- New: 5 audio-chain actions: add, amount, bypass, remove and clear.
  These expose the existing `AUDIOFX` remote control alongside picture
  actions `FX ADD`, `FX AMOUNT` and `FX CLEAR`.
- Audio amounts and parameters use percentages, matching `AUDIOFX` and
  the inspector. Picture actions retain the 0–1 units used by `FX`.

## 2026-09-12 - v0.99.356 (a cue has a sound as well as a look)

### Audio
- New: per-cue audio chain directly below EFFECTS in the inspector.
  Click an effect name to choose its kind, set its amount, use `B` to
  bypass while retaining settings, and use arrows to reorder. Processing
  order changes the sound.
- Added 14 effects: High Pass, Low Pass, Tilt EQ, Compressor, Gate, Delay,
  Reverb, Width, Binaural, Picture, Placement, Seam, Frame Lock and Suspend.
- Picture: brightness opens the filter and darkness closes it; movement
  opens it further, linking the sound to the cue's picture.
- Placement: picture position sets sound position. Shrinking the picture
  reduces treble, width and level; the far ear receives delay, head shadow
  and reduced level.
- Seam: reduces treble and adds a short room over the final seconds,
  leaving level unchanged so cue fades remain independent.
- Frame Lock: stutters in grains of 1, 2 or 4 video frames, aligned to
  frame boundaries rather than musical tempo. One frame at 23.976 fps is
  41.708 ms.
- Suspend: crossfades and repeats the last moment of audio for the duration
  of a hold, retaining room tone, audience sound, rain or hum.
- The 5 cue-aware effects pass audio through unchanged and explain when
  their input is unavailable: e.g. Frame Lock on an audio cue, Seam on an
  open-ended cue, or Picture on a cue without a picture.
- Audio-chain edits apply to every selected cue, as gain, pan and mono
  already do. New effects start with audible settings; Delay and Reverb
  use sends, adding to the unchanged dry source.
- Chains save with the cue. Older shows load unchanged with no audio chain.
- New: `--audio-fx-check` tests every effect with a speech-shaped signal
  and reports level, peak, low/high spectrum changes, inter-word noise
  floor and any sound remaining after the input stops.

### Remote protocol
- New: `AUDIOFX` reads the chain; `AUDIOFX ADD <effect> [amount%]`
  appends; `AUDIOFX <slot> <amount%> [a% b% c% d%]` sets values.
  Also supports `BYPASS`, `OFF` and `CLEAR`. All numbers are percentages,
  matching the inspector.
- `HELP ALL` lists `RENAME` and includes itself in the command count.

### macOS
- Measured on an M4: 10 seconds of 4K30 video used 0.46 seconds of CPU with
  hardware decode, versus 5.48 seconds in software, about 12 times as much.
  The previous ffmpeg subprocess path achieved 20 fps at that raster.
- VideoToolbox frames pass directly into Metal textures without CPU
  download.

### Diagnostics
- New: `--decode-bench <file> [seconds] download` retains hardware decode
  but requests packed CPU frames, separating decode cost from frame
  download cost.

### Interface
- Fixed an issue where the cue inspector waveform overlapped the preview.
  The preview and waveform now occupy separate areas; the thumbnail box
  scales with the interface. At 1.5x the strip previously occupied over
  half the box.
- Waveforms now show peak as an outline and RMS as a bright body in each of
  512 buckets. Peak-only waveforms appeared nearly solid on long cues:
  an 11-minute cue placed 1.3 seconds in each bucket, whose peaks were
  within a few dB of the whole file's maximum.
- Fixed an issue where effect amount and parameter `-` and `+` buttons
  did nothing while value scrubbing worked.
- Added 13 splash scenes and JPEG support alongside PNG. The new art uses
  15 MB instead of 44 MB, with no visible difference at 1:1. At the splash
  size, PNG was larger than the source and about 3 times a high-quality
  JPEG; the art fills the background behind a translucent, tinted card.

## 2026-09-12 - v0.99.353 (zero-copy decode on macOS -- written, not yet measured)

### macOS
- New: VideoToolbox frames backed by IOSurface pass directly to Metal
  textures without CPU download, pixel conversion or re-upload. This
  removes the system-memory round trip that accounts for most of the
  cost at 4K.
- Metal devices can wrap the same surface directly. Unlike Windows, the
  decoder need not share the renderer's device or copy each frame into
  another GPU texture.
- Unsupported surface formats, including 10-bit, retain the CPU download
  path. This avoids assuming every hardware format can be wrapped, an
  assumption that previously rendered 10-bit HEVC green on Windows.
- `--decode-bench` now reports whether a frame actually avoided copying
  on macOS as well as Windows.
- Fixed a macOS link failure by linking CoreVideo.
- Subsequent M4 measurements are recorded in v0.99.356.

### Stage timer
- New: typeface mode alongside the existing seven-segment and blocky
  faces. Select a `.ttf` or `.otf` in the cue inspector, or use
  `TIMER FACE typeface` and `TIMER FONT <path>`.
- Clearing the font uses the bundled app face. Existing defaults remain
  unchanged; seven-segment and blocky render identically across machines.
- The message line uses the selected face. Typeface mode preserves mixed
  case and accents without the built-in 5x7 face's uppercase conversion
  and 40-character limit.

### Interface
- Fixed an issue where the program-monitor picture overlapped the LIVE
  badge and output caption above 100% interface scale. Picture insets now
  follow the scaled badge and caption.
- Added a recessed bezel, corner ticks and a fine dot field around the
  monitor, outside the programme picture.

## 2026-09-12 - v0.99.352 (IPTV channel lists)

### Cues
- New: import IPTV `.m3u` channel lists as one stream cue per channel.
  Cue names come from entries; group titles go into searchable cue notes.
  HLS URL playback was already supported.
- `.m3u8` files containing any `#EXT-X-` tag are treated as one HLS
  stream, rather than importing each segment as a cue. Other lists are
  read as channel lists.

## 2026-09-12 - v0.99.351 (a teleprompter)

### Outputs
- New: prompter output with large scrolling text, a fixed reading line,
  pace in lines per minute and mirroring for teleprompter glass.
  Assign its display and arm or disarm it with the show.
- With no separate script, the prompter reads live-cue notes, matching the
  presenter view. A supplied script supports talks without slides.
- The active line uses full brightness; other lines are dimmed.
  Reading markers are wedges in the margins, outside the words.
  The final line stops on the marker instead of scrolling off screen.
- Controls are available in Settings for the prompter output.

### Remote protocol
- New: `PROMPTER RUN`, `STOP`, `TOP`, `SPEED`, `SCROLL`, `SIZE`,
  `LINE`, `MIRROR` and `SCRIPT`. `SCROLL` jogs by whole lines.

### Playback
- New: in-process hardware decode on Linux through VAAPI and on macOS
  through VideoToolbox, alongside Windows D3D11VA. Machines without a
  supported hardware device fall back to software.
- Frame copying is still avoided only on Windows in this version, where
  the decoder shares the output renderer's device.
- `--decode-bench` reports the decoder actually used. `--no-hw-decode`
  or `DECKBOY_NO_HW_DECODE=1` forces software for comparison using the
  same binary and footage.
- On a 2015 Intel iGPU, VAAPI used about 10% less CPU for 1080p H.264;
  hardware decode was slower than software at 4K on the same chip.
  Results depend on the machine.

### Interface
- Fixed an issue where the cue inspector displayed `loading preview...`
  indefinitely for patterns, timers and tones with no still to load.
  The message now appears only during an active load.

### Documentation
- Added screenshots of the desk using built-in patterns and of presenter
  view. Presenter view is listed as an available feature instead of a
  roadmap item.
- Reconciled contradictory statements about signing in the README and
  roadmap.

## 2026-09-12 - v0.99.349 (arrange the presenter screen yourself)

### Presenter view
- New: ARRANGE in the program-monitor header for a focused presenter output,
  in Settings, or through `PRESENTER ARRANGE`. Move any of the 4 panels
  by dragging; resize from a corner.
- The arrangement preview preserves the output aspect ratio and snaps
  panel edges to its visible grid. Editing starts from the current named
  layout; PRESETS restores a named layout.
- Named layouts reflow when panels are disabled. Custom layouts retain
  the positions you set.
- New: `PRESENTER PANEL <live|prev|next|notes> <x> <y> <w> <h>` positions
  a panel in percentages.
- Notes now scroll beyond the former `... more below` marker. Clicker
  forward scrolls through the current build before advancing to the next
  one, with eased motion and fades at edges hiding more text.
  `NOTESTEP SCROLL <rows>` scrolls manually.
- New: Notes take adjusts the share of the screen given to notes in every
  layout. Live, previous and next pictures can each be disabled; disabling
  all 3 gives notes the full screen at the chosen text size.
- WIDE shows a large live picture, previous and next beside it, and notes
  below. FILMSTRIP puts the 3 pictures above large notes. NOTES prioritises
  text with pictures in a narrow strip.
- Every panel can be disabled independently. Notes have their own size;
  background, ink and accent are customisable. Intermediate shades follow
  these colours, retaining readable secondary ink on white backgrounds.
- Controls are in `Settings > Video Outputs > Screen`, shown only for
  presenter outputs, and available through `PRESENTER` remote commands.
- Notes split on a line of `---`. All parts through the current build
  remain visible: current text at full brightness, earlier parts dimmed,
  with scrolling to keep the current position in view.
- When enabled, `Page Down` advances through remaining builds before the
  slide; `Page Up` steps backwards. `NOTESTEP` provides remote control.
- Changed: PREVIOUS shows the slide last displayed, including after jumps,
  rather than the cue immediately above the live cue in the list.
- Added rounded cards, pill captions, build dots and a bottom slide strip
  highlighting the live slide.
- Fixed an issue where long note lines lost their final word to an ellipsis.
  Notes now wrap at their actual drawing width.

### Cues
- New: split notes into one cue per part from a cue's right-click menu or
  `SPLITNOTES`. Each cue retains the same slide and its own note chunk;
  only the first fades in, so NEXT advances without a picture blink.
  Parts become individually addressable by cue number, timecode and Companion.
- PowerPoint `.pptx` imports now read embedded speaker notes. PDFs also
  pick up notes from an adjacent PowerPoint file, including paired PDF and
  `.pptx` exports from Google Slides; no hand-written sidecar is required.
- Notes were read from 80 slides in a 109-slide deck in a fraction of a
  second, without adding a build dependency.
- Slide-import progress now shows miniature slides filling in as pages
  render, with the newest slide animated as it arrives.
- Fixed an issue where multiline notes did not survive save and reload.

### Interface
- Fixed an issue where Orientation displayed a corrupted degree sign.
  It now reads `0 deg (Normal)`.
- Encoding checks now fail the build on this class of text corruption.

## 2026-09-12 - v0.99.346 (a presenter view, and notes that come with the deck)

### Outputs
- New: presenter view output showing the live slide, next slide, live-cue
  notes and time on a separate display. Uses the existing output display
  picker, fullscreen, arming and health reporting.
- Independently show or hide the previous slide, next slide, notes, clock
  and timers. Includes 3 layouts and custom background, ink and accent
  colours.

### Cues
- New: note builds split on a line of `---`, advancing notes without
  changing the slide.
- Optionally, NEXT advances through remaining note builds before taking
  the next cue, supporting slide clickers.
- PDF imports read per-slide notes from adjacent `.pdfpc` files, as used
  by LaTeX and Beamer, or `.notes.txt` and `.notes.md` sidecars.
  `### 1` begins slide 1's notes; `---` splits builds within them.

### Documentation
- Corrected platform support: Spout output works on Windows; Spout input
  and Syphon are not built in this version.
- Clarified that macOS and Linux use CPU decode because the in-process
  hardware path is D3D11VA. VideoToolbox and VAAPI support are on the
  roadmap, rather than described as platform limitations.

## 2026-09-12 - v0.99.345 (the monitor and the preview follow the scale)

### Interface
- Fixed an issue where program-monitor and preview captions used fixed
  boxes under scaled text. PROGRAM, LIVE, output resolution, both VJ deck
  captions and preview-thumbnail captions now follow interface scale.
- Fixed an issue where the empty inspector's `No cue selected`,
  `Drop media here` and `Press A to take cue` lines overlapped at 150%.
  Their spacing no longer stays at 20 pixels as text grows.
- Restored missing labels on the colour-tag and cue ID rows.

## 2026-09-12 - v0.99.344 (rows that know what they are)

### Interface
- Added labels for Notes, cue ID and goto target.
- Changed: Loop, Hold and End action use label-left, control-right rows.
  The end-action value is separated from the `X` cycle hint.
- Unified the layout of 5 editable-row variants with the other inspector
  rows.
- Fixed an issue where the timeline header's cue name and clock overlapped
  at 150% scale. Both rows now follow their font heights.

## 2026-09-12 - v0.99.343 (a clock, a readable meter, and a licence GitHub can read)

### Interface
- New: optional wall clock at the right end of the toolbar, off by default.
  Cycle off, 24-hour, 12-hour or analogue with a second hand in
  `Settings > System > Appearance`. Its position stays fixed as other
  toolbar items appear or disappear.
- Fixed an issue where VU readings such as `-12` and `-48` were clipped
  at 150% scale. The reading column now fits its widest value; L and R
  are centred above their bars.

### Documentation and packaging
- Replaced the 19-line source-file licence notice with the full GPL-3.0
  licence text, allowing GitHub to detect the licence. Added copyright
  information to the README licence section.
- Removed 6 unused splash images, about 15 MB, including one still using
  its generator-assigned filename.
- Removed a Reddit keyword-scanning script unrelated to the product from
  the public repository.
- Corrected the landing page's NVENC-only conversion claim: Deckboy tries
  3 GPU encoders, and NVENC is unavailable on AMD, Intel and Apple machines.
- Corrected the landing page's no-installer claim. Windows has an installer
  with a Start Menu entry and uninstaller.

## 2026-09-12 - v0.99.342 (pages that finish their own sentences)

### Interface
- Fixed an issue where keyboard-shortcut descriptions were cut off and rows
  nearly touched at 150% scale. The modal and rows now follow font size;
  the key column fits the widest key label in the active font.
- Fixed an issue where the cue name overlapped SELECTED CUE and the final
  2 summary lines extended outside the panel at 150% scale.
  Panel size and layout now use the same 3 measured row heights.

### Remote protocol
- New: `SHORTCUTS SHOW|HIDE|TOGGLE` opens or closes the shortcuts page
  from Stream Deck or another control surface, alongside dashboard
  control. Previously the page was reachable only by keyboard.

## 2026-09-12 - v0.99.341 (a menu you cannot see through)

### Interface
- Fixed an issue where SOURCE ignored the theme and showed underlying text
  through its menu. Replaced the fixed dark-green, 96%-opaque background
  with fully opaque theme panel colours.
- SOURCE rows and padding now follow interface scale.

## 2026-09-12 - v0.99.340 (the cue list scales with the desktop)

### Interface
- Fixed an issue where cue names, metadata and action icons overlapped at
  150% scale because rows remained 80 pixels high. Row dimensions and
  offsets now follow interface scale.
- Cue-kind labels use their full column width. Pattern no longer truncates
  to `PA...`; Window Source and Lower Third have room for their labels.
- Fixed an issue where timeline hover tips such as `Click to seek`
  appeared over the dashboard and shortcuts modals.
- Fixed an issue where the dashboard's `Ctrl+D or Esc to close` hint
  was truncated.

## 2026-09-12 - v0.99.339 (one question per row, everywhere)

### Interface
- Changed: every settings card and inspector row uses a label on the left
  and one control on the right. Replaces full-width labelled pills, bare
  buttons, reversed label/control rows and wrapping chip grids.
- Fixed an issue where enabled controls were harder to read than disabled
  controls. Fill brightness now indicates state while ink remains dark
  on light in both states.
- Applied the same contrast rule to settings toggles, inspector rows,
  dropdowns and cue colour tags. Tag text contrast follows the swatch
  colour rather than the theme.

### Stage timer
- Added a large clock readout that turns amber and red at the cue's
  configured thresholds.
- START and RESET are direct buttons. Replaced duplicated RESET buttons
  and generic numeric rows with 4 nudges: `-1 min`, `-10 s`, `+10 s`
  and `+1 min`.

## 2026-09-12 - v0.99.338 (held cues come up, and the transitions are the ones you asked for)

### Playback
- Fixed an issue where held cues, including patterns held by default,
  remained black because fade-in used a timeline position fixed at zero.
  Visual and audio fades now run from take time, complete over the chosen
  duration and remain at full level.

### Outputs
- Fixed an issue where transitions reached only the first armed output.
  A recording made alongside a live window output could remain black
  throughout the transition. Each output now receives its own outgoing
  picture.

### Remote protocol
- Fixed an issue where transition names with separators, including
  `push-left`, silently selected a dissolve. `push-left`, `push_left`,
  `pushleft` and `Push Left` now select the same style.
- Added `dissolve` as an alias for `crossfade`.
- Invalid transition names return an error instead of selecting a dissolve.

## 2026-09-12 - v0.99.337 (a long playlist runs at full rate again)

### Interface
- Fixed an issue where frame rate fell as cues were added. A 16-cue show
  had fallen to 23 fps; longer slide decks, including a 62-slide deck,
  increased the label-rendering work.
- Labels that need smaller text now use a cached smaller font face without
  invalidating other labels. Text measurements are cached; labels that
  fit skip truncation.
- Measured: 64 cues at 1.5x desktop scale hold 60 fps.
- Fixed an issue where truncation could split a multibyte character.
  Labels now truncate at character boundaries.
- Long-label truncation now takes about 6 measurements instead of 30,
  using a halving search.

## 2026-09-11 - v0.99.336 (the cyphers say what they mean)

### Interface and localisation
- Fixed an issue where cypher labels were transformed twice. ROT13 and
  Atbash displayed plain English despite the selected setting; Morse
  displayed rows of slashes.
- All 4 cyphers now transform English once. The 14 translated languages
  were unaffected.

## 2026-09-11 - v0.99.335 (slide decks in their own process, tally from the switcher, nineteen languages)

### Cues
- Windows: PDF pages now render in a separate process with its own graphics
  device, using the same engine as Edge. Imports no longer share the
  show's graphics resources; renderer failures affect the import.
- Linux retains its separate rendering process; macOS continues to use
  CoreGraphics.
- PDF import still produces one still cue per page at full 4K width.
  The page counter updates as each page renders, including during long
  imports such as a 60-page deck.

### Triggers
- New: direct, read-only ATEM connection to follow its programme bus.
  Set the switcher address and select Deckboy's input from the ATEM's own
  source names. Going on air starts playback; Deckboy sends no switcher
  commands.
- New: NDI receiver programme tally can start playback when a receiver
  puts a Deckboy output on air, without a switcher.
- Both triggers are off by default. On leaving air, choose load next cue,
  pause, stop, clear or no action.

### Remote protocol
- HyperDeck emulation now listens on the protocol's standard port for ATEM
  Software Control, Companion's HyperDeck module and other deck clients.
- Settings displays the address to use, selected for the route towards
  the switcher. HyperDeck emulation can be disabled.
- NMC transport send/receive settings are now in a Network card and saved
  with the project: direction, port, target and source filter.
  Only fields used by the selected direction are shown.
- NMC continues to synchronise play, pause, stop and locate in either
  direction. Existing launch environment variables remain supported.

### Interface and localisation
- Added language support: English, 14 translations and 4 cyphers, for
  19 choices. Translation files map English to translated strings;
  missing entries fall back to English. Established trade terms remain
  in English where appropriate.
- Translations include Cuban Spanish, Klingon in its written form and
  Lumeni, a language created for Deckboy with its own grammar.
- Cyphers transform English without word lists and cover every label:
  ROT13, Atbash, 1337 and Morse.
- Changed: interface scale follows desktop DPI unless overridden,
  including Windows displays at 125% and 150%. The scale setting shows
  the applied value. Older shows retain their saved scale.

### Outputs
- New: per-output matte from 16:9 through 2.39:1, with bars that can dim
  instead of fully masking, plus a still-image overlay for house frames
  or station logos.
- Mattes and overlays persist through cuts, clears and panic.
  Recording, streams, NDI and the program monitor receive the same
  composited picture.

### Diagnostics
- Crash reports now include module names and offsets for every stack
  frame and write essential information first, making the report readable
  without access to the originating machine.

## 2026-09-10 - v0.99.334 (10-bit playback, a live coder that keeps up, motion mosh)

### Playback
- New: direct GPU compositing of P010 surfaces for HEVC Main 10, VP9
  Profile 2 and 10-bit AV1, matching the existing 8-bit path.
  Removes full-resolution CPU download, conversion and re-upload while
  retaining 10-bit depth.
- A 4K60 10-bit clip improved from 39.6 fps to 60 fps. A 720p 10-bit
  x265 clip also avoids the per-frame download.

### Sources
- Code sources now evaluate frames across CPU cores. At 4K, 8.3 million
  pixels previously ran the prelude and 3 channel programs on one core.
- `r` and `a` are calculated only when used. Expressions such as
  `sin(x*12+t)` avoid unused square-root and arctangent calculations.
- Measured improvements: 8.8x at 4K, from 1.2 to 10.4 fps; 8.3x at 1080p,
  from 4.8 to 39.5 fps. Output is byte-identical.
- Code cues are labelled as sources in the playlist and inspector,
  matching the SOURCE menu.

### Effects
- New: Motion Mosh, a live datamosh effect for any cue, including cameras
  and NDI feeds. Estimates motion by matching blocks between frames and
  retains the previous picture inside the effect.
- Refresh controls picture replacement; Hold sets how long the old picture
  persists; Block size ranges from visible square edges to smoother flow.
  Every parameter supports an LFO.
- Motion Mosh writes no file and needs no transcode. The existing codec
  datamosh remains available, withholding keyframes at cuts through a
  transcode. Motion Mosh measured 4.6 ms at 1080p.

### Interface
- macOS: Command now works for shortcuts and selection, including
  `Cmd+S`, `Cmd+O`, `Cmd+Z` and `Cmd-click`. Control remains supported.
- New: `Ctrl+,` toggles preferences; `Cmd+,` on macOS. Bare comma remains
  the previous-cue key.
- Fixed an issue where menu and settings toasts appeared beneath the
  panel, hiding messages such as update-download refusal reasons.
- The recording folder setting is now available in System as well as
  Video Outputs.

### Updates and packaging
- Update refusal messages now identify the blocking condition. A paused
  deck still counts as playing; any armed output blocks an update, while
  disarm acts on the focused output.
- Fixed an issue where the updater selected the Apple Silicon disk image
  on Intel Macs. Releases have included both architectures since v0.99.307.
- Excluded the recent-shows list and its absolute local paths from release
  packages.

### Remote protocol
- Added `SCALEMODE` to `HELP ALL`.

## 2026-09-09 - v0.99.333 (bug fixes: display locking, capture cards, timer, hover tips, pocket mode)

### macOS
- Fixed an issue where arming a fullscreen output put it on the control
  display and trapped keyboard focus there. Fullscreen now waits one
  event-loop tick after showing the window so macOS can assign its display.
- Fixed an issue where camera and capture cues opened blank on cards such
  as Blackmagic and ATEM Mini. Capture no longer forces 1280x720 NV12,
  which built-in cameras offered but capture cards could reject.
- Resolution and pixel format follow the device, often 1920x1080 for
  capture cards; scaling fits the cue. Frame-rate requests use the
  hardware's 15 and 30 fps modes, avoiding rejection of 29.97 fps.

### Stage timer
- Changed: duration arrows step by 5 minutes instead of 30 seconds,
  avoiding 60 clicks to set a 30-minute timer. Live nudge buttons retain
  their 1-minute steps.

### Interface
- Fixed an issue where hover tips from controls behind Settings or the
  startup dialog appeared over those panels.
- Fixed an issue where opening a show in Pocket 3 mode reset interface
  scale and touch mode to defaults instead of restoring the saved layout.

## 2026-09-07 - v0.99.332 (each show named once on the startup screen)

### Desk
- Fixed an issue where the most recent show appeared both in OPEN PREVIOUS
  and in the startup screen's 5-entry recent list.
- OPEN PREVIOUS retains the latest show; the list now shows up to 5 other
  shows without duplicating it.

## 2026-09-07 - v0.99.331 (recent shows on the splash, settings pages that line up)

### Desk
- New: startup screen list of up to 5 recent shows, newest first, with
  their folders. Click a row or press its number to open it.
- Deckboy remembers the last 8 shows. Shows on disconnected drives retain
  their place until the drive returns.

### Interface
- Settings cards and sections now size themselves to their rows and follow
  UI Scale consistently on Windows, macOS and Linux, with each platform
  showing its available hardware.
- Audio uses one column. Video Outputs scrolls when content extends below
  the view. Encoder format choices are readable in every theme with the
  selected format clearly marked.

## 2026-09-07 - v0.99.330 (packaging release)

### Packaging
- Redistributed the v0.99.329 build on all platforms so the in-app updater
  finds a newer version on each. No functional changes.

## 2026-09-07 - v0.99.329 (a clean exit on Linux)

### Linux
- Fixed an issue where quitting did not complete cleanly. Deckboy now
  returns to the desktop and reports a successful exit to waiting
  processes, including the updater.

## 2026-09-07 - v0.99.328 (packaging release)

### Packaging
- Redistributed the v0.99.327 build on all platforms so the installer
  visibility fix has a newer version to install. No functional changes.

## 2026-09-07 - v0.99.327 (the Windows installer arrives on screen)

### Windows
- Changed: the update installer opens with its window visible and ready
  for interaction.
- The updater waits for Deckboy to close before replacing the installed
  build.

## 2026-09-07 - v0.99.326 (packaging release)

### Packaging
- Redistributed the v0.99.325 build on all platforms for end-to-end
  verification of automatic installation. No functional changes.

## 2026-09-07 - v0.99.325 (one-button updates on Windows, macOS and Linux)

### Updates
- New: automatic update installation on macOS and Linux. The update button
  downloads, unpacks over the app and reopens it without manual disk-image
  copying or AppImage placement.
- Windows, macOS and Linux now download, install, relaunch and clean up
  through the updater.
- Windows: successful updates delete their downloaded setup file, about
  94 MB. Installers downloaded manually are retained.

## 2026-09-07 - v0.99.324 (INSTALL & RESTART, start to finish)

### Updates
- Changed: INSTALL & RESTART returns directly to the newly installed build
  when installation finishes, without another prompt or checkbox.

## 2026-09-07 - v0.99.323 (packaging release)

### Packaging
- Redistributed the v0.99.322 build on all platforms for end-to-end
  verification of update restart. No functional changes.

## 2026-09-07 - v0.99.322 (INSTALL & RESTART brings Deckboy back)

### Updates
- INSTALL & RESTART runs the updater separately, waits for Deckboy to
  close, installs the update and launches the newly installed build.
- Added `relaunch.log` beside the download, recording update actions
  and times for later inspection on unattended machines.

## 2026-09-07 - v0.99.321 (VJ mode fits a laptop, and geometry answers for itself)

### Interface
- VJ mode now fits a 1470-pixel-wide desktop. Its bar spans the programme
  and inspector columns, showing VJ MODE, both deck labels, crossfader,
  blend mode, TAP and tempo in full.
- Playlists receive 20% more space; the inspector uses the smaller end of
  its width range while mixing. The monitor row retains room for A and B
  previews.
- Restored all 3 heckler faces on laptop layouts, above deck A, programme
  and deck B, each with its own commentary.
- Mascot tips use readable ink against the darkened programme monitor.

### Remote protocol
- New: `SCALEMODE fit|fill|stretch|unscaled` sets cue-to-output scaling
  from Companion, a control surface or a script.
- Bare `SCALEMODE` returns the current mode.

## 2026-09-07 - v0.99.320 (the mascot speaks up, and the crossfader keeps its width)

### Interface
- Mascot tips now use ink selected for the darkened programme-monitor
  background.
- VJ mode hides tempo, TAP and blend controls as the window narrows to
  preserve usable crossfader width. Control labels remain complete.

## 2026-09-07 - v0.99.319 (INSTALL & RESTART restarts)

### Updates
- Changed: INSTALL & RESTART waits for the Windows installer to finish,
  then launches the new build from the same settings-page action.
- macOS and Linux label the button INSTALL in this version; disk images
  and AppImages still require manual placement.

## 2026-09-06 - v0.99.318 (browser cues on every platform, and a pointer of our own)

### Sources
- Browser cues render, accept clicks, typing and keys, and play on output
  on Windows, macOS and Linux. Includes `BROWSER KEY Enter` on Linux
  and the browser entry in the macOS SOURCE menu.
- Unreachable pages show a warning card naming the address. Cues without
  an address open the Video Jockey page.
- Window-source identifiers now behave consistently across platforms:
  `desktop`, `screen:N`, `window:<id>`, `title:<text>` and
  `region:X,Y,W,H`. Every platform has a window picker; unsupported
  capture sources report the limitation.
- Fixed an issue where taking a browser cue on an unfocused deck did not
  put it live on that deck.

### Interface
- New: custom pointer with a hot-pink tip, cyan tail, dark outline and
  light inner edge for visibility on dark and light backgrounds.
  Follows UI scale without per-frame cost; disable in
  `Settings > Appearance`.
- Changed: button text shrinks to fit the whole label instead of
  truncating it throughout the app.

### Patterns
- New: Frame Count test pattern, including an emoji variant.
  The whole field changes colour each tick to expose dropped or repeated
  frames and measure end-to-end frame delay, complementing Test Clock
  for checking feed synchronisation.

### Effects
- Brightness, contrast, saturation, hue and chroma key changes reach the
  output immediately on stills, patterns, live sources and playing video.

### Recording
- Recording retains a live picture throughout the take.

### Remote protocol
- The control port responds only to control clients.

## 2026-09-06 - v0.99.317 (browser cues on macOS, and one window source everywhere)

### Sources
- New: browser cues on macOS, with rendering, clicks, typing and programme
  output. Supports the same `BROWSER CLICK`, `TYPE`, `KEY`, `SCROLL`
  and `URL` commands as other platforms.
- Browser cues without an address open the Video Jockey page. Unreachable
  pages display a warning card naming the address.
- Window-source identifiers are consistent on Windows, macOS and Linux:
  `desktop`, `screen:N`, `window:<id>`, `title:<text>` and
  `region:X,Y,W,H`. Adding a window source opens the window picker on
  every platform; unsupported capture sources report the limitation.

### Patterns
- New: Frame Count, with an emoji variant. The field changes colour each
  tick to expose dropped and repeated frames.
- Photographing source and output together gives frame latency from the
  difference between their numbers. Complements Test Clock for checking
  whether feeds are synchronised.

### Effects
- Brightness, contrast, saturation, hue and chroma key changes update
  output immediately on stills, patterns, live sources and playing video.

## 2026-09-06 - v0.99.316 (browser cues you can drive, and a desk that explains itself)

### Sources
- New: interact with browser cues through the programme monitor.
  Click to dismiss consent dialogs or use page controls; use the wheel
  to scroll. Page scrollbars are hidden by default.
- Remote browser control: `BROWSER SCROLL`, `TOP`, `BOTTOM`, `CLICK`,
  `BACK`, `FORWARD`, `RELOAD` and `URL` from Companion or a network
  client.

### Interface
- All inline text editors now support a caret, arrow keys, `Home`,
  `End`, `Ctrl` word jumps, and clipboard paste, copy and cut.
  Long values scroll with the caret for editing within URLs and other text.
- Added hover tips with a short dwell for toolbar, bottom-bar, playlist
  loop/shuffle and per-cue controls. The startup mascot adds contextual
  tips for the hovered control. Disable in `Settings > Appearance`.
- Dashboard buttons now offer a menu of 34 actions across transport,
  show, audio, decks, VJ, picture and output, each with a label and glyph.
  Free-text commands remain available.
- Narrow playlists retain per-cue toggles by reducing thumbnail space
  first, then button width. Default width shows both picture and controls.
- Audio cue rows display waveforms; other cue kinds use their type icons.
- Empty VJ A, programme and B panes each show their own commentator.

### Desk
- Large imports process a few files at a time with progress reporting;
  dropped folders are scanned in the background. The window continues
  drawing and responding during imports of thousands of cues.
- Changed: SAVE overwrites the open show. SAVE AS is a separate button
  for creating a new file.

### Audio
- New: `AUDIONORM ALL` measures every file-backed cue in the deck and
  matches them to one loudness target. Waveform height follows the trim.
- Gain, pan, mono and normalise apply across both decks when both playlists
  are visible in the VJ split.

## 2026-09-06 - v0.99.315 (Pocket and touch mode, legible at any scale)

### Interface
- Fixed an issue where toolbar and bottom-bar labels were truncated at
  touch scales. IMPORT, BLACKOUT, NEW, OPEN, SAVE and BUNDLE now size
  from their font and retain complete labels at every UI scale.
- Button groups wrap onto a second row when needed, including Pocket
  mode at double scale. A button alone on a row retains its full width.
  Layout at 1x is unchanged.

## 2026-09-06 - v0.99.314 (exact at every raster width)

### Outputs
- ST 2110 picture conversion now handles every raster width, including
  odd widths, with the same accuracy as standard sizes.

### Validation
- Completed a review of less frequently changed areas, prioritised by
  change frequency: PTP, NMOS JSON and HTTP parsing, caption formats,
  motion fields, code-editor cursor handling, sockets, library loading
  and worker scheduling. All reviewed areas cleared.

## 2026-09-06 - v0.99.313 (files and links open through the platform itself)

### Interface
- Changed: Show in explorer and dependency links pass paths as arguments
  to the platform's native opener on every platform, without interpreting
  them as command text.
- Missing file-manager support is reported instead of assuming the
  request succeeded.

## 2026-09-06 - v0.99.312 (the overlay bin on the wire, and precise MMC)

### Remote protocol
- New: `OVERLAY PUSH <n>`, `OVERLAY POP` and `OVERLAY CLEAR` control
  the cue overlay bin from Companion, a surface or a script.
  Bare `OVERLAY` retains its timecode burn-in toggle.
- `STILLDUR`, `LOWERALPHA` and `OVERLAY PUSH` now explain rejected
  requests: missing arguments, unreadable numbers, no selected cue or an
  inapplicable cue type, instead of returning bare `OK`.

### MIDI
- Changed: MMC LOCATE accepts only complete timecodes, preventing partial
  requests from moving transport to an unintended frame.

## 2026-09-06 - v0.99.311 (the picture as a surface in space)

### Effects
- New: displacement mesh. Cue brightness displaces a grid out of the
  picture plane, with controls for height, tilt, yaw, spin and grid.
  Enable with `MESH ON`.
- Uses geometry rendering shared with perspective warp, without a new
  dependency, shader or depth buffer. Off by default; existing shows
  remain unchanged until enabled.

## 2026-09-06 - v0.99.310 (the dashboard responds, and clips look right)

### Interface
- Fixed an issue where dashboard tiles did not respond to clicks in all
  positions, including Add a button. The dashboard receives clicks before
  underlying controls; clicking outside closes it.
- Clip thumbnails preserve their aspect ratio, including 16:9 frames,
  and fill their slots without letterboxing or stretching.

## 2026-09-06 - v0.99.309 (you can see your clips)

### Interface
- New: video and still cues show their own picture at the start of each
  playlist row.
- Thumbnails load from cache without blocking. Missing pictures are
  requested as rows appear, with one decode at a time even in a
  thousand-cue playlist.
- Theme creatures now occupy empty playlist and inspector areas below
  their final cue or open section.
- The configured creature count is distributed across available gaps,
  rather than duplicated in each. Each group stays within its own gap
  and off controls; spider threads attach to the top of that gap.

## 2026-09-05 - v0.99.308 (a dashboard, six new effects, and every setting reachable)

### Effects
- New: Slit Scan holds a frame and refreshes a moving band, combining
  different moments in one picture.
- New: Ferrofluid turns bright areas into spikes using brightness as a
  magnetic field.
- New: Shatter divides the picture into independently sliding and rotating
  shards.
- New: Edge Ignite colours outlines from dull red to white according to
  edge strength.
- New: Relight treats brightness as height and illuminates it with a
  moving side light.
- New: Depth Split treats brightness as depth with different views for
  each eye.
- All 6 effects run within a 60 fps frame at 1080p and are covered by
  offline, animation and preview checks.

### Settings
- Added controls for the ST 2110 network interface, PTP domain, remuxing
  recordings to ordinary MP4 at stop, number of ASIO outputs to open,
  and MSC device ID. All show settings are now reachable in the app.

### Desk
- New: `Ctrl+D` opens a custom dashboard. Each tile runs a remote-protocol
  command and shares its slot with Companion's `DASH <n>`.
- The grid reflows with window size and when buttons are added or removed.
  Each tile's pencil edits label, command and glyph on one line; its
  colour chip cycles theme colours.
- Tiles drift independently and spring when fired.
- New: `DASH SHOW` opens the dashboard from a control surface.

## 2026-09-04 - v0.99.307 (streaming, stream telemetry, and audio cues you can watch)

### Streaming
- SRT, RTMP and UDP deliver continuously while the receiver is listening.
  Cue changes keep the connection open; receiver loss triggers automatic
  reconnection.
- Subsequent measurement: 30 seconds of SRT arrived as 30 seconds,
  900 packets at exactly 30 fps, with audio and video within one frame.
- New: live delivered frame rate, uptime, frames sent and frames dropped.
  Streams report when the receiver stops accepting frames.
- YouTube and Twitch destination presets set ingest URL, keyframe interval
  and bitrate together.

### Audio
- New: audio-cue pictures selectable in the inspector's AUDIO section or
  with `AUDIOVIS <mode>`: waveform, oscilloscope, Lissajous, third-octave
  spectrum, peak/RMS meters or name card.
- Lissajous plots left against right to expose polarity differences.
  The program monitor and outputs draw the same selected visualisation.
- Cues expected to have audio now report device, file or decode reasons
  for silence, even when transport and waveform appear normal.

### Cues
- Trimmed clips show green/red end caps and a range readout such as
  `TRIM 00:30.0 - 01:30.0`. The ruler uses source time instead of
  restarting at zero.
- In and out points now apply to live cues. Changing out preserves
  uninterrupted picture; changing in restarts decode while holding the
  same displayed frame. Moving out behind the playhead loops immediately.

### Effects
- Parameter readouts and `FX PARAM` now show meaningful quantities or
  choices, e.g. `100 cols`, `symbols` and `green`; other parameters
  show percentages. Values retain the 0–1 range used by LFOs.

### Interface
- Still-image timeline tiles retain their original aspect ratio, using
  the same tiled layout as other cue kinds.
- Added a slide-conversion animation: pages leave a hopper, pass a scan
  bar and land on a stack that grows with completed pages.
- Startup mascot text types with a cursor; the mascot winks one eye
  every few seconds.

### macOS
- Published both architectures: Intel for macOS 13 and later, also
  runnable on Apple Silicon; native Apple Silicon for macOS 14 and later.

### Diagnostics
- `DECKBOY_STATE_DIR` redirects all writable state, including the pointer
  to the real show, into an isolated directory for test runs.

### Remote protocol
- `HELP ALL` counts itself correctly. The remote-help audit checks
  command counts as well as names.

## 2026-09-03 - v0.99.306 (a readable About page, and a mascot you can choose)

### Interface
- Improved About-page readability across all themes, including dark themes.
- New: None mascot choice removes character art from the splash and About
  masthead. Mascot changes apply immediately without restarting, and the
  selected character is displayed.

### Remote protocol
- Fixed an issue where `SET theme`, `SET ui scale` and `SET mascot`
  changed saved settings without applying them to the running interface.

## 2026-09-03 - v0.99.305 (the dashboard: your own buttons)

### Desk
- New: dashboard slots containing a label, glyph and any accepted Deckboy
  command. Examples include panic, `VJ BLEND ember`, output arming and
  `SET title Act Two`.
- Slots save with the show. Glyphs accept symbols and emoji Deckboy can
  draw.
- Commands that fire the dashboard from a dashboard slot are refused
  to prevent loops.

### Remote protocol
- `DASH SET <n> <label> | <command> | <glyph>` defines a slot.
  Examples: slot 1 labelled Panic runs `PANIC` with a warning glyph;
  slot 2 labelled Ember runs `VJ BLEND ember` with a fire glyph;
  slot 3 labelled Act Two runs `SET title Act Two` with a clapper glyph.
- `DASH LIST` lists slots; `DASH 2` fires slot 2;
  `DASH CLEAR 3` clears slot 3.
- Companion's `DASH <n>` uses the same saved slots as on-screen buttons.

## 2026-09-03 - v0.99.304 (every project setting, over the wire)

### Remote protocol
- New: `GET` and `SET` expose all 86 named project settings to control
  surfaces and scripts, including settings previously without commands.
- `GET` lists keys; `GET <key>` reads a value;
  `SET <key> <value>` changes it.
- Clamped values report the result, e.g. `SET ptp_domain 9999` returns
  `ptp_domain = 127`. Unknown keys are rejected.

## 2026-09-03 - v0.99.303 (all six builds, everywhere)

### Packaging
- Published the v0.99.302 release across all 6 builds, including macOS,
  Linux and Windows configurations without Spout. No behaviour changes.

## 2026-09-03 - v0.99.302 (Spout input works)

### Sources
- New: Syphon/Spout source cues receive Spout senders on the local machine,
  including Resolume, OBS and another Deckboy, for mixing as ordinary cues.
- `SPOUTCUE <sender name>` adds a source. Omit the name to receive an
  available sender.
- Stopping a Spout, NDI or DeckLink cue stops capture and its processing
  cost until the cue is taken again.

## 2026-09-03 - v0.99.301 (a source says what this machine can do)

### Sources
- Adding a Syphon/Spout cue now immediately reports whether the current
  platform can receive it, using capture support information instead of a
  placeholder picture.
- Windows Spout receive and macOS Syphon receive are still planned in this
  version.
- Working window and camera sources are unchanged and show no additional
  message.

## 2026-09-03 - v0.99.3 (an output can be driven from a surface)

### Remote protocol
- New: `OUTPUT` returns the focused output's state; `OUTPUT LIST`
  lists every output and whether it is enabled.
- `OUTPUT ON|OFF|TOGGLE` controls output enable.
  `OUTPUT <n>` focuses an output.
- `OUTPUT SPOUT ON|OFF|TOGGLE` controls Spout routing;
  `OUTPUT SPOUT NAME <sender name>` sets its sender name.
  Spout now has remote control alongside NDI and DeckLink.

### Interface
- Fixed an issue where Settings did not open on the requested tab,
  including System.

## 2026-09-03 - v0.99.2 (NDI input works)

### Sources
- New: NDI source cues receive pictures directly through the NDI runtime,
  as DeckLink uses native capture.
- Add from the cue menu or with `NDICUE <source name>`.
  Loose name matching accepts names such as `Test Pattern` without
  requiring the sending machine's name.
- Cues keep searching when the source is not yet available.
- Stopping an NDI or DeckLink input cue stops capture.

## 2026-09-03 - v0.99.1 (rendered slides live with the show)

### Cues
- PowerPoint and Keynote slide renders now live in a `<show>_media`
  folder beside the show file. Cues reference these media files directly;
  copying or deleting the show folder includes its rendered slides.
- Unsaved shows render into the state folder. Saving a show leaves
  existing renders at the locations its cues already reference.

## 2026-09-03 - v0.99.0 (text mode, at full brightness)

### Effects
- Changed: coloured Text Mode uses character density for tone and ink for
  colour. Channel ratios preserve hue; each character uses the maximum
  brightness that hue can reach.
- On a test frame, average lit-pixel brightness increased from 49 to 195
  out of 255.
- Green, amber, cyan and palette phosphor inks are unchanged.

## 2026-09-03 - v0.98.1 (a dismissed suggestion stays dismissed)

### Interface
- Fixed an issue where dismissing the HAP conversion suggestion did not
  keep it dismissed across launches.

## 2026-09-03 - v0.98.0 (ten blend modes, faders you can reach the ends of)

### VJ
- 10 blend modes: `dissolve`, `add`, `screen`, `multiply`,
  `lighten`, `darken`, `subtract`, `undercut`, `infiltrate` and
  `ember`.
- Only Dissolve fades out the outgoing deck. Other modes retain it at full
  level while introducing the incoming deck.
- Lighten and Darken select the brighter or darker value per channel.
  Undercut subtracts outgoing from incoming. Infiltrate introduces the
  incoming deck through outgoing shadows; Ember uses highlights.
- Multiply responds across the full fader range from zero.
- Bare `VJ BLEND` cycles modes. Unknown names are rejected instead of
  selecting Dissolve.

### Interface
- Deck-opacity faders now support dragging as well as clicking.
- Opacity and crossfader endpoints have wider landing areas for 0/100
  and full A/full B, replacing single-pixel targets. Precision along the
  middle of the rail is unchanged.

## 2026-09-02 - v0.97.0 (NDI output verified, NDI input tells the truth)

### Outputs
- Verified NDI output frame by frame against a receiver: 3840x2160,
  correct orientation and correct colour-bar order.

### Sources
- NDI cues report input availability when added. This build relies on
  ffmpeg's `libndi_newtek` device, which ffmpeg no longer includes;
  the cue reports this instead of remaining blank.

### Remote protocol
- New: `NDICUE <source name>` adds an NDI input, completing remote
  creation commands for live source kinds.

## 2026-09-02 - v0.96.0 (Spout output, the right way up and the right colour)

### Outputs
- Fixed an issue where Spout output arrived inverted or with red and blue
  exchanged. Verified against the test card in Resolume with no receiver
  correction; feeds are usable directly in Resolume, OBS and Deckboy.
- Spout sends directly from the capture buffer without an intermediate
  texture, avoiding about 66 MB of copying per 4K frame.

## 2026-09-02 - v0.95.2 (a converter that fails now says why)

### Cues
- Presentation conversion failures now display the converter's own error,
  e.g. `PowerPoint: Exception from HRESULT: 0x80CB4002`.
- When importing 2 slide decks simultaneously, the progress card remains
  visible until both finish and shows the number of active imports.

## 2026-09-02 - v0.95.1 (the video synth reaches the text mode controls)

### Video Synth
- Fixed an issue where the Text Mode row did not reflect the effect on a
  Video Synth cue. Character-grid controls now appear and respond on these
  cues, where that row is their only entry point.

## 2026-09-02 - v0.95.0 (one text mode)

### Effects
- Changed: Text Mode uses one effect on every cue, replacing the separate
  Video Synth character-grid switch. All controls use the same effect.
- Older shows automatically convert the former switch to an effect while
  retaining its settings. Shows saved by this version still open in older
  builds.
- Sprite Sheet and Font (type anything) character sets are now available
  on every cue, previously only Video Synth. Existing shows retain their
  chosen character set.
- Turning Text Mode off and on preserves all settings.

## 2026-09-02 - v0.94.1 (type your own text into TEXT MODE)

### Effects
- Fixed an issue where Text Mode glyph and phrase edits were not retained
  on ordinary clips and Video Synth cues.
- Fixed an issue where the selected Text Mode font was used only for
  characters outside the built-in 5x7 set. It now renders ordinary letters
  as well as extended characters.

## 2026-09-02 - v0.94.0 (import a PowerPoint or a Keynote)

### Cues
- New: direct import of `.pptx`, `.ppt`, `.pps`, `.key` and `.odp`.
  Export uses PowerPoint on Windows, Keynote for `.key` on macOS, or
  LibreOffice where installed, followed by PDF page rendering.
- Each slide becomes a 3840-pixel-wide still cue that holds until the next
  take. Native application export preserves fonts, layout and image
  resolution; fine hatching remained resolved in a test-pattern deck.
- Imports using LibreOffice report the converter on completion, since it
  can substitute unavailable fonts.
- A 100-slide deck takes about 30 seconds to convert and render.
  Progress shows the startup character, slide count and a moving bar.
- Imports report unplugged drives and renamed source folders.

### Interface
- Actionable warnings remain visible longer and use distinct styling from
  confirmation messages.

### Known issues
- PDF-based imports do not retain builds, transitions or embedded video
  and audio in this version. Use a live window-capture source for
  animated presentations.

## 2026-09-02 - v0.93.1 (the picture-following wobble follows the picture)

### Effects
- Changed: Text Mode Flow wobble follows picture edges where present and
  returns to the wave in areas without an edge. Hue wobble is weighted
  by the amount of colour in each cell.
- Remote wobble modes accept `flow` and `hue`, with aliases `luma`
  and `colour`. Unknown names are rejected instead of selecting
  `drift`.
- Hand-entered character sets now indicate when a preset replaces them,
  matching the character-set row.
- Fixed an issue where glitch effects changed the background colour around
  rotated characters.

## 2026-09-02 - v0.93.0 (text that turns, and a show that keeps a spare)

### Effects
- Changed: Text Mode wobble rotates each character as a small card about
  2 axes with perspective, converging edges and visible near/far sides.
- Rotation travels across the grid as a wave. Lighting darkens characters
  edge-on and restores brightness as they face the viewer.
- One amount control; zero disables the effect and its processing cost.
- The area around rotated characters retains the background at every
  character size and angle, without exposing the source picture.
- Added a None preset position. Selecting a built-in character set
  replaces an active preset and reports the change; the row also
  indicates custom sets.
- Preset, font and custom-glyph controls now sit directly below the set
  they override, in their application order.

### Desk
- New: a `.bak` beside the show before its first overwrite, preserving
  the show as it was when opened. Existing all-or-nothing saves remain
  unchanged.

### Remote protocol
- New: `ASCII STATUS` returns the complete Text Mode state on one line:
  ink, set, preset, columns, chaos, wobble and typeface.

## 2026-09-01 - v0.92.1 (the glitch takes the picture's colour)

### Effects
- Changed: Text Mode glitch marks use the clip's own colour, including
  Picture ink.
- One third of marks reduce one colour channel for fringing. Colour is
  scaled without adding brightness, so dark source areas stay dark.

### Diagnostics
- Crash reports identify the faulting thread and whether it owns drawing.

## 2026-09-01 - v0.92.0 (curated sets, a typeface of your choosing, and wobble)

### Effects
- New: 32 Text Mode presets of symbols and emoji. Symbol sets include
  dots, stars, sparkles, music, hearts, flowers, arrows, geometry,
  blocks, box drawing, circles, weather, zodiac, chess and cards,
  runes, Greek, braille, currency, maths, dice and faces.
- Emoji presets span a dozen sets, including faces, nature, show, party,
  creatures, space and fruit.
- Presets fill the editable custom-glyph field and order symbols from
  light to heavy for brightness mapping.
- New: Font row selects the character typeface; click again for automatic.
  The selected font is tried first, with system-font fallback for
  missing characters.
- Characters unsupported by every installed font are omitted instead of
  drawn as empty boxes.
- New: Wobble tilts character cards using squash, stretch and shear, with
  an independent phase per cell.
- Wobble by modes: Drift uses position and time; Flow follows the picture's
  luma gradient; Hue uses cell colour. Zero amount has no processing cost.

## 2026-09-01 - v0.91.0 (it can tell you there is a new one)

### Updates
- New: UPDATES card in System settings checks GitHub for newer releases
  and offers a download.
- Startup checks are off by default; CHECK NOW works whether they are
  enabled or not. This is Deckboy's only automatic outgoing connection
  in this version.
- Checking, downloading and installing are separate actions. Installation
  requires its own button and restarts the app.
- Download and installation are refused while anything is live.
- Downloads are checked against the release's reported size. Incomplete
  downloads are not offered for installation.
- Completed installers are deleted at the next start, avoiding retained
  files of about 90 MB in the state folder.
- `deckboy --check-update` prints the result and exits.
- `UPDATE check|download|install|status` provides remote control.

## 2026-09-01 - v0.90.0 (type anything, including emoji)

### Effects
- Custom Text Mode glyphs accept typed or pasted letters, stars, notes,
  flowers, arrows, box drawing, dingbats and scripts supported by installed
  fonts.
- Each character is searched across multiple fonts, rendered once per
  size and cached. Mixed sets such as `A♪★b✿` are ordered from darkest
  to brightest.
- New: colour emoji where the platform has a colour emoji font.
  Windows and macOS include one; most Linux installations can add one.
  Emoji retain their own colours and ignore the ink setting.
- New: Font (type anything) glyph set forces font rendering even for
  plain letters. Entering a character absent from the built-in sets
  selects font rendering automatically.

## 2026-09-01 - v0.89.5 (picture ink means the picture)

### Effects
- Changed: Text Mode Picture ink uses each cell's source colour.
  On one held frame, 19.5% of lit source pixels and 20.1% of resulting
  glyphs were desaturated.
- Palette ink retains its quantised 16-colour appearance.

## 2026-08-31 - v0.89.4 (fold the inspector down to the effects)

### Interface
- Fixed an issue where collapsing inspector sections misplaced EFFECTS
  and TEXT MODE. They now continue directly after the preceding sections,
  whether expanded or folded.

### Remote protocol
- `SECTION playback|metadata|geometry|key|effects|timer|tone|text` folds
  the corresponding inspector section from a control surface.

## 2026-08-31 - v0.89.3 (the text mode section works on any cue, and a chaos knob)

### Effects
- Text Mode controls now apply to any cue carrying the effect, including
  clips, stills, cameras and Video Synth: columns, glyph set, shuffle,
  ink, glitch amounts, custom glyphs and phrases.
- New: Chaos row. At 0, glyphs follow cell brightness; at 1, cells can
  use any glyph in the set. Intermediate values mix the two behaviours.
- Chaos exposes the full Music & Sparkle set even in flat picture areas,
  and selects only entered characters for custom sets. Its pattern
  follows cell position rather than frame number, avoiding flicker.
- Fewer columns enlarge cells, making detailed glyphs such as notes
  easier to read.

### Remote protocol
- New: `ASCII INK`, `ASCII SET`, `ASCII SHUFFLE`, `ASCII COLS <n>`
  and `ASCII CHAOS <0..1>` control Text Mode on any cue carrying
  the effect.

## 2026-08-31 - v0.89.2 (the text mode controls drive the picture)

### Effects
- Fixed an issue where Text Mode Columns, glyph set and Ink controls did
  not change the picture on cues carrying the effect.
- Cycle GLYPHS to select Music & Sparkle.
- The inspector and rendering now share the same mapping of the effect's
  4 parameters to the character grid.

## 2026-08-31 - v0.89.1 (text mode fills the frame)

### Effects
- Fixed an issue where Text Mode did not fill the raster at every column
  count and output size. Proportional cell boundaries now place the last
  row and column exactly on the frame edge.
- Glitch marks use the same grid and remain inside their cells through
  the final row and column.

## 2026-08-31 - v0.89.0 (marks to draw with, devices that come back)

### Effects
- New: Music & Sparkle glyph set with dots, rings, arc, tilde, plus signs,
  crosses, diamonds, star, and quarter, quaver, beamed and double-beamed
  notes. Select through GLYPHS or the Text Mode glyph-set parameter,
  including LFO control.
- Custom glyphs accept typed or pasted marks and recognise alternate
  character spellings of the same symbol. Letters and marks can be mixed.
- SHUFFLE remaps which glyph represents each brightness using its seed.

### Audio and MIDI
- Decks retain the selected audio-device name when the interface is absent.
  `not found, on default` reports fallback to the system default;
  the deck returns to the selected interface when it appears.
- Disconnecting an active audio interface switches to the system default.
- The selected MIDI port saves with the show, survives restart and is
  reported by name when absent. Disconnects are reported; reconnects are
  automatic.

### Diagnostics
- New: `deckboy --devices` lists audio devices, displays, MIDI ports and
  render drivers using the names required by show files. Includes device
  rates and channel counts, and display refresh and scaling.

### Cues
- Audio cues now support in and out points in the inspector and remote
  protocol, as video clips do.
- Streams and capture inputs expose GEOMETRY, KEY and EFFECTS.
  DeckLink inputs have a full inspector and name the selected card.

### Desk
- Shows save to a temporary file beside the destination, then rename into
  place, preventing partial show files.
- Opening a show leaves its file unchanged.
- Fixed timecode readout rollover at minute boundaries.

### Remote protocol
- `IN`, `OUT` and `MIDI` report their results and reasons for
  requests they cannot complete.

## 2026-08-29 - v0.88.0 (text mode everywhere, and a language to write in)

### Effects
- New: TEXT MODE effect on clips, capture cards, cameras, browser cues
  and stills, using the Video Synth character grid, glyph sets and
  phosphor inks.
- The inspector's TEXT MODE section exposes columns, glyph set, shuffle,
  ink, custom glyphs, phrases and phrase hold.
- Columns, corruption, glyph set and ink are effect parameters with
  faders and LFO support.
- Amount mixes with the source: 1.0 replaces the picture with the grid;
  intermediate values retain the original between characters.
- Custom glyphs are ordered darkest first. Sets can use 2 characters,
  words or box-drawing marks.
- Phrases are separated by `|`, shown one at a time at changing
  positions. Phrase hold sets their dwell.
- Video Synth memory use remains flat through long-running cues and
  playback runs at full rate at 4K. Text Mode renders at 60 fps at 4K.

### Sources
- Code sources now support named values and semicolon-separated
  statements ending in channel expressions. Reused values are evaluated
  once; existing expressions without semicolons retain their meaning.
- Example: `ox = sin(t)*0.55; oy = cos(t*0.8)*0.4;`
  `d = length(cx-ox, cy-oy); glow = smoothstep(0.45, 0.0, d);`
  `glow, glow*0.35, 1-glow*0.6`.
- Added `length`, `smoothstep`, `if`, `sign`, `exp`, `log`
  and `atan`.
- The editor accepts `Shift+Enter` for new lines, sizes to its contents,
  highlights user-defined names and includes 6 statement-form examples.
  The adjacent helper uses larger, brighter text and explains functions.

### Interface
- New: VJ mode switch at the top of
  `Settings > System > SHOW FLOW`, choosing one deck and playlist or
  two decks and a crossfader.
- Creature visibility now offers Off, When idle and Always.
  When idle remains the default, keeping the interface still during shows.

### Companion and remote protocol
- `ASCII ON|OFF|TOGGLE`, `ASCII GLYPHS`, `ASCII PHRASES` and
  `ASCII HOLD` control Text Mode, with corresponding Companion actions.

## 2026-08-29 - v0.87.0 (VJ mode, a code source, and eight effects nobody has)

### VJ
- New: VJ mode combines 2 independently controlled decks on one output
  with a crossfader and tempo. Disabling it preserves existing cue-deck
  behaviour; deck levels and cue fades remain effective.
- Dissolve fades both decks over black. Add and Multiply keep the base
  deck at full level while the incoming deck follows the fader.
- Recorded red/blue test: Dissolve moved from RGB 250/0/0 through
  64/0/127 to 0/0/253; Add produced magenta and Multiply black.
- Tap tempo averages recent taps and resets after a gap over 2 seconds.
  Quantised takes wait for the next beat. At 60 BPM, measured take delay
  was 0.04 s without quantisation and 0.16–0.81 s with it.
- VJ mode shows a control band across the programme column, a distinct
  window-border colour and side-by-side playlists labelled A and B.
- The band enters over one third of a second. Badge and frame pulse to
  the beat; the fader handle leans with movement and leaves a fading
  trail. Its readout stays on a dark background; controls contract to
  preserve fader width on small windows.

### Sources
- New: Code source, a per-pixel expression editable during playback.
  Accepts one expression or 3 comma-separated RGB expressions, e.g.
  `sin(x*12+t)*0.5+0.5, sin(y*9-t)*0.5+0.5, r`.
- Variables: `x`, `y` in 0–1; `cx`, `cy` in -1–1 from the
  centre; `r` radius; `a` angle; `t` seconds.
- Functions: `sin`, `cos`, `tan`, `abs`, `floor`, `fract`,
  `sqrt`, `min`, `max`, `mod`, `pow`, `atan2`, `step`,
  `clamp`, `mix`.
- Expressions compile once and are cached by text for CPU evaluation
  across cores. No runtime GLSL compiler is bundled: D3D11, D3D12, Metal
  and OpenGL use compiled SPIR-V, DXIL or MSL formats, and a compiler would
  add tens of megabytes plus backend translation.
- Compile errors retain the last picture and appear in the inspector.
  Division or modulo by zero and square roots of negative values are
  bounded instead of producing infinities. The language has a test suite.
- The editor colours functions, values, numbers, brackets, operators and
  RGB separators. Invalid names turn red while typing, using the same
  accepted names as compilation.
- Standard caret keys and click-to-place are supported. Variable/function
  chips insert text; functions include brackets with the caret inside.
  Examples are selectable from a picker.
- The editor mascot explains the hovered name and displays compile errors.

### Effects
- EFFECTS is now available on every cue kind, including patterns, stills,
  cameras, NDI and streams. Grain, Caustics and Vignette were verified on
  a colour-bar pattern through remote commands.
- Added live per-frame chain cost against the 16.7 ms budget at 60 fps,
  measured on the current machine and raster after processing runs.
  The 12-effect limit remains; over-budget chains are identified.
- Audio remains the master clock when effects exceed the frame budget:
  sound continues in real time and picture frames are dropped to keep sync.
- New: Schlieren, using the picture as a density field for a directional
  gradient, like knife-edge imaging of shockwaves or heat. Flat areas
  remain mid-grey; knife angle selects visible gradients. 4.7 ms at 1080p.
- New: Chladni, arranging the picture along stationary lines of a vibrating
  plate, based on figures catalogued in 1787. Two mode numbers set the
  shape: integers produce classical figures and intermediate values
  simulate off-resonance drive. 7.2 ms at 1080p.
- New: Wavefront solves a wave equation seeded by picture brightness.
  Waves retain inertia, interfere and reflect from frame edges.
  14.9 ms at 1080p.
- New: Crystallise grows grains from scattered seeds at different speeds
  until they meet. Each grain's lighting follows its direction to the
  seed, producing irregular lit facets. 15.7 ms at 1080p.
- New: Night Eyes keeps brightness immediate while colour catches up
  after movement, modelling fast colour-blind rods and slower cones.
  Purkinje shifts sensitivity towards blue. 3.8 ms at 1080p.
- New: Grain Flow smears along local picture structure using line-integral
  convolution and a structure tensor, following features rather than
  crossing their edges. Flat areas are unchanged. 13.0 ms at 1080p.
- All 6 effects fit a 60 fps frame at 1080p. At 3840x2160, measured costs
  are Night Eyes 14 ms, Schlieren 17 ms, Chladni 27 ms, Grain Flow 46 ms,
  Crystallise 54 ms and Wavefront 57 ms; Caustics is 23 ms.
  Heavy 4K effects can take several frames; use one at a time on a fast
  machine and measure with `--effect-bench <token> 3840x2160`.
- New: Caustics combines refraction with light concentration and spreading
  derived from the displacement field's divergence. Four crossed waves
  use different angles and rates.
- Caustics controls: Chop spans ocean swell to puddle ripples; Swell speed
  sets rate; Focus sets light concentration, compressing highlights to
  avoid clipping to white. 1.9 ms at 1080p.
- New: Feedback with Zoom, Spin, Drift and Colour bleed.
  Zoom makes inward/outward tunnels, Spin makes spirals, Drift offsets
  trails, and Colour bleed shifts trail colour as it fades.
- Feedback uses the brighter of live picture and decayed echo instead of
  adding them, bounding brightness to the input. Over 120 passes, output
  settled within 3 levels out of 255.
- Feedback clears on take. Multiple outputs sharing a deck advance it
  only once per frame. Measured at 1.0 ms at 1080p, 5.4x faster than the
  initial version with byte-identical output.
- New: Motion Puppet Memory accumulates motion and Spring returns it to
  rest. On the same driver, mean displacement changed from 10.5 to
  25.9 grey levels per pixel. Memory 0 retains the original per-frame
  motion field and preserves older shows.
- Time-based effects animate on stills: Grain, Ripple, Caustics and
  Feedback. On static colour bars, monitor pixels changed by 9.7%, 2.0%
  and 2.8% respectively for Grain, Ripple and Caustics over 1 second.
- Video effects still follow source frame indices, preserving repeatable
  grading and recordings.
- Pixel Sort now produces identical output for the same cue and frame
  on Windows, macOS and Linux.

### Modulation
- New: LFOs on every effect parameter and Amount. Toggle `~` beside
  a parameter to expose shape, rate and depth.
- Shapes: Sine, Triangle, Saw, Ramp, Square and Sample-and-hold.
  Sample-and-hold selects one value per cycle, reproducible for the same
  cycle number.
- Tempo sync uses musical cycle lengths from one quarter to 32 beats.
  Free-running rate changes multiplicatively, from one cycle per minute
  to several per second.
- Modulation centres on the existing value without a jump when enabled.
  Values remain within 0–1; near either end, the swing becomes asymmetric.
- Preview and output share one clock sampled per frame. Effects remain
  independently measurable and headlessly renderable; cues without LFOs
  avoid modulation-copy overhead.
- Only enabled LFOs are saved. Older shows load unchanged; new shows
  remain readable by builds predating LFOs.

### Companion and remote protocol
- New: `VJ ON|OFF`, `VJ MIX <0-1>`,
  `VJ BLEND <dissolve|add|multiply>`, `VJ TAP`, `VJ BPM <n>`,
  `VJ QUANTISE <on|off>`, `VJ DECKS <a> <b>`, `VJ STATUS`
  and `VJ TOGGLE`.
- New: `CODE GET`, `CODE SET <expression>` and `CODE EDIT`.
- New: `FX LFO <n> <A-E> on|off|shape|rate|depth|phase|sync|beats [value]`,
  where E controls Amount.
- Companion adds actions for crossfader, blend, tap tempo, BPM, quantised
  takes, deck assignment, effect creation, amount, parameters, LFOs and
  code-source expressions.

### Packaging
- Tags build and publish 6 packages: `-windows-x64-setup.exe`,
  `-windows-x64.zip`, `-macos-arm64.dmg`, `-macos-arm64.zip`,
  `.AppImage` and `-linux-x86_64.tar.gz`.
- Publishing uses artifacts from tested build jobs without rebuilding.
  Packaging jobs unpack their output and run the contained binary;
  published files are the same ones that passed `--smoke`.
- Publishing requires all 6 packages and takes notes from the matching
  version heading in this changelog.
- Packaging runs on every push to main; only publishing requires a tag.

### Diagnostics and interface
- `--effect-dump` now accepts a pass count for effects with state.
- Offline animation checks sample every effect at 9 frame indices and
  verify effects declared to animate.
- Preview checks measure edge energy as well as changed pixels using a
  held half-fractal, half-colour-bar frame, eliminating seek variation.
- Text in the timeline and cue inspector stays within its margins.

## 2026-08-27 - v0.86.0 (recordings that move, effects, a faster synth)

### Recording
- Fixed an issue where recordings repeated pictures instead of retaining
  the moving output. GPU readback is flushed and drained through a bounded
  queue; forced synchronous 4K60 readback captured 896 of 902 frames.
  Repeats no longer copy a 33 MB frame.
- Recording checks now count distinct pictures in the finished file,
  rather than relying only on reported frame totals.
- Segments carry continuous SMPTE timecode: each starts at the take's
  start plus frames already written. Verified across a tenth-minute
  boundary where drop-frame numbering skips no numbers.
- Fixed an issue where normal latency in the 3-frame readback pipeline
  triggered dropped-frame alarms. The alarm now checks for stale pictures
  and separately verifies that the writer drains.
- macOS and Linux gain asynchronous GPU readback. SDL 3.4 is now required.

### Video Synth
- Reduced 4K frame time from 35–40 ms to 6.1 ms, about 6 times faster.
  CRT processing now runs before upscaling; removed an overwritten
  33 MB clear per frame.
- Recording a synth cue improved from 233 of 360 frames with 12 alarms
  to 354 frames with no alarms.

### Effects
- New: ordered per-cue chain with Invert, Posterise, Solarise, Threshold,
  Vignette, Grain, Scanlines, RGB Split, Temporal Dither and Motion Puppet.
- Temporal Dither advances a reduced-palette dither each frame, allowing
  shade integration at 60 Hz. Pausing exposes the checkerboard.
- Motion Puppet uses a driver clip's codec motion vectors to displace
  the cue, without showing the driver on output. A camera can therefore
  follow motion from another clip.
- `--motion-probe <file>` assesses drivers. A mostly static test clip
  moved 1.7% of cells with no visible effect; a rotating clip moved 50%
  with strong displacement.
- Preview applies the same grade and effect chain with or without an
  armed output. All 15 pixel effects changed a fixed, seeked-and-paused
  frame during preview checks with no output armed.
- Expanded from 6 to 40 named parameters across 18 effects:
  Invert pivot and channel spread; Vignette size/falloff; Grain size
  and shared-channel film or separate-channel video noise; Scanlines
  darkness; RGB Split angle and green split; reverse Pixel Sort;
  Ripple frequency/speed; Kaleidoscope rotation; Reaction Bloom seed
  density and white-lifting glow.
- Only parameters used by an effect appear. Parameter checks verify
  every named control changes the picture.
- New parameter defaults reproduce older shows. All 15 pre-existing
  effects rendered byte-identically against the previous implementation
  at saved-show defaults. New and old show formats remain mutually
  readable, including the 2 added parameter slots.
- Optimised effects with exact channel lookup tables, parallel rows
  except for Block Glitch, reusable workers for dependent simulation
  steps, and bulk row shifts for Block Glitch.
- All optimised results were compared byte for byte with the previous
  implementation and remained identical.
- At 4K, table-based effects take 1.5–2.6 ms; Pixel Sort, warps and
  Dye Advect take 25–70 ms, dominated by scattered reads across a
  33 MB frame. Reducing effect detail can lower cost.
- New: Dye Advect carries colour along the picture's own structure,
  perpendicular to the luma gradient. Multiple short backward steps
  follow curves and preserve edges.
- New: Reaction Bloom uses picture-seeded Gray-Scott reaction-diffusion,
  with hundreds of iterations per frame producing coral, veins and
  dividing spots associated with Turing's 1952 patterns.
  One Feed/Kill control follows active wave, labyrinth, coral, worm
  and hole presets; a new shot seeds new growth.
- New: Lightspeed combines relativistic aberration with Doppler colour
  and brightness shifts. The centre opens, edges compress, forward light
  shifts brighter/blue and side light dimmer/red. A per-frame radial
  lookup replaces per-pixel trigonometry.
- Fixed luma-channel selection so Luma Displace follows visible brightness.

### Performance
- `--effect-bench` results at 1920x1080, median of 11 frames, all within
  a 60 fps frame budget:
- Scanlines 0.65 ms; Solarise 0.64 ms; Posterise 0.67 ms;
  Temporal Dither 0.74 ms; Invert 0.78 ms; Channel Offset 0.87 ms.
- Threshold 0.93 ms; Grain 1.74 ms; Vignette 2.29 ms;
  Block Glitch 2.70 ms; Luma Displace 5.59 ms; Polar Warp 7.50 ms.
- Ripple 7.19 ms; Relativistic 8.05 ms; Pixel Sort 9.13 ms;
  Kaleidoscope 10.8 ms; Dye Advect 14.9 ms; Reaction Bloom 15.6 ms.
- `--effect-bench <token[:amt[:a[:b]]]> [WxH] [frames]` reports median
  frame cost and the proportion of a 60 fps budget.

### Cues
- New: PDF import as one ordinary image cue per page, in document order
  and named after it, e.g. `keynote 1`, `keynote 2`. Each page uses
  HOLD until the next take; it can fade, crossfade, carry effects or be
  reordered without a document renderer running during the show.
- Pages render as PNG at 3840 pixels wide into the state directory,
  leaving the source document's folder untouched.
- Rendering uses each platform's existing engine: Windows' PDF engine
  used by Edge, macOS CoreGraphics used by Preview, and Linux
  `pdftoppm` from `poppler-utils`. Missing Linux support is reported;
  no PDF library is bundled.
- Target width accounts for Windows' 96-DPI units, macOS' 72-DPI points
  and Linux's DPI-based renderer. Windows checks the first PNG width
  and corrects for applied display scaling.
- The same PDF measured 3840 pixels wide on Windows and Linux Mint
  with Poppler 24.02; heights differed by at most one pixel from rounding.
- Rendering runs in the background; a 100-page deck takes seconds.
- Keynote exports with one page per build import as one cue per stage.
  Static decks can use Deckboy's own slide transitions.
- Dropping `.pptx`, `.ppt`, `.key`, `.odp` or `.pps` explains
  supported import/capture routes.
- `Page Down` and `Page Up` take the next and previous cue, supporting
  USB keyboard clickers such as D'San Perfect Cue, Logitech and Kensington.
- Schedules start from the running show's current time; opening at 2 pm
  does not fire missed morning cues.

### Interface
- Launch now shows the first rendered splash or loading frame instead of
  an empty window.
- Copy chain/Paste chain at the foot of EFFECTS copies effects without
  geometry, fades, crop or colour. Paste applies to all selected cues
  and includes the motion driver.
- Whole-cue COPY also includes the effect chain and motion driver.
- Motion drivers have an inspector thumbnail, position, field count and
  a clickable/draggable scrub bar. Preview and output share one driver
  advancement per frame; the thumbnail reuses the decoded driver picture.
- Removing or replacing the final Motion Puppet, clearing the chain or
  pasting a chain without one also removes its driver. Bypassing retains
  the driver and its settings.
- Added typed editing for 18 Video Synth, tone and chip-synth values:
  speed, scale, feedback, zoom, audio reactivity, detail, smear, glitch,
  CRT, sprite spin, level, frequency, note, attack, release, mod depth,
  mod ratio and retrigger. `Shift` while dragging gives fine adjustment.

### Remote protocol and diagnostics
- New: `FX COPY` and `FX PASTE` for effect chains.
- `HELP` lists effect commands; `FX LIST` returns its list to the
  client instead of displaying only a toast.
- `GOEND` jumps the playing cue to its final moment.
- New: `--effect-dump <token[:amount[:a[:b]]]> <in.ppm> <out.ppm> [frame]`
  processes one picture without a window, decoder or timing dependency,
  reporting cost. The offline sweep runs all effects in seconds and
  can produce a contact sheet.
- `--inspector-scroll` reaches offscreen inspector content, alongside
  `--settings` for opening a settings tab.
- The action audit checks missing handlers, unreachable controls and
  duplicate settings IDs. Checked 257 actions with no orphans.

### Known issues
- PowerPoint PDF export flattens builds to their final state and removes
  transitions. Use a live PowerPoint window-source cue for original
  animation and clicker control.

## 2026-08-24 - v0.85.0 (program recording, audio in, the synth sources)

### Recording
- New: programme recording from RECORD in the bottom bar's OUTPUT group.
  The button pulses while armed and displays file size. Recording has its
  own destination setting, separate from the encoder queue.
- Recording raster and rate are independent of programme output and
  default to following input. `RECFORMAT 1920x1080 59.94` scales a
  4K programme on the GPU before readback, reducing transferred bytes
  to one quarter.
- Broadcast rates use exact fractions: 23.976 is 24000/1001, not 23.98.
- Constant frame rate contains `rate x elapsed` frames. Missing pictures
  repeat the last frame; inability to keep up is reported once per second
  in output health and the show log.
- Timecode can start at a specified value, time of day or zero, with
  drop-frame, non-drop and automatic rate-based selection. Drop-frame
  supports 29.97 and 59.94; at 29.97 it skips 2 timecode numbers each
  minute except every tenth. It never drops video frames. The file
  carries the SMPTE flag.
- Codecs: ProRes LT/422/HQ/4444, DNxHR LB/SQ/HQ/HQX, H.264 and HEVC,
  with corresponding containers, pixel formats and vendor tags.
- Segment by minutes or megabytes, with a 3.8 GB ceiling for FAT32.
  Fragmented recordings remain playable after a power cut; clean stop
  remuxes to ordinary MP4, as in OBS.
- Windows uses asynchronous GPU readback and disables control-window vsync
  while recording. Verified frame-exact from a 4K programme: 2160p25,
  2160p30, 2160p50, 2160p59.94, 1080p50, 1080p29.97 and ProRes HQ 1080p25.
- macOS and Linux use portable readback in this version. Measured
  frame-exact at 1080p50, 1080p59.94 and 2160p25; behind at 4K above
  30p, with the dropped-frame alarm active.

### Audio
- New: microphone and line input with device selection, gain, clipping
  indicator, mono folding and recording bitrate.
- Input routes to programme, streams and recording. Streams and recordings
  now carry live audio on Windows.
- New: ASIO driver enumeration and direct cue-audio output through a
  real-time buffered callback, using the bundled SDK. Mismatched device
  sample rates are converted instead of rejected.

### Sources
- New: tone generator with diagnostic displays and a visual on/off control.
- New: 2A03 and FDS chip voices in one SYNTH section, selectable from
  SOURCE and controlled by parameters, MIDI or the computer keyboard.
- New: Video Synth with oscillators, mirrors, feedback, glitch stack,
  hardware palettes, CRT and a 95-glyph ASCII Text Mode.
- Sprite sets load from `data/sprites`, with per-tile rotation, flip,
  jitter and chaos. A picker imports a sprite sheet or sprite folder.

### Stage timer
- Added custom colours, 6 chimes, a silent rest, event logo and message
  placeholders.

### Cues and interface
- HAP conversion suggestions now use measured benefit figures.
- Datamosh adds EXTREME and per-cue encoder overrides.
- First launch shows the green Deckboy wordmark; subsequent splash
  images rotate by theme.
- Removed redundant Source suffixes from source-type labels.

### Validation
- Windows, macOS and Linux builds and smoke checks passed.

## 2026-08-20 - v0.84.0 (datamosh, HAP, stage timer, show log)

### Effects
- New: per-cue Datamosh withholds keyframes so P-frames move the previous
  picture using their motion vectors.
- Enabling Datamosh prepares media in the background. The cue continues
  playing the original until the complete transcode is available, including
  takes during preparation.
- CLASSIC uses MPEG-4 Part 2 for persistent motion smearing.
  SUBTLE uses H.264, whose intra-coded macroblocks refresh regions and
  heal the smear within a few frames, especially on detailed pictures.

### Playback
- New: HAP playback through a bundled container parser and Snappy
  decompressor, avoiding ffmpeg's CPU expansion of DXT to RGB.
  All-intra frames allow direct seeking without decoding forward.
- Added HAP sample generation for testing; ffmpeg requires a libsnappy
  build to encode HAP.

### Stage timer
- New: Timer cue with countdown, count-up and time-of-day modes,
  amber/red thresholds, overtime, message line and progress bar.
- Seven-segment and dot-matrix digits use geometry, rendering identically
  across machines without depending on installed fonts.
- Timer run, hold, reset and nudge are independent of cue transport,
  keeping the display on air while the clock is operated.

### Desk and cues
- New: show log records takes, blocked takes, stops, reracks, panics
  and show opens with wall-clock time and elapsed milliseconds.
  Each write is flushed.
- New: scheduled starts at wall-clock times without external timecode.
  Crossing a scheduled time triggers the cue even across a long frame;
  daily schedules re-arm at midnight.
- New: named cue markers. Jumps seek without retaking or blinking the
  picture; backward stepping has tolerance to return to a just-passed
  marker.

### Media encoder
- Added 22 output formats with availability checked against the installed
  ffmpeg, per-job hold/reorder and progress reporting.
- The queue defaults to one job at a time to preserve playback resources.
  The format picker encodes the selected format.

### Interface and packaging
- Interface chrome uses Press Start 2P, with a separate readable filename
  face. Cue-row icons stay within their boxes.
- Added 4 splash scenes.
- Child processes close with Deckboy; transcodes cannot outlive the app.

## 2026-08-19 — v0.83.2 (media encoder: queue, presets, progress)

### Media encoder
- New: encoder queue with a concurrency limit, default 1.
  CONVERT ALL FLAGGED on a 31-cue show queues all 31 and runs one.
- Progress follows ffmpeg's `-progress pipe:1` and `out_time_us`
  against media duration. It remains indeterminate until the first
  update, normally about once per second.
- Added 4 presets, selectable in the tab or through `ENCODEPRESET`:
  Delivery H.264 using NVENC with libx264 fallback, Proxy 720p,
  Match Source and Datamosh.
- Datamosh uses libx264 with `-bf 0 -sc_threshold 0 -refs 1 -g 120`.
  Verified B-frames changed from 1 to 0, references to 1 and GOP from
  `IBBPBBP` to `IPPPPPP`, with regular 2.0 s keyframes.
- Normal and Datamosh encodes both retained 1200 frames and 20.000000 s,
  allowing replacement without shifting sync. Datamosh writes
  `<stem>_mosh.mp4` beside the source without replacing it.
- NVENC is excluded from Datamosh because it ignores reference/scene-cut
  controls and inserts its own IDR frames.
- Queue controls: pause/resume, cancel all and per-job cancel.
  Cancelling stops ffmpeg and deletes the partial output.
- The busy panel reuses the startup mascot, with a progress bar per job
  and contextual text.
- New: MOSH: SMOOTH / CHUNKY. Smooth uses H.264 deblocking for flowing
  edges and writes `_mosh.mp4`; Chunky uses MPEG-4 Part 2 without
  deblocking for hard edges and writes `_mosh.avi`.
- Both recipes were verified with no B-frames, `IPPPPPP` GOPs and
  matching frame counts, preserving source interchangeability.
- Added 22 formats, offered only when present in `ffmpeg -encoders`.
  Includes ProRes 422/4444, DNxHR, QuickTime RLE, VP9, AV1, FFV1,
  MJPEG, GIF, PNG sequence, WAV/MP3 stems, HAP, HAP Alpha and HAP Q.
- Quality controls are codec-specific. Bulk encoding 5 or more cues
  to a mastering codec warns about disk space first.

### Windows
- Child processes terminate with Deckboy. Verified by forcibly closing
  Deckboy during an encode: the ffmpeg process count fell from 1 to 0.

### Interface
- Encoder cards, rows and body text now follow UI Scale and theme ink
  colours. PRESET and queue text use measured label widths.
- Added beach, autumn rooftop, winter campfire and Brooklyn waterfront
  splash scenes. Greyscale masters are gamma-matched to the existing
  pool for per-theme tinting.

### Documentation
- Added implementation plans for Datamosh and HAP playback.

### Known issues
- HAP still uses ordinary CPU DXT-to-RGB decode in this version, making
  it slower than H.264 with much larger files. Fast HAP playback and
  the Datamosh effect remain planned.

## 2026-08-16 — v0.83.2 (macOS: portable state, a real CLI, a protocol that answers)

### Packaging and storage
- Separated read-only bundled themes, fonts and sounds from writable state,
  preserving macOS app signatures and supporting read-only/non-writable
  installations.
- State location order: `DECKBOY_STATE_DIR`; otherwise the data folder
  if writable and outside an app bundle; otherwise per-user application
  data. Portable Windows and Linux installations retain their behaviour.
- Per-user paths: `%APPDATA%\Deckboy`,
  `~/Library/Application Support/Deckboy` and
  `$XDG_DATA_HOME/deckboy`.
- Existing show and last-opened pointer migrate once, preserving the
  current show during an upgrade. Soak logs and `_converted` media
  also use the state directory.

### Command line
- Added `--help` listing every flag.
- Option flags work in any position:
  `--decode-bench clip.mp4 --no-inproc-decode` and
  `--no-inproc-decode --decode-bench clip.mp4` are equivalent.
- `--flag=value` and `--flag value` are both accepted.
- Unknown flags and missing operands print an error and exit with code
  `2` instead of silently opening the interface.
- A bare path opens a show or imports media. The Windows installer's
  `.deckboy` association can use `"Deckboy.exe" "%1"`.

### Remote protocol
- Every command returns `OK <VERB>`,
  `ERR unknown command: <VERB>` or `ERR <VERB>: <reason>`.
  `HELP` lists supported commands. Companion ignores acknowledgements
  and logs errors.
- Changed: `MASTERVOL` uses percentages, matching `STATE`, toasts,
  Companion, MIDI CC and OSC. `MASTERVOL 60` means 60%.
- Explicit `150%` and `1.5x` are accepted; bare fractional values
  at or below 2 retain multiplier interpretation for older scripts.
  Out-of-range values are rejected instead of clamped.
- Added `RERACK`, completing remote access to all 4 transport buttons;
  command documentation matches the implemented verbs.

### Playback
- Audio streams close before their owners, audio decode is synchronised
  with device swaps, and PiP overlay playback closes while the renderer
  is still available, giving shutdown a consistent order.

## 2026-08-09 — v0.83.1 (macOS in the field, and proper installers for all three)

First macOS field run and first release with installers for all 3 platforms.

### macOS
- Import, open, save and relink use native in-process file dialogs, shared
  across platforms, with thread-safe result handling and retained filters.
- New: `.heic` and `.heif` still imports using HEIF reconstruction.
  Verified with an iPhone camera-roll file.
- Still-image decode reports failures separately from requests superseded
  by the next cue.
- Splash controls size from measured text, retaining full labels with
  wider macOS and Linux fonts.
- Added a native app icon from the master artwork.

### Audio and remote control
- Bundled libltc in macOS and Linux portable builds so LTC works without
  a separate install. Runtime discovery checks beside the executable first.
- Verified identical OSC, Companion, HyperDeck, Art-Net and TSL tally
  behaviour on Windows, macOS and Linux.

### Packaging
- Bundled ffmpeg is reachable on macOS and Linux.
- New: macOS `.dmg` with drag-to-Applications installation. Moving out
  of Downloads avoids App Translocation; CI mounts and checks the image.
- New: Windows Inno Setup `.exe` with Start Menu entry, optional desktop
  shortcut, opt-in `.deckboy` association and uninstaller.
  Install, run and uninstall were verified.
- New: self-contained Linux AppImage for current distributions.
  Verified with a cleared environment, finding both ffmpeg and libltc.
- Packaging documentation covers builds for every format.

### Diagnostics
- Linux and macOS write symbolised `deckboy-crash.log` reports.

### Known issues
- macOS notarisation and Windows code signing are not yet available because
  the required accounts are absent. Downloaded macOS builds need a one-time
  quarantine removal with `xattr -dr com.apple.quarantine`; Windows
  displays an unknown-publisher prompt on first run.
- HEIC requires ffmpeg 7.1 or newer for HEIF demuxing. It works on macOS
  but not in Linux builds using Ubuntu 24.04's ffmpeg 6.1.

## 2026-08-08 — v0.83.0 (portable macOS bundle)

### macOS

- New: portable `Deckboy.app` in `Deckboy-<VERSION>-macos-<arch>.zip`,
  equivalent to the Windows portable zip.
- Bundles all non-system libraries, including transitive dependencies, in
  `Contents/Frameworks`; references use `@rpath` instead of build-machine
  Homebrew paths.
- Re-signs binaries ad-hoc, inside-out, after rewriting library references so
  Apple Silicon accepts their signatures.
- Stores bundled data in `Contents/Resources`, keeping `Contents/MacOS`
  code-only. Renaming the app does not affect resource discovery; build-tree
  launches are unchanged.
- Includes camera, microphone and local-network usage descriptions required by
  macOS for camera and microphone access.

### Packaging

- Excludes the build machine's last-opened show pointer and default show, as
  on Windows.
- macOS CI packages on a Mac, rejects remaining `/opt/homebrew` or
  `/usr/local` binary references and build-machine state, and runs
  `codesign --verify --deep --strict`.
- CI runs `--self-check` and `--smoke` from inside the bundle to check
  relocated libraries and bundled data.

### Known issues

- No Developer ID signing, notarisation or universal binary. Downloaded zips
  require `xattr -dr com.apple.quarantine`, as documented in the bundled
  README.
- Browser cues, Spout and D3D11VA zero-copy decode remain Windows-only.

## 2026-08-08 — v0.83.0 (NMOS IS-04/IS-05: the 2110 senders become discoverable)

### NMOS

- New: IS-04 v1.3 Node API over HTTP, exposing node, devices, sources, flows
  and senders. Receivers are an empty list; Deckboy is a source device.
- Registers resources in dependency order: node, device, source, flow, sender.
  Heartbeats run every `5 s` against the registry's default `12 s` health
  timeout.
- A heartbeat `404` triggers re-registration after a registry restart. An
  unavailable registry retries with backoff from `1 s` to `30 s`.
- New: IS-05 v1.1 Connection API with per-sender constraints, staged state,
  active state and transportfile. Controllers can stage changes before
  activating them; the transportfile matches the SDP shown in Settings.
- IS-05 `PATCH` applies `master_enable` and `transport_params` with
  `activate_immediate` to the sender before replying. Changing the multicast
  group moves the stream; paired audio follows the video base port at `+2`.
- Resource IDs use UUIDv5 with a fixed Deckboy namespace and stable per-output
  seed. Saved controller routes survive app restarts without storing IDs on
  disk; three seeds matched Python's UUIDv5 results byte for byte.
- Controls are in `Settings > Video Outputs > Devices`, below ST 2110. Remote:
  `NMOS ON|OFF|STATUS|REGISTRY|PORT|NIC`, matching the existing `ST2110`
  command.
- `LOCAL ONLY`, the default, withholds registration because the Node API
  listens only on loopback. Settings and the arming toast explain why.
- AMWA NMOS Testing Tool `IS-05-01`: `36` passed, `2` failed for unsupported
  scheduled activation, `23` receiver tests not applicable.
- Verified against the AMWA reference registry `nmos-cpp` on a separate Linux
  machine: `1` device, `2` sources, `2` flows, `2` senders, `0` receivers. LAN
  SDP retrieval through `manifest_href` and an IS-05 `PATCH` to
  `239.77.7.7:22000` were reflected in the served SDP; registry expiry was
  `12 s`.

### Outputs

- Changed: the SDP origin address uses the route to the stream's destination
  multicast group. Verified with ProtonVPN active: the LAN NIC was selected
  instead of the tunnel or loopback.
- Destination address lookups are cached with a `30 s` refresh.

### Known issues

- No mDNS or DNS-SD registry discovery, or peer-to-peer discovery. Enter the
  registry URL manually.
- Scheduled activation is unsupported: `activate_scheduled_absolute` and
  `activate_scheduled_relative` return `501`; activation is not gated by the
  PTP clock.
- No IS-05 bulk staging (`501`), receivers, IS-07, IS-08, HTTPS or IS-10
  authentication.
- ST 2110 output is not PTP-locked or narrow-model paced.
- The `IS-04-01` Node API conformance suite has not been run; it requires the
  tool's registry and unsupported DNS-SD.

## 2026-08-07 — v0.82.1 (patterns hold a flat footprint)

### Patterns

- Pocket Test caches its static layer for a bounded set of raster sizes,
  allowing output and cue preview to retain their own cached sizes.
- Animated patterns reuse their existing frame storage.
- Fixed an issue where a pattern showed a stale video surface when played
  after a hardware-decoded video cue.
- Measured at `3840x2160`: private commit was `244 MB` idle, `265 MB` during
  video, `244 MB` for static SMPTE bars, `242 MB` for animated Test Bars and
  `245–246 MB` for Pocket Test; each had a `1 MB` swing.
- Pocket Test output measured a steady `60.0 fps` at 4K, with byte-identical
  rendering.

### Cues

- Changed: the right-click menu deletes the live cue in one click. The item
  reads `delete LIVE cue` and uses a stronger red.
- Keyboard `Delete` still asks for confirmation.

## 2026-08-07 — v0.82.0 (SMPTE ST 2110 output, PTP, dual streaming, safety UI)

### Outputs

- New: SMPTE ST 2110-20 uncompressed YCbCr 4:2:2 video, in `10-bit`
  (`2 px / 5 octets`) or `8-bit`. Supports RTP Sample Row Data descriptors,
  marker-bit framing, BT.709 studio-swing conversion, multicast TTL, NIC
  pinning and SDP with exact rational frame rates.
- Verified FFmpeg reception as `yuv422p10le` and `uyvy422`, with the correct
  picture.
- ST 2110-21 pacing spreads packets across the frame interval on a separate
  sender thread, without holding the render loop.
- New: ST 2110-30 / AES67 audio at `48 kHz`, with `1 ms` L24 packets carrying
  the same audio sent to the PA. Verified reception as
  `pcm_s24be, 48000 Hz, stereo`.
- New: PTP slave (IEEE 1588 / ST 2059) follows a grandmaster on a configurable
  domain and disciplines the RTP media clock. SDP advertises `ts-refclk:ptp`
  only when locked.
- Fixed an issue where edge feathering faded across the picture instead of
  forming an edge ramp. Works with and without perspective warp.

### Streaming

- SRT and RTMP can run simultaneously, with independent destination
  configurations.
- SRT controls include caller/listener mode, latency, passphrase and stream
  ID. These merge into the URL; manually entered URL values take precedence.
- RTMPS retains its secure transport; GOP length follows encoder settings.

### Timecode

- New: `--ltc-generate` generates LTC at `24`, `25` and `30 fps`, verified by
  decoding it in Deckboy, including midnight rollover.

### Desk

- New: `BLACKOUT` button beside `CLEAR`, also available with `B`.
- Changed: `Backspace` deletes cues; `U` clears overlays.
- Changed: successive `Escape` presses return to the control window, clear
  output, then quit.
- Added ten missing bindings to the shortcuts overlay.
- The `OUTPUT` group shows animated on-air indicators for each stream type.
- Fixed an issue where `SETTINGS` could not be opened when a scrolled-away
  inspector dropdown overlapped its button.
- Restored refresh-rate, bit-depth and raster-mode controls. Removed
  approximately `1,646` lines of superseded inspector implementation and `21`
  unreachable control handlers.

### Audio

- Loudness normalization uses a look-ahead peak limiter so requested gain
  reaches the deck audio path; a `-26.8 LUFS` source requesting `+10.8 dB`
  receives that gain.
- Changed: waveforms use a dB scale with headroom, showing gain changes that
  previously saturated the linear display above approximately `-6 dBFS`.
- Waveform analysis covers the first `5 minutes` of each file.

### Patterns

- Updated Terrarium from upstream revision `4931aa0`, with recorded
  provenance.
- New: `terrarium-pico` renders the Raspberry Pi panel's one-pixel-per-cell
  picture.
- Fixed an issue where `--pattern-dump` ignored its size argument for some
  patterns.

### Fonts

- Disabled UI font kerning to keep words such as `Target URL` intact.

### Packaging

- Packages exclude build-machine state.
- Crash reports include a symbolised stack in `data/deckboy-crash.log`,
  including crashes on file-dialog threads.

### Known issues

- ST 2110 uses software timestamps and wide-model pacing; no NMOS discovery.
  Limits are stated in the interface and feasibility documentation.
- LTC generation is not yet connected to a live audio output.

## 2026-07-31 — v0.81.4 (MIDI input on every platform)

### MIDI

- Added Windows and macOS MIDI input through RtMidi. Note and CC input uses
  the same command handling and documented mappings as Linux ALSA.
- New remote command: `MIDI ON|OFF|TOGGLE`, allowing Companion to arm MIDI
  without opening Audio settings.
- `--self-check` reports `midi-runtime` and the RtMidi port count.
- MTC quarter-frame and MMC/MSC SysEx remain ALSA-only; RtMidi input exposes
  channel-voice messages only. The integration catalogue names these limits.

### Diagnostics

- `--self-check` reports the cross-platform UDP NMC bridge on every platform.
  On Windows, it binds its port and reports `nmc[on,ok]`.

## 2026-07-30 — v0.81.3 (OSC Query on Windows, the full DeckLink card)

### Remote protocol

- Added OSC Query on Windows. Verified: `OSCQUERY ON` binds TCP port `5511`;
  an HTTP GET returns the OSC Query page with mirrored `/deckboy/state`
  feedback.

### Outputs

- Fixed an issue where the DeckLink `10-BIT` toggle and NDI `KEY` control
  extended outside their panels at some font sizes.
- NDI, DeckLink, Edge Blend and AOI sections size their headers and rows
  consistently.

## 2026-07-30 — v0.81.2 (genuinely dark themes)

### Themes

- Changed: bottom-bar groups, playlist body, deck list and timeline lanes use
  `screen_tile` and `screen_fg`, introduced in v0.79.3. The `15` themes
  defining these roles now draw dark structural panels.
- Themes without these roles retain their appearance: tile falls back to
  `screen_light`, foreground to `screen_deep`. The `gameboy` theme is
  unchanged.
- The library supports bright moulded cases, dark tinted chassis and
  true-black OLED terminals.

### Audio

- Fixed an issue where mono waveforms lacked the over-scale clip tint or drew
  outside their lane at higher gain trims. Both waveform views now use the
  same drawing behaviour.

### Build

- Verified CI on Windows, macOS and Linux.
- Fixed an issue where `-DENABLE_MIDI=ON` configuration failed with a
  CONFIG-package SDL3 installation. PkgConfig discovery now runs for every
  configuration.
- Display-topology support builds with both stable SDL `3.2.x` and `3.4`.

## 2026-07-30 — v0.81.1 (theme variety, honest LTC reporting, README)

### Themes

- Reworked `25` themes into three families: moulded cases with muted shells
  and saturated LCDs, dark tinted chassis with bright text, and true-black
  OLED terminals.
- OLED themes: `dark`, `virtual-boy`, `famicom`, `captain-falcon`,
  `game-and-watch`.
- `gameboy`, `pocket`, `sp`, `advance` and `color` retain their existing
  designs.
- Case colours use low saturation because `shell_inner` fills most interface
  panels; stronger character colours are reserved for the screen.
- All `30` themes passed the theme contrast audit.

### Timecode

- `--self-check` reports `ltc-runtime: ok` when the LTC library is available
  and `missing (...)` otherwise, on every platform.
- Windows loads `ltc.dll` at runtime; the portable zip includes it.

### Build

- Fixed an issue where `ENABLE_DECKLINK=OFF` builds failed to link. Both
  enabled and disabled builds include the required DeckLink support or
  fallback.
- CI runs on pushes and pull requests to the current branch, installs SDL3 on
  all three platforms, and uses one configure step per platform.
- CI runs smoke tests alongside `--self-check`, plus a separate Companion
  module test job.

### Documentation

- Updated the README to lead with the portable zip and Companion module, use a
  consistent theme count, and identify released features as available.

## 2026-07-29 — v0.81.0 (preview locked to output, display hot-plug, Companion module)

### Program monitor

- Changed: the control preview samples the finished program composite on every
  presented output frame, at full output rate instead of the approximately
  `10 fps` hardware-frame download path.
- Downscaling before readback reduces each transfer to approximately `0.5 MB`,
  from `3–12 MB`. Synchronization was checked by capturing both windows in
  both orders against a burned-in frame counter.
- The preview is taken before warp, AOI and edge blend so warp handles remain
  over an unwarped image. With no window output armed, the existing
  decoder-frame preview remains in use.

### Outputs

- Added detection of display connections, disconnections, replacements,
  desktop rearrangements and resolution changes while Deckboy is running.
- Display changes are debounced to allow the operating system's display
  topology to settle.
- Affected outputs move to their assigned displays even while fullscreen;
  unaffected outputs remain in place.
- When a pinned display disappears, its output becomes windowed and hidden
  with health `display missing: <name>`. It restores when that display
  returns, leaving the control screen available.
- Display-change toasts name the display, total display count and number of
  outputs moved, for example
  `display connected: DELL U2720Q (2 total) — 1 output re-homed`. `RESCAN`
  forces reassignment even when the topology has not changed.

### Interface

- Fixed an issue where neighbouring controls had misaligned labels. Panels and
  labels now use the same bounds, with width-dependent padding and consistent
  ellipsis and clipping.
- Settings cards and Video Outputs sections use consistent header heights;
  `CONNECTED DISPLAYS > IDENTIFY` stays inside its header.
- Reworked About into two aligned columns beneath the wordmark, version and
  build date.
- `PROJECT` lists copyright, GPL-3.0-or-later rights, source, warranty
  disclaimer, live session ports and theme.
- `BUILT WITH` lists the platform, SDL3/FreeType, FFmpeg, SDKs enabled in this
  build, timecode, font licence and key reference.

### Patterns

- New: `Test Clock (sync + latency)`, with colour bars, an aspect-check
  circle, scrolling hue band, seconds and frame counter, and exact timecode.
  Photograph two displays to compare synchronization.
- Aliases: `testsrc1`, `sync-card`, `latency-clock`.

### Audio

- Changed: cue gain trim spans `-40..+40 dB` consistently in editing,
  normalization, waveform display, playback, saved shows and remote control. A
  `-34 LUFS` source can receive the requested `+17.8 dB`.
- Normalization measures true peak in the same analysis pass and limits boost
  to keep peaks below `-1 dBFS`. The result toast identifies peak-limited
  adjustments, for example
  `normalized: -0.9 dB (was -25.7 LUFS) - peak-limited at -0.1 dBFS`.
- Over-scale waveform columns use the theme's danger colour as a clip warning
  and respond to gain changes.
- Toasts size to their text. Normalization results remain visible for `2.6 s`,
  or `3.2 s` when peak-limited.

### Companion

- New: Bitfocus Companion module replaces the Generic TCP/UDP setup. It polls
  `STATUS` for cue tally, transport colour, output health, a connection
  watchdog and derived countdown.
- Includes transport, cue select/take/goto, seek, levels, output, find and
  raw-command actions; `9` feedbacks; approximately `70` variables; and wired
  presets.
- Parser tests use a captured Deckboy status reply.
- Configuration and connection errors explain that Deckboy defaults to
  localhost-only access.

## 2026-07-22 — v0.80.2 (waveforms show gain, normalize feedback, SKIP button)

### Transport

- New: timeline skip buttons `>|` and `<|`, keys `.` and `,`, and remote
  commands `SKIP` and `SKIPBACK`.
- `>|` takes the next playable cue immediately using the same order as
  end-of-cue advance: goto targets, shuffle, playlist loop and missing-media
  skipping.
- `<|` takes the previous playable cue in list order, ignoring goto targets
  and shuffle.

### Audio

- Changed: cue gain trim scales waveform amplitude live in the inspector
  thumbnail, video-cue audio strip, timeline audio lane, active-cue mini view
  and output-monitor overlay. Gain edits and R128 normalization update the
  visible transients.
- Normalization reports `normalize: selection has no file-backed audio` when
  no selected cue has file-backed audio.

## 2026-07-18 — v0.80.1 (fast on slow drives, a DMG sound pack, vivid Terrarium)

### Desk

- Improved responsiveness with large playlists (`1,400+` cues) on slow USB and
  exFAT drives by caching resolved media paths per show after their first
  lookup.
- Changed: the missing-media scan runs in the background at startup and when
  opening a show, allowing the splash to appear immediately. The
  `N media missing (RELINK)` toast appears when scanning finishes.

### Patterns

- Changed: Terrarium uses more vivid colours in both the standalone executable
  and in-app pattern.
- Foliage uses spring-to-forest greens with varied saturation and brightness;
  desert foliage uses sage and olive. Alien foliage retains the full hue
  range.
- Blooms favour yellow and white, followed by violet, red and orange, with
  uncommon blue and rare magenta; accent colours dominate the petals.
- Fauna uses russet, tan and chestnut, with bird- and beetle-inspired
  iridescent accents. One in eight uses a full-colour tropical or wetland
  palette.
- Large flowers use a round rosette and stem. Mushroom caps use cream, tan and
  fly-agaric red; ambient flowers use poppy, marigold, orchid and thistle
  colours.

### Audio

- New: DMG-style UI sound synthesis with two pulse channels at hardware duty
  cycles, a `15-bit` LFSR noise channel, `4-bit` quantized envelopes and
  NR51-style stereo placement.
- New: swung startup chiptune with an original YMCK-inspired jazz melody over
  the Giant Steps major-third cycle: `B△7 → D7 → G△7 → B♭7 → E♭△7`.
- The startup jingle respects `little bloops`; ordinary UI sounds wait until
  its final chord finishes.
- New sounds: refused-action buzzer for a take blocked by missing media,
  dive-and-whoosh for `PANIC` / `Esc`, and a dice-roll trill when shuffle is
  enabled. Existing UI sounds are unchanged.

### Cues

- Added live PiP inspector controls: source/type, `TL/TR/BL/BR` corner
  presets, `SM/BIG/70-30` size presets, `CLEAR OVERLAY`, `GEOMETRY`, `KEY` and
  `METADATA`.
- SRT, RTMP, RTSP, UDP and NDI source cues now expose `PLAYBACK` controls for
  fades, transition and style, and audio; `METADATA` includes editable URL or
  NDI source name, tag, notes and cue ID.
- Audio cues now expose the same `AUDIO` controls as video cues: gain trim,
  pan, mono downmix, output pair, independent audio fades and R128
  normalization.
- Fixed an issue where inspector sections ending in a status line could not be
  fully reached by scrolling.
- Fixed an issue where dropdown click areas persisted after changing the
  selected cue.
- The empty inspector displays `NO CUE SELECTED` once.

## 2026-07-15 — v0.80.0 (settings overhaul, stereo waveforms, panic audio, Test Bars)

### Settings

- Changed: every tab uses one dialog size, sized for the busiest tab.
- AOI edits `X / Y / WIDTH / HEIGHT` in output pixels, with `16 px` minus/plus
  nudges, click-to-type values and a `FULL` reset. The header shows the
  current rectangle, for example `1920x1080 @ 960,540`.
- AOI storage remains four edge fractions; show files remain compatible in
  both directions.
- Edge-blend values also use output pixels.
- Tabs size to their labels so `Video Outputs` reads in full.
- `Toggle Fullscreen` and `Orientation` share a row; the Display & Raster card
  uses its reserved space.
- Fixed an issue where About displayed a duplicate `v` before the version.

### Audio

- Changed: waveform channel layout follows the audio content, measured sample
  by sample from side-signal energy. Stereo content shows separate L/R lanes;
  mono content in a stereo container uses one full-height lane.
- Stereo detection also applies to older cues without channel-count metadata.
  Opening a show fills missing channel counts through a background media
  probe.
- Fixed an issue where audio continued after `PANIC` or triple-`Esc`.
  `Outputs off` now stops every deck engine and browser cue as well as
  disarming video.
- Fixed an issue where `STOP` failed to stop Audio cues. It now closes their
  decoder and audio stream as it does for video.

### Patterns

- New: `Test Bars (motion diagnostics)`, a testsrc2-style pattern with six
  saturated bars, bouncing rainbow diagonal, dissolving checker patch, sliding
  grey reference block, clock and frame counter.
- URI: `pattern://test-bars`; aliases: `testsrc`, `testsrc2`.

### Desk

- The boot console selects `8` random lines from `40` sci-fi subsystem
  messages each startup, interleaved with initialization values that always
  appear.
- Fixed an issue where the playlist-header `>LIVE` label was clipped. Its
  button sizes to the label, and marquee dots stop before it.
- New: right-click video, audio or image cues and choose `show in explorer` to
  reveal the media file. Uses Explorer `/select` on Windows, Finder reveal on
  macOS and the containing folder on Linux.

### Command line

- New: `--import <file>` imports media at launch and skips the startup menu
  and splash.
- New: `--settings [tab[.subtab]]` opens Settings at startup. Both flags
  support scripted tests and screenshots.

## 2026-07-12 — v0.79.13 (mascot tip: the trim keys)

### Interface

- Changed: the mascot's trim tip explicitly names `Ctrl+I` for the in point
  and `Ctrl+O` for the out point. Bare `I` imports media.

## 2026-07-12 — v0.79.12 (telecined and variable-rate video stay in sync)

### Playback

- Fixed an issue where audio and video drifted apart when playing telecined
  `3:2` pulldown or variable-frame-rate media, including DVD MPEG-2 and some
  phone clips.
- Frames now use their presentation timestamps, so `23.976 fps` film with soft
  pulldown in a `29.97 fps` container follows the correct timeline position
  and audio clock.
- Constant-frame-rate indexing is unchanged. Streams without timestamps retain
  sequential frame counting.

## 2026-07-12 — v0.79.11 (cue navigation: jump to current + auto-follow)

### Cues

- New: `J` and the playlist-header `>LIVE` button centre the live cue in the
  playlist, or the selected cue if nothing is live.
- New: auto-follow reveals the focused deck's live cue after take,
  auto-advance or shuffle, but scrolls only when that cue is off-screen.

## 2026-07-12 — v0.79.10 (10-bit H.265, and a clean slate on open)

### Playback

- Fixed an issue where `10-bit` H.265/HEVC Main 10 video rendered incorrectly
  when hardware decoding was used.
- True NV12 surfaces retain zero-copy playback. P010 and other surface formats
  transfer through the CPU and convert to NV12 or RGBA. `8-bit` content
  retains the fast zero-copy path.
- `--decode-bench` reports per-frame GPU and CPU counts.

### Desk

- Changed: opening a show starts with nothing live, matching a fresh launch.
  Timeline and preview agree, the startup mascot appears, and the operator
  takes the first cue explicitly.
- The selected cue is preserved for preparation.

## 2026-07-12 — v0.79.9 (mascot tips: bigger, plainer, clearer)

### Interface

- Changed: mascot tips use larger plain text across the full width below the
  face, replacing the speech box with short, jargon-free lines.

## 2026-07-12 — v0.79.8 (mascot: per-element Balatro-style drift)

### Interface

- Added a subtle oscillating tilt around the startup face's centre, on top of
  its overall hover.
- Each eye and the mouth drift and breathe at separate phases, producing
  slightly offset motion inspired by Balatro's card animations.

## 2026-07-12 — v0.79.7 (startup mascot in the empty program monitor)

### Program monitor

- New: a hovering startup face and rotating operator tips occupy the empty
  program monitor until the first clip loads. The face then remains hidden for
  the rest of the session.
- The BMO-style face uses glowing, theme-tinted eyes and mouth directly on a
  darkened screen, including on OLED themes.
- Continuous animation includes two-dimensional hover, eased squish-blinks,
  slow gaze drift, a breathing smile and twinkling orbiting stars, without
  abrupt state changes.
- Tips cover import, the timeline grip, themes, relink, per-cue audio and
  shortcuts.

## 2026-07-12 — v0.79.6 (denser timeline filmstrip; consistent thumbnail aspect)

### Timeline

- Changed: filmstrips show `9` stills per clip, up from `5`, for denser
  coverage in enlarged timeline lanes.
- Fixed an issue where thumbnails distorted when the lane size changed. Each
  tile now uses an aspect-preserving centre crop within its own column.

## 2026-07-12 — v0.79.5 (settings on terminal themes)

### Interface

- Fixed an issue where Settings content was difficult to read on terminal
  themes.
- The content frame uses the theme's tile fill, dark on OLED themes and
  unchanged on light themes. The `Mappings` label uses `screen_fg`.
- Card titles retain bright text on dark title plates; hints use
  `screen_ink_soft`; buttons retain dark text on bright or mid-tone fills.

## 2026-07-12 — v0.79.4 (resizable timeline; dialog readability; sharper thumbnails)

### Interface

- New: a draggable grip between the program monitor and timeline transfers
  height between preview and timeline lanes. The monitor retains a usable
  minimum height; the grip appears only when resizing is possible.
- The split is session-only, matching the existing pane splitters.
- Fixed an issue where startup, the `New show / Open previous / Open saved`
  menu, keyboard shortcuts and the Settings title were difficult to read on
  terminal themes.
- These labels now use `screen_fg`, `screen_fg_soft` and `screen_ink_soft`;
  the boot console uses the tile fill. OLED themes show bright text on dark
  panels; light themes retain their appearance.

### Timeline

- Doubled filmstrip thumbnail resolution to `256x144` and added linear
  filtering for enlarged lanes.

## 2026-07-12 — v0.79.3 (terminal/OLED themes; manual rewrite)

### Themes

- New roles: `screen_fg` for primary body text, `screen_fg_soft` for secondary
  tile text and `screen_tile` for interactive tile fill. They support
  true-black backgrounds with bright text and theme accents.
- Fallbacks are `screen_deep`, `screen_dark` and `screen_light`, respectively.
  Themes without the new keys remain byte-identical.
- Reworked all `25` dark-cased themes as OLED terminals with matching accents:
  green `dark`; red `virtual-boy`, `famicom`, `mario`; amber `metroid`; teal
  `n64`, `wave-race`; violet `gamecube`, `super-famicom`; blue `star-fox`,
  `switch-neon`, `captain-falcon`; magenta `ganon`; pink `kirby`, `peach`;
  gold `zelda`; yellow `pikachu`; lime `piranha-plant`; cyan `dolphin`; ice
  `ice-climber`; white `r-o-b`; and grey-green `game-and-watch`, among others.
- Light-cased `gameboy`, `pocket`, `sp`, `advance` and `color` themes are
  unchanged.
- Inspector labels, playlist, timeline, header and footer text, cue rows,
  toolbar and quick-row buttons, and section headers use the new roles.
- The inspector body uses `shell_inner` to keep labels readable.
- The contrast audit covers the new roles; all themes passed.

### Documentation

- Rewrote the manual for SDL3, Windows-first operation and in-process decode.
- Covers cue types, inspector, outputs and geometry, per-cue and multichannel
  audio, missing-media relink, timecode, themes, remote control, soak testing
  and the full keyboard reference.

## 2026-07-11 — v0.79.2 (soak harness; shuffle seeding; inspector readability)

### Diagnostics

- New: `--soak [minutes]` loops the loaded show, or generated patterns when no
  show is loaded, through the normal app loop and then quits.
- Logs RSS, decode-stall and missing-media counters once per minute to stdout
  and `deckboy-soak.log`. Does not save the test's looped state into the show.
- Default duration is `24 h`, intended for long runs on show hardware.
  Example: `Deckboy.exe --soak 720` runs for `12 h`.

### Cues

- Changed: shuffle uses a freshly seeded uniform random distribution for a
  different order each launch.

### Interface

- Fixed an issue where cue-inspector text was difficult to read on some
  themes. Its body now uses the contrast-audited `shell_inner` fill; theme
  colours and case designs are unchanged.

## 2026-07-11 — v0.79.1 (mid-show media loss hardening)

### Cues

- Fixed an issue where taking a cue could interrupt output when its media
  disappeared mid-show, such as after unplugging a drive or losing a network
  share. The take is refused with `MEDIA MISSING`; the existing output stays
  visible.
- Auto-advance skips missing cues with a toast for each skip. The search is
  bounded so an entirely missing, looped playlist cannot spin indefinitely.
- Each take checks the disk and updates `MISSING` badges and the toolbar
  `RELINK` count. Restoring a drive clears the warning on the next take
  without reopening the show.
- A decoder stall caused by a missing file reports
  `MEDIA LOST (RELINK when restored)`.

## 2026-07-11 — v0.79.0 (multichannel audio output routing)

### Audio

- New: `Settings > AUDIO OUTPUT > Outs` selects `2/4/6/8` device channels per
  deck and reopens that deck's audio device.
- With more than two channels open, the cue inspector's `AUDIO > outs` row
  routes post-gain, pan and mono stereo audio to `1-2`, `3-4`, `5-6` or `7-8`.
- Gain, fades, delay and VU metering remain stereo; unused device outputs
  receive silence. A/V timing, backpressure and sync-pop pacing account for
  the selected channel count.
- SDL folds additional pairs down when the device has fewer physical outputs
  than requested, allowing laptop preparation before venue routing.
- Remote: `AUDIOOUTS <pair>` sets the selected cue's output pair, numbered
  from `1`.
- Per-cue routing and per-deck channel counts persist in the show; save/load
  round trips are covered by smoke tests.

## 2026-07-11 — v0.78.16 (missing media: detection + relink)

### Cues

- New: opening a show checks all file-backed cues. Missing files receive a red
  `MISSING` badge and a toolbar `RELINK n` button beside `BUNDLE`.
- `RELINK` opens a folder picker and recursively searches for matching
  filenames. An exact file-size match takes precedence when several files
  share a name.
- Relinking updates cue paths and reports `relinked X, Y still missing`.
- Clicking `RELINK` first checks the disk again, clearing warnings for a
  restored drive without requiring folder selection.
- Smoke tests cover the missing-media scan and relink round trip.

## 2026-07-11 — v0.78.15 (audio fade envelopes on every waveform)

### Audio

- Added audio fade envelopes to the timeline audio lane, program-monitor
  strip, cue-panel thumbnails and cue-panel strips.
- Envelopes use the cue's explicit audio fades when set, otherwise its visual
  fades, anchored to the in/out points to match playback.

## 2026-07-11 — v0.78.14 (AUDIO section; deck fader named; independent audio fades)

### Audio

- New: collapsible `AUDIO` inspector section in both layouts, containing
  enable, gain, pan, mono, audio fades and normalization, alongside the
  existing `GEOMETRY` and `OVERLAYS` sections.
- Changed: `volume` is named `deck fader`. It controls the deck's live
  playback level with `+` / `-` and is not saved per cue; per-cue gain trim
  remains in `AUDIO`.
- New: per-cue `a-fade in` and `a-fade out`, set to follow the visual fade by
  default, none, or an explicit duration in seconds.
- Audio fades can duck sound before a long video tail or retain it through a
  fast visual cut. Settings persist, retain backward compatibility and have
  smoke-test save/load coverage.

## 2026-07-11 — v0.78.13 (readability lives in the themes)

### Themes

- Changed: readability is defined by theme colours; removed the renderer-side
  contrast override introduced in v0.78.12.
- Adjusted `shell_inner` in `26` affected themes to a light tint of each
  theme's own hue, meeting the required ink contrast ratios while retaining
  console styling.
- Reworked `dark` with graphite panels, light rows and near-black text.
- Adjusted `gamecube` selected-row purple and two `screen_light` tones.
- Added a theme contrast audit for the interface's actual ink/fill pairs and
  minimum ratios, to run after theme edits.

### Interface

- Retained the headline-sized startup wordmark from v0.78.12 and the smaller
  version tag on the subtitle line.

## 2026-07-10 — v0.78.12 (theme readability guard; startup headline)

### Themes

- Added automatic WCAG-style contrast adjustment for the interface's ink/fill
  pairs: `4.5:1` for light text on deep fills, `3:1` for dark text on light
  fills, with proportional thresholds for accents and secondary text.
- Only the lightness of a failing tone changes; hue is preserved and compliant
  themes remain unchanged.

### Interface

- Changed: the startup `Deckboy` headline uses a `42 pt` pixel font that
  follows UI scale.

## 2026-07-10 — v0.78.11 (A/V delay offset)

### Audio

- New: `Settings > Audio > A/V delay`, from `0–1000 ms` in `10 ms` steps,
  delays all deck audio to align the display and PA chain.
- Changes apply live without restarting playback. The A/V master clock remains
  on the undelayed timeline; VU meters follow the delayed audio heard at the
  device.
- Pocket Test's sync pop uses the same delay. Adjust it until the beacon flash
  and pop coincide at the end of the chain.
- Saved with the show as `audio_delay_ms`.

## 2026-07-10 — v0.78.10 (per-cue loudness normalize)

### Audio

- New: per-cue loudness normalization through the audio-section button or
  remote `AUDIONORM`.
- Measures the file's EBU R128 integrated loudness in the background and sets
  gain trim for a `-16 LUFS` target. The resulting trim remains manually
  adjustable.
- A completion toast reports measured loudness and applied trim.

## 2026-07-10 — v0.78.9 (per-cue audio: gain, pan, mono)

### Audio

- New: audio controls for every cue with audio: gain trim from `-24` to
  `+12 dB`, stereo-balance pan with centre snap, and mono downmix.
- Scrubbable inspector rows sit below the audio toggle. Changes apply live
  without restarting decode; VU meters and Pocket Test's sync pop follow them.
- New remote commands: `AUDIOGAIN <dB>`, `AUDIOPAN <-1..1>`,
  `AUDIOMONO <0|1>`, with the same behaviour as the inspector.
- Settings persist per cue and remain compatible with older shows. `--smoke`
  verifies the save/load round trip.

## 2026-07-10 — v0.78.8 (Pocket Test: the ball bounces; patterns run at display rate)

### Patterns

- Changed: pattern updates follow the selected display's refresh rate, or the
  project's explicit output refresh when set. Animation advances once per
  display frame.
- Pocket Test caches its static grid, bars, grayscale, ramp, PLUGE/detail
  patches, border and crosshair per raster. Its island scene renders at
  `640x360` with nearest-neighbour sampling; dynamic elements update each
  frame.
- Changed: the centre circle is a ball moving slowly across the full frame at
  constant velocity, bouncing like a DVD logo.
- The ball reveals the underlying island scene, including sky, beach and
  characters. It serves as a smooth-motion/judder reference and burn-in rover.
- The A/V sync beacon stays at the ball's twelve o'clock position; the
  alignment crosshair remains at frame centre.

## 2026-07-09 — v0.78.7 (Pocket Test: PM5544 edition; pattern policy pass)

### Patterns

- Rebuilt Pocket Test with a full-frame crosshatch grid, `75%` colour bars and
  grayscale staircase at the top, and ramp, PLUGE, fine-detail and shimmer
  patches at the bottom.
- Added a slow diagonal grid sweep and Emerald-style ID box showing version,
  raster, clock and scene.
- The island scene occupies the centre circle, in Test Card F style, retaining
  crossfaded day, sunset, night and storm scenes. The beacon flashes at twelve
  o'clock during the `80 ms` sync-pop window; the crosshair stays at frame
  centre.
- Changed: pattern motion is slow, smooth and diagonal. Crosshatch moves one
  cell per `8 s` at `45°`; checkerboard moves one period per `10 s`; SMPTE
  bars use a `12 s` diagonal sweep instead of scan lines.
- Changed: solid-colour patterns remain static, including when loading older
  shows that requested motion.
- Terrarium is omitted from pattern pickers until discovered; saved Terrarium
  cues still load.

## 2026-07-09 — v0.78.6 (Pocket Test: audible for real, strobe defused)

### Patterns

- Fixed an issue where existing Pocket Test cues remained silent after
  loading. Older cues are made audible once; new cues expose the audio toggle
  and start with audio enabled. Subsequent per-cue muting persists.
- Changed: the `?` block uses a single-pixel checkerboard with phase inversion
  at `30 Hz`, replacing the full-square `30 Hz` flash to reduce the
  photosensitivity hazard. Dropped or doubled frames remain visible as
  freezing or beating.

### Diagnostics

- New: `--sync-pop-test` exercises the actual sync-pop playback path against
  the default audio device and reports `PASS` or `FAIL`.
- Verified the synthesized sync-pop path end to end on a real device: `PASS`.

## 2026-07-09 — v0.78.5 (Pocket Test field notes: audio, scaling, Emerald)

### Patterns

- Pocket Test defaults to audio on. Its sync pop follows the visible buoy lamp
  even when transport state changes; `STOP` with dark output silences it.
- Changed: scenes crossfade over `1.4 s` instead of cutting between palettes.
  Card borders and labels remain fully visible above the blend.
- All patterns render pixel-for-pixel at the live program-output raster,
  checked on every rebuild. Switching displays retargets them automatically.
- Added a skiff moving across the ocean at constant speed, `4 s` per screen,
  over the existing `10%` fence ticks for judder checks.
- Restyled Pocket Test after GBA Pokémon Emerald: white windows, dark-grey
  outlines and text, teal bevels, light text shadows, red names and continue
  cursor.
- The battle-style status panel's HP bar drains green to yellow to red across
  each `14 s` scene. Its EXP bar fills each second in step with the sync pop.

## 2026-07-09 — v0.78.4 (Terrarium goes native)

### Patterns

- New: `pattern://terrarium` runs the full ecosystem simulation inside Deckboy
  without a companion process or window capture.
- Runs at `9 ticks/s`, rendering a `200x112` glyph world into `1600x896` with
  the standalone app's `8x8` font, palettes, seasons, weather, clouds and
  creatures.
- Each show shares one world across decks and previews, retaining simulation
  state across cue reloads. New shows start with an established ecosystem.
- `Terrarium (living ecosystem)` appears in the pattern picker; the purple
  Terrarium cue uses the native pattern.
- The standalone `terrarium.exe` remains available and shares the simulation.
- `--smoke` checks that the native pattern renders a living world.

## 2026-07-09 — v0.78.3 (Pocket Test A/V sync pop)

### Patterns

- New: Pocket Test A/V sync signal, the first pattern-cue audio. A bobbing
  buoy flashes its lamp for `80 ms` at the start of each second, accompanied
  by a `1 kHz` pop in the same window.
- Compare the flash and pop at the end of the playback chain to measure its
  A/V offset.
- The pop follows deck volume, master gain and cue fades, drives the VU
  meters, and can be muted with the cue's audio toggle.

## 2026-07-09 — v0.78.2 (Pocket Test goes diegetic)

### Patterns

- Changed: Pocket Test embeds its measurement instruments in the island scene
  without changing their values.
- Colour bars appear on a beach billboard; the grayscale staircase becomes
  stone steps at the right edge; the banding ramp is a sky banner.
- Black-crush checks use cave eyes at `2%` and `4%` on black. White-clipping
  checks use cloud patches at `98%` and `96%`.
- The flicker reference is a `?` block at approximately `30 Hz`; a beach TV
  shows single-pixel checkerboard and stripe detail.
- A runner crosses at constant speed past fence posts spaced at exactly `10%`
  of the frame width.
- The Pokémon-style dialog retains version, raster, clock, scene messages such
  as `A WILD NIGHT APPEARED!`, blinking continue cursor and chunky Game Boy
  borders.
- The scene and rainbow footer remain fully visible; border, safe-area and
  crosshair guides are unchanged.
- Smoke tests verify `75%` red, `2%` cave eyes and the `96%` cloud patch.

## 2026-07-09 — v0.78.1 (Pocket Test is a real test card)

### Patterns

- Added broadcast test instruments over the island backdrop of auto-cycling
  `pocket-test`. The `pocket-day`, `pocket-sunset`, `pocket-night` and
  `pocket-storm` variants remain plain backgrounds.
- Added a single-pixel checkerboard border and corner marks for pixel mapping,
  crop and overscan checks; scaling turns the border grey.
- Added dashed `90%` and `80%` safe-area guides and a centre crosshair.
- Instrument strip: `75%` colour bars, `11-step` grayscale, continuous banding
  ramp, PLUGE-style `0/2/4%` black patches and `100/98/96%` white patches.
- Fine-detail and interlace checks use single-pixel checkerboard and stripe
  patches. A roughly `30 Hz` flicker box tests dropped/doubled frames; a
  full-width constant-speed motion lane with `10%` ticks tests judder.
- A built-in `3x5` pixel-font plate shows build version, actual raster,
  running clock and scene name.
- New: `--pattern-dump <pattern-id> <out.ppm> [WxH] [t]` renders any pattern
  frame to PPM for inspection or documentation.
- `--smoke` checks that instruments appear on the test card and remain absent
  from the scene variants.

## 2026-07-09 — v0.78.0 (in-process GPU decode — zero-copy video)

### Playback

- Changed: file-backed Video and Audio cues decode in-process through FFmpeg
  libraries instead of two FFmpeg subprocess pipes per deck.
- Windows D3D11VA decodes NV12 video on the program-output GPU device and
  copies decoded textures directly into the output texture. Frames avoid CPU
  download, colour conversion, approximately `41–83 MB/s` of pipe transfer and
  GPU re-upload, reducing transport cost on devices such as the fanless Pocket
  3.
- Live SRT/NDI streams, source capture, stills, thumbnails, waveform analysis,
  media probing and stream encoding retain the FFmpeg command-line path.
- Files with rotation metadata retain command-line decoding for automatic
  rotation.
- Chroma-key and colour-control cues, and codecs without hardware decoding,
  decode in-process to CPU frames. An in-process open failure automatically
  falls back to command-line decoding.
- `--no-inproc-decode` forces command-line decoding for the session. Build
  with `-DDECKBOY_INPROC_DECODE=OFF` for a command-line-only binary.
- The first frame is decoded before accepting a file. Runs of corrupt packets
  end playback as EOF; a stalled decoder reracks the deck dark and reports a
  toast. `--smoke` includes a corrupt-file test.
- Frame buffering, backpressure, the audio-master clock, seeking, EOF and
  looping retain their behaviour. Audio remains `16-bit / 48 kHz` stereo, with
  the same tempo-preserving speed control.
- Decode thread counts are capped to leave render capacity on four-thread
  CPUs. Both decode paths skip unnecessary scaling; textures upload only when
  the frame changes.
- Windows enables thread-safe D3D11 device access so rendering and decode can
  share the GPU device.

### Diagnostics

- New: `--decode-bench <file> [seconds] [cli]` measures throughput through the
  actual playback engine for before/after comparisons.

### Packaging

- The portable zip includes FFmpeg library DLLs and `LICENSE-ffmpeg.txt`.

## 2026-07-08 — v0.77.0 (SDL3 migration)

### Build

- Migrated the app and SDL_ttf from SDL `2.32` to `3.4`, providing D3D11
  texture import for future in-process zero-copy video decoding.
- Retained integer-based layout and existing display-index behaviour through
  the migration.
- Migrated the Terrarium companion executable; the zip ships one SDL3 runtime.
- Verified a clean build, successful `--self-check`, `--smoke` with zero
  failures, and live control/output checks using an HDMI dongle with video,
  audio and VU meters active.

### Outputs

- Changed: vsync is set per renderer. Program outputs synchronize to their
  display; stream-only outputs and offscreen deck decoding run without vsync
  throttling.

### Windows

- SDL3 now handles mixed-DPI displays natively, replacing the previous
  per-monitor DPI hint.

### Audio

- Migrated to SDL3 audio streams. Each deck retains its own logical device on
  the selected output; UI sounds use a separate device on the default output.
- LTC input uses a recording stream resampled to `48 kHz`, mono, signed
  `16-bit` audio.

## 2026-07-05 — v0.76.31 (Media Encoder, splash system, multi-select, audio hot-swap)

### Media Encoder

- New: built-in media converter and `Settings > Encoder` tab.
- Import flags media that cannot play or may play poorly, including `10-bit`
  HEVC, AV1, ProRes and material above `1080p`. A contextual `CONVERT` button
  appears in the inspector.
- Batch conversion creates H.264 MP4 copies using the GPU with a libx264
  fallback, stores them in `_converted/` beside the show and repoints the
  cues. Original files are preserved.

### Interface

- Grayscale scene splashes cycle at startup and take the active theme's tint.
  The default `gameboy` theme uses the branded `DECKBOY` wordmark splash.
- Changed: the main cue list uses a bottom-only rubber-band scroll limit that
  returns when the wheel stops.

### Themes

- Renamed sci-fi colourways after Nintendo characters, including Luigi, Kirby,
  Mario, Pikachu, Star Fox, Game & Watch, Peach, Ganon, R.O.B., Waluigi and
  Midna.
- Generated themes use near-black `#050505` backgrounds.
- The theme is saved per show; a new show resets to the default skin.

### Cues

- New: `Ctrl+A` selects all cues. Hold, loop, fade and audio row toggles apply
  to the whole selection.
- New: `SELECTED CUE > RESET` restores inspector settings to deck defaults
  across the selection, preserving media, names and metadata.

### Audio

- Changed: switching audio devices replaces the device while playback
  continues.

### Outputs

- Fixed an issue where the fullscreen button required more than one click to
  re-arm after a new show or relaunch.

## 2026-07-04 — v0.76.30 (Save/Save As, theme library, output black-on-disable)

### Desk

- Changed: toolbar `SAVE` and `Ctrl+S` prompt for a file location and save the
  show only. `BUNDLE` still exports media; `Ctrl+Shift+S` remains explicit
  Save As.
- Dropping a folder recursively imports supported video, image and audio files
  in name order. Audio files become Audio cues.
- Cue-list scrolling stops at the final cue in both the deck list and overlay
  bin.

### Themes

- Added `24` themes: Tritium, Cerenkov, Ion, Amber CRT, Plasma, Halon, Nebula,
  Infrared, Hazard, Cryo, Toxic, Cobalt, Ultraviolet, Quasar, Voidsteel,
  Crimson Protocol, Virtual Boy, Famicom, Super Famicom, N64, GameCube, Switch
  Neon, Hyrule and Metroid.
- The Appearance dropdown discovers `data/themes/<name>/theme.txt`
  automatically.
- The chosen theme is saved with the show and restored after restart.
  `DECKBOY_THEME` still overrides it at startup; older shows without a theme
  retain the current selection.

### Outputs

- Changed: disabling an output, creating a show, switching displays or
  quitting sends a black frame, preventing displays and capture devices from
  retaining the previous session's final picture.

### Interface

- Inspector value editors select the existing value on entry: click, type,
  then `Enter` to replace it.
- The transport button shows pause while playing and play while paused.
- Added click-to-seek on the audio timeline, matching the video lane.

## 2026-07-03 — v0.76.29 (Extended boot sequence)

### Interface

- Expanded the splash console to approximately `24` scrolling lines.
- Displays theme, fonts, raster, display count, audio buffer, decks, cues,
  Companion port, NDI state and wall-clock/audio-crystal synchronization
  values.
- Interleaves fictional subsystem messages for a `1.21 GW` flux capacitor,
  Heisenberg compensators, dilithium matrix, gremlin containment and spline
  reticulation.
- Order and timing vary each boot, including quick bursts and occasional
  half-second pauses, while finishing within the approximately `5 s` splash.
- `Enter`, `Esc` or a click skips immediately.

## 2026-07-03 — v0.76.28 (Terrarium wired in, richer boot console)

### Patterns

- Added Terrarium `v0.46` as a bundled companion executable.
- Its launch sequence opens a window and adds a `TERRARIUM (secret)` Window
  Source cue to the playlist. `TAKE` sends the ecosystem to program.

### Interface

- Expanded the line-by-line boot console with version, raster, display count,
  shelved decks/cues, active Companion port and NDI runtime state.
- Retained startup tasks and added fictional checks such as rubber-chicken
  calibration.

### Packaging

- Packages include `terrarium.exe`.

## 2026-07-03 — v0.76.27 (Window picker, boot log, layer fader)

### Sources

- New: adding a Window Source cue opens a picker listing all visible window
  titles and `Entire Desktop`.

### Interface

- Added a retro BIOS-style startup log, revealing raster, displays, decks,
  cues, Companion port and NDI runtime line by line, interleaved with
  fictional diagnostics such as `RUBBER CHICKEN ... CALIBRATED`.

### Outputs

- Changed: the playlist-footer opacity control is labelled `LAYER n%` in dark
  text. It controls the deck's compositing weight when several decks share an
  output.
- The fader is hidden for single-deck shows and appears when a second deck
  exists.

### Known issues

- Window capture matches titles exactly through gdigrab. Re-select windows
  after title changes, such as when a browser changes tabs.

## 2026-07-03 — v0.76.26 (transport verbs, cameras, full-speed in background)

### Windows

- Changed: Deckboy opts out of Windows 11 EcoQoS and timer-resolution
  coalescing at startup. Decode and audio timing retain their `4 ms` cadence
  while another app has focus.

### Transport

- `PAUSE` freezes playback in place and remains resumable.
- `RERACK` returns to the start and holds the first frame.
- `STOP` darkens the deck, reracks it and releases decoders and capture
  devices, including turning off a stopped webcam's light. `PLAY` after `STOP`
  reopens playback.

### Sources

- New: DirectShow camera input on Windows, including webcams, HDMI capture
  sticks and Blackmagic WDM devices.
- Adding a Camera/Capture cue lists devices in a picker, or selects
  automatically when exactly one is available. `TAKE` starts capture; `STOP`
  releases the device.

### Audio

- Empty waveform-analysis results are cached to prevent repeated analysis.
- Live sources show `live audio`; cues without audio show `no audio track`.
  Waveform analysis runs only for file-backed video and audio cues.

### Patterns

- Changed: the picker lists base patterns only; the motion toggle controls
  animation.
- Combined four Pocket scene variants into the cycling Pocket Test. Older
  pattern IDs still load.
- Crosshatch grids remain aligned to the centre crosshair at every raster.
- Added the input sequence `↑ ↑ ↓ ↓ ← → ← → B A Start`.

### Interface

- The deck opacity strip below the playlist is labelled `OPACITY n%`.

### Diagnostics

- Added automated checks for duplicate action IDs, unhandled quick actions,
  buttons without handlers and configuration fields written but never used by
  the app. All checks were clean for this release.

### Known issues

- DirectShow devices allow exclusive access only. Simultaneous live preview
  and program capture of the same device require shared capture, which is not
  yet implemented.

## 2026-07-03 — v0.76.25 (the master volume fader drives the audio)

### Audio

- Fixed an issue where the header master-volume fader did not change playback
  levels. Master gain now applies across all decks in addition to cue volume,
  whether changed through the fader, `MASTERVOL`, show loading or undo.
- The master fader supports dragging as well as clicking.
- Fixed an issue where saved boost levels did not restore at their saved
  value. The supported range is `0–2`.

### Interface

- Added clean cityscape splash art for both mascots, without an embedded
  dialog.

## 2026-07-03 — v0.76.24 (settings readability pass)

### Settings

- Fixed an issue where settings controls invoked another control's action or
  failed to respond. Video Outputs sub-tabs, Allow Remote, Stream Key,
  Processing, Mascot, UI Scale, Pocket 3 and Identify now have distinct
  actions.
- Added a pixel-font `SETTINGS` title.
- Main tabs and Video Outputs sub-tabs use cartridge-shelf styling: active
  tabs sit at full height against the content frame; inactive tabs are
  recessed.
- Cards and sections use consistent dark label plates with light text. The
  visual redesign preserves control positions and behaviour.

### Interface

- Added cityscape splash art for both mascot choices, showing Deckbot and
  Deckgirl rigging a dot-matrix wall above the skyline.

## 2026-07-03 — v0.76.23 (value scrubbing, math shorthand)

### Cues

- New: drag inspector value cells horizontally to scrub width, height,
  offsets, rotation, crop, fades, volume, speed and other quick rows with
  minus/plus controls.
- A click still opens exact entry. Width and height scrubbing respects the
  aspect-ratio link.

### Interface

- Numeric entry accepts `x` as multiplication and ignores `px` units:
  `1920x2`, `960px * 2` and `3840/2` are valid expressions.
- Applies to every numeric field; four new smoke checks cover the shorthand.

## 2026-07-03 — v0.76.22 (fades off by default, pixel commands)

### Cues

- Changed: new imports default to fades off. Cue-row fade icons and inspector
  rows toggle between `0` and the deck's default fade duration.
- Existing shows retain saved deck settings.
  `Settings > System > Show Flow > FADE IN / FADE OUT` sets defaults for
  future imports into an existing deck.
- Added three end-to-end smoke checks for fade-in and fade-out ramps, using
  the actual playback clock and visual gain applied to output opacity.

### Remote protocol

- New: `WIDTH <px>` and `HEIGHT <px>` size cues in pixels from Companion or
  OSC and respect the inspector's aspect-ratio link.
- Existing `SCALE`, `SCALEX` and `SCALEY` factor commands are unchanged.

## 2026-07-03 — v0.76.21 (pixel-based geometry editing with aspect link)

### Cues

- Changed: `GEOMETRY > width / height` shows and edits rendered output size in
  pixels. Clicking a value opens an exact pixel prompt, such as `Width (px)`.
- Show storage retains per-cue scale factors derived from each cue's base
  rendered size.
- New: `link aspect`, enabled by default and saved with the show. Editing or
  nudging width scales height proportionally, and vice versa; disable it for
  independent dimensions.
- Multi-selection applies the requested pixel dimension to each cue, allowing
  cues with different source sizes to share a width such as `960 px`.

## 2026-07-02 — v0.76.20 (fullscreen that stays put, display identify)

### Outputs

- Fixed an issue where fullscreen output minimized when the control window
  received focus.
- Changed: display-native outputs use borderless fullscreen by default.
  Exclusive fullscreen is reserved for explicitly selected fixed rasters or
  refresh rates, avoiding unnecessary display-mode switches and their blanking
  or mixed-DPI placement effects.
- Recovery logs identify what triggered an output move.

## 2026-07-02 — v0.76.19 (engine and display robustness)

### Playback

- Fixed an issue where importing media or deleting other cues could disturb
  the live cue. Playback retains a snapshot of the loaded cue; edits to that
  cue, including fades, still apply immediately.
- Audio fade and gain changes are synchronized with audio decoding to avoid
  concurrent-access faults.
- Changed: audio is the A/V master clock. Video re-anchors when drift exceeds
  approximately two frames, maintaining synchronization on long clips and
  variable-frame-rate sources.

### Outputs

- Changed: display assignments persist by name, allowing outputs to return to
  the intended monitor after hot-plugging, reboot or display-number changes.
- Connecting or disconnecting a display preserves outputs left windowed with
  `Esc`; affected outputs recover individually.

### Remote protocol

- Fixed an issue where HyperDeck transport or clip replies could misinterpret
  a cue named `playing`. Replies use a consistent transport and clip snapshot.

### Interface

- Empty timeline lanes no longer draw grid lines beneath
  `take or select a cue...`. Time graduations use theme ink and appear only
  over an actual timeline.
- Inspector rows share one label-column width, aligning their value boxes.
- Fixed an issue where descenders were clipped in the `LOOP | ORDER` footer
  and empty inspector at scaled or HiDPI font sizes. Text heights now follow
  the active font.
- Cue-row icons use filled fade wedges, thicker pause bars and a solid speaker
  horn for readability at `20 px`.

## 2026-06-20 — v0.76.18 (new app/taskbar icon)

### Windows

- Added a pixel-art handheld-cartridge app icon with a green play triangle.
- Embedded the multi-resolution `16–256 px` icon in `Deckboy.exe` and applied
  it to live windows, Explorer, `Alt+Tab` and pinned taskbar entries.
- Build-time icon generation survives a fresh CMake configuration; the icon
  can be regenerated from its master image.

## 2026-06-20 — v0.76.17 (stills hold, high-refresh loop)

### Playback

- Fixed an issue where Image, Pattern, Browser or Composite cues faded away
  when set to pause on their final frame. They now hold at full opacity after
  their duration.
- Pausing mid-cue preserves the current fade state.
- Changed: still-type cues default to no fade-out. Explicit per-cue fade-outs
  remain supported.

### Outputs

- Raised the main loop's timing floor to `240 Hz`, allowing stills and
  transitions to follow native `144/165/240 Hz` display rates through vsync.

## 2026-06-17 — v0.76.16 (resizable control window, F11 fullscreen, mascot dropdown)

### Interface

- New: resizable control window with a minimum size of `1500x900`. Layout
  adapts while resizing or maximizing.
- New: `F11` toggles control-window fullscreen and reports `fullscreen` or
  `windowed`.
- Changed: mascot selection uses a dropdown, matching Theme and UI Scale.

## 2026-06-17 — v0.76.15 (layout chrome scales, dependency prompts, touch mode)

### Interface

- Changed: header and button heights, panel padding, gaps and spacing scale
  with the UI setting, allowing panels and the bottom bar to grow with their
  fonts.
- New: mouse and touch interaction modes, with mouse as the default. The
  Pocket 3 preset selects touch mode alongside `2.0x` scale.
- Touch mode suppresses splitter and context-menu hover highlights to avoid
  highlights remaining after a drag or tap.

### Dependencies

- Missing-runtime prompts now appear for every operator-initiated path:
  interface toggles, hotkeys and OSC/Companion `NDI` and `DECKLINK` commands.
- Loading a show remains silent when its configured runtimes are unavailable.

## 2026-06-17 — v0.76.14 (mascot swap, dependency prompts, UI scale, portable zip)

### Interface

- New: `Settings > APPEARANCE > MASCOT` switches between Deckbot and Deckgirl
  and saves the choice per show.
- Mascot assets are loaded in `.mp4`, `.gif`, then `.png` order, allowing
  animated replacements.
- New: `Settings > APPEARANCE > UI SCALE`, with `1.00x` default, `1.25x`,
  `1.50x` and `2.00x`. Fonts reload at the scaled point size for sharp text on
  4K and Pocket 3 displays.
- New: `Pocket 3 / Touch` preset toggles `2.0x` scale for `8-inch, 1920x1200`
  handhelds.

### Dependencies

- Enabling a backend with a missing runtime offers a link to its official
  vendor download page. No prompts run at startup and no installers download
  automatically.
- Covers NDI Output and `N`, DeckLink Output and Blackmagic Desktop Video, and
  Browser cue creation with WebView2 on Windows.

### Packaging

- New: portable `Deckboy-<VERSION>-windows-x64.zip`, ready to unzip and run.
- Includes the executable, its vcpkg DLLs, FFmpeg, ffprobe, app-local MSVC
  runtime DLLs, `data/`, `LICENSE` and `README.txt` describing optional NDI,
  DeckLink and WebView2 installation.
- No separate Visual C++ Redistributable installation is required.

## 2026-06-16 — v0.76.13 (GPU video upload — NV12 fast path)

### Playback

- Changed: live video uses NV12 by default, reducing pipe bandwidth and CPU
  memory traffic by approximately `62%` compared with RGBA. The GPU performs
  YUV-to-RGB conversion during rendering.
- Chroma-key and colour-control cues retain RGBA decoding for CPU effects.
- Changed: live video scaling uses `fast_bilinear`, at approximately one-third
  the cost of bicubic with no visible difference on moving video at deck
  output sizes. Stills and thumbnails retain nearest-neighbour sampling.
- All six frame-upload paths recreate textures when format or size changes and
  use the corresponding upload format.

### Known issues

- Enabling chroma key or colour controls on a cue already decoding as NV12
  takes effect at the next `TAKE`.
- For effects that must be switched live, enable at least one effect parameter
  before `TAKE` so decoding starts in RGBA.

## 2026-04-24 — v0.76.12 (full networking stack on Windows)

### Windows

- Added Winsock2 support across the networking stack.
- Companion TCP/UDP control includes OSC parsing, subscriber tracking and
  feedback broadcasts.
- OSC Query serves HTTP endpoint discovery and state queries.
- ATEM tally and Art-Net DMX UDP bridges accept transport and lighting
  triggers.
- NMC UDP accepts play, stop and seek and broadcasts transport state to
  followers.
- NDI metadata can trigger cues; the TCP HyperDeck server accepts
  Blackmagic-compatible hardware controllers.

### Timecode

- Added Windows LTC input using libltc `1.3.2`, built as `ltc.dll` and
  discovered alongside the existing Linux/macOS library candidates.
- SDL audio capture provides PCM input on every platform.

### MIDI

- ALSA MIDI input remains Linux-only; other platforms use the cross-platform
  RtMidi backend.

## 2026-04-24 — v0.76.11 (DeckLink UI, NDI on Windows, Spout output, RtMidi)

### Outputs

- New: DeckLink controls in `Video Outputs`: enable, connected-device picker
  with SDI/HDMI/4K capability labels, output modes from `720p` through 4K at
  standard frame rates, and a `10-bit` toggle.
- Changing the DeckLink device or mode reinitializes output with the new
  configuration.
- Added Windows NDI output, including audio.
- Added Windows Spout2 output for receivers such as OBS, Resolume and
  TouchDesigner. Includes enable and sender-name controls, output routing,
  on-demand sender creation and saved show settings; requires the SpoutLibrary
  DLL.
- Added Windows SRT/RTMP output through FFmpeg, receiving raw video on
  `pipe:0`.

### Sources

- Added Windows desktop-region capture for Window Source cues through FFmpeg's
  gdigrab.

### MIDI

- Added cross-platform RtMidi `6.0.0` input with device enumeration, port
  opening/closing and polling.
- Handles CC, Note On, Note Off and Program Change. Note On with velocity `0`
  is treated as Note Off.

### Build

- Added NDI 6 SDK search paths for Windows and Linux.
- Spout2 is installed with `spout2:x64-windows`, detected by CMake and enabled
  only in builds with Spout support.
- RtMidi is detected from vcpkg and linked statically on Windows, requiring no
  additional DLL.

## 2026-04-11 — v0.76.10 (audio-lane loading animation)

### Timeline

- Added an audio-lane loading animation while waveform peaks are being
  computed, matching the video filmstrip's frame, dimming overlay and pulsing
  `LOADING` label.
- An animated `9-bar` EQ replaces the filmstrip cells. Bars use offset sine
  phases with squared envelopes, brighter peaks and `2 px` highlight caps.

## 2026-04-11 — v0.76.9 (settings text that fits at every font size)

### Settings

- Changed: card rows follow the active font height at `1x`, Retina sizes and
  intermediate UI scales.
- Covers `SAFETY / TIMECODE`, `SHOW FLOW`, `CUE TOOLS`,
  `PLAYLIST PREFERENCES`, `APPEARANCE`, `AUDIO OUTPUT`, `MIDI CONTROL`,
  `REMOTE CONTROL`, `OSC QUERY/FEEDBACK`, `DISCOVERY / NOTES`,
  `INTEGRATION ADAPTERS` and About's `RUNTIME` rows.
- Edge Blending, Area of Interest and the empty-deck
  `I import / B browser / P pattern` hint use the same font-based spacing.

### Interface

- Fixed an issue where text descenders were clipped inside short controls,
  including modal titles, `AOI RESET` and inspector-thumbnail labels.
- Text centres on the control's midline and expands its clipping area to fit
  the font.

## 2026-04-11 — v0.76.8 (correct probe of audio-first mp4s, duplicate cues allowed)

### Cues

- Fixed an issue where MP4 files imported as audio-only cues when their audio
  stream appeared before the video stream.
- Changed: the same media can be added to a deck more than once, allowing
  separate in/out points or repeated assets in a playlist.
- The cue list and toast identify duplicates; deduplication remains a
  media-library concern.

## 2026-04-11 — v0.76.7 (video cues fade in playlists)

### Playback

- Fixed an issue where playlist video cues ignored their fade-in and fade-out
  settings. Mixed Browser and Video playlists now apply fades consistently.
- Cue fade-in remains the visible transition on playlist advance; looping cues
  suppress the fade ramp on each repeat.

### Audio

- Fixed an issue where auto-advancing video cues stopped audio abruptly
  despite an audio fade-out. Audio now ramps to silence at cue end.

## 2026-04-10 — v0.76.6 (audio thread hardening, camera factory per platform)

### Audio

- Fixed an issue where short FFmpeg pipe reads could split an audio sample.
  Reads now process complete two-byte samples and retain an odd trailing byte
  for the next read.

### Sources

- Camera capture selects v4l2 on Linux and the matching platform scaffold
  elsewhere: `mediafoundation` on Windows, `avfoundation` on macOS, consistent
  with the capture catalogue.
- Windows smoke tests verify gdigrab window-capture support, available since
  v0.76.1.

### Linux

- Fixed an issue where stopping a child process could leave a reader blocked
  when the process had already exited. The read pipe now closes before process
  termination.

## 2026-04-07 — v0.76.5 (rapid takes, and TAKE without a pause)

### Playback

- Improved rapid video `TAKE` handling. Decoder shutdown terminates the
  process before waiting for reader threads and releasing its handle, allowing
  pipe reads to finish at EOF.
- The same shutdown order applies to image loading, thumbnails and timeline
  filmstrips.
- Removed the synchronous media probe from `TAKE`; playback reuses dimensions
  measured during import.

## 2026-04-06 — v0.76.4 (fade in/out on the output path)

### Outputs

- Fixed an issue where cue fade-in and fade-out did not reach every output.
- Browser, Video, Image, Pattern and source-capture cues now combine deck
  opacity with the cue's current visual fade. Output alpha is deck opacity
  multiplied by fade gain and `255`.

## 2026-04-04 — v0.76.3 (GPU decode, TSL/tally, SRT/NDI input, audio buffer tuning)

### Playback

- Added automatic GPU video decoding with `-hwaccel auto`: DXVA2/D3D11VA on
  Windows and NVDEC/VAAPI on Linux when available, with automatic software
  fallback.

### Tally

- New: TSL `3.1` UDP output sends program and preview tally when deck activity
  changes.
- Configure in `Settings > Network > Tally / TSL`, with target and port
  controls. Default port: `5800`. Available on every platform.

### Sources

- Video cues accept `srt://host:port` media URLs through FFmpeg built with
  `--enable-libsrt`.
- Live `srt://`, `rtmp://`, `rtsp://` and `udp://` URLs skip media probing and
  seeking.
- Added NDI input through FFmpeg's `libndi_newtek` device using
  `ndi://NDI_Source_Name`, with Windows NDI SDK library discovery.

### Audio

- Added buffer-size selection: `256/512/1024/2048` samples, default `1024`.
  Smaller buffers reduce latency; larger buffers favour stability.

## 2026-04-04 — v0.76.2 (browser cue duration, Area of Interest crop)

### Cues

- Fixed an issue where Browser cues lost their still duration, preventing
  playlist fade-out and auto-advance.

### Outputs

- New: per-output Area of Interest crop in
  `Settings > Output > Area of Interest`.
- Left, right, top and bottom edges use fractions from `0–1`, adjusted in `5%`
  steps. Includes reset and saved show settings.
- Crop is applied when drawing the composite into the output window.

## 2026-04-01 — v0.76.1 (browser cues on Windows, SOURCE menu, filmstrip)

### Windows

- Added Browser cues using the installed Edge or Chrome browser, without a
  separate Chromium download.
- Launches the browser in `--app=` mode at the requested size and captures its
  desktop region through FFmpeg gdigrab, corresponding to Linux's Xvfb capture
  workflow.

### Sources

- Moved `Browser / URL Cue` into the `SOURCE` dropdown alongside Window,
  Camera and Syphon, retaining the three-group button layout.

### Interface

- Fixed an issue where button labels were offset from their painted
  backgrounds. Labels now centre within the same snapped bounds.
- Fixed an issue where `Select or import a cue` clipped its descenders in the
  empty inspector.
- Fixed an issue where timeline grid lines extended outside the filmstrip.
  Each draw is clipped to the filmstrip bounds.

## 2026-03-31 — v0.76.0 (audio waveform, VU meter, text overflow, version flow)

### Audio

- Failed waveform analyses retry on the next run instead of retaining an empty
  cached result.
- Fixed an issue where the VU meter did not return to zero when the focused
  deck stopped.

### Interface

- Fixed an issue where labels crossed their control boundaries, including
  `LIVE`, `STREAM`, `DECODE`, `WARP` and the dB scale. Text clips to the same
  bounds as the painted panel.

### Build

- Changes to the version file take effect on the next `cmake --build` without
  reconfiguration.
- Fixed an issue where the app selected a build-output data directory instead
  of the project root. Root discovery skips known build-directory names before
  checking for data.

## 2026-03-29 (the previous show is the one you were in)

### Desk

- Fixed an issue where `Open Previous Show` did not open the last show used.
  Startup now offers the most recently opened or saved `.deckboy` path,
  recorded in `data/last_project.txt`.
- Repaired media paths in the saved default show so its clips load.

## 2026-03-29 (cue inspector text stays in its box)

### Interface

- Fixed an issue where inspector text escaped its row while scrolling. Text is
  clipped to both the control bounds and the scroll viewport.
- Increased row heights and spacing between rows, sections, labels, values and
  edit controls.

## 2026-03-29 (timeline scrubbing, and a roomier cue list)

### Timeline

- New: hold and drag the progress bar to scrub continuously until release.
  Clicking still seeks immediately, matching the existing tooltip.

### Interface

- Increased vertical space in cue rows and inspector controls.
- Cue names use the smaller font to accommodate longer names.

## 2026-03-29 (the Deckboy icon on every window)

### Windows

- Applied the embedded Deckboy icon to large and small app-window icons.
- Taskbar and title-bar icons now match the executable regardless of
  Explorer's cached icon.

## 2026-03-29 (the program monitor owns its area)

### Program monitor

- Fixed an issue where a second preview strip appeared at some layout widths.
  The program monitor fills its area, and the unused preview no longer decodes
  in the background.
- Output, decode and stream badges shrink in narrow layouts and omit counts
  when necessary to keep clear of `WARP`.

## 2026-03-29 (background analysis stays in the background)

### Diagnostics

- Media-probe and waveform-analysis failures now report a toast without
  propagating into the main loop.

### Windows

- Fixed an issue where waveform analysis produced no peaks. Analysis now
  drains FFmpeg's PCM output.

## 2026-03-29 (a seek holds the picture)

### Playback

- Fixed an issue where transport jumps, progress-bar seeks and
  `goto -20s/-30s` flashed black while waiting for the requested frame.
- Seeking holds the last visible frame until its replacement is ready. An
  explicit hard-clear seek remains available.

## 2026-03-29 (calmer output display switching)

### Outputs

- Improved display switching on multi-monitor setups by using one fullscreen
  re-entry sequence and delaying resize until the window has left fullscreen.
- Enabled window outputs rebuild on the selected display at the next update,
  preventing recovery and fullscreen transitions from competing over the move.
- Output teardown clears pending transitions before creating the replacement
  output.

## 2026-03-29 (Windows launches like a normal app)

### Windows

- Changed: `Deckboy.exe` launches as a GUI application without a blank
  terminal window.
- `--version`, `--self-check` and `--smoke` retain their existing behaviour
  through the GUI executable.

## 2026-03-29 (keyboard focus hygiene)

### Interface

- Changed: transport hotkeys respond only in the main control window.
- Output windows still accept `Esc` for fullscreen escape and emergency
  disarm.
- Fixed an issue where inline editors could miss typed input. Opening an
  editor raises the control window before text entry, including cue-token
  entry with `Ctrl+G`.

## 2026-03-28 (the app is called Deckboy)

### Packaging

- Changed: executable names are `Deckboy` and `Deckboy.exe`.
- Build artifacts use `Deckboy-linux-*`, `Deckboy-macos-*` and
  `Deckboy-windows-*`.

### Interface

- Startup, splash, About and Settings display `Deckboy` with the generated
  build version.

## 2026-03-28 (version flow groundwork)

### Build

- Added one repository-wide version value, read by CMake at configuration time
  to keep the app and build metadata aligned.
- CI checks that each pushed `v*` release tag matches the repository version
  before Linux, macOS and Windows builds.

### Command line

- New: `Deckboy --version` prints the SemVer release-tag version.
- `--self-check` includes the version near the top of its output.

## 2026-03-28 (playback fixes + browser backend seam)

### Timeline

- Fixed an issue where the final filmstrip tile sampled beyond the clip. The
  last sample now falls inside its duration.
- Fixed an issue where a completed cached filmstrip left its placeholder
  visible.
- Added an animated loading state to the audio lane, matching the video
  filmstrip.

### Patterns

- Fixed an issue where animated engineering patterns failed to update on some
  outputs. Motion-enabled patterns and Pocket Test variants now publish each
  regenerated frame.
- Crosshatch and checkerboard motion loops wrap cleanly; `crosshatch-motion`
  drifts on one axis in preview and output.
- Changed: new Pattern cues default to `hold` instead of inheriting playlist
  auto-advance.
- Smoke tests verify that static checkerboard frames are opaque.

### Audio

- Fixed an issue where the focused-deck VU meter remained raised after a cue
  stopped supplying audio. It now decays to zero.

### Browser

- Separated browser lifecycle and phased startup into a dedicated backend for
  platform-specific implementations.

## 2026-03-26 (DeckLink SDI output + SRT subtitles)

### Outputs

- Added DeckLink SDI output in SDK-enabled builds, with `22` modes covering
  `720p`, `1080i`, `1080p` and `2160p` at standard frame rates.
- Each output saves its device ID, mode and `10-bit` choice with the show.
  BGRA output frames convert to UYVY for DeckLink, alongside streaming and NDI
  on the same output.
- Remote: `DECKLINK ON/OFF/TOGGLE`, `DECKLINK DEVICE <id>`,
  `DECKLINK MODE <token>`, `DECKLINK 10BIT ON/OFF/TOGGLE`.
- Builds without the DeckLink SDK retain a fallback on every platform.

### Subtitles

- New: external `.srt` subtitles and embedded subtitle tracks. The first
  embedded subtitle stream, `0:s:0`, is detected automatically and cached on
  take.
- Embedded tracks are extracted with `ffmpeg -map <streamId> -f srt`.
- Subtitles draw centred at the bottom of the output with a drop shadow and
  translucent background bar.
- Remote: `SUBTITLE ON/OFF/TOGGLE`, `SUBTITLE FILE <path>`, `SUBTITLE CLEAR`.
- Subtitle path, stream ID and enabled state persist per cue.

## 2026-03-26 (Companion module + status snapshot)

### Companion

- New: Bitfocus Companion module with a TCP connection that polls
  `STATUS JSON` at a configurable interval.
- Includes `35` actions for transport, cue navigation, deck focus, seek,
  volume, blackout, transitions, cue properties, overlays, outputs, NDI,
  streaming, timecode, panic, shuffle, fullscreen and raw commands.
- Includes `13` boolean feedbacks covering deck playing/paused/stopped state,
  blackout, output health, NDI enabled/receivers, streaming, output enabled,
  test card and deck focus.
- Includes more than `50` variables for global state; focused-deck transport,
  cue, position, volume and timecode; decks `1–4`; and focused-output health,
  NDI, stream and FPS.
- Includes more than `30` drag-and-drop presets grouped under Transport, Cue
  Navigation, Deck Selection, Master, Output, Status and Transitions.

### Remote protocol

- JSON and text status now include `masterDimmer` (`0–100`), `blackout`
  (boolean) and `masterVolume` (`0–200`), supporting Companion feedback and
  level variables.

## 2026-03-25 (splash redesign, animation, and readable settings)

### Interface

- Reworked the splash with full-bleed background art beneath the existing
  `760x430` framed card and translucent backing. Boot-console and sparkle
  animations remain.
- Added program-monitor corner sparkles during playback, floating idle
  particles and a timeline playhead sparkle.
- Active blackout uses a pulsing red border glow; header sparkles span the
  available width.
- The inspector header shows an activity sparkle when a cue is selected,
  doubled during playback. Path and URL text wraps to two clipped lines.
- Fixed an issue where `VIDEO` and `AUDIO` timeline labels were truncated.

### Settings

- Improved readability across all five tabs, with clearance between card
  subtitles and their first controls.
- Expanded System labels: `FI` to `FADE IN`, `FO` to `FADE OUT`, `AUD` to
  `AUDIO`, `P-BEGIN` to `PAUSE BEGIN`, `P-END` to `PAUSE END`, `NEXT X` to
  `NEXT TRANSITION`, and `PLAYLIST PREFS` to `PLAYLIST PREFERENCES`.
- Changed System toggle rows from four controls per row to two.
- Renamed Network's `NDI TRIG` to `NDI TRIGGER`.
- Reworked Video Outputs into two columns. Dropdowns include labels such as
  `Hardware Display:` and `Resolution:`; NDI source name uses a dropdown;
  edge-blend labels sit above controls; fullscreen and orientation occupy
  full-width rows.
- About shows complete runtime information and ellipsizes dynamic paths. Audio
  information ellipsizes instead of overflowing.
- Retained standard abbreviations `TC`, `SFX`, `NDI` and `OSC`.

## 2026-03-16 (visual overhaul: beveled panels, scanlines, generation themes)

### Themes

- Changed: approximately `774` colour references use a palette built at
  startup and on theme changes.
- The default DMG theme separates grey-green shell colours
  `C4CFA1/A5B088/5A6B4A` from LCD colours `9BBC0F/8BAC0F/306230/0F380F`, with
  distinct secondary ink, button and delete-button bezel colours.
- Added Game Boy generation themes: `pocket` with silver-grey LCD, `color`
  with vivid green and indigo shell, `advance` with washed-out screen and
  indigo shell, and `sp` with a bright backlit screen and metallic silver
  shell.

### Interface

- Added panel bevels with top-left highlights and bottom-right shadows.
  Accents brighter than the fill appear raised; darker accents appear
  recessed.
- Added a procedural `1x4` scanline overlay for a CRT/dot-matrix appearance.
  Theme key `scanline_alpha` controls opacity; `0` disables it.

## 2026-03-16 (responsive cue rows and imports)

### Cues

- Changed: large imports create usable placeholder cues immediately and probe
  metadata in the background. Rows display `probing...` until results arrive.
- Cached cue tokens, kind labels, ellipsized names and metadata lines update
  when their inputs change, reducing repeated text measurement.
- Reduced per-cue memory padding by approximately `40 bytes` without changing
  the saved show format.

### Interface

- Time, timecode and inspector-number formatting no longer allocates memory
  each frame.
- All waveform displays share one peak-lookup path.

## 2026-03-15 (steadier remote control, one palette, one inspector)

### Remote protocol

- Improved thread safety for Companion/OSC TCP connections. Client-list
  updates are synchronized without holding the lock during socket waits.
- Socket polling uses a `100 ms` timeout for responsiveness.

### Interface

- All drawing uses a pre-converted palette rebuilt at startup and after theme
  changes.
- Docked and floating inspectors share `15` layout and row operations, keeping
  inset, fonts, ellipsis and spacing behaviour consistent.

### Documentation

- Added an audit roadmap for remaining optimization and cleanup work.

## 2026-03-15 (one subprocess API across platforms)

### Platform support

- Unified subprocess handling across platforms, with inherited, discarded,
  piped or merged standard streams and detached execution.
- Added common presets for piped stdout, silent detached execution and
  combined-output capture.
- Existing subprocess callers retain their behaviour through compatibility
  wrappers.

## 2026-03-15 (Deckboy 0.60 audit + cleanup pass)

### Interface

- Changed: every text prompt uses inline editing, including `Ctrl+G`, cue
  renumbering from Settings or `Ctrl+Shift+R`, MIDI port, Companion/OSC port,
  OSC Query port, OSC feedback rate, Art-Net port, canvas size and Browser cue
  creation.

### Cues

- Removed deck-level auto-advance from stored state as well as the interface,
  keeping keyboard and interface behaviour consistent. Older `auto_advance`
  fields remain harmless when loaded.
- Removed unfinished `LOWER 3RD`, `SCENE` and `PIP` creation controls from the
  bottom `MEDIA` group. `G`, `M`, `Shift+P` and remote add commands report
  that creation is unavailable.
- Existing Lower Third, PiP and Composite cues still load, inspect, save and
  render.

### Known issues

- Portability review found no need for an architecture rewrite, but Unix-only
  subprocess/FIFO paths, Linux-only browser and source capture, and
  Windows/macOS runtime backends remain unfinished.

## 2026-03-15 (Deckboy 0.60 branch, first composite cue cut)

### Cues

- New: Composite cues, saved with the show and created through `MEDIA > SCENE`
  or `M`.
- Stores `2-UP`, `70/30` or `QUAD` layout, per-slot sources, scene audio-slot
  selection and background colour.
- Inspector `PLAYBACK` controls hold, duration, fades and end action; `SCENE`
  controls layout, sources and audio slot; `OVERLAYS` attaches Lower Third and
  PiP bin items.
- Taking a Composite cue displays its authored scene placeholder in Program,
  Preview and Output.

### Known issues

- Live slot rendering from media, browser and source inputs is not yet
  implemented.

## 2026-03-15 (Composite cue architecture spec)

### Documentation

- Added the Composite cue architecture specification.
- Recommends Composite scenes within Deckboy's single-primary-cue model
  instead of a generic live-layer system.
- Covers cue and slot data, playback and rendering integration, audio and
  transport rules, inspector and monitor editing, rollout phases and explicit
  exclusions.

## 2026-03-13 (copy and paste cue settings, longer default fades)

### Cues

- New: `Ctrl+C` copies the selected cue's playback, geometry and key settings;
  `Ctrl+V` applies them to the selection while preserving each cue's media,
  name and identity.
- Added `COPY` and `PASTE` buttons to the inspector summary.
- Changed: new decks and cues default to `1.5 s` fades, up from `0.5 s`.

### Outputs

- New: `Ctrl+Shift+C` copies the focused deck's warp and edge-blend settings;
  `Ctrl+Shift+V` pastes them.
- Added `COPY` and `PASTE` beside `SAVE` in the warp editor.
- Warp preset naming now uses inline editing.

## 2026-03-13 (attached overlays + self-contained PIP sources)

### Overlays

- New: primary cues can attach one Lower Third and one PiP from `OVERLAY BIN`
  through the `OVERLAYS` inspector section.
- Attachments fire on `TAKE` only and do not participate in the main cue's
  next/loop sequencing.
- PiP sources are self-contained: `Media File / Still`, `Browser URL`,
  `Window Source`, `Camera Source` or `Syphon/Spout Source`.
- Older cue-linked PiP entries still load as `Legacy Cue Link`; live PiP
  resolves its configured source.
- Manually taking a Lower Third or PiP from the bin replaces the active
  overlay of the same kind, using the same playback behaviour as attachments.

## 2026-03-13 (overlay bin split, PIP presets, sequencing)

### Overlays

- Changed: the rundown and `OVERLAY BIN` occupy separate columns with
  independent mouse-wheel scrolling.
- Lower Third and PiP cues appear in the bin, which remains hidden until an
  overlay exists.
- Main-list next badges, keyboard next/previous and end-of-cue advance skip
  overlay-only entries.
- PiP `PLAYBACK` places `SET TARGET CUE` first, followed by
  `TL / TR / BL / BR` corner presets and `SM / BIG / 70/30` size presets
  before manual geometry.

## 2026-03-13 (cue row controls, inspector and bottom bar)

### Interface

- Inspector empty states use framed cards.
- Program-monitor `OUTPUT / DECODE / STREAM` indicators have room for their
  numbers and separate labels from values. `WARP` and `-30 / -20 / -10`
  transport controls use roomier, aligned labels.
- Reworked the bottom bar with larger `MEDIA / TRANSPORT / OUTPUT` tiles and
  group labels inside their panels.
- Removed floating Source and Pattern selectors; added `LOWER 3RD` alongside
  `IMPORT`, `SOURCE` and `PATTERN`.

### Cues

- Added row toggles for fade-in, fade-out, loop, hold on last frame and cue
  audio.
- Loop, hold and fade toggles apply to still, source, browser, pattern and
  lower-third cues as well as video and audio.
- Source type selection moved to the inspector for Window, Camera and
  Syphon/Spout cue creation.

### Overlays

- `Clear` immediately removes active Lower Thirds. Edit their `title` and
  `sub` in the inspector.
- New: PiP overlay cue through `MEDIA > PIP` or `Shift+P`, with independent
  silent playback.
- PiP includes a target cue-token editor, geometry, colour and key controls,
  and the target cue's thumbnail in preview.

### Settings

- Reorganized System, Audio and Network. Theme and UI feedback are under
  `System > Appearance`; audio-device selection is under `Audio`; network and
  integration controls use larger cards.
- `System > Appearance > UI MOTION` is always-on feedback instead of a toggle.
  Older shows load with motion enabled.

## 2026-03-12 (NMC transport sync runtime)

### Remote protocol

- Added NMC UDP transport and locate synchronization behind the existing `NMC`
  adapter toggle.
- One mode runs at a time: input applies received transport and locate
  messages to the focused deck; output broadcasts its play, pause, stop and
  locate changes.
- Configure with `DECKBOY_NMC_MODE=input|output`, `DECKBOY_NMC_PORT`,
  `DECKBOY_NMC_HOST` for the output target, `DECKBOY_NMC_SOURCE` for sender
  filtering, and `DECKBOY_NMC_LOCATE_MS` for rolling locate cadence.
- `--self-check` reports `nmc-sync-runtime`; route status reports `nmc[ok]`.

## 2026-03-12 (NDI metadata trigger runtime)

### NDI

- Added metadata-trigger reception from a discovered NDI source. `libndi`
  loads at runtime without requiring SDK headers during the build.
- Accepted metadata enters remote-command handling as `NDIEVENT`, with the
  same behaviour as Companion, OSC and ATEM commands.
- Accepts raw Deckboy commands and XML `command`, `cmd`, `action` or `event`
  attributes/elements.
- XML `cue` and `goto` attributes map to `GOTO ...`; `group` maps to
  `GROUP ... FIRE`.
- `--self-check` reports `ndi-trigger-runtime: ok/missing`; route status
  reports `ndi-trigger[ok]`.
- `DECKBOY_NDI_TRIGGER_SOURCE` restricts reception to a named source.
  `DECKBOY_NDI_LIB` overrides the runtime library path.

## 2026-03-12 (LTC ingest)

### Timecode

- Added LTC input using a runtime-loaded libltc library and the default SDL
  audio-capture device.
- Decoded timecode feeds the existing chase and trigger handling through
  `LTCEXT`, sharing the MTC quarter-frame input route.
- `--self-check` reports `ltc-runtime: ok/missing`; route status reports
  `ltc[ok]`.
- `DECKBOY_LTC_LIB` overrides the library path; `DECKBOY_LTC_DEVICE` selects a
  capture device by name.

## 2026-03-12 (bundled show export)

### Desk

- New: toolbar `BUNDLE` and `Ctrl+Shift+E` export a `.deckboy` show with a
  sibling `<show>_media/` folder.
- Media is copied and cue paths become relative, allowing the show and media
  to move together.
- Decode, thumbnails, preview and waveform analysis resolve file-backed cues
  relative to the current show folder, without depending on original absolute
  paths.

## 2026-03-10 (SRT stream stability)

### Streaming

- Fixed an issue where a stalled SRT stream or missing listener blocked the
  interface. Video writes now run separately from rendering through explicit
  process pipes.
- FFmpeg stream processes no longer inherit Deckboy's control and listener
  sockets during startup or shutdown.
- Missing listeners and reconnects report retry or recovering state.
- Documented local loopback setup: `OUTPUT ON` arms output, `STREAMING: ON`
  starts network transmission, and an external listener such as `ffplay`
  displays it.

## 2026-03-10 (portability prep + docs refresh)

### Build

- CMake prefers exported `SDL2` and `SDL2_ttf` configuration packages, with
  pkg-config and manual-lookup fallbacks.
- Centralized optional macOS framework linking and isolated Unix-only
  subprocess declarations.
- Socket sending supports platforms without `MSG_NOSIGNAL`.

### Platform support

- Added executable-location discovery on Linux, macOS and Windows.
- Sans and monospace font lookup includes macOS and Windows system fonts
  alongside project-local overrides.

### Documentation

- Documented executable-root discovery on all three platforms and current
  build/runtime readiness.

## 2026-03-06 (Phase 4 inline editing + floating panel workspace)

### Workspace

- Panel presentation and visibility now persist with the existing workspace
  model.
- New status-strip `PANELS` row: `PGM[D/F]`, `INS[D/F]`, `OUT[D/F/H]`,
  `RTG[D/F/H]`, `MSC[D/F/H]`.
- New: `Deckboy Panels` floating workspace for detached operational panels,
  each with a `DOCK` return control.
- Panel headers and cue rows show focused Deck, Output and Cue badges in
  addition to the global status strip.

### Interface

- Inline editing now covers custom output raster, refresh rate, canvas size
  and view offset; stream URL and bitrate; output alpha and delay; NDI source
  and key names; cue goto target, notes, Browser URL and ID.
- Extended text clipping and sizing improvements to quit confirmation, startup
  dialog, splash headings, About, and advanced-output and routing headers.

## 2026-03-06 (Phase 3 workflow polish + selector cleanup)

### Interface

- Changed: audio output device, output display, stream protocol and
  mirror-source selectors use non-blocking dropdowns, replacing the blocking
  list picker.
- Program / Transport uses explicit `CURRENT`, `NEXT`, `TRANSPORT`, `TIMELINE`
  and `REMAIN` labels.
- The Program summary and playlist rows use one next-cue selection rule.
- The default workspace allocates more room to Program / Transport.
- Long labels ellipsize within their controls across Decks-window tracker
  columns, playlist headers and rows, Master Scene programmer and lists,
  Program/Preview labels, thumbnail placeholders, details footer, Settings
  title/tabs/output summary and dropdowns.

## 2026-03-06 (Phase 2 operational panel split)

### Workspace

- Split the control window into operational panels: `Deck Playlist`,
  `Program / Transport`, `Cue Inspector`, `Output Panels`, `Routing Matrix`
  and `Master Scene`.
- Deck Playlist follows the focused deck. Program / Transport includes
  current/next cue summaries, focused Deck/Output route, Program and Preview
  monitors, progress, remaining time and stack view.
- Cue Inspector and Routing Matrix each have one panel; outputs use a
  repeating panel list; Master Scene occupies the right-hand column.
- Docked and floating panels share their frame styling and track their actual
  bounds.
- Output Panels and Routing Matrix are scrollable and non-blocking. Output
  controls include focus, recover, disarm and FPS display.
- Program now uses the full panel width because Cue Inspector occupies its own
  panel.
- Playback, routing, OSC, Companion, shortcuts and output-safety behaviour are
  unchanged.

## 2026-03-06 (workspace foundation: panel registry + persistence)

### Workspace

- Added workspace and focus tracking for Program / Transport, Preview, Cue
  Inspector, Routing, Master Scene, Preferences, Deck Playlist and Output
  panels.
- The operational strip always shows `WORKSPACE ...` and
  `FOCUS: DECK ... | OUTPUT ... | CUE ...`.
- Workspace state is saved separately in `data/deckboy.workspace`, preserving
  show-file compatibility.
- Saves panel visibility; docked, floating or modal presentation; panel
  bounds; control and Decks-window geometry; focused panel and
  Deck/Output/Cue; and Master Scene sidebar/programmer state.
- Startup restores geometry and focus; updates save automatically. Smoke tests
  cover serialization.

## 2026-03-06 (operator terminology normalization)

### Interface and documentation

- Changed: `Master Scene` becomes `Master Cue`; `MASTER SCENES` becomes
  `MASTER CUES`; `Create Standard` becomes `Create Window`.
- Changed: `Camera` and `Syphon/Spout` become `Camera Source` and
  `Syphon/Spout Source`; `lower-third / graphic` becomes `Lower Third`;
  `Decks panel` and `tracker window` become `Decks window`.
- The Master Cue sidebar uses `MC#`, `<MC`, `MC>`, `TAKE` and the
  `Master Cue Name` rename prompt.
- Compatibility names remain unchanged: `GROUP`, `SCENE`, `.deckboy`,
  `DECKBOY_*`, `/deckboy/*` and `deckboy-native`.

## 2026-03-05 (audio inspector metadata section)

### Cues

- Reorganized the Audio cue inspector into `PLAYBACK` for transport/audio
  behaviour, `METADATA` for tag, notes, cue ID and pause points, and `ROUTING`
  below.
- Loop, hold and end controls use consistent panel rows; pause points align
  with other metadata.

## 2026-03-05 (inspector metadata sections for lower-third, browser and source cues)

### Cues

- Lower Third inspectors use boxed `PLAYBACK` and `METADATA` sections.
- Still, Pattern, Browser and source-cue inspectors separate playback from
  metadata and source rows, matching the rest of the inspector.
- Message, information, edit, status, action and tag rows use shared
  alignment.
- Fixed an issue where Lower Third `CLEAR OVERLAY` was not clickable.

## 2026-03-05 (inspector sections, routing alignment, control styling)

### Cues

- Added boxed, collapsible `PLAYBACK`, `GEOMETRY`, `KEY` and `ROUTING`
  sections with aligned labels and minus/plus controls.

### Outputs

- Routing controls use compact table rows. Deck label, output selector, layer
  selector and assign/link action align in the output strip.

### Interface

- Bottom-bar buttons use stronger top bands and title fonts sized to fit.
- Buttons and dropdowns share panel styling and bounded text rendering.

## 2026-03-05 (grid layout cleanup: safer spacing, a clearer control window)

### Interface

- Unified vertical, horizontal, grid and table layouts, bounded text, panels,
  buttons and dropdowns.
- The control window uses an `8 px` grid, `16 px` panel padding, `12 px` gaps,
  `2 px` frames, `40 px` bottom-bar buttons and a `56 px` header.
- The header separates title, output/timecode and controls. Content reserves
  selector and bottom-control space before allocating columns.
- Deck headers, footers and cue-row text ellipsize within their bounds.
- Bottom-bar buttons have consistent widths within
  `MEDIA / TRANSPORT / OUTPUT`, labelled
  `IMPORT / SOURCE / PATTERN / TAKE / STOP / PLAY / CLEAR / PREFS`.
- The program area uses a clearer title, time and progress hierarchy with one
  monitor frame; stack and inspector spacing follow the shared layout sizes.

## 2026-03-05 (output activation: explicit health, recover and disarm)

### Outputs

- Added explicit health states: `OFF`, `ARMED`, `LIVE`, `RECOVERING` and
  `ERROR`, each retaining the reason for its latest transition.
- Output chips show health and reason, while keeping the focused output
  highlighted.
- New: per-chip `REC` recovers or re-arms the output; `OFF` disarms it.
- Health tracks fullscreen enable/recovery, stream startup, write/audio
  failures and unavailable NDI runtimes or senders.
- Escaping fullscreen reports `ARMED` with a reason. Pressing `ON` again on a
  stream output restarts transmission.

### Remote protocol

- Text `STATUS` adds `health=` and optional `health_reason="..."`.
- `STATUS JSON` adds per-output `health` and `healthReason`.

## 2026-03-05 (toggleable per-output FPS counter)

### Outputs

- New: per-output rolling FPS measurement, controlled by `FPS ON/OFF` in the
  output strip.
- Each output chip shows an `xx.xfps` readout that updates while rendering.

## 2026-03-05 (browser diagnostics, cue-panel refactor, regression smoke)

### Browser

- Added live startup diagnostics in the cue panel's `state` row:
  `starting xvfb`, `starting browser`, `starting capture`, `live` or
  `failed: <reason>`.
- Failure reasons include `url missing`, `browser not found`,
  `xvfb launch failed`, `browser launch failed` and `capture start failed`.

### Interface

- Source, URL and notes controls share consistent labelled-value, edit and
  status rows.

### Diagnostics

- Added smoke checks for Decks-window visibility: hidden with one deck unless
  manually opened, visible with two or more.
- Added checks for transition gain when stopping or taking from a paused deck,
  and for browser-status labels.

## 2026-03-05 (browser cue take/capture reliability)

### Browser

- Changed: Browser capture uses the same platform capture planning as source
  and window capture, matching the working FFmpeg `x11grab` arguments.
- Fixed an issue where a failed capture left a Browser cue black without an
  explanation. Startup now stops and reports `browser capture failed`.

## 2026-03-05 (browser cue URL editing in the cue panel)

### Browser

- New: cue-panel `url > edit` control, matching the right-side editing flow
  for still, pattern and source cues.
- Accepts a URL or local file path and converts it to a browser-safe URL.
- Editing the live cue's URL reloads it immediately.

## 2026-03-05 (transport continuity: no black flash)

### Transport

- Fixed an issue where `STOP` flashed output black while rewinding. The
  visible frame remains until frame `0` is decoded.
- Fixed an issue where `TAKE` from a paused or stopped deck flashed black. The
  outgoing transition frame retains full gain.
- Stop and rewind preserve the visible picture while decoding restarts.

## 2026-03-05 (Decks window visibility)

### Workspace

- Changed: the Decks window starts hidden for a single-deck show and opens
  when the show reaches two decks.
- The header `decks` button toggles it; manually closing it keeps it closed.
- `New` and `Open` reset manual visibility and re-evaluate it from the deck
  count.
- Hidden Decks windows do not render.

## 2026-03-05 (UI de-clutter + non-blocking dropdowns)

### Interface

- Changed: `PATTERN` and `SOURCE` actions run immediately without a blocking
  dialog.
- Added non-blocking dropdowns with outside-click dismissal, `Esc`, selection,
  mouse-wheel scrolling, `Up/Down/Enter` navigation, type-to-filter and
  `Backspace`. Only visible rows render.
- Bottom-bar `Source: … v` and `Pattern: … v` selectors set defaults.
  `PATTERN` or `P` adds the selected pattern; `SOURCE` adds the selected
  `window`, `camera` or `syphon|spout` source.
- Pattern type and transition style use cue-panel dropdowns. Source editing
  uses inline entry with plain-language prompts.
- Source aliases: `focused` or `recommended` for windows; `default` for camera
  and Syphon. Multi-selection resolves aliases according to each cue's type.
- Added optional UI art packs, preferring `data/ui/deckboy_ui_pack_v3` and
  falling back to `v2`. Covers header, output chips, cue icons, monitor frame
  and splash; mascot art remains on the splash.
- Bottom controls are grouped under `MEDIA / TRANSPORT / OUTPUT`, with
  reserved layout space and compact selectors above them.

### Diagnostics

- New: `DECKBOY_UI_PROFILE=1` logs UI frames over `50 ms`,
  event/update/layout/render timings, popup open/close events, item counts and
  popup render counts.

## 2026-03-05 (output activation stability)

### Outputs

- Fixed an issue where arming output repeatedly exited and re-entered
  fullscreen on some SDL/display-driver combinations.
- Display-mismatch recovery now requires a non-fullscreen window and valid
  display index.
- Non-fullscreen auto-recovery runs only shortly after an explicit fullscreen
  request. Hidden, minimized and wrong-display recovery remains active, with
  throttled retries.
- Recovery can apply display settings while preserving fullscreen state.
- Enable, recover and fullscreen actions verify entry into fullscreen and
  report a toast if it fails.

## 2026-03-05 (integration runtime: ATEM bridge, MTC ingest, Art-Net triggers)

### Remote protocol

- New: ATEM UDP trigger bridge on port `9910`, overridden by
  `DECKBOY_ATEM_BRIDGE_PORT`. Incoming payloads enter command handling as
  `ATEMEVENT ...`.
- Accepts `CUT`, `AUTO`, `TAKE`, `PLAY`, `STOP`, `NEXT`, `PREV`, `CLEAR`,
  `PANIC`, `SCENE <n>` and `DECKBOY <command>`.
- New: Art-Net trigger bridge parses `ArtDMX` on the configured port and emits
  edge-triggered `ARTNETEVENT ...` commands.
- Default channels: `1 TAKE`, `2 PLAY`, `3 STOP`, `4 GO`, `5 NEXT`, `6 PREV`,
  `7 CLEAR`, `8 PANIC`. Value changes on channel `9` issue `TAKE <value>`;
  channel `10` issues `GROUP <value> FIRE`.
- `Network > INTEGRATION ADAPTERS` shows ATEM and Art-Net UDP ports. Editing
  the Art-Net port restarts its listener.

### Timecode

- Added ALSA MTC quarter-frame decoding into `MTCEXT <seconds> <fps>`.
- Applies to chase-enabled decks, or the focused deck otherwise.

## 2026-03-05 (integration adapter foundation)

### Integrations

- Added an integration backend catalogue and route planner.
- Added backward-compatible saved settings for ATEM, NDI trigger, NMC, MTC,
  LTC, Art-Net and the Art-Net port.
- New: `Settings > Network > INTEGRATION ADAPTERS`, with six individual
  toggles, Art-Net port entry and an all-adapters toggle.

### Remote protocol

- New commands: `ATEM`, `NDITRIGGER`, `NMC`, `MTC`, `LTC`, `ARTNET`,
  `ARTNETPORT`, `INTEGRATIONS`.
- OSC endpoints: `/atem`, `/ndi/trigger`, `/nmc`, `/mtc`, `/ltc`, `/artnet`,
  `/artnet/port`, `/integration`.
- `STATUS` and `STATUS JSON` include an integration-route summary. OSC
  feedback publishes `/deckboy/integration/*`.

### Diagnostics

- `--self-check` adds `integration-backends` and `integration-route-defaults`.

## 2026-03-05 (runtime egress route wrappers)

### Outputs

- Changed: streaming and NDI sends require a supported backend in the active
  output route.
- Streaming stops when its route becomes inactive or unsupported.

### Remote protocol

- Output status adds `backend=...` in text `STATUS` and `backendRoute` in
  `STATUS JSON`.

## 2026-03-05 (capture/output backend planning APIs)

### Sources

- Added executable capture plans for Linux `x11grab` window capture, `v4l2`
  camera capture and desktop fallback for app textures.
- Added Windows and macOS scaffold plans with explicit availability reasons.
- Source-capture command construction now follows the selected platform plan.

### Outputs

- Added route planning for the active `window`, `stream`, `ndi` and `decklink`
  backend chain.

### Diagnostics

- `--self-check` adds `capture-plan-defaults` and `output-route-defaults`.

## 2026-03-05 (official Stream Deck + Companion profile package)

### Companion

- Added an official Stream Deck profile package for Bitfocus Companion's
  `Generic TCP/UDP` connection.
- Includes setup documentation, a canonical JSON key map and a CSV main-page
  layout.
- Pages cover Main transport, Deck focus, Output control and Master Cue
  control.
- README and manual Companion sections link directly to the package.

## 2026-03-05 (warp mode: linear vs perspective)

### Outputs

- New: saved warp modes `linear` and `perspective`. Linear is the default,
  including for older shows.
- Linear retains quad geometry; perspective uses tessellated projective
  mapping for corner pinning. Orientation and edge blend are unchanged.
- `Video Outputs > Advanced` adds `Mode Linear` and `Mode Perspective`; the
  status toast reports warp enabled state and mode.

### Remote protocol

- New: `VIDEO WARP MODE LINEAR|PERSPECTIVE|NEXT|PREV`, with direct aliases
  `VIDEO WARP LINEAR|PERSPECTIVE`.
- Deck status adds `warp_mode`; `STATUS JSON` adds `warpMode`; OSC feedback
  adds `/deckboy/deck/<n>/warp_mode`.

## 2026-03-05 (output parity: span/duplicate, orientation, test cards)

### Outputs

- Added per-output `span` or `duplicate` layout, `0/90/180/270°` orientation
  and test-card enable state. Settings persist with defaults for older shows.
- With canvas mode enabled, span uses the host deck's view offsets; duplicate
  uses origin `0,0`.
- Window, NDI, stream and delayed output use the same view region and
  orientation. Test cards render through the compositor.
- `Video Outputs` adds `Span / Duplicate`, `Rotate 0°/90°/180°/270°`,
  focused-output `Test Card ON/OFF` and `All Cards ON/OFF`.

### Remote protocol

- New: `VIDEO OUTPUT LAYOUT SPAN|DUPLICATE|NEXT|PREV`.
- New: `VIDEO OUTPUT ORIENTATION 0|90|180|270|NEXT|PREV|RESET`.
- New: `VIDEO OUTPUT TESTCARD ON|OFF|TOGGLE|ALL ON|OFF`.
- `STATUS` and `STATUS JSON` expose `layout`, `orientation` and `test_card`.
- OSC feedback adds `/deckboy/output/<n>/layout`,
  `/deckboy/output/<n>/orientation` and `/deckboy/output/<n>/testcard`.

## 2026-03-05 (multi-select inspector parity)

### Cues

- New: multi-selection inspector with `mixed` values, grouped Playback,
  Geometry, Key and Routing controls, and controls enabled according to
  compatible cue types.
- Quick edits apply across the selection: trim in/out/clear, cue timecode mark
  set/clear, loop, hold, pause begin, audio, transition-to-next, fades,
  transition duration/style and end action.
- Batch geometry covers scale mode, X/Y scale, offsets, rotation and crop.
  Batch keying covers enable, colour, tolerance and softness.
- Batch colour covers brightness, contrast, saturation and hue. Also supports
  Lower Third alpha, still duration, repeats and speed.
- Mixed selections derive toggle changes from the first eligible cue, even
  when the focused cue is incompatible.

## 2026-03-05 (OSC Query + OSC feedback mirror)

### Remote protocol

- New: optional OSC Query HTTP server. `/` browses endpoints; `/oscquery.json`
  provides endpoint documentation and live state; `/state.json` provides live
  status.
- New: optional value-based OSC feedback under `/deckboy/deck/*` and
  `/deckboy/output/*`, sent to subscribers.
- Feedback rate is configurable from `40–2000 ms`, default `120 ms`. Existing
  `/deckboy/state` JSON feedback remains.
- Network settings control Query enable/port and feedback enable/rate.
  Changing the Companion/OSC port restarts its listener immediately.
- Commands: `OSCQUERY ON|OFF`, `OSCQUERYPORT <port>`, `OSCFEEDBACK ON|OFF`,
  `OSCFEEDBACKRATE <ms>`, with matching OSC addresses.
- Saved keys: `osc_query_enabled`, `osc_query_port`, `osc_feedback_mirror`,
  `osc_feedback_rate_ms`. Ports and feedback intervals are clamped to valid
  ranges.

## 2026-03-05 (playlist preferences: deck-level timebase and defaults)

### Cues

- New: saved per-deck playlist defaults for `24/25/29.97/30 fps` timebase,
  starting timecode offset, fade duration and non-movie duration.
- Default new-cue flags include loop, fade-in, fade-out, audio, pause begin,
  pause end and transition-to-next.
- `Prefs > System > PLAYLIST PREFS` edits the focused deck's timebase, start,
  fade and still defaults, with direct flag toggles and an
  SMPTE/start-timecode/timing summary.
- Defaults apply to media import and Browser, source, Lower Third and Pattern
  cue creation.

## 2026-03-05 (capture/output backend catalogs)

### Platform support

- Added catalogues of planned capture backends: Linux `x11grab`, macOS
  ScreenCaptureKit and Windows DXGI for windows; Linux `v4l2`, macOS
  AVFoundation and Windows Media Foundation for cameras; Syphon/Spout for app
  textures.
- Output catalogue includes SDL windows, FFmpeg streams, SDK-enabled NDI and
  optional DeckLink.
- `--self-check` reports `capture-backends` and `output-backends` for
  readiness checks across all three platforms.

## 2026-03-05 (cue metadata, toggles and deck opacity)

### Cues

- Added saved `cue_id`, `audio enabled`, `pause at beginning`,
  `transition to next` and `goto target` fields, appended for compatibility.
- Cue IDs are uppercased and limited to `6` characters. Rows show the ID, then
  cue number, then index as fallback.
- Pause-at-beginning loads without autoplay on take. Transition-to-next
  selects transition timing or a cut for auto-advance and goto.
- Goto resolves cue ID, cue number or name token at cue end. Disabling cue
  audio skips audio decoding.
- New: `Shift+click` range selection and `Ctrl/Cmd+click` selection toggling.
- Batch editing covers notes, cue IDs, loop/hold, fades, colour tags and the
  new playback toggles.
- Video inspectors add `pause in`, `audio`, `next xfade` and `goto` rows.

### Outputs

- Added saved deck playlist opacity (`0–100%`), automatic fade enable and fade
  duration.
- Opacity scales the deck's composited contribution, with optional animation
  to the target.
- The deck footer has a clickable/draggable opacity rail; `Alt+click` snaps to
  `0%` or `100%`.

### Remote protocol

- New: `PAUSEBEGIN`, `PAUSEEND`, `CUEAUDIO`, `NEXTTRANS`, `CUEGOTO`,
  `CUEIDSHORT`.
- New opacity aliases: `PLAYLISTOPACITY / DECKOPACITY / DECKDIM`.
- New fade aliases: `PLAYLISTAUTOFADE / DECKAUTOFADE` and
  `PLAYLISTFADE / DECKFADE`.
- Each command has an OSC path.

## 2026-03-05 (UI clarity: header, stack, routing table, splash)

### Interface

- The header shows per-deck live summaries such as `D1 LIVE ...`, show
  filename and Companion/timecode state.
- Output chips show index, target type, armed/live state, stronger focus
  highlighting and an inline `ON/OFF` control.
- Deck headers show `Deck N`, routed output, layer token and audio device.
- Cue rows use fixed token, type, name and duration/state columns. Long names
  truncate with hover tooltips.
- Program shows focused-output name, raster and refresh, with
  `STACK VIEW (Output X)` listing deck/layer occupancy top to bottom and a
  larger progress bar containing time text.
- Cue settings have collapsible Playback, Geometry, Key and Routing sections;
  routing adds output previous/next, layer minus/plus and assign/unassign.
- Video Outputs routing is an editable `Deck | Output | Layer | Assigned`
  table, with output navigation, layer adjustment and link/unlink per row.
- Master Cue rows show the indexed scene name over its deck-slot summary, with
  a larger `TAKE` button.
- Added a startup splash with boot messages and `press ENTER to start`,
  skipped by `Enter`, `Esc` or click. Character art stays on the splash; About
  uses text, logo and runtime information.

### Documentation

- Added notes on layout components, palette tuning, cue-type icons and
  routing-table actions.

## 2026-03-05 (output FX: alpha, delay, overlay, colour space)

### Outputs

- New focused-output controls in `Video Outputs`: `Overlay ON/OFF` for time/ID
  display, `Alpha` from `0–100%`, `Delay` from `0–5000 ms`, `Color` as
  `AUTO/BT709/SRGB`, and `Delay +100`.
- Alpha dims the finished composite. The output overlay switches independently
  of the deck overlay.
- NDI and streaming use the output's delayed-frame queue or live frame; window
  output remains immediate.
- Stream metadata uses BT.709 matrix, primaries and transfer for `BT709`;
  `SRGB` uses BT.709 matrix/primaries with `iec61966-2-1` transfer.

### Remote protocol

- New: `VIDEO OUTPUT ALPHA|DELAY|OVERLAY|COLORSPACE ...`.
- Alpha percent, delay milliseconds, overlay state and colour-space token
  appear in both status formats and persist per output.

## 2026-03-05 (live source cues)

### Sources

- Added live Window Source capture through Linux FFmpeg `x11grab` and Camera
  capture through `v4l2`.
- Syphon/Spout cues use desktop capture on Linux while native backends remain
  planned.
- `Take` with autoplay starts capture; `Play` starts or restarts it; `Pause`
  stops capture while holding the frame; `Stop` stops capture and restores the
  source slate.
- Deck status shows `Live Source` or `Source Ready`.
- Hover tips identify live source cues; toasts distinguish loaded, live and
  unavailable sources.

## 2026-03-05 (output operations flow + source cue scaffold)

### Outputs

- Added `Create Standard` for window output and `Create Stream`, plus direct
  `Set Window` and `Set Stream` controls in Video Outputs.
- A focused-output signal-flow line shows deck layers, output and display.

### Sources

- New cue types: `Window Source`, `Camera` and `Syphon/Spout`, created through
  `SOURCE` or `Preferences > System > Add Source Cue...`.
- Commands: `SOURCE WINDOW|CAMERA|SYPHON ...`; aliases: `WINDOWSOURCE`,
  `CAMERACUE`, `SYPHONCUE`, `SPOUTCUE`, with OSC mappings for each.
- Saved type tokens: `window_source`, `camera`, `syphon`.
- Source cues currently render a generated placeholder and participate in
  normal transport, routing, save/load and status.

## 2026-03-05 (NDI is per-output)

### NDI

- Changed: NDI belongs to outputs. `N`, `NDI ...`, `NDINAME`, `NDIKEY...` and
  Video Outputs controls act on the focused output.
- Each output sends fill video, optional key video and mixed stereo audio from
  its assigned deck stack.
- Older deck NDI settings migrate to outputs that have no NDI configuration.
  Legacy deck fields remain in the show format for compatibility.
- NDI status appears only on output entities.

### Documentation

- Updated NDI instructions to focus an output first: `VIDEO OUTPUT <n>`, then
  `NDI ...`.

## 2026-03-05 (UI declutter + output workflow clarity)

### Interface

- The empty control view uses text-only prompts: `Insert cartridge`,
  `Drop media here`, `Press A to take cue`.
- `Video Outputs > Routing In Main Strip` is informational.
- `Show Advanced / Hide Advanced` reveals or hides refresh, bit depth, canvas
  and warp controls.
- Renamed the secondary window `Deckboy Decks`, with a `DECKS` header above
  its deck list and playlists.

## 2026-03-05 (demo show generator + layout presets)

### Demo shows

- Added repeatable demo generation in `data/demos/`.
- `demo_70_30_4pip_bg_5deck.deckboy`: five decks with a full background and
  four right-column PiPs. Master cues: `Open - BG + 4 PiP`, `BG Only`,
  `PiP Motion Sweep`.
- `demo_quad_2x2_4pip_bg_5deck.deckboy`: five decks with a full background and
  four PiPs in a `2x2` grid. Master cues: `Quad Open`, `Quad Motion`.
- `demo_program_preview_clean_3deck.deckboy`: three decks with program
  background, preview PiP and corner bug. Master cues:
  `Program + Preview + Bug`, `Program + Bug`, `Program Clean`.

## 2026-03-05 (safety, output quality, master-cue speedups)

### Desk

- New: a startup lock prevents a second app instance and reports a terminal
  message. `--allow-multi-instance` bypasses the lock.

### Outputs

- Changed: arming or re-arming a window output changes global fixed sizing to
  display-native sizing.
- Repeating `VIDEO OUTPUT ON` applies the same sizing before recovery; toasts
  report `auto native`.

### Master cues

- Clicking a programmer row outside its buttons assigns that deck's selected
  cue.
- Scrolling over a row cycles its cue directly, without a popup.

## 2026-03-05 (panic timing, cue find, timecode follower)

### Desk

- New: `Preferences > Audio > Panic fade (sec)` and `Panic auto restore`.
- Panic fade profiles use the selected duration; automatic restore returns the
  dimmer after the deck action.
- Remote: `PANICFADE <seconds>`, `PANICAUTORESTORE ON|OFF|TOGGLE`.

### Cues

- New: `FIND <token>`, `FINDNEXT`, `FINDPREV`, `FINDTAKE <token>`.
- New: `RENUMBER [prefix] [start]` and `RENUMBER CLEAR`, with aliases `AUTOID`
  and `CUEAUTOID`.
- Cue tokens accept `FIRST`, `LAST`, `NEXT`, `PREV`, `SEL`, `ACT`, relative
  `+N/-N` offsets and cue-number prefixes.
- Added `FINDCLEAR / FINDRESET`, `FINDSTATUS`, `CUEFIND*` aliases,
  `STATUS CUES / STATE CUES` and `STATUS FIND / STATE FIND`.
- Find status includes token/cursor metadata and per-deck `selected_num`,
  `selected_id`, `active_num` and `active_id`.
- OSC: `/find`, `/find/next`, `/find/prev`, `/find/take`, `/find/clear`,
  `/renumber`.

### Timecode

- Added saved per-deck jam-sync and freewheel-duration settings in
  `Preferences > Audio`.
- Remote: `TIMECODE JAM ON|OFF`, `TIMECODE FREEWHEEL <seconds>`; OSC:
  `/timecode/jam i`, `/timecode/freewheel f`.
- A running deck in chase holds when external timecode has been absent longer
  than the freewheel duration.
- With `TC JAM OFF`, incoming timecode inside the freewheel window does not
  reset the running clock. `TIMECODE SET ...` always forces a jam.

### Settings

- Grouped Audio into `System + UI`, `Playback Semantics`,
  `Safety + Timecode Follower` and `Cue Tools (menu-first)`.
- Cue Tools includes `Find Cue...`, `Next`, `Prev`, `Find+Take`,
  `Renumber...`, `Clear Numbers` and `Clear Find`.

### Remote protocol

- Text status includes panic profile, fade and restore, plus per-deck `tc_jam`
  and `tc_freewheel_s`.
- JSON includes top-level panic fields and per-deck `timecodeJam` and
  `timecodeFreewheelSeconds`.

## 2026-03-04 (playback semantics)

### Transport

- New: `Preferences > Audio > Jump Mode`, set to `Trigger` or `Load`, and
  `Jump Transition ON/OFF`.
- `Take` and `Goto` trigger playback in Trigger mode, or load without autoplay
  in Load mode. Jump Transition chooses cue/deck transition timing or a cut.
- New: `Preferences > Audio > Panic Profile`, with `Outputs Off`,
  `Fade+Pause`, `Fade+Rewind`, `Fade+LoadNext`, plus `Run Panic`.
- Outputs Off disarms all outputs. Fade profiles dim first, then pause, rewind
  or load the next cue.
- Settings persist as `jump_mode`, `jump_transition` and `panic_profile`.

### Remote protocol

- New: `JUMPMODE TRIGGER|LOAD|TOGGLE`, `JUMPTRANSITION ON|OFF|TOGGLE`,
  `PANICPROFILE <name>|NEXT|PREV`.
- `PANIC` runs the chosen profile; `PANIC <profile>` applies a one-time
  override.

## 2026-03-04 (a bigger, clearer decks workspace)

### Workspace

- Changed: one operating workflow replaces the mode split.
- The Decks window opens at `1560x920`, minimum `1260x700`, in Game Boy green
  with star accents.
- Enlarged tracker rows, deck-name columns, deck cards, per-deck headers,
  transport buttons and playlist cue rows.
- Main transport is labelled `Play/Pause`; deck cards use `Take` and `Stop`
  without a competing Go button.

### Master cues

- Added inline slot programming: `-` and `+` cycle each deck slot's cue.
- Middle-click and Ctrl interactions cycle cues instead of opening a picker.

## 2026-03-04 (an always-on master-cue sidebar)

### Master cues

- Changed: the Master Cue sidebar is wider and permanently visible, with a
  fixed `Master Cues` badge in the output strip.
- `Name` renames the focused Master Cue; `CapSel` captures each deck's
  selected cue; `CapAct` captures each deck's active cue.
- `Prog+ / Prog-` expands or collapses a programmer row per deck with `Sel`,
  `Act`, `Byp`, `-` and `+`.

### Workspace

- Enlarged Decks-window tracker, headers, deck cards, buttons and cue rows.
- Expanded labels to `selected`, `active`, `layer`, `state` and `timecode`.
- Added the hint `click cue to select, right-click cue to take`.

### Outputs

- Changed: choosing the focused output's display switches fixed raster sizing
  to display-native. The toast reports `auto native`.

### Patterns

- Expanded Pocket Test into a platform-adventure scene with automatic
  `pocket-test` cycling and selectable `pocket-day`, `pocket-sunset`,
  `pocket-night` and `pocket-storm` variants.
- Added animated crab, jumping fish, parrot, turtle, dinosaur-style enemy and
  puff friend alongside the explorer, coin line and signal strip.

## 2026-03-04 (deck/output separation + stream outputs)

### Outputs

- Changed: window outputs default to `OFF` in new shows and newly created
  outputs. Output windows start hidden; loaded shows open disarmed regardless
  of saved enable state.
- `Preferences > Video` adds focused-output `Enabled`, which opens fullscreen
  on the selected display, plus `Prev / Next / Rescan` display assignment and
  a live display label.
- `F` enables the focused window output; remote: `VIDEO OUTPUT ON|OFF|TOGGLE`.
- `Esc` leaves fullscreen on the active output, falling back to a fullscreen
  output sharing the control display. That output remains intentionally
  windowed until explicitly re-armed.
- Three `Esc` presses within approximately `0.9 s` disarm all outputs. Re-arm
  with `F` on the output or `VIDEO OUTPUT ON`.
- Recovery restores display placement, raises hidden/minimized windows and
  reapplies fullscreen. Repeating `VIDEO OUTPUT ON` triggers recovery; `F`
  reasserts fullscreen instead of toggling to windowed.
- A `1 Hz` background check recovers enabled outputs that move off target.
- Display hot-plug and `Rescan` refresh valid assignments and reapply
  placement. Clicking a `Connected Displays` row assigns the focused output.

### Routing

- Route labels use `Deck 1`, `Output 2`, `None`, `Background` and `Layer N`.
- Route Editor layer controls use `-` and `+`, `Ctrl` for tenfold steps,
  `Shift` to reverse, and explicit `Assign / Unassign`, without a blocking
  popup.
- Added a Deck × Output matrix in Video settings. Row labels focus decks,
  column headers focus outputs, and cells assign routes.
- `MOVE / ADD` selects single-output routing or fan-out. Assigned cells adjust
  layers with click `+1`, `Shift+click -1`, and `Ctrl` for tenfold steps.
- Main controls include `Add Output`, `Link / Unlink`, `Layer- / Layer+`, and
  `Focused Route: Deck N -> Output N Background/Layer N/Not Linked`.

### Streaming

- Added independent FFmpeg-backed streams per output. Video settings control
  enable, `SRT / RTMP`, URL and bitrate.
- Remote: `VIDEO STREAM ON|OFF|TOGGLE`, `VIDEO STREAM SRT|RTMP`,
  `VIDEO STREAM URL ...`, `VIDEO STREAM BITRATE ...`.
- Streams combine H.264 video and stereo AAC. Audio follows the output's deck
  assignments, falling back to the host deck when no assignments exist.
- Outputs save window/stream type, mirror-source output, stream enable,
  protocol, URL and bitrate.
- Protocol names are normalized, bitrate is clamped to `500–50000 kbps`, and
  default URLs follow output index and protocol.

### Master cues

- Changed: group presets are presented as Master Cues, with default names
  `Master Cue 1`, `Master Cue 2` and matching prompts/toasts.
- Added `MASTER` and `MASTERCUE` aliases alongside `GROUP`.
- Saved Master Cues contain one cue-ID or bypass slot per deck and retain the
  focused Master Cue.
- Commands: `GROUP ADD`, `GROUP ADDEMPTY`, `GROUP SELECT`, `GROUP NAME`,
  `GROUP DELETE`, `GROUP SET <deck> <cue-token|SEL|ACTIVE|BYPASS>`,
  `GROUP BYPASS`, `GROUP CAPTURE SEL|ACTIVE`, `GROUP FIRE [index]`.
- `Ctrl+Shift+G` fires the focused Master Cue; `Ctrl+Shift+N` captures
  selected cues into a new one; `Ctrl+Shift+[` and `Ctrl+Shift+]` change
  focus.
- Rows show deck slots and a fire action. Click assigns the selected cue;
  `Shift+click` assigns the active cue; middle-click or `Ctrl+click` opens a
  picker; right-click bypasses the slot.

### Workspace

- The main window emphasizes program and outputs; the Decks window holds
  playlists and per-deck `Take / Go / Stop`.
- Deck headers show transport, timecode value/FPS, chase/free and run/hold
  state. The main panel shows focused-deck number, name and timecode.
- The tracker pages around the focused deck and reserves playlist-grid height
  so lower deck columns remain visible.
- The Decks window is always shown, with larger default and minimum
  dimensions.
- Added compact LSDJ-style tracker rows, `sel` and `act` cue-number columns
  and focused-deck highlighting.
- Main-header `New / Open / Save / SaveAs` controls manage the show. Save
  writes the active path immediately; SaveAs writes when a path is selected.
- An application quit event or closing the main window exits immediately.
  Closing Decks hides it; closing an output disarms that output.

### Cues

- `I` and `O` set trim-in and trim-out at the playhead, snapped to the cue's
  frame rate. `Shift+I` imports; `Shift+O` toggles the time overlay.
- While paused, `Left/Right` moves one frame, `Shift` five frames, `Ctrl` ten
  frames and `Alt` `1.0 s`, all frame-snapped.
- Output respects independent X/Y scale and `Fit / Fill / Stretch / Unscaled`.
- `off X / off Y` adjust by `1 px`. `scale X`, `scale Y`, `off X`, `off Y` and
  `rot` accept typed expressions with `+`, `-`, `*`, `/` and parentheses.

### Patterns

- Added a Pattern picker and per-cue `pattern` row. `-` and `+` cycle base
  types; the centre toggles supported motion.
- Added `smpte-bars-motion`, `crosshatch-motion`, `checkerboard-motion` and
  `full-*-motion`, with frame updates for every animated type.
- Remote: `PATTERN SET <type>`, `PATTERN LIST`, `PATTERN <type> MOTION`.
- Reworked Pocket Test as a deterministic tropical scene with sky, ocean and
  beach gradients, procedural palms, island silhouette, animated coins, retro
  explorer and bottom signal-reference strip.

### Remote protocol

- Text `STATUS` adds `OUTPUT ...` rows with `type`, `host`, `display`,
  `layers`, `mirror`, stream state, URL and bitrate.
- `STATUS JSON` adds `focusedOutput`, `outputCount` and `outputs[]`.

## March 2025 — the foundations

Deckboy's single-file prototype was rebuilt as a modular application.

### Platform support

- Established eight modules for core, media, rendering, control, interface,
  platform, NDI and browser support, with public APIs, a CMake build strategy,
  feature gates and a documented dependency graph.
- Added `55` dependency-free shared utilities for strings, timecode, colour
  conversion, field parsing and JSON escaping.
- Centralized FFmpeg and capture-process management.
- Added optional RtMidi input across platforms, `10-bit` DeckLink SDI output,
  Syphon/Spout texture sharing and native browser rendering, each with
  fallback behaviour when its SDK is absent.

### Build

- Added platform abstraction and GitHub Actions builds for Linux, macOS and
  Windows.
- Build with `cmake .. && make -j4`.
- Optional configuration: `-DENABLE_MIDI=ON`, `-DENABLE_SIPHON=ON`,
  `-DENABLE_SPOUT=ON`, `-DENABLE_DECKLINK=ON -DDECKLINK_SDK=/path/to/sdk`.
- Established GPL compliance and licensing.

### Diagnostics

- `./deckboy-native --self-check` reports the project root, sans/mono/pixel
  font availability, FFmpeg, ffprobe and NDI SDK status.
- Reports `ndi-sdk: not built (set DECKBOY_NDI_SDK or install SDK headers)`
  when NDI was not compiled.
- Reports UI sounds using a separate SDL audio device when available, and the
  default Companion TCP/UDP port `5510`.
