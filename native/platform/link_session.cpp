#include "link_session.hpp"

#ifdef DECKBOY_HAS_LINK
#include <ableton/Link.hpp>

#include <algorithm>
#include <chrono>

namespace deckboy {

struct LinkSession::Impl {
  explicit Impl(double bpm) : link(bpm) {}
  ableton::Link link;
};

LinkSession::LinkSession() = default;
LinkSession::~LinkSession() = default;

bool LinkSession::available() { return true; }

void LinkSession::setEnabled(bool on, double bpm) {
  if (!impl_) {
    if (!on) {
      return;
    }
    impl_ = std::make_unique<Impl>(std::clamp(bpm, 20.0, 300.0));
  } else if (on && !impl_->link.isEnabled()) {
    // Set while still alone, before the network threads run.
    auto state = impl_->link.captureAppSessionState();
    state.setTempo(std::clamp(bpm, 20.0, 300.0), impl_->link.clock().micros());
    impl_->link.commitAppSessionState(state);
  }
  impl_->link.enable(on);
}

bool LinkSession::enabled() const { return impl_ && impl_->link.isEnabled(); }

std::size_t LinkSession::peers() const {
  return enabled() ? impl_->link.numPeers() : 0;
}

double LinkSession::tempo() const {
  return impl_ ? impl_->link.captureAppSessionState().tempo() : 0.0;
}

void LinkSession::proposeTempo(double bpm) {
  if (!enabled()) {
    return;
  }
  auto state = impl_->link.captureAppSessionState();
  state.setTempo(std::clamp(bpm, 20.0, 300.0), impl_->link.clock().micros());
  impl_->link.commitAppSessionState(state);
}

bool LinkSession::beatsNow(double quantum, double& beatsOut) const {
  if (!enabled()) {
    return false;
  }
  const auto state = impl_->link.captureAppSessionState();
  beatsOut = state.beatAtTime(impl_->link.clock().micros(), quantum);
  return true;
}

}  // namespace deckboy

#else  // no Link in this build

namespace deckboy {

struct LinkSession::Impl {};
LinkSession::LinkSession() = default;
LinkSession::~LinkSession() = default;
bool LinkSession::available() { return false; }
void LinkSession::setEnabled(bool, double) {}
bool LinkSession::enabled() const { return false; }
std::size_t LinkSession::peers() const { return 0; }
double LinkSession::tempo() const { return 0.0; }
void LinkSession::proposeTempo(double) {}
bool LinkSession::beatsNow(double, double&) const { return false; }

}  // namespace deckboy

#endif
