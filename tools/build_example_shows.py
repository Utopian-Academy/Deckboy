"""Author portable examples through Deckboy's real cue builders.

python tools/build_example_shows.py --exe path/to/Deckboy
Only writes data/demos/starter-pack and isolated state under build/examples.
"""
import argparse
from array import array
import json
import math
from pathlib import Path
import time
import wave

from audit_record_layout import writer_order
from example_show_runtime import ROOT, Runtime

PACK = ROOT / "data/demos/starter-pack"
CUE_FIELDS = {member: column for column, member in writer_order(
    (ROOT / "native/core/project_file.ipp").read_text(encoding="utf8")) if member}
BASE = {
    "focused_deck": 0, "focused_output": 0, "advanced_mode": 1,
    "ui_sounds": 0, "hover_tips": 1, "theme": "dark", "ui_scale": 1.5,
    "master_volume": .5, "master_dimmer": 1, "output_follow_display": 0,
    "output_render_width": 1280, "output_render_height": 720,
    "output_refresh_hz": 0, "output_canvas_enabled": 0,
    "update_check": 0, "allow_remote_network": 0, "web_remote_enabled": 0,
    "web_monitor_enabled": 0, "hyperdeck_enabled": 0, "tracker_loop": 0,
    "tracker_classic": 0, "clicker_tracker": 0, "dashboard_mode": 0,
    "geometry_aspect_link": 0, "recording_width": 1280,
    "recording_height": 720, "recording_fps": 30, "recording_codec": "h264",
}


# What a SHIPPED show says about its output: follow the screen it is put on,
# its own resolution and its own refresh. The shows are AUTHORED on a fixed
# 1280x720 raster (BASE) so every cue's size is the same on every machine that
# builds them, but they shipped with that pin -- and with a 30 Hz refresh,
# which made Deckboy switch the screen to its nearest mode: 50 Hz on a 60 Hz
# desk monitor, the "unsteady playback" an example show gave on a 4K screen.
# An example has no business changing anybody's display mode.
SHIP = {"output_follow_display": 1, "output_refresh_hz": 0}


def seed(title, shipped=False):
    values = {"title": title, **BASE}
    if shipped:
        values.pop("output_render_width")
        values.pop("output_render_height")
        values.update(SHIP)
    return "\n".join(f"{key}\t{value}" for key, value in values.items()) + "\n"


def make_audio():
    """An original 39-second, 120 BPM music bed. No samples."""
    path = PACK / "media/soft-circuit.wav"
    path.parent.mkdir(parents=True, exist_ok=True)
    rate, seconds = 24000, 39
    samples = array("h")
    melody = [69, 72, 76, 79, 76, 72, 71, 67,
              69, 72, 76, 81, 79, 76, 72, 71]
    bass = [45, 41, 48, 43]
    for i in range(rate * seconds):
        t = i / rate
        beat = t * 2
        note_age = t % .5
        note = melody[int(beat) % len(melody)]
        freq = 440 * 2 ** ((note - 69) / 12)
        env = min(1, note_age / .012) * math.exp(-note_age * 8)
        bell = (math.sin(2 * math.pi * freq * t) +
                .18 * math.sin(2 * math.pi * freq * 2 * t)) * env * .15
        bass_freq = 440 * 2 ** ((bass[int(t / 4) % 4] - 69) / 12)
        bass_age = t % 2
        low = math.sin(2 * math.pi * bass_freq * bass_age) * min(1, bass_age / .025) * math.exp(-bass_age * 1.8) * .13
        drum_age = t % .5
        kick = math.sin(2 * math.pi * (48 * drum_age + 5 * (1 - math.exp(-drum_age * 35)))) * math.exp(-drum_age * 24) * .09
        # A tiny fade at the file ends avoids clicks.
        edge = min(1, t / .02, (seconds - t) / .02)
        pan = .20 * math.sin(2 * math.pi * t / 8)
        samples.extend(int(32767 * edge * (bell * (1 + side * pan) + low + kick))
                       for side in (-1, 1))
    import sys
    if sys.byteorder != "little":
        samples.byteswap()
    with wave.open(str(path), "wb") as out:
        out.setparams((2, 2, rate, 0, "NONE", "not compressed"))
        out.writeframes(samples.tobytes())


