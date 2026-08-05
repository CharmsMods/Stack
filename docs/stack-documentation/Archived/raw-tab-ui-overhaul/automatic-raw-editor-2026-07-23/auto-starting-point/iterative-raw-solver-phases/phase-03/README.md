# Phase 03 Checkpoint

- Checkpoint: `phase-03-v1`
- Date: 2026-07-10
- Decision: PASS
- Candidate engine: `raw-candidate-engine-v1`
- Parameter space: `raw-parameter-space-v1`
- Objective/constraint contract: `raw-objective-constraints-v1`
- Surface plan/report: `raw-objective-surfaces-v1`
- Rendered features: `rendered-features-v1`
- Raw evidence: `raw-technical-evidence-v1`
- Current project recipe mutation: prohibited and not performed
- Optimizer selection/implementation: prohibited and not performed
- Next phase activated: no

## Outcome

Phase 03 now has an isolated, immutable candidate engine over 13 visible RAW
controls; exact candidate/evaluation/cache identities; lexicographic Tier 0/1
constraints; inspectable individual Tier 2/3/5 terms; reproducible 1D/2D
surface enumeration; opt-in actual Stack stage-image readbacks; exact candidate
memoization; and same-recipe proxy/full-resolution comparison.

Candidate rendering clones the render graph and substitutes a recipe copy. It
does not instantiate editor apply state, alter the current recipe, touch undo
history, set project dirty, or write sliders/graphs. Normal rendering still has
stage-image capture disabled. Every serialized evaluation explicitly keeps
`combinedTotalScore` null.

No optimizer, convergence rule, objective weights, numeric perceptual limits,
winner, or production apply was introduced.

## Accepted Artifacts

| Version | Artifact |
| --- | --- |
| `raw-candidate-engine-v1` | `src/Raw/RawPreciseCandidateEngine.h/.cpp` |
| `phase-03-schema-v1` | [Candidate evaluation schema](candidate-evaluation-schema-v1.md) |
| `phase-03-fixtures-v1` | `tools/precise_candidate_tests.cpp` and `StackPreciseCandidateTests.exe` |
| `raw-objective-surfaces-v1` | [Frozen real-render surface dataset](objective-surfaces-v1.json) |
| `phase-04-benchmark-requirements-v1` | [Measured optimizer requirements](optimizer-requirements-v1.md) |
| `phase-03-validation-command-v1` | `--validate-raw-candidate-surfaces` |

The surface JSON SHA-256 is:

```text
ABDC9368B86D10E76B78E912ECE70C3854B97ECD38A7CFE2C5B0FDAAED94AD06
```

## Candidate And Constraint Fixture Gate

`StackPreciseCandidateTests` passes:

- source path, timestamp, and UI label exclusion from visible-recipe identity;
- visible field inclusion and exact generation/renderer/proxy invalidation;
- immutable proposal construction and all 13 visible parameter writes;
- Phase 00 bounds and limiting-plane RAW headroom bounds;
- deterministic 13 one-dimensional plus seven interaction surfaces;
- exact warm candidate inclusion in every surface;
- individual terms with no aggregate score;
- ownership, RAW headroom, graph order/capacity/endpoints, NaN, stale, failed,
  canceled, recipe, undo, and dirty-state rejection paths;
- exact cache hits/misses with canceled/stale non-storage; and
- per-stage proxy/full promotion records.

Production float fields are normalized before canonical JSON identity. The
fixture and real surface run confirm harmless numeric storage conversion no
longer triggers a false mutation rejection.

## Actual Stack Renderer Evidence

The diagnostic command used the real OpenGL RAW Development graph, not the
Phase 02 LibRaw-only validation renderer:

```text
.\build\Stack.exe --validate-raw-candidate-surfaces \
  --development-file <IMG_260608_203425.dng> \
  --validation-file <DSC00650.ARW> \
  --output <phase-03/objective-surfaces-v1.json> \
  --proxy-max-dimension 256 \
  --feature-max-dimension 256 \
  --warm-max-dimension 2048 \
  --full-resolution-promotions 1
```

Only filenames and content identities are durable. Absolute local source paths
are scrubbed and the command rejects its output if a source directory escapes.

| Measure | Result |
| --- | ---: |
| Partitions | development + validation; locked untouched |
| Sources complete | 2 / 2 |
| Required surfaces per source | 13 one-dimensional + 7 interactions |
| Warm candidate inclusion | 20 / 20 surfaces on each source |
| Unique candidates | 52 DNG + 50 ARW |
| Exact memo hits | 51 DNG + 57 ARW |
| Required stage images | 5 / 5 on each source |
| Repeatability | zero feature delta at all 5 stages on both sources |
| True full promotion | 256x192 -> 4080x3060 DNG |
| Command runtime | 74529.4 ms |
| Recipe/undo/dirty mutation | false / false / false |
| Optimizer/aggregate score/threshold | none / null / none |

