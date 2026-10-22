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
    a.playlist("Start here")
    a.text("01 Output first: N, then TAKE", r"YOUR OUTPUT IS LIVE.\nTAKE sends the selected cue here.",
           "FIRST press N to activate the Programme output (or DASH > 1 Activate output). F switches fullscreen/windowed. THEN return to the desk and press TAKE (Enter). GO (Space) advances to the next cue.",
           "TEXTCUE CARD 255")
    a.swirl("02 A backdrop made live", "No movie file: this swirl is generated live. Open the Swirl section and change palette, speed or twist.")
    a.synth("03 Video synth - ice rings", "A second live source. Open VIDEO SYNTH and try its shape, mirror and palette controls.", "ice", "rings")
    a.swirl("04 The same source, a new look", "Open EFFECTS: kaleidoscope changes the swirl. Lower its amount or bypass it to compare.", 3,
            "FX ADD kaleidoscope 0.7", "FX PARAM 1 A 0.3")
    a.text("05 Your turn", r"MAKE IT YOURS.\nDuplicate a cue. Change its look.",
           "Use SAVE AS before experimenting. Each cue has its own settings; the 0.6-second playlist transition blends between them.",
           "TEXTCUE CARD 255")
    return []


def layers(a):
    a.playlist("Backdrop")
    a.swirl("01 Forest backdrop", "This is the bottom layer. The other playlists are composited over it.")
    a.swirl("02 Ice backdrop", "Switching this playlist leaves the inset and title running.", 2)
    a.cue("PATTERN ADD fireside", "03 Fireside", "Live generated set dressing. Try it behind the lower third.")
    a.playlist("Inset")
    a.swirl("01 Ice window", "A second playlist at 40% size, offset to the upper right. Change WIDTH, HEIGHT or OFFSET in GEOMETRY.", 2,
            "SCALE 0.4", "OFFSET 330 -145")
    a.swirl("02 Neon window", "Same placement, a kaleidoscope effect and a different palette. The background stays live.", 3,
            "SCALE 0.4", "OFFSET 330 -145", "FX ADD kaleidoscope 0.7")
    a.clear()
    a.playlist("Name strap")
    a.lower("01 Bob Cobb - clean", "hairline", "A transparent lower third over both playlists. Change TITLE and SUB to use your own name.")
    a.lower("02 Bob Cobb - arcade", "arcade", "Same words, a different entrance and animated look. Try TEXTCUE OUT from the dashboard.")
    a.clear()
    return a.steps([
        ("All three layers", 0, {1: 1, 2: 1, 3: 1}),
        ("Change the backdrop", 0, {1: 2, 2: 1, 3: 1}),
        ("Change inset and strap", 0, {1: 2, 2: 2, 3: 2}),
        ("Fireside interview", 0, {1: 3, 2: 3, 3: 1}),
        ("Clean backdrop", 0, {1: 1, 2: 3, 3: 3}),
    ])


def effects(a):
    a.playlist("Looks laboratory")
    a.swirl("01 Reference - no effects", "The reference source. Every following cue starts with this same swirl and palette.", 3)
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
    a.swirl("01 Doors", "A quiet holding look. The tracker brings in the title and sound together.")
    a.synth("02 Opening", "An ice plasma source for the opening scene.", "ice", "plasma")
    a.synth("03 Main look", "A mirrored diamond source for the main scene.", "vapor", "diamond", "SYNTH MIRROR quad")
    a.swirl("04 Closing", "A slower closing backdrop.", 2, "SWIRL SPEED 0.25")
    a.cue("PATTERN ADD full-black", "05 Black", "The final picture cue closes the show.")
    a.playlist("Words")
    a.text("01 Stand by", r"SOFT CIRCUIT\nA small show in four playlists.",
           "The title is transparent and composited over the picture playlist.", "TEXTCUE SIZE 8")
    a.text("02 Begin", r"PICTURE. WORDS. SOUND.\nOne GO.",
           "This title and its backdrop fire on the same tracker step.", "TEXTCUE SIZE 9")
    a.lower("03 Speaker", "arcade", "A lower third replaces the full-screen title while other playlists continue.")
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
    ("01-start-here", "Start here", first_show),
    ("02-layers-and-titles", "Layers and titles", layers),
    ("03-effects-playground", "Effects playground", effects),
    ("04-one-button-show", "One-button show", coordinated),
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
    if slug == "01-start-here":
        buttons[0] = ("1 Activate output", "OUTPUT ON")
        buttons[2] = ("2 Take selection", "TAKE")
        buttons[3] = ("3 Next cue", "GO")
    if slug == "02-layers-and-titles":
        buttons += [("Name strap out", "DECK 3 TEXTCUE OUT")]
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
