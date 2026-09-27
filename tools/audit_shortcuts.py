#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
# This file is part of Deckboy, a cue deck for live events.
# See LICENSE for details.
"""Cross-check the shortcut overlay against the keys the app actually handles.

The overlay in app_overlays.ipp is the only list of shortcuts an operator ever
sees, and it has drifted in both directions:

  * It has advertised keys that do nothing. Seven entries were found to be
    fiction and removed by hand -- Ctrl+O listed twice, a parked feature, a
    Backspace described as something it never did.
  * It has OMITTED keys that work, which is the worse direction, because a
    feature nobody can find is indistinguishable from one that was never built.
    Eight were missing at once, including take / play / stop on every deck and
    Ctrl+D, the only way into the dashboard and the master tracker. The
    dashboard helpfully says "Ctrl+D or Esc to close" -- which you can only read
    once you have already found it.

Both directions were fixed by reading the two lists side by side, which is
exactly the job a script should do. So:

  MISSING  a Ctrl chord the input handler acts on that the overlay never names.
  FICTION  a Ctrl chord the overlay names that no handler acts on.

Only Ctrl chords, deliberately. Bare letters are matched in a `switch` spread
over hundreds of lines with fall-through and guards, and every attempt to infer
them produces noise; a Ctrl chord is written one way -- `ctrl && key == SDLK_x`
-- and can be read exactly. A narrow check that is always right beats a broad
one that gets ignored.

    python3 tools/audit_shortcuts.py
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OVERLAYS = os.path.join(ROOT, "native", "app", "app_overlays.ipp")
INPUT_IPP = os.path.join(ROOT, "native", "app", "app_input.ipp")
MANUAL = os.path.join(ROOT, "MANUAL.md")

# How an SDL keycode is spelled in the overlay's key column.
KEY_NAMES = {
    "SLASH": "/",
    "COMMA": ",",
    "SPACE": "Space",
    "RETURN": "Enter",
    "KP_ENTER": "Enter",
    "PERIOD": ".",
    "LEFTBRACKET": "[",
    "RIGHTBRACKET": "]",
}


def key_label(token):
    """SDLK_D -> "D";  SDLK_SLASH -> "/"."""
    name = token[len("SDLK_"):]
    return KEY_NAMES.get(name, name.upper() if len(name) == 1 else name)


def handled_chords(text):
    """Every Ctrl chord the input handler acts on, as a set of labels."""
    found = set()
    # Four shapes are all in use, and missing one of them invents fiction
    # rather than finding it -- the first version of this check read only the
    # first two and reported six working shortcuts as unhandled:
    #
    #   ctrl && key == SDLK_D                     plain
    #   ctrl && shift && key == SDLK_S            with shift
    #   ctrl && !shift && key == SDLK_Z           plain, said explicitly
    #   ctrl && (key == SDLK_RETURN || ...)       either of two keys
    #
    # `!shift` means the chord WITHOUT shift, so it is a plain Ctrl chord.
    pattern = re.compile(
        r"ctrl\s*&&\s*(!\s*shift\s*&&\s*|shift\s*&&\s*)?"
        r"\(?\s*key\s*==\s*(SDLK_[A-Za-z0-9_]+)")
    for modifier, token in pattern.findall(text):
        with_shift = modifier.strip().startswith("shift")
        chord = "Ctrl+" + ("Shift+" if with_shift else "") + key_label(token)
        found.add(chord)
    return found


def listed_chords(text):
    """Every Ctrl chord the overlay's table names, as a set of labels."""
    start = text.find("static const ShortcutEntry shortcuts[]")
    if start < 0:
        sys.exit("audit_shortcuts: could not find the shortcuts table")
    end = text.find("};", start)
    table = text[start:end]
    found = set()
    for key in re.findall(r'\{\s*"([^"]+)"\s*,', table):
        # "Ctrl+Shift+Space" yes; ". / ," and "Up / Down" no.
        if key.startswith("Ctrl+"):
            found.add(key)
    return found


def normalise(chord):
    """Compare case-insensitively on the key, so Ctrl+D matches Ctrl+d."""
    return chord.lower()


def overlay_table(text):
    """Every (key, description) pair the overlay draws, in order."""
    start = text.find("static const ShortcutEntry shortcuts[]")
    if start < 0:
        sys.exit("audit_shortcuts: could not find the shortcuts table")
    end = text.find("};", start)
    return re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}', text[start:end])


