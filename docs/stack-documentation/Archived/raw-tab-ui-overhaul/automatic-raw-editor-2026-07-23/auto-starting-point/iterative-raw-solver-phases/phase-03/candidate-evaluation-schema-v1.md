# Candidate Evaluation Schema V1

- Schema version: `1`
- Candidate engine: `raw-candidate-engine-v1`
- Parameter space: `raw-parameter-space-v1`
- Objective/constraint contract: `raw-objective-constraints-v1`
- Surface plan: `raw-objective-surfaces-v1`
- Rendered features: `rendered-features-v1`
- Raw evidence: `raw-technical-evidence-v1`

## Purpose

This contract makes an alternative RAW recipe fully reproducible and
inspectable without applying it to the editor. It is the frozen problem that
Phase 04 may benchmark. It is not an optimizer, winner policy, or production
apply contract.

The implementation authority is `src/Raw/RawPreciseCandidateEngine.h/.cpp`.
The diagnostic authority is `--validate-raw-candidate-surfaces` and the frozen
surface report linked from the Phase 03 checkpoint.

## Candidate Identity

Every candidate identity includes:

```text
candidate-engine version
parameter-space version
source content SHA-256
decode identity
Phase 01 evidence identity
base visible-recipe identity
candidate visible-recipe identity
renderer identity
proxy/full-resolution identity
rendered-feature version
evaluation-budget identity
control-ownership identity
generation
all 13 requested parameters
```

The visible-recipe identity is SHA-256 over stable JSON with `sourceRef`
removed. The source path, display name, modification time, and other UI/source
locator fields therefore cannot change the visible recipe identity or leak
into a durable record. Source and decode identities are carried separately.

Production float controls are written as floats before canonical identity is
computed. This prevents harmless JSON double-to-float normalization from being
misclassified as recipe mutation while retaining an exact serialize/deserialize
round-trip gate.

## Frozen First Parameter Space

| Parameter | Visible owner | Experimental bound |
| --- | --- | --- |
| `raw_exposure_ev` | RAW Exposure | UI/warm neighborhood intersected with WB-scaled Phase 01 headroom |
| `local_target_ev` | Local Range graph/mask | visible Local Range EV domain |
| `local_delta_ev` | Local Range graph | `[-1, +1] EV` |
| `local_width_ev` | Local Range luminance mask | `[0.02, 1.5] EV` half-width |
| `local_feather` | Local Range luminance mask | `[0, 1]` |
| `finish_y_1` | Finish Tone graph at x=0.25 | `[0, 1]` |
| `finish_y_2` | Finish Tone graph at x=0.50 | `[0, 1]` |
| `finish_y_3` | Finish Tone graph at x=0.75 | `[0, 1]` |
| `display_black_ev` | View Transform | `[-16, 0)` EV |
| `display_white_ev` | View Transform | `[0, 16]` EV |
| `display_middle_grey` | View Transform | `[0.01, 1]` linear |
| `display_shoulder` | View Transform | `[0.05, 4]` |
| `display_toe` | View Transform | `[0, 1]` |

Local and Finish Tone parameters are materialized as visible editable graph
points. Display parameters are materialized as visible View Transform fields.
No hidden render pass is permitted.

## Isolated Render Record

`CandidateRenderEvidence` records:

- attempted/success/canceled/stale/full-resolution disposition;
- exact render and proxy identities;
- render and feature dimensions;
- render and feature-extraction cost;
- graph-image and RAW-stage cache hit/miss counts;
- one exact feature record per required stage; and
- a reason on failure.

The required stage images are:

```text
Neutral Scene
Raw Placement
Local Candidate (complete or declared fallback image)
Finish Tone Candidate
Display Candidate
```

The readback is opt-in. Normal rendering uses a maximum readback dimension of
zero. Candidate capture adds that dimension to the RAW Development render
fingerprint so a final-image cache entry produced without stage capture cannot
masquerade as complete stage evidence.

Candidate rendering clones `RenderGraphSnapshot`, substitutes an immutable
recipe copy, and never instantiates or calls editor apply state. Evaluation
records independently assert current-recipe, undo-depth, and dirty-state
preservation.

## Constraint Order

Constraints are lexicographic. A lower-tier objective observation cannot
compensate for a failed constraint.

Tier 0 covers:

- proposal, base-recipe, candidate-recipe, and render identity;
- finite canonical round trip;
- parameter bounds;
- visible graph endpoints, ordering, monotonic Finish Tone output, and graph
  capacity;
- control ownership;
- unchanged current recipe, undo history, and dirty state; and
- required, current source/recipe/raw-evidence stage feature identities.

Tier 1 covers:

- exact Phase 01 evidence identity;
- limiting-plane WB-scaled RAW headroom; and
- capture-domain all-channel clipping invariance.

Missing RAW headroom prohibits new positive exposure expansion. No constraint
is weakened to make a surface smoother.

## Objective Terms

Each term remains a separate record with ID, tier, validity, value, units,
uncertainty, stage, role, and reason.

Tier 2 technical observations include scene placement/range, rendered and raw
noise evidence, gamut pressure, regional conflict, structure ratios, halo band
and reversal evidence, and Finish Tone curvature.

Tier 3 display observations include linear display high/low clipping, relative
middle placement, and before/after hue shift.

Tier 5 tie-breaker observations include normalized visible edit distance and
changed-parameter count. They cannot override Tier 0/1 and are not yet a winner
policy.

Every serialized evaluation explicitly contains:

```json
"combinedTotalScore": null
```

No final weight, scalar winner score, perceptual threshold, or optimizer is
part of V1.

## Status And Cache Semantics

An evaluation is one of `pending`, `complete`, `rejected`, `failed`,
`canceled`, or `stale`.

- `rejected` means the candidate rendered and was measured but failed an
  owning hard constraint.
- `failed` means rendering or required feature extraction did not complete.
- canceled and stale evaluations are never stored in the exact cache.
- an exact cache hit requires the complete candidate identity, not merely the
  same parameter vector.

The surface harness memoizes exact candidate identities across overlapping
slices. A deliberate repeatability render uses a new pipeline and bypasses
that memoization.

## Surface And Promotion Records

The frozen surface plan contains 13 one-dimensional slices and seven declared
two-dimensional interactions. Every slice contains the exact Pass 94 warm
candidate, including conditional Local Range experiments.

Each sample stores the candidate/evaluation identity, constraint disposition,
individual terms, uncertainty, runtime, and cache disposition. Shape summaries
record observed ranges, maximum adjacent deltas, exact plateaus, and direction
changes. They do not classify a discontinuity or select a winner.

A full-resolution promotion compares same-recipe stage feature records against
the proxy and stores per-stage comparable-feature count, mean relative delta,
maximum relative delta, and missing/mismatched IDs. Phase 03 selects no numeric
acceptance threshold.

## Cancellation And Invalidation

Changing source, decode, raw evidence, base recipe, renderer/proxy policy,
feature version, ownership, budget, or generation creates a different
candidate identity. Stale evidence is rejected rather than merged. The engine
has explicit canceled/stale records and the editor's existing generation/source
handoff remains the production cancellation authority.

