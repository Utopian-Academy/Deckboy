# Playboy_0.01

Playboy_0.01 is now a native SDL2 desktop app, not a web app. The current direction is Linux-first with a cross-platform code path, using native windows for control and program output and FFmpeg tools for ingest and playback decoding.

The UI is styled with a Game Boy-inspired look: monochrome green screen palette, chunky shell framing, cute "cartridge shelf" language, and a more playful control surface.

Cute extras are now optional:

- `UI sounds` use a separate SDL audio device when available, so they stay off the program playback stream.
- `UI transitions` are limited to the control window and do not alter the output window's media path.

## Current MVP

- Native control window plus separate native output window
- Drag-and-drop import or native file picker import
- Playlist save, save-as, and open for different `.playboy` show files
- Optional multi-deck show file model with deck-local playlist state and routing
- Simultaneous independent deck runtimes with one output window and one audio path per deck
- FFprobe metadata ingest for video clips and stills
- FFmpeg-driven video frame decode and audio decode
- Cue list, selection, drag reorder, take, play/pause, stop, clear, seek, volume
- Cue controls for fade in, fade out, loop, hold on last frame, in-point, and out-point
- Deck transition engine with `cut` / `crossfade` / `dip` styles
- Playlist controls for auto-advance and playlist loop
- Browser cues rendered **into** the output window via Xvfb + ffmpeg x11grab — smooth transitions and program monitor preview, just like any other cue
- Engineering test patterns: SMPTE 75% colour bars, crosshatch, checkerboard, full-field (white/black/red/green/blue)
- **Pocket Test** — full-colour animated kawaii procedural scene (day/night cycle, walking girl, clouds, sparkles) plus signal reference strip at bottom
- Multi-layer overlay compositor: stack up to 4 lower-third/graphic cues in z-order (`OVERLAY PUSH/POP/CLEAR`)
- Audio output device selection and output display selection
- Video output mode control (display-native EDID mode or fixed raster presets up to 4K UHD)
- Optional per-deck NDI output (video + audio sender name + enable/disable)
- Optional deck-local time overlay in output
- Cue IDs with ID-targeted select/take and `GOTO` search
- Timecode chase layer (manual or OSC-fed), cue timecode marks, and trigger take
- Output fullscreen toggle
- Companion control over a native TCP/UDP command port
- OSC input support (single messages + bundles) and OSC state feedback/ack replies
- Built-in smoke harness (`--smoke` and `scripts/smoke.sh`)
- Persistent show file in `data/project.playboy`

## Run

Build and launch the native app:

```bash
cd /home/user/playboy
chmod +x bin/playboy bin/playboy-native
./bin/playboy
```

The launcher builds with CMake automatically and then runs the native binary.

Useful options:

```bash
./build/native/playboy-native --self-check
./build/native/playboy-native --smoke
./scripts/smoke.sh
```

To change the Companion port:

```bash
PLAYBOY_COMPANION_PORT=5610 ./bin/playboy
```

## Controls

- `Enter`: take selected cue
- `Space`: play/pause active cue
- `S`: stop
- `C`: clear output
- `F`: toggle output fullscreen
- `I`: import media
- `B`: add browser cue
- `P`: add kawaii test pattern cue
- `L`: toggle selected cue loop
- `E`: toggle selected cue hold on last frame
- `[` / `]`: adjust fade in on selected video cue
- `Shift` + `[` / `]`: adjust fade out on selected video cue
- `Up` / `Down`: move selection
- `Shift` + `Up` / `Down`: reorder selected cue
- `Delete`: remove selected cue
- `1`: toggle UI sounds
- `2`: toggle UI transitions
- `3`: toggle playlist auto-advance
- `4`: toggle playlist loop
- `A`: cycle audio output device
- `D`: cycle output display
- `N`: toggle NDI output for the focused deck
- `O`: toggle time overlay for the focused deck
- `T`: toggle focused deck timecode run mode
- `5`: toggle focused deck timecode chase mode
- `Ctrl+S`: save current playlist
- `Ctrl+O`: open playlist
- `Ctrl+Shift+S`: save playlist as
- `Ctrl+N`: add a new deck
- `Ctrl+Enter`: take selected cue on **all** decks simultaneously
- `Ctrl+Space`: play/pause **all** decks simultaneously
- `Tab`: focus next deck
- `Shift+Tab`: focus previous deck
- Click a deck card in the control window to focus it

## Companion

Use Companion's `Generic TCP/UDP` connection and point it at the machine running Playboy.

- Host: the Playboy machine IP
- Port: `5510` by default
- Protocol: either TCP or UDP

Each Companion button can send a plain text command. Good starting commands:

