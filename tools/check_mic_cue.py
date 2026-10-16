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
  - DRIFT: the test input's clock 2000 ppm fast, then 2000 ppm slow (ten times
    a real device's error). The output must never run dry, no block may be
    dropped, the delay must hold, and the recorded tone must have no click --
    the old answer to drift dropped a 10 ms block, heard on music.
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


def clicks(x, hz, rate=48000):
    """Where a sine is broken. For a pure tone x[n+1] + x[n-1] = 2cos(w) x[n]
    exactly, so the residual is ~0 except at a seam: a dropped block or a gap
    leaves a residual the size of the tone itself."""
    w = 2 * np.pi * hz / rate
    r = x[2:] + x[:-2] - 2 * np.cos(w) * x[1:-1]
    return np.abs(r) / max(1e-9, np.abs(x).max())


# The detector, proved on a tone with about 10 ms missing (what drift used to
# do) before it is trusted to say there are none. Not exactly 480 samples:
# that is ten whole cycles of 1 kHz, and leaves no seam to find.
n = np.arange(48000 * 2)
broken = np.sin(2 * np.pi * 1000 * np.delete(n, slice(48000, 48487)) / 48000)
expect(clicks(broken, 1000).max() > 0.1 and clicks(np.sin(2 * np.pi * 1000 * n / 48000), 1000).max() < 0.01,
       'the click detector finds a dropped block and passes a clean tone')

for ppm in (2000, -2000):
    dwork = work / f'drift{ppm:+d}'
    dstate = dwork / 'state'
    dport = c.free_port()
    denv = dict(env, DECKBOY_STATE_DIR=str(dstate), DECKBOY_COMPANION_PORT=str(dport), DECKBOY_MIC_TEST_PPM=str(ppm))
    dlog = (work / f'app-drift{ppm:+d}.log').open('w')
    app = subprocess.Popen([str(ROOT / 'build/windows/Release/Deckboy.exe'), '--allow-multi-instance', str(show)],
                           env=denv, cwd=ROOT, stdout=dlog, stderr=dlog)
    dcmd = lambda line: c.command(dport, line)
    try:
        for _ in range(200):
            try:
                if dcmd('PING').startswith('OK'):
                    break
            except OSError:
                time.sleep(0.3)
        dcmd('MIC ADD TEST')
        dcmd('RECORD ON')
        time.sleep(2)
        dcmd('TAKE 1')
        time.sleep(10)   # settle: the correction learns the error
        settled = dcmd('MIC')
        time.sleep(30)
        status = dcmd('MIC')
        print(status)
        dcmd('STOP')
        time.sleep(2)
        dcmd('RECORD OFF')
        time.sleep(3)
    finally:
        app.kill()
        app.wait(timeout=10)
        dlog.close()
    get = lambda key, text: int(re.search(key + r'=(-?\d+)', text).group(1))
    drift = get('drift_ppm', status)
    expect(abs(drift - ppm) < ppm * 0.1 or abs(drift - ppm) < 100, f'{ppm:+d} ppm: the correction found it ({drift:+d} ppm)')
    under = get('underruns', status) - get('underruns', settled)
    expect(under == 0, f'{ppm:+d} ppm: the output never ran dry after settling ({under})')
    expect(get('dropped', status) == 0, f'{ppm:+d} ppm: no block dropped ({get("dropped", status)})')
    lat = get('latency_ms', status)
    expect(0 < lat < 60, f'{ppm:+d} ppm: the delay held ({lat} ms)')
    rec = sorted(glob.glob(str(dstate / 'recordings' / '*')) + glob.glob(str(root / 'data' / 'recordings' / '*')),
                 key=os.path.getmtime)
    if rec:
        raw = subprocess.run([ffmpeg, '-v', 'error', '-i', rec[-1], '-vn', '-f', 's16le', '-ac', '2', '-ar', '48000', '-'],
                             capture_output=True).stdout
        x = np.frombuffer(raw, np.int16).reshape(-1, 2)[:, 0].astype(float) / 32768.0
        loud = np.where(np.abs(x) > 0.01)[0]
        if len(loud):
            mid = x[loud[0] + 48000 * 11:loud[0] + 48000 * 39]   # the settled 28 s
            spec = np.abs(np.fft.rfft(mid * np.hanning(len(mid))))
            hz = np.argmax(spec) * 48000.0 / len(mid)
            worst = clicks(mid, hz).max()
            # The input's own pitch moves with its clock; the correction may
            # move it back by no more than the drift itself.
            expect(abs(hz - 1000) < 5, f'{ppm:+d} ppm: the tone is {hz:.1f} Hz')
            expect(worst < 0.05, f'{ppm:+d} ppm: no click in 28 s of tone (worst seam {worst:.4f} of full scale)')
    else:
        expect(False, f'{ppm:+d} ppm: a recording was written')

print(f'mic cue: {len(failures)} failures; evidence: {work}')
sys.exit(1 if failures else 0)
