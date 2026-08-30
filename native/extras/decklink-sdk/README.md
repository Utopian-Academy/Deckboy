# Blackmagic DeckLink SDK — vendored headers

Version **16.0**, from the Blackmagic DeckLink SDK, unmodified.

## Why these are here

Deckboy talks to DeckLink and UltraStudio hardware directly, for both playout
and capture. Building that needs the SDK's headers, and the SDK is a manual
download behind a registration form — so before this, DeckLink support could
only be compiled by someone who had gone and fetched it. CI never built the
path, and the released packages shipped without it. Vendoring the headers
means a DeckLink card works in a downloaded build, which is the only version
of "supported" that means anything to an operator.

## What is here, and what is not

Headers only:

- `Win/include` — the `.idl` interface definitions, compiled by MIDL at build
  time into `DeckLinkAPI_h.h` and `DeckLinkAPI_i.c`.
- `Linux/include`, `Mac/include` — the `.h` headers plus
  `DeckLinkAPIDispatch.cpp`, which provides the factory function on those
  platforms.

No libraries, no drivers, no redistributables. The runtime comes from
Blackmagic's **Desktop Video** package, which the operator installs with their
card; Deckboy loads it through the API these headers describe. A machine with
no Desktop Video installed simply reports no devices.

## Updating

Replace the three `include` directories from a newer SDK and rebuild. The
interfaces are versioned and backwards compatible: a binary built against 16.0
runs against a newer driver. Record the new version at the top of this file.

## Licence

These files are Blackmagic Design's and carry their own terms; they are not
covered by Deckboy's GPL. They are included as the SDK permits for building
applications against the API — the same arrangement OBS Studio uses for the
same files.
