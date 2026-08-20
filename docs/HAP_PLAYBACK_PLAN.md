# HAP Accelerated Playback - Plan

**Status: DECODE HALF DONE AND VERIFIED. GPU upload not started.**

Done:
- `native/engine/hap_decoder.{hpp,cpp}` - container parse (all three
  second-stage compressors: none, Snappy, chunked/complex) plus a vendored
  Snappy decompressor. No new build dependency on any platform.
- `probeHapFile()` in libav_decoder.cpp - demuxes HAP packets WITHOUT calling
  avcodec_send_packet, so ffmpeg never CPU-unpacks the DXT.
- `--hap-probe <file.mov>` dev flag, and smoke coverage.
- `tools/make_hap_sample.py` - writes real HAP media (DXT1 + Snappy + minimal
  MOV), because ffmpeg only encodes HAP with --enable-libsnappy.

Verified:
- The Snappy decompressor round-trips six vectors produced by the REFERENCE
  implementation (python-snappy), covering long literals, 1/2/4-byte copy
  offsets and overlapping runs. Non-circular: those streams come from Google
  format, not from anything in this repo.
- The generated sample decodes correctly in **ffmpeg's own HAP decoder** to the
  exact expected pixels, which independently validates the container, the
  Snappy layer and the DXT1 encoding.
- `--hap-probe` on that file: 20/20 frames demuxed and decoded to RGB_DXT1,
  32768 bytes each -- exactly 256x256 / 16 pixels-per-block * 8 bytes.

- `decompressToRgba()` - DXT1/DXT5 block expansion on the CPU. Verified against
  ffmpeg's HAP decoder on real media: **87% of bytes identical, max difference
  1 level**, and measured against the ORIGINAL uncompressed image both decoders
  are equally accurate (mean absolute error 1.22 for ffmpeg, 1.29 here, out of
  255). That residue is a rounding convention, not a defect: S3TC/DXT
  decompression is not specified to be bit-exact, which is why GPUs from
  different vendors also disagree by a level on the same block. Do not spend
  time chasing bit-parity with any one decoder.

**What the CPU path does and does not buy.** It makes HAP play, with the
all-intra instant seeking that is half the reason to use the format. It does
NOT buy the many-layers-at-no-CPU win, which needs the blocks uploaded as a
compressed texture.

The blocker for that is concrete: **SDL3 has no pixel format for BC data.**
`SDL_CreateTexture` cannot express BC1/BC3 and `SDL_UpdateTexture` has no notion
of block pitch, so blocks cannot be handed to the renderer the way the NV12
zero-copy path hands it decoder surfaces (`createWrappedNV12Texture` works only
because SDL *does* know NV12). The GPU route therefore needs a D3D11 texture
created outside SDL, sampled through a shader into an SDL-wrappable RGBA target
-- the sampler decompresses BC for free in hardware. That is the remaining work.

Remaining: upload those blocks as a compressed GPU texture and composite them
(step 3-4 below). That is the part that still needs the D3D11 bridge and a
display to verify.

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
