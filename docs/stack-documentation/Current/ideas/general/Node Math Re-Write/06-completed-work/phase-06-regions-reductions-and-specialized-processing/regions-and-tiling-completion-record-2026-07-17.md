# Phase 6A Completion Evidence

> **Historical completion record:** This file describes the state when this
> phase ended. Later phases and the current status page supersede its point-in-time
> “next phase” language.

- Date: 2026-07-17
- Result: complete
- Program state after this pass: Phase 6 remains in progress; no active slice
- Accepted decision: NMR-135
- Contract: `../../03-technical-contracts/regions-reductions-and-specialized-processing/image-region-and-tiling-contract-v1.md`

## Scope And Starting Context

Phase 6A was activated as the smallest reliable first Phase 6 vertical slice:
establish shared spatial and region-planning contracts, then prove them through
Stack's existing Gaussian Blur tile path without adding a public node or
changing its formula.

The starting repository baseline was commit
`152ebe4d206f4f436a1d826bf750dc0ae8a7a886` on `main`, with substantial
pre-existing user changes and untracked work. This pass preserved unrelated
work and limited edits to Node Math spatial contracts, render-region/tile
planning, render-worker publication, focused inspection UI, validation, build
selection, and this workstream's documentation.

NMR-122 was narrowed for this slice: no general public kernel, structuring
element, morphology, sampler, or reconstruction value was introduced. NMR-123
remains open because no public reusable reduction path was selected.

## Implemented Contract

### Spatial descriptor v2

`src/NodeMath/ContractTypes.*` and `DescriptorSerialization.cpp` now represent
and validate:

- finite full and data windows;
- explicit bottom-left or top-left raster origin;
- positive finite pixel aspect; and
- deterministic descriptor schema-1 migration to bottom-left origin.

Canonical semantic descriptor serialization advances from schema 1 to schema
2. This deliberately advances semantic fingerprints and dependent cache
identities. It does not advance the editor graph project schema, add a project
migration promise, or change source pixels.

### Shared region planning

New `src/NodeMath/RegionPlanning.*` owns:

- empty, finite, full, and global render requests;
- half-open integer rectangles and channel spans;
- independent X/Y render scale;
- pointwise identity mapping;
- neighborhood input expansion and output preservation;
- reduction full-input mapping; and
- validation and intersection/containment helpers.

The reduction mapping is contract infrastructure only. Phase 6A does not add a
live reduction node or execution path.

### Graph and tile planning

`src/Renderer/RenderTiling.*` now builds a region plan from a real graph
snapshot. It:

- classifies the existing supported pointwise stages;
- classifies existing Gaussian Blur and Box Blur as neighborhood stages;
- derives their required support from `int(max(1, amount))`;
- accumulates support through multiple neighborhood stages;
- keeps clamp as the named live border behavior;
- treats a legal unsupported stage as an explained full-frame boundary;
- distinguishes that fallback from structural graph failures such as cycles or
  missing nodes;
- uses the greater of planner-required halo and user-requested extra halo; and
- exposes cancellation-aware tile iteration with Completed, Canceled, and
  Failed results.

The effective tile content is reduced as needed so its halo-expanded allocation
stays within the selected tile texture size.

### Render publication and inspection

`src/Editor/EditorRenderWorker.*` consumes the shared graph-region plan before
choosing tiled execution. A canceled traversal destroys partial textures and
returns without publishing them as a renderer result.

Graph performance state now records whether planning selected tileable or
full-frame execution, the required X/Y halo, and an explanatory fallback
reason. The performance popup presents those facts. The settings popup now
calls the user preference `Extra Tile Halo` and explains that Stack derives the
correctness halo automatically.

## Preserved Pixel Behavior

Phase 6A does not change:

- Gaussian Blur or Box Blur sample equations;
- their square support rule;
- clamp border sampling;
- which RGBA components they process;
- operation order in the authored graph;
- pointwise IR ordering or fusion rules;
- RGBA16F intermediate materialization;
- color, transfer, alpha, viewport, or output conversions; or
- the public node browser.

The only live behavior change is scheduling: an eligible blur chain can now be
tiled with planner-derived support instead of relying on a manually guessed
halo. A legal stage outside the Phase 6A planner remains full-frame.

## Automated Evidence

### Focused region contract

`StackNodeMathPhase6Tests.exe` passes 19 checks covering:

