# Deckboy UI Icon Pack — Artist Brief

## What is Deckboy?

Deckboy is an open-source show control and media playback application for live events (theatre, concerts, corporate AV). Think of it as a cue-deck that operators use backstage to play video, audio, graphics, and browser content to stage outputs. It has a retro Game Boy aesthetic — a 4-shade dot-matrix palette with chunky pixel art.

The UI currently uses text labels for all buttons and icons. We need pixel-art icons to replace them, making the interface faster to read for operators working in dark venues under time pressure.

## Visual Style

- **Game Boy DMG pixel art** — chunky, low-res, 1-bit or 2-bit shading
- Think: LSDJ, Nanoloop, mGB, Pocket Camera — music/creative tools on the Game Boy
- Professional show control readability — borrow layout instincts from industry tools, but render them in pixel art
- Icons must be instantly legible at small sizes (24px–32px on screen)
- No anti-aliasing, no gradients — hard pixel edges only
- Charm and personality encouraged, but clarity comes first

## Palette Reference

The app has a theme system. The default theme is Game Boy green:

```
screen_light   #9BBC0F   (brightest — background)
screen_mid     #8BAC0F   (mid tone)
screen_dark    #306230   (dark tone)
screen_deep    #0F380F   (darkest — text/outlines)
```

Other bundled themes include dark, pocket (grey), color, advance, and SP. Each has its own 4-shade palette. **Your icons must work with ALL themes**, not just the green one.

## Technical Requirements

### Delivery format

| Requirement | Value |
|---|---|
| File format | **PNG**, 8-bit RGBA, non-interlaced |
| Color | **White (`#FFFFFF`) on transparent** |
| Shading | Use **white at varying alpha levels** for shading (e.g. `#FFFFFF` at 100% for outlines, 60% for midtones, 30% for highlights) |
| No ICC profiles | Strip all color profiles |
| No anti-aliasing | Hard pixel edges only — every pixel should be fully opaque white or fully transparent (exception: the 2-3 alpha levels noted above for shading) |

### Why white-on-transparent?

The app tints icons at runtime using `SDL_SetTextureColorMod()` to match the active theme's palette. White pixels become the target color. This means one set of icons works across all themes automatically.

### File structure

All icons go in the existing UI pack directory structure:

```
data/ui/deckboy_ui_pack_v3/
├── manifest.json              ← update with new entries
├── controls/                  ← transport buttons
│   ├── play.png               (220 x 70)
│   ├── stop.png               (220 x 70)
│   ├── pause.png              (220 x 70)  ← NEW
│   ├── take.png               (220 x 70)
│   ├── clear.png              (220 x 70)
│   ├── rerack.png             (220 x 70)  ← NEW
│   └── blackout.png           (220 x 70)  ← NEW
├── cue_icons/                 ← cue type badges
│   ├── video.png              (32 x 32)
│   ├── image.png              (32 x 32)
│   ├── audio.png              (32 x 32)
│   ├── browser.png            (32 x 32)
│   ├── pattern.png            (32 x 32)
│   ├── source.png             (32 x 32)
│   ├── lowerthird.png         (32 x 32)
│   ├── pip.png                (32 x 32)   ← NEW
│   ├── srt.png                (32 x 32)   ← NEW
│   └── composite.png          (32 x 32)   ← NEW
├── end_actions/               ← NEW directory — cue end-action badges
│   ├── loop.png               (16 x 16)
│   ├── hold.png               (16 x 16)
│   ├── next.png               (16 x 16)
│   └── stop.png               (16 x 16)
├── mode_icons/                ← NEW directory — toggle/status indicators
│   ├── loop_on.png            (24 x 24)
│   ├── shuffle_on.png         (24 x 24)
│   ├── live.png               (24 x 24)
│   ├── armed.png              (24 x 24)
│   ├── off.png                (24 x 24)
│   └── warp.png               (24 x 24)
├── toolbar/                   ← NEW directory — header toolbar buttons
│   ├── new.png                (24 x 24)
│   ├── open.png               (24 x 24)
│   ├── save.png               (24 x 24)
│   ├── import.png             (24 x 24)
│   └── prefs.png              (24 x 24)
├── outputs/                   ← output status chips (existing)
│   ├── chip_idle.png          (200 x 50)
│   ├── chip_armed.png         (200 x 50)
│   ├── chip_live.png          (200 x 50)
│   ├── chip_warning.png       (200 x 50)
│   └── chip_offline.png       (200 x 50)
├── header/
│   └── header_default.png     (900 x 140)
├── splash/
│   └── deckboy_splash_deckgirl.png   (splash screen illustration)
├── monitor/
│   └── monitor_frame.png      (program monitor frame)
├── textures/
│   └── dot_grid.png           (background texture)
└── styleguide/
    └── styleguide.png         (palette + typography reference)
```

### Exact pixel dimensions

These dimensions must be exact — the app loads and scales based on them:

| Category | Dimensions | Notes |
|---|---|---|
| `controls/*` | **220 x 70** | Transport buttons — large, prominent. Icon art centered within the canvas. |
| `cue_icons/*` | **32 x 32** | Cue type badges — shown in cue list rows. Must be distinct at 28x28 rendered. |
| `end_actions/*` | **16 x 16** | Tiny corner badges — must read at 12x12 rendered. Keep extremely simple. |
| `mode_icons/*` | **24 x 24** | Toggle indicators in header/footer bars. |
| `toolbar/*` | **24 x 24** | Header toolbar action buttons. |
| `outputs/chip_*` | **200 x 50** | Output status chips — contain both an icon region and space for text. |
| `header/header_default` | **900 x 140** | Top header strip — decorative, can include the Deckboy wordmark. |
| `splash/*` | **Free size** | Splash screen illustration — creative freedom. |
| `monitor/monitor_frame` | **Free size** | Program monitor decorative frame. |
| `styleguide/styleguide` | **Free size** | Reference sheet for the pack. |

