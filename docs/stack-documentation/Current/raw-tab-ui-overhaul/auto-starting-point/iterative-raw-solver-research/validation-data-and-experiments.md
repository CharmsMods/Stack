# Validation Data And Experiments

- Captured: 2026-07-09
- Source: current Stack validation rules, MIT-Adobe FiveK, HDR quality research, and user testing requirements
- Type: research
- Topic: iterative-raw-solver
- Verification: Phase 00 corpus contract and initial local manifest exist;
  photographic labels and locked multi-camera acquisition remain ongoing

## Purpose

A more complicated optimizer will only be smarter if its measurements,
constraints, and objective correlate with real failures and human review. This
document defines the evidence needed before tuning, training, or implementation
readiness claims.

## Dataset Layers

Use separate layers rather than one undifferentiated image folder.

### A. Sensor And Metadata Fixtures

- DNGs with black/white levels, masked areas, `NoiseProfile`,
  `LinearResponseLimit`, and multiple CFA layouts.
- Files with and without camera/as-shot WB.
- Linear/enhanced DNGs and proprietary RAWs decoded through LibRaw.
- Known clipping patterns: no clipping, one plane, two planes, all planes.
- Dark frames, flat fields, and controlled exposure brackets where available.

Purpose: validate raw normalization, headroom, noise, and metadata fallback.

### B. Photographic Stress Corpus

At minimum:

- severely underexposed but recoverable scenes;
- high dynamic range without sensor clipping;
- partial-channel and all-channel clipping;
- backlit people and objects;
- sunsets, fire, stage lighting, neon, and saturated highlights;
- night images with deep shadow noise;
- high-key snow/white scenes and low-key intentional dark scenes;
- portraits across skin tones and mixed illumination;
- foliage, architecture, water, specular reflections, and fine texture;
- flat/hazy scenes;
- images with bright borders in any orientation;
- rotated/orientation-tagged files;
- images with an existing user crop and existing manual graphs;
- ordinary well-exposed images that should change little.

### C. Controlled Synthetic Perturbations

Starting from trustworthy scene-linear images, generate controlled:

- exposure offsets;
- clipping thresholds and channel-specific saturation;
- Poisson-Gaussian noise levels;
- WB casts;
- local shadow/bright-region imbalance;
- halo and gradient-reversal artifacts;
- curve discontinuities;
- proxy-resolution changes.

Purpose: measure whether features move monotonically and detect known errors.

### D. Human-Edited Reference Sets

The MIT-Adobe FiveK dataset contains RAW photographs adjusted by multiple
trained retouchers. It is useful for studying ranges of valid global tonal
adjustment and preference variability:

https://people.csail.mit.edu/sparis/publi/2011/cvpr_auto/Bychkovsky_11_Learning_Photo_Adjustment.pdf

It must not be treated as Stack's unique target look. Dataset license and use
conditions must be reviewed before inclusion or derived artifacts enter the
repo.

### E. User-Owned Stack Review Set

Create a versioned local corpus with explicit permission and source identity.
For each file preserve:

```text
original RAW
camera metadata summary
current fast-solver result
candidate precise-solver results
visible control values
stage diagnostics
human review and failure labels
```

Large RAW files should not automatically be committed to the repository.

## Dataset Split Rules

If any learned proposal or calibrated weighting is introduced:

- split by capture session/camera/scene, not random near-duplicate frames;
- keep a locked test set that never informs weights;
- include out-of-distribution cameras and scene types;
- record every dataset version and transformation;
- avoid training and evaluating on different edits of the same RAW;
- report performance by failure category, not only one average score.

## No Single Ground Truth

Validation should separate:

```text
hard technical failure
acceptable technical result
preferred rendition
style disagreement
```

Multiple safe renditions can be acceptable. Human review should capture an
acceptable range and pairwise preference rather than demand one exact slider
vector.

## Metric Families

### Safety

- Per-channel raw headroom and clipping.
- Non-linear-response-limit violations.
- Invalid/NaN graph or recipe state.
- User ownership and undo correctness.
- Source/recipe evidence identity.

### Technical Image Quality

- Shadow visibility versus SNR/noise amplification.
- Highlight detail and reconstruction uncertainty.
- Multiscale local-contrast preservation.
- Halo, gradient reversal, overshoot, and mask leakage.
- Hue shift, gamut pressure, and saturation clipping.
- Proxy/full-resolution disagreement.

