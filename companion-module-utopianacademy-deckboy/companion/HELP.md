## Deckboy

Controls [Deckboy](https://github.com/Utopian-Academy/Deckboy), a free, open-source cue deck for
theatre, live events and broadcast. Buttons carry **tally, transport state, output health and a
countdown**, because the module polls Deckboy's status rather than only sending commands.

### Connect

| Field | Default | Notes |
|-------|---------|-------|
| Deckboy IP address | `127.0.0.1` | The machine running Deckboy |
| Port | `5510` | Must match Deckboy's **Settings → Network → Companion port** |
| Status poll interval | `250 ms` | Lower = smoother countdowns |

Deckboy accepts connections from its own machine only until you turn on **Settings → Network →
REMOTE**. To control it from Companion on another computer, switch REMOTE on.

### Actions

- **Transport:** Take, GO, Play, Pause, Stop, Rerack, Skip forward / back, Seek
- **Cues:** Select or Take by number, Goto by id or name
- **Levels:** master volume, master dimmer, deck fader
- **Playback:** Loop, Shuffle
- **Outputs:** on/off, fullscreen, send to display, Clear, Blackout, PANIC
- **Find:** set token, next, previous, take match
- **Custom command:** sends any Deckboy command. The full list is in the manual under
  [Remote Control](https://utopian-academy.github.io/Deckboy/manual.html#remote-control).

Deck actions take a **Deck** number. `0` means whichever deck has focus; a button that names its
deck always acts on that deck.

### Feedbacks

| Feedback | Use |
|----------|-----|
| Deck transport state | Green while playing, amber while paused |
| Deck has a cue live | Red on the Take button while something is on air |
| Specific cue is live | Per-cue tally: red on the button that is on air |
| Specific cue is selected | Green on the next cue |
| Deck remaining below threshold | The "wrap it up" warning |
| Output armed | Green while the output is on |
| Output health | Shows an output that lost its display or left fullscreen |
| Blackout active | Red while blacked out |
| Deckboy unreachable | Shows the connection is down |

### Variables

Global: `connected`, `version`, `focused_deck`, `deck_count`, `output_count`, `master_volume`,
`master_dimmer`, `blackout`, `panic_profile`, `find_token`, `find_matches`.

Per deck (1–4): `deckN_name`, `deckN_status`, `deckN_cue`, `deckN_cue_id`, `deckN_selected`,
`deckN_active`, `deckN_position`, `deckN_duration`, `deckN_remaining`,
`deckN_remaining_seconds`, `deckN_volume`, `deckN_raster`, `deckN_audio_device`, `deckN_timecode`.

Per output (1–4): `outputN_name`, `outputN_enabled`, `outputN_health`, `outputN_type`,
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

There is no password on Deckboy's control port. With **REMOTE** on, anything that can reach the
port can drive the show — take cues, black the output, run PANIC. On a venue or hotel network,
put Deckboy behind a firewall rule rather than relying on the port being obscure, and leave
REMOTE off when Companion runs on the same machine.

### If it will not connect

1. Is **Settings → Network → REMOTE** on? It is off by default, and Deckboy is then reachable
   only from its own machine.
2. Does the port match **Settings → Network → Companion port**?
3. Can the machine reach it at all? Connect to the port by hand and type `HELP`; Deckboy prints
   its whole protocol. If that fails it is the network or a firewall, not this module.
4. Deckboy's own `--devices` prints the audio devices, displays and MIDI ports it can actually
   see, which separates a Deckboy fault from a machine that cannot see its hardware.
