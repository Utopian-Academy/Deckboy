// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
// This file is part of Deckboy, a cue deck for live events.
// See LICENSE for details.

// ============================================================================
// expression_parser.hpp — Simple arithmetic expression evaluator.
//
// Allows operators to type math expressions into numeric text fields in the
// inspector (e.g. "1920/2" for offset, "0.5+0.1" for crop values).
//
// Supports: +, -, *, /, unary +/-, parentheses, decimal numbers.
// Does NOT support: variables, functions, exponents, or bitwise ops.
//
// Grammar (recursive descent):
//   expr   → term (('+' | '-') term)*
//   term   → factor (('*' | '/') factor)*
//   factor → ['+' | '-'] factor | '(' expr ')' | number
//
// Returns std::nullopt on:
//   - Empty/whitespace-only input
//   - Syntax errors (unbalanced parens, unexpected characters)
//   - Division by zero (denominator < 1e-12)
//   - Non-finite results (NaN, infinity)
//   - Trailing unparsed characters
//
// Called by: inline text editor commit handlers in app_quick_action.ipp
// ============================================================================

#pragma once

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

// Parse and evaluate a simple arithmetic expression string.
// Returns the computed double value, or nullopt if the expression is invalid.
inline std::optional<double> parseNumericExpression(std::string expression) {
  // Inline trim to avoid coupling to utils (this header is self-contained).
  {
    auto begin = expression.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
      return std::nullopt;  // all whitespace
    }
    auto end = expression.find_last_not_of(" \t\r\n");
    expression = expression.substr(begin, end - begin + 1);
  }
  if (expression.empty()) {
    return std::nullopt;
  }

  // Recursive descent parser — operates on the trimmed expression string.
  struct Parser {
    const std::string& text;  // reference to the expression being parsed
    size_t pos = 0;           // current parse position

    // Skip whitespace between tokens.
    void skipWs() {
      while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
      }
    }

    // Parse an expression: term (('+' | '-') term)*
    // This is the lowest-precedence level (addition/subtraction).
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

    // Parse a term: factor (('*' | '/') factor)*
    // Higher precedence than addition (multiplication/division).
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
          // Guard against division by zero
          if (std::fabs(*rhs) < 1e-12) {
            return std::nullopt;
          }
          *lhs /= *rhs;
        }
      }
      return lhs;
    }

    // Parse a factor: unary prefix, parenthesized group, or literal number.
    // This is the highest-precedence level.
    std::optional<double> parseFactor() {
      skipWs();
      if (pos >= text.size()) {
        return std::nullopt;
      }

      char ch = text[pos];

      // Unary plus/minus prefix (recursive to handle chains like "--5")
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

      // Parenthesized sub-expression
      if (ch == '(') {
        ++pos;
        auto value = parseExpr();
        if (!value) {
          return std::nullopt;
        }
        skipWs();
        if (pos >= text.size() || text[pos] != ')') {
          return std::nullopt;  // missing closing parenthesis
        }
        ++pos;
        return value;
      }

      // Numeric literal (integer or floating point, parsed by strtod)
      const char* start = text.c_str() + pos;
      char* end = nullptr;
      double value = std::strtod(start, &end);
      if (end == start) {
        return std::nullopt;  // not a valid number
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
  // Ensure the entire expression was consumed (no trailing garbage)
  parser.skipWs();
  if (parser.pos != expression.size() || !std::isfinite(*value)) {
    return std::nullopt;
  }
  return value;
}
