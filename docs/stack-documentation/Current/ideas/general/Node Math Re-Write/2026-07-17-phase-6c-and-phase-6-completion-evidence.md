# Phase 6C And Phase 6 Completion Evidence

- Date: 2026-07-17
- Phase 6C result: complete
- Phase 6 result: complete
- Program state after this pass: stopped before Phase 7
- Accepted decisions: NMR-137 and NMR-138
- Contract: `phase-6c-geometry-and-specialized-contract-v1.md`

## Product Result

Stack now has one real extent-changing graph operation. `Reformat`, in the
Geometry / Transform browser category, produces the exact width and height the
user enters. It supports explicit Nearest and Linear reconstruction with Clamp
border behavior and does not alter or guess color, transfer, range, or alpha
state.

Downstream rendering, texture caching, reference-source lookup, viewport
dimensions, persistence, semantic descriptors, and the shared region planner
all carry the changed extent. Different-sized images cannot silently enter a
multi-image operation: the plan fails before rendering and asks for an explicit
Reformat.

Specialized work is now represented by typed plan boundaries for RAW decode and
development, RAW neural/external execution, multi-frame merge, FFT and inverse
FFT, retained-spectrum operations, scopes, preview readback, and export
readback. RAW remains opaque. Consumer readbacks report their extent/quality
policy and never change graph pixels.

## Exact Reformat Behavior

`stack:geometry/reformat` version `1.0.0` uses inverse pixel-center mapping.
Nearest and separable linear fetches clamp at the source edge and process all
four RGBA components independently. The output uses the declared finite extent,
preserves the incoming origin/raster convention/pixel aspect when known, and
sets its data window to the full result.

The current production path materializes one RGBA16F full-frame result. It is a
Sample/Resample and pointwise-fusion boundary. Linear downscaling does not claim
an antialiasing prefilter; this limitation is visible in the contract rather
than hidden behind a vague Scale promise.

## Authored Live Evidence

The geometry validator built, saved, reloaded, lowered, and executed:

```text
4x3 Image -> 7x5 Linear Reformat -> Exposure 0.25 EV -> Output
```

The result was a live 7x5 texture. It matched the independent CPU reference
with maximum absolute difference `0.000351787`, inside the `2.5e-3` RGBA16F
tolerance. Preview, scope, and export then recorded typed consumer boundaries
without changing the graph result.

The specialized validator built and executed:

```text
8x4 Image -> FFT -> Inverse FFT -> Output
```

The planner reported distinct typed forward and inverse full-frame multipass
stages. The live round trip differed from the source by `0.000451922`, inside
the declared `7.5e-3` tolerance, with no specialized failure.

The prior Phase 6 evidence remains part of the combined exit proof:

- Gaussian full-frame versus planner-derived tiled output: maximum difference
  `0`;
- Field Mean: exactly `0.5` from 32 samples, persistent scalar-cache reuse, and
  maximum difference `0` from a constant-EV reference; and
- authored pointwise order/fusion evidence from Phase 4 remains green under the
  shared graph and renderer.

## Automated Coverage

`StackNodeMathPhase6Tests.exe` passes 39 checks. The new cases cover:

- Reformat settings and finite-extent validation;
- origin, raster convention, pixel aspect, and data-window propagation;
- inverse ROI mapping and reconstruction support;
- nearest and linear RGBA reference results;
- proxy pixel-center invariance;
- RAW, RAW external, FFT, inverse FFT, scope, preview, and export policies.

`StackGraphBehaviorTests.exe` additionally covers:

- exact Reformat definition and socket metadata;
- authored completed-chain traversal before execution;
- settings and definition save/load;
- planner output extent and Sample/Resample classification;
- hard mismatch diagnostics requiring explicit Reformat; and
- distinct typed RAW decode/development stages.

The maintained `node-socket-catalog.md` was regenerated from the live registry
and contains Reformat's required image input and image output.

## Commands And Results

```powershell
cmake --build build --config Release --target StackNodeMathPhase6Tests StackGraphBehaviorTests Stack -j 4
.\build\StackNodeMathPhase6Tests.exe
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-node-math-phase6
.\build\Stack.exe --validate-layer-registry
.\build\Stack.exe --write-node-socket-catalog "<absolute catalog path>"
ctest --test-dir build -C Release --output-on-failure
.\build.cmd
```

Results:

- Phase 6 CPU contract program: 39 checks passed;
- full registered CTest set: 13/13 passed;
- graph behavior tests: passed;
- layer registry validation: passed;
- live geometry, neighborhood, reduction, consumer, and frequency validation:
  passed;
- maintained socket catalog generation: passed; and
- repository-preferred Windows build: passed.

The preferred build caught and closed one cross-phase source-compatibility
issue: the new semantic geometry field initially interrupted older aggregate
initializers. The field was moved to the end of the structure, preserving
existing construction order without changing runtime semantics, and the full
build then passed.

## Primary Owners

- `src/NodeMath/GeometryMath.*`
- `src/NodeMath/SpecializedPlanning.*`
- `src/NodeMath/SemanticSpine.*`
- `src/Renderer/RenderTiling.*`
- `src/Renderer/RenderPipeline.h`
- `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`
- `src/Renderer/Internal/RenderPipelineNodePasses.cpp`
- `src/Renderer/Internal/RenderPipelinePrograms.cpp`
- `src/Renderer/Internal/RenderPipelineReadback.cpp`
- `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`
- `src/Editor/NodeGraph/EditorNodeGraph.*`
- `src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp`
- `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`
- `src/Editor/NodeGraph/UnifiedNodeDefinitionRegistry.cpp`
- graph traversal/mutation/UI owners for Reformat
- `tools/node_math_phase6_tests.cpp`
- `tools/graph_behavior_tests.cpp`
- `src/App/Validation/Suites/NodeMathPhase6Validation.cpp`

## Phase 6 Exit Audit

The roadmap's two exit requirements are satisfied:

1. Pointwise, geometry, neighborhood, reduction, and specialized examples now
   share the semantic descriptor, stable definition/socket identity,
   diagnostic, fingerprint, and physical planning framework.
2. The representative tiled/full-frame Gaussian generated case matches within
   tolerance, and the new extent-changing and specialized full-frame paths have
   independent CPU or round-trip numerical evidence.

Phase 6 is therefore complete. This does not claim that every operation is
tileable, proxy-capable, decomposable, or publicly exposed. It means those
different execution classes can now state their requirements and fail honestly
inside one framework.

## Stop Boundary

Phase 7 has not started. This pass does not authorize a mass operation-table
import, more geometry/filter/reduction nodes, formula corrections, RAW
decomposition, high-level compound expansion, or browser redesign. The next
workstream action is a deliberate Phase 7 product-selection pass.
