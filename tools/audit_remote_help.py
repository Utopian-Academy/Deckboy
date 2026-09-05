"""Does the remote protocol's HELP describe the protocol that exists?

HELP is the only description of the remote interface an operator or a surface
author ever sees. When it drifts, it does so in two directions and both are
damaging:

  - a verb HELP promises that is NOT handled answers "unknown command", and
    the person reading it concludes the connection is broken rather than the
    documentation. HELP advertised `OUT <on|off>` for turning an output on;
    OUT is the trim out-point verb and answers "expected a number of seconds".

  - a verb that IS handled but is not in HELP cannot be found at all. Text
    mode grew from three verbs to fifteen without HELP changing, so twelve
    working controls were reachable only by reading the source.

Both are checkable: the handled set is `command == "X"` in the dispatcher, and
the documented set is the words in the HELP string. This compares them.

Not every handled verb needs a line of its own -- aliases and deliberate
shorthands are listed in EXPECT_UNDOCUMENTED below, so an exemption is a
decision somebody wrote down rather than an accident.

    python tools/audit_remote_help.py
"""
import io
import os
import re
import sys

DISPATCH = os.path.join('native', 'app', 'app_remote_command.ipp')
HELP = os.path.join('native', 'app', 'app_network.ipp')

# Handled verbs that deliberately have no HELP line of their own.
EXPECT_UNDOCUMENTED = {
    # Aliases for a documented verb.
    'TRIMOUT', 'TRIMIN', 'COLUMNS', 'CHARSET', 'SIPHON', 'SYPHONCUE',
    'CAMERACUE', 'WINDOWCUE', 'SPOUTCUE', 'NDICUE', 'DURATION', 'STILLDUR',
    # Development and diagnostic verbs, not part of the operator surface.
    'SECTION', 'CODE', 'IMPORT', 'CLICK',
    'AUDIOVISUAL',
}


def handled_verbs(text):
    """Every verb the dispatcher compares against."""
    found = set()
    for m in re.finditer(r'command\s*==\s*"([A-Z][A-Z0-9_]*)"', text):
        found.add(m.group(1))
    return found


def duplicate_handlers(text):
    """Verbs the dispatcher tests for MORE THAN ONCE.

    The first branch wins and every later one is unreachable -- and worse than
    unreachable, because the first branch then answers for messages it was
    never meant to see. OVERLAY was handled twice: the time-code toggle
    claimed it, so the cue overlay bin (PUSH/POP/CLEAR) could not be reached at
    all, and "OVERLAY PUSH 3" silently flipped the timecode burn-in instead
    while replying OK.

    Counted as OCCURRENCES, not names. Both branches spell the verb
    identically, so the set of handled names -- which is what every other
    check here uses -- cannot see it.
    """
    # Only TOP-LEVEL branches count. The dispatcher's own `if (command == ...)`
    # sits at four spaces; a verb tested again at six or more is INSIDE its own
    # branch, which is how `if (A || B)` then `if (A)` reads -- perfectly
    # reachable, and the first version of this check called eight of those a
    # collision. Continuation lines of the same `if` are folded in so a chain
    # spread over several lines is still one branch.
    seen = {}
    lines = text.split(chr(10))
    for i, line in enumerate(lines):
        if not re.match(r'^ {4}if \(command ==', line):
            continue
        logical = line
        for extra in lines[i + 1:i + 3]:
            if re.match(r'^ {6,}', extra) and 'command ==' in extra:
                logical += ' ' + extra
            else:
                break
        for name in set(re.findall(r'command\s*==\s*"([A-Z][A-Z0-9_]*)"', logical)):
            seen[name] = seen.get(name, 0) + 1
    return dict((name, n) for name, n in seen.items() if n > 1)


def help_text(text):
    """The HELP reply, as one string."""
    start = text.index('"DECKBOY_0.01 help')
    # The reply is a run of adjacent string literals; take them until the
    # statement ends.
    end = text.index(';', start)
    chunk = text[start:end]
    return ' '.join(re.findall(r'"((?:[^"\\]|\\.)*)"', chunk))


