"""Build an isolated DECKBOY_ROOT that is actually dressed.

An isolated root keeps a test off the operator's show file and out of their
recordings folder, which is the point. But a root containing nothing but
`data/default.deckboy` boots an app with no themes, no fonts, no splash art and
no UI images — it runs, and it logs "Theme not found", and every screenshot from
it is missing half its furniture. Tests that boot a half-dressed app are not
testing the thing that ships.

So: copy the ASSETS (themes, fonts, UI art, sprites, demos) and leave behind
everything that is the operator's — their show, their converted media, their
recordings, their logs. `_converted` alone is over two gigabytes.
"""

import os
import shutil

# Directories worth having. Everything else under data/ is either the
# operator's material or generated at runtime.
ASSET_DIRS = ("themes", "ui", "sprites", "demos", "fonts")
ASSET_FILE_SUFFIXES = (".ttf", ".otf")


def repo_data_dir(start=None):
    """Find the repo's data/ directory by walking up from this file."""
    here = os.path.dirname(os.path.abspath(start or __file__))
    for _ in range(6):
        candidate = os.path.join(here, "data")
        if os.path.isdir(candidate):
            return candidate
        parent = os.path.dirname(here)
        if parent == here:
            break
        here = parent
    return None


def populate(root, source_data=None, verbose=False):
    """Copy the bundled assets into `root`/data. Returns what was copied.

    Cheap enough to do per run (about 40MB) and skipped entirely when the
    destination already has it, so repeated cases in one run pay once.
    """
    src = source_data or repo_data_dir()
    dst = os.path.join(root, "data")
    os.makedirs(dst, exist_ok=True)
    if not src or not os.path.isdir(src):
        return []

    copied = []
    for name in ASSET_DIRS:
        s = os.path.join(src, name)
        d = os.path.join(dst, name)
        if os.path.isdir(s) and not os.path.exists(d):
            shutil.copytree(s, d)
            copied.append(name + "/")
    for entry in os.listdir(src):
        if entry.lower().endswith(ASSET_FILE_SUFFIXES):
            s = os.path.join(src, entry)
            d = os.path.join(dst, entry)
            if os.path.isfile(s) and not os.path.exists(d):
                shutil.copy2(s, d)
                copied.append(entry)
    if verbose and copied:
        print("test root dressed with: %s" % ", ".join(copied))
    return copied


def warn_if_stale(exe, quiet=False):
    """Shout if the binary predates the newest source file.

    Twice now a whole round of testing has been run against a stale
    executable -- once from a build directory nobody had rebuilt in days, once
    from a build loop that matched the word "Deckboy.exe" inside a LINK ERROR
    and reported success. Both times the results were confidently wrong and the
    conclusions drawn from them were nonsense.

    A binary older than the source is never worth testing, so say so.
    """
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    try:
        built = os.path.getmtime(exe)
    except OSError:
        return False
    newest = 0.0
    newest_name = ""
    for base in ("native", "tools"):
        for dirpath, _dirs, files in os.walk(os.path.join(root, base)):
            for name in files:
                if not name.endswith((".cpp", ".hpp", ".ipp")):
                    continue
                try:
                    stamp = os.path.getmtime(os.path.join(dirpath, name))
                except OSError:
                    continue
                if stamp > newest:
                    newest, newest_name = stamp, name
    if newest > built:
        if not quiet:
            print("!! STALE BINARY: %s was built before %s changed." % (
                os.path.basename(exe), newest_name))
            print("!! Rebuild before trusting anything below.")
        return True
    return False


def cue_effects_field_index(fields):
    """Where the effects string lives in a cue record, given its fields.

    It is NOT a fixed column. The record carries its composite slots inline, so
    every field after them shifts by 11 per slot. Hardcoding 154 works only for
    a cue with no composite slots; on any other cue it writes into the
    motion-driver path instead, and the effect silently never arrives -- which
    looks exactly like a broken effect.

    The driver path is the field immediately after.
    """
    try:
        slots = int(fields[68] or 0)
    except (IndexError, ValueError):
        slots = 0
    return 154 + slots * 11
