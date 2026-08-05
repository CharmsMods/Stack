# Phase 01 And Phase 02 Fixture Plan v0

## Fixture Rules

- Small synthetic arrays/recipes may live in Git.
- Large/proprietary RAWs stay external and are addressed by the corpus
  manifest.
- Every mathematical fixture has an analytic or explicitly generated expected
  result and tolerance.
- Synthetic fixtures prove math/plumbing, not photographic readiness.
- Phase 01 and 02 remain non-mutating.

## Phase 01: Raw Evidence Foundation

| ID | Fixture | Required assertions |
| --- | --- | --- |
| `P01-ID-01` | Same bytes at two paths | Same source hash and group; no split/cache duplication. |
| `P01-ID-02` | One-byte source change | Different source/evidence identity; pending old result stale. |
| `P01-DECODE-01` | Toggle each decode-affecting field | Decode identity changes exactly when required. |
| `P01-NORM-01` | Constant black/white ramp | Zero maps to 0, white to 1, intermediate values analytic. |
| `P01-NORM-02` | Repeating per-plane BlackLevel | Correct CFA-plane normalization and ActiveArea origin. |
| `P01-NORM-03` | H/V black deltas | Correct row/column sum and denominator; no orientation confusion. |
| `P01-NORM-04` | Linearization table | Table is applied before black subtraction. |
| `P01-AREA-01` | ActiveArea plus all four orientation tags | Sensor samples invariant; photographic coordinates transform correctly. |
| `P01-AREA-02` | Masked rectangles and user/default crop | Masked black evidence and crop evidence remain distinct. |
| `P01-CLIP-01` | No, near, and clipped samples per plane | Exact counts/fractions and threshold boundary behavior. |
| `P01-CLIP-02` | CFA superpixels with 0/1/2/all planes clipped | Exact no/partial/all classification; mosaic-only path cannot overclaim. |
| `P01-LRL-01` | LRL below white and missing LRL | Non-linear pressure and fallback/uncertainty are correct. |
| `P01-WB-01` | Known plane gains and percentile | Per-plane headroom and limiting plane match analytic EV. |
| `P01-NOISE-01` | Known `S,O` at several `x` | Sigma, variance, and SNR match formula. |
| `P01-NOISE-02` | WB/exposure gains | Variance scales by gain squared; SNR invariance within tolerance. |
| `P01-META-01` | Present/missing AsShotNeutral, BaselineExposure, NoiseProfile, opcodes, gain maps | Provenance, capability, fallback, and uncertainty are explicit. |
| `P01-STATE-01` | Success, render failure, cancel, source/recipe/decode change | Recipe bytes, dirty state, graph ownership, and undo depth unchanged. |
| `P01-REAL-01` | Eight frozen Tennis DNG records | Reproduce metadata/orientation/bit depth and sampled per-plane clip ordering. |
| `P01-REAL-02` | Representative ARWs from each manifest session | Stable identity, decode record, and missing-metadata behavior. |

Phase 01 exits only when these outputs include units, stage, plane order,
validity, uncertainty, and source/decode identity. It may not implement scene
quality scoring.

## Phase 02: Measurement And Artifact Foundation

| ID | Fixture | Required assertions |
| --- | --- | --- |
| `P02-LUMA-01` | Known working-to-XYZ matrix and RGB patches | Y/EV exact; Rec.709 constants are not silently used. |
| `P02-LUMA-02` | Negative/zero/positive linear values | Negative fraction recorded; log epsilon policy exact. |
| `P02-PROXY-01` | Thin lines, texture, smooth gradients at resolution ladder | Per-feature minimum resolution and proxy/full delta recorded. |
| `P02-FILTER-01` | Step+ramp+texture for bilateral/guided/local-Laplacian | Support, base/detail response, cost, and parameter monotonicity compared. |
| `P02-HALO-01` | Step edges with signed overshoot bands at multiple widths | Reversal, overshoot, band energy, shift, and scale ordering exact. |
| `P02-HALO-02` | Texture adjacent to strong edge | Contrast-halo texture-ratio response without false positive far from edge. |
| `P02-HALO-03` | Staircase and compartment patterns | Separate artifact terms respond to their own perturbations. |
| `P02-HALO-04` | Smooth sky/tree, window/wall, roof/sky, light/dark fabric real crops | Human labels correlate with high-percentile profile terms. |
| `P02-NOISE-01` | Poisson-Gaussian noisy linear patches under local/global lift | Prediction and rendered residual direction; no reward for noise as detail. |
| `P02-WB-01` | Neutral chart, single-color scene, mixed lights, intentional warm/cool | Estimator agreement/conflict and confidence policy behave as designed. |
| `P02-COLOR-01` | Hue wheel, near-neutral, saturated highlights, gamut boundary | Hue reliability and gamut-pressure behavior. |
| `P02-GRAPH-01` | Finish curves with duplicate x, reversal, extreme slope/curvature | Validity, slopes, curvature, serialization, and constraints exact. |
| `P02-GRAPH-02` | Local graph endpoints, overlap, 12-point capacity, delta extremes | Order/capacity/ownership constraints exact. |
| `P02-DISPLAY-01` | Black/white/grey/shoulder/toe sweeps | Clip, mid-grey, rolloff, monotonicity terms move in expected direction. |
| `P02-DISPLAY-02` | Known/unknown absolute display model | Calibrated terms enabled only with required inputs. |
| `P02-REAL-01` | Dark/noisy, wide-range, partial clip, backlit, high/low key, saturated light, bright boundary, ordinary no-op | Per-family human technical acceptability and feature correlation. |

## Real-Image Acquisition/Review Quotas

Before a feature becomes an objective term, review at least two independent
sessions for the failure it claims to measure and include both positive and
negative examples. Before Phase 07, the locked plan in `corpus-contract-v1.md`
applies across at least three camera families.

## Automated Record Fields

Every fixture result stores:

```text
fixture id and generator/version
source/corpus identity
feature version
stage, space, units, reference
expected and measured value/tolerance
valid fraction and uncertainty
proxy/full resolution
runtime and memory
pass/fail and reason
recipe/no-mutation assertion
```
