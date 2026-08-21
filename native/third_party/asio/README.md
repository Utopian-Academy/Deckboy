# Steinberg ASIO SDK — vendored host subset

Source: `ASIO-SDK_2.3.4_2025-10-15.zip`, Steinberg Media Technologies GmbH.

## Why this can live in a GPL repository

ASIO SDK 2.3.4 is **dual-licensed**: the proprietary Steinberg ASIO License
**or** GPL v3, the choice being the integrator's "on a case-by-case basis
(commercial or not)". Deckboy is GPL-3.0-or-later, so these files are used
under **option (b), GPL v3**, and distributing them alongside Deckboy's own
source is exactly what that option permits.

This is worth writing down because the widely-repeated advice — that ASIO
cannot be shipped in a GPL program, which is why Audacity makes users compile
their own — describes OLDER SDK releases. It was true before Steinberg added
the GPL option, and it is no longer true here. Check `LICENSE.txt` in this
directory before acting on anything you read elsewhere.

## What is here, and what is deliberately not

Host-side only. Deckboy *uses* ASIO drivers; it does not *implement* one, so
the driver-authoring half of the SDK is omitted:

| kept | why |
|---|---|
| `asio.h`, `asiosys.h`, `iasiodrv.h` | the ASIO interface and platform defines |
| `asio.cpp` | host-side glue: implements ASIOInit/ASIOGetChannels/... by calling through the loaded driver. Despite living in `common/` it is NOT driver code |
| `asiodrivers.{h,cpp}` | driver load/unload, the host entry point |
| `asiolist.{h,cpp}` | enumerates installed drivers from the registry |
| `ginclude.h` | SDK integer type sizing |

Omitted: `dllentry.cpp`, `register.cpp`, `asiodrvr.*`, `combase.*`,
`wxdebug.h`, `debugmessage.cpp` (all driver-side), and the `hostsample`
project, PDFs and logo artwork.

## Obligations that ride along

- Steinberg's name may not be used to endorse or promote Deckboy.
- The ASIO and ASIO-compatible logos have usage rules of their own; see
  Steinberg's usage guidelines before putting either in the UI.
- "ASIO is a trademark and software of Steinberg Media Technologies GmbH."

## Updating

Re-extract the same file list from a newer SDK zip and re-read `LICENSE.txt` —
the dual-licence grant is a property of the release, not a permanent guarantee.
