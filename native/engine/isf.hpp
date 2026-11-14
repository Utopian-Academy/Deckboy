// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#pragma once
// SHADERS AS A SOURCE (ISF, https://isf.video).
//
// A .fs file is GLSL plus a /*{ JSON }*/ header that names its INPUTS. Here it
// is parsed, wrapped as one GLSL 450 compute shader and (with DECKBOY_ISF)
// compiled at run time: glslang makes SPIR-V (Vulkan takes it as it is) and
// SPIRV-Cross turns that into HLSL (Direct3D 11) and MSL (Metal).
//
// Version 1: ONE pass and NO image inputs (generators). Anything else is
// refused, with a reason a cue can show.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace deckboy::isf {

enum class InputType : int { Float = 0, Color, Bool, Long, Point2D, Event };

struct Input {
  std::string name, label;
  InputType type = InputType::Float;
  int comps = 1;                 // 1 float/bool/long, 2 point2D, 4 colour
  float def[4] = {0, 0, 0, 1};
  float mn[4] = {0, 0, 0, 0};
  float mx[4] = {1, 1, 1, 1};
  std::vector<int> values;       // long: VALUES
  std::vector<std::string> labels;  // long: LABELS
  int offset = -1;               // byte offset in the uniform block (from the SPIR-V)
};

struct Shader {
  std::string path;
  std::string description, credit;
  std::vector<std::string> categories;
  std::vector<Input> inputs;
  // Empty when the shader can run; otherwise why not (refused, parse error,
  // compile error with its line).
  std::string problem;
  std::string glsl;              // the wrapped compute shader
  std::vector<std::uint32_t> spirv;
  std::string hlsl;              // Direct3D 11 (entry "main", cs_5_0)
  std::string msl;               // Metal (entry "main0")
  int uniformBytes = 0;
  std::uint64_t id = 0;          // new for every load of changed text
  bool compiled = false;
  bool ok() const { return problem.empty() && compiled; }
};

// Whether this build can compile shaders at all (DECKBOY_ISF); `why` says so
// when it cannot.
bool compilerAvailable(std::string* why = nullptr);

// Parse and wrap `text` (no compile). `name` is only for messages.
std::shared_ptr<Shader> parseShader(const std::string& text, const std::string& path);
// Compile a parsed shader (SPIR-V, HLSL, MSL) and read the block's offsets.
// Returns false and sets `problem` on failure. Thread-safe.
bool compileShader(Shader& shader);
// Read, parse and compile a file; cached by path, size and modified time.
// Never null: a shader that cannot run carries its `problem`.
std::shared_ptr<const Shader> loadShader(const std::string& path);
// Forget the cache (the EDIT button's next load reads the file again).
void forgetShaders();

// The path a cue names ("pattern://isf:<path>" or "isf:<path>"), or empty.
std::string pathFromCuePath(const std::string& cuePath);
std::string cuePathFor(const std::string& path);

// What a cue keeps for one input.
struct Setting {
  std::string name;
  float v[4] = {0, 0, 0, 1};
};
// The starting value of every input.
void defaultsFor(const Shader& shader, std::vector<float>& flat);   // 4 floats per input

// The uniform block's bytes for one moment. `value` holds four floats per
// input, in the shader's order (already modulated by any LFO).
void packUniforms(const Shader& shader, const std::vector<float>& value, double time, double delta,
                  std::int64_t frame, int width, int height, std::vector<std::uint8_t>& out);

}  // namespace deckboy::isf