## Icon Descriptions

### Transport controls (220 x 70) — Priority 1

These are the operator's primary interaction. They must be unmistakable.

| Filename | What it does | Icon concept |
|---|---|---|
| `play.png` | Start playback | Right-pointing triangle (▶), large and bold |
| `stop.png` | Stop playback | Filled square (■) |
| `pause.png` | Pause playback | Two vertical bars (⏸) |
| `take.png` | Take selected cue live (the "GO" button) | Bold downward arrow into a line — "commit" / "push to stage" |
| `clear.png` | Fade output to black | Rectangle dissolving / fading out |
| `rerack.png` | Rewind to start, paused | Skip-to-start: vertical bar + left-pointing triangle (\|◀) |
| `blackout.png` | Master blackout — kills all output instantly | Filled circle with diagonal slash, or bold "X" |

### Cue type icons (32 x 32) — Priority 2

Shown in cue list rows. Operators scan these to find the right cue.

| Filename | Cue type | Icon concept |
|---|---|---|
| `video.png` | Video file | Film frame / clapperboard |
| `image.png` | Still image | Mountain landscape in a frame |
| `audio.png` | Audio-only | Speaker cone or waveform bars |
| `browser.png` | Web/URL content | Globe or browser window |
| `pattern.png` | Test pattern | Grid / color bars |
| `source.png` | Live camera/capture source | Camera lens or video camera |
| `lowerthird.png` | Lower third graphic overlay | Two stacked rectangles (foreground layer) |
| `pip.png` | Picture-in-picture composite | Small rectangle inside a larger one |
| `srt.png` | SRT network stream input | Antenna with signal waves |
| `composite.png` | Multi-source composite layout | Four rectangles arranged in a grid |

### End action badges (16 x 16) — Priority 3

Tiny badges in the corner of cue rows. These must be recognizable at 12px.

| Filename | Meaning | Icon concept |
|---|---|---|
| `loop.png` | Loop: restart when finished | Circular arrow (↻) |
| `hold.png` | Hold: freeze on last frame | Two vertical bars (pause symbol) |
| `next.png` | Auto-advance to next cue | Double right chevron (>>) |
| `stop.png` | Stop: go to black | Small filled square |

### Mode/status icons (24 x 24) — Priority 4

| Filename | Meaning | Icon concept |
|---|---|---|
| `loop_on.png` | Playlist loop active | Circular arrow |
| `shuffle_on.png` | Shuffle/random order active | Crossing/interleaving arrows |
| `live.png` | Output is live/active | Filled circle (record dot) |
| `armed.png` | Output armed, ready | Hollow circle / ring |
| `off.png` | Output disabled | Circle with slash |
| `warp.png` | Geometry/warp edit mode | Four outward-pointing corner arrows |

### Toolbar icons (24 x 24) — Priority 5

| Filename | Action | Icon concept |
|---|---|---|
| `new.png` | New show | Blank page with small plus |
| `open.png` | Open show file | Opening folder |
| `save.png` | Save show | Floppy disk |
| `import.png` | Import media files | Arrow pointing down into a tray / plus in circle |
| `prefs.png` | Open preferences/settings | Gear / cog |

### Output status chips (200 x 50) — existing, redesign

Each chip represents one output in the monitors bar. Left portion should have a distinctive icon/symbol, right portion has space for text (the app overlays the output number and status text).

| Filename | State | Visual treatment |
|---|---|---|
| `chip_idle.png` | Idle, no signal | Subdued / dim, hollow monitor outline |
| `chip_armed.png` | Armed, ready | Bright outline, standby indicator |
| `chip_live.png` | Live, outputting | Bold, filled, prominent — operator must see this instantly |
| `chip_warning.png` | Warning/recovering | Exclamation mark or alert triangle |
| `chip_offline.png` | Disabled/offline | Crossed out or very dim |

### Header art (900 x 140) — existing, redesign

The top strip of the application. Can include:
- "DECKBOY" wordmark in pixel art
- Decorative elements (the app's mascot is a pixel-art cat-girl — include if you like)
- Should feel like a Game Boy cartridge label or title screen

### Splash screen — existing, redesign

Shown on startup for ~2.5 seconds. Full creative freedom. Should feature the "Deckgirl" mascot character (pixel-art cat-girl with cat ears, anime eyes, winking) and the Deckboy wordmark. Game Boy boot screen vibe.

## Delivery Checklist

- [ ] All PNGs are white-on-transparent (no pre-baked colors)
- [ ] All PNGs are 8-bit RGBA, non-interlaced, no ICC profiles
- [ ] All pixel dimensions match the table above exactly
- [ ] No anti-aliasing — every pixel is a hard edge
- [ ] Alpha shading uses at most 3–4 distinct alpha levels (100%, ~70%, ~40%, 0%)
- [ ] `manifest.json` updated with entries for all new files
- [ ] Icons are visually distinct from each other at their rendered size
- [ ] Transport icons are recognizable from 1 meter away on a 1080p display
- [ ] End action badges are distinguishable at 12x12px rendered size

## Testing

To see your icons in the app:

1. Place files in `data/ui/deckboy_ui_pack_v3/` following the structure above
2. Build: `cmake -S . -B build/native && cmake --build build/native -j$(nproc)`
3. Run: `./build/native/Deckboy`
4. The app loads the pack at startup — icons appear in the cue list, transport bar, and header

If the pack directory is missing or a file is missing, the app falls back to text labels. You can work incrementally — deliver one category at a time.
