#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Deckboy Contributors
# This file is part of Deckboy, a cue deck for live events.
# See LICENSE for details.
"""Drive Deckboy's vMix-compatible surface the way a real panel would.

WHAT THIS IS FOR. The point of speaking vMix is that panels nobody here wrote
can drive the desk. So the test has to behave like one of those panels rather
than like code that knows how the server is built: it opens the documented
ports, sends the documented commands, and reads the replies as a client with
no inside knowledge would.

The things it insists on, each because getting them wrong would break a real
panel silently:

  THE XML PARSES, with a real XML parser. Cue names come from filenames and
  carry ampersands and angle brackets; a document that a panel's parser
  rejects is worse than no document, because the panel shows stale state
  rather than an error.

  TALLY IS ONE DIGIT PER INPUT. Panels index into that string. A length that
  disagrees with the input count lights the wrong button.

  A FUNCTION THAT DOES NOT EXIST IS REFUSED. vMix itself answers success to a
  Function that failed, and a desk that says a cue was taken when it was not
  is how a show goes dark with every light green. HTTP answers 404, TCP
  answers ER.

  SUBSCRIBE PUSHES. That is the entire reason a panel uses the TCP surface
  rather than polling XML, and a subscription that never pushes looks exactly
  like a quiet show.

Usage:
    python tools/check_vmix_api.py [--exe PATH]
"""
import argparse
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def default_exe():
    for rel in ("build/windows/Release/Deckboy.exe", "build/Release/Deckboy",
                "build/Deckboy", "build/linux/Deckboy"):
        p = os.path.join(ROOT, rel.replace("/", os.sep))
        if os.path.isfile(p):
            return p
    return None


