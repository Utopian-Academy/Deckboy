# Platform art sources

The painted cartridge master and Press Start 2P font are existing repository
assets. They are unchanged. All new art is generated from those assets and
small pixel grids authored in `tools/generate_platform_art.ps1`; no new mascot,
logo source or third-party artwork was imported.

- Windows: ICO entries 16, 24, 32, 48, 64, 128, 256; Inno wizard and small images
  include the classic and current Inno 6 scale sets.
- Linux: hicolor PNGs through 512 and SVG pixel runs from the 48-pixel design.
- macOS: legacy app body uses a 1024 canvas with an 824-pixel continuous-corner
  body, 100-pixel margins and shadow. Small slots have separately rasterized
  grids. The layered icon has an opaque green fill and the cartridge as a
  transparent foreground; the system supplies the modern shape and treatment.
- Show files: a folded page containing the same small cartridge.
- Runtime: alpha-bearing BITMAPV4 assets, loadable by SDL without ffmpeg.

The platform art uses cartridge-derived greens: ink #091B0C, body #7E9741,
highlight #DAE370, mid #496821, screen #274B17, play #B8EB1E, tally #FFB418.
These are artwork colours, not new UI theme tokens. The proposed broader brand
palette is documented separately in the review-only brand plan.

The modern icon flat preview is a layer composite, not a macOS system render.
The DMG backgrounds are artwork previews, not mounted Finder screenshots.
Packaging commands and dependencies are in `docs/PACKAGING.md`.
