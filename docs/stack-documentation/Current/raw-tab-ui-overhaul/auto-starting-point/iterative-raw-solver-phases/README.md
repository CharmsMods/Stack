# Iterative RAW Solver Guided Phases

- Captured: 2026-07-09 19:02
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-09-1902-iterative-solver-phase-program.md`
- Type: update
- Topic: iterative-raw-solver
- Verification: phase design checked against the active RAW state, implementation contract, and iterative-solver research bundle

## Purpose

This folder is the gated research-to-production program for Stack's future
precise one-click RAW Starting Point solver. It tells an implementation agent:

- which research and image-science documents must be read for each phase;
- which mathematical claims must be converted into Stack-specific contracts;
- what code may be written at that point;
- what evidence must exist before the next phase starts;
- when recipe mutation is still forbidden;
- what finished functionality means; and
- where the workstream must stop rather than expanding into a hidden automatic
  photo-finishing system.

This folder does not replace the current solver, the active pass ledger, or the
research bundle. It consumes them.

## Authority And Routing

The authority order is:

```text
../implementation-progress.md
-> ../implementation-contract.md
-> this README and program-contract.md
-> checkpoint-protocol.md
-> the active numbered phase
-> research-dependency-map.md and the phase's required research
```

`../implementation-progress.md` remains the only active code-pass tracker. The
numbered files here are a program map, not permission to declare a phase active.
When this program begins, the parent progress file must name the active phase,
the current implementation slice, the allowed work, and the stop rule.

If a numbered phase conflicts with `../implementation-contract.md`, stop and
reconcile the documentation before changing code.

## Cold-Start Reading Order

Before any phase work:

```text
../implementation-progress.md
../README.md
../agent-reread-guide.md
../implementation-contract.md
README.md
program-contract.md
checkpoint-protocol.md
the active phase file
research-dependency-map.md
the research files required by that phase
```

For code work, also read `../implementation-pass-readiness.md` and inspect the
current code rather than trusting historical line references.

## Start, Finish, And Stop

The program starts with research closure and a frozen Pass 94 baseline. The
first solver-related production code appears in Phase 01 and is diagnostic
only: it measures raw evidence without changing the recipe.

The program finishes only when one explicit action can analyze a RAW, search
real rendered alternatives, converge or fail honestly, verify the winning
candidate at full resolution, and write the accepted result into Stack's
visible editable controls with one Undo.

The program stops at an excellent technical starting point. It does not own a
final creative grade, invisible enhancement texture, automatic crop,
retouching, object removal, creative effects, or continuous rewriting after
the one-click action ends.

`program-contract.md` defines these boundaries precisely.

## Phase Index

| Phase | Name | Recipe mutation | Required outcome |
| --- | --- | --- | --- |
| 00 | Research Closure And Baseline | None | Stack-specific math contracts, representative corpus, frozen Pass 94 baseline, and prototype-ready gates. |
| 01 | RAW Evidence Foundation | None | Trustworthy raw metadata/mosaic safety and uncertainty diagnostics with fixtures. |
| 02 | Feature Reliability | None | Validated scene, regional, multiscale, halo, color, noise, display, and resolution features. |
| 03 | Candidate Engine And Objective Surfaces | None | Isolated alternative renders, constraints/objective records, and measured low-dimensional surfaces. |
| 04 | Optimizer Selection | None | Evidence-based optimizer choice, convergence rules, and bounded evaluation budgets. |
| 05 | Dry-Run Precise Solver | None | End-to-end candidate search and full-resolution verification that produces a report but does not apply it. |
| 06 | Visible One-Click Integration | Explicit final apply only | Accepted candidates become visible sliders/graphs with one Undo and safe cancellation. |
| 07 | Product Validation And Completion | Explicit final apply only | Locked-corpus and native review prove the precise solver is safer and better than the frozen baseline. |

Recipe mutation is prohibited through Phase 05. Temporary candidate textures
or isolated recipe objects may exist for evaluation, but they may not alter the
current project recipe or masquerade as an applied edit.

## Required Phase Order

```text
phase-00-research-closure-and-baseline.md
phase-01-raw-evidence-foundation.md
phase-02-feature-reliability.md
phase-03-candidate-engine-and-objective-surfaces.md
phase-04-optimizer-selection.md
phase-05-dry-run-precise-solver.md
phase-06-visible-one-click-integration.md
phase-07-product-validation-and-completion.md
```

Phases may contain multiple small implementation passes. A phase number is not
a reason to make one large change. Every slice must compile, remain
inspectable, and update the parent active progress file.

## Gate Rule

No phase completes because code exists. A phase completes only when:

1. every entry condition was met;
2. every required deliverable exists;
3. automated checks pass;
4. required real-image or controlled-fixture evidence is recorded;
5. failures and limitations are documented;
6. the phase exit decision is explicit; and
7. the next phase can begin without assuming an unverified claim.

If a gate fails, remain in the current phase or return to the phase that owns
the failed assumption. Do not tune around a broken measurement or weaken a
constraint to make a checkpoint pass.

## Program Completion In One Sentence

Open a RAW, click Build Starting Point in the chosen precise mode, wait while
Stack measures and compares safe alternatives, and receive a strong natural
starting recipe whose Exposure, eligible WB, Local Range, Finish Tone, and
Display Fit decisions are visible, editable, explainable, reversible, and
verified against the full-resolution image.

## Document Ownership

| Document | Owns |
| --- | --- |
| `program-contract.md` | Product start line, finish line, stop boundary, invariants, and definition of done. |
| `checkpoint-protocol.md` | Phase activation, evidence, checkpoint, failure, and handoff rules. |
| `research-dependency-map.md` | Exact routes from phase questions to research, math, data science, image science, standards, and source backlog. |
| `phase-00-research-closure-and-baseline.md` | Research acceptance, corpus readiness, objective v0, and baseline freeze. |
| `phase-01-raw-evidence-foundation.md` | Raw metadata/mosaic measurements and uncertainty. |
| `phase-02-feature-reliability.md` | Non-mutating feature prototypes and reliability decisions. |
| `phase-03-candidate-engine-and-objective-surfaces.md` | Candidate isolation, evaluation records, constraints, and surface sampling. |
| `phase-04-optimizer-selection.md` | Algorithm comparison, convergence, and budget selection. |
| `phase-05-dry-run-precise-solver.md` | End-to-end non-applying precise solve. |
| `phase-06-visible-one-click-integration.md` | Full-resolution verified visible recipe application and native integration. |
| `phase-07-product-validation-and-completion.md` | Locked-corpus acceptance, regression, product completion, and final stop. |

## Current Status

```text
Program documentation: created
Completed program phase: Phase 06 - Visible One-Click Integration
Active program phase: none; Phase 07 is inactive
Current production baseline: Pass 94 heuristic solver
Checkpoint: phase-06/README.md (`phase-06-v1`, PASS)
Selected integrated solver: `raw-precise-integration-v1` using frozen `raw-precise-dry-run-v1`
Next action: stop; Phase 07 requires explicit activation in the parent ledger
Implementation authorization from this folder alone: none
```
