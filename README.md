# Deckboy

Deckboy is a native SDL2 desktop app, not a web app. Windows is the primary development target with a cross-platform code path (Linux and macOS supported), using native windows for control and program output and FFmpeg tools for ingest and playback decoding.
Project goal: keep Deckboy fully open source and ship first-class builds on Windows, Linux, and macOS.

For a structural map of the codebase see [`docs/CODEMAP.md`](docs/CODEMAP.md). Windows build instructions live in [`CLAUDE.md`](CLAUDE.md) (Build section).

**🚀 March 2025 Refactoring**: See [CHANGES.md](CHANGES.md) for a comprehensive summary of modular architecture improvements, broadcast SDK integration (MIDI, DeckLink, Syphon/Spout), cross-platform support, and automated CI/CD setup.

The UI is styled with a Game Boy-inspired look: monochrome green screen palette, chunky shell framing, cute "cartridge shelf" language, and a more playful control surface.

Cute extras are now optional:

- `UI sounds` use a separate SDL audio device when available, so they stay off the program playback stream.
- `UI transitions` are limited to the control window and do not alter the output window's media path.

## Versioning

- Deckboy now keeps its source-of-truth app version in [`VERSION`](VERSION).
- The native binary reports that value with `--version`.
- GitHub Actions validates `vX.Y.Z` tags against `VERSION` before running release-tag builds.
- See [`docs/VERSION_FLOW.md`](docs/VERSION_FLOW.md) for the version/release workflow.

## Current MVP

- Native control window plus separate native output windows
- Drag-and-drop import or native file picker import
- Playlist save, save-as, and open for different `.deckboy` show files
- Main header file controls in UI: `New`, `Open`, `Save`, `SaveAs`
- Main output strip in UI: one per-output toggle in the control window (`O1`, `O2`, ...), so each output can be armed/disarmed without opening Preferences.
- Main output strip now includes explicit routing controls for the focused deck/output:
  - `Add Output`
  - `Link` / `Unlink`
  - `Layer-` / `Layer+`
  - live status text (`Focused Route: Deck N -> Output N ...`)
- Main header includes a `decks` button that always shows/raises the separate deck workspace.
- Optional multi-deck show model with explicit Deck / Output / Layer entities
- Playback semantics controls in Preferences -> Audio:
  - `Jump Mode` (`Trigger`/`Load`) for `Take`/`Goto` behavior
  - `Jump Transition` toggle (apply or bypass cue/deck transition on jump)
  - `Panic Profile` selector + `Run Panic` action (`Outputs Off`, `Fade+Pause`, `Fade+Rewind`, `Fade+LoadNext`)
  - `Panic fade (sec)` and `Panic auto restore` options
  - focused-deck timecode follower controls: `TC jam` and `TC freewheel`
  - menu-only cue tools: `Find Cue...`, `Next`, `Prev`, `Find+Take`, `Renumber...`, `Clear Numbers`, `Clear Find`
- Main window is now output/program focused (no deck playlist column).
- Main window now includes a permanent left master-cue sidebar (master-cue home):
  - master-cue list rows (`MC#`, name, slot summary)
  - quick actions: `<MC`, `MC>`, `New`, `Del`, `Take`, `Name`, `CapSel`, `CapAct`
  - expandable programmer area (`Prog+` / `Prog-`) with per-deck slot assignment controls:
    - `Sel` = assign that deck's selected cue
    - `Act` = assign that deck's active cue
    - `Byp` = toggle bypass for that deck slot
    - `-` / `+` = cycle the slot cue number directly (no popup picker)
    - row click (outside buttons) = assign from that deck's currently selected cue
    - mouse wheel over a programmer row = cycle that slot cue (up/down)
  - row click focuses a master cue; row `Take` fires that master cue
- `Deckboy Decks` companion window now carries deck playlist control for multi-deck operation:
  - per-deck playlist columns (click cue rows to select, per-deck `Take`/`Stop` transport buttons)
  - per-deck transport/timecode summary in each deck column header
  - plus tracker-style deck overview (deck index, selected/active cue number, layer, transport state, timecode)
  - this Decks window stays hidden by default with one deck, auto-opens when a second deck is created, and can always be raised from the header `decks` button
