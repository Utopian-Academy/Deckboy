# NMOS mDNS registry discovery

Worktree: `deckboy-codex-nmos`, branch `codex/nmos-mdns`. Date: 2026-10-09.

## Changes

- Typed registry URLs always register, including with the remote network off.
  The app passes the typed URL through unchanged; `allowRemote` still chooses
  the listener's bind address. Typed registry heartbeat/retry behavior is kept.
- Only a blank registry URL with remote networking allowed enables discovery.
  Otherwise the status says `NMOS: discovery needs the remote network allowed`.
- IPv4 mDNS browses both `_nmos-register._tcp.local` and the older
  `_nmos-registration._tcp.local`, resolves PTR/SRV/TXT/A, and selects HTTP
  v1.3 registries advertising `api_auth=false`. TXT priority orders candidates;
  equal priorities are randomized. Discovery never overwrites the saved URL.
- Settings displays `typed: URL` or `found: host:port (mDNS)`. `NMOS STATUS`
  returns the effective `registry=` and `registry_source=typed|mdns|searching|none`
  over the actual remote-control connection, as well as printing to stdout.
- Retained bounded, cancellable nonblocking HTTP I/O: a discovered unreachable
  endpoint must not trap failover or shutdown. Partial writes, interrupted and
  would-block I/O, Windows `ioctlsocket`, POSIX `fcntl`, Linux `MSG_NOSIGNAL`,
  and macOS `SO_NOSIGPIPE` are handled. The listener closes after its thread
  joins, avoiding a descriptor close/reuse race during settings changes.
- Updated `MANUAL.md` and regenerated `docs/manual.html`. Reverted the earlier
  `CLAUDE.md` addition. No version change. The README roadmap entry remains
  pending native macOS and real-registry validation.

## Live test

Run from this worktree (Windows uses `py` when `python` is not on PATH):

```text
python tools/check_nmos_mdns_live.py --exe build/codex/Release/Deckboy.exe --screenshots
```

The standard-library fixture launches the actual Release Deckboy with an
isolated show and state directory. It answers ordinary LAN mDNS questions
from this machine only. SRV/TXT advertise `api_proto=http`, `api_ver=v1.3`,
`api_auth=false`, and `pri=10`; the A record is `127.0.0.1`. Two distinct HTTP
registries listen only on loopback, so a typed URL can demonstrably beat the
advertised registry. Outputs, video/audio RTP enable flags, update checking,
NDI and Art-Net integrations are off. The only intended off-machine traffic
is standard link-local mDNS browsing; HTTP and control traffic stay local.

| Case | Observed browse queries | Registered resources | Heartbeats |
| --- | ---: | --- | --- |
| Current service name, blank URL, remote on | 45 | 1 node, 1 device, 2 sources, 2 flows, 2 senders | At least 2 |
| Legacy service name, blank URL, remote on | 45 | Same eight resources | At least 2 |
| Typed URL, remote on | 0 | Same eight, typed registry only | At least 2 |
| Typed URL, remote off | 0 | None (LOCAL ONLY holds registration back; see below) | 0 |
| Blank URL, remote off | 0 | None | 0 |

The fixture checks parent resources arrive before their dependents, matches
registered IDs to all five Node API endpoints, checks the HTTP Host header,
and asserts the registry URL and origin in the remote status reply. The
local-only case observes at least eight seconds after startup and verifies
the explicit discovery status. JSON evidence, application logs and Settings
captures from the resumed run are under `build/codex/nmos-live-resumed/`;
the console log is `build/codex/nmos-live-resumed.log`. The earlier evidence
remains under `build/codex/nmos-live/`.

Run mDNS fixtures serially. An initial overlapping run observed the other
test's loopback advertisements; it was discarded and rerun in isolation.
The live responder now excludes loopback browse clients, which production
LAN discovery also excludes.

## Build and other checks

