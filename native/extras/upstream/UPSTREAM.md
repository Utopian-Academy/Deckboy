# Vendored from the standalone Terrarium repo

Source: https://github.com/Utopian-Academy/terrarium  (`~/terrarium`)

| | |
|---|---|
| Upstream commit | `6133297` |
| Working-tree state at copy | clean (committed) |
| Copied | 2026-08-29 |
| Files | `terrarium_core.hpp`, `terrarium_core.cpp`, `terrarium_pixelview.hpp` |

## Rules

These three files are **byte-identical to upstream and must stay that way.**
Re-syncing is then a plain copy instead of a merge. Deckboy's own additions —
the namespace wrapper and the RGBA renderer — live one directory up, in
`terrarium_vendor.{hpp,cpp}` and `terrarium_render_rgba.hpp`.

This file exists because the previous vendored copy recorded no provenance at
all, and drifted five weeks stale without anyone noticing. If you update these,
update the commit above in the same change.

It drifted again anyway: the 2026-08-07 copy was **68 commits behind** by
2026-08-29, missing the sky biome, the open ocean, seasons, per-biome palettes,
the airship, and the fixes for the sky ignoring the brightness knob and most of
the "sea" actually being a river. Recording the commit tells you WHETHER you
have drifted; it does not stop you drifting. Worth checking whenever the
easter egg is touched.

Upstream has **no namespace**; Deckboy needs one (`step`, `clamp01` and
friends are global there and Deckboy is effectively one huge translation unit).
The wrapper supplies it at include time so these files need no edits.

Only these three are vendored: they are the ones with **zero SDL dependency**.
The glyph renderer (`terrarium_visuals/​glyphs/​render`) is written against
`SDL_Renderer` and cannot be used from Deckboy's raw-RGBA pattern path.

---

# Vendored: QR Code generator (Project Nayuki)

Source: https://github.com/nayuki/QR-Code-generator  (C++ edition, `cpp/`)

| | |
|---|---|
| Upstream tag | `v1.8.0` |
| Licence | MIT (header of each file) |
| Copied | 2026-10-04 |
| Files | `qrcodegen/qrcodegen.hpp`, `qrcodegen/qrcodegen.cpp` |
| SHA-256 | `.hpp b779c3b156cf7a57ce789d6fee4fc991ccc2913774d26c909d22bb8f26b2a793` |
| | `.cpp 1f3b3fcdac6954c32cf583ccd02ec9b5901f756a38c461acedc70be4a77d3757` |

Used by the Web Monitor's QR code (Settings). Same rule as above: byte-identical
to upstream, so a re-sync is a plain copy and the hashes above can be checked.