### Perceptual/Display Quality

- Display clipping and grey placement.
- HDR-VDP-style visible difference/quality where a reference is meaningful.
- TMQI-style structural fidelity and naturalness as research metrics.
- Pairwise human acceptability and preference.

### Solver Behavior

- Improvement over the current heuristic warm start.
- Candidate evaluations and accepted iterations.
- Convergence status versus budget exhaustion.
- Determinism across repeated runs.
- Sensitivity to proxy resolution and minor input perturbation.
- Runtime, GPU memory, and cancellation latency.

### Product Handoff

- Every applied change appears in visible controls.
- Graph points and slider values match the recorded candidate.
- One Undo restores the original recipe.
- Diagnostics explain rejected constraints and winning objective terms.
- Existing user-owned curves are preserved.

## Experiment Sequence

### Experiment 1: Measurement Reliability

For every proposed feature:

1. Run on controlled perturbations and real RAW categories.
2. Measure repeatability across resolution and demosaic choices.
3. Compare to a known signal or human-labeled artifact.
4. Reject features with unstable domain dependence or no useful correlation.

Output: accepted/rejected feature ledger.

### Experiment 2: Current Solver Baseline

Freeze the current Pass 94 solver and record:

- changed controls;
- objective/feature values before and after;
- human failure labels;
- runtime and render count.

This is the baseline every future method must beat.

### Experiment 3: Objective Surface Sampling

For selected RAWs, densely sample low-dimensional slices:

```text
RAW Exposure x Local Range delta
Local Range target x width
Finish Tone shadow y x highlight y
white EV x shoulder
```

Plot safety boundaries, objective terms, human acceptability, and local minima.
This determines whether coordinate search, trust-region DFO, or a different
method fits the actual surface.

### Experiment 4: Optimizer Comparison

Use the same bounds and evaluation budget for:

- structured coordinate/pattern search;
- model-based derivative-free trust region;
- Bayesian optimization;
- CMA-ES benchmark;
- heuristic warm start alone.

Compare quality, failures, evaluations, determinism, and explainability.

### Experiment 5: Objective Ablation

Remove one term at a time:

- no noise penalty;
- no halo penalty;
- no uncertainty;
- no semantic weighting;
- no edit-minimality term;
- no full-resolution verification.

Confirm that each retained term prevents a measurable failure.

### Experiment 6: Human Review

Use blinded side-by-side or pairwise review. Capture:

- technically acceptable yes/no;
- preferred result;
- reason codes such as too dark, too bright, clipped, noisy, flat, halo,
  unnatural color, overprocessed, or subject still hidden;
- confidence and reviewer identity/category;
- whether further manual editing feels easy from the visible controls.

### Experiment 7: Native Stack Handoff

Verify the winning candidate inside the actual RAW tab, including graph point
visibility, readouts, undo, source switching, and project persistence.

## Review Record Template

```text
Source key:
Source hash:
Camera / ISO:
Scene category:
Intent profile:
Solver mode and version:
Evaluation budget:
Convergence status:
Changed visible controls:
Raw safety result:
Noise result:
Highlight result:
Halo/edge result:
Color result:
Display result:
Proxy/full agreement:
Technical acceptability:
Preference rank:
Failure labels:
Reviewer notes:
```

## Readiness Gates

Research may move to an implementation prototype only when:

1. The corpus covers every named failure family.
2. Raw safety and graph constraints have automated fixtures.
3. The first feature set has reliability evidence.
4. Objective surface experiments justify an optimizer family.
5. The current solver baseline is frozen and reproducible.

A prototype may write production recipes only after:

1. Full-resolution verification is reliable.
2. Native visible-control handoff and undo pass.
3. The precise solver beats the baseline without raising critical failures.
4. Human reviewers accept the result across the locked real-RAW set.
5. Budget exhaustion, cancellation, and missing evidence fail safely.

## Immediate Data Work

- Phase 00 defined non-committed user-owned RAW locations and a content-addressed
  manifest without copying proprietary RAWs into the repository.
- Eight initial Tennis DNG raw-safety records and 178 unique Sony ARW identities
  are frozen in `../iterative-raw-solver-phases/phase-00/`.
- Continue structured labels and acquire independent clipped, noisy, normal,
  high-key, low-key, portrait, and mixed-light sessions before any objective
  weight is selected. New never-reviewed sessions populate the locked set.
