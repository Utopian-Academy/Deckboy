"""Which saved settings can a person actually change?

Every field in Project, OutputTarget, Deck and Cue is written to the show file
and read back, so each one is a promise that something can set it. This session
kept finding fields where nothing could:

  - OutputTarget::spoutEnabled had no remote verb and no keyboard path, so
    turning Spout on meant hand-editing the show file.
  - CueKind::NdiSource could only be added from a dialog, so an NDI input could
    not be added from a control surface -- or tested at all.
  - VideoSynthSettings::asciiGlyphs and asciiPhrases had editors that read the
    right cue and wrote a different one, so typing into them did nothing.

A field nothing writes is not necessarily a bug -- some are derived, some are
runtime-only, some are set as a group. But it should be a DECISION, and right
now it is a surprise. This lists them so the surprises can be looked at.

    python tools/audit_reachable.py            # summary
    python tools/audit_reachable.py --list     # every unreachable field
"""
import io
import os
import re
import sys

TYPES = os.path.join('native', 'core', 'types.hpp')

# Structs whose fields are operator-visible settings.
STRUCTS = ('Project', 'OutputTarget', 'Deck', 'Cue', 'VideoSynthSettings',
           'DashboardSlot')

# Fields that are deliberately not settable by hand.
EXPECT_UNSETTABLE = {
    # Identity and bookkeeping, assigned by the app.
    'outputId', 'cueId', 'shortId', 'autoId',
    # Runtime-only, never meant to persist a user choice.
    'loadedCleanly',
}

FIELD = re.compile(
    r'^\s{2,}(?:std::)?[A-Za-z_][\w:<>,\s\*&]*?\s([a-z][A-Za-z0-9_]*)\s*(?:=|;)')


def struct_fields(text, name):
    """Field names declared directly in `struct name`."""
    m = re.search(r'\bstruct\s+' + name + r'\b[^{]*\{', text)
    if not m:
        return []
    depth = 1
    i = m.end()
    body_start = i
    while i < len(text) and depth:
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
        i += 1
    body = text[body_start:i - 1]
    # Drop nested struct bodies so their fields are not attributed here.
    body = re.sub(r'\bstruct\s+\w+[^{]*\{[^{}]*\}[^;]*;', '', body)
    out = []
    for line in body.split('\n'):
        if line.strip().startswith('//'):
            continue
        fm = FIELD.match(line)
        if fm:
            out.append(fm.group(1))
    return out


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    os.chdir(root)
    show_all = '--list' in sys.argv

    types = io.open(TYPES, encoding='utf-8', errors='replace').read()

    # Everything that could set a field: the UI, the remote protocol, the
    # quick actions. NOT the loader or the writer -- those are what make a
    # field persist, not what lets a person change it.
    reach = ''
    for dirpath, _dirnames, filenames in os.walk(os.path.join('native', 'app')):
        for fn in filenames:
            if fn.endswith(('.ipp', '.hpp')):
                reach += io.open(os.path.join(dirpath, fn),
                                 encoding='utf-8', errors='replace').read()
    reach += io.open(os.path.join('native', 'main.cpp'),
                     encoding='utf-8', errors='replace').read()

    total = 0
    unreachable = {}
    for struct in STRUCTS:
        fields = struct_fields(types, struct)
        if not fields:
            print('FAIL: no fields found in struct %s -- it has moved or been'
                  % struct)
            print('renamed, and this audit is reading the wrong thing.')
            return 2
        missing = []
        for f in fields:
            total += 1
            if f in EXPECT_UNSETTABLE:
                continue
            # An assignment to the field anywhere a person's action reaches.
            # Through a dot OR an arrow: half the codebase holds a Cue* and
            # `cue->field =` is as much an assignment as `cue.field =`. Missing
            # the arrow form made this report six fields as unreachable that
            # are set from the inspector every day.
            if re.search(r'(?:\.|->)' + re.escape(f) +
                         r'\s*(?:=[^=]|\+=|-=)', reach):
                continue
            # A nested settings struct is reached through its own fields, never
            # assigned whole -- `cue.videoSynth.asciiCols = x`, not
            # `cue.videoSynth = ...`. Those are not unreachable, they are
            # containers.
            if re.search(r'(?:\.|->)' + re.escape(f) + r'\.[a-z]', reach):
                continue
            missing.append(f)
        if missing:
            unreachable[struct] = missing

    count = sum(len(v) for v in unreachable.values())
    print('settings fields: %d   never assigned by any UI or remote path: %d'
          % (total, count))
    print()
    for struct, fields in sorted(unreachable.items()):
        print('  %s: %d' % (struct, len(fields)))
        if show_all:
            for f in fields:
                print('      %s' % f)
    if not show_all and count:
        print()
        print('  (--list to see them)')
    # Reported, not enforced: some of these are legitimately group-assigned.
    # The number is the thing to watch -- it should go down, never up.
    return 0


if __name__ == '__main__':
    sys.exit(main())
