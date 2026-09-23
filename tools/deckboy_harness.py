"""Launch an isolated Deckboy and drive it the way an operator's controller does.

The check_* harnesses built on this measure what the app DOES -- pixels in a
recording, lines in the saved show -- not what it says over the socket. A
setter that answers OK proves only that a setter ran.

Four traps, each of which has already produced a false result here:

  - RECORD makes its own stream output that MIRRORS THE PROGRAMME. Measure an
    effect that lands on some other output and you are measuring the wrong
    picture, and can "prove" a feature both broken and working.
  - STATUS reports the ACTIVE cue. Read dur= before anything is taken and every
    cue kind there is reads 00:00.0.
  - Default deck names follow their position. Remove deck 2 and deck 3 becomes
    "Deck 2"; assert on names without knowing that and you report a product
    fault that is your own assumption.
  - Scripted mouse and keyboard input does not reach SDL3. The socket verbs and
    screenshots are the only ways in; --menu source|routing|cue opens a menu
    for a screenshot, and check_preview_effects.py has a window capture.

Every run gets its own DECKBOY_ROOT (a dressed one, see deckboy_testroot) and
its own port, so a harness never touches the operator's show or collides with a
Deckboy that is already running. And every run starts from a FRESH show -- one
playlist, one output -- never a copy of data/default.deckboy. That file is the
operator's working show and is gitignored: copying it tests whatever this
machine's show happens to hold (four playlists, on the day that was found), and
on a CI runner it does not exist at all.

On a machine where someone else is building, run against a PRIVATE COPY of the
build under another name (copy build/windows/Release to a temp folder, rename
Deckboy.exe, pass --exe). A relink replaces the binary mid-run, and a
kill-by-name before a rebuild takes every Deckboy with it: both show up here as
"Deckboy exited (code 4294967295)", which is a stopped process, not a crash.
"""

import argparse
import glob
import io
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

TOOLS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import deckboy_testroot  # noqa: E402

# Where a build lands: the local Windows build, CI's Windows build, then the
# Linux and macOS trees.
EXE_CANDIDATES = (
    os.path.join("build", "windows", "Release", "Deckboy.exe"),
    os.path.join("build", "Release", "Deckboy.exe"),
    os.path.join("build", "Deckboy"),
    os.path.join("build", "Deckboy.app", "Contents", "MacOS", "Deckboy"),
)


def default_exe():
    for rel in EXE_CANDIDATES:
        path = os.path.join(REPO, rel)
        if os.path.isfile(path):
            return path
    return ""


def parse_args(description, default_port):
    parser = argparse.ArgumentParser(description=description,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", default=default_exe(),
                        help="the Deckboy binary (default: the first build found under build/)")
    parser.add_argument("--port", type=int, default=default_port,
                        help="remote-control port for this run (default %(default)s)")
    parser.add_argument("--ffmpeg", default="", help="ffmpeg for reading recordings back")
    parser.add_argument("--keep", action="store_true", help="keep the test root afterwards")
    args = parser.parse_args()
    if not args.exe or not os.path.isfile(args.exe):
        parser.error("no Deckboy binary found; build one or pass --exe")
    args.exe = os.path.abspath(args.exe)
    return args


def find_ffmpeg(exe, given=""):
    """The ffmpeg bundled beside the binary first: it is the one that ships."""
    if given:
        return given
    for name in ("ffmpeg.exe", "ffmpeg"):
        beside = os.path.join(os.path.dirname(exe), name)
        if os.path.isfile(beside):
            return beside
    staged = sorted(glob.glob(os.path.join(REPO, "dist", "staging", "*", "ffmpeg*")))
    return staged[-1] if staged else (shutil.which("ffmpeg") or "")


