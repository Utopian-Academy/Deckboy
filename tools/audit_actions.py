#!/usr/bin/env python3
"""Find controls that cannot be reached and actions that do nothing.

This codebase's signature bug is the dead control: something wired to state but
not to effect, or wired to effect but reachable by nothing. It has produced the
masterVolume clamp, the encoder format picker, three timer controls in same-day
code, an `asciiGreen` toggle that wrote a field nobody read, and a working
skip-to-end with no button, no key and no verb. Every one was found by hand,
late, and usually by accident.

So it is a script now. Three checks:

  1. QuickAction with no `case` label -- fires and nothing happens.
  2. QuickAction referenced ONLY by its own case label -- nothing in the UI can
     fire it. Not always a bug (a superseded action whose handler still works is
     fine), but it should be a decision rather than a surprise.
  3. Duplicate settings action ids. Two constants sharing a value silently kills
     whichever handler runs second; ids 634-637 were double-allocated once and
     the symptom was "the Processing sub-tab does nothing".

    python3 tools/audit_actions.py            # report
    python3 tools/audit_actions.py --strict   # non-zero exit if anything is
                                              # unreachable, for CI

Exit code is 1 when check 1 or 3 finds anything, always. `--strict` also fails
on check 2.
"""

import argparse
import collections
import glob
import os
import re
import sys


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.dirname(here)


def read(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return ""


def enum_values(text, name):
    """Every identifier in `enum class <name> { ... }`.

    Entries share lines here ("ToneFreqDec, ToneFreqInc,"), carry trailing
    comments, and sometimes have explicit values -- parsing a line at a time
    silently reported a third of them and made the audit look clean.
    """
    lines = text.split("\n")
    try:
        start = next(i for i, l in enumerate(lines)
                     if l.startswith("enum class %s" % name))
    except StopIteration:
        return []
    end = next(i for i in range(start + 1, len(lines)) if lines[i].startswith("};"))
    found = []
    for line in lines[start + 1:end]:
        for piece in line.split("//")[0].split(","):
            ident = piece.split("=")[0].strip()
            if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", ident):
                found.append(ident)
    return sorted(set(found))


def all_sources(root):
    text = ""
    for pattern in ("native/**/*.cpp", "native/**/*.hpp", "native/**/*.ipp"):
        for path in sorted(glob.glob(os.path.join(root, pattern), recursive=True)):
            text += read(path)
    return text


def audit_quick_actions(root, src):
    types = read(os.path.join(root, "native", "core", "types.hpp"))
    actions = enum_values(types, "QuickAction")
    no_handler, no_caller = [], []
    for action in actions:
        uses = len(re.findall(r"QuickAction::%s\b" % action, src))
        cases = len(re.findall(r"case QuickAction::%s\b" % action, src))
        if cases == 0:
            no_handler.append(action)
        elif uses - cases <= 0:
            no_caller.append(action)
    return actions, no_handler, no_caller


def audit_settings_ids(root):
    """Duplicate values among the kSettingsAction* constants."""
    main = read(os.path.join(root, "native", "main.cpp"))
    by_value = collections.defaultdict(list)
    for name, value in re.findall(
            r"constexpr\s+int\s+(kSettingsAction\w+)\s*=\s*(\d+)\s*;", main):
        by_value[int(value)].append(name)
    return {v: names for v, names in by_value.items() if len(names) > 1}


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--strict", action="store_true",
                        help="also fail when an action has no caller")
    args = parser.parse_args()

    root = repo_root()
    src = all_sources(root)
    actions, no_handler, no_caller = audit_quick_actions(root, src)
    duplicates = audit_settings_ids(root)

    print("QuickAction values: %d" % len(actions))

    # Nothing found is not a pass.
    #
    # Every check below asks whether a list is EMPTY, so an audit that parsed
    # no actions at all reports no unhandled ones, no duplicate ids, and
    # "clean" -- a perfect score for having looked at nothing. That is how
    # audit_warnings.py came to pass on an empty log while a dead local went
    # to CI. If the enum is ever renamed or moved, this says so instead.
    if not actions:
        print("")
        print("FAIL: no QuickAction values found at all. The enum has")
        print("moved or been renamed; this audit is reading the wrong")
        print("thing and its 'clean' would mean nothing.")
        print("or been renamed; this audit is reading the wrong thing and its")
        print("'clean' would mean nothing.")
        return 2

    print("\n[1] no handler -- fires and nothing happens: %d" % len(no_handler))
    for action in no_handler:
        print("      %s" % action)

    print("\n[2] no caller -- nothing in the UI can fire it: %d" % len(no_caller))
    for action in no_caller:
        print("      %s" % action)
    if no_caller:
        print("      (not always a bug: a superseded action whose handler still")
        print("       works is fine. It should be a decision, not a surprise.)")

    print("\n[3] duplicate settings action ids: %d" % len(duplicates))
    for value, names in sorted(duplicates.items()):
        print("      %d shared by %s" % (value, ", ".join(names)))

    failed = bool(no_handler) or bool(duplicates)
    if args.strict and no_caller:
        failed = True
    print("\n%s" % ("FAIL" if failed else "clean"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
