// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#include "isf.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

#ifdef DECKBOY_ISF
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#if defined(DECKBOY_ISF_RESOURCE_LIMITS)
#include <glslang/Public/ResourceLimits.h>
#endif
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>
#endif

namespace deckboy::isf {
namespace {

// ---------------------------------------------------------------------------
// A small tolerant JSON reader: ISF headers are written by hand, so a
// trailing comma or a comment must not sink a shader.
struct Json {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> a;
  std::vector<std::pair<std::string, Json>> o;
  const Json* get(const char* key) const {
    for (const auto& kv : o)
      if (kv.first == key) return &kv.second;
    return nullptr;
  }
};

class JsonReader {
 public:
  explicit JsonReader(const std::string& t) : t_(t) {}
  bool parse(Json& out) {
    if (!value(out)) return false;
    ws();
    return true;
  }
  std::string error;

 private:
  const std::string& t_;
  std::size_t i_ = 0;
  void ws() {
    for (;;) {
      while (i_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[i_]))) ++i_;
      if (t_.compare(i_, 2, "//") == 0) {
        while (i_ < t_.size() && t_[i_] != '\n') ++i_;
      } else if (t_.compare(i_, 2, "/*") == 0) {
        const auto e = t_.find("*/", i_ + 2);
        i_ = e == std::string::npos ? t_.size() : e + 2;
      } else {
        return;
      }
    }
  }
  bool fail(const std::string& m) {
    if (error.empty()) {
      int line = 1;
      for (std::size_t k = 0; k < i_ && k < t_.size(); ++k) line += t_[k] == '\n';
      error = m + " (header line " + std::to_string(line) + ")";
    }
    return false;
  }
  bool str(std::string& out) {
    ++i_;
    while (i_ < t_.size() && t_[i_] != '"') {
      if (t_[i_] == '\\' && i_ + 1 < t_.size()) {
        ++i_;
        switch (t_[i_]) {
          case 'n': out += '\n'; break;
          case 't': out += '\t'; break;
          case 'u': out += '?'; i_ += 4; break;
          default: out += t_[i_]; break;
        }
      } else {
        out += t_[i_];
      }
      ++i_;
    }
    if (i_ >= t_.size()) return fail("unterminated string");
    ++i_;
    return true;
  }
  bool value(Json& v) {
    ws();
    if (i_ >= t_.size()) return fail("unexpected end");
    const char c = t_[i_];
    if (c == '{') {
      v.type = Json::Obj;
      ++i_;
      for (;;) {
        ws();
        if (i_ < t_.size() && t_[i_] == '}') { ++i_; return true; }
        if (i_ >= t_.size() || t_[i_] != '"') return fail("expected a name");
        std::string key;
        if (!str(key)) return false;
        ws();
        if (i_ >= t_.size() || t_[i_] != ':') return fail("expected ':'");
        ++i_;
        Json child;
        if (!value(child)) return false;
        v.o.emplace_back(std::move(key), std::move(child));
        ws();
        if (i_ < t_.size() && t_[i_] == ',') ++i_;
      }
    }
    if (c == '[') {
      v.type = Json::Arr;
      ++i_;
      for (;;) {
        ws();
        if (i_ < t_.size() && t_[i_] == ']') { ++i_; return true; }
        Json child;
        if (!value(child)) return false;
        v.a.push_back(std::move(child));
        ws();
        if (i_ < t_.size() && t_[i_] == ',') ++i_;
      }
    }
    if (c == '"') {
      v.type = Json::Str;
      return str(v.s);
    }
    if (t_.compare(i_, 4, "true") == 0) { v.type = Json::Bool; v.b = true; i_ += 4; return true; }
    if (t_.compare(i_, 5, "false") == 0) { v.type = Json::Bool; v.b = false; i_ += 5; return true; }
    if (t_.compare(i_, 4, "null") == 0) { i_ += 4; return true; }
    char* end = nullptr;
    v.n = std::strtod(t_.c_str() + i_, &end);
    if (end == t_.c_str() + i_) return fail("unexpected character");
    v.type = Json::Num;
    i_ = static_cast<std::size_t>(end - t_.c_str());
    return true;
  }
};

