#!/usr/bin/env python3
"""Every string Deckboy measures or draws goes through the shaper.

WHY THIS EXISTS. SDL_ttf shapes one run of text in one font and one direction.
Called directly, it draws an Arabic or Persian label unjoined and backwards, a
Japanese cue name as empty boxes on an English desk, and numbers inside a
right-to-left label reversed ("%59"). native/render/text_shaper.cpp is the one
place that cuts a string into pieces SDL_ttf can do -- by the Unicode bidi
algorithm and by the font that has the glyphs -- and puts them back on one
baseline. A direct call anywhere else is a label that is wrong in every
language but the one it was tested in, so this fails the build on one.

Allowed: the shaper itself, and comment lines that name the old calls.

Usage:
    python tools/audit_text_shaping.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CALLS = re.compile(r"\bTTF_(RenderText_\w+|GetStringSize\w*|MeasureString|RenderGlyph\w*)\s*\(")
ALLOWED = {
    os.path.join("native", "render", "text_shaper.cpp"),
}
SKIP_DIRS = {os.path.join("native", "extras")}


def main():
    findings = []
    native = os.path.join(ROOT, "native")
    for dirpath, dirnames, filenames in os.walk(native):
        rel_dir = os.path.relpath(dirpath, ROOT)
        if any(rel_dir == d or rel_dir.startswith(d + os.sep) for d in SKIP_DIRS):
            dirnames[:] = []
            continue
        for name in filenames:
            if not name.endswith((".cpp", ".hpp", ".ipp", ".mm", ".h")):
                continue
            rel = os.path.join(rel_dir, name)
            if rel in ALLOWED:
                continue
            with open(os.path.join(dirpath, name), encoding="utf-8", errors="replace") as f:
                for number, line in enumerate(f, 1):
                    code = line.split("//", 1)[0]
                    if CALLS.search(code):
                        findings.append("%s:%d: %s" % (rel, number, line.strip()))
    if findings:
        print("Text drawn or measured around the shaper (use deckboy::render::shaping):")
        for f in findings:
            print("  " + f)
        return 1
    print("every text measure and draw goes through the shaper")
    return 0


if __name__ == "__main__":
    sys.exit(main())
