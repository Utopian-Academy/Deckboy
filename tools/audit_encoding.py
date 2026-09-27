#!/usr/bin/env python3
"""Find text that was encoded to UTF-8 twice, and non-ASCII in drawn labels.

WHY THIS EXISTS. Twice now a character has gone into the source double-encoded
and the app has drawn mojibake on a control: the effect-move arrows, and the
degree sign on the Orientation button ("0 (Normal)" arrived on screen as
"0A-circumflex-degree (Normal)"). Neither was caught by a compiler, a test or a
screenshot -- both were caught by somebody eventually looking at the button.

Two checks:

  1. DOUBLE ENCODING. A character encoded to UTF-8, then read back as Latin-1
     and encoded again, always begins with C3 82 or C3 83 followed by C2..C5.
     "C3 82" is a legitimate "A-circumflex" in its own right, but this codebase
     writes English, so every occurrence has been a mistake.

  2. NON-ASCII IN A DRAWN STRING. Box-drawing characters, arrows and accents in
     COMMENTS are fine -- this codebase is full of them and they never reach a
     renderer. In a string literal passed to a draw call they depend on the
     font having the glyph, which the bundled UI font often does not.

Usage:
    python tools/audit_encoding.py            # report
    python tools/audit_encoding.py --strict   # non-zero exit on any finding
"""

import argparse
import os
import sys

# Anything under here is somebody else's code, with somebody else's rules.
SKIP_DIRS = {"upstream", "extras"}
SOURCE_SUFFIXES = (".cpp", ".hpp", ".ipp", ".h")

# C3 82 <cont> and C3 83 <cont> C2 <cont>: the two shapes a double encoding
# takes for the characters that actually turn up in this source.
DOUBLE_ENCODED = (bytes([0xC3, 0x82]), bytes([0xC3, 0x83]))

# ── THE ONE THAT CATCHES THE REST ────────────────────────────────────────────
#
# The two signatures above are what double-encoding a Latin-1 character leaves
# behind, and they missed every double-encoded ARROW and SYMBOL in the tree,
# because those are three bytes rather than one. Put U+25BC (the dropdown
# triangle) through UTF-8 twice and you get C3 A2 C2 96 C2 BC: the lead byte
# becomes C3 A2, which is not in the list above, and nothing fired.
#
# What every one of them does leave is a byte in U+0080..U+009F -- encoded as
# C2 80 through C2 9F. Those code points are the C1 control characters: not
# letters, not punctuation, not printable, and not something anybody types.
# Any three-byte glyph (arrows, box drawing, music, stars, the whole symbol
# range) has a continuation byte in 0x80..0x9F, so double-encoding it always
# produces one of these, and nothing legitimate ever does.
#
# This found two that had been sitting in the tree: a dropdown triangle drawn
# as mojibake beside the cue transition style, and a smoke test feeding the
# text-mode renderer a double-encoded music note and star, so it had been
# exercising the wrong characters.
C1_CONTROLS = tuple(bytes([0xC2, b]) for b in range(0x80, 0xA0))


def source_files(roots):
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for name in sorted(filenames):
                if name.endswith(SOURCE_SUFFIXES):
                    yield os.path.join(dirpath, name).replace(os.sep, "/")


def line_of(data, offset):
    return data[:offset].count(b"\n") + 1


def context(data, offset, before=45, after=25):
    """The bytes around a hit, rendered so any console can print them.

    This used to hand the decoded text straight to print(), which throws
    UnicodeEncodeError on a Windows console (cp1252 has no U+0096) -- so the
    check crashed on exactly the fault it had just found, reporting a traceback
    instead of a file and a line. Anything outside printable ASCII is shown as
    \\xNN, which is also more use here: the whole point is which BYTES are
    wrong, and a mojibake glyph rendered as a mojibake glyph tells you nothing.
    """
    start = max(0, offset - before)
    chunk = data[start:offset + after]
    out = []
    for byte in chunk:
        if byte == 0x0A:
            out.append(" ")
        elif 0x20 <= byte < 0x7F:
            out.append(chr(byte))
        else:
            out.append("\\x%02x" % byte)
    return "".join(out).strip()


def find_double_encoded(path, data):
    out = []
    for signature in DOUBLE_ENCODED + C1_CONTROLS:
        at = -1
        while True:
            at = data.find(signature, at + 1)
            if at < 0:
                break
            out.append((path, line_of(data, at), context(data, at)))
    # One report per line: a double-encoded arrow trips several signatures at
    # once and three lines saying the same thing reads as three faults.
    seen = set()
    unique = []
    for entry in out:
        key = (entry[0], entry[1])
        if key in seen:
            continue
        seen.add(key)
        unique.append(entry)
    return sorted(unique, key=lambda e: e[1])


def find_nonascii_literals(path, data):
    """Non-ASCII bytes inside a "..." literal on a line that draws something.

    Deliberately crude: it looks at whole LINES rather than parsing C++, so a
    label built across two lines is missed. The alternative is a C++ parser for
    a check whose entire job is to catch a character somebody pasted, and the
    crude version has caught both real instances.
    """
    out = []
    for number, raw in enumerate(data.split(b"\n"), start=1):
        stripped = raw.lstrip()
        if stripped.startswith(b"//") or stripped.startswith(b"*"):
            continue
        if b'"' not in raw:
            continue
        if not any(k in raw for k in (b"draw", b"Text", b"label", b"Label",
                                      b"triggerToast", b"Btn(")):
            continue
        # Only what is between the first and last quote on the line.
        first, last = raw.find(b'"'), raw.rfind(b'"')
        if last <= first:
            continue
        literal = raw[first:last + 1]
        if all(byte < 0x80 for byte in literal):
            continue
        out.append((path, number,
                    literal.decode("utf-8", "replace")[:70].strip()))
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--strict", action="store_true",
                        help="exit non-zero when anything is found")
    parser.add_argument("roots", nargs="*", default=["native"])
    args = parser.parse_args()
    roots = args.roots or ["native"]

    doubles, literals = [], []
    for path in source_files(roots):
        with open(path, "rb") as handle:
            data = handle.read()
        doubles += find_double_encoded(path, data)
        literals += find_nonascii_literals(path, data)

    print("[1] double-encoded UTF-8 (drawn as mojibake): %d" % len(doubles))
    for path, line, ctx in doubles:
        print("    %s:%d  ...%s..." % (path, line, ctx))
    print()
    print("[2] non-ASCII in a drawn string literal: %d" % len(literals))
    for path, line, text in literals:
        print("    %s:%d  %s" % (path, line, text))

    if not doubles and not literals:
        print("\nclean")
        return 0
    # [1] IS ALWAYS FATAL, and it used to need --strict to be fatal at all --
    # which CI does not pass. So the one thing this check calls never
    # acceptable was the one thing it could not fail a build over, and two
    # double-encoded glyphs sat in the tree being reported to nobody. The
    # comment already said it was never acceptable; now the exit code agrees.
    #
    # [2] stays advisory even under --strict-less runs: a glyph the bundled
    # font HAS is perfectly fine and this cannot tell which. --strict is what
    # promotes [2] to a failure.
    if doubles:
        print("\n%d double-encoded sequence(s): these draw as mojibake." % len(doubles))
        return 1
    if args.strict and literals:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
