#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
# This file is part of Deckboy, a cue deck for live events.
# See LICENSE for details.
"""Which catalogue lines would make a string built at run time translate.

translate() takes a string it has no line for apart along the seams the desk
uses (native/core/i18n.cpp, composeHit): a name and its qualifier, numbers
({n}), columns (two or more spaces), a label and its value. This does the same
on a list of strings -- typically the misses a run with DECKBOY_I18N_MISSES
recorded -- and prints the smallest pieces the catalogue does not have.

    python tools/i18n_pieces.py <misses.txt> [--lang bg]

A piece that is only numbers, punctuation or a path is left out: it has nothing
to translate. What is printed still needs a person (or tools/i18n_translate.py)
to judge: a screen's name or a codec is a piece too, and stays English.
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NUMBER = re.compile(r"\d+(?:[.,:]\d+)*")


def load(code):
    keys = set()
    for line in open(ROOT / "data" / "lang" / f"{code}.tsv", encoding="utf8"):
        if "\t" in line and not line.startswith("#"):
            keys.add(line.split("\t", 1)[0].lower())
    return keys


def pieces(s, have, out, depth=0):
    """True when the catalogue already covers `s`; otherwise adds the pieces
    it lacks to `out`, as composeHit would look them up."""
    if not s or depth > 4:
        return False
    if s.lower() in have:
        return True
    template = NUMBER.sub("{n}", s)
    if template != s and template.lower() in have:
        return True
    found = []
    if len(s) > 4 and s.endswith(")") and " (" in s:
        open_ = s.rfind(" (")
        base, qual = s[:open_], s[open_ + 2:-1]
        if "(" not in qual and ")" not in qual and open_ > 0:
            found.append((base, qual))
    if "  " in s:
        found.append(tuple(p for p in re.split(r" {2,}", s) if p))
    if ": " in s and s.index(": ") > 0:
        i = s.index(": ")
        found.append((s[:i], s[i + 2:]))
    if not found:
        out.add(template)
        return False
    # The first seam that applies is the one composeHit takes apart first;
    # report its pieces.
    any_hit = False
    for part in found[0]:
        any_hit = pieces(part, have, out, depth + 1) or any_hit
    return False if not any_hit else True


def interesting(p):
    if not re.search(r"[A-Za-z]{2}", p):
        return False                         # numbers, symbols
    if re.search(r"[\\/]\w*\.\w|^[A-Za-z]:\\", p):
        return False                         # a path or a file
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("misses")
    ap.add_argument("--lang", default="bg")
    a = ap.parse_args()
    have = load(a.lang)
    out = set()
    for s in open(a.misses, encoding="utf8").read().splitlines():
        if s.strip():
            pieces(s, have, out)
    sys.stdout.reconfigure(encoding="utf-8")
    for p in sorted(p for p in out if interesting(p)):
        print(p)


if __name__ == "__main__":
    main()
