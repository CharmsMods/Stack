# Phase Checkpoint Protocol

## Purpose

This protocol prevents the iterative solver program from moving forward on
code volume, promising screenshots, or undocumented intuition. Every phase
must leave evidence that the next phase can consume without guessing.

## Activation Rule

A phase becomes active only when `../implementation-progress.md` records:

```text
Program: Iterative RAW Solver
Active phase: Phase NN - name
Current slice: bounded implementation or research task
Entry evidence: paths to prior checkpoint records
Allowed work: exact scope
Stop rule: exact prohibited next step
```

Do not create a second active progress ledger in this folder. The parent file
is authoritative for the whole RAW Starting Point workstream.

## Slice Rule

A phase may require many slices. Each slice should produce one coherent,
verifiable capability such as:

- one metadata family with fixtures;
- one feature extractor with controlled perturbation tests;
- one candidate-render isolation contract;
- one objective-surface experiment;
- one optimizer benchmark;
- one native apply/cancellation behavior.

Avoid combining new measurements, new scoring, new optimization, and new
production apply behavior in the same slice.

## Checkpoint Evidence

Every phase checkpoint must record:

```text
Phase and checkpoint id
Date and solver/feature version
Research and decisions consumed
Files and behavior changed
Automated tests and exact commands
Fixture or corpus version
Real-image categories reviewed
Measurements and units added
Known failures and uncertainty
Entry conditions verified
Exit conditions passed or failed
Explicit next allowed work
Explicit prohibited next work
```

Store durable experiment or validation records in an appropriate validation
location defined during Phase 00. Keep `../implementation-progress.md` short
and link to the durable record.

## Evidence Hierarchy

Use evidence in this order:

1. Source/recipe identity and automated state-integrity tests.
2. Controlled sensor, graph, and synthetic fixtures.
3. Repeatable measurements on the versioned local RAW corpus.
4. Objective-surface and optimizer experiment records.
5. Native Stack interaction tests.
6. Blinded or structured human review.

Synthetic fixtures can prove plumbing and mathematical response. They cannot
prove photographic readiness. A single visually successful RAW cannot set
constants or close a phase.

## Research Acceptance Rule

A paper or official specification becomes implementation guidance only after a
Stack-specific extraction records:

- variables and units;
- source data domain and color space;
- assumptions and required inputs;
- equations or algorithm steps;
- failure cases;
- computational cost;
- licensing/code implications;
- exact proposed Stack use;
- what the source does not establish;
- required fixtures and corpus validation.

“Initial extraction” in the research source ledger is not sufficient approval
for a production algorithm or objective weight.

## Measurement Acceptance Rule

A feature may enter an objective only when its record includes:

- version, units, valid range, stage, color space, and reference;
- source/recipe identity and proxy resolution;
- sampling mask and valid-pixel fraction;
- uncertainty and fallback behavior;
- controlled-perturbation response;
- proxy/full-resolution sensitivity;
- real-image correlation with the failure it claims to measure;
- cost and minimum valid resolution.

Classify every proposed feature as:

```text
accepted
rejected
prototype-only
needs human-label study
```

Do not silently retain unstable features because implementation effort was
already spent.

## Constraint And Objective Rule

Hard constraints must be tested before weighted or aesthetic scoring. At
minimum, isolate these tiers:

```text
Tier 0: source, recipe, graph, and ownership validity
Tier 1: raw safety and irreversible artifact constraints
Tier 2: technical quality
Tier 3: perceptual/display quality
Tier 4: declared user intent or aesthetic ranking
Tier 5: edit minimality and runtime tie-breakers
```

Each accepted candidate record must retain individual terms. A single total
score without reasons cannot pass a checkpoint.

## No-Mutation Gate

Through Phase 05:

- candidate work uses isolated recipe objects, offscreen/proxy renders, or
  diagnostic commands;
- the current project recipe does not change;
- candidate previews cannot be confused with applied edits;
- no graph or slider receives the result;
- test-only mutation must be confined to isolated fixtures and restored.

Phase 06 is the first phase allowed to atomically apply an accepted candidate.

## Failure Handling

When a checkpoint fails:

1. Record the failed assumption and evidence.
2. Identify the phase that owns it.
3. Return to that phase or keep the current phase active.
4. Preserve the fast Pass 94 fallback.
5. Do not weaken constraints, omit difficult images, or retune against the test
   set merely to advance.

A hard research uncertainty may narrow the first precise-solver parameter
space. It must not be replaced by unbounded confidence.

## Test-Set Discipline

Phase 00 must define development, validation, and locked test partitions.

- Development data may guide implementation.
- Validation data may guide parameter and policy choices.
- Locked test data may only evaluate a frozen candidate version.
- Near-duplicate captures and edits of the same RAW remain in one partition.
- Results must be reported by failure category and camera/session grouping,
  not only as one average.

If the locked set informs a design change, it is no longer locked for that
version and must be replaced or versioned honestly.

## Phase Exit Decision

Use one of these decisions:

```text
PASS: all mandatory evidence exists; next phase may activate.
CONDITIONAL PASS: only explicitly deferred optional scope remains; required
  first parameter space is still safe and useful.
FAIL: a required measurement, contract, or validation gate is not met.
BLOCKED: required user-owned data, licensing decision, or external capability
  is unavailable; no unsafe substitute is allowed.
```

A conditional pass must list the deferred scope and prove the next phase does
not silently depend on it.

## Handoff Template

End each completed phase file or linked checkpoint record with:

```text
Decision:
Accepted artifacts:
Rejected approaches:
Frozen versions:
Known limitations:
Next phase entry evidence:
Next allowed slice:
Do not do next:
```

## Program Completion Rule

Phase 07 may mark the program complete only when the definition of done in
`program-contract.md` is satisfied. Near-complete behavior, a successful demo,
or budget exhaustion is not completion.
