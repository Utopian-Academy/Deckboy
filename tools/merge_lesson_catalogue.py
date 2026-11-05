#!/usr/bin/env python3
"""Put a translation of the tutorials into a language's catalogue.

The translation is a file of `english<TAB>translation` pairs, one per line,
the English exactly as `tools/lesson_strings.py` lists it. A pair whose
English is not on that list is refused (the lessons moved on: translate the
new line), a line holding a stray tab is refused, and a line of the list with
no pair is reported, because it will show in English. The catalogue's
tutorial section is replaced whole, between its markers, so running it again
is safe.

    py -3 tools/merge_lesson_catalogue.py de path/to/de-pairs.tsv
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BEGIN = "# --- tutorials: tools/lesson_strings.py (a whole line each) ---"
END = "# --- end of tutorials ---"


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    code, source = sys.argv[1], Path(sys.argv[2])
    english = subprocess.run([sys.executable, str(ROOT / "tools/lesson_strings.py")], capture_output=True,
                             text=True, encoding="utf-8", check=True).stdout.splitlines()
    known = set(english)
    pairs = {}
    for number, raw in enumerate(source.read_text(encoding="utf-8").splitlines(), 1):
        if not raw.strip():
            continue
        parts = raw.split("\t")
        if len(parts) != 2 or not parts[1].strip():
            print("line %d: expected english<TAB>translation: %r" % (number, raw[:70]))
            return 1
        if parts[0] not in known:
            print("line %d: not a tutorial line (any more?): %r" % (number, parts[0][:70]))
            return 1
        pairs[parts[0]] = parts[1].strip()
    missing = [line for line in english if line not in pairs]
    catalogue = ROOT / "data/lang" / (code + ".tsv")
    text = catalogue.read_text(encoding="utf-8")
    if BEGIN in text:
        text = text[:text.index(BEGIN)].rstrip("\n") + "\n"
    else:
        text = text.rstrip("\n") + "\n"
    lines = [BEGIN]
    for line in english:
        if line in pairs and pairs[line] != line:   # a line the same in both says nothing
            lines.append(line + "\t" + pairs[line])
    lines.append(END)
    catalogue.write_text(text + "\n" + "\n".join(lines) + "\n", encoding="utf-8")
    print("%s: %d tutorial lines translated; %d with no pair (shown in English)" % (code, len(lines) - 2,
                                                                                   len(missing)))
    for line in missing:
        print("  untranslated: " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
