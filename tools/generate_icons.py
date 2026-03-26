#!/usr/bin/env python3
"""Generate Deckboy UI icon pack — white-on-transparent pixel art PNGs."""

from PIL import Image, ImageDraw
import os
import json

PACK_DIR = os.path.join(os.path.dirname(__file__), "..", "data", "ui", "deckboy_ui_pack_v3")

WHITE = (255, 255, 255, 255)
MID = (255, 255, 255, 180)
SOFT = (255, 255, 255, 100)
CLEAR = (0, 0, 0, 0)


def ensure_dir(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)


def save(img, *parts):
    path = os.path.join(PACK_DIR, *parts)
    ensure_dir(path)
    img.save(path)
    print(f"  {'/'.join(parts)}")


def filled_rect(draw, x, y, w, h, color=WHITE):
    draw.rectangle([x, y, x + w - 1, y + h - 1], fill=color)


def filled_circle(draw, cx, cy, r, color=WHITE):
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color)


def hollow_circle(draw, cx, cy, r, color=WHITE, width=2):
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], outline=color, width=width)


def draw_triangle_right(draw, x, y, w, h, color=WHITE):
    """Right-pointing filled triangle."""
    draw.polygon([(x, y), (x + w, y + h // 2), (x, y + h)], fill=color)


def draw_triangle_left(draw, x, y, w, h, color=WHITE):
    """Left-pointing filled triangle."""
    draw.polygon([(x + w, y), (x, y + h // 2), (x + w, y + h)], fill=color)


# ---------------------------------------------------------------------------
# Transport controls (32 x 32 square, matching cue icon size)
# ---------------------------------------------------------------------------

def make_control(name, draw_fn):
    img = Image.new("RGBA", (32, 32), CLEAR)
    draw = ImageDraw.Draw(img)
    draw_fn(draw, 32, 32)
    save(img, "controls", name)


def draw_play_control(draw, w, h):
    # Play triangle
    draw_triangle_right(draw, 9, 5, 16, 22)


def draw_stop_control(draw, w, h):
    # Filled square
    filled_rect(draw, 7, 7, 18, 18)


def draw_pause_control(draw, w, h):
    # Two vertical bars
    filled_rect(draw, 9, 6, 5, 20)
    filled_rect(draw, 18, 6, 5, 20)


def draw_take_control(draw, w, h):
    # Downward arrow into a base line
    cx = 16
    # Shaft
    filled_rect(draw, cx - 3, 4, 6, 12)
    # Arrowhead
    draw.polygon([
        (cx - 8, 16), (cx + 8, 16), (cx, 24)
    ], fill=WHITE)
    # Base line
    filled_rect(draw, 5, 27, 22, 2)


def draw_clear_control(draw, w, h):
    # Fading rectangle — dissolve to black
    rect_w, rect_h = 22, 18
    x, y = 5, 7
    for col in range(rect_w):
        alpha_frac = 1.0 - (col / rect_w) ** 1.5
        a = int(255 * max(0.0, min(1.0, alpha_frac)))
        color = (255, 255, 255, a)
        filled_rect(draw, x + col, y, 1, rect_h, color)
    filled_rect(draw, x, y, 2, rect_h, WHITE)
    filled_rect(draw, x, y, rect_w // 2, 2, WHITE)
    filled_rect(draw, x, y + rect_h - 2, rect_w // 2, 2, WHITE)


def draw_rerack_control(draw, w, h):
    # |◀  skip back to start
    filled_rect(draw, 6, 7, 3, 18)
    draw_triangle_left(draw, 11, 8, 14, 16)


def draw_blackout_control(draw, w, h):
    # Circle with diagonal slash
    cx, cy = 16, 16
    r = 10
    hollow_circle(draw, cx, cy, r, WHITE, width=2)
    draw.line([(cx - 6, cy + 6), (cx + 6, cy - 6)], fill=WHITE, width=2)


def draw_fullscreen_control(draw, w, h):
    # Four corner arrows pointing outward
    # Top-left
    filled_rect(draw, 4, 4, 8, 2, WHITE)
    filled_rect(draw, 4, 4, 2, 8, WHITE)
    # Top-right
    filled_rect(draw, 20, 4, 8, 2, WHITE)
    filled_rect(draw, 26, 4, 2, 8, WHITE)
    # Bottom-left
    filled_rect(draw, 4, 26, 8, 2, WHITE)
    filled_rect(draw, 4, 20, 2, 8, WHITE)
    # Bottom-right
    filled_rect(draw, 20, 26, 8, 2, WHITE)
    filled_rect(draw, 26, 20, 2, 8, WHITE)


def draw_window_control(draw, w, h):
    # Windowed mode — four corner arrows pointing inward
    # Top-left arrow pointing inward (toward center)
    filled_rect(draw, 9, 5, 2, 7, WHITE)
    filled_rect(draw, 5, 9, 7, 2, WHITE)
    # Top-right
    filled_rect(draw, 21, 5, 2, 7, WHITE)
    filled_rect(draw, 20, 9, 7, 2, WHITE)
    # Bottom-left
    filled_rect(draw, 9, 20, 2, 7, WHITE)
    filled_rect(draw, 5, 21, 7, 2, WHITE)
    # Bottom-right
    filled_rect(draw, 21, 20, 2, 7, WHITE)
    filled_rect(draw, 20, 21, 7, 2, WHITE)


# ---------------------------------------------------------------------------
# Cue type icons (32 x 32)
# ---------------------------------------------------------------------------

def make_cue_icon(name, draw_fn):
    img = Image.new("RGBA", (32, 32), CLEAR)
    draw = ImageDraw.Draw(img)
    draw_fn(draw)
    save(img, "cue_icons", name)


def draw_cue_video(draw):
    # Film frame / clapperboard
    filled_rect(draw, 3, 6, 26, 20, WHITE)
    filled_rect(draw, 5, 8, 22, 16, CLEAR)
    # Sprocket holes
    for y in [8, 13, 18, 23]:
        filled_rect(draw, 4, y, 3, 2, WHITE)
        filled_rect(draw, 25, y, 3, 2, WHITE)
    # Play triangle in center
    draw_triangle_right(draw, 13, 11, 8, 10)


def draw_cue_image(draw):
    # Frame with mountain
    filled_rect(draw, 3, 4, 26, 24, MID)
    filled_rect(draw, 5, 6, 22, 20, CLEAR)
    # Mountains
    draw.polygon([(6, 24), (13, 12), (20, 24)], fill=WHITE)
    draw.polygon([(16, 24), (22, 15), (26, 24)], fill=MID)
    # Sun
    filled_rect(draw, 21, 8, 4, 4, WHITE)


def draw_cue_audio(draw):
    # Waveform bars
    bars = [6, 14, 22, 26, 18, 24, 20, 10, 8, 12]
    bar_w = 2
    gap = 1
    total_w = len(bars) * (bar_w + gap) - gap
    start_x = (32 - total_w) // 2
    for i, h in enumerate(bars):
        x = start_x + i * (bar_w + gap)
        y = (32 - h) // 2
        filled_rect(draw, x, y, bar_w, h, WHITE)


def draw_cue_browser(draw):
    # Globe
    cx, cy, r = 16, 16, 11
    hollow_circle(draw, cx, cy, r, WHITE, width=2)
    # Horizontal line
    filled_rect(draw, cx - r, cy, r * 2 + 1, 2, WHITE)
    # Vertical line
    filled_rect(draw, cx, cy - r, 2, r * 2 + 1, WHITE)
    # Curved meridians (approximate with ellipses)
    draw.ellipse([cx - 5, cy - r, cx + 5, cy + r], outline=MID, width=1)


def draw_cue_pattern(draw):
    # Grid / color bars
    for i in range(4):
        x = 4 + i * 6
        filled_rect(draw, x, 4, 5, 24, WHITE if i % 2 == 0 else MID)
    # Horizontal lines
    filled_rect(draw, 4, 4, 24, 2, WHITE)
    filled_rect(draw, 4, 26, 24, 2, WHITE)


def draw_cue_source(draw):
    # Camera lens
    cx, cy = 16, 16
    hollow_circle(draw, cx, cy, 10, WHITE, width=2)
    hollow_circle(draw, cx, cy, 5, WHITE, width=2)
    filled_rect(draw, cx - 1, cy - 1, 3, 3, WHITE)
    # Camera body hint
    filled_rect(draw, 2, 10, 5, 12, MID)


def draw_cue_lowerthird(draw):
    # Two overlapping rectangles — foreground overlay
    filled_rect(draw, 3, 4, 22, 12, MID)
    filled_rect(draw, 5, 18, 24, 10, WHITE)
    # Text lines in lower rect
    filled_rect(draw, 8, 20, 16, 2, CLEAR)
    filled_rect(draw, 8, 24, 12, 2, CLEAR)


def draw_cue_pip(draw):
    # Small rectangle inside larger one
    filled_rect(draw, 3, 4, 26, 24, MID)
    filled_rect(draw, 5, 6, 22, 20, CLEAR)
    # Small PiP window
    filled_rect(draw, 17, 16, 10, 8, WHITE)


def draw_cue_srt(draw):
    # Antenna with signal waves
    cx = 16
    # Antenna pole
    filled_rect(draw, cx - 1, 14, 3, 14, WHITE)
    # Antenna base
    filled_rect(draw, cx - 5, 26, 11, 3, WHITE)
    # Signal arcs
    for i, r in enumerate([5, 9, 13]):
        a = 255 - i * 60
        color = (255, 255, 255, a)
        draw.arc([cx - r, 8 - r, cx + r, 8 + r], 200, 340, fill=color, width=2)


def draw_cue_composite(draw):
    # Four rectangles in a 2x2 grid
    gap = 2
    cell_w = (26 - gap) // 2
    cell_h = (24 - gap) // 2
    for row in range(2):
        for col in range(2):
            x = 3 + col * (cell_w + gap)
            y = 4 + row * (cell_h + gap)
            alpha = WHITE if (row + col) % 2 == 0 else MID
            filled_rect(draw, x, y, cell_w, cell_h, alpha)


# ---------------------------------------------------------------------------
# End action badges (16 x 16)
# ---------------------------------------------------------------------------

def make_end_badge(name, draw_fn):
    img = Image.new("RGBA", (16, 16), CLEAR)
    draw = ImageDraw.Draw(img)
    draw_fn(draw)
    save(img, "end_actions", name)


def draw_end_loop(draw):
    # Circular arrow
    cx, cy, r = 8, 8, 5
    draw.arc([cx - r, cy - r, cx + r, cy + r], 30, 330, fill=WHITE, width=2)
    # Arrowhead at ~330 degrees (top-right)
    draw.polygon([(12, 4), (14, 6), (10, 6)], fill=WHITE)


def draw_end_hold(draw):
    # Pause bars
    filled_rect(draw, 5, 3, 2, 10, WHITE)
    filled_rect(draw, 9, 3, 2, 10, WHITE)


def draw_end_next(draw):
    # Double right chevron >>
    draw.polygon([(3, 3), (8, 8), (3, 13)], fill=WHITE)
    draw.polygon([(8, 3), (13, 8), (8, 13)], fill=WHITE)


def draw_end_stop(draw):
    # Small square
    filled_rect(draw, 4, 4, 8, 8, WHITE)


# ---------------------------------------------------------------------------
# Mode/status icons (24 x 24)
# ---------------------------------------------------------------------------

def make_mode_icon(name, draw_fn):
    img = Image.new("RGBA", (24, 24), CLEAR)
    draw = ImageDraw.Draw(img)
    draw_fn(draw)
    save(img, "mode_icons", name)


def draw_mode_loop(draw):
    cx, cy, r = 12, 12, 8
    draw.arc([cx - r, cy - r, cx + r, cy + r], 30, 330, fill=WHITE, width=2)
    # Arrowhead
    draw.polygon([(18, 5), (21, 8), (16, 9)], fill=WHITE)


def draw_mode_shuffle(draw):
    # Crossing arrows
    draw.line([(4, 6), (20, 18)], fill=WHITE, width=2)
    draw.line([(4, 18), (20, 6)], fill=WHITE, width=2)
    # Arrowheads
    draw.polygon([(20, 18), (16, 18), (18, 14)], fill=WHITE)
    draw.polygon([(20, 6), (16, 6), (18, 10)], fill=WHITE)


def draw_mode_once(draw):
    # Single play: right arrow with "1"
    draw_triangle_right(draw, 4, 6, 10, 12)
    # "1" character
    filled_rect(draw, 17, 5, 2, 14, WHITE)
    filled_rect(draw, 15, 5, 2, 2, WHITE)
    filled_rect(draw, 14, 19, 8, 2, WHITE)


def draw_mode_order(draw):
    # Sequential: three horizontal lines with downward arrow
    filled_rect(draw, 4, 5, 12, 2, WHITE)
    filled_rect(draw, 4, 10, 12, 2, WHITE)
    filled_rect(draw, 4, 15, 12, 2, WHITE)
    # Down arrow on right side
    filled_rect(draw, 19, 4, 2, 12, WHITE)
    draw.polygon([(15, 16), (23, 16), (19, 21)], fill=WHITE)


def draw_mode_live(draw):
    # Filled circle (record dot)
    filled_circle(draw, 12, 12, 7, WHITE)


def draw_mode_armed(draw):
    # Hollow circle (standby ring)
    hollow_circle(draw, 12, 12, 7, WHITE, width=2)


def draw_mode_off(draw):
    # Circle with slash
    hollow_circle(draw, 12, 12, 7, WHITE, width=2)
    draw.line([(7, 17), (17, 7)], fill=WHITE, width=2)


def draw_mode_warp(draw):
    # Four corner arrows
    corners = [(3, 3), (21, 3), (3, 21), (21, 21)]
    dirs = [(1, 1), (-1, 1), (1, -1), (-1, -1)]
    for (cx, cy), (dx, dy) in zip(corners, dirs):
        filled_rect(draw, cx, cy, 2, 2, WHITE)
        # Small arrow lines pointing inward
        for i in range(5):
            px = cx + dx * i
            py = cy + dy * i
            if 0 <= px < 24 and 0 <= py < 24:
                draw.point((px, py), fill=WHITE)
    # Dashed border
    for i in range(0, 24, 3):
        filled_rect(draw, i, 0, 2, 1, MID)
        filled_rect(draw, i, 23, 2, 1, MID)
        filled_rect(draw, 0, i, 1, 2, MID)
        filled_rect(draw, 23, i, 1, 2, MID)


# ---------------------------------------------------------------------------
# Toolbar icons (24 x 24)
# ---------------------------------------------------------------------------

def make_toolbar_icon(name, draw_fn):
    img = Image.new("RGBA", (24, 24), CLEAR)
    draw = ImageDraw.Draw(img)
    draw_fn(draw)
    save(img, "toolbar", name)


def draw_toolbar_new(draw):
    # Blank page with plus
    filled_rect(draw, 5, 2, 14, 20, MID)
    # Page fold
    draw.polygon([(14, 2), (19, 7), (14, 7)], fill=WHITE)
    # Plus
    filled_rect(draw, 9, 12, 6, 2, WHITE)
    filled_rect(draw, 11, 10, 2, 6, WHITE)


def draw_toolbar_open(draw):
    # Opening folder
    # Back panel
    filled_rect(draw, 3, 6, 18, 14, MID)
    # Tab
    filled_rect(draw, 3, 4, 8, 4, MID)
    # Front panel (open)
    draw.polygon([(2, 20), (5, 10), (22, 10), (19, 20)], fill=WHITE)


def draw_toolbar_save(draw):
    # Floppy disk
    filled_rect(draw, 3, 3, 18, 18, WHITE)
    # Metal slider
    filled_rect(draw, 8, 3, 8, 7, MID)
    filled_rect(draw, 12, 3, 3, 7, CLEAR)
    # Label area
    filled_rect(draw, 6, 13, 12, 7, MID)
    filled_rect(draw, 7, 14, 10, 2, WHITE)
    filled_rect(draw, 7, 17, 7, 2, WHITE)


def draw_toolbar_import(draw):
    # Arrow down into tray
    cx = 12
    # Arrow shaft
    filled_rect(draw, cx - 2, 3, 5, 10, WHITE)
    # Arrowhead
    draw.polygon([(cx - 6, 13), (cx + 7, 13), (cx, 19)], fill=WHITE)
    # Tray
    filled_rect(draw, 4, 20, 16, 2, WHITE)
    filled_rect(draw, 4, 16, 2, 6, MID)
    filled_rect(draw, 18, 16, 2, 6, MID)


def draw_toolbar_prefs(draw):
    # Gear / cog
    cx, cy = 12, 12
    # Center hub
    filled_circle(draw, cx, cy, 3, WHITE)
    # Outer ring
    hollow_circle(draw, cx, cy, 7, WHITE, width=2)
    # Teeth (8 directions)
    import math
    for angle_deg in range(0, 360, 45):
        angle = math.radians(angle_deg)
        for r in range(7, 10):
            px = int(cx + r * math.cos(angle))
            py = int(cy + r * math.sin(angle))
            if 0 <= px < 24 and 0 <= py < 24:
                filled_rect(draw, px, py, 2, 2, WHITE)


# ---------------------------------------------------------------------------
# Output chips (200 x 50) — reuse existing dimensions
# ---------------------------------------------------------------------------

def make_output_chip(name, draw_fn):
    img = Image.new("RGBA", (200, 50), CLEAR)
    draw = ImageDraw.Draw(img)
    draw_fn(draw)
    save(img, "outputs", name)


def draw_chip_idle(draw):
    # Subdued monitor outline
    filled_rect(draw, 4, 4, 192, 42, SOFT)
    filled_rect(draw, 6, 6, 188, 38, CLEAR)
    # Monitor icon (left area)
    filled_rect(draw, 14, 12, 24, 16, MID)
    filled_rect(draw, 16, 14, 20, 12, CLEAR)
    # Stand
    filled_rect(draw, 22, 28, 8, 4, MID)
    filled_rect(draw, 18, 32, 16, 2, MID)


def draw_chip_armed(draw):
    # Brighter outline, standby ring
    filled_rect(draw, 4, 4, 192, 42, MID)
    filled_rect(draw, 6, 6, 188, 38, CLEAR)
    # Hollow ring (standby)
    hollow_circle(draw, 26, 25, 8, WHITE, width=2)


def draw_chip_live(draw):
    # Bold, prominent
    filled_rect(draw, 4, 4, 192, 42, WHITE)
    filled_rect(draw, 6, 6, 188, 38, CLEAR)
    # Filled circle (live dot)
    filled_circle(draw, 26, 25, 8, WHITE)


def draw_chip_warning(draw):
    # Alert triangle
    filled_rect(draw, 4, 4, 192, 42, MID)
    filled_rect(draw, 6, 6, 188, 38, CLEAR)
    # Triangle
    draw.polygon([(26, 12), (36, 34), (16, 34)], fill=WHITE)
    draw.polygon([(26, 18), (32, 31), (20, 31)], fill=CLEAR)
    # Exclamation
    filled_rect(draw, 25, 19, 2, 6, WHITE)
    filled_rect(draw, 25, 27, 2, 2, WHITE)


def draw_chip_offline(draw):
    # Very dim, X'd out
    filled_rect(draw, 4, 4, 192, 42, SOFT)
    filled_rect(draw, 6, 6, 188, 38, CLEAR)
    # X mark
    draw.line([(18, 14), (34, 36)], fill=MID, width=2)
    draw.line([(34, 14), (18, 36)], fill=MID, width=2)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    print("Generating Deckboy icon pack...")
    print()

    print("[controls]")
    make_control("play.png", draw_play_control)
    make_control("stop.png", draw_stop_control)
    make_control("pause.png", draw_pause_control)
    make_control("take.png", draw_take_control)
    make_control("clear.png", draw_clear_control)
    make_control("rerack.png", draw_rerack_control)
    make_control("blackout.png", draw_blackout_control)
    make_control("fullscreen.png", draw_fullscreen_control)
    make_control("window.png", draw_window_control)

    print("[cue_icons]")
    make_cue_icon("video.png", draw_cue_video)
    make_cue_icon("image.png", draw_cue_image)
    make_cue_icon("audio.png", draw_cue_audio)
    make_cue_icon("browser.png", draw_cue_browser)
    make_cue_icon("pattern.png", draw_cue_pattern)
    make_cue_icon("source.png", draw_cue_source)
    make_cue_icon("lowerthird.png", draw_cue_lowerthird)
    make_cue_icon("pip.png", draw_cue_pip)
    make_cue_icon("srt.png", draw_cue_srt)
    make_cue_icon("composite.png", draw_cue_composite)

    print("[end_actions]")
    make_end_badge("loop.png", draw_end_loop)
    make_end_badge("hold.png", draw_end_hold)
    make_end_badge("next.png", draw_end_next)
    make_end_badge("stop.png", draw_end_stop)

    print("[mode_icons]")
    make_mode_icon("loop_on.png", draw_mode_loop)
    make_mode_icon("once.png", draw_mode_once)
    make_mode_icon("shuffle_on.png", draw_mode_shuffle)
    make_mode_icon("order.png", draw_mode_order)
    make_mode_icon("live.png", draw_mode_live)
    make_mode_icon("armed.png", draw_mode_armed)
    make_mode_icon("off.png", draw_mode_off)
    make_mode_icon("warp.png", draw_mode_warp)

    print("[toolbar]")
    make_toolbar_icon("new.png", draw_toolbar_new)
    make_toolbar_icon("open.png", draw_toolbar_open)
    make_toolbar_icon("save.png", draw_toolbar_save)
    make_toolbar_icon("import.png", draw_toolbar_import)
    make_toolbar_icon("prefs.png", draw_toolbar_prefs)

    print("[outputs]")
    make_output_chip("chip_idle.png", draw_chip_idle)
    make_output_chip("chip_armed.png", draw_chip_armed)
    make_output_chip("chip_live.png", draw_chip_live)
    make_output_chip("chip_warning.png", draw_chip_warning)
    make_output_chip("chip_offline.png", draw_chip_offline)

    # Update manifest
    manifest_path = os.path.join(PACK_DIR, "manifest.json")
    manifest = {
        "header/header_default": "top header strip",
        "outputs/chip_*": "output status chips",
        "cue_icons/*": "cue type icons (32x32, white-on-transparent)",
        "end_actions/*": "cue end-action badges (16x16, white-on-transparent)",
        "mode_icons/*": "mode/status toggle icons (24x24, white-on-transparent)",
        "toolbar/*": "header toolbar action icons (24x24, white-on-transparent)",
        "controls/*": "transport buttons (220x70, white-on-transparent)",
        "monitor/monitor_frame": "program monitor frame",
        "routing/routing_row": "routing table row",
        "master_cues/scene_card": "master scene card",
        "textures/dot_grid": "background grid",
        "splash/deckboy_splash_deckgirl": "splash screen with Deckgirl illustration",
        "styleguide/styleguide": "palette and typography reference"
    }
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2)
    print()
    print(f"Updated {manifest_path}")

    total = 7 + 10 + 4 + 6 + 5 + 5
    print(f"\nDone — {total} icons generated.")


if __name__ == "__main__":
    main()
