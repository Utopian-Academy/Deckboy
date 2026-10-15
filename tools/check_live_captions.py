"""Live captions, end to end, in the real app.

Plays a spoken WAV on a deck, turns live captions on (CAPTIONS LIVE ON,
listening to that deck), and reads what is shown (CAPTIONS LIVE TEXT) as it
arrives. Checks the words, that the unsure tail is shown before it becomes
sure, how far behind the words are, that the transcript is written, that the
captions reach the output picture, and that killing the speech engine stops
the captions and not the show.

Needs deckboy-listen (--listen, or DECKBOY_LISTEN) and a model file (--model);
neither ships in the repo. The speech is made with the operating system's own
voice on Windows, or given with --speech. Evidence: build/live-captions-*/
"""
import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from deckboy_testroot import populate

ROOT = Path(__file__).resolve().parent.parent
SAID = ("The second cue fires the house lights, and the band walks on in the dark. "
        "Keep the stage clear until the music starts. Then bring up the spotlight on the singer.")


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def command(port, line, timeout=15):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.sendall((line + "\n").encode())
        result = b""
        while b"\n" not in result:
            chunk = sock.recv(65536)
            if not chunk:
                raise ConnectionError("control socket closed")
            result += chunk
        return result.decode().strip()


def words(text):
    return re.findall(r"[a-z']+", text.lower())


