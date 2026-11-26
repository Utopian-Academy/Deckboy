"""Saved effect chains, end to end on a private desktop.

Builds a chain on a cue, saves it (FX SAVE), clears the cue, loads it back
(FX LOAD), adds it to the end (FX LOAD ... ADD), lists (FX PRESETS), checks
the file on disk, and opens the list in the inspector for a screenshot.

Usage: py -3 tools/check_effect_presets.py [--exe build/windows/Release/Deckboy.exe] [--out DIR]
Exit 0 = every check passed.
"""
import argparse
import re
import shutil
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STARTERS = sorted((ROOT / "data/starter-chains").glob("*.chain"))
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402


def quick_ids():
    """QuickAction ids by name, read from the enum (QUICK takes the number)."""
    text = (ROOT / "native/core/types.hpp").read_text(encoding="utf-8")
    body = re.search(r"enum class QuickAction[^{]*\{(.*?)\n\};", text, re.S).group(1)
    body = re.sub(r"//[^\n]*", "", body)
    names = [n.strip() for n in body.split(",") if n.strip()]
    return {n: i for i, n in enumerate(names)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    work = Path(tempfile.mkdtemp(prefix="deckboy-presets-"))
    state = work / "rt" / "state"   # example_show_runtime points DECKBOY_STATE_DIR here
    show = work / "presets.deckboy"
    shutil.copy2(ROOT / "data/demos/starter-pack/02-running-a-show.deckboy", show)
    shutil.copytree(ROOT / "data/demos/starter-pack/media", work / "media")

    failures = []
    quick = quick_ids()

    def check(name, ok, detail=""):
        print(("PASS " if ok else "FAIL ") + name + (("  " + detail) if detail else ""), flush=True)
        if not ok:
            failures.append(name)

    with Runtime(Path(args.exe), work / "rt", show) as rt:
        def say(cmd):
            reply = rt.send(cmd)
            time.sleep(0.3)
            return reply

        say("PATTERN ADD swirl")
        say("FX ADD breath 0.35")
        say("FX ADD posterise")
        say("FX ADD scanlines")
        before = say("FX LIST")
        check("chain built", "breath" in before and "scanlines" in before, before)

        reply = say("FX SAVE VHS wobble")
        check("FX SAVE answers saved", "saved VHS wobble" in reply, reply)
        files = list((state / "effect-chains").glob("*.chain"))
        check("one file written", len(files) == 1, str(files))
        if files:
            text = files[0].read_text(encoding="utf-8")
            check("file names the chain", "name\tVHS wobble" in text, text[:120])
            check("file holds the effects", "breath:" in text and "scanlines:" in text)

        reply = say("FX SAVE VHS wobble")
        check("FX SAVE again replaces", "replaced VHS wobble" in reply, reply)
        check("still one file", len(list((state / "effect-chains").glob("*.chain"))) == 1)

        say("FX CLEAR")
        check("cleared", "no effects" in say("FX LIST"))

        reply = say("FX LOAD vhs WOBBLE")   # names match without case
        check("FX LOAD answers loaded", "loaded VHS wobble" in reply, reply)
        after = say("FX LIST")
        check("loaded chain matches", after == before, f"{before!r} vs {after!r}")

        say("FX LOAD VHS wobble ADD")
        added = say("FX LIST")
        check("ADD appends", added.count("breath") == 2, added)

        try:
            reply = rt.send("FX LOAD nothing by this name")
        except RuntimeError as e:
            reply = str(e)
        check("unknown name is an error", "ERR FX" in reply and "no saved chain" in reply, reply)

        reply = say("FX PRESETS")
        check("FX PRESETS lists it", "VHS wobble" in reply, reply)

        # The buttons' own path: QUICK fires a QuickAction by id, as a click does.
        say("FX CLEAR")
        say("FX ADD grain")
        say("FX ADD vignette")
        say("FX SAVE Soft dream")          # sorts first: index 0
        say("FX CLEAR")
        say(f"QUICK {quick['EffectChainPresetLoad']} 1")   # VHS wobble
        check("load button replaces", say("FX LIST") == before)
        say(f"QUICK {quick['EffectChainPresetAdd']} 0")    # + add Soft dream
        listed = say("FX LIST")
        check("+ add button appends", listed.endswith("4:grain, 5:vignette"), listed)
        say(f"QUICK {quick['EffectChainPresetsToggle']}")  # open the list

        # The starters ship in data/starter-chains and come after yours.
        listed = say("FX PRESETS")
        check("ten starters listed", listed.count("(starter)") == len(STARTERS), listed)
        check("yours come first", listed.index("Soft dream") < listed.index("(starter)"), listed)
        say("FX LOAD old tv")
        check("a starter loads", say("FX LIST").endswith("1:tape, 2:scanlines, 3:vignette"))

        if args.out:
            out = Path(args.out)
            out.mkdir(parents=True, exist_ok=True)
            say("CLICK 2100 440")
            say("SECTION EFFECTS")
            say("HOVER 2100 900 WHEEL -60")
            say("HOVER 1000 600")
            time.sleep(0.6)
            rt.snapshot(out / "presets-list.bmp")

    # A portable install: no separate state dir, so stateDir() is data/. Your
    # chains and the starters must still be told apart.
    port = work / "portable"
    with Runtime(Path(args.exe), port, show, env={"DECKBOY_STATE_DIR": None}) as rt:
        rt.send("SELECT 2")
        rt.send("FX ADD grain")
        rt.send("FX SAVE Mine")
        listed = rt.send("FX PRESETS")
        check("portable: starters still starters", listed.count("(starter)") == len(STARTERS), listed)
        check("portable: yours is yours", "Mine;" in listed or listed.endswith("Mine"), listed)
        mine = list((port / "data" / "effect-chains").glob("*.chain"))
        check("portable: saved beside, not among, the starters", len(mine) == 1, str(mine))

    shutil.rmtree(work, ignore_errors=True)
    print("FAILED: " + ", ".join(failures) if failures else "all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
