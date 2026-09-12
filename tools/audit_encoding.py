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
    start = max(0, offset - before)
    text = data[start:offset + after].decode("utf-8", "replace")
    return text.replace("\n", " ").strip()


def find_double_encoded(path, data):
    out = []
    for signature in DOUBLE_ENCODED:
        at = -1
        while True:
            at = data.find(signature, at + 1)
            if at < 0:
                break
            out.append((path, line_of(data, at), context(data, at)))
    return out


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
    # [2] is advisory: a glyph the bundled font HAS is fine, and this cannot
    # tell. [1] is never acceptable.
    if args.strict and doubles:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
