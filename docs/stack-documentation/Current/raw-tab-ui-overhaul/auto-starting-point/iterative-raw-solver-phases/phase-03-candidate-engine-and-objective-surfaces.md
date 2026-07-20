# Phase 03: Candidate Engine And Objective Surfaces

- Program state: completed as `phase-03-v1`; Phase 04 is not active
- Current project recipe mutation: prohibited
- Optimization algorithm selection: prohibited
- Owns: isolated candidate renders, constraint/objective records, cache identity, and measured parameter surfaces

Checkpoint: [Phase 03 PASS](phase-03/README.md). Frozen versions, schema,
surface dataset, proxy/full evidence, evaluation costs, and Phase 04 benchmark
requirements are linked there. Stop before Phase 04 activation.

## Purpose

Turn accepted measurements into an isolated candidate-evaluation system and
sample how Stack's actual renderer responds to visible-control alternatives.
The result determines whether the proposed objective is useful and which
optimizer family is appropriate.

## Required Reading

Read the standard cold-start set, Phase 02 checkpoint, and:

```text
../implementation-contract.md
../auto-starting-point-sampling-design.md
../iterative-raw-solver-research/objective-function-and-constraints.md
../iterative-raw-solver-research/optimizer-and-convergence-design.md
../iterative-raw-solver-research/validation-data-and-experiments.md
accepted objective/constraint v0
accepted feature ledger
```

## Entry Conditions

- Phase 02 passed with a frozen accepted feature version.
- Candidate identity and no-mutation designs were approved.
- Hard constraints have automated fixtures.
- The initial visible parameter space and bounds are frozen for this experiment
  version.
- Phase 03 was explicitly activated in `../implementation-progress.md` before
  implementation and is now frozen by the checkpoint above.

## Candidate Evaluation Contract

For candidate vector `theta`, record:

```text
source/decode identity
base recipe identity
candidate visible recipe and parameter version
proxy resolution and render-stage identities
raw evidence version
feature version and values
constraint results by tier
individual objective terms and uncertainty
render/cache status and timing
full-resolution promotion status when requested
```

Candidate rendering must use an isolated recipe or equivalent offscreen state.
The editor's current project recipe and undo history remain untouched.

## Required Infrastructure

- Exact candidate recipe fingerprinting.
- Stage-correct proxy rendering for Exposure, Local Range, Finish Tone, and
  Display Fit evaluation.
- Cache keys that include source, decode, recipe, feature version, and
  resolution.
- Cancellation on source/base-recipe/decode change.
- Batched evaluation where it is safe and measurable.
- Hard-constraint evaluation before lower-tier scoring.
- Inspectable per-term diagnostics and candidate comparison records.
- A diagnostic command or harness for reproducible surface sampling.

## Objective Surface Experiments

Sample low-dimensional grids around the Pass 94 warm start, including:

```text
RAW Exposure
RAW Exposure x Local Range delta
Local Range target x width
Local Range delta x feather
Finish Tone shadow y x highlight y
Display white EV x shoulder
Display black EV x toe
selected cross-stage interactions
```

For each sample, plot or record:

- hard safety boundaries;
- individual technical/perceptual terms;
- uncertainty;
- human technical acceptability where practical;
- proxy/full-resolution disagreement;
- discontinuities, plateaus, local minima, and noise;
- runtime and cache reuse.

Use development and validation partitions. Keep the locked test set untouched.

## Objective Review

The provisional objective passes only if:

- better technical candidates usually improve the intended terms;
- critical visible failures trigger the owning constraint or penalty;
- terms do not duplicate or cancel each other invisibly;
- well-exposed/intentional high-key/low-key images are not forced toward one
  normal histogram;
- safe warm-start candidates are not displaced by tiny uncertain gains;
- human disagreements are labeled as preference when both outcomes are
  technically acceptable.

Revise or reject objective terms before selecting an optimizer.

## Required Tests

- Candidate/current-recipe isolation and undo-history preservation.
- Exact identity/cache hit and invalidation behavior.
- Stage recipe matching and stale evidence rejection.
- Constraint fixtures for raw, graph, ownership, NaN, and state safety.
- Candidate render failure and cancellation behavior.
- Reproducible surface sampling.
- Full-resolution promotion without production apply.
- Regression tests proving Build Starting Point remains Pass 94 behavior.

## Checkpoint

Phase 03 passes only when:

- isolated candidates cannot alter the current recipe;
- every candidate is fully identifiable and explainable;
- hard constraints reliably reject their fixtures;
- objective surfaces exist across all first-space dimensions and key
  interactions;
- objective terms correlate with the intended real and controlled failures;
- proxy/full-resolution limits are measured;
- surface shape, noise, discontinuities, and evaluation cost are sufficient to
  choose optimizer benchmarks;
- the frozen Pass 94 warm start remains a candidate in every experiment.

## Stop Rules

Do not:

- select an optimizer from literature before measuring Stack's surfaces;
- apply a candidate to visible controls;
- hide individual terms behind one total score;
- allow display readability to compensate for upstream raw/scene failure;
- tune against the locked test set;
- weaken a constraint to smooth the objective.

## Handoff

Freeze the candidate-engine version, objective/constraint version, parameter
bounds, surface dataset, proxy policy, and evaluation costs. Summarize which
optimizer properties the measured surfaces require. Phase 04 benchmarks methods
against this frozen problem.
