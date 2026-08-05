# Phase 02 Checkpoint

- Checkpoint: `phase-02-v1`
- Date: 2026-07-10
- Decision: PASS
- Rendered feature schema: `rendered-features-v1`
- Phase 01 input: `raw-technical-evidence-v1`
- Solver behavior changed: no
- Recipe mutation: prohibited and not performed
- Candidate optimization/scoring: prohibited and not performed
- Next phase activated: no

## Outcome

Phase 02 provides a versioned, non-mutating CPU measurement layer for declared
scene-linear, region-mask, linear-display, and before/after rendered pixels.
It measures scene placement, regional conflict, mask uncertainty, multiscale
structure, halo behavior, rendered/raw noise evidence, raw clipping, WB/color
uncertainty, display state, and proxy/reference disagreement.

Every feature retains identity, units, domain, color transform, reference,
resolution, valid fraction, uncertainty, measured cost, minimum resolution,
and disposition. Matching Phase 01 raw priors are carried forward by exact
evidence identity. Stale stages, recipes, spaces, transforms, crops, raw
evidence, and insufficient resolutions are rejected.

No feature is combined into a winner score. No image candidate is searched.
No slider, graph, recipe, project, render output, Pass 94 decision, or UI state
is changed. The only app integration is an explicit command-line validation
runner.

## Accepted Artifacts

| Version | Artifact |
| --- | --- |
| `rendered-features-v1` | `src/Raw/RenderedFeatureEvidence.h/.cpp` |
| `phase-02-schema-contract-v1` | [Feature schema and disposition contract](rendered-feature-schema-v1.md) |
| `phase-02-fixtures-v1` | `tools/rendered_feature_tests.cpp` and `StackRenderedFeatureTests.exe` |
| `phase-02-real-corpus-correlation-v1` | [16-source JSON correlation record](real-corpus-correlation-v1.json) |
| `phase-02-validation-command-v1` | `--validate-rendered-feature-foundation` |

The corpus JSON SHA-256 is:

```text
74D8EB5A6235ABABD2D7E0087CA24987CB9F9C48224864591D3AF54F4AC330B3
```

## Research And Decisions Consumed

The implementation follows the accepted Phase 00 math/feature ledger and the
research contracts for:

- declared working-linear luminance and EV rather than display statistics;
- log-luminance base/detail measurements at named scales;
- guided, bilateral, and local-Laplacian methods as prototypes, not automatic
  production adoption;
- signed edge gradients, overshoot/undershoot, extrema, and adjacent bands;
- Gray World, Shades-of-Gray, and Gray Edge disagreement as uncertainty, not
  white-balance truth;
- separate raw clipping, scene gamut pressure, and display clipping;
- separate linear-display and encoded-display measurements;
- calibrated absolute-display claims only when a display model exists; and
- proxy/full disagreement as first-class uncertainty.

Histogram targets, semantic intent, learned aesthetics, and numeric perceptual
thresholds remain outside the accepted set.

## Fixture Gate

`StackRenderedFeatureTests` passes the Phase 00 Phase 02 fixture plan:

| Fixture family | Result |
| --- | --- |
| `P02-LUMA-01/02` | Declared non-Rec.709 matrix produces analytic Y/EV; negative fraction is per channel; nonlinear scene input is rejected. |
| orientation/crop | EXIF orientation mapping, orientation-normalized regions, crop identity, and pre-orientation unavailability pass. |
| regional masks | Signed foreground/background EV conflict, binary/soft ambiguity, boundary support, and unsupported-boundary leakage direction pass. |
| `P02-PROXY-01` | Fine texture raises detail energy; 128px comparison remains populated; 32px records reject more features by declared minimum resolution; stale stage/recipe rejection passes. |
| `P02-FILTER-01` | Bilateral, guided, and local-Laplacian-inspired prototypes are finite, non-mutating, declared-size, and remain prototypes; bilateral step preservation passes. |
| `P02-HALO-01/03` | Signed step profiles detect overshoot, undershoot, new extrema, adjacent bands, and 2D unsharp halo response. |
| `P02-NOISE-01` | Deterministic luma/chroma noise raises rendered residuals; declared +3EV maximum lift yields analytic 8x amplification. |
| `P02-WB-01` | Neutral field agrees; opposed illuminant quadrants raise spatial disagreement. |
| `P02-COLOR-01` | Chroma-weighted hue responds to a channel transform; near-neutral hue is invalid rather than unstable; out-of-range RGB raises gamut pressure. |
| `P02-DISPLAY-01/02` | Linear/encoded p50 stay distinct; high clipping moves; absolute dynamic range is unavailable without a model and analytic with one. |
| Phase 01 carry | Matching evidence carries exact raw clipping/SNR; source or evidence identity mismatch rejects it visibly. |
| record contract | Serialization retains version, transform/reference/raw identity, validity, uncertainty, runtime, minimum resolution, and individual values. |
| no mutation | Source image vectors and filter inputs remain byte-for-byte unchanged. |

