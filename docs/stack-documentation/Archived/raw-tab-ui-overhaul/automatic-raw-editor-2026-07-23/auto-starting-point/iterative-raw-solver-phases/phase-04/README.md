# Phase 04 Checkpoint

- Checkpoint: `phase-04-v1`
- Date: 2026-07-10
- Decision: PASS
- Selected optimizer: `stage-ordered-pattern-search-v1`
- Benchmark: `raw-optimizer-benchmark-v1`
- Convergence: `raw-convergence-contract-v1`
- Budget: `raw-precise-budget-v1`
- Comparator: `diagonal-quadratic-trust-region-v1`
- Current project recipe mutation: prohibited and not performed
- Production solver integration/apply: prohibited and not performed
- Next phase activated: no

## Outcome

Phase 04 selected a deterministic stage-ordered pattern search over the frozen
Phase 03 candidate/objective problem. The implementation provides pure
non-mutating optimizer mechanics, lexicographic component comparison, exact
point caching, continuous and adjacent-discrete polls, declared pair
interactions, pattern extension, radius control, constraint-boundary
oscillation handling, explicit runtime states, and full-resolution rejection
fallback.

The benchmark compares Pass 94 warm-only, the selected pattern search, and a
bounded diagonal-quadratic derivative-free trust-region comparator. It does
not use a combined score or one average winner.

No optimizer is connected to Build Starting Point, the editor, a production
render job, graph/sliders, or recipe apply. That end-to-end non-applying wiring
belongs to Phase 05.

## Accepted Artifacts

| Version | Artifact |
| --- | --- |
| `stage-ordered-pattern-search-v1` | `src/Raw/RawOptimizerSelection.h/.cpp` |
| `diagonal-quadratic-trust-region-v1` | Same module, benchmark comparator only |
| `raw-convergence-contract-v1` | [Convergence and budget contract](convergence-and-budget-v1.md) |
| `optimizer-source-extraction-v1` | [Primary-source extraction](optimizer-source-extraction-v1.md) |
| `raw-optimizer-benchmark-v1` | [Frozen benchmark report](optimizer-benchmark-v1.json) |
| `phase-04-fixtures-v1` | `tools/optimizer_selection_tests.cpp` and `StackOptimizerSelectionTests.exe` |
| `phase-04-validation-command-v1` | `--validate-raw-optimizer-benchmarks` |

The benchmark JSON SHA-256 is:

```text
D94A67081418E3F861B9CDE93DEB6CD358557C941817364FF5E151D96D8F657B
```

The command rewrote the report twice with the same hash.

## Selected Architecture

```text
Pass 94 warm candidate
-> RAW Exposure pattern search
-> Local Range pattern search
-> Finish Tone pattern search
-> Display Fit pattern search
-> declared pair interactions where required
-> one independent full-resolution verification
-> return candidate/report without applying it
```

Acceptance remains lexicographic:

```text
Tier 0 identity/state -> Tier 1 RAW -> Tier 2 technical
-> Tier 3 display -> Tier 5 edit tie-breakers
```

Each component carries its own uncertainty/meaningful delta. Serialized
evaluations and results retain `combinedTotalScore: null`.

## Benchmark Results

Controlled cases covered bounded smooth improvement, a coupled valley,
plateau/discontinuity, sub-tolerance ripple, unsafe-warm feasibility,
candidate-render failure, missing evidence, cancellation, and full-resolution
rejection fallback. The selected method passed every named gate and reproduced
the same status, selected point, counts, and trace structure on repeat.

Frozen Phase 03 real-surface replay:

| Source | Warm state | Pattern result | Unique evaluations | Trust comparator |
| --- | --- | --- | ---: | --- |
| `IMG_260608_203425.dng` | Complete at `+0.50 EV` | Warm retained at `+0.50 EV`; no lower-tier target invented | 3 | Same result, 3 |
| `DSC00650.ARW` | Rejected at `0 EV` by WB-scaled headroom | Converged to sampled safe `-0.75 EV` | 2 | Same result, 2 |

