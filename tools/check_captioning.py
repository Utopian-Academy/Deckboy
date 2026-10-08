"""Captions made on this machine, for a cue that has none.

WHY THIS EXISTS. James, 2026-10-07: "is there any way to have deckboy
generate captions if there are none, optionally?" This drives the real path:
a spoken clip, CAPTIONS GENERATE, whisper-cli listening, an SRT beside the
media attached to the cue -- and checks the words, not just that a file
appeared. It also checks the opt-in: without the model, the first GENERATE
only asks.

Needs whisper-cli and a model: --whisper <path to whisper-cli> --model <ggml .bin>.
Windows makes the spoken clip with its own voice (System.Speech).

    python tools/check_captioning.py --whisper path/whisper-cli.exe --model path/ggml-base.en.bin
"""

import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from deckboy_harness import Deckboy, parse_args  # noqa: E402


def main():
    argv = sys.argv
    whisper = model = ""
    for flag in ("--whisper", "--model"):
        if flag in argv:
            i = argv.index(flag)
            if flag == "--whisper":
                whisper = argv[i + 1]
            else:
                model = argv[i + 1]
            del argv[i:i + 2]
    args = parse_args(__doc__, 5851)
    if not (whisper and model and os.path.exists(whisper) and os.path.exists(model)):
        sys.exit("needs --whisper and --model")
    work = tempfile.mkdtemp(prefix="deckboy-captions-")
    speech = os.path.join(work, "welcome.wav")
    ps = ("Add-Type -AssemblyName System.Speech; $s = New-Object System.Speech.Synthesis.SpeechSynthesizer; "
          "$s.SetOutputToWaveFile('%s'); $s.Speak('Good evening, and welcome to the show. "
          "Please take your seats.'); $s.Dispose()" % speech)
    subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True)
    if not os.path.exists(speech):
        sys.exit("could not make the spoken clip")
    fails = []

    def note(name, ok, info=""):
        print("  %-50s %s" % (name, "ok" if ok else "FAIL  " + info))
        if not ok:
            fails.append(name)

    os.environ["DECKBOY_WHISPER"] = whisper
    with Deckboy(args, prefix="deckboy-captions-", extra_args=["--import", speech]) as db:
        print("check: captions made on this computer")
        print()
        db.send("SELECT 1")
        time.sleep(1.0)
        reply = db.send("CAPTIONS GENERATE")
        note("without the model, GENERATE only asks", "ask again" in reply, reply)
        models = os.path.join(db.root, "data", "models")
        os.makedirs(models, exist_ok=True)
        shutil.copy(model, os.path.join(models, os.path.basename(model)))
        reply = db.send("CAPTIONS GENERATE")
        note("with the model, it starts listening", "started" in reply, reply)
        status = ""
        for _ in range(120):
            status = db.send("CAPTIONS")
            if "idle" in status:
                break
            time.sleep(0.5)
        note("the cue gets its captions, switched on", "welcome.auto.srt on" in status, status)
        srt = os.path.join(work, "welcome.auto.srt")
        text = open(srt, encoding="utf-8", errors="replace").read() if os.path.exists(srt) else ""
        note("beside the media, and the words are right",
             "welcome to the show" in text.lower() and "seats" in text.lower(), text[:120])
    print()
    print("%d FAILED" % len(fails) if fails else "all ok")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
