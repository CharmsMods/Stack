# Research Session Handoff

- Captured: 2026-07-09
- Source: iterative RAW solver research session before context compaction
- Type: research
- Topic: iterative-raw-solver
- Verification: superseded for implementation-critical claims by the completed
  Phase 00 checkpoint linked below

## Phase 00 Closure Update

The original handoff below records the state before Phase 00 and should remain
as history. Its "Not completed" list is superseded by:

```text
../iterative-raw-solver-phases/phase-00/README.md
```

Checkpoint `phase-00-v1` froze Pass 94, content-addressed the initial local
corpus, completed the required DNG/noise/clipping/luminance/WB and
multiscale/halo/display extractions, classified every first feature, bounded
the first visible parameter space, and specified candidate isolation and
Phase 01/02 fixtures. It deliberately did not select an optimizer, objective
weights, halo thresholds, or a production runtime budget. No solver behavior
or recipe was changed.

## Why This File Exists

This is the short continuity record for the first iterative-solver research
session. It preserves the conclusions and caveats that are easiest to lose when
conversation context is compacted.

Do not interpret the existence of the research bundle as approval to implement
an optimizer. The bundle is an organized starting point for deeper mathematical
research.

## State At Handoff

Completed in this session:

- audited the current solver's hard-coded/formula-driven and data-driven parts;
- confirmed that the current four-pass loop is dependency iteration, not
  numerical objective convergence;
- created a proposed raw-to-display data-domain order;
- created a feature catalog extending far beyond brightness percentiles;
- drafted a lexicographic constrained objective;
- drafted a hybrid render-in-the-loop optimizer architecture;
- drafted a validation corpus and experiment sequence;
- located and initially extracted standards, primary papers, and authoritative
  product/implementation sources;
- archived the user request and completed documentation intake routing.

Not completed:

- no paper has yet been converted into a complete Stack-specific equation and
  implementation crosswalk;
- no final objective term, threshold, weight, optimizer, or runtime budget is
  approved;
- no representative RAW corpus or experiment results exist;
- no solver or renderer code was changed for this research workstream.

## Most Important Current-Code Truth

The existing solver is a closed-loop heuristic planner:

```text
measure current image
-> evaluate fixed formulas, thresholds, and caps
-> apply a safe visible subset
-> render again
-> repeat up to four upstream passes
```

It does not currently:

- sample a neighborhood of competing graph/slider values;
- minimize a declared loss;
- backtrack from a worse candidate;
- build a local response model;
- stop because objective improvement converged;
- prove the chosen candidate is better than nearby safe candidates.

The current system should be retained as a fast mode, warm start, safety prior,
and fallback baseline.

## Strongest Research Conclusions So Far

### 1. Data Domain Must Be Part Of Every Measurement

A percentile or clipping fraction is unusable without its stage, color space,
reference, crop/orientation, recipe identity, resolution, and uncertainty.

The research architecture must keep raw mosaic safety, demosaiced scene-linear
analysis, local/tone candidate analysis, and display/perceptual analysis
separate.

### 2. There Are Two Loops

Do not combine:

```text
technical evidence loop:
decode -> raw normalization/headroom/noise/color evidence -> cache

visible optimization loop:
propose visible recipe -> proxy render -> measure -> constrain/score -> refine
```

Technical evidence changes with source/decode state. Candidate evidence changes
with every visible recipe proposal.

### 3. Raw Safety Should Be A Constraint, Not A Score Tradeoff

No improvement in shadow visibility, contrast, or aesthetic score may purchase
more raw clipping or violate user ownership. The objective should be
lexicographic:

```text
identity/ownership validity
-> raw and graph safety
-> technical quality
-> perceptual/display quality
-> aesthetic/user intent
-> edit minimality/runtime
```

### 4. The Solver Needs More Than Brightness Statistics

The first high-value feature families are:

- per-channel raw headroom and non-linearity limits;
- signal-dependent noise and SNR;
- orientation-normalized regional/subject/background distributions;
- multiscale local contrast and structure;
- halo, overshoot, mask-leak, and gradient-reversal measurements;
- hue, gamut, WB-estimator disagreement, and partial-channel clipping;
- display clipping, contrast visibility, and perceptual structural fidelity;
- uncertainty and proxy/full-resolution disagreement.

### 5. The Likely Optimizer Is Hybrid And Structured

The initial recommended research hypothesis is:

1. Current heuristic warm start.
2. Raw-derived safe parameter bounds.
3. Bounded one-dimensional RAW Exposure search.
4. Mixed discrete/continuous Local Range search over region, target EV, delta,
   width, and feather.
5. Constrained monotonic Finish Tone knot optimization.
6. Display Fit search after upstream scene controls settle.
7. Small joint derivative-free trust-region refinement.
8. Full-resolution safety/artifact verification before visible apply.

Structured coordinate/pattern search and model-based derivative-free trust
regions should be benchmarked first. Bayesian optimization and CMA-ES are
comparison candidates, not default decisions.

### 6. Learned Methods May Propose, Not Authorize

MIT-Adobe FiveK and Deep Bilateral Learning show that content-dependent human
edits can be modeled and approximated. NIMA-style models show that rating
distributions can represent technical/aesthetic preferences.

