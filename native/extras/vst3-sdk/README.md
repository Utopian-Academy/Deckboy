# VST3 SDK — vendored headers

`pluginterfaces/` is Steinberg's VST3 interface definitions, taken from
[steinbergmedia/vst3_pluginterfaces](https://github.com/steinbergmedia/vst3_pluginterfaces)
and vendored here unchanged apart from dropping the `test/` directory.

**Licence: MIT** (`pluginterfaces/LICENSE.txt`), which is why it can sit in a
GPL-3 repository without qualification. Note that this is the *interfaces*
package only — the full VST3 SDK, which is not here and is not needed, is
dual-licensed GPLv3-or-proprietary instead. Do not copy the licensing note
from one to the other; they are different packages.

## Why vendored rather than fetched

Because it was fetched once and that was worse.

v0.99.370 was first published on all three platforms advertising VST3 plugin
hosting while having no plugin host in it at all. The runners had no SDK,
`find_path` was quiet by design, every job went green, and the binaries would
have listed an operator's plugins and refused to load every one of them. The
release was deleted before anybody downloaded it.

The first repair fetched the headers in CI. That worked, but it made every
build depend on a third-party repository being reachable, and it left the
property that a missing SDK is a *normal* state the build tolerates. Vendoring
removes both: the headers are in the checkout, so the only way to lose plugin
hosting is to ask for it to be off.

This is the same decision, for the same reason, as `../ndi-sdk` — whose own
note records that every released package once shipped without NDI while the
build stayed green — and `../decklink-sdk`.

## What uses it

`native/platform/audio_plugin_vst3.inc`, behind `DECKBOY_HAS_VST3`. CMake
prefers an SDK installed on the machine (vcpkg, a manual install) and falls
back to this copy, so a developer with their own SDK still builds against it.

A miss is a broken checkout, not a missing optional dependency: configure with
`-DENABLE_VST3=OFF` to deliberately build without plugin hosting.

## Updating

Replace `pluginterfaces/` wholesale from a tagged upstream release, drop
`test/`, keep `LICENSE.txt`, and run `--plugin-chain-check` against a real
plugin afterwards. The IIDs are defined by Deckboy itself (see the top of
`audio_plugin_vst3.inc`), so an upstream change to an interface's UID is a
thing this project has to notice rather than inherit silently.
