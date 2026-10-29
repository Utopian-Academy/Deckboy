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
  4. An id inside ANOTHER control's range. A `Base` constant with a handler
     that takes `Base .. Base + 4` reserves all four values, and only the first
     one is visible to check 3 or to a grep. Seven controls were allocated
     inside the encoder queue's three row blocks (RESET DEFAULTS, the ATEM
     address and input, HyperDeck emulation, the three NMC fields) and did
     nothing for weeks, while check 3 said clean. The ranges are read from the
     handlers' own `action >= X && action < Y` tests, so a new block is
     covered the moment it has a handler.

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


def settings_constants(root):
    """Every kSettingsAction* constant with its value.

    A few are written as arithmetic on others ("50008 + 1024 * 8"), so each is
    evaluated once the names it uses are known. Only digits, names and + - * ( )
    are ever evaluated.
    """
    main = read(os.path.join(root, "native", "main.cpp"))
    exprs = dict(re.findall(
        r"constexpr\s+int\s+(kSettingsAction\w+)\s*=\s*([^;]+);", main))
    values = {}
    for _ in range(len(exprs) + 1):
        for name, expr in exprs.items():
            if name not in values:
                value = evaluate(expr, values)
                if value is not None:
                    values[name] = value
    return values


def evaluate(expr, names):
    """The value of a constant expression, or None when it is not one."""
    text = re.sub(r"\b[A-Za-z_]\w*\b",
                  lambda m: str(names[m.group(0)]) if m.group(0) in names else m.group(0),
                  expr)
    if not re.fullmatch(r"[\d\s+*()-]+", text):
        return None
    return int(eval(text, {"__builtins__": {}}, {}))  # digits and + - * only


def audit_settings_ids(values):
    """Duplicate values among the kSettingsAction* constants."""
    by_value = collections.defaultdict(list)
    for name, value in values.items():
        by_value[value].append(name)
    return {v: names for v, names in by_value.items() if len(names) > 1}


# The largest a runtime-sized range can grow: a handler that takes
# `Base + decks.size()` reserves a slot for every deck there can ever be.
def size_limits(root):
    constants = read(os.path.join(root, "native", "core", "constants.hpp"))
    cue_mgmt = read(os.path.join(root, "native", "app", "app_cue_mgmt.ipp"))
    limits = {}
    for name in ("kMaxDecks", "kMaxOutputs"):
        match = re.search(r"constexpr\s+int\s+%s\s*=\s*(\d+)" % name, constants)
        if match:
            limits[name] = int(match.group(1))
    catalog = re.search(r"encoderFormatCatalog\(\)\s*\{.*?kFormats\s*\{(.*?)\n\s*\};",
                        cue_mgmt, re.S)
    if catalog:
        limits["formats"] = len(re.findall(r"^\s*\{\"", catalog.group(1), re.M))
    return {
        "project_.decks.size()": limits.get("kMaxDecks"),
        "project_.outputs.size()": limits.get("kMaxOutputs"),
        "encoderFormatCatalog().size()": limits.get("formats"),
    }


