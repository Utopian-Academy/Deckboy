"""A Mic cue, measured: what it plays, from a known input.

The cue's input is the built-in test signal (MIC INPUT TEST): 1 kHz on the left
at -18 dBFS, 500 Hz on the right at -24 dBFS. The deck plays to a SILENT device
(Steam Streaming Speakers if this machine has it; DECKBOY_MIC_TEST_SPEAKERS
names another) and the programme is recorded, then read back:
  - MONO L: 1 kHz in both channels at -18 dBFS peak (-21 dBFS RMS)
  - MONO R: 500 Hz in both at -24 dBFS peak (-27 RMS)
  - STEREO: 1 kHz left, 500 Hz right, each at its own level
  - STOP fades it over the cue's fade out (1 s) and then the deck is silent
  - a real input (REAL_MIC=<device name>) opens, reports live and a latency
Every window hidden. Evidence: build/mic-cue-*/
"""
import os, sys, time, tempfile, subprocess, shutil, glob, re
from pathlib import Path
import numpy as np
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import check_hdr_sources as c
from deckboy_testroot import populate

failures = []


def expect(ok, label):
    print(('ok   ' if ok else 'FAIL ') + label, flush=True)
    if not ok:
        failures.append(label)


speakers = os.environ.get('DECKBOY_MIC_TEST_SPEAKERS', 'Speakers (Steam Streaming Speakers)')
real_mic = os.environ.get('REAL_MIC', '')
ffmpeg = shutil.which('ffmpeg')
work = Path(tempfile.mkdtemp(prefix='mic-cue-', dir=ROOT / 'build'))
port = c.free_port()
root = work / 'root'
state = work / 'state'
populate(str(root))
show = root / 'data/default.deckboy'
show.write_text(f'update_check\t0\nui_sounds\t0\naudio_output\t{speakers}\n', encoding='utf-8')
env = dict(os.environ, DECKBOY_ROOT=str(root), DECKBOY_STATE_DIR=str(state), DECKBOY_PROJECT=str(show),
           DECKBOY_COMPANION_PORT=str(port), DECKBOY_HIDE_WINDOWS='1')
log = (work / 'app.log').open('w')
app = subprocess.Popen([str(ROOT / 'build/windows/Release/Deckboy.exe'), '--allow-multi-instance', str(show)],
                       env=env, cwd=ROOT, stdout=log, stderr=log)
cmd = lambda line: c.command(port, line)
marks = {}
try:
    for _ in range(200):
        try:
            if cmd('PING').startswith('OK'):
                break
        except OSError:
            time.sleep(0.3)
    print(cmd('MIC LIST'))
    print(cmd('MIC ADD TEST'))
    status = cmd('MIC')
    print(status)
    expect('input=test signal' in status and 'channel=MONO L' in status, 'a new mic cue: test input, MONO L')
    print(cmd('RECORD ON'))
    time.sleep(2)
    t0 = time.time()
    print(cmd('TAKE 1'))
    time.sleep(1.5)
    status = cmd('MIC')
    print(status)
    lat = re.search(r'latency_ms=(\d+)', status)
    expect('status=live' in status, 'TAKE puts it live')
    expect(lat is not None and 0 < int(lat.group(1)) < 120, f'latency reported ({lat.group(1) if lat else None} ms)')
    level = re.search(r'level=(-?[\d.]+)dBFS', status)
    expect(level is not None and abs(float(level.group(1)) + 18) < 1.0,
           f'the meter reads the input at -18 dBFS ({level.group(1) if level else None})')
    marks['L'] = (time.time() - t0 - 1.0, time.time() - t0)
    print(cmd('MIC CHANNEL R'))
    time.sleep(2)
    marks['R'] = (time.time() - t0 - 1.2, time.time() - t0)
    print(cmd('MIC CHANNEL STEREO'))
    time.sleep(2)
    marks['S'] = (time.time() - t0 - 1.2, time.time() - t0)
    stop_at = time.time() - t0
    print(cmd('STOP'))
    time.sleep(2.5)
    status = cmd('MIC')
    print(status)
    expect('not taken' in status, 'after the fade the deck has stopped')
    marks['after'] = (time.time() - t0 - 0.8, time.time() - t0)
    marks['fade'] = (stop_at, stop_at + 1.0)
    print(cmd('RECORD OFF'))
    time.sleep(3)
    if real_mic:
        print(cmd(f'MIC INPUT {real_mic}'))
        print(cmd('TAKE 1'))
        time.sleep(2.5)
        status = cmd('MIC')
        print(status)
        lat = re.search(r'latency_ms=(\d+)', status)
        level = re.search(r'level=(-?[\d.]+)dBFS', status)
        expect('status=live' in status and real_mic in status, f'the real input opens ({real_mic})')
        expect(level is not None and float(level.group(1)) > -89, f'it hears something ({level.group(1) if level else None} dBFS)')
        print(f'   real input latency estimate: {lat.group(1) if lat else None} ms')
        print(cmd('STOP'), cmd('STOP'))
finally:
    app.kill()
    app.wait(timeout=10)
    log.close()

files = sorted(glob.glob(str(state / 'recordings' / '*')) + glob.glob(str(root / 'data' / 'recordings' / '*')),
               key=os.path.getmtime)
expect(bool(files), 'a recording was written')
if files:
    raw = subprocess.run([ffmpeg, '-v', 'error', '-i', files[-1], '-vn', '-f', 's16le', '-ac', '2', '-ar', '48000', '-'],
                         capture_output=True).stdout
    pcm = np.frombuffer(raw, np.int16).reshape(-1, 2).astype(float) / 32768.0
    # The recording started ~2 s before TAKE; find the onset rather than trust the clock.
    loud = np.where(np.abs(pcm).max(axis=1) > 0.01)[0]
    expect(len(loud) > 0, 'the recording carries the mic')
    if len(loud):
        onset = loud[0] / 48000.0

        def seg(name):
            a, b = marks[name]
            return pcm[int((onset + a) * 48000):int((onset + b) * 48000)]

        def tone(x):
            if len(x) < 4800:
                return 0, -120.0
            spec = np.abs(np.fft.rfft(x * np.hanning(len(x))))
            hz = np.argmax(spec) * 48000.0 / len(x)
            rms = 20 * np.log10(max(1e-9, float(np.sqrt(np.mean(x ** 2)))))
            return round(hz), round(rms, 1)

        for name, want in [('L', ((1000, -21), (1000, -21))), ('R', ((500, -27), (500, -27))),
                           ('S', ((1000, -21), (500, -27)))]:
            s = seg(name)
            got = (tone(s[:, 0]), tone(s[:, 1]))
            ok = all(abs(g[0] - w[0]) <= 15 and abs(g[1] - w[1]) <= 1.0 for g, w in zip(got, want))
            expect(ok, f'{name}: left {got[0]}, right {got[1]} (want {want})')
        tail = seg('after')
        tail_db = 20 * np.log10(max(1e-9, float(np.abs(tail).max()))) if len(tail) else 0
        expect(tail_db < -60, f'silent after STOP ({tail_db:.1f} dBFS peak)')
        f = seg('fade')
        if len(f) > 4800:
            first = 20 * np.log10(max(1e-9, float(np.sqrt(np.mean(f[:4800] ** 2)))))
            last = 20 * np.log10(max(1e-9, float(np.sqrt(np.mean(f[-4800:] ** 2)))))
            expect(first - last > 10, f'STOP fades rather than cuts ({first:.1f} -> {last:.1f} dB RMS)')
print(f'mic cue: {len(failures)} failures; evidence: {work}')
sys.exit(1 if failures else 0)
