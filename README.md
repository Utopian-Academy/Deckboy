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
- FFprobe metadata ingest for video clips and stills
- FFmpeg-driven video frame decode and audio decode
- Cue list, selection, drag reorder, take, play/pause, stop, clear, seek, volume
- Cue controls for fade in, fade out, loop, and hold on last frame
- Playlist controls for auto-advance and playlist loop
- Built-in kawaii test pattern cue
- Browser cues that launch a clean Chromium-style output window on the target display
- Audio output device selection and output display selection
- Output fullscreen toggle
- Companion control over a native TCP/UDP command port
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
- `Ctrl+S`: save current playlist
- `Ctrl+O`: open playlist
- `Ctrl+Shift+S`: save playlist as

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
TAKE
TAKE 3
VOLUME 75
SEEK 12.5
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
PATTERN
BROWSER https://example.com
AUDIO NEXT
AUDIO DEFAULT
DISPLAY NEXT
DISPLAY 2
```

Notes:

- Cue numbers are `1`-based.
- `TAKE` uses the currently selected cue.
- `TAKE 3` selects cue 3 and takes it.
- `GO` toggles play/pause, or takes the selected cue live if nothing is active yet.
- `BROWSER ...` adds a new browser cue to the current playlist.
- `AUDIO ...` uses SDL output device names; `DEFAULT` returns to the system default output.
- `DISPLAY 2` means the second display.

## Notes

- This machine has the runtime pieces needed for the native build: `g++`, `cmake`, `SDL2`, `SDL2_ttf`, `ffmpeg`, and `ffprobe`.
- The older browser prototype is still on disk for reference and can be launched with `./bin/playboy-web`, but it is no longer the default path.
- Browser cues currently rely on a Chromium-family browser already being available on the machine.
- If a Dante or network audio device appears to the OS as a normal output device, Playboy can select it the same way it selects any other SDL audio output. True native Dante routing/control is not implemented yet.
- Multi-output is not on by default. The current app is still single-program-output; the next large refactor is an optional advanced mode with multiple independent outputs/decks, each with its own playlist, transport, display target, and audio routing.
- For deeper PlaybackPro/Mitti parity, the next logical upgrades are timecode, true multi-output/deck architecture, transitions between cues, and a more robust decode backend than subprocess-driven FFmpeg piping.
