"""A damaged show file opens with bounded memory instead of asking for the world.

The 2026-10-19 audit (F6) found that a cue line's composite slot count was
trusted before the loader looked for the slots behind it, so one corrupt number
reserved memory for two billion slots. This writes the first example show with
that count set to 2000000000, opens it in a hidden instance, and requires the app
to answer with its cues intact while staying under 1 GB.

    python tools/check_corrupt_show.py [--exe build/windows/Release/Deckboy.exe]
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from check_control_trust import free_port, session  # noqa: E402
from deckboy_testroot import populate  # noqa: E402

COUNT_FIELD = 68  # "cue", deck, then the loader's offset + 66
LIMIT_BYTES = 1 << 30


def peak_memory(pid):
    """Peak working set in bytes, or None where the platform cannot say."""
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes

        class Counters(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                        ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                        ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]
        handle = ctypes.windll.kernel32.OpenProcess(0x0410, False, pid)
        if not handle:
            return None
        counters = Counters()
        counters.cb = ctypes.sizeof(counters)
        ok = ctypes.windll.psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb)
        ctypes.windll.kernel32.CloseHandle(handle)
        return max(counters.PeakWorkingSetSize, counters.PeakPagefileUsage) if ok else None
    try:
        for line in Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("VmHWM:"):
                return int(line.split()[1]) * 1024
    except OSError:
        pass
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", default=str(ROOT / "build/windows/Release/Deckboy.exe"))
    args = parser.parse_args()
    (ROOT / "build").mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="corrupt-show-", dir=ROOT / "build"))
    populate(str(work))
    source = ROOT / "data/demos/starter-pack/01-picture-on-screen.deckboy"
    lines = source.read_text(encoding="utf-8").splitlines()
    corrupted = 0
    for i, line in enumerate(lines):
        fields = line.split("\t")
        if fields[0] == "cue" and len(fields) > COUNT_FIELD and corrupted == 0:
            if fields[COUNT_FIELD] != "0":
                print(f"FAIL field {COUNT_FIELD} is {fields[COUNT_FIELD]!r}, not the slot count; the format moved")
                return 1
            fields[COUNT_FIELD] = "2000000000"
            lines[i] = "\t".join(fields)
            corrupted += 1
    show = work / "corrupt.deckboy"
    show.write_text("\n".join(lines) + "\nupdate_check\t0\nui_sounds\t0\n", encoding="utf-8")
    port = free_port()
    env = dict(os.environ, DECKBOY_ROOT=str(work), DECKBOY_STATE_DIR=str(work / "state"),
               DECKBOY_PROJECT=str(show), DECKBOY_COMPANION_PORT=str(port),
               SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", DECKBOY_RENDERER="software",
               DECKBOY_HIDE_WINDOWS="1")
    log = (work / "app.log").open("w", encoding="utf-8")
    proc = subprocess.Popen([args.exe, "--allow-multi-instance", str(show)], cwd=work, env=env,
                            stdout=log, stderr=log,
                            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    failures = 0
    try:
        reply = ""
        until = time.monotonic() + 45
        while proc.poll() is None and time.monotonic() < until:
            reply = (session("127.0.0.1", port, ["STATUS"], timeout=2) or [""])[0]
            if reply:
                break
            time.sleep(0.3)
        alive = proc.poll() is None
        print(("ok   " if alive else "FAIL ") + "the app opened the damaged show and kept running")
        failures += not alive
        ok = "decks=1" in reply
        print(("ok   " if ok else "FAIL ") + "it answers with its deck intact" + ("" if ok else f"  [{reply[:120]}]"))
        failures += not ok
        peak = peak_memory(proc.pid) if alive else None
        if peak is None:
            print("skip peak memory: not measurable here")
        else:
            ok = peak < LIMIT_BYTES
            print(("ok   " if ok else "FAIL ") + f"peak memory {peak / 1048576:.0f} MB (limit {LIMIT_BYTES >> 20} MB)")
            failures += not ok
    finally:
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=10)
        log.close()
    print(f"corrupt show: {failures} failure(s); evidence: {work}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