class Author:
    def __init__(self, db):
        self.db = db
        self.deck = 1
        self.counts = {1: 0}
        self.names = {}
        # Cue fields with no remote verb (a cue's own transition time), set
        # straight on the saved record by finish(): {(deck, index): {member: value}}.
        self.fields = {}

    def commands(self, *commands):
        self.db.commands(*commands)

    def playlist(self, name):
        if self.names:
            self.db.send("DECKADD")
            self.deck += 1
            self.db.send(f"DECK {self.deck}")
            self.counts[self.deck] = 0
        self.names[self.deck - 1] = name
        self.db.send("TRANSITION 0.6")

    def cue(self, create, title, notes, *commands):
        self.db.send(create)
        self.counts[self.deck] += 1
        n = self.counts[self.deck]
        # Adding a cue changes the primary selection but can retain the old
        # batch selection. Explicitly select away and back before editing.
        if n > 1:
            self.db.send("SELECT 1")
        self.db.commands(f"SELECT {n}", "RENAME " + title,
                         f"CUENOTES {n} {notes}", *commands)
        return n

    def clip(self, title, notes, filename, *commands, fields=None):
        """A movie from media/, imported the way a drop on the playlist imports it."""
        drop = self.db.work / "clip-import.txt"
        drop.write_text(str(PACK / "media" / filename) + "\n", encoding="utf8")
        before = self.counts[self.deck]
        self.db.send("DROPTEST " + str(drop))
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            reply = self.db.send("DROPTEST")
            if "pending=0" in reply and "probes=0" in reply:
                time.sleep(.4)
                break
            time.sleep(.25)
        self.counts[self.deck] = n = before + 1
        if n > 1:
            self.db.send("SELECT 1")
        self.db.commands(f"SELECT {n}", "RENAME " + title, f"CUENOTES {n} {notes}", *commands)
        if fields:
            self.fields[(self.deck - 1, n)] = fields
        return n

    def swirl(self, title, notes, palette=0, *commands):
        return self.cue("PATTERN ADD swirl", title, notes, f"SWIRL PALETTE {palette}",
                        "SWIRL SPEED 0.45", "SWIRL TWIST 1.3", "SWIRL PIXELS 320", *commands)

    def synth(self, title, notes, palette="ice", shape="plasma", *commands):
        return self.cue("SYNTH ADD", title, notes, "SYNTH PALETTE " + palette,
                        "SYNTH SHAPE " + shape, *commands)

    def text(self, title, body, notes, *commands):
        return self.cue("TEXTCUE NEW", title, notes, "TEXTCUE BODY " + body,
                        "TEXTCUE SIZE 9", "TEXTCUE CARD 0", "TEXTCUE ANIM fade", *commands)

    def lower(self, title, look, notes):
        return self.cue("LOWERTHIRD", title, notes, "TEXTCUE TITLE Bob Cobb",
                        "TEXTCUE SUB Can Man", "TEXTCUE LOOK " + look,
                        "TEXTCUE BAR ink", "TEXTCUE ACCENT green")

    def clear(self):
        return self.cue("TEXTCUE NEW", "Clear layer",
                        "An empty transparent text cue clears this playlist from the composition. Take another cue to bring it back.",
                        "TEXTCUE CARD 0")

    def steps(self, steps):
        for i, (title, length, cells) in enumerate(steps, 1):
            self.db.send("TRACKER ADD")
            self.db.send(f"TRACKER SECTION {i} {title}")
            for deck, action in cells.items():
                self.db.send(f"TRACKER SET {i} {deck} {action}")
        return [(title, length) for title, length, _ in steps]


def first_show(a):
    a.playlist("Picture on screen")
    a.text("01 Your output is live", r"YOUR OUTPUT IS LIVE.\nPage Down takes the next cue.",
           "THIS SHOW: a picture on a screen. 1) Turn the output on: MENU > Video Outputs > OUTPUT OFF, so it reads OUTPUT ON (or DASH > 1 Output on). 2) Click this cue and press TAKE (Enter).",
           "TEXTCUE CARD 255")
    a.clip("02 A clip", "Page Down took this cue. It is a movie file from media/. Press I to bring in your own.",
           "tears-of-steel-street.mp4")
    a.swirl("03 Made by Deckboy", "Not a file: Deckboy draws this picture live. Page Down for the last cue.")
    a.text("04 That's a show", r"THAT'S A SHOW.\nPress B for BLACK. Then turn the output off in MENU.",
           "B (BLACK) clears the screen at any time, and B again brings it back. Next: open 02 Running a show.",
           "TEXTCUE CARD 255")
    return []