If researched later, learned systems may propose warm starts, region masks, or
priors. Deterministic raw safety, graph validity, user ownership, and
full-resolution verification remain mandatory.

## Specific Source Findings Preserved From This Session

### Adobe DNG 1.7.1.0

The specification is the highest-priority raw source. Initial extraction found:

- `AsShotNeutral` records capture white balance as neutral coordinates in
  linear reference values.
- `BaselineExposure` adjusts the exposure-control zero point because camera
  models trade highlight headroom against shadow noise differently.
- `LinearResponseLimit` marks the encoding fraction above which sensor response
  may become significantly non-linear and cause highlight color shifts.
- `MaskedAreas` may provide fully masked pixels for black-level measurement.
- `NoiseProfile` models normalized raw standard deviation as:

```text
sigma_c(x) = sqrt(S_c * x + O_c)
```

  where `S_c` models signal-dependent shot noise and `O_c` models read-noise
  variance.
- Opcode lists distinguish operations on raw data as read, after mapping to
  linear reference values, and after demosaic.

Source:
https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf

The full solver-relevant tag and unit crosswalk remains unfinished.

### Adobe Camera Raw And Adaptive Profile

Adobe Camera Raw confirms that Auto analyzes an image and writes normal visible
tone controls, while still describing Auto as an initial approximation.

Adobe's Adaptive Profile overview describes global, regional, and local tone
behavior using a Profile Gain Table Map, plus separate color tables including
sky and main-subject components. This is evidence that robust commercial
systems use spatial/semantic data beyond one histogram. It does not expose
enough math by itself to reproduce Adobe's system.

Sources:

- https://helpx.adobe.com/camera-raw/using/make-color-tonal-adjustments-camera.html
- https://blog.adobe.com/en/publish/2024/10/14/the-adobe-adaptive-profile

### darktable And OpenColorIO

darktable supplies authoritative implementation-oriented evidence for:

- raw black/white, highlight reconstruction, raw denoise, and demosaic ordering;
- a scene-referred tone equalizer using a guided luminance mask and EV graph;
- separation of scene editing from sigmoid/filmic display mapping;
- the fact that extreme or noisy minimum/maximum measurements can fail.

OpenColorIO supplies the clean boundary definition for a view transform from
scene-referred reference space to display-referred reference space.

### Primary Tone/Edge Research

- Reinhard: photographic scene key and tone reproduction.
- Durand/Dorsey: edge-preserving base/detail dynamic-range compression.
- Guided filter: efficient edge-aware filtering for mask/base construction.
- Local Laplacian: multiscale edge-aware tone/detail manipulation designed to
  avoid halos.
- Mantiuk display-adaptive tone mapping: contrast-distortion minimization under
  display constraints, including a quadratic-program direction.
- HDR-VDP-2: predicted visibility/quality differences under luminance and
  contrast-masking conditions.
- TMQI: multiscale structural fidelity plus statistical naturalness.

These are candidate components and metrics. None is a complete Stack solver.

### Optimization Research

- Powell-style model-based derivative-free methods provide local trust-region
  search without analytic gradients.
- Bayesian optimization can use expensive evaluations efficiently and can
  account for variable cost and parallel candidates.
- CMA-ES handles nonlinear non-convex continuous black-box problems but may
  require many stochastic evaluations.

The actual Stack objective surface must be sampled before selecting among them.

## What Is Still Most Uncertain

1. Which raw corrections must be present in analysis proxies before their
   statistics are trustworthy.
2. How to construct a halo metric that correlates with human reports across
   clipped edges, masks, and tone curves.
3. How accurately DNG NoiseProfile or metadata fallbacks predict visible noise
   after demosaic, denoise, local lift, tone, and display mapping.
4. Which perceptual metric can be used without a unique human-edited reference.
5. How to encode acceptable ranges for low-key, high-key, backlit, mixed-light,
   and stylized photographs.
6. Whether Local Range mask/region assignments should stay fixed during the
   search or update with accepted candidates.
7. How to compare camera/as-shot intent, neutral estimates, and multiple
   illuminants without forcing a global “correct” WB.
8. Whether the full visible parameter space is smooth enough for local
   trust-region search.
9. Licensing and redistribution rules for candidate public RAW datasets and
   learned models.

## Exact Next Research Sequence

1. Fully extract DNG solver-relevant tags into a table with stage, units,
   formula, fallback, current Stack coverage, and validation fixture.
2. Fully derive the Display Adaptive Tone Mapping objective and decide which
   contrast/display components can be separated from its original assumptions.
3. Compare bilateral, guided, and local-Laplacian methods on halo generation,
   gradient reversal, mask stability, and cost.
4. Derive a shadow-lift noise penalty from NoiseProfile and primary
   Poisson-Gaussian noise literature.
5. Build a paper-only definition of the first objective terms and hard
   constraints; still no code.
6. Define the local RAW manifest and failure taxonomy for the validation corpus.
7. Sample low-dimensional objective surfaces offline or diagnostically only
   after the feature/constraint design is approved.
8. Use those surfaces to choose an optimizer family and evaluation budget.

## Resume Instruction

After compaction, start with this file and `README.md`. Then continue with the
first unfinished item in the exact next research sequence. Do not reread every
long parent research file unless the routed entry documents identify a conflict.
