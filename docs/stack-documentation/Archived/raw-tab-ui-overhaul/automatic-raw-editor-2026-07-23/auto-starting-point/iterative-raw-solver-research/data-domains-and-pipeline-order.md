# Data Domains And Pipeline Order

- Captured: 2026-07-09
- Source: DNG 1.7.1.0, official darktable/OpenColorIO documentation, primary tone-mapping research, and current Stack staging
- Type: research
- Topic: iterative-raw-solver
- Verification: partially-verified; ordering is a research target, not current implementation truth

## Why Domain And Order Come First

The same number has a different meaning in a sensor mosaic, demosaiced
scene-linear RGB, tone-shaped scene data, and display RGB. A reliable solver
must attach every measurement to:

```text
source identity
pipeline stage
color space
transfer function
reference grey/white
crop/orientation
resolution
recipe fingerprint
uncertainty
```

A percentile without that record is not reusable evidence.

## Proposed Research Pipeline

This is the initial dependency order to investigate:

```text
0. source identity and metadata validation
1. raw mosaic decode, linearization, black subtraction, and normalization
2. raw defects, raw denoise evidence, channel clipping, and reconstruction policy
3. technical white balance and camera color calibration evidence
4. demosaic into scene-linear RGB
5. early scene-linear denoise/profile corrections needed for trustworthy analysis
6. orientation-normalized and crop-aware scene analysis
7. global RAW Exposure candidate search
8. regional/semantic masks and Local Range candidate search
9. global Finish Tone candidate search
10. Display Fit / View Transform candidate search
11. final display/perceptual/artifact verification
12. visible recipe write and one-action undo handoff
```

The exact placement of raw denoise, chromatic adaptation, camera-to-working
space conversion, lens corrections, and semantic analysis remains a research
question. Each must be justified by what information it preserves or changes.

## Domain 0: Source And Metadata

Record before decoding:

- file/source identity and content hash;
- camera and lens identifiers;
- ISO, exposure time, aperture, focal length, orientation, and crop metadata;
- CFA layout and color plane order;
- black/white levels and active/masked areas;
- `AsShotNeutral`, calibration illuminants, color matrices, and forward
  matrices when available;
- `BaselineExposure`, `BaselineNoise`, `NoiseProfile`,
  `LinearResponseLimit`, opcode lists, gain maps, and profile dynamic range;
- whether the input is original mosaic RAW, linear DNG, enhanced DNG, or a
  rendered fallback.

Missing metadata must become explicit uncertainty, never a silent zero.

## Domain 1: Normalized Raw Mosaic

For raw sample `r_c` in CFA plane `c`, the basic normalized signal is:

```text
x_c = clamp((r_c - b_c) / max(epsilon, w_c - b_c), 0, upperPolicy)
```

where `b_c` is the black encoding level and `w_c` is the usable white level.
Spatially varying black levels, row/column deltas, masked pixels, and sensor
non-linearity must be included when available.

The raw mosaic is the strongest domain for:

- per-channel saturation and near-saturation;
- black-level estimation and drift;
- defective/hot pixels;
- WB-scaled channel headroom;
- signal-dependent noise estimation;
- determining whether highlight information exists in one channel but not
  another.

It is not the right domain for semantic regions, perceptual hue, final local
contrast, or display clipping.

## Raw Noise Model

The DNG `NoiseProfile` model is:

```text
sigma_c(x) = sqrt(S_c * x + O_c)
variance_c(x) = S_c * x + O_c
SNR_c(x) = x / max(epsilon, sigma_c(x))
```

`S_c` models signal-dependent shot noise and `O_c` models signal-independent
read noise in normalized raw values. A future solver can use this to price a
shadow lift by predicted post-lift noise rather than ISO alone.

## Raw Highlight State

For each color plane, track at least:

```text
valid
near nonlinear
near clipped
clipped
recoverable from other planes
all planes clipped
```

If `g_c` is the technical WB multiplier and `q_c` is a robust high raw
percentile, a provisional headroom measurement is:

```text
h_c = log2(max(epsilon, limit_c) / max(epsilon, g_c * q_c))
safePositiveExposure <= min_c(h_c) - margin
```

The limit should account for white level and `LinearResponseLimit`. The margin
must be calibrated by camera evidence and uncertainty, not fixed permanently.

## Domain 2: Post-Technical-WB Raw Or Camera RGB

Technical white balance changes channel headroom and noise amplification. It
must therefore be considered before approving positive exposure, but image
derived creative WB should not be allowed to redefine raw safety casually.

Research must distinguish:

- camera/as-shot neutral metadata;
- technical multipliers used before demosaic or camera-space conversion;
- chromatic adaptation into a working illuminant;
- creative Temp/Tint intent;
- mixed-illuminant local color correction.

## Domain 3: Demosaiced Scene-Linear RGB

This is the first appropriate domain for:

- full-resolution luma and chromaticity;
- edge and texture analysis;
- multiscale local contrast;
- connected regions and edge-aware masks;
- subject/face/sky/foreground evidence;
- Local Range simulation;
- color constancy alternatives;
- predicted hue and gamut effects.

Demosaicing creates estimates and can add noise, zippering, false color, or
overshoot. Feature records must name the demosaic method and proxy scale.

## Orientation And Crop Normalization

Semantic position such as “sky near the top” is meaningless until orientation
metadata is applied. Spatial analysis should use an orientation-normalized
coordinate system, while raw black/masked-area analysis should retain sensor
coordinates. The transform between both systems must be recorded.

Crop policy also matters. Solver measurements should distinguish:

- active sensor area;
- default crop;
- current user crop;
- pixels outside the crop that still provide raw safety evidence.

## Domain 4: Raw Placement Candidate

RAW Exposure should be evaluated on a rendered scene-linear candidate, not only
predicted by adding EV to old percentiles. Candidate measurements should
include:

- global and region-weighted scene key;
- WB-scaled headroom;
- low-percentile SNR after the proposed lift;
- subject and background placement;
- unresolved regional conflicts;
- change from current visible Exposure.

## Domain 5: Local Range Candidate

Local Range should consume orientation-normalized, edge-aware scene evidence.
Its candidate render should measure:

- change in target-region visibility;
- change in highlight and shadow safety;
- local-contrast preservation inside regions;
- gradient behavior across region boundaries;
- noise amplification in affected pixels;
- color and mask leakage;
- graph smoothness and overlap with existing user points.

## Domain 6: Finish Tone Candidate

Finish Tone should be evaluated after accepted Exposure and Local Range values.
It owns global tonal relationships, not raw recovery or final device mapping.
Measure:

- monotonicity and slope;
- middle-tone contrast;
- compression distribution between shadows and highlights;
- structural fidelity across scales;
- hue/chroma changes caused by the chosen curve implementation;
- remaining dynamic range before the display transform.

## Domain 7: Display Candidate

Display Fit maps scene-referred values into the output display space. The
OpenColorIO definition of a view transform as a scene-reference to
display-reference conversion is the correct boundary model.

Measure:

- display black/white clipping;
- middle-grey placement;
- highlight rolloff and shadow toe;
- output gamut pressure and hue behavior;
- perceptual visibility at the assumed display luminance and ambient state;
- whether readability is being obtained by hiding an upstream failure.

## Two Different Loops

The future architecture should not mix these loops:

### Technical Evidence Loop

```text
decode -> raw safety/noise/color evidence -> cache by source/decode identity
```

This should rerun only when the source or decode-affecting settings change.

### Visible Candidate Optimization Loop

```text
visible parameter proposal -> proxy render -> stage measurements
-> constraint check -> objective score -> next proposal
```

This reruns during a precise solve and must key every result to the exact
visible recipe candidate.

## Ordering Sources

- Adobe DNG 1.7.1.0 defines raw linearization, black subtraction,
  normalization, clipping, metadata, and opcode stages before/after mapping to
  linear reference values and demosaic:
  https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf
- darktable documents raw black/white, highlight reconstruction, raw denoise,
  and demosaic before later scene-referred editing:
  https://docs.darktable.org/usermanual/3.6/en/special-topics/module-order/
- darktable describes tone equalizer as scene-referred, mask-guided exposure
  adjustment:
  https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/tone-equalizer/
- OpenColorIO defines the scene-reference to display-reference view-transform
  boundary:
  https://opencolorio.readthedocs.io/en/v2.4.2/api/viewtransform.html

## Unresolved Ordering Questions

1. Which corrections must be applied to analysis proxies to make their
   statistics trustworthy without making proxy generation too slow?
2. Should profiled denoise affect scoring, or should scoring use both noisy and
   denoised evidence?
3. At what stage should camera-space RGB be converted into the solver working
   space?
4. Should local/semantic masks be computed once from a neutral render or
   updated during optimization?
5. Which lens and gain-map corrections materially affect luminance and region
   statistics?
6. How should linear DNGs and enhanced DNGs alter the raw safety ledger?
7. Which stage is the reference for final perceptual comparison when no human
   edited target exists?

