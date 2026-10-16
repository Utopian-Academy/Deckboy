"""The fixture patch, measured on the wire.

Patches three fixtures, builds a DMX cue by fixture, sends it as Art-Net to a
socket on this machine, and reads the packets back:
  - a dimmer at 20% is channel 1 = 51; an RGBW wash at 85% blue is G 54, B 217
  - a level LFO moves the dimmer between packets; a colour LFO moves the wash
  - a later cue that sets the dimmer stops its LFO and leaves the wash alone
  - blackout zeroes everything and stops every LFO
  - the patch and the cue's fixtures survive a reload
  - VIDEO DIP: a clip rolling on deck 1 dips the fixtures marked for it to
    their dip level over the fade, leaves the others, and brings them back
    to exactly where they were when it stops
Every window hidden. Evidence: build/fixture-patch-*/
"""
import os, sys, time, tempfile, subprocess, socket
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
import check_hdr_sources as c
from deckboy_testroot import populate

failures = []


def expect(ok, label):
    print(('ok   ' if ok else 'FAIL ') + label, flush=True)
    if not ok:
        failures.append(label)


work = Path(tempfile.mkdtemp(prefix='fixture-patch-', dir=ROOT / 'build'))
root = work / 'root'
populate(str(root))
show = root / 'data/default.deckboy'
show.write_text('update_check\t0\nui_sounds\t0\n', encoding='utf-8')
listen = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
listen.bind(('127.0.0.1', 0))
listen.settimeout(0.5)
lport = listen.getsockname()[1]


def start():
    port = c.free_port()
    env = dict(os.environ, DECKBOY_ROOT=str(root), DECKBOY_STATE_DIR=str(work / 'state'), DECKBOY_PROJECT=str(show),
               DECKBOY_COMPANION_PORT=str(port), DECKBOY_HIDE_WINDOWS='1')
    app = subprocess.Popen([str(ROOT / 'build/windows/Release/Deckboy.exe'), '--allow-multi-instance', str(show)],
                           env=env, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    cmd = lambda line: c.command(port, line)
    for _ in range(200):
        try:
            if cmd('PING').startswith('OK'):
                break
        except OSError:
            time.sleep(0.3)
    return app, cmd


def packets(seconds):
    """Art-Net DMX payloads (512 bytes) seen in the next `seconds`."""
    listen.setblocking(False)
    try:
        while True:
            listen.recvfrom(2048)   # what queued before this moment is not "after"
    except (BlockingIOError, OSError):
        pass
    listen.settimeout(0.5)
    out, until = [], time.time() + seconds
    while time.time() < until:
        try:
            data, _ = listen.recvfrom(2048)
        except socket.timeout:
            continue
        if data[:8] == b'Art-Net\x00' and len(data) >= 18 + 24:
            out.append(data[18:])
    return out


app, cmd = start()
try:
    for line in ['FIXTURE ADD House dimmer 0 1', 'FIXTURE ADD Wash L rgbw 0 10', 'FIXTURE ADD Up lights drgb 0 20',
                 'FIXTURE LIST', 'DMXCUE NEW', f'DMXCUE HOST 127.0.0.1', f'DMXCUE PORT {lport}',
                 'DMXCUE FIXTURE House 20', 'DMXCUE FIXTURE Wash L 85 blue', 'DMXCUE']:
        print(line, '->', cmd(line))
    print(cmd('DMXCUE FIRE'))
    p = packets(0.6)
    expect(len(p) > 5, f'Art-Net is flowing ({len(p)} packets in 0.6 s)')
    if p:
        last = p[-1]
        expect(last[0] == 51, f'House at 20% is channel 1 = 51 ({last[0]})')
        expect(tuple(last[9:13]) == (0, 54, 217, 0), f'Wash L at 85% blue is 0,54,217,0 ({tuple(last[9:13])})')
        expect(tuple(last[19:23]) == (0, 0, 0, 0), f'Up lights left alone ({tuple(last[19:23])})')
    print(cmd('DMXCUE LFO House LEVEL ON 2 100'), cmd('DMXCUE LFO Wash L COLOUR ON 1 100'), cmd('DMXCUE FIRE'))
    p = packets(1.2)
    house = sorted({q[0] for q in p})
    wash = {tuple(q[9:12]) for q in p}
    expect(len(house) > 6 and house[0] < 30 and house[-1] > 80, f'the level LFO moves House ({house[0]}..{house[-1]}, {len(house)} values)')
    expect(len(wash) > 6, f'the colour LFO moves Wash L round the wheel ({len(wash)} colours)')
    print(cmd('DMXCUE NEW'), cmd(f'DMXCUE HOST 127.0.0.1'), cmd(f'DMXCUE PORT {lport}'),
          cmd('DMXCUE FIXTURE House 100'), cmd('DMXCUE FIRE'))
    p = packets(0.8)
    house = {q[0] for q in p}
    wash = {tuple(q[9:12]) for q in p}
    expect(house == {255}, f'a later cue takes House and stops its LFO ({sorted(house)})')
    expect(len(wash) > 3, f'Wash L keeps its colour LFO ({len(wash)} colours)')
    print(cmd('DMXCUE BLACKOUT'))
    p = packets(0.8)
    expect(bool(p) and all(not any(q[:24]) for q in p[2:]), 'blackout: everything at 0, no LFO running')
    print(cmd('SELECT 1'))
    time.sleep(1.5)
finally:
    app.kill()
    app.wait()

app, cmd = start()
try:
    listing = cmd('FIXTURE LIST')
    print(listing)
    expect('House dimmer 0/1' in listing and 'Wash L rgbw 0/10' in listing and 'Up lights drgb 0/20' in listing,
           'the patch survives a reload')
    print(cmd('SELECT 1'))
    status = cmd('DMXCUE')
    print(status)
    print(cmd(f'DMXCUE PORT {lport}'), cmd('DMXCUE FIRE'))
    p = packets(1.0)
    house = {q[0] for q in p}
    expect(len(house) > 4, f'the reloaded cue still carries its LFO ({len(house)} House values)')
finally:
    app.kill()
    app.wait()
clip = work / 'clip.mp4'
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i', 'testsrc2=size=640x360:rate=25:duration=20',
                '-pix_fmt', 'yuv420p', str(clip)], check=True)
