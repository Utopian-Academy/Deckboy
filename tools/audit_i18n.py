#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
# This file is part of Deckboy, a cue deck for live events.
# See LICENSE for details.
"""How much of the interface a language catalogue actually covers.

WHY THIS EXISTS. "The Chinese and Japanese translations don't seem to be
loading" was reported, and they load perfectly: all thirty-one catalogues
parse, every one is offered in the picker, the fonts for them resolve to real
system faces, and `translate()` is called from inside the drawing helpers so
every drawn string goes through it.

The catalogues are simply THIN. Seventy-two strings each -- the top bar, the
settings tabs, a dozen card titles, ON/OFF, the transport states. Everything
else stays English. Pick Japanese and the buttons change while the rest of the
interface does not, which from the operator's chair is indistinguishable from
a translation that failed to load.

So the thing to measure is COVERAGE, and nothing was measuring it. This walks
the source for the strings that actually reach the screen and reports what
fraction of them any given catalogue can translate.

WHAT COUNTS AS A DRAWN STRING. A literal passed as the text argument to one of
the drawing helpers. Those helpers call i18n::translate() themselves, so a
literal reaching one of them is exactly a string that COULD be translated and
either is or is not. Format strings, paths, tokens and single characters are
skipped -- they are not prose and translating them would break things.

Usage:
    python tools/audit_i18n.py              # coverage for every language
    python tools/audit_i18n.py --missing    # and the untranslated strings
    python tools/audit_i18n.py --missing --top 40
"""
import argparse
import os
import re
import sys

# The console here is cp1252 and several of these strings are not. Printing
# them must not be the thing that fails the audit.
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LANG_DIR = os.path.join(ROOT, "data", "lang")

# The helpers that translate their text argument. Anything passed to one of
# these is a candidate; anything else never reaches i18n at all.
DRAW_HELPERS = [
    # The raw helpers, which call i18n::translate() themselves.
    "drawTextSafe", "drawCenteredTextSafe", "drawCenteredText",
    "drawCenteredTextUnclipped", "drawText", "drawTextRaw",
    # And the wrappers that carry a label to one of them. Counting only the
    # raw helpers undercounted badly: almost every settings label reaches the
    # screen through settingsRow or drawActionBtn, never through drawTextSafe
    # directly, so the first version of this reported 203 drawn strings while
    # matching only 15 of the 72 catalogue keys -- the other 57 keys were
    # strings it could not see.
    "drawCard", "drawSettingsCard", "settingsRow", "drawActionBtn",
    "drawUIDropdown", "drawUIValueControl", "drawPill", "drawPillToggle",
    "drawSettingsToggle", "drawSettingsStepper", "drawSectionFrame",
]

SOURCE_DIRS = [os.path.join(ROOT, "native")]

# A literal is prose worth translating when it is not one of these.
TOKENISH = re.compile(r"^[a-z0-9_.:/\\-]+$")          # ids, paths, tokens
FORMATTY = re.compile(r"%[-0-9.]*[sdfxu]|\{\}")        # format strings

# NOT PROSE, and translating these would be actively wrong rather than merely
# wasted. A unit is the same unit in every language an operator works in, and
# the fragments are half of a string assembled at runtime -- "A: ", "W:",
# " ms" -- whose whole sentence never exists as a literal to translate.
#
# The first version of this counted all of them and reported 430 strings to
# translate, which made the coverage figure look worse than it is and would
# have sent a translator at "dB".
UNITS = {
    "db", "hz", "khz", "ms", "px", "ch", "fps", "smp", "bpm",
    "in", "out", "vu", "go", "hex", "tap", "arm", "len", "yes", "no",
    "s", "lufs", "kbps", "mbps", "srt", "rtmp", "rtsp", "udp",
}
# A number and its unit ("15 s", "48 kHz"): the same in every language.
NUMBER_UNIT = re.compile(r"^\s*\d+(\.\d+)?\s*[A-Za-z]{1,4}\s*$")
NOT_PROSE = re.compile(r"^[\s\W\d]*$")     # punctuation, spacing, digits only


def is_prose(s):
    """Is this a phrase a translator should be given?"""
    t = s.strip()
    if len(t) < 3:
        return False                       # "GO", "VU", "dB": glyphs, not prose
    if NOT_PROSE.match(t):
        return False
    if t.lower().strip(":. ") in UNITS or NUMBER_UNIT.match(t):
        return False
    # A fragment ending or starting with a colon is half of a runtime
    # concatenation, so there is no whole sentence here to translate.
    if t.endswith(":") or t.startswith(":"):
        return False
    return True


