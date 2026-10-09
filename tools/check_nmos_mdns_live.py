"""Exercise the built Deckboy against a local fake IS-04 registry over real mDNS.

Standard library only. HTTP binds to 127.0.0.1; the responder answers only
this machine's legacy-unicast mDNS queries, never another LAN host. The app's
normal LAN multicast browse is used unchanged. Outputs are inactive, with
loopback RTP destinations; update checking and other integrations are off.
Artifacts and isolated app state stay under --out (default build/codex).

python tools/check_nmos_mdns_live.py --exe build/codex/Release/Deckboy.exe
On hosts whose hostname omits LAN interfaces, pass --interface <local IPv4>.
This is a protocol fixture, not vendor interoperability certification.
Run it alone: other NMOS discovery tests on this host share UDP 5353.
"""

import argparse
from collections import Counter
from contextlib import ExitStack
import http.client
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import threading
import time
import uuid


REPO = Path(__file__).resolve().parents[1]
SERVICES = ("_nmos-register._tcp.local", "_nmos-registration._tcp.local")
RESOURCE_PATH = "/x-nmos/registration/v1.3/resource"
HEALTH_PATH = "/x-nmos/registration/v1.3/health/nodes/"
EXPECTED = {"node": 1, "device": 1, "source": 2, "flow": 2, "sender": 2}


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def dns_name(value):
    return b"".join(bytes([len(label)]) + label.encode("ascii")
                    for label in value.split(".")) + b"\0"


def question(packet):
    """Decode the one uncompressed question emitted by the production browser."""
    if len(packet) < 17 or packet[2] & 0x80 or packet[4:6] != b"\0\1":
        return None
    pos, labels = 12, []
    while pos < len(packet):
        length = packet[pos]
        pos += 1
        if not length:
            break
        if length > 63 or pos + length > len(packet):
            return None
        labels.append(packet[pos:pos + length].decode("ascii").lower())
        pos += length
    if pos + 4 != len(packet):
        return None
    kind, cls = struct.unpack_from("!HH", packet, pos)
    return (".".join(labels), kind) if cls == 1 else None


class Responder:
    def __init__(self, interfaces, port, legacy):
        # Production discovery skips loopback. Do not answer a separate test
        # browser on 127.0.0.1 or mistake its traffic for the app's LAN browse.
        self.local = {address for address in interfaces if not address.startswith("127.")}
        self.service = SERVICES[int(legacy)]
        self.instance = "deckboy-check." + self.service
        self.host = "deckboy-check-" + uuid.uuid4().hex[:8] + ".local"
        self.port = port
        self.queries, self.errors = [], []
        self.done = threading.Event()

    def __enter__(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            if hasattr(socket, "SO_REUSEPORT"):
                self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            self.sock.bind(("", 5353))
            self.sock.settimeout(0.1)
            joined = []
            for address in sorted(self.local):
                try:
                    self.sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP,
                                        socket.inet_aton("224.0.0.251") + socket.inet_aton(address))
                    joined.append(address)
                except OSError:
                    pass
            require(any(not address.startswith("127.") for address in joined),
                    "No usable LAN mDNS interface; supply --interface <local IPv4>")
            self.thread = threading.Thread(target=self.run, daemon=True)
            self.thread.start()
            return self
        except BaseException:
            self.sock.close()
            raise

    def run(self):
        txt = ("api_proto=http", "api_ver=v1.3", "api_auth=false", "pri=10")
        answers = {
            (self.service, 12): dns_name(self.instance),
            (self.instance, 33): struct.pack("!HHH", 0, 0, self.port) + dns_name(self.host),
            (self.instance, 16): b"".join(bytes([len(item)]) + item.encode() for item in txt),
            (self.host, 1): socket.inet_aton("127.0.0.1"),
        }
        while not self.done.is_set():
            try:
                packet, sender = self.sock.recvfrom(9000)
                if sender[0] not in self.local or sender[1] == 5353:
                    continue
                q = question(packet)
                if not q or not (q[0] in SERVICES or q in answers):
                    continue
                self.queries.append({"name": q[0], "type": q[1], "source": sender})
                if q not in answers:
                    continue
                data = answers[q]
                # RFC 6762 6.7: echo ID/question, class IN without cache-flush,
                # TTL <= 10, uncompressed SRV target, unicast from port 5353.
                reply = packet[:2] + struct.pack("!HHHHH", 0x8400, 1, 1, 0, 0)
                reply += packet[12:] + dns_name(q[0])
                reply += struct.pack("!HHIH", q[1], 1, 10, len(data)) + data
                self.sock.sendto(reply, sender)
            except socket.timeout:
                continue
            except Exception as exc:
                self.errors.append(str(exc))
                return

    def __exit__(self, *unused):
        self.done.set()
        self.thread.join(timeout=2)
        self.sock.close()