- Project schema now includes explicit `OutputTarget` and `LayerAssignment` entities (backward-compatible with legacy deck routing fields)
- Independent deck runtimes for media/audio plus separate output runtimes for output windows/compositor
- Layered routing is assignment-driven (`LayerAssignment`), including deck fan-out to multiple outputs
- Preferences -> Video now has a larger layout with direct focused-output controls and a deck/output routing manager UI
- Video Outputs now includes explicit creation actions:
  - `Create Window` (Window Output)
  - `Create Stream` (Stream Output)
  - per-output signal-flow summary line in the status block
- Outputs are off by default; enable them from Preferences -> Video using the focused-output `OUTPUT ON/OFF` control. Enabling a window output immediately sends it fullscreen.
- Enabling or re-arming a `window` output now auto-switches sizing mode to display-native when fixed raster mode was active (`auto native` quality safety).
- Loaded shows are also disarmed on open/launch (outputs forced off until operator enables), to prevent startup screen takeover.
- Preferences -> Video now includes top-right display controls (`Prev`, `Next`, `Rescan`) with the current display label shown inline.
- Preferences -> Video now includes a `Connected Displays` list in the Video tab; click any detected display row to target the focused output directly.
- When an enabled window output changes display target, Deckboy now re-fullscreens and raises that output window on the chosen display automatically.
- Repeating output enable (`VIDEO OUTPUT ON` or the Video-tab `Enabled` switch while already ON) now triggers output recovery instead of doing nothing.
- Enabled window outputs are now self-healed in the background (1 Hz): hidden/minimized/off-display/not-fullscreen outputs are re-positioned, raised, and re-fullscreened.
- `Esc` escape now pauses auto-recovery for the escaped output so it stays windowed long enough for operator control.
- Triple-`Esc` panic safety: pressing `Esc` three times quickly (`~0.9s` gaps) in output safety context disarms all outputs (`outputs off`).
- `F` or repeated `VIDEO OUTPUT ON` explicitly re-arms fullscreen recovery for that output.
- Video routing manager now shows explicit lists for `Decks` and `Outputs`, plus a focused route editor (`Assign`/`Unassign`, layer `-`/`+`).
- Routing labels are plain English (`Deck 1`, `Output 1`, `Background`, `Layer 2`, `None`) instead of shorthand codes.
- Preferences -> Video `Mirror` control now opens a direct mirror-source picker (`off` or any other output), instead of cycling blindly.
- Video-tab status text is clipped to a left info panel and no longer overlaps nearby controls.
- Display hot-plug is now watched at runtime; connecting/disconnecting monitors triggers automatic display topology refresh (and `Rescan` can be used manually).
- Single-instance safety lock is enabled by default to prevent runaway duplicate launches; use `--allow-multi-instance` only when intentionally testing multi-launch behavior.
- Output targets now support `window` and `stream` types
- Stream outputs can either render their own assigned deck/layer stack or mirror another output feed
- Per-output network stream output (`SRT` or `RTMP`) via ffmpeg with URL + bitrate controls and an H.264/AAC transport mux
- FFprobe metadata ingest for video clips and stills
- FFmpeg-driven video frame decode and audio decode
- Cue list, selection, drag reorder, take, play/pause, stop, clear, seek, volume
- Cue controls for fade in, fade out, loop, hold on last frame, in-point, and out-point
- **Per-cue separate X/Y scaling** for aspect ratio correction and distortion effects (0.25x to 4.0x)
- **Scale modes** (Fit / Fill / Stretch / Unscaled) for flexible canvas composition
- Per-cue geometry and color controls: offset, rotation, crop, chroma key, brightness, contrast, saturation, hue
- Geometry size is edited in output pixels: `width`/`height` rows show the rendered px size, clicking the value prompts for an exact pixel value, and the `link aspect` toggle (on by default) keeps the aspect ratio when either axis changes. Offsets (`off X/Y`) and `rot` also support exact entry, all with simple calculator expressions (`+ - * / ()`)
- Geometry nudge controls now use `1px` offset steps (`off X/Y`) for finer placement
- Deck transition engine with `cut` / `crossfade` / `dip` styles
- Playlist loop plus per-cue end behavior driven by hold / end settings
- Browser cues rendered **into** the output window via Xvfb + ffmpeg x11grab — smooth transitions and program monitor preview, just like any other cue
- Live source cues (shared `SOURCE ...` command path + `source://...` scheme):
  - `Window Source`: live X11 screen/window capture via ffmpeg `x11grab` (Linux)
  - `Camera Source`: live device capture via ffmpeg `v4l2` (Linux)
  - `Syphon/Spout Source`: cue/runtime path implemented; Linux currently uses desktop-capture fallback while native Syphon/Spout backends remain roadmap
