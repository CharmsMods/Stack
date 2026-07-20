# Phase 05 Checkpoint

- Checkpoint: `phase-05-v1`
- Date: 2026-07-10
- Decision: PASS
- Dry-run solver: `raw-precise-dry-run-v1`
- Report schema: `raw-precise-dry-run-report-v1`
- Objective policy: `raw-range-goal-policy-v1`
- Candidate engine: `raw-candidate-engine-v1`
- Optimizer: `stage-ordered-pattern-search-v1`
- Convergence/budget: `raw-convergence-contract-v1` / `raw-precise-budget-v1`
- Recipe/slider/graph/apply mutation: prohibited and not performed
- Next phase activated: no

## Outcome

Phase 05 integrates the frozen Phase 01-04 evidence, candidate, constraint,
optimizer, convergence, and budget contracts into one complete non-applying
solve. It decodes a RAW, builds exact raw evidence, reproduces Pass 94 in an
isolated recipe, searches all 13 visible fields in stage order, renders every
candidate through Stack's real OpenGL RAW graph, independently promotes one
finalist to true resolution, and returns a complete canonical visible recipe
plus explanation.

The current project recipe, Undo history, dirty state, visible controls, and
preview remain unchanged. No editor module or apply function is instantiated.

## Accepted Artifacts

| Version | Artifact |
| --- | --- |
| `raw-precise-dry-run-v1` | `src/Raw/RawPreciseDryRun.h/.cpp` |
| `raw-precise-dry-run-report-v1` | [Report schema](dry-run-report-schema-v1.md) |
| `phase-05-fixtures-v1` | `tools/precise_dry_run_tests.cpp` and `StackPreciseDryRunTests.exe` |
| `phase-05-validation-command-v1` | `--validate-raw-precise-dry-run` |
| shared validation runtime | `src/App/Validation/PreciseCandidateValidationRuntime.h/.cpp` |
| `phase-05-engineering-subset-v1` | [Renderer-backed validation summary](validation-summary-v1.md) |
| `raw-precise-dry-run-report-v1` evidence | [Frozen 4-source report](precise-dry-run-v1.json) |

The report JSON SHA-256 is:

```text
BFEC482F0B24683B1B2E2A9E95490A218BA2DC4C328DF91F18583B430D5B42CB
```

The report is about 69.3 MB because it retains every five-stage feature,
constraint, candidate, rejection, optimizer trace, and full-resolution record.

## Complete Dry-Run Path

```text
validate source / solver base / ownership
-> decode and build raw-technical-evidence-v1
-> reproduce isolated Pass 94 warm start
-> build raw-parameter-space-v1
-> evaluate warm candidate through all five Stack stages
-> RAW Exposure pattern search (8)
-> Local Range pattern search (16)
-> Finish Tone pattern search (12)
-> Display Fit pattern search (12)
-> independently render/recheck one true-resolution finalist
-> return complete visible recipe and reasons
-> apply nothing
```

The selected 48-evaluation, 16-accepted-iteration, 60-second proxy budget is
enforced across the stage searches. Exact candidate identity is reused across
stage handoffs. Budget exhaustion remains distinct from convergence.

## Phase 05 Safety Addition

Full and proxy acceptance use the Phase 01 WB-scaled headroom minus its declared
uncertainty. This prevents a boundary value from passing merely because it is a
few thousandths of an EV inside the central estimate.

The frozen Phase 03 candidate engine still records its original measured
constraint. Phase 05 adds the conservative margin in its versioned
lexicographic acceptance adapter; it does not rewrite evidence or weaken a
constraint.

## Objective Policy

V1 admits no universal photographic target because the accepted feature phases
did not establish a human brightness, intent, halo-visibility, or naturalness
range. Therefore:

- Tier 0/1 feasibility can force a safer visible recipe;
- declared future Tier 2/3 range goals are supported and fixture-tested;
- without such a goal, the solver prefers the smallest visible edit; and
- `combinedTotalScore` remains null everywhere.

The focused fixtures prove declared range goals can author Local Range points,
Finish Tone graph values, and Display Fit fields with exact round-trip. The real
V1 run does not move those controls simply to make activity visible.

## Real Renderer Results

The frozen engineering subset contains one unclipped DNG, one partial-channel
clipped DNG, one all-channel clipped DNG, and one validation ARW.