```text
PING
GO
PLAY
PAUSE
STOP
CLEAR
FULLSCREEN
NEXT
PREV
SELECT 3
SELECTID cue-abc123
TAKE
TAKE 3
TAKEID cue-abc123
GOTO 3
GOTO cue-abc123
GOTO opener
VOLUME 75
SEEK 12.5
IN 2.0
OUT 8.5
TRIM CLEAR
SFX ON
SFX OFF
ANIM ON
ANIM OFF
DELETE
LOOP ON
LOOP OFF
HOLD ON
HOLD OFF
FADEIN 1.5
FADEOUT 1.0
AUTONEXT ON
AUTONEXT OFF
PLAYLISTLOOP ON
PLAYLISTLOOP OFF
TRANSITION 0.75
TRANSITION STYLE DIP
TRANSITIONSTYLE CROSSFADE
TCMARK NOW
TCMARK 00:00:12:10
TCMARK CLEAR
TIMECODE 00:01:02:12
TIMECODE SET 12.5
TIMECODE CHASE ON
TIMECODE RUN ON
TIMECODE FPS 29.97
TIMECODE TRIGGER ON
PATTERN
PATTERN pocket-test
PATTERN smpte-bars
PATTERN crosshatch
PATTERN checkerboard
PATTERN full-white
PATTERN full-black
BROWSER https://example.com
ALLTAKE
ALLGO
ALLPLAY
ALLPAUSE
ALLSTOP
AUDIO NEXT
AUDIO DEFAULT
DISPLAY NEXT
DISPLAY 2
VIDEO
VIDEO NATIVE
VIDEO 4K
VIDEO 1920x1080
VIDEO CUSTOM 3440x1440
VIDEO SIZE DISPLAY
NDI ON
NDI OFF
NDI NAME Stage Left Feed
OVERLAY ON
OVERLAY OFF
OVERLAY PUSH 3
OVERLAY POP
OVERLAY CLEAR
DECK 2
DECK 2 TAKE
DECK 2 SELECT 3
DECK 2 NDI ON
DECKNEXT
DECKPREV
NEWDECK
STATUS
STATUS 2
STATUS JSON
STATE
STATE JSON
```

Notes:

- Cue numbers are `1`-based.
- `TAKE` uses the currently selected cue.
- `TAKE 3` selects cue 3 and takes it.
- `GO` toggles play/pause, or takes the selected cue live if nothing is active yet.
- `BROWSER ...` adds a new browser cue to the current playlist.
- `AUDIO ...` uses SDL output device names; `DEFAULT` returns to the system default output.
- `DISPLAY 2` means the second display.
- `VIDEO NATIVE` follows the selected display desktop mode (EDID/OS resolution).
- `VIDEO 4K` sets a fixed 3840x2160 output raster.
- `VIDEO 1920x1080` (or any `WIDTHxHEIGHT`) sets a fixed custom raster.
- `VIDEO CUSTOM 3440x1440` is an alias form for custom EDID/timing rasters.
- `VIDEO SIZE DISPLAY` repositions/resizes the focused output to the selected display.
- `NDI NAME ...` renames the NDI sender for the focused deck.
- `OVERLAY ...` toggles the output time/ID overlay for the focused deck.
- `TRANSITION ...` controls deck transition time/style.
- `TIMECODE ...` controls deck timecode clock/chase behavior.
- `TCMARK ...` sets or clears the selected cue's timecode trigger mark.
- `SELECTID`/`TAKEID` target cues by stored cue ID.
- `GOTO` accepts cue number, cue ID, or partial cue name.
- `IN`/`OUT` and `TRIM CLEAR` control selected cue trim points.
- `DECK 2 TAKE` switches focus to deck 2 and runs the nested command there.
- `STATUS` and `STATE` return a multi-line TCP snapshot of all decks.
- `STATUS 2` or `STATE 2` returns a single deck snapshot.
- `STATUS JSON` or `STATE JSON` returns a JSON snapshot.

### OSC Input

Playboy also accepts OSC over UDP on the Companion port (`5510` by default).
Supported OSC addresses include:

- `/go`, `/play`, `/pause`, `/stop`, `/clear`, `/next`, `/prev`
- `/select i`, `/take i`, `/goto s`
- `/deck i`, `/deck/next`, `/deck/prev`
- `/volume f`, `/seek f`
- `/autonext i`, `/playlistloop i`
- `/transition f`, `/transition/style s`
- `/ndi i`, `/ndi/name s`
- `/video s`
- `/overlay i`, `/timeoverlay i`
- `/in f`, `/out f`, `/trim/clear`
- `/timecode s|f`, `/timecode/chase i`, `/timecode/run i`, `/timecode/fps f`, `/timecode/mark s`
- `/status`, `/state`, `/ping`

Notes:

- OSC supports both single messages and `#bundle` packets.
- OSC values are mapped into the same internal command path used by Companion text commands.
- OSC senders receive `/playboy/ack` replies for accepted commands.
- OSC senders can query `/status` or `/state` and receive `/playboy/state` JSON replies.
- Recent OSC senders also receive periodic `/playboy/state` feedback broadcasts.

## Notes

- This machine has the runtime pieces needed for the native build: `g++`, `cmake`, `SDL2`, `SDL2_ttf`, `ffmpeg`, and `ffprobe`.
- The older browser prototype is still on disk for reference and can be launched with `./bin/playboy-web`, but it is no longer the default path.
- Browser cues currently rely on a Chromium-family browser already being available on the machine.
- If a Dante or network audio device appears to the OS as a normal output device, Playboy can select it the same way it selects any other SDL audio output. True native Dante routing/control is not implemented yet.
- NDI output is now optional and deck-local. If the app finds NDI SDK headers at build time, it can dynamically load `libndi` at runtime and publish each enabled deck as a network source.
- NDI output includes the deck audio stream for video cues.
- If NDI runtime libraries are not on your system path, set `PLAYBOY_NDI_LIB` to the full path for `libndi.so.6`.
- Multi-output is still optional. `Playboy_0.01` now saves multiple decks in the project file, and each deck has its own playlist, selection, active cue pointer, auto-advance, loop setting, audio target, display target, output window, and transport runtime.
- For deeper PlaybackPro/Mitti parity, the next logical upgrades are LTC/MTC ingest, DeckLink class outputs, and a more robust decode backend than subprocess-driven FFmpeg piping.