float numOf(const Json* j, float fallback) {
  if (!j) return fallback;
  if (j->type == Json::Num) return static_cast<float>(j->n);
  if (j->type == Json::Bool) return j->b ? 1.0f : 0.0f;
  return fallback;
}

// Identifier-wise replace.
std::string replaceToken(const std::string& text, const std::string& from, const std::string& to) {
  std::string out;
  out.reserve(text.size());
  auto isId = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
  std::size_t i = 0;
  while (i < text.size()) {
    if (text.compare(i, from.size(), from) == 0 && (i == 0 || !isId(text[i - 1])) &&
        (i + from.size() >= text.size() || !isId(text[i + from.size()]))) {
      out += to;
      i += from.size();
    } else {
      out += text[i++];
    }
  }
  return out;
}

bool validName(const std::string& n) {
  if (n.empty() || std::isdigit(static_cast<unsigned char>(n[0]))) return false;
  for (char c : n)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
  return n.find("__") == std::string::npos && n.rfind("gl_", 0) != 0 && n.rfind("isf_", 0) != 0;
}

std::mutex gLock;
std::map<std::string, std::pair<std::string, std::shared_ptr<const Shader>>> gCache;   // path -> stamp, shader
std::uint64_t gNextId = 1;

}  // namespace

bool compilerAvailable(std::string* why) {
#ifdef DECKBOY_ISF
  (void) why;
  return true;
#else
  if (why) *why = "this build was made without shader support (DECKBOY_ISF is off)";
  return false;
#endif
}

std::string pathFromCuePath(const std::string& cuePath) {
  std::string v = cuePath;
  if (v.rfind("pattern://", 0) == 0) v = v.substr(10);
  std::size_t b = 0;
  while (b < v.size() && std::isspace(static_cast<unsigned char>(v[b]))) ++b;
  v = v.substr(b);
  if (v.size() >= 4 && (v[0] == 'i' || v[0] == 'I') && (v[1] == 's' || v[1] == 'S') &&
      (v[2] == 'f' || v[2] == 'F') && v[3] == ':') {
    return v.substr(4);
  }
  return {};
}

std::string cuePathFor(const std::string& path) { return "pattern://isf:" + path; }

