# Phase 04: Optimizer Selection

- Program state: completed; checkpoint `phase-04/README.md` (`phase-04-v1`, PASS)
- Current project recipe mutation: prohibited
- Production solver integration: prohibited
- Owns: optimizer benchmark, convergence contract, determinism, and evaluation budget

## Purpose

Select the simplest optimizer that reliably improves the frozen Pass 94 warm
start on Stack's measured candidate surfaces while respecting constraints,
runtime, determinism, and explainability.

## Required Reading

Read the standard cold-start set, Phase 03 checkpoint, and:

```text
../iterative-raw-solver-research/optimizer-and-convergence-design.md
../iterative-raw-solver-research/objective-function-and-constraints.md
optimizer sources routed by ../iterative-raw-solver-research/source-ledger.md
frozen Phase 03 surface and evaluation-cost records
```

Read algorithm papers after the measured surface summary. The problem chooses
the benchmark; the reputation of an optimizer does not.

## Entry Conditions

- Phase 03 passed.
- Candidate engine, objective, constraints, parameter bounds, and surfaces are
  frozen for the benchmark version.
- Evaluation failures and proxy noise are characterized.
- Comparison budgets and reporting metrics are defined.
- Phase 04 is active in `../implementation-progress.md`.

## Required Benchmarks

Benchmark at least:

1. Pass 94 warm start with no additional search.
2. Structured coordinate or pattern search following control ownership and
   stage order.
3. A model-based derivative-free trust-region method suitable for bounded or
   constrained local refinement.

Benchmark Bayesian optimization or CMA-ES only if the surface/cost evidence
justifies their additional complexity. They are comparison candidates, not
required dependencies.

## Structured Hybrid Hypothesis

The initial method tested was:

```text
Pass 94 warm start
-> bounded one-dimensional RAW Exposure refinement
-> discrete/continuous Local Range proposal search
-> constrained monotonic Finish Tone refinement
-> Display Fit refinement after upstream acceptance
-> small joint trust-region correction
-> full-resolution candidate verification
```

Each subproblem may use a different appropriate method. "One optimizer" does
not require flattening every graph, mask, and slider into an undifferentiated
vector.

## Comparison Metrics

Use identical candidate bounds and declared budgets to compare:

- hard-constraint violations;
- technical acceptability and failure labels;
- improvement over warm start by individual objective tier;
- number of renders and accepted/rejected evaluations;
- determinism and run-to-run variation;
- convergence versus budget exhaustion;
- sensitivity to initialization and minor input perturbations;
- proxy/full-resolution disagreement;
- runtime, GPU memory, cancellation latency, and cache effectiveness;
- diagnostic explainability;
- implementation and maintenance complexity.

Report results per scene/failure category, not only as an average.

## Convergence Contract

Calibrate and freeze criteria for:

```text
minimum meaningful tier improvement
relative objective improvement for k accepted iterations
maximum normalized parameter step
trust-region or interval size
constraint-boundary oscillation
region/mask stability
proxy/full-resolution agreement
evaluation and time budget
```

The runtime result states must remain distinct:

```text
converged
safe improvement found, budget exhausted
warm start retained
blocked by missing evidence
canceled
candidate render failed
full-resolution verification rejected
```

Budget exhaustion is not convergence.

## Selection Rule

Prefer the method that is deterministic, constrained, inspectable, and
efficient enough while delivering meaningful quality. A statistically small
quality gain does not justify a much more opaque or failure-prone method.

The selected solver must retain:

- the Pass 94 fallback;
- accepted-state monotonicity by objective tier;
- backtracking or shrink behavior;
- exact candidate records;
- bounded runtime;
- safe cancellation.

## Required Tests

- Standard analytic black-box test functions where useful for implementation
  correctness.
- Frozen Phase 03 surfaces and real candidate engine evaluations.
- Constraint-boundary, plateau, discontinuity, and noisy-proxy cases.
- Repeatability with fixed input and budget.
- Cancellation and failed-render injection.
- Warm-start retention when no meaningful improvement exists.
- Full-resolution rejection fallback.
- No mutation of the current recipe or undo state.

## Checkpoint

Phase 04 passes only when:

- benchmark records justify a named optimizer/hybrid architecture;
- convergence thresholds and budgets are evidence-based and versioned;
- the method beats the warm start meaningfully on validation data without more
  critical failures;
- determinism, cancellation, and failed-render behavior pass;
- complexity is justified by measured benefit;
- the selected method can return a complete candidate recipe without applying
  it;
- deferred optimizer alternatives are documented rather than half-integrated.

Phase 04 passed these gates on 2026-07-10. The selected method is
`stage-ordered-pattern-search-v1`; convergence and budget are
`raw-convergence-contract-v1` and `raw-precise-budget-v1`. The frozen benchmark
and decision record are in `phase-04/README.md`. Phase 05 is not active.

## Stop Rules

Do not:

- add production UI or recipe apply;
- make stochastic optimization the default without a reproducibility contract;
- select the winner by one total average;
- consume the locked test set for algorithm choice;
- change features, objective, bounds, and optimizer simultaneously without
  versioning a new experiment;
- adopt machine learning as a shortcut around an inadequate objective.

## Handoff

Freeze the selected algorithm, versions, parameters, budgets, convergence
states, benchmark dataset, and fallback rules. Phase 05 integrates exactly this
selection into an end-to-end dry run.

Handoff frozen at `phase-04-v1`. Stop before Phase 05 activation.
