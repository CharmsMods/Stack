# Phase 05 Renderer-Backed Validation Summary

- Validation subset: `phase-05-engineering-subset-v1`
- Source manifest: `corpus-manifest-v1`
- Report: `precise-dry-run-v1.json`
- Date: 2026-07-10
- Locked partition touched: no
- Human review claimed: no

## Scope

The frozen run uses three development DNGs and one validation ARW. The DNGs
deliberately cover no sampled co-sited clipping, partial-channel clipping, and
all-channel clipping. The ARW retains the Phase 03 unsafe-warm feasibility case
and adds orientation-normalized full-resolution verification.

This is an engineering integration subset, not Phase 07 product acceptance.

## Results

| Source | Partition / RAW state | Pass 94 proxy | Selected RAW Exposure | Uncertainty-adjusted maximum | Final status | True full render |
| --- | --- | --- | ---: | ---: | --- | --- |
| `IMG_260608_203425.dng` | Development; no sampled co-sited clipping | Complete | `+0.50 EV` | `+2.1494 EV` | Warm retained | `4080x3060` |
| `IMG_260608_203701.dng` | Development; partial-channel clipping | Rejected | `-0.125 EV` | `-0.1003 EV` | Safe improvement; budget exhausted | `4080x3060` |
| `IMG_260608_204713.dng` | Development; all-channel clipping present | Rejected | `-0.625 EV` | `-0.5618 EV` | Safe improvement; budget exhausted | `2736x3648`, orientation normalized |
| `DSC00650.ARW` | Validation; no sampled co-sited clipping | Rejected | `-0.50 EV` | `-0.4254 EV` | Safe improvement; budget exhausted | `4024x6024`, orientation normalized |

Existing all-channel capture clipping remains recorded and unrecoverable. The
solver does not claim to recreate detail; it only avoids worsening the frozen
RAW constraint and moves the visible exposure into the uncertainty-adjusted
safe region.

## Counts And Runtime

Each source produced:

- 45 unique actual proxy candidate renders;
- four exact cross-stage proxy cache hits;
- 48 optimizer-accounted stage evaluations;
- all five Stack stage feature records for every proxy and full finalist;
- one independent true-resolution finalist;
- an exact deterministic orchestration replay; and
- zero recipe, Undo, dirty-state, preview, or apply mutation.

Totals are 180 unique proxy renders, 16 exact cache hits, and four full
finalists. Per-source proxy-search time ranged from about 25.7 to 35.8 seconds,
inside the frozen 60-second envelope. The four-source command took about 151.2
seconds including decode, raw evidence, Pass 94 reproduction, full renders,
serialization, and replay.

## Technical Acceptability Decision

The validation ARW improves from a Pass 94 candidate rejected by WB-scaled
headroom to a complete proxy and complete full candidate. No higher-tier
constraint or critical failure is added. That satisfies the Phase 05 technical
improvement gate for the frozen validation case.

The two clipped DNGs exercise the same behavior without hiding their capture
state. The already-safe DNG correctly remains nearly unchanged.

No human pairwise preference, strength, naturalness, or next-control rating is
claimed. Those fields are explicitly `not-rated` in the report.

## Controlled Failure And State Coverage

`StackPreciseDryRunTests` covers:

- deterministic well-exposed warm retention;
- uncertainty-adjusted unsafe-warm feasibility restoration;
- evaluation-budget exhaustion distinct from convergence;
- declared-goal movement of visible Local Range, Finish Tone, and Display Fit;
- complete graph/slider recipe round-trip;
- required raw evidence missing;
- unsupported/invalid request identity;
- pre-start cancellation;
- same-key/source staleness during search;
- base-recipe identity change;
- warm candidate render failure;
- full-resolution constraint rejection;
- full-resolution render failure;
- user-owned group withholding; and
- no applied texture or recipe mutation.

Earlier accepted fixtures remain the evidence authority for raw normalization,
noise/SNR, clipping classes, scene EV, multiscale response, halo detection,
color/WB uncertainty, display clipping, graph validity, and editor-state
handoff. Phase 05 composes those versions; it does not retune them.

## Limits

- The real subset contains two camera families and four sources, not every
  difficult-image category named for Phase 07.
- Dark/HDR/backlit/mixed-light/halo-prone technical features are renderer-backed
  and fixture-tested, but no human-labeled objective ranges are admitted.
- Consequently V1 can restore hard feasibility and verify a complete recipe,
  but it normally retains Pass 94 Local Range, Finish Tone, and Display Fit
  unless a future version supplies accepted range goals.
- All-channel clipping is reported, not reconstructed.
- Full verification proves the same candidate at actual output resolution;
  it does not establish a universal perceptual proxy/full threshold.
