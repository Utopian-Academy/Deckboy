#!/usr/bin/env python3
"""Render MANUAL.md as the manual page on the Pages site.

The manual is the largest piece of writing Deckboy has and the part people
actually search for -- "send NDI to SDI", "loop a clip on a projector",
"SMPTE 2110 playout" -- and until now it existed only as raw markdown on
GitHub, which a search engine treats as a code file rather than a document.

This generates docs/manual.html from it, so the page follows the manual
instead of being a copy that goes stale. Run it after editing MANUAL.md:

    python tools/build_manual_page.py

There is no markdown library on the build machines and this needs no
dependency, so the subset the manual actually uses is handled here and
nothing else: headings, bullets, numbered lists, tables, rules, fenced code,
one blockquote, and inline bold / code / links.
"""

import html
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "MANUAL.md"
TARGET = ROOT / "docs" / "manual.html"
SITE = "https://utopian-academy.github.io/Deckboy/"


def slug(text):
    """An anchor from a heading, with the chapter number left off.

    Numbers move when a chapter is inserted; the words do not. An anchor that
    survives a renumbering is one that can be linked to from outside.
    """
    text = re.sub(r"^\d+[a-z]?\.\s*", "", text)
    text = re.sub(r"[^a-z0-9]+", "-", text.lower())
    return text.strip("-")


def inline(text):
    """Inline markdown, applied to already-escaped text."""
    text = html.escape(text, quote=False)
    text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"<em>\1</em>", text)
    return text


def table_cells(row, tag):
    parts = row.strip().strip("|").split("|")
    return "".join("<{0}>{1}</{0}>".format(tag, inline(c.strip())) for c in parts)


def convert(lines):
    """Markdown lines to HTML blocks, plus the chapter list for the contents."""
    out = []
    chapters = []
    para = []
    bullets = []
    numbers = []
    table = []

    def flush():
        if para:
            out.append("<p>" + inline(" ".join(para)) + "</p>")
            del para[:]
        if bullets:
            out.append("<ul>" + "".join(
                "<li>" + inline(b) + "</li>" for b in bullets) + "</ul>")
            del bullets[:]
        if numbers:
            out.append("<ol>" + "".join(
                "<li>" + inline(b) + "</li>" for b in numbers) + "</ol>")
            del numbers[:]
        if table:
            rows = [r for r in table if not re.match(r"^\|[\s:\-|]+\|$", r)]
            head, body = rows[0], rows[1:]
            out.append(
                '<div class="table-scroll"><table><thead><tr>'
                + table_cells(head, "th")
                + "</tr></thead><tbody>"
                + "".join("<tr>" + table_cells(r, "td") + "</tr>" for r in body)
                + "</tbody></table></div>")
            del table[:]

    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]

        if line.startswith("```"):
            flush()
            i += 1
            code = []
            while i < n and not lines[i].startswith("```"):
                code.append(lines[i])
                i += 1
            out.append("<pre><code>"
                       + html.escape("\n".join(code), quote=False)
                       + "</code></pre>")
            i += 1
            continue

        if line.startswith("|"):
            if para or bullets or numbers:
                flush()
            table.append(line)
            i += 1
            continue
        if table:
            flush()

        heading = re.match(r"^(#{2,3})\s+(.*)$", line)
        if heading:
            flush()
            level = len(heading.group(1))
            title = heading.group(2).strip()
            anchor = slug(title)
            if level == 2:
                chapters.append((anchor, title))
                out.append('<h2 id="{0}"><a href="#{0}" class="anchor">{1}</a></h2>'
                           .format(anchor, inline(title)))
            else:
                out.append('<h3 id="{0}">{1}</h3>'.format(anchor, inline(title)))
            i += 1
            continue

        if re.match(r"^-{3,}\s*$", line):
            flush()
            i += 1
            continue

        bullet = re.match(r"^[-*]\s+(.*)$", line)
        if bullet:
            if para or numbers:
                flush()
            bullets.append(bullet.group(1))
            i += 1
            continue

        number = re.match(r"^\d+\.\s+(.*)$", line)
        if number:
            if para or bullets:
                flush()
            numbers.append(number.group(1))
            i += 1
            continue

        if line.startswith(">"):
            flush()
            out.append("<blockquote><p>" + inline(line.lstrip("> ")) + "</p></blockquote>")
            i += 1
            continue

        if not line.strip():
            flush()
            i += 1
            continue

        # A continuation line: the markdown wraps hard, the page should not.
        if bullets:
            bullets[-1] += " " + line.strip()
        elif numbers:
            numbers[-1] += " " + line.strip()
        else:
            para.append(line.strip())
        i += 1

    flush()
    return out, chapters


