# Scopes analysis core

`native/core/scopes.hpp` is a C++20, standard-library-only signal analyzer. It
does not open windows or attach itself to playback. `tools/scopes_check.cpp`
tests it with generated signals; the desktop's existing `--smoke` also calls
the same production implementation. The core is compiled unconditionally on
Windows, macOS and Linux, independently of ASIO and in-process decoding.

## Input contract

Supply the active picture only. `FrameView` accepts interleaved three-component
samples as R'G'B' or Y'CbCr, with a stride in samples. `Rgba8View` accepts
full-range RGB bytes with an ignored fourth byte and a stride in bytes. Zero
stride means packed rows. The final row does not require trailing padding.
A subspan and the original stride can select a rectangular region.

`Bgra422View` takes the BGRA output frame and measures it as SDI and ST 2110
send it: BT.709 narrow-range Y'CbCr 4:2:2, 8- or 10-bit, encoded by
`core/ycbcr422.hpp` (the outputs' own encoder), each pixel rebuilt from its own
Y' and the Cb Cr its horizontal pair shares. `Options::matrix` does not apply;
the signal is BT.709. This is the only input whose 8-bit source can leave gamut.

Normalized signal input uses black 0, nominal white 1, and neutral chroma 0
(nominal chroma endpoints -0.5 and +0.5). Narrow-code input supports 8, 10, 12
and 16 bits. Full-code input supports RGB only. Use normalized samples for
other externally decoded representations. `Options::gamutBitDepth` separately
selects the depth of the measurement's total-range limits; it defaults to 10.

Input channels must already have the intended nonlinear transfer encoding and
primaries. The matrix selector does not convert primaries, linearize light,
apply a gamma curve, decode PQ/HLG to nits, or support BT.2020 constant
luminance. Alpha is ignored; callers provide a finished composite. There is
no decoder for packed YUV from elsewhere, implicit downsampling, or temporal averaging.
If a caller downsamples before analysis, the resulting counts describe that
sampled picture, not a full-resolution compliance measurement.

Metadata and grid sizes are checked before reading. Invalid metadata throws
`std::invalid_argument`; allocation failure propagates. A 0x0 frame is valid.
NaN, infinity and arithmetic overflow exclude a whole pixel from the scope
densities and increment `invalidPixels`. An invalid sample also invalidates
every filtered measurement whose footprint touches it.

## Results and coordinates

Each density is a row-major array of 64-bit counts. A parade is three separate
planes, all preserving source horizontal position; the luma waveform is the
Y' plane of the YCbCr parade. Horizontal bin assignment is
`floor(sourceX * columns / sourceWidth)`. Bin rows ascend from the lowest
signal level; a future renderer must reverse them for screen coordinates.

The vectorscope has Cb on X and Cr on Y, with equal scale and positive Cr
upwards. It has no analogue UV rotation. Targets are normalized coordinates
for R, yellow, G, cyan, B and magenta with zero pedestal and either 75% or 100%
RGB amplitude. The skin-tone guide is a radius-0.5 ray from neutral along the
traditional +I signal direction, projected through the chosen signal matrix.
It is a reference guide, not a test that skin must lie on one hue or a
colourimetric mapping from NTSC primaries.

Histograms contain Y' and individual R', G', B' distributions. Axis ranges are
configurable; default levels are 0..1 and chroma -0.5..0.5. Bins have equal
width, include their lower edge and exclude their upper edge, except that the
last bin includes the upper endpoint. Values outside an axis accumulate in
the end bins and in separate below/above counters. This display policy never
clips values used for gamut measurement. Grids are bounded to 4096 per axis
and an aggregate 16 million count cells (128 MiB).

## Exact 8-bit bins

On the standard axes (levels 0..1, chroma -0.5..0.5) an 8-bit pixel's Y', Cb
and Cr are ratios of integers. Both matrices state Kr, Kg and Kb to four
decimal places, so in ten-thousandths (`exactCoefficients`: 2126, 7152, 722
for BT.709; 2627, 6780, 593 for BT.2020):

- `255 * 10000 * Y' = n = kr R + kg G + kb B`, and the row is `n * bins / 2550000`
- the Cb row is `(10000 B - n + 255 (10000 - kb)) * bins / (510 (10000 - kb))`,
  and the Cr row is the same with R and kr
- the R', G', B' row is `v * bins / 255`; the diamond's cells are
  `(a + g) * bins / 510` and `(a - g + 255) * bins / 510`

each rounded down and capped at the last bin. `analyze(Rgba8View)` bins this
way. Computing Y' in floating point first, as the general path does, puts 17
of the 16.7 million 8-bit colours one luma bin low in BT.709 (12 in BT.2020).
Each lies exactly on a bin edge and rounds to just under it. The check holds
the 8-bit path to an independent 64-bit integer statement of these formulas
over every 8-bit colour.

## The GPU analyser

`native/engine/gpu_scopes` runs the same exact bins as a compute shader on
the output's own capture texture, where the frame already is. Only the counts
come back: about 6.7 MB at 960 waveform columns, instead of a 33 MB 4K frame.
One thread group takes each waveform column and counts it in shared memory.
The vectorscope and the diamonds are too big to share, so each thread adds a
run of equal cells once. The histograms are the waveform rows summed, because
on these axes a value's histogram bin is its waveform row. It covers the 8-bit
R'G'B' frame on the monitor's axes (`gpuScopesSupport`). The 4:2:2 wire
signal and the HDR picture stay on the CPU.

One shader, `gpu_scopes.hlsl`, runs on two backends. Direct3D 11 (Windows'
output renderer) compiles it at run time. SDL_GPU, the "gpu" renderer the
outputs use on macOS and Linux, takes it as DXIL for Direct3D 12 and SPIR-V
for Vulkan, both made by `tools/build_scopes_shaders.py` with dxc and embedded
in `gpu_scopes_shaders.inc`. Metal takes `gpu_scopes.metal`, a line-for-line
copy, as source. SDL_GPU's renderer submits its drawing only when it presents,
so an analysis there is issued one present after the capture is drawn. Where
neither backend runs, the CPU analyser measures. That includes Vulkan until a
dxc with SPIR-V output makes the embed (the Windows SDK's has none).
`DECKBOY_CPU_SCOPES=1` forces the CPU analyser, for comparison.

Run on: Direct3D 11 on an RTX 4070 Laptop GPU and on Microsoft's software
renderer (the Windows CI runner), and SDL_GPU on Direct3D 12. Metal: the
shader compiles in CI (`xcrun metal`), and the macOS runner runs the check. `tools/scopes_gpu_check.cpp` holds the GPU's
result against `analyze(Rgba8View)` bin for bin on a real renderer. It covers
every 8-bit colour, odd rasters, all four output orientations, a flat 4K
field (the worst case for the shared adds), a real frame, and the false colour
picture's samples. Run it with `--bench <frame.bgra> <w> <h>` to time it.

Measured on a 32-thread PC with an RTX 4070 Laptop GPU, through `check_scopes.py
--performance`: a 4K60 output is measured 61 times a second with 0.9 cores
busy, against about 21 times a second on 3.2 cores before. A 1080p60 output
is measured 59 times a second on 0.8 cores. The render thread spends about
1.9 ms a 4K reading on the capture copy, the dispatch and copying out the
counts.

## Standards and derivation

The implementation was written from the following standards. No third-party
scope implementation or source was used.

- [ITU-R BT.709-6](https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.709-6-201506-I!!PDF-E.pdf),
  section 3: luma weights 0.2126, 0.7152, 0.0722; colour differences
  `(B'-Y')/1.8556` and `(R'-Y')/1.5748`. Narrow-range coding uses 219 and
  224 scale factors with offsets 16 and 128, scaled for bit depth. Inverse
  conversion is the algebraic inverse without clamping.
- [ITU-R BT.2020-2](https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2020-2-201510-I!!PDF-E.pdf),
  Tables 4 and 5: non-constant-luminance weights 0.2627, 0.6780, 0.0593;
  colour differences divide by 1.8814 and 1.4746. BT.2020 specifies 10/12-bit
  interfaces; this core additionally accepts 8/16-bit numerical
  representations using the same normalized signal equations.
- [ITU-R BT.470-6](https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.470-6-199811-S!!PDF-E.pdf),
  Table 2, items 2.4, 2.5 and 2.9: the +I reference uses the 33-degree
  modulation phase, `U=0.493(B'-Y')`, `V=0.877(R'-Y')` and the NTSC luma
  weights. Setting Q and Y' to zero gives an RGB signal direction that is
  then encoded with the selected scope matrix. This explains why its angle
  in raw Cb/Cr coordinates is not the analogue 123-degree UV angle.
- [EBU R 103 v3.0](https://tech.ebu.ch/files/live/sites/tech/files/shared/r/r103v3_0.pdf),
  Table 1 and the filtering/occurrence recommendations: counts cover Y', R',
  G', B' at nominal, preferred and total limits. Preferred limits are 5..246,
  20..984, 80..3936 and 1280..62976 for 8/10/12/16 bits respectively. Total
  limits are 1..254, 4..1019, 16..4079 and 256..65279. Bounds are inclusive;
  a normalized tolerance of 1e-12 avoids floating-point boundary noise.
  Per-channel counters overlap; pixel counters count the union once.
  `outOfGamut()` means preferred-range errors exceed 1% of the supplied
  active image, including invalid pixels in the denominator. Invalid counts
  must also be checked before interpreting a measurement.
  The default progressive filter is separable: horizontal
  `[1,2,3,4,3,2,1]/16`, vertical `[1,2,1]/4`. Interlaced mode uses same-field
  rows two lines apart. Active-picture edges repeat within the same field.
  Raw counts are always retained. With filtering disabled, filtered counts
  equal raw counts. Filtering uses at most five cached rows plus one source
  row; it does not store another full picture.

## Checks

With a configured Deckboy build:

```
cmake --build build/windows --config Release --target deckboy-scopes-check
ctest --test-dir build/windows -C Release -R "^scopes$" --output-on-failure
```

The GPU analyser against the CPU one (needs a renderer with a GPU analyser;
`--require` fails rather than skipping when there is none):

```
cmake --build build/windows --config Release --target deckboy-scopes-gpu-check
build/windows/Release/deckboy-scopes-gpu-check --require
py -3 tools/check_scopes.py --gpu
```

The core checks can also compile without any application dependency:

```
c++ -std=c++20 -I native tools/scopes_check.cpp native/core/scopes.cpp -o scopes-check
./scopes-check
```

Coverage includes independent decimal bar references for both matrices,
75/100% targets, the +I phase, ramps, source-X retention, histogram/density
conservation, padded rows, ignored alpha, narrow/full-code normalization,
inclusive gamut boundaries and their one-code neighbours at all four depths,
illegal RGB reconstructed from legal YCbCr component values, PLUGE, the
strict 1% threshold, FIR impulses, field isolation, invalid samples, malformed
views, allocation bounds and empty/single-pixel images. A separate integer-code
2D convolution oracle checks every 1..9 by 1..9 frame size in progressive and
interlaced modes, including both field parities and row-cache wraparound.
