#!/usr/bin/env python3
"""Check that saveProject writes the cue record in the order loadProject reads.

The show file is tab-delimited and POSITIONAL: the writer emits one long
operator<< chain, and the reader addresses columns as `base + N`. Nothing in
the language ties the two together, so a field inserted into the middle of the
writer -- rather than appended at the end -- silently shifts every column after
it, and the reader goes on addressing the old positions.

The symptom is not a crash. It is values that clamp back into range and look
plausible. tone.synth.tuning and referenceHz were written mid-record and read
from the end, which put twenty-three video-synth fields two columns early:
text mode came back holding the pixel-sort value, and a column count of 20,
read from a column holding 1, clamped to 20 -- its own minimum -- and looked
perfect. It survived years of round-trip smoke tests.

Gap-and-duplicate checks do NOT catch that: the reader's offsets were
contiguous the whole time, just attached to the wrong fields. The only thing
that catches it is comparing the two orders directly, which is what this does.

Both sides are matched by the member they name (`cue.videoSynth.shape`), so a
field the writer emits from a literal or a lambda is skipped rather than
guessed at -- those columns are checked only by the fields around them staying
in step.

Exit status is 1 on any disagreement, so CI can hold the line.
"""
from __future__ import annotations

import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The show file's reader and writer, which used to be in main.cpp.
DEFAULT_SRC = os.path.join(REPO, 'native', 'core', 'project_file.ipp')

MEMBER = re.compile(r'\bcue\.([A-Za-z_][A-Za-z0-9_.]*)')
READ = re.compile(r'\bcue\.([A-Za-z_][A-Za-z0-9_.]*)\s*=[^;]*?'
                  r'\bsafe(?:String|Int|Double|Bool|Float|Size)\s*\(\s*fields\s*,'
                  r'\s*([A-Za-z_][A-Za-z0-9_]*)\s*\+\s*(\d+)', re.S)


def writer_order(text: str) -> list[tuple[int, str]]:
    """(column, member) for each field the cue writer emits, in order.

    Column 0 is the "cue" tag. A field written by a multi-line lambda is one
    field, not one per line, so brace depth gates the counting.
    """
    lines = text.split('\n')
    try:
        start = next(i for i, L in enumerate(lines)
                     if L.strip() == '<< "cue\\t"')
    except StopIteration:
        # Said plainly, because the alternative is a stack trace that reads
        # like the tool is broken when the code it audits has simply moved.
        raise SystemExit(
            "no cue record writer in this file. The show file is "
            "written by saveProject; point --source at wherever "
            "that now lives.")
    fields: list[tuple[int, str]] = []
    col = 0
    depth = 0
    pending = ''
    for L in lines[start + 1:]:
        stripped = L.strip()
        if stripped.startswith("<< '\\n';"):
            break
        opens = L.count('{') + L.count('[')
        closes = L.count('}') + L.count(']')
        if depth > 0:
            pending += ' ' + stripped
            depth += opens - closes
            continue
        # Split this line on its tab writes; each one closes a field.
        parts = re.split(r"<< '\\t'", stripped)
        for i, part in enumerate(parts):
            pending += ' ' + part
            if i < len(parts) - 1:
                col += 1
                m = MEMBER.search(pending)
                fields.append((col, m.group(1) if m else ''))
                pending = ''
        depth += opens - closes
    if pending.strip():
        col += 1
        m = MEMBER.search(pending)
        fields.append((col, m.group(1) if m else ''))
    return fields


def reader_order(text: str) -> dict[str, tuple[str, int]]:
    """member -> (base, offset), for the cue branch only."""
    start = text.index('} else if (fields[0] == "cue")')
    body = text[start:]
    out: dict[str, tuple[str, int]] = {}
    for m in READ.finditer(body):
        line_end = body.find(chr(10), m.end())
        if 'layout-probe' in body[m.start():line_end if line_end > 0 else len(body)]:
            continue
        out.setdefault(m.group(1), (m.group(2), int(m.group(3))))
    return out


def audit(text: str) -> list[str]:
    written = writer_order(text)
    read = reader_order(text)
    # Pair up every field both sides name, and find the constant that turns a
    # writer column into a reader offset. The right constant is the one most
    # fields agree on; anything that disagrees with it is a skew.
    problems: list[str] = []
    pairs = [(col, name, read[name]) for col, name in written
             if name and name in read]
    by_base: dict[str, list[tuple[int, str, int]]] = {}
    for col, name, (base, off) in pairs:
        by_base.setdefault(base, []).append((col, name, off))
    for base, items in sorted(by_base.items()):
        if len(items) < 4:
            continue
        votes: dict[int, int] = {}
        for col, _, off in items:
            votes[col - off] = votes.get(col - off, 0) + 1
        best = max(votes, key=lambda k: votes[k])
        for col, name, off in sorted(items):
            if col - off != best:
                problems.append(
                    'cue.%s: written at column %d, read from %s + %d '
                    '-- %d column%s %s than the rest of the record'
                    % (name, col, base, off, abs((col - off) - best),
                       '' if abs((col - off) - best) == 1 else 's',
                       'earlier' if (col - off) > best else 'later'))
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', default=DEFAULT_SRC)
    args = ap.parse_args()
    text = open(args.source, encoding='utf-8', errors='replace').read()
    problems = audit(text)
    if not problems:
        print('cue record: writer and reader agree on every field they both name')
        return 0
    print('cue record: writer and reader disagree')
    for p in problems:
        print('  ' + p)
    print()
    print('%d field(s) out of step. A positional record has to be appended to, '
          'never inserted into.' % len(problems))
    return 1


if __name__ == '__main__':
    sys.exit(main())
