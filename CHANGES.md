# CHANGES - Incremental Updates (March-October 2026)

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

`tools/package_macos.sh` builds `dist/Deckboy-<VERSION>-macos-<arch>.zip`, the
counterpart to the Windows portable zip: a double-clickable `Deckboy.app` that
carries everything it needs.

Homebrew installs SDL3 and FFmpeg under `/opt/homebrew`, so a bundle that merely
links against those paths would run on the build machine and nowhere else. Every
non-system dylib is copied into `Contents/Frameworks` and every reference
rewritten to `@rpath` — recursively, because dependencies have dependencies.

Two things that break naive macOS bundles, handled explicitly:

- **`install_name_tool` invalidates a code signature**, and on Apple Silicon the
  kernel refuses to run a binary whose signature is broken. Everything is
  re-signed ad-hoc *after* rewriting, inside-out.
- **`Contents/MacOS` is a code-only directory**, so `data/` goes in
  `Contents/Resources` and `Paths::resolveProjectRoot()` has a small `__APPLE__`
  branch that recognises the `…/Contents/MacOS/<exe>` structure. Detection is
  structural rather than a `.app` suffix test, because whoever downloads the
  bundle may rename it. A plain build-tree run on macOS is unaffected.

The `Info.plist` carries `NSCameraUsageDescription` /
`NSMicrophoneUsageDescription` / `NSLocalNetworkUsageDescription`. These are
load-bearing rather than boilerplate: macOS terminates a process that touches
the camera or microphone with no matching usage string, so these are what make
"add a camera cue" work.

Per-machine state (`last_project.txt`, `default.deckboy`) is stripped, same as
the Windows packager.

A new `macos-package` CI job runs the packager on a real Mac and then checks the
result rather than assuming it: no `/opt/homebrew` or `/usr/local` reference
survives in any binary, `codesign --verify --deep --strict` passes, the app runs
`--self-check` and `--smoke` **from inside the bundle** (exercising the rewritten
`@rpath` and the bundled `data/`), and no build-machine state shipped.

Limits: no Developer ID signing or notarisation yet (a downloaded zip needs
`xattr -dr com.apple.quarantine`, documented in the bundle's README), and no
universal binary. Browser cues, Spout and d3d11va zero-copy decode are Windows
features by construction.

## 2026-08-08 — v0.83.0 (NMOS IS-04/IS-05: the 2110 senders become discoverable)

### What this adds
An ST 2110 flow is undiscoverable on its own, and copying an SDP out of a
settings modal by hand is not how facilities work: a node registers itself with
a Registration & Discovery System, and a broadcast controller connects it
through IS-05. This is the difference between "emits valid packets" and "shows
up in the plant".

New `native/platform/nmos_node.{hpp,cpp}`:

- **IS-04 v1.3 Node API** over HTTP — node, devices, sources, flows, senders.
  Receivers are advertised as an empty list, because Deckboy is a source device
  and an honest empty list is what stops a controller offering to route into it.
- **IS-04 registration** — POSTs the resource tree in dependency order
  (node → device → source → flow → sender; a registry rejects a flow whose
  source it has not seen), then heartbeats every 5 s against the registry's 12 s
  default health timeout. A 404 on heartbeat means the registry restarted and
  forgot us, which triggers automatic re-registration. A missing registry backs
  off 1s→30s instead of hammering the network.
- **IS-05 v1.1 Connection API** — constraints / staged / active / transportfile
  per sender, with a real staged scratch state so a controller's
  stage-then-activate workflow behaves as the spec describes. The transportfile
  is the *same* SDP the settings modal shows: `st2110ConfigForOutput()` is the
  single source of truth, so a receiver can never be handed two different
  descriptions of one flow.
- **IS-05 PATCH really reconfigures the sender.** `master_enable` and
  `transport_params` with `activate_immediate` are applied to the actual
  `OutputTarget` — moving the multicast group over IS-05 moves the stream, and
  the paired audio leg follows the video base port by the +2 convention. The
  handler blocks until the main thread has genuinely applied the change; telling
  a controller a route moved before it had is the exact lie that makes a plant
  untrustworthy.
- **Resource ids are UUIDv5** derived from a fixed Deckboy namespace and a
  stable per-output seed, so a controller's saved route survives an app restart
  with nothing persisted to disk. Verified byte-identical against Python's
  `uuid.uuid5` on three seeds.

Settings: Video Outputs → Devices, directly under ST 2110 (it is meaningless
without it). Remote command `NMOS ON|OFF|STATUS|REGISTRY|PORT|NIC`, mirroring
the existing `ST2110` command. Actions 710–714; next free 715+.

### Verified — AMWA official conformance suite
Run against the real app with **AMWA's own NMOS Testing Tool** (`IS-05-01`,
Connection Management API): **36 pass, 2 fail, 23 not applicable**. The 23 are
all receiver tests — Deckboy is a source device and advertises an empty receiver
list. The 2 failures are the deliberate scheduled-activation refusal below.

### Verified — reference registry interop
`nmos-cpp` (the AMWA reference implementation) run on a separate Linux machine.
Deckboy registered its full resource tree; the registry held 1 device, 2 sources,
2 flows, 2 senders, 0 receivers. That machine then fetched the SDP over the LAN
via the advertised `manifest_href` and issued an IS-05 PATCH moving the group to
239.77.7.7:22000, which the served SDP reflected. The registry's own
`registration_expiry_interval` is 12 s, the timeout our 5 s heartbeat was sized
against.

With the network set to LOCAL ONLY (the default) Deckboy **withholds
registration** and says why, in the settings status line and in a toast when
armed: publishing a LAN address while listening only on loopback would put a
sender in the plant that nothing can reach.

### The SDP names the interface the stream actually leaves by
The origin line resolves the local address by asking the routing table which
interface reaches **the stream's own destination group**, rather than probing a
generic internet address and getting the default route — which on a machine
running a VPN is the tunnel. Verified on a host with ProtonVPN up: origin
resolves to the LAN NIC, not the tunnel and not loopback. Memoised per
destination with a 30 s refresh, because SDPs are rebuilt every time the NMOS
sender snapshot is taken.

### Limits — do not claim it
- **No mDNS / DNS-SD.** The registry is configured by URL. Deckboy cannot find a
  registry on its own, and cannot be found in peer-to-peer mode. This is the
  single biggest remaining gap.
- **No scheduled activation.** `activate_scheduled_absolute` / `_relative`
  return 501; honouring them needs the PTP clock to gate the switch, and taking
  a source at the wrong instant is worse than refusing.
- No IS-05 bulk staging (501), no receivers, no IS-07/08, no HTTPS, no IS-10 auth.
- The underlying 2110 caveats stand: not PTP-locked, not narrow-model paced.
- **`IS-04-01` (Node API suite) has not been run**, only `IS-05-01`. It needs
  the tool's own registry and DNS-SD, which we do not support.

## 2026-08-07 — v0.82.1 (patterns hold a flat footprint)

### Pocket-test runs like every other pattern
The static layer of the pocket-test card is cached per raster in a small bounded
set, so the programme output and the cue-preview runtime hit the cache rather
than evicting each other. Animated patterns also build **in place** into the
frame the engine already holds, and that frame's GPU zero-copy state is reset
explicitly, so a pattern following a hardware-decoded video cue draws itself and
not a stale decoder surface.

Measured, with controls, at 3840x2160:

| | private commit | swing |
|---|---|---|
| idle | 244 MB | 1 MB |
| video playing | 265 MB | 1 MB |
| smpte-bars (static) | 244 MB | 1 MB |
| test-bars (animated) | 242 MB | 1 MB |
| pocket-test | 245 → 246 MB | 1 MB |

Live output frame rate with pocket-test at 4K is a steady 60.0. Rendering is
byte-identical.

### Deleting a cue from the right-click menu
Deleting the **live** cue from a context menu takes one click: the item reads
"delete LIVE cue" in a hotter red, because choosing a named item from a menu is
already deliberate. The keyboard Delete path still asks for confirmation, where
a repeated keypress is natural.

## 2026-08-07 — v0.82.0 (SMPTE ST 2110 output, PTP, dual streaming, safety UI)

### SMPTE ST 2110
- **ST 2110-20 uncompressed video output.** Correct pgroup packing (YCbCr-4:2:2
  10-bit = 2px/5 octets, and 8-bit), RTP + payload headers with Sample Row Data
  descriptors, marker-bit framing, BT.709 studio-swing conversion, multicast with
  TTL and NIC pinning, and SDP generation with an exact rational frame rate.
  Verified against ffmpeg: decodes as `yuv422p10le` / `uyvy422`, correct picture.
- **ST 2110-21 pacing.** Packets are spread across the frame interval on a
  dedicated sender thread, at a rate receivers accept, and the render loop is
  never held to pace a stream.
- **ST 2110-30 (AES67) audio.** 48 kHz, 1 ms packets, L24, fed from the engine's
  existing audio tap so the stream carries exactly what the PA hears. Verified:
  received as `pcm_s24be, 48000 Hz, stereo`.
- **PTP (IEEE 1588 / ST 2059) slave.** Follows the grandmaster on a configurable
  domain and disciplines the RTP media clock. **The SDP only advertises
  `ts-refclk:ptp` when genuinely locked** — never optimistically.
- Honest limits, stated in the UI and the code: software timestamping, wide-model
  pacing, no NMOS discovery. See `docs/ST2110_FEASIBILITY.md`.

### Streaming
- **SRT and RTMP are independent destinations that can run at the same time**,
  each with its own complete configuration.
- **SRT has its own controls**: caller/listener mode, latency, passphrase and
  stream ID, merged into the URL (anything typed by hand still wins).
- RTMPS is carried as RTMPS, and the GOP length follows the encoder settings.

### Timecode
- **LTC generator.** `--ltc-generate` produces LTC that round-trips through
  Deckboy's own decoder at 24/25/30 fps, including midnight rollover — so
  Deckboy can generate timecode as well as chase it. Not yet wired to a live
  audio output.

### Safety and clarity
- **BLACKOUT has a button** (and `B`), beside CLEAR — the fastest and most
  reversible way to kill the picture, and now the one nearest to hand.
- **Backspace deletes; `U` clears overlays.** One key, one meaning.
- **Escape is a three-stage escalation** — control window, then clear output,
  then quit.
- **The shortcuts overlay matches the app**, with ten missing bindings added.
- Animated on-air indicators per stream type in the OUTPUT group.

### Audio
- **Loudness normalize reaches its target.** Peaks are handled by a new
  look-ahead peak limiter in the deck audio path, so a −26.8 LUFS cartoon that
  wants +10.8 dB gets +10.8 dB.
- **The waveform is drawn on a dB scale with headroom**, so gain changes are
  visible where a linear scale pinned everything above about −6 dBFS.
- Waveform analysis covers the first 5 minutes of a file.

### Terrarium
- Re-vendored from upstream (`4931aa0`) with recorded provenance. A new
  `terrarium-pico` pattern renders the Raspberry Pi panel's 1px-per-cell
  picture.
- `--pattern-dump` honours its size argument for every pattern.

### Also
- Settings opens whatever the inspector is doing: the inspector's dropdowns are
  gated to their viewport, so a scrolled-away row keeps clear of the SETTINGS
  button.
- **Edge feathering works with and without perspective warp**, as a true edge
  ramp rather than a fade across the picture.
- Packaging ships no machine state from the build box.
- Kerning is disabled on UI fonts, so words stay whole ("Target URL").
- A crash logger writes a symbolised stack to `data/deckboy-crash.log`,
  including from file-dialog threads.
- Refresh-rate, bit-depth and raster-mode controls are back in the UI, and
  ~1,646 lines of superseded cue-inspector code and 21 unreachable handlers are
  gone.


## 2026-07-31 — v0.81.4 (MIDI input on every platform)

### MIDI
- **MIDI input works on Windows and macOS**, through the cross-platform RtMidi
  wrapper and into the same command queue Linux uses — so a note or a CC does
  exactly what it does on ALSA, and every documented note/CC mapping is live on
  all three platforms.
- **New MIDI remote command** (`MIDI ON|OFF|TOGGLE`), so a Companion surface can
  arm MIDI rather than requiring a trip to the Audio settings tab.
- `--self-check` reports `midi-runtime` with the RtMidi port count.
- MTC quarter-frame and MMC/MSC sysex are ALSA-only, since the wrapper surfaces
  channel-voice messages only. The catalog says so by name.

### Diagnostics
- `--self-check` reports the NMC bridge for real on every platform: it is
  cross-platform UDP, and on Windows it binds its port and reports `nmc[on,ok]`.

## 2026-07-30 — v0.81.3 (OSC Query on Windows, the full DeckLink card)

### Remote control
- **OSC Query runs on Windows**, on the same cross-platform socket code
  Companion control already uses. Verified live — `OSCQUERY ON` binds TCP 5511
  and an HTTP GET returns the Deckboy OSC Query page, with the mirrored
  `/deckboy/state` feedback.

### Video Outputs
- **Every DeckLink control is on screen.** The NDI, DeckLink, Edge Blend and AOI
  sections size from the shared header + row metrics, so DeckLink's **10-BIT**
  toggle and NDI KEY sit fully inside their panels at any font size.

## 2026-07-30 — v0.81.2 (genuinely dark themes)

### Themes
- **Deckboy can have genuinely dark themes.** The structural chrome — bottom-bar
  groups, playlist body, deck list, timeline lanes — draws from the
  `screen_tile` / `screen_fg` pair added in v0.79.3 rather than from the bright
  ink role. Both roles fall back to exactly what was there before
  (`tile` → `screen_light`, `fg` → `screen_deep`), so a theme that does not
  define them renders identically — `gameboy` is untouched — while the 15 that
  do become properly dark.
- The library now spans three looks: bright moulded-case, dark tinted chassis,
  and true-black OLED terminal.

### Audio
- **Both waveform views draw through `drawColumn`**, so the mono view carries the
  over-scale clip tint and keeps its bars inside their lane at any gain trim.

### Build
- CI is green on all three platforms: `find_package(PkgConfig)` runs
  unconditionally, so `-DENABLE_MIDI=ON` configures against a CONFIG-package
  SDL3, and the display-topology code builds against the stable SDL 3.2.x
  release as well as 3.4.

## 2026-07-30 — v0.81.1 (theme variety, honest LTC reporting, README)

### Themes
- **The library looks varied.** The 25 themes that shared one recipe now span
  three families: **moulded case** (muted plastic shell with a saturated LCD,
  the structure the default Game Boy theme uses), **tinted chassis** (a
  genuinely coloured dark body with bright ink, for booth use), and **true-black
  OLED terminals** (kept for `dark`, `virtual-boy`, `famicom`,
  `captain-falcon`, `game-and-watch`). `gameboy`, `pocket`, `sp`, `advance` and
  `color` were already distinct and are untouched.
- Case colours are deliberately low-chroma. `shell_inner` is the dominant panel
  fill across the whole UI, so a vivid case floods the interface — the
  character's hue belongs to the screen, which is exactly how the original
  Game Boy theme is built.
- All 30 pass `tools/audit_theme_contrast.ps1`.

### Timecode
- **`--self-check` reports LTC for real on every platform** — `ltc-runtime: ok`
  with the library present, `missing (...)` without it. `LtcApi` loads `ltc.dll`
  dynamically and the portable zip ships it, so Windows has LTC and now says so.

### Build
- **DeckLink builds with or without it enabled.** `decklink.cpp` compiles
  always, carrying its complete stub behind `!DECKBOY_HAS_DECKLINK`, so an
  `ENABLE_DECKLINK=OFF` build links.
- **CI is repaired and runs on every push and PR**, on the branch that exists,
  with SDL3 installed on all three platforms, one configure step per platform,
  smoke tests beside `--self-check`, and the Companion module's suite as its own
  job.

### Docs
- **README overhauled** to describe what exists: the portable zip leads the run
  instructions, Companion leads with the module, the theme count is one number,
  and shipped features are listed as shipped.

## 2026-07-29 — v0.81.0 (preview locked to output, display hot-plug, Companion module)

### Program monitor
- **The preview is locked to the program output.** The control window samples
  the output's finished composite on the output's own render pass, scaled down
  to preview size first (~0.5 MB read back instead of 3–12 MB), every presented
  frame — so the monitor cannot drift from what leaves the machine, and it runs
  at full rate rather than the ~10 fps a full-resolution hardware-frame download
  allowed. Verified by capturing both windows in both orders against a burned-in
  frame counter.
- The tap is taken *before* warp/AOI/edge blend, so the warp editor still draws
  its handles over an unwarped image. When no window output is armed the
  decoder-frame path still runs, unchanged.

### Displays
- **Deckboy notices displays connected or disconnected while it is running.**
  The topology scan compares a per-display fingerprint (name + desktop
  placement) rather than the display count, so it also catches a monitor swapped
  for another one, a rearranged desktop, and resolution changes. All SDL display
  events are handled and debounced, since Windows emits a burst of them and
  reports half-built topology partway through.
- On a hot-plug an affected output is re-homed **even while fullscreen**, which
  is what lets a monitor connected mid-show pick up its output. Unaffected
  outputs are left alone.
- **Unplugging a projector leaves your control screen alone.** An output whose
  pinned display disappears parks windowed and hidden with health
  `display missing: <name>`, and restores itself when the panel comes back.
- Toasts name what happened — "display connected: DELL U2720Q (2 total) —
  1 output re-homed" — and RESCAN forces a re-home even when nothing changed.

### Interface
- **One text-placement contract across the whole app.** Panels paint the rect
  they are given and all three label helpers centre on it, so neighbouring
  controls line up whichever helper drew them. Label padding is continuous with
  width, and every centring helper ellipsizes and clips, so a long label
  truncates inside its pill.
- Settings cards and Video Outputs sections share one header-plate contract, so
  a section title sits at the same height on every tab, and the CONNECTED
  DISPLAYS **IDENTIFY** button sits inside its header plate.

### About
- **Rewritten as a real credits page**: masthead with wordmark, version and
  build date; a PROJECT column (copyright, GPL-3.0-or-later and what that grants,
  source, warranty disclaimer, live session ports and theme); and a BUILT WITH
  column (platform, SDL3/FreeType, FFmpeg, the optional SDKs *this* binary was
  actually compiled against, timecode, font licence, key reference). Both
  columns share one label gutter.

### Patterns
- **New "Test Clock (sync + latency)" pattern** — colour bars, an aspect-truth
  circle (reads as an egg the moment a stretch mode is wrong), a scrolling hue
  band for sub-second phase, and a large seconds + frame counter with exact
  timecode in the corner. Point two displays at it and photograph them to see
  whether they agree. Aliases: `testsrc1`, `sync-card`, `latency-clock`.

