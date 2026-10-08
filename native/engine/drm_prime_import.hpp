// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ── A DECODER'S FRAME, SHOWN WITHOUT BEING COPIED (Linux, OpenGL ES) ─────────
//
// On a Raspberry Pi the hardware decoder writes into buffers the CPU can only
// read slowly. Measured on a Pi 3, Totoro 1080p24: decoding a frame took
// 0.6 ms and READING it out took 44-50 ms -- longer than the 41.7 ms a frame
// lasts -- so a copy-based player can never keep up there, however fast the
// copy. mpv and Kodi never read those buffers; they hand them to the GPU.
//
// This does the same: a DRM-PRIME frame (a dmabuf per plane) becomes two
// EGLImages, one for brightness (R8) and one for colour (GR88), bound to GL
// textures and wrapped as one NV12 SDL texture -- so SDL's own NV12 shader
// draws it, with the same colour maths as every other NV12 frame. A 3-plane
// 4:2:0 frame becomes an IYUV texture the same way.
//
// EVERY FRAME IS IMPORTED AFRESH. This used to cache one import per decoder
// buffer and reuse it when the decoder recycled that buffer -- and on the Pi 3
// the picture froze on the film's first frames while the counters reported a
// perfect 24 fps. vc4 cannot sample a linear buffer, so the driver samples a
// copy made at import, and a reused import never sees the decoder's new
// contents. Kodi and mpv import per frame for the same reason. The few kept
// here are the frame on screen and the ones just before it, still owned until
// replaced: the importer owns the EGLImages, the GL textures and the SDL
// textures (SDL never deletes a GL texture it was handed). clear() must run
// while the renderer still exists.

#if defined(__linux__)

#include <SDL3/SDL.h>
#include <SDL3/SDL_egl.h>
#include <SDL3/SDL_opengles2.h>

extern "C" {
#include <libavutil/hwcontext_drm.h>
}

#include <cstdint>
#include <vector>

namespace deckboy::engine {

class DrmPrimeImporter {
 public:
  ~DrmPrimeImporter() { clear(); }

  // The SDL texture showing this frame, or null when it cannot be imported
  // (the caller then shows nothing new rather than a wrong picture).
  SDL_Texture* textureFor(SDL_Renderer* renderer, const AVDRMFrameDescriptor* desc,
                          int width, int height, SDL_Colorspace colorspace) {
    if (!renderer || !desc || desc->nb_layers < 1 || desc->nb_objects < 1 || !loadEntryPoints()) {
      return nullptr;
    }
    renderer_ = renderer;
    const Key key = keyFor(desc, width, height);
    Entry fresh;
    fresh.key = key;
    fresh.lastUsed = ++tick_;
    if (!import(renderer, desc, width, height, colorspace, fresh)) {
      // Said once: a decoder that hands over buffers this GPU cannot take
      // would otherwise fail silently on every frame.
      if (!warned_) {
        warned_ = true;
        SDL_Log("zero-copy display: could not import a frame (%d planes, fourcc 0x%08x, modifier 0x%llx, "
                "egl error 0x%x): %s",
                desc->layers[0].nb_planes, static_cast<unsigned>(desc->layers[0].format),
                static_cast<unsigned long long>(desc->objects[0].format_modifier),
                eglGetError_ ? eglGetError_() : 0u, SDL_GetError());
      }
      release(fresh);
      return nullptr;
    }
    if (cache_.size() >= kMaxCached) {
      std::size_t oldest = 0;
      for (std::size_t i = 1; i < cache_.size(); ++i) {
        if (cache_[i].lastUsed < cache_[oldest].lastUsed) oldest = i;
      }
      release(cache_[oldest]);
      cache_.erase(cache_.begin() + static_cast<std::ptrdiff_t>(oldest));
    }
    cache_.push_back(fresh);
    return cache_.back().texture;
  }

  // Everything imported: the decoder's pool is changing (a new cue, a seek
  // that reopens) or the renderer is going away.
  void clear() {
    for (Entry& e : cache_) release(e);
    cache_.clear();
  }

