// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#include "platform/nmc_sync.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "core/utils.hpp"

using deckboy::core::utils::toUpper;
using deckboy::core::utils::trim;

namespace deckboy::platform {

namespace {

// Sender matching for input mode. "LOCALHOST" is spelled out because an
// operator typing it means the loopback address, not a host whose name happens
// to contain the word.
bool senderMatchesFilter(const std::string& sender, const std::string& filter) {
  if (filter.empty()) {
    return true;
  }
  const std::string senderUpper = toUpper(sender);
  const std::string filterUpper = toUpper(filter);
  if (filterUpper == "LOCALHOST") {
    return sender == "127.0.0.1";
  }
  return senderUpper == filterUpper ||
         senderUpper.find(filterUpper) != std::string::npos;
}

bool resolveTargetAddress(const NmcSyncConfig& config, sockaddr_in* outAddress,
                          std::string* error) {
  if (!outAddress) {
    return false;
  }
  std::memset(outAddress, 0, sizeof(sockaddr_in));
  outAddress->sin_family = AF_INET;
  outAddress->sin_port = htons(static_cast<std::uint16_t>(config.port));

  if (config.targetHost.empty()) {
    if (error) *error = "nmc host missing";
    return false;
  }

  addrinfo hints {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* results = nullptr;
  const int status = getaddrinfo(config.targetHost.c_str(), nullptr, &hints, &results);
  if (status != 0 || !results) {
    if (error) *error = "nmc host resolve failed";
    if (results) freeaddrinfo(results);
    return false;
  }
  const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(results->ai_addr);
  outAddress->sin_addr = ipv4->sin_addr;
  freeaddrinfo(results);
  return true;
}

std::string formatPacket(const std::string& command, std::optional<double> seconds) {
  std::ostringstream output;
  output << "DECKBOY_NMC1 " << toUpper(trim(command));
  if (seconds) {
    output << ' ' << std::fixed << std::setprecision(6) << std::max(0.0, *seconds);
  }
  return output.str();
}

}  // namespace

NmcSync::~NmcSync() { stop(); }

void NmcSync::resetOutputState() {
  lastSentState_ = TransportState::Stopped;
  lastSentSeconds_ = -1.0;
  lastLocateSentMs_ = 0;
  outputStateInitialized_ = false;
}

void NmcSync::announceErrorOnce() {
  if (lastError_.empty() || lastError_ == lastAnnouncedError_) {
    return;
  }
  if (hooks_.announce) {
    hooks_.announce("nmc sync: " + lastError_);
  }
  lastAnnouncedError_ = lastError_;
}

void NmcSync::listenLoop() {
  running_.store(true);
  const std::string sourceFilter = active_.sourceFilter;
  while (!stopFlag_.load()) {
    fd_set readFds;
    FD_ZERO(&readFds);
    watchFd(socket_, &readFds);
    timeval timeout {};
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000;
    const int ready = select(selectNfds(socket_), &readFds, nullptr, nullptr, &timeout);
    if (ready <= 0 || !readyFd(socket_, &readFds)) {
      continue;
    }
    std::array<char, 1024> buffer {};
    sockaddr_in sender {};
    socklen_t senderLen = sizeof(sender);
    const int bytes = recvfrom(socket_, buffer.data(),
                               static_cast<int>(buffer.size() - 1), 0,
                               reinterpret_cast<sockaddr*>(&sender), &senderLen);
    if (bytes <= 0) {
      continue;
    }
    buffer[bytes] = '\0';
    if (!senderMatchesFilter(socketAddressToString(sender), sourceFilter)) {
      continue;
    }
    const std::string payload =
      trim(std::string(buffer.data(), static_cast<size_t>(bytes)));
    if (payload.empty()) {
      continue;
    }
    if (hooks_.enqueueCommand) {
      hooks_.enqueueCommand("NMCEVENT " + payload);
    }
  }
  running_.store(false);
}

bool NmcSync::start(const NmcSyncConfig& config) {
  stop();

  active_ = config;
  lastError_.clear();
  lastAnnouncedError_.clear();
  resetOutputState();

  if (config.mode == "output") {
    std::string error;
    if (!resolveTargetAddress(config, &targetAddress_, &error)) {
      lastError_ = error.empty() ? "nmc target resolve failed" : error;
      return false;
    }
    socket_ = createDatagramSocket(true);
    if (socket_ == kInvalidSocket) {
      lastError_ = "nmc output socket unavailable";
      return false;
    }
    return true;
  }

  socket_ = createBoundSocket(SOCK_DGRAM, config.port, false, config.localOnly);
  if (socket_ == kInvalidSocket) {
    lastError_ = "nmc listen socket unavailable";
    return false;
  }
  stopFlag_.store(false);
  running_.store(false);
  thread_ = std::thread([this]() { listenLoop(); });
  return true;
}

void NmcSync::stop() {
  stopFlag_.store(true);
  if (socket_ != kInvalidSocket) {
    closeSocket(socket_);
    socket_ = kInvalidSocket;
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  running_.store(false);
  active_ = NmcSyncConfig{};
  resetOutputState();
}

void NmcSync::refresh(const NmcSyncConfig& config) {
  if (!config.enabled) {
    stop();
    lastError_.clear();
    lastAnnouncedError_.clear();
    restartBlockedUntilMs_ = 0;
    return;
  }

  // Only the fields that decide the BINDING count as a change. Re-reading the
  // same settings must not tear a working socket down.
  NmcSyncConfig desired = config;
  if (desired.mode != "output") desired.targetHost.clear();
  if (desired.mode != "input") desired.sourceFilter.clear();
  if (!desired.sameBindingAs(active_)) {
    stop();
  }

  if (thread_.joinable()) {
    if (running_.load()) {
      return;
    }
    thread_.join();
  }

  if (desired.mode == "output" && socket_ != kInvalidSocket) {
    return;
  }

  const Uint64 now = SDL_GetTicks();
  if (restartBlockedUntilMs_ > now) {
    announceErrorOnce();
    return;
  }

  if (!start(desired)) {
    restartBlockedUntilMs_ = now + 3000;
    announceErrorOnce();
    return;
  }
  restartBlockedUntilMs_ = 0;
}

bool NmcSync::sendPacket(const std::string& command, std::optional<double> seconds) {
  if (socket_ == kInvalidSocket || active_.mode != "output") {
    return false;
  }
  const std::string payload = formatPacket(command, seconds);
  const int sent = sendto(socket_, payload.c_str(),
                          static_cast<int>(payload.size()), kSocketSendFlags,
                          reinterpret_cast<const sockaddr*>(&targetAddress_),
                          static_cast<socklen_t>(sizeof(targetAddress_)));
  if (sent < 0) {
    // Drop the socket and back off rather than spinning on a dead route --
    // refresh() will rebuild it once the block expires.
    lastError_ = "nmc send failed";
    restartBlockedUntilMs_ = SDL_GetTicks() + 3000;
    closeSocket(socket_);
    socket_ = kInvalidSocket;
    return false;
  }
  lastError_.clear();
  return true;
}

void NmcSync::tickOutput(const NmcSyncConfig& config, TransportState state,
                         double seconds, bool haveCue) {
  if (!config.enabled || active_.mode != "output" || socket_ == kInvalidSocket) {
    return;
  }
  const Uint64 now = SDL_GetTicks();
  const double position = haveCue ? std::max(0.0, seconds) : 0.0;

  const bool stateChanged = !outputStateInitialized_ || state != lastSentState_;
  const bool positionJumped =
    outputStateInitialized_ && std::fabs(position - lastSentSeconds_) >= 0.75;
  const bool shouldSendLocate = haveCue &&
    (state == TransportState::Playing
       ? (!outputStateInitialized_ ||
          now >= lastLocateSentMs_ + static_cast<Uint64>(config.locateIntervalMs))
       : positionJumped);

  if (stateChanged) {
    switch (state) {
      case TransportState::Playing: sendPacket("PLAY", position); break;
      case TransportState::Paused:  sendPacket("PAUSE", position); break;
      case TransportState::Stopped: sendPacket("STOP", position); break;
    }
    lastLocateSentMs_ = now;
  } else if (shouldSendLocate) {
    sendPacket("LOCATE", position);
    lastLocateSentMs_ = now;
  }

  lastSentState_ = state;
  lastSentSeconds_ = position;
  outputStateInitialized_ = true;
}

std::string NmcSync::describe(const NmcSyncConfig& config) const {
  if (!config.enabled) {
    return "off";
  }
  std::ostringstream summary;
  summary << config.mode << '@' << config.port;
  if (config.mode == "output") {
    summary << " -> " << config.targetHost;
  } else if (!config.sourceFilter.empty()) {
    summary << " from " << config.sourceFilter;
  }
  return summary.str();
}

}  // namespace deckboy::platform
