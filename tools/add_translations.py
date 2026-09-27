#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
# This file is part of Deckboy, a cue deck for live events.
# See LICENSE for details.
"""Append translations to a catalogue, safely and repeatably.

Filling thirty-one catalogues by hand is thousands of lines, and the ways to
get it wrong are all silent: a stray tab splits one entry into two, a
duplicate key shadows an earlier one, a translation left equal to its English
key looks done and is not, and a trailing space makes a key that never
matches. Every one of those produces a file that loads and a language that is
quietly still English.

So the entries go in through here. It reads a UTF-8 TSV of
`english<TAB>translation` on stdin, and:

  * refuses a key that is already in the catalogue, rather than adding a
    second line that will be shadowed by the first
  * refuses a value containing a tab or a newline
  * refuses a value identical to its key, which is the shape of an entry
    somebody meant to come back to
  * trims, and refuses a key that is empty after trimming
  * keeps the file's own line endings and its header block

It prints what it added and what it refused, so a mistake is visible at the
moment it is made rather than at the next audit.

Usage:
    python tools/add_translations.py de < batch.tsv
"""
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LANG_DIR = os.path.join(ROOT, "data", "lang")


def add_one(code, entries, allow_same, fill_empty=False):
    """entries is a list of (english, translation). Returns (added, refused).

    With fill_empty, a key already present but with an EMPTY value is filled in
    place instead of refused. Those lines are the unfinished ones -- i18n.cpp
    skips them so they draw as English -- and they cannot be appended past,
    because the first occurrence of a key wins and an appended duplicate would
    sit there being shadowed by the blank above it. It only ever writes over
    nothing; a key that already has a translation is still refused.
    """
    path = os.path.join(LANG_DIR, code + ".tsv")
    if not os.path.isfile(path):
        return [], [("(catalogue)", "no file at " + path)]

    text = io.open(path, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in text else "\n"
    existing = {}
    for line in text.split(nl):
        if not line or line.startswith("#") or "\t" not in line:
            continue
        k, _, v = line.partition("\t")
        existing[k] = v

    added, refused, filled = [], [], {}
    for key, value in entries:
        key, value = key.strip(), value.strip()
        if not key:
            refused.append((value[:40], "empty key"))
        elif not value:
            refused.append((key, "empty translation"))
        elif value == key and not allow_same:
            refused.append((key, "identical to the English (use --allow-same "
                                 "if that is the translation)"))
        elif "\t" in value or "\n" in value:
            refused.append((key, "translation contains a tab or newline"))
        elif key in existing and existing[key].strip():
            refused.append((key, "already in the catalogue"))
        elif key in existing:
            if not fill_empty:
                refused.append((key, "present but blank (use --fill-empty)"))
                continue
            filled[key] = value
            existing[key] = value
            added.append((key, value))
        else:
            existing[key] = value
            added.append((key, value))

    if added:
        lines = text.rstrip(nl).rstrip("\n").split(nl)
        if filled:
            for i, line in enumerate(lines):
                if not line or line.startswith("#") or "\t" not in line:
                    continue
                k, _, v = line.partition("\t")
                if not v.strip() and k in filled:
                    lines[i] = k + "\t" + filled[k]
        appended = [(k, v) for k, v in added if k not in filled]
        body = nl.join(lines)
        if appended:
            body += nl + nl.join("%s\t%s" % (k, v) for k, v in appended)
        io.open(path, "w", encoding="utf-8", newline="").write(body + nl)
    return added, refused


def add_multi(allow_same, fill_empty=False):
    """One file, `code<TAB>english<TAB>translation`, many languages."""
    raw = io.open(sys.stdin.fileno(), encoding="utf-8", errors="replace").read()
    byLang = {}
    malformed = 0
    for line in raw.split("\n"):
        line = line.rstrip("\r")
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            malformed += 1
            continue
        code = parts[0].strip()
        # A translation may itself contain nothing odd, but rejoin anyway so a
        # stray tab inside it is caught by add_one rather than silently
        # truncating the entry here.
        byLang.setdefault(code, []).append((parts[1], "\t".join(parts[2:])))

    worst = 0
    for code in sorted(byLang):
        added, refused = add_one(code, byLang[code], allow_same, fill_empty)
        print("%-10s added %3d  refused %3d" % (code, len(added), len(refused)))
        for key, why in refused[:6]:
            print("    %-38s %s" % (key[:38], why))
        if len(refused) > 6:
            print("    ... and %d more" % (len(refused) - 6))
        worst = max(worst, 1 if refused else 0)
    if malformed:
        print("%d lines had fewer than three columns and were skipped" % malformed)
    return worst


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    allow_same = "--allow-same" in sys.argv
    fill_empty = "--fill-empty" in sys.argv
    # MANY LANGUAGES IN ONE FILE. With thirty of them to fill, a file per
    # language is a lot of round trips for no benefit; `--multi` reads
    # `code<TAB>english<TAB>translation` and sorts them out here.
    if "--multi" in sys.argv:
        return add_multi(allow_same, fill_empty)
    if not args:
        sys.exit("usage: add_translations.py <language code> [--allow-same]  < entries.tsv\n"
                 "       add_translations.py --multi [--allow-same]  < code_english_translation.tsv")
    code = args[0]
    path = os.path.join(LANG_DIR, code + ".tsv")
    if not os.path.isfile(path):
        sys.exit("no catalogue at " + path)

    text = io.open(path, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in text else "\n"
    existing = {}
    for line in text.split(nl):
        if not line or line.startswith("#") or "\t" not in line:
            continue
        k, _, v = line.partition("\t")
        existing[k] = v

    raw = io.open(sys.stdin.fileno(), encoding="utf-8", errors="replace").read()
    added, refused = [], []
    for line in raw.split("\n"):
        line = line.rstrip("\r")
        if not line.strip():
            continue
        if "\t" not in line:
            refused.append((line[:40], "no tab between english and translation"))
            continue
        key, _, value = line.partition("\t")
        key = key.strip()
        value = value.strip()
        if not key:
            refused.append((line[:40], "empty key"))
            continue
        if not value:
            refused.append((key, "empty translation"))
            continue
        if value == key and not allow_same:
            # Usually this is an entry somebody meant to come back to: it
            # reads as done in every count and changes nothing on screen.
            #
            # Sometimes it is simply the answer -- Port, Format, Stereo and
            # Video are the same word in German, and Deckboy deliberately
            # leaves the trade's own vocabulary alone (TAKE, PROGRAM, NDI,
            # SRT, OSC) in every language. Those are real translations and
            # should count as done, so they are allowed with --allow-same:
            # stated on purpose, once, rather than waved through silently.
            refused.append((key, "identical to the English (use --allow-same "
                                 "if that is the translation)"))
            continue
        if "\t" in value or "\n" in value:
            refused.append((key, "translation contains a tab or newline"))
            continue
        if key in existing:
            refused.append((key, "already in the catalogue"))
            continue
        existing[key] = value
        added.append((key, value))

    if added:
        body = text.rstrip(nl).rstrip("\n")
        body += nl + nl.join("%s\t%s" % (k, v) for k, v in added) + nl
        io.open(path, "w", encoding="utf-8", newline="").write(body)

    print("%s: added %d, refused %d" % (code, len(added), len(refused)))
    for key, why in refused[:20]:
        print("  refused %-40s %s" % (key[:40], why))
    if len(refused) > 20:
        print("  ... and %d more refusals" % (len(refused) - 20))
    return 1 if refused else 0


if __name__ == "__main__":
    sys.exit(main())
