# companion-module-deckboy

Bitfocus Companion module for [Deckboy](https://github.com/Utopian-Academy/Deckboy).

This replaces the older *Generic TCP/UDP* recipe in [`docs/streamdeck/`](../docs/streamdeck/).
That recipe could only push commands one way, so a Stream Deck key never knew
whether the cue it fired actually went live. This module polls Deckboy's
`STATUS` reply, so buttons carry **tally, transport state, output health and a
countdown**.

---

## Install

Until this is published in Companion's module store, load it as a developer
module:

1. Copy or symlink this folder somewhere Companion can see it.
2. In Companion, set **Settings → Developer modules path** to the folder that
   *contains* `companion-module-deckboy`.
3. Restart Companion, then add a connection: **Utopian Academy → Deckboy**.

```
npm install         # once, to pull @companion-module/base
npm test            # parser tests, run against a captured Deckboy status reply
```

## Connect

| Field | Default | Notes |
|-------|---------|-------|
| Deckboy IP address | `127.0.0.1` | The machine running Deckboy |
| Port | `5510` | Must match Settings → Network → Companion port |
| Status poll interval | `250 ms` | Lower = smoother countdowns |

> **Deckboy listens on localhost only until you turn on Settings → Network →
> REMOTE.** With it off, only a Companion instance on the same machine can
> connect — a remote Companion will sit at "connection refused". The module says
> so in its error status rather than leaving you guessing.
>
> On Windows, also check that the port isn't inside a WinNAT excluded range
> (`netsh int ipv4 show excludedportrange protocol=tcp`). Deckboy toasts
> `COMPANION PORT n UNAVAILABLE` when this bites.

## Actions

Transport (`Take`, `GO`, `Play`, `Pause`, `Stop`, `Rerack`, `Skip ±`), cue
selection (`Select`/`Take` by number, `Goto` by id or name), `Seek`, levels
(master volume, master dimmer, deck fader), `Loop`, `Shuffle`, output control
(`on/off`, `fullscreen`, `send to display`), `Clear`, `Blackout`, `PANIC`, and
find (`set token`, `next`, `previous`, `take match`).

Every deck-targeted action takes a **Deck** number, where `0` means "whichever
deck has focus". A button that names its deck acts on that deck regardless of
what the operator last touched.

Anything not covered has a **Custom command** action that sends a raw Deckboy
command — the full vocabulary is in [`MANUAL.md`](../MANUAL.md) §20.

## Feedbacks

| Feedback | Use |
|----------|-----|
| Deck transport state | Green while playing, amber while paused |
| Deck has a cue live | Red on the Take button while something is on air |
| Specific cue is live | Per-cue **tally** — red on the button that is on air |
| Specific cue is selected | Green on the previewed/next cue |
| Deck remaining below threshold | The "wrap it up" warning |
| Output armed | Green while the output is on |
| Output health | Catch an output that lost its display or dropped fullscreen |
| Blackout active | Red while blacked out |
| Deckboy unreachable | The surface shows the desk is deaf, not silently dead |

## Variables

Global: `connected`, `version`, `focused_deck`, `deck_count`, `output_count`,
`master_volume`, `master_dimmer`, `blackout`, `panic_profile`, `find_token`,
`find_matches`.

Per deck 1–4: `deckN_name`, `deckN_status`, `deckN_cue`, `deckN_cue_id`,
`deckN_selected`, `deckN_active`, `deckN_position`, `deckN_duration`,
`deckN_remaining`, `deckN_remaining_seconds`, `deckN_volume`, `deckN_raster`,
`deckN_audio_device`, `deckN_timecode`.

Per output 1–4: `outputN_name`, `outputN_enabled`, `outputN_health`,
`outputN_type`, `outputN_display`, `outputN_fps`.

`deckN_remaining` is derived by the module (Deckboy reports position and
duration, not remaining) — it is what most operators actually want on a button:

```
$(deckboy:deck1_cue)
$(deckboy:deck1_remaining)
```

## Presets

Drag-and-drop buttons that arrive with their feedbacks already wired:
**Transport** (take/go/pause/stop/rerack/skip/select), **Status** (now-playing
with countdown, connection watchdog), **Output** (on-off, fullscreen, clear,
blackout, PANIC) and **Cues** (a cue button with tally — duplicate it and change
the cue number).

## Licence

GPL-3.0-or-later, same as Deckboy.
