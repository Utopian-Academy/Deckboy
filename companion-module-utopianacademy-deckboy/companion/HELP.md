## Deckboy

Controls [Deckboy](https://github.com/Utopian-Academy/Deckboy), a free, open-source cue deck for
theatre, live events and broadcast. Buttons carry **tally, transport state, output health and a
countdown**, because the module polls Deckboy's status rather than only sending commands.

### Connect

| Field                | Default     | Notes                                                             |
| -------------------- | ----------- | ----------------------------------------------------------------- |
| Deckboy IP address   | `127.0.0.1` | The machine running Deckboy                                       |
| Port                 | `5510`      | Must match Deckboy's **Settings → Network → Companion port**      |
| Status poll interval | `250 ms`    | Lower = smoother countdowns                                       |
| Passphrase           | empty       | Only if Deckboy has a **Companion passphrase** set (see Security) |

Deckboy accepts connections from its own machine only until you turn on **Settings → Network →
REMOTE**. To control it from Companion on another computer, switch REMOTE on, and add that
computer under **Settings → Network → Trusted controllers**.

### Actions

- **Transport:** Take, GO, Play, Pause, Stop, Rerack, Skip forward / back, Seek
- **Cues:** Select or Take by number, Goto by id or name
- **Levels:** master volume, master dimmer, deck fader
- **Audio routing:** patch a playlist to a named output (Deckboy's **Settings → Audio → OUTPUTS**)
  at a level or off; the output limiter on or off; bypass a device's output effects for an A/B
- **Playback:** Loop, Shuffle
- **Outputs:** on/off, fullscreen, send to display, Clear, Blackout, PANIC
- **Show:** Take all playlists, run a dashboard tile (and the sequence on it), run or stop a sequence
- **Find:** set token, next, previous, take match
- **VJ:** VJ mode, crossfader, blend mode, tap tempo, tempo, quantised takes (the next beat, or the next 1, 2 or 4 bars), Ableton Link, deck assignment
- **Effects:** add, amount, parameter (A to F), parameter LFO, choice (Melt's direction and
  material, which includes `liquid`, and Tape's speed and interference, by name), colour (with Companion's colour picker: a
  strobe's flash, the edges line, or the hue Colour and Colour Replace use; Deckboy 0.99.423 or later),
  clear the chain; copy or paste a chain.
  An effect is added by its name: `levels`, `curves`, `colour`, `colour_replace`, `blur`,
  `sharpen`, `glow`, `mirror`, `mosaic`, `edges`, `luma_key`, `strobe`, `melt`, `tape`, `circuit_bend`, and the rest listed in the
  manual under Per-Cue Effects
- **Audio effects:** add, amount, bypass, remove, clear the chain
- **Sources:** code source expression; video synth text mode, glyphs and phrases
- **Custom command:** sends any Deckboy command. The full list is in the manual under
  [Remote Control](https://utopian-academy.github.io/Deckboy/manual.html#remote-control).

Deck actions take a **Deck** number. `0` means whichever deck has focus; a button that names its
deck always acts on that deck, and leaves focus where it was.

New for Deckboy 0.99.425 and later (Multiview windows can also show the Scopes):

- **Multiview:** on or off, the layout, and what each window shows.
- **Motion:** on or off.
- **Mask:** put a mask on or take it off, invert it, set its opacity. Use the mask id, for example `m1`.
- **Slice:** turn a slice on or off, on the output selected in Deckboy.
- **Screens window:** open or close it.
- **Draw:** undo, clear, and how the drawing plays.
- **MIDI map:** add a ready-made map for a controller.
- **Lessons** and **Bug Swat:** open the lesson list, or start the game.

New for Deckboy 0.99.428:

- **Direction:** play the selected video cue forward, in reverse, or bounce. Reverse and bounce play a copy that Deckboy makes first.
- **Datamosh:** flip the Datamosh look on the selected cue.
- **VJ Ableton Link:** join, leave or toggle the Link session. VJ quantise now waits for the next beat, or 1, 2 or 4 bars.
- **Shader:** load a `.fs` (ISF) file as a cue (only when Companion runs on the Deckboy computer), set an input of the selected shader cue, or give a number input an LFO.
- **Lumia:** set any control of the selected Lumia cue, or give a slider an LFO.
- **Voice:** make the selected Voice cue say a text, pick its character, words or sounds, and set pitch, size and breath for the `CUSTOM` character.
- **Captions:** live captions on or off, what they listen to, which outputs show them, lines and size. Make captions for the selected cue. Subtitles on the live cue on or off.
- **Pad:** the phone's X/Y pad. Put it at a point, let go, hold or latch, and set which controls it moves.
- **Render to file:** open or close the Render window, or cancel a running render. Deckboy starts a render only from its own computer, so there is no Start action.
- **Edge blend:** set a whole row of projectors in one press.

Some commands work only on the Deckboy computer itself, for safety: `RENDER START`, `SUBTITLE CONVERT`, `SHADER LOAD`, and the test commands (`CLICK`, `KEY`, `TYPE`, and others). The **Custom command** action cannot run them from another computer.

Deckboy does not report Link, Direction, Pad or Shader state in its status line yet, so these have actions but no feedbacks or variables.

### Feedbacks

| Feedback                       | Use                                                      |
| ------------------------------ | -------------------------------------------------------- |
| Deck transport state           | Green while playing, amber while paused                  |
| Deck has a cue live            | Red on the Take button while something is on air         |
| Specific cue is live           | Per-cue tally: red on the button that is on air          |
| Specific cue is selected       | Green on the next cue                                    |
| Deck remaining below threshold | The "wrap it up" warning                                 |
| Output armed                   | Green while the output is on                             |
| Output health                  | Shows an output that lost its display or left fullscreen |
| Blackout active                | Red while blacked out                                    |
| Deckboy unreachable            | Shows the connection is down                             |

### Variables

Global: `connected`, `version`, `focused_deck`, `deck_count`, `output_count`, `master_volume`,
`master_dimmer`, `blackout`, `panic_profile`, `find_token`, `find_matches`.

Per deck (1–16): `deckN_name`, `deckN_status`, `deckN_cue`, `deckN_cue_id`, `deckN_selected`,
`deckN_active`, `deckN_position`, `deckN_duration`, `deckN_remaining`,
`deckN_remaining_seconds`, `deckN_volume`, `deckN_raster`, `deckN_audio_device`, `deckN_timecode`.

Per output (1–16): `outputN_name`, `outputN_enabled`, `outputN_health`, `outputN_type`,
`outputN_display`, `outputN_fps`.

A now-playing button with a countdown:

```
$(deckboy:deck1_cue)
$(deckboy:deck1_remaining)
```

### Presets

Ready-made buttons with their feedbacks already wired: **Transport**, **Status** (now playing
with countdown, connection watchdog), **Output** (on/off, fullscreen, clear, blackout, PANIC) and
**Cues** (a cue button with tally; duplicate it and change the cue number).

### Security

With **REMOTE** on, Deckboy answers its own machine and the machines in **Settings → Network →
Trusted controllers**, and refuses anyone else (**Last refused** names them, with an **ALLOW**
button). A **Companion passphrase** adds a second lock: a client on another machine must send
it before anything is answered, and this module sends it for you when the **Passphrase** field
is filled in. Five wrong passphrases end the connection.

Anything trusted can drive the show — take cues, black the output, run PANIC. On a venue or hotel
network, keep the trusted list short, set a passphrase, and leave REMOTE off when Companion runs
on the same machine.

### If it will not connect

1. Is **Settings → Network → REMOTE** on? It is off by default, and Deckboy is then reachable
   only from its own machine.
2. Is this computer in **Trusted controllers**? A refused one shows under **Last refused**.
3. If Deckboy has a passphrase, is the same one in this connection's **Passphrase** field? The
   connection says so when it is refused.
4. Does the port match **Settings → Network → Companion port**?
5. Can the machine reach it at all? Connect to the port by hand and type `HELP`; Deckboy prints
   its whole protocol. If that fails it is the network or a firewall, not this module.
6. Deckboy's own `--devices` prints the audio devices, displays and MIDI ports it can actually
   see, which separates a Deckboy fault from a machine that cannot see its hardware.
