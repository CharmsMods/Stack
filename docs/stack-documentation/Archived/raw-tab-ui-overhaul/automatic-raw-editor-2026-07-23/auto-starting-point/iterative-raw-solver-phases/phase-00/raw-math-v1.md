# Raw Math v1

## Scope And Sources

This note closes the Phase 00 math needed for Phase 01 diagnostics. Its primary
authority is the [Adobe DNG 1.7.1.0 specification](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf),
with Adobe's [DNG resource page](https://helpx.adobe.com/camera-raw/digital-negative.html)
as the official entrypoint. Noise interpretation also uses Foi et al.,
[Practical Poissonian-Gaussian Noise Modeling and Fitting](http://sp.cs.tut.fi/pubdl/Foi2008-PracticalD.pdf).

Every measurement produced from these equations must record source/decode
identity, sensor rectangle, CFA/color-plane order, units, validity fraction,
and uncertainty.

## DNG Linear Reference Mapping

For stored sample `r` at sensor location `(u,v)` and plane `c`:

```text
l = LinearizationTable[r]                         when the table exists
l = r                                             otherwise
b(u,v,c) = BlackLevel(u,v,c)
           + BlackLevelDeltaH(u)
           + BlackLevelDeltaV(v)
x(u,v,c) = (l - b(u,v,c)) / (WhiteLevel(c) - b_max(c))
```

`b_max(c)` is the maximum applicable computed black level for the plane in the
normalization region. The DNG reference order is linearization, black
subtraction, rescaling, then clipping. Values above one are clipped at the DNG
linear-reference boundary; negative values may be preserved in an early
analysis buffer because their distribution is useful for black/noise
estimation. They are never passed to `log2` without a policy.

Rules:

- `BlackLevelRepeatDim` and `BlackLevel` form a repeating sensor-coordinate
  pattern whose origin is the top-left of `ActiveArea`.
- `ActiveArea` is top, left, bottom, right. Sensor-domain sampling occurs in
  this rectangle before display orientation.
- `MaskedAreas` are fully masked rectangles. They may estimate black encoding
  level only when black has not already been subtracted and with an explicit
  robust estimator and uncertainty.
- `DefaultCrop` and the current user crop affect photographic measurement, not
  the sensor-coordinate definition of black/masked areas.
- Missing or invalid denominator, CFA order, or white level blocks normalized
  raw safety. It is not replaced by display white.

### Metadata Meaning

| Item | Solver meaning | Phase 01 fallback |
| --- | --- | --- |
| `AsShotNeutral` | Neutral coordinates in linear reference values; technical/as-shot WB evidence, not creative truth. | Use decoder camera WB when provenance is known; otherwise unavailable. |
| `BaselineExposure` | Camera-model default-brightness zero-point shift in EV caused by headroom/noise tradeoff. | Zero only when the tag is genuinely absent, with lower confidence. Do not confuse with current RAW Exposure. |
| `LinearResponseLimit` | Fraction of encoded range above which sensor response may be significantly nonlinear and highlight color may shift. | Use white level as the outer limit and mark non-linearity boundary unavailable. |
| `NoiseProfile` | Per-plane normalized shot/read-noise coefficients. | Prefer measured camera profile; DNG's BaselineNoise approximation is low confidence and never camera-specific truth. |
| Opcode list 1 | Operations on raw as read. | Record unsupported opcode count; do not claim equivalent raw evidence. |
| Opcode list 2 | Operations just after mapping to linear reference. | Same. |
| Opcode list 3 | Operations just after demosaic. | Same. |
| Gain map / profile gain table | Spatial/profile gain that changes signal/noise or later scene appearance. | Record presence and applied stage; unknown application lowers confidence. |

`ProfileGainTableMap` belongs after camera-to-XYZ/RIMM conversion and
BaselineExposure/opcodes/warps, before `ProfileToneCurve`, while preserving
over-range values. Its spatial and tone-conditioned gains are evidence that a
single global histogram is insufficient; they are not permission to add an
invisible Adobe-like correction in Stack.

## Clipping And Headroom

Let:

```text
limit_c = min(1, LinearResponseLimit_c when valid)
g_c     = technical WB multiplier normalized by a declared convention
q_c     = robust raw percentile below isolated defects
h_c     = log2(limit_c / max(epsilon, g_c * q_c))
```

The uncertainty-adjusted positive RAW Exposure ceiling is:

```text
e_max = min_c(h_c - margin_c - uncertainty_c)
```

This is a bound, not an exposure target. Percentile, sampling stride, hot-pixel
rejection, plane counts, and the limiting plane must be retained.

For a channel-aligned pixel/superpixel neighborhood, classify each plane:

```text
valid:          x_c < limit_c - near_margin
near_nonlinear: x_c approaches LinearResponseLimit_c
near_clipped:   x_c >= 1 - near_margin
clipped:        x_c >= 1 or decoder-declared saturation
```

Then classify the neighborhood:

```text
no_channel_clipped     if zero required planes are clipped
partial_channel_clip   if some but not all required planes are clipped
all_channel_clip       if every required plane is clipped
```

A Bayer sample is not a co-sited RGB pixel. Mosaic plane counts can prove that
a plane has clipped samples, but channel-aligned partial/all classification
requires CFA-aware superpixels or a separately declared aligned stage. All
planes clipped means no measured luminance detail remains there. Partial-plane
clipping carries color/reconstruction uncertainty and must not be treated as
unclipped merely because final display RGB looks smooth.

Required outputs are fractions and connected regions for every state, distance
to the active/crop boundary, boundary-gradient evidence, and confidence. Never
collapse them into one `highlightRisk` boolean.

## Shot And Read Noise

DNG defines normalized raw standard deviation:

```text
sigma_c(x) = sqrt(S_c*x + O_c)
var_c(x)   = S_c*x + O_c
SNR_c(x)   = x / sigma_c(x)
```

`S_c > 0`, `O_c >= 0`. Two values apply to every plane or `2*ColorPlanes`
values follow `CFAPlaneColor`. The model assumes spatially and spectrally
independent white noise; it does not model fixed pattern noise, PRNU, spatial
thermal effects, demosaic correlation, or clipped reconstruction.

For exposure multiplier `A = 2^e` and WB gain `g_c`:

```text
y_c = A*g_c*x_c
var(y_c | x_c) = (A*g_c)^2 * (S_c*x_c + O_c)
```

Re-expressed as a function of output signal `y_c`:

```text
var(y_c) = A*g_c*S_c*y_c + (A*g_c)^2*O_c
```

Pure multiplication does not create sensor SNR:

```text
y_c / sqrt(var(y_c)) = x_c / sqrt(S_c*x_c + O_c)
```

It makes existing noise more visible and changes downstream clipping,
denoising, quantization, and display visibility. A spatial Local Range gain
`A(u,v)` uses the same propagation per pixel; mask gradients additionally
create boundary-risk terms. ISO is metadata context, not a substitute for
these coefficients or rendered residual evidence.

When no valid profile exists, Phase 01 reports `noise_model_unavailable` or a
named low-confidence fallback. The DNG illustrative BaselineNoise=1/ISO-100
approximation (`S=2e-5`, `O=4.5e-7`) may test plumbing but may not set a
production lift bound.

## Scene Luminance And EV

Scene luminance is computed only after demosaic and camera-to-working-space
conversion. If `M_working_to_XYZ` is the declared linear matrix:

```text
Y = row_Y(M_working_to_XYZ) dot RGB_working_linear
EV_Y = log2(max(Y, epsilon) / Y_grey)
```

`Y_grey` defaults to `0.18` only for a version that declares that convention.
Do not use Rec.709 coefficients unless the working space actually makes them
valid. Display-encoded RGB is not scene luminance.

Negative scene-linear components are preserved for color math and diagnostics.
For logs and ratios, report negative/invalid fraction separately, exclude
invalid samples, clamp only the logarithm argument to a declared epsilon, and
lower confidence when the excluded fraction is material.

Percentiles must record weighting, crop, region/mask, proxy size, and valid
fraction. A median EV is evidence, not a permanent target.

## WB Evidence And Disagreement

As-shot metadata remains the default technical evidence. Image-derived
estimators are independent observations, not authorities. A common family is:

```text
E_c(n,p,sigma) = (∫ |∇^n(G_sigma * I_c)(x)|^p dx)^(1/p)
```

- `n=0, p=1`: Gray World family;
- `n=0`, finite higher `p`: Shades of Gray;
- `n=1`: Gray Edge;
- `p -> infinity`: channel maximum/maximum-derivative limit.

Sources: Finlayson and Trezzi,
[Shades of Gray](https://pdfs.semanticscholar.org/acf3/6cdadfec869f136602ea41cad8b07e3f8ddb.pdf),
and van de Weijer et al.,
[Edge-Based Color Constancy](https://citeseerx.ist.psu.edu/document?doi=f14a55562e6f34d8fbae6dbaf01e0e8cc1ed6bad&repid=rep1&type=pdf).

Normalize estimates to a declared chromaticity and measure pairwise angular
or log-chromatic disagreement. High disagreement, spatially different
estimates, clipped channels, too few valid pixels, or conflict with as-shot
metadata increases uncertainty and blocks global automatic WB. The value
`p=6` reported in one Shades-of-Gray experiment is not a universal constant.

## Phase 01 Acceptance Outputs

Phase 01 may implement diagnostics only when each record returns:

```text
value and units
source/decode/hash identity
sensor rectangle and orientation transform
plane order and sample count
stage and normalization version
validity and uncertainty
metadata source or named fallback
limiting plane/region when applicable
warnings and unsupported operations
```

It may not write a slider, graph, profile, or hidden image correction.