The focused executable currently completes in under one second on this machine.
That is a regression-suite runtime, not a candidate-search budget.

## Real-Corpus Correlation

The durable report contains the same first eight Samsung DNGs plus eight Sony
ILCE-7M2 ARWs from the Phase 01 corpus:

| Measure | Result |
| --- | ---: |
| Source records | 16 / 16 complete |
| Decode failures | 0 |
| Feature failures | 0 |
| True full-resolution references | 2 (one DNG, one ARW) |
| Other declared validation renders | Half-size LibRaw, then 1024px diagnostic base |
| Per-source proxy ladder | 512 / 256 / 128 long-side targets |
| Command runtime | 289670.3 ms |
| Recipe mutation | false |
| Combined winner score | false |
| Numeric objective threshold selection | false |

The LibRaw linear-sRGB render is explicitly validation-only. It is not Pass 94
or production candidate evidence. It exists to test feature response on real
image content without connecting features to the solver.

Known signals were injected independently into every real image:

| Known signal -> feature | Expected direction observed |
| --- | ---: |
| exact +1EV multiplication -> scene p50 EV | 16 / 16 (delta numerically 1EV) |
| deterministic chromatic noise -> rendered chroma residual | 16 / 16 |
| resolution-cycle blur -> small-scale detail RMS | 16 / 16 |
| unsharp halo -> adjacent-band energy | 16 / 16 |
| opposed color cast -> chroma-weighted hue shift | 16 / 16 |
| +1EV display multiplication -> display high-clip fraction | 16 / 16 |
| opposed left/right cast -> WB spatial disagreement | 14 / 16 |

The two WB non-increases are retained. The measurement remains accepted only
as uncertainty evidence: naturally high baseline regional disagreement can
already exceed the injected perturbation. It is not a WB target and cannot
set a global-WB value or objective threshold.

Proxy records retain 30-35 comparable features at the smallest tested scales.
At the smallest ladder entry, four 128px-minimum features are explicitly
missing when the short side falls below 128. Mean and maximum deltas remain in
the JSON per source; they are uncertainty records, not acceptance thresholds.

## Classification Summary

Accepted technical measurement roles:

- scene EV distribution and occupied range;
- oriented regional and continuous-mask conflict/uncertainty;
- multiscale detail/base-gradient energy and before/after ratios;
- signed halo reversal/overshoot/band/shift diagnostics;
- rendered residual noise and declared-lift amplification;
- exact Phase 01 raw clipping/SNR carry-forward;
- WB estimator/spatial disagreement as uncertainty only;
- working-gamut pressure and before/after hue shift as technical deltas;
- linear/encoded display percentiles and clipping;
- calibrated absolute display range when inputs exist; and
- per-feature proxy/reference disagreement.

Prototype-only: bilateral/guided/local-Laplacian-inspired constructors,
edge-texture ratio, RGB chroma-change mean, and distribution-only display
shoulder/toe ratios.

Needs human study: high/low-key intent, skin/memory color, semantic masks as
subject truth, numeric halo visibility, aesthetic preference, and numeric
technical acceptability thresholds.

Rejected: a single histogram target, ISO-only noise authority, TMQI/HDR-VDP as
a default production objective, and any combined feature score in Phase 02.

