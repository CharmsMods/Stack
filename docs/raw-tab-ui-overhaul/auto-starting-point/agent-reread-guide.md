# Agent Reread Guide

## Reading Intent

This is the restart file for future RAW tab implementation work. Read it after
context compaction, after a long interruption, or whenever the task feels blurry.
It does not replace the research files. It tells an agent which smaller files to
reread first and which longer file to open for a specific decision.

The goal is memory fidelity. A future agent should not have to remember the
whole conversation to avoid the main mistakes.

## Repo Entrypoint Rule

The repo-root `AGENTS.md` is intentionally tiny and should route Codex here
automatically for RAW Auto Starting Point work. Treat `AGENTS.md` as the door,
not the source of detailed truth. The detailed state lives in
`implementation-progress.md`, and the detailed rules live in this folder.

If the thread resumes after compaction, do not trust memory of the active pass.
Open `implementation-progress.md`, follow its active checklist, and only then
touch code.

## The Short Contract

Automatic RAW processing in STACK must author visible manual controls. It must
not become a hidden output pass that makes the preview look good while leaving
the user without editable values.

The recurring beginner workflow is:

```text
Fit to see -> expose the scene -> fix regions -> shape contrast -> refit display
```

The recurring implementation workflow is:

```text
raw safety evidence constrains the edit
scene-linear evidence chooses visible control values
final display evidence checks readability
```

If a proposed change violates either of those chains, pause and reread
`human-workflow-notes.md` and `implementation-contract.md`.

## Long-Horizon Rule

Do not collapse this documentation into a one-pass implementation brief. The
docs intentionally describe a multi-update program: stage evidence first,
diagnostics next, conservative visible writes after that, and broader
Local Range / Finish Tone authoring only when validation and UI fidelity make
those edits explainable. Early-pass language is a risk-control sequence, not a
limit on the final system.

## Minimal Reread Set

After compaction or before implementation, read these in order. If starting
from the repo root, `AGENTS.md` intentionally routes through
`implementation-progress.md` first so the active pass is visible immediately.

1. `AGENTS.md` when starting from the repo root
2. `implementation-progress.md` when starting from the repo root, resuming, or
   changing code
3. `README.md`
4. `agent-reread-guide.md`
5. `human-workflow-notes.md`
6. `implementation-contract.md`
7. `implementation-pass-readiness.md` when the task involves code changes or a
   staged implementation plan

That set should be small enough to reload often. It is the canonical working
memory for naming, user explanation, ownership, and safety.

## Cold Start Procedure

When opening this folder with no reliable thread context, do this:

1. If starting from the repo root, read `AGENTS.md`.
2. If implementing or resuming implementation, read
   `implementation-progress.md`.
3. Read `README.md` for the folder purpose and current thesis.
4. Read this file for routing and stop conditions.
5. Read `human-workflow-notes.md` to recover the user-facing mental model.
6. Read `implementation-contract.md` to recover the non-negotiable rules.
7. Choose the task-specific document below.

If the next action is code implementation, read
`implementation-progress.md` and `implementation-pass-readiness.md` before
touching source files. If the next action is a prose answer to the user, read
only the task-specific long file needed for the answer unless uncertainty
remains.

## Task-Specific Reread Paths

Use this table to avoid rereading every long research file for every small UI or
code task.

| Task | Reread |
| --- | --- |
| UI labels, panel order, tooltip language | `human-workflow-notes.md`, then `auto-controls-ordering-research.md` |
| Auto/manual ownership, stale states, recompute timing | `implementation-contract.md`, then `auto-manual-compute-model.md` |
| Resume implementation or know what pass is active | `implementation-progress.md`, then `implementation-pass-readiness.md` |
| Multi-pass implementation sequencing, score formulas, validation records | `implementation-pass-readiness.md` |
| Current STACK readbacks, display-domain labels, DNG parsing/application | `code-web-research-readbacks-and-dng.md` |
| Build Starting Point button or candidate stages | `implementation-contract.md`, then `auto-starting-point-sampling-design.md` |
| Solver math for RAW Exposure, Local Range, Finish Tone, Display Fit | `auto-starting-point-solver-research.md` |
| Raw mosaic, demosaic boundary, clipping/headroom/noise evidence | `auto-raw-processing-math-and-science.md` |
| What is still unknown or validation-sensitive | `auto-starting-point-gap-audit.md` |

