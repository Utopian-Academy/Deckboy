"""Measure the vocal-tract voice's vowels (core/vocal_tract.hpp) against targets.

Renders the held vowels with deckboy-voice-check, finds F1 and F2 of each by
linear prediction, and compares them with the targets: u and a from the
reference video James chose (a vocal-tract simulator on Pink Trombone's model,
2026-11-14: u 380/1028 Hz, a 734/1145 Hz), the rest from Peterson and Barney's
adult averages. A vowel passes within 18% on both formants: the tract's
vowels are a cartoon's, and the check is that each sits where its neighbours
expect it, not that it is one person's.

  py -3 tools/check_voice.py [--exe build/windows/Release/deckboy-voice-check.exe] [--keep DIR]
"""
import argparse
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGETS = {
    "i": (270, 2290), "I": (390, 1990), "e": (530, 1840), "ae": (660, 1720),
    "a": (734, 1145), "o": (570, 840), "u": (380, 1028), "U": (440, 1020), "^": (640, 1190),
}
TOLERANCE = 0.18


def read(path):
    with wave.open(path, "rb") as w:
        rate = w.getframerate()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float64) / 32768.0
    return data, rate


def formants(x, rate, order=12, target_rate=10000):
    step = rate // target_rate
    x = x[len(x) // 3: len(x) // 3 + rate // 4]          # a steady quarter second
    # a cheap low-pass before decimating
    k = np.ones(step) / step
    x = np.convolve(x, k, mode="same")[::step]
    fs = rate / step
    x = np.append(x[0], x[1:] - 0.63 * x[:-1])             # pre-emphasis
    x = x * np.hamming(len(x))
    r = np.correlate(x, x, mode="full")[len(x) - 1: len(x) + order]
    a = np.zeros(order + 1)
    a[0] = 1.0
    e = r[0]
    for i in range(1, order + 1):                           # Levinson-Durbin
        acc = r[i] + np.dot(a[1:i], r[i - 1:0:-1])
        kk = -acc / e
        a[1:i + 1] = a[1:i + 1] + kk * np.append(a[i - 1:0:-1], 1.0)
        e *= 1.0 - kk * kk
    roots = [z for z in np.roots(a) if z.imag > 0]
    found = []
    for z in roots:
        f = np.angle(z) * fs / (2 * np.pi)
        bw = -fs / np.pi * np.log(abs(z))
        if f > 150 and bw < 500:
            found.append(f)
    return sorted(found)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=os.path.join(ROOT, "build", "windows", "Release", "deckboy-voice-check.exe"))
    ap.add_argument("--keep", help="write the WAVs here and keep them")
    args = ap.parse_args()
    out = args.keep or tempfile.mkdtemp(prefix="deckboy-voice-")
    os.makedirs(out, exist_ok=True)
    r = subprocess.run([args.exe, out], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout + r.stderr)
        return 1
    failures = 0
    print("%-4s %-16s %-16s %s" % ("", "F1 F2 found", "F1 F2 target", ""))
    for v, (t1, t2) in TARGETS.items():
        name = "vowel_%s.wav" % ("cup" if v == "^" else v + "_lax" if v.isupper() else v)   # as voice_check names them
        x, rate = read(os.path.join(out, name))
        f = formants(x, rate)
        if len(f) < 2:
            print("%-4s FAIL fewer than two formants: %s" % (v, f))
            failures += 1
            continue
        f1, f2 = f[0], f[1]
        ok = abs(f1 - t1) / t1 <= TOLERANCE and abs(f2 - t2) / t2 <= TOLERANCE
        failures += 0 if ok else 1
        print("%-4s %5.0f %5.0f      %5d %5d       %s" % (v, f1, f2, t1, t2, "ok" if ok else "OFF"))
    print("voice vowels: %s" % ("ok" if failures == 0 else "%d off" % failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
