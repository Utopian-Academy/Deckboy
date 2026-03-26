// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

#pragma once

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

inline std::optional<double> parseNumericExpression(std::string expression) {
  // Inline trim to avoid coupling to utils.
  {
    auto begin = expression.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
      return std::nullopt;
    }
    auto end = expression.find_last_not_of(" \t\r\n");
    expression = expression.substr(begin, end - begin + 1);
  }
  if (expression.empty()) {
    return std::nullopt;
  }

  struct Parser {
    const std::string& text;
    size_t pos = 0;

    void skipWs() {
      while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
      }
    }

    std::optional<double> parseExpr() {
      auto lhs = parseTerm();
      if (!lhs) {
        return std::nullopt;
      }
      while (true) {
        skipWs();
        if (pos >= text.size() || (text[pos] != '+' && text[pos] != '-')) {
          break;
        }
        char op = text[pos++];
        auto rhs = parseTerm();
        if (!rhs) {
          return std::nullopt;
        }
        if (op == '+') {
          *lhs += *rhs;
        } else {
          *lhs -= *rhs;
        }
      }
      return lhs;
    }

    std::optional<double> parseTerm() {
      auto lhs = parseFactor();
      if (!lhs) {
        return std::nullopt;
      }
      while (true) {
        skipWs();
        if (pos >= text.size() || (text[pos] != '*' && text[pos] != '/')) {
          break;
        }
        char op = text[pos++];
        auto rhs = parseFactor();
        if (!rhs) {
          return std::nullopt;
        }
        if (op == '*') {
          *lhs *= *rhs;
        } else {
          if (std::fabs(*rhs) < 1e-12) {
            return std::nullopt;
          }
          *lhs /= *rhs;
        }
      }
      return lhs;
    }

    std::optional<double> parseFactor() {
      skipWs();
      if (pos >= text.size()) {
        return std::nullopt;
      }

      char ch = text[pos];
      if (ch == '+' || ch == '-') {
        ++pos;
        auto value = parseFactor();
        if (!value) {
          return std::nullopt;
        }
        if (ch == '-') {
          *value = -*value;
        }
        return value;
      }

      if (ch == '(') {
        ++pos;
        auto value = parseExpr();
        if (!value) {
          return std::nullopt;
        }
        skipWs();
        if (pos >= text.size() || text[pos] != ')') {
          return std::nullopt;
        }
        ++pos;
        return value;
      }

      const char* start = text.c_str() + pos;
      char* end = nullptr;
      double value = std::strtod(start, &end);
      if (end == start) {
        return std::nullopt;
      }
      pos = static_cast<size_t>(end - text.c_str());
      return value;
    }
  };

  Parser parser {expression, 0};
  auto value = parser.parseExpr();
  if (!value) {
    return std::nullopt;
  }
  parser.skipWs();
  if (parser.pos != expression.size() || !std::isfinite(*value)) {
    return std::nullopt;
  }
  return value;
}
