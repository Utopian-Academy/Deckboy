// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
//
// PRIVATE EXTRAS: things a particular build carries that the public source does
// not. A folder `private-extras/` at the repository root (ignored by git, with
// a CMakeLists.txt of its own) adds its sources to the app, and each extra
// registers itself here when the program starts. Without one every list below
// is empty and Deckboy behaves exactly as published.
//
// An extra can bring three things:
//   - a generator: a source under SOURCE > MADE BY DECKBOY, drawn as pixels
//     (pattern://<id>), like Terrarium;
//   - remote verbs, answered like any other (and, when marked, allowed from a
//     paired phone);
//   - web pages, served by the phone server under /remote/x/<name>, so they
//     share the phone's pairing and can send its commands.
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace deckboy::extras {

// One button on the desk, in the Cue Inspector while a cue of the extra's
// source is selected. Buttons with the same section and row share a line.
struct DeskControl {
  std::string section;   // a heading; each new one starts a group
  int row = 0;
  std::string label;
  std::string command;   // a remote command run on click; empty: shown, not clickable
  bool lit = false;      // drawn as an ON toggle
};

struct Generator {
  std::string id;     // pattern://<id>, lower case
  std::string name;   // as SOURCE > MADE BY DECKBOY lists it
  // Fill rgba (w * h * 4 bytes, opaque, straight) for time `seconds`. Called
  // from engine threads, and from more than one at once (an output and the
  // preview), so it must guard any state it keeps.
  std::function<void(std::uint8_t* rgba, int w, int h, double seconds)> render;
  // A page of its own (see WebPage) that the phone controller offers as a
  // button while this source is live on any deck. Empty: none.
  std::string page = {};
  // The desk's controls for it, rebuilt every frame from its state. Empty:
  // the inspector shows nothing of its own.
  std::function<std::vector<DeskControl>()> desk = {};
  // Listed in SOURCE > MADE BY DECKBOY only once the desk has found it.
  bool hidden = false;
};

struct Command {
  std::string verb;            // upper case, e.g. "TABLE"
  bool phoneAllowed = false;   // may a paired phone send it
  // parts[0] is the verb. Returns false with `reply` as the reason when it
  // cannot act; on success `reply` is the detail after "OK <VERB>".
  std::function<bool(const std::vector<std::string>& parts, std::string& reply)> run;
};

struct WebPage {
  std::string name;          // served at /remote/x/<name>
  std::string contentType;   // e.g. "text/html; charset=utf-8"
  std::function<std::string()> body;
};

inline std::vector<Generator>& generators() { static std::vector<Generator> list; return list; }
inline std::vector<Command>& commands() { static std::vector<Command> list; return list; }
inline std::vector<WebPage>& webPages() { static std::vector<WebPage> list; return list; }

inline const Generator* findGenerator(const std::string& id) {
  for (const auto& g : generators()) if (g.id == id) return &g;
  return nullptr;
}
inline const Command* findCommand(const std::string& upperVerb) {
  for (const auto& c : commands()) if (c.verb == upperVerb) return &c;
  return nullptr;
}
inline const WebPage* findWebPage(const std::string& name) {
  for (const auto& p : webPages()) if (p.name == name) return &p;
  return nullptr;
}

// One per extra, at namespace scope in its own source file.
struct Registration {
  explicit Registration(Generator g) { generators().push_back(std::move(g)); }
  explicit Registration(Command c) { commands().push_back(std::move(c)); }
  explicit Registration(WebPage p) { webPages().push_back(std::move(p)); }
};

}  // namespace deckboy::extras
