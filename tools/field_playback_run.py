#!/usr/bin/env python3
"""Playback measurements on a machine with Deckboy and Python but no ffmpeg.

WHY THIS EXISTS. The playback checks need ffmpeg to build their clips and to
read their recordings, and the machines that most need measuring -- a Mac
field box, a Linux show PC -- often have neither ffmpeg nor a toolchain. So
this does only the part that has to happen THERE: it plays clips made
elsewhere, records what came out, and samples Deckboy's own STATUS. The
recordings then go back to a machine with ffmpeg, where the same checks judge
them:

    python3 tools/check_loop_seam.py --analyze <loop recording>
    python3 tools/record_avsync_check.py --analyze <sync recording>

Make the clips on a machine with ffmpeg:

    python3 tools/check_loop_seam.py --write-clip loop60.mkv
    python3 tools/record_avsync_check.py --write-clip sync30.mp4

Then, on the field machine (standard library only):

    python3 field_playback_run.py --exe Deckboy.app/Contents/MacOS/Deckboy \\
        --loop-clip loop60.mkv --sync-clip sync30.mp4 --out ~/deckboy-field

Each phase starts its own Deckboy with its own show file and state folder, so
nothing on the machine is touched outside --out. The output is fullscreen on
the first screen while it runs. Sound: the recordings need it, the room does
not -- on macOS the system output is muted first (--no-mute to skip).
"""
import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time


def send(port, command, timeout=5.0):
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall((command + "\n").encode())
        data = b""
        try:
            while True:
                part = sock.recv(65536)
                if not part:
                    break
                data += part
                sock.settimeout(0.05)
        except OSError:
            pass
    return data.decode(errors="replace").strip()


def launch(exe, folder, clip, port):
    os.makedirs(os.path.join(folder, "state"), exist_ok=True)
    show = os.path.join(folder, "field.deckboy")
    with open(show, "w", encoding="utf-8") as f:
        f.write("update_check\t0\nui_sounds\t0\n")
    env = dict(os.environ)
    env["DECKBOY_STATE_DIR"] = os.path.join(folder, "state")
    env["DECKBOY_COMPANION_PORT"] = str(port)
    log = open(os.path.join(folder, "app.log"), "w")
    proc = subprocess.Popen([exe, "--allow-multi-instance", show, "--import", clip],
                            env=env, cwd=folder, stdout=log, stderr=subprocess.STDOUT)
    for _ in range(120):
        try:
            send(port, "PING", timeout=1.0)
            return proc, log
        except OSError:
            time.sleep(0.5)
    proc.kill()
    raise RuntimeError("Deckboy did not answer on port %d -- see %s" % (port, os.path.join(folder, "app.log")))


def stop(proc, log):
    proc.terminate()
    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()
    log.close()


def status_values(port):
    text = send(port, "STATUS")
    decode = re.search(r"decode_fps=([\d.]+)", text)
    out = re.search(r"OUTPUT 1 .*?presents=(\d+).*?missed=(\d+)", text)
    target = re.search(r"OUTPUT 1 .*?target_hz=([\d.]+)", text)
    return (float(decode[1]) if decode else None,
            int(out[1]) if out else None, int(out[2]) if out else None,
            float(target[1]) if target else None)


