"""Open relocated example shows in Deckboy and verify their actual playback.

Requires Pillow for frame inspection. All app state and captures stay in build/.
python tools/check_example_shows.py --exe path/to/Deckboy
"""
import argparse
from array import array
import json
import hashlib
import math
from pathlib import Path
import re
import shutil
import subprocess
import time
import wave

from PIL import Image, ImageDraw, ImageStat

from audit_record_layout import writer_order
from example_show_runtime import ROOT, Runtime

FIELDS = {member: column for column, member in writer_order(
    (ROOT / "native/core/project_file.ipp").read_text(encoding="utf8")) if member}


def verify_design(slug, cues):
    # Inspect independent cue settings: a shared edit selection must not turn
    # the reference cue into another copy of the look it is meant to compare.
    swirls = [c for c in cues if c[2] == "pattern://swirl"]
    expected = {"01-picture-on-screen": [0], "02-running-a-show": [], "03-layers-and-titles": [0, 2, 2, 3],
                "04-looks-and-effects": [3] * 6, "05-one-button-show": [0, 2, 1]}
    assert [int(c[FIELDS["swirl.palette"]]) for c in swirls] == expected[slug]
    if slug == "02-running-a-show":
        # The running order's lessons are the cue settings themselves.
        clips = [c for c in cues if c[4] == "video"]
        assert len(clips) == 4 and all(c[2].startswith("media/tears-of-steel-") for c in clips)
        assert float(clips[0][FIELDS["cueTransitionSeconds"]]) == 2
        assert clips[1][FIELDS["pauseOnLastFrame"]] == "0" and clips[0][FIELDS["pauseOnLastFrame"]] == "1"
        assert clips[2][FIELDS["loop"]] == "1"
        assert float(clips[3][FIELDS["fadeOutSeconds"]]) == 3
    if slug == "01-picture-on-screen":
        assert "press N" not in "".join(c[33] for c in cues), "the N key is NDI, not the output"
    if slug == "04-looks-and-effects":
        fx = [c[FIELDS["effects"]] for c in cues]
        assert not fx[0] and "kaleidoscope:" in fx[1]
        assert "channel_offset:" in fx[2] and "scanlines:" in fx[2]
        assert "ripple:" in fx[3] and "text_mode:" in fx[4]
        assert [float(c[34]) for c in cues] == [1, 1, 1, 1, 1, .7]