class Registry:
    def __init__(self):
        self.resources, self.events, self.errors = {}, [], []
        self.heartbeats = 0
        self.lock = threading.Lock()

    def __enter__(self):
        registry = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *unused):
                pass

            def do_POST(self):
                with registry.lock:
                    try:
                        require(self.client_address[0] == "127.0.0.1", "nonlocal HTTP client")
                        length = int(self.headers.get("Content-Length", "0"))
                        require(0 <= length < 1048576, "invalid content length")
                        payload = self.rfile.read(length)
                        if self.path == RESOURCE_PATH:
                            item = json.loads(payload)
                            kind, data = item["type"], item["data"]
                            require(kind in EXPECTED and bool(data["id"]), "invalid resource")
                            # Every referenced parent must already have arrived.
                            refs = {"device": (("node", "node_id"),),
                                    "source": (("device", "device_id"),),
                                    "flow": (("source", "source_id"), ("device", "device_id")),
                                    "sender": (("flow", "flow_id"), ("device", "device_id"))}
                            for parent, field in refs.get(kind, ()):
                                require((parent, data[field]) in registry.resources,
                                        f"{kind} arrived before its {parent}")
                            registry.resources[kind, data["id"]] = data
                            registry.events.append({"path": self.path, "host": self.headers.get("Host"),
                                                    "type": kind, "data": data})
                            code, body = 201, data
                        else:
                            require(self.path.startswith(HEALTH_PATH), "unexpected POST path")
                            require(("node", self.path[len(HEALTH_PATH):]) in registry.resources,
                                    "heartbeat before node registration")
                            registry.heartbeats += 1
                            registry.events.append({"path": self.path})
                            code, body = 200, {"health": str(int(time.time()))}
                    except Exception as exc:
                        registry.errors.append(str(exc))
                        code, body = 400, {"error": str(exc)}
                wire = json.dumps(body).encode()
                self.send_response(code)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(wire)))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(wire)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.port = self.server.server_port
        self.url = f"http://127.0.0.1:{self.port}"
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        return self

    def counts(self):
        with self.lock:
            return dict(Counter(kind for kind, _ in self.resources))

    def __exit__(self, *unused):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def send(port, command):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as control:
        control.sendall((command + "\n").encode())
        data = b""
        while b"\n" not in data:
            more = control.recv(65536)
            if not more:
                break
            data += more
        return data.decode().strip()


def wait_for(predicate, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.1)
    raise AssertionError(f"Timed out after {seconds}s")