std::shared_ptr<Shader> parseShader(const std::string& text, const std::string& path) {
  auto sh = std::make_shared<Shader>();
  sh->path = path;
  // The header: the first /* { ... } */ comment, blanked (newlines kept) so
  // the GLSL's line numbers are the file's.
  std::string body = text;
  std::string header;
  const auto open = text.find("/*");
  std::size_t at = open == std::string::npos ? std::string::npos : open + 2;
  while (at != std::string::npos && at < text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at;
  if (open == std::string::npos || at >= text.size() || text[at] != '{') {
    sh->problem = "no /*{ ... }*/ header at the top of the file";
    return sh;
  }
  const auto close = text.find("*/", open + 2);
  if (close == std::string::npos) {
    sh->problem = "the header comment is never closed";
    return sh;
  }
  header = text.substr(at, close - at);
  for (std::size_t k = open; k < close + 2; ++k)
    if (body[k] != '\n') body[k] = ' ';
  Json root;
  JsonReader reader(header);
  if (!reader.parse(root) || root.type != Json::Obj) {
    sh->problem = "the header is not valid JSON: " + (reader.error.empty() ? std::string("not an object") : reader.error);
    return sh;
  }
  if (const Json* d = root.get("DESCRIPTION"); d && d->type == Json::Str) sh->description = d->s;
  if (const Json* d = root.get("CREDIT"); d && d->type == Json::Str) sh->credit = d->s;
  if (const Json* c = root.get("CATEGORIES"); c && c->type == Json::Arr)
    for (const auto& e : c->a)
      if (e.type == Json::Str) sh->categories.push_back(e.s);
  if (const Json* p = root.get("PASSES"); p && p->type == Json::Arr && p->a.size() > 1) {
    sh->problem = "this shader has " + std::to_string(p->a.size()) +
                  " passes; only single-pass shaders run so far";
    return sh;
  }
  if (const Json* p = root.get("PASSES"); p && p->type == Json::Arr && !p->a.empty()) {
    if (const Json* per = p->a[0].get("PERSISTENT"); per && per->type == Json::Bool && per->b) {
      sh->problem = "this shader keeps a picture from frame to frame; that does not run yet";
      return sh;
    }
  }
  if (const Json* in = root.get("INPUTS"); in && in->type == Json::Arr) {
    for (const auto& e : in->a) {
      Input input;
      const Json* name = e.get("NAME");
      const Json* type = e.get("TYPE");
      if (!name || name->type != Json::Str || !type || type->type != Json::Str) {
        sh->problem = "an input has no NAME or TYPE";
        return sh;
      }
      input.name = name->s;
      if (!validName(input.name)) {
        sh->problem = "the input name \"" + input.name + "\" cannot be used in GLSL";
        return sh;
      }
      if (const Json* l = e.get("LABEL"); l && l->type == Json::Str) input.label = l->s;
      if (input.label.empty()) input.label = input.name;
      const std::string& t = type->s;
      if (t == "float") { input.type = InputType::Float; input.comps = 1; }
      else if (t == "color") { input.type = InputType::Color; input.comps = 4; }
      else if (t == "bool") { input.type = InputType::Bool; input.comps = 1; }
      else if (t == "long") { input.type = InputType::Long; input.comps = 1; }
      else if (t == "point2D") { input.type = InputType::Point2D; input.comps = 2; }
      else if (t == "event") { input.type = InputType::Event; input.comps = 1; }
      else if (t == "image" || t == "audio" || t == "audioFFT") {
        sh->problem = "the input \"" + input.name + "\" is a" + (t == "image" ? "n image" : "n audio") +
                      " input; only shaders that make a picture on their own (no image inputs) run so far";
        return sh;
      } else {
        sh->problem = "the input \"" + input.name + "\" has a type this does not know: " + t;
        return sh;
      }
      const Json* def = e.get("DEFAULT");
      const Json* mn = e.get("MIN");
      const Json* mx = e.get("MAX");
      if (input.type == InputType::Float) {
        input.mn[0] = numOf(mn, 0.0f);
        input.mx[0] = numOf(mx, 1.0f);
        input.def[0] = numOf(def, input.mn[0]);
      } else if (input.type == InputType::Color) {
        for (int c = 0; c < 4; ++c) input.def[c] = c == 3 ? 1.0f : 0.0f;
        if (def && def->type == Json::Arr)
          for (int c = 0; c < 4 && c < static_cast<int>(def->a.size()); ++c) input.def[c] = numOf(&def->a[static_cast<std::size_t>(c)], input.def[c]);
        for (int c = 0; c < 4; ++c) { input.mn[c] = 0; input.mx[c] = 1; }
      } else if (input.type == InputType::Point2D) {
        for (int c = 0; c < 2; ++c) {
          input.def[c] = 0;
          input.mn[c] = mn && mn->type == Json::Arr && c < static_cast<int>(mn->a.size()) ? numOf(&mn->a[static_cast<std::size_t>(c)], 0) : 0;
          input.mx[c] = mx && mx->type == Json::Arr && c < static_cast<int>(mx->a.size()) ? numOf(&mx->a[static_cast<std::size_t>(c)], 1) : 1;
        }
        if (def && def->type == Json::Arr)
          for (int c = 0; c < 2 && c < static_cast<int>(def->a.size()); ++c) input.def[c] = numOf(&def->a[static_cast<std::size_t>(c)], 0);
      } else if (input.type == InputType::Long) {
        if (const Json* v = e.get("VALUES"); v && v->type == Json::Arr)
          for (const auto& x : v->a) input.values.push_back(static_cast<int>(numOf(&x, 0)));
        if (const Json* l = e.get("LABELS"); l && l->type == Json::Arr)
          for (const auto& x : l->a) input.labels.push_back(x.type == Json::Str ? x.s : std::string());
        if (input.values.empty()) {
          const int count = std::max<int>(1, static_cast<int>(input.labels.size()));
          for (int k = 0; k < count; ++k) input.values.push_back(k);
        }
        while (input.labels.size() < input.values.size()) input.labels.push_back(std::to_string(input.values[input.labels.size()]));
        input.def[0] = numOf(def, static_cast<float>(input.values[0]));
      } else {   // bool, event
        input.def[0] = numOf(def, 0.0f) != 0.0f ? 1.0f : 0.0f;
      }
      sh->inputs.push_back(std::move(input));
    }
  }

  // The wrapped compute shader. The block is std140; its members are the
  // inputs by their own names, so the shader reads them as globals.
  std::ostringstream g;
  g << "#version 450\n"
       "layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;\n"
       "layout(set = 1, binding = 0, rgba8) uniform writeonly image2D isf_out;\n"
       "layout(std140, set = 2, binding = 0) uniform ISFBlock {\n"
       "  float TIME;\n  float TIMEDELTA;\n  int FRAMEINDEX;\n  int PASSINDEX;\n"
       "  vec2 RENDERSIZE;\n  vec4 DATE;\n";
  for (const auto& in : sh->inputs) {
    switch (in.type) {
      case InputType::Float: g << "  float " << in.name << ";\n"; break;
      case InputType::Color: g << "  vec4 " << in.name << ";\n"; break;
      case InputType::Point2D: g << "  vec2 " << in.name << ";\n"; break;
      case InputType::Long: g << "  int " << in.name << ";\n"; break;
      default: g << "  int isf_u_" << in.name << ";\n"; break;
    }
  }
  g << "};\n";
  for (const auto& in : sh->inputs)
    if (in.type == InputType::Bool || in.type == InputType::Event)
      g << "#define " << in.name << " (isf_u_" << in.name << " != 0)\n";
  g << "vec4 isf_FragColor = vec4(0.0);\n"
       "vec4 isf_FragCoord = vec4(0.0);\n"
       "vec2 isf_FragNormCoord = vec2(0.0);\n"
       "vec2 vv_FragNormCoord = vec2(0.0);\n";
  std::string shaderText = body;
  shaderText = replaceToken(shaderText, "gl_FragColor", "isf_FragColor");
  shaderText = replaceToken(shaderText, "gl_FragCoord", "isf_FragCoord");
  shaderText = replaceToken(shaderText, "main", "isf_shader_main");
  shaderText = replaceToken(shaderText, "texture2D", "texture");
  // The shader's own lines keep the file's numbers.
  g << "#line 1\n" << shaderText << "\n#line 100000\n"
       "void main() {\n"
       "  ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
       "  if (p.x >= int(RENDERSIZE.x) || p.y >= int(RENDERSIZE.y)) return;\n"
       "  isf_FragCoord = vec4(float(p.x) + 0.5, RENDERSIZE.y - float(p.y) - 0.5, 0.0, 1.0);\n"
       "  isf_FragNormCoord = isf_FragCoord.xy / RENDERSIZE;\n"
       "  vv_FragNormCoord = isf_FragNormCoord;\n"
       "  isf_shader_main();\n"
       "  imageStore(isf_out, p, clamp(isf_FragColor, 0.0, 1.0));\n"
       "}\n";
  sh->glsl = g.str();
  return sh;
}

#ifdef DECKBOY_ISF
namespace {
std::mutex gCompileLock;
bool gInit = false;

#if !defined(DECKBOY_ISF_RESOURCE_LIMITS)
// glslang's default limits, for a build whose glslang has no limits library.
TBuiltInResource makeLimits() {
  TBuiltInResource r {};
  r.maxLights = 32; r.maxClipPlanes = 6; r.maxTextureUnits = 32; r.maxTextureCoords = 32;
  r.maxVertexAttribs = 64; r.maxVertexUniformComponents = 4096; r.maxVaryingFloats = 64;
  r.maxVertexTextureImageUnits = 32; r.maxCombinedTextureImageUnits = 80; r.maxTextureImageUnits = 32;
  r.maxFragmentUniformComponents = 4096; r.maxDrawBuffers = 32; r.maxVertexUniformVectors = 128;
  r.maxVaryingVectors = 8; r.maxFragmentUniformVectors = 16; r.maxVertexOutputVectors = 16;
  r.maxFragmentInputVectors = 15; r.minProgramTexelOffset = -8; r.maxProgramTexelOffset = 7;
  r.maxClipDistances = 8; r.maxComputeWorkGroupCountX = 65535; r.maxComputeWorkGroupCountY = 65535;
  r.maxComputeWorkGroupCountZ = 65535; r.maxComputeWorkGroupSizeX = 1024; r.maxComputeWorkGroupSizeY = 1024;
  r.maxComputeWorkGroupSizeZ = 64; r.maxComputeUniformComponents = 1024; r.maxComputeTextureImageUnits = 16;
  r.maxComputeImageUniforms = 8; r.maxComputeAtomicCounters = 8; r.maxComputeAtomicCounterBuffers = 1;
  r.maxVaryingComponents = 60; r.maxVertexOutputComponents = 64; r.maxGeometryInputComponents = 64;
  r.maxGeometryOutputComponents = 128; r.maxFragmentInputComponents = 128; r.maxImageUnits = 8;
  r.maxCombinedImageUnitsAndFragmentOutputs = 8; r.maxCombinedShaderOutputResources = 8;
  r.maxImageSamples = 0; r.maxVertexImageUniforms = 0; r.maxTessControlImageUniforms = 0;
  r.maxTessEvaluationImageUniforms = 0; r.maxGeometryImageUniforms = 0; r.maxFragmentImageUniforms = 8;
  r.maxCombinedImageUniforms = 8; r.maxGeometryTextureImageUnits = 16; r.maxGeometryOutputVertices = 256;
  r.maxGeometryTotalOutputComponents = 1024; r.maxGeometryUniformComponents = 1024;
  r.maxGeometryVaryingComponents = 64; r.maxTessControlInputComponents = 128;
  r.maxTessControlOutputComponents = 128; r.maxTessControlTextureImageUnits = 16;
  r.maxTessControlUniformComponents = 1024; r.maxTessControlTotalOutputComponents = 4096;
  r.maxTessEvaluationInputComponents = 128; r.maxTessEvaluationOutputComponents = 128;
  r.maxTessEvaluationTextureImageUnits = 16; r.maxTessEvaluationUniformComponents = 1024;
  r.maxTessPatchComponents = 120; r.maxPatchVertices = 32; r.maxTessGenLevel = 64; r.maxViewports = 16;
  r.maxVertexAtomicCounters = 0; r.maxTessControlAtomicCounters = 0; r.maxTessEvaluationAtomicCounters = 0;
  r.maxGeometryAtomicCounters = 0; r.maxFragmentAtomicCounters = 8; r.maxCombinedAtomicCounters = 8;
  r.maxAtomicCounterBindings = 1; r.maxVertexAtomicCounterBuffers = 0; r.maxTessControlAtomicCounterBuffers = 0;
  r.maxTessEvaluationAtomicCounterBuffers = 0; r.maxGeometryAtomicCounterBuffers = 0;
  r.maxFragmentAtomicCounterBuffers = 1; r.maxCombinedAtomicCounterBuffers = 1;
  r.maxAtomicCounterBufferSize = 16384; r.maxTransformFeedbackBuffers = 4;
  r.maxTransformFeedbackInterleavedComponents = 64; r.maxCullDistances = 8;
  r.maxCombinedClipAndCullDistances = 8; r.maxSamples = 4;
  r.limits.nonInductiveForLoops = true; r.limits.whileLoops = true; r.limits.doWhileLoops = true;
  r.limits.generalUniformIndexing = true; r.limits.generalAttributeMatrixVectorIndexing = true;
  r.limits.generalVaryingIndexing = true; r.limits.generalSamplerIndexing = true;
  r.limits.generalVariableIndexing = true; r.limits.generalConstantMatrixVectorIndexing = true;
  return r;
}
#endif
}  // namespace
#endif

bool compileShader(Shader& sh) {
  if (!sh.problem.empty()) return false;
#ifndef DECKBOY_ISF
  sh.problem = "this build was made without shader support (DECKBOY_ISF is off)";
  return false;
#else
  std::lock_guard<std::mutex> hold(gCompileLock);
  if (!gInit) {
    glslang::InitializeProcess();
    gInit = true;
  }
#if defined(DECKBOY_ISF_RESOURCE_LIMITS)
  const TBuiltInResource* limits = GetDefaultResources();
#else
  static const TBuiltInResource limitsValue = makeLimits();
  const TBuiltInResource* limits = &limitsValue;
#endif
  glslang::TShader shader(EShLangCompute);
  const char* src = sh.glsl.c_str();
  shader.setStrings(&src, 1);
  shader.setEnvInput(glslang::EShSourceGlsl, EShLangCompute, glslang::EShClientVulkan, 100);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
  if (!shader.parse(limits, 450, false, static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules))) {
    std::string log = shader.getInfoLog();
    // "ERROR: 0:12: ..." -> "line 12: ..." (the file's own line).
    std::string out;
    std::istringstream lines(log);
    std::string line;
    while (std::getline(lines, line)) {
      if (line.rfind("ERROR: ", 0) != 0) continue;
      std::string rest = line.substr(7);
      const auto c1 = rest.find(':');
      const auto c2 = c1 == std::string::npos ? c1 : rest.find(':', c1 + 1);
      if (c2 != std::string::npos) {
        const std::string num = rest.substr(c1 + 1, c2 - c1 - 1);
        const int n = std::atoi(num.c_str());
        rest = (n >= 100000 ? std::string("the wrapper") : "line " + num) + ":" + rest.substr(c2 + 1);
      }
      out += (out.empty() ? "" : " | ") + rest;
      if (out.size() > 400) break;
    }
    sh.problem = "compile error, " + (out.empty() ? log : out);
    return false;
  }
  glslang::TProgram program;
  program.addShader(&shader);
  if (!program.link(static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules))) {
    sh.problem = std::string("link error, ") + program.getInfoLog();
    return false;
  }
  sh.spirv.clear();
  glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), sh.spirv);
  if (sh.spirv.empty()) {
    sh.problem = "no SPIR-V came out";
    return false;
  }
  try {
    // The block's offsets, from the SPIR-V itself.
    spirv_cross::Compiler reflect(sh.spirv);
    const auto res = reflect.get_shader_resources();
    if (res.uniform_buffers.empty()) {
      sh.problem = "the wrapped shader has no uniform block";
      return false;
    }
    const auto& ubo = res.uniform_buffers[0];
    const auto& type = reflect.get_type(ubo.base_type_id);
    sh.uniformBytes = static_cast<int>(reflect.get_declared_struct_size(type));
    for (auto& in : sh.inputs) {
      const std::string want = (in.type == InputType::Bool || in.type == InputType::Event) ? "isf_u_" + in.name : in.name;
      in.offset = -1;
      for (std::uint32_t m = 0; m < type.member_types.size(); ++m)
        if (reflect.get_member_name(ubo.base_type_id, m) == want)
          in.offset = static_cast<int>(reflect.type_struct_member_offset(type, m));
    }
    spirv_cross::CompilerHLSL hlsl(sh.spirv);
    spirv_cross::CompilerHLSL::Options ho;
    ho.shader_model = 50;
    hlsl.set_hlsl_options(ho);
    sh.hlsl = hlsl.compile();
    spirv_cross::CompilerMSL msl(sh.spirv);
    spirv_cross::CompilerMSL::Options mo;
    mo.set_msl_version(2, 0);
    msl.set_msl_options(mo);
    sh.msl = msl.compile();
  } catch (const std::exception& e) {
    sh.problem = std::string("cross-compile error, ") + e.what();
    return false;
  }
  sh.compiled = true;
  return true;
