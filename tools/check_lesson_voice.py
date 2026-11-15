"""The lesson voices in the real app: CHIP, WORDS, SOUNDS and OFF.

Starts a lesson with each voice (LESSONS VOICE <v>, LESSONS START layers) and
reads what the UI sound stream was given (DECKBOY_UI_SOUND_CAPTURE: 16-bit
stereo at 48 kHz, exactly what goes to the speakers). The dummy audio driver
keeps it silent. Checks: CHIP, WORDS and SOUNDS are heard, OFF is not; the
Experimental voice is speech-length and has no click (a one-sample step over
half scale); the setting loads from a show file; a bad word is refused.

  py -3 tools/check_lesson_voice.py [--exe build/windows/Release/Deckboy.exe]
"""
import argparse
import os
import struct
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from example_show_runtime import Runtime  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def samples(path):
    if not path.exists():
        return []
    data = path.read_bytes()
    n = len(data) // 4
    return [struct.unpack_from("<h", data, i * 4)[0] / 32768.0 for i in range(n)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="deckboy-lesson-voice-"))
    failures = 0

    def expect(ok, label, detail=""):
        nonlocal failures
        failures += 0 if ok else 1
        print(("ok   " if ok else "FAIL ") + label + ("" if ok else "  [%s]" % detail), flush=True)

    heard = {}
    for voice in ("CHIP", "WORDS", "SOUNDS", "OFF"):
        cap = work / ("ui-%s.raw" % voice.lower())
        show = work / ("show-%s.deckboy" % voice.lower())
        # WORDS comes from the show file itself (the load path); the
        # other two from the remote verb.
        chosen = "lesson_voice\t1\n" if voice == "WORDS" else ""
        show.write_text("update_check\t0\nui_sounds\t1\n" + chosen, encoding="utf-8")
        env = {"DECKBOY_UI_SOUND_CAPTURE": str(cap), "SDL_AUDIO_DRIVER": "dummy", "DECKBOY_HIDE_WINDOWS": "1"}
        with Runtime(args.exe, work / voice.lower(), show, env=env) as db:
            if voice != "WORDS":
                reply = db.send("LESSONS VOICE %s" % voice)
                expect(reply.startswith("OK"), "LESSONS VOICE %s" % voice, reply)
            # The boot jingle holds every other UI sound back for ~5 s; a
            # lesson started under it would have said its first lines unheard.
            time.sleep(6)
            # Only what the lesson says from here: skip what is captured so far.
            skip = cap.stat().st_size // 4 if cap.exists() else 0
            if voice == "CHIP":
                try:
                    bad = db.send("LESSONS VOICE KAZOO")
                except RuntimeError as e:   # the helper raises on ERR
                    bad = str(e).split(" => ", 1)[-1]
                expect(bad.startswith("ERR"), "LESSONS VOICE KAZOO is refused", bad)
            reply = db.send("LESSONS START layers")
            expect(reply.startswith("OK"), "a lesson starts", reply)
            time.sleep(9)
        x = samples(cap)[skip:]
        heard[voice] = x
        secs = len(x) / 48000.0
        if voice == "OFF":
            expect(secs == 0.0, "OFF: nothing is played", "%.2f s played" % secs)
        else:
            expect(secs > 1.0, "%s: the teachers are heard" % voice, "%.2f s played" % secs)
        if voice in ("WORDS", "SOUNDS") and x:
            step = max(abs(b - a) for a, b in zip(x, x[1:]))
            peak = max(abs(v) for v in x)
            expect(step < 0.5, voice + ": no clicks (biggest step %.2f)" % step, "%.2f" % step)
            expect(0.1 < peak <= 1.0, voice + ": a sensible level (peak %.2f)" % peak, "%.2f" % peak)
    if heard.get("CHIP") and heard.get("WORDS") and heard.get("SOUNDS"):
        expect(len({tuple(heard[k][:4000]) for k in ("CHIP", "WORDS", "SOUNDS")}) == 3, "the three voices differ")
    print("lesson voices: %d failures; evidence: %s" % (failures, work))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
