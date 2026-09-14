#!/usr/bin/env python3
"""Fail if the Pages site links to something that is not there.

The site is now six pages that link to each other, to anchors inside the
manual, and to images. A broken link on a landing page is worse than a missing
page: the visitor arrived, which is the expensive part, and then left.

This checks what can be checked without the network -- every local href and
src, and every #anchor against the ids actually present in the target page.
External links are listed but not fetched, because a build that fails when
somebody else's site is slow is a build nobody trusts.

Run:  python tools/check_site_links.py
"""

import re
import sys
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"


class Page(HTMLParser):
    """The links a page makes and the ids it offers."""

    def __init__(self):
        super().__init__()
        self.links = []   # (attr value, kind)
        self.ids = set()

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.add(attrs["id"])
        if "name" in attrs and tag == "a":
            self.ids.add(attrs["name"])
        for key, kind in (("href", "href"), ("src", "src")):
            value = attrs.get(key)
            if value:
                self.links.append((value, kind))


def load(path):
    page = Page()
    page.feed(path.read_text(encoding="utf-8", errors="replace"))
    return page


def main():
    pages = {p.name: load(p) for p in sorted(DOCS.glob("*.html"))}
    if not pages:
        print("no pages found in docs/ -- has the site moved?")
        return 1

    problems = []
    external = set()
    checked = 0

    for name, page in pages.items():
        for value, kind in page.links:
            if value.startswith(("http://", "https://", "mailto:", "data:")):
                external.add(value)
                continue
            target, _, fragment = value.partition("#")
            checked += 1

            if not target:                      # same-page anchor
                if fragment and fragment not in page.ids:
                    problems.append("%s: #%s is not on this page" % (name, fragment))
                continue

            resolved = (DOCS / target).resolve() if target != "./" else DOCS
            if target == "./":
                resolved = DOCS / "index.html"
            if not resolved.exists():
                problems.append("%s: %s -> %s is missing" % (name, kind, target))
                continue

            if fragment and resolved.suffix == ".html":
                other = pages.get(resolved.name) or load(resolved)
                if fragment not in other.ids:
                    problems.append("%s: %s#%s -- no such anchor in %s"
                                    % (name, target, fragment, resolved.name))

    # Everything in the sitemap has to exist, and everything that exists and is
    # not a redirect ought to be in the sitemap -- an unlisted page is a page
    # the crawler finds late or not at all.
    sitemap = DOCS / "sitemap.xml"
    if sitemap.exists():
        listed = set()
        for loc in re.findall(r"<loc>([^<]+)</loc>", sitemap.read_text(encoding="utf-8")):
            tail = loc.rstrip("/").rsplit("/", 1)[-1]
            listed.add("index.html" if not tail.endswith(".html") else tail)
        for missing in sorted(listed - set(pages)):
            problems.append("sitemap.xml lists %s, which is not in docs/" % missing)
        for unlisted in sorted(set(pages) - listed):
            problems.append("%s is not in sitemap.xml" % unlisted)

    print("pages: %d   local links checked: %d   external (not fetched): %d"
          % (len(pages), checked, len(external)))
    if problems:
        print()
        for line in problems:
            print("  " + line)
        print()
        print("A visitor who arrived and then hit a dead link is the most "
              "expensive kind to lose.")
        return 1
    print("every local link and anchor resolves, and the sitemap matches")
    return 0


if __name__ == "__main__":
    sys.exit(main())
