// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ---------------------------------------------------------------------------
// nmos_node.hpp — AMWA NMOS IS-04 (discovery/registration) + IS-05 (connection
// management) for Deckboy's ST 2110 senders.
//
// WHY THIS EXISTS
// ---------------
// An ST 2110 stream is undiscoverable on its own. Before this, the only way a
// receiver learned about a Deckboy flow was an operator copying an SDP out of
// the settings modal by hand. Facilities do not work that way: a node registers
// itself with a Registration & Discovery System (an "RDS"), and receivers are
// connected to it through IS-05 by a broadcast controller. This file is the
// difference between "emits valid packets" and "shows up in the plant".
//
// WHAT IS IMPLEMENTED
// -------------------
//   * IS-04 v1.3 Node API, served over HTTP: node, devices, sources, flows,
//     senders. Receivers are advertised as an empty list — Deckboy is a source
//     device, it does not accept 2110 in.
//   * IS-04 registration: POSTs the resource tree to a configured registry in
//     dependency order, then heartbeats. A registry that forgets us (404 on
//     heartbeat, e.g. it restarted) triggers automatic re-registration.
//   * IS-05 v1.1 Connection API: constraints / staged / active / transportfile
//     for every sender, plus bulk staging. The transportfile is the very same
//     SDP the settings modal shows, so there is one source of truth.
//   * IS-05 PATCH of `master_enable` and `transport_params` with
//     `activate_immediate`, delivered to the app through a callback that
//     genuinely reconfigures the sender. This is deliberately NOT a stub that
//     accepts and discards: a controller that thinks it moved a multicast group
//     and did not is worse than one that gets an honest error.
//
// WHAT IS NOT IMPLEMENTED — do not claim it
// -----------------------------------------
//   * NO DNS-SD / mDNS. The registry is configured by URL. Real plants usually
//     advertise the RDS over unicast DNS-SD or mDNS (`_nmos-register._tcp`);
//     without it Deckboy cannot FIND a registry on its own, and cannot be found
//     in peer-to-peer mode. Manual registry URL is a legitimate and common
//     deployment, but it is a smaller claim.
//   * NO scheduled activation. `activate_scheduled_absolute` and
//     `activate_scheduled_relative` are rejected with 501, because honouring
//     them needs the PTP clock to gate the switch and pretending otherwise
//     would silently take a source at the wrong moment.
//   * NO IS-05 receivers, no IS-07, no IS-08, no authorisation (IS-10). HTTP
//     only, no HTTPS.
//   * The underlying 2110 sender's own caveats still stand (see
//     st2110_output.hpp): not narrow-model paced.
//
// THREADING
// ---------
// Two owned threads: an HTTP server thread (accept + serve, one client at a
// time, short deadlines) and a registration thread (register, then heartbeat on
// an interval). Both are joined by stop(). The resource snapshot is guarded by
// `mutex_`; the app pushes updates with setSenders() from the main thread.
// ---------------------------------------------------------------------------
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace deckboy {
namespace platform {
namespace video {

// Which essence a sender carries. Drives the flow/source JSON shape: video
// flows carry raster geometry and components, audio flows carry sample rate,
// bit depth and a channel list.
enum class NmosFormat {
  Video,
  Audio,
};

// One publishable sender. The app builds these from its OutputTargets; this
// file never sees an OutputTarget so the two stay decoupled.
//
// `key` is the stable identity seed. IDs are UUIDv5-derived from it, so the
// same output keeps the same NMOS id across restarts WITHOUT persisting
// anything — a controller's saved routes survive an app restart. Change the key
// and you have created a new sender as far as the plant is concerned.
struct NmosSenderInfo {
  std::string key;              // stable seed, e.g. "output-0-video"
  std::string label;            // human name shown in controllers
  std::string description;
  NmosFormat format = NmosFormat::Video;

  // Transport (RTP multicast).
  std::string destinationAddress = "239.20.10.1";
  int destinationPort = 20000;
  std::string sourceAddress;    // local NIC address ("" = default route)
  int sourcePort = 0;           // 0 = ephemeral / auto
  bool active = false;          // is the sender currently emitting?

  // The SDP served as the IS-05 transportfile. Supplied by the app so it is
  // byte-identical to what the settings modal shows.
  std::string sdp;

  // Video essence parameters (ignored when format == Audio).
  int width = 1920;
  int height = 1080;
  double frameRate = 60000.0 / 1001.0;
  bool interlaced = false;
  int bitDepth = 10;

  // Audio essence parameters (ignored when format == Video).
  int channels = 2;
  int sampleRate = 48000;
};

// A staged change arriving over IS-05. Only the fields the controller actually
// touched are flagged, so the app can apply a master_enable flip without
// tearing down a socket that did not move.
struct NmosSenderPatch {
  std::string senderKey;             // the NmosSenderInfo::key this targets
  bool masterEnableChanged = false;
  bool masterEnable = false;
  bool destinationChanged = false;
  std::string destinationAddress;
  int destinationPort = 0;
  bool activateImmediate = false;    // false = staged only, do not apply yet
};

// Installed by the app. Return true only if the change was genuinely applied;
// returning false makes the Connection API answer 500 rather than lie to the
// controller. Called on the HTTP SERVER THREAD — the app implementation must be
// thread-safe (Deckboy's queues it for the main thread and waits).
using NmosPatchHandler = std::function<bool(const NmosSenderPatch&)>;

// IS-05 keeps TWO states per sender: `staged`, a scratch area a controller
// writes and reads back at will, and `active`, which only changes when an
// activation is requested. Collapsing them (applying a PATCH straight to the
// live sender, and answering GET /staged from the live config) makes a
// controller's stage-then-activate workflow silently impossible — it stages a
// change, reads it back, and sees its edit gone.
//
// Every field is paired with a `*Set` flag so a PATCH that touches only
// master_enable does not blank the destination a previous PATCH staged.
struct NmosStagedState {
  bool masterEnable = false;
  bool masterEnableSet = false;
  std::string destinationAddress;
  bool destinationAddressSet = false;
  int destinationPort = 0;
  bool destinationPortSet = false;
  int sourcePort = 0;
  bool sourcePortSet = false;
  std::string receiverId;          // empty + receiverIdSet = explicit null
  bool receiverIdSet = false;
  bool rtpEnabled = true;
  bool rtpEnabledSet = false;
};

struct NmosConfig {
  bool enabled = false;
  // Registry base URL, e.g. "http://registry.local:8010". Empty = serve the Node
  // API and IS-05 locally but do not register anywhere (useful for testing, and
  // the closest thing to peer-to-peer mode we can offer without mDNS).
  std::string registryUrl;
  int nodePort = 3210;            // port the Node + Connection API listen on
  std::string hostAddress;        // IP advertised to the registry ("" = auto-detect)
  std::string label = "Deckboy";
  std::string interfaceName = "eth0";
  bool allowRemote = true;        // bind 0.0.0.0 rather than loopback
  // PTP state, mirrored into the node's clock resource. Advertising a traceable
  // clock we are not locked to is exactly the lie that makes a stream fail to
  // genlock, so this comes from the real PtpClient.
  bool ptpLocked = false;
  std::string ptpGrandmaster;
};

class NmosNode {
 public:
  NmosNode();
  ~NmosNode();

  NmosNode(const NmosNode&) = delete;
  NmosNode& operator=(const NmosNode&) = delete;

  bool start(const NmosConfig& config);
  void stop();
  bool running() const { return running_.load(std::memory_order_relaxed); }

  // Replace the published sender set. Safe to call every frame; it early-outs
  // when nothing changed, and only bumps resource versions (forcing a re-POST)
  // when something a controller can observe actually moved.
  void setSenders(const std::vector<NmosSenderInfo>& senders);

  void setPatchHandler(NmosPatchHandler handler);

  // Mirror live PTP state into the advertised clock.
  void setPtpState(bool locked, const std::string& grandmaster);

  // ── Status, for the settings UI ────────────────────────────────────────────
  bool registered() const { return registered_.load(std::memory_order_relaxed); }
  bool httpReady() const { return httpReady_.load(std::memory_order_relaxed); }
  int senderCount() const { return senderCount_.load(std::memory_order_relaxed); }
  std::uint64_t heartbeatCount() const { return heartbeats_.load(std::memory_order_relaxed); }
  std::string nodeId() const;
  std::string lastError() const;

  // The Node API base URL an operator can paste into a browser to see what the
  // plant sees. Empty when not running.
  std::string nodeApiUrl() const;

 private:
  struct Resource {
    std::string id;
    std::string json;
    const char* type;  // "node" | "device" | "source" | "flow" | "sender"
  };

  void httpLoop();
  void registrationLoop();
  void serveClient(std::uintptr_t client);

  // Route one request. Returns the full response body and fills status/type.
  std::string routeRequest(const std::string& method, const std::string& path,
                           const std::string& body, std::string& statusOut,
                           std::string& contentTypeOut);
  std::string handleConnectionApi(const std::string& method, const std::string& path,
                                  const std::string& body, std::string& statusOut,
                                  std::string& contentTypeOut);
  // Apply one IS-05 PATCH body to a sender. Shared by single/ and bulk/ so the
  // two can never drift in what they accept. `errorOut` carries a reason when
  // it returns a non-2xx status.
  int applyStagedPatch(const NmosSenderInfo& target, const std::string& body,
                       bool& activatedOut, std::string& errorOut);
  // Render the IS-05 staged/active object for a sender.
  std::string renderConnectionState(const NmosSenderInfo& target, bool activeEndpoint,
                                    const std::string& host);

  void rebuildResources();               // caller holds mutex_
  bool postResource(const Resource& resource);
  bool sendHeartbeat();
  bool registerAll();

  void setLastError(const std::string& message);

  NmosConfig config_;
  mutable std::mutex mutex_;
  std::vector<NmosSenderInfo> senders_;
  std::map<std::string, NmosStagedState> staged_;  // by NmosSenderInfo::key
  // Last activation mode per sender key. /active has to report the activation
  // that put it in its current state, not a settled null — a controller uses
  // that to confirm its request actually landed.
  std::map<std::string, std::string> lastActivation_;
  std::vector<Resource> resources_;      // in registration dependency order
  std::string nodeId_;
  std::string deviceId_;
  std::string versionStamp_;             // "<sec>:<nsec>", bumped on change
  std::string resolvedHost_;

  NmosPatchHandler patchHandler_;
  std::mutex handlerMutex_;

  std::thread httpThread_;
  std::thread registrationThread_;
  std::atomic<bool> stop_ {false};
  std::atomic<bool> running_ {false};
  std::atomic<bool> registered_ {false};
  std::atomic<bool> httpReady_ {false};
  std::atomic<bool> resourcesDirty_ {false};
  std::atomic<int> senderCount_ {0};
  std::atomic<std::uint64_t> heartbeats_ {0};

  std::uintptr_t listenSocket_ = 0;
  bool listenValid_ = false;
  bool winsockStarted_ = false;

  std::condition_variable wake_;
  std::mutex wakeMutex_;

  mutable std::mutex errorMutex_;
  std::string lastError_;
};

// ── Exposed for unit testing ────────────────────────────────────────────────
// UUIDv5 (SHA-1, RFC 4122 §4.3) over a fixed Deckboy namespace. Deterministic:
// the same seed always yields the same id, which is what lets a controller's
// saved route survive an app restart with nothing written to disk.
std::string nmosDeterministicUuid(const std::string& seed);

// Parse "http://host:port/base" into its parts. Returns false on anything it
// does not understand rather than guessing at a default host.
bool nmosParseUrl(const std::string& url, std::string& hostOut, int& portOut,
                  std::string& pathOut);

}  // namespace video
}  // namespace platform
}  // namespace deckboy