def deck_status(reply, deck):
    line = next(line for line in reply.splitlines() if line.startswith(f"DECK {deck} "))
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def frame(db, path, black=False):
    db.snapshot(path, output=True)
    with Image.open(path) as im:
        image = im.convert("RGB")
    stats = ImageStat.Stat(image)
    mean = sum(stats.mean) / 3
    if black:
        assert mean < 2, (path, "expected black", mean)
    else:
        assert max(stats.stddev) > 8, (path, "expected a picture", stats.stddev)
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--pack", default=str(ROOT / "data/demos/starter-pack"))
    args = parser.parse_args()
    source = Path(args.pack).resolve()
    report_dir = ROOT / "build/examples/check"
    # A different location with spaces exercises portable media paths.
    moved = report_dir / "Moved shows"
    shutil.copytree(source, moved, dirs_exist_ok=True)
    manifest = json.loads((source / "manifest.json").read_text(encoding="utf8"))
    report, thumbnails = [], []
    for info in manifest["shows"]:
        slug = Path(info["file"]).stem
        show = moved / info["file"]
        original = show.read_text(encoding="utf8")
        records = [line.split("\t") for line in original.splitlines()]
        cues = [f for f in records if f[0] == "cue" and int(f[1]) >= 0]
        verify_design(slug, cues)
        assert len(cues) == sum(n for d, n in info["cue_counts"].items() if int(d) >= 0)
        assert all(not re.match(r"^[A-Za-z]:", f[2]) for f in cues)
        assert all((show.parent / f[2]).is_file() for f in cues if f[4] in ("audio", "video", "image"))
        work = report_dir / slug
        recording_dir = work / "recordings"
        recording_dir.mkdir(parents=True, exist_ok=True)
        # Test recordings go to this run's directory, never the user's Videos.
        show.write_text(original + "recording_dir\t" + recording_dir.as_posix() + "\n", encoding="utf8")
        item = {"file": info["file"], "cue_count": len(cues),
                "sha256": hashlib.sha256((source / info["file"]).read_bytes()).hexdigest()}
        with Runtime(args.exe, work, show) as db:
            check = db.send("CHECK")
            assert "nothing broken" in check, check
            item["check"] = check
            # A loaded show must really accept edits, including replacement of
            # an existing saved file. An OK control reply alone cannot prove it.
            marker = "Example autosave verification - " + slug
            db.commands("DECK 1", "CUENOTES 1 " + marker)
            assert marker in db.saved(), "First autosave did not reach disk"
            db.send("CUENOTES 1 " + marker + " second edit")
            assert marker + " second edit" in db.saved(), "Replacement autosave failed"
            db.send("CUENOTES 1 " + cues[0][33])
            restored = db.saved()
            assert marker not in restored and cues[0][33] in restored
            item["autosave"] = "two edits and restoration written successfully"
            db.send("DASH 1")
            time.sleep(.8)
            if not info["steps"]:
                for number in range(1, len(cues) + 1):
                    db.commands("DECK 1", f"SELECT {number}", "TAKE")
                    time.sleep(1.2)
                    status = deck_status(db.send("STATUS"), 1)
                    assert status["active"] == str(number), status
                    picture = frame(db, work / f"cue-{number}.bmp")
                    if number == (3 if slug.startswith("02") else 2):
                        thumbnails.append((info["title"], picture.copy()))
                    print(f"{slug}: cue {number} rendered", flush=True)
            elif not info["run_seconds"]:
                expected = [(1, 1, 1), (2, 1, 1), (2, 2, 2), (3, 3, 1), (1, 3, 3)]
                for number, targets in enumerate(expected, 1):
                    db.send(f"DASH {number + 2}")
                    time.sleep(1.5)
                    status = db.send("STATUS")
                    for deck, target in enumerate(targets, 1):
                        actual = deck_status(status, deck)
                        assert actual["active"] == str(target), actual
                    picture = frame(db, work / f"step-{number}.bmp")
                    if number == 1:
                        thumbnails.append((info["title"], picture.copy()))
                    print(f"{slug}: scene {number} rendered", flush=True)
                db.send("TRACKER STEP 1")
            else:
                db.send("RECORD START")
                started = time.monotonic()
                db.send("DASH 9")
                seen = []
                while time.monotonic() - started < info["run_seconds"] + 15:
                    state = db.send("TRACKER")
                    match = re.search(r"\| step (\d+)", state)
                    step = int(match[1]) if match else 0
                    if step and step not in seen:
                        seen.append(step)
                        time.sleep(1.2)
                        status = db.send("STATUS")
                        if step == 1:
                            sound = deck_status(status, 4)
                            assert sound["active"] == "1", sound
                            assert sound["status"].lower().startswith("play"), sound
                        if step < 6:
                            picture = frame(db, work / f"step-{step}.bmp")
                            if step == 3:
                                thumbnails.append((info["title"], picture.copy()))
                        print(f"{slug}: {state}", flush=True)
                    if "stopped" in state:
                        break
                    time.sleep(.2)
                elapsed = time.monotonic() - started
                assert seen == list(range(1, 7)), seen
                assert "stopped" in state, state
                assert abs(elapsed - info["run_seconds"]) < 5, elapsed
                frame(db, work / "ending.bmp", black=True)
                db.send("RECORD STOP")
                time.sleep(3)
                recordings = list(recording_dir.glob("*.mp4"))
                assert recordings, list(recording_dir.iterdir())
                recording = max(recordings, key=lambda p: p.stat().st_mtime)
                wav = work / "recorded-audio.wav"
                subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(recording),
                                "-vn", "-ac", "1", "-ar", "24000", str(wav)], check=True)
                with wave.open(str(wav), "rb") as audio:
                    rate = audio.getframerate()
                    samples = array("h", audio.readframes(audio.getnframes()))
                # Test the music across every scene, including the inset changes.
                levels = []
                for second in (3, 11, 19, 27, 33):
                    part = samples[second * rate:(second + 1) * rate]
                    rms = math.sqrt(sum(x*x for x in part) / max(1, len(part)))
                    assert rms > 20, (second, rms)
                    levels.append(round(rms, 2))
                item.update(observed_steps=seen, elapsed_seconds=round(elapsed, 2),
                            recording=str(recording), audio_rms=levels)
                db.send("TRACKER STEP 3")
            db.commands("DECK 1", "SELECT 1")
            time.sleep(.4)
            db.snapshot(work / "desk-dark.bmp")
            saved = db.saved()
        # Reopen what the app actually saved, in the saturated theme at 150%.
        show.write_text(saved.replace("theme\tdark\n", "theme\tvirtual-boy\n"), encoding="utf8")
        with Runtime(args.exe, work / "virtual-boy", show) as db:
            assert "nothing broken" in db.send("CHECK")
            db.commands("OUTPUT ON", "TRACKER STEP 1" if info["steps"] else "TAKE")
            time.sleep(1)
            db.snapshot(work / "desk-virtual-boy.bmp")
            assert cues[0][33] in db.saved(), "Cue notes did not survive reopening"
            item["reopened"] = True
        report.append(item)
        print(f"Passed {slug}", flush=True)
    sheet = Image.new("RGB", (1281, 520), "#101719")
    draw = ImageDraw.Draw(sheet)
    for index, (title, image) in enumerate(thumbnails):
        x, y = (index % 3) * 427, (index // 3) * 260
        image.thumbnail((411, 216))
        sheet.paste(image, (x + 8, y + 8))
        draw.text((x + 16, y + 228), title, fill="#d7e8e0", font_size=18)
    sheet.save(report_dir / "overview.png")
    result = {"shows": report, "audio_sha256": hashlib.sha256(
        (source / "media/soft-circuit.wav").read_bytes()).hexdigest()}
    (report_dir / "report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf8")
    print("All examples passed; captures and report:", report_dir, flush=True)


if __name__ == "__main__":
    main()