app, cmd = start()
try:
    for line in ['FIXTURE DIP House 20', 'VIDEODIP ON', 'VIDEODIP FADE 0.5', 'DMXCUE NEW', 'DMXCUE HOST 127.0.0.1',
                 f'DMXCUE PORT {lport}', 'DMXCUE FIXTURE House 80', 'DMXCUE FIXTURE Wash L 85 blue', 'DMXCUE FIRE']:
        print(line, '->', cmd(line))
    p = packets(0.5)
    expect(bool(p) and p[-1][0] == 204, f'House at 80% before the roll ({p[-1][0] if p else None})')
    (work / 'drop.txt').write_text(f'{clip}\n')
    print(cmd(f'DROPTEST {work / "drop.txt"}'))
    time.sleep(3)
    n = cmd('STATUS')
    print(cmd('TAKE 4'))
    time.sleep(1.5)
    print(cmd('VIDEODIP'))
    p = packets(0.5)
    expect(bool(p) and p[-1][0] == 51, f'the roll dips House to 20% ({p[-1][0] if p else None})')
    expect(bool(p) and tuple(p[-1][9:13]) == (0, 54, 217, 0), f'Wash L (LEAVE) is untouched ({tuple(p[-1][9:13]) if p else None})')
    print(cmd('STOP'))
    time.sleep(1.2)
    print(cmd('VIDEODIP'))
    p = packets(0.5)
    expect(bool(p) and p[-1][0] == 204, f'STOP brings House back to 80% ({p[-1][0] if p else None})')
finally:
    app.kill()
    app.wait()
print(f'fixture patch: {len(failures)} failures; evidence: {work}')
sys.exit(1 if failures else 0)
