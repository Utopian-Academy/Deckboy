#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
"""Check face speech coverage, UTF-8, catalogue structure and command tokens."""
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FULL_LANGUAGES = ('ar bg da de el es es-CU fa fi fr gd hi is it ja ko lum nl pl '
                  'pt-BR ru sco sjn tlh tr uk zh-Hans zh-Hant zu').split()
COMMANDS = re.compile(r'Ctrl\+[IO/]|AUDIONORM ALL|RELINK|CHECK|\bGO\b|\bESC\b|'
                      r'\bEsc\b|(?<=Press )I\b|\b[SJ]\b|\bEnter\b|\bNDI\b|\bSRT\b|\bRTMP\b|'
                      r'\bMIDI\b|\bOSC\b|\bUDP\b|\bTCP\b|\bDMX\b|\bLTC\b')


def face_lines():
    src = (ROOT / 'native/app/app_render_main.ipp').read_text(encoding='utf8')
    start = src.index('static const char* mascotAdviceForTip')
    end = src.index('THE MONITOR IS A MONITOR', start)
    advice = re.findall(r'\{"[^"]*",\s*("(?:[^"\\]|\\.)*")\}', src[start:end])
    start = src.index('void drawStartupMascot(')
    end = src.index('tipFont, tipRect, spoken', start)
    # Keep strings intact while ignoring comments (including quoted examples).
    tokens = re.findall(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:[^"\\]|\\.)*"', src[start:end])
    lines = [json.loads(t) for t in advice + tokens if t.startswith('"')]
    return list(dict.fromkeys(s for s in lines if len(s) >= 4 and
                             not s.startswith('DECKBOY_') and '%' not in s))


def catalogue(code):
    entries = {}
    for number, line in enumerate((ROOT / 'data/lang' / (code + '.tsv')).read_text(encoding='utf8').splitlines(), 1):
        if not line or line.startswith('#'):
            continue
        if line.count('\t') != 1:
            raise ValueError(f'{code}:{number}: expected one tab')
        key, value = line.split('\t')
        if key != key.strip() or value != value.strip():
            raise ValueError(f'{code}:{number}: surrounding whitespace')
        if any(c in key + value for c in ('\ufffd', '\u200b', '\ufeff')):
            raise ValueError(f'{code}:{number}: replacement glyph or hidden break')
        if key in entries:
            raise ValueError(f'{code}:{number}: duplicate key {key!r}')
        entries[key] = value
    return entries


def missing(code):
    have = catalogue(code)
    return [s for s in face_lines() if not have.get(s, '').strip()]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('languages', nargs='*')
    ap.add_argument('--missing', action='store_true')
    args = ap.parse_args()
    total = 0
    lines = face_lines()
    if not lines:
        raise ValueError('no face lines found; check source inventory boundaries')
    for code in args.languages or FULL_LANGUAGES:
        absent = missing(code)
        have = catalogue(code)
        print(f'{code}: {len(lines) - len(absent)}/{len(lines)} face lines')
        total += len(absent)
        if args.missing:
            print('\n'.join(absent))
        for key in lines:
            # Keyboard/protocol names remain executable. Esc's casing is cosmetic.
            for token in COMMANDS.findall(key):
                if token.lower() not in have.get(key, '').lower():
                    print(f'  missing command token {token!r}: {key}')
                    total += 1
    return int(total != 0)


if __name__ == '__main__':
    raise SystemExit(main())
