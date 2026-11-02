#!/usr/bin/env python3
"""Read a DECKBOY_PRESENT_TRACE file and say whose clock each window kept.

WHY THIS EXISTS. On 2026-11-01, on Windows with the desk on a 60 Hz monitor and
the output on a 59.94 Hz screen, the loop ticked at 60 and one output present
in about 17 s came a refresh late. 17 s is the beat of 60 against 59.94, so two
clocks were meeting somewhere. Every renderer Deckboy uses keeps one frame in
flight, so a present waits until whatever consumes that window's frames has
taken the last one: the screen itself for a window flipped straight to it, the
desktop compositor (DWM) for a composed one. The reading of that run is that
the output was composed, and so presented on DWM's clock instead of its own
screen's. This turns one traced run into a yes or no.

Record a trace (any platform; the swap chain and DWM columns are Windows only):

    set DECKBOY_PRESENT_TRACE=C:\\temp\\trace.csv      (PowerShell: $env:...)
    Deckboy.exe show.deckboy          -- arm the output, play, wait 60 s, quit

Then:

    python3 tools/present_trace_report.py C:\\temp\\trace.csv

For each window it prints the rate its presents kept between hitches, the
screen's own rate and DWM's (from the app's "present clock" lines, which also
say whether the GPU that draws an output drives its screen and whether any
window sits over it), the late presents and which present in that pass of
the loop was the one that blocked. On Windows it adds the swap chain's own
frame statistics: the clock that showed its frames, and how many refreshes
repeated a frame or swallowed one. And DWM's counters: compositions it skipped.

`--selftest` checks the reading on two made-up traces: an output composed on a
60 Hz compositor beside its 59.94 Hz screen, and one flipped straight to it.
"""

import argparse
import math
import re
import statistics
import sys

LATE = 1.5           # a present more than 1.5 frames after the last is late
BLOCKED_MS = 4.0     # a present that held the loop this long waited for something
SAME_CLOCK = 0.0003  # rates within 0.03% are one clock, as far as a run can tell


def parse(lines):
    trace = {"counter_hz": None, "compositor_hz": None, "passes": [], "presents": {},
             "info": [], "windows": []}
    for raw in lines:
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            m = re.search(r"counter_hz=(\d+)", line)
            if m:
                trace["counter_hz"] = float(m.group(1))
            m = re.search(r"compositor_hz=([\d.]+)", line)
            if m:
                trace["compositor_hz"] = float(m.group(1))
            continue
        kind, _, rest = line.partition(",")
        if kind == "info":
            trace["info"].append(rest)
            continue
        fields = rest.split(",")
        if kind == "pass":
            entry = {"t": int(fields[0])}
            if len(fields) >= 6 and fields[1]:
                entry.update(vblank=int(fields[1]), refresh=int(fields[2]), frame=int(fields[3]),
                             dropped=int(fields[4]), missed=int(fields[5]))
            trace["passes"].append(entry)
        elif kind == "present":
            window = fields[0]
            entry = {"before": int(fields[1]), "after": int(fields[2]), "last": int(fields[3] or 0),
                     "pass": len(trace["passes"]) - 1}
            if len(fields) >= 8 and fields[4]:
                entry.update(stat_present=int(fields[4]), stat_refresh=int(fields[5]),
                             sync_refresh=int(fields[6]), sync_counter=int(fields[7]))
            if window not in trace["presents"]:
                trace["presents"][window] = []
                trace["windows"].append(window)
            trace["presents"][window].append(entry)
    if not trace["counter_hz"]:
        raise SystemExit("not a Deckboy present trace: no counter_hz header")
    return trace


def latest_verdicts(info_lines):
    """The last 'present clock' line for each output number."""
    out = {}
    for text in info_lines:
        m = re.search(r"present clock: output (\d+) screen=([\d.]+) Hz paced=([\d.]+) Hz compositor=([\d.]+) Hz",
                      text)
        if not m:
            continue
        fields = dict(re.findall(r'(\w+)=("[^"]*"|\S+)', text))
        out["out" + m.group(1)] = {
            "screen_hz": float(m.group(2)), "paced_hz": float(m.group(3)),
            "compositor_hz": float(m.group(4)), "text": text,
            "same_gpu": fields.get("same_gpu", "unknown"),
            "dxgi_mode": fields.get("dxgi_mode", "unknown"),
            "covers_screen": fields.get("covers_screen", "-1"),
            "windows_above": fields.get("windows_above", "-1"),
        }
    return out


def same_clock(a, b):
    return a > 0 and b > 0 and abs(a - b) < max(a, b) * SAME_CLOCK


def steady_rate(times, nominal_hz=None):
    """Rate of the intervals that were not late, as the span they cover."""
    intervals = [b - a for a, b in zip(times, times[1:]) if b > a]
    if len(intervals) < 30:
        return None, intervals
    period = 1.0 / nominal_hz if nominal_hz else statistics.median(intervals)
    regular = [d for d in intervals if d <= period * LATE]
    if len(regular) < 30:
        return None, intervals
    return len(regular) / sum(regular), intervals