The long files are research sources. The smaller files are the working contract.
When they disagree, do not guess. Reconcile the conflict in the docs before
changing code.

## Lookup Scenarios

When returning to the folder to check one fact, search or open by the question
you are answering instead of rereading everything.

| Question | Open First | Useful Search Terms |
| --- | --- | --- |
| What should the UI call this control or action? | `human-workflow-notes.md` | `Preferred Labels`, `Beginner wording`, `Automatic Actions` |
| What order should the user learn the four controls in? | `human-workflow-notes.md` | `The Core Workflow`, `The Two Orders` |
| What order does the renderer/process use? | `auto-controls-ordering-research.md` | `Processing Order`, `Renderer position`, `Current Code Model` |
| Is this automatic value allowed to apply? | `implementation-contract.md` | `Control Ownership`, `Forbidden Shortcuts`, `visible manual controls` |
| Should this recompute live, after settle, or on request? | `auto-manual-compute-model.md` | `Recompute Timing Rules`, `Computed Live`, `Computed After Settle`, `Computed On Request` |
| Which image stage should be sampled? | `auto-starting-point-sampling-design.md` | `Which Image State To Sample`, `Stage Readbacks Needed` |
| What stats should a stage produce? | `implementation-pass-readiness.md` | `Required Stage Stats`, `SceneStageStats`, `DisplayStageStats` |
| What does the current code actually read back or parse from DNG? | `code-web-research-readbacks-and-dng.md` | `Current Readback Boundaries`, `ReadTextureStats`, `DNG Metadata` |
| What pass is active and what must not be skipped? | `implementation-progress.md` | `Current State`, `Next Allowed Work`, `Do not skip` |
| What math should choose RAW Exposure, Local Range, Finish Tone, or Display Fit? | `auto-starting-point-solver-research.md` | `RAW Exposure Solver`, `Local Range Solver`, `Finish Tone Solver`, `View Transform` |
| What raw-domain facts matter before demosaic? | `auto-raw-processing-math-and-science.md` | `Raw Safety Ledger`, `Raw Clipping`, `Noise`, `Demosaic` |
| What still needs validation instead of more theory? | `auto-starting-point-gap-audit.md` | `Validation Is Still Real Research`, `Product Policy Still Open` |
| What pass should implementation do next? | `implementation-pass-readiness.md` | `Multi-Pass Implementation Plan`, `Remaining True Gaps` |

For exact lookups, prefer `rg` over opening every file. Example:

```text
rg -n "Display Fit|View Transform|Build Starting Point" docs/raw-tab-ui-overhaul/auto-starting-point
```

After finding the answer, check whether it changes any non-negotiable rule in
`implementation-contract.md`. If it does, update the contract or explain why
the new information is only supporting research.

## Compaction Recovery Checklist

When a new agent resumes this work, do this before writing code:

1. Reread the minimal set above.
2. Read `implementation-progress.md` to learn the active pass, last completed
   work, next allowed work, active checklist, verification commands, and stop
   conditions.
3. Run `rg --files docs/raw-tab-ui-overhaul/auto-starting-point` to see whether
   new docs were added.
4. Check the current code instead of trusting old notes for exact behavior.
5. Identify which of the four controls the work touches.
6. Confirm that every automatic value lands in a visible recipe field.
7. Update `implementation-progress.md` before starting and after finishing an
   implementation pass.
8. Update this folder when the code dig reveals new behavior or when a design
   decision becomes more precise.

Do not wait until the end of a long research or implementation pass to update
docs. This folder exists because previous context ran out.

## Four Questions To Keep The Agent On Track

Every RAW auto feature should answer these questions clearly:

1. Which control owns this correction?
2. Which image stage is being measured?
3. Is the result visible and editable in the manual UI?
4. What makes the automatic state stop changing after the user edits it?

If those answers are vague, the implementation is probably drifting toward a
hidden auto-enhance pass or a confusing UI state.

## Documentation Maintenance

Prefer small additions to the canonical files when the new information is a
rule future agents must obey. Prefer dated sections in the research files when
the new information is supporting evidence, source research, or exploratory
reasoning.

Do not split the long research files just to reduce line count. Split only when
a section has become a stable source of truth that agents need to reread by
itself.