### Audio
- **Loudness normalize reaches target on quiet material.** The trim range is
  −40..+40 dB, read from one shared constant by the setter, the normalizer, the
  waveform scaler, the engine mirror, project load and the remote command — so a
  −34 LUFS transfer that wants +17.8 dB gets it.
- **Normalize is peak-aware.** The same analysis pass measures true peak
  (`ebur128=peak=true`) and holds the boost so peaks stay under −1 dBFS. When
  the ceiling is what limited the result the toast says so — `normalized: -0.9
  dB (was -25.7 LUFS) - peak-limited at -0.1 dBFS`.
- **Gain changes are visible on loud material.** Over-scale columns draw in the
  theme's danger colour, so pushing past the ceiling reads as hot and backing
  off cools it — and doubles as a clip warning.
- Toasts size to their message, and normalize results hold for 2.6 s — 3.2 s
  when peak-limited.

### Remote control
- **A real Bitfocus Companion module** (`companion-module-deckboy/`) replaces the
  Generic TCP/UDP recipe. It polls `STATUS`, so buttons carry **cue tally**,
  transport colour, output health, a connection watchdog and a derived
  countdown, where a Generic connection could only push commands one way. Ships
  actions (transport, cue select/take/goto, seek, levels, output, find, plus a
  raw-command escape hatch), nine feedbacks, ~70 variables and wired presets.
  Parser tests run against a captured Deckboy status reply.
- The module names the localhost-only default in its config screen and in its
  connection error, since that is the usual reason a remote Companion sees
  nothing.

## 2026-07-22 — v0.80.2 (waveforms show gain, normalize feedback, SKIP button)

### Transport
- **New `>|` / `<|` skip buttons** in the timeline transport strip
  (`<| |< << ▶ >> >|`), hotkeys **`.`** / **`,`**, remote commands
  **`SKIP`** / **`SKIPBACK`**. `>|` takes the cue the deck would naturally
  play next — honouring goto targets, shuffle, playlist loop, and the
  missing-media walk — without waiting for the current cue to end
  (end-of-cue auto-advance and skip share one resolver, so they can never
  disagree about what "next" means). `<|` takes the previous playable cue
  (deliberately ignores goto/shuffle — back means the cue above).

### Audio
- **Waveforms grow and shrink with gain.** Every waveform view — inspector
  thumbnail, video-cue audio strip, timeline audio lane, active-cue mini view,
  output-monitor overlay — scales its drawn amplitude by the cue's gain trim,
  live, so nudging gain or landing an R128 normalize visibly changes the
  transients and you can *see* what normalize did.
- **Normalize always answers.** A selection with no file-backed audio toasts
  "normalize: selection has no file-backed audio".

## 2026-07-18 — v0.80.1 (fast on slow drives, a DMG sound pack, vivid Terrarium)

### Performance
- **The UI stays smooth when show media lives on a slow drive.** Cue path
  resolution is memoised per (path, project file), so after the first touch the
  per-frame cost is a map lookup rather than a disk stat per path component —
  which is what large playlists (1,400+ cues) on USB and exFAT drives feel most.
- **The splash appears immediately.** The media-presence scan behind the RELINK
  badge runs on a background thread at boot and on project open, with the
  "N media missing (RELINK)" toast following when it lands.

### Terrarium
- **Vibrant, nature-evoking colours** (exe and in-app pattern — they share the
  core). **Foliage lives in the green band** (spring-to-forest hues, varied
  saturation and value; sage and olive for desert; alien keeps the free wheel),
  **blooms draw from a real meadow distribution** (mostly yellows and whites,
  then violets, reds and oranges, blues uncommon, magenta rare) and render
  accent-dominant so they pop, and **fauna wears earth tones** (russet, tan,
  chestnut) with plumage accents from bird and beetle iridescence — plus a
  1-in-8 full-colour tropical/wetland showoff. Everything runs through a
  slightly stronger `vividify`.
- The big-flower glyph is a round rosette with a stem. Mushrooms wear
  forest-floor caps (cream, tan, fly-agaric red), and ambient flower glyphs take
  poppy, marigold, orchid and thistle tones.

### Game Boy sound pack
- **New DMG-style synth voice** behind the UI sounds: two pulse channels
  locked to the hardware duty cycles, a 15-bit LFSR noise channel, 4-bit
  quantized envelopes, and NR51-style stereo placement.
- **Boot jingle** — a swung chiptune over Coltrane changes (B△7 → D7 →
  G△7 → B♭7 → E♭△7, the Giant Steps major-third cycle) with an original
  melody: YMCK-school cute jazz, golden-changes edition. Plays over the
  startup splash, honours the "little bloops" toggle; ordinary bloops hold
  off until the final chord rings out.
- **New sound effects**: refused-action buzzer (take blocked on missing
  media), a panic dive-and-whoosh for PANIC/ESC, and a dice-roll trill when
  shuffle turns on. Existing bloops are unchanged.

### Cue inspector
- **PIP cues are editable in the live inspector**: source/type editor, corner
  presets (TL/TR/BL/BR), size presets (SM/BIG/70-30), CLEAR OVERLAY, and the
  GEOMETRY/KEY/METADATA sections.
- **Stream cues get an inspector.** SRT/RTMP/RTSP/UDP stream cues and NDI source
  cues have PLAYBACK (fades, per-cue transition + style, audio toggle) and
  METADATA (editable URL / NDI source name, tag, notes, cue id).
- **Audio cues get the AUDIO section**: gain trim, pan, mono downmix, output
  pair, independent a-fades and R128 normalize, the same as a video cue.
- The scroll extent tracks section bottoms, so a section ending in a status line
  is fully reachable; dropdown hit zones are cleared when the selection changes;
  and the empty inspector says "NO CUE SELECTED" once.


## 2026-07-15 — v0.80.0 (settings overhaul, stereo waveforms, panic audio, Test Bars)

### Settings menu
- **One steady dialog size** — every tab shares one envelope sized for the
  busiest tab, so the modal holds still as you move between them.
- **Area of Interest is a resolution, not four percentages** — the AOI card
  edits `X / Y / WIDTH / HEIGHT` in pixels of the output raster (e.g.
  `1920x1080 @ 960,540`), with `-`/`+` nudges (16 px), click-to-type exact
  values, and a `FULL` reset. The card header shows the live rect. Storage is
  still the four edge fractions, so show files are unchanged both ways.
- **Edge blending reads in pixels** too, of the output raster.
- **Tabs size to their labels** — "Video Outputs" reads in full.
- **Display sub-tab tidied** — Toggle Fullscreen and Orientation share one
  row, and the Display & Raster card uses the space it reserves.
- The About tab prints one `v`.

### Timeline / waveforms
- **Stereo audio shows both channels.** The waveform analysis measures true
  stereo-ness sample by sample (side-signal energy) and the L/R split follows
  the *content*: really-stereo material always splits, even for cues saved by
  older versions whose metadata lacks a channel count, while mono material in a
  stereo container gets one full-height lane instead of two identical twins.
  Older cues have their channel count backfilled by a background re-probe on
  project open.

### Playback safety
- **Panic / triple-Esc silences audio.** "Outputs off" stops every deck engine
  and browser cue as well as disarming the video outputs.
- **STOP works on Audio cues**, closing the decode pipe and the stream the same
  way it does for video.

### New
- **Test Bars pattern** — a testsrc2-style motion-diagnostics pattern: six
  saturated bars, a bouncing rainbow diagonal, a dissolving checker patch, a
  sliding grey reference block, and a running clock + frame counter. In the
  pattern picker as "Test Bars (motion diagnostics)"; `pattern://test-bars`
  (aliases: testsrc, testsrc2).
- **Boot console variety** — the splash deals a random hand of 8 lines from a
  40-line pool of sci-fi subsystems each boot, woven between the real init
  values, which always print.
- **`>LIVE` reads in full** — the playlist-header jump button sizes to its
  label, and the marquee dots stop short of it.
- **Right-click a cue → "show in explorer"** — file-backed cues (video, audio,
  image) get a context-menu entry that opens the OS file manager with the media
  file selected (Explorer `/select` on Windows, Finder reveal on macOS,
  containing folder on Linux).
- **Dev flags** — `--import <file>` imports media at launch, skipping the
  startup menu and splash; `--settings [tab[.subtab]]` opens the settings modal
  at boot. Both are for scripted testing and screenshots.

## 2026-07-12 — v0.79.13 (mascot tip: the trim keys)

- The mascot's trim tip names the keys it means: `Ctrl+I` sets the in point and
  `Ctrl+O` the out point; bare `I` is import.

## 2026-07-12 — v0.79.12 (telecined and variable-rate video stay in sync)

- **Audio and video stay locked on telecined (3:2-pulldown) and variable-rate
  material** — classic DVD MPEG-2 anime, some phone clips. Frames are indexed by
  their actual presentation timestamp, so 23.976 fps film soft-pulldowned into a
  29.97 container lands on the right point of the timeline and stays with the
  audio clock. Constant-frame-rate content is unaffected (the indices are
  identical), and a stream with no timestamps falls back to the sequential
  counter.

## 2026-07-12 — v0.79.11 (cue navigation: jump to current + auto-follow)

- **Jump to the current cue** — press `J` or the new `>LIVE` button in the
  playlist header to snap a long playlist back to the cue that is playing (or
  the selection if nothing is live) and centre it. For shows with hundreds of
  cues, this is "where's the show right now?" in one key.
- **Auto-follow** — when the focused deck's live cue changes (take,
  auto-advance, and especially **shuffle**, where the next cue is
  unpredictable), the playlist reveals it — but only if it had scrolled
  off-screen, so the list never yanks while you are looking right at it.

## 2026-07-12 — v0.79.10 (10-bit H.265, and a clean slate on open)

- **10-bit H.265/HEVC (Main 10) renders correctly.** The decoder reads each
  hardware surface's own software format and zero-copies true NV12; P010 and
  anything else takes a CPU transfer and swscale to NV12/RGBA. 8-bit content
  keeps the fast zero-copy path, and `--decode-bench` reports per-frame gpu/cpu
  counts so the split is visible.
- **Opening a show lands on a neutral "nothing live" state**, like a fresh
  launch: the timeline and the preview agree, the operator explicitly takes the
  first cue, and the startup mascot shows. The selected cue is preserved for
  prepping.

## 2026-07-12 — v0.79.9 (mascot tips: bigger, plainer, clearer)

- Mascot tips are **bigger plain text**, drawn full-width under the face rather
  than inside a speech box, in short jargon-free lines.

## 2026-07-12 — v0.79.8 (mascot: per-element Balatro-style drift)

- **The startup face has individual-element life.** On top of the overall hover,
  the whole face gets a small oscillating tilt/rock — placed through a rotation
  about the face centre, so the eyes swing one way as the mouth swings the other
  — and each element, each eye and the mouth, also drifts and breathes on its
  own phase, so they float slightly out of sync. Evokes the springy,
  semi-independent motion of Balatro's card animations, kept subtle.

## 2026-07-12 — v0.79.7 (startup mascot in the empty program monitor)

- **A hovering "terminal face friend" + rotating tips** fill the empty program
  monitor at the start of a session, until the first clip is loaded into it.
  BMO-style: glowing theme-tinted eyes and mouth drawn straight onto the dark
  screen, so the monitor itself is the face. Everything animates smoothly and
  continuously — a 2D floaty hover, eased squish-blinks, a slow look-around
  drift, a smile that breathes, and twinkling stars slowly orbiting the face —
  with no discrete state snaps. A tip line beneath cycles operator hints
  (import, timeline grip, themes, relink, per-cue audio, shortcuts). It retires
  the moment a clip loads and stays gone for the run.
- While the face is up the empty-monitor backdrop is darkened, which also reads
  better on OLED themes than the bright idle fill.

## 2026-07-12 — v0.79.6 (denser timeline filmstrip; consistent thumbnail aspect)

- **9 stills per clip** on the timeline filmstrip (was 5), so it reads as a
  continuous strip and samples the clip densely enough for the now-enlargeable
  timeline lane.
- **Consistent thumbnail aspect.** Each tile is drawn into its own column with
  an aspect-preserving centre-crop, so thumbnails stay undistorted at any lane
  size.

## 2026-07-12 — v0.79.5 (settings on terminal themes)

- **The settings modal reads on terminal themes.** The content frame uses the
  tile fill — a dark frame on OLED themes, unchanged on light ones — and the one
  bare card-body label ("Mappings") routes through `screen_fg`. The rest of the
  modal was already terminal-safe: card titles render bright on dark title
  plates, hints use the audited `screen_ink_soft`, and control buttons sit on
  bright/`mid` fills with dark text.

## 2026-07-12 — v0.79.4 (resizable timeline; dialog readability; sharper thumbnails)

- **Resizable program monitor ↔ timeline split.** A draggable grip in the gap
  under the program monitor lets the operator take height from the preview to
  enlarge the timeline lanes, and give it back. The monitor never shrinks below
  a usable minimum, and the grip appears only when there is room to move.
  Runtime-only, like the existing pane splitters.
- **Sharper timeline filmstrip.** Lane thumbnails are 2x resolution (256×144)
  and rendered with linear filtering, so they stay crisp when the lane is
  enlarged.
- **Terminal-theme dialog readability.** The startup boot splash, the
  startup-mode menu ("New show / Open previous / Open saved"), the keyboard
  shortcuts overlay and the Settings title draw through the on-body ink roles
  (`screen_fg` / `screen_fg_soft` / `screen_ink_soft`), and the boot console
  uses the tile fill — bright text on dark panels in OLED themes, unchanged on
  light ones. The themes were already right; the dialogs simply had not been
  routed through the new roles yet.

## 2026-07-12 — v0.79.3 (terminal/OLED themes; manual rewrite)

- **Terminal / OLED themes.** New theme roles let dark themes render as a
  true-black terminal — OLED-black backgrounds and tiles with bright
  phosphor text and per-theme accents. `screen_fg` (primary on-body ink),
  `screen_fg_soft` (secondary on-tile ink), and `screen_tile` (interactive
  tile fill) each fall back to an existing role (`screen_deep`,
  `screen_dark`, `screen_light`), so **every existing theme is byte-for-
  byte unchanged** and only themes that set the new keys invert.
  - All 25 dark-cased themes reworked as OLED terminals with hue-matched
    accents (green `dark`, red `virtual-boy`/`famicom`/`mario`, amber
    `metroid`, teal `n64`/`wave-race`, violet `gamecube`/`super-famicom`,
    blue `star-fox`/`switch-neon`/`captain-falcon`, magenta `ganon`, pink
    `kirby`/`peach`, gold `zelda`, yellow `pikachu`, lime `piranha-plant`,
    cyan `dolphin`, ice `ice-climber`, white `r-o-b`, grey-green
    `game-and-watch`, and more). Light-cased themes (gameboy, pocket, sp,
    advance, color) are untouched.
  - On-body text sites (inspector row labels, playlist/timeline/header/
    footer chrome) and interactive tiles (cue rows, toolbar and quick-row
    buttons, section headers) draw through the new roles.
  - The inspector body fills with `shell_inner`, so its labels always have a
    legible fill.
  - `tools/audit_theme_contrast.ps1` covers the new roles; all themes pass.
- **MANUAL.md rewritten** from scratch for the current app (SDL3, Windows-
  first, in-process decode) covering cue types, the inspector, outputs and
  geometry, per-cue + multichannel audio, missing-media relink, timecode,
  themes, remote control, soak testing, and the full keyboard reference.

## 2026-07-11 — v0.79.2 (soak harness; shuffle seeding; inspector readability)

- **`--soak [minutes]` long-run stability harness.** Loops the loaded show (or
  synthesized patterns if none) through the real app loop, logging RSS /
  decode-stall / missing-media counters once a minute to stdout and
  `deckboy-soak.log`, then quits. Never persists the looped state into the show
  file. For 24 h+ runs on show hardware. Default 24 h; e.g.
  `Deckboy.exe --soak 720` for a 12 h run.
- **Shuffle deals a different order every launch**, from a `std::mt19937` seeded
  by `std::random_device` with a proper uniform distribution.
- **Cue inspector text is readable on every theme.** The inspector body fills
  with `shell_inner`, the fill the palette contract already assumes for dark
  ink and the contrast audit verifies. Theme colours are unchanged; each console
  keeps its case.

## 2026-07-11 — v0.79.1 (mid-show media loss hardening)

- **A vanished file cannot take a deck down mid-show.** Taking a cue whose media
  is gone (drive pulled, share dropped) is refused with a MEDIA MISSING toast
  before the engine sees it — the output keeps whatever it was showing.
  Auto-advance skips missing cues, with a toast per skip, bounded so an
  all-missing looped playlist cannot spin.
- The take-time check is fresh from disk and updates the MISSING badges and the
  toolbar RELINK count in both directions — a re-mounted drive clears the
  warning on the next take, with no project reload.
- A decode stall whose file turns out to be gone reports "MEDIA LOST (RELINK
  when restored)".

## 2026-07-11 — v0.79.0 (multichannel audio output routing)

- **Cues can route to any output pair of a multichannel interface.** Settings →
  AUDIO OUTPUT has an "Outs" control (2/4/6/8 channels, per deck) that reopens
  the deck's device with that many channels. When more than 2 are open, the cue
  inspector's AUDIO section grows an **outs** row: route each cue's (post
  gain/pan/mono) stereo onto outs 1-2, 3-4, 5-6, or 7-8 — VT to the PA on 1-2,
  click to monitors on 3-4.
- The engine pipeline stays stereo end to end (gain, fades, delay line, VU tap);
  expansion to the device's channel count happens only at the final stream
  write, with silence on the unused outs. All byte↔frame math (A/V master clock,
  backpressure, sync-pop pacing) is channel-aware.
- On a device with fewer physical outs than the opened count, SDL folds the
  extra pairs down — prep on the laptop, route at the venue.
- Remote: `AUDIOOUTS <pair>` (1-based) sets the selected cue's output pair.
- Routing persists per cue and per deck; smoke covers the round trip.

## 2026-07-11 — v0.78.16 (missing media: detection + relink)

- **Deckboy notices when show media is missing.** Every project load scans
  file-backed cues; cues whose files cannot be found get a red MISSING badge in
  the cue list, and a red **RELINK n** button appears in the toolbar next to
  BUNDLE while anything is missing.
- **One-click relink.** RELINK opens a folder picker; Deckboy searches the
  chosen folder recursively for files matching each missing cue's filename and
  repoints the cues — exact file-size match wins when several files share a
  name. Toast reports "relinked X, Y still missing". Clicking RELINK first
  re-checks the disk, so a re-mounted drive clears the warning without any
  picking.
- Smoke test covers the scan + relink round trip.

## 2026-07-11 — v0.78.15 (audio fade envelopes on every waveform)

- **The audio fade envelope is drawn over every waveform** — the timeline's
  audio lane, the program monitor strip, and both cue-panel thumbs and strips.
  Ramps show the EFFECTIVE audio fades (the cue's a-fades when set, otherwise
  the visual fades), anchored to the in/out points — the same resolution the
  audio thread applies, so what you see is what plays.

