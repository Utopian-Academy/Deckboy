# Deckboy for Stream Deck

A native Stream Deck plugin. Keys talk to Deckboy directly, so **Companion is
not required** — install it, point a key at the machine running Deckboy, and
press it.

Keys read Deckboy's state back, so a transport key shows what the deck is
actually doing rather than only what you last asked it to do.

## Actions

| Action | What it does |
|---|---|
| **Take** | Takes the selected cue, or a cue by number if one is set on the key |
| **Stop** | Stops the focused deck |
| **Pause** | Pauses or resumes |
| **Panic** | Outputs off, engines stopped |
| **Blackout** | Toggles blackout on the outputs |
| **Command** | Any Deckboy command, sent verbatim |

Every action is a Deckboy command line — the same vocabulary the manual
documents and Companion drives. A key does exactly what that command does
anywhere else, so anything Deckboy can be told, a key can tell it.

## Installing

Download `Deckboy-<version>.streamDeckPlugin` from the
[releases page](https://github.com/Utopian-Academy/Deckboy/releases) and
double-click it. Stream Deck installs it and the actions appear under
**Deckboy**.

To build one instead:

```
npm run build && node tools/pack.js
```

Or copy `com.deckboy.streamdeck.sdPlugin` into the plugins folder by hand and
restart Stream Deck:

- **Windows** `%APPDATA%\Elgato\StreamDeck\Plugins\`
- **macOS** `~/Library/Application Support/com.elgato.StreamDeck/Plugins/`

## Settings

Each key carries a host and port. They default to `127.0.0.1:5510`, which is
right when the Stream Deck is plugged into the machine running the show. For a
separate control position, set the host to the show machine's address — the
plugin holds ONE connection whatever the keys, so a page of thirty keys is one
socket rather than thirty.

If Deckboy is not running, keys read `offline` and say so when pressed rather
than pretending. The connection re-establishes on its own, so relaunching
Deckboy between shows needs nothing done at the deck.

## Requirements

Stream Deck 6.4 or newer, which is what ships the Node runtime the plugin uses.
Nothing else: the plugin has **no dependencies at all** — Stream Deck speaks
WebSocket and Node has had a client built in since version 22, so there is
nothing to install, audit or keep updated on a show machine.

## Developing

```
npm test          # the protocol and connection behaviour, no hardware needed
npm run build     # assemble the .sdPlugin folder
```

The WebSocket half needs Stream Deck to exercise it. Everything that decides
what to send and what a key should say is an ordinary function, and that is
what the tests cover — a wrong command string is a key that quietly does
nothing on a show.