class Deckboy:
    """One isolated Deckboy, started on entry and stopped on exit."""

    def __init__(self, args, prefix, extra_args=(), port=None):
        self.args = args
        self.port = port or args.port
        self.root = tempfile.mkdtemp(prefix=prefix)
        os.makedirs(os.path.join(self.root, "data"), exist_ok=True)
        self.show = os.path.join(self.root, "data", "default.deckboy")
        deckboy_testroot.populate(self.root)
        self.recordings = os.path.join(os.path.dirname(self.show), "recordings")
        self.extra_args = list(extra_args)
        self.proc = None
        self.log = None

    def __enter__(self):
        env = dict(os.environ)
        env.update(DECKBOY_ROOT=self.root, DECKBOY_PROJECT=self.show,
                   DECKBOY_COMPANION_PORT=str(self.port))
        self.log = open(os.path.join(self.root, "app.log"), "w")
        self.proc = subprocess.Popen([self.args.exe, self.show] + self.extra_args, env=env,
                                     cwd=os.path.dirname(self.args.exe),
                                     stdout=self.log, stderr=subprocess.STDOUT)
        # A failure from here on is raised out of __enter__, and Python does
        # not call __exit__ for that: without this the app would be left
        # running -- one stray per failed run, holding the binary open.
        try:
            self.wait_ready()
            self.send("MASTERVOL 0")
            self.send("SHUFFLE OFF")
            time.sleep(1.0)
        except BaseException:
            self.__exit__(None, None, None)
            raise
        return self

    def __exit__(self, *exc):
        """Stop the app and make sure it is gone, not just asked to go."""
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=10)
        self.log.close()
        if not self.args.keep:
            shutil.rmtree(self.root, ignore_errors=True)
        return False

    def wait_ready(self, seconds=60.0):
        deadline = time.time() + seconds
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError("Deckboy exited during startup (code %s); log in %s"
                                   % (self.proc.returncode, self.root))
            try:
                self.send("HELP", timeout=2.0)
                return
            except OSError:
                time.sleep(0.5)
        raise RuntimeError("Deckboy never answered on port %d" % self.port)

    def send(self, command, timeout=10.0):
        try:
            with socket.create_connection(("127.0.0.1", self.port), timeout=timeout) as s:
                s.settimeout(timeout)
                s.sendall((command + "\n").encode())
                try:
                    return s.recv(65536).decode(errors="replace").strip()
                except OSError:
                    return ""
        except OSError:
            # A refused connection mid-run is a dead app, not a network fault:
            # say so, with the exit code that separates a clean "no" from a crash.
            if self.proc is not None and self.proc.poll() is not None:
                self.args.keep = True
                raise RuntimeError("Deckboy exited (code %s) while handling %r; its log is %s"
                                   % (self.proc.returncode, command,
                                      os.path.join(self.root, "app.log")))
            raise

    def status(self, deck_prefix, key):
        """One key=value from a STATUS line, e.g. status("DECK 1", "pos").
        STATUS describes the ACTIVE cue: take one before reading dur=."""
        for line in self.send("STATUS").splitlines():
            if line.startswith(deck_prefix):
                for token in line.split():
                    if token.startswith(key + "="):
                        return token[len(key) + 1:]
        return ""

    def record(self, seconds=5.0):
        """Record the PROGRAMME for `seconds`; the path of the new file, or None.
        The recording mirrors the programme, not any other output."""
        def listing():
            return set(os.listdir(self.recordings)) if os.path.isdir(self.recordings) else set()

        before = listing()
        started = self.send("RECORD START")
        time.sleep(seconds)
        stopped = self.send("RECORD STOP")
        # Wait for the file to be FINISHED, not for a fixed time: the encoder
        # drains after STOP, and at 4K that outlasted the three seconds this
        # used to sleep, so a frame was asked of a file still being written.
        path, last_size, steady = None, -1, 0
        deadline = time.time() + 30.0
        while time.time() < deadline and steady < 3:
            time.sleep(0.5)
            new = sorted(listing() - before)
            if not new:
                continue
            path = os.path.join(self.recordings, new[-1])
            size = os.path.getsize(path)
            steady = steady + 1 if size == last_size and size > 0 else 0
            last_size = size
        if not path or last_size <= 0:
            # Say WHY, in the run's own output: on a CI runner this is the
            # only evidence there will be once the root is cleaned up.
            print("   no recording: RECORD START -> %r, STOP -> %r" % (started[:100], stopped[:100]))
            print("   recordings folder: %s" % (sorted(listing()) or "empty or missing"))
            print("   ffmpeg on PATH: %s" % (shutil.which("ffmpeg") or "none"))
            # Did it land somewhere else? The root, and the user's Videos folder.
            since = time.time() - seconds - 60
            for top in (self.root, os.path.join(os.path.expanduser("~"), "Videos")):
                for dirpath, _, files in os.walk(top):
                    for f in files:
                        full = os.path.join(dirpath, f)
                        if f.lower().endswith((".mp4", ".mov", ".mkv")) and os.path.getmtime(full) > since:
                            print("   recent video elsewhere: %s (%d bytes)" % (full, os.path.getsize(full)))
            self.log_excerpt(r".", lines=40)
        return path

    def log_excerpt(self, pattern, lines=15):
        """Print the app log's last lines matching `pattern` (a regex)."""
        import re
        self.log.flush()
        try:
            text = io.open(os.path.join(self.root, "app.log"), encoding="utf-8", errors="replace").read()
        except OSError:
            return
        hits = [line for line in text.splitlines() if re.search(pattern, line, re.I)]
        for line in hits[-lines:]:
            print("   log: %s" % line[:200])

    def saved_show(self, settle=5.0):
        """The show file as saved, after giving the autosave time to land."""
        time.sleep(settle)
        return io.open(self.show, encoding="utf-8", errors="replace").read()

    def frame(self, mp4, at="00:00:02"):
        """One frame of a recording as (width, height, rgb24 bytes), or None."""
        if not mp4:
            return None
        ffmpeg = find_ffmpeg(self.args.exe, self.args.ffmpeg)
        if not ffmpeg:
            print("   no ffmpeg to read the recording back: pass --ffmpeg or put one on PATH")
            return None
        ppm = os.path.join(self.root, "frame.ppm")
        if os.path.exists(ppm):
            os.unlink(ppm)
        run = subprocess.run([ffmpeg, "-nostdin", "-loglevel", "error", "-y", "-ss", at, "-i", mp4,
                              "-frames:v", "1", "-pix_fmt", "rgb24", "-f", "image2", ppm],
                             capture_output=True, text=True)
        if not os.path.exists(ppm):
            print("   could not read a frame from %s (%d bytes): %s"
                  % (os.path.basename(mp4), os.path.getsize(mp4), run.stderr.strip()[:200]))
            return None
        with open(ppm, "rb") as f:
            assert f.readline().strip() == b"P6"
            line = f.readline()
            while line.startswith(b"#"):
                line = f.readline()
            w, h = map(int, line.split())
            f.readline()
            return w, h, f.read(w * h * 3)


def mean_rgb(frame):
    if not frame:
        return None
    w, h, px = frame
    n = float(w * h)
    return {"w": w, "h": h, "r": sum(px[0::3]) / n, "g": sum(px[1::3]) / n, "b": sum(px[2::3]) / n}


def finish(name, fails, notes=()):
    """Print the verdict the way every harness does, and return the exit code."""
    print()
    for note in notes:
        print(note)
    for f in fails:
        print("FAIL:", f)
    if not fails:
        print("%s: all cases pass" % name)
    return 1 if fails else 0