## Automated Verification

These focused checks passed against the frozen feature code:

```text
.\build.cmd
.\build\StackRenderedFeatureTests.exe
.\build\StackRawEvidenceTests.exe
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-raw-starting-point-editor-state
```

The final 16-source command returned success and wrote the report linked above.
The existing graph/editor-state suites remain the recipe/graph/undo regression
authority; the new feature module has no planner or graph consumer.

## Gate Audit

| Phase 02 gate | Evidence | Result |
| --- | --- | --- |
| Scene placement | Exact EV fixtures, robust distributions, real +1EV response. | Pass |
| Regional conflict | Oriented heuristics plus continuous-mask conflict/support/leakage fixtures. | Pass |
| Noise cost | Phase 01 SNR, rendered luma/chroma residual, lift amplification, real noise injection. | Pass for measurement/uncertainty; no threshold |
| Highlight/color risk | Exact raw clip states, gamut pressure, hue delta, WB disagreement. | Pass for technical evidence; reconstruction quality deferred |
| Multiscale structure | Four declared scales, before/after ratios, blur and proxy fixtures. | Pass |
| Halo/artifact risk | Exact 1D profiles, 2D fixtures, and 16/16 real-content injected-halo direction. | Pass for detector; perceptual limit deferred |
| Display quality | Separate linear/encoded state, clipping, calibrated-model gate. | Pass for accepted metrics; rolloff/toe prototype |
| Proxy/full disagreement | Two true full references, 16 ladders, per-feature missing/delta records. | Pass |
| Uncertainty | Valid fractions, per-feature uncertainty, missing reasons, WB/mask/proxy/raw identity evidence. | Pass |
| No recipe behavior change | No consumer outside fixtures/validation; focused, graph, raw, editor-state, and full build pass. | Pass |

## Known Limitations

- This is a development-corpus technical audit, not a locked Phase 07 product
  evaluation. It covers two camera families and no independent human sessions.
- The real halo evidence uses a known injected unsharp perturbation. Natural
  halo visibility thresholds and labeled sky/tree/window/roof/fabric crops are
  still human-study scope; no numeric threshold is accepted.
- WB spatial disagreement is not monotonic on 2/16 already-conflicted images.
  The feature is uncertainty only and must not become a WB truth target.
- Rendered noise residual can confuse fine texture; its uncertainty and 128px
  minimum remain mandatory. Pure gain amplification does not change raw SNR.
- Center/border regions are geometric heuristics, never subject semantics.
  Mask reliability measures support/ambiguity, not semantic correctness.
- The validation render is not Stack's production candidate path. Phase 03
  must prove candidate isolation and stage readback identity before scoring.
- CPU implementation cost is suitable for research, not yet an interactive
  candidate budget. Phase 04 owns optimizer/evaluation-budget choice.

## Handoff

Decision: PASS

Accepted artifacts: `rendered-features-v1`, `phase-02-schema-contract-v1`,
`phase-02-fixtures-v1`, `phase-02-real-corpus-correlation-v1`, and the explicit
validation command.

Rejected approaches: hidden Rec.709 luma, nonlinear scene analysis, stale raw
or rendered evidence, silent low-resolution substitution, one aggregate score,
histogram intent, ISO-only noise cost, production adoption of research filters,
and tuning away the two WB counterexamples.

Frozen versions: listed above. The corpus JSON hash is recorded above.

Known limitations: listed above. Non-admitted features cannot enter a Phase 03
objective without returning to this phase and satisfying their missing gate.

Next phase entry evidence: this checkpoint, [the schema](rendered-feature-schema-v1.md),
the focused fixture executable/source, the [real-corpus report](real-corpus-correlation-v1.json),
and the Phase 01 checkpoint.

Next allowed slice: stop. Phase 03 may start only after explicit activation in
the parent progress ledger and must begin with isolated candidate/render
identity—not objective weights or an optimizer.

Do not do next: mutate a recipe, combine a winner score, tune objective weights,
select an optimizer, write graphs/sliders, or treat the validation-only LibRaw
render as a production candidate.
