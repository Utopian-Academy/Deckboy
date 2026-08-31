// code_source.hpp — a per-pixel expression, compiled once and run on every core.
//
// WHY NOT A SHADER. The obvious answer to "let people live-code effects" is
// GLSL, and it is the wrong one here. Deckboy draws through SDL_Renderer, whose
// backend is D3D11, D3D12, Metal or OpenGL depending on the machine, and SDL's
// own shader path (SDL_GPU) wants SPIR-V, DXIL or MSL -- already compiled.
// Accepting GLSL at RUNTIME on every platform would mean bundling a shader
// compiler (glslang and friends), which is tens of megabytes and a per-backend
// translation step, to run arithmetic that fits in a few hundred lines.
//
// So the language is evaluated on the CPU instead, and the thing that makes
// that viable already exists: the effect stack's parallelRows splits a frame
// across cores, and a compiled expression is a handful of operations per pixel.
// At 1080p that is a couple of million evaluations, which is the same order as
// the effects that already run there.
//
// The expression is compiled ONCE per change into a flat instruction list --
// not walked as a syntax tree per pixel, which would spend all its time
// chasing pointers. Text in, bytecode out, and the bytecode is what the inner
// loop sees.
//
// WHAT IT LOOKS LIKE. Three comma-separated expressions, one per channel, each
// returning 0..1:
//
//     sin(x*10+t)*0.5+0.5, y, 0.5
//     r, sin(a*3+t)*0.5+0.5, 1-r
//
// Variables: x and y are 0..1 across the frame, cx and cy are -1..1 from the
// centre, r is distance from the centre, a is the angle, t is seconds.