def bound_text(flat, start):
    """The upper bound of a range test: everything up to the end of the test,
    keeping parentheses balanced, so `Base + static_cast<int>(x.size())` is
    read whole."""
    depth, i = 0, start
    while i < len(flat):
        ch = flat[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            if depth == 0:
                break
            depth -= 1
        elif depth == 0 and (flat.startswith("&&", i) or flat.startswith("||", i)
                             or ch in ";?{"):
            break
        i += 1
    return flat[start:i].strip()


def action_ranges(root, values):
    """Every `action >= LO && action < HI` (or <=, or the `action < LO ||
    action >= HI` refusal) in the handlers, as (lo, hi_exclusive, low name,
    bound text, where). Bounds that cannot be sized come back with hi None."""
    limits = size_limits(root)
    found = []
    for pattern in ("native/**/*.cpp", "native/**/*.hpp", "native/**/*.ipp"):
        for path in sorted(glob.glob(os.path.join(root, pattern), recursive=True)):
            text = read(path)
            # Comments out, one line per statement, line numbers kept by offset.
            code = re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), text)
            flat = re.sub(r"\s", " ", code)
            tests = [(m, "and") for m in re.finditer(
                r"\b((?:sb\.)?action)\s*>=\s*(\w+)\s*&&\s*\1\s*(<=?)\s*", flat)]
            tests += [(m, "or") for m in re.finditer(
                r"\b((?:sb\.)?action)\s*<\s*(\w+)\s*\|\|\s*\1\s*(>=)\s*", flat)]
            for match, form in tests:
                low = match.group(2)
                lo = values.get(low, int(low) if low.isdigit() else None)
                if lo is None:
                    continue           # a range over something other than ids
                upper = bound_text(flat, match.end())
                sized = re.sub(r"static_cast<int>\((.*)\)", r"\1", upper)
                for expr, limit in limits.items():
                    if limit is not None:
                        sized = sized.replace(expr, str(limit))
                hi = evaluate(sized, values)
                if hi is not None and match.group(3) == "<=":
                    hi += 1
                where = "%s:%d" % (os.path.relpath(path, root).replace("\\", "/"),
                                   text.count("\n", 0, match.start()) + 1)
                found.append((lo, hi, low, upper, where))
    return found


def family(first, last):
    """What two ends of a NAMED block have in common, cut at a word:
    kSettingsActionPresenterShowPrevious .. kSettingsActionPresenterArrange
    is the kSettingsActionPresenter family."""
    common = os.path.commonprefix([first, last])
    return re.sub(r"[A-Z][a-z0-9]*$", "", common) if not (
        len(common) < len(first) and first[len(common)].isupper()) else common


def audit_ranges(root, values):
    """Ids that sit inside a range some other control's handler takes.

    A Base + N block belongs to its Base alone. A block between two named
    constants (`>= kXFirst && <= kXLast`) belongs to the names that share its
    ends' prefix. Anything else inside either is a collision, whichever
    handler happens to run first today."""
    names_by_value = collections.defaultdict(list)
    for name, value in values.items():
        names_by_value[value].append(name)
    intruders, unsized, seen = [], [], set()
    ranges = action_ranges(root, values)
    for lo, hi, low, upper, where in ranges:
        if hi is None:
            unsized.append((low, upper, where))
            continue
        named_block = low in values and upper in values
        owners = family(low, upper) if named_block else None
        for value in range(lo, hi):
            for name in names_by_value.get(value, []):
                if name == low or (owners and name.startswith(owners)):
                    continue
                key = (name, low)
                if key in seen:
                    continue
                seen.add(key)
                intruders.append((value, name, low, lo, hi - 1, where))
    return intruders, unsized, len(ranges)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--strict", action="store_true",
                        help="also fail when an action has no caller")
    args = parser.parse_args()

    root = repo_root()
    src = all_sources(root)
    actions, no_handler, no_caller = audit_quick_actions(root, src)
    values = settings_constants(root)
    duplicates = audit_settings_ids(values)
    intruders, unsized, range_count = audit_ranges(root, values)

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
        return 2
    if not values or not range_count:
        print("")
        print("FAIL: no kSettingsAction constants or no `action >= X` range tests")
        print("found. They have moved; checks 3 and 4 would pass on nothing.")
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

    print("\n[4] ids inside another control's range: %d (%d ranges read)"
          % (len(intruders), range_count))
    for value, name, low, lo, last, where in sorted(intruders):
        print("      %d %s is inside %s's %d..%d (%s)" % (value, name, low, lo, last, where))
    # A bound the audit cannot size is a range it cannot check. Say so rather
    # than pass it: teach size_limits() the new expression.
    for low, upper, where in unsized:
        print("      cannot size %s .. %s (%s): teach size_limits() this bound"
              % (low, upper, where))

    failed = bool(no_handler) or bool(duplicates) or bool(intruders) or bool(unsized)
    if args.strict and no_caller:
        failed = True
    print("\n%s" % ("FAIL" if failed else "clean"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