- Engineering test patterns: SMPTE 75% colour bars, crosshatch, checkerboard, full-field (white/black/red/green/blue), plus motion variants for all standard patterns
- New engineering pattern cues default to `hold`, so taking a test pattern does not auto-clear the output unless you explicitly set a duration
- Motion test patterns loop cleanly, and `crosshatch-motion` now uses a gentler horizontal drift so the preview stays readable
- The timeline audio lane now shows an animated analysis/loading state while waveform peaks are still being generated
- The focused-deck VU meter now falls back to zero naturally when the active cue has no audio
- **Pocket Test** — full-colour procedural tropical platform-adventure scene pack:
  - scene cycle: day, sunset, night, storm
  - creatures/elements: crab, jumping fish, parrot, turtle, dino-style enemy, puff friend, coins, explorer
  - plus signal reference strip at bottom
- Compatibility support for experimental overlay / scene cues:
  - existing `Lower Third`, `PIP`, and `Composite` cues still load, save, and render
  - new authoring for those cue types is temporarily parked on `deckboy-0.60`
    while audit / cleanup work takes priority
- Master Cues (internal model: group presets) for simultaneous multi-deck firing (per-deck cue slot or bypass)
- Audio output device selection and output display selection
- Video output mode control (display-native EDID mode or fixed raster presets up to 4K UHD)
- Changing an output display target now auto-switches to display-native sizing (`auto native`) for best output quality.
- Refresh-rate-aware fullscreen mode targeting for custom EDID timings
- Optional per-output NDI output (fill/key sender name + enable/disable)
- Per-output output FX controls in Video Outputs:
  - output alpha/dimmer (`0-100%`)
  - output delay (`0-5000 ms`) applied to NDI/stream egress frame path
  - output-scoped time/ID overlay toggle
  - output color-space mode (`AUTO`/`BT709`/`SRGB`) with stream encoder metadata flags
- Optional deck-local time overlay in output
- Cue IDs with ID-targeted select/take and `GOTO` search
- Timecode chase layer (manual or OSC-fed), cue timecode marks, and trigger take
- Output fullscreen toggle
- Companion control over a native TCP/UDP command port
- OSC input support (single messages + bundles) and OSC state feedback/ack replies
- Bundled show export (`BUNDLE` in the toolbar or `Ctrl+Shift+E`) copies file-backed
  cue media into a sibling `<show>_media/` folder and saves the exported show with
  relative media paths for handoff/move-safe playback
- Built-in smoke harness (`--smoke` and `scripts/smoke.sh`)
- Persistent show file in `data/project.deckboy`

## Run

Build and launch the native app:

```bash
cd /home/james/deckboy
chmod +x bin/deckboy bin/deckboy-native
./bin/deckboy
```

The launcher builds with CMake automatically and then runs the native binary.

Useful options:

```bash
./build/native/Deckboy --self-check
./build/native/Deckboy --smoke
./build/native/Deckboy --version
./build/native/Deckboy --allow-multi-instance
./scripts/smoke.sh
./scripts/generate_demo_shows.sh
```

Demo show files are generated in `data/demos/` (including a `70/30 + 4 PiP over BG` 5-deck example).

To change the Companion port:

```bash
DECKBOY_COMPANION_PORT=5610 ./bin/deckboy
```

## Controls