#ifndef DECKBOY_CORE_CODE_SOURCE_HPP
#define DECKBOY_CORE_CODE_SOURCE_HPP

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace deckboy::code {

// The variables an expression can read. Filled per pixel by the evaluator.
enum class Var : int { X, Y, Cx, Cy, R, A, T, Count };

enum class Op : std::uint8_t {
  PushConst, PushVar,
  Add, Sub, Mul, Div, Mod, Pow, Neg,
  Sin, Cos, Tan, Abs, Floor, Fract, Sqrt, Min, Max, Clamp, Step, Mix, Atan2,
  LessThan, GreaterThan,
  // Named values: read one, write one. The slot is an index into the caller's
  // per-pixel scratch, sized by the number of names the source declared.
  PushUser, StoreUser,
  // Enough of a standard library that the common shapes are one call rather
  // than a line of algebra: a distance, a soft edge, a choice.
  Length, Smoothstep, Sign, Exp, Log, Atan, If,
};

struct Instruction {
  Op op = Op::PushConst;
  double value = 0.0;   // PushConst
  int slot = 0;         // PushVar
};

// One compiled channel.
using Program = std::vector<Instruction>;

struct CompiledSource {
  // Everything before the last statement: the named values, computed once per
  // pixel and then read by whichever channels want them.
  Program prelude;
  Program channel[3];
  // One entry per name the source declared, in the order the slots were
  // allocated. The editor colours these as variables, and the evaluator sizes
  // its scratch from the count.
  std::vector<std::string> names;
  std::string error;        // empty when it compiled
  bool ok() const { return error.empty(); }
};

namespace detail {

struct Token {
  enum class Kind { Number, Name, Op, LParen, RParen, Comma, End } kind = Kind::End;
  double number = 0.0;
  std::string text;
};

inline bool isNameChar(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

inline std::vector<Token> tokenise(const std::string& src, std::string& error) {
  std::vector<Token> out;
  std::size_t i = 0;
  while (i < src.size()) {
    const char c = src[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++i; continue; }
    Token token;
    if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < src.size())) {
      std::size_t used = 0;
      token.kind = Token::Kind::Number;
      token.number = std::stod(src.substr(i), &used);
      i += used;
    } else if (isNameChar(c) && !(c >= '0' && c <= '9')) {
      std::size_t start = i;
      while (i < src.size() && isNameChar(src[i])) ++i;
      token.kind = Token::Kind::Name;
      token.text = src.substr(start, i - start);
    } else if (c == '(') { token.kind = Token::Kind::LParen; ++i; }
    else if (c == ')') { token.kind = Token::Kind::RParen; ++i; }
    else if (c == ',') { token.kind = Token::Kind::Comma; ++i; }
    else if (std::string("+-*/%^<>").find(c) != std::string::npos) {
      token.kind = Token::Kind::Op;
      token.text = std::string(1, c);
      ++i;
    } else {
      error = std::string("unexpected character '") + c + "'";
      return {};
    }
    out.push_back(token);
  }
  out.push_back(Token{});
  return out;
}

inline int varSlot(const std::string& name) {
  if (name == "x")  return static_cast<int>(Var::X);
  if (name == "y")  return static_cast<int>(Var::Y);
  if (name == "cx") return static_cast<int>(Var::Cx);
  if (name == "cy") return static_cast<int>(Var::Cy);
  if (name == "r")  return static_cast<int>(Var::R);
  if (name == "a")  return static_cast<int>(Var::A);
  if (name == "t")  return static_cast<int>(Var::T);
  return -1;
}

// Function name to (op, argument count). Arity is checked, because a mistyped
// call should say so rather than reading whatever is left on the stack.
inline bool functionOp(const std::string& name, Op& op, int& args) {
  struct Entry { const char* name; Op op; int args; };
  static const Entry kTable[] = {
    {"sin", Op::Sin, 1},   {"cos", Op::Cos, 1},     {"tan", Op::Tan, 1},
    {"abs", Op::Abs, 1},   {"floor", Op::Floor, 1}, {"fract", Op::Fract, 1},
    {"sqrt", Op::Sqrt, 1}, {"min", Op::Min, 2},     {"max", Op::Max, 2},
    {"mod", Op::Mod, 2},   {"pow", Op::Pow, 2},     {"atan2", Op::Atan2, 2},
    {"step", Op::Step, 2}, {"clamp", Op::Clamp, 3}, {"mix", Op::Mix, 3},
    {"length", Op::Length, 2},     {"smoothstep", Op::Smoothstep, 3},
    {"sign", Op::Sign, 1},         {"exp", Op::Exp, 1},
    {"log", Op::Log, 1},           {"atan", Op::Atan, 1},
    {"if", Op::If, 3},
  };
  for (const Entry& e : kTable) {
    if (name == e.name) { op = e.op; args = e.args; return true; }
  }
  return false;
}

inline int precedence(const std::string& op) {
  if (op == "<" || op == ">") return 1;
  if (op == "+" || op == "-") return 2;
  if (op == "*" || op == "/" || op == "%") return 3;
  if (op == "^") return 4;   // right-associative; see compileExpression
  return 0;
}

inline Op binaryOp(const std::string& op) {
  if (op == "+") return Op::Add;
  if (op == "-") return Op::Sub;
  if (op == "*") return Op::Mul;
  if (op == "/") return Op::Div;
  if (op == "%") return Op::Mod;
  if (op == "^") return Op::Pow;
  if (op == "<") return Op::LessThan;
  return Op::GreaterThan;
}

}  // namespace detail

