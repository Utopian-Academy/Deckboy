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
// halfFloat: the source is RGBA half floats (an HDR output's compositor,
// SDL_PIXELFORMAT_RGBA64_FLOAT), read back 8 bytes a pixel as they are.
// Otherwise BGRA, 4 bytes a pixel.
void* createStagingReadback(SDL_Renderer* renderer, int width, int height, bool halfFloat = false);
void destroyStagingReadback(void* handle);

// False when no frame was ready this tick (ring priming, or the GPU is still
// busy) -- not an error; the caller simply reuses the previous picture.
bool stagingReadbackFrame(void* handle, SDL_Texture* source,
                          std::uint8_t* out, std::size_t outBytes,
                          int width, int height);

// AN HDR OUTPUT'S PICTURE AS P010, MADE ON THE GPU. A compute shader reads the
// float (scRGB, half-float) compositor and writes 10-bit BT.2020 NCL Y'CbCr,
// limited range, 4:2:0, in PQ or HLG -- core/hdr_to_sdr.hpp's
// HdrEncoder::convertHalfToP010, which stays the fallback -- and the result
// comes back through the same kind of staging ring: never waited on, a few
// frames behind, false meaning "nothing new yet". MEASURED on the CPU: 12-19 ms
// a 4K frame on 28 threads held a 4K HLG stream under 50 fps.
// Direct3D 11 only (null elsewhere, and where the shader cannot be compiled);
// width and height even. `out` gets the luma plane then the chroma plane.
void* createHdrP010Readback(SDL_Renderer* renderer, int width, int height);
void destroyHdrP010Readback(void* handle);
bool hdrP010ReadbackFrame(void* handle, SDL_Texture* source, float whiteNits, bool hlg,
                          std::uint8_t* out, std::size_t outBytes);

}  // namespace deckboy::gpu

#endif  // DECKBOY_ENGINE_GPU_READBACK_HPP