def running_order(a):
    a.playlist("Running order")
    a.text("01 Doors", r"RUNNING A SHOW\nPage Down takes each cue in turn.",
           "THIS SHOW: a running order. Turn the output on and TAKE this cue, then press Page Down for each next one.",
           "TEXTCUE CARD 255")
    a.clip("02 Crossfade in", "This cue has its own 2-second crossfade: inspector > PLAYBACK > transition.",
           "tears-of-steel-street.mp4", fields={"cueTransitionSeconds": "2"})
    a.clip("03 Goes to black at its end", "HOLD is off on this one: at its end the picture goes to black. The other clips have HOLD on (the default) and stop on their last picture. Let it finish, then Page Down.",
           "tears-of-steel-workshop.mp4", "HOLD OFF")
    a.clip("04 Loops until you move on", "LOOP is on (the L key): it plays round and round until you take the next cue.",
           "tears-of-steel-street.mp4", "LOOP ON")
    a.clip("05 Fades out by itself", "FADE OUT is 3 seconds: at its end it fades to black on its own. Nothing to press.",
           "tears-of-steel-workshop.mp4", "FADEOUT 3")
    a.text("06 Thank you", r"THANK YOU.",
           "Press B for BLACK at any time, B again to come back. Next: open 03 Layers and titles.",
           "TEXTCUE CARD 255")
    return []


def layers(a):
    a.playlist("Backdrop")
    a.swirl("01 Forest backdrop", "THIS SHOW: three playlists stacked into one picture. This is the bottom layer. The other playlists are composited over it.")
    a.swirl("02 Ice backdrop", "Switching this playlist leaves the inset and title running.", 2)
    a.cue("PATTERN ADD fireside", "03 Fireside", "Live generated set dressing. Try it behind the Lower Third.")
    a.playlist("Inset")
    a.swirl("01 Ice window", "A second playlist at 40% size, offset to the upper right. Change WIDTH, HEIGHT or OFFSET in GEOMETRY.", 2,
            "SCALE 0.4", "OFFSET 330 -145")
    a.swirl("02 Neon window", "Same placement, a kaleidoscope effect and a different palette. The background stays live.", 3,
            "SCALE 0.4", "OFFSET 330 -145", "FX ADD kaleidoscope 0.7")
    a.clear()
    a.playlist("Lower Third")
    a.lower("01 Bob Cobb - clean", "hairline", "A transparent Lower Third over both playlists. Change TITLE and SUB to use your own name.")
    a.lower("02 Bob Cobb - arcade", "arcade", "Same words, a different entrance and animated look. Try TEXTCUE OUT from the dashboard.")
    a.clear()
    return a.steps([
        ("All three layers", 0, {1: 1, 2: 1, 3: 1}),
        ("Change the backdrop", 0, {1: 2, 2: 1, 3: 1}),
        ("Change inset and Lower Third", 0, {1: 2, 2: 2, 3: 2}),
        ("Fireside interview", 0, {1: 3, 2: 3, 3: 1}),
        ("Clean backdrop", 0, {1: 1, 2: 3, 3: 3}),
    ])


def effects(a):
    a.playlist("Looks laboratory")
    a.swirl("01 Reference - no effects", "THIS SHOW: one picture, six looks. The reference source. Every following cue starts with this same swirl and palette.", 3)
    a.swirl("02 Kaleidoscope", "One effect. In EFFECTS, change Amount and its parameters to reshape the symmetry.", 3,
            "FX ADD kaleidoscope 0.8", "FX PARAM 1 A 0.35")
    a.swirl("03 RGB split + scanlines", "Two effects in order. Reorder them, copy the chain, or bypass one row to compare.", 3,
            "FX ADD channel_offset 0.28", "FX ADD scanlines 0.25")
    a.swirl("04 Moving ripple", "The ripple's Amount has a slow sine LFO. Open its ~ controls; switch it off to hold a fixed amount.", 3,
            "FX ADD ripple 0.42", "FX LFO 1 E ON", "FX LFO 1 E RATE 0.18", "FX LFO 1 E DEPTH 0.25")
    a.swirl("05 Text mode", "Live imagery rendered as characters. Open TEXT MODE, change the columns, then try another glyph set.", 3,
            "ASCII ON", "ASCII COLS 80")
    a.swirl("06 Gentle geometry motion", "No pixel effect: the source is scaled to 70%, with a slow horizontal geometry LFO. Inspect GEOMETRY.", 3,
            "SCALE 0.7", "GEOLFO x ON", "GEOLFO x RATE 0.1", "GEOLFO x DEPTH 0.06")
    return []