def wait_for_port(port, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            socket.create_connection(("127.0.0.1", port), 0.5).close()
            return True
        except OSError:
            time.sleep(0.3)
    return False


class Control:
    """Deckboy's own port, used only to set the desk up."""

    def __init__(self, port=5510):
        self.sock = socket.create_connection(("127.0.0.1", port), 3.0)
        self.sock.settimeout(3.0)

    def send(self, line, seconds=0.8):
        self.sock.sendall((line + "\n").encode("utf-8"))
        deadline = time.time() + seconds
        buf = b""
        while time.time() < deadline:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            buf += chunk
        return buf.decode("utf-8", "replace").strip()

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class VmixTcp:
    """A panel, speaking the documented text protocol and nothing else."""

    def __init__(self, port=8099):
        self.sock = socket.create_connection(("127.0.0.1", port), 3.0)
        self.sock.settimeout(3.0)
        self.buf = b""

    def line(self, command=None, seconds=2.0):
        if command is not None:
            self.sock.sendall((command + "\r\n").encode("utf-8"))
        deadline = time.time() + seconds
        while b"\r\n" not in self.buf and time.time() < deadline:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            self.buf += chunk
        if b"\r\n" not in self.buf:
            return ""
        line, self.buf = self.buf.split(b"\r\n", 1)
        return line.decode("utf-8", "replace")

    def xml(self):
        """XML <length>, then exactly that many bytes -- the documented shape."""
        header = self.line("XML")
        if not header.startswith("XML "):
            return header, ""
        try:
            length = int(header.split()[1])
        except (IndexError, ValueError):
            return header, ""
        deadline = time.time() + 3.0
        while len(self.buf) < length and time.time() < deadline:
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                break
            if not chunk:
                break
            self.buf += chunk
        body = self.buf[:length].decode("utf-8", "replace")
        self.buf = self.buf[length:]
        return header, body

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def http_get(url):
    try:
        with urllib.request.urlopen(url, timeout=5) as r:
            return r.status, r.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except OSError as e:
        return 0, str(e)


def make_media(path, name):
    out = os.path.join(path, name)
    subprocess.run(
        ["ffmpeg", "-y", "-v", "error", "-f", "lavfi",
         "-i", "testsrc=size=160x120:rate=10:duration=1",
         "-pix_fmt", "yuv420p", out],
        capture_output=True, check=False)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=default_exe())
    ap.add_argument("--http", type=int, default=8088)
    ap.add_argument("--tcp", type=int, default=8099)
    args = ap.parse_args()

    if not args.exe or not os.path.isfile(args.exe):
        sys.exit("check_vmix_api: no Deckboy binary; pass --exe")

    work = tempfile.mkdtemp(prefix="deckboy_vmix_")
    state = os.path.join(work, "state")
    os.makedirs(state)
    env = dict(os.environ)
    env["DECKBOY_STATE_DIR"] = state

    # Two cues. The second is RENAMED over the control port rather than given
    # an awkward FILENAME, because Windows will not allow < or > in one -- and
    # those are exactly the characters that break a document built by pasting
    # strings together. A cue named from a subtitle, a lower third or an
    # operator's own typing can contain any of them.
    # Imported by LAUNCHING with --import, because there is no IMPORT verb on
    # the control port -- and --import expands a FOLDER into every playable
    # file in it, so one argument brings both cues in.
    media = os.path.join(work, "media")
    os.makedirs(media)
    clip = make_media(media, "plain.mp4")
    second = make_media(media, "second.mp4")
    AWKWARD = "Q&A <live> \"quoted\" it's"


    failures = []
    app = subprocess.Popen([args.exe, "--allow-multi-instance",
                            "--import", media], env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    control = None
    panel = None
    try:
        if not wait_for_port(5510, 45):
            sys.exit("check_vmix_api: Deckboy's control port never opened")
        control = Control()
        # The import runs at launch; give the metadata probe a moment.
        time.sleep(3.0)
        # Select the second cue and give it the awkward name.
        control.send("DECK 1 SELECT 2")
        control.send("RENAME " + AWKWARD)
        time.sleep(0.8)

        reply = control.send("VMIX ON")
        if not reply.startswith("OK"):
            failures.append("VMIX ON was refused: " + reply)
        print("  VMIX ON: " + reply)

        if not wait_for_port(args.tcp, 15):
            sys.exit("check_vmix_api: the vMix TCP port never opened")
        if not wait_for_port(args.http, 15):
            failures.append("the vMix HTTP port never opened")

        # ── HTTP: the state document ────────────────────────────────────────
        status, body = http_get("http://127.0.0.1:%d/api" % args.http)
        if status != 200:
            failures.append("GET /api answered %s" % status)
        root = None
        try:
            root = ET.fromstring(body)
        except ET.ParseError as e:
            failures.append("the XML does not parse: %s" % e)
        if root is not None:
            inputs = root.findall("./inputs/input")
            print("  GET /api: %d inputs, active=%s preview=%s"
                  % (len(inputs),
                     root.findtext("active"), root.findtext("preview")))
            if len(inputs) < 2:
                failures.append("expected at least the two imported cues, got %d"
                                % len(inputs))
            # The parser above has already done the real work: if the
            # escaping were wrong the document would not have parsed at all.
            # This checks the name came back INTACT rather than mangled into
            # something that merely parses.
            titles = [i.get("title") or "" for i in inputs]
            if AWKWARD not in titles:
                failures.append("the awkward cue name did not survive intact: %r"
                                % titles)
            for i in inputs:
                if not (i.get("number") or "").isdigit():
                    failures.append("an input has no usable number: %r" % i.attrib)
                    break

        # ── HTTP: an unknown Function must be refused ───────────────────────
        status, _ = http_get(
            "http://127.0.0.1:%d/api?Function=NoSuchFunctionAtAll" % args.http)
        if status != 404:
            failures.append("an unknown Function answered %s, not 404" % status)
        print("  unknown Function over HTTP: %s" % status)

        # ── HTTP: a real Function ───────────────────────────────────────────
        status, _ = http_get(
            "http://127.0.0.1:%d/api?Function=PreviewInput&Input=2" % args.http)
        if status != 200:
            failures.append("PreviewInput answered %s" % status)
        time.sleep(1.2)
        status, body = http_get("http://127.0.0.1:%d/api" % args.http)
        preview = ET.fromstring(body).findtext("preview") if status == 200 else "?"
        if preview != "2":
            failures.append("PreviewInput Input=2 left preview at %r" % preview)
        print("  PreviewInput Input=2 -> preview=%s" % preview)

        # ── TCP ─────────────────────────────────────────────────────────────
        panel = VmixTcp(args.tcp)
        version = panel.line("VERSION")
        if not version.startswith("VERSION OK"):
            failures.append("VERSION answered %r" % version)
        print("  VERSION: " + version)

        tally = panel.line("TALLY")
        if not tally.startswith("TALLY OK"):
            failures.append("TALLY answered %r" % tally)
        else:
            digits = tally.split(None, 2)[2] if len(tally.split(None, 2)) > 2 else ""
            expected = len(ET.fromstring(body).findall("./inputs/input"))
            if len(digits) != expected:
                failures.append("TALLY is %d digits for %d inputs"
                                % (len(digits), expected))
            print("  TALLY: %r (%d digits, %d inputs)"
                  % (digits, len(digits), expected))

        header, doc = panel.xml()
        if not header.startswith("XML "):
            failures.append("XML answered %r" % header)
        else:
            try:
                ET.fromstring(doc)
            except ET.ParseError as e:
                failures.append("the TCP XML body does not parse: %s" % e)
            if len(doc) != int(header.split()[1]):
                failures.append("XML said %s bytes and sent %d"
                                % (header.split()[1], len(doc)))
            print("  XML over TCP: %s bytes, parses" % header.split()[1])

        bad = panel.line("FUNCTION NoSuchFunctionAtAll")
        if not bad.startswith("FUNCTION ER"):
            failures.append("an unknown Function over TCP answered %r" % bad)
        print("  unknown Function over TCP: " + bad)

        unknown = panel.line("NOTAVERB")
        if not unknown.startswith("ER"):
            failures.append("an unknown command answered %r" % unknown)

        # ── SUBSCRIBE must actually push ────────────────────────────────────
        sub = panel.line("SUBSCRIBE TALLY")
        if not sub.startswith("SUBSCRIBE OK"):
            failures.append("SUBSCRIBE answered %r" % sub)
        # Change the tally from the other side and wait to be told.
        control.send("DECK 1 TAKE 1")
        pushed = panel.line(None, seconds=6.0)
        if not pushed.startswith("TALLY OK"):
            failures.append("SUBSCRIBE never pushed a tally change (got %r)" % pushed)
        else:
            print("  SUBSCRIBE pushed: " + pushed)
            if "1" not in pushed.split(None, 2)[-1]:
                failures.append("the pushed tally shows nothing on air: %r" % pushed)

        # ── OFF means the ports close ───────────────────────────────────────
        panel.close()
        panel = None
        control.send("VMIX OFF")
        time.sleep(1.5)
        status, _ = http_get("http://127.0.0.1:%d/api" % args.http)
        if status == 200:
            failures.append("the HTTP port still answers after VMIX OFF")
        print("  after VMIX OFF, HTTP: %s" % (status or "refused"))
    finally:
        if panel:
            panel.close()
        if control:
            try:
                control.send("QUIT", 0.3)
            except OSError:
                pass
            control.close()
        app.terminate()
        try:
            app.wait(timeout=15)
        except subprocess.TimeoutExpired:
            app.kill()
        shutil.rmtree(work, ignore_errors=True)

    if failures:
        print()
        for f in failures:
            print("FAIL " + f)
        print("check_vmix_api: %d failures" % len(failures))
        return 1
    print("check_vmix_api: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