The DNG produced 52 complete unique evaluations and no hard rejection. The
validation ARW produced five complete and 45 fully rendered/measured hard
rejections. All 45 are owned by `raw.wb_scaled_headroom`: the frozen Pass 94
warm recipe itself has 0EV RAW Exposure while exact Phase 01 evidence says the
limiting WB-scaled plane requires a lower safe point. The warm candidate remains
in every surface and is not hidden or relabeled safe. No downstream display
term compensates for the Tier 1 failure.

## Objective Surface Review

The DNG surfaces move the intended owning terms:

- RAW Exposure moves Raw Placement p50 almost exactly with the sampled EV span;
- Local Range delta moves local structure and halo/band observations;
- Finish Tone ordinates move pre-display placement/structure;
- View Transform controls move display placement and clip observations; and
- the seven interaction grids expose cross-stage direction changes and
  plateaus without an aggregate score.

The accepted Phase 02 controlled fixtures remain the correlation authority for
noise, halo, blur, gamut, WB uncertainty, and display-clip perturbations. Phase
03 adds real Stack stage ownership and surface response. Human acceptability,
aesthetic preference, and numeric perceptual thresholds remain unclaimed.

## Proxy/Full And Cost Review

Full-resolution promotion retained 35 comparable features per scene stage and
10 for Display Candidate. Mean relative proxy/full disagreement was about
0.109-0.119; individual maxima reached 0.660-1.0. These values are recorded as
uncertainty, not accepted limits. A 256px winner cannot be silently treated as
full-resolution truth.

At this proxy, feature extraction was roughly 0.5-0.6 seconds per unique
candidate and dominated the roughly 0.03-0.14 second render cost. Exact cache
reuse is therefore a required Phase 04 benchmark property.

## Automated Verification

These checks pass against the frozen Phase 03 implementation:

```text
.\build.cmd
.\build\StackPreciseCandidateTests.exe
.\build\StackRenderedFeatureTests.exe
.\build\StackRawEvidenceTests.exe
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-raw-starting-point-editor-state
```

The final two-partition real-render command returned success and wrote the
hashed report above.

## Gate Audit

| Phase 03 gate | Evidence | Result |
| --- | --- | --- |
| Candidate isolation | Graph/recipe copies, no editor apply, explicit recipe/undo/dirty assertions and regressions. | Pass |
| Exact identity/cache | Complete identity tuple, path scrubbing, invalidation fixtures, exact memo report. | Pass |
| Hard constraints | RAW, graph, ownership, NaN, state, stale, cancel/fail fixtures; real ARW headroom rejection. | Pass |
| All first-space surfaces | 13 1D + 7 key interactions on development and validation sources. | Pass |
| Warm retained | Exact Pass 94 reproduction in 20/20 surfaces per source, including unsafe ARW disposition. | Pass |
| Objective response | Phase 02 controlled directions plus actual Stack stage-owned term ranges. | Pass for technical observations; no winner policy |
| Surface shape/noise/cost | Individual shape summaries, zero-delta exact repeats, runtime/cache records. | Pass |
| Proxy/full limits | True 4080x3060 promotion and per-stage feature disagreement. | Pass for measurement; no threshold |
| Pass 94 unchanged | No production consumer/apply; graph and editor-state regressions pass. | Pass |

## Known Limitations

- Two real sources are sufficient for engineering surface shape and cost, not
  Phase 07 product-quality generalization or human preference claims.
- The validation ARW starts outside the accepted RAW feasibility region. Phase
  04 must benchmark feasibility restoration or honest failure; it may not
  weaken headroom to make the warm candidate pass.
- Feature extraction is research-grade CPU work and currently dominates cost.
- Proxy/full disagreement is measured on one development DNG only. No numeric
  promotion threshold is accepted.
- No human technical-acceptability labels, semantic intent labels, or locked
  captures were consumed.
- Cancellation/stale paths are fixture-covered; this phase does not add a
  long-running UI solve or production job controller.

## Handoff

Decision: PASS.

Frozen problem: versions, schema, bounds, surface data, proxy policy, costs,
constraint ordering, and benchmark requirements listed above.

Phase 04 may compare optimizer/convergence methods against this exact problem.
It must retain lexicographic feasibility, exact caching, warm retention,
failure/cancellation records, and full-resolution promotion. It must include
the unsafe-warm feasibility case.

Next allowed slice: stop. Phase 04 is not active.

Do not do next: select or implement an optimizer, combine terms into a winner
score, tune weights/thresholds, apply a candidate, write visible controls,
touch the locked partition, or weaken RAW/graph constraints.

