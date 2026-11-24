#!/usr/bin/env python3
"""The site and the README must name the newest RELEASED version.

The site's download button, its structured data and its "What's new in"
heading name a version. Twice (0.99.428, 0.99.429) a release went out and the
site still named the one before. This check fails when VERSION changes and
docs/index.html does not, so the site is updated with the release.
"""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
# THE NEWEST RELEASED VERSION, not VERSION. VERSION moves to the next number
# as soon as work on it starts; the site must keep naming the release people
# can download until that next one is tagged (2026-11-24: the site said
# "Download v0.99.430" and linked a release page that did not exist). A
# release dates its CHANGES heading ("## <date> - v<version> (...)"), and that
# is the moment the site has to follow.
changes = (root / "CHANGES.md").read_text(encoding="utf-8")
released = re.search(r"^## \d{4}-\d{2}-\d{2} - v([0-9.]+)", changes, re.M)
if not released:
    print("CHANGES.md: no dated release heading found")
    sys.exit(1)
version = released.group(1)
page = (root / "docs" / "index.html").read_text(encoding="utf-8")

checks = {
    "structured data softwareVersion": r'"softwareVersion":\s*"([^"]+)"',
    "download button": r'class="btn-large">Download v([0-9.]+)<',
    "What's new in heading": r'id="latest-title">What&rsquo;s new in ([0-9.]+)<',
}
bad = 0
for name, pattern in checks.items():
    m = re.search(pattern, page)
    if not m:
        print(f"docs/index.html: no {name} found")
        bad += 1
    elif m.group(1) != version:
        print(f"docs/index.html: {name} says {m.group(1)}, the newest release is {version}")
        bad += 1
# THE README IS A PUBLIC PAGE TOO (GitHub's front page for the project). Its
# "What's new in" names the newest release and links its release page; naming
# an unreleased version links a page that does not exist (2026-11-24).
readme = (root / "README.md").read_text(encoding="utf-8")
m = re.search(r"^### What's new in ([0-9.]+)", readme, re.M)
if not m:
    print("README.md: no \"### What's new in\" heading found")
    bad += 1
elif m.group(1) != version:
    print(f"README.md: \"What's new in\" says {m.group(1)}, the newest release is {version}")
    bad += 1
for tag in re.findall(r"releases/tag/v([0-9.]+)", readme + page):
    if tag not in changes:
        print(f"a release link names v{tag}, which has no CHANGES heading")
        bad += 1
    elif not re.search(r"^## \d{4}-\d{2}-\d{2} - v" + re.escape(tag) + r"\b", changes, re.M):
        print(f"a release link names v{tag}, which is not released (no dated CHANGES heading)")
        bad += 1
if bad:
    print("README.md and docs/index.html name only RELEASED versions; next-version text waits in CHANGES 'Unreleased' until the release.")
    sys.exit(1)
print(f"site names {version}: OK")