- `Enter`: jump selected cue (Trigger/Load based on `Jump Mode` in Preferences -> Audio)
- `Space`: play/pause active cue
- `S`: stop
- `C`: clear output
- `F`: toggle output fullscreen
- `I`: set trim-in at current playhead (active video cue, frame-snapped)
- `Shift+I`: import media
- Main control bar `SOURCE` button: add `Window Source` / `Camera Source` / `Syphon/Spout Source` cues
- `B`: add browser cue
- `M`: add composite scene cue
- Main control bar `Pattern` button: open test-pattern menu and add any pattern type
- Pattern cue settings row (`pattern`): in-menu `- / +` cycles type, center toggle switches motion on/off
- Newly added pattern cues default to `hold`; set a duration if you want them to auto-advance
- `P`: add currently selected default pattern type (keyboard optional)
- `L`: toggle selected cue loop
- `E`: toggle selected cue hold on last frame
- `[` / `]`: adjust fade in on selected video cue
- `Shift` + `[` / `]`: adjust fade out on selected video cue
- `Up` / `Down`: move selection
- `Shift` + `Up` / `Down`: reorder selected cue
- `Left` / `Right` (while paused on active video): nudge playhead by `1` frame
- `Shift` + `Left` / `Right` (while paused on active video): nudge by `5` frames
- `Ctrl` + `Left` / `Right` (while paused on active video): nudge by `10` frames
- `Alt` + `Left` / `Right` (while paused on active video): nudge by `1.0s` (frame-snapped)
- `Delete`: remove selected cue
- `1`: toggle UI sounds
- `2`: toggle UI transitions
- `4`: toggle playlist loop
- `A`: cycle audio output device
- `D`: cycle output display
- `N`: toggle NDI output for the focused output
- `O`: set trim-out at current playhead (active video cue, frame-snapped)
- `Shift+O`: toggle time overlay for the focused deck
- `T`: toggle focused deck timecode run mode
- `5`: toggle focused deck timecode chase mode
- `Ctrl+C`: copy selected cue settings
- `Ctrl+V`: paste copied cue settings to selected cue(s)
- `Ctrl+Shift+C`: copy focused deck warp settings
- `Ctrl+Shift+V`: paste copied warp settings to focused deck
- `Ctrl+S`: save current playlist
- `Ctrl+Shift+E`: export bundled project with copied media
- `Ctrl+O`: open playlist
- `Ctrl+Shift+S`: save playlist as
- `Ctrl+N`: add a new deck
- `Ctrl+Enter`: take selected cue on **all** decks simultaneously
- `Ctrl+Space`: play/pause **all** decks simultaneously
- `Esc`: emergency exit from fullscreen output takeover; escaped outputs stay windowed (auto-recovery paused) until explicitly re-armed
- `Esc` (three quick presses): panic disarm all outputs (`outputs off`) during output safety context
- `Tab`: focus next deck
- `Shift+Tab`: focus previous deck
- Click a deck row or cue row in the `Deckboy Decks` Decks window to focus that deck
- Master-cue programming is fully menu-driven in the left sidebar:
  - `CapSel` / `CapAct` captures all deck selections/actives into focused master cue
  - `Name` renames focused master cue
  - per-deck `Sel` / `Act` / `Byp` / `-` / `+` edits each slot directly

## Companion

Use Companion's `Generic TCP/UDP` connection and point it at the machine running Deckboy.

- Host: the Deckboy machine IP
- Port: `5510` by default
- Protocol: either TCP or UDP

