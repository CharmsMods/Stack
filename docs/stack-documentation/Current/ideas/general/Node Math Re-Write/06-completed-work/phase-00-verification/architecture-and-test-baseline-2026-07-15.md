# Phase 0 Current Architecture Delta

> **Historical completion record:** This file describes the state when this
> phase ended. Later phases and the current status page supersede its point-in-time
> “next phase” language.

- Evidence date: 2026-07-15
- Compared with: `../../07-history/2026-07-12-code-audit/stack-image-pipeline-audit.md`
- Starting commit: `152ebe4d206f4f436a1d826bf750dc0ae8a7a886`
- Branch: `main`
- Working-tree condition: substantial pre-existing user changes were present;
  Phase 0 stayed inside its recorded additive test/build/documentation scope
- Result: Phase 0 exit gate passed

## Why This Delta Exists

The 2026-07-12 audit remains the dated baseline. This document rechecks the
specific integration facts needed to start the rewrite and records changes in
the working tree without rewriting the older audit. It is not a promise to
preserve old projects or old pixel output.

## Revalidation Results

| Rechecked area | 2026-07-15 result | Delta from the dated audit |
| --- | --- | --- |
| Active source selection | Production sources are still recursively collected in `cmake/StackSources.cmake`, with explicit exclusions for inactive legacy aggregate layer files. Validation executables use named source lists. | Same basic production-selection model. Phase 0 adds one named standalone node-math test list and target. |
| Graph and container versions | Graph JSON still writes version `3`. The Stack binary container writes version `2` and retains a legacy version `1` reader path. | No rewrite-generation or stable definition-version system has been introduced. |
| Ordinary image input | Ordinary raster loading still uses `stbi_load(..., 4)` and uploads four unsigned-byte channels through `CreateTextureFromPixels`; it does not yet attach the future semantic color descriptor. | No production source behavior changed. |
| Intermediate/output precision | General empty render targets still use `GL_RGBA16F`. Exact `GL_FLOAT` readbacks exist in selected current runtime paths. | Phase 0 uses isolated RGBA32F test targets so its oracle comparison is not limited by half-float storage; this does not change product targets. |
| Viewport display state | The editor viewport still draws the selected output/tile texture through ImGui. No `GL_FRAMEBUFFER_SRGB` or `GL_SRGB` state was found in the inspected app/editor/renderer path. | The future direct-output/color-state contract is documented but not implemented in Phase 0. |
| Ordinary versus RAW precision | RAW mosaic work remains specialized; corrected/linear/output RAW textures use float-oriented paths including RGBA16F, while ordinary source loading begins from four-channel bytes. | The semantic difference remains implicit in production graph types. |
| Export | The ordinary editor export path still captures four unsigned-byte channels and writes RGBA PNG through `stbi_write_png`; no future explicit output color/profile contract is present. | No change. |
| Alpha/composite math | The current Alpha Over shader still computes `outA = b.a + a.a * (1 - b.a)` and `outRgb = b.rgb * b.a + a.rgb * (1 - b.a)` before division. The backdrop RGB term still lacks multiplication by `a.a`. | The audited defect remains. Phase 0 deliberately does not correct formulas. |
| Graph texture caches | General graph image/mask caches still retain one texture per keyed entry and prune inactive entries; no general byte budget or LRU policy was found. | A RAW-development stage cache now has a specific soft byte budget, but that does not solve general graph resource planning. |
| Validation/diagnostics | `Graph::Validate()` is now called from `Graph::SyncLayerNodes()`, but its returned result is discarded there and there is still no persistent typed semantic-diagnostic system. | Partial call-site change after the audit; Phase 1/2 work is still required. |
| Catalog size and stubs | The current catalog builder contributes 52 browser-visible registered layers plus 63 explicitly added non-layer entries, for 115 entries. Known incomplete/specialized items remain and the catalogs are not a rewrite backlog. | Count rechecked against the dirty working tree; no Phase 0 catalog change. |
| Test registration | CMake now registers the CPU and GPU modes of `StackNodeMathReferenceTests` with CTest. Existing focused validation executables remain directly runnable. | This is the intended Phase 0 change. It is selected pointwise math coverage, not general formula/color/alpha coverage. |
| Runtime shader coverage | Production shaders remain outside the new suite. The Phase 0 shader independently exercises Identity, Add, Multiply, Add-then-Multiply, and Multiply-then-Add. | Establishes the reusable comparison path without claiming existing production-node correctness. |