// Compile one channel expression to a flat program. Shunting-yard, so the
// output is already in the order the evaluator wants and nothing needs walking
// per pixel.
inline bool compileExpression(const std::string& source, Program& out,
                              std::string& error,
                              const std::vector<std::string>& names = {}) {
  out.clear();
  std::vector<detail::Token> tokens = detail::tokenise(source, error);
  if (!error.empty()) return false;

  // Shunting-yard. The stack holds three kinds of thing and they behave
  // differently, so they are distinguished rather than encoded in a string:
  // an operator, a function waiting for its closing paren, and a plain paren.
  struct Entry {
    enum class Kind { Operator, Function, Paren } kind = Kind::Operator;
    Op op = Op::Add;
    int precedence = 0;
    bool rightAssociative = false;
  };
  std::vector<Entry> stack;
  bool expectValue = true;   // tells a unary minus from a subtraction

  auto emitTop = [&out, &stack]() {
    out.push_back({stack.back().op, 0.0, 0});
    stack.pop_back();
  };

  for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
    const detail::Token& token = tokens[i];
    using Kind = detail::Token::Kind;
    if (token.kind == Kind::Number) {
      out.push_back({Op::PushConst, token.number, 0});
      expectValue = false;
    } else if (token.kind == Kind::Name) {
      Op fnOp = Op::Add; int args = 0;
      const int slot = detail::varSlot(token.text);
      if (slot >= 0) {
        out.push_back({Op::PushVar, 0.0, slot});
        expectValue = false;
      } else if (token.text == "pi") {
        out.push_back({Op::PushConst, 3.14159265358979323846, 0});
        expectValue = false;
      } else if (detail::functionOp(token.text, fnOp, args)) {
        stack.push_back({Entry::Kind::Function, fnOp, 0, false});
        expectValue = true;
      } else {
        // A name the source declared earlier. Searched AFTER the built-ins, so
        // no assignment can shadow x, t or sin -- a source that redefined its
        // own coordinates would be a puzzle rather than a feature.
        int userSlot = -1;
        for (std::size_t nameIndex = 0; nameIndex < names.size(); ++nameIndex) {
          if (names[nameIndex] == token.text) {
            userSlot = static_cast<int>(nameIndex);
            break;
          }
        }
        if (userSlot >= 0) {
          out.push_back({Op::PushUser, 0.0, userSlot});
          expectValue = false;
        } else {
          error = "unknown name '" + token.text + "'";
          return false;
        }
      }
    } else if (token.kind == Kind::Op) {
      if (expectValue) {
        if (token.text != "-" && token.text != "+") {
          error = "'" + token.text + "' needs a value before it";
          return false;
        }
        if (token.text == "-") {
          // Unary. Binds tighter than multiplication and is right
          // associative, so -3+5 is (-3)+5 and not -(3+5) -- which is what it
          // was, and it made every negated expression silently wrong.
          stack.push_back({Entry::Kind::Operator, Op::Neg, 5, true});
        }
        continue;   // unary plus is a no-op
      }
      const int prec = detail::precedence(token.text);
      const bool rightAssoc = token.text == "^";
      while (!stack.empty() && stack.back().kind == Entry::Kind::Operator &&
             (stack.back().precedence > prec ||
              (stack.back().precedence == prec && !rightAssoc))) {
        emitTop();
      }
      stack.push_back({Entry::Kind::Operator, detail::binaryOp(token.text),
                       prec, rightAssoc});
      expectValue = true;
    } else if (token.kind == Kind::LParen) {
      stack.push_back({Entry::Kind::Paren, Op::Add, 0, false});
      expectValue = true;
    } else if (token.kind == Kind::Comma) {
      // Pop back to the open paren but LEAVE IT: the argument list is still
      // open. Pushing a fresh paren here is what broke every multi-argument
      // call.
      while (!stack.empty() && stack.back().kind != Entry::Kind::Paren) {
        emitTop();
      }
      if (stack.empty()) {
        error = "comma outside a function call";
        return false;
      }
      expectValue = true;
    } else if (token.kind == Kind::RParen) {
      while (!stack.empty() && stack.back().kind != Entry::Kind::Paren) {
        emitTop();
      }
      if (stack.empty()) {
        error = "unbalanced )";
        return false;
      }
      stack.pop_back();                       // the paren itself
      if (!stack.empty() && stack.back().kind == Entry::Kind::Function) {
        emitTop();                            // now the call can be emitted
      }
      expectValue = false;
    }
  }
  while (!stack.empty()) {
    if (stack.back().kind == Entry::Kind::Paren) {
      error = "unbalanced (";
      return false;
    }
    if (stack.back().kind == Entry::Kind::Function) {
      error = "function call is missing its (";
      return false;
    }
    emitTop();
  }
  if (out.empty()) {
    error = "empty expression";
    return false;
  }
  return true;
}

