# Phase 05 Precise Dry-Run Report Schema

- Schema version: `1`
- Solver: `raw-precise-dry-run-v1`
- Report: `raw-precise-dry-run-report-v1`
- Objective policy: `raw-range-goal-policy-v1`
- Candidate engine: `raw-candidate-engine-v1`
- Optimizer: `stage-ordered-pattern-search-v1`
- Apply behavior: absent by contract

## Purpose

The report is the complete non-applying handoff between solver science and a
future native apply wrapper. It proves which visible recipe Stack would choose,
why it chose or retained it, whether the same candidate passed full-resolution
checks, and that no current project state changed.

It is diagnostic JSON. It contains no rendered output texture and is explicitly
not loadable as an applied image.

## Identity Envelope

Every source record carries:

```text
source content SHA-256
decode identity SHA-256
raw-evidence identity SHA-256
input project recipe identity
isolated Pass 94 solver-base identity
candidate engine / parameter / feature / objective versions
optimizer / convergence / budget versions
proxy and full-resolution renderer identities
ownership identity and generation
corpus manifest and validation-subset versions
```

Absolute local source paths are rejected before report write. Filenames and
content identities remain for audit. The complete candidate recipe uses the
candidate engine's canonical path-free visible recipe representation.

## Solve Sequence

The report records this actual sequence:

```text
decode + Phase 01 evidence
-> isolated Pass 94 warm recipe
-> 13-field bounded parameter space
-> RAW Exposure search (quota 8)
-> Local Range search (quota 16)
-> Finish Tone search (quota 12)
-> Display Fit search (quota 12)
-> independent true-resolution finalist render
-> candidate report only
```

Each stage owns a separate optimizer record with dimensions, trace, accepted
iterations, cache events, radius/stability state, rejected candidates, and
termination reason. The four stage quotas remain inside 48 unique evaluations,
16 accepted iterations, and 60 seconds of proxy search per source.

## Parameter Decisions

All 13 fields map to existing visible controls:

| Fields | Visible owner |
| --- | --- |
| `raw_exposure_ev` | RAW Exposure slider |
| `local_target_ev`, `local_delta_ev`, `local_width_ev`, `local_feather` | Local Range graph and luminance-range mask controls |
| `finish_y_1`, `finish_y_2`, `finish_y_3` | Finish Tone graph |
| `display_black_ev`, `display_white_ev`, `display_middle_grey`, `display_shoulder`, `display_toe` | View Transform / Display Fit controls |

The report states whether each dimension was active and why. A user-owned group
is withheld before search. The selected proposal embeds the complete canonical
visible recipe, including graph points, even when a group remains unchanged.

## Lexicographic Evaluation

Phase 05 translates every frozen candidate constraint into a nonnegative merit
component. Tier 0 identity/state and Tier 1 RAW constraints precede declared
Tier 2/3 range goals and Tier 5 edit minimality. `combinedTotalScore` remains
null at report, solve, optimizer, and candidate-evaluation levels.

WB-scaled RAW headroom uses the uncertainty-adjusted limit:

```text
safeExposureLimitEv = measuredHeadroomEv - headroomUncertaintyEv
violationEv = max(0, candidateExposureEv - safeExposureLimitEv)
```

This additional Phase 05 acceptance margin does not alter Phase 01 evidence or
the frozen Phase 03 candidate record. It prevents a candidate that is barely
inside the measured limit from being called safely verified.

No numeric brightness, key, naturalness, halo-visibility, or aesthetic goal is
admitted in V1 because the accepted earlier phases did not establish a human
threshold. With no declared goal, search may restore hard feasibility and then
prefers the smallest visible edit. Synthetic fixtures prove the same engine can
move Local Range, Finish Tone, and Display Fit when a versioned range goal is
supplied; real V1 does not invent one to force slider movement.

## Candidate And Evaluation Records

Every unique proxy candidate retains:

- complete proposal and canonical visible recipe;
- all constraints and their status/value/limit/uncertainty;
- all individual objective observations;
- five Stack stage feature records and warm comparisons;
- render, feature, cache, and stage-cache costs;
- complete/rejected/failed/canceled/stale disposition;
- current-recipe, Undo, and dirty-state assertions; and
- explicit `combinedTotalScore: null`.

Rejected candidates therefore keep the exact blocking constraint instead of
being summarized as a bad score.

## Full-Resolution Verification

One normalized visible finalist per source is rendered with preview scaling
disabled. Orientation may swap dimensions or remove a small inactive edge, so
true resolution is checked by:

```text
both rendered dimensions > proxy maximum
and full rendered pixels / metadata-visible pixels >= 0.95
```

The same recipe independently re-runs Tier 0/1 constraints and every declared
range goal. Proxy/full feature disagreements remain individual evidence; V1
does not introduce one arbitrary maximum-delta threshold. Missing, failed, or
rejected full evidence makes the candidate ineligible and retains the warm
record without apply.

## Runtime States

The report preserves the Phase 04 states:

```text
converged
safe-improvement-budget-exhausted
warm-start-retained
blocked-by-missing-evidence
canceled
candidate-render-failed
full-resolution-verification-rejected
```

An accepted candidate found before a quota ends is not relabeled converged.

## Determinism

Each real source is solved once from actual rendered evidence, then replayed
from the exact candidate records. The replay must reproduce candidate request
order, stage results, selected points, accepted counts, final visible recipe,
fallback state, and full-verification disposition. Phase 03 remains the
renderer repeatability authority; Phase 05 adds deterministic orchestration.

## No-Mutation Envelope

Every successful report states:

```text
input project recipe unchanged = true
isolated solver base unchanged = true
Undo history unchanged = true
project dirty state unchanged = true
editor module instantiated = false
production apply called = false
applied preview texture created = false
recipeApplied = false
loadableAsAppliedTexture = false
```

`candidateEligibleForFutureApply` means only that Phase 06 may consume the
verified visible recipe. It is not evidence that Phase 05 applied anything.