- descriptor-v2 serialization and both raster origins;
- schema-1 migration;
- invalid aspect/origin rejection;
- odd dimensions and nonzero-origin ROI mapping;
- pointwise, neighborhood, and reduction mappings;
- invalid support, channel span, and render scale; and
- Gaussian support under render scale.

### Graph planner and cancellation

`StackGraphBehaviorTests.exe` covers:

- exact Gaussian support discovery;
- output full/data windows and origin;
- pointwise versus neighborhood classification;
- accumulated support through two blur stages;
- planner halo overriding an insufficient user extra halo;
- Completed, Canceled, and Failed tile iteration;
- and explained full-frame fallback at a legal unsupported layer.

### Live GPU equivalence

`Stack.exe --validate-node-math-phase6` creates a hidden OpenGL 4.3 context and
a generated 530x270 RGBA8 source, then renders:

```text
Image
-> Gaussian Blur (amount 3.9)
-> Technical Exposure
-> Output
```

It compares the production full-frame result with a manually stitched result
using planner-derived tile regions and halo. Maximum absolute difference was
`0`, within the declared `2.5e-3` tolerance. The case exercises odd dimensions,
edge tiles, clamp borders, a neighborhood stage followed by a pointwise stage,
and exact planner support.

## Commands And Results

```powershell
cmake --build build --config Release --target StackNodeMathContractTests StackNodeMathPhase2Tests StackNodeMathPhase3Tests StackNodeMathPhase4Tests StackNodeMathPhase5Tests StackNodeMathPhase6Tests StackGraphBehaviorTests Stack -j 1
ctest --test-dir build -C Release --output-on-failure -R StackNodeMath
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-node-math-phase6
.\build\Stack.exe --validate-layer-registry
.\build.cmd
```

Results:

- serial selected-target build: passed;
- Node Math CTest suite: 11/11 passed;
- graph behavior tests: passed;
- Phase 6 live validation: passed, maximum difference `0`;
- layer registry validation: passed; and
- repository-preferred Windows build: passed.

The first parallel `build.cmd` attempt encountered the repository's known
generated-font-header race in `tools/bake_fonts.py`. The immediate clean retry
passed without a source change; the serial selected-target build had already
proved the same generated asset and application target. This pass did not
expand into unrelated build-tool repair.

## Files And Owners

Primary Phase 6A owners:

- `src/NodeMath/ContractTypes.*`
- `src/NodeMath/DescriptorSerialization.cpp`
- `src/NodeMath/RegionPlanning.*`
- `src/Renderer/RenderTiling.*`
- `src/Editor/EditorRenderWorker.*`
- `src/Editor/EditorModuleTypes.h`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/Editor/Internal/EditorModulePreviewState.cpp`
- `src/App/AppSettingsPopup.cpp`
- `src/App/Validation/Suites/NodeMathPhase6Validation.cpp`
- `src/App/Validation/ValidationCommandRunner.cpp`
- `src/App/Validation/ValidationSuites.h`
- `tools/node_math_phase6_tests.cpp`
- `tools/graph_behavior_tests.cpp`
- `CMakeLists.txt`
- `cmake/StackSources.cmake`

## Review Status And Limitations

Automated contract, graph, GPU, registry, and build evidence is complete.
Native visual confirmation of the `Extra Tile Halo` wording and the new Graph
Performance region-plan line was not performed; it is a small optional human
review item and not evidence for pixel correctness.

Phase 6A intentionally does not prove or implement:

- true extent-changing geometry or resampling execution;
- a live proxy/render-scale path;
- reusable reductions or analysis outputs;
- public kernels, morphology, samplers, or reconstruction filters;
- pyramids or multiresolution planning;
- general RAW, frequency, external-model, preview, or export region planning;
- public node-library expansion; or
- Phase 7 infrastructure.

The live tile planner's selected pointwise and neighborhood set is deliberately
conservative. Unsupported legal stages execute full-frame with a reason.

## Exit Decision

Phase 6A meets its bounded contract and verification gates. It is complete.
Phase 6 itself is not complete because the roadmap's geometry, reduction, and
specialized-stage coverage is still outstanding. No Phase 6B slice is active.
The next implementation action must select and activate one small vertical
slice rather than treating this evidence as authorization for the remainder of
Phase 6 or Phase 7.
