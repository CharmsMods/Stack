# Measurement And Feature Catalog

- Captured: 2026-07-09
- Source: primary imaging research, DNG 1.7.1.0, official RAW-editor documentation, and current Stack analysis
- Type: research
- Topic: iterative-raw-solver
- Verification: research draft; every feature still requires implementation and corpus validation

## Purpose

This file enumerates what the future solver may measure. Inclusion here does
not mean a feature is useful, independent, cheap, or approved. Each feature
must later be classified as accepted, rejected, prototype-only, or requiring a
human-label study.

The key change from the current solver is that “image brightness” becomes one
small feature family among sensor safety, noise, multiscale structure, color,
regional conflict, semantic importance, display perception, and uncertainty.

## Feature Record Contract

Every measurement should carry:

```text
feature id and version
numeric value, units, and valid range
source and recipe identity
pipeline stage and color space
resolution and crop/orientation
sampling mask and valid-pixel fraction
uncertainty/confidence and fallback reason
computation cost
dependencies
```

No objective term should consume a bare value without provenance.

## 1. Raw Sensor And Metadata Features

### Black And White Encoding

- Per-plane black level and spatial deltas.
- White level and usable linear-response limit.
- Active area, masked areas, and black-level sample count.
- Fraction below black, inside range, near non-linear, near white, and clipped.
- Per-row/per-column drift where metadata or masked samples allow it.

Normalized sample:

```text
x_c = (r_c - b_c) / max(epsilon, w_c - b_c)
```

### Channel Headroom

For robust high percentile `q_c`, technical WB multiplier `g_c`, and usable
limit `l_c`:

```text
headroom_c_ev = log2(l_c / max(epsilon, g_c * q_c))
minimum_headroom_ev = min_c(headroom_c_ev)
```

Track the limiting channel and the spread between channels. A single aggregate
cannot distinguish neutral all-channel clipping from partial-channel color
risk.

### Noise And SNR

When DNG `NoiseProfile` exists:

```text
variance_c(x) = S_c * x + O_c
sigma_c(x) = sqrt(variance_c(x))
snr_c(x) = x / max(epsilon, sigma_c(x))
```

Candidate lift `e` approximately scales signal and visible noise by `2^e`, but
later denoise, tone mapping, and display encoding change perception. Measure
predicted raw SNR and rendered residual noise separately.

Potential summaries:

- SNR percentiles inside shadow, subject, and background masks.
- Fraction of affected pixels below an SNR threshold.
- Chroma versus luma noise after demosaic.
- Noise amplification cost weighted by the candidate Local Range delta.
- Uncertainty when only ISO/BaselineNoise is available.

## 2. Global Scene-Linear Distribution Features

Define scene luminance in a declared working RGB space:

```text
Y = a_R R + a_G G + a_B B
L = log2(max(epsilon, Y) / referenceGrey)
```

Candidate features:

- p001, p01, p05, p10, p25, p50, p75, p90, p95, p99, p999.
- Log-average luminance.
- Inter-percentile spreads and tail mass.
- Histogram entropy and occupied EV range.
- Robust skewness or asymmetry around the median.
- Fractions below/above declared scene zones.
- Separate distributions for full image, crop, subject, background, skin,
  sky, and high-confidence neutral pixels.

Percentiles are robust summaries, not semantic truth. A black night scene and
an underexposed daylight scene can have similar histograms.

## 3. Regional And Semantic Features

Potential regions:

- primary subject and secondary subjects;
- faces and skin-like regions;
- sky, ground, foliage, architecture, and specular sources;
- bright connected background and dark connected foreground;
- clipped and near-clipped components;
- user crop and user-selected focus/importance regions.

For each region record:

```text
area fraction
median and percentile EV
contrast against surrounding region
edge confidence
mean/variance chromaticity
noise/SNR distribution
clipping state
mask uncertainty
distance from crop center and border
```

Semantic classification must be treated as uncertain evidence. It may guide
candidate generation or weights, but cannot override raw safety.

## 4. Multiscale Structure And Local Contrast

Let `ell = log(max(epsilon, Y))`. At scale `s`, compute a blurred/base image
`B_s(ell)` and detail/contrast representation:

```text
D_s = ell - B_s(ell)
G_s = gradient(B_s(ell))
```

Candidate measurements:

- detail energy per scale and region;
- loss or amplification of local contrast;
- edge magnitude and orientation preservation;
- texture masking around noise;
- large-scale illumination versus fine detail;
- subject/background contrast at several radii.

Durand and Dorsey separate an edge-preserving base layer from detail for HDR
compression. Guided filtering and local Laplacian filters provide alternative
edge-aware structures. These methods are research candidates for measurement
and mask construction, not automatic adoption.

Sources:

- https://graphics.cs.yale.edu/publications/fast-bilateral-filtering-display-high-dynamic-range-images
- https://people.csail.mit.edu/kaiming/eccv10/index.html
- https://people.csail.mit.edu/sparis/publi/2011/siggraph/

## 5. Halo And Gradient-Reversal Features

A solver needs a measurable artifact penalty rather than “look for halos” as a
manual instruction.