def recordings(folder):
    found = []
    for base, _, files in os.walk(folder):
        for name in files:
            if name.startswith("program") and name.rsplit(".", 1)[-1] in ("mp4", "mov", "mkv"):
                found.append(os.path.join(base, name))
    return sorted(found, key=os.path.getmtime)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, help="the Deckboy binary")
    parser.add_argument("--loop-clip", required=True)
    parser.add_argument("--sync-clip", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--seconds", type=float, default=60.0, help="pacing measurement length")
    parser.add_argument("--port", type=int, default=5624)
    parser.add_argument("--no-mute", action="store_true")
    parser.add_argument("--display", type=int, default=0,
                        help="put the output on this screen (1 = the first) before arming it")
    args = parser.parse_args()
    out = os.path.abspath(os.path.expanduser(args.out))
    os.makedirs(out, exist_ok=True)
    exe = os.path.abspath(os.path.expanduser(args.exe))
    loop_clip = os.path.abspath(os.path.expanduser(args.loop_clip))
    sync_clip = os.path.abspath(os.path.expanduser(args.sync_clip))
    if sys.platform == "darwin" and not args.no_mute:
        subprocess.run(["osascript", "-e", "set volume output muted true"], check=False)
    summary = {"machine": sys.platform}
    screen = ["DISPLAY %d" % args.display] if args.display > 0 else []

    def counters(port, start, seconds):
        end = status_values(port)
        if None in (start[1], end[1]):
            return None
        return {"presents_per_second": round((end[1] - start[1]) / seconds, 2),
                "missed": end[2] - start[2], "target_hz": end[3]}

    # 1. PACING: the loop clip fullscreen on the first screen, playing round,
    # with Deckboy's own counters sampled twice a second.
    folder = os.path.join(out, "pacing")
    proc, log = launch(exe, folder, loop_clip, args.port)
    try:
        for command in screen + ["SELECT 1", "LOOP ON", "OUTPUT ON", "TAKE"]:
            send(args.port, command)
        time.sleep(6.0)
        start = status_values(args.port)
        samples = []
        t0 = time.monotonic()
        while time.monotonic() - t0 < args.seconds:
            samples.append(status_values(args.port)[0])
            time.sleep(0.5)
        end = status_values(args.port)
        elapsed = time.monotonic() - t0
        decodes = [d for d in samples if d is not None]
        summary["pacing"] = {
            "seconds": round(elapsed, 1),
            "presents_per_second": round((end[1] - start[1]) / elapsed, 2) if end[1] is not None and start[1] is not None else None,
            "missed": (end[2] - start[2]) if end[2] is not None and start[2] is not None else None,
            "target_hz": end[3],
            "decode_min": min(decodes) if decodes else None,
            "decode_mean": round(sum(decodes) / len(decodes), 2) if decodes else None,
        }
    finally:
        stop(proc, log)
    print("pacing:", json.dumps(summary["pacing"]), flush=True)

    # 2. LOOP SEAMS: the loop clip recorded through several passes.
    folder = os.path.join(out, "loop")
    proc, log = launch(exe, folder, loop_clip, args.port)
    try:
        for command in screen + ["OUTPUT ON", "VIDEO 640x360", "RECFORMAT 640x360 60", "SELECT 1", "LOOP ON", "TAKE"]:
            send(args.port, command)
        time.sleep(2.5)
        send(args.port, "RECORD on")
        start = status_values(args.port)
        time.sleep(10.5)
        summary["loop_output"] = counters(args.port, start, 10.5)
        send(args.port, "RECORD off")
        time.sleep(4.0)
    finally:
        stop(proc, log)
    summary["loop_recordings"] = recordings(folder)
    print("loop recordings:", summary["loop_recordings"], flush=True)
    print("output while recording the loop:", json.dumps(summary.get("loop_output")), flush=True)

    # 3. A/V SYNC: the flash-and-beep clip recorded at 30 fps.
    folder = os.path.join(out, "sync")
    proc, log = launch(exe, folder, sync_clip, args.port)
    try:
        for command in screen + ["OUTPUT ON", "RECFORMAT 1920x1080 30", "SELECT 1", "TAKE"]:
            send(args.port, command)
        time.sleep(3.0)
        send(args.port, "RECORD on")
        start = status_values(args.port)
        time.sleep(20.0)
        summary["sync_output"] = counters(args.port, start, 20.0)
        send(args.port, "RECORD off")
        time.sleep(4.0)
    finally:
        stop(proc, log)
    summary["sync_recordings"] = recordings(folder)
    print("sync recordings:", summary["sync_recordings"], flush=True)
    print("output while recording the sync clip:", json.dumps(summary.get("sync_output")), flush=True)

    with open(os.path.join(out, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
    print("summary:", os.path.join(out, "summary.json"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