#endif
}

std::shared_ptr<const Shader> loadShader(const std::string& path) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const auto size = fs::file_size(fs::u8path(path), ec);
  const auto when = ec ? fs::file_time_type() : fs::last_write_time(fs::u8path(path), ec);
  const std::string stamp = std::to_string(static_cast<long long>(size)) + ":" +
                            std::to_string(static_cast<long long>(when.time_since_epoch().count()));
  {
    std::lock_guard<std::mutex> hold(gLock);
    auto it = gCache.find(path);
    if (it != gCache.end() && it->second.first == stamp) return it->second.second;
  }
  std::shared_ptr<Shader> sh;
  std::ifstream in(fs::u8path(path), std::ios::binary);
  if (!in) {
    sh = std::make_shared<Shader>();
    sh->path = path;
    sh->problem = "cannot open " + path;
  } else {
    std::stringstream ss;
    ss << in.rdbuf();
    sh = parseShader(ss.str(), path);
    if (sh->problem.empty()) compileShader(*sh);
  }
  {
    std::lock_guard<std::mutex> hold(gLock);
    sh->id = gNextId++;
    gCache[path] = {stamp, sh};
  }
  return sh;
}

void forgetShaders() {
  std::lock_guard<std::mutex> hold(gLock);
  gCache.clear();
}