// Compile "rExpr, gExpr, bExpr". One expression is allowed and drives all
// three channels, because a greyscale field is the most common thing anyone
// types first and asking for it three times is a poor welcome.
// ---------------------------------------------------------------------------
// Showing the language, as opposed to running it.
//
// The editor has to colour what has been typed and to offer a list of what
// there is to type. Both need the same tables the COMPILER uses, and a
// highlighter that keeps its own copy is one that is wrong the first time
// either list changes -- so both read from here, and a name shown as "unknown"
// is exactly one the compiler will refuse.
// ---------------------------------------------------------------------------

enum class Syntax {
  Space,
  Number,
  Variable,     // x y cx cy r a t, and pi
  Function,     // sin, clamp, mix ...
  Operator,
  Bracket,
  Comma,        // the channel separator: worth its own colour
  Unknown,      // a name the compiler will reject
};

struct SyntaxRun {
  std::size_t begin = 0;
  std::size_t end = 0;      // one past the last byte
  Syntax kind = Syntax::Space;
};

// What an operator can put in, as a list to click rather than a syntax to
// recall. Each carries the plain-language hint that goes with it.
struct LanguageEntry {
  const char* name;
  const char* hint;
  bool callable;            // insert with brackets, caret left inside them
};

inline const std::vector<LanguageEntry>& languageVariables() {
  static const std::vector<LanguageEntry> kVars = {
    {"x",  "across the frame, 0 at the left, 1 at the right", false},
    {"y",  "down the frame, 0 at the top, 1 at the bottom", false},
    {"cx", "across from the centre, -1 to 1", false},
    {"cy", "down from the centre, -1 to 1", false},
    {"r",  "distance from the centre", false},
    {"a",  "angle from the centre, in radians", false},
    {"t",  "seconds since the cue was taken", false},
    {"pi", "3.14159...  half a turn, in radians", false},
  };
  return kVars;
}

inline const std::vector<LanguageEntry>& languageFunctions() {
  static const std::vector<LanguageEntry> kFns = {
    {"sin",   "a wave, -1 to 1", true},
    {"cos",   "a wave, a quarter turn ahead of sin", true},
    {"tan",   "steep near a quarter turn", true},
    {"abs",   "drop the sign", true},
    {"floor", "round down to a whole number", true},
    {"fract", "only the fractional part: a sawtooth", true},
    {"sqrt",  "square root", true},
    {"min",   "the smaller of two", true},
    {"max",   "the larger of two", true},
    {"mod",   "remainder: makes a range repeat", true},
    {"pow",   "raise to a power", true},
    {"atan2", "the angle of a direction", true},
    {"step",  "0 below the edge, 1 above it", true},
    {"clamp", "hold a value between two others", true},
    {"mix",   "blend between two, by a third", true},
    {"length", "distance to a point: length(cx, cy) is the radius", true},
    {"smoothstep", "like step, but with a soft edge between two values", true},
    {"if",    "if(test, yes, no) -- picks one of two", true},
    {"sign",  "-1, 0 or 1, whichever way the value leans", true},
    {"exp",   "grows fast; good for glows and falloff", true},
    {"log",   "grows slowly; tames a value that runs away", true},
    {"atan",  "the angle of a slope", true},
  };
  return kFns;
}

// Whitespace, by code point rather than as escapes. Written this way after a
// tool ate the backslashes on the way into this file and left the test
// comparing against a real tab and a real newline, which is an unterminated
// character constant. By value there is nothing left to mangle.
inline bool isSpaceChar(char c) {
  return c == ' ' || c == 0x09 || c == 0x0A || c == 0x0D;
}

