#!/usr/bin/env python3
"""Fail if anything published gives away a secret.

Deckboy hides a few things on purpose. They stop being worth hiding the moment
the changelog explains where they are -- and a changelog is not a private note,
it becomes the GitHub release body verbatim.

This has already happened twice. The Konami sequence for Terrarium was printed
in full in a release, key by key; and the mascot easter egg shipped in the
v0.99.335 notes complete with how to trigger it and what it does. Neither was
caught by a person reading the file, because by then it reads like any other
entry.

So it is checked by machine, over every file that is published: CHANGES.md,
README.md and anything under docs/. Source comments are deliberately NOT
checked -- they are where this knowledge belongs.

Run:  python tools/audit_spoilers.py
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Two tiers, because "published" is not one thing.
#
# EVERYTHING: any file whose text leaves the building. A leak here is a leak
# whenever it was written, so the changelog is in even though it is history.
PUBLISHED = [ROOT / "CHANGES.md", ROOT / "README.md", ROOT / "MANUAL.md"]
PUBLISHED += sorted(ROOT.glob("docs/*.md"))
# The Pages site itself -- the pages a search engine reads, and the ones that
# were not being checked at all.
PUBLISHED += sorted(ROOT.glob("docs/*.html"))

# THE FRONT DOOR: what a person reads to find out what Deckboy is. The rule is
# stricter here, because merely naming a hidden thing in current documentation
# is enough to give it away -- someone reading the manual is not doing
# archaeology, they are being told what the app has. The changelog and the
# internal engineering notes under docs/ are deliberately NOT in this list: a
# changelog records what happened, and the plan documents are excluded from the
# site by docs/_config.yml.
FRONT_DOOR = {ROOT / "README.md", ROOT / "MANUAL.md"}
FRONT_DOOR |= set(ROOT.glob("docs/*.html"))

# What must never appear anywhere. Each is a phrase that tells a reader HOW to
# reach something hidden, or that there is something hidden to reach.
FORBIDDEN = [
    (r"konami", "names the door to a hidden feature"),
    (r"[↑↓←→]{4}", "prints an arrow key sequence"),
    (r"easter\s*egg", "announces that something is hidden"),
    (r"poke the (face|mascot)", "explains how to trigger the mascot"),
    (r"mascot notices you", "gives away the mascot behaviour"),
    (r"grows wings|believe i can fly", "gives away the flying cue"),
    (r"secret (code|key|sequence|combination)", "points at a hidden input"),
]

# What must never appear in the front door, on top of the above. The manual
# listed Terrarium among the test patterns as "a hidden ecosystem simulation,
# unlocked per-save as a secret" -- the whole thing in one sentence, on the page
# the metainfo points at as the help URL.
FRONT_DOOR_FORBIDDEN = [
    (r"terrarium", "names the hidden feature in current documentation"),
    (r"hidden (feature|mode|pattern|simulation|ecosystem)", "announces a hidden thing"),
    (r"unlocked? per[- ]save", "explains how the hidden thing is kept"),
]


def main() -> int:
    problems = 0
    scanned = 0
    for path in PUBLISHED:
        if not path.exists():
            continue
        scanned += 1
        text = path.read_text(encoding="utf-8", errors="replace")
        rules = FORBIDDEN + (FRONT_DOOR_FORBIDDEN if path in FRONT_DOOR else [])
        for lineno, line in enumerate(text.splitlines(), 1):
            for pattern, why in rules:
                if re.search(pattern, line, re.IGNORECASE):
                    print(f"{path.name}:{lineno}: {why}")
                    print(f"    {line.strip()[:100]}")
                    problems += 1
    print()
    print(f"published files scanned: {scanned}   spoilers: {problems}")
    if problems:
        print()
        print("A secret that is written down in the release notes is not a secret.")
        print("Say what changed without saying where it is hidden.")
        return 1
    print("clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