Official Stream Deck mapping package:
- [`docs/streamdeck/README.md`](docs/streamdeck/README.md)
- [`docs/streamdeck/deckboy_companion_profile_map.json`](docs/streamdeck/deckboy_companion_profile_map.json)

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
WIDTH 960
HEIGHT 540
SFX ON
SFX OFF
ANIM ON
ANIM OFF
JUMPMODE TRIGGER
JUMPMODE LOAD
JUMPMODE TOGGLE
JUMPTRANSITION ON
JUMPTRANSITION OFF
PANICPROFILE NEXT
PANICPROFILE FADE_REWIND
PANICFADE 1.2
PANICAUTORESTORE ON
PANIC
PANIC FADE_LOAD_NEXT
FIND intro
FINDNEXT
FINDPREV
FINDTAKE 12A
FINDCLEAR
FINDSTATUS
RENUMBER Q 1
RENUMBER CLEAR
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
TIMECODE JAM ON
TIMECODE FREEWHEEL 1.5
PATTERN
PATTERN LIST
PATTERN SET smpte-bars
PATTERN pocket-test
PATTERN pocket-day
PATTERN pocket-sunset
PATTERN pocket-night
PATTERN pocket-storm
PATTERN smpte-bars
PATTERN smpte-bars-motion
PATTERN crosshatch
PATTERN crosshatch-motion
PATTERN checkerboard
PATTERN checkerboard-motion
PATTERN full-white
PATTERN full-black
PATTERN full-red
PATTERN full-green
PATTERN full-blue
PATTERN full-white-motion
PATTERN full-black-motion
PATTERN full-red-motion
PATTERN full-green-motion
PATTERN full-blue-motion
PATTERN smpte-bars MOTION
BROWSER https://example.com
ALLTAKE
ALLGO
ALLPLAY
ALLPAUSE
ALLSTOP
MASTER ADD Opener
MASTER NEXT
MASTER NAME Main Opener
MASTER SET 1 12
MASTER SET 2 BYPASS
MASTER CAPTURE SEL
MASTER FIRE
AUDIO NEXT
AUDIO DEFAULT
DISPLAY NEXT
DISPLAY 2
VIDEO
VIDEO NATIVE
VIDEO 4K
VIDEO 1920x1080
VIDEO CUSTOM 3440x1440
VIDEO 3840x2160@50
VIDEO REFRESH AUTO
VIDEO REFRESH NEXT
VIDEO REFRESH 59.94
VIDEO SIZE DISPLAY
VIDEO OUTPUT NEXT
VIDEO OUTPUT 2
VIDEO OUTPUT ADD
VIDEO OUTPUT ADD STREAM
VIDEO OUTPUT ON
VIDEO OUTPUT OFF
VIDEO OUTPUT TOGGLE
VIDEO OUTPUT ASSIGN
VIDEO OUTPUT ASSIGN 5
VIDEO OUTPUT HOST 1
VIDEO OUTPUT TYPE STREAM
VIDEO OUTPUT TYPE WINDOW
VIDEO OUTPUT MIRROR 1
VIDEO OUTPUT MIRROR OFF
VIDEO OUTPUT ALPHA 85
VIDEO OUTPUT DELAY 250
VIDEO OUTPUT OVERLAY ON
VIDEO OUTPUT COLORSPACE BT709
VIDEO STREAM ON
VIDEO STREAM OFF
VIDEO STREAM SRT
VIDEO STREAM RTMP
VIDEO STREAM URL srt://127.0.0.1:9000?mode=caller
VIDEO STREAM BITRATE 6500
NDI ON
NDI OFF
NDI NAME Stage Left Feed
SOURCE WINDOW active-window
SOURCE CAMERA default-camera
SOURCE SYPHON default-bus
OVERLAY ON
OVERLAY OFF
OVERLAY PUSH 3
OVERLAY POP
OVERLAY CLEAR
DECK 2
DECK 2 TAKE
DECK 2 SELECT 3
VIDEO OUTPUT 2
NDI ON
DECKNEXT
DECKPREV
NEWDECK
STATUS
STATUS 2
STATUS CUES
STATUS FIND
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
- `PATTERN SET <type>` sets the default pattern used by `P` and bare `PATTERN`.
- Add / edit patterns from UI menus too: main `Pattern` button and cue-settings `pattern` row.
- `AUDIO ...` uses SDL output device names; `DEFAULT` returns to the system default output.
- `DISPLAY 2` means the second display.
- `VIDEO NATIVE` follows the selected display desktop mode (EDID/OS resolution).
- `VIDEO 4K` sets a fixed 3840x2160 output raster.
- `VIDEO 1920x1080` (or any `WIDTHxHEIGHT`) sets a fixed custom raster.
- `VIDEO CUSTOM 3440x1440` is an alias form for custom EDID/timing rasters.
- `VIDEO 3840x2160@50` sets raster and target refresh together.
- `VIDEO REFRESH ...` controls target fullscreen refresh (auto/next/prev/specific Hz).
- `VIDEO SIZE DISPLAY` repositions/resizes the focused output to the selected display.
- `VIDEO OUTPUT ...` controls focused output selection, creation, enable/disable state, host deck, and focused-deck assignment.
- `VIDEO OUTPUT ON` enables the focused output and fullscreenes window outputs immediately; `VIDEO OUTPUT OFF` hides/stops that output feed.
- After `Esc` windowed escape, repeated `VIDEO OUTPUT ON` also re-arms recovery/fullscreen for that output.
- `VIDEO OUTPUT TYPE ...` changes focused output target type (`WINDOW` or `STREAM`).
- `VIDEO OUTPUT MIRROR ...` lets a stream output mirror another output feed (or `OFF` to render own assignments).
- `VIDEO OUTPUT ALPHA ...` sets per-output dimmer/opacity (`0-100` or `0.0-1.0`).
- `VIDEO OUTPUT DELAY ...` sets per-output egress delay (`ms`) for NDI/stream frame sends.
- `VIDEO OUTPUT OVERLAY ...` toggles per-output time/ID overlay.
- `VIDEO OUTPUT COLORSPACE ...` sets output color-space mode (`AUTO`, `BT709`, `SRGB`); stream outputs apply matching encoder color metadata.
- `VIDEO STREAM ...` controls the focused output's network stream target (`SRT`/`RTMP`), URL, and bitrate.
- Local SRT loopback uses an external listener, for example:
  `ffplay -fflags nobuffer -flags low_delay "srt://0.0.0.0:9000?mode=listener&transtype=live"`
