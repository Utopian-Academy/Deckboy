# Deckboy Stream Deck + Companion Profile

> **Prefer the real Companion module: [`companion-module-deckboy/`](../../companion-module-deckboy/).**
> It does everything below *and* reads Deckboy's state back, so buttons carry
> cue tally, transport colour, output health and a countdown. The Generic
> TCP/UDP mapping in this folder is one-way only — a key never knows whether the
> cue it fired actually went live. It is kept for setups that can't install a
> module, and as the canonical list of command strings.

This folder contains the Deckboy Stream Deck control mapping for the
`Generic TCP/UDP` Companion connection.

Deckboy does not require a proprietary Stream Deck plugin. Use Companion as
the transport and map Stream Deck keys to plain-text Deckboy commands.

## Files

- `deckboy_companion_profile_map.json`
  - Canonical multi-page mapping manifest (labels + commands + key positions).
- `deckboy_main_page.csv`
  - Quick one-page (15-key) layout for copy/edit workflows.

## Companion Setup

1. In Companion, add connection: `Generic TCP/UDP`.
2. Set host to Deckboy machine IP.
3. Set port to Deckboy control port (`5510` default).
4. Use TCP or UDP (both supported).

## Stream Deck Setup

1. Create Companion buttons using the commands in this folder.
2. Assign Stream Deck keys to those Companion buttons.
3. Optional: enable OSC feedback mirror in Deckboy (`OSCFEEDBACK ON`) for
   richer state feedback integrations.

## Notes

- Commands are case-insensitive plain text.
- Prefer `DECK n` before cue commands if your workflow jumps between decks.
- Full command list: see `MANUAL.md` section "Companion Command Reference".