  // Whether this renderer can take imported frames at all: OpenGL ES, with
  // the EGL dmabuf import extension. Asked before the decoder is told to hand
  // over DRM-PRIME frames, so a machine that cannot never gets them.
  // `why` says which condition failed, for the log: "off" with no reason is
  // the kind of answer that cost a test run on the Pi.
  static bool rendererCanImport(SDL_Renderer* renderer, const char** why = nullptr) {
    auto no = [&](const char* reason) { if (why) *why = reason; return false; };
    if (!renderer) return no("no renderer");
    const char* name = SDL_GetRendererName(renderer);
    if (!name || SDL_strcmp(name, "opengles2") != 0) return no("renderer is not opengles2");
    SDL_EGLDisplay dpy = SDL_EGL_GetCurrentDisplay();
    if (!dpy) {
      // SDL tracks its own EGL display only for some video drivers; ask EGL.
      using CurrentDisplay = void* (*)();
      auto current = reinterpret_cast<CurrentDisplay>(SDL_EGL_GetProcAddress("eglGetCurrentDisplay"));
      dpy = current ? current() : nullptr;
    }
    if (!dpy) return no("no current EGL display");
    using QueryString = const char* (*)(void*, int);
    auto query = reinterpret_cast<QueryString>(SDL_EGL_GetProcAddress("eglQueryString"));
    if (!query) return no("eglQueryString not found");
    const char* ext = query(dpy, 0x3055 /* EGL_EXTENSIONS */);
    if (!ext || !SDL_strstr(ext, "EGL_EXT_image_dma_buf_import")) return no("EGL lacks dma_buf import");
    if (!SDL_GL_ExtensionSupported("GL_OES_EGL_image")) return no("GL lacks OES_EGL_image");
    if (why) *why = "ok";
    return true;
  }

 private:
  static constexpr std::size_t kMaxCached = 3;

  struct Key {
    int fd = -1;
    std::uint32_t offset1 = 0;
    int width = 0;
    int height = 0;
    bool operator==(const Key& o) const {
      return fd == o.fd && offset1 == o.offset1 && width == o.width && height == o.height;
    }
  };
  struct Entry {
    Key key;
    std::uint64_t lastUsed = 0;
    void* images[3] = {nullptr, nullptr, nullptr};
    GLuint textures[3] = {0, 0, 0};
    SDL_Texture* texture = nullptr;
  };

  struct Plane {
    int fd = -1;
    std::uint32_t offset = 0;
    std::uint32_t pitch = 0;
    std::uint64_t modifier = 0;
  };

  static constexpr std::uint32_t fourcc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(a) | (static_cast<std::uint32_t>(b) << 8) |
           (static_cast<std::uint32_t>(c) << 16) | (static_cast<std::uint32_t>(d) << 24);
  }

  static Key keyFor(const AVDRMFrameDescriptor* desc, int width, int height) {
    Key key;
    key.fd = desc->objects[0].fd;
    key.width = width;
    key.height = height;
    const AVDRMLayerDescriptor& layer = desc->layers[0];
    key.offset1 = layer.nb_planes > 1 ? static_cast<std::uint32_t>(layer.planes[1].offset) : 0u;
    return key;
  }

  // Every plane in order, whether the decoder described one layer with
  // several planes (NV12, YUV420) or one layer per plane.
  static std::vector<Plane> planesOf(const AVDRMFrameDescriptor* desc) {
    std::vector<Plane> planes;
    for (int l = 0; l < desc->nb_layers; ++l) {
      const AVDRMLayerDescriptor& layer = desc->layers[l];
      for (int p = 0; p < layer.nb_planes; ++p) {
        const AVDRMPlaneDescriptor& plane = layer.planes[p];
        if (plane.object_index < 0 || plane.object_index >= desc->nb_objects) return {};
        const AVDRMObjectDescriptor& object = desc->objects[plane.object_index];
        planes.push_back({object.fd, static_cast<std::uint32_t>(plane.offset),
                          static_cast<std::uint32_t>(plane.pitch), object.format_modifier});
      }
    }
    return planes;
  }

