# Deckboy Icon Design Brief

Target: replace text labels with pixel-art icons across the Deckboy UI.
Aesthetic: DMG Game Boy (4-shade palette), chunky, readable at small sizes.

## Rendering constraints

- Icons are rendered as SDL textures (PNG or BMP) loaded at startup
- All icons must work at multiple sizes: 16x16, 24x24, 32x32
- 4-color palette only (light/mid/dark/deep from active theme)
- Supply as 1x PNGs with transparency — the app tints at runtime
- Monochrome with alpha — the app will colorize based on state (active/inactive/hover)
- Must be legible on both light and dark theme backgrounds

## Priority 1 — Transport controls (operator-critical)

These appear in the bottom bar and program monitor header. Operators need to identify them instantly.

| ID | Current text | Purpose | Suggested icon | Size used |
|----|-------------|---------|----------------|-----------|
| `ico-play` | ▶ / PLAY | Start playback | Right-pointing triangle | 24x24, 32x32 |
| `ico-stop` | ■ / STOP | Stop playback | Filled square | 24x24, 32x32 |
| `ico-pause` | ⏸ / PAUS | Pause playback | Two vertical bars | 24x24, 32x32 |
| `ico-take` | TAKE | Take selected cue live | Downward arrow into line (commit/push) | 24x24, 32x32 |
| `ico-rerack` | RERACK | Rewind to start, paused | Skip-to-start bar + left triangle | 24x24, 32x32 |
| `ico-clear` | CLEAR | Fade output to black | Fading rectangle / dissolve | 24x24, 32x32 |
| `ico-skip-start` | \|< | Jump to beginning | Bar + left triangle | 16x16, 24x24 |
| `ico-skip-back` | << | Skip back 10s | Double left triangle | 16x16, 24x24 |
| `ico-skip-fwd` | >> | Skip forward 10s | Double right triangle | 16x16, 24x24 |
| `ico-blackout` | BLACKOUT / BLK | Master blackout toggle | Filled circle with slash (kill) | 24x24 |

## Priority 2 — Cue list icons

Shown in cue rows (64px tall, icon area ~28x28). Need to distinguish cue types at a glance.

| ID | Current text | Purpose | Suggested icon |
|----|-------------|---------|----------------|
| `ico-cue-video` | VID | Video cue | Film frame / clapperboard |
| `ico-cue-image` | IMG | Still image cue | Mountain/landscape in frame |
| `ico-cue-audio` | AUD | Audio-only cue | Speaker / waveform |
| `ico-cue-pattern` | PAT | Test pattern cue | Grid / crosshatch |
| `ico-cue-browser` | WEB | Browser/URL cue | Globe / window |
| `ico-cue-overlay` | GFX | Lower third / overlay | Layered rectangles |
| `ico-cue-srt` | SRT | SRT stream cue | Antenna / signal waves |

## Priority 3 — End action badges

Small badges (12x12 or 16x16) shown in the corner of cue rows to indicate what happens when the cue finishes.

| ID | Current text/glyph | Purpose | Suggested icon |
|----|-------------------|---------|----------------|
| `ico-end-loop` | ↻ | Loop back to start | Circular arrow |
| `ico-end-hold` | ‖ | Pause on last frame | Two vertical bars (pause) |
| `ico-end-next` | ▶▶ | Auto-advance to next cue | Double right arrow |
| `ico-end-stop` | ■ | Stop (black) | Small filled square |

## Priority 4 — Mode toggles & status

Header bar and footer indicators.

| ID | Current text | Purpose | Suggested icon |
|----|-------------|---------|----------------|
| `ico-loop` | LOOP | Playlist loop mode active | Circular arrow |
| `ico-once` | ONCE (currently "ON"!) | Play once, no loop | Right arrow with bar (one-shot) |
| `ico-shuffle` | SHUFFLE | Random cue order | Crossing arrows (⤮) |
| `ico-sequential` | SEQ | Sequential cue order | Down arrow with numbers |
| `ico-live` | LIVE | Output is active | Filled circle (record dot) |
| `ico-armed` | ARMED | Output ready, not live | Hollow circle |
| `ico-off` | OFF | Output disabled | Circle with line through |
| `ico-warp` | WARP | Canvas/geometry edit mode | Four-corner arrows (transform) |

## Priority 5 — File operations & settings

Header toolbar buttons.

| ID | Current text | Purpose | Suggested icon |
|----|-------------|---------|----------------|
| `ico-new` | NEW | New show | Blank page with plus |
| `ico-open` | OPEN | Open show file | Folder opening |
| `ico-save` | SAVE | Save show | Floppy disk / down arrow to tray |
| `ico-prefs` | PREFS | Open settings | Gear / cog |
| `ico-import` | IMPORT | Import media | Plus in circle / arrow into tray |
| `ico-fullscreen` | FULLSCR | Toggle fullscreen | Expanding corners |
| `ico-windowed` | WINDOWED | Toggle windowed | Contracting corners |
| `ico-copy` | COPY | Copy state | Two overlapping rectangles |
| `ico-paste` | PASTE | Paste state | Clipboard with page |

## Priority 6 — Source type icons (bottom bar)

| ID | Current text | Purpose | Suggested icon |
|----|-------------|---------|----------------|
| `ico-source` | SOURCE | Add file source | Film reel or file |
| `ico-pattern` | PATTERN | Add test pattern | Color bars / grid |

## Delivery format

- Directory: `data/icons/{16,24,32}/ico-name.png`
- White-on-transparent (app tints at load time)
- Alternatively: single sprite sheet per size with documented coordinates
- File format: PNG with alpha, 8-bit, no ICC profile

## Reference

- Game Boy pixel art style guides (DMG green palette for reference, but icons should be monochrome)
- The app already draws a pixel-art cat mascot and star effects — match that vibe
- Look at LSDJ, Nanoloop, mGB for UI icon inspiration (tracker/music tool aesthetic)
- Professional show control reference: QLab, Mitti, Millumin transport icons (for operator familiarity)
