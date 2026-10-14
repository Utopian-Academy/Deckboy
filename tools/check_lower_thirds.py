"""Capture the real lower-third entrances through the controller protocol.

The sheets are for visual review; different pixels do not prove good design.
All runtime files stay under the chosen worktree, including the isolated show.
Pass --verify to fail on blank/clipped output, inert motion, non-repeatable
seeks, or cuts that wait for the animation duration. This launches app/output
windows: use a test desktop or an isolated display session.
"""
import argparse
import json
import math
import os
from pathlib import Path
import re
import struct
import tempfile
import time
import wave

from PIL import Image, ImageChops, ImageDraw, ImageOps
from deckboy_harness import Deckboy, REPO


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True)
    parser.add_argument('--looks', default='')
    parser.add_argument('--out', default='lt_sheets')
    parser.add_argument('--port', type=int, default=19483)
    parser.add_argument('--raster', default='1280x720', choices=['1280x720', '3840x2160'])
    parser.add_argument('--script', default='latin', choices=['latin', 'arabic'])
    parser.add_argument('--seek', action='store_true', help='sample exact cue times while paused')
    parser.add_argument('--verify', action='store_true',
                        help='also check rendered entrances, exits, cuts, Wiggle and deterministic seeks')
    parser.add_argument('--audio', action='store_true',
                        help='check Equalizer with a separate audio playlist using dummy audio')
    parser.add_argument('--move', default='fade', choices=['left', 'right', 'rise', 'fade', 'pop', 'wipe', 'grow', 'typewriter', 'cut'])
    args = parser.parse_args()
    args.seek = args.seek or args.verify
    args.exe = os.path.abspath(args.exe)
    args.keep = True
    width, height = map(int, args.raster.split('x'))
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    runtime = Path(REPO) / 'build' / 'lt_runtime'
    runtime.mkdir(parents=True, exist_ok=True)
    tempfile.tempdir = str(runtime)
    os.environ.update(TEMP=str(runtime), TMP=str(runtime), DECKBOY_STATE_DIR='')
    if args.audio:
        os.environ['SDL_AUDIO_DRIVER'] = 'dummy'
    source = (Path(REPO) / 'native/core/types.hpp').read_text(encoding='utf-8')
    looks = re.findall(r'case LowerThirdLook::\w+:\s*return "(\w+)";', source)
    if args.looks:
        looks = args.looks.split(',')
    timings = [0.20, 0.65, 1.10, 1.65, 2.35, 3.15, 4.10]
    records = []
    extra_args = ['--allow-multi-instance']
    if args.audio:
        # An ordinary audio cue exercises PLAY/PAUSE/STOP. The indefinite
        # Tone generator has different transport semantics in this branch.
        audio_path = out / 'programme.wav'
        samples = (int(4096 * math.sin(i * math.tau / 48)) for i in range(48))
        cycle = b''.join(struct.pack('<hh', sample, sample) for sample in samples)
        with wave.open(str(audio_path), 'wb') as audio:
            audio.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
            for unused in range(120):
                audio.writeframesraw(cycle * 1000)
        extra_args += ['--import', str(audio_path)]
    with Deckboy(args, 'lower-thirds-', extra_args=extra_args) as app:
        def command(value):
            reply = app.send(value)
            if not reply.startswith('OK'):
                raise RuntimeError(f'{value}: {reply}')
            return reply

        def capture(name):
            path = out / (name + '.bmp')
            if path.exists():
                path.unlink()
            command('OUTSNAP ' + str(path))
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if path.exists():
                    try:
                        with Image.open(path) as im:
                            if im.size != (width, height):
                                raise RuntimeError(f'{path.name}: unexpected raster {im.size}')
                            frame = im.convert('RGB').copy()
                        path.unlink()
                        return frame
                    except OSError:
                        pass
                time.sleep(0.02)
            raise RuntimeError('No complete snapshot: ' + str(path))

        def at(moment, name):
            command('SEEK ' + str(moment))
            time.sleep(0.12)
            return capture(name)

        title, subtitle = ('Name Surname', 'Title or role') if args.script == 'latin' else (
            '\u0645\u0631\u064a\u0645 \u0623\u062d\u0645\u062f',
            '\u0645\u062f\u064a\u0631\u0629 \u0627\u0644\u0625\u0646\u062a\u0627\u062c')
        for value in ['VIDEO ' + args.raster, 'LOWERTHIRD',
                      'TEXTCUE TITLE ' + title, 'TEXTCUE SUB ' + subtitle,
                      'LOWERANIM 3', 'TEXTCUE IN ' + args.move,
                      'TEXTCUE OUTMOVE ' + args.move, 'TEXTCUE WIGGLE OFF']:
            command(value)
        # The window initially inherits its display's raster. Set the programme
        # size before arming it, then let the output rebuild reach the next frame.
        command('OUTPUT ON')
        time.sleep(0.6)
        for bad in ['TEXTCUE WIGGLE', 'TEXTCUE WIGGLE MAYBE', 'TEXTCUE WIGGLE ON extra']:
            assert app.send(bad).startswith('ERR'), bad
        for value in ['ON', 'TOGGLE', 'OFF']:
            assert command('TEXTCUE WIGGLE ' + value).endswith('on' if value == 'ON' else 'off')
        for look in looks:
            command('TEXTCUE LOOK ' + look)
            command('TAKE')
            if args.seek:
                command('PAUSE')
            started = time.monotonic()
            frames, actual = [], []
            for index, moment in enumerate(timings):
                if args.seek:
                    command('SEEK ' + str(moment))
                    time.sleep(0.12)
                else:
                    time.sleep(max(0, started + moment - time.monotonic()))
                frames.append(capture(f'{look}_{index}'))
                actual.append(moment if args.seek else round(time.monotonic() - started, 3))
            # Exit is sampled too: the material must reassemble in reverse.
            command('TEXTCUE OUT')
            if args.seek:
                command('SEEK ' + str(timings[-1] + 1.35))
                time.sleep(0.12)
            else:
                time.sleep(1.35)
            frames.append(capture(f'{look}_out'))
            # One crop for the whole move keeps its travel visible. A settled
            # card enlarged separately makes the material legible for review.
            bounds = [frame.getbbox() for frame in frames if frame.getbbox()]
            crop = (max(0, min(b[0] for b in bounds) - 24),
                    max(0, min(b[1] for b in bounds) - 24),
                    min(width, max(b[2] for b in bounds) + 24),
                    min(height, max(b[3] for b in bounds) + 24))
            tiles = []
            for index, frame in enumerate(frames):
                tile = Image.new('RGB', (640, 340), '#24272b')
                picture = ImageOps.contain(frame.crop(crop), (640, 320))
                tile.paste(picture, ((640 - picture.width) // 2, 20 + (320 - picture.height) // 2))
                ImageDraw.Draw(tile).text((8, 2), look + (' OUT +1.35' if index == 7 else f' {actual[index]:.2f}s'), fill='white')
                tiles.append(tile)
            sheet = Image.new('RGB', (1280, 1360), '#24272b')
            for index, tile in enumerate(tiles):
                sheet.paste(tile, ((index % 2) * 640, (index // 2) * 340))
            sheet.save(out / (look + '_entrance.jpg'), quality=94)
            # Shared crops make the temporal sequence comparable; keep an
            # animated proof alongside the still contact sheet for playback.
            animation = [ImageOps.contain(frame.crop(crop), (960, 540)) for frame in frames]
            animation[0].save(out / (look + '_motion.gif'), save_all=True,
                              append_images=animation[1:], duration=250, loop=0)
            frames[5].save(out / (look + '_settled.png'))
            if args.verify:
                def changed(a, b):
                    return ImageChops.difference(a, b).getbbox() is not None

                box = frames[5].getbbox()
                # Glass deliberately spans the full frame; its horizontal
                # edges are the design, not clipped nameplate furniture.
                fits_width = box and (look == 'glass' or 0 < box[0] < box[2] < width)
                assert fits_width and 0 < box[1] < box[3] < height, (
                    look, 'settled graphic empty or clipped', box)
                if args.move != 'cut':
                    assert changed(frames[0], frames[5]), look + ': entrance did not move'
                assert not at(7.2, look + '_gone').getbbox(), look + ': OUT did not clear'

                # Retake clears the OUT timestamp; PAUSE and SEEK exercise the
                # same cue clock that the operator controls, including rewind.
                command('TAKE')
                command('PAUSE')
                off = at(4.1, look + '_off')
                command('TEXTCUE WIGGLE ON')
                first = at(4.1, look + '_wiggle_a')
                second = at(4.4, look + '_wiggle_b')
                replay = at(4.1, look + '_wiggle_replay')
                assert changed(first, second), look + ': Wiggle did not move'
                assert not changed(first, replay), look + ': seek was not repeatable'
                assert changed(first, off) == (look != 'arcade'), look + ': Wiggle toggle mismatch'
                command('TEXTCUE WIGGLE OFF')

                # A cut has no animation interval, including at cue time zero
                # and at the exact time the operator presses OUT.
                command('TEXTCUE IN cut')
                command('TEXTCUE OUTMOVE cut')
                command('TAKE')
                command('PAUSE')
                assert at(0, look + '_cut_in').getbbox(), look + ': cut entrance is blank at zero'
                at(4.1, look + '_cut_hold')
                command('TEXTCUE OUT')
                assert not at(4.1, look + '_cut_out').getbbox(), look + ': cut OUT waited for out time'
                command('TEXTCUE IN ' + args.move)
                command('TEXTCUE OUTMOVE ' + args.move)
            records.append(dict(look=look, actual_seconds=actual))
            print(look, actual, flush=True)
        if args.audio:
            # A paused graphic makes time a controlled variable. Compare only
            # its opaque band, located before adding the audio host underneath.
            for value in ['TEXTCUE LOOK equalizer', 'TEXTCUE IN cut', 'TEXTCUE WIGGLE OFF',
                          'TAKE', 'PAUSE']:
                command(value)
            band = at(4.1, 'audio_band').getbbox()
            assert band, 'Equalizer band was empty'
            # The harness mutes the master on launch. Decoded audio analysis
            # follows that gain; dummy audio keeps this fixture inaudible.
            for value in ['STOP', 'SELECT 1', 'MASTERVOL 100', 'TAKE', 'DECKADD', 'DECK 2',
                          'LOWERTHIRD', 'TEXTCUE TITLE ' + title, 'TEXTCUE SUB ' + subtitle,
                          'TEXTCUE LOOK equalizer', 'TEXTCUE IN cut', 'TEXTCUE WIGGLE OFF',
                          'VIDEO OUTPUT 1', 'VIDEO OUTPUT ASSIGN 2', 'TAKE', 'PAUSE', 'SEEK 4.1']:
                command(value)
            layers = command('VIDEO OUTPUT LAYER')
            assert 'Deck 1' in layers and 'Deck 2' in layers, layers

            audio_states = []

            def audio_frame(name):
                frame = capture(name).crop(band)
                frame.save(out / (name + '.png'))
                audio_states.append(name + '\n' + app.send('STATUS'))
                (out / 'audio_states.txt').write_text('\n\n'.join(audio_states), encoding='utf-8')
                return frame

            time.sleep(1)
            loud = audio_frame('audio_loud')
            # AUDIOGAIN changes decoded samples, taking this -18 dBFS signal
            # below the meter floor without changing the graphic's clock.
            command('DECK 1')
            assert app.status('DECK 1 ', 'status') == 'Playing', app.send('STATUS')
            command('AUDIOGAIN -40')
            time.sleep(2)
            quiet = audio_frame('audio_quiet')
            assert ImageChops.difference(loud, quiet).getbbox(), 'Equalizer ignored host audio'
            command('AUDIOGAIN 0')
            time.sleep(1)
            restored = audio_frame('audio_restored')
            assert ImageChops.difference(restored, quiet).getbbox(), 'Equalizer did not recover'
            command('PAUSE')
            time.sleep(0.2)
            assert app.status('DECK 1 ', 'status') == 'Paused', app.send('STATUS')
            paused = audio_frame('audio_paused')
            assert not ImageChops.difference(paused, quiet).getbbox(), (
                'Equalizer retained paused host level',
                ImageChops.difference(paused, quiet).getbbox())
            command('PLAY')
            time.sleep(1)
            assert app.status('DECK 1 ', 'status') == 'Playing', app.send('STATUS')
            resumed = audio_frame('audio_resumed')
            assert ImageChops.difference(resumed, paused).getbbox(), 'Equalizer did not resume'
            command('STOP')
            time.sleep(0.2)
            assert app.status('DECK 1 ', 'status') == 'Stopped', app.send('STATUS')
            stopped = audio_frame('audio_stopped')
            assert not ImageChops.difference(stopped, quiet).getbbox(), 'Equalizer retained stopped host level'
            print('Equalizer: host audio, silence, recovery, pause, resume and stop checks passed', flush=True)
        (out / 'capture.json').write_text(json.dumps(dict(root=app.root, raster=args.raster,
            script=args.script, move=args.move, seek=args.seek, verified=args.verify,
            audio_verified=args.audio, records=records), indent=2))
    settled = sorted(out.glob('*_settled.png'))
    sheet = Image.new('RGB', (1920, ((len(settled) + 3) // 4) * 224), '#24272b')
    for index, path in enumerate(settled):
        tile = Image.new('RGB', (480, 224), '#24272b')
        with Image.open(path) as frame:
            box = frame.convert('RGB').getbbox()
            if box:
                box = (max(0, box[0] - 16), max(0, box[1] - 16),
                       min(frame.width, box[2] + 16), min(frame.height, box[3] + 16))
                picture = ImageOps.contain(frame.crop(box), (480, 200))
                tile.paste(picture, ((480 - picture.width) // 2, 24 + (200 - picture.height) // 2))
        ImageDraw.Draw(tile).text((10, 6), path.stem.removesuffix('_settled'), fill='white')
        sheet.paste(tile, ((index % 4) * 480, (index // 4) * 224))
    sheet.save(out / 'all_settled.jpg', quality=95)
    if args.verify:
        print(f'Verified {len(records)} looks: entrance, OUT, Wiggle, seek and cut checks passed', flush=True)
    print('Sheets:', out, flush=True)


if __name__ == '__main__':
    main()
