#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
# This file is part of Deckboy, a cue deck for live events.
# See LICENSE for details.
"""Translate every interface string a full catalogue is missing.

WHY THIS EXISTS. New text kept reaching the desk in English: a toast, a
settings row, a line the code builds at run time. Nobody had to forget
anything -- a new string is simply not in thirty-four files until somebody
writes it there. James, 2026-11-08: "if new text is entered for any reason,
lets auto translate it to all to avoid this in the future". So this finds the
strings the catalogues lack and has Claude translate them, under the same
rules a person would follow, and the pre-commit hook (tools/hooks/pre-commit)
runs it on whatever a commit adds.

WHAT IT TRANSLATES. Every string audit_i18n.py finds drawn (the draw helpers,
toasts, i18n::format templates, menu labels) that a full catalogue cannot show
-- by its own line, or by the rules translate() uses to take a built string
apart (tools/i18n_pieces.py) -- and is not on tools/i18n_keep_english.txt.
--misses adds the pieces of a running app's misses (DECKBOY_I18N_MISSES);
--keys takes a list.

HOW. `claude -p`, Claude Code run without tools, on the login this machine
already has: no API key, nothing billed beyond the plan. Each answer is checked
before it is written (placeholders, key names and trade words kept, no tabs, no
line equal to its key) and goes in through add_translations.py. A string the
translator says stays English is recorded on the keep list, never written into
a catalogue as an identical line.

    python tools/i18n_translate.py --dry-run            # what is missing
    python tools/i18n_translate.py                      # translate all of it
    python tools/i18n_translate.py --staged             # what the staged diff adds (the hook)
    python tools/i18n_translate.py --langs de,fr --limit 40
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
LANG_DIR = ROOT / "data" / "lang"
KEEP_FILE = HERE / "i18n_keep_english.txt"
sys.path.insert(0, str(HERE))
import audit_i18n  # noqa: E402
import i18n_pieces  # noqa: E402

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

# The languages Deckboy ships whole. The word lists (Scots, Klingon, Sindarin,
# Mohawk, Navajo...) and the cyphers are not translated wholesale.
FULL = ("ar bg ckb da de el es-CU es fa fi fr gd haw hi is it ja kab kmr ko lo lum nl "
        "pap-AW pl pt-BR ru tr uk vi zgh zh-Hans zh-Hant zu").split()

# James's rules for every translation (private-notes/codex/2026-11-07-i18n-missing.md).
TRADE = ["TAKE ALL", "TAKE", "PROGRAM", "NDI", "SRT", "RTMP", "OSC", "MIDI", "LTC", "MTC", "ATEM",
         "TSL", "Art-Net", "DMX", "HAP", "DNxHR", "MXF", "MOV", "AVI", "GIF", "PNG", "WAV",
         "FFmpeg", "libav", "SDL", "FreeType", "GPL", "PTP", "Spout", "HDR", "VU", "dB",
         "Deckboy", "SUPER DECKBOY", "Companion", "Bug Swat", "Lower Third", "DeckLink",
         "LICENSE", "NVENC", "ProRes", "H.264", "H.265", "ASIO", "Syphon", "SMPTE", "DASH"]
KEY_NAMES = ["Enter", "Space", "Esc", "Tab", "Ctrl", "Shift", "Alt", "Cmd", "Del", "Ins",
             "PgUp", "PgDn", "Home", "End", "Backspace"] + [f"F{i}" for i in range(1, 13)]
PLACEHOLDER = re.compile(r"\{(?:n|[1-9])\}|%[-0-9.]*[sdfxu]")

RULES = """You translate the interface of Deckboy, a cue deck for live events (video
playback for theatre, broadcast and events). Each English string is a label,
button, menu item, toast or a line from a tutorial. Rules, all binding:

1. Trade words stay English: TAKE, TAKE ALL, PROGRAM, NDI, SRT, RTMP, OSC, MIDI,
   LTC, MTC, ATEM, TSL, Art-Net, DMX, HAP, DNxHR, MXF, MOV, AVI, GIF, PNG, WAV,
   FFmpeg, libav, SDL, FreeType, GPL, PTP, Spout, HDR, VU, dB, codec and format
   names. Product names stay: Deckboy, SUPER DECKBOY, Companion, Bug Swat.
   Character names stay English. Write "Lower Third" exactly so, never translated.