def manual_table(text):
    """Every (key, description) pair in MANUAL.md's keyboard reference.

    The manual is a published web page, so its copy of this table is the one
    most people read -- and it had drifted furthest. It listed H for hold when
    the app has no H binding at all, P for preferences when P adds a pattern
    cue, and Backspace for clearing overlays, which is one of the entries the
    app's own list had already been corrected to remove as fiction. Roughly
    twenty keys were simply absent.
    """
    head = text.find("## 24. Keyboard Reference")
    if head < 0:
        sys.exit("audit_shortcuts: MANUAL.md has no section 24")
    tail = text.find("## 25.", head)
    body = text[head:tail if tail > 0 else len(text)]
    rows = []
    for line in body.split("\n"):
        line = line.strip()
        if not line.startswith("|") or line.startswith("|--") or "Key | Action" in line:
            continue
        cells = [c.strip() for c in line.strip("|").split("|")]
        if len(cells) != 2:
            continue
        # "`Ctrl+Shift+C` / `Ctrl+Shift+V`" -> "Ctrl+Shift+C / Ctrl+Shift+V"
        key = " / ".join(p.strip().strip("`") for p in cells[0].split("/"))
        rows.append((key, cells[1]))
    return rows


def main():
    overlays = io.open(OVERLAYS, encoding="utf-8", errors="replace").read()
    keys = io.open(INPUT_IPP, encoding="utf-8", errors="replace").read()

    handled = handled_chords(keys)
    listed = listed_chords(overlays)
    if not handled:
        sys.exit("audit_shortcuts: found no Ctrl chords in app_input.ipp -- "
                 "did the handler change shape?")
    if not listed:
        sys.exit("audit_shortcuts: found no Ctrl chords in the overlay table")

    listed_norm = {normalise(c) for c in listed}
    handled_norm = {normalise(c) for c in handled}

    missing = sorted(c for c in handled if normalise(c) not in listed_norm)
    fiction = sorted(c for c in listed if normalise(c) not in handled_norm)

    print("Ctrl chords handled: %d   listed in the overlay: %d"
          % (len(handled), len(listed)))

    if missing:
        print()
        print("MISSING from the shortcut overlay: %d" % len(missing))
        print("  (the app does these; nothing tells the operator so)")
        for chord in missing:
            print("    %s" % chord)
    if fiction:
        print()
        print("ADVERTISED but not handled: %d" % len(fiction))
        print("  (the overlay promises these; no handler acts on them)")
        for chord in fiction:
            print("    %s" % chord)

    # ── AND THE MANUAL SAYS THE SAME THING ───────────────────────────────────
    #
    # Compared as whole rows, key AND description, because a manual that names
    # the right key for the wrong action is no more use than one that names the
    # wrong key. Both tables are short and the manual's is generated from the
    # overlay, so exact equality is the right bar.
    overlay_rows = overlay_table(overlays)
    manual_text = io.open(MANUAL, encoding="utf-8", errors="replace").read()
    manual_rows = manual_table(manual_text)

    def rowkey(pair):
        key, desc = pair
        key = " / ".join(p.strip() for p in key.split("/"))
        return (key.lower(), " ".join(desc.split()).lower())

    overlay_set = {rowkey(r) for r in overlay_rows}
    manual_set = {rowkey(r) for r in manual_rows}
    not_in_manual = sorted(r for r in overlay_rows if rowkey(r) not in manual_set)
    not_in_overlay = sorted(r for r in manual_rows if rowkey(r) not in overlay_set)

    print("overlay rows: %d   MANUAL.md section 24 rows: %d"
          % (len(overlay_rows), len(manual_rows)))
    if not_in_manual:
        print()
        print("IN THE APP BUT NOT IN THE MANUAL: %d" % len(not_in_manual))
        for key, desc in not_in_manual:
            print("    %-18s %s" % (key, desc))
    if not_in_overlay:
        print()
        print("IN THE MANUAL BUT NOT IN THE APP: %d" % len(not_in_overlay))
        for key, desc in not_in_overlay:
            print("    %-18s %s" % (key, desc))

    if missing or fiction or not_in_manual or not_in_overlay:
        return 1
    print("every Ctrl chord the app handles is listed, nothing extra, and "
          "MANUAL.md agrees row for row.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