Initial candidate metrics:

1. Detect strong reference edges on the neutral scene image.
2. Sample signed gradients normal to each edge before and after the candidate.
3. Penalize sign reversals, overshoot, undershoot, and new extrema outside a
   narrow edge support.
4. Measure low-frequency bright/dark bands adjacent to the edge.
5. Weight by edge confidence, affected mask strength, and visibility in the
   display candidate.

Provisional form:

```text
L_halo = mean_edges(
    w_e * reversal(g_ref, g_candidate)
  + u_e * overshoot(profile_candidate, profile_ref)
  + v_e * band_energy(profile_difference)
)
```

This must be tested against real halo examples and edge-preserving methods.

## 6. Color, White Balance, And Gamut Features

Potential technical measurements:

- camera/as-shot neutral consistency;
- candidate gains and their channel-noise cost;
- Gray World, Shades of Gray, Gray Edge, and neutral-patch estimates;
- disagreement between illuminant estimators;
- multiple-illuminant evidence;
- chromaticity distribution by region;
- hue changes from scene candidate to display candidate;
- out-of-gamut fraction and distance;
- saturation compression and bright-color rolloff;
- partial-channel clipping and reconstructed color uncertainty.

Estimator disagreement is useful uncertainty. If camera metadata, neutral
samples, and edge-based constancy disagree strongly, global automatic WB should
be penalized or withheld.

Sources:

- https://pdfs.semanticscholar.org/acf3/6cdadfec869f136602ea41cad8b07e3f8ddb.pdf
- https://citeseerx.ist.psu.edu/document?doi=f14a55562e6f34d8fbae6dbaf01e0e8cc1ed6bad&repid=rep1&type=pdf

## 7. Highlight Recoverability Features

Separate these cases:

```text
display clipped, raw valid
one raw color plane clipped
multiple planes clipped with one valid
all planes clipped
near non-linear but not encoded as clipped
reconstruction already applied
```

Record component area, boundary complexity, neighboring valid colors, and
cross-channel correlation. A reconstructed value must carry uncertainty and
must never be reported as recovered ground truth.

The objective should penalize visible color discontinuities and halos around
reconstructed components, not only count clipped pixels.

## 8. Display And Perceptual Features

Potential measurements after View Transform:

- display p01/p50/p99 and clipping fractions;
- middle-grey and subject placement;
- black/white utilization without forcing full-range occupancy;
- highlight rolloff continuity and shadow-toe continuity;
- multiscale structural fidelity against the scene-linear reference;
- perceptual visibility of changes under a declared display model;
- statistical naturalness as a weak signal, not a hard truth;
- hue/gamut stability;
- readability that is not caused by hiding upstream placement errors.

Mantiuk et al. formulate display-adaptive tone mapping as minimizing visible
contrast distortions under display constraints. HDR-VDP-2 predicts visibility
and quality differences under luminance conditions. TMQI combines multiscale
structural fidelity with statistical naturalness. These provide objective-term
ideas, not drop-in Stack metrics.

Sources:

- https://www.cl.cam.ac.uk/~rkm38/pdfs/mantiuk08datm.pdf
- https://www.cl.cam.ac.uk/~rkm38/pdfs/mantiuk11hdrvdp2.pdf
- https://ece.uwaterloo.ca/~z70wang/research/tmqi/

## 9. Aesthetic And Preference Features

Technical quality and aesthetic preference must remain separate. Possible
preference evidence includes:

- user-selected intent such as natural, brighter subject, protect highlights,
  or low-noise;
- prior edits by the same user;
- subject importance;
- learned ranges from multiple expert adjustments;
- no-reference aesthetic/technical scores as advisory research signals.

MIT-Adobe FiveK demonstrates that expert retouchers produce different valid
results. NIMA predicts rating distributions rather than a single objective
truth. Neither should become a hidden aesthetic authority in Stack.

Sources:

- https://people.csail.mit.edu/sparis/publi/2011/cvpr_auto/Bychkovsky_11_Learning_Photo_Adjustment.pdf
- https://arxiv.org/abs/1709.05424

## 10. Uncertainty Features

Uncertainty should be first-class:

```text
measurement variance
sample count and valid fraction
stage completeness
metadata presence and trust level
proxy/full-resolution disagreement
semantic-mask confidence
estimator disagreement
demosaic sensitivity
candidate score variance
```

A robust objective may use:

```text
robustCost(theta) = expectedCost(theta) + kappa * uncertainty(theta)
```

or reduce the allowed parameter radius when uncertainty rises.

## Initial Feature Priority

Highest-value first prototypes:

1. Full per-channel raw headroom and `LinearResponseLimit` handling.
2. DNG `NoiseProfile`-based shadow-lift cost.
3. Orientation-normalized region records.
4. Multiscale local contrast before/after each candidate.
5. Gradient-reversal/halo metric on strong boundaries.
6. Display structural-fidelity and clipping measurements.
7. Proxy versus full-resolution agreement.

Semantic/aesthetic models should be evaluated only after technical objectives
and validation infrastructure are reliable.

