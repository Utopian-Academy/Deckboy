"""DEFAULTS, measured: a customised show saved as the defaults; NEW is that
setup with no cues; RESET, NEW, and it is factory again. Every window hidden.
Evidence: build/defaults-*/"""
import os, sys, time, tempfile, subprocess, struct
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import check_hdr_sources as c
from deckboy_testroot import populate

work = Path(tempfile.mkdtemp(prefix='defaults-', dir=ROOT / 'build'))
populate(str(work))
media = work / 'media'; media.mkdir()
for name in ('Alpha', 'Beta'):
    w = h = 16
    row = b'\x00\x80\xff' * w + b'\x00' * ((4 - (w * 3) % 4) % 4)
    data = row * h
    (media / f'{name}.bmp').write_bytes(b'BM' + struct.pack('<IHHI', 54 + len(data), 0, 0, 54) +
                                        struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0) + data)
show = work / 'data/default.deckboy'; show.write_text('update_check\t0\nui_sounds\t0\n', encoding='utf-8')
state = work / 'state'
port = c.free_port()
env = dict(os.environ, DECKBOY_ROOT=str(work), DECKBOY_STATE_DIR=str(state), DECKBOY_PROJECT=str(show),
           DECKBOY_COMPANION_PORT=str(port), DECKBOY_HIDE_WINDOWS='1')
app = subprocess.Popen([str(ROOT / 'build/windows/Release/Deckboy.exe'), '--allow-multi-instance', str(show),
                        '--import', str(media)], env=env, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
cmd = lambda line: c.command(port, line)
fails = []
def expect(ok, label):
    print(('ok   ' if ok else 'FAIL ') + label, flush=True)
    if not ok: fails.append(label)

def status():
    return cmd('STATUS')

def newshow():
    r = cmd('NEWSHOW')
    if r.startswith('ERR'):
        r = cmd('NEWSHOW')
    time.sleep(1.0)
    # Nothing changed yet means nothing autosaved: mark it (harmlessly) so
    # the show file on disk is the NEW show, not the one before it.
    cmd('DASH TILES')
    time.sleep(1.5)
    return r

try:
    for _ in range(200):
        try:
            if cmd('PING').startswith('OK'): break
        except OSError: time.sleep(0.3)
    time.sleep(2)
    print(cmd('DEFAULTS'))
    expect('factory' in cmd('DEFAULTS'), 'starts with factory defaults')
    print(cmd('SET theme dark'), cmd('DECKADD'), cmd('DASH SET 1 Test | DECK 1 GO'))
    time.sleep(1)
    before = status()
    print('before:', before[:300])
    print(cmd('DEFAULTS SAVE'))
    d = (state / 'defaults.deckboy')
    expect(d.exists(), 'defaults.deckboy written to the state folder')
    text = d.read_text(encoding='utf-8', errors='replace') if d.exists() else ''
    expect('theme\tdark' in text, 'it carries the theme')
    expect('Alpha' not in text and 'Beta' not in text, 'it carries no cues')
    expect('dashboard\t0\tTest' in text, 'it carries DASH')
    print(cmd('DEFAULTS'))
    print('NEWSHOW:', newshow())
    after = status()
    print('after:', after[:300])
    time.sleep(1)
    cur = show.read_text(encoding='utf-8', errors='replace') if show.exists() else ''
    expect('theme\tdark' in cur, 'the new show has the theme')
    expect('dashboard\t0\tTest' in cur, 'the new show has the DASH button')
    expect('Alpha' not in cur and 'Beta' not in cur and 'decks=2' in after, 'the new show has two decks and no cues')
    print(cmd('DEFAULTS RESET'))
    expect(not d.exists() and 'factory' in cmd('DEFAULTS'), 'RESET forgets them')
    print('NEWSHOW:', newshow())
    time.sleep(1)
    cur = show.read_text(encoding='utf-8', errors='replace') if show.exists() else ''
    expect('theme\tgameboy' in cur, 'after RESET a new show is factory (gameboy)')
    expect('dashboard\t0\tTest' not in cur, 'and has no DASH buttons')
finally:
    app.kill(); app.wait()
print(f'defaults: {len(fails)} failures; evidence {work}')
sys.exit(1 if fails else 0)