def fit_rate(pairs):
    """Hz of a (count, seconds) sequence, by least squares."""
    if len(pairs) < 10:
        return None
    xs = [c for c, _ in pairs]
    ys = [t for _, t in pairs]
    mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx <= 0:
        return None
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    return 1.0 / slope if slope > 0 else None


def analyse(trace):
    hz = trace["counter_hz"]
    sec = lambda c: c / hz
    verdicts = latest_verdicts(trace["info"])
    passes = trace["passes"]
    report = {"windows": {}, "lines": []}
    say = report["lines"].append
    compositor = trace["compositor_hz"] or 0.0
    say("compositor (DWM) clock: %s" % ("%.3f Hz" % compositor if compositor else "unknown (not Windows)"))

    for window in trace["windows"]:
        presents = trace["presents"][window]
        verdict = verdicts.get(window)
        screen_hz = verdict["screen_hz"] if verdict else None
        times = [sec(p["after"]) for p in presents]
        rate, intervals = steady_rate(times, screen_hz)
        blocks = sorted((p["after"] - p["before"]) * 1000.0 / hz for p in presents)
        result = {"steady_hz": rate, "screen_hz": screen_hz, "late": [], "presents": len(presents)}
        report["windows"][window] = result
        say("")
        say("%s: %d presents over %.1f s" % (window, len(presents), times[-1] - times[0] if times else 0))
        if rate:
            say("  kept %.3f Hz between hitches (long run %.3f Hz)" %
                (rate, (len(times) - 1) / (times[-1] - times[0])))
        if blocks:
            say("  present held the loop: median %.2f ms, 99th %.2f ms, worst %.2f ms" %
                (statistics.median(blocks), blocks[min(len(blocks) - 1, int(len(blocks) * 0.99))], blocks[-1]))
        if verdict:
            say("  screen %.3f Hz, DXGI mode %s, same GPU as its screen: %s, covers its screen: %s, "
                "windows over it: %s" % (verdict["screen_hz"], verdict["dxgi_mode"], verdict["same_gpu"],
                                         verdict["covers_screen"], verdict["windows_above"]))
        if rate and screen_hz:
            if same_clock(rate, screen_hz):
                result["clock"] = "screen"
                say("  -> its presents keep ITS SCREEN's clock")
            elif compositor and same_clock(rate, compositor):
                result["clock"] = "compositor"
                beat = abs(rate - screen_hz)
                say("  -> its presents keep the COMPOSITOR's clock (%.3f Hz), not its screen's (%.3f Hz):"
                    " composed by DWM; the two slip a frame every %.1f s" % (compositor, screen_hz, 1.0 / beat))
            else:
                result["clock"] = "other"
                say("  -> its presents keep a clock that is neither its screen's nor the compositor's")

        # Late presents, and which present in that pass held it.
        period = 1.0 / screen_hz if screen_hz else (1.0 / rate if rate else None)
        if period:
            for i in range(1, len(presents)):
                gap = times[i] - times[i - 1]
                if gap <= period * LATE:
                    continue
                pass_index = presents[i]["pass"]
                holders = []
                for other in trace["windows"]:
                    for p in trace["presents"][other]:
                        if p["pass"] == pass_index:
                            held = (p["after"] - p["before"]) * 1000.0 / hz
                            if held > BLOCKED_MS:
                                holders.append("%s %.1f ms" % (other, held))
                result["late"].append({"t": times[i] - times[0], "gap_ms": gap * 1000.0, "held_by": holders})
            if result["late"]:
                say("  %d late presents (more than %.1f frames apart):" % (len(result["late"]), LATE))
                for late in result["late"][:12]:
                    say("    at %7.2f s, %5.1f ms after the last; held in that pass by: %s" %
                        (late["t"], late["gap_ms"], ", ".join(late["held_by"]) or "no present"))
                if len(result["late"]) > 12:
                    say("    ...")
                if len(result["late"]) >= 3:
                    spacing = [b["t"] - a["t"] for a, b in zip(result["late"], result["late"][1:])]
                    say("  late presents come every %.1f s (median)" % statistics.median(spacing))

        # The swap chain's own statistics (Windows, Direct3D 11).
        stats = [p for p in presents if "stat_present" in p]
        if stats:
            syncs = sorted({(p["sync_refresh"], sec(p["sync_counter"])) for p in stats if p["sync_counter"] > 0})
            sync_hz = fit_rate(syncs)
            shown = []
            for p in stats:
                pair = (p["stat_present"], p["stat_refresh"])
                if not shown or pair != shown[-1]:
                    shown.append(pair)
            repeats = swallowed = 0
            for (p1, r1), (p2, r2) in zip(shown, shown[1:]):
                dp, dr = p2 - p1, r2 - r1
                if dp <= 0 or dr < 0:
                    continue
                repeats += max(0, dr - dp)
                swallowed += max(0, dp - dr)
            result.update(sync_hz=sync_hz, repeats=repeats, swallowed=swallowed)
            if sync_hz:
                which = ("its screen's" if screen_hz and same_clock(sync_hz, screen_hz) else
                         "the compositor's" if compositor and same_clock(sync_hz, compositor) else "another")
                say("  swap chain: its frames were shown on a %.3f Hz clock (%s)" % (sync_hz, which))
            say("  swap chain: %d refreshes repeated a frame, %d frames were never shown on a refresh of"
                " their own" % (repeats, swallowed))

    # DWM skipping compositions.
    dwm = [p for p in passes if "refresh" in p]
    if len(dwm) > 2:
        skipped = []
        for a, b in zip(dwm, dwm[1:]):
            dr, df = b["refresh"] - a["refresh"], b["frame"] - a["frame"]
            if dr > df >= 0 and dr <= 8:
                skipped.append((sec(b["t"]) - sec(dwm[0]["t"]), dr - df))
        report["dwm_skipped"] = skipped
        say("")
        say("DWM: %d vblanks without a composition, frames dropped %d, missed %d over the run" %
            (sum(n for _, n in skipped), dwm[-1]["dropped"] - dwm[0]["dropped"], dwm[-1]["missed"] - dwm[0]["missed"]))
        for t, n in skipped[:12]:
            say("  at %7.2f s: %d" % (t, n))
    return report


