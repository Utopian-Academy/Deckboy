#!/usr/bin/env python3
"""Find functions that always fail while their callers treat them as working.

WHY THIS EXISTS. v0.99.373 shipped with six deck-to-output routing functions
that were single-deck stubs -- `assignDeckToOutput` ignored the layer it was
handed, `assignmentIndexForDeckOutput` answered "deck 0 on output 0" and
nothing else, `setDeckOutputAssignmentLayer` returned false, and
`unassignDeckFromOutput` refused outright -- while nine call sites, and the
whole output-routing UI, were written as though they worked.

Nothing caught it. `audit_actions.py` checks that every QuickAction has a
handler and that something fires it; both were true. The compiler is happy
with a function that returns false. Only pressing the program found it.

WHAT THIS DOES NOT DO, and three attempts are recorded so the next one does
not repeat them. A check for "a capability the socket can reach and no control
can" was tried three ways:

  - "called from a file that draws" named 38 functions, every one of which the
    UI reached through one more hop.
  - "does anything that draws write the same field" fired on nothing at all,
    because almost every field is written somewhere.
  - a transitive walk of the call graph could not be made to FAIL on a
    capability whose doors had been removed by hand, which means its clean
    result was not evidence of anything.

A check for "an action whose only control is a stepper chevron" -- the shape
that left thirty-one inspector rows dead -- cannot be written against the
source either: the chevrons are registered inside one shared helper, which
pushes a variable rather than a named action, so every row looks identical
from here.

Both faults are real and both were found by driving the program. A gate that
reports clean without being able to fail is worse than no gate; it is what let
all of this ship. So this file holds the one check that has been shown to
fail on the real fault, and says plainly what it does not cover.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NATIVE = os.path.join(ROOT, "native")

# Deliberate cases. A name here is a promise that somebody looked at it and
# wrote down why, not a way to make the audit quiet.
ALLOW = {
    # Nothing yet. When one lands, say why it is allowed to do nothing.
}


def sources():
    for base, _dirs, files in os.walk(NATIVE):
        if "upstream" in base:
            continue
        for name in files:
            if name.endswith((".cpp", ".hpp", ".ipp")):
                yield os.path.join(base, name)


FILES = {p: io.open(p, encoding="utf-8", errors="replace").read() for p in sources()}
ALL = "\n".join(FILES.values())

# The SHAPE of a body that does nothing: a single return of a falsy value,
# optionally after a toast, optionally after comments. A function that refuses
# for a reason tests something first, so it cannot match this.
STUB = re.compile(
    r"\n  (?:bool|int|std::optional<[^>]+>|std::string)\s+(\w+)\s*\([^;{]*\)\s*(?:const\s*)?\{\s*"
    r"(?://[^\n]*\n\s*)*"
    r"(?:triggerToast\([^;]*\);\s*)?"
    r"return\s*(?:false|-1|\{\}|std::nullopt|\"\")\s*;\s*\}", re.S)

fails = []
checked = 0
for path, text in FILES.items():
    for m in STUB.finditer(text):
        checked += 1
        name = m.group(1)
        if name in ALLOW:
            continue
        # Only a fault if something else calls it. An unused stub is dead
        # code -- untidy, but it is not lying to anybody.
        callers = len(re.findall(r"\b%s\s*\(" % re.escape(name), ALL)) - 1
        if callers > 0:
            fails.append("%s() always fails, and %d call site%s treat it as working  (%s)"
                         % (name, callers, "" if callers == 1 else "s",
                            os.path.basename(path)))

print("audit: stubs that lie to their callers")
print()
print("  files scanned:     %d" % len(FILES))
print("  do-nothing bodies: %d" % checked)
print()
for f in fails:
    print("  FAIL " + f)
print()
print("clean" if not fails else "%d finding%s" % (len(fails), "" if len(fails) == 1 else "s"))
sys.exit(1 if fails else 0)
