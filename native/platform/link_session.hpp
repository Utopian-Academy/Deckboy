// Ableton Link, behind one small class.
//
// Link (github.com/Ableton/link, GPLv2+, vendored in native/extras/link) lets
// apps on one LAN share a tempo and a bar phase. Deckboy joins as an ordinary
// peer: it reads the session's beat clock, and it proposes a tempo when the
// operator taps or types one.
//
// Only link_session.cpp includes Link and Asio, because they are large and
// bring their own platform macros. Everything else sees this header. A build
// with ENABLE_LINK=OFF compiles link_session.cpp to a stub: available() is
// false and every call does nothing.
#pragma once

#include <cstddef>
#include <memory>

namespace deckboy {

class LinkSession {
public:
  LinkSession();
  ~LinkSession();
  LinkSession(const LinkSession&) = delete;
  LinkSession& operator=(const LinkSession&) = delete;

  // True when this build contains Link.
  static bool available();

  // Join or leave the session. The first enable starts Link's network threads.
  // `bpm` is the tempo this app offers: it stands only while no peer has one
  // (a peer's tempo wins), so joining never drags a running session around.
  void setEnabled(bool on, double bpm);
  bool enabled() const;

  // Other apps in the session, not counting this one. 0 while alone.
  std::size_t peers() const;

  // The session tempo, in beats per minute.
  double tempo() const;
  // Ask the session to change tempo. Every peer follows.
  void proposeTempo(double bpm);

  // Beats on the shared timeline right now. The bar phase is
  // fmod(beats, quantum), the same number on every peer.
  // Returns false when Link is off.
  bool beatsNow(double quantum, double& beatsOut) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace deckboy
