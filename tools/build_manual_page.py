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
            <a href="slides.html">Slides</a>
            <a href="compare.html">Compare</a>
            <a href="faq.html">FAQ</a>
            <a href="latency.html">Latency</a>
            <a href="test-patterns.html">Patterns</a>
            <a href="led-wall.html">Generator</a>
            <a href="databend.html">Databend</a>
            <a href="screen-check.html">Screen check</a>
            <a href="https://github.com/Utopian-Academy/Deckboy">GitHub</a>
            <a href="https://github.com/Utopian-Academy/Deckboy/releases" class="btn-primary">Download Free</a>
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
            <a href="https://github.com/Utopian-Academy/Deckboy/releases" class="btn-large">Download Latest Release</a>
            <a href="./" class="btn-large btn-outline">Back to Deckboy</a>
        </div>
    </div>

    <footer>
        <p>Built as an open-source project by Utopian-Academy.
        <a href="https://github.com/Utopian-Academy/Deckboy">Contribute on GitHub</a>.</p>
    </footer>
</body>
</html>
"""


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
