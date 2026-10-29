#!/usr/bin/env python3
"""The MIDI and OSC monitor: every message, and what Deckboy did with it.

"My controller does nothing" was a guess until now: a note no cue claims, a
CC with no mapping and an address nothing answers all looked like a broken
cable. MENU > Network > MONITOR lists each message with DID, NOTHING or
REFUSED and the reason, and INPUTMONITOR over the wire returns the same rows.

This sends real OSC packets over UDP, remote commands over TCP and MIDI
through MIDIIN (the handlers the MIDI thread calls; CI runners have no MIDI
port), then reads INPUTMONITOR back. Untested here: a refused controller, which
needs a second machine.

    py -3 tools/check_input_monitor.py --exe build/windows/Release/Deckboy.exe
"""
import argparse
import json
import shutil
import socket
import struct
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from example_show_runtime import Runtime  # noqa: E402

fails = []


def note(name, ok, info=""):
    print("  %-62s %s" % (name, "ok" if ok else "FAIL  " + str(info)), flush=True)
    if not ok:
        fails.append(name)


def osc_string(text):
    data = text.encode("utf8") + b"\0"
    return data + b"\0" * (-len(data) % 4)


def osc_packet(address, *ints):
    return osc_string(address) + osc_string("," + "i" * len(ints)) + b"".join(struct.pack(">i", v) for v in ints)


def rows(db, count=50):
    reply = db.send("INPUTMONITOR %d" % count)
    return json.loads(reply.split(": ", 1)[1])


def find(entries, kind, message):
    for entry in entries:
        if entry["kind"] == kind and entry["message"] == message:
            return entry
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = ap.parse_args()
    exe = Path(args.exe).resolve()
    print("check: the MIDI and OSC monitor\n")

    work = Path(tempfile.mkdtemp(prefix="deckboy-input-monitor-"))
    shutil.copytree(ROOT / "data/demos/starter-pack", work / "show")
    show = work / "show" / "01-picture-on-screen.deckboy"
    with Runtime(exe, work / "run", show) as db:
        db.send("SELECT 2")
        db.send("TRIGGER OSC /lights/go")
        db.send("SELECT 3")
        db.send("TRIGGER MIDI 36")
        db.send("INPUTMONITOR CLEAR")

        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.sendto(osc_packet("/lights/go"), ("127.0.0.1", db.port))
        udp.sendto(osc_packet("/lights/scene", 3), ("127.0.0.1", db.port))
        time.sleep(0.6)
        db.send("STATUS")                       # a poller: left out
        try:
            db.send("FROBNICATE")
        except RuntimeError:
            pass                                # ERR unknown command, as it should be
        db.send("MIDIIN NOTE 36 100")
        db.send("MIDIIN NOTE 60 127")           # no cue claims it, and there is no cue 61
        db.send("MIDIIN NOTE 1 90")             # no cue claims it: the old rule, cue 2
        db.send("MIDIIN NOTE 36 0")
        db.send("MIDIIN CC 1 10")
        db.send("MIDIIN CC 7 96")
        time.sleep(0.6)

        entries = rows(db)
        for entry in entries:
            print("    %s  %-6s %-22s %-24s %-8s %s" % (entry["time"], entry["kind"], entry["from"],
                                                      entry["message"], entry["outcome"], entry["what"]))
        print()
        e = find(entries, "OSC", "/lights/go")
        note("OSC an address a cue claims: DID, and says so",
             e is not None and e["outcome"] == "DID" and "1 cue" in e["what"], e)
        e = find(entries, "OSC", "/lights/scene 3")
        note("OSC an address nothing answers: NOTHING, and why",
             e is not None and e["outcome"] == "NOTHING" and "no cue has the address" in e["what"], e)
        e = find(entries, "REMOTE", "FROBNICATE")
        note("REMOTE an unknown verb: NOTHING",
             e is not None and e["outcome"] == "NOTHING" and "no command FROBNICATE" in e["what"], e)
        note("pollers (STATUS) and the monitor's own verbs are left out",
             not any(x["message"].split()[0] in ("STATUS", "INPUTMONITOR", "MIDIIN") for x in entries
                     if x["kind"] == "REMOTE"), [x["message"] for x in entries if x["kind"] == "REMOTE"])
        e = find(entries, "MIDI", "NOTE ON 36 (C2) vel 100")
        note("MIDI a note a cue claims: DID, the cue it took",
             e is not None and e["outcome"] == "DID" and "claims note 36" in e["what"], e)
        e = find(entries, "MIDI", "NOTE ON 60 (C4) vel 127")
        note("MIDI a note nothing claims and no cue to go to: NOTHING, and why",
             e is not None and e["outcome"] == "NOTHING" and "no cue 61" in e["what"], e)
        e = find(entries, "MIDI", "NOTE ON 1 (C#-1) vel 90")
        note("MIDI a note nothing claims: the old rule, and says so",
             e is not None and e["outcome"] == "DID" and "so cue 2" in e["what"], e)
        e = find(entries, "MIDI", "NOTE OFF 36 (C2)")
        note("MIDI a note-off with synth mode off: NOTHING, and why",
             e is not None and e["outcome"] == "NOTHING" and "synth" in e["what"], e)
        e = find(entries, "MIDI", "CC 1 = 10")
        note("MIDI a CC with no mapping: NOTHING, and why",
             e is not None and e["outcome"] == "NOTHING" and "CC 7" in e["what"], e)
        e = find(entries, "MIDI", "CC 7 = 96")
        note("MIDI CC 7: DID, the volume it became",
             e is not None and e["outcome"] == "DID" and e["command"].startswith("MASTERVOL"), e)
        order = [x["message"] for x in entries]
        note("rows in the order they arrived",
             order.index("/lights/go") < order.index("FROBNICATE") < order.index("CC 7 = 96")
             if all(m in order for m in ("/lights/go", "FROBNICATE", "CC 7 = 96")) else False, order)

        db.send("INPUTMONITOR PAUSE")
        udp.sendto(osc_packet("/lights/scene", 4), ("127.0.0.1", db.port))
        time.sleep(0.6)
        paused = rows(db)
        db.send("INPUTMONITOR RESUME")
        note("PAUSE stops new rows", find(paused, "OSC", "/lights/scene 4") is None, len(paused))
        db.send("INPUTMONITOR CLEAR")
        note("CLEAR empties it", rows(db) == [], rows(db))
        udp.close()

    shutil.rmtree(work, ignore_errors=True)
    print()
    if fails:
        print("FAIL: %d case(s): %s" % (len(fails), ", ".join(fails)))
        return 1
    print("input monitor: all cases pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