The ARW result is a meaningful technical improvement because it restores a
hard Tier 1 feasibility failure without weakening that constraint or adding a
new failure. The DNG result is intentionally not forced away from a safe warm
candidate when the frozen surface has no accepted photographic target.

The trust-region comparator showed no advantage on the two frozen real cases.
Its interpolation/model-geometry complexity is therefore deferred rather than
half-integrated. Bayesian optimization and CMA-ES were not justified by the
measured surface/cost evidence.

## Frozen Budget

- 48 unique proxy evaluations;
- 16 accepted iterations;
- 60 seconds;
- normalized radius `0.25` initial, `0.03125` minimum, `0.50` maximum;
- shrink `0.50`, expand `1.50`;
- two stable sweeps;
- four safe/rejected boundary oscillations per radius; and
- one independent full-resolution finalist verification.

Stage planning quotas are RAW Exposure 8, Local Range 16, Finish Tone 12, and
Display Fit 12. Phase 05 owns scheduling inside those frozen maxima.

## Automated Verification

The Phase 04 focused gate is:

```text
.\build\StackOptimizerSelectionTests.exe
.\build\Stack.exe --validate-raw-optimizer-benchmarks \
  --surface-report <phase-03/objective-surfaces-v1.json> \
  --output <phase-04/optimizer-benchmark-v1.json>
```

The command verifies the frozen Phase 03 SHA-256 and version/non-mutation
envelope before benchmarking. It writes no source paths and refuses a changed
surface report.

The standard Stack/RAW regression set is recorded in the parent progress file.

## Gate Audit

| Phase 04 gate | Evidence | Result |
| --- | --- | --- |
| Named optimizer justified | Per-case analytic and frozen-surface report; no total average. | Pass |
| Meaningful validation improvement | Unsafe ARW warm restored to the sampled safe region. | Pass |
| Warm retention | Safe DNG warm retained without inventing a target. | Pass |
| Determinism/cache | Exact repeat signature and byte-identical report; cache fixtures. | Pass |
| Convergence/budget | Versioned states, thresholds, quotas, boundary and full-resolution rules. | Pass |
| Failure/cancel/fallback | Distinct injected states and full-resolution warm fallback. | Pass |
| Complete candidate capability | Generic bounded mixed-vector result carries the complete selected point/record. | Pass for mechanics; Phase 05 supplies the full renderer-backed vector |
| No mutation/integration | Pure callbacks plus explicit recipe/Undo/dirty assertions; no editor consumer. | Pass |
| Deferred alternatives | Trust region retained only as comparator; stochastic methods documented as unjustified. | Pass |

## Known Limits

- Two real sources establish engineering behavior, not broad image-quality
  superiority.
- Frozen real replay exercises the RAW Exposure feasibility dimension. The
  controlled suite exercises interactions and failure states; Phase 05 must
  prove end-to-end all-stage candidate rendering.
- No universal brightness, contrast, aesthetic, or human-preference target was
  introduced.
- Full-resolution acceptance mechanics are fixture-tested, but Phase 05 must
  run them on a complete renderer-backed finalist.
- The comparator is a small diagonal quadratic trust-region benchmark, not an
  implementation of BOBYQA/PDFO and not entitled to those methods' guarantees.

## Handoff

Decision: PASS.

Freeze the versions, selected stage order, adjacent-discrete behavior,
lexicographic comparator, exact caching, budget, states, boundary rule,
full-resolution independent recheck, and Pass 94 fallback above.

Next allowed slice: stop. Phase 05 is not active.

Do not do next: connect this optimizer to Build Starting Point, create a solver
job/controller, evaluate a live editor recipe, apply a candidate, write
sliders/graphs, change Undo/dirty state, tune objective targets, or consume the
locked corpus.