def documented_verbs(help_body):
    """Words in HELP that look like verbs: capitals at a word boundary."""
    return set(re.findall(r'\b([A-Z][A-Z0-9_]{1,})\b', help_body))


def listing_claim(text):
    """The number HELP ALL claims it is about to print.

    A count is a promise the same way a verb name is, and this one had drifted
    to 257 while the listing below it held 263 -- so a surface author counting
    on it to know whether their build was current was told the wrong thing by
    the one line that exists to say so.
    """
    m = re.search(r'DECKBOY_0\.01 every verb \((\d+)\)', text)
    return int(m.group(1)) if m else None


def full_listing(text):
    """The verb list HELP ALL prints."""
    start = text.index('"DECKBOY_0.01 every verb')
    end = text.index(';', start)
    chunk = text[start:end]
    literals = re.findall(r'"((?:[^"\\]|\\.)*)"', chunk)
    return set(re.findall(r'([A-Z][A-Z0-9_]{1,})', ' '.join(literals)))


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)
    dispatch = io.open(DISPATCH, encoding='utf-8', errors='replace').read()
    network_text = io.open(HELP, encoding='utf-8', errors='replace').read()
    network = network_text

    handled = handled_verbs(dispatch)
    body = help_text(network)
    documented = documented_verbs(body)

    if not handled:
        print('FAIL: no verbs found in the dispatcher. It has moved or been')
        print('renamed, and this audit is reading the wrong thing.')
        return 2

    # Words in HELP that look like verbs but nothing handles. Sub-commands
    # (ASCII GLYPHS, FX ADD) are documented under their parent, so only flag a
    # word when its PARENT is not handled either.
    promised = sorted(
        w for w in documented
        if w not in handled and len(w) > 2 and not w.isdigit())
    # A sub-command is fine when it appears after a handled verb on its line.
    real_promises = []
    for word in promised:
        line = next((L for L in body.split('\\n') if word in L), '')
        parent = any(h in line for h in handled)
        if not parent:
            real_promises.append(word)

    missing = sorted(v for v in handled
                     if v not in documented and v not in EXPECT_UNDOCUMENTED)

    # THE COMPLETE LISTING IS THE ONE THAT MUST NOT DRIFT. The summary above is
    # curated and covers about a quarter of the protocol on purpose -- it has
    # to fit on a screen. HELP ALL is the reference, and a verb missing from it
    # cannot be discovered by anyone building a surface.
    listing = full_listing(network_text)
    listing.discard('DECKBOY_0')
    unlisted = sorted(v for v in handled if v not in listing)
    claimed = listing_claim(network_text)
    miscounted = claimed is not None and claimed != len(listing)

    print('verbs handled: %d   words documented: %d' % (len(handled), len(documented)))
    print()
    print('[1] promised by HELP, handled by nothing: %d' % len(real_promises))
    for w in real_promises:
        print('      %s' % w)
    print()
    print('[2] handled but absent from the HELP summary: %d' % len(missing))
    print('      (a summary, not a fault -- HELP ALL is the reference)')
    print()
    print('[3] handled but MISSING FROM HELP ALL: %d' % len(unlisted))
    for v in unlisted:
        print('      %s' % v)
    print()
    print('[4] HELP ALL counts itself correctly: %s'
          % ('no -- says %d, lists %d' % (claimed, len(listing))
             if miscounted else 'yes (%d)' % len(listing)))

    dupes = duplicate_handlers(dispatch)
    print()
    print('[5] verbs the dispatcher tests more than once: %d' % len(dupes))
    for name in sorted(dupes):
        print('      %s (%d branches; only the first can ever run)'
              % (name, dupes[name]))

    failed = bool(real_promises) or bool(unlisted) or miscounted or bool(dupes)
    print()
    print('FAIL' if failed else 'clean')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
