# Phase 07: Product Validation And Completion

- Program state: not active
- Recipe mutation: explicit final apply only, as established in Phase 06
- Owns: locked-corpus evaluation, native acceptance, regression, completion decision, and final stop

## Purpose

Determine whether the frozen precise solver is genuinely smarter, safer,
stronger when justified, and easier to continue editing than Pass 94. This
phase validates the product; it does not become an open-ended tuning phase.

## Required Reading

Read the standard cold-start set, Phase 06 checkpoint, and:

```text
program-contract.md
checkpoint-protocol.md
../iterative-raw-solver-research/validation-data-and-experiments.md
frozen corpus and partition manifest
frozen Pass 94 baseline records
frozen precise-solver and UI version records
all known limitations from Phases 00-06
```

## Entry Conditions

- Phase 06 passed.
- Solver, features, objective, optimizer, budgets, apply state machine, and UI
  are frozen for the candidate version.
- Development/validation decisions are closed.
- A genuinely locked test partition exists and has not informed this version.
- Review protocol and critical-failure definitions are approved.
- Phase 07 is active in `../implementation-progress.md`.

## Locked Evaluation

Compare at least:

```text
current/manual starting recipe where relevant
Pass 94 fast solver
precise solver candidate
```

Record per image:

- technical acceptability;
- critical failure labels;
- pairwise preference only after acceptability;
- strength: too weak, sufficient, or unjustifiably strong;
- highlight, noise, halo, color, and local-contrast results;
- whether well-exposed/high-key/low-key intent was preserved;
- next manual control the reviewer would change;
- visible recipe correctness and editability;
- convergence/termination state;
- runtime and candidate/render count;
- proxy/full-resolution agreement;
- deterministic repeat behavior.

Report by scene, camera/session, ISO/noise, clipping class, metadata coverage,
orientation/crop, and failure family.

## Critical Failures

At minimum, treat these as critical:

- new unsafe raw or color-channel clipping beyond policy;
- invalid/non-editable graph or recipe state;
- obvious halo, gradient reversal, or boundary band caused by the solve;
- severe noise amplification outside the accepted intent profile;
- destructive hue/gamut behavior or unjustified WB neutralization;
- hidden output mismatch with visible controls;
- failure to undo or partial apply;
- stale candidate applied to a different source or recipe;
- crash, hang, unbounded search, or unsafe failed-render behavior;
- systematic flattening of high-key, low-key, or already-good images.

No average quality gain can pay for an increased critical-failure rate without
an explicit product decision and remediation.

## Human Review

Use structured, preferably blinded comparisons. Separate:

```text
technically unacceptable
technically acceptable
preferred rendition
style disagreement
```

Reviewers should also answer whether the result is a good starting point and
whether the next manual edit is obvious from the visible controls. The solver
does not need to win stylistic preference when both results are safe; it must
not create a technically worse or harder-to-edit handoff.

## Failure Routing

If validation finds a failure:

| Failure | Return to |
| --- | --- |
| Bad/missing raw evidence | Phase 01 |
| Feature misses noise, halo, color, region, or display problem | Phase 02 |
| Constraint/objective ranks the wrong result | Phase 03 |
| Search fails despite a good measurable candidate | Phase 04 |
| Integration/full-resolution/report logic fails | Phase 05 |
| Apply, graph, UI, Undo, persistence, or cancellation fails | Phase 06 |
| Corpus/review protocol is inadequate | Phase 00 |

Any change creates a new version and re-enters the appropriate validation
path. Do not tune directly against locked-test examples and keep calling the
same partition locked.

## Checkpoint

### Completion Gate

The program may complete only when:

1. Every named mandatory failure family is represented and reported.
2. The precise solver improves technical acceptability or useful strength over
   Pass 94 across the agreed categories.
3. It does not raise the critical-failure rate.
4. Full-resolution verification catches proxy-only failures reliably.
5. Well-exposed, high-key, low-key, and style-sensitive scenes are not
   normalized blindly.
6. Native sliders, graphs, readouts, Undo, cancellation, save/load, and source
   identity behavior pass.
7. Repeated runs meet the determinism contract.
8. Runtime modes and budget-exhausted outcomes are understandable and useful.
9. Diagnostics explain representative successes, withholds, and failures.
10. Reviewers accept the output as a technically strong, editable starting
    point rather than an overprocessed final look.
11. The Pass 94 fast path remains a safe fallback.
12. Known limitations are documented and fail conservatively.

## Final Functionality

At completion, a user can open a RAW and explicitly request the precise
Starting Point solve. Stack can safely use raw technical evidence, scene and
regional measurements, multiscale/noise/color/artifact features, actual
candidate renders, constrained optimization, backtracking, convergence, and
full-resolution verification. It then writes one accepted recipe into the
appropriate visible controls and stops.

The solver may strongly reveal recoverable shadows, protect highlights, resolve
regional conflicts, author meaningful Local Range and Finish Tone graph points,
and fit the display when the evidence supports those choices. It may also
withhold a control or leave an image nearly unchanged.

## Stop Rules

### Final Stop

After this gate passes, stop expanding the workstream. Completion does not
authorize:

- final artistic grading;
- continuous auto editing;
- hidden raw enhancement layers;
- automatic crop/retouch/object work;
- mandatory semantic or learned aesthetics;
- unrelated RAW/editor redesign;
- indefinite optimizer research after the accepted product meets the contract.

New capabilities outside `program-contract.md` require a new explicitly
approved workstream.

## Completion Handoff

Record:

```text
Decision: PASS or not complete
Frozen production version
Frozen corpus and test version
Pass 94 comparison summary
Critical-failure summary
Human review summary
Runtime/convergence summary
Native build/test commands
Known limitations and safe fallbacks
Final stop boundary
```

Only after this record exists and all gates pass may
`../implementation-progress.md` mark the iterative precise solver program
complete.
