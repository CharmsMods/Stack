# Phase 06: Visible One-Click Integration

- Program state: completed at `phase-06-v1`; Phase 07 is inactive
- Recipe mutation: allowed only as one explicit atomic final apply
- Owns: native action, visible-control projection, Undo, cancellation, persistence, and handoff

## Purpose

Connect the frozen Phase 05 precise solver to Stack's one-click Starting Point
experience without changing the accepted image science. This is the first phase
allowed to apply a candidate to the current recipe.

## Required Reading

Read the standard cold-start set, Phase 05 checkpoint, and:

```text
../one-click-starting-point-alignment.md
../human-workflow-notes.md
../auto-manual-compute-model.md
../implementation-contract.md
../implementation-progress.md
Phase 05 report and candidate schema
current RAW tab, graph, undo, persistence, and editor-state code
```

## Entry Conditions

- Phase 05 passed.
- Solver science and output schema are frozen for this integration version.
- Every candidate dimension has an exact visible recipe owner.
- Full-resolution verification occurs before apply.
- Atomic apply and one-snapshot design are reviewed.
- Phase 06 is active in `../implementation-progress.md`.

## Required User Flow

The intended flow is:

```text
user explicitly starts Build Starting Point precise solve
-> UI reports analysis/search progress and remains cancelable
-> current recipe remains unchanged during search
-> solver converges, retains warm start, exhausts budget, blocks, or fails
-> full-resolution verification runs
-> accepted candidate applies atomically if policy allows
-> sliders, graphs, readouts, and diagnostics update together
-> one Undo restores the exact original recipe
-> solver stops and user owns the result
```

The exact product choice between one Build Starting Point action with a mode and
a clearly labeled precise variant may be finalized here. It must not restore a
confusing multi-action first-run workflow.

## Visible Projection Requirements

On successful apply:

- RAW Exposure slider equals the accepted value.
- WB mode/multipliers change only under the approved eligibility policy.
- Local Range graph points, widths/feathers, region/mask ownership, numeric
  readouts, and any bridge to advanced controls match the candidate.
- Finish Tone graph knots and numeric values match the candidate.
- Display Fit / View Transform sliders match the final verified candidate.
- Diagnostics record applied, withheld, neutral, rejected, and warning states.
- Save/load recreates the same recipe without rerunning the solver.

No output-only texture or hidden tone field may differ from the visible recipe.

## State And Ownership Requirements

- Capture one undo snapshot before the first atomic recipe write.
- Do not expose intermediate candidate writes in the project recipe.
- Cancel on source/decode/base-recipe change and discard stale results.
- Preserve existing user-owned graphs according to the approved explicit-solve
  policy.
- Mark Display Fit stale after later manual upstream edits rather than silently
  refitting.
- Do not continuously rerun or rewrite values after apply.
- A failed final render/apply cannot leave a partially applied recipe.
- Closing/switching the project or RAW source follows safe cancellation.

## Progress And Diagnostics

The UI should report useful bounded states such as:

```text
analyzing raw evidence
measuring scene
evaluating candidates N / budget
verifying full resolution
applying visible recipe
converged
safe improvement, budget exhausted
warm start retained
blocked by missing evidence
canceled
failed safely
```

Do not promise convergence before full-resolution verification.

## Required Tests

- Native one-click apply on representative real RAWs.
- Exact slider/graph/readout/candidate equality.
- One Undo restoration of the original recipe.
- Project save/load and reopening without automatic rerun.
- Source switch, recipe edit, same-key identity change, cancel, app close, and
  render failure during every major state.
- Failed apply atomicity and undo preservation.
- Existing user-owned curve policy.
- Well-exposed no-op/warm-start retention.
- Dark, HDR, clipped, noisy, backlit, mixed-light, and halo-prone cases.
- Fast Pass 94 mode remains available and unchanged unless a separately
  approved migration says otherwise.
- Release build, behavior tests, editor-state validation, and new solver tests.

## Checkpoint

Phase 06 passes only when:

- one explicit action reaches a safe final state without manual scaffolding;
- current recipe remains unchanged until the final atomic apply;
- every applied correction is visible and editable;
- full-resolution verified values exactly match sliders and graphs;
- Undo, cancellation, persistence, source switching, and failure atomicity pass;
- the solver stops after apply and respects subsequent manual ownership;
- native reviewers can understand what changed and continue editing naturally;
- Phase 07 can test a frozen product candidate rather than a moving science
  experiment.

Decision: PASS. Durable evidence, frozen versions, commands, native checks, and
known limits are recorded in [the Phase 06 checkpoint](phase-06/README.md) and
[validation summary](phase-06/validation-summary-v1.md).

## Stop Rules

Do not:

- change objective weights or optimizer behavior to fix UI integration;
- expose hidden candidate results as if applied;
- add continuous auto-compensation;
- overwrite user edits outside the explicit solve contract;
- expand into final creative grading or unrelated RAW controls;
- declare product completion from successful native smoke tests.

## Handoff

Frozen: `raw-precise-dry-run-v1`, `raw-precise-integration-v1`,
`raw-precise-native-runtime-v1`, Precise-default/Fast-retained product mode,
the 13-field visible projection, lifecycle, one atomic apply, one Undo, and the
checkpoint commands.

Next allowed slice: stop. Phase 07 evaluates this frozen version only after the
parent progress ledger explicitly activates it. Do not change solver science,
consume the locked set, or claim product completion before that activation.