# ── self test ───────────────────────────────────────────────────────────────

def synth(composed, seconds=40.0, counter_hz=10_000_000):
    """A made-up run: desk on 60 Hz, output on a 59.94 Hz screen."""
    screen = 60000.0 / 1001.0
    dwm_hz = 60.0
    lines = ["# deckboy present trace 1 counter_hz=%d compositor_hz=%.3f" % (counter_hz, dwm_hz)]
    c = lambda s: int(s * counter_hz)
    t = 0.0
    out_count = desk_count = 0
    dwm_refresh = dwm_frame = 0
    next_dwm = 1.0 / dwm_hz
    stall_every = 1.0 / (dwm_hz - screen)
    next_stall = stall_every
    while t < seconds:
        # DWM ticks up to now; one tick a beat composes nothing (it waits for the slower screen).
        while next_dwm <= t:
            dwm_refresh += 1
            if composed and next_dwm >= next_stall:
                next_stall += stall_every
            else:
                dwm_frame += 1
            next_dwm += 1.0 / dwm_hz
        lines.append("pass,%d,%d,%d,%d,0,0" % (c(t), c(next_dwm - 1.0 / dwm_hz), dwm_refresh, dwm_frame))
        t += 0.004
        desk_before = t
        if composed and abs((t % stall_every) - 0.0) < 1.0 / 60:
            t += 0.016   # the stall lands on the desk's present
        desk_count += 1
        lines.append("present,desk,%d,%d,%d" % (c(desk_before), c(t), desk_count))
        t += 0.003
        out_before = t
        clock = (1.0 / dwm_hz) if composed else (1.0 / screen)
        t = (math.floor(t / clock) + 1) * clock
        out_count += 1
        # The swap chain counts the refreshes of whatever consumes it: DWM's
        # for the composed run (so a stalled pass spans two), the screen's else.
        shown_refresh = int(round(t / clock))
        lines.append("present,out1,%d,%d,%d,%d,%d,%d,%d" %
                     (c(out_before), c(t), out_count, out_count - 1, shown_refresh, shown_refresh, c(t)))
        if out_count % 600 == 0:
            paced = dwm_hz if composed else screen
            lines.append("info,present clock: output 1 screen=%.3f Hz paced=%.2f Hz compositor=%.3f Hz dxgi_mode=%s"
                         " same_gpu=%s covers_screen=1 windows_above=0" %
                         (screen, paced, dwm_hz, "composed" if composed else "overlay",
                          "no" if composed else "yes"))
    return lines


def selftest():
    composed = analyse(parse(synth(True)))
    flipped = analyse(parse(synth(False)))
    problems = []
    if composed["windows"]["out1"].get("clock") != "compositor":
        problems.append("composed run not read as the compositor's clock: %r" % composed["windows"]["out1"])
    if flipped["windows"]["out1"].get("clock") != "screen":
        problems.append("flipped run not read as the screen's clock: %r" % flipped["windows"]["out1"])
    sync = composed["windows"]["out1"].get("sync_hz") or 0
    if not same_clock(sync, 60.0):
        problems.append("composed run's swap chain clock read as %r" % sync)
    for line in composed["lines"]:
        print("  composed | " + line)
    for line in flipped["lines"]:
        print("  flipped  | " + line)
    if problems:
        for p in problems:
            print("FAIL: " + p)
        return 1
    print("selftest ok")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("trace", nargs="?", help="the DECKBOY_PRESENT_TRACE file")
    parser.add_argument("--selftest", action="store_true", help="check the reading on made-up traces")
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if not args.trace:
        parser.error("give a trace file, or --selftest")
    with open(args.trace, encoding="utf-8", errors="replace") as handle:
        report = analyse(parse(handle))
    print("\n".join(report["lines"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
