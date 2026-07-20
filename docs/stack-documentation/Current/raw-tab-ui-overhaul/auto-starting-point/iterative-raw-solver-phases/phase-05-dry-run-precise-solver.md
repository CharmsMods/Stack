# Phase 05: Dry-Run Precise Solver

- Program state: completed through the parent progress ledger
- Checkpoint: [phase-05/README.md](phase-05/README.md) (`phase-05-v1`, PASS)
- Current project recipe mutation: prohibited
- Visible slider/graph writes: prohibited
- Owns: end-to-end precise solve, full-resolution verification, and explainable winning recipe report

## Purpose

Integrate the accepted evidence, features, objective, candidate engine, and
optimizer into the complete future solve path without applying the result. This
is the final safety barrier before production recipe mutation.

## Required Reading

Read the standard cold-start set, Phase 04 checkpoint, and:

```text
program-contract.md
checkpoint-protocol.md
accepted raw/feature/objective versions
candidate/cache/cancellation contract
selected optimizer and convergence decision
../one-click-starting-point-alignment.md
../implementation-contract.md
```

## Entry Conditions

- Phase 04 passed.
- The selected optimizer and budgets are frozen.
- Full-resolution candidate promotion is available without applying a recipe.
- Every output dimension maps exactly to an existing visible Stack control.
- A diagnostic entrypoint can clearly distinguish a dry-run result from an
  applied result.
- Phase 05 is active in `../implementation-progress.md`.

## End-To-End Dry Run

The dry-run solver must execute:

```text
validate source/base recipe/ownership
-> load or compute raw evidence
-> render and measure neutral/current scene
-> generate Pass 94 warm start
-> derive uncertainty-adjusted safe bounds
-> render and evaluate alternatives
-> accept/reject/backtrack/refine
-> stop with a declared status
-> full-resolution verify the best candidate
-> emit a candidate recipe and explanation
-> leave the project recipe and undo history unchanged
```

## Required Report

For every solve record:

- source, base recipe, decode, solver, feature, objective, and corpus versions;
- active parameter dimensions and why others were withheld;
- warm-start values and score/constraint record;
- candidate count, render count, accepted iterations, and cache behavior;
- rejected candidates and their blocking constraints;
- convergence or termination status;
- proxy and full-resolution measurements;
- winning visible values and graph points;
- predicted improvement by objective tier;
- uncertainty and remaining warnings;
- why the winner beat or did not beat Pass 94;
- exact statement that no recipe was applied.

## Behavior Requirements

- Well-exposed images may retain the warm start or current recipe.
- Stronger edits are allowed only when raw, noise, color, and artifact evidence
  supports them.
- All dimensions remain within visible-control and user-ownership policy.
- Missing evidence narrows the space or blocks dimensions.
- Full-resolution failure rejects the proxy winner or shrinks toward the warm
  start.
- Source/base-recipe changes cancel the solve and invalidate candidate output.
- The candidate report must be serializable for offline comparison but must not
  be loadable as a hidden applied texture.

## Validation Requirements

Run development and validation corpus categories through both Pass 94 and the
dry-run solver. Record:

- critical technical failures;
- technical acceptability;
- pairwise preference where both are acceptable;
- next manual control a reviewer would touch;
- result strength and naturalness;
- convergence state and runtime;
- changed-control count and edit minimality;
- full-resolution rejection frequency;
- failure by camera, scene, ISO, clipping class, and metadata coverage.

Do not use the locked test set to tune the integrated solver.

## Required Tests

- End-to-end deterministic dry runs.
- Current recipe and Undo byte/state equivalence before and after.
- Source switch, same-key identity change, base-recipe edit, cancel, and render
  failure.
- Missing raw evidence and unsupported source types.
- Candidate-to-visible-control serialization fidelity.
- Full-resolution rejection and fallback.
- Budget exhaustion distinct from convergence.
- Well-exposed no-op and intentional high-/low-key cases.
- Representative dark, HDR, clipped, noisy, backlit, mixed-light, and
  halo-prone cases.

## Checkpoint

Phase 05 passes only when:

- the complete solver produces a valid visible-control candidate without
  mutating project state;
- full-resolution verification is reliable enough to catch proxy-only failures;
- candidate values exactly round-trip through the visible recipe schema;
- the precise dry run improves technical acceptability over Pass 94 on the
  validation set without increasing critical failures;
- failure/cancellation paths retain the warm start/current recipe safely;
- diagnostics are understandable enough to explain representative wins,
  withholds, and rejections;
- the runtime modes and budgets are usable for native integration;
- the implementation science is frozen before apply behavior is added.

## Stop Rules

Do not:

- write any slider or graph;
- create an ambiguous preview that looks applied;
- add UI polish as a substitute for failed solver validation;
- tune on the locked test set;
- change optimizer/objective/feature behavior incidentally while implementing
  the report;
- proceed when the candidate cannot round-trip exactly through visible fields.

## Handoff

Freeze the dry-run solver version, accepted parameter space, report schema,
runtime modes, validation results, and full-resolution policy. Phase 06 may add
atomic visible apply around this frozen solver; scientific changes return to
their owning earlier phase.

Phase 05 passed on July 10, 2026. The frozen handoff is
`raw-precise-dry-run-v1` with `raw-precise-dry-run-report-v1`, the
uncertainty-adjusted headroom gate, exact 13-field visible-recipe projection,
the four-source renderer report, and the no-apply contract recorded in the
checkpoint. Phase 06 is not active.
