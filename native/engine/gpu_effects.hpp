// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Deckboy Contributors
#pragma once
// PICTURE EFFECTS ON THE GRAPHICS CARD.
//
// The CPU effects (core/cue_effects.hpp) stay the reference and the fallback:
// a stack runs here only when EVERY effect in it has a GPU version and the
// output's renderer is one this can reach (Direct3D 11, or SDL_GPU on Metal or
// Direct3D 12 -- Vulkan too once the build has SPIR-V). Anything else, and
// DECKBOY_CPU_EFFECTS=1, takes the CPU path exactly as before.
//
// tools/check_gpu_effects.py compares the two on the same pictures.
#include <cstdint>
#include <string>
#include <vector>

#include "../core/cue_effects.hpp"

struct SDL_Renderer;
struct SDL_Texture;

namespace deckboy::gpu {

struct GpuEffects;

// Whether every effect in this (already LFO-modulated) stack has a GPU
// version. Bypassed and zero-amount effects do not count: they do nothing on
// either path.
bool gpuEffectsSupportStack(const std::vector<deckboy::effects::CueEffect>& stack);
// The effect kinds that have one, for the inspector and the checks.
bool gpuEffectKindSupported(deckboy::effects::CueEffectKind kind);

// Null when the renderer is not one this can drive, or its shaders would not
// build (the reason goes to the log once).
GpuEffects* createGpuEffects(SDL_Renderer* renderer);
void destroyGpuEffects(GpuEffects* g);
std::string gpuEffectsBackend(const GpuEffects* g);

// Runs `stack` over an RGBA picture (w x h) for `slot` (one per playlist) and
// returns a texture holding the result, owned by `g` and valid until the next
// run for the same slot. Null on any failure: the caller then runs the CPU
// path, so a GPU fault costs speed, never the picture.
SDL_Texture* runGpuEffects(GpuEffects* g, int slot, const std::uint8_t* rgba, int w, int h,
                           const std::vector<deckboy::effects::CueEffect>& stack,
                           const deckboy::effects::CueEffectContext& ctx);

// The same, straight from a decoded NV12 picture already on the GPU (`nv12`,
// an SDL NV12 texture on the output's renderer: the zero-copy wrap, or an
// NV12 upload). It is converted with `colorspace`'s own matrix and range and
// reduced by `shrink` on the card, so the frame never comes back to the CPU.
// Direct3D 11 only for now; null elsewhere (the caller then uses RGBA).
SDL_Texture* runGpuEffectsNv12(GpuEffects* g, int slot, SDL_Texture* nv12, int srcW, int srcH, int shrink,
                               unsigned colorspace, const std::vector<deckboy::effects::CueEffect>& stack,
                               const deckboy::effects::CueEffectContext& ctx);
// Whether runGpuEffectsNv12 can run on this one.
bool gpuEffectsTakeNv12(const GpuEffects* g);

// The last result for `slot`, read back to the CPU (for a transition, which
// needs the outgoing picture's pixels). Waits for the GPU; once per cue change.
bool readGpuEffectsResult(GpuEffects* g, int slot, std::vector<std::uint8_t>& rgba, int& w, int& h);
void releaseGpuEffectsSlot(GpuEffects* g, int slot);

// Compile the Direct3D 11 shaders now, on any thread, so the first effect does
// not. The app calls it on a background thread at launch.
void warmGpuEffectsShaders();

}  // namespace deckboy::gpu