// Every run in the source, in order, covering it completely so a caller can
// walk this and draw. Never fails: a character the compiler would reject comes
// back as Unknown rather than stopping the walk, because the whole point is to
// colour text that is still being typed and is therefore usually invalid.
inline std::vector<SyntaxRun> highlight(const std::string& src) {
  // FIRST, the names this source gives itself.
  //
  // Anything of the form `name =` (and not `==`, `<=` or `>=`) declares a
  // value, and every later mention of it is a variable like x or t. Without
  // this pass they came back Unknown, which is the colour that means the
  // compiler will refuse it -- on names the compiler accepts perfectly well.
  //
  // Collected over the WHOLE source before colouring any of it, so a name
  // reads the same on the line that declares it as on the lines that use it.
  std::vector<std::string> declared;
  for (std::size_t k = 0; k < src.size(); ++k) {
    if (src[k] != '=') continue;
    if (k + 1 < src.size() && src[k + 1] == '=') continue;
    if (k > 0 && (src[k - 1] == '<' || src[k - 1] == '>' || src[k - 1] == '=')) {
      continue;
    }
    std::size_t e = k;
    while (e > 0 && isSpaceChar(src[e - 1])) --e;
    std::size_t b = e;
    while (b > 0 && detail::isNameChar(src[b - 1])) --b;
    if (b < e) {
      declared.push_back(src.substr(b, e - b));
    }
  }
  std::vector<SyntaxRun> runs;
  auto push = [&runs](std::size_t begin, std::size_t end, Syntax kind) {
    if (end > begin) runs.push_back({begin, end, kind});
  };
  std::size_t i = 0;
  while (i < src.size()) {
    const char c = src[i];
    const std::size_t start = i;
    if (isSpaceChar(c)) {
      while (i < src.size() && isSpaceChar(src[i])) ++i;
      push(start, i, Syntax::Space);
    } else if ((c >= '0' && c <= '9') || c == '.') {
      while (i < src.size() && ((src[i] >= '0' && src[i] <= '9') ||
                                src[i] == '.')) ++i;
      push(start, i, Syntax::Number);
    } else if (detail::isNameChar(c)) {
      while (i < src.size() && detail::isNameChar(src[i])) ++i;
      const std::string name = src.substr(start, i - start);
      Op op = Op::Add;
      int args = 0;
      Syntax kind = Syntax::Unknown;
      if (detail::varSlot(name) >= 0 || name == "pi") {
        kind = Syntax::Variable;
      } else if (detail::functionOp(name, op, args)) {
        kind = Syntax::Function;
      } else {
        for (const std::string& own : declared) {
          if (own == name) { kind = Syntax::Variable; break; }
        }
      }
      push(start, i, kind);
    } else if (c == '(' || c == ')') {
      ++i;
      push(start, i, Syntax::Bracket);
    } else if (c == ',') {
      ++i;
      push(start, i, Syntax::Comma);
    } else if (std::string("+-*/%^<>").find(c) != std::string::npos) {
      ++i;
      push(start, i, Syntax::Operator);
    } else {
      ++i;
      push(start, i, Syntax::Unknown);
    }
  }
  return runs;
}