## CPU/GPU Reference Architecture Decision

For selected factual math definitions, Stack will use a small independent CPU
implementation as the reference rather than capturing old Stack output:

1. Generate finite numerical values in memory.
2. Evaluate the documented formula independently on the CPU, using double
   precision where practical.
3. Evaluate the selected GPU implementation in its declared float/storage
   policy.
4. Read the result back exactly in the target's component type.
5. Compare every component using a tolerance recorded by the test.
6. Add algorithm-specific properties and user visual review when a later change
   is not adequately described by pointwise reference values.

Phase 0 implements this policy for a minimal pointwise set. It does not decide
the complete Phase 1 node-definition schema, and it does not require every
algorithm to have a simple scalar CPU duplicate.

## Phase 0 Observability Subset

The standalone GPU run reports:

- OpenGL vendor, renderer, and version;
- generated target dimensions and RGBA32F storage;
- source, output, and peak test-target bytes;
- shader compilation/link failure through the existing GL helper;
- labeled OpenGL failure stages;
- exact `GL_FLOAT` readback;
- maximum CPU/GPU absolute error and comparison tolerance;
- CPU and GPU authored-order separation; and
- `glFinish`-synchronized wall time for each tiny pass.

This is intentionally enough to support the first definitions. It is not yet a
production render-graph inspector or resource planner.

## Reproducible Evidence

Commands executed from the repository root:

```powershell
cmake --build build --config Release --target StackNodeMathReferenceTests
.\build\StackNodeMathReferenceTests.exe --cpu-only
.\build\StackNodeMathReferenceTests.exe --gpu
ctest --test-dir build -C Release --output-on-failure -R StackNodeMathReference
.\build.cmd
.\build\StackGraphBehaviorTests.exe
```

Recorded results:

- CPU: five formulas over four generated RGBA values passed; authored-order
  separation was `4`.
- GPU: NVIDIA GeForce RTX 3060, OpenGL `4.3.0 NVIDIA 596.49`; maximum CPU/GPU
  absolute error was `7.947286e-08`, below the `2e-6` tolerance; authored-order
  separation was `4`.
- CTest: 2/2 registered node-math tests passed.
- Preferred full build: passed.
- Existing focused graph behavior test: passed.

The exact pass timings are emitted on every GPU run but are not treated as
stable performance baselines because driver, hardware, clock state, and system
load affect them.

## Known Limits And Hardware Dependence

- The GPU case requires a working hidden OpenGL 4.3 context and driver. A
  context or shader failure is a real test failure, not silently skipped.
- CPU double and GPU float results are not expected to be bit-identical; each
  future definition must record a justified tolerance and precision policy.
- The current cases are finite pointwise values. NaN/Infinity policy, alpha,
  color transforms, sampling, neighborhoods, reductions, tiling, and
  specialized algorithms need their own later cases.
- Synchronized wall timings are diagnostic, not GPU timer-query benchmarks.
- The harness shader proves the test path and authored-order distinction. It
  does not certify current production Add/Multiply nodes and is not the future
  semantic IR or fusion compiler.
- No product pixels or UI changed, so user visual review is not applicable to
  this pass. Later pixel-changing passes require user review on real images
  selected at the time of the change.

## Phase 0 Exit Audit

| Requirement | Evidence | Result |
| --- | --- | --- |
| Deterministic generated-value/reference harness | Fixed four-value RGBA suite and independent CPU formulas | Pass |
| Identity, Add, Multiply, and both authored orders | Five CPU and GPU cases | Pass |
| Current architecture delta | Revalidation table above | Pass |
| Bounded observability | GL identity/errors, shader diagnostics, float readback, bytes, timings, comparison metrics | Pass |
| CPU-versus-GPU test decision | NMR-125 and the policy above | Pass |
| Per-pass review pattern | Automated reference tests + preferred build + risk-appropriate user visual review recorded in the tracker | Pass |
| Equations and order proved | CPU/GPU error within tolerance; both orderings differ by `4` | Pass |
| No historical corpus/guarantee | Only generated in-memory values are used | Pass |
| Hardware/nondeterminism explicit | Limits above | Pass |

Phase 0 is complete. Phase 1 is eligible for a separately documented and
explicitly activated contract slice; it is not activated by this exit decision.
