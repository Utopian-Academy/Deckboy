#!/usr/bin/env python3
"""Compare the primary programme compositor with independently decoded pixels.

The recording output owns a different renderer/device and can take a different
video bridge from the programme window. Read the primary output's own egress
tap through the web monitor, including on D3D11, rather than infer its colours
from a recording. Static lossless HEVC fixtures cover SDR at 8/10 bits and
limited/full range. Each case takes, pauses, and repeatedly checks the picture.
"""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
from urllib.request import urlopen

from deckboy_harness import Deckboy, default_exe, find_ffmpeg

WIDTH, HEIGHT = 320, 180
COLOURS = [(24, 24, 24), (55, 55, 55), (95, 95, 95), (145, 145, 145),
           (195, 195, 195), (230, 230, 230), (150, 65, 45), (45, 130, 70),
           (45, 65, 160), (160, 140, 45), (125, 55, 145), (45, 145, 150)]


def decode_rgb(ffmpeg, source, encoded=False):
    command = [ffmpeg, '-v', 'error']
    if encoded:
        command += ['-i', source]
        payload = None
    else:
        command += ['-i', 'pipe:0']
        payload = source
    command += ['-frames:v', '1', '-vf', 'scale=320:180:flags=area,format=rgb24',
                '-f', 'rawvideo', 'pipe:1']
    pixels = subprocess.run(command, input=payload, capture_output=True, check=True).stdout
    if len(pixels) != WIDTH * HEIGHT * 3:
        raise RuntimeError('no complete colour-reference picture')
    return pixels


