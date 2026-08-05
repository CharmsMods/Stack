# RAW Starting Point Archived Progress

Last updated: July 23, 2026.

## Archive Decision

The user retired the goal of an automatic RAW editor on July 23, 2026.
Phase 07 was never activated. The frozen Phase 06 implementation and all
supporting research remain preserved as historical engineering work, but this
packet no longer owns current product direction or implementation work.

The successor workstream is:

```text
docs/stack-documentation/Current/raw-tab-ui-overhaul/manual-first-raw-workflow/
```

## Purpose

This is the final frozen handoff for the retired RAW Starting Point work. Do
not reactivate a phase from this file. Keep it as evidence of the completed
automatic-editor boundary.

Treat this file as historical context only for RAW Starting Point, RAW tab
automatic processing, Build Starting Point, RAW Exposure automation, Local
Range automation, Finish Tone automation, or View Transform / Display Fit
automation.

The archived Pass 0-8 ledger is preserved at:

```text
docs/stack-documentation/Archived/raw-tab-ui-overhaul/auto-starting-point/implementation-progress-pass8-ledger-2026-07-02.md
```

## Read Order Before Code Changes

```text
implementation-progress.md
README.md
agent-reread-guide.md
implementation-contract.md
implementation-pass-readiness.md
```

If the change touches readbacks, stats domains, DNG metadata, or validation
records, also read `code-web-research-readbacks-and-dng.md`.

Do not skip the active pass recorded in this file, and do not add hidden
automatic RAW processing that bypasses visible editable recipe controls.

## Current State

```text
Phase: Archived iterative precise solver program
Current pass: none
Status: retired after Phase 06 at `phase-06-v1`; Phase 07 was never activated
Last completed: native Precise-default/Fast-retained Build Starting Point mode,
  isolated cancelable search, full-resolution handoff gates, exact 13-field
  visible projection, one prechecked atomic apply, one Undo, persistence and
  stale-result safety, live graph/readout updates, and Display Fit ownership.
Entry evidence: `phase-06/README.md` (`phase-06-v1`, PASS), frozen four-source
  `raw-precise-integration-report-v1`, and native cancel/apply/graph/Undo checks.
Current slice: none; checkpoint frozen and work stopped.
Next allowed work: none in this packet without a new explicit user decision to
  restore the automatic-editor goal.
Do not do next: reactivate Phase 07, expose automatic edit actions in current
  UI, change solver science, tune against locked images, add continuous
  rewriting, or claim final product completion.
```

## Capability Matrix

| Capability | Current code state | Next target |
| --- | --- | --- |
| Analyze | Explicit secondary action; refreshes current-frame evidence without changing recipe values. | Keep explicit and non-mutating. |
| Fit / Refit Display | Explicit secondary View Transform / Display Fit action; Starting Point can queue post-upstream refit. | Keep as inspectable/manual display action, but not a required separate step for the base solve. |
| Build Starting Point | Primary full-width action; conservative planner writes safe visible controls; staged Raw Technical + Neutral Scene evidence can cap large safe RAW Exposure lifts; lower-confidence RAW Exposure proposals can apply small visible nudges (-0.25 EV lowering, +0.15 EV lift), including after rendered post-WB Raw Placement evidence; Suggested WB can apply only without camera WB and strong neutral evidence; exact rendered Raw Placement / Local Range / Finish Tone / Display Fit evidence can apply only when it matches the selected visible recipe; dark-reveal cases can use stronger editable shadow Local Range and Finish Tone values; one click can continue through up to four freshly rendered safe upstream passes before final Display Fit; current-plan and result readouts report control status, changed values, evidence, warnings, matching manual-control status, and full View Transform value blocks. | Review dark RAWs for too-weak/too-flat/too-noisy starts. |
| Local Range | Strict one-click planner gate plus optional advanced action hidden behind disclosure; Build Starting Point can author up to two safe visible graph points, cap contextual proposals to 0.45 EV, use orientation-agnostic backlight evidence, and add a highlight-protected lower-quartile fallback when staged or current-frame evidence shows wide range. | Keep folded into the primary safe subset; review real images for strength and halo-free transitions. |
| Finish Tone | Low-risk one-click planner gate plus optional advanced action hidden behind disclosure; exact or projected evidence can author mild visible points, dark reveal can author a stronger shadow-opening curve, and wide-range scenes can author a five-point shadow/highlight balancing curve. | Keep folded into the primary safe subset; review real images for natural contrast and highlight rolloff. |
| Undo | One snapshot covers the Starting Point visible-control action; failed post-fit, active-source render failure, selected-source switch, and same-key source identity mismatch paths preserve undo/pending state and report why continuation stopped. | Add broader native source-switch/cancel coverage as needed. |
| Stage readbacks | Raw Technical, Neutral Scene, Raw Placement, Finish Tone Candidate, and Display Candidate are captured; Local Candidate is fallback pre-local or complete post-local for the current render; successful Base candidate renders merge only when recipe, visible-control scope, and replaced-stage status match; rejected rendered evidence stays visible in Diagnostics. | Keep evidence accounting explicit before broader tuning. |
| Precise integration | `raw-precise-integration-v1` runs the frozen solver in an isolated native worker, full-verifies, prechecks and atomically applies one complete visible recipe, updates editable controls, and preserves one Undo; cancellation keeps the live preview/evidence intact. | Freeze for Phase 07 evaluation. |
| Real RAW validation | The Phase 06 engineering subset contains three DNG cases plus one validation ARW; all four pass render, identity, atomic apply, persistence, cancellation, and Undo gates. | Phase 07 owns the broader locked corpus and structured human review. |