2. If the whole string should stay English in this language (it is all trade
   words, a name, a unit, or the translation would be identical), answer "=".
3. Keep every placeholder exactly: {n}, {1}, {2} ... and %s %d. You may move
   {1}/{2} to where the grammar wants them. Keep every key name a user presses
   (Enter, Space, Esc, Tab, F1, and single letters like "S" or "A" that name a
   key) and keep punctuation that separates columns (two spaces, " | ", "->").
4. Match the catalogue's own words and conventions: the EXISTING LINES given for
   each language are binding vocabulary. When a string contains a word or a
   button name that has an existing line ("deck", "MENU", "OUTPUT OFF"), use
   that line's translation exactly, so a sentence names a button the way the
   button reads. In Deckboy a "deck" is a playlist of cues, never a ship's deck. ALL-CAPS English gets all-caps where the
   script has case. zgh is written in Tifinagh (Latin only for trade words);
   haw uses the ʻokina (U+02BB) and kahakō; lum is a constructed language --
   follow its grammar notes exactly, plain Latin letters only; lo is Lao script;
   ckb is Sorani in Arabic script; kmr is Kurmanji in Latin; es-CU is Cuban
   Spanish; pap-AW is Aruban Papiamento.
5. Short labels stay short: about 1.5x the English at most where the language
   allows. Plain words, active voice. No quotation marks around the answer.

