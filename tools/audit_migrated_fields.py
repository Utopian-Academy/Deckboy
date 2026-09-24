#!/usr/bin/env python3
"""A field that MOVED must not still be read where it used to live.

WHY THIS EXISTS. In 0.99.374 warp and edge blend moved from Deck to
OutputTarget, because they correct for the screen a picture lands on and one
deck can feed a warped projector and a clean stream at once. Every setter moved.
The compositor moved. The EDITOR did not:

  * dragging a warp corner wrote Deck::warpTopLeftX, which nothing renders
  * RESET zeroed the deck's copy and left the real warp untouched
  * SAVE PRESET stored zeroes; RECALL wrote them back
  * copy/paste copied the deck's dead fields between decks
  * the arm button asked the deck whether warp was on, so it always said no

Nothing warned and nothing failed to compile, because both structs have
identically named fields and both are perfectly valid C++. The whole editor
worked, on the wrong object, silently -- which is the exact fault the migration
was meant to end.

HOW IT LOOKS, and why the obvious version does not work. The first draft of
this file matched `focusedDeck().warpTopLeftX` and reported zero sites -- zero
even in the files where the migration code legitimately lives, which is the
tell that a detector is not detecting. Almost nothing writes it that way. The
real code binds a reference first:

    Deck& wd = focusedDeckMutable();
    wd.warpTopLeftX = ...;             <-- this is the bug, and it was missed

So this tracks the VARIABLES: every name declared as a Deck (reference,
const reference, value or parameter) in a file, and then every use of a migrated
field on one of those names. Plus the direct accessor form.

It SELF-TESTS on a sample containing both shapes, so a clean run means the
detector ran rather than that it matched nothing. `--self-test` shows it.

Add a MIGRATION entry whenever a field moves between structs. It costs one line
and it is the only thing standing between a migration and a working UI that
edits nothing.
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MIGRATIONS = [
    {
        "what": "warp and edge blend: Deck -> OutputTarget (v0.99.374)",
        "old_type": "Deck",
        "accessors": ["focusedDeck", "focusedDeckMutable"],
        "fields": [
            "warpEnabled", "warpMode",
            "warpTopLeftX", "warpTopLeftY", "warpTopRightX", "warpTopRightY",
            "warpBottomRightX", "warpBottomRightY",
            "warpBottomLeftX", "warpBottomLeftY",
            "edgeBlendLeft", "edgeBlendRight", "edgeBlendTop", "edgeBlendBottom",
        ],
        "allowed": {
            "native/core/project_file.ipp":
                "migrates the value out of the deck record on load",
            "native/app/app_smoke.ipp":
                "asserts the migration, including that the deck is now clear",
            "native/core/types.hpp":
                "declares the fields that are kept for migration",
            "native/main.cpp":
                "normalizeDeck clamps them, and normalizeProjectOutputsAndLayers "
                "is the migration itself: it reads the deck's values, copies them "
                "onto the output and then clears them",
        },
    },
]

SEARCH_DIRS = ["native"]
SEARCH_EXT = (".cpp", ".hpp", ".ipp")


def scan(text, migration):
    """Every (line, snippet) where a migrated field is used on the OLD type.

    Resolved per use, not per file. A name like `bd` or `deck` is bound to a
    Deck in one function and an OutputTarget in the next, so collecting every
    Deck-typed name in a file and then flagging every use of that name reports
    the whole edge-blend UI as broken when it is correct. For each use we take
    the NEAREST PRECEDING declaration of that name and believe it.
    """
    fields = "|".join(re.escape(f) for f in migration["fields"])
    old = migration["old_type"]

    # Every declaration in the file: (position, name, type).
    decls = []
    decl_re = re.compile(
        r"\b(?:const\s+)?([A-Za-z_]\w*)\s*[&*]?\s*([A-Za-z_]\w*)\s*(?:=|\)|,|;|\{)")
    for m in decl_re.finditer(text):
        decls.append((m.start(), m.group(2), m.group(1)))
    for m in re.finditer(r"std::optional\s*<\s*([A-Za-z_]\w*)\s*>\s*([A-Za-z_]\w*)", text):
        decls.append((m.start(), m.group(2), m.group(1)))
    decls.sort()

    def type_of(name, at):
        best = None
        for pos, n, t in decls:
            if pos > at:
                break
            if n == name:
                best = t
        return best

    hits = []
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*(?:\.|->)\s*(%s)\b" % fields, text):
        if type_of(m.group(1), m.start()) == old:
            hits.append(m)
    acc = "|".join(re.escape(a) for a in migration["accessors"])
    for m in re.finditer(r"\b(?:%s)\s*\(\s*\)\s*\.\s*(%s)\b" % (acc, fields), text):
        hits.append(m)

    out = []
    seen = set()
    for m in sorted(hits, key=lambda h: h.start()):
        line = text[:m.start()].count(chr(10)) + 1
        if (line, m.group(0)) in seen:
            continue
        seen.add((line, m.group(0)))
        snippet = " ".join(text[max(0, m.start() - 30):m.end() + 20].split())
        out.append((line, snippet))
    return out


SELF_TEST = """
void editor() {
  Deck& wd = focusedDeckMutable();
  wd.warpTopLeftX = 0.0f;                 // caught: bound reference
  const Deck& r = focusedDeck();
  float y = r.warpBottomLeftY;            // caught: const reference
  if (focusedDeck().warpEnabled) { }      // caught: direct accessor
  OutputTarget& out = focusedOutputMutable();
  out.warpTopLeftX = 1.0f;                // NOT caught: the new home
  someCue.warpMode = "linear";            // NOT caught: unrelated object
}
"""


def self_test():
    found = scan(SELF_TEST, MIGRATIONS[0])
    lines = [ln for ln, _ in found]
    want = 3
    print("self-test: %d of %d shapes caught  (lines %s)"
          % (len(found), want, ", ".join(str(n) for n in lines)))
    for ln, snip in found:
        print("    %d: %s" % (ln, snip))
    ok = len(found) == want
    print("self-test: %s" % ("ok" if ok else "BROKEN -- this audit proves nothing"))
    return ok


def source_files():
    for base in SEARCH_DIRS:
        for dirpath, _dirs, names in os.walk(os.path.join(ROOT, base)):
            if "extras" + os.sep + "upstream" in dirpath:
                continue
            for name in names:
                if name.endswith(SEARCH_EXT):
                    yield os.path.join(dirpath, name)


def main():
    if "--self-test" in sys.argv:
        return 0 if self_test() else 1

    print("audit: fields that moved are not read where they used to live")
    print()
    if not self_test():
        return 1
    print()

    findings = []
    for migration in MIGRATIONS:
        total = 0
        for path in source_files():
            rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
            text = io.open(path, encoding="utf-8", errors="replace").read()
            hits = scan(text, migration)
            total += len(hits)
            if rel in migration["allowed"]:
                continue
            for line, snippet in hits:
                findings.append("%s:%d  %s" % (rel, line, snippet))
        print("  %s" % migration["what"])
        print("    fields watched: %d, sites found: %d"
              % (len(migration["fields"]), total))
        for rel, why in sorted(migration["allowed"].items()):
            print("      allowed: %-32s %s" % (rel, why))
    print()
    for f in findings:
        print("  FAIL " + f)
    if findings:
        print()
        print("  These read the struct the value moved OFF. It still compiles")
        print("  and it still runs; it edits a field nothing renders. Point it")
        print("  at the new home, or add the file to `allowed` with a reason.")
    print()
    print("clean" if not findings
          else "%d finding%s" % (len(findings), "" if len(findings) == 1 else "s"))
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