HEAD = """<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Deckboy Manual | Cue-based video playback and show control</title>
    <meta name="description" content="The complete Deckboy manual: cues and decks, transport and transitions, NDI, SRT, SDI and SMPTE ST 2110 outputs, warp and edge blend, per-cue effects, audio, timecode, themes, remote control and the full keyboard reference.">
    <link rel="canonical" href="SITEmanual.html">

    <meta property="og:type" content="article">
    <meta property="og:site_name" content="Deckboy">
    <meta property="og:title" content="Deckboy Manual">
    <meta property="og:description" content="Every part of Deckboy, written down: cues, decks, outputs, routing, effects, audio, timecode, themes and remote control.">
    <meta property="og:url" content="SITEmanual.html">
    <meta property="og:image" content="SITEimages/social-card.png">

    <meta name="twitter:card" content="summary_large_image">
    <meta name="twitter:title" content="Deckboy Manual">
    <meta name="twitter:image" content="SITEimages/social-card.png">

    <link rel="stylesheet" href="style.css">

    <style>
        .prose { max-width: 860px; margin: 0 auto 4rem; padding: 0 1.5rem; }
        .prose h2 { margin: 3rem 0 .7rem; font-size: 1.3rem; color: var(--ink);
                    scroll-margin-top: 5rem; }
        .prose h3 { margin: 2rem 0 .5rem; font-size: 1.05rem; color: var(--ink);
                    scroll-margin-top: 5rem; }
        .prose h2 .anchor { color: inherit; text-decoration: none; }
        .prose p, .prose li { color: var(--ink-soft); }
        .prose p { margin-bottom: 1rem; }
        .prose ul, .prose ol { margin: 0 0 1rem 1.25rem; }
        .prose li { margin-bottom: .4rem; }
        .prose code { background: var(--rule); padding: .12em .4em;
                      font-size: .9em; }
        .prose pre { background: var(--rule); padding: 1rem 1.1rem;
                     overflow-x: auto; margin-bottom: 1.25rem; }
        .prose pre code { background: none; padding: 0; font-size: .86em;
                          line-height: 1.55; }
        .prose blockquote { margin: 0 0 1rem; padding-left: 1rem;
                            border-left: 3px solid var(--rule); }
        .table-scroll { overflow-x: auto; margin-bottom: 1.25rem; }
        .prose table { border-collapse: collapse; width: 100%; font-size: .92rem; }
        .prose th, .prose td { text-align: left; padding: .45rem .7rem;
                               border-bottom: 1px solid var(--rule);
                               color: var(--ink-soft); vertical-align: top; }
        .prose th { color: var(--ink); white-space: nowrap; }
        .toc { display: grid; gap: .3rem .9rem; margin: 0 0 3rem;
               grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
               list-style: none; padding: 0; }
        .toc li { margin: 0; }
        .toc a { color: var(--ink-soft); text-decoration: none; font-size: .92rem; }
        .toc a:hover { color: var(--ink); }
        /* The Mini booklet: see booklet() in tools/build_manual_page.py. */
        .mini-hint { font-size: .85rem; }
        .mini-booklet { display: flex; flex-wrap: wrap; gap: 1.1rem; margin: .4rem 0 2rem; }
        .mini-page { position: relative; flex: 0 0 150px; height: 210px; box-sizing: border-box;
                     padding: 10px 9px 16px; overflow: hidden; cursor: zoom-in;
                     background: #9bbc0f; color: #0f380f; border: 3px solid #0f380f;
                     box-shadow: 4px 4px 0 #306230; font-family: 'Deckboy Pixel', monospace;
                     font-size: 4.4px; line-height: 1.6; transition: transform .18s ease, box-shadow .18s ease; }
        .mini-page:hover, .mini-page:focus { transform: scale(2.3); z-index: 5; outline: none;
                     box-shadow: 2px 2px 0 #306230; overflow: auto; }
        .mini-booklet .mini-page p, .mini-booklet .mini-page li, .mini-booklet .mini-page td,
        .mini-booklet .mini-page th { color: #0f380f; margin: 0 0 4px; font-size: 1em; line-height: 1.6; }
        .mini-booklet .mini-page code, .mini-booklet .mini-page pre { background: none; padding: 0;
                     font-family: inherit; font-size: 1em; color: #0f380f; }
        .mini-booklet .mini-page pre { white-space: pre-wrap; margin: 0 0 4px; }
        .mini-booklet .mini-page pre code { font-size: 1em; line-height: 1.7; }
        .mini-booklet .mini-page .table-scroll { margin: 0; overflow: visible; }
        .mini-booklet .mini-page table { font-size: 1em; }
        .mini-booklet .mini-page th, .mini-booklet .mini-page td { padding: 1px 2px; border-bottom: 1px solid #8bac0f; }
        .mini-booklet .mini-page a { color: #0f380f; }
        .mini-title { font-size: 5.6px !important; text-transform: uppercase; border-bottom: 1px solid #0f380f;
                      padding-bottom: 3px; margin-bottom: 6px !important; }
        .mini-folio { position: absolute; bottom: 0; left: 0; right: 0; text-align: center; font-size: 4.4px;
                      padding: 3px 0 4px; background: #9bbc0f; }
        .mini-cover .mini-folio { background: #0f380f; }
        .mini-page { overflow-wrap: anywhere; }
        .mini-booklet .mini-page td:first-child { white-space: nowrap; overflow-wrap: normal; }
        .mini-cover { background: #0f380f; border-color: #0f380f; text-align: center; padding-top: 22px; }
        .mini-booklet .mini-cover p, .mini-cover .mini-folio { color: #9bbc0f; }
        .mini-cart { position: relative; display: block; width: 44px; height: 52px; margin: 0 auto 14px;
                     background: #8bac0f; clip-path: polygon(0 0, 76% 0, 100% 13%, 100% 100%, 0 100%); }
        .mini-cart::before { content: ""; position: absolute; left: 5px; right: 14px; top: 4px; height: 5px;
                     background: repeating-linear-gradient(90deg, #306230 0 2px, transparent 2px 4px); }
        .mini-cart::after { content: ""; position: absolute; left: 6px; right: 6px; top: 14px; bottom: 11px;
                     background: #9bbc0f; border: 2px solid #306230; }
        .mini-booklet .mini-cover .mini-name { font-size: 11px; line-height: 1.5; margin-bottom: 10px; }
        .mini-booklet .mini-cover .mini-sub { font-size: 4.8px; margin-bottom: 18px; }
        .mini-booklet .mini-cover .mini-tag { font-size: 4.4px; color: #8bac0f; }
        .mini-stage { height: 214px; margin: .4rem 0 0; transition: height .3s ease; }
        .mini-stage.zoomed { height: 492px; }
        .mini-booklet.is-flipbook { display: block; position: relative; width: 150px; height: 210px; margin: 0;
                     perspective: 900px; transform-origin: top left; transition: transform .3s ease; }
        .mini-stage.zoomed .mini-booklet { transform: scale(2.3); }
        .is-flipbook:focus { outline: 2px dashed #306230; outline-offset: 6px; }
        .is-flipbook .mini-page { position: absolute; inset: 0; margin: 0; cursor: pointer;
                     transform-origin: left center; backface-visibility: hidden;
                     transition: transform .55s ease; }
        .is-flipbook .mini-page:hover, .is-flipbook .mini-page:focus { transform: none; overflow: hidden;
                     box-shadow: 4px 4px 0 #306230; }
        .is-flipbook .mini-page.turned { transform: rotateY(-178deg); pointer-events: none; }
        .mini-controls { display: flex; gap: .6rem; align-items: center; margin: .5rem 0 2rem;
                     font-family: 'Deckboy Pixel', monospace; font-size: 9px; color: var(--ink-soft); }
        .mini-controls button { font: inherit; background: #0f380f; color: #9bbc0f; border: 2px solid #306230;
                     padding: 5px 8px; cursor: pointer; }
        .mini-controls button:hover { background: #306230; }
    </style>

    <script type="application/ld+json">
    {
      "@context": "https://schema.org",
      "@type": "TechArticle",
      "headline": "Deckboy Manual",
      "description": "The complete operating manual for Deckboy, an open-source cue-based media playback and show control application for live video.",
      "url": "SITEmanual.html",
      "inLanguage": "en",
      "about": {
        "@type": "SoftwareApplication",
        "name": "Deckboy",
        "applicationCategory": "MultimediaApplication",
        "operatingSystem": "Windows 10, Windows 11, macOS 12, Linux",
        "url": "SITE"
      },
      "author": { "@type": "Organization", "name": "Utopian Academy" },
      "license": "https://www.gnu.org/licenses/gpl-3.0.html"
    }
    </script>
</head>
<body>
    <div class="background-glow"></div>

    <nav class="navbar">
        <div class="logo"><a href="./" style="color:inherit;text-decoration:none">Deckboy</a></div>
        <div class="nav-links">
            <a href="trailer.html">Trailer</a>
            <a href="compare.html">Compare</a>
            <a href="manual.html">Manual</a>
            <a href="faq.html">FAQ</a>
            <a href="specs.html">Specs</a>
            <a href="https://github.com/Utopian-Academy/Deckboy">GitHub</a>
            <a href="https://github.com/Utopian-Academy/Deckboy/releases/latest" class="btn-primary">Download Free</a>
        </div>
    </nav>

    <header class="hero" style="min-height:auto;padding-top:4rem;padding-bottom:1rem">
        <div class="hero-content">
            <h1>Manual</h1>
            <p class="hero-subtitle">Everything Deckboy does, and how to make it do it.</p>
        </div>
    </header>

    <div class="prose">
        <ul class="toc">
TOC
        </ul>

BODY

        <div class="cta-group" style="justify-content:flex-start;margin-top:3rem">
            <a href="https://github.com/Utopian-Academy/Deckboy/releases/latest" class="btn-large">Download Latest Release</a>
            <a href="./" class="btn-large btn-outline">Back to Deckboy</a>
        </div>
    </div>

    <footer>
        <div class="footer-cols">
            <div>
                <h4>Deckboy</h4>
                <a href="./">What it does</a>
                <a href="trailer.html">The trailer</a>
                <a href="compare.html">How it compares</a>
                <a href="qlab-alternative.html">Coming from QLab</a>
                <a href="https://github.com/Utopian-Academy/Deckboy/releases/latest">Download</a>
            </div>
            <div>
                <h4>Using it</h4>
                <a href="manual.html">Manual</a>
                <a href="faq.html">FAQ</a>
                <a href="specs.html">Specifications</a>
                <a href="slides.html">Slides &amp; presenting</a>
                <a href="window-cues.html">Window &amp; browser cues</a>
                <a href="ndi.html">NDI in and out</a>
                <a href="stream-deck.html">Stream Deck &amp; Companion</a>
                <a href="latency.html">Where latency hides</a>
            </div>
            <div>
                <h4>Tools</h4>
                <a href="test-patterns.html">Test patterns</a>
                <a href="led-wall.html">LED pattern generator</a>
                <a href="led-calculator.html">LED calculator</a>
                <a href="screen-check.html">Screen check</a>
                <a href="databend.html">Databend</a>
            </div>
            <div>
                <h4>Guides</h4>
                <a href="shooting-led.html">Shooting an LED wall</a>
                <a href="press.html">Press kit</a>
                <a href="https://github.com/Utopian-Academy/Deckboy">Source on GitHub</a>
            </div>
        </div>
        <p class="footer-note">Built as an open-source project by Utopian-Academy.
        Free under the GPL-3.0.</p>
    </footer>
    <script>
    // The Deckboy Mini booklet as a flipbook: the pages stack, a click turns one
    // (the left third turns back), the arrow keys work once it has focus, and
    // ZOOM makes the miniature readable. Without this, the pages are a grid.
    document.querySelectorAll('.mini-booklet').forEach(function (book) {
      var pages = Array.prototype.slice.call(book.querySelectorAll('.mini-page'));
      if (!pages.length) return;
      var stage = document.createElement('div');
      stage.className = 'mini-stage';
      book.parentNode.insertBefore(stage, book);
      stage.appendChild(book);
      book.classList.add('is-flipbook');
      book.tabIndex = 0;
      book.setAttribute('role', 'group');
      book.setAttribute('aria-label', 'Deckboy Mini instruction booklet');
      pages.forEach(function (page, i) { page.style.zIndex = pages.length - i; page.removeAttribute('tabindex'); });
      var bar = document.createElement('div');
      bar.className = 'mini-controls';
      bar.innerHTML = '<button type="button" data-go="-1">&lt; PREV</button><span class="mini-count"></span>' +
                      '<button type="button" data-go="1">NEXT &gt;</button><button type="button" data-zoom>ZOOM</button>';
      stage.parentNode.insertBefore(bar, stage.nextSibling);
      var count = bar.querySelector('.mini-count');
      var at = 0;
      function show() {
        pages.forEach(function (page, i) {
          page.classList.toggle('turned', i < at);
          page.setAttribute('aria-hidden', i === at ? 'false' : 'true');
        });
        count.textContent = (at + 1) + ' / ' + pages.length;
      }
      function go(step) { at = Math.max(0, Math.min(pages.length - 1, at + step)); show(); }
      bar.addEventListener('click', function (e) {
        var button = e.target.closest('button');
        if (!button) return;
        if (button.hasAttribute('data-zoom')) {
          stage.classList.toggle('zoomed');
          button.textContent = stage.classList.contains('zoomed') ? 'SHRINK' : 'ZOOM';
        } else {
          go(parseInt(button.getAttribute('data-go'), 10));
        }
      });
      book.addEventListener('click', function (e) {
        var box = book.getBoundingClientRect();
        go(e.clientX - box.left < box.width / 3 ? -1 : 1);
      });
      book.addEventListener('keydown', function (e) {
        if (e.key === 'ArrowRight') { go(1); e.preventDefault(); }
        if (e.key === 'ArrowLeft') { go(-1); e.preventDefault(); }
      });
      var hint = stage.previousElementSibling;
      if (hint && hint.classList.contains('mini-hint')) {
        hint.textContent = 'A miniature manual for a miniature player. Click a page to turn it, or ZOOM to read it.';
      }
      show();
    });
    </script>
</body>
</html>
"""