def drawn_strings():
    """Every literal passed as text to a drawing helper."""
    call = re.compile(
        r"\b(" + "|".join(DRAW_HELPERS) + r")\s*\(", re.MULTILINE)
    literal = re.compile(r'"((?:[^"\\]|\\.)*)"')
    found = {}
    for root in SOURCE_DIRS:
        for dirpath, _dirs, files in os.walk(root):
            if "extras" in dirpath.replace("\\", "/").split("/"):
                continue          # vendored upstream, not our interface
            for name in files:
                if not name.endswith((".cpp", ".hpp", ".ipp")):
                    continue
                path = os.path.join(dirpath, name)
                text = open(path, encoding="utf-8", errors="replace").read()
                for m in call.finditer(text):
                    # Take the call's argument list by matching brackets, so a
                    # nested call's literals are still seen but a following
                    # statement's are not.
                    depth = 0
                    i = m.end() - 1
                    while i < len(text):
                        if text[i] == "(":
                            depth += 1
                        elif text[i] == ")":
                            depth -= 1
                            if depth == 0:
                                break
                        i += 1
                    args = text[m.end():i]
                    for lit in literal.finditer(args):
                        s = lit.group(1)
                        if not s or len(s) < 2:
                            continue
                        if TOKENISH.match(s) or FORMATTY.search(s):
                            continue
                        if not is_prose(s):
                            continue
                        if s.startswith("\\"):
                            continue
                        rel = os.path.relpath(path, ROOT).replace("\\", "/")
                        found.setdefault(s, rel)
    return found


# THE MENUS. A dropdown, a picker or a SOURCE menu is built as a list of
# (id, label) pairs and drawn later, row by row, through drawTextSafe -- so
# its labels ARE translated at draw time, but none of them is a literal inside
# a draw call, and this audit never saw them. That is how every submenu (the
# test pattern picker, the SOURCE menu, the stream-deck button menu) stayed
# English in every language without the coverage figure moving: a translator
# working from --missing was never shown a single one of them.
#
# A pair is an id literal (lower case, a token) followed by a label literal,
# as `emplace_back("smpte-bars", "SMPTE Colour Bars")` or
# `{"tracker", "Tracker"}`.
OPTION_PAIRS = (
    re.compile(r'(?:emplace_back|push_back)\(\s*\{?\s*"[a-z0-9_.:/][a-z0-9_.:/\-]*"\s*,\s*"((?:[^"\\]|\\.)*)"'),
    re.compile(r'\{\s*"[a-z0-9_.:/][a-z0-9_.:/\-]*"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}'),
)


# Pairs that are not menu rows: a glyph set written as byte escapes, an
# expression for the code editor, an ffmpeg flag or filter. And main.cpp's
# command-line options, whose help goes to the console and is never drawn:
# their id is the flag itself, and OPTION_PAIRS never takes an id starting "-".
def not_a_label(s):
    return ("\\x" in s or s.startswith("+") or "=" in s or "*" in s or "^" in s)


def option_labels():
    """Every label of an (id, label) pair: what menus and pickers draw."""
    found = {}
    for root in SOURCE_DIRS:
        for dirpath, _dirs, files in os.walk(root):
            if "extras" in dirpath.replace("\\", "/").split("/"):
                continue
            for name in files:
                if not name.endswith((".cpp", ".hpp", ".ipp")):
                    continue
                path = os.path.join(dirpath, name)
                text = open(path, encoding="utf-8", errors="replace").read()
                rel = os.path.relpath(path, ROOT).replace("\\", "/")
                for pattern in OPTION_PAIRS:
                    for m in pattern.finditer(text):
                        s = m.group(1)
                        if TOKENISH.match(s) or FORMATTY.search(s) or not is_prose(s):
                            continue
                        if not_a_label(s):
                            continue
                        found.setdefault(s, rel)
    return found


# What translate() in i18n.cpp will actually find: the string as written, the
# same words in another case, or -- for "Name (qualifier)" -- each half on its
# own. Counting only exact keys reported strings as missing that the app draws
# translated.
def covered(s, keys, lowered):
    if s in keys or s.lower() in lowered:
        return True
    m = re.match(r"^(.+) \(([^()]+)\)$", s)
    if m:
        base, qualifier = m.group(1), m.group(2)
        return covered(base, keys, lowered) or qualifier.lower() in lowered
    return False


def catalogue_keys(path):
    """The keys this catalogue actually TRANSLATES.

    A key with an empty value is not one of them. i18n.cpp skips those when it
    loads the file -- deliberately, so an unfinished line reads as English
    rather than as an empty button -- and counting them here credited every
    catalogue for work nobody had done. Twenty to forty lines per language were
    being reported as translated while the app drew the English.

    A string the trade leaves in English in every language (TAKE, PROGRAM, NDI)
    is a real decision and should count, so it is written with the English AS
    its value rather than left blank. That is what --allow-same in
    add_translations.py is for: stated once, on purpose, instead of looking
    identical to an unfinished line.
    """
    keys = set()
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.rstrip("\r\n")
        if not line or line.startswith("#"):
            continue
        if "\t" not in line:
            continue
        key, _, value = line.partition("\t")
        if not value.strip():
            continue
        keys.add(key)
    return keys