- `MASTER ...` (alias: `GROUP ...`) manages master-cue presets for multi-deck simultaneous cue firing.
- Quick keys for master cues: `Ctrl+Shift+G` fires the focused Master Cue, `Ctrl+Shift+N` creates one from current selections, `Ctrl+Shift+[` / `Ctrl+Shift+]` cycles focus.
- `NDI NAME ...` renames the NDI sender for the focused output.
- `OVERLAY ...` still controls deck-local time/ID overlay state for the focused deck.
- `VIDEO OUTPUT OVERLAY ...` controls output-scoped overlay state on the focused output target.
- `TRANSITION ...` controls deck transition time/style.
- `TIMECODE ...` controls deck timecode clock/chase behavior.
- `TIMECODE JAM ...` and `TIMECODE FREEWHEEL ...` control chase follower behavior.
- `TCMARK ...` sets or clears the selected cue's timecode trigger mark.
- `FIND ...` / `FINDNEXT` / `FINDPREV` / `FINDTAKE ...` provide menu-free cue lookup.
- `FINDCLEAR` clears current find token/match state; `FINDSTATUS` reports active find state.
- `RENUMBER ...` / `AUTOID ...` bulk-assign cue numbers.
- `SELECTID`/`TAKEID` target cues by stored cue ID.
- `GOTO` accepts cue number, cue ID, or partial cue name.
- `IN`/`OUT` and `TRIM CLEAR` control selected cue trim points.
- `WIDTH <px>` / `HEIGHT <px>` set the selected cue's rendered size in output pixels (respects the aspect link; legacy `SCALE`/`SCALEX`/`SCALEY` factor commands still work).
- `DECK 2 TAKE` switches focus to deck 2 and runs the nested command there.
- `STATUS` and `STATE` return a multi-line TCP snapshot of all decks and outputs.
- `STATUS 2` or `STATE 2` returns a single deck snapshot.
- `STATUS CUES` / `STATE CUES` returns cue-programming snapshot (selected/active cue number+id by deck).
- `STATUS FIND` / `FINDSTATUS` returns active find token/match summary.
- `STATUS JSON` or `STATE JSON` returns a JSON snapshot including output entities (`focusedOutput`, `outputCount`, `outputs[]`), panic settings, timecode follower fields, and find state.

### OSC Input

Deckboy also accepts OSC over UDP on the Companion port (`5510` by default).
Supported OSC addresses include:

