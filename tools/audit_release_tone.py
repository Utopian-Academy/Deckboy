#!/usr/bin/env python3
"""The release section of CHANGES.md describes what the release DOES.

WHY THIS EXISTS. CHANGES.md becomes the GitHub release body verbatim -- the
release job reads it out of the checkout AT THE TAGGED COMMIT -- so whatever
that section says is the first thing a downloader reads.

The v0.99.374 section was very nearly published opening with "James tested
0.99.373 and reported the new work as broadly broken", under the heading "(what
0.99.373 got wrong, ...)". Not because the rule was unknown -- it is written
down and it names this file -- but because the section had been used as a
WORKING LOG through the day, appended to in the voice of each fix, and then a
release was cut from it.

`audit_spoilers.py` already guards this file against giving away a secret.
Nothing guarded its TONE, which is the other way it can embarrass a release.

WHAT THIS IS NOT. It does not judge prose. It looks for a short list of
phrases that can only be describing Deckboy as broken, and only in the section
for the version in VERSION -- history stays as it was written. Describing a
fault in somebody's SHOW is fine and is what several features are for (CHECK
finds missing media; RELINK finds moved files), so the list avoids words that
do that work.

If a phrase here is genuinely the clearest way to say something, put it in the
release notes anyway and add it to ALLOWED with a reason. The point is that
somebody decides, once, in the open.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Phrases that can only be saying "Deckboy was broken". Kept deliberately
# short: a long list becomes noise, and noise gets silenced.
BANNED = [
    "broadly broken",
    "was broken",
    "were broken",
    "did not work",
    "never worked",
    "does not work",
    "was inert",
    "were inert",
    "no longer lags",
    "no longer crashes",
    "stopped working",
    "got wrong",
    "regression",
    "root cause",
    "the bug",
    "a bug",
    "bugfix",
    "bug fix",
]

# Deliberate exceptions, each with a reason.
ALLOWED = {
    # Nothing yet. When one lands, say why it belongs on a release page.
}


def main():
    version = io.open(os.path.join(ROOT, "VERSION"), encoding="utf-8").read().strip()
    text = io.open(os.path.join(ROOT, "CHANGES.md"), encoding="utf-8",
                   errors="replace").read()

    # The section for THIS version, up to the next heading.
    start = None
    for m in re.finditer(r"^## .*$", text, re.M):
        if re.search(r"v%s(\D|$)" % re.escape(version), m.group(0)):
            start = m.start()
            break
    if start is None:
        print("audit: release notes")
        print()
        print("  no CHANGES.md section for v%s -- the Version Guard fails on this"
              % version)
        print("  too, so there is nothing for this check to read.")
        return 0

    nxt = re.search(r"^## ", text[start + 3:], re.M)
    section = text[start:start + 3 + nxt.start()] if nxt else text[start:]

    fails = []
    lowered = section.lower()
    for phrase in BANNED:
        if phrase in ALLOWED:
            continue
        at = lowered.find(phrase)
        if at < 0:
            continue
        line = section[:at].count("\n") + 1
        # A little context, so the message is actionable without opening the file.
        snippet = " ".join(section[max(0, at - 60):at + 60].split())
        fails.append('"%s" (line %d of the section): ...%s...' % (phrase, line, snippet))

    print("audit: release notes say what the release does")
    print()
    print("  version:          v%s" % version)
    print("  section length:   %d lines" % section.count("\n"))
    print("  phrases checked:  %d" % len(BANNED))
    print()
    for f in fails:
        print("  FAIL " + f)
    if fails:
        print()
        print("  CHANGES.md becomes the release body verbatim. Say what the")
        print("  release DOES; the fault it fixes belongs in")
        print("  private-notes/DECISIONS.md.")
    print()
    print("clean" if not fails else "%d finding%s" % (len(fails), "" if len(fails) == 1 else "s"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
