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

Normalized signal input uses black 0, nominal white 1, and neutral chroma 0
(nominal chroma endpoints -0.5 and +0.5). Narrow-code input supports 8, 10, 12
and 16 bits. Full-code input supports RGB only. Use normalized samples for
other externally decoded representations. `Options::gamutBitDepth` separately
selects the depth of the measurement's total-range limits; it defaults to 10.

Input channels must already have the intended nonlinear transfer encoding and
primaries. The matrix selector does not convert primaries, linearize light,
apply a gamma curve, decode PQ/HLG to nits, or support BT.2020 constant
luminance. Alpha is ignored; callers provide a finished composite. There is
no subsampled/packed YUV decoder, implicit downsampling, or temporal averaging.
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