# THE MINI BOOKLET. Deckboy Mini's section of the manual is drawn as what it
# is: a tiny cartridge instruction booklet, a strip of pocket-sized pages in
# LCD greens. The text really is miniature; hovering, focusing or tapping a
# page magnifies it to a readable size. MANUAL.md is still the only source --
# this only changes how that one section is laid out on the page.
MINI_SECTION = "deckboy-mini"


def booklet(blocks):
    marker = '<h3 id="{0}">'.format(MINI_SECTION)
    start = next((i for i, b in enumerate(blocks) if b.startswith(marker)), None)
    if start is None:
        return blocks
    end = start + 1
    while end < len(blocks) and not blocks[end].startswith(("<h2", "<h3")):
        end += 1
    pages = []
    for number, block in enumerate(blocks[start + 1:end], start=2):
        lead = re.match(r"<p><strong>(.*?)</strong>", block)
        if lead:
            title = re.sub(r"<[^>]+>", "", lead.group(1)).rstrip(".,: ")
        elif block.startswith("<pre"):
            title = "Inserting the cartridge"
        elif "<table" in block:
            title = "Options"
        elif number == 2:
            title = "About this cartridge"
        else:
            title = "Notes"
        pages.append('<section class="mini-page" tabindex="0" aria-label="Page {0}: {1}">'
                     '<p class="mini-title">{1}</p>{2}<span class="mini-folio">{0}</span></section>'
                     .format(number, html.escape(title), block))
    cover = ('<section class="mini-page mini-cover" tabindex="0" aria-label="Cover">'
             '<span class="mini-cart" aria-hidden="true"></span>'
             '<p class="mini-name">DECKBOY<br>MINI</p>'
             '<p class="mini-sub">INSTRUCTION BOOKLET</p>'
             '<p class="mini-tag">ONE DECK &middot; ONE OUTPUT</p>'
             '<span class="mini-folio">1</span></section>')
    hint = '<p class="mini-hint">A miniature manual for a miniature player. Hover or tap a page to read it.</p>'
    return (blocks[:start + 1] + [hint, '<div class="mini-booklet">' + cover + "".join(pages) + "</div>"]
            + blocks[end:])


def main():
    lines = SOURCE.read_text(encoding="utf-8").splitlines()

    # Drop the title and the manual's own hand-written contents list; the page
    # builds its own from the headings, so the two cannot drift apart.
    start = 0
    for index, line in enumerate(lines):
        if re.match(r"^##\s+1\.", line):
            start = index
            break
    if not start:
        print("could not find chapter 1 -- has the manual's shape changed?")
        return 1

    body, chapters = convert(lines[start:])
    body = booklet(body)
    toc = "\n".join('            <li><a href="#{0}">{1}</a></li>'
                    .format(a, html.escape(t)) for a, t in chapters)
    page = (HEAD.replace("SITE", SITE)
                .replace("TOC", toc)
                .replace("BODY", "\n".join("        " + b for b in body)))
    TARGET.write_text(page, encoding="utf-8", newline="\n")
    print("wrote {0} -- {1} chapters, {2} KB"
          .format(TARGET.relative_to(ROOT), len(chapters), len(page) // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