def patch_means(pixels):
    means = []
    for tile in range(len(COLOURS)):
        cx = (tile % 6) * (WIDTH // 6) + WIDTH // 12
        cy = (tile // 6) * (HEIGHT // 2) + HEIGHT // 4
        points = [(y * WIDTH + x) * 3 for y in range(cy - 8, cy + 8)
                  for x in range(cx - 8, cx + 8)]
        means.append(tuple(sum(pixels[p + c] for p in points) / len(points)
                           for c in range(3)))
    return means


def fixture(ffmpeg, root, bits, range_name):
    ppm = root / 'reference.ppm'
    if not ppm.exists():
        pixels = bytearray()
        for y in range(HEIGHT):
            for x in range(WIDTH):
                tile = min(5, x * 6 // WIDTH) + (y * 2 // HEIGHT) * 6
                pixels.extend(COLOURS[tile])
        ppm.write_bytes(('P6\n%d %d\n255\n' % (WIDTH, HEIGHT)).encode() + pixels)
    target = root / ('sdr-%d-%s.mkv' % (bits, range_name))
    subprocess.run([
        ffmpeg, '-v', 'error', '-y', '-loop', '1', '-i', str(ppm),
        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000', '-t', '15',
        '-r', '25', '-vf', 'scale=in_range=full:out_range=%s:out_color_matrix=bt709' % range_name,
        '-c:v', 'libx265', '-preset', 'ultrafast',
        '-pix_fmt', 'yuv420p10le' if bits == 10 else 'yuv420p',
        '-x265-params', 'lossless=1:pools=2:frame-threads=2:log-level=error',
        '-color_range', 'tv' if range_name == 'limited' else 'pc',
        '-colorspace', 'bt709', '-color_trc', 'bt709', '-color_primaries', 'bt709',
        '-c:a', 'pcm_s16le', str(target)], check=True)
    return target


def command(db, text):
    reply = db.send(text)
    if not reply.startswith('OK'):
        raise RuntimeError('%s -> %s' % (text, reply))
    return reply


def check_web_player(db, port, ffprobe, work):
    page = urlopen('http://127.0.0.1:%d/' % port, timeout=10).read().decode()
    match = re.search(r"fetch\('(/av/\d+)'\)", page)
    if '<video ' not in page or not match or 'SOUND ON' not in page:
        raise RuntimeError('web home page is not the programme player with sound')
    # Stop at a complete mdat: a timed network read can end halfway through
    # a box header or fragment, which is not a valid file for ffprobe.
    target = work / 'web-programme.mp4'
    captured = bytearray()
    with urlopen('http://127.0.0.1:%d%s' % (port, match.group(1)), timeout=15) as stream:
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            chunk = stream.read(8192)
            if not chunk:
                break
            captured.extend(chunk)
    offset = complete_end = 0
    fragments = 0
    while len(captured) - offset >= 8:
        size = int.from_bytes(captured[offset:offset + 4], 'big')
        header_size = 8
        if size == 1:
            if len(captured) - offset < 16:
                break
            size = int.from_bytes(captured[offset + 8:offset + 16], 'big')
            header_size = 16
        if size < header_size or size > len(captured) - offset:
            break
        if captured[offset + 4:offset + 8] == b'mdat':
            complete_end = offset + size
            fragments += 1
        offset += size
    if not fragments:
        raise RuntimeError('web programme stream delivered no complete media fragment')
    target.write_bytes(captured[:complete_end])
    probe = subprocess.run([ffprobe, '-v', 'error', '-show_streams', '-of', 'json', str(target)],
                           capture_output=True, text=True)
    if probe.returncode:
        db.log_excerpt('web|stream|ffmpeg|error', lines=25)
        raise RuntimeError('web programme probe failed: ' + probe.stderr.strip())
    streams = json.loads(probe.stdout)['streams']
    if not any(s.get('codec_type') == 'video' for s in streams):
        raise RuntimeError('web programme stream has no video track')
    if not any(s.get('codec_type') == 'audio' and s.get('sample_rate') == '48000'
               and s.get('channels') == 2 for s in streams):
        raise RuntimeError('web programme stream has no stereo 48 kHz audio track')
    print('web programme: home-page player, video + stereo 48 kHz audio ok', flush=True)
    command(db, 'WEBMONITOR OFF')
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        status = db.send('STATUS')
        line = next((line for line in status.splitlines() if 'proto=web' in line), '')
        if 'enabled=off' in line and 'stream=off' in line:
            break
        time.sleep(0.1)
    else:
        raise RuntimeError('web monitor left the programme encoder armed after OFF')
    command(db, 'WEBMONITOR ON')


def run_case(args, media, bits, range_name, case_number, ffmpeg, ffprobe, work):
    reference = patch_means(decode_rgb(ffmpeg, str(media), encoded=True))
    options = SimpleNamespace(exe=args.exe, port=args.port + case_number, keep=args.keep, ffmpeg=ffmpeg)
    web_port = args.web_port + case_number
    with Deckboy(options, 'deckboy-programme-colour-', extra_args=['--import', str(media)]) as db:
        command(db, 'OUTPUT ON')
        command(db, 'VIDEO 640x360')
        command(db, 'WEBMONITOR PORT %d' % web_port)
        command(db, 'WEBMONITOR ON')
        command(db, 'SELECT 1')
        command(db, 'TAKE')
        time.sleep(3)
        # Keep the actual primary tap active; /snap alone does not add a viewer.
        with socket.create_connection(('127.0.0.1', web_port), timeout=10) as viewer:
            viewer.sendall(b'GET /out/1 HTTP/1.1\r\nHost: localhost\r\n\r\n')
            time.sleep(1)
            errors = []
            for held in (False, True):
                if held:
                    command(db, 'PAUSE')
                    time.sleep(0.5)
                for sample in range(3):
                    jpeg = urlopen('http://127.0.0.1:%d/snap/1' % web_port, timeout=10).read()
                    measured = patch_means(decode_rgb(ffmpeg, jpeg))
                    error = max(abs(a - b) for got, want in zip(measured, reference)
                                for a, b in zip(got, want))
                    errors.append(error)
                    (work / ('%d-%s-%s-%d.jpg' %
                     (bits, range_name, 'paused' if held else 'playing', sample))).write_bytes(jpeg)
                    time.sleep(0.2)
            # JPEG and the 10-to-8-bit bridge can introduce a few code values
            # of quantisation. Wrong PQ decoding changes patches by tens to
            # hundreds; a six-value bound also detects range/matrix mistakes.
            peak = max(errors)
            print('%d-bit SDR %s: primary output peak patch error %.2f/255 %s' %
                  (bits, range_name, peak, 'ok' if peak <= 6 else 'FAIL'), flush=True)
            if peak > 6:
                raise RuntimeError('primary programme colour differs from decoded reference')
        if case_number == 0 and args.web_player:
            command(db, 'PAUSE')  # resume audio/video transport
            check_web_player(db, web_port, ffprobe, work)
        if args.keep:
            print('isolated root:', db.root, flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe', default=default_exe())
    ap.add_argument('--ffmpeg', default='')
    ap.add_argument('--renderer', default='direct3d11' if os.name == 'nt' else 'software')
    ap.add_argument('--port', type=int, default=5770)
    ap.add_argument('--web-port', type=int, default=18794)
    ap.add_argument('--keep', action='store_true')
    ap.add_argument('--web-player', action='store_true', help='also check programme A/V and monitor lifecycle')
    args = ap.parse_args()
    args.exe = str(Path(args.exe).resolve())
    ffmpeg = find_ffmpeg(args.exe, args.ffmpeg) or shutil.which('ffmpeg')
    if not ffmpeg:
        ap.error('ffmpeg is required')
    ffprobe = str(Path(ffmpeg).with_name('ffprobe.exe' if os.name == 'nt' else 'ffprobe'))
    if not Path(ffprobe).is_file():
        ffprobe = shutil.which('ffprobe')
    if not ffmpeg or not ffprobe or not Path(args.exe).is_file():
        ap.error('Deckboy, ffmpeg and ffprobe are required')
    os.environ['DECKBOY_OUTPUT_RENDERER'] = args.renderer
    os.environ['DECKBOY_EGRESS_READBACK'] = 'sync'
    work = Path(tempfile.mkdtemp(prefix='deckboy-colour-references-'))
    try:
        for case_number, (bits, range_name) in enumerate(
                [(10, 'limited'), (10, 'full'), (8, 'limited'), (8, 'full')]):
            media = fixture(ffmpeg, work, bits, range_name)
            run_case(args, media, bits, range_name, case_number, ffmpeg, ffprobe, work)
        return 0
    finally:
        if args.keep:
            print('colour reference evidence:', work, flush=True)
        else:
            shutil.rmtree(work)


if __name__ == '__main__':
    sys.exit(main())
