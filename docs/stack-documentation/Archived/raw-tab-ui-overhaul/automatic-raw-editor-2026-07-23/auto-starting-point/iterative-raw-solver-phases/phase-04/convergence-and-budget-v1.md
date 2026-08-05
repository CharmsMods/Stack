# Phase 04 Convergence And Budget Contract

- Contract: `raw-convergence-contract-v1`
- Budget: `raw-precise-budget-v1`
- Selected search: `stage-ordered-pattern-search-v1`
- Comparator: `diagonal-quadratic-trust-region-v1`
- Candidate/objective input: frozen Phase 03 V1

## Non-Scalar Acceptance Rule

Every evaluation carries ordered nonnegative violation/gap components. There
is no weighted total photo score.

Comparison order is:

```text
Tier 0 identity/state constraints
-> Tier 1 RAW-artifact constraints
-> Tier 2 technical goals
-> Tier 3 display goals
-> Tier 5 edit/complexity tie-breakers
```

Within a tier, the versioned component ID supplies deterministic order. A
candidate can replace the incumbent only when the first materially different
valid component improves. Material difference is the maximum of both records'
declared meaningful deltas and uncertainties. An invalid, failed, canceled, or
missing evaluation cannot purchase a lower-tier improvement.

The model-based comparator may fit only the first currently unmet constraint
or goal component. It may not combine tiers into a surrogate total score.

## Stage-Ordered Search

The selected dry-run architecture searches these ownership groups in order:

```text
RAW Exposure -> Local Range -> Finish Tone -> Display Fit
```

Continuous coordinates poll negative then positive at the normalized radius.
Discrete coordinates poll the adjacent discrete value in that direction.
After an accepted coordinate step, one same-direction extension may be tested.
Declared two-control interaction pairs are polled only after a coordinate sweep
finds no improvement. Bounds, graph invariants, and inactive dimensions remain
owned by the frozen parameter-space contract.

Phase 05 may populate the full candidate vector and accepted individual goal
ranges, but it must not change these comparison mechanics without a new
optimizer/convergence version and benchmark.

## Frozen Budget

| Limit | V1 value | Rationale |
| --- | ---: | --- |
| Unique proxy evaluations | 48 | Phase 03 DNG P95 render + features was about 0.74 s, so 48 leaves orchestration margin inside 60 s. |
| Accepted iterations | 16 | Prevents long chains of small accepted edits. |
| Wall time | 60,000 ms | Explicit upper bound; time exhaustion is not convergence. |
| Initial normalized radius | 0.25 | Reaches a meaningful local neighbor without beginning at the full domain. |
| Minimum normalized radius | 0.03125 | One thirty-second of a continuous span. |
| Maximum normalized radius | 0.50 | Prevents an expansion from spanning more than half a domain. |
| Shrink / expand | 0.50 / 1.50 | Conservative deterministic radius control. |
| Stable sweeps | 2 | Requires repeated absence of meaningful improvement at small radius. |
| Boundary oscillations at one radius | 4 | Forces shrink after repeated safe/rejected crossings. |
| Full-resolution verification | one finalist | Proxy evidence cannot be final acceptance. |

The 48-evaluation envelope is allocated by ownership for Phase 05 planning:

| Group | Maximum evaluations |
| --- | ---: |
| RAW Exposure | 8 |
| Local Range | 16 |
| Finish Tone | 12 |
| Display Fit | 12 |

Unused group quota may not silently enlarge total budget or bypass a later
group's evidence requirement. Phase 05 owns exact quota scheduling.

## Convergence And Stop States

`converged` means all of the following are true:

- the selected record is complete and passes hard constraints;
- any accepted improvement exceeded component uncertainty/meaningful delta;
- the radius fell below the minimum after at least two stable sweeps or the
  equivalent model-based stability condition; and
- a required full-resolution verification accepted the same candidate under
  the same independent hard constraints and declared goal ranges.

The runtime states remain distinct:

| State | Meaning |
| --- | --- |
| `converged` | Stability/radius criteria passed with a safe selected result. |
| `safe-improvement-budget-exhausted` | A safe improvement exists, but an evaluation, accepted-iteration, or time limit ended search first. |
| `warm-start-retained` | No meaningful safe improvement displaced the Pass 94 fallback. |
| `blocked-by-missing-evidence` | A required evaluator, component, or full-resolution verifier is unavailable. |
| `canceled` | Source generation/user cancellation ended work; nothing is applied. |
| `candidate-render-failed` | The required candidate render/evaluation failed. |
| `full-resolution-verification-rejected` | Proxy winner failed independent full-resolution checks; warm fallback is retained. |

Budget exhaustion must never be serialized as convergence.

## Boundary, Cache, And Failure Rules

- Exact normalized candidate identity is cached; a cache hit cannot increment
  the unique-evaluation budget.
- Every complete-safe to rejected-unsafe (or reverse) observation is counted.
  Four transitions at one radius force an explicit radius-shrink trace event.
- Shrink retains the last complete safe accepted state.
- Failed, missing, and canceled records remain diagnostic outputs and are not
  fitted as convenient numeric penalties.
- The current recipe, Undo history, and project dirty state remain unchanged
  throughout search and full-resolution verification.

## Full-Resolution Rule

Phase 03 measured large feature-specific proxy/full disagreement. V1 therefore
does not accept a single arbitrary maximum-delta threshold. The promoted
candidate must independently pass the same Tier 0/1 constraints and every
declared goal range at full resolution. Missing verification blocks; rejection
falls back. A proxy `converged` state is provisional until this recheck passes.

## Evidence Limits

The frozen DNG supplies correct warm retention when no lower-tier technical
target has been declared. The frozen ARW supplies feasibility restoration from
an honestly rejected warm start. These prove optimizer mechanics, not general
photo quality. Phase 05 must use accepted Phase 01-03 evidence contracts, and
Phase 07 remains responsible for corpus-wide quality and human review.