Answer with one line per (language, id), nothing else, as
code<TAB>id<TAB>translation
"""


def catalogue(code):
    keys = {}
    header = []
    for line in open(LANG_DIR / f"{code}.tsv", encoding="utf-8"):
        line = line.rstrip("\r\n")
        if line.startswith("#"):
            header.append(line)
            continue
        if "\t" in line:
            k, v = line.split("\t", 1)
            if v.strip():
                keys.setdefault(k, v)
    return keys, header


def load_keep():
    keep = {}
    if KEEP_FILE.exists():
        for line in KEEP_FILE.read_text(encoding="utf-8").splitlines():
            if not line.strip() or line.startswith("#"):
                continue
            code, _, key = line.partition("\t")
            keep.setdefault(code, set()).add(key)
    return keep


def kept(keep, code, key):
    return key in keep.get("*", set()) or key in keep.get(code, set())


def candidate_keys(args):
    keys = {}
    if args.keys:
        for s in Path(args.keys).read_text(encoding="utf-8").splitlines():
            if s.strip():
                keys[s] = args.keys
    else:
        keys.update(audit_i18n.drawn_strings())
        for s, where in audit_i18n.option_labels().items():
            keys.setdefault(s, where)
    if args.misses:
        cat, _ = catalogue("bg")
        have = {k.lower() for k in cat}
        out = set()
        for s in Path(args.misses).read_text(encoding="utf-8").splitlines():
            if s.strip():
                i18n_pieces.pieces(s, have, out)
        for p in out:
            if i18n_pieces.interesting(p):
                keys.setdefault(p, "runtime")
    # C++ escapes as the catalogue holds them: a literal \" is a ".
    return {k.replace('\\"', '"').replace("\\\\", "\\"): v for k, v in keys.items()
            if "\\n" not in k and "\\t" not in k}


def staged_filter(keys):
    """Only strings the staged diff adds: what the hook translates."""
    diff = subprocess.run(["git", "diff", "--cached", "-U0", "--", "native"], cwd=ROOT,
                          capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
    added = "\n".join(l[1:] for l in diff.splitlines() if l.startswith("+") and not l.startswith("+++"))
    added = re.sub(r'"\s+"', "", added)
    return {k: v for k, v in keys.items() if '"' + k.replace("\\", "\\\\").replace('"', '\\"') + '"' in added}


def missing_for(code, keys, keep):
    cat, _ = catalogue(code)
    lowered = {k.lower() for k in cat}
    out = []
    for k in keys:
        if kept(keep, code, k):
            continue
        if audit_i18n.covered(k, cat, lowered) or i18n_pieces.pieces(k, lowered, set()):
            continue
        out.append(k)
    return out


def glossary(code, batch, limit=60):
    """Existing lines of this catalogue the batch should agree with.

    FIRST the terms the batch contains: a catalogue key that appears inside a
    batch string as a whole phrase, in any case -- "deck" in "Deck {n}",
    "OUTPUT OFF" inside a tutorial sentence. The first version only ranked lines
    by shared words, the exact term was crowded out, and the pilot run wrote
    "deck" as a ship's deck in three languages and left OUTPUT OFF in English
    beside a button that says ВЫХОД ВЫКЛ. Then lines sharing words fill the rest.
    """
    cat, header = catalogue(code)
    text = "\n".join(batch).lower()
    terms = []
    for k, v in cat.items():
        kl = k.lower()
        if 2 <= len(kl) <= 40 and re.search(r"(?<![a-z])" + re.escape(kl) + r"(?![a-z])", text):
            terms.append((-len(k), k, v))
    terms.sort()
    chosen = [(k, v) for _, k, v in terms[:limit]]
    seen = {k for k, _ in chosen}
    words = {w.lower() for k in batch for w in re.findall(r"[A-Za-z]{3,}", k)}
    scored = []
    for k, v in cat.items():
        if len(k) > 60 or k in seen:
            continue
        hits = len(words & {w.lower() for w in re.findall(r"[A-Za-z]{3,}", k)})
        if hits:
            scored.append((-hits, len(k), k, v))
    scored.sort()
    chosen += [(k, v) for _, _, k, v in scored[:max(0, limit - len(chosen))]]
    notes = [h for h in header if code == "lum" and h.startswith("#  ")]
    return notes, chosen


def problems(en, tr):
    if "\t" in tr or "\n" in tr:
        return "tab or newline"
    if tr != tr.strip():
        return "space at an end"
    if sorted(PLACEHOLDER.findall(en)) != sorted(PLACEHOLDER.findall(tr)):
        return "placeholders changed"
    for word in TRADE + KEY_NAMES:
        if re.search(r"(?<![A-Za-z])" + re.escape(word) + r"(?![A-Za-z])", en) and word not in tr:
            return f"lost {word!r}"
    if "  " in en and "  " not in tr:
        return "column spacing lost"
    return None


def ask(prompt, model):
    cmd = ["claude", "-p", "--tools", "", "--no-session-persistence", "--output-format", "text"]
    if model:
        cmd += ["--model", model]
    with tempfile.TemporaryDirectory() as cwd:      # nothing of the repo in reach
        run = subprocess.run(cmd, input=prompt, capture_output=True, text=True, encoding="utf-8",
                             cwd=cwd, timeout=900)
    if run.returncode != 0:
        raise RuntimeError(run.stderr.strip()[:300] or "claude -p failed")
    return run.stdout


def translate_chunk(langs, batch, model):
    lines = [RULES, "", "STRINGS (id<TAB>English):"]
    lines += [f"{i}\t{k}" for i, k in enumerate(batch)]
    for code in langs:
        _, header = catalogue(code)
        name = next((h[6:] for h in header if h.startswith("#name ")), code)
        notes, gloss = glossary(code, batch)
        lines += ["", f"LANGUAGE {code} ({name}). EXISTING LINES:"]
        lines += notes
        lines += [f"{k}\t{v}" for k, v in gloss]
    lines += ["", "Languages to answer for: " + ", ".join(langs),
              f"Answer every id 0-{len(batch) - 1} for every language."]
    text = ask("\n".join(lines), model)
    got = {}
    for line in text.splitlines():
        parts = line.split("\t")
        if len(parts) != 3 or parts[0] not in langs or not parts[1].strip().isdigit():
            continue
        i = int(parts[1])
        if 0 <= i < len(batch):
            got[(parts[0], i)] = parts[2].strip()
    return got


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dry-run", action="store_true", help="list what is missing; translate nothing")
    ap.add_argument("--staged", action="store_true", help="only strings the staged diff adds")
    ap.add_argument("--misses", help="a DECKBOY_I18N_MISSES file: translate its pieces too")
    ap.add_argument("--keys", help="translate exactly these strings, one per line")
    ap.add_argument("--langs", help="comma-separated codes (default: every full catalogue)")
    ap.add_argument("--limit", type=int, default=0, help="at most this many strings")
    ap.add_argument("--chunk", type=int, default=40, help="strings per call")
    ap.add_argument("--lang-chunk", type=int, default=6, help="languages per call")
    ap.add_argument("--model", default="", help="model for claude -p (default: its own)")
    args = ap.parse_args()

    langs = args.langs.split(",") if args.langs else FULL
    keys = candidate_keys(args)
    if args.staged:
        keys = staged_filter(keys)
        # SEVERAL SESSIONS SHARE THIS CHECKOUT. A catalogue somebody else has
        # unstaged edits in is left alone: staging the file would put their
        # half-done work into this commit.
        busy = subprocess.run(["git", "diff", "--name-only", "--", "data/lang"], cwd=ROOT,
                              capture_output=True, text=True).stdout.split()
        busy = {Path(b).stem for b in busy} & set(langs)
        if busy:
            print("i18n_translate: skipping catalogues with unstaged edits: " + ", ".join(sorted(busy)))
        langs = [c for c in langs if c not in busy]
    keep = load_keep()
    need = {code: missing_for(code, keys, keep) for code in langs}
    every = sorted({k for v in need.values() for k in v}, key=lambda s: (len(s), s))
    if args.limit:
        every = every[:args.limit]
        need = {c: [k for k in v if k in set(every)] for c, v in need.items()}
    total = sum(len(v) for v in need.values())
    print(f"i18n_translate: {len(every)} strings missing, {total} lines across {len(langs)} languages")
    if args.dry_run or not every:
        for k in every[:200]:
            print("  " + k)
        return 0

    added_lines, kept_lines, refused = [], [], []
    for start in range(0, len(every), args.chunk):
        batch = every[start:start + args.chunk]
        for ls in range(0, len(langs), args.lang_chunk):
            group = [c for c in langs[ls:ls + args.lang_chunk] if any(k in need[c] for k in batch)]
            if not group:
                continue
            print(f"  strings {start + 1}-{start + len(batch)}, {', '.join(group)} ...", flush=True)
            try:
                got = translate_chunk(group, batch, args.model)
            except Exception as e:  # one failed call must not lose the others
                print(f"  call failed: {e}")
                continue
            for code in group:
                for i, en in enumerate(batch):
                    if en not in need[code]:
                        continue
                    tr = got.get((code, i))
                    if tr is None:
                        refused.append((code, en, "no answer"))
                    elif tr == "=" or tr == en:
                        kept_lines.append(f"{code}\t{en}")
                    else:
                        why = problems(en, tr)
                        if why:
                            refused.append((code, en, why + ": " + tr))
                        else:
                            added_lines.append(f"{code}\t{en}\t{tr}")
    if added_lines:
        before = {c: (LANG_DIR / f"{c}.tsv").read_bytes() for c in langs}
        run = subprocess.run([sys.executable, str(HERE / "add_translations.py"), "--multi"],
                             input="\n".join(added_lines) + "\n", capture_output=True, text=True,
                             encoding="utf-8", cwd=ROOT)
        print(run.stdout.strip())
        if run.returncode != 0:
            print(run.stderr.strip())
        if args.staged:
            # Into the commit being made: exactly the files this run wrote.
            changed = [str(LANG_DIR / f"{c}.tsv") for c in langs
                       if (LANG_DIR / f"{c}.tsv").read_bytes() != before[c]]
            if changed:
                subprocess.run(["git", "add", "--"] + changed, cwd=ROOT)
    if kept_lines:
        existing = KEEP_FILE.read_text(encoding="utf-8") if KEEP_FILE.exists() else ""
        have = set(existing.splitlines())
        new = [l for l in kept_lines if l not in have]
        with open(KEEP_FILE, "a", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(new) + ("\n" if new else ""))
        if args.staged and new:
            subprocess.run(["git", "add", "--", str(KEEP_FILE)], cwd=ROOT)
    print(f"i18n_translate: added {len(added_lines)}, kept English {len(kept_lines)}, refused {len(refused)}")
    for code, en, why in refused[:40]:
        print(f"  refused {code}: {en!r}: {why}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
