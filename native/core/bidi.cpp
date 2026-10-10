// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// bidi.cpp -- see bidi.hpp. Rule names (X5a, W4, N0 ...) are UAX #9's, so a
// reader can hold this next to https://www.unicode.org/reports/tr9/ and check
// each step against the text it implements.

#include "core/bidi.hpp"

#include <algorithm>
#include <array>

namespace deckboy::core::bidi {

namespace {

struct BidiRange {
  char32_t first;
  char32_t last;
  BidiClass cls;
};

struct BidiBracket {
  char32_t codePoint;
  char32_t pair;
  bool opening;
};

#include "core/bidi_data.inc"

using C = BidiClass;

constexpr int kMaxDepth = 125;          // BD2
constexpr std::size_t kBracketStack = 63;  // BD16

bool isIsolateInitiator(C c) { return c == C::LRI || c == C::RLI || c == C::FSI; }

bool isRemovedByX9(C c) {
  return c == C::RLE || c == C::LRE || c == C::RLO || c == C::LRO ||
         c == C::PDF || c == C::BN;
}

// NI in N1/N2: neutrals and the isolate formatting characters.
bool isNeutralOrIsolate(C c) {
  return c == C::B || c == C::S || c == C::WS || c == C::ON || c == C::LRI ||
         c == C::RLI || c == C::FSI || c == C::PDI;
}

// For N0 and N1, European and Arabic numbers count as R.
C strongForNeutrals(C c) {
  if (c == C::L) return C::L;
  if (c == C::R || c == C::EN || c == C::AN) return C::R;
  return C::ON;
}

// Canonical equivalents among the paired brackets. BD16 says the only ones are
// U+2329/U+3008 and U+232A/U+3009, and that no more will ever be added.
char32_t canonicalBracket(char32_t cp) {
  if (cp == 0x2329) return 0x3008;
  if (cp == 0x232A) return 0x3009;
  return cp;
}

int leastOddAbove(int level) { return (level + 1) | 1; }
int leastEvenAbove(int level) { return (level + 2) & ~1; }

// BD9: each isolate initiator's matching PDI (or -1), and the reverse.
void matchIsolates(const std::vector<C>& types, std::vector<int>& pdiFor,
                   std::vector<int>& initiatorFor) {
  const std::size_t n = types.size();
  pdiFor.assign(n, -1);
  initiatorFor.assign(n, -1);
  std::vector<int> open;
  for (std::size_t i = 0; i < n; ++i) {
    if (isIsolateInitiator(types[i])) {
      open.push_back(static_cast<int>(i));
    } else if (types[i] == C::PDI && !open.empty()) {
      pdiFor[static_cast<std::size_t>(open.back())] = static_cast<int>(i);
      initiatorFor[i] = open.back();
      open.pop_back();
    } else if (types[i] == C::B) {
      open.clear();
    }
  }
}

// P2 over [from, to): 1 if the first strong character is R or AL, 0 if it is
// L, -1 if there is none. Isolates are skipped whole, as the rule says.
int firstStrongLevel(const std::vector<C>& types, const std::vector<int>& pdiFor,
                     std::size_t from, std::size_t to) {
  for (std::size_t i = from; i < to; ++i) {
    const C t = types[i];
    if (isIsolateInitiator(t)) {
      const int pdi = pdiFor[i];
      if (pdi < 0) return -1;  // to the end of the paragraph
      i = static_cast<std::size_t>(pdi);
      continue;
    }
    if (t == C::L) return 0;
    if (t == C::R || t == C::AL) return 1;
    if (t == C::B) break;
  }
  return -1;
}

// One isolating run sequence (BD13) and the rules applied to it.
struct Sequence {
  std::vector<std::size_t> idx;  // element indices, X9 removals excluded
  std::vector<C> types;          // working types, parallel to idx
  int level = 0;
  C sos = C::L;
  C eos = C::L;
};

void resolveWeak(Sequence& s) {
  auto& t = s.types;
  const std::size_t n = t.size();
  // W1. NSM takes the type of what precedes it -- ON after an isolate
  // initiator or a PDI -- or sos at the start of the sequence.
  for (std::size_t k = 0; k < n; ++k) {
    if (t[k] != C::NSM) continue;
    if (k == 0) {
      t[k] = s.sos;
    } else {
      const C prev = t[k - 1];
      t[k] = (isIsolateInitiator(prev) || prev == C::PDI) ? C::ON : prev;
    }
  }
  // W2. EN after AL (nearest strong, else sos) becomes AN.
  {
    C lastStrong = s.sos;
    for (std::size_t k = 0; k < n; ++k) {
      if (t[k] == C::L || t[k] == C::R || t[k] == C::AL) {
        lastStrong = t[k];
      } else if (t[k] == C::EN && lastStrong == C::AL) {
        t[k] = C::AN;
      }
    }
  }
  // W3. AL becomes R.
  for (C& c : t) {
    if (c == C::AL) c = C::R;
  }
  // W4. A single ES between ENs, or a single CS between numbers of one type.
  for (std::size_t k = 1; k + 1 < n; ++k) {
    if (t[k] == C::ES) {
      if (t[k - 1] == C::EN && t[k + 1] == C::EN) t[k] = C::EN;
    } else if (t[k] == C::CS) {
      if (t[k - 1] == C::EN && t[k + 1] == C::EN) {
        t[k] = C::EN;
      } else if (t[k - 1] == C::AN && t[k + 1] == C::AN) {
        t[k] = C::AN;
      }
    }
  }
  // W5. A run of ET touching an EN becomes EN.
  for (std::size_t k = 0; k < n;) {
    if (t[k] != C::ET) {
      ++k;
      continue;
    }
    std::size_t end = k;
    while (end < n && t[end] == C::ET) ++end;
    const bool touches = (k > 0 && t[k - 1] == C::EN) || (end < n && t[end] == C::EN);
    if (touches) {
      for (std::size_t j = k; j < end; ++j) t[j] = C::EN;
    }
    k = end;
  }
  // W6. Separators and terminators left over become ON.
  for (C& c : t) {
    if (c == C::ES || c == C::ET || c == C::CS) c = C::ON;
  }
  // W7. EN after L (nearest strong, else sos) becomes L.
  {
    C lastStrong = s.sos;
    for (std::size_t k = 0; k < n; ++k) {
      if (t[k] == C::L || t[k] == C::R) {
        lastStrong = t[k];
      } else if (t[k] == C::EN && lastStrong == C::L) {
        t[k] = C::L;
      }
    }
  }
}

// N0. Paired brackets resolve together.
void resolveBrackets(Sequence& s, const std::vector<BracketInfo>& brackets,
                     const std::vector<char32_t>& codePoints,
                     const std::vector<C>& originalTypes) {
  if (brackets.empty()) return;
  auto& t = s.types;
  const std::size_t n = t.size();

  // BD16 -- identify the pairs.
  struct Open {
    char32_t closer;
    std::size_t at;
  };
  std::vector<Open> stack;
  std::vector<std::pair<std::size_t, std::size_t>> pairs;
  for (std::size_t k = 0; k < n; ++k) {
    // BD14/BD15: a bracket only counts while its CURRENT type is ON; an
    // override (X6) can have made it strong.
    if (t[k] != C::ON) continue;
    const std::size_t i = s.idx[k];
    const BracketInfo& b = brackets[i];
    if (b.pairedWith == 0) continue;
    if (b.opening) {
      if (stack.size() >= kBracketStack) {
        pairs.clear();
        stack.clear();
        break;  // overflow: no pairs at all in this sequence
      }
      stack.push_back({canonicalBracket(b.pairedWith), k});
    } else {
      const char32_t self = canonicalBracket(codePoints[i]);
      for (std::size_t depth = stack.size(); depth-- > 0;) {
        if (stack[depth].closer == self) {
          pairs.emplace_back(stack[depth].at, k);
          stack.resize(depth);
          break;
        }
      }
    }
  }
  std::sort(pairs.begin(), pairs.end());

  const C embedding = (s.level & 1) ? C::R : C::L;
  const C opposite = (embedding == C::L) ? C::R : C::L;
  auto setBracket = [&](std::size_t k, C to) {
    t[k] = to;
    // NSMs that followed the bracket (before W1 rewrote them) follow it here.
    for (std::size_t j = k + 1; j < n && originalTypes[s.idx[j]] == C::NSM; ++j) {
      t[j] = to;
    }
  };
  for (const auto& [open, close] : pairs) {
    bool sawEmbedding = false;
    bool sawOpposite = false;
    for (std::size_t k = open + 1; k < close; ++k) {
      const C strong = strongForNeutrals(t[k]);
      if (strong == embedding) sawEmbedding = true;
      else if (strong == opposite) sawOpposite = true;
    }
    if (sawEmbedding) {
      setBracket(open, embedding);
      setBracket(close, embedding);
    } else if (sawOpposite) {
      C context = s.sos;
      for (std::size_t k = open; k-- > 0;) {
        const C strong = strongForNeutrals(t[k]);
        if (strong != C::ON) {
          context = strong;
          break;
        }
      }
      const C to = (context == opposite) ? opposite : embedding;
      setBracket(open, to);
      setBracket(close, to);
    }
    // No strong type inside: N1 and N2 decide them.
  }
}

void resolveNeutrals(Sequence& s) {
  auto& t = s.types;
  const std::size_t n = t.size();
  const C embedding = (s.level & 1) ? C::R : C::L;
  for (std::size_t k = 0; k < n;) {
    if (!isNeutralOrIsolate(t[k])) {
      ++k;
      continue;
    }
    std::size_t end = k;
    while (end < n && isNeutralOrIsolate(t[end])) ++end;
    const C before = (k == 0) ? s.sos : strongForNeutrals(t[k - 1]);
    const C after = (end == n) ? s.eos : strongForNeutrals(t[end]);
    const C to = (before == after && before != C::ON) ? before : embedding;  // N1, else N2
    for (std::size_t j = k; j < end; ++j) t[j] = to;
    k = end;
  }
}

}  // namespace

BidiClass classOf(char32_t cp) {
  const auto* begin = std::begin(kBidiRanges);
  const auto* end = std::end(kBidiRanges);
  const auto* it = std::upper_bound(begin, end, cp, [](char32_t v, const BidiRange& r) {
    return v < r.first;
  });
  if (it == begin) return C::L;
  --it;
  return (cp <= it->last) ? it->cls : C::L;
}

BracketInfo bracketOf(char32_t cp) {
  const auto* begin = std::begin(kBidiBrackets);
  const auto* end = std::end(kBidiBrackets);
  const auto* it = std::lower_bound(begin, end, cp, [](const BidiBracket& b, char32_t v) {
    return b.codePoint < v;
  });
  if (it == end || it->codePoint != cp) return {};
  return {it->pair, it->opening};
}

const char* unicodeVersion() { return kUnicodeVersion; }

namespace {
constexpr std::array<const char*, 23> kNames = {
  "L", "R", "AL", "EN", "ES", "ET", "AN", "CS", "NSM", "BN", "B", "S", "WS", "ON",
  "LRE", "LRO", "RLE", "RLO", "PDF", "LRI", "RLI", "FSI", "PDI"};
}

const char* className(BidiClass c) { return kNames[static_cast<std::size_t>(c)]; }

bool classFromName(std::string_view name, BidiClass& out) {
  for (std::size_t i = 0; i < kNames.size(); ++i) {
    if (name == kNames[i]) {
      out = static_cast<BidiClass>(i);
      return true;
    }
  }
  return false;
}

Resolution resolve(const std::vector<BidiClass>& types0,
                   const std::vector<BracketInfo>& brackets,
                   const std::vector<char32_t>& codePoints, Direction direction) {
  Resolution out;
  const std::size_t n = types0.size();
  out.originalTypes = types0;
  out.levels.assign(n, 0);

  std::vector<int> pdiFor;
  std::vector<int> initiatorFor;
  matchIsolates(types0, pdiFor, initiatorFor);

  // P2, P3 (or the caller's choice, HL1).
  int paragraphLevel = 0;
  if (direction == Direction::RightToLeft) {
    paragraphLevel = 1;
  } else if (direction == Direction::Auto) {
    paragraphLevel = std::max(0, firstStrongLevel(types0, pdiFor, 0, n));
  }
  out.paragraphLevel = paragraphLevel;
  if (n == 0) return out;

  // X1-X8: explicit levels and directions.
  struct Status {
    int level;
    int override;  // 0 neutral, 1 left-to-right, 2 right-to-left
    bool isolate;
  };
  std::vector<Status> stack;
  stack.reserve(kMaxDepth + 2);
  stack.push_back({paragraphLevel, 0, false});
  int overflowIsolates = 0;
  int overflowEmbeddings = 0;
  int validIsolates = 0;
  std::vector<C> types = types0;  // X6 rewrites types under an override
  std::vector<int>& levels = out.levels;
  auto overridden = [&](std::size_t i) {
    const int o = stack.back().override;
    if (o == 1) types[i] = C::L;
    else if (o == 2) types[i] = C::R;
  };

  for (std::size_t i = 0; i < n; ++i) {
    const C t = types0[i];
    switch (t) {
      case C::RLE:
      case C::LRE:
      case C::RLO:
      case C::LRO: {  // X2-X5
        const bool rtl = (t == C::RLE || t == C::RLO);
        const int level = rtl ? leastOddAbove(stack.back().level)
                              : leastEvenAbove(stack.back().level);
        if (level <= kMaxDepth && overflowIsolates == 0 && overflowEmbeddings == 0) {
          const int o = (t == C::RLO) ? 2 : (t == C::LRO) ? 1 : 0;
          stack.push_back({level, o, false});
        } else if (overflowIsolates == 0) {
          ++overflowEmbeddings;
        }
        levels[i] = stack.back().level;  // removed by X9; kept tidy for runs
        break;
      }
      case C::RLI:
      case C::LRI:
      case C::FSI: {  // X5a-X5c
        levels[i] = stack.back().level;
        overridden(i);
        bool rtl = (t == C::RLI);
        if (t == C::FSI) {
          const std::size_t to =
            pdiFor[i] >= 0 ? static_cast<std::size_t>(pdiFor[i]) : n;
          rtl = firstStrongLevel(types0, pdiFor, i + 1, to) == 1;
        }
        const int level = rtl ? leastOddAbove(stack.back().level)
                              : leastEvenAbove(stack.back().level);
        if (level <= kMaxDepth && overflowIsolates == 0 && overflowEmbeddings == 0) {
          ++validIsolates;
          stack.push_back({level, 0, true});
        } else {
          ++overflowIsolates;
        }
        break;
      }
      case C::PDI: {  // X6a
        if (overflowIsolates > 0) {
          --overflowIsolates;
        } else if (validIsolates > 0) {
          overflowEmbeddings = 0;
          while (!stack.back().isolate) stack.pop_back();
          stack.pop_back();
          --validIsolates;
        }
        levels[i] = stack.back().level;
        overridden(i);
        break;
      }
      case C::PDF: {  // X7
        if (overflowIsolates > 0) {
          // within an overflow isolate: nothing
        } else if (overflowEmbeddings > 0) {
          --overflowEmbeddings;
        } else if (!stack.back().isolate && stack.size() >= 2) {
          stack.pop_back();
        }
        levels[i] = stack.back().level;
        break;
      }
      case C::B:  // X8
        levels[i] = paragraphLevel;
        break;
      case C::BN:
        levels[i] = stack.back().level;
        break;
      default:  // X6
        levels[i] = stack.back().level;
        overridden(i);
        break;
    }
  }

  // X9: removed elements play no further part.
  std::vector<bool> removed(n);
  for (std::size_t i = 0; i < n; ++i) removed[i] = isRemovedByX9(types0[i]);

  // X10, BD7: level runs over what remains.
  std::vector<std::vector<std::size_t>> runs;
  std::vector<int> runOf(n, -1);
  {
    int lastLevel = -1;
    for (std::size_t i = 0; i < n; ++i) {
      if (removed[i]) continue;
      if (runs.empty() || levels[i] != lastLevel) {
        runs.emplace_back();
        lastLevel = levels[i];
      }
      runs.back().push_back(i);
      runOf[i] = static_cast<int>(runs.size() - 1);
    }
  }

  // BD13: chain level runs into isolating run sequences.
  std::vector<Sequence> sequences;
  for (std::size_t r = 0; r < runs.size(); ++r) {
    const std::size_t first = runs[r].front();
    if (types0[first] == C::PDI && initiatorFor[first] >= 0) continue;  // joined to its initiator's
    Sequence s;
    std::size_t current = r;
    while (true) {
      for (std::size_t i : runs[current]) s.idx.push_back(i);
      const std::size_t last = runs[current].back();
      if (!isIsolateInitiator(types0[last]) || pdiFor[last] < 0) break;
      const int next = runOf[static_cast<std::size_t>(pdiFor[last])];
      if (next < 0 || static_cast<std::size_t>(next) == current) break;
      current = static_cast<std::size_t>(next);
    }
    sequences.push_back(std::move(s));
  }

  std::vector<int> resolved(n, kRemoved);
  for (Sequence& s : sequences) {
    const std::size_t first = s.idx.front();
    const std::size_t last = s.idx.back();
    s.level = levels[first];
    // sos/eos (X10): against the neighbouring levels, X9 removals skipped.
    int before = paragraphLevel;
    for (std::size_t i = first; i-- > 0;) {
      if (!removed[i]) {
        before = levels[i];
        break;
      }
    }
    int after = paragraphLevel;
    if (!isIsolateInitiator(types0[last])) {
      for (std::size_t i = last + 1; i < n; ++i) {
        if (!removed[i]) {
          after = levels[i];
          break;
        }
      }
    }
    s.sos = (std::max(before, s.level) & 1) ? C::R : C::L;
    s.eos = (std::max(after, s.level) & 1) ? C::R : C::L;
    s.types.reserve(s.idx.size());
    for (std::size_t i : s.idx) s.types.push_back(types[i]);

    resolveWeak(s);
    resolveBrackets(s, brackets, codePoints, types0);
    resolveNeutrals(s);

    // I1, I2.
    for (std::size_t k = 0; k < s.idx.size(); ++k) {
      const C t = s.types[k];
      int level = s.level;
      if ((level & 1) == 0) {
        if (t == C::R) level += 1;
        else if (t == C::AN || t == C::EN) level += 2;
      } else if (t == C::L || t == C::EN || t == C::AN) {
        level += 1;
      }
      resolved[s.idx[k]] = level;
    }
  }
  out.levels = std::move(resolved);
  return out;
}

std::vector<int> lineLevels(const Resolution& r, std::size_t begin, std::size_t end) {
  std::vector<int> levels(r.levels.begin() + static_cast<std::ptrdiff_t>(begin),
                          r.levels.begin() + static_cast<std::ptrdiff_t>(end));
  const auto& t = r.originalTypes;
  auto whitespaceLike = [&](std::size_t i) {
    const C c = t[i];
    // Implementation notes (5.2): characters X9 removed go with the
    // whitespace whose level L1 resets.
    return c == C::WS || isIsolateInitiator(c) || c == C::PDI || isRemovedByX9(c);
  };
  auto resetBackFrom = [&](std::size_t stop) {
    for (std::size_t j = stop; j-- > begin;) {
      if (!whitespaceLike(j)) break;
      if (levels[j - begin] != kRemoved) levels[j - begin] = r.paragraphLevel;
    }
  };
  for (std::size_t i = begin; i < end; ++i) {
    if (t[i] == C::S || t[i] == C::B) {
      levels[i - begin] = r.paragraphLevel;
      resetBackFrom(i);
    }
  }
  resetBackFrom(end);
  return levels;
}

std::vector<std::size_t> visualOrder(const Resolution& r, std::size_t begin, std::size_t end) {
  const std::vector<int> levels = lineLevels(r, begin, end);
  std::vector<std::size_t> order;
  std::vector<int> orderLevels;
  for (std::size_t i = begin; i < end; ++i) {
    if (levels[i - begin] == kRemoved) continue;
    order.push_back(i);
    orderLevels.push_back(levels[i - begin]);
  }
  if (order.empty()) return order;
  const int highest = *std::max_element(orderLevels.begin(), orderLevels.end());
  int lowestOdd = highest + 1;
  for (int l : orderLevels) {
    if ((l & 1) && l < lowestOdd) lowestOdd = l;
  }
  // L2: from the highest level down to the lowest odd one, reverse every
  // contiguous run at that level or above.
  for (int level = highest; level >= lowestOdd; --level) {
    for (std::size_t k = 0; k < order.size();) {
      if (orderLevels[k] < level) {
        ++k;
        continue;
      }
      std::size_t end2 = k;
      while (end2 < order.size() && orderLevels[end2] >= level) ++end2;
      std::reverse(order.begin() + static_cast<std::ptrdiff_t>(k),
                   order.begin() + static_cast<std::ptrdiff_t>(end2));
      std::reverse(orderLevels.begin() + static_cast<std::ptrdiff_t>(k),
                   orderLevels.begin() + static_cast<std::ptrdiff_t>(end2));
      k = end2;
    }
  }
  return order;
}

std::size_t decodeUtf8(std::string_view s, std::size_t at, char32_t& out) {
  const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
  const unsigned char lead = byte(at);
  out = 0xFFFD;
  if (lead < 0x80) {
    out = lead;
    return 1;
  }
  std::size_t len = 0;
  char32_t cp = 0;
  char32_t min = 0;
  if ((lead & 0xE0) == 0xC0) {
    len = 2;
    cp = lead & 0x1F;
    min = 0x80;
  } else if ((lead & 0xF0) == 0xE0) {
    len = 3;
    cp = lead & 0x0F;
    min = 0x800;
  } else if ((lead & 0xF8) == 0xF0) {
    len = 4;
    cp = lead & 0x07;
    min = 0x10000;
  } else {
    return 1;
  }
  if (at + len > s.size()) return 1;
  for (std::size_t k = 1; k < len; ++k) {
    const unsigned char c = byte(at + k);
    if ((c & 0xC0) != 0x80) return 1;
    cp = (cp << 6) | (c & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 1;
  out = cp;
  return len;
}

std::vector<Paragraph> paragraphs(std::string_view text, Direction direction) {
  std::vector<Paragraph> out;
  std::size_t at = 0;
  while (at < text.size() || out.empty()) {
    Paragraph p;
    p.byteBegin = at;
    std::vector<C> types;
    std::vector<BracketInfo> brackets;
    std::vector<char32_t> cps;
    bool anyBracket = false;
    while (at < text.size()) {
      char32_t cp = 0;
      const std::size_t len = decodeUtf8(text, at, cp);
      p.offsets.push_back(at);
      const C c = classOf(cp);
      types.push_back(c);
      cps.push_back(cp);
      const BracketInfo b = bracketOf(cp);
      anyBracket = anyBracket || b.pairedWith != 0;
      brackets.push_back(b);
      at += len;
      if (c == C::B) break;  // P1: the separator ends its paragraph
    }
    p.offsets.push_back(at);
    p.byteEnd = at;
    if (!anyBracket) {
      brackets.clear();
      cps.clear();
    }
    p.resolution = resolve(types, brackets, cps, direction);
    out.push_back(std::move(p));
    if (at >= text.size()) break;
  }
  return out;
}

std::vector<Run> lineRuns(const Paragraph& p, std::size_t byteBegin, std::size_t byteEnd) {
  std::vector<Run> runs;
  // Code point indices of the line.
  const auto& offsets = p.offsets;
  const std::size_t count = offsets.size() - 1;
  std::size_t first = 0;
  while (first < count && offsets[first] < byteBegin) ++first;
  std::size_t last = first;
  while (last < count && offsets[last] < byteEnd) ++last;
  if (first >= last) return runs;

  std::vector<int> levels = lineLevels(p.resolution, first, last);
  // Elements X9 removed take a neighbour's level, so a ZWJ or an embedding
  // control sits inside the run beside it instead of splitting it.
  {
    int previous = kRemoved;
    for (int& l : levels) {
      if (l == kRemoved) l = previous;
      else previous = l;
    }
    // Only a line that STARTS with removals is left with any; they take the
    // first real level after them, or the paragraph's if there is none.
    int next = p.resolution.paragraphLevel;
    for (std::size_t k = levels.size(); k-- > 0;) {
      if (levels[k] == kRemoved) levels[k] = next;
      else next = levels[k];
    }
  }
  // Level runs, logical order.
  for (std::size_t k = 0; k < levels.size();) {
    std::size_t end = k;
    while (end < levels.size() && levels[end] == levels[k]) ++end;
    Run run;
    run.begin = offsets[first + k];
    run.end = offsets[first + end];
    run.level = levels[k];
    runs.push_back(run);
    k = end;
  }
  // L2 at run granularity: every character of a run shares its level, so
  // reversing runs is reversing characters, a run at a time.
  int highest = 0;
  int lowestOdd = 1000;
  for (const Run& r : runs) {
    highest = std::max(highest, r.level);
    if ((r.level & 1) && r.level < lowestOdd) lowestOdd = r.level;
  }
  for (int level = highest; level >= lowestOdd; --level) {
    for (std::size_t k = 0; k < runs.size();) {
      if (runs[k].level < level) {
        ++k;
        continue;
      }
      std::size_t end = k;
      while (end < runs.size() && runs[end].level >= level) ++end;
      std::reverse(runs.begin() + static_cast<std::ptrdiff_t>(k),
                   runs.begin() + static_cast<std::ptrdiff_t>(end));
      k = end;
    }
  }
  return runs;
}

std::vector<Run> visualRuns(std::string_view text, Direction direction,
                            bool* paragraphRightToLeft) {
  std::vector<Run> out;
  const std::vector<Paragraph> paras = paragraphs(text, direction);
  if (paragraphRightToLeft) {
    *paragraphRightToLeft = !paras.empty() && paras.front().rightToLeft();
  }
  for (const Paragraph& p : paras) {
    const std::vector<Run> runs = lineRuns(p, p.byteBegin, p.byteEnd);
    out.insert(out.end(), runs.begin(), runs.end());
  }
  return out;
}

bool needsBidi(std::string_view text) {
  for (std::size_t at = 0; at < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[at]);
    if (lead < 0x80) {
      ++at;
      continue;
    }
    char32_t cp = 0;
    at += decodeUtf8(text, at, cp);
    const C c = classOf(cp);
    // FSI is not here: it only turns right to left when an R or AL inside it
    // says so, and that R or AL is caught on its own.
    if (c == C::R || c == C::AL || c == C::RLE || c == C::RLO || c == C::RLI) {
      return true;
    }
  }
  return false;
}

bool firstStrongIsRightToLeft(std::string_view text) {
  const std::vector<Paragraph> paras = paragraphs(text, Direction::Auto);
  return !paras.empty() && paras.front().rightToLeft();
}

}  // namespace deckboy::core::bidi
