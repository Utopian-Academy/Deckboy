// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// nmc_sync.hpp — the Network Master Clock bridge.
//
// One Deckboy broadcasts its transport over UDP ("output") and others chase it
// ("input"). This owns the socket, the listener thread and the state that
// decides when a packet is worth sending; it does not own the settings and it
// cannot reach the app.
//
// EXTRACTED FROM THE App CLASS, and the reason is worth stating: App is one
// class of some seventy thousand lines, and this subsystem was sixteen members
// and fifteen functions of it that nothing else touched. The only thing it
// shared was `project_`, which every file shares because it is the document.
// So the settings arrive as a plain struct and the two things it needs the app
// to do arrive as callbacks -- which also means the bridge can be reasoned
// about, and its socket lifetime checked, without reading the app at all.
// ============================================================================

#ifndef DECKBOY_PLATFORM_NMC_SYNC_HPP
#define DECKBOY_PLATFORM_NMC_SYNC_HPP

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include "core/sdl_compat.hpp"
#include "core/types.hpp"
#include "platform/network.hpp"

namespace deckboy::platform {

// Resolved settings. The caller reads the project and the environment; this
// class reads neither, so the same bridge can be driven by a test.
struct NmcSyncConfig {
  bool enabled = false;
  std::string mode;              // "input" (chase) or "output" (broadcast)
  int port = 0;
  std::string targetHost;        // output mode
  std::string sourceFilter;      // input mode; empty accepts any sender
  int locateIntervalMs = 250;
  bool localOnly = true;         // refuse off-box senders

  bool sameBindingAs(const NmcSyncConfig& other) const {
    return mode == other.mode && port == other.port &&
           targetHost == other.targetHost && sourceFilter == other.sourceFilter;
  }
};

// The two things the bridge needs the app to do for it.
struct NmcSyncHooks {
  // An inbound packet, already filtered by sender, as a remote command line.
  // Called ON THE LISTENER THREAD, so it must be a queue rather than an edit.
  std::function<void(const std::string&)> enqueueCommand;
  // An error worth telling the operator about. Called on the main thread, and
  // only when the message CHANGES -- a bridge that cannot bind would otherwise
  // toast every tick.
  std::function<void(const std::string&)> announce;
};

class NmcSync {
 public:
  NmcSync() = default;
  ~NmcSync();
  NmcSync(const NmcSync&) = delete;
  NmcSync& operator=(const NmcSync&) = delete;

  void setHooks(NmcSyncHooks hooks) { hooks_ = std::move(hooks); }

  // Start, stop or restart to match `config`. Safe to call every tick; it does
  // nothing when the settings and the socket already agree.
  void refresh(const NmcSyncConfig& config);

  // Output mode: broadcast the transport when it has moved enough to matter.
  // `haveCue` is false when nothing is racked, which suppresses the periodic
  // locate without suppressing a state change.
  void tickOutput(const NmcSyncConfig& config, TransportState state,
                  double seconds, bool haveCue);

  void stop();

  // One line for the status readouts: "off", or "input@51010 from 10.0.0.4".
  std::string describe(const NmcSyncConfig& config) const;

 private:
  bool start(const NmcSyncConfig& config);
  void listenLoop();
  bool sendPacket(const std::string& command,
                  std::optional<double> seconds = std::nullopt);
  void resetOutputState();
  void announceErrorOnce();

  NmcSyncHooks hooks_;

  std::thread thread_;
  std::atomic<bool> stopFlag_ {false};
  std::atomic<bool> running_ {false};
  SocketHandle socket_ = kInvalidSocket;
  sockaddr_in targetAddress_ {};

  // What the running bridge was started with, so refresh() can tell a settings
  // change from a settings re-read.
  NmcSyncConfig active_;

  std::string lastError_;
  std::string lastAnnouncedError_;
  Uint64 restartBlockedUntilMs_ = 0;

  // Output-side change detection.
  TransportState lastSentState_ = TransportState::Stopped;
  double lastSentSeconds_ = -1.0;
  Uint64 lastLocateSentMs_ = 0;
  bool outputStateInitialized_ = false;
};

}  // namespace deckboy::platform

#endif  // DECKBOY_PLATFORM_NMC_SYNC_HPP