# ── ONE WORD, TWO ALPHABETS ──────────────────────────────────────────────────
#
# Latin a, Cyrillic а and Greek α are three different characters that draw as
# the same mark. Put one in the middle of a Latin word and the word is still
# legible on screen, still passes every eye and every diff, and no longer
# matches anything. In a value that is a misspelling nobody can see; in a KEY
# the lookup silently misses and that string stays English forever.
#
# It is not enough to forbid a catalogue from mixing scripts. "NDI KEY: ВКЛ."
# is Cyrillic and Latin on one line and entirely correct, because the trade's
# own vocabulary stays in English in every language. The fault is narrower:
# a single WORD built out of two alphabets. No language does that on purpose,
# so every instance is a typo or a bad paste.
#
# Written because exactly that happened -- four Cyrillic letters reached a
# Latin-script catalogue through a hand-written batch, and nothing caught them
# until the bytes were counted.
SCRIPT_RANGES = (
    ("Latin",    ((0x0041, 0x005A), (0x0061, 0x007A), (0x00C0, 0x024F))),
    ("Greek",    ((0x0370, 0x03FF), (0x1F00, 0x1FFF))),
    ("Cyrillic", ((0x0400, 0x052F),)),
)

# Characters that belong to a word without belonging to an alphabet, so they
# neither start a word nor end one: digits, the two apostrophes, the hyphen
# used inside a compound.
WORD_GLUE = "0123456789-_'’"


def script_of(ch):
    cp = ord(ch)
    for name, ranges in SCRIPT_RANGES:
        for lo, hi in ranges:
            if lo <= cp <= hi:
                return name
    return None


def mixed_script_words(s):
    """The words of s built from more than one alphabet, as (word, scripts)."""
    out = []
    word = ""
    scripts = set()
    for ch in s + " ":
        script = script_of(ch)
        if script is not None:
            word += ch
            scripts.add(script)
            continue
        if ch in WORD_GLUE and word:
            word += ch
            continue
        if len(scripts) > 1:
            out.append((word, sorted(scripts)))
        word = ""
        scripts = set()
    return out


def confusable_report():
    """Every mixed-script word in every catalogue: (code, line, field, word, scripts)."""
    bad = []
    for name in sorted(f for f in os.listdir(LANG_DIR) if f.endswith(".tsv")):
        path = os.path.join(LANG_DIR, name)
        with open(path, encoding="utf-8", errors="replace") as handle:
            for number, line in enumerate(handle, 1):
                line = line.rstrip("\r\n")
                if not line or line.startswith("#") or "\t" not in line:
                    continue
                key, _, value = line.partition("\t")
                for field, text in (("key", key), ("value", value)):
                    for word, scripts in mixed_script_words(text):
                        bad.append((name[:-4], number, field, word, scripts))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--missing", action="store_true",
                    help="list the drawn strings no catalogue translates")
    ap.add_argument("--top", type=int, default=25)
    args = ap.parse_args()

    drawn = drawn_strings()
    if not drawn:
        sys.exit("audit_i18n: found no drawn strings -- did the helpers change name?")
    options = option_labels()
    for s, where in options.items():
        drawn.setdefault(s, where)

    langs = sorted(f for f in os.listdir(LANG_DIR) if f.endswith(".tsv"))
    print("drawn strings that could be translated: %d (%d of them menu and picker labels)"
          % (len(drawn), len(options)))
    print()
    print("  %-12s %6s  %s" % ("language", "keys", "coverage of drawn strings"))
    worst = None
    for f in langs:
        code = f[:-4]
        keys = catalogue_keys(os.path.join(LANG_DIR, f))
        lowered = {k.lower() for k in keys}
        hit = sum(1 for s in drawn if covered(s, keys, lowered))
        pct = (hit * 100.0 / len(drawn)) if drawn else 0.0
        print("  %-12s %6d  %5.1f%%  (%d of %d)" % (code, len(keys), pct, hit, len(drawn)))
        if worst is None:
            worst = pct

    if args.missing:
        # Anything no catalogue has. Sorted shortest first: short strings are
        # usually labels and buttons, which is where translating pays best.
        every = set()
        for f in langs:
            every |= catalogue_keys(os.path.join(LANG_DIR, f))
        every_lower = {k.lower() for k in every}
        missing = sorted((s for s in drawn if not covered(s, every, every_lower)),
                         key=lambda s: (len(s), s))
        print()
        print("untranslated drawn strings: %d" % len(missing))
        for s in missing[:args.top]:
            print("  %-46s %s" % (('"' + s + '"')[:46], drawn[s]))
        if len(missing) > args.top:
            print("  ... and %d more" % (len(missing) - args.top))
    # A word spelled out of two alphabets is always a mistake, and it is
    # the one mistake in a catalogue that nobody can see.
    confusables = confusable_report()
    if confusables:
        print()
        print("MIXED-SCRIPT WORDS: %d" % len(confusables))
        for code, number, field, word, scripts in confusables[:20]:
            print("  %-10s line %-5d %-5s %-24s %s"
                  % (code, number, field, word[:24], "+".join(scripts)))
        if len(confusables) > 20:
            print("  ... and %d more" % (len(confusables) - 20))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
