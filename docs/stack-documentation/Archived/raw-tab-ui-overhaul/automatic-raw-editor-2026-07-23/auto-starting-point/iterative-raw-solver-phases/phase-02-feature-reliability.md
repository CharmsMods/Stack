# Phase 02: Feature Reliability

- Program state: complete; `phase-02-v1` PASS; stopped before Phase 03
- Recipe mutation: prohibited
- Candidate optimization: prohibited
- Owns: non-mutating scene, regional, structural, artifact, color, noise, display, and uncertainty features

## Purpose

Implement and validate the measurements that will later tell the candidate
engine whether an image is actually improving. This phase proves features; it
does not optimize controls.

## Required Reading

Read the standard cold-start set, Phase 01 checkpoint, and:

```text
../iterative-raw-solver-research/data-domains-and-pipeline-order.md
../iterative-raw-solver-research/measurement-and-feature-catalog.md
../iterative-raw-solver-research/validation-data-and-experiments.md
accepted Phase 00 feature/math extractions
Phase 01 raw evidence schema and limitations
```

Follow `research-dependency-map.md` for the accepted guided/bilateral/local-
Laplacian, color, display, and perceptual source work.

## Entry Conditions

- Phase 01 passed with a frozen raw-evidence version.
- Controlled perturbations and real corpus categories exist.
- The first feature ledger identifies mandatory prototype candidates.
- Stage/color-space/reference definitions are accepted.
- Phase 02 is active in `../implementation-progress.md`.

## Required Feature Families

The first accepted set must be capable of describing:

### Scene placement

- robust global and region-specific EV distributions;
- log-average key and occupied range;
- high-key/low-key ambiguity evidence;
- subject/center/background placement without treating center as certain
  semantics.

### Regional conflict

- orientation-normalized region records;
- bright-background/dark-subject and shadow-region conflicts;
- mask confidence, leakage risk, and boundary support;
- current crop versus active-sensor provenance.

### Multiscale structure

- base/detail or contrast energy at declared scales;
- local-contrast preservation and amplification;
- texture-versus-noise sensitivity;
- scale and resolution requirements.

### Halos and gradient behavior

- reference edge selection;
- signed normal-profile comparison;
- gradient reversals, overshoot/undershoot, new extrema, and adjacent band
  energy;
- visibility weighting and confidence;
- known difficult clipped/mask boundaries.

### Noise

- raw-predicted SNR from Phase 01;
- rendered luma/chroma residual noise;
- predicted amplification under global/local lift;
- display visibility of noise;
- confidence when the metadata model is missing.

### Highlights and color

- partial/all-channel raw state carried forward;
- visible reconstruction discontinuity uncertainty;
- hue shift, saturation clipping, and gamut pressure;
- as-shot/neutral-estimator disagreement;
- mixed-light uncertainty.

### Display and proxy agreement

- linear-display and encoded-display metrics labeled separately;
- display clipping, middle placement, and rolloff/toe continuity;
- structural/contrast visibility terms approved in Phase 00;
- proxy-versus-full-resolution disagreement.

## Feature Record

Every emitted feature follows the contract in the research catalog and adds a
stable feature version. A downstream objective must be able to reject a feature
whose stage, recipe, resolution, or validity does not match the candidate.

## Experiment Requirements

For every prototype feature:

1. Run controlled exposure, clipping, noise, WB, halo, curve, and resolution
   perturbations.
2. Confirm directionality and monotonic regions where expected.
3. Measure repeatability across supported proxy scales and demosaic choices.
4. Compare against human-labeled artifacts or known signals.
5. Record camera/scene dependence and uncertainty.
6. Measure cost.
7. Classify it as accepted, rejected, prototype-only, or human-study required.

The accepted set may be smaller than the research catalog. Reliability matters
more than feature count.

## Required Tests

- Unit tests for stage/reference/EV definitions.
- Orientation and crop normalization tests.
- Controlled multiscale contrast response.
- Labeled halo/reversal/overshoot fixtures.
- Noise response across signal and lift levels.
- Hue/gamut and partial-channel stress cases.
- Linear-versus-encoded display metric tests.
- Proxy/full-resolution sensitivity records.
- Identity and stale-stage rejection.
- Regression tests proving no recipe or Pass 94 behavior changes.

## Checkpoint

Phase 02 passes only when an accepted first feature set covers:

```text
scene placement
regional conflict
noise cost
highlight/color risk
multiscale structure
halo/artifact risk
display quality
proxy/full-resolution disagreement
uncertainty
```

Each accepted feature must have reliable fixtures, real-corpus correlation,
declared cost, and a minimum valid resolution. Rejected features and unresolved
human-study items must remain visible in the ledger.

## Stop Rules

Do not:

- combine features into a winner score;
- search graph or slider values;
- accept a feature because its output image looks plausible;
- use semantic confidence to override raw safety;
- adopt a learned aesthetic or subject model as a requirement;
- let proxy metrics silently stand in for full-resolution noise or thin halos;
- tune features only on the images that motivated them.

## Handoff

Freeze the accepted feature version, rejected ledger, costs, required stages,
minimum resolutions, fixtures, and corpus correlation report. Phase 03 may
score only these accepted features.

Completed checkpoint: [phase-02/README.md](phase-02/README.md).

Frozen feature/API version: `rendered-features-v1`.

Phase 03 is not active. The next action is stop until the parent progress
ledger explicitly activates a bounded Phase 03 candidate-isolation slice.