- Configured and built the requested Visual Studio 2022 x64 Release tree at
  `build/codex` using the supplied vcpkg toolchain. Built
  `build/codex-disabled` with both `ENABLE_ASIO=OFF` and
  `DECKBOY_INPROC_DECODE=OFF`.
- `tools/audit_warnings.py` passed for both builds. Existing warnings in
  byte-preserved `native/extras/upstream/terrarium_pixelview.hpp` are exempt
  under that audit; no warning it reports remains in the changed code.
- Windows CTest: 4/4 passed (broadcast, DNS packet/parser checks, both mDNS
  service-name loopback exchanges, HTTP registration). The HTTP check covers
  heartbeat 404 re-registration, typed registration and heartbeats with remote
  networking off, and cancellation of an HTTP peer that accepts but stalls.
- Linux/WSL: compiled `nmos_mdns.cpp`, `nmos_node.cpp` and the regression tool
  with GCC C++17, `-Wall -Wextra -Wshadow -Werror`, pthreads and ASan/UBSan.
  Parser tests (including 10,000 mutated packets), both mDNS loopback cases
  and the HTTP registration cases passed. Logs: `build/codex/linux-*.log`.
- Manual generation, site links (906 local links/anchors), action audit
  (zero missing handlers/callers or duplicate IDs), and encoding audit
  (zero double-encoded strings) passed. Scaled-layout strict audit passed:
  90 existing findings against the baseline of 103; NMOS gaps use `kCtlGap`.
- `Deckboy --contrast-check build/codex/contrast` completed for all 30 bundled
  themes with Settings open at 1.5x. Notice checks passed. Opened the dark and
  Virtual Boy screenshots and inspected the changed NMOS panel. This is not
  a claim that every control on every screen has been visually inspected.

## Resumed verification

The saved implementation was reviewed and rebuilt without restarting the work.
Both Release configurations passed again, with warning audits against the
incremental logs and the earlier compilation logs. Windows CTest passed 4/4;
all five live application cases above passed again, including three observed
heartbeats in each registering case and zero queries in both typed cases and
the blank/local-only case. The initial sandboxed multicast test received no
replies; it passed with network access enabled. No production change was needed.

Linux GCC rebuilt the NMOS sources and test tool with the warning and sanitizer
flags listed above. All three test modes passed again; logs are
`build/codex/linux-*-resumed.log`. The manual generator, site links, action,
scaled-layout and encoding audits also passed again. The resumed contrast run
produced 60 captures under `build/codex/contrast-resumed/`; dark and Virtual Boy
were opened and inspected, as were live Settings captures showing both found
and typed registry origins. The resumed contrast captures use the smaller
window's fitted scale; the 1.5x live discovery captures and original 1.5x
contrast captures remain available.

## Still unverified

- A real vendor's NMOS registry/controller and a real ST 2110 plant. The local
  fixture proves discovery and registration/heartbeat exchanges, not vendor
  interoperability, media transport, facility timing, or AMWA conformance.
- Native macOS compilation/runtime and a full Linux application build were
  unavailable here. Reviewed the platform guards; macOS shares the compiled
  POSIX discovery/I/O path with its guarded `SO_NOSIGPIPE` option, but no Apple
  SDK or macOS runner was available to verify that branch by compilation.
- Multi-registry failover on a real LAN, long-duration operation, and IPv6,
  unicast DNS-SD, HTTPS/authorization or peer-to-peer Node advertisements.
  The latter protocols remain outside the implementation's supported scope.

No push or release was made.

## Correction (Claude, 2026-10-10)

The brief's premise that typed registries always registered under LOCAL ONLY was
wrong: since v0.83.0 the app withheld registration there (`nmosLocalOnlyBlocked_`),
because the listener binds loopback while the advertised href is the LAN address.
The guard, its status line and toast are restored; the live check's
typed-remote-off case now requires no registration and no queries. All five live
cases pass again (evidence: `build/claude-review/nmos-live/`).