def coordinated(a):
    a.playlist("Picture")
    a.swirl("01 Doors", "THIS SHOW: everything so far, run from one DASH button with music. A quiet holding look. The tracker brings in the title and sound together.")
    a.synth("02 Opening", "An ice plasma source for the opening scene.", "ice", "plasma")
    a.synth("03 Main look", "A mirrored diamond source for the main scene.", "vapor", "diamond", "SYNTH MIRROR quad")
    a.swirl("04 Closing", "A slower closing backdrop.", 2, "SWIRL SPEED 0.25")
    a.cue("PATTERN ADD full-black", "05 Black", "The final picture cue closes the show.")
    a.playlist("Words")
    a.text("01 Stand by", r"SOFT CIRCUIT\nA small show in four playlists.",
           "The title is transparent and composited over the picture playlist.", "TEXTCUE SIZE 8")
    a.text("02 Begin", r"PICTURE. WORDS. SOUND.\nOne GO.",
           "This title and its backdrop fire on the same tracker step.", "TEXTCUE SIZE 9")
    a.lower("03 Speaker", "arcade", "A Lower Third replaces the full-screen title while other playlists continue.")
    a.text("04 Thank you", r"THAT WAS DECKBOY.\nNow change something.",
           "Closing title. The final tracker row fades the show out.", "TEXTCUE SIZE 9")
    a.clear()
    a.playlist("Inset")
    a.swirl("01 Small orbit", "An independent inset joins for the main section.", 1,
            "SCALE 0.3", "OFFSET 390 -195")
    a.clear()
    a.playlist("Sound")
    drop = a.db.work / "audio-import.txt"
    drop.write_text(str(PACK / "media/soft-circuit.wav") + "\n", encoding="utf8")
    a.db.send("DROPTEST " + str(drop))
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        reply = a.db.send("DROPTEST")
        if "pending=0" in reply and "probes=0" in reply:
            time.sleep(.4)
            break
        time.sleep(.25)
    a.counts[a.deck] = 1
    a.commands("SELECT 1", "RENAME Soft Circuit - original music", "AUDIOGAIN -6", "LOOP OFF", "FADEOUT 3",
               "CUENOTES 1 Original 39-second music bed bundled in media/. The tracker starts it once and lets it continue across scenes; its last three seconds fade out.")
    return a.steps([
        ("Doors", 8, {1: 1, 2: 1, 3: 2, 4: 1}),
        ("Opening", 8, {1: 2, 2: 2}),
        ("Main scene", 8, {1: 3, 2: 3, 3: 1}),
        ("Clear the inset", 4, {3: 2}),
        ("Closing", 8, {1: 4, 2: 4}),
        ("Fade to black", 3, {1: 5, 2: 5, 3: 2}),
    ])


SHOWS = [
    ("01-picture-on-screen", "Picture on screen", first_show),
    ("02-running-a-show", "Running a show", running_order),
    ("03-layers-and-titles", "Layers and titles", layers),
    ("04-looks-and-effects", "Looks and effects", effects),
    ("05-one-button-show", "One-button show", coordinated),
]


