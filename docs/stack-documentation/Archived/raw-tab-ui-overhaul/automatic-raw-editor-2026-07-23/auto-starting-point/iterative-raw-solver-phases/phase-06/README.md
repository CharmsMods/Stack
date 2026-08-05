# Phase 06 Checkpoint

- Checkpoint: `phase-06-v1`
- Date: 2026-07-10
- Decision: PASS
- Integration: `raw-precise-integration-v1`
- Native runtime: `raw-precise-native-runtime-v1`
- Frozen solver: `raw-precise-dry-run-v1`
- Report: `raw-precise-integration-report-v1`
- Product mode: one `Build Starting Point` action; Precise default, Fast retained
- Next phase activated: no

## Outcome

Phase 06 connects the frozen Phase 05 solver to Stack's native RAW tab. Precise
mode evaluates isolated rendered recipes in the background, remains cancelable,
full-resolution verifies one candidate, and then either applies the complete
visible recipe once or leaves the current recipe unchanged. Candidate search
never replaces the live preview or exposes intermediate values.

The final recipe projects exactly into RAW Exposure, eligible WB, Local Range,
Finish Tone, and Display Fit controls. One existing Starting Point Undo snapshot
restores the complete pre-action recipe. Save/load round-trip and source/base
identity gates are part of the apply contract.

## Frozen Product Contract

```text
explicit Build Starting Point (Precise)
-> isolated native decode/evidence/warm start/search
-> 45 real proxy renders in the frozen stage budget
-> independent five-stage true-resolution verification
-> source/base/recipe/version/persistence preflight
-> prepare and exact-check every complete recipe copy
-> one main-thread visible-recipe commit
-> ordinary live recipe render
-> sliders, graphs, readouts, diagnostics, and Display Fit ownership update
-> stop; user owns the result
```

Cancel invalidates the isolated worker snapshot directly. It does not rerender
or clear the unchanged live preview, analysis, suggestions, or diagnostics.
Precise worker results are lifecycle/candidate records only and cannot enter
the generic viewport-result path.

## Accepted Artifacts

| Version | Artifact |
| --- | --- |
| `raw-precise-integration-v1` | `src/Raw/RawPreciseIntegration.h/.cpp` |
| `raw-precise-native-runtime-v1` | `src/Raw/RawPreciseNativeRuntime.h/.cpp` |
| native lifecycle and atomic apply | `EditorRenderWorker.*`, `EditorModuleRendering.cpp`, `EditorModuleRawWorkspaceAutoBase.cpp` |
| `phase-06-fixtures-v1` | `tools/precise_integration_tests.cpp` and `StackPreciseIntegrationTests.exe` |
| `phase-06-validation-command-v1` | `--validate-raw-precise-integration` |
| `phase-06-engineering-subset-v1` | [Validation summary](validation-summary-v1.md) |
| `raw-precise-integration-report-v1` | [Frozen four-source report](precise-integration-v1.json) |

Report SHA-256:

```text
FED2E76C26C178904DAAABB5A42A8D6768EFA6D8C806309F7B746ED6490B4D19
```

## Required State Guarantees

- Search performs zero recipe, Undo, dirty-state, slider, graph, or preview
  mutation.
- Apply requires exact request, source key/hash/content, base recipe, solver,
  integration, full-resolution, and round-trip identity.
- All potentially rejecting preparation and exact-match checks finish before
  the live recipe is touched.
- At most one complete recipe commit occurs.
- Existing controls outside the 13-field solver projection are preserved.
- Managed/custom graph projects preserve ownership; Precise is unavailable
  there and the existing Fast path remains available.
- Source switch, same-key identity change, recipe edit, close, cancel, or failed
  verification cannot apply a stale candidate.
- The solver does not rerun after apply or on load.
- A later manual upstream edit marks Display Fit stale; the exact applied
  recipe's first accepted live render records it as `Auto-current`.

## Automated Verification

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

The renderer-backed validation command is:

```text
.\build\Stack.exe --validate-raw-precise-integration \
  --development-file <unclipped.dng> \
  --development-file <partial-clip.dng> \
  --development-file <all-channel-clip.dng> \
  --validation-file <validation.arw> \
  --output <phase-06/precise-integration-v1.json> \
  --proxy-max-dimension 256 \
  --feature-max-dimension 256 \
  --warm-max-dimension 2048
```

## Native Verification

The rebuilt native RAW tab was exercised on `DSC00631.ARW`:

- Cancel left the source `Not Edited`, kept RAW Exposure and Display Fit exact,
  preserved the current preview and suggestions, and labeled all four groups
  `unchanged (canceled before apply)`.
- A complete solve applied once and reported only `Finish Tone` changed.
- The editable RGB Finish Tone graph showed five points:
  `0.00->0.00`, `0.18->0.31`, `0.45->0.53`, `0.75->0.77`, `1.00->1.00`.
- The applied live render reported `Display Fit: Auto-current`.
- One Undo restored the original darker image, neutral recipe, default visible
  values, and `Display Fit: Ready`.
- No save was performed and the test recipe was undone before handoff.

This is structural native integration evidence, not a claim of human
photographic preference or locked-corpus product readiness.

## Known Limits

- The engineering report contains four sources and two camera families; it is
  not the locked Phase 07 corpus.
- Human ratings for strength, naturalness, halo visibility, noise acceptability,
  and next-manual-control choice have not been collected.
- Solver science, objective policy, feature validity, and budgets remain frozen
  from Phase 05. Phase 06 did not tune around native examples.
- Fast remains the compatibility path for managed/custom graphs.

## Handoff

Decision: PASS.

Accepted artifacts: the versions, report, product mode, projection, lifecycle,
atomic apply, cancellation, persistence, Undo, and commands above.

Rejected approaches: applying intermediate candidates, output-only preview
replacement, continuous reruns, post-mutation validation failure, and treating
cancellation as a normal viewport render.

Frozen versions: `raw-precise-dry-run-v1`, `raw-precise-integration-v1`,
`raw-precise-native-runtime-v1`, and `phase-06-v1`.

Known limitations: locked-corpus and structured human acceptance remain Phase
07 work.

Next phase entry evidence: this checkpoint and its frozen report.

Next allowed slice: stop. Phase 07 may begin only after the parent progress
ledger explicitly activates it.

Do not do next: change solver science, tune against the locked set, claim
photographic completion, or start Phase 07 implicitly.
