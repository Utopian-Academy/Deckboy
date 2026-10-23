#!/usr/bin/env python3
"""Bundle the published press assets. Run with --check to detect stale contents."""
import argparse
import io
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "docs/deckboy-press-kit.zip"
FILES = {
    "press-copy.txt": "docs/press-copy.txt",
    "images/desk-gameboy.png": "docs/images/readme/release-419/example-02-gameboy.png",
    "images/desk-dark.png": "docs/images/readme/release-419/example-02-dark.png",
    "images/desk-virtual-boy.png": "docs/images/readme/release-419/example-02-virtual-boy.png",
    "images/master-tracker.png": "docs/images/readme/master-tracker.png",
    "images/phone-controller.png": "docs/images/readme/phone-controller.png",
    "images/social-card.png": "docs/images/social-card.png",
    "demos/live-effects.gif": "docs/images/readme/release-419/live-effects.gif",
    "demos/picture-scopes.gif": "docs/images/readme/release-419/picture-scopes.gif",
    "demos/dmx-by-fixture.gif": "docs/images/readme/dmx-by-fixture.gif",
    "demos/mic-inspector.gif": "docs/images/readme/mic-inspector.gif",
    "CAPTURE-NOTES.md": "docs/images/readme/README.md",
    "LICENSE.txt": "LICENSE",
}
README = """DECKBOY PRESS KIT

Website: https://utopian-academy.github.io/Deckboy/
Download: https://github.com/Utopian-Academy/Deckboy/releases/latest
Source: https://github.com/Utopian-Academy/Deckboy
More images, MP4 clips and trailer: https://utopian-academy.github.io/Deckboy/press.html

CONTENTS AND CAPTIONS
press-copy.txt: product facts, short descriptions and workflow copy. The date
and release used to check the feature claims are at the top of that file.
images/desk-*.png: v0.99.419, three themes, the bundled layers and titles example.
images/master-tracker.png: v0.99.417 pattern tracker, sections and action cells.
images/phone-controller.png: complete v0.99.417 phone controller.
images/social-card.png: 1280 x 640 sharing card with the current desk capture.
demos/: v0.99.419 effects and scopes; earlier fixture and Mic captures.
CAPTURE-NOTES.md: dates, source versions and descriptions for each capture.

CREDIT AND REUSE
Deckboy project artwork and product copy: Deckboy Contributors,
GPL-3.0-or-later; the licence text is included as LICENSE.txt.
Suggested credit: Deckboy / Utopian Academy.
Source files: https://github.com/Utopian-Academy/Deckboy/tree/main/docs

The included captures use Deckboy-generated material and the bundled examples.
The Mic demo explicitly uses a test signal. No third-party film or music is
bundled. Other images and the trailer on the press page have their own credits
and terms; those terms are not replaced by this kit.
"""


def contents():
    result = {name: (ROOT / path).read_bytes() for name, path in FILES.items()}
    # Git may check out text as CRLF on Windows. Keep the archive portable.
    result = {name: data.replace(b"\r\n", b"\n") if name.endswith((".txt", ".md")) else data
              for name, data in result.items()}
    result["README.txt"] = README.encode("utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = contents()
    if args.check:
        try:
            with zipfile.ZipFile(DEST) as archive:
                valid = (len(archive.namelist()) == len(expected)
                         and set(archive.namelist()) == set(expected)
                         and all(archive.read(name) == data for name, data in expected.items()))
        except (OSError, zipfile.BadZipFile, KeyError):
            valid = False
        if not valid:
            raise SystemExit("Press kit is stale: run python tools/build_press_kit.py")
        print(f"Press kit matches all {len(expected)} source files")
        return
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for name, data in sorted(expected.items()):
            entry = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)
    DEST.write_bytes(buffer.getvalue())
    print(f"Wrote {DEST.name}: {DEST.stat().st_size:,} bytes, {len(expected)} files")


if __name__ == "__main__":
    main()
