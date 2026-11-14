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
    # The cue inspector draws almost every label through these. Not counting
    # them reported 55% coverage while most inspector rows were English.
    "drawQuickRow", "inspDrawQuickRow", "inspDrawValueRow", "drawChoiceRow", "inspDrawChoiceRow",
    "drawInspectorMessageRow", "inspDrawMessageRow", "drawInspectorEditableRow", "drawTCtrl",
    # Toasts are drawn through drawTextSafe too, and were never counted: they
    # were most of what a run of the app found untranslated (2026-11-08).
    # failRemoteCommand toasts its reason as well. And a template handed to
    # i18n::format or i18n::translate is a line a catalogue must hold.
    "triggerToast", "failRemoteCommand", r"i18n::format", r"i18n::translate",
    # The inline editor draws its title and prompt through drawText; its literals
    # (key colour, offset, width...) were never counted either.
    r"openInline\w*Editor",
]

SOURCE_DIRS = [os.path.join(ROOT, "native")]

# A literal is prose worth translating when it is not one of these.
TOKENISH = re.compile(r"^[a-z0-9_.:/\\-]+$|^[a-z]+(\.[A-Za-z0-9_]+)+$")   # ids, paths, tokens, cue.effect.paramA
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
    # Space at either end, or an operator: the other half of a sentence the
    # code builds (" cue", "A =", "added "). It never reaches translate() on
    # its own; the whole does, and translate() takes that apart itself.
    if (s != s.strip() or t.endswith("=") or t.startswith(("=", ")"))
            or t.endswith(("(", "'", "\""))):
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
                if name == "app_smoke.ipp":
                    continue      # the self-checks' INPUTS, not interface text: translating
                                  # "spare (also spare)" broke the check that it stays as written
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
                    # "one " "sentence" across lines is ONE string to C++,
                    # and the catalogue key is the whole of it. Counted in
                    # pieces, every wrapped tip read as two or three missing
                    # strings that no catalogue could ever hold.
                    args = re.sub(r'"\s+"', "", args)
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
    # A prompt drawn with the editor's suffix glued on is looked up as the bare hint.
    return {bare_hint(k): v for k, v in found.items()}


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
                if name == "app_smoke.ipp":
                    continue      # the self-checks' INPUTS, not interface text: translating
                                  # "spare (also spare)" broke the check that it stays as written
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


# STRINGS THAT TRAVEL IN A VARIABLE. drawn_strings() only sees a literal written
# inside a draw call, and option_labels() only an (id, label) pair. The effect
# inspector draws `cueEffectParamLabelFor(fx, which)`, a tip from
# `cueEffectChoiceTip`, an import failure read out of `result.error`: the
# literal sits in a table or a function far from the draw, so both audits were
# blind to it and about 900 interface strings were in no catalogue at all
# (2026-11-13: "channel spread", "tape age", the Keynote message). translate()
# looks every one of them up at draw time, so each needs a line like any other.
#
# Three static scans, no compiling:
#   * a function named like a label table (xxxLabel, xxxTip, xxxName, xxxOptions
#     ...): the literals in its `return` statements. A `return "a" + b` is
#     skipped, because the whole never exists as a literal.
#   * an `error = "..."` or `why = ...` assignment in the files whose text reaches
#     a toast (pdf_import, the app's .ipp files, a few platform sources).
#   * NumericParamSpec{token, label, hint, ...} rows: the label, and the hint
#     with the suffix the editor always appends to it.
LABEL_FN_NAME = re.compile(r"(Label|Tip|Name|Reason|Options|Choices|Title|Hint|Help)(For)?$")
# Functions the name test catches that do not make interface text: wire
# formats, hardware names, file stems, defaults that take a number after them.
NOT_A_LABEL_FN = {
    "textureFormatName", "hwDeviceName", "hardwareDecodeName", "st2110SamplingLabel",
    "className", "dxgiPresentationModeName", "deckLinkModeLabel", "exeName", "getName",
    "deckDefaultName", "outputDefaultName", "escapeFixtureName", "unescapeFixtureName",
    "sanitizeBundleFilenameStem", "hostnameSafe", "colourNameForms", "normaliseColourName",
    "defaultNdiSourceName", "defaultNdiKeySourceName", "defaultOutputNdiSourceName",
    "defaultOutputNdiKeySourceName", "compositeSlotDefaultName", "browserCueNameForUrl",
    "macDefaultPlaybackDeviceName", "cleanAudioOutputName", "getDeckLinkModelName",
    "getDeckLinkDisplayName", "deckboyGetDisplayName", "cueTargetVerbToken", "playlistTimebaseLabel",
    "extractEmbeddedSubtitleSrt", "describeMessage", "describeOutMessage", "refUnsupportedReason",
    "sourceCueRefFriendlyLabel", "deckLinkCueDeviceLabel",
}
FUNCTION_HEAD = re.compile(
    r"^[ \t]*(?:(?:inline|static|constexpr)\s+)*"
    r"(?:const\s+char\s*\*|std::string|std::vector<const\s+char\s*\*>|std::vector<std::string>)"
    r"\s+(?:\w+::)*(\w+)\s*\([^)]*\)\s*(?:const)?\s*\{", re.MULTILINE)
