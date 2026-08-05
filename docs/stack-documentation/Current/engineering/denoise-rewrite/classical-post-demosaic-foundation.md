# Classical Post-Demosaic Denoise Foundation

Last updated: July 24, 2026.

## Outcome

Stack now has a new recipe-backed RGB denoise stage for the manual RAW
workflow. It is separate from:

- CFA Denoise, which filters same-color sensor samples before demosaic;
- the legacy load-only Classical RGB Denoise and Scene Denoise graph nodes;
- the retired neural-denoise runtime; and
- general-purpose editor-graph filters such as Bilateral Filter.

The stage is disabled by default. Opening or migrating a project therefore
does not change its pixels.

## Pipeline Contract

When RGB Denoise is enabled, RAW placement is evaluated in this order:

```text
CFA denoise
raw demosaic
white balance
camera-to-working-space conversion
RGB denoise
authored RAW Exposure
Local Exposure
Local Range / Zones
Finish Tone
View Transform
```

DNG BaselineExposure remains part of the technical RAW conversion before RGB
denoise. The new stage does not change crop, output encoding, export behavior,
or the later tone/display contracts.

The disabled path intentionally stays on the established combined RAW GPU
render. Stack only splits neutral conversion, RGB denoise, and authored
exposure when the new stage is enabled. This is the compatibility boundary
that preserves old output when RGB denoise is off.

## Recipe Contract

RAW recipe schema 10 added the classical fields. Schema 11 preserves those
defaults and extends the same stage with optional Restormer identity fields:

```text
rgbDenoise.version = 1
rgbDenoise.enabled = false
rgbDenoise.method = classical-multiscale-v1
rgbDenoise.colorNoise = 0.35
rgbDenoise.luminanceNoise = 0.20
rgbDenoise.detailProtection = 0.75
```

Schema 11 serializes `rgbDenoise.version = 2` and additionally records method,
mapping, exact package version, model SHA-256, and adapter version. Existing
schema-10 recipes migrate as disabled Classical Multiscale with Scene-linear
Safe selected and no pinned AI artifact.

All three continuous controls are finite values clamped to `[0, 1]`. Schema-9
and older projects migrate with the stage disabled and gain `rgb-denoise` in
their normalized stage order immediately before `pre-tone-exposure`.

The method field is versioned now so a future public-model method can share
the same pipeline position without pretending to be the classical algorithm.
No AI model or runtime is part of this pass.

## Classical Multiscale V1

The GPU implementation works in the recipe's configured linear working space.
It first converts RGB to a reversible luminance/opponent representation:

```text
Y  = dot(RGB, working-space luminance weights)
Cb = B - Y
Cr = R - Y
```

Rec.2020 and linear-sRGB use their own luminance weights. Reconstruction
solves the same equations in reverse and does not clamp scene-linear RGB.
Alpha is preserved.

The denoiser then:

1. builds three undecimated B3-spline scales with pixel gaps 1, 2, and 4;
2. gates each blur using log-luminance differences so samples across strong
   scene edges contribute less;
3. estimates local band noise from a 3-by-3 median absolute deviation;
4. applies bounded Bayes-style soft thresholds independently to luminance and
   the two opponent-color bands;
5. reduces thresholding near strong structure according to Detail Protection;
6. reconstructs the untouched coarse residual plus retained detail bands.

Color Noise controls the two opponent-color thresholds. Luminance Noise
controls the brightness-detail threshold. Detail Protection narrows
edge-guided support and retains more wavelet detail around structure.

This is intentionally a heavier baseline than a single bilateral pass. It is
not claimed to be final tuning or equal to a production neural denoiser.

## Render and Cache Boundaries

Enabled RGB denoise has three structural cache identities:

```text
neutral RAW base
    -> RGB-denoised neutral base
        -> authored RAW Exposure placement
```

Changing only Exposure reuses the denoised neutral base. Changing only Color
Noise, Luminance Noise, or Detail Protection reuses the neutral RAW base.
Downstream Local/Tone/View changes continue to reuse RAW placement.

Scratch images use the existing transient graph-target pool. The settled
denoised image is cached through the existing RAW stage cache. If shader
creation or denoise rendering fails, Stack passes the neutral demosaic through
and still applies authored exposure rather than publishing a blank image.

## RAW Lab Surface

The text-first tool island now reads:

```text
CFA Denoise  RGB Denoise  Exposure  Zones  Curve  View
```

It wraps to a second row when the rail is too narrow. RGB Denoise exposes:

- `Denoise`;
- `Color Noise`;
- `Luminance Noise`; and
- `Detail Protection`.

Reset restores disabled state and the schema defaults. The original RAW tab
and graph-denoise authoring surfaces were not changed.

## Validation

Completed:

- schema-10 defaults, round trip, clamping, stable method string, and
  schema-9 migration in `StackGraphBehaviorTests`;
- complete Release build;
- live OpenGL real-DNG A/B/A smoke on
  `IMG_260608_204838.dng`;
- nonblank finite output, visible denoise effect, visible exposure effect,
  deterministic repeated output, and upstream cache reuse.

The 1024-pixel-long-edge real-DNG smoke measured approximately:

```text
first enabled render: 167-173 ms
Exposure-only edit:     23-100 ms
cached repeat:          20-24 ms
```

These figures include graph execution and readback work in the validation
trace; they are evidence that cache boundaries work, not a final performance
target.

Commands:

```powershell
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-develop-real-raw-smoke C:\Users\djhbi\Downloads\Tennis\IMG_260608_204838.dng
.\build.cmd
```

## Remaining Gates

- Compare the defaults across the user's noisy MotionCam S24 corpus at 100%.
- Tune chroma blotch removal, natural grain retention, skin, foliage, fabric,
  text, and demosaic-artifact behavior.
- Add image-level zero-strength and synthetic edge/noise fixtures if the
  classical method is promoted beyond experimental status.
- Profile full-resolution memory pressure and settled-render time. The
  three-scale method holds several RGBA16F intermediates despite pooling.
- Decide whether interactive preview should use fewer scales only if the
  preview remains perceptually faithful and settled output remains unchanged.
- Research public post-demosaic AI models and their licenses after the
  classical baseline has been judged on the same images.

Do not present this foundation as a finished AI denoiser.