| Source class | Result |
| --- | --- |
| Safe unclipped DNG | Pass 94 `+0.50 EV` retained and full verified. |
| Partial-channel-clipped DNG | Rejected warm restored to safe `-0.125 EV`; existing clipping retained as evidence. |
| All-channel-clipped DNG | Rejected warm restored to safe `-0.625 EV`; no lost-detail reconstruction claimed. |
| Validation ARW | Rejected `0 EV` warm restored to safe `-0.50 EV`. |

All four sources passed every gate. Each used 45 actual unique proxy renders,
four exact cache hits, all five stage records, deterministic replay, and one
true-resolution finalist. Full outputs were `4080x3060`, `4080x3060`,
orientation-normalized `2736x3648`, and orientation-normalized `4024x6024`.

## Required Report Coverage

Each solve record includes:

- all source/decode/base/solver/feature/objective/corpus versions;
- active/withheld dimensions and visible owners;
- Pass 94 values, full constraints, and individual terms;
- candidate/render/cache/accepted/rejected counts;
- exact stage termination status and trace;
- every rejected candidate and blocking constraint;
- proxy and true-resolution measurements;
- winning sliders and graph points in a complete canonical recipe;
- improvement deltas by lexicographic tier;
- uncertainty, warnings, and the selection/retention reason; and
- explicit no-apply/no-texture/no-editor assertions.

## Automated Verification

The Phase 05 focused commands are:

```text
.\build\StackPreciseDryRunTests.exe
.\build\Stack.exe --validate-raw-precise-dry-run \
  --development-file <unclipped.dng> \
  --development-file <partial-clip.dng> \
  --development-file <all-channel-clip.dng> \
  --validation-file <validation.arw> \
  --output <phase-05/precise-dry-run-v1.json> \
  --proxy-max-dimension 256 \
  --feature-max-dimension 256 \
  --warm-max-dimension 2048
```

The standard Stack/RAW regression set is recorded in the parent progress file.

## Gate Audit

| Phase 05 gate | Evidence | Result |
| --- | --- | --- |
| Complete non-applying solver | Real decode/evidence/warm/search/full/report path on four sources. | Pass |
| Valid visible-control candidate | All 13 mapped; complete canonical recipe and parameters round-trip exactly. | Pass |
| Full-resolution rejection | True full finalists plus injected rejection and render-failure fallback fixtures. | Pass |
| Validation improvement | Validation ARW moves from Tier 1 rejection to proxy/full complete with uncertainty margin. | Pass |
| No new critical failures | All four real sources pass higher-tier constraints; clipping remains explicit. | Pass |
| Failure/cancel/state safety | Missing/stale/cancel/render/full/base-edit/ownership fixtures and no-mutation assertions. | Pass |
| Determinism | Exact primary/replay candidate order, stage results, counts, and selected recipe per source. | Pass |
| Runtime/budget | 45 actual proxy renders/source; about 25.7-35.8 s proxy search/source. | Pass |
| Explainability | Individual merits, constraints, traces, selection reason, warnings, complete recipe. | Pass |
| Science frozen before apply | Versioned feasibility/minimality policy; no UI/apply consumer. | Pass |

## Known Limits

- This engineering subset has four sources and two camera families. It is not
  the locked Phase 07 product corpus.
- Human technical acceptability, pairwise preference, strength, naturalness,
  and next-manual-control fields are deliberately not rated.
- Dark/HDR/backlit/noisy/mixed-light/halo-prone feature mechanics remain covered
  by accepted controlled and real-content perturbation fixtures, not by new
  human-labeled Phase 05 goal ranges.
- Without an admitted Tier 2/3 range goal, real V1 normally keeps the warm
  Local Range, Finish Tone, and Display Fit values after testing neighbors.
- The dry-run command is diagnostic and synchronous. Phase 06 owns native job
  lifecycle, visible status, atomic apply, and one Undo.

## Handoff

Decision: PASS.

Freeze `raw-precise-dry-run-v1`, the report schema, uncertainty-adjusted
headroom acceptance, complete visible-recipe projection, four stage quotas,
runtime states, exact caching, deterministic replay, true-resolution policy,
and no-apply contract above.

Next allowed slice: stop. Phase 06 is not active.

Do not do next: connect the dry run to Build Starting Point, display temporary
candidate output, write sliders/graphs, mutate the recipe, create an Undo
snapshot, change solver science while adding UI, or consume the locked set.