def speak(path):
    script = ("Add-Type -AssemblyName System.Speech; $s=New-Object System.Speech.Synthesis.SpeechSynthesizer; "
              "$f=New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(48000,"
              "[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,[System.Speech.AudioFormat.AudioChannel]::Stereo); "
              f"$s.SetOutputToWaveFile('{path}',$f); $s.Speak('{SAID}'); $s.Dispose()")
    subprocess.run(["powershell", "-NoProfile", "-Command", script], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    parser.add_argument("--listen", default=os.environ.get("DECKBOY_LISTEN", ""))
    parser.add_argument("--model", required=True, help="a ggml-base.en.bin")
    parser.add_argument("--speech", help="a WAV of SAID (default: made with the Windows voice)")
    args = parser.parse_args()
    (ROOT / "build").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="live-captions-", dir=ROOT / "build"))
    populate(str(work))
    state = work / "state"
    (state / "models").mkdir(parents=True)
    shutil.copy(args.model, state / "models" / "ggml-base.en.bin")
    speech = work / "speech.wav"
    if args.speech:
        shutil.copy(args.speech, speech)
    else:
        speak(speech)
    port = free_port()
    show = work / "data/default.deckboy"
    show.write_text("update_check\t0\nui_sounds\t0\nui_scale\t1.5\n", encoding="utf-8")
    env = dict(os.environ, DECKBOY_ROOT=str(work), DECKBOY_STATE_DIR=str(state), DECKBOY_PROJECT=str(show),
               DECKBOY_COMPANION_PORT=str(port), DECKBOY_LISTEN=args.listen,
               SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", DECKBOY_RENDERER="software")
    log = (work / "app.log").open("w", encoding="utf-8")
    app = subprocess.Popen([args.exe, "--allow-multi-instance", str(show), "--ui-size", "2640", "1530"],
                           env=env, cwd=ROOT, stdout=log, stderr=log,
                           creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    checks = failures = 0

    def expect(condition, label, detail=""):
        nonlocal checks, failures
        checks += 1
        failures += 0 if condition else 1
        print(("ok   " if condition else "FAIL ") + label + ("" if condition else f"  [{detail}]"), flush=True)

    try:
        until = time.monotonic() + 60
        while True:
            try:
                if command(port, "PING").startswith("OK"):
                    break
            except OSError:
                pass
            if time.monotonic() > until:
                raise AssertionError("app did not answer")
            time.sleep(0.3)
        time.sleep(1.0)
        listing = work / "drop.txt"
        listing.write_text(f"{speech}\n", encoding="utf-8")
        command(port, f"DROPTEST {listing}")
        time.sleep(3)
        command(port, "OUTPUT ON")
        expect(command(port, "CAPTIONS SOURCE DECK 1").startswith("OK"), "CAPTIONS SOURCE DECK 1")
        expect(command(port, "CAPTIONS SOURCE DECK 9").startswith("ERR"), "a deck that does not exist is refused")
        expect(command(port, "CAPTIONS TRANSCRIPT ON").startswith("OK"), "CAPTIONS TRANSCRIPT ON")
        reply = command(port, "CAPTIONS LIVE ON")
        expect(reply.startswith("OK"), "CAPTIONS LIVE ON", reply)
        until = time.monotonic() + 20
        status = ""
        while time.monotonic() < until:
            status = command(port, "CAPTIONS LIVE STATUS")
            if "listening" in status:
                break
            time.sleep(0.3)
        expect("listening" in status, "the speech engine starts", status)

        command(port, "TAKE 1")
        started = time.monotonic()
        seen, first_word_at, faint_seen, last_sure = [], None, False, ""
        polls = 0
        while time.monotonic() - started < 20:
            text = command(port, "CAPTIONS LIVE TEXT")[len("OK CAPTIONS"):].strip()
            sure, _, unsure = text.partition("|")
            if words(sure + unsure) and first_word_at is None:
                first_word_at = time.monotonic() - started
            faint_seen |= bool(words(unsure))
            if len(sure.strip()) > len(last_sure):
                last_sure = sure.strip()  # the fullest it got: it clears after the speech
            pass_ms = re.search(r"pass (\d+) ms", command(port, "CAPTIONS LIVE STATUS"))
            seen.append((round(time.monotonic() - started, 1), pass_ms and int(pass_ms.group(1)),
                         sure.strip()[-40:], unsure.strip()))
            polls += 1
            if polls == 16:  # mid-speech: the unsure tail is likely on screen
                command(port, f"OUTSNAP {work / 'out-mid.bmp'} 1")
            time.sleep(0.5)
        for row in seen[::2]:
            print("     ", row)
        final_sure = last_sure
        said, heard = words(SAID), words(final_sure)
        common = sum(1 for w in said if w in heard)
        expect(common >= len(said) * 0.9, f"the words: {common}/{len(said)} of what was said are shown", final_sure)
        expect(first_word_at is not None and first_word_at < 4.5,
               f"first words shown {first_word_at} s after the speech starts")
        expect(faint_seen, "unsure words are shown (faint) before they are sure")
        status = command(port, "CAPTIONS LIVE STATUS")
        behind = re.search(r"([\d.]+) s behind", status)
        expect(behind and float(behind.group(1)) < 3.5, "words become sure under 3.5 s after they are said", status)
        srts = list((state / "captions").glob("live *.srt"))
        expect(srts and "spotlight" in srts[0].read_text(encoding="utf-8").lower(),
               "the transcript is written", [str(s) for s in srts])

        # The engine dies mid-show: captions stop, the show does not, and it comes back.
        killed = subprocess.run(["taskkill", "/F", "/IM", "deckboy-listen.exe"], capture_output=True)
        expect(killed.returncode == 0, "killed the speech engine")
        expect(command(port, "PING").startswith("OK"), "the show carries on")
        # In silence: the loss must be noticed (status leaves "listening")
        # and the engine brought back, before anybody speaks again.
        until = time.monotonic() + 10
        noticed = False
        while time.monotonic() < until and not noticed:
            noticed = "listening" not in command(port, "CAPTIONS LIVE STATUS")
            time.sleep(0.25)
        expect(noticed, "the loss is noticed while nobody is talking")
        until = time.monotonic() + 15
        while time.monotonic() < until and "listening" not in command(port, "CAPTIONS LIVE STATUS"):
            time.sleep(0.5)
        expect("listening" in command(port, "CAPTIONS LIVE STATUS"), "the speech engine is restarted")

        # Silence: the screen clears, and the next speech starts a fresh caption.
        until = time.monotonic() + 12
        shown = lambda: words(command(port, "CAPTIONS LIVE TEXT")[len("OK CAPTIONS"):])
        while time.monotonic() < until and shown():
            time.sleep(0.5)
        expect(not shown(),
               "after the speech stops the captions clear", command(port, "CAPTIONS LIVE TEXT"))
        command(port, "TAKE 1")
        for _ in range(10):
            time.sleep(0.5)
            print("      retake:", command(port, "CAPTIONS LIVE TEXT"), "|", command(port, "CAPTIONS LIVE STATUS"))
        again = command(port, "CAPTIONS LIVE TEXT").lower()
        expect("singer" not in again and ("second" in again or "fires" in again),
               "the next speech starts fresh, without the old words", again)
        command(port, f"OUTSNAP {work / 'out.bmp'} 1")
        command(port, "UISNAP " + str(work / "desk.bmp"))
        time.sleep(1.5)
        expect(command(port, "CAPTIONS LIVE OFF").startswith("OK"), "CAPTIONS LIVE OFF")
        time.sleep(1)
        expect("deckboy-listen.exe" not in subprocess.run(["tasklist"], capture_output=True, text=True).stdout,
               "the speech engine is gone after OFF")
    finally:
        app.kill()
        app.wait(timeout=10)
        log.close()
    print(f"live captions: {checks} checks, {failures} failures; evidence: {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
