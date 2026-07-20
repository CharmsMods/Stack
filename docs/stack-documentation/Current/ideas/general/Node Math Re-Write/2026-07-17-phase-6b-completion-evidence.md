# Phase 6B Completion Evidence

- Date: 2026-07-17
- Result: complete
- Program state after this pass: Phase 6 remains in progress; no active slice
- Accepted decision: NMR-136
- Contract: `phase-6b-reduction-contract-v1.md`

## Scope And Product Result

Phase 6B implemented one bounded reusable reduction: the public `Field Mean`
node. It converts an explicitly authored one-channel per-pixel scalar field into
one uniform scalar value:

```text
Scalar field -> Field Mean -> Scalar
```

That scalar can drive an existing uniform graph input such as Exposure EV. A
full image is not silently converted to luminance and cannot connect directly
to `Field Mean`; the user must choose a channel with `Channel Split` or place
an explicit `Luminance Mask` first.

The node is available in the Analysis / Measure browser category with exact
definition ID `stack:analysis/field-mean` and version `1.0.0`. Its required
`fieldIn` input is a one-channel per-pixel `ScalarField`; its `valueOut` output
is a one-component uniform `Scalar`. Both use Unknown units in v1, so the node
does not invent or convert units.

## Exact Math And Failure Policy

`Field Mean` computes the arithmetic mean over every sample in the connected
field's current full frame. The first correctness path reads the materialized
float field in bounded row chunks and accumulates samples in deterministic
order with Neumaier compensated float64 summation.

An empty population, NaN, or infinity fails the reduction. Stack does not
ignore, clamp, replace, or publish a stale measured value. A dependent Exposure
render is not submitted when the scalar evaluation fails, and Graph Performance
records the named reduction failure.

## Scheduling, Identity, And Cache Behavior

The region planner classifies `Field Mean` as `Reduction`, requires full-frame
execution, and treats it as a pointwise-fusion barrier. The scalar fingerprint
includes the exact node definition ID/version/hash, output socket, algorithm
version, connected input fingerprint, and extent.

Successful values are cached as runtime data and are never serialized into the
project. Re-executing an unchanged reduction input after invalidating only the
downstream Exposure reuses the persistent scalar cache. Inactive entries are
pruned with the graph execution lifecycle.

## Authored Graph And Persistence Proof

The live validator creates the real editor graph and lowers it through
`EditorModule::BuildGraphSnapshot`:

```text
Image -> Channel Split -> R -> Field Mean -> Exposure EV
Image -------------------------------> Exposure Image -> Output
```

The graph produced a nonzero viewport texture. Its generated 8x4 red field has
32 samples and exact mean `0.5`. The renderer reported one reduction pass and
one cache miss on first execution, then zero new reduction passes and a cache
hit after a downstream-only invalidation. Its Exposure result matched the same
authored graph driven by a constant scalar `0.5` with maximum absolute
difference `0`, inside the declared RGBA16F tolerance of `2.5e-3`.

Graph behavior coverage also proves exact definition resolution, normalized
socket metadata, full-image rejection with explicit extraction guidance,
connection into Exposure EV, save/load preservation of the node and scalar
link, and full-frame Reduction planning.

## Generated CPU Evidence

`StackNodeMathPhase6Tests.exe` now passes 24 checks. The five Phase 6B cases
cover:

- ordinary samples with mean `0.5`;
- cancellation-sensitive samples `1e8, 1, -1e8`, producing `1/3`;
- empty-input failure;
- NaN failure; and
- infinity failure.

The maintained `node-socket-catalog.md` was regenerated and records both Field
Mean sockets, directions, logical types, roles, channel/component shapes,
required status, visibility tier, and Unknown units.

## Commands And Results

```powershell
cmake --build build --config Release --target StackNodeMathPhase6Tests StackGraphBehaviorTests Stack -j 1
ctest --test-dir build -C Release --output-on-failure -R '^StackNodeMath'
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-node-math-phase6
.\build\Stack.exe --validate-layer-registry
.\build.cmd
```

Results:

- Phase 6 CPU contract program: 24 checks passed;
- full Node Math CTest suite: 13/13 passed, including the dedicated Phase 6B
  CPU and live-GPU aliases;
- graph behavior tests: passed;
- layer registry validation: passed;
- live Field Mean result: `0.5` from 32 samples;
- persistent reduction cache proof: passed;
- constant-EV maximum difference: `0`;
- existing Gaussian full/tiled maximum difference: `0`; and
- repository-preferred Windows build: passed.

## Files And Owners

Primary Phase 6B owners are:

- `src/NodeMath/ReductionMath.*`
- `src/Editor/NodeGraph/NodeGraphTypes.h`
- `src/Editor/NodeGraph/EditorNodeGraph.*`
- `src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp`
- `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`
- `src/Editor/NodeGraph/UnifiedNodeDefinitionRegistry.cpp`
- `src/Editor/Internal/EditorModuleGraphMutation.cpp`
- `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`
- `src/Renderer/RenderPipeline.h`
- `src/Renderer/RenderTiling.*`
- `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`
- `src/Renderer/Internal/RenderPipelineGraphPointwiseFusion.cpp`
- `src/Editor/Internal/EditorModulePreviewState.cpp`
- `tools/node_math_phase6_tests.cpp`
- `tools/graph_behavior_tests.cpp`
- `src/App/Validation/Suites/NodeMathPhase6Validation.cpp`
- `CMakeLists.txt`
- `cmake/StackSources.cmake`

## Review Status And Limitations

The formula, failure policy, graph contract, persistence, planning, runtime
execution, cache behavior, registry, catalog, and full build are automatically
verified. A native human can optionally confirm the Field Mean browser entry,
pin wording, and Graph Performance reduction lines; this is not required for
the numerical evidence.

Phase 6B intentionally does not implement weighted mean, sum, min/max,
variance, percentiles, histograms, statistics resources, reduction masks or
ROI, automatic luminance selection, automatic exposure, GPU reduction trees,
extent-changing geometry, pyramids, RAW/frequency integration, formula
corrections, or Phase 7 library expansion.

## Exit Decision

Phase 6B meets NMR-136 and its bounded verification gates. It is complete.
Phase 6 itself remains open because representative extent-changing geometry and
specialized-stage integration have not been activated or completed. No next
Phase 6 slice is active; beginning one requires a separate contract and status
update.