def finish(raw, slug, title, author, steps):
    records = [line.split("\t") for line in raw.splitlines()]
    kept = []
    ids = {}
    indices = {}
    tracker = 0
    for f in records:
        if f[0] == "deck":
            index = int(f[1])
            f[2], f[3], f[4] = author.names[index], "0", "-1"
            f[7] = ""  # portable default audio device
            kept.append(f)
        elif f[0] == "output_target" and f[1] == "0":
            f[2], f[3], f[4], f[5], f[6], f[8] = "Programme", "0", "-1", "0", "0", ""
            f[10], f[12], f[14] = "0", "0", "window"
            f[16] = "example-" + slug + "-out"
            # The output's layer list also determines its programme audio mix.
            f[77] = ",".join(str(d) for d in author.names if d > 0)
            kept.append(f)
        elif f[0] == "cue":
            deck = int(f[1])
            indices[deck] = indices.get(deck, 0) + 1
            index = indices[deck]
            ids[f[19]] = f"example-{slug}-{deck}-{index}"
            f[39] = f"{index:02d}"
            if f[3] == "Clear layer":
                f[CUE_FIELDS["textBody"]] = ""
            if deck == -1:
                name, length = steps[tracker]
                tracker += 1
                f[3], f[33] = name, "Tracker scene: " + name
                f[CUE_FIELDS["postWaitSeconds"]] = str(length)
                f[CUE_FIELDS["continueMode"]] = "auto-continue" if length else "none"
            if f[4] == "audio":
                f[2], f[17] = "media/soft-circuit.wav", "0"
            elif "/media/" in f[2].replace("\\", "/"):
                f[2] = "media/" + f[2].replace("\\", "/").rsplit("/", 1)[1]
            for member, value in author.fields.get((deck, index), {}).items():
                f[CUE_FIELDS[member]] = value
            kept.append(f)
    text = seed(title, shipped=True)
    for f in kept:
        line = "\t".join(f)
        for old, new in ids.items():
            line = line.replace(old, new)
        text += line + "\n"
    buttons = [("Output on", "OUTPUT ON"), ("Output off", "OUTPUT OFF")]
    if steps:
        buttons += [(name, f"TRACKER STEP {i}") for i, (name, _) in enumerate(steps, 1)]
        if any(length for _, length in steps):
            buttons += [("Run sequence", "TRACKER PLAY"), ("Hold sequence", "TRACKER STOP")]
        buttons += [("Open tracker", "DASH TRACKER")]
    else:
        buttons += [("Take selection", "TAKE"), ("Next cue", "GO"), ("Stop", "STOP")]
    if slug == "01-picture-on-screen":
        buttons[0] = ("1 Output on", "OUTPUT ON")
        buttons[2] = ("2 Take selection", "TAKE")
        buttons[3] = ("3 Next cue", "GO")
    if slug == "03-layers-and-titles":
        buttons += [("Lower Third out", "DECK 3 TEXTCUE OUT")]
    for i, (label, command) in enumerate(buttons):
        text += f"dashboard\t{i}\t{label}\t{command}\t\t{i % 5}\n"
    if "C:/" in text or "C:\\" in text:
        raise ValueError("Machine path leaked into example")
    return text, indices


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True)
    parser.add_argument("--shows", nargs="+", choices=[s[0] for s in SHOWS],
                        help="Rebuild only these shows, then refresh the whole manifest")
    args = parser.parse_args()
    PACK.mkdir(parents=True, exist_ok=True)
    make_audio()
    manifest = {"minimum_version": "0.99.417", "shows": []}
    for slug, title, build in SHOWS:
        if args.shows and slug not in args.shows:
            continue
        work = ROOT / "build/examples/author" / slug
        work.mkdir(parents=True, exist_ok=True)
        show = work / "author.deckboy"
        show.write_text(seed(title), encoding="utf8")
        with Runtime(args.exe, work, show) as db:
            author = Author(db)
            steps = build(author)
            raw = db.saved()
        text, counts = finish(raw, slug, title, author, steps)
        (PACK / (slug + ".deckboy")).write_text(text, encoding="utf8", newline="\n")
        print("Authored", slug, counts, flush=True)
    for slug, title, _ in SHOWS:
        records = [line.split("\t") for line in (PACK / (slug + ".deckboy")).read_text(encoding="utf8").splitlines()]
        cues = [f for f in records if f[0] == "cue"]
        counts = {}
        for f in cues:
            counts[int(f[1])] = counts.get(int(f[1]), 0) + 1
        steps = [f for f in cues if f[1] == "-1"]
        manifest["shows"].append({"file": slug + ".deckboy", "title": title,
                                 "playlists": [f[2] for f in records if f[0] == "deck"],
                                 "cue_counts": counts, "steps": [f[3] for f in steps],
                                 "run_seconds": sum(float(f[CUE_FIELDS["postWaitSeconds"]]) for f in steps)})
    (PACK / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")


if __name__ == "__main__":
    main()
