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


DEFINES = re.compile(r"(--[a-zA-Z0-9_-]+)\s*:")
# A var() with a fallback still renders something, so only a bare one counts.
USES = re.compile(r"var\(\s*(--[a-zA-Z0-9_-]+)\s*\)")


RULES = re.compile(r"([^{}]+)\{([^{}]*)\}", re.S)
CLASS_ATTR = re.compile(r'class="([^"]*)"')


def button_display(pages):
    """A class that looks like a button has to say what kind of box it is.

    `.btn-large` set padding and a border but never `display`, so it stayed
    inline. An inline box that wraps is drawn as two fragments, each with its
    own padding and its own border down the middle, and its vertical padding
    does not push the neighbouring lines apart -- which is what "the download
    button looks broken on a phone" turned out to be.

    Only the button written into a paragraph showed it. The others are flex
    items, and flex blockifies its children, so they were safe by accident of
    their parent. That is not safety: one of them copied into prose breaks the
    same way. So every btn* class is checked, wherever it happens to sit.
    """
    stylesheet = DOCS / "style.css"
    shared_text = stylesheet.read_text(encoding="utf-8") if stylesheet.exists() else ""

    def declared(css_text):
        """Classes the stylesheet gives a display to, and ones it styles at all."""
        with_display, styled = set(), set()
        for selector, body in RULES.findall(css_text):
            names = set(re.findall(r"\.(btn[a-zA-Z0-9_-]*)", selector))
            styled |= names
            if re.search(r"(^|[;{\s])display\s*:", body):
                with_display |= names
        return with_display, styled

    shared_display, shared_styled = declared(shared_text)

    problems = []
    for name in sorted(pages):
        text = (DOCS / name).read_text(encoding="utf-8", errors="replace")
        blocks = " ".join(re.findall(r"<style[^>]*>(.*?)</style>", text, re.S))
        page_display, page_styled = declared(blocks)
        with_display = shared_display | page_display
        styled = shared_styled | page_styled
        used = set()
        for attr in CLASS_ATTR.findall(text):
            used |= {c for c in attr.split() if c.startswith("btn")}
        for cls in sorted(used & styled - with_display):
            problems.append("%s uses .%s, which sets no display -- an inline "
                            "button splits its own border when the label wraps"
                            % (name, cls))
    return problems


def undefined_colours(pages):
    """Every var(--x) a page relies on has to be defined somewhere it can see.

    The palette was renamed once -- --dmg-* to --db-* -- and five pages and
    the manual generator went on asking for --dmg-dark. CSS does not complain
    about that: the declaration is simply dropped, so code chips, table rules
    and button backgrounds quietly rendered as nothing, and it shipped. This is
    the check that would have failed instead.

    A page can see what style.css defines plus what its own <style> blocks
    define. Uses are looked for in style.css, in each page's <style> blocks,
    and in inline style="" attributes, which are easy to forget.
    """
    stylesheet = DOCS / "style.css"
    shared_text = stylesheet.read_text(encoding="utf-8") if stylesheet.exists() else ""
    shared = set(DEFINES.findall(shared_text))

    problems = []
    for missing in sorted(set(USES.findall(shared_text)) - shared):
        problems.append("style.css uses %s, which nothing defines" % missing)

    for name in sorted(pages):
        text = (DOCS / name).read_text(encoding="utf-8", errors="replace")
        blocks = " ".join(re.findall(r"<style[^>]*>(.*?)</style>", text, re.S))
        inline = " ".join(re.findall(r'style="([^"]*)"', text))
        known = shared | set(DEFINES.findall(blocks))
        for missing in sorted(set(USES.findall(blocks + " " + inline)) - known):
            problems.append("%s uses %s, which neither it nor style.css defines"
                            % (name, missing))
    return problems


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

    problems.extend(undefined_colours(pages))
    problems.extend(button_display(pages))

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
