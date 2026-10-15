"""An HDR output, measured: what it sends the display, in nits.

OUTPUT HDR ON gives a window output an scRGB swapchain and a float compositor.
This plays SDR EBU bars (with a 100% white patch) and an exact PQ ladder on it,
every window hidden (DECKBOY_HIDE_WINDOWS), and checks:
  - the back buffer (OUTSNAP to .f32, scRGB: 1.0 = 80 nits): SDR white at the
    BT.2408 203 nits; each PQ grey at its own luminance (within 3%)
  - the 8-bit copy NDI, SDI and recordings take (the scopes): the bars exact
Needs an HDR-capable renderer (Direct3D 11 here); it says so if the output
fell back to SDR. Evidence: build/hdr-out-*/
"""
import os, sys, time, tempfile, subprocess, shutil, re
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import check_hdr_sources as c
from deckboy_testroot import populate
from PIL import Image

hide = os.environ.get('SHOW', '0') != '1'
failures = []


def expect(ok, label):
    print(('ok   ' if ok else 'FAIL ') + label)
    if not ok:
        failures.append(label)

work = Path(tempfile.mkdtemp(prefix='hdr-out-', dir=ROOT / 'build'))
ff = shutil.which('ffmpeg')
pq = work / 'pq.mov'
ps = c.make_clip(ff, pq, 'pq')
bars = Image.new('RGB', (1280, 720))
for i, col in enumerate(c.EBU75):
    bars.paste(col, (i * 160, 0, (i + 1) * 160, 720))
bars.paste((255, 255, 255), (0, 0, 160, 120))
bp = work / 'bars.png'
bars.save(bp)
port = c.free_port()
root = work / 'root'
populate(str(root))
show = root / 'data/default.deckboy'
show.write_text('update_check\t0\nui_sounds\t0\n', encoding='utf-8')
env = dict(os.environ, DECKBOY_ROOT=str(root), DECKBOY_STATE_DIR=str(work / 'state'), DECKBOY_PROJECT=str(show),
           DECKBOY_COMPANION_PORT=str(port), DECKBOY_OUTPUT_RENDERER='direct3d11')
if hide:
    env['DECKBOY_HIDE_WINDOWS'] = '1'
log = (work / 'app.log').open('w')
app = subprocess.Popen([str(ROOT / 'build/windows/Release/Deckboy.exe'), '--allow-multi-instance', str(show)],
                       env=env, cwd=ROOT, stdout=log, stderr=log)
cmd = lambda line: c.command(port, line)


def window_nits(name, points):
    path = work / f'{name}.f32'
    cmd(f'OUTSNAP {path} 1')
    for _ in range(40):
        if path.exists() and path.stat().st_size > 8:
            break
        time.sleep(0.1)
    time.sleep(0.3)
    raw = path.read_bytes()
    w, h = np.frombuffer(raw[:8], np.int32)
    img = np.frombuffer(raw[8:], np.float32).reshape(h, w, 4)
    return [tuple(round(float(v) * 80, 1) for v in img[int(fy * (h - 1)), int(fx * (w - 1)), :3]) for fx, fy in points]


try:
    for _ in range(200):
        try:
            if cmd('PING').startswith('OK'):
                break
        except OSError:
            time.sleep(0.3)
    lst = work / 'drop.txt'
    lst.write_text(f'{bp}\n{pq}\n')
    cmd(f'DROPTEST {lst}')
    time.sleep(4)
    print(cmd('OUTPUT HDR ON'))
    print(cmd('OUTPUT ON'))
    time.sleep(3)
    status = cmd('OUTPUT HDR STATUS')
    print(status)
    expect('running HDR' in status, 'the output runs HDR (scRGB, float compositor)')
    cmd('TAKE 1')
    time.sleep(2)
    cmd('SCOPES SCOPES')
    time.sleep(3)
    stats = cmd('SCOPES STATS')
    expect('targets75=6/6' in stats and 'luma_peaks=0.2,5.3,15.8,21.3,53.7,58.8,69.3,74.8' in stats,
           'the 8-bit copy (NDI/SDI/recording) carries the EBU bars exactly')
    print('SDR bars, sent to the display (nits):')
    pts = [(0.06, 0.08)] + [((i + 0.5) / 8, 0.6) for i in range(8)]
    got = window_nits('sdr', pts)
    for label, v in zip(['100% white'] + [str(x) for x in c.EBU75], got):
        print(f'   {label:16s} {v}')
    expect(all(abs(x - 203) <= 3 for x in got[0]), f'SDR white sent at 203 nits ({got[0]})')
    cmd('TAKE 2')
    time.sleep(3)
    cmd('PAUSE')
    time.sleep(1)
    print('PQ ladder, sent to the display (nits):')
    pts = [((i % 8 + 0.5) / 8, (i // 8 + 0.5) / 2) for i in range(len(ps))]
    got = window_nits('pq', pts)
    for (label, nits, _), v in zip(ps, got):
        print(f'   {label:22s} {v}')
    for (label, nits, _), v in list(zip(ps, got))[:8]:
        target = float(nits[0])
        expect(all(abs(x - target) <= max(0.3, target * 0.03) for x in v), f'PQ {label} sent at its own luminance {v}')
    if not hide:
        time.sleep(float(os.environ.get('HOLD', '5')))
finally:
    app.kill()
    log.close()
print(f'hdr output: {len(failures)} failures; evidence: {work}')
sys.exit(1 if failures else 0)
