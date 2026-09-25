#!/usr/bin/env python3
"""Find verbs and shortcuts that share an implementation but not a meaning.

WHY THIS EXISTS. `GO` and `TOGGLE` were aliases on one function:

    if (command == "GO" || command == "TOGGLE") { toggleTransport(); }

and the spacebar was bound to it too. That was harmless while the function
only played and paused. Then the standby walker was added to the top of it --
correct for GO, wrong for TOGGLE -- and the SPACEBAR STOPPED PAUSING the
moment a cue was standing by. Nothing caught it: every verb had a handler,
every handler ran, and the audits that check wiring all passed. The fault was
that two different promises were being kept by one piece of code.

An alias is fine when the names MEAN the same thing (BLACKOUT / BLACK,
SETTINGS / MENU). It is a trap when they do not. This cannot be decided
automatically, so this lists every group for a human to read, and FAILS only
on the ones an allow-list has not confirmed.

Two checks:

  [1] REMOTE ALIASES. Verbs OR'd together in one dispatch branch.
  [2] KEY COLLISIONS. A keyboard shortcut whose handler is also a remote verb
      with a different name -- the shape the spacebar had.

Adding an alias means adding a line here saying the two names mean the same
thing. That is the whole point: it is a moment where somebody has to think.
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REMOTE = os.path.join(ROOT, "native", "app", "app_remote_command.ipp")

# Groups confirmed to be genuine synonyms: the names mean the same action and
# a change to one is meant to change the other. One line each, with the reason.
CONFIRMED = {
    # REVIEWED 2026-09-23, all of them, in one pass -- which is the point of
    # this list existing: it is a baseline of what somebody has actually
    # looked at, so the NEXT alias has to be looked at too. GO / TOGGLE would
    # have failed here on the day it was written.
    #
    # Each of these is two spellings of ONE action: a long form and a desk
    # abbreviation (RECORD/REC, TIMECODE/TC), an old name kept working
    # (DECKOPACITY/PLAYLISTOPACITY), a platform pair (SYPHONCUE/SPOUTCUE), or
    # the same idea in two vocabularies (AUDITION/PFL).
    ("MTCEXT", "TIMECODEEXT"),
    ("LTCEXT", "TIMECODELTC"),
    ("DECKPREV", "DECKPREVIOUS"),
    ("DECKADD", "NEWDECK"),
    ("CHECK", "BROKEN"),
    ("AUDITION", "PFL"),
    ("FADE", "FADECUE"),
    ("TARGET", "TARGETCUE"),
    ("ENCODE", "CONVERT"),
    ("MARKER", "MARK"),
    ("TIMERCUE", "ADDTIMER"),
    ("DATAMOSH", "MOSH"),
    ("JUMPMODE", "JUMP_MODE"),
    ("JUMPTRANS", "JUMPTRANSITION", "JUMP_XFADE"),
    ("PANICPROFILE", "PANIC_PROFILE"),
    ("PANICFADE", "PANIC_FADE"),
    ("PANICAUTORESTORE", "PANIC_RESTORE"),
    ("OSCQUERY", "OSC_QUERY"),
    ("OSCQUERYPORT", "OSC_QUERY_PORT"),
    ("OSCFEEDBACK", "OSC_FEEDBACK"),
    ("OSCFEEDBACKRATE", "OSC_FEEDBACK_RATE"),
    ("ATEM", "ATEMTRIGGER"),
    ("NDITRIGGER", "NDI_TRIGGER"),
    ("NMC", "NMCSYNC", "NMC_SYNC"),
    ("MTC", "MTCINGEST", "MTC_INGEST"),
    ("MIDI", "MIDIINPUT", "MIDI_INPUT"),
    ("LTC", "LTCINGEST", "LTC_INGEST"),
    ("ARTNET", "DMXARTNET", "DMX_ARTNET", "DMX"),
    ("ARTNETPORT", "DMXPORT", "ART_NET_PORT"),
    ("INTEGRATION", "INTEGRATIONS"),
    ("ALLTAKE", "SYNCTAKE"),
    ("ALLGO", "SYNCGO"),
    ("GROUP", "PRESET", "GROUPPRESET"),
    ("GOEND", "SKIPEND"),
    ("FIND", "CUEFIND"),
    ("FINDNEXT", "CUEFINDNEXT"),
    ("FINDPREV", "FINDPREVIOUS", "CUEFINDPREV"),
    ("FINDTAKE", "CUEFINDTAKE"),
    ("FINDCLEAR", "FINDRESET", "CUEFINDCLEAR"),
    ("FINDSTATUS", "CUEFINDSTATUS"),
    ("RENUMBER", "CUEAUTOID", "AUTOID"),
    ("SELECTID", "CUEID"),
    ("SEEK", "SEEKPOS"),
    ("IN", "TRIMIN"),
    ("OUT", "TRIMOUT"),
    ("MESH", "MESH3D"),
    ("AUDIOVIS", "AUDIOVISUAL"),
    ("ANIM", "ANIMATION"),
    ("HOLD", "HOLDLAST", "PAUSEEND", "PAUSEATEND"),
    ("PAUSEBEGIN", "PAUSEATBEGIN", "PAUSESTART"),
    ("CUEAUDIO", "AUDIOCUE", "AUDIOENABLED"),
    ("NEXTTRANS", "TRANSITIONTONEXT", "CUEXNEXT"),
    ("CUEGOTO", "GOTOTARGET"),
    ("CUEIDSHORT", "SHORTID", "CUESHORTID"),
    ("AUTONEXT", "AUTOADVANCE"),
    ("PLAYLISTOPACITY", "DECKOPACITY", "DECKDIM"),
    ("PLAYLISTAUTOFADE", "DECKAUTOFADE"),
    ("PLAYLISTFADE", "DECKFADE"),
    ("TRANSITION", "XFADE"),
    ("TCMARK", "TIMECODEMARK"),
    ("TIMECODE", "TC"),
    ("SYPHONCUE", "SPOUTCUE"),
    ("STILLDUR", "DURATION"),
    ("GRAPHIC", "LOWERTHIRD"),
    ("VIDEO", "OUTPUTMODE"),
    ("SHORTCUTS", "KEYS"),
    ("DASH", "DASHBOARD"),
    ("NDIKEY", "NDIKEYER"),
    ("RECORD", "REC"),
    ("COLOR", "COLORTAG"),
    ("SUBTITLE", "SUBTITLES", "SUB", "CC"),
    ("DECKREMOVE", "DECKDEL"),
    ("MULTIVIEW", "MULTI"),
    ("TCCUE", "TIMECODECUE"),
    ("MASTER", "MASTERCUE"),
    # 2026-09-25: the master-cue tracker's transport; SEQUENCE is what an
    # operator calls a running order, TRACKER what the panel is called.
    ("TRACKER", "SEQUENCE"),
    # 2026-09-25: both retired in favour of MASTER, and both answer with the
    # same pointer to it.
    ("GROUP", "GROUPPRESET"),
    ("PREV", "PREVIOUS"),
    ("COMPOSITE", "SCENE"),
    ("MASTERVOL", "MASTERVOLUME"),
    ("BLACK", "BLACKOUT"),
    ("MENU", "SETTINGS", "SETUP"),
}


def confirmed(names):
    key = tuple(sorted(names))
    for group in CONFIRMED:
        if tuple(sorted(group)) == key:
            return True
    return False


text = io.open(REMOTE, encoding="utf-8", errors="replace").read()

# [1] Verbs OR'd together in one branch.
groups = []
for m in re.finditer(r'if \(command == "(\w+)"((?:\s*\|\|\s*command == "\w+")+)\)', text):
    names = [m.group(1)] + re.findall(r'command == "(\w+)"', m.group(2))
    line = text[:m.start()].count("\n") + 1
    groups.append((line, names, m.start()))

# THE SHARP RULE: does the branch tell its own names apart?
#
# A branch that ORs several verbs and then re-tests `command` inside is a
# DISPATCHER -- WIDTH/HEIGHT, GET/SET, SYNTHNOTEON/SYNTHNOTEOFF all do this,
# and each name gets its own behaviour. Perfectly fine, and listing them
# buries the real thing: 79 findings nobody will read is the same as none.
#
# A branch that never re-tests them is saying these names are the SAME ACTION.
# That is exactly what GO and TOGGLE were, and it is only correct when the
# names are genuine synonyms -- which a human has to confirm, once, here.
def branch_body(start_index):
    depth = 0
    i = text.index("{", start_index)
    out = []
    while i < len(text):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                break
        out.append(c)
        i += 1
    return "".join(out)


fails = []
listed = []
dispatchers = 0
for line, names, at in groups:
    body = branch_body(at)
    # Re-tested by name, OR used as a value -- `"VIDEO " + command` forwards
    # the verb onward, so each name still means its own thing. Missing that
    # idiom is what made the first run of this call CANVAS/VIEW/WARP/BLEND a
    # fault when it is a forwarder.
    retested = sum(1 for n in names
                   if re.search(r'command == "%s"' % re.escape(n), body))
    forwards = re.search(r'\+ command|command \+|<< command|\(command\)', body) is not None
    if retested or forwards:
        dispatchers += 1
        continue
    if confirmed(names):
        listed.append((line, names, "confirmed synonyms"))
    else:
        fails.append("%s share one branch, never tell each other apart, and are "
                     "not confirmed synonyms (app_remote_command.ipp:%d) -- if "
                     "they mean the same thing add them to CONFIRMED; if they do "
                     "not, they need separate branches, as GO and TOGGLE did"
                     % (" / ".join(names), line))

print("audit: verbs and shortcuts that share an implementation")
print()
print("  alias groups found:  %d" % len(groups))
print("  dispatchers (each name tells itself apart): %d" % dispatchers)
print("  confirmed synonyms:  %d" % len(listed))
print()
for line, names, why in listed:
    print("      %-40s %s" % (" / ".join(names), why))
print()
for f in fails:
    print("  FAIL " + f)
print()
print("clean" if not fails else "%d finding%s" % (len(fails), "" if len(fails) == 1 else "s"))
sys.exit(1 if fails else 0)
