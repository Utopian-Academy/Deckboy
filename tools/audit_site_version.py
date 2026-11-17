#!/usr/bin/env python3
"""The site must name the version in VERSION.

The site's download button, its structured data and its "What's new in"
heading name a version. Twice (0.99.428, 0.99.429) a release went out and the
site still named the one before. This check fails when VERSION changes and
docs/index.html does not, so the site is updated with the release.
"""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
version = (root / "VERSION").read_text(encoding="utf-8").strip()
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
        print(f"docs/index.html: {name} says {m.group(1)}, VERSION is {version}")
        bad += 1
if bad:
    print("Update docs/index.html for this version (see the 'What's new in' section).")
    sys.exit(1)
print(f"site names {version}: OK")