RETURN_STATEMENT = re.compile(r"\breturn\b([^;]*);")
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
# Files whose error text is shown to the operator. The engine's own decoder
# and driver errors (snappy, ASIOInit) are log lines, not interface.
ERROR_FILES = {"pdf_import.cpp", "midi.cpp", "masks.hpp", "live_captions.hpp", "screen_colour.cpp",
               "code_source.hpp", "ndi_input.cpp", "asio_audio.cpp", "spout_input.cpp"}
ERROR_ASSIGNMENT = re.compile(
    r"\b(\w*(?:[eE]rror|[rR]eason|why|[aA]dvice|errorMessage|loadError))\s*(?:=|\+=)\s*([^;]*);"
    r"|\bconst\s+char\s*\*\s*(k[A-Z]\w+)\s*=\s*([^;]*);")
SPEC_ROW = re.compile(
    r'NumericParamSpec\s+\w+\s*\{\s*"[^"]*"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*((?:"(?:[^"\\]|\\.)*"\s*)+)')
# What openInlineNumericExpressionEditor appends to a spec's hint. The editor
# translates the hint on its own and fills it into one template (app_ui_widgets
# .ipp), so the catalogues need the bare hint and the template, never the two
# glued together.
HINT_SUFFIX = " (supports + - * / and ())"
# Templates the app fills with a translated piece (i18n::format), and words it
# translates on their own before building a line. Each is one catalogue line.
TEMPLATES = ("{1} (supports + - * / and ())", "added {1}", "(bypassed)")
# key as translate() sees it -> the literal in the source, for --staged.
JOINED = {}


def bare_hint(s):
    """A prompt with the editor's suffix glued on, as the hint alone."""
    return s[:-len(HINT_SUFFIX)] if s.endswith(HINT_SUFFIX) and len(s) > len(HINT_SUFFIX) else s


def strip_comments(text):
    """Blank out // and /* */ comments but leave string literals alone."""
    return re.sub(r'"(?:[^"\\]|\\.)*"|//[^\n]*|/\*.*?\*/',
                  lambda m: m.group(0) if m.group(0).startswith('"') else " ", text, flags=re.S)


def block_end(text, start):
    """Index just past the brace block whose first line begins at `start`."""
    depth = 1
    i = start
    while i < len(text) and depth:
        c = text[i]
        if c == '"':
            i += 1
            while i < len(text) and text[i] != '"':
                i += 2 if text[i] == "\\" else 1
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    return i


def label_ok(s):
    return (is_prose(s) and not FORMATTY.search(s) and not not_a_label(s)
            and not s.startswith(("\\", "'", "-", "+")) and re.search(r"[A-Za-z]", s) is not None)


def source_files():
    for root in SOURCE_DIRS:
        for dirpath, _dirs, files in os.walk(root):
            if "extras" in dirpath.replace("\\", "/").split("/"):
                continue
            for name in files:
                if name.endswith((".cpp", ".hpp", ".ipp")) and name != "app_smoke.ipp":
                    path = os.path.join(dirpath, name)
                    text = open(path, encoding="utf-8", errors="replace").read()
                    yield name, os.path.relpath(path, ROOT).replace("\\", "/"), strip_comments(text)


def table_strings():
    """Literals returned by label tables: {string: 'file:function'}."""
    found = {}
    for _name, rel, text in source_files():
        for head in FUNCTION_HEAD.finditer(text):
            fn = head.group(1)
            if not LABEL_FN_NAME.search(fn) or fn in NOT_A_LABEL_FN:
                continue
            body = text[head.end():block_end(text, head.end())]
            for ret in RETURN_STATEMENT.finditer(body):
                stmt = re.sub(r'"\s+"', "", ret.group(1))
                if "+" in LITERAL.sub("", stmt):
                    continue          # "added " + name: no whole string to look up
                for lit in LITERAL.finditer(stmt):
                    if label_ok(lit.group(1)):
                        found.setdefault(lit.group(1), rel + ":" + fn)
    return found