// Split on a character, ignoring anything inside parentheses.
inline std::vector<std::string> splitTop(const std::string& source, char sep) {
  std::vector<std::string> parts;
  int depth = 0;
  std::string current;
  for (char c : source) {
    if (c == '(') ++depth;
    if (c == ')') --depth;
    if (c == sep && depth == 0) {
      parts.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  parts.push_back(current);
  return parts;
}

inline std::string trimmed(const std::string& s) {
  std::size_t a = 0, b = s.size();
  while (a < b && isSpaceChar(s[a])) ++a;
  while (b > a && isSpaceChar(s[b - 1])) --b;
  return s.substr(a, b - a);
}

inline CompiledSource compile(const std::string& source) {
  CompiledSource compiled;
  // STATEMENTS, then the channels.
  //
  // Everything before the last semicolon names a value; what follows is the
  // one or three expressions the channels take. A source with no semicolon in
  // it is the whole of the old language, unchanged -- which is why every
  // expression written before this still compiles and still means the same
  // thing.
  std::vector<std::string> statements = splitTop(source, ';');
  while (statements.size() > 1 && trimmed(statements.back()).empty()) {
    statements.pop_back();   // a trailing semicolon is not an empty statement
  }
  for (std::size_t s = 0; s + 1 < statements.size(); ++s) {
    const std::string& statement = statements[s];
    const std::size_t eq = statement.find('=');
    if (eq == std::string::npos) {
      compiled.error = "every line before the last one names a value, "
                       "like  d = length(cx, cy);";
      return compiled;
    }
    const std::string name = trimmed(statement.substr(0, eq));
    if (name.empty()) {
      compiled.error = "a name is missing before '='";
      return compiled;
    }
    for (char c : name) {
      if (!detail::isNameChar(c)) {
        compiled.error = "'" + name + "' is not a name";
        return compiled;
      }
    }
    if (detail::varSlot(name) >= 0 || name == "pi") {
      compiled.error = "'" + name + "' is already part of the language";
      return compiled;
    }
    Op unusedOp = Op::Add; int unusedArgs = 0;
    if (detail::functionOp(name, unusedOp, unusedArgs)) {
      compiled.error = "'" + name + "' is already a function";
      return compiled;
    }
    std::string error;
    // Compiled against the names declared SO FAR, so a value can build on the
    // ones above it and a forward reference is reported rather than silently
    // reading zero.
    if (!compileExpression(statement.substr(eq + 1), compiled.prelude, error,
                           compiled.names)) {
      compiled.error = name + ": " + error;
      return compiled;
    }
    int slot = -1;
    for (std::size_t i = 0; i < compiled.names.size(); ++i) {
      if (compiled.names[i] == name) { slot = static_cast<int>(i); break; }
    }
    if (slot < 0) {
      slot = static_cast<int>(compiled.names.size());
      compiled.names.push_back(name);
    }
    compiled.prelude.push_back({Op::StoreUser, 0.0, slot});
  }

  std::vector<std::string> parts = splitTop(statements.back(), ',');
  if (parts.size() == 1) {
    parts.push_back(parts[0]);
    parts.push_back(parts[0]);
  }
  if (parts.size() != 3) {
    compiled.error = "expected one expression or three separated by commas";
    return compiled;
  }
  for (int c = 0; c < 3; ++c) {
    std::string error;
    if (!compileExpression(parts[c], compiled.channel[c], error,
                           compiled.names)) {
      compiled.error = std::string(c == 0 ? "red: " : c == 1 ? "green: " : "blue: ") + error;
      return compiled;
    }
  }
  return compiled;
}

// Run one compiled channel. The stack is the caller's, reused across pixels so
// nothing allocates in the inner loop.
inline double evaluate(const Program& program, const double (&vars)[7],
                       std::vector<double>& stack,
                       std::vector<double>* names = nullptr) {
  stack.clear();
  auto pop = [&stack]() {
    if (stack.empty()) return 0.0;
    const double v = stack.back();
    stack.pop_back();
    return v;
  };
  for (const Instruction& in : program) {
    switch (in.op) {
      case Op::PushConst: stack.push_back(in.value); break;
      case Op::PushVar:   stack.push_back(vars[in.slot]); break;
      case Op::PushUser:
        stack.push_back(names && in.slot < static_cast<int>(names->size())
                          ? (*names)[in.slot] : 0.0);
        break;
      case Op::StoreUser: {
        const double v = pop();
        if (names && in.slot < static_cast<int>(names->size())) {
          (*names)[in.slot] = v;
        }
        break;
      }
      case Op::Sign:      { const double v = pop();
                            stack.push_back(v < 0 ? -1.0 : (v > 0 ? 1.0 : 0.0)); break; }
      case Op::Exp:       stack.push_back(std::exp(std::min(pop(), 60.0))); break;
      case Op::Log:       { const double v = pop();
                            stack.push_back(v > 1e-12 ? std::log(v) : 0.0); break; }
      case Op::Atan:      stack.push_back(std::atan(pop())); break;
      case Op::Length:    { const double b = pop(), a = pop();
                            stack.push_back(std::sqrt(a * a + b * b)); break; }
      case Op::Smoothstep: { const double v = pop(), hi = pop(), lo = pop();
                            double f = std::fabs(hi - lo) < 1e-9
                                         ? 0.0 : (v - lo) / (hi - lo);
                            f = f < 0 ? 0 : (f > 1 ? 1 : f);
                            stack.push_back(f * f * (3.0 - 2.0 * f)); break; }
      case Op::If:        { const double no = pop(), yes = pop(), cond = pop();
                            stack.push_back(cond > 0.5 ? yes : no); break; }
      case Op::Neg:       stack.push_back(-pop()); break;
      case Op::Sin:       stack.push_back(std::sin(pop())); break;
      case Op::Cos:       stack.push_back(std::cos(pop())); break;
      case Op::Tan:       stack.push_back(std::tan(pop())); break;
      case Op::Abs:       stack.push_back(std::fabs(pop())); break;
      case Op::Floor:     stack.push_back(std::floor(pop())); break;
      case Op::Fract:     { const double v = pop(); stack.push_back(v - std::floor(v)); break; }
      case Op::Sqrt:      stack.push_back(std::sqrt(std::fabs(pop()))); break;
      case Op::Add:       { const double b = pop(), a = pop(); stack.push_back(a + b); break; }
      case Op::Sub:       { const double b = pop(), a = pop(); stack.push_back(a - b); break; }
      case Op::Mul:       { const double b = pop(), a = pop(); stack.push_back(a * b); break; }
      case Op::Div:       { const double b = pop(), a = pop();
                            stack.push_back(std::fabs(b) < 1e-9 ? 0.0 : a / b); break; }
      case Op::Mod:       { const double b = pop(), a = pop();
                            stack.push_back(std::fabs(b) < 1e-9 ? 0.0 : std::fmod(a, b)); break; }
      case Op::Pow:       { const double b = pop(), a = pop();
                            stack.push_back(std::pow(std::fabs(a), b)); break; }
      case Op::Min:       { const double b = pop(), a = pop(); stack.push_back(a < b ? a : b); break; }
      case Op::Max:       { const double b = pop(), a = pop(); stack.push_back(a > b ? a : b); break; }
      case Op::Atan2:     { const double b = pop(), a = pop(); stack.push_back(std::atan2(a, b)); break; }
      case Op::Step:      { const double b = pop(), a = pop(); stack.push_back(b < a ? 0.0 : 1.0); break; }
      case Op::LessThan:  { const double b = pop(), a = pop(); stack.push_back(a < b ? 1.0 : 0.0); break; }
      case Op::GreaterThan: { const double b = pop(), a = pop(); stack.push_back(a > b ? 1.0 : 0.0); break; }
      case Op::Clamp:     { const double hi = pop(), lo = pop(), v = pop();
                            stack.push_back(v < lo ? lo : (v > hi ? hi : v)); break; }
      case Op::Mix:       { const double f = pop(), b = pop(), a = pop();
                            stack.push_back(a + (b - a) * f); break; }
    }
  }
  return stack.empty() ? 0.0 : stack.back();
}

}  // namespace deckboy::code

#endif  // DECKBOY_CORE_CODE_SOURCE_HPP
