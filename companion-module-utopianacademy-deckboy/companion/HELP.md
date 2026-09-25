## Deckboy

Cue deck for live events. This module drives Deckboy over its control port and
**polls it back**, so a button knows whether the cue it fired actually went
live — tally, transport state, output health and a running countdown.

### Before you connect

Deckboy listens on **localhost only** until you turn on
**Settings → Network → REMOTE**. Leave it off and only Companion running on the
same machine can reach it; turn it on to drive Deckboy from another machine.

The port here must match **Settings → Network → Companion port**, which is
`5510` unless you have changed it.

There is no password on the control port. Anything that can reach it can drive
the show, so on a shared network put Deckboy behind a firewall rule rather than
relying on obscurity.

### Connection settings

| Field | Default | Notes |
|---|---|---|
| Deckboy IP address | `127.0.0.1` | The machine running Deckboy |
| Port | `5510` | Settings → Network → Companion port |
| Status poll interval | `250 ms` | 250 keeps countdowns smooth. Raise it if many surfaces poll one machine |

`connected` goes to `false` and the **Connection lost** feedback turns on if the
poll stops answering, so a surface can show that it has lost the desk rather
than showing stale state.

---

### Actions

**Transport** — `take`, `go`, `play`, `pause`, `stop`, `rerack`, `seek`

**Choosing a cue** — `select_next`, `select_prev`, `select_cue`, `take_cue`,
`goto_cue`, `skip_next`, `skip_prev`

Selecting and taking are separate on purpose: the selection is where you are
looking, the take is what goes on air. `take_cue` does both in one press.

**Finding** — `find`, `find_next`, `find_prev`, `find_take`

Search the playlist by name or number from a surface, step the matches, and
take the one you land on.

**Playlists** — `focus_deck`, `deck_fader`, `loop`, `shuffle`

**Outputs** — `output_enable`, `output_fullscreen`, `output_display`

**Levels and safety** — `master_volume`, `master_dimmer`, `blackout`, `clear`,
`panic`

`panic` runs the panic profile set in Deckboy, which is a configured response
rather than a fixed one — check what it is set to before you put it on a
surface.

**VJ and tempo** — `vj_mode`, `vj_mix`, `vj_blend`, `vj_decks`, `vj_tap`,
`vj_bpm`, `vj_quantise`

**Picture effects** — `fx_add`, `fx_amount`, `fx_param`, `fx_lfo`, `fx_clear`,
`fx_copy_paste`

**Audio effects** — `audiofx_add`, `audiofx_amount`, `audiofx_bypass`,
`audiofx_remove`, `audiofx_clear`

**Generated sources** — `code_set`, `text_mode`, `text_glyphs`, `text_phrases`

**`custom`** — send any line of Deckboy's control protocol. `HELP` over the
same port lists every verb, so anything the module has no action for can still
be put on a button. Deckboy answers `OK <VERB>` or `ERR <VERB>: <reason>`, and
a refusal is reported rather than silently swallowed.

---

### Feedbacks

| Feedback | Turns on when |
|---|---|
| `deck_status` | A playlist is playing, paused or stopped — pick which |
| `deck_has_live_cue` | That playlist has something on air |
| `cue_is_live` | That specific cue is the one on air |
| `cue_is_selected` | That specific cue is the selection |
| `deck_remaining_below` | The countdown drops under a threshold you set |
| `output_enabled` | That output is armed |
| `output_health` | An output reports a fault |
| `blackout_active` | Blackout is on |
| `connection_lost` | The poll has stopped answering |

`cue_is_live` and `cue_is_selected` are the pair worth putting on every cue
button: together they give you the standard red/amber of a cue surface.

### Variables

`connected`, `version`, `focused_deck`, `deck_count`, `output_count`,
`master_volume`, `master_dimmer`, `blackout`, `panic_profile`, `find_token`,
`find_matches`

### Presets

Seventeen ready-made buttons covering transport, tally, outputs and the master
levels. Drag one onto a surface and change the playlist or cue number on it —
they are a starting point, not a fixed set.

---

### If it will not connect

1. Is **Settings → Network → REMOTE** on? It is off by default and Deckboy is
   then reachable only from its own machine.
2. Does the port match **Settings → Network → Companion port**?
3. Can the machine reach it at all? `telnet <host> 5510` then typing `HELP`
   should print the protocol. If that fails, it is the network or a firewall,
   not this module.
4. `--devices` on the Deckboy machine prints what it can actually see.
