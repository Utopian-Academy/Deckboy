#!/usr/bin/env python3
"""Package the Companion module so somebody can actually install it.

Companion's official module list is not open to us yet: Bitfocus require module
SOURCE to be MIT so it stays portable, and Deckboy's is GPL-3.0-or-later. That
is the owner's decision to make, not something to work around -- see the note
in .github/workflows/build.yml.

Meanwhile Companion has a second, entirely legitimate route: the developer
modules folder. Point Companion at a directory and it loads what is in it. That
needs no packager and no approval, and until today the instruction for a
Companion user was effectively "clone the repository and work it out", which is
a wall in front of exactly the people the module is for.

This builds the thing that route wants: the module, its manifest, and its
PRODUCTION dependencies, in one zip that unpacks and runs. The dev
dependencies are left out -- they are the build tooling, they are most of the
weight, and a show machine has no use for them.

Run:  python tools/pack_companion_module.py
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MODULE = ROOT / "companion-module-utopianacademy-deckboy"
DIST = MODULE / "dist"

# What a running module needs. Tests and build tooling are deliberately absent.
SHIP_FILES = ["package.json", "package-lock.json", "main.js", "README.md"]
SHIP_DIRS = ["src", "companion"]


def main():
    if not MODULE.exists():
        print("no module directory at %s" % MODULE)
        return 1

    version = json.loads((MODULE / "package.json").read_text(encoding="utf-8"))["version"]
    name = "Deckboy-companion-module-%s" % version

    stage_root = Path(tempfile.mkdtemp(prefix="deckboy-companion-"))
    stage = stage_root / name
    stage.mkdir(parents=True)

    for item in SHIP_FILES:
        src = MODULE / item
        if src.exists():
            shutil.copy2(src, stage / item)
    for item in SHIP_DIRS:
        src = MODULE / item
        if src.exists():
            shutil.copytree(src, stage / item)

    # Production dependencies only. `npm ci` rather than `install` so the zip
    # matches the lockfile the tests ran against.
    npm = shutil.which("npm") or shutil.which("npm.cmd")
    if not npm:
        print("npm is needed to stage the module's dependencies")
        return 1
    result = subprocess.run(
        [npm, "ci", "--omit=dev", "--no-audit", "--no-fund"],
        cwd=str(stage), capture_output=True, text=True, shell=False)
    if result.returncode != 0 or not (stage / "node_modules").exists():
        print("npm ci failed:\n" + (result.stderr or result.stdout)[-1500:])
        return 1

    DIST.mkdir(parents=True, exist_ok=True)
    archive = DIST / (name + ".zip")
    if archive.exists():
        archive.unlink()

    count = 0
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
        for folder, _, files in os.walk(stage):
            for file_name in files:
                full = Path(folder) / file_name
                bundle.write(full, str(full.relative_to(stage_root)))
                count += 1
        bundle.writestr(name + "/INSTALL.txt", install_notes(version))

    shutil.rmtree(stage_root, ignore_errors=True)
    print("packed %s  (%d files, %.1f MB)"
          % (archive.relative_to(ROOT), count + 1,
             archive.stat().st_size / 1048576))
    return verify(archive, name)


def verify(archive, name):
    """Unpack what we just built and check it is loadable.

    A module zip that is missing a dependency, or whose manifest points at
    a file that is not there, fails silently: Companion just does not list
    it, and the person who downloaded it concludes the module is broken
    rather than the package. Cheaper to find here.

    What cannot be checked without Companion itself is the IPC handshake --
    main.js calls runEntrypoint, which needs Companion's environment, so
    importing it standalone is SUPPOSED to fail. The sources it pulls in are
    checked instead.
    """
    node = shutil.which("node") or shutil.which("node.exe")
    if not node:
        print("  (node not found -- skipping the load check)")
        return 0

    check_root = Path(tempfile.mkdtemp(prefix="deckboy-companion-check-"))
    try:
        with zipfile.ZipFile(archive) as bundle:
            bundle.extractall(check_root)
        unpacked = check_root / name

        manifest = json.loads(
            (unpacked / "companion" / "manifest.json").read_text(encoding="utf-8"))
        entry = (unpacked / "companion" / manifest["runtime"]["entrypoint"]).resolve()
        if not entry.exists():
            print("  FAIL the manifest entrypoint %s is not in the zip"
                  % manifest["runtime"]["entrypoint"])
            return 1
        print("  ok   manifest entrypoint resolves to %s" % entry.name)

        sources = sorted(str(f.relative_to(unpacked)).replace("\\", "/")
                         for f in (unpacked / "src").glob("*.js"))
        script = ("Promise.all([%s].map(f => import(f))).then("
                  "() => console.log('loaded'), "
                  "e => { console.error(e.message); process.exit(1); });"
                  % ", ".join("'./%s'" % f for f
                              in ["node_modules/@companion-module/base/dist/index.js"]
                                 + sources))
        result = subprocess.run([node, "--input-type=module", "-e", script],
                                cwd=str(unpacked), capture_output=True, text=True)
        if result.returncode != 0:
            print("  FAIL the packaged module does not load:")
            print("       " + (result.stderr or result.stdout).strip()[:400])
            return 1
        print("  ok   %d source files and @companion-module/base load from the zip"
              % len(sources))
        return 0
    finally:
        shutil.rmtree(check_root, ignore_errors=True)


def install_notes(version):
    return "\n".join([
        "Deckboy for Bitfocus Companion, version " + version,
        "=" * 52,
        "",
        "Companion loads modules from a developer modules folder. Point it at",
        "the folder this zip unpacks into and the module appears in the list.",
        "",
        "  1. Unzip this somewhere permanent -- not Downloads, because",
        "     Companion reads it every time it starts.",
        "  2. In Companion, open Settings, then Developer modules path, and",
        "     choose the folder CONTAINING this one.",
        "  3. Restart Companion. Add a connection and search for Deckboy.",
        "",
        "In Deckboy, the control port is in Settings under Network. The module",
        "asks for that host and port, polls STATUS, and drives transport, cue",
        "tally, output health and countdowns.",
        "",
        "Everything the module needs is in here already; there is nothing to",
        "install and no build step.",
        "",
        "https://utopian-academy.github.io/Deckboy/",
        "https://github.com/Utopian-Academy/Deckboy",
    ]) + "\n"


if __name__ == "__main__":
    sys.exit(main())