  bool import(SDL_Renderer* renderer, const AVDRMFrameDescriptor* desc, int width, int height,
              SDL_Colorspace colorspace, Entry& out) {
    const std::vector<Plane> planes = planesOf(desc);
    const bool nv12 = planes.size() == 2;
    const bool yuv420 = planes.size() == 3;
    if (!nv12 && !yuv420) return false;
    if (!planeTexturesSupported()) {
      return importExternal(renderer, planes, width, height, colorspace, out);
    }
    // Everything SDL draws after this must find the state it left: we bind
    // our own textures to import, so the binding is put back afterwards.
    SDL_FlushRenderer(renderer);
    GLint previous = 0;
    glGetIntegerv_(GL_TEXTURE_BINDING_2D, &previous);
    const std::uint32_t r8 = fourcc('R', '8', ' ', ' ');
    const std::uint32_t gr88 = fourcc('G', 'R', '8', '8');
    bool ok = true;
    for (std::size_t i = 0; i < planes.size() && ok; ++i) {
      const bool chroma = i > 0;
      const int w = chroma ? (width + 1) / 2 : width;
      const int h = chroma ? (height + 1) / 2 : height;
      const std::uint32_t format = (nv12 && chroma) ? gr88 : r8;
      out.images[i] = createImage(planes[i], w, h, format);
      if (!out.images[i]) {
        ok = false;
        break;
      }
      glGenTextures_(1, &out.textures[i]);
      glBindTexture_(GL_TEXTURE_2D, out.textures[i]);
      glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      imageTargetTexture_(GL_TEXTURE_2D, out.images[i]);
      ok = glGetError_() == GL_NO_ERROR;
    }
    glBindTexture_(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    if (!ok) return false;

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
                          nv12 ? SDL_PIXELFORMAT_NV12 : SDL_PIXELFORMAT_IYUV);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_COLORSPACE_NUMBER, colorspace);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_NUMBER, out.textures[0]);
    if (nv12) {
      SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_UV_NUMBER, out.textures[1]);
    } else {
      SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_U_NUMBER, out.textures[1]);
      SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_V_NUMBER, out.textures[2]);
    }
    out.texture = SDL_CreateTextureWithProperties(renderer, props);
    SDL_DestroyProperties(props);
    if (out.texture) {
      SDL_SetTextureScaleMode(out.texture, SDL_SCALEMODE_LINEAR);
    }
    return out.texture != nullptr;
  }

  // ONE- AND TWO-CHANNEL TEXTURES (R8, GR88) need OpenGL ES 3 or
  // EXT_texture_rg. A Raspberry Pi 3's GPU is ES 2 without it: the planes
  // import without an error and then sample as zero -- a dark green picture,
  // measured. There the whole frame goes in as one external YUV image instead.
  bool planeTexturesSupported() {
    if (planeTextures_ < 0) {
      using GetStringFn = const unsigned char* (*)(GLenum);
      auto getString = reinterpret_cast<GetStringFn>(SDL_GL_GetProcAddress("glGetString"));
      const char* version = getString ? reinterpret_cast<const char*>(getString(GL_VERSION)) : nullptr;
      const bool es3 = version && SDL_strstr(version, "OpenGL ES 3") != nullptr;
      planeTextures_ = (es3 || SDL_GL_ExtensionSupported("GL_EXT_texture_rg")) ? 1 : 0;
    }
    return planeTextures_ == 1;
  }

  // The whole frame as one external image: the driver samples YUV and returns
  // RGB (GL_OES_EGL_image_external), told the matrix and range by EGL hints so
  // the colours match every other path. SDL draws it as EXTERNAL_OES.
  bool importExternal(SDL_Renderer* renderer, const std::vector<Plane>& planes, int width, int height,
                      SDL_Colorspace colorspace, Entry& out) {
    if (!SDL_GL_ExtensionSupported("GL_OES_EGL_image_external")) return false;
    SDL_EGLDisplay dpy = SDL_EGL_GetCurrentDisplay();
    if (!dpy) return false;
    const std::uint32_t format = planes.size() == 2 ? fourcc('N', 'V', '1', '2') : fourcc('Y', 'U', '1', '2');
    std::vector<SDL_EGLAttrib> attribs = {
      0x3057 /* EGL_WIDTH */, width,
      0x3056 /* EGL_HEIGHT */, height,
      0x3271 /* EGL_LINUX_DRM_FOURCC_EXT */, static_cast<SDL_EGLAttrib>(format),
      0x327B /* EGL_YUV_COLOR_SPACE_HINT_EXT */,
        SDL_ISCOLORSPACE_MATRIX_BT601(colorspace) ? 0x327F /* REC601 */ : 0x3280 /* REC709 */,
      0x327C /* EGL_SAMPLE_RANGE_HINT_EXT */,
        SDL_ISCOLORSPACE_FULL_RANGE(colorspace) ? 0x3282 /* FULL */ : 0x3283 /* NARROW */,
    };
    for (std::size_t i = 0; i < planes.size(); ++i) {
      // PLANEn_FD/OFFSET/PITCH are three consecutive values per plane.
      const SDL_EGLAttrib base = 0x3272 + static_cast<SDL_EGLAttrib>(i) * 3;
      attribs.insert(attribs.end(), {base, planes[i].fd,
                                     base + 1, static_cast<SDL_EGLAttrib>(planes[i].offset),
                                     base + 2, static_cast<SDL_EGLAttrib>(planes[i].pitch)});
    }
    attribs.push_back(0x3038 /* EGL_NONE */);
    out.images[0] = createImage_(dpy, nullptr, 0x3270 /* EGL_LINUX_DMA_BUF_EXT */, nullptr, attribs.data());
    if (!out.images[0]) return false;
    constexpr GLenum kExternal = 0x8D65;         // GL_TEXTURE_EXTERNAL_OES
    constexpr GLenum kExternalBinding = 0x8D67;  // GL_TEXTURE_BINDING_EXTERNAL_OES
    SDL_FlushRenderer(renderer);
    GLint previous = 0;
    glGetIntegerv_(kExternalBinding, &previous);
    glGenTextures_(1, &out.textures[0]);
    glBindTexture_(kExternal, out.textures[0]);
    glTexParameteri_(kExternal, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri_(kExternal, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri_(kExternal, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri_(kExternal, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    imageTargetTexture_(kExternal, out.images[0]);
    const bool ok = glGetError_() == GL_NO_ERROR;
    glBindTexture_(kExternal, static_cast<GLuint>(previous));
    if (!ok) return false;
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_EXTERNAL_OES);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_OPENGLES2_TEXTURE_NUMBER, out.textures[0]);
    out.texture = SDL_CreateTextureWithProperties(renderer, props);
    SDL_DestroyProperties(props);
    if (out.texture) {
      SDL_SetTextureScaleMode(out.texture, SDL_SCALEMODE_LINEAR);
    }
    return out.texture != nullptr;
  }

  void* createImage(const Plane& plane, int w, int h, std::uint32_t format) {
    SDL_EGLDisplay dpy = SDL_EGL_GetCurrentDisplay();
    if (!dpy) return nullptr;
    // EGL_LINUX_DMA_BUF_EXT and its attributes (EGL_EXT_image_dma_buf_import),
    // spelled as numbers so no EGL headers beyond SDL's are needed.
    std::vector<SDL_EGLAttrib> attribs = {
      0x3057 /* EGL_WIDTH */, w,
      0x3056 /* EGL_HEIGHT */, h,
      0x3271 /* EGL_LINUX_DRM_FOURCC_EXT */, static_cast<SDL_EGLAttrib>(format),
      0x3272 /* EGL_DMA_BUF_PLANE0_FD_EXT */, plane.fd,
      0x3273 /* EGL_DMA_BUF_PLANE0_OFFSET_EXT */, static_cast<SDL_EGLAttrib>(plane.offset),
      0x3274 /* EGL_DMA_BUF_PLANE0_PITCH_EXT */, static_cast<SDL_EGLAttrib>(plane.pitch),
    };
    constexpr std::uint64_t kModInvalid = 0x00ffffffffffffffULL;
    if (plane.modifier != kModInvalid && plane.modifier != 0) {
      attribs.insert(attribs.end(), {
        0x3443 /* EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT */, static_cast<SDL_EGLAttrib>(plane.modifier & 0xffffffffu),
        0x3444 /* EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT */, static_cast<SDL_EGLAttrib>(plane.modifier >> 32),
      });
    }
    attribs.push_back(0x3038 /* EGL_NONE */);
    return createImage_(dpy, nullptr, 0x3270 /* EGL_LINUX_DMA_BUF_EXT */, nullptr, attribs.data());
  }

  void release(Entry& e) {
    if (e.texture) SDL_DestroyTexture(e.texture);
    e.texture = nullptr;
    if (glDeleteTextures_) {
      for (GLuint& t : e.textures) {
        if (t) glDeleteTextures_(1, &t);
        t = 0;
      }
    }
    SDL_EGLDisplay dpy = SDL_EGL_GetCurrentDisplay();
    for (void*& image : e.images) {
      if (image && destroyImage_ && dpy) destroyImage_(dpy, image);
      image = nullptr;
    }
  }

  // EGL 1.5 eglCreateImage / eglDestroyImage take EGLAttrib lists; the GL
  // calls come from the renderer's own context through SDL.
  using CreateImageFn = void* (*)(SDL_EGLDisplay, void*, unsigned int, void*, const SDL_EGLAttrib*);
  using DestroyImageFn = unsigned int (*)(SDL_EGLDisplay, void*);
  using ImageTargetFn = void (*)(GLenum, void*);
  using GenTexturesFn = void (*)(GLsizei, GLuint*);
  using DeleteTexturesFn = void (*)(GLsizei, const GLuint*);
  using BindTextureFn = void (*)(GLenum, GLuint);
  using TexParameteriFn = void (*)(GLenum, GLenum, GLint);
  using GetIntegervFn = void (*)(GLenum, GLint*);
  using GetErrorFn = GLenum (*)();
  using EglGetErrorFn = int (*)();

  bool loadEntryPoints() {
    if (loaded_) return ready_;
    loaded_ = true;
    createImage_ = reinterpret_cast<CreateImageFn>(SDL_EGL_GetProcAddress("eglCreateImage"));
    destroyImage_ = reinterpret_cast<DestroyImageFn>(SDL_EGL_GetProcAddress("eglDestroyImage"));
    imageTargetTexture_ = reinterpret_cast<ImageTargetFn>(SDL_GL_GetProcAddress("glEGLImageTargetTexture2DOES"));
    glGenTextures_ = reinterpret_cast<GenTexturesFn>(SDL_GL_GetProcAddress("glGenTextures"));
    glDeleteTextures_ = reinterpret_cast<DeleteTexturesFn>(SDL_GL_GetProcAddress("glDeleteTextures"));
    glBindTexture_ = reinterpret_cast<BindTextureFn>(SDL_GL_GetProcAddress("glBindTexture"));
    glTexParameteri_ = reinterpret_cast<TexParameteriFn>(SDL_GL_GetProcAddress("glTexParameteri"));
    glGetIntegerv_ = reinterpret_cast<GetIntegervFn>(SDL_GL_GetProcAddress("glGetIntegerv"));
    glGetError_ = reinterpret_cast<GetErrorFn>(SDL_GL_GetProcAddress("glGetError"));
    eglGetError_ = reinterpret_cast<EglGetErrorFn>(SDL_EGL_GetProcAddress("eglGetError"));
    ready_ = createImage_ && destroyImage_ && imageTargetTexture_ && glGenTextures_ &&
             glDeleteTextures_ && glBindTexture_ && glTexParameteri_ && glGetIntegerv_ && glGetError_;
    return ready_;
  }

  SDL_Renderer* renderer_ = nullptr;
  std::vector<Entry> cache_;
  std::uint64_t tick_ = 0;
  bool loaded_ = false;
  bool warned_ = false;
  int planeTextures_ = -1;   // -1 not asked yet
  bool ready_ = false;
  CreateImageFn createImage_ = nullptr;
  DestroyImageFn destroyImage_ = nullptr;
  ImageTargetFn imageTargetTexture_ = nullptr;
  GenTexturesFn glGenTextures_ = nullptr;
  DeleteTexturesFn glDeleteTextures_ = nullptr;
  BindTextureFn glBindTexture_ = nullptr;
  TexParameteriFn glTexParameteri_ = nullptr;
  GetIntegervFn glGetIntegerv_ = nullptr;
  GetErrorFn glGetError_ = nullptr;
  EglGetErrorFn eglGetError_ = nullptr;
};

}  // namespace deckboy::engine

#endif
