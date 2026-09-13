#!/usr/bin/env python3
"""Fail when an enum grew and the code that rebuilds it from an int did not.

A saved show is integers. Every one of them has to be turned back into an enum
value, and every one of those conversions carries a bound: a clamp in the
loader, a modulo in a UI cycler. When a new member is appended to the enum, the
enum is the only thing that changes -- the bounds sit there silently one short.

What that looks like from the outside is the worst failure this project has:
the feature works perfectly for the whole session and is quietly wrong the next
time the show is opened. Two live examples, both found by hand:

  * ToneWaveform::Fds (5) clamped to 0..4 -- a 2A03 synth cue reopened as
    Identify, the channel-walking test tone. It kept its name and its synth
    settings, the inspector kept drawing them, and playing a note answered
    "no synth cue is live", because nothing on air was a synth any more.

  * VideoSynthPalette's last six of eleven clamped to 0..4 -- a cue made on
    Game Boy, CGA, NES or Vapor reopened on Mono.

Neither is a compiler error, neither trips a test that never saves and reloads,
and neither looks wrong in a screenshot taken before the reload.

The fix in both cases was the same and is what this checks for: name the size
once (kToneWaveformCount, kVideoSynthPaletteCount, kDatamoshLookCount) and let
every conversion refer to it. A symbolic bound is accepted without inspection
here -- the point is that it cannot drift.

Usage:  python tools/audit_enum_bounds.py [--root .]
"""

import argparse
import os
import re
import sys

SKIP = ("extras/upstream",)
SOURCE_SUFFIXES = (".cpp", ".hpp", ".ipp")

# The conversions are found by SCANNING, not by one regex: the value being
# converted is itself a call with commas in it -- safeInt(fields, tb + 22, 0) --
# so a pattern that stops at the first comma matches nothing at all. A version
# of this file did exactly that and passed a tree it should have failed, which
# is the same class of quiet nothing it exists to catch.
CAST = re.compile(r"static_cast<(\w+)>\s*\(")


def balanced(src, open_index):
    """Text inside the parenthesis that starts at open_index, or None."""
    depth = 0
    for i in range(open_index, len(src)):
        if src[i] == "(":
            depth += 1
        elif src[i] == ")":
            depth -= 1
            if depth == 0:
                return src[open_index + 1:i]
    return None


def split_args(text):
    """Top-level comma split -- nested calls keep their own commas."""
    args, depth, start = [], 0, 0
    for i, ch in enumerate(text):
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        elif ch == "," and depth == 0:
            args.append(text[start:i].strip())
            start = i + 1
    args.append(text[start:].strip())
    return args


def split_modulo(text):
    """Split on a % that is outside brackets; [text] when there is none."""
    depth = 0
    for i, ch in enumerate(text):
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        elif ch == "%" and depth == 0:
            return [text[:i].strip(), text[i + 1:].strip()]
    return [text]


def integer(token):
    token = token.strip()
    return int(token) if re.fullmatch(r"-?\d+", token) else None


def wrong_count_constant(token, enum_name):
    """A k<Something>Count bound that names a DIFFERENT enum, or None.

    A symbolic bound cannot drift, which is the whole point of using one -- but
    it can name the wrong enum, and that reads as correct at a glance. A search
    and replace over these cyclers put kVideoSynthPaletteCount (11) on the FDS
    carrier (5) in three places, and every one of them looked deliberate.
    """
    found = re.search(r"\bk(\w+)Count\b", token)
    if not found or found.group(1) == enum_name:
        return None
    return found.group(0)


def assignment_before(src, index, name):
    """What `name` was last assigned in the few statements before index."""
    window = src[max(0, index - 600):index]
    matches = list(re.finditer(r"\b" + re.escape(name) + r"\s*=\s*([^;]+);", window))
    return matches[-1].group(1).strip() if matches else None


def source_files(root):
    for base, dirs, names in os.walk(os.path.join(root, "native")):
        rel = base.replace(os.sep, "/")
        if any(skip in rel for skip in SKIP):
            dirs[:] = []
            continue
        for name in sorted(names):
            if name.endswith(SOURCE_SUFFIXES):
                yield os.path.join(base, name)


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        return handle.read()


def collect_enums(root):
    """name -> (member count, last member, where it is declared)."""
    enums = {}
    for path in source_files(root):
        if not path.endswith(".hpp"):
            continue
        src = read(path)
        for match in re.finditer(r"enum\s+class\s+(\w+)[^{;]*\{(.*?)\}\s*;", src, re.S):
            name, body = match.group(1), match.group(2)
            body = re.sub(r"//[^\n]*", "", body)
            body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
            members = [t.split("=")[0].strip() for t in body.split(",") if t.strip()]
            if not members:
                continue
            # A trailing Count marker is the size, not a value.
            if members[-1].lower() in ("count", "kcount", "last"):
                members = members[:-1]
            if members:
                enums[name] = (len(members), members[-1], path)
    return enums


def findings_for(root, enums):
    findings = []
    for path in source_files(root):
        src = read(path)
        rel = os.path.relpath(path, root).replace(os.sep, "/")
        for match in CAST.finditer(src):
            name = match.group(1)
            if name not in enums:
                continue
            inner = balanced(src, match.end() - 1)
            if inner is None:
                continue
            count, last, _ = enums[name]
            inner = inner.strip()
            bound = expected = None
            what = ""
            token = None
            if inner.startswith("std::clamp("):
                args_text = balanced(inner, len("std::clamp(") - 1)
                parts = split_args(args_text) if args_text is not None else []
                if len(parts) == 3:
                    token = parts[2]
                    bound = integer(parts[2])
                    expected = count - 1
                    what = "A value above the bound is clamped away on load."
            else:
                # The cycler is written both ways: the modulo inline inside the
                # cast, and one line above it into a local. Miss the second and
                # the check is half a check -- cycleToneWaveform is written that
                # way, and it was one of the two faults that prompted this.
                expression = inner
                if re.fullmatch(r"\w+", inner):
                    expression = assignment_before(src, match.start(), inner)
                pieces = split_modulo(expression) if expression else [inner]
                if len(pieces) == 2 and "static_cast<int>" in pieces[0]:
                    token = pieces[1]
                    bound = integer(pieces[1])
                    expected = count
                    what = "The cycler cannot reach the members past it."
            line = src[:match.start()].count("\n") + 1
            if bound is None:
                borrowed = wrong_count_constant(token, name) if token else None
                if borrowed:
                    findings.append(
                        "%s:%d: %s is bounded by %s, which is another enum's size"
                        " (%s has %d members, last is %s)."
                        % (rel, line, name, borrowed, name, count, last))
                continue
            if bound == expected:
                continue
            findings.append(
                "%s:%d: %s is bounded by %d but has %d members (last is %s). %s"
                % (rel, line, name, bound, count, last, what))
    return findings


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=".", help="repository root")
    args = parser.parse_args()

    enums = collect_enums(args.root)
    findings = findings_for(args.root, enums)
    if findings:
        print("Enum bounds that have fallen behind their enum:\n")
        for finding in findings:
            print("  " + finding)
        print("\nName the size once next to the enum (see kToneWaveformCount)"
              " and use it at every conversion.")
        return 1

    print("enum bounds: %d enums, every int-to-enum conversion agrees with its"
          " enum" % len(enums))
    return 0


if __name__ == "__main__":
    sys.exit(main())
