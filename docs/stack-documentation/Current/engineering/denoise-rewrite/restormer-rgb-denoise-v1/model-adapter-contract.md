# Model Adapter Contract

Adapter ID: `restormer-rgb-adapter-v2`

## Shared Input Proxy

1. Convert Stack working-space linear RGB to linear sRGB.
2. Measure a bounded model-only exposure from the scene's 60th-percentile
   luminance. Map that percentile toward 0.18 with a gain constrained to
   `0.25x` through `32x`.
3. Apply the gain only while constructing the model proxy.
4. For proxy construction only, move negative out-of-gamut components toward
   neutral luminance until the proxy is nonnegative.
5. Compress each nonnegative component with `x / (1 + x)`.
6. Apply the sRGB transfer function.

The inverse adapter divides the decoded model residual by the same gain before
returning it to Stack's working space. The original scene-linear RGBA image
remains untouched. This exposure normalization is required because the frozen
checkpoints were trained on normally developed RGB photographs, while Stack's
stage receives pre-exposure scene-linear pixels that can be extremely dark.

## Scene-linear Safe V1

The model output is interpreted as a residual relative to its input proxy.
The adapter decodes the input and output proxies, separates luminance and two
opponent-color components, removes tile-wide median drift, bounds each
component to four robust local deviations, applies Color Noise, Luminance
Noise, and Detail Protection, converts the delta to the working space, and
adds it to the original scene-linear pixel.

## Processed RGB Match V1

This mapping retains the complete decoded model residual, including its DC
response, while still applying the three user controls and finite/headroom
guards. It also transfers only the delta back to the original scene-linear
pixel; it never replaces the source with a clamped display image.

## Invariants

- alpha is copied exactly;
- zero color and luminance strength is exact identity;
- highlights above 1.0 and negative working-space values are not clamped;
- invalid model numbers cannot become accepted non-finite output;
- model output caching is independent of residual application controls.
