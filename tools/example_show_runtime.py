"""Isolated runtime for authoring and checking the bundled example shows."""
import ctypes
import os
from pathlib import Path
import random
import socket
import subprocess
import time

from deckboy_testroot import populate

ROOT = Path(__file__).resolve().parents[1]


class Runtime:
    def __init__(self, exe, work, show, extra=(), env=None):
        self.exe, self.work, self.show = Path(exe).resolve(), Path(work).resolve(), Path(show).resolve()
        self.extra = extra
        # Changes to the app's environment; None removes a variable (so a
        # check can have the real audio driver instead of the dummy one).
        self.env_changes = env or {}
        self.proc = None
        self.desktop = None

    def __enter__(self):
        self.work.mkdir(parents=True, exist_ok=True)
        populate(str(self.work), str(ROOT / "data"))
        # Deckboy binds TCP and UDP to one port. Avoid Windows' high excluded
        # ranges and check both protocols before handing the port to the app.
        for _ in range(100):
            candidate = random.SystemRandom().randrange(12000, 30000)
            try:
                with socket.socket() as tcp, socket.socket(type=socket.SOCK_DGRAM) as udp:
                    tcp.bind(("127.0.0.1", candidate))
                    udp.bind(("127.0.0.1", candidate))
                self.port = candidate
                break
            except OSError:
                continue
        else:
            raise RuntimeError("No free local TCP/UDP control port")
        env = dict(os.environ, DECKBOY_ROOT=str(self.work), DECKBOY_STATE_DIR=str(self.work / "state"),
                   DECKBOY_PROJECT=str(self.show), DECKBOY_COMPANION_PORT=str(self.port),
                   DECKBOY_HIDE_WINDOWS="1", SDL_AUDIODRIVER="dummy")
        for key, value in self.env_changes.items():
            if value is None:
                env.pop(key, None)
            else:
                env[key] = value
        kwargs = {}
        if os.name == "nt":
            # WebView may create visible children: keep the entire process on
            # a private desktop, never switch the user's desktop.
            self.user32 = ctypes.WinDLL("user32", use_last_error=True)
            self.user32.CreateDesktopW.restype = ctypes.c_void_p
            self.user32.CreateDesktopW.argtypes = [
                ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_void_p,
                ctypes.c_uint, ctypes.c_uint, ctypes.c_void_p]
            name = "DeckboyExamples" + str(self.port)
            self.desktop = self.user32.CreateDesktopW(name, None, None, 0, 0x1FF, None)
            if not self.desktop:
                raise ctypes.WinError(ctypes.get_last_error())
            startup = subprocess.STARTUPINFO()
            startup.lpDesktop = "WinSta0\\" + name
            kwargs.update(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
        self.log = (self.work / "app.log").open("w", encoding="utf8")
        try:
            self.proc = subprocess.Popen(
                [str(self.exe), "--allow-multi-instance", str(self.show),
                 "--ui-size", "2400", "1350", *self.extra],
                env=env, cwd=self.work, stdout=self.log, stderr=self.log, **kwargs)
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                if self.proc.poll() is not None:
                    raise RuntimeError("App exited: " + (self.work / "app.log").read_text(errors="replace"))
                try:
                    self.send("PING")
                    return self
                except OSError:
                    time.sleep(.15)
            raise TimeoutError("Deckboy did not answer")
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *_):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(10)
        self.log.close()
        if self.desktop:
            self.user32.CloseDesktop.argtypes = [ctypes.c_void_p]
            self.user32.CloseDesktop(self.desktop)

    def send(self, command):
        with socket.create_connection(("127.0.0.1", self.port), 15) as s:
            s.sendall((command + "\n").encode())
            data = b""
            while b"\n" not in data:
                part = s.recv(65536)
                if not part:
                    break
                data += part
            # STATUS is multiline. Read the complete reply, not just its
            # first packet, while the command connection remains open.
            s.settimeout(.04)
            try:
                while True:
                    part = s.recv(65536)
                    if not part:
                        break
                    data += part
            except socket.timeout:
                pass
        reply = data.decode(errors="replace").strip()
        query = command in ("STATUS", "STATUS JSON", "STATUS CUES")
        if not reply or reply.startswith("ERR") or (not query and not reply.startswith("OK")):
            raise RuntimeError(command + " => " + reply)
        return reply

    def commands(self, *commands):
        for command in commands:
            self.send(command)

    def saved(self):
        # Every edit is autosaved. Wait for the file to settle rather than
        # sending a nonexistent SAVE verb.
        previous = None
        deadline = time.monotonic() + 12
        stable = 0
        while time.monotonic() < deadline:
            time.sleep(.3)
            current = self.show.read_bytes()
            stable = stable + 1 if current == previous else 0
            if stable >= 4:
                return current.decode("utf8")
            previous = current
        raise TimeoutError("Show did not finish saving")

    def snapshot(self, path, output=False):
        path = Path(path).resolve()
        path.parent.mkdir(parents=True, exist_ok=True)
        if path.exists():
            path.unlink()
        self.send(("OUTSNAP " if output else "UISNAP ") + str(path) + (" 1" if output else ""))
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if path.exists() and path.stat().st_size > 54:
                time.sleep(.15)
                return path
            time.sleep(.1)
        raise TimeoutError("No snapshot: " + str(path))
