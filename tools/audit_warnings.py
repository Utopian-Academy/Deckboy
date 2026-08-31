#!/usr/bin/env python3
"""Fail the build when a name hides another or a local is never read.

Both of these have already cost real time in this project. A shadowed name is
the worse of the two: whoever edits the inner scope has no indication the outer
one exists, and a duplicated block whose second copy shadowed the first meant a
fix to one arm silently applied to half the cases. A local that is initialised
and never read is the leftover of an approach that was replaced, and it reads as
though it still matters -- one of them sat there long enough to give a later
loop something to shadow.

The compiler already knows about both. It just files them at a level nobody
reads, among several hundred int-to-float conversions into SDL's float APIs.
CMakeLists promotes exactly these to level 1; this reads the build log and turns
them into a failure.

VENDORED CODE IS EXEMPT. native/extras/upstream is byte-identical to upstream
Terrarium by design, and two of its locals are unread. Fixing them here would
mean the next sync no longer applies cleanly, which costs more than the warning.

Usage:
    cmake --build build/windows --config Release 2>&1 | tee build.log
    python tools/audit_warnings.py build.log

Reads stdin when given no file, so it can sit at the end of a pipe.
"""
import re
import sys

# MSVC:        path(line,col): warning C4456: declaration of 'x' hides ...
# GCC / Clang: path:line:col: warning: declaration of 'x' shadows ... [-Wshadow]
MSVC = re.compile(r'^(?P<file>.+?)\((?P<line>\d+)[,)].*warning (?P<code>C4456|C4457|C4458|C4459|C4189): (?P<text>.*)$')
GNU = re.compile(r'^(?P<file>.+?):(?P<line>\d+):\d+: warning: (?P<text>.*?)\s*\[-W(?P<code>shadow[^\]]*|unused-variable)\]\s*$')

EXEMPT = 'extras/upstream'


def findings(lines):
    seen = set()
    for raw in lines:
        line = raw.rstrip('\n').strip()
        m = MSVC.match(line) or GNU.match(line)
        if not m:
            continue
        path = m.group('file').replace('\\', '/')
        if EXEMPT in path:
            continue
        # MSVC prints the same warning once per translation unit that includes
        # the header, so the same site can appear a dozen times.
        key = (path, m.group('line'), m.group('code'))
        if key in seen:
            continue
        seen.add(key)
        short = path.split('deckboy/')[-1]
        text = m.group('text').split('[')[0].strip()
        yield '%s:%s  %s: %s' % (short, m.group('line'), m.group('code'), text)


def main():
    if len(sys.argv) > 1:
        with open(sys.argv[1], encoding='utf-8', errors='replace') as fh:
            found = list(findings(fh))
    else:
        found = list(findings(sys.stdin))

    if not found:
        print('no shadowed names and no unread locals')
        return 0

    print('%d to fix -- a hidden name or a local nothing reads:\n' % len(found))
    for f in found:
        print('  ' + f)
    print('\nRename the inner one, or delete the local. If it is genuinely')
    print('wanted and unused, say so: (void)name; reads as deliberate.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
