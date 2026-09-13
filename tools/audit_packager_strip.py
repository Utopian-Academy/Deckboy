"""Do the packagers strip everything the app writes into the state dir?

`Paths::stateDir()` is the ONLY place Deckboy may write. So anything it creates
there is, by definition, the packaging machine's own state -- and a release
built on that machine must not carry it. Three times now the list of things to
strip has been wrong:

  - a release shipped the packager's `default.deckboy`, so a fresh install
    opened somebody else's show, and its CAMERA cue meant the first TAKE
    opened a stranger's webcam.
  - `recent_projects.txt` arrived in v0.99.331 and was taught to none of the
    three packagers, so the zip carried an absolute path per show that machine
    had ever opened.
  - `_converted/` was taught to Windows only; macOS and Linux shipped it. On
    one machine that directory was 2.3 GB of the operator's personal footage.
    `recordings/` -- captured programme output -- was known to none of them.

Each time the fix was a comment saying "keep these in sync". Comments do not
fail builds, so each time they drifted again. This does fail.

Two checks:

  1. The three packagers strip the SAME set. They are three files in three
     languages and they have drifted apart twice.
  2. Every name the source writes under `stateDir()` appears in that set. This
     is the check that catches the NEXT one, because a new state file has to be
     added to the source before a packager can be taught about it.

An entry that genuinely should ship is listed in EXPECT_SHIPPED below, so an
exemption is a decision somebody wrote down rather than an oversight.

    python tools/audit_packager_strip.py
"""
import io
import os
import re
import sys

WINDOWS = os.path.join('tools', 'package_windows.ps1')
MACOS = os.path.join('tools', 'package_macos.sh')
LINUX = os.path.join('tools', 'package_linux.sh')
NATIVE = 'native'

# Written under stateDir() but deliberately NOT stripped. Keep the reason.
EXPECT_SHIPPED = {
    # Nothing yet. If you add one, say why -- "it is small" is not a reason;
    # the question is whether it describes the PACKAGING machine.
}

# Names the regex below cannot see, because the path is built through a local
# rather than written as stateDir() / "name". Each needs a source reference so
# it can be rechecked rather than trusted.
INDIRECT = {
    # app_output_mgmt.ipp: base = <project dir> or Paths::stateDir(), then
    # base / "recordings". With no show open it lands in the state dir.
    'recordings': 'native/app/app_output_mgmt.ipp (base / "recordings")',
}


def read(path):
    with io.open(path, encoding='utf-8', errors='replace') as handle:
        return handle.read()


def state_writes():
    """Every name the source writes under stateDir()."""
    found = dict(INDIRECT)
    pattern = re.compile(r'stateDir\(\)\s*/\s*"([^"]+)"')
    for root, _dirs, files in os.walk(NATIVE):
        for name in files:
            if not name.endswith(('.cpp', '.hpp', '.ipp', '.h')):
                continue
            path = os.path.join(root, name)
            for hit in pattern.findall(read(path)):
                found.setdefault(hit, path.replace('\\', '/'))
    return found


def stripped_windows(text):
    names = set()
    for block in re.findall(r'foreach\s*\(\s*\$\w+\s+in\s+@\(([^)]*)\)', text):
        names.update(re.findall(r'"([^"]+)"', block))
    return names


def stripped_shell(text):
    names = set()
    for block in re.findall(r'for\s+\w+\s+in\s+((?:[^\n;]|\\\n)+?)\s*;\s*do', text):
        cleaned = block.replace('\\\n', ' ')
        names.update(token for token in cleaned.split() if token and not token.startswith('$'))
    return names


def main():
    for path in (WINDOWS, MACOS, LINUX):
        if not os.path.exists(path):
            print('audit_packager_strip: missing %s' % path)
            return 1

    lists = {
        'windows': stripped_windows(read(WINDOWS)),
        'macos': stripped_shell(read(MACOS)),
        'linux': stripped_shell(read(LINUX)),
    }

    writes = state_writes()
    expected = set(writes) - set(EXPECT_SHIPPED)

    failures = []

    # 1. Do the three agree, on the names that matter?
    relevant = {name: values & (expected | set(EXPECT_SHIPPED)) for name, values in lists.items()}
    if len({frozenset(v) for v in relevant.values()}) != 1:
        failures.append('the three packagers strip different sets:')
        for name in ('windows', 'macos', 'linux'):
            failures.append('    %-8s %s' % (name, ' '.join(sorted(relevant[name])) or '(none)'))
        union = set().union(*relevant.values())
        for name in ('windows', 'macos', 'linux'):
            missing = union - relevant[name]
            if missing:
                failures.append('    %s is missing: %s' % (name, ' '.join(sorted(missing))))

    # 2. Does every state write appear in all three?
    for item in sorted(expected):
        absent = [name for name in ('windows', 'macos', 'linux') if item not in lists[name]]
        if absent:
            failures.append(
                'written to the state dir but not stripped by %s: %s   (%s)'
                % (', '.join(absent), item, writes[item]))

    if failures:
        print('audit_packager_strip: FAIL')
        print()
        for line in failures:
            print('  ' + line)
        print()
        print('  stateDir() is the only place the app writes, so anything it creates')
        print('  there describes the PACKAGING machine and must not ship. Add the name')
        print('  to all three packagers, or to EXPECT_SHIPPED with a reason.')
        return 1

    print('audit_packager_strip: OK -- %d state entries, stripped by all three'
          % len(expected))
    for item in sorted(expected):
        print('  %s' % item)
    return 0


if __name__ == '__main__':
    sys.exit(main())