void defaultsFor(const Shader& shader, std::vector<float>& flat) {
  flat.assign(shader.inputs.size() * 4, 0.0f);
  for (std::size_t i = 0; i < shader.inputs.size(); ++i)
    for (int c = 0; c < 4; ++c) flat[i * 4 + static_cast<std::size_t>(c)] = shader.inputs[i].def[c];
}

void packUniforms(const Shader& shader, const std::vector<float>& value, double time, double delta,
                  std::int64_t frame, int width, int height, std::vector<std::uint8_t>& out) {
  out.assign(static_cast<std::size_t>(std::max(shader.uniformBytes, 48)), 0);
  auto putF = [&](int off, float v) { if (off >= 0 && off + 4 <= static_cast<int>(out.size())) std::memcpy(out.data() + off, &v, 4); };
  auto putI = [&](int off, std::int32_t v) { if (off >= 0 && off + 4 <= static_cast<int>(out.size())) std::memcpy(out.data() + off, &v, 4); };
  putF(0, static_cast<float>(time));
  putF(4, static_cast<float>(delta));
  putI(8, static_cast<std::int32_t>(frame));
  putI(12, 0);
  putF(16, static_cast<float>(width));
  putF(20, static_cast<float>(height));
  const std::time_t now = std::time(nullptr);
  std::tm local {};
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  putF(32, static_cast<float>(local.tm_year + 1900));
  putF(36, static_cast<float>(local.tm_mon + 1));
  putF(40, static_cast<float>(local.tm_mday));
  putF(44, static_cast<float>(local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec));
  for (std::size_t i = 0; i < shader.inputs.size(); ++i) {
    const Input& in = shader.inputs[i];
    if (in.offset < 0) continue;
    const float* v = i * 4 + 3 < value.size() ? &value[i * 4] : in.def;
    switch (in.type) {
      case InputType::Float: putF(in.offset, v[0]); break;
      case InputType::Color:
        for (int c = 0; c < 4; ++c) putF(in.offset + c * 4, v[c]);
        break;
      case InputType::Point2D:
        putF(in.offset, v[0]);
        putF(in.offset + 4, v[1]);
        break;
      case InputType::Long: putI(in.offset, static_cast<std::int32_t>(std::lround(v[0]))); break;
      default: putI(in.offset, v[0] >= 0.5f ? 1 : 0); break;
    }
  }
}

}  // namespace deckboy::isf
