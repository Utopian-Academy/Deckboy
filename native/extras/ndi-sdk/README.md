# NDI SDK — vendored headers

The header files from the **NDI 6 SDK** (`Include/`), unmodified.

## Why these are here

Deckboy sends and receives NDI, and building that needs the SDK's headers.
The SDK is an installer behind a licence acceptance, so until these were
vendored, NDI could only be compiled on a machine where somebody had installed
it by hand. CI never had it, so the released packages were built without NDI.
Vendoring the headers means NDI works in a downloaded build.

## What is here, and what is not

Headers only. No libraries, no runtime, no redistributables.

Deckboy never links against NDI. `native/platform/ndi_api.hpp` and
`ndi_input.cpp` load the runtime at run time and resolve every function by
name, so these headers supply types and nothing else. The runtime comes from
the operator's own **NDI Tools** / NDI runtime installation; a machine without
it reports NDI as unavailable and says so.

## Licence

Each header carries its own notice: *"The following MIT license applies to
this file ONLY and not to the SDK as a whole."* MIT is compatible with
Deckboy's GPL-3.0-or-later.

The same notice says that using any part of the SDK is acknowledgement of the
**NDI SDK License Agreement** — https://ndi.link/ndisdk_license. That agreement
was accepted for this project by its owner on 2026-09-17. Read it before
changing how Deckboy uses NDI, and before redistributing anything from the SDK
beyond these headers.

NDI® is a registered trademark of Vizrt NDI AB.

## Updating

Copy the `Include/*.h` files from a newer SDK over these, check every file
still carries the per-file MIT notice, and build on all three platforms —
`--self-check` must print `ndi-sdk: headers detected`.
