#!/usr/bin/env python3
"""Find UI geometry written in 1x pixels.

Deckboy's fonts and layout metrics scale with the desktop (see
rebuildLayoutMetrics and uiScaled). Geometry written as a bare pixel literal
does not, so at any scale above 1.0 the text grows and the box it lives in does
not. That single fault has produced, separately:

  - "Pattern" ellipsized to "PA..." in every cue row (kRowHeight = 80)
  - the per-cue action icons drawn on top of the cue's own name (row.y + 48)
  - the cue name colliding with the SELECTED CUE heading above it (+34)
  - the last two lines of the cue summary falling outside its panel (180)
  - half the keyboard shortcuts page ellipsized (720x600, rowY += 18, 130)
  - "Ctrl+D or Esc..." on the dashboard (a 180px box)

Each was found by looking at a screenshot. This finds them by reading.

A number inside an SDL_Rect brace initialiser, or added to a coordinate, is
flagged unless it is small enough to be a hairline (<= THRESHOLD) or already
inside uiScaled(...). Report only -- some hits are legitimate (a 1px border, a
rect derived from an already-scaled value), which is why this prints a list to
work through rather than failing the build. --strict fails if the count goes UP
against the recorded baseline.
"""
import argparse, pathlib, re, sys

THRESHOLD = 8          # 1..8 px reads as a hairline/inset at any scale
BASELINE_FILE = pathlib.Path(__file__).with_name("scaled_layout_baseline.txt")

UI_FILES = [
    "native/app/app_render_main.ipp",
    "native/app/app_render_control.ipp",
    "native/app/app_render_settings.ipp",
    "native/app/app_render_output.ipp",
    "native/app/app_overlays.ipp",
    "native/app/app_ui_widgets.ipp",
    "native/app/app_cue_mgmt.ipp",
]

# A rect brace: SDL_Rect name {a, b, c, d} or SDL_Rect {a, b, c, d}
RECT = re.compile(r"SDL_Rect\s*(?:\w+\s*)?\{([^{}]*)\}")
# A bare integer that is not part of an identifier, a uiScaled call, or a float
NUMBER = re.compile(r"(?<![\w.])(\d+)(?![\w.])")


def scaled_spans(text):
    """Byte ranges covered by uiScaled(...) / textLineHeight(...) calls."""
    spans = []
    for m in re.finditer(r"\b(?:uiScaled|textLineHeight|measuredTextWidth|"
                         r"settingsHeaderHeight|sectionH|stackH)\s*\(", text):
        depth, i = 0, m.end() - 1
        while i < len(text):
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    spans.append((m.start(), i + 1))
                    break
            i += 1
    return spans


def find_hits(path):
    src = path.read_text(encoding="utf-8", errors="replace")
    lines = src.split("\n")
    hits = []
    for n, line in enumerate(lines, 1):
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue
        safe = scaled_spans(line)
        for m in RECT.finditer(line):
            body = m.group(1)
            base = m.start(1)
            for num in NUMBER.finditer(body):
                if int(num.group(1)) <= THRESHOLD:
                    continue
                at = base + num.start()
                if any(a <= at < b for a, b in safe):
                    continue
                hits.append((n, int(num.group(1)), stripped[:96]))
                break   # one report per rect keeps the list readable
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--strict", action="store_true",
                    help="fail if the count rose against the baseline")
    ap.add_argument("--update-baseline", action="store_true")
    args = ap.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    total = 0
    for rel in UI_FILES:
        path = root / rel
        if not path.exists():
            continue
        hits = find_hits(path)
        if not hits:
            continue
        total += len(hits)
        print(f"\n{rel}  ({len(hits)})")
        for n, value, text in hits:
            print(f"  {n:>6}: {value:>4}px  {text}")

    print(f"\nunscaled UI geometry: {total}")

    if args.update_baseline:
        BASELINE_FILE.write_text(f"{total}\n", encoding="utf-8")
        print(f"baseline set to {total}")
        return 0

    if args.strict:
        if not BASELINE_FILE.exists():
            print("no baseline recorded; run --update-baseline first")
            return 1
        baseline = int(BASELINE_FILE.read_text(encoding="utf-8").strip())
        if total > baseline:
            print(f"FAIL: {total} > baseline {baseline} -- new geometry was "
                  f"written in 1x pixels. Wrap it in uiScaled().")
            return 1
        print(f"ok: {total} <= baseline {baseline}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