## 2026-07-11 — v0.78.14 (AUDIO section; deck fader named; independent audio fades)

- **The cue inspector has a collapsible AUDIO section** in both layouts: enable,
  gain, pan, mono, the new audio fades, and normalize — one place for the cue's
  whole audio story, collapsible like GEOMETRY and OVERLAYS.
- **"volume" is renamed "deck fader."** It is the deck's live playback level
  (keyboard +/-) and is not saved with the cue; per-cue trim is gain, in the
  AUDIO section.
- **Independent audio fades**: `a-fade in` / `a-fade out` per cue — follow the
  visual fade (default), none, or explicit seconds. Duck audio early under a
  long video tail, or hold it under a fast visual cut. Applied through the same
  audio-thread fade mirrors; persisted, backward compatible, with a smoke
  round-trip.

## 2026-07-11 — v0.78.13 (readability lives in the themes)

- **Readability is a theme-data contract**, and the v0.78.12 renderer-side guard
  is gone (the owner: fix the data, not the renderer).
  - The 26 affected themes take a light trim tint of their own hue for
    `shell_inner` — which is also what the real consoles look like — tuned until
    every ink role clears its ratio.
  - The `dark` theme is reworked: graphite chrome, light rows, near-black ink.
  - `gamecube`'s selected-row purple and two `screen_light` tones nudged.
- New `tools/audit_theme_contrast.ps1` encodes the exact ink/fill pairs the UI
  draws with, at per-pair minimum ratios — run it after editing any theme; "all
  themes pass" is the contract.
- The startup wordmark stays headline-sized from v0.78.12; the version tag stays
  small on the subtitle line.

## 2026-07-10 — v0.78.12 (theme readability guard; startup headline)

- **Every theme renders readable text.** `rebuildPalette` ends with a
  readability pass: WCAG-style contrast is enforced between the role pairs the
  UI actually draws (light-on-deep 4.5:1, dark-on-light 3:1, accents and
  secondary ink proportionally), nudging only the offending tone's lightness.
  Hue identity survives; compliant themes are untouched.
- The startup prompt's "Deckboy" headline is headline-sized (a new 42 pt
  pixel-font instance, scaling with UI scale).

## 2026-07-10 — v0.78.11 (A/V delay offset)

- **Settings → Audio: "A/V delay" (0–1000 ms, ±10 steps).** Holds ALL deck audio
  back by the set amount, for chains where the display or PA DSP lags the video.
  Applied live in the audio threads through a delay FIFO — no restarts — and the
  A/V master clock anchors to the undelayed timeline so the skew is real at the
  device. VU meters follow the delayed (heard) audio. The Pocket Test sync pop
  runs through the same delay, so the dial-in workflow is: take the Pocket Test
  at the venue, watch the beacon, and nudge the delay until flash and pop land
  together.
- Persisted with the show (`audio_delay_ms`).

## 2026-07-10 — v0.78.10 (per-cue loudness normalize)

- **Normalize loudness, per cue**: one button in the audio section, and the
  `AUDIONORM` remote command, measures the file's EBU R128 integrated loudness
  on a worker thread and sets the gain trim for -16 LUFS playback — a starting
  point you can still nudge. Toasts the measured loudness and the applied trim
  when the analysis lands.

## 2026-07-10 — v0.78.9 (per-cue audio: gain, pan, mono)

- **Every cue with audio has its own audio section:**
  - **Gain**: -24 to +12 dB trim (dB, not multipliers), for media that arrives
    at wildly different loudness.
  - **Pan**: stereo balance with centre snap.
  - **Mono**: downmix toggle for mono sources and mono venue PAs.
- All three apply **live** in the audio thread — no decode restart, VU meters
  follow — and they sit under the audio toggle in the inspector as scrubbable
  quick rows. The Pocket Test sync pop honours them too.
- New remote commands for Companion: `AUDIOGAIN <dB>`, `AUDIOPAN <-1..1>`,
  `AUDIOMONO <0|1>` — the same single write path as the inspector.
- Persisted per cue (appended fields, backward compatible); covered by a new
  `--smoke` round-trip assertion.

## 2026-07-10 — v0.78.8 (Pocket Test: the ball bounces; patterns run at display rate)

- **Pattern rebuilds are locked to the selected display's refresh rate** — or
  the project's explicit output refresh when set — and the card is layered: the
  static layer (grid, bars, grayscale, ramp, PLUGE/detail patches, border,
  crosshair) is cached per raster and memcpy'd, the island scene renders at a
  fixed internal 640x360 (chunky pixel art — nearest sampling is on-brand), and
  only the dynamics draw per frame.
- **The circle is a ball.** It bounces slowly around the whole frame, DVD-logo
  style, at constant velocity — the scene behaves as the full background behind
  the card, and the ball is a porthole revealing whatever it floats over (sky up
  top, beach and characters at the bottom). It doubles as the smooth-motion /
  judder object and a burn-in rover; the A/V sync beacon rides at its 12
  o'clock. The alignment crosshair stays fixed at frame centre.
- Motion is smoother across the board: pattern animation steps once per display
  frame.

## 2026-07-09 — v0.78.7 (Pocket Test: PM5544 edition; pattern policy pass)

- **Pocket Test rebuilt as a proper broadcast test card**: full-frame crosshatch
  grid, 75% colour bars + grayscale staircase across the top, ramp +
  PLUGE/fine-detail/shimmer patch row across the bottom, slow diagonal sweep
  over the grid, Emerald-style ID box (version, raster, clock, scene) — and the
  island scene lives INSIDE the centre circle, Test Card F style, still cycling
  day/sunset/night/storm with the crossfade. The sync beacon sits at the
  circle's 12 o'clock, flashing in the 80 ms pop window; crosshair at dead
  centre.
- **Motion policy: all pattern motion is slow, smooth, and diagonal.**
  Crosshatch drifts one cell per 8 s at 45°, checkerboard one period per 10 s,
  SMPTE bars get a 12 s diagonal sweep line instead of scan lines.
- **Solid colour patterns are static** — a pulsing reference level is a
  contradiction; legacy saves degrade to the static colour.
- **Terrarium is a secret again:** hidden from the pattern pickers until it is
  found. Saved terrarium cues load regardless.

## 2026-07-09 — v0.78.6 (Pocket Test: audible for real, strobe defused)

- **Existing Pocket Test cues become audible on load.** `normalizeProject` flips
  legacy pocket-test cues to audible once, and new ones are created with
  `hasAudio=true` so the audio toggle is visible; a mute made after that
  persists. (`--sync-pop-test` verified the engine synth path end to end against
  a real device: PASS.)
- **The ? block shimmers rather than strobing.** A full-square 30 Hz flash is a
  photosensitivity hazard; it is now a single-pixel checkerboard whose phase
  inverts at 30 Hz — a soft shimmer, while dropped or doubled frames still make
  it freeze or beat visibly.
- New `--sync-pop-test` CLI: runs the real sync-pop path against the default
  audio device and reports PASS/FAIL — first stop for any "test card has no
  audio" question.

## 2026-07-09 — v0.78.5 (Pocket Test field notes: audio, scaling, Emerald)

Four things from the owner's first hands-on with the test card:

- **The sync pop plays.** Pocket Test cues default to audio ON, and the pop
  follows the visual rather than the transport state: if the buoy lamp is
  flashing on the output, it pops — STOP-dark silences it.
- **Scene transitions crossfade** (1.4 s pixel blend) instead of hard palette
  cuts. The card chrome draws over the blend and never fades.
- **All patterns build pixel-mapped to the LIVE program-output raster,**
  re-checked every rebuild, so a display switch mid-show retargets patterns
  automatically (new `OutputSizeProvider` on MediaEngine). A test pattern that
  is not 1:1 with the selected display is lying.
- **A skiff sails the ocean** at constant velocity (4 s per screen) as the
  judder object, over the same 10% fence ticks.
- **Chrome restyled as GBA Pokémon Emerald** (the game, not the colour): white
  windows with dark-grey outline + teal beveled frame band, dark-grey text with
  the signature light drop shadow, red name text and continue-cursor, plus an
  Emerald battle-style status panel — the HP bar drains across each 14 s scene
  (green→yellow→red, doubling as the scene timer) and the EXP bar fills every
  second in step with the buoy pop.


## 2026-07-09 — v0.78.4 (Terrarium goes native)

- **Terrarium is a first-class Deckboy generator source:** `pattern://terrarium`
  runs the full ecosystem simulation in-process — no companion exe, no window
  capture. The sim (`native/extras/terrarium_core.hpp`) ticks at its native
  9 TPS and renders its 200x112 glyph world to a 1600x896 frame with the same
  8x8 font, palettes, seasons, weather, clouds and creatures as the standalone
  app. One world per show: every deck and preview shows THE terrarium, and it
  keeps living across cue reloads. New shows start with a warmed-up ecosystem,
  not bare dirt.
- "Terrarium (living ecosystem)" appears openly in the pattern picker; the
  secret purple cue is now backed by the native pattern.
- The standalone `terrarium.exe` still builds and ships, sharing the same
  simulation core.
- `--smoke` verifies the native terrarium renders a living world.

## 2026-07-09 — v0.78.3 (Pocket Test A/V sync pop)

- **The test card checks audio/video sync.** A buoy bobs in the ocean; its lamp
  flashes for 80 ms at the top of every second, and the deck plays a matching
  1 kHz pop in exactly that window — the first audio a pattern cue has ever
  produced. Watch and listen at the end of the chain: any gap between flash and
  pop is the chain's A/V offset. The pop respects deck volume, master gain and
  cue fades, and drives the VU meters like any decoded audio. Mute it per-cue
  with the cue's audio toggle.

## 2026-07-09 — v0.78.2 (Pocket Test goes diegetic)

