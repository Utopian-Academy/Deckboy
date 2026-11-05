#!/usr/bin/env python3
"""Every line the tutorials say, for the translation catalogues.

The basics tour (app_tutorial.ipp), part two's lessons and the lesson list
(app_lessons.ipp) and Bug Swat (app_bugswat.ipp) are translated a whole line
at a time (lessonText), so a catalogue entry has to be the exact English line.
This lists them, so a catalogue can be written from the list and checked
against it as the lessons change.

    py -3 tools/lesson_strings.py                 # every line, one per row
    py -3 tools/lesson_strings.py --check de      # what data/lang/de.tsv lacks
    py -3 tools/lesson_strings.py --summary       # coverage of every catalogue

Names stay in English (DR. PIXEL, PROF. VIDEO, the guests), as do the trade's
words the catalogues already leave alone (TAKE, NDI...).
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
NEWLINE = "\n"
ESCAPED_NEWLINE = "\\" + "n"


def unescape(text):
    return text.replace('\\"', '"').replace("\\'", "'").replace("\\\\", "\\")


def strip_comments(source):
    """Code only: quotes in comments (James's words, kept as history) are not
    things the desk says. Then adjacent literals are joined, as the compiler
    joins them, so a line written across two source lines is one line here."""
    source = re.sub(r"//[^" + NEWLINE + r"]*", "", source)
    return re.sub(r'"\s*' + NEWLINE + r'\s*"', "", source)


def said(text):
    """Not a line the desk says: a remote verb's reply, a fragment joined to
    a number, or text with a line break in it (a cue's body, not a line)."""
    if text != text.strip() or ESCAPED_NEWLINE in text or NEWLINE in text:
        return False
    if re.match(r"^(BUGSWAT|LESSONS|TUTORIAL)( [A-Z]+)*:", text) or re.match(
            r"^(FX (ADD|LFO) |AUDIOFX ADD |MULTIVIEW (ON|WINDOW|LAYOUT)|SLICE |GOTO \d|PAUSE$)", text):
        return False
    # Pieces the tour joins into a sentence at run time (the screens' notes):
    # a whole-line lookup can never match them, so they are not offered.
    if text.startswith((". ", ", ")) or text in (
            "pen path", "left window", "middle window", "right window", "the other one", "the other one.",
            "A second screen, skapi", "Another screen, skapi", "The picture is on this one now.",
            "The picture will open on this one.", "Press Win+P and choose Extend",
            "In System Settings > Displays, use it as an extended display",
            "Set them to extend in your display settings"):
        return False
    return True


def prose(text):
    if len(text) < 6 or "%" in text or "::" in text:
        return False
    if any(c in text for c in "*=;{}") and not text.startswith(">") and not text.startswith("*"):
        return False
    return bool(re.search(r"[a-z]{2,}", text)) and " " in text


def title(text):
    return (bool(re.fullmatch(r"[A-Z0-9][A-Z0-9 '!?.,\-:]{2,}", text)) and " " in text) or text in (
        "PRESETS", "NDI", "BLACK", "PANIC", "INVERT", "FEATHER", "HOLD!", "TA-DA")


def collect():
    lines = []
    seen = set()

    def add(text):
        text = unescape(text)
        if text and said(text) and text not in seen:
            seen.add(text)
            lines.append(text)

    tutorial = strip_comments((ROOT / "native/app/app_tutorial.ipp").read_text(encoding="utf-8"))
    i = tutorial.index("kLines[kTutorialSteps] = {")
    j = tutorial.index("};", i)
    for m in LIT.finditer(tutorial[i:j]):
        if m.group(1) not in ("I", "ENTER", "B", "CTRL+S"):
            add(m.group(1))
    # And the rest of the tour's lines: praise, nudges, the screens' notes.
    for m in LIT.finditer(tutorial[:i] + tutorial[j:]):
        if prose(m.group(1)):
            add(m.group(1))

    lessons = strip_comments((ROOT / "native/app/app_lessons.ipp").read_text(encoding="utf-8"))
    # The guests' faces carry no words; their names stay English.
    faces_from = lessons.index("static const GuestFace kFaces[] = {")
    faces_to = lessons.index("if (!name) return nullptr;", faces_from)
    lessons = lessons[:faces_from] + lessons[faces_to:]
    names = set(re.findall(r'"([A-Z][A-Z .\'()]+)", "', lessons))
    # Every lesson's title, line and show, however short (a title can be one word).
    for m in re.finditer(r'l\.(?:title|about|show) = "((?:[^"\\]|\\.)+)"', lessons):
        add(m.group(1))
    for m in LIT.finditer(lessons):
        text = m.group(1)
        if text in names:
            continue
        if prose(text) or title(text):
            add(text)

    bugswat = strip_comments((ROOT / "native/app/app_bugswat.ipp").read_text(encoding="utf-8"))
    for m in LIT.finditer(bugswat):
        if prose(m.group(1)) or title(m.group(1)):
            add(m.group(1))
    return lines


def catalogue(code):
    path = ROOT / "data/lang" / (code + ".tsv")
    entries = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#") or "\t" not in line:
            continue
        english, translated = line.split("\t", 1)
        if translated.strip():
            entries[english] = translated
    return entries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", metavar="CODE")
    parser.add_argument("--summary", action="store_true")
    args = parser.parse_args()
    lines = collect()
    if args.check:
        have = catalogue(args.check)
        missing = [line for line in lines if line not in have]
        for line in missing:
            print(line)
        print("%s: %d of %d tutorial lines translated" % (args.check, len(lines) - len(missing), len(lines)),
              file=sys.stderr)
        return 0
    if args.summary:
        for path in sorted((ROOT / "data/lang").glob("*.tsv")):
            have = catalogue(path.stem)
            done = sum(1 for line in lines if line in have)
            print("%-8s %4d / %d" % (path.stem, done, len(lines)))
        return 0
    for line in lines:
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