def error_strings():
    """Literals of error/reason assignments in the files that toast them."""
    found = {}
    for name, rel, text in source_files():
        if name not in ERROR_FILES and not (rel.startswith("native/app/") or rel == "native/main.cpp"):
            continue
        for m in ERROR_ASSIGNMENT.finditer(text):
            who = m.group(1) or m.group(3)
            stmt = re.sub(r'"\s+"', "", m.group(2) if m.group(1) else m.group(4))
            for lit in LITERAL.finditer(stmt):
                s = lit.group(1)
                if len(s) < 4 or not label_ok(s):
                    continue
                if " " not in s and not s[0].islower():
                    continue          # "Keynote", "PowerPoint": a product name
                found.setdefault(s, rel + ":" + who)
    return found


def spec_strings():
    """NumericParamSpec labels and hints, and the editor's prompt template."""
    found = {}
    for _name, rel, text in source_files():
        for m in SPEC_ROW.finditer(text):
            label = m.group(1)
            hint = "".join(LITERAL.findall(m.group(2)))
            if label_ok(label):
                found.setdefault(label, rel + ":NumericParamSpec")
            if hint and label_ok(bare_hint(hint)):
                found.setdefault(bare_hint(hint), rel + ":NumericParamSpec")
    for t in TEMPLATES:
        found.setdefault(t, "i18n::format template")
    return found


def variable_strings():
    """Every string that reaches the screen through a variable, not a draw call."""
    found = {}
    for part in (table_strings(), error_strings(), spec_strings()):
        for s, where in part.items():
            found.setdefault(s, where)
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
# neither start a word nor end one: digits and the two apostrophes. NOT the
# hyphen: Russian, Ukrainian and Greek join a Latin acronym to a native word
# with one ("MIDI-нота", "VU-метр"), which is correct spelling. A homoglyph
# slip lands inside one half, and each half is still checked on its own.
WORD_GLUE = "0123456789_'’"


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


# ── THE GATE ─────────────────────────────────────────────────────────────────
#
# New interface text is translated by tools/i18n_translate.py, which the
# pre-commit hook runs on what a commit adds. A commit that did not pass through
# the hook (another machine, the web editor, a hook that could not reach the
# translator) is caught here instead: any drawn string a full catalogue cannot
# show, that is not kept English on purpose and was not already missing when the
# baseline was written, fails the build. The baseline only ever shrinks.
BASELINE = os.path.join(HERE, "i18n_baseline.txt")


def gate(write):
    sys.path.insert(0, HERE)
    import argparse as _argparse
    import i18n_translate as tr                 # the same reading of "missing"
    keys = tr.candidate_keys(_argparse.Namespace(keys=None, misses=None))
    keep = tr.load_keep()
    gaps = set()
    for code in tr.FULL:
        for k in tr.missing_for(code, keys, keep):
            gaps.add(code + "\t" + k)
    if write:
        with open(BASELINE, "w", encoding="utf-8", newline="\n") as f:
            f.write("# Untranslated when the gate began. tools/audit_i18n.py --gate allows these\n"
                    "# and nothing new. Run tools/i18n_translate.py, then --write-baseline.\n")
            f.write("\n".join(sorted(gaps)) + "\n")
        print("audit_i18n: baseline written, %d gaps" % len(gaps))
        return 0
    allowed = set()
    if os.path.exists(BASELINE):
        for line in open(BASELINE, encoding="utf-8"):
            line = line.rstrip("\n")
            if line and not line.startswith("#"):
                allowed.add(line)
    new = sorted(gaps - allowed)
    if not new:
        print("audit_i18n --gate: no new untranslated strings (%d older gaps in the baseline)"
              % len(gaps & allowed))
        return 0
    strings = sorted({g.split("\t", 1)[1] for g in new})
    print("audit_i18n --gate: %d new strings are not translated in every full language:" % len(strings))
    for s in strings[:40]:
        langs = [g.split("\t", 1)[0] for g in new if g.split("\t", 1)[1] == s]
        print("  %-50s missing in %d" % (('"' + s + '"')[:50], len(langs)))
    print("Translate them: python tools/i18n_translate.py  (or add a stays-English line to"
          " tools/i18n_keep_english.txt)")
    return 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--missing", action="store_true",
                    help="list the drawn strings no catalogue translates")
    ap.add_argument("--top", type=int, default=25)
    ap.add_argument("--gate", action="store_true",
                    help="fail on a drawn string a full catalogue lacks that is not in the baseline")
    ap.add_argument("--write-baseline", action="store_true",
                    help="record today's gaps as the baseline the gate allows")
    args = ap.parse_args()
    if args.gate or args.write_baseline:
        return gate(args.write_baseline)

    drawn = drawn_strings()
    if not drawn:
        sys.exit("audit_i18n: found no drawn strings -- did the helpers change name?")
    options = option_labels()
    for s, where in options.items():
        drawn.setdefault(s, where)
    for s, where in variable_strings().items():
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
