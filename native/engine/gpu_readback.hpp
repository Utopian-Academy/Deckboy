// gpu_readback.hpp -- asynchronous GPU -> CPU frame readback for egress.
//
// This lives OUTSIDE the decoder on purpose. It used to sit in
// libav_decoder.cpp, whose whole namespace is behind DECKBOY_INPROC_DECODE --
// so a build with in-process decode off did not compile the recording path's
// readback at all and failed with "createStagingReadback: identifier not
// found". Reading a rendered frame back is a RENDERER concern; it has nothing
// to do with how media was decoded.
#ifndef DECKBOY_ENGINE_GPU_READBACK_HPP
#define DECKBOY_ENGINE_GPU_READBACK_HPP

#include <cstddef>
#include <cstdint>

#include "../core/sdl_compat.hpp"

namespace deckboy::gpu {

// How many frames deep the asynchronous readback is. Both implementations use
// the same depth, and callers need it: the frame the writer receives is this
// many behind the one just rendered, so anything measuring "am I keeping up"
// has to allow for it or it will report a healthy pipeline as a fault.
inline constexpr int kAsyncReadbackDepth = 3;

// Asynchronous GPU -> CPU readback, for the recording/egress path.
// SDL_RenderReadPixels blocks the render thread until the GPU drains the copy
// (MEASURED: 11.8ms per 1080 frame, 21-24ms at 4K), which is what capped the
// achievable recording rate. These copy into a staging ring and map the oldest
// entry without waiting, so the CPU never stalls. Two frames of latency.
//
// createStagingReadback returns null on a renderer with no asynchronous path --
// today anything that is not D3D11, which means macOS and Linux. Callers MUST
// fall back to SDL_RenderReadPixels; see DEVNOTES for what that costs and what
// closing the gap would actually take.
void* createStagingReadback(SDL_Renderer* renderer, int width, int height);
void destroyStagingReadback(void* handle);

// False when no frame was ready this tick (ring priming, or the GPU is still
// busy) -- not an error; the caller simply reuses the previous picture.
bool stagingReadbackFrame(void* handle, SDL_Texture* source,
                          std::uint8_t* out, std::size_t outBytes,
                          int width, int height);

}  // namespace deckboy::gpu

#endif  // DECKBOY_ENGINE_GPU_READBACK_HPP
