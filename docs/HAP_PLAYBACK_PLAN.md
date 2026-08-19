# HAP Accelerated Playback — Plan

**Status:** not started. Catalog rows exist (`hap`, `hap_alpha`, `hap_q`) and
probe as unavailable on builds without `--enable-libsnappy`.

## Why

HAP is the live/VJ standard (Resolume, VDMX, Isadora, TouchDesigner, Millumin,
Watchout) and it targets exactly Deckboy's job:

- Frames are **DXT/BC block-compressed** — the format a GPU samples natively.
  Decode is a Snappy decompression, then the compressed blocks upload straight
  to a texture: no YUV→RGB conversion, no per-frame CPU scale.
- **All-intra**, so every frame is a keyframe: instant frame-accurate scrub, no
  GOP to fight on seek.
- Payoff is *many simultaneous layers at low CPU* — the Super Deckboy multi-deck
  / PIP direction.

Costs: files run ~5–10× H.264, and DXT is lossy per 4×4 block, so gradients and
flat colour can show blocking.

## The trap to avoid

**Playing HAP through the ordinary ffmpeg decode path is SLOWER than H.264.**
ffmpeg's `hap` decoder decompresses DXT to RGB on the CPU, so you pay HAP's file
size and throw away the entire GPU advantage — worse than the existing d3d11va
NV12 zero-copy path. Shipping HAP support without the path below would actively
mislead: operators would convert their show expecting speed and get less.

## The path

1. **Demux** the MOV without decoding — `av_read_frame` on the video stream
   gives HAP chunks as packets. Do NOT `avcodec_send_packet`.
2. **Snappy-decompress** each chunk. HAP's container framing is simple (section
   header: 3-byte size + 1-byte type, optional chunk table for multi-chunk
   frames). `-chunks 4` on encode means 4 independently decompressible chunks,
   which parallelises well across the decode thread pool.
3. **Upload as a compressed texture.** `DXGI_FORMAT_BC1_UNORM` (Hap),
   `BC3_UNORM` (Hap Alpha), `BC3_UNORM` + YCoCg reconstruction in a shader
   (Hap Q). Row pitch is per-block, not per-pixel.
4. **Composite** through the existing per-deck GPU bridge.

## Why this is more feasible than it sounds

The hard part already exists. The v0.78.0 zero-copy work creates
`ID3D11Texture2D` outside SDL's texture API and composites it via the per-deck
GPU bridge (`app_render_output.ipp`). This reuses that route.

It *has* to reuse it: **SDL3's texture API cannot accept compressed formats.**
`SDL_CreateTexture` takes a fixed set of RGBA/YUV formats; there is no BC1/BC3
entry and `SDL_UpdateTexture` has no block-pitch concept. Any attempt to do this
through `deckboyCreateTexture` will fail — go to D3D11 directly.

## Platform reality

- **Windows/D3D11** — the path above. First-class, matches the primary target.
- **macOS/Metal** — `MTLPixelFormatBC1_RGBA` is Apple-silicon only; Intel Macs
  need the ASTC/ETC route or a CPU fallback. Syphon already gives Mac users a
  GPU handoff, so this is lower priority there.
- **Linux/GL** — `GL_COMPRESSED_RGBA_S3TC_DXT1_EXT`, widely available.
- Everywhere else: fall back to ffmpeg's CPU decode and *say so*, rather than
  silently being slow.

## Encoding

ffmpeg's HAP encoder needs `--enable-libsnappy`; the local build does not have
it (decoder present, encoder absent). Options, in order of cost:

1. Ship / point at an ffmpeg built with libsnappy. The encoder itself is
   unremarkable; it is purely a build-flag problem.
2. Encode natively (DXT compression + Snappy). Real work, no clear payoff over
   option 1.
3. Leave encoding to the tools VJs already own.

The catalog already handles the current state honestly: the HAP rows probe as
unavailable with a reason rather than failing into "conversion failed".

## Note

HAP is the exact opposite of the datamosh format: all-intra means it can never
mosh, because there are no P-frames to smear. See `docs/DATAMOSH_PLAN.md`.