## Open Gaps

- Broader native review is still needed, but real DNG review now confirms that
  per-control readouts, Local Range points, Finish Tone points, and Display Fit
  sliders can all expose automatic writes from the same one-click action.
- The frozen Pass 94 Raw Technical payload remains partial and unchanged. The
  separate Phase 01 `raw-technical-evidence-v1` record now parses and reports
  ActiveArea/MaskedAreas when present, true LinearResponseLimit, NoiseProfile,
  per-plane headroom/clipping, and opcode/profile coverage without feeding the
  planner. The current local DNGs contain no explicit active/masked-area tags.
- Neutral Scene is measured from a separate analysis render, and successful
  queued Base Display Candidate renders can now resolve Display Fit apply.
  Suggested WB has a conservative visible-apply policy, Local Range can use
  rendered evidence after RAW Exposure or Suggested WB changes, exact rendered
  post-WB Raw Placement can unlock RAW Exposure, exact post-WB-plus-exposure
  Display Candidate evidence can finish Display Fit, exact Local Candidate
  evidence can author up to two Local Range points, and exact Finish Tone
  Candidate evidence can author mild visible tone points. Exact Display
  Candidate evidence after Local/Tone graph writes can finish View Transform
  when the full visible upstream recipe matches, and Pass 59 now queues those
  follow-up full-recipe Display Candidate renders during bounded iterations.
- Candidate scoring still uses projected/fallback/pending evidence where true
  per-candidate renders fail, are unavailable, or do not exist; application now
  withholds downstream graph writes when upstream edits would make
  current-render evidence stale, and durable readouts report the trusted or
  pending staged evidence for the visible plan. Mis-scoped rendered evidence is
  reported as rejected in candidate diagnostics. The render loop is bounded,
  not open-ended. Validation summaries now gate tuning readiness on complete
  candidate-stage evidence accounting.
- Helper, editor-state, Phase 06 integration, real renderer, and native UI checks
  cover cancellation, source/recipe/same-key identity rejection, failure
  atomicity, exact apply/persistence, Fast retention, live graph projection,
  Display Fit ownership, and one Undo. Phase 07 still owns broad product review.
- The dirty worktree includes unrelated app/theme/node-graph changes; do not
  revert or mix them into RAW recovery decisions.
- The four-source engineering subset is deliberately insufficient for product
  readiness or tuning. It cannot replace the locked Phase 07 corpus or human
  judgments of strength, naturalness, noise, halos, and preferred next edit.

## Documentation Rules

- Active progress entries must stay short: pass, behavior changed, verification,
  next allowed work, and stop rules.
- Research stays in the long research files. Validation/reporting work counts as
  feature progress only when it unblocks a named user-facing capability.
- Do not write "latest slice" paragraphs here. Archive long history instead.

## Latest Verification

At the Phase 06 checkpoint on July 10, 2026, these checks passed:

```text
.\build.cmd
.\build\StackPreciseIntegrationTests.exe
.\build\StackPreciseDryRunTests.exe
.\build\StackOptimizerSelectionTests.exe
.\build\StackPreciseCandidateTests.exe
.\build\StackRenderedFeatureTests.exe
.\build\StackRawEvidenceTests.exe
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-raw-starting-point-editor-state
```

The Phase 06 renderer run passed the same four real sources, 180 unique proxy
renders, 16 exact cache hits, four true-resolution finalists, and every apply,
Undo, persistence, cancellation, identity, failure, ownership, and Fast-mode
gate. Native `DSC00631.ARW` testing passed cancel, visible five-point Finish
Tone projection, `Display Fit: Auto-current`, and exact one-Undo restoration.
Frozen report SHA-256: `FED2E76C26C178904DAAABB5A42A8D6768EFA6D8C806309F7B746ED6490B4D19`.
Checkpoint: `iterative-raw-solver-phases/phase-06/README.md`.