def scroll_settings(proc):
    """Reach the bottom card on Windows without moving the user's pointer."""
    if os.name != "nt":
        return
    import ctypes
    from ctypes import wintypes
    user = ctypes.WinDLL("user32", use_last_error=True)
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    windows = []

    @callback
    def visit(handle, unused):
        pid, rect = wintypes.DWORD(), wintypes.RECT()
        user.GetWindowThreadProcessId(handle, ctypes.byref(pid))
        if (pid.value == proc.pid and user.IsWindowVisible(handle)
                and user.GetClientRect(handle, ctypes.byref(rect))):
            windows.append((rect.right * rect.bottom, handle, rect))
        return True

    user.EnumWindows(visit, 0)
    require(bool(windows), "No Deckboy window to scroll")
    _, handle, rect = max(windows, key=lambda item: item[0])
    center = ((rect.bottom // 2) << 16) | (rect.right // 2)
    user.PostMessageW(handle, 0x0200, 0, center)  # WM_MOUSEMOVE
    user.PostMessageW(handle, 0x020A, ((-120 * 30) & 0xffff) << 16, center)
    time.sleep(0.3)


def write_show(path, node_port, typed, remote):
    # Only ST 2110 metadata is armed: the output and media senders are disabled.
    output = [""] * 108
    for index, value in {0: "output_target", 1: "0", 2: "NMOS fixture", 5: "0",
                         10: "0", 12: "0", 14: "window", 15: "-1", 16: "nmos-fixture",
                         17: "1", 36: "1", 37: "127.0.0.1", 38: "127.0.0.1",
                         39: "20000", 40: "1", 99: "0", 100: "0", 101: "0", 102: "0",
                         105: "127.0.0.1", 106: "20002", 107: "127.0.0.1"}.items():
        output[index] = value
    path.write_text(
        f"nmos_enabled\t1\nnmos_registry\t{typed}\nnmos_port\t{node_port}\n"
        f"allow_remote_network\t{int(remote)}\nupdate_check\t0\nui_sounds\t0\n"
        "ui_scale\t1.5\nintegration_ndi_trigger\t0\nintegration_dmx_artnet\t0\n"
        "ndi_tally_trigger\t0\nndi_enabled\t0\nndi_key_enabled\t0\n" +
        "\t".join(output) + "\n", encoding="utf-8")


def run_case(args, label, legacy=False, typed=False, remote=True):
    case = args.out / label
    case.mkdir(parents=True, exist_ok=True)
    (case / "tmp").mkdir(exist_ok=True)
    with ExitStack() as stack:
        discovered = stack.enter_context(Registry())
        manual = stack.enter_context(Registry())
        responder = stack.enter_context(Responder(args.interface, discovered.port, legacy))
        node_port, control_port = free_port(), free_port()
        show = case / "test.deckboy"
        write_show(show, node_port, manual.url if typed else "", remote)
        env = dict(os.environ, DECKBOY_ROOT=str(REPO), DECKBOY_STATE_DIR=str(case),
                   DECKBOY_PROJECT=str(show), DECKBOY_COMPANION_PORT=str(control_port),
                   DECKBOY_HYPERDECK_PORT=str(free_port()), DECKBOY_TEST_NO_ALERTS="1",
                   TEMP=str(case / "tmp"), TMP=str(case / "tmp"))
        log = stack.enter_context((case / "app.log").open("w", encoding="utf-8"))
        startup = None
        if os.name == "nt":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
        proc = subprocess.Popen([str(args.exe), str(show), "--settings", "3.2"],
                                cwd=args.exe.parent, env=env, stdout=log,
                                stderr=subprocess.STDOUT, startupinfo=startup)
        status = ""
        try:
            def ready():
                require(proc.poll() is None, f"Deckboy exited: see {case / 'app.log'}")
                try:
                    return send(control_port, "NMOS STATUS").startswith("OK NMOS")
                except OSError:
                    return False
            wait_for(ready)
            target = manual if typed else discovered
            if typed or remote:
                wait_for(lambda: target.counts() == EXPECTED and target.heartbeats >= 2)
                status = send(control_port, "NMOS STATUS")
                expected_url = manual.url if typed else f"http://{responder.host}:{discovered.port}"
                require("NMOS REGISTERED" in status and f"registry={expected_url}" in status,
                        f"incorrect selected registry: {status}")
                require(f"registry_source={'typed' if typed else 'mdns'}" in status,
                        f"incorrect registry origin: {status}")
                # The Node API must contain the same registered resources.
                for kind, count in EXPECTED.items():
                    api = http.client.HTTPConnection("127.0.0.1", node_port, timeout=2)
                    try:
                        api.request("GET", "/x-nmos/node/v1.3/" + ("self" if kind == "node" else kind + "s"))
                        reply = api.getresponse()
                        require(reply.status == 200, f"Node API failed for {kind}")
                        payload = json.loads(reply.read())
                        items = [payload] if kind == "node" else payload
                        require(len(items) == count, f"Node API {kind} count differs")
                        require(all((kind, item["id"]) in target.resources for item in items),
                                f"Node API {kind} differs from registered resources")
                    finally:
                        api.close()
                require(not (discovered if typed else manual).events, "wrong registry received HTTP")
                if not typed:
                    asked = {(q["name"], q["type"]) for q in responder.queries}
                    require(all((service, 12) in asked for service in SERVICES), "both service names must be queried")
                    require((responder.instance, 33) in asked and (responder.instance, 16) in asked
                            and (responder.host, 1) in asked, "SRV/TXT/A follow-ups missing")
                    require(all(event.get("host", f"{responder.host}:{discovered.port}") ==
                                f"{responder.host}:{discovered.port}" for event in target.events),
                            "HTTP Host must use the SRV name while connecting to its A address")
            else:
                # Longer than a complete 2.5-second browse plus initial retry.
                time.sleep(8)
                status = send(control_port, "NMOS STATUS")
                require("NMOS: discovery needs the remote network allowed" in status, status)
                require("registry=-" in status and "registry_source=none" in status, status)
                require(not discovered.events and not manual.events, "local-only blank URL registered")
            if args.screenshots:
                for theme in ("dark", "virtual-boy"):
                    require(send(control_port, "SET theme " + theme).startswith("OK SET"),
                            "could not change theme")
                    # Let the status toast expire so it cannot cover the panel.
                    time.sleep(4)
                    scroll_settings(proc)
                    shot = case / (theme + ".bmp")
                    if shot.exists():
                        shot.unlink()
                    require(send(control_port, "UISNAP " + str(shot)).startswith("OK UISNAP"),
                            "could not request Settings capture")
                    wait_for(lambda: shot.exists() and shot.stat().st_size > 54, 5)
            if typed or not remote:
                require(not responder.queries, "discovery queried despite typed URL or local-only setting")
            require(not responder.errors and not discovered.errors and not manual.errors,
                    str(responder.errors + discovered.errors + manual.errors))
            print(f"[ok] {label}: queries={len(responder.queries)}, "
                  f"resources={target.counts()}, heartbeats={target.heartbeats}\n  {status}", flush=True)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
            (case / "evidence.json").write_text(json.dumps({
                "status": status, "queries": responder.queries,
                "discovered_registry": discovered.events, "typed_registry": manual.events,
                "errors": responder.errors + discovered.errors + manual.errors,
            }, indent=2), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, default=REPO / "build/codex/nmos-live")
    parser.add_argument("--interface", action="append", default=[])
    parser.add_argument("--screenshots", action="store_true",
                        help="also capture Settings in dark and Virtual Boy at 1.5x")
    args = parser.parse_args()
    args.exe, args.out = args.exe.resolve(), args.out.resolve()
    require(args.exe.is_file(), "Build Deckboy first")
    if not args.interface:
        args.interface = sorted({entry[4][0] for entry in
                                 socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)})
    print("Local mDNS interfaces: " + ", ".join(args.interface), flush=True)
    run_case(args, "current-service")
    run_case(args, "legacy-service", legacy=True)
    run_case(args, "typed-remote-on", typed=True)
    run_case(args, "typed-remote-off", typed=True, remote=False)
    run_case(args, "blank-remote-off", remote=False)
    print(f"All five live cases passed. Evidence: {args.out}", flush=True)


if __name__ == "__main__":
    main()