- `/go`, `/play`, `/pause`, `/stop`, `/clear`, `/next`, `/prev`
- `/select i`, `/take i`, `/goto s`
- `/deck i`, `/deck/next`, `/deck/prev`
- `/find s`, `/find/next`, `/find/prev`, `/find/take s`, `/find/clear`, `/renumber s`
- `/volume f`, `/seek f`
- `/autonext i`, `/playlistloop i`
- `/transition f`, `/transition/style s`
- `/ndi i`, `/ndi/name s`
- `/video s`
- `/overlay i`, `/timeoverlay i`
- `/in f`, `/out f`, `/trim/clear`
- `/timecode s|f`, `/timecode/chase i`, `/timecode/run i`, `/timecode/fps f`, `/timecode/jam i`, `/timecode/freewheel f`, `/timecode/mark s`
- `/status`, `/state`, `/ping`

Notes:

- OSC supports both single messages and `#bundle` packets.
- OSC values are mapped into the same internal command path used by Companion text commands.
- OSC senders receive `/deckboy/ack` replies for accepted commands.
- OSC senders can query `/status` or `/state` and receive `/deckboy/state` JSON replies.
- Recent OSC senders also receive periodic `/deckboy/state` feedback broadcasts.

## Notes

- This machine has the runtime pieces needed for the native build: `g++`, `cmake`, `SDL2`, `SDL2_ttf`, `ffmpeg`, and `ffprobe`.
- The older browser prototype is still on disk for reference and can be launched with `./bin/deckboy-web`, but it is no longer the default path.
- Browser cues now flow through `native/platform/browser.*` as a backend seam.
- The current Linux browser backend still relies on an external Chromium-family browser being available on the machine.
- If a Dante or network audio device appears to the OS as a normal output device, Deckboy can select it the same way it selects any other SDL audio output. True native Dante routing/control is not implemented yet.
- NDI output is now optional and output-local. If the app finds NDI SDK headers at build time, it can dynamically load `libndi` at runtime and publish each enabled output as a network source.
- Output NDI fill includes composited output video plus mixed stereo output audio; optional key output publishes a separate matte stream.
- Output delay control currently applies to NDI/stream egress paths; window-output presentation remains immediate.
- If NDI runtime libraries are not on your system path, set `DECKBOY_NDI_LIB` to the full runtime library path.
- Window Source / Camera Source / Syphon/Spout Source cues now run through real source transport in the native runtime:
  - `Window Source` = X11 capture path
  - `Camera Source` = V4L2 capture path
  - `Syphon/Spout Source` = Linux desktop fallback path (native Syphon/Spout remains planned for macOS/Windows parity)
- Multi-output is now output-entity based. `Deckboy_0.01` persists `OutputTarget` + `LayerAssignment`, and output windows are managed separately from deck runtimes.
- Output create/assignment/type/mirror controls now ship in Preferences -> Video and via `VIDEO OUTPUT ...` commands.
- Output network streaming (`SRT` + `RTMP`) is now per-output and ffmpeg-backed; stream outputs can mirror another output feed or render their own assignments.
- Stream outputs now mux H.264 video + AAC stereo audio. Audio follows the output's assigned deck stack (with host-deck fallback when no assignments are present).
- NDI metadata triggers are now live on Linux/macOS builds through a runtime-loaded `libndi` receive bridge. If the runtime library is not on your system path, set `DECKBOY_NDI_LIB`. If you need Deckboy to target a specific NDI source name, set `DECKBOY_NDI_TRIGGER_SOURCE`.
- NMC transport sync is now live on Linux/macOS builds as a UDP transport/locate bridge. Default mode is `input` on UDP `51010`; use `DECKBOY_NMC_MODE=output` to broadcast, `DECKBOY_NMC_PORT` to change port, `DECKBOY_NMC_HOST` to choose the output target, `DECKBOY_NMC_SOURCE` to filter inbound senders, and `DECKBOY_NMC_LOCATE_MS` to change rolling locate cadence.
- Third-party NMC interop still needs validation with a real source.
- Future upgrades: DeckLink class outputs, multichannel audio routing, and a more robust decode backend than subprocess-driven FFmpeg piping.
- LTC ingest is now live on Linux/macOS builds through the default SDL capture input. If the runtime library is not on your system path, set `DECKBOY_LTC_LIB`. If you need a specific input device, set `DECKBOY_LTC_DEVICE` to the capture-device name SDL should open.