- **The test card's instruments are part of the island world** — same
  measurements, exact values, new bodies:
  - Colour bars → a **billboard** on the beach.
  - Grayscale staircase → **stone steps** climbing to the right edge.
  - Banding ramp → a **banner** strung across the sky.
  - Black-crush check → a **cave** whose creature-eyes (2% / 4% on black)
    vanish if the chain crushes blacks.
  - White-clip check → a **cloud** with 98% / 96% lumps.
  - Flicker box → a **flashing ? block** (~30 Hz).
  - Fine-detail patch → a **beach TV playing static** (1px checker + stripes).
  - Judder lane → a **runner** crossing the screen at constant velocity past
    **fence posts at exact 10% spacing**.
  - The Pokémon-style dialog box (version, raster, clock, "A WILD NIGHT
    APPEARED!", blinking continue-cursor) stays as the game-UI layer, with
    chunky Game Boy borders.
- The scene and its rainbow footer are fully visible, with the border,
  safe-area and crosshair guides unchanged.
- Smoke verifies the diegetic values (75% red, 2% cave eyes, 96% cloud lump).

## 2026-07-09 — v0.78.1 (Pocket Test is a real test card)

- **The Pocket Test pattern grew broadcast instrumentation.** The island scene
  stays as the living backdrop; drawn over it (auto-cycling `pocket-test` only —
  `pocket-day/sunset/night/storm` stay clean for use as backgrounds):
  - **1px checkerboard border + corner marks** — pixel mapping and
    crop/overscan: any scaling between Deckboy and the display greys the border
    out instantly.
  - **Dashed 90% / 80% safe-area guides + centre crosshair.**
  - **Instrument strip:** 75% colour bars, 11-step grayscale staircase,
    continuous ramp (banding check), PLUGE-style 0/2/4% black and 100/98/96%
    white patches (black crush / white clip), single-pixel checker + 1px stripe
    patches (fine detail / interlace), ~30 Hz flicker box (dropped/doubled
    frames), and a full-width constant-velocity motion lane with 10% ticks
    (judder).
  - **Info plate** in a built-in 3x5 pixel font: build version, actual raster
    (e.g. 1280x720), running clock, scene name.
- New `--pattern-dump <pattern-id> <out.ppm> [WxH] [t]` CLI renders any pattern
  frame to a PPM for inspection and docs.
- `--smoke` asserts the card instrumentation is present and that the scene
  variants stay clean.

## 2026-07-09 — v0.78.0 (in-process GPU decode — zero-copy video)

- **File-backed Video/Audio cues decode in-process through the FFmpeg libraries
  (libav\*)** rather than two `ffmpeg.exe` subprocess pipes per deck. On Windows,
  video decodes via **d3d11va directly on the program output renderer's D3D11
  device and the frames never touch the CPU**: the output compositor GPU-copies
  each decoded NV12 texture slice into a persistent SDL texture. That removes the
  per-frame GPU→CPU download, the swscale pass, ~41–83 MB/s of pipe transfer and
  the CPU→GPU re-upload — the transport cost that made the fanless Pocket 3
  marginal.
- **What still uses the ffmpeg CLI** (by design): live streams (SRT/NDI), source
  capture, stills/thumbnails, waveform analysis, ffprobe ingest, and stream
  encode-out. Files with rotation metadata also stay on the CLI, which
  autorotates where libav does not.
- **Automatic fallbacks:** effects cues (chroma key / colour controls) and
  non-hw codecs decode in-process to CPU frames — still no subprocess, no pipe —
  and if an in-process open fails for any reason the engine falls straight back
  to the CLI pipe path. `--no-inproc-decode` forces the CLI path for a whole
  run, and building with `-DDECKBOY_INPROC_DECODE=OFF` produces the pure-CLI
  binary.
- **Resilience:** the decoder validates files by priming the first frame before
  committing, tolerates runs of corrupt packets by degrading to EOF, and a
  watchdog reracks the deck dark with a toast if a decode wedges mid-show. A
  corrupt-file test is part of `--smoke`.
- **Playback semantics unchanged:** same frame queue and backpressure, same
  audio-master A/V clock (audio decodes in-process to the same s16/48k stereo
  stream, speed through the same atempo semantics), same seek/EOF/loop
  behaviour.
- **`--decode-bench <file> [seconds] [cli]`** measures decode throughput through
  the real engine path for before/after comparison on a given machine.
- **Cheap wins bundled:** decode threads are capped so they cannot starve the
  render loop on 4-thread CPUs; the always-on no-op scale pass is skipped on
  both paths; and the engine re-uploads a frame to its texture only when it
  changes.
- **Load-bearing hint:** `SDL_HINT_RENDER_DIRECT3D_THREADSAFE=1` is set at init
  — SDL otherwise creates single-threaded D3D11 devices, which cannot be shared
  with a decode thread. Never remove.
- The zip carries the libav\* DLLs plus the FFmpeg licence notice
  (`LICENSE-ffmpeg.txt`).

## 2026-07-08 — v0.77.0 (SDL3 migration)

- **Whole-app migration from SDL 2.32 to SDL 3.4** (same for SDL_ttf), the
  platform groundwork for the in-process GPU decode rewrite, which needs SDL3's
  `SDL_CreateTextureWithProperties` D3D11 texture import for zero-copy video.
- **Immediate wins shipped with the migration:**
  - Per-renderer vsync (`SDL_SetRenderVSync`): program outputs stay vsynced to
    their display; stream-only outputs and the hidden per-deck decode renderers
    run unthrottled.
  - SDL3's Windows DPI handling replaces the `permonitorv2` hint; mixed-DPI
    display topologies are handled natively by SDL.
  - Audio moved to SDL3 audio streams (`SDL_OpenAudioDeviceStream` +
    `SDL_PutAudioStreamData`): each deck keeps its own logical device on the
    chosen output, UI sounds keep a separate logical device on the default
    output, and LTC ingest reads a recording stream that resamples to 48 kHz
    mono S16 in SDL.
- **Compatibility layer** `native/core/sdl_compat.hpp` keeps the codebase's
  integer-rect layout math and SDL2-style display indices working on SDL3.
- **Terrarium** companion exe migrated in the same pass — one SDL3 runtime ships
  in the zip.
- Validated: clean build, `--self-check` ok, `--smoke` 0 failures, live visual
  check (control UI + program output on the HDMI dongle, video + audio playing,
  VU meters live).

## 2026-07-05 — v0.76.31 (Media Encoder, splash system, multi-select, audio hot-swap)

- **Built-in media converter + ENCODER tab.** Cues Deckboy cannot play, or would
  play poorly — 10-bit HEVC, AV1, ProRes, >1080p — are flagged on import; a
  contextual CONVERT button appears in the inspector, and a new **Settings →
  Encoder** tab batch-converts them (H.264 MP4, GPU with libx264 fallback) into a
  portable `_converted/` folder next to the show, then swaps the cue to the copy.
  Original media is never touched.
- **Splash system.** Grayscale scene splashes cycle at boot, tinted to the active
  theme; the default (gameboy) theme boots the branded DECKBOY-wordmark splash.
- **Themes.** The sci-fi colourways take Nintendo names (Luigi, Kirby, Mario,
  Pikachu, Star Fox, Game & Watch, Peach, Ganon, R.O.B., Waluigi, Midna, …) and
  every generated theme's background goes to a near-pure `#050505` black so
  accents pop harder. Theme is saved per show; a New show resets to the default
  skin.
- **Multi-select.** Ctrl+A selects all cues; per-cue row toggles (hold, loop,
  fade, audio) apply to the whole selection.
- **Reset button.** RESET in the SELECTED CUE panel restores a cue's inspector
  settings to deck defaults across the whole selection, leaving media, name and
  metadata intact.
- **Scrolling.** The main deck cue list clamps with a bottom-only rubber-band
  that springs back when the wheel goes idle.
- **Changing audio device keeps playback running** — the device is hot-swapped
  on the running engine.
- **The fullscreen button re-arms in one click** after New Show or a relaunch.

## 2026-07-04 — v0.76.30 (Save/Save As, theme library, output black-on-disable)

- **SAVE prompts for a location.** The toolbar SAVE button and Ctrl+S open a file
  picker and write the project only. BUNDLE is unchanged (export with media);
  Ctrl+Shift+S remains an explicit Save As.
- **24 new themes** under `data/themes/` — dark, high-contrast sci-fi colourways
  (Tritium, Cerenkov, Ion, Amber CRT, Plasma, Halon, Nebula, Infrared, Hazard,
  Cryo, Toxic, Cobalt, Ultraviolet, Quasar, Voidsteel, Crimson Protocol) plus
  Nintendo-flavoured ones (Virtual Boy, Famicom, Super Famicom, N64, GameCube,
  Switch Neon, Hyrule, Metroid). The Appearance dropdown auto-discovers any
  `data/themes/<name>/theme.txt`.
- **Theme is saved with the show.** `Project::theme` persists the chosen
  colourway so it survives restarts (`DECKBOY_THEME` still overrides at boot);
  opening an older theme-less show leaves the current pick untouched.
- **Output clears to black instead of holding a frame.** Disabling an output,
  starting a new show, switching displays or quitting flushes a black frame, so
  the program display — and capture dongles that latch the last signal — do not
  keep the previous session's last picture.
- **The cue list stops at the last cue**, in both the deck list and the overlay
  bin.
- **Inspector fields are type-to-replace.** Opening a value field treats the
  existing value as selected: click → type → Enter.
- **Transport play/pause reflects state** — pause icon while playing, play icon
  while paused.
- **The audio timeline is click-to-seek**, matching the video lane.
- **Dropping a folder recursively imports** every acceptable video, image and
  audio file in name order; audio files import as Audio cues.

## 2026-07-03 — v0.76.29 (Extended boot sequence)

- The splash boot console runs a ~24-line scrolling sequence: real init values
  (theme, fonts, raster/displays, audio buffer, decks/cues, Companion port, NDI
  state, wall-clock/audio-crystal sync) interleaved with critical subsystems —
  flux capacitor (1.21 GW nominal), heisenberg compensators (probably), dilithium
  matrix, gremlin containment field, spline reticulation. Lines scroll
  console-style with randomised boot timing — quick bursts, normal lines, and the
  occasional probe that stalls for half a second — reshuffled every boot and
  always finishing within the ~5s splash (Enter/Esc/click still skips instantly).

## 2026-07-03 — v0.76.28 (Terrarium wired in, richer boot console)

- **The secret grows things.** Terrarium (v0.46, vendored at
  `native/extras/terrarium.cpp`) ships as a bundled companion exe. Entering the
  code launches it windowed and adds a "TERRARIUM (secret)" window-source cue to
  the playlist — TAKE it to put the ecosystem on program.
- **Splash boot console enriched**: the original boot tasks joined by real values
  (version, raster + display count, decks/cues shelved, live Companion port, NDI
  runtime state) and essential hardware checks (rubber chicken calibration),
  revealed line by line.
- Packaging bundles terrarium.exe.

## 2026-07-03 — v0.76.27 (Window picker, boot log, layer fader)

- **Window cues get a real picker**: adding a Window Source cue lists every
  visible window by title, plus "Entire Desktop". Note: gdigrab matches titles
  exactly, so apps that retitle themselves (browsers per tab) need re-picking
  after a title change.
- **Startup boot log**: the startup dialog plays a retro BIOS-style boot log —
  real values (raster, displays, decks/cues, Companion port, NDI runtime)
  interleaved with important diagnostics such as RUBBER CHICKEN ... CALIBRATED,
  revealed line by line.
- **Deck layer fader tidied**: the playlist-footer opacity rail is the multi-deck
  LAYER fader (compositing weight when decks stack on one output). It hides in
  single-deck shows and appears labelled ("LAYER n%", legible deep ink) when a
  second deck exists.

## 2026-07-03 — v0.76.26 (transport verbs, cameras, full-speed in background)

- **Full speed whether or not Deckboy has focus**: it opts out of Windows 11
  EcoQoS and timer-resolution coalescing at startup, so the decode and audio
  timing loops hold their 4ms cadence while you work in another window.
- **Three distinct transport verbs**: PAUSE freezes in place (resumable); RERACK
  returns to the top holding the first frame, ready; STOP darkens the deck AND
  reracks — visual cleared, decode pipes and capture devices released, so a
  stopped webcam turns its light off. PLAY after a dark STOP revives the pipes.
- **Camera cues work on Windows** through a new DirectShow backend. Webcams,
  HDMI capture sticks and Blackmagic WDM devices are all DirectShow video
  devices, so one Camera/Capture cue covers them: adding a camera cue enumerates
  devices and opens a picker, auto-selecting when there is exactly one. TAKE
  starts capture; STOP releases the device. (DirectShow devices are
  exclusive-open, so live preview-while-live-program of the same device needs a
  shared-capture architecture — future work.)
- **Audio lane honesty**: an empty waveform analysis is cached rather than
  respawning ffmpeg, live sources show "live audio", audio-less cues show "no
  audio track", and waveform analysis runs only for file-backed video and audio
  cues.
- **Test pattern cleanup**: the pattern picker lists base types only — motion is
  the toggle's job — and the four Pocket scene variants merged into the one
  cycling Pocket Test (legacy ids still load). The crosshatch grid is anchored to
  the centre crosshair at every raster.
- **Deck opacity fader labelled**: the strip under the playlist reads
  "OPACITY n%".
- **??? **: ↑ ↑ ↓ ↓ ← → ← → B A Start.
- **Dead-control sweep**: automated checks for duplicate action ids, unhandled
  quick actions, buttons without handlers, and struct fields written but never
  consumed — all clean after this release.

## 2026-07-03 — v0.76.25 (the master volume fader drives the audio)

- **The header master volume fader controls audio.** It feeds a per-engine master
  gain applied in the audio thread on top of the per-cue volume, synced from the
  project every tick, so every path to it — the fader, the `MASTERVOL` remote
  command, project load, undo — takes effect on all decks.
- **The fader is draggable** as well as clickable.
- Saved boost levels load at the value they were saved with (range 0–2).
- New splash art (clean cityscape, no baked-in dialog) for both mascots.

## 2026-07-03 — v0.76.24 (settings readability pass)

- **Every settings button fires.** The Video Outputs sub-tabs, the Allow Remote
  toggle and the Stream Key prompt have action ids of their own, so Processing,
  Mascot, UI Scale, Pocket 3 and Identify each do their own job.
- **Settings modal readability redesign** — zero functional change, every control
  in the same place:
  - Pixel-face **SETTINGS** title.
  - Cartridge-shelf tab bars: the active tab is full height and "plugged in" to
    the content frame; inactive tabs sit recessed. Applied to the main tabs and
    the Video Outputs sub-tabs.
  - Every card and section has a dark **label plate** header with light text —
    one strong, scannable anchor per group. One shared helper replaces three
    duplicated card-drawing lambdas.
- **New splash art**: the "cue gremlin cityscape" scene (Deckbot + Deckgirl
  rigging a dot-matrix wall over the skyline) for both mascot choices.

## 2026-07-03 — v0.76.23 (value scrubbing, math shorthand)

- **Drag any inspector value to scrub it.** Click-hold a value cell (width,
  height, offsets, rotation, crop, fades, volume, speed, …) and drag horizontally
  to step it — the same gesture as number scrubbing in AE and Resolve. A plain
  click still opens the exact-entry editor. Works on every inspector quick row
  with -/+ buttons; width/height scrubbing respects the aspect link.
- **Math shorthand in numeric entry**: `x` multiplies and `px` units are ignored,
  so `1920x2`, `960px * 2` and `3840/2` all evaluate. Applies to every numeric
  entry field — they share one expression parser — covered by four new smoke
  checks.

## 2026-07-03 — v0.76.22 (fades off by default, pixel commands)

- **New clips import with fades OFF**, so a freshly imported clip cuts in and out
  cleanly until you turn a fade on per cue (cue-row fade icons or the inspector
  fade rows toggle 0 ↔ the deck's default fade time). Existing show files keep
  their saved deck settings; the FADE IN / FADE OUT pills in Settings → System →
  Show Flow set an existing deck's default for future imports.
- **Fades are covered by the smoke harness**: three end-to-end checks drive a
  real MediaEngine through loadCue → position clock → `currentVisualFadeGain()`
  — the exact gain the output compositor multiplies into the frame alpha — and
  assert the fade-in and fade-out ramps.
- **Remote `WIDTH <px>` / `HEIGHT <px>` commands**: pixel-based cue sizing from
  Companion or OSC, through the same code path as the inspector editors, so the
  aspect link applies. Legacy `SCALE`/`SCALEX`/`SCALEY` factor commands are
  unchanged.

## 2026-07-03 — v0.76.21 (pixel-based geometry editing with aspect link)

- **Cue size is edited in pixels, everywhere.** The GEOMETRY rows are labelled
  `width` / `height`, show the actual rendered output size in px, and clicking a
  value prompts for a pixel value ("Width (px)"). The multiplier still exists
  under the hood, per cue, derived from the cue's base rendered size, but the
  operator never sees it.
- **Aspect-ratio link** (new `link aspect` toggle row, on by default, persisted
  in the show file): changing width scales height proportionally and vice versa —
  including the `-`/`+` nudge buttons and typed exact values. Toggle it off for
  deliberate distortion, like the chain-link in most media software.
  Multi-select edits apply the pixel value per cue, so "make them all 960px
  wide" works.

## 2026-07-02 — v0.76.20 (fullscreen that stays put, display identify)

- **Fullscreen outputs stay on the program screen when you click the control
  window.** `SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0` is set at startup, so a
  playout output holds its display no matter where focus is.
- **Borderless fullscreen by default**: exclusive fullscreen — a real display
  mode switch, with its screen blanking and mixed-DPI placement quirks — is used
  only when the operator explicitly asked for a fixed raster or a specific
  refresh rate. Display-native outputs use borderless fullscreen, which is what
  every modern playout tool does.
- Recovery messages name the trigger, so the log says what moved an output and
  why.


## 2026-07-02 — v0.76.19 (engine and display robustness)

- **The media engine owns its cue.** The engine keeps a private snapshot of the
  loaded cue, so importing media or deleting cues while another cue is live
  leaves the live cue exactly as it was. Live edits to the active cue — fade in
  and out, and the rest — still apply immediately, because the app refreshes the
  snapshot on every project edit.
- **Audio fades are race-free**: the audio decode thread reads fade parameters
  from atomic mirrors, so a gain change lands cleanly.
- **A/V sync — audio is the master clock**: video position re-anchors to the
  audio device clock when the two drift more than about two frames apart, which
  holds lip-sync over long-form clips and keeps variable-frame-rate sources
  watchable.
- **Displays are matched by name, not number** (new `displayName` field in the
  show file). After a hot-plug, a reboot or a driver re-enumeration shuffles
  display numbers, an output re-attaches to the monitor it was aimed at, so
  program output stays off the operator's screen.
- **Display hot-plug respects the Esc safety contract**: connecting or
  disconnecting a monitor leaves outputs the operator escaped to windowed
  windowed, and healing goes through the per-output recovery path.
- **HyperDeck server hardening**: transport and clips replies are served from a
  structured main-thread snapshot, so a cue named "playing" is just a cue name.

UI polish, from the operator's screenshot notes:

- Empty timeline lanes draw no stray grid lines under the "take or select a
  cue..." placeholder — time graduations draw only over an actual timeline, in
  theme ink.
- Cue inspector rows share one label-column width, so value boxes line up down
  the panel.
- Playlist footer ("LOOP | ORDER") and the inspector empty-state panel derive
  text heights from the live font, so descenders survive at scaled and HiDPI
  font sizes.
- Cue-row action icons are solid shapes: filled rising/falling wedges for fade
  in and out, thicker pause bars, a filled speaker horn — legible at 20px.

## 2026-06-20 — v0.76.18 (new app/taskbar icon)

- **New Deckboy app icon**: a pixel-art handheld-cartridge icon with a green
  play triangle, embedded in `Deckboy.exe` and applied to the live windows.
  Source art at `art/windows/icons/deckboy_app.ico` (multi-resolution 16→256 px,
  regenerable from `deckboy_app_master.png`). `art/windows/deckboy.rc.in` plus a
  `configure_file` step generate the resource at build time, so it survives a
  fresh CMake reconfigure and feeds both the Explorer/Alt-Tab/pinned-taskbar
  icon and the runtime window icon.

## 2026-06-20 — v0.76.17 (stills hold, high-refresh loop)

- **Still cues hold at the end of their duration** at full opacity — an
  Image, Pattern, Browser or Composite cue set to pause on its last frame stays
  on screen. Pausing mid-cue keeps its fade state where it was, too.
- **Still-type cues default to no fade-out.** A static graphic that holds should
  not dip to black at the end; a fade-out is still honoured if you turn it on
  per cue.
- **High-refresh render loop**: the main loop's floor is 240 Hz, so stills and
  transitions render at the monitor's full native rate (144/165/240 Hz) through
  vsync.

## 2026-06-17 — v0.76.16 (resizable control window, F11 fullscreen, mascot dropdown)

- **The control window is resizable** (minimum 1500×900). The per-frame layout
  reflow already adapts to the live window size, so dragging an edge or
  maximising spreads the UI into the space.
- **F11 toggles fullscreen** on the control window, with a
  "fullscreen"/"windowed" toast.
- **The mascot picker is a dropdown**, matching the Theme and UI Scale
  selectors.

## 2026-06-17 — v0.76.15 (layout chrome scales, dependency prompts, touch mode)

- **Layout chrome scales with `uiScale`.** Header height, button height, panel
  padding, gaps and the spacing unit are recomputed whenever UI scale changes,
  so panels, buttons and the bottom bar grow alongside the fonts instead of
  leaving them stranded in 1× chrome.
- **The dependency prompt fires from every operator-initiated path** — UI
  toggles, hotkeys, and the OSC/Companion `NDI` and `DECKLINK` commands all
  prompt when a runtime is missing. Loading a project stays silent, so opening a
  show authored elsewhere does not surprise you with prompts.
- **Touch interaction mode**: a new `Project::interactionMode` ("mouse" default,
  "touch"). The Pocket 3 preset flips it alongside the 2.0× scale, and in touch
  mode the splitter and context-menu hover highlights are suppressed — a tap
  cannot hover, and a sticky highlight after a drag-release is the worst of it.

## 2026-06-17 — v0.76.14 (mascot swap, dependency prompts, UI scale, portable zip)

- **Swappable splash mascot**: Settings → APPEARANCE has a `MASCOT` pill that
  flips between **Deckbot** and **Deckgirl**, saved per project. The splash
  loader tries `.mp4` and `.gif` before `.png`, so an animated mascot is a
  drop-in asset replacement with no code change.
- **Runtime dependency prompt**: Deckboy detects when you enable a backend whose
  runtime is not installed on this machine and offers a one-tap link to the
  official vendor download page. Lazy by design — nothing fires at startup.
  Wired for the NDI Runtime (NDI Output toggle and the `N` hotkey), Blackmagic
  Desktop Video (DeckLink Output toggle) and the Microsoft WebView2 Runtime
  (Browser cue creation, Windows). No auto-downloaded installers.
- **UI scale factor**: Settings → APPEARANCE has a `UI SCALE` dropdown (1.00×
  default, 1.25×, 1.50×, 2.00×). Every TTF face is reopened at `base × scale`
  point size, so text stays crisp on 4K and Pocket 3 displays.
- **Pocket 3 / Touch preset**: an APPEARANCE pill that flips UI scale to 2.0× and
  back — a single-click ergonomic profile for 8" 1920×1200 handhelds.
- **Portable Windows zip**: `tools\package_windows.ps1` produces
  `dist\Deckboy-<VERSION>-windows-x64.zip` with the exe, every co-located vcpkg
  DLL, ffmpeg + ffprobe, the MSVC C++ runtime DLLs (app-local, so no Visual C++
  Redistributable install is needed), `data/`, `LICENSE`, and a `README.txt`
  describing the optional NDI / DeckLink / WebView2 installs. Unzip and run.

## 2026-06-16 — v0.76.13 (GPU video upload — NV12 fast path)

- **Live video decode uses NV12 by default** — about 62% less pipe bandwidth and
  CPU memory traffic than the RGBA path. FFmpeg writes a planar Y plus
  interleaved UV plane straight into the frame buffer; SDL uploads it through
  `SDL_UpdateNVTexture` and the GPU samples the YUV→RGB conversion at blit time.
  Cues with chroma key or colour controls decode as RGBA so the CPU effects path
  keeps working unchanged.
- **The software scaler runs fast_bilinear** in the live video pipeline, which
  costs a third of bicubic and is indistinguishable on moving video at deck
  output sizes. Stills and thumbnails keep their `flags=neighbor` path.
- `DecodedFrame` carries a `FramePixelFormat` tag, and all six upload sites
  recreate their texture on a format or size change and pick the right upload
  call. New helper `syncFrameTexture()` in `render/texture_helpers.hpp`.
- **Known trade-off**: toggling chroma key or colour controls on a cue already
  decoded as NV12 takes effect at the next TAKE. A cue that needs
  live-toggleable effects should have at least one effect parameter enabled
  before TAKE, so the decoder picks RGBA up front.

## 2026-04-24 — v0.76.12 (full networking stack on Windows)

- **Every network protocol works on Windows**, through Winsock2:
  - **Companion control** (TCP+UDP): Bitfocus Companion integration, OSC message
    parsing, subscriber tracking, feedback broadcasting
  - **OSC Query server** (TCP HTTP): endpoint discovery and state queries
  - **ATEM tally bridge** (UDP): ATEM tally packets as transport triggers
  - **Art-Net DMX bridge** (UDP): DMX packets for lighting trigger integration
  - **NMC sync** (UDP): Network Master Clock, both input (play/stop/seek) and
    output (broadcasting transport state to followers)
  - **NDI trigger bridge**: NDI metadata frames as cue triggers
  - **HyperDeck server** (TCP): Blackmagic HyperDeck protocol emulation for
    hardware controllers
- **LTC ingest works on Windows**: libltc 1.3.2 built from source as `ltc.dll`,
  found by the dynamic loader alongside the Linux and macOS candidates, with SDL
  audio capture providing the PCM input on every platform.
- **What remains platform-gated**: ALSA MIDI input is Linux-only, and is covered
  elsewhere by the cross-platform RtMidi backend.

## 2026-04-24 — v0.76.11 (DeckLink UI, NDI on Windows, Spout output, RtMidi)

- **DeckLink settings UI** in the Video Outputs tab: enable toggle, device
  dropdown (connected Blackmagic devices with SDI/HDMI/4K capability labels),
  output mode dropdown (720p through 4K at all standard frame rates), and a
  10-bit toggle. Changing device or mode re-initialises the output with the new
  configuration.
- **NDI output works on Windows**, audio sends included.
- **NDI 6 SDK search paths** added to CMake for Windows and Linux.
- **Spout2 output backend** (Windows): interprocess texture sharing through the
  SpoutLibrary DLL, so a Deckboy sender is visible to any Spout-capable receiver
  — OBS, Resolume, TouchDesigner. Includes the catalog entry with a
  `DECKBOY_HAS_SPOUT` build gate, route planning, lazy sender lifecycle,
  settings UI (enable toggle + sender name editor) and project serialisation.
  Installed through vcpkg (`spout2:x64-windows`), auto-detected by CMake.
- **Stream output works on Windows**: ffmpeg SRT/RTMP egress reads raw video
  from `pipe:0`, through a new `StdioMode::Pipe` on the cross-platform
  `ChildProcess` API.
- **Live source capture works on Windows**: WindowSource cues capture desktop
  regions through ffmpeg's gdigrab.
- **RtMidi integration** (cross-platform): real hardware MIDI input through
  RtMidi 6.0.0 — device enumeration, port open/close, and message polling that
  dispatches CC, NoteOn, NoteOff and ProgramChange, including the
  NoteOn-with-velocity-0 → NoteOff conversion the MIDI spec asks for.
  Auto-detected from vcpkg; statically linked on Windows, so no extra DLL.

## 2026-04-11 — v0.76.10 (audio-lane loading animation)

- **The audio timeline lane has its own loading animation** while a cue's
  waveform peaks are still being computed: the same widget frame, dimming
  overlay and pulsing LOADING label as the video lane's filmstrip, with an
  animated 9-bar EQ meter in place of the filmstrip cells. Each bar's height is
  driven by an offset-per-bar sine phase with a squared envelope, so the motion
  feels musical rather than mechanical, peak bars get a brighter fill, and each
  bar has a 2px highlight cap.


## 2026-04-11 — v0.76.9 (settings text that fits at every font size)

- **Every settings card derives its row positions from the live font.** SAFETY /
  TIMECODE, SHOW FLOW, CUE TOOLS, PLAYLIST PREFERENCES, APPEARANCE, AUDIO
  OUTPUT, MIDI CONTROL, REMOTE CONTROL, OSC QUERY/FEEDBACK, DISCOVERY / NOTES,
  INTEGRATION ADAPTERS and the About tab's RUNTIME rows all reflow cleanly at
  1×, at retina sizes and at every UI scale between.
- The empty-deck hint in the control window ("I import / B browser / P pattern")
  and the Edge Blending and Area of Interest sub-panels follow the same rule.
- New helpers in `render/layout.hpp`: `textLineHeight(font)`,
  `rowYBelowLabel(labelY, font, gap)`, `rowYBelowLines(startY, font, lines,
  gap)`.
- **`drawTextSafe` / `drawCenteredTextSafe` centre on the rect midline and
  expand their clip to fit the text**, so descenders render in full even in a
  rect shorter than the font — the modal title, the AOI RESET button and the
  thin rects in the inspector thumbnail area all read completely.

## 2026-04-11 — v0.76.8 (correct probe of audio-first mp4s, duplicate cues allowed)

- **A video clip imports as a video cue** whichever order its streams appear in,
  so an mp4 that carries audio before video is no longer taken for an audio-only
  file.
- **The same media can be added to a deck twice** — the same clip as two cues
  with different in/out points, or a playlist that returns to an asset. The
  toast and the cue list make a duplicate obvious; library-level dedup belongs
  in the media library, not the cue list.

## 2026-04-11 — v0.76.7 (video cues fade in playlists)

- **Video cues in a playlist honour `fadeInSeconds` / `fadeOutSeconds`**, so a
  playlist mixing browser and video cues fades both the same way. The per-cue
  fade-in is the visible transition on playlist advance; a looping cue still
  suppresses the ramp on each cycle, which is what you want.
- **Audio fade ramps apply on auto-advancing video cues too**, so a cue with a
  fade-out ramps to silence at end-of-cue rather than stopping flat.

## 2026-04-10 — v0.76.6 (audio thread hardening, camera factory per platform)

- **The audio decoder thread copies whole samples**, aligning a short read from
  the FFmpeg pipe down to an even byte count and picking the odd byte up on the
  next read.
- **The camera capture factory answers per platform**:
  `createCameraCaptureBackend()` returns the v4l2 backend on Linux and the
  correct scaffold id elsewhere (`mediafoundation` / `avfoundation`), matching
  what the capture backend catalog advertises.
- Linux `ChildProcess::stop` closes the read end before the kill, so a reader is
  always unblocked, whether or not the process had already been reaped.
- Smoke asserts `supported && backendId == "gdigrab"` on Windows, which is what
  window capture has done since v0.76.1.

## 2026-04-07 — v0.76.5 (rapid takes, and TAKE without a pause)

- **Rapid TAKEs of video clips are safe.** Decoder shutdown kills the process
  first, so the read end returns EOF cleanly, then joins the threads, then
  releases the handle — the same order for the image thread and the thumbnail
  and timeline-strip threads.
- **TAKE is instant.** The dimensions probed at ingest are reused, so there is
  no synchronous ffprobe on the main thread at take time.

## 2026-04-06 — v0.76.4 (fade in/out on the output path)

- **Cue fade-in and fade-out apply on every output.** `MediaEngine` exposes
  `currentVisualFadeGain()` and the compositor multiplies it into the bridge
  texture alpha — `alpha = deckOpacity × fadeGain × 255` — for every cue type:
  browser, video, image, pattern and source capture.

## 2026-04-04 — v0.76.3 (GPU decode, TSL/tally, SRT/NDI input, audio buffer tuning)

- **GPU hardware decode**: video decode passes `-hwaccel auto`, so DXVA2/D3D11VA
  on Windows and NVDEC/VAAPI on Linux are used when available and fall back to
  software automatically — no configuration.
- **TSL/Tally protocol**: a UDP tally sender on port 5800 (TSL 3.1) sends
  program and preview tally state to tally hardware as deck active status
  changes. Settings → Network → Tally / TSL, with configurable port and target,
  on every platform.
- **SRT input**: a Video cue accepts an `srt://host:port` URL as its media path,
  handled natively by FFmpeg (which needs `--enable-libsrt`). Probe and seek are
  skipped for live URLs.
- **Live stream support** (RTMP/RTSP/UDP): the same live-stream handling for
  `rtmp://`, `rtsp://` and `udp://` — no probe delay, no injected seek.
- **NDI input via ffmpeg**: a Video cue accepts `ndi://NDI_Source_Name`, decoded
  by ffmpeg's `libndi_newtek` input device, with Windows NDI SDK library
  candidates added.
- **Audio buffer size tuning**: the SDL audio buffer reads from
  `Project::audioBufferSamples` (256/512/1024/2048, default 1024) — smaller for
  latency, larger for stability.

## 2026-04-04 — v0.76.2 (browser cue duration, Area of Interest crop)

- **A browser cue keeps its still duration**, so fade-out and auto-advance work
  for browser cues in a playlist.
- **Area of Interest (AOI) output crop**: per-output fractional edge crop
  (left/right/top/bottom, 0–1, 5% step) applied at the compositor→window blit,
  controlled from Settings → Output → Area of Interest, with a reset button and
  persistence in the show file.

## 2026-04-01 — v0.76.1 (browser cues on Windows, SOURCE menu, filmstrip)

- **Browser cues work natively on Windows** with no separate Chromium download:
  Deckboy finds the system Edge (or Chrome) installation, launches it in
  `--app=` mode at the requested size, and pipes frames into the deck engine
  through ffmpeg's `gdigrab` desktop-region capture — the same pipeline as the
  Linux Xvfb approach, with Edge and GDI.
- **"Browser / URL Cue" lives in the SOURCE dropdown**, alongside Window, Camera
  and Syphon sources, which keeps the three-group button balance.
- **Button labels sit centred**: the centring helpers snap the full button rect
  before computing the vertical centre, matching the painted background.
- **"Select or import a cue" keeps its descenders**, at an inspector
  empty-state rect height that snaps exactly.
- **Timeline filmstrip grid lines stay inside the filmstrip**, through per-draw
  clip rects.

## 2026-03-31 — v0.76.0 (audio waveform, VU meter, text overflow, version flow)

- **A clip that failed to analyse retries.** Empty waveform results are not
  cached, so a clip that could not be decoded on one run is analysed on the
  next.
- **The VU meter returns to zero** when the focused deck stops playing.
- **Text stays inside its box, everywhere.** `drawTextSafe` and
  `drawCenteredTextSafe` clip to the same snapped rect `drawUIPanel` paints, so
  the LIVE/STREAM/DECODE/WARP badges and the dB scale labels keep to their own
  separators.
- **Build-time version generation**: a change to `VERSION` takes effect on the
  next `cmake --build`, with no reconfigure.
- **Project root detection hardened**: the walk-up skips known build
  subdirectory names before checking for a `data/` directory, so the app anchors
  to the real root rather than to `build/windows/Release/data/`.

## 2026-03-29 (the previous show is the one you were in)

- **"Open Previous Show" opens the last project you actually used.** Deckboy
  remembers the last opened or saved `.deckboy` path in
  `data/last_project.txt` and offers that at startup.
- The saved default show's cue paths were repaired, so its clips load again.

## 2026-03-29 (cue inspector text stays in its box)

- **Inspector text clips to its own control box while scrolling**, against the
  real control bounds as well as the scroll viewport, so a partially scrolled
  row keeps its text inside its row.
- **Inspector spacing is roomier**: taller rows, more gap between rows and
  sections, and more breathing room around label, value and edit controls.

## 2026-03-29 (timeline scrubbing, and a roomier cue list)

- **The timeline supports held-drag scrubbing.** Clicking the progress bar still
  seeks immediately; holding and dragging updates the seek target continuously
  until you let go, which is what the tooltip always said.
- **Cue rows and inspector controls have more vertical headroom**, with the cue
  name in the smaller face so long names sit comfortably.

## 2026-03-29 (the Deckboy icon on every window)

- **The running Windows app applies the Deckboy icon to its SDL windows**, big
  and small, from the embedded `IDI_DECKBOY_APP_ICON` resource — so the
  taskbar and titlebar match the executable's branding whatever Explorer has
  cached.

## 2026-03-29 (the program monitor owns its area)

- **The program monitor fills the monitor area** at every layout width, with no
  second preview strip appearing — and nothing is decoded for one in the
  background either.
- **Program header telemetry respects tight layouts**: the output, decode and
  stream badges shrink, and drop count if they must, so header chrome never
  collides with the `WARP` control.

## 2026-03-29 (background analysis stays in the background)

- **A failed media probe is a toast**, not something that reaches the main loop;
  the same for a waveform analysis that cannot complete.
- Windows waveform analysis drains ffmpeg's PCM output, so it produces peaks.

## 2026-03-29 (a seek holds the picture)

- **A jump keeps the last visible frame until the new one is ready**, so
  transport jumps, progress-bar seeks and `goto -20s/-30s` no longer flash
  black. An explicit hard clear is still available through `seek(..., true)`.

## 2026-03-29 (calmer output display switching)

- **Switching an output's display is one clean move** on multi-monitor setups: a
  single fullscreen re-entry path, no resizing while SDL still reports the
  window fullscreen, and an enabled window output rebuilds its runtime on the
  newly chosen display. The rebuild is queued onto the next update tick, so
  recovery and fullscreen logic do not fight the same monitor move, and teardown
  clears pending transition flags so a fresh output starts clean.

## 2026-03-29 (Windows launches like a normal app)

- **`Deckboy.exe` is a Win32 GUI executable**, so launching it opens the UI and
  no blank terminal. `--version`, `--self-check` and `--smoke` route through the
  same app logic behind a `WinMain` wrapper.

## 2026-03-29 (keyboard focus hygiene)

- **Transport hotkeys belong to the main control window.** An output or
  secondary window receiving a key event does not fire them, though output
  windows still pass `Esc` through so fullscreen escape and emergency disarm
  work from the big screen.
- **Opening an inline editor raises the control window** before starting text
  input, so `Ctrl+G` captures the cue token you type.

## 2026-03-28 (the app is called Deckboy)

- **The built app is `Deckboy` / `Deckboy.exe`**, and GitHub Actions artifacts
  publish as `Deckboy-linux-*`, `Deckboy-macos-*` and `Deckboy-windows-*`. The
  startup dialog, splash overlay and About/settings menu render `Deckboy` with
  the live generated version.

## 2026-03-28 (version flow groundwork)

- **One repo-wide version source of truth**: a top-level `VERSION` file, read by
  CMake, with `deckboy_version.hpp` generated at configure time so native code
  and build metadata stay aligned.
- **`Deckboy --version`** prints the SemVer tag-style version, and
  `--self-check` includes it near the top.
- **GitHub Actions understands release tags**: CI validates that a pushed `v*`
  tag matches the repo `VERSION`, and the Linux, macOS and Windows builds share
  that guard.

## 2026-03-28 (playback fixes + browser backend seam)

- **The timeline filmstrip's last tile samples inside the clip**, and a fully
  built cached strip replaces the placeholder rather than leaving it on screen.
- **Animated engineering patterns update on every output path**: a regenerated
  frame publishes with a fresh index, so motion-enabled patterns and Pocket Test
  variants move.
- **Crosshatch and checkerboard motion loops wrap cleanly**, and
  `crosshatch-motion` drifts on one axis in both preview and output.
- **A new pattern cue defaults to `hold`**, so it is live-safe rather than
  inheriting playlist auto-advance; static checkerboard frames are smoke-tested
  as opaque.
- **The audio lane shows an animated loading state** like the video filmstrip,
  and the focused-deck VU meter decays to zero when a cue stops feeding audio.
- **Browser cue runtime has a real backend seam**: the browser lifecycle and
  phased startup live behind `native/platform/browser.*` rather than inside
  `App`.


## 2026-03-26 (DeckLink SDI output + SRT subtitles)

- **DeckLink SDI output, end to end** (feature-gated by `DECKBOY_HAS_DECKLINK`):
  22 modes — 720p, 1080i, 1080p and 2160p at all standard frame rates — with
  per-output device id, mode and 10-bit selection, saved with the show. The
  compositor captures the egress frame and `DeckLinkOutput::sendFrame()` handles
  the BGRA→UYVY conversion, alongside stream and NDI on the same output.
  Companion commands: `DECKLINK ON/OFF/TOGGLE`, `DECKLINK DEVICE <id>`,
  `DECKLINK MODE <token>`, `DECKLINK 10BIT ON/OFF/TOGGLE`. A stub fallback
  compiles on every platform when the SDK is absent.
- **SRT subtitle rendering**: `core/subtitle_parser.hpp` parses an external
  `.srt`, and an embedded subtitle stream is extracted with
  `ffmpeg -map <streamId> -f srt`. `probeCue()` auto-detects an embedded track
  (`0:s:0`), the track is cached on take, and the output window renders the text
  centred at the bottom with a drop shadow over a semi-transparent bar.
  Companion commands: `SUBTITLE ON/OFF/TOGGLE`, `SUBTITLE FILE <path>`,
  `SUBTITLE CLEAR`. The cue's subtitle path, stream id and enable flag are saved
  with the show.

## 2026-03-26 (Companion module + status snapshot)

- **A Bitfocus Companion module** in `companion/companion-module-deckboy/`:
  - `connection.js` — a TCP client polling `STATUS JSON` on a configurable
    interval
  - `actions.js` — 35 actions covering transport, cue navigation, deck focus,
    seek, volume, blackout, transitions, cue properties, overlays, outputs, NDI,
    streaming, timecode, panic, shuffle, fullscreen, and raw command passthrough
  - `feedbacks.js` — 13 boolean feedbacks: playing/paused/stopped per deck,
    blackout, output health, NDI enabled/receivers, stream enabled, output
    enabled, test card, deck focused
  - `variables.js` — 50+ variables: global state, focused deck
    transport/cue/position/volume/timecode, per-deck (1-4) status, focused
    output health/NDI/stream/FPS
  - `presets.js` — 30+ drag-and-drop button presets by category (Transport, Cue
    Navigation, Deck Selection, Master, Output, Status, Transitions)
- **The status snapshot carries more**: `masterDimmer` (0-100), `blackout`
  (bool) and `masterVolume` (0-200) in both the JSON and text forms, which is
  what powers the blackout feedback and the dimmer and volume variables.

## 2026-03-25 (splash redesign, animation, and readable settings)

- **Splash screen redesign**: full-bleed background art through the new
  `drawUiImageCover()`, with the original framed card (760×430) preserved on top
  over a semi-transparent backing. Boot console and sparkle animations retained.
- **Program monitor animations**: corner sparkles while playing, idle floating
  particles, and a playhead sparkle on the timeline during playback.
- **Bottom bar animations**: a pulsing red border glow on the blackout button
  while it is active, and header sparkles across the full available space.
- **Inspector animations**: an activity sparkle in the header when a cue is
  selected, doubled while playing. Path and URL displays wrap to two lines
  inside their clip.
- **VIDEO and AUDIO timeline labels read in full.**
- **All five settings tabs redesigned for readability**:
  - Every card subtitle has clearance before the first interactive element.
  - System tab: "FI"→"FADE IN", "FO"→"FADE OUT", "AUD"→"AUDIO",
    "P-BEGIN"→"PAUSE BEGIN", "P-END"→"PAUSE END", "NEXT X"→"NEXT TRANSITION";
    toggle rows go from four per row to two; "PLAYLIST PREFS"→"PLAYLIST
    PREFERENCES".
  - Network tab: "NDI TRIG"→"NDI TRIGGER".
  - Video Outputs tab: a two-column rewrite — the dropdowns self-describe
    ("Hardware Display: …", "Resolution: …"), NDI source name becomes a
    dropdown, edge blend labels sit above their buttons, and fullscreen and
    orientation are full-width rows.
  - About tab: runtime info renders in full, with dynamic paths ellipsized.
  - Audio tab: long info text ellipsizes rather than overflowing its card.
- Industry abbreviations (TC, SFX, NDI, OSC) are kept; only truncated words were
  expanded.

## 2026-03-16 (visual overhaul: beveled panels, scanlines, generation themes)

- **The palette is built once and used everywhere**, converted from the theme
  constants at startup and on every theme load, so all ~774 colour references
  draw the theme's own colours.
- **A differentiated default DMG palette**: shell colours (grey-green plastic
  `C4CFA1`, `A5B088`, `5A6B4A`) distinct from the LCD screen colours (classic
  `9BBC0F`/`8BAC0F`/`306230`/`0F380F`), with `inkSoft`, `buttonBezel` and
  `deleteBezel` all their own values.
- **Beveled panel rendering**: panels draw a highlight on the top-left and a
  shadow on the bottom-right, detecting raised versus inset automatically — an
  accent brighter than the fill reads as raised, darker as recessed. No
  call-site changes.
- **Scanline overlay**: a 1×4 procedural texture drawn before each present, for
  a CRT / dot-matrix feel. Controlled by `pal.scanlineAlpha` (0 disables); theme
  key `scanline_alpha`.
- **Game Boy generation themes**: `pocket` (silver-grey LCD), `color` (vivid
  green + indigo shell), `advance` (washed-out + indigo) and `sp` (bright
  backlit + metallic silver).

## 2026-03-16 (responsive cue rows and imports)

- **Importing a large batch of media stays responsive.** `importPaths()` creates
  placeholder cues immediately, usable in the UI, and probes them on background
  threads; a row shows "probing..." until its metadata lands.
- **Cue row display strings are cached** per cue — token, kind label, ellipsized
  name and metadata line — self-invalidating by input comparison, so the text
  measurement loop does not run per row per frame.
- **Formatting allocates nothing per frame**: `formatSeconds()`,
  `formatTimecode()` and the inspector's float formatting use `snprintf`.
- **One waveform lookup helper** (`getWaveformPeaks(path, pending)`) shared by
  every render path.
- The `Cue` struct is grouped by alignment, saving ~40 bytes of padding per cue;
  serialisation is by explicit field name, so the show format is unaffected.

## 2026-03-15 (steadier remote control, one palette, one inspector)

- **Companion/OSC TCP client handling is thread-safe**: the client list is
  snapshotted for `select()` with the lock released while it blocks, then
  re-locked for recv/accept/close, with the timeout at 100ms for
  responsiveness.
- **A pre-converted colour palette** (`Palette pal` + `rebuildPalette()`) is
  built once at startup and after each theme load, and used by every draw site.
- **One inspector implementation.** The docked and floating panels share 15
  helpers parameterised by an `InspectorCtx` (inset, fonts, ellipsize, gaps),
  so a change to a row type reaches both.
- `docs/AUDIT_ROADMAP.md` maps the remaining optimisation and cleanup work.

## 2026-03-15 (one subprocess API across platforms)

- `native/core/subprocess.hpp/cpp` becomes a unified cross-platform API: a
  `SpawnOptions` struct with a `StdioMode` enum (Inherit, Null, Pipe, Merge) and
  a detached mode, one `spawnProcess()` entry point, and convenience presets
  (`pipedStdout()`, `detachedSilent()`, `captureAll()`). The legacy wrappers
  remain as thin forwards, so every existing call site works unchanged.

## 2026-03-15 (Deckboy 0.60 audit + cleanup pass)

- **Every text prompt uses the inline editor**: `Ctrl+G` cue goto, cue
  renumbering (from settings and `Ctrl+Shift+R`), MIDI port, Companion/OSC port,
  OSC Query port, OSC feedback rate, Art-Net port, canvas size, and browser-cue
  creation.
- **Deck auto-advance is gone from the model as well as the UI**, so the
  keyboard and the interface agree about what a deck does. Old `auto_advance`
  fields still load harmlessly.
- **The unfinished overlay/scene authoring surfaces are parked**: `LOWER 3RD`,
  `SCENE` and `PIP` come out of the bottom `MEDIA` group, and `G`, `M`,
  `Shift+P` and the remote add commands say so. Existing `Lower Third`, `PIP`
  and `Composite` cues still load, inspect, save and render.
- Portability conclusion for this pass: cross-platform work is realistic without
  an architecture rewrite; the remaining blockers are the Unix-only
  subprocess/FIFO paths, the Linux-only browser and source capture backends, and
  the Windows/macOS runtime backends.

## 2026-03-15 (Deckboy 0.60 branch, first composite cue cut)

- A real `Composite` cue kind in the project model and the save format, with
  `SCENE` in the bottom `MEDIA` group and `M` to add one.
- A composite cue stores its layout preset (`2-UP`, `70/30`, `QUAD`), a
  per-slot source spec, a per-scene audio slot selection and a scene background
  colour.
- A dedicated composite inspector: `PLAYBACK` for hold, duration, fades and end
  action; `SCENE` for layout presets, slot source entry and audio-slot cycling;
  `OVERLAYS` for attached `Lower Third` / `PIP` bin items.
- The first rendering pass is deliberately bounded: taking a `Composite` cue
  shows an authored scene placeholder in Program, Preview and Output. Live slot
  rendering from media, browser and source runtimes is the next phase.

## 2026-03-15 (Composite cue architecture spec)

- An engineering spec for the `Composite` cue in
  [docs/COMPOSITE_CUE_SPEC.md](docs/COMPOSITE_CUE_SPEC.md), recommending
  `Composite` over a generic live layer system for Deckboy's
  single-primary-cue architecture, and covering the cue data model and slot
  model, runtime and render integration, audio and transport rules, inspector
  and monitor-editing behaviour, and rollout phases with explicit non-goals.

## 2026-03-13 (copy and paste cue settings, longer default fades)

- **Settings copy/paste for cue work**: `Ctrl+C` copies the selected cue's
  playback, geometry and key settings, `Ctrl+V` pastes them onto the selection
  while each cue keeps its own media, name and identity. The inspector summary
  card carries visible `COPY` / `PASTE` buttons.
- **Warp copy/paste**: `Ctrl+Shift+C` copies the focused deck's warp and blend
  state, `Ctrl+Shift+V` pastes it back, with `COPY` / `PASTE` buttons beside
  `SAVE` in the warp editor.
- **Naming a warp preset uses the inline editor.**
- **New decks and cues default to a 1.5s cue fade** (up from 0.5s), so a fade
  reads as a fade.

## 2026-03-13 (attached overlays + self-contained PIP sources)

- **Overlays are reusable bin items plus per-cue attachments**: a primary cue
  has an `OVERLAYS` inspector section and can attach one `Lower Third` and one
  `PIP` from the `OVERLAY BIN`. Attached overlays fire on TAKE only, and stay
  out of the main cue's next/loop sequencing.
- **PIP carries its own source**: `Media File / Still`, `Browser URL`, `Window
  Source`, `Camera Source` or `Syphon/Spout Source`. Legacy cue-linked PIP cues
  still load and are shown as `Legacy Cue Link`, and a live PIP resolves from
  the configured source.
- **Firing an overlay by hand uses the same runtime path** as an attached one,
  so taking a `Lower Third` or `PIP` from the bin replaces the live overlay of
  that kind rather than stacking duplicates.

## 2026-03-13 (overlay bin split, PIP presets, sequencing)

- **The rundown and the overlay bin are separate columns.** `Lower Third` and
  `PIP` cues live in the `OVERLAY BIN`, which stays hidden until an overlay cue
  exists; each column has its own mouse-wheel scroll.
- **Main cue sequencing skips overlay-only cues** — next badges, keyboard
  next/prev and cue-end auto-advance all stay on the main list.
- **PIP controls are direct**: `SET TARGET CUE` at the top of the `PLAYBACK`
  section, with corner presets (`TL / TR / BL / BR`) and size presets
  (`SM / BIG / 70/30`) before any manual geometry.

## 2026-03-13 (cue row controls, inspector and bottom bar)

- Empty states in the cue inspector render inside proper framed cards; the
  program monitor `OUTPUT / DECODE / STREAM` pills are wide enough for their
  numbers; and the `WARP` and `-30 / -20 / -10` transport buttons have roomier,
  aligned labels.
- **The bottom action bar reworked**: the floating `Source` / `Pattern`
  selectors are gone, the section labels (`MEDIA`, `TRANSPORT`, `OUTPUT`) sit
  inside their group panels, and a `LOWER 3RD` button joins `IMPORT`, `SOURCE`
  and `PATTERN`.
- **Per-cue playback state lives in the playlist row**: icon toggles for fade
  in, fade out, loop, hold on last frame and cue audio.
- **Source-cue type selection lives in the inspector**, so the footer needs no
  separate source-kind selector to create window, camera or Syphon/Spout cues.
- Loop, hold and fade toggles apply consistently across still, source, browser,
  pattern and lower-third cues as well as video and audio.
- Follow-up on the same pass: larger `MEDIA / TRANSPORT / OUTPUT` footer tiles;
  telemetry pills that split label from value; `Clear` drops active Lower Third
  overlays immediately; Lower Third cues are edited in the inspector (`title`
  and `sub`); and the `System`, `Audio` and `Network` tabs are reorganised —
  theme and UI feedback under `System → Appearance`, audio device selection
  under `Audio`, and network/integration controls in larger cards.
- **A first real `PIP` overlay cue**: `PIP` in the bottom `MEDIA` group and on
  `Shift+P`, pushed into the overlay stack like a Lower Third but running its
  own silent media engine, with a target cue token editor plus geometry, colour
  and key controls, and the target's own thumbnail in the preview.
- `System → Appearance` shows `UI MOTION` as always-on feedback rather than an
  on/off toggle, and older projects load with motion normalised back on.


## 2026-03-12 (NMC transport sync runtime)

- **A live NMC transport sync backend** behind the existing `NMC` adapter
  toggle: a UDP transport/locate bridge with one active mode at a time. Input
  mode listens for transport and locate packets and applies them to the focused
  deck; output mode broadcasts play, pause, stop and locate updates from it.
- Runtime controls: `DECKBOY_NMC_MODE=input|output`, `DECKBOY_NMC_PORT`,
  `DECKBOY_NMC_HOST` (output target), `DECKBOY_NMC_SOURCE` (sender filter) and
  `DECKBOY_NMC_LOCATE_MS` (rolling locate cadence).
- `--self-check` reports `nmc-sync-runtime`, and route planning reports
  `nmc[ok]`.

## 2026-03-12 (NDI metadata trigger runtime)

- **An NDI metadata trigger backend**: `libndi` is loaded at runtime rather than
  needing SDK headers at build time, and a lightweight receive bridge listens
  for metadata frames on a discovered source. Accepted metadata routes into the
  existing remote-command path as `NDIEVENT`, so it is handled exactly like a
  Companion, OSC or ATEM command.
- Conservative parsing: raw Deckboy command text, common XML forms with
  `command` / `cmd` / `action` / `event` attributes or elements, and `cue`,
  `goto` and `group` XML attributes as `GOTO ...` and `GROUP ... FIRE`
  shortcuts.
- `--self-check` reports `ndi-trigger-runtime: ok/missing`, and route planning
  reports `ndi-trigger[ok]`. `DECKBOY_NDI_TRIGGER_SOURCE` constrains the bridge
  to one source name; `DECKBOY_NDI_LIB` overrides the runtime library path.

## 2026-03-12 (LTC ingest)

- **An LTC ingest backend**: `libltc` is loaded at runtime, capture comes from
  the default SDL audio input, and decoded LTC feeds the existing timecode chase
  and trigger path — emitted internally as `LTCEXT`, the same ingest route MTC
  quarter-frame decode uses.
- `--self-check` reports `ltc-runtime: ok/missing`, and route planning reports
  `ltc[ok]`. `DECKBOY_LTC_LIB` overrides the library path and
  `DECKBOY_LTC_DEVICE` names a specific capture device.

## 2026-03-12 (bundled show export)

- **BUNDLE** on the toolbar, and `Ctrl+Shift+E`: exports a new `.deckboy` plus a
  sibling `<show>_media/` folder, with the copied media rewritten to relative
  cue paths so the show moves as one thing.
- File-backed cues resolve against the current project folder for decode,
  thumbnails, preview and waveform analysis, so a bundled show does not depend
  on the original absolute paths.

## 2026-03-10 (SRT stream stability)

- **A stalled or unlistened stream never holds the UI.** Child stream processes
  launch through explicit pipes, video writes happen on a dedicated stream
  writer thread rather than the render thread, startup and shutdown keep
  Deckboy's own control and listener sockets out of the ffmpeg children, and a
  missing listener or a reconnect surfaces as retry/recovering state.
- The local SRT loopback workflow is spelled out: `OUTPUT ON` arms the output,
  `STREAMING: ON` starts network egress for it, and local viewing uses an
  external SRT listener such as `ffplay`.

## 2026-03-10 (portability prep + docs refresh)

- The top-level CMake prefers exported `SDL2` / `SDL2_ttf` config packages and
  falls back to pkg-config or a manual lookup, and macOS feature-gated framework
  linking goes through a real helper.
- `native/core/paths.cpp` resolves executable locations on Linux, macOS and
  Windows; sans and mono font lookup includes the macOS and Windows system font
  locations alongside project-local overrides; the subprocess layer keeps
  Unix-only members and headers behind conditionals; and socket send helpers
  tolerate a platform with no `MSG_NOSIGNAL`.
- `PORTABILITY.md` documents executable-root lookup on all three platforms and
  the current build and runtime readiness.

## 2026-03-06 (Phase 4 inline editing + floating panel workspace)

- **Panel presentation and visibility persist** on top of the Phase 1 workspace
  model.
- **A `PANELS` control row in the status strip**: `PGM[D/F]`, `INS[D/F]`,
  `OUT[D/F/H]`, `RTG[D/F/H]`, `MSC[D/F/H]`.
- **A secondary `Deckboy Panels` floating workspace window** for popped-out
  operational panels, with a per-panel `DOCK` return control.
- **Panel-local focus badges** so focused Deck / Output / Cue state is visible
  in panel headers and cue rows, not only in the global strip.
- **The inline editor replaces every remaining operational text prompt**: custom
  output raster, output refresh, output canvas size, canvas view offset, stream
  URL, stream bitrate, output alpha, output delay, NDI name, NDI key name, cue
  goto target, cue notes, browser URL and cue ID.
- Safe-text cleanup continues through the quit confirmation, startup dialog,
  splash overlay headings, About tab, and the video output advanced and routing
  headers.

## 2026-03-06 (Phase 3 workflow polish + selector cleanup)

- **Every operational selector is a non-blocking dropdown**: audio output
  device, output display, stream protocol and mirror source. The old blocking
  list picker is gone from the live UI flow.
- **Program / Transport reads clearly**: explicit `CURRENT` and `NEXT`, and
  labelled `TRANSPORT`, `TIMELINE` and `REMAIN`.
- **One next-cue rule** shared by the Program summary and the deck playlist
  rows.
- The default workspace gives `Program / Transport` more room, and a text-safe
  pass covers the Decks window tracker columns, playlist headers and rows,
  Master Scene programmer and list rows, Program / Preview labels, cue thumbnail
  placeholders and details footer, the settings modal title, tabs and output
  summary, and dropdown popovers — long labels ellipsize rather than colliding.

## 2026-03-06 (Phase 2 operational panel split)

- The control UI splits into explicit panels rather than one shell:
  - `Deck Playlist` for the focused deck
  - `Program / Transport`, with the current and next cue summaries, the focused
    Deck / Output route summary, the Program and Preview monitors, progress and
    remaining time, and the stack view
  - `Cue Inspector` as its own singleton panel
  - `Output Panels` as a repeating operational panel list
  - `Routing Matrix` as a singleton operational panel rather than a
    Preferences-only surface
  - `Master Scene` in the right-hand operational column
- Reusable panel chrome through `drawOperationalPanel(...)`, with docked and
  floating panel instances recording their real frames.
- Scrollable, non-blocking views for `Output Panels` and `Routing Matrix`, and
  Output-panel controls for focus, recover, disarm and the FPS toggle.
- The Program panel uses its full width now that the Inspector is its own panel.
- No playback, routing, OSC, Companion, shortcut or output-safety behaviour
  changed.

## 2026-03-06 (workspace foundation: panel registry + persistence)

- A panel/workspace foundation — `UiPanelDefinition`, `UiPanelState`,
  `UiPanelManager`, `UiWorkspaceState`, `UiFocusState` — with logical panel
  kinds registered for Program / Transport, Preview, Cue Inspector, Routing,
  Master Scene, Preferences, Deck Playlist and Output.
- Always-visible workspace and focus summary lines in the operational strip:
  `WORKSPACE ...` and `FOCUS: DECK ... | OUTPUT ... | CUE ...`.
- Workspace save/load at `data/deckboy.workspace`: panel visibility,
  presentation (docked / floating / modal), frames, control and decks window
  geometry, focused panel, focused Deck / Output / Cue context, and the last
  Master Scene sidebar and programmer state. It loads during init, applies
  window geometry and focus, and auto-flushes from the update loop — with no
  change to the show file format, and a serialisation smoke test.

## 2026-03-06 (operator terminology normalization)

- Terminology normalised across the live UI and the docs: `Master Scene` →
  `Master Cue`, the `MASTER SCENES` sidebar → `MASTER CUES`, `Create Standard` →
  `Create Window`, `Camera` / `Syphon/Spout` → `Camera Source` /
  `Syphon/Spout Source`, "lower-third / graphic" → `Lower Third`, and
  "Decks panel" / "tracker window" → `Decks window`.
- The Master Cue sidebar reads consistently: an `MC#` focus badge, `<MC` / `MC>`
  nav buttons, a `TAKE` fire button, and a `Master Cue Name` rename prompt.
- Compatibility aliases and protocol identifiers are unchanged: the `GROUP` and
  `SCENE` command aliases still work, and `.deckboy`, `DECKBOY_*`, `/deckboy/*`
  and `deckboy-native` stay as they are.

## 2026-03-05 (audio inspector metadata section)

- The audio cue inspector matches the section model: `PLAYBACK` for transport
  and audio behaviour, `METADATA` for tag, notes, cue id and pause points, with
  `ROUTING` below. Loop, hold and end rows use the shared panel helpers, and
  pause points render in the aligned metadata row style.

## 2026-03-05 (inspector metadata sections for lower-third, browser and source cues)

- Lower-third cues use boxed `PLAYBACK` and `METADATA` sections;
  still, pattern, browser and source cues split playback controls from metadata
  and source rows, in the same aligned panel style as the rest of the inspector.
- Shared inspector row helpers for message/info rows, edit rows, status rows,
  action rows and tag rows.
- Lower-third `CLEAR OVERLAY` is a real clickable action.

## 2026-03-05 (inspector sections, routing alignment, control styling)

- **Scoped inspector section cards** for `PLAYBACK`, `GEOMETRY`, `KEY` and
  `ROUTING`, with consistent collapse affordances, boxed grouping, and a shared
  row style with aligned labels and +/- controls.
- **Routing controls use a compact table row layout**, and the output-strip
  routing rows use `UITable` alignment for the deck label, output selector,
  layer selector and assign/link action.
- **Control styling**: bottom-bar buttons take a stronger top band and adaptive
  title font sizing, and dropdowns and buttons share one panel treatment and
  safer text rendering.

## 2026-03-05 (grid layout cleanup: safer spacing, a clearer control window)

- Reusable layout and safety primitives: `VerticalLayout`, `HorizontalLayout`,
  `GridLayout`, `UITable`, `drawTextSafe(...)`, and shared `drawUIPanel(...)`,
  `drawUIButton(...)` and `drawUIDropdown(...)`.
- The main control window snaps to an 8px grid: panel padding `16`, panel gap
  `12`, chunky `2px` framing, a shared bottom-bar button height of `40`, and a
  compact `56` global header.
- The header splits into title / output+TC / controls zones, the content area
  reserves space for selectors and bottom controls before laying out columns,
  and deck header, footer and cue row text ellipsizes inside its bounds.
- The bottom bar takes consistent-width buttons, keeps the `MEDIA / TRANSPORT /
  OUTPUT` grouping, and simplifies its labels to `IMPORT / SOURCE / PATTERN /
  TAKE / STOP / PLAY / CLEAR / PREFS`.
- The program area gets a clearer title/time/progress hierarchy, a single
  monitor frame, and stack view and inspector spacing on the new constants.

## 2026-03-05 (output activation: explicit health, recover and disarm)

- **An explicit per-output health model**: `OFF`, `ARMED`, `LIVE`,
  `RECOVERING`, `ERROR`, each carrying the reason it last changed.
- **Output chips show it**: the state token comes from the health model rather
  than ad-hoc stream flags, with the reason inline on the chip and the focused
  output still highlighted.
- **Per-chip controls**: `REC` recovers or re-arms that output in one click;
  `OFF` disarms it.
- Health transitions are wired through fullscreen enable and recovery, stream
  start, write and audio failures, an unavailable NDI runtime or sender, and the
  escape-to-windowed path, which reports as armed with a reason. Pressing `ON`
  again on a stream output performs a real egress restart.
- Status snapshots expose it: `health=` plus optional `health_reason="..."` in
  text `STATUS`, and `health` / `healthReason` per output in `STATUS JSON`.

## 2026-03-05 (toggleable per-output FPS counter)

- Per-output FPS measurement with rolling sampling, an `FPS ON/OFF` toggle in
  the output strip, and an `xx.xfps` readout on each output chip that updates
  continuously while outputs render.

## 2026-03-05 (browser diagnostics, cue-panel refactor, regression smoke)

- **Live browser startup diagnostics** in the cue panel: a `state` row reading
  `starting xvfb`, `starting browser`, `starting capture`, `live`, or
  `failed: <reason>`, with concise reasons stored in the deck runtime (`url
  missing`, `browser not found`, `xvfb launch failed`, `browser launch failed`,
  `capture start failed`).
- Shared helpers for labelled value, edit and status rows replace the duplicated
  row blocks for `source`, `url` and `notes`.
- New smoke regression checks: the Decks window visibility policy (one deck
  hidden unless manual, two or more visible), the transport transition
  source-gain policy that keeps a stop or take from flashing black, and the
  browser status summary label mapping.


## 2026-03-05 (browser cue take/capture reliability)

- Browser capture goes through the same platform capture planner as source and
  window capture, so a browser cue's ffmpeg arguments match the known-good
  `x11grab` path.
- If browser capture cannot start, the startup sequence stops and toasts
  `browser capture failed` rather than leaving the cue black.

## 2026-03-05 (browser cue URL editing in the cue panel)

- **A browser cue's URL is edited in the cue panel**, in the same right-side
  settings flow as still, pattern and source cues: a `url` row with an `edit`
  button, accepting a URL or a local file path and normalising it to a
  browser-safe URL.
- Editing the URL of the live cue reloads it, so the change is on screen
  immediately.

## 2026-03-05 (transport continuity: no black flash)

- `STOP` rewinds while holding the visible frame until frame 0 is decoded, and a
  `TAKE` from a paused or stopped deck keeps its transition source at full — so
  neither flashes the output black. `seek(...)` preserves the visual frame
  through the decoder restarts that stop and rewind use.

## 2026-03-05 (Decks window visibility)

- The Decks window starts hidden for a single-deck show and opens itself when a
  show crosses to two decks. The header `decks` button toggles it open and
  closed, and closing it keeps it closed.
- Manual open state resets on `New` and `Open`, after which visibility is
  re-evaluated from the deck count, and the renderer does no work while the
  window is hidden.

## 2026-03-05 (UI de-clutter + non-blocking dropdowns)

### A responsive control path
- `DECKBOY_UI_PROFILE=1` adds UI-thread timing and popup watchdog logs: frame
  timing when `dt > 50ms`, segmented event/update/layout/render timings, popup
  open and close logs with item counts, and popup render counts.
- The `PATTERN` and `SOURCE` button actions run immediately, with no blocking
  dialog anywhere in the live control path.

### A reusable non-blocking dropdown
- Click to open; close on outside click, `Esc` or selection; mouse wheel
  scrolling; keyboard navigation (`Up/Down/Enter/Esc`); type-to-filter with
  `Backspace`; clipped visible-row rendering. No nested modal event loops.

### Where it lands
- The bottom bar carries `Source: … v` and `Pattern: … v` default selectors;
  the `PATTERN` button and `P` add the selected default pattern directly, and
  `SOURCE` adds using the selected default source type
  (`window`/`camera`/`syphon|spout`).
- In the cue settings panel, pattern type and transition style become dropdowns,
  and source cues get in-menu `source` editing through the inline text editor,
  with human-friendly prompts and plain aliases — `focused`/`recommended` for a
  window, `default` for camera and Syphon — resolved per cue type across a
  multi-select.
- Optional external UI art pack support: `data/ui/deckboy_ui_pack_v3` preferred,
  falling back to `v2`, covering header art, output-chip backgrounds, cue-type
  icons, the monitor frame and the splash image. Mascot art stays on the splash.

### Layout
- The bottom bar is grouped into `MEDIA`, `TRANSPORT` and `OUTPUT`, the main
  layout reserves its space so content panels never overlap the controls, and
  compact selector chips sit above the bottom controls.

## 2026-03-05 (output activation stability)

- Display-mismatch recovery is evaluated only when the output window is
  non-fullscreen and its display index is valid, so arming an output does not
  cycle through fullscreen tear-down and re-apply on some SDL and display-driver
  combinations.
- Anti-thrash gating: non-fullscreen auto-recovery triggers only shortly after an
  explicit fullscreen request, hidden/minimised/wrong-display recovery stays
  active, and attempts are throttled.
- The display-apply path has a non-transition mode that preserves fullscreen
  state, which recovery uses; and enable, recover and fullscreen actions check
  whether fullscreen was actually entered and toast when it was not.

## 2026-03-05 (integration runtime: ATEM bridge, MTC ingest, Art-Net triggers)

- **A live ATEM UDP trigger bridge**: a listener on UDP `9910` by default
  (`DECKBOY_ATEM_BRIDGE_PORT` overrides), enqueuing inbound payloads into the
  remote command path as `ATEMEVENT ...`. Supported payloads include `CUT`,
  `AUTO`, `TAKE`, `PLAY`, `STOP`, `NEXT`, `PREV`, `CLEAR`, `PANIC`,
  `SCENE <n>` and `DECKBOY <command>`.
- **A live Art-Net trigger bridge**: a listener on the configured `artNetPort`
  parsing `ArtDMX` packets and edge-triggering `ARTNETEVENT ...`. Default
  channel map: ch1 `TAKE`, ch2 `PLAY`, ch3 `STOP`, ch4 `GO`, ch5 `NEXT`, ch6
  `PREV`, ch7 `CLEAR`, ch8 `PANIC`, with ch9 `TAKE <value>` and ch10
  `GROUP <value> FIRE` on value changes.
- **MTC quarter-frame ingest** through ALSA: the MIDI loop decodes
  `SND_SEQ_EVENT_QFRAME` to `MTCEXT <seconds> <fps>`, applied to chase-enabled
  decks and otherwise the focused deck.
- The `INTEGRATION ADAPTERS` panel on the Network tab shows the ATEM and Art-Net
  UDP ports, and an Art-Net port edit restarts the listener at runtime.

## 2026-03-05 (integration adapter foundation)

- An integration backend planning module
  (`native/platform/integration_backend.hpp/.cpp`) with a catalog API and a
  route planner.
- Backward-compatible project fields and keys for ATEM, NDI trigger, NMC, MTC,
  LTC and Art-Net, plus the Art-Net port.
- `Settings → Network → INTEGRATION ADAPTERS` with direct toggles for all six,
  an Art-Net port prompt and an all-adapters quick toggle.
- Commands `ATEM`, `NDITRIGGER`, `NMC`, `MTC`, `LTC`, `ARTNET`, `ARTNETPORT`
  and `INTEGRATIONS`, with OSC endpoints `/atem`, `/ndi/trigger`, `/nmc`,
  `/mtc`, `/ltc`, `/artnet`, `/artnet/port` and `/integration`.
- `STATUS` and `STATUS JSON` carry an integration route summary, the OSC mirror
  publishes `/deckboy/integration/*`, and `--self-check` prints
  `integration-backends` and `integration-route-defaults`.

## 2026-03-05 (runtime egress route wrappers)

- Output egress runs through the backend route planner: a stream send happens
  only when the route includes a supported `stream` backend, an NDI send only
  with a supported `ndi` backend, and stream runtime stops when stream routing
  is unsupported or inactive.
- Output status snapshots carry the route: `backend=...` in text `STATUS` and
  `backendRoute` in `STATUS JSON`.

## 2026-03-05 (capture/output backend planning APIs)

- `native/platform/capture_backend.*` grows executable planning interfaces —
  `SourceCaptureRequest`, `SourceCapturePlan`, a `SourceCaptureBackend` factory
  set and `planSourceCapture(...)` — with Linux backends (`x11grab` window
  capture, `v4l2` camera capture, a desktop-fallback app-texture path) and
  explicit macOS and Windows scaffold plans carrying reason strings.
  `MediaEngine::buildSourceCaptureArgs(...)` delegates to the planner.
- `native/platform/output_backend.*` grows route planning —
  `OutputBackendRouteRequest`, `OutputBackendRoutePlan`,
  `planOutputBackendRoute(...)` — describing the active backend chain for
  `window` / `stream` / `ndi` / `decklink`.
- `--self-check` reports `capture-plan-defaults` and `output-route-defaults`.

## 2026-03-05 (official Stream Deck + Companion profile package)

- A profile docs bundle: `docs/streamdeck/README.md`,
  `docs/streamdeck/deckboy_companion_profile_map.json` and
  `docs/streamdeck/deckboy_main_page.csv`. The JSON manifest is the canonical
  Deckboy key map for Stream Deck through Bitfocus Companion
  (`Generic TCP/UDP`), grouped into Main transport, Deck focus, Output control
  and Master Cue control pages.
- The README's and MANUAL's Companion sections link straight to it.

## 2026-03-05 (warp mode: linear vs perspective)

- **`Deck.warpMode`**, `linear` (default) or `perspective`, serialised
  append-only so older show files load as `linear`.
- The output present path honours the mode: `linear` keeps the quad-geometry
  path, `perspective` uses tessellated projective UV mapping
  (`renderPerspectiveWarp`) for proper corner-pin behaviour. Orientation and
  edge blend are unaffected.
- `Video Outputs → Advanced` gains `Mode Linear` / `Mode Perspective`, with
  `VIDEO WARP MODE LINEAR|PERSPECTIVE|NEXT|PREV` and the direct aliases
  `VIDEO WARP LINEAR|PERSPECTIVE`. The status toast reports enable state and
  mode.
- Deck status carries `warp_mode`, `STATUS JSON` carries `warpMode`, and the OSC
  mirror publishes `/deckboy/deck/<n>/warp_mode`.

## 2026-03-05 (output parity: span/duplicate, orientation, test cards)

- `OutputTarget` gains `outputLayoutMode` (`span` | `duplicate`),
  `outputOrientationDegrees` (`0/90/180/270`) and `outputTestCardEnabled`,
  serialised append-only with defaults for older shows.
- Window presentation honours per-output orientation; `span` uses the host
  deck's canvas view offsets when canvas mode is on, `duplicate` locks to origin
  (`0,0`); and a per-output test-card feed renders in the compositor path. The
  egress capture path (NDI, stream, delay) captures the same view region and
  applies orientation before sending.
- The Video Outputs tab gains `Span` / `Duplicate`, a `Rotate
  0°/90°/180°/270°` cycle, `Test Card ON/OFF` for the focused output and
  `All Cards ON/OFF`, with commands
  `VIDEO OUTPUT LAYOUT SPAN|DUPLICATE|NEXT|PREV`,
  `VIDEO OUTPUT ORIENTATION 0|90|180|270|NEXT|PREV|RESET` and
  `VIDEO OUTPUT TESTCARD ON|OFF|TOGGLE|ALL ON|OFF`.
- `STATUS` / `STATUS JSON` expose `layout`, `orientation` and `test_card`, and
  the OSC mirror publishes `/deckboy/output/<n>/layout`, `/orientation` and
  `/testcard`.

## 2026-03-05 (multi-select inspector parity)

- **A dedicated multi-selection inspector mode**: common controls with `mixed`
  labels where values conflict, grouped Playback / Geometry / Key / Routing
  sections, and compatibility masking when the selection mixes cue kinds.
- **Every quick-action edit applies to the whole selection**: trim in/out and
  clear, cue timecode mark set and clear, loop / hold / pause begin / cue audio /
  transition-to-next toggles, fade in and out, transition duration and style,
  end action, geometry (scale mode, scale X/Y, offsets, rotation, crop), key
  (enable, colour, tolerance, softness), colour (brightness, contrast,
  saturation, hue), lower-third alpha, still duration, repeats and playback
  speed.
- Mixed selections resolve to the first eligible cue, so a toggle still works
  when the focused cue is not the compatible one.

## 2026-03-05 (OSC Query + OSC feedback mirror)

- **An optional OSC Query HTTP server**: `/` as a lightweight endpoint browser,
  `/oscquery.json` for endpoint docs plus live state, and `/state.json` for the
  live status payload.
- **An optional canonical OSC feedback mirror**: value-based `/deckboy/deck/*`
  and `/deckboy/output/*` updates to subscribed senders, with a configurable
  rate limiter (40–2000 ms, default 120 ms). The existing `/deckboy/state` JSON
  feedback is retained.
- The Network tab controls all four, a Companion/OSC port change restarts the
  listener immediately, and the commands are `OSCQUERY ON|OFF`,
  `OSCQUERYPORT <port>`, `OSCFEEDBACK ON|OFF` and `OSCFEEDBACKRATE <ms>`, with
  matching OSC addresses.
- Persisted as `osc_query_enabled`, `osc_query_port`, `osc_feedback_mirror` and
  `osc_feedback_rate_ms`, clamped to a valid port and 40–2000 ms.

## 2026-03-05 (playlist preferences: deck-level timebase and defaults)

- **Per-deck playlist preferences**, persisted: playlist timebase FPS (24, 25,
  29.97, 30), playlist start timecode offset, default cue fade duration, default
  non-movie duration, and the default new-cue toggles — loop, fade in, fade out,
  audio, pause begin, pause end and transition-to-next.
- A `PLAYLIST PREFS` block in `Prefs → System` for the focused deck: an edit
  dialog for timebase, start, fade and still defaults, direct toggles for the
  new-cue flags, and an inline summary showing SMPTE base, start TC and default
  timings.
- Every new-cue path applies them — media import, and browser, source,
  lower-third and pattern cue creation — so a long playlist behaves
  predictably.

## 2026-03-05 (capture/output backend catalogs)

- Platform backend catalogs (`native/platform/capture_backend.*` and
  `output_backend.*`) report the planned backend families per platform: window
  capture (Linux `x11grab`, macOS ScreenCaptureKit, Windows DXGI), camera
  capture (Linux `v4l2`, macOS AVFoundation, Windows Media Foundation) and
  app-texture transport (Syphon/Spout); and for output, SDL window, FFmpeg
  stream, NDI (SDK-gated) and DeckLink (feature-gated).
- `--self-check` prints `capture-backends` and `output-backends`, so backend
  readiness on all three platforms can be audited in one place.

## 2026-03-05 (cue metadata, toggles and deck opacity)

- **New cue fields**, append-only in the show format: `cue_id` (a short
  operator-facing ID, up to 6 characters, normalised uppercase), `audio
  enabled`, `pause at beginning`, `transition to next` and `goto target`.
- **Deck-level playlist fader fields**: `playlist opacity` (0–100%),
  `playlist auto fade` and `playlist fade seconds`.
- Runtime: `pause at beginning` loads without autoplay on take; `transition to
  next` decides whether auto-advance and goto use transition timing or a cut;
  `goto target` resolves by cue token (`cue_id`, cue number or name token) at
  end of cue; `audio enabled` gates the audio decode path, so a muted cue runs
  video-only; and deck playlist opacity multiplies the deck's contribution in
  the compositor, with an optional fade-to-target animation.
- Cue rows show the operator's cue token (`cue_id`, then cue number, then
  index). Multi-select arrives: `Shift+click` for a range, `Ctrl/Cmd+click` to
  toggle, and batch apply for notes, cue id, loop/hold, fades, colour tag and
  the parity toggles. The deck footer gains an opacity rail (click or drag, with
  `Alt+click` snapping to 0% or 100%), and video cues gain `pause in`, `audio`,
  `next xfade` and `goto` inspector rows.
- Commands: `PAUSEBEGIN`, `PAUSEEND`, `CUEAUDIO`, `NEXTTRANS`, `CUEGOTO`,
  `CUEIDSHORT`, `PLAYLISTOPACITY` / `DECKOPACITY` / `DECKDIM`,
  `PLAYLISTAUTOFADE` / `DECKAUTOFADE`, `PLAYLISTFADE` / `DECKFADE`, with OSC
  paths for each.

## 2026-03-05 (UI clarity: header, stack, routing table, splash)

- The header carries a compact per-deck live summary (`D1 LIVE ...`) alongside
  the show file and Companion/TC state.
- **Output chips scan faster**: index, target type and armed/live state, a
  stronger highlight on the focused output, and a one-click inline `ON/OFF`.
- **The deck column header** shows `Deck N`, the routed output and layer token,
  and the audio device label.
- **Cue rows use fixed scan columns** — cue token / type token / cue name /
  duration-state — truncating with a hover tooltip for long names.
- **The program monitor** shows focused output info (output, raster, refresh),
  with a `STACK VIEW (Output X)` beneath it listing deck and layer occupancy top
  to bottom, and a chunkier progress bar carrying its time text.
- **The cue settings panel** gains grouped, collapsible `Playback`, `Geometry`,
  `Key` and `Routing` headers, plus routing controls (output prev/next, layer
  +/-, assign/unassign).
- **The Video Outputs routing table is editable inline**: `Deck | Output | Layer
  | Assigned`, with per-row output prev/next, layer +/- and a link/unlink
  toggle, wired to the existing assignment logic.
- **Master cue rows are two lines**: the indexed scene name above, the deck slot
  summary below, and a larger `TAKE` on the right.
- **A launch splash overlay** with boot messages and `press ENTER to start`,
  skippable with `Enter`, `Esc` or a click. Character art stays out of the
  operational UI, and the About tab uses text and logo runtime info only.
- `DEVNOTES.md` documents the layout component map, palette tuning points, the
  cue-type icon hook and the routing table action wiring.

## 2026-03-05 (output FX: alpha, delay, overlay, colour space)

- **Focused-output FX controls** in the Video Outputs tab: `Overlay ON/OFF` (an
  output-scoped time/ID overlay), `Alpha` (0–100% output dimmer), `Delay`
  (0–5000 ms) and `Color` (`AUTO`, `BT709`, `SRGB`), plus a one-tap
  `Delay +100`.
- Per-output alpha applies as a post-composite dimmer; the output-scoped overlay
  toggles independently of the deck-local one; and a per-output delayed frame
  queue feeds the egress paths — NDI and stream send the delayed-or-live frame
  while window presentation stays immediate.
- The stream encoder applies colour metadata from the output's colour-space
  mode: `BT709` → bt709 matrix/primaries/trc, `SRGB` → bt709 matrix/primaries
  with the `iec61966-2-1` transfer.
- Commands `VIDEO OUTPUT ALPHA|DELAY|OVERLAY|COLORSPACE ...`, with alpha
  percent, delay ms, overlay state and colour-space token in both status
  snapshots and persisted on the output target.

## 2026-03-05 (live source cues)

- **`Window Source` cues capture live** through ffmpeg `x11grab` on Linux, and
  **`Camera` cues** through `v4l2`. `Syphon/Spout` cues run through the source
  transport path, using desktop capture on Linux while the native backends are
  planned.
- **Source cues take part in normal transport**: `Take` with autoplay starts
  live capture, `Play` starts or restarts it, `Pause` parks capture and holds
  the frame, `Stop` parks capture and restores the source slate. Deck transport
  status shows `Live Source` / `Source Ready`.
- Cue-row hover tips describe source cues as live cues, and toasts distinguish
  source-loaded, source-live and source-unavailable.

## 2026-03-05 (output operations flow + source cue scaffold)

- **Explicit creation actions in Video Outputs**: `Create Standard` (window
  output) and `Create Stream`, with direct `Set Window` / `Set Stream` buttons
  and a focused-output signal-flow line reading deck layer stack → output →
  display.
- **New cue kinds** — `Window Source`, `Camera`, `Syphon/Spout` — created
  through one shared path: the `SOURCE` button and
  `Preferences → System → Add Source Cue...`, the commands `SOURCE WINDOW|CAMERA
  |SYPHON ...` with the aliases `WINDOWSOURCE`, `CAMERACUE`, `SYPHONCUE` and
  `SPOUTCUE`, the persistence tokens `window_source`, `camera` and `syphon`, and
  OSC mappings for each. They render through a common generated placeholder
  frame, with transport, routing, save/load and status on the normal cue flow.

## 2026-03-05 (NDI is per-output)

- **NDI is output-scoped end to end.** The `N` key, the `NDI ...`, `NDINAME` and
  `NDIKEY...` commands and the Video Outputs actions all resolve through the
  focused output target, and the output runtime sends fill video, an optional
  key video stream, and mixed stereo audio for that output's assignment stack.
- A migration shim maps legacy deck NDI settings onto output NDI during project
  normalisation where output NDI is not already configured, and the legacy deck
  fields stay in the save format for older shows.
- NDI status is reported under output entities only, and `MANUAL.md` and
  `README.md` describe NDI as per-output, with examples that focus an output
  first (`VIDEO OUTPUT <n>`, then `NDI ...`).

## 2026-03-05 (UI declutter + output workflow clarity)

- The no-cue control state is text-only: `Insert cartridge`, `Drop media here`,
  `Press A to take cue`.
- In Video Outputs, `Routing In Main Strip` is informational rather than
  clickable, and a `Show Advanced` / `Hide Advanced` toggle keeps the dense
  refresh, depth, canvas and warp controls out of the way until wanted.
- The Decks window reads as `Deckboy Decks`, with a `DECKS` header over the deck
  list and playlist view.

## 2026-03-05 (demo show generator + layout presets)

- `scripts/generate_demo_shows.sh` writes repeatable demo shows into
  `data/demos/`:
  - `demo_70_30_4pip_bg_5deck.deckboy` — a 5-deck show with a full background
    and four right-column PiPs, with master cues `Open - BG + 4 PiP`,
    `BG Only` and `PiP Motion Sweep`
  - `demo_quad_2x2_4pip_bg_5deck.deckboy` — a 5-deck show with a full background
    and a 2x2 PiP quad, with master cues `Quad Open` and `Quad Motion`
  - `demo_program_preview_clean_3deck.deckboy` — a 3-deck show with a program
    background, preview PiP and corner bug, with master cues
    `Program + Preview + Bug`, `Program + Bug` and `Program Clean`

## 2026-03-05 (safety, output quality, master-cue speedups)

- **A single-instance lock** at startup, so a second launch exits with a clear
  terminal message. `--allow-multi-instance` bypasses it deliberately.
- **Output quality goes auto-native on arm and re-arm**: enabling a `window`
  output switches global output sizing to display-native when fixed mode was
  active, and repeating `VIDEO OUTPUT ON` applies the same path before recovery.
  Toasts say `auto native`.
- **Master-cue programming without a popup**: clicking a programmer row outside
  its buttons assigns the slot from that deck's selected cue, and the mouse
  wheel over a row cycles the slot cue directly.

## 2026-03-05 (panic timing, cue find, timecode follower)

### Panic timing
- `Preferences → Audio` gains `Panic fade (sec)` and `Panic auto restore`, and
  the panic fade profiles use the configured duration. The dimmer restores
  automatically after a panic deck action when enabled. Commands:
  `PANICFADE <seconds>` and `PANICAUTORESTORE ON|OFF|TOGGLE`.

### Cue find and renumber
- `FIND <token>`, `FINDNEXT`, `FINDPREV`, `FINDTAKE <token>`, plus
  `RENUMBER [prefix] [start]` and `RENUMBER CLEAR` (aliases `AUTOID`,
  `CUEAUTOID`).
- Cue token matching understands `FIRST`, `LAST`, `NEXT`, `PREV`, `SEL`, `ACT`,
  relative offsets (`+N`, `-N`) and cue-number prefixes.

### Timecode follower: jam and freewheel
- Per-deck persistent `timecodeJamSyncEnabled` and `timecodeFreewheelSeconds`,
  with menu-first controls in `Preferences → Audio` for the focused deck, the
  commands `TIMECODE JAM ON|OFF` and `TIMECODE FREEWHEEL <seconds>`, and the OSC
  mappings `/timecode/jam i` and `/timecode/freewheel f`.
- A chasing, running deck holds after the freewheel timeout when external TC
  stops; with `TC JAM OFF`, incoming TC inside the freewheel window does not
  re-jam the running clock; and `TIMECODE SET ...` remains a forced operator
  jam.

### Status and the Audio tab
- Text status carries the panic profile, fade and restore state and per-deck
  `tc_jam` and `tc_freewheel_s`; JSON status carries the same as top-level panic
  fields and per-deck `timecodeJam` and `timecodeFreewheelSeconds`.
- `Preferences → Audio` is grouped into `System + UI`, `Playback Semantics`,
  `Safety + Timecode Follower` and `Cue Tools (menu-first)`, the last carrying
  `Find Cue...`, `Next`, `Prev`, `Find+Take`, `Renumber...`, `Clear Numbers` and
  `Clear Find`.
- Find state has its own commands and endpoints: `FINDCLEAR` / `FINDRESET`,
  `FINDSTATUS`, the `CUEFIND*` aliases, `STATUS CUES` / `STATE CUES` and
  `STATUS FIND` / `STATE FIND`, with find token and cursor metadata and per-deck
  `selected_num`, `selected_id`, `active_num` and `active_id` in the payloads,
  and OSC mappings `/find`, `/find/next`, `/find/prev`, `/find/take`,
  `/find/clear` and `/renumber`.


## 2026-03-04 (playback semantics)

### Jump mode and panic profiles
- `Preferences → Audio` gains `Jump Mode` (`Trigger` or `Load`), `Jump
  Transition` (`ON`/`OFF`), `Panic Profile` (`Outputs Off`, `Fade+Pause`,
  `Fade+Rewind`, `Fade+LoadNext`) and a `Run Panic` button, all persisted in the
  show file as `jump_mode`, `jump_transition` and `panic_profile`.
- `Take` and `Goto` run through jump-mode semantics: `Trigger` jumps the
  selected cue live, `Load` loads it without autoplay. Jump Transition decides
  whether a jump uses cue and deck transition timing or cuts.
- The panic profiles execute properly: `Outputs Off` disarms every output, and
  the fade profiles arm a dimmer fade and then run the deck action — pause,
  rewind, or load next.
- Commands: `JUMPMODE TRIGGER|LOAD|TOGGLE`, `JUMPTRANSITION ON|OFF|TOGGLE`,
  `PANICPROFILE <name>|NEXT|PREV` and `PANIC` (or `PANIC <profile>` for a
  one-shot override).

## 2026-03-04 (a bigger, clearer decks workspace)

- One operating flow, no mode split.
- The companion deck window opens at `1560x920` (minimum `1260x700`), in Game
  Boy green with star accents, with larger tracker rows, deck-name width, deck
  cards, per-deck header blocks and transport buttons, and larger cue rows in
  each playlist card.
- The main transport button reads `Play/Pause`, and a deck card carries `Take`
  and `Stop` without a competing `Go`.
- Master-cue slot programming is inline: `-` / `+` cycle the cue per deck slot,
  and middle or ctrl interactions cycle cues rather than opening a picker.

## 2026-03-04 (an always-on master-cue sidebar)

- **The master-cue sidebar is a permanent panel**, wider, with a fixed
  `Master Cues` badge in the output strip marking where it lives.
- **Direct sidebar actions**: `Name` renames the focused master cue, `CapSel`
  captures each deck's selected cue into it, and `CapAct` captures each deck's
  active cue. The expandable programmer (`Prog+` / `Prog-`) keeps a row per deck
  with `Sel`, `Act`, `Byp`, `-` and `+`.
- **The `Deckboy Decks` window reads more easily**: larger tracker and header
  sizing, expanded labels (`selected`, `active`, `layer`, `state`, `timecode`),
  bigger deck-card headers, buttons and cue rows, and an interaction hint
  ("click cue to select, right-click cue to take").
- **Changing a focused output's display goes auto-native**, switching video
  sizing from fixed raster to display-native and saying `auto native` in the
  toast.
- **The Pocket Test scene expands**: an original platform-adventure vibe with an
  automatic scene cycle (`pocket-test`) and selectable variants (`pocket-day`,
  `pocket-sunset`, `pocket-night`, `pocket-storm`), plus procedural animated
  creatures — a crab, a jumping fish, a parrot, a turtle, a dino-style enemy and
  a puff friend — alongside the explorer, coin line and signal strip.

## 2026-03-04 (deck/output separation + stream outputs)

### Outputs never take a screen by surprise
- Window outputs default to `OFF` in new projects and new outputs, output
  runtime windows are created hidden, and a loaded show opens disarmed — so a
  saved output-on state cannot take over a screen at launch.
- `Preferences → Video` gains an `Enabled` toggle for the focused output, which
  fullscreens it on the selected display, plus `Prev` / `Next` / `Rescan`
  display assignment with a live display label. `F` enables the focused window
  output if it is off. The command is `VIDEO OUTPUT ON|OFF|TOGGLE`.
- **Emergency escape**: `Esc` exits fullscreen on the active output window,
  falling back to any fullscreen output sharing the control window's display,
  and marks that output intentionally windowed so auto-recovery leaves it alone.
  Three presses within ~0.9s triggers panic disarm (`outputs off`). Re-arming is
  explicit — `F` on that output, or `VIDEO OUTPUT ON` again.
- **Recovery**: `recoverWindowOutputIfNeeded(outputIndex)` re-applies display
  placement, raises a hidden or minimised window and re-asserts fullscreen on
  the target display; repeating `VIDEO OUTPUT ON` performs it, `F` re-asserts
  fullscreen rather than dropping to windowed, and a 1 Hz background poll
  recovers enabled window outputs that drift off target.
- **Display topology refresh**: monitor hot-plug is detected and outputs
  re-clamped and re-applied automatically; manual `Rescan` runs the same path. A
  `Connected Displays` list shows what is detected, and clicking a row assigns
  the focused output to it.

### Routing you can read
- Route labels are plain English — `Deck 1`, `Output 2`, and route values
  `None`, `Background` or `Layer N` — with no shorthand codes.
- Layer edits are direct in the Route Editor: `-` / `+`, `Ctrl` for a ×10 step,
  `Shift` to reverse, and explicit `Assign` / `Unassign`, with no blocking
  popup.
- **A Deck × Output routing matrix** in the Video tab: click row labels to focus
  decks, column headers to focus outputs, and cells to assign. A `MOVE` /
  `ADD` toggle switches between single-output routing and fan-out, and an
  assigned cell nudges its layer directly (`click +1`, `Shift+click -1`, `Ctrl`
  for ×10).
- The main window carries an `Add Output` button and focused-route controls —
  `Link` / `Unlink`, `Layer-` / `Layer+` — with a plain status line:
  `Focused Route: Deck N -> Output N  Background/Layer N/Not Linked`.

### Per-output network streaming (SRT + RTMP)
- An ffmpeg-backed stream output per `OutputTarget`, with focused-output
  controls in the Video tab for enable, protocol (`SRT` / `RTMP`), URL and
  bitrate, and the commands `VIDEO STREAM ON|OFF|TOGGLE`,
  `VIDEO STREAM SRT|RTMP`, `VIDEO STREAM URL ...` and
  `VIDEO STREAM BITRATE ...`.
- The stream muxes H.264 video with AAC stereo audio, and the audio follows the
  output assignment stack, falling back to the host deck when there are no
  assignments.
- `OutputTarget` gains `outputType` (`window` or `stream`),
  `mirrorSourceOutputIndex`, `streamEnabled`, `streamProtocol`, `streamUrl` and
  `streamBitrateKbps`, with protocol normalisation, bitrate clamping
  (500..50000 kbps) and a default URL per output index and protocol.

### Master cues
- Group presets become operator-facing **Master Cues**: default names
  `Master Cue 1`, `Master Cue 2`, …, with `MASTER` / `MASTERCUE` command
  aliases alongside the existing `GROUP` commands, and toasts and prompts that
  speak in master-cue terms.
- A persistent `GroupPreset` + `GroupSlot` model — one slot per deck (`cueId` or
  bypass), with the focused index saved in the show — driven by `GROUP ADD`,
  `ADDEMPTY`, `SELECT`, `NAME`, `DELETE`, `SET <deck> <cue-token|SEL|ACTIVE|
  BYPASS>`, `BYPASS`, `CAPTURE SEL|ACTIVE` and `FIRE [index]`.
- Keyboard: `Ctrl+Shift+G` fires the focused preset, `Ctrl+Shift+N` creates one
  from the selected cues, and `Ctrl+Shift+[` / `]` cycle the focus.
- Master-cue rows show each deck's slot inline and fire from the row, and slot
  assignment happens in the window — click to assign, `Shift+click` for the
  active cue, middle or `Ctrl+click` for a picker, right-click to bypass — with
  no external dialog.

### Deck window and main window
- The main control window is output- and program-first; the separate decks
  window carries the deck playlists, per-deck transport (`Take`, `Go`, `Stop`)
  and per-deck header state — transport status, `tc` value and fps, and the
  chase/free and run/hold flags. The main panel shows the focused deck's number,
  name and timecode line.
- The deck tracker pages around the focused deck rather than always starting at
  deck 1, and reserves height for the playlist grid so lower deck columns stay
  visible with many decks.
- The decks window is always shown, at a larger default and minimum size.

### File controls
- `New`, `Open`, `Save` and `SaveAs` in the main header. `Save` writes the
  active show path immediately, and `SaveAs` writes as soon as a path is chosen.

### Frame-accurate trim
- `I` sets trim-in at the playhead and `O` sets trim-out, both snapped to cue
  frame boundaries using the cue's fps. `Shift+I` opens the import picker and
  `Shift+O` toggles the time overlay.
- Paused nudge: `Left/Right` for ±1 frame, `Shift` for ±5, `Ctrl` for ±10, and
  `Alt` for ±1.0 second, frame-snapped.

### Pattern workflow
- A `Pattern` button opens an in-app type picker, and a pattern cue's settings
  gain a `pattern` row: `-` / `+` cycles the base type and the centre toggles
  motion where the type supports it.
- Animated variants for the engineering patterns: `smpte-bars-motion`,
  `crosshatch-motion`, `checkerboard-motion` and the `full-*-motion` set, with
  the animation loop rebuilding for any animated type.
- Commands: `PATTERN SET <type>`, `PATTERN LIST` and the `PATTERN <type> MOTION`
  shorthand.

### Pocket Test
- Reworked into a deterministic tropical retro scene: sky, ocean and beach
  gradients, procedural palms and an island silhouette, an animated coin line
  and a retro explorer sprite, over the bottom signal reference strip.

### Quit and close
- `SDL_QUIT` exits immediately, closing the main window exits, closing the Decks
  window hides it cleanly, and closing an output window disarms that output.

### Geometry precision and status
- The output path applies independent X and Y scaling and respects the cue's
  scale mode (`Fit`/`Fill`/`Stretch`/`Unscaled`).
- `off X` / `off Y` step by 1px, and `scale X`, `scale Y`, `off X`, `off Y` and
  `rot` accept typed values, including simple expressions with `+`, `-`, `*`,
  `/` and parentheses.
- Status carries the output entities: `OUTPUT ...` rows in text `STATUS`
  (`type`, `host`, `display`, `layers`, `mirror`, stream state, url and
  bitrate), and `focusedOutput`, `outputCount` and an `outputs[]` array in
  `STATUS JSON`.
- The `Deckboy Decks` tracker takes a denser LSDJ-inspired pass: compact rows
  for every deck in view, `sel` and `act` cue-number columns, and a focused-deck
  highlight.

---

## March 2025 — the foundations

Deckboy began as a single-file prototype and was rebuilt into the modular
application these notes describe. That first pass established what everything
since has been built on:

- **A modular architecture**: eight logical modules — core, media, render,
  control, ui, platform, ndi, browser — with public APIs, a CMake compilation
  strategy, feature gates and a documented dependency graph.
- **`native/core/utils`**: 55 shared utilities — string operations, timecode
  parsing, colour conversion, field parsing, JSON escaping — with no external
  dependencies, underneath every other module.
- **`native/core/subprocess`**: one place for spawning and managing FFmpeg and
  capture processes.
- **Professional broadcast features**, each behind a feature gate with a
  graceful stub when its SDK is absent: cross-platform MIDI input through
  RtMidi, DeckLink 10-bit SDI output, Syphon and Spout texture sharing, and
  native browser rendering.
- **Cross-platform infrastructure**: platform abstraction, feature gates and
  GitHub Actions CI building on Linux, macOS and Windows.
- **GPL compliance and licensing** put in order at the same time.

Build it with `cmake .. && make -j4`, and `./deckboy-native --self-check`
reports what this machine can do:

```
deckboy-native self-check
project-root: "..."
font-sans: ok
font-mono: ok
font-pixel: ok
ffmpeg: ok
ffprobe: ok
ndi-sdk: not built (set DECKBOY_NDI_SDK or install SDK headers)
ui-sfx: enabled by separate SDL audio device when available
companion-control: tcp/udp port 5510 by default
```

Optional features are switched on at configure time —
`-DENABLE_MIDI=ON`, `-DENABLE_SIPHON=ON`, `-DENABLE_SPOUT=ON`, and
`-DENABLE_DECKLINK=ON -DDECKLINK_SDK=/path/to/sdk`.
