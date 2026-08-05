# Node Math Rewrite Code Source Map

- Evidence date: 2026-07-12 Stack audit; connection compatibility owner update 2026-08-01
- Created: 2026-07-15
- Status: dated navigation map plus completed Phase 6 ownership update, not a
  substitute for current-code inspection

Before editing, inspect current code, CMake selection, tests, and the dirty
working tree. Update this map when ownership changes; do not trust old line
numbers.

## Phase-Oriented Map

| Phase | Primary current owners to inspect | Main questions |
| --- | --- | --- |
| 0 — Forward verification and observability | `CMakeLists.txt`; `cmake/StackSources.cmake`; `src/main.cpp`; `src/App/AppShell.cpp`; `src/Renderer/GLHelpers.cpp`; `src/Renderer/Internal/RenderPipelineReadback.cpp`; `src/Renderer/Internal/RenderPipelineResources.cpp`; `src/Editor/EditorRenderWorker.cpp`; `src/Editor/Internal/EditorModuleRendering.cpp`; `src/Editor/UI/EditorViewport.cpp`; `tools/graph_behavior_tests.cpp`; `tools/node_math_reference_harness.*`; `tools/node_math_reference_tests.cpp` | Which binaries/sources are active? Where should generated numeric inputs, CPU/reference math, GPU results, GL failures, passes, and bytes be exposed without saving an old-output corpus? |
| 1 — Contracts and identities | Existing owners to represent: `src/Editor/NodeGraph/NodeGraphTypes.h`; `NodeGraphModelTypes.h`; `NodeGraphPayloads.h`; `EditorNodeGraph.h`; `EditorNodeGraphSerializer.cpp`; `EditorNodeGraphSelectionExport.cpp`; `src/Persistence/StackBinaryFormat.cpp`. Accepted isolated owners: `src/NodeMath/ContractTypes.*`; `NodeDefinition.*`; `DescriptorPropagation.*`; `ProjectSchema.*`; `tools/node_math_contract_tests.cpp`. | Where do definition, port, parameter, graph, and project identities live? How should the new breaking project generation, future versions, unknown kinds, invalid links, copy/paste, and missing definitions behave? |
| 2 — Semantic image spine | Contract/math owners: `src/NodeMath/DescriptorSerialization.*`; `SourceColorMetadata.*`; `TechnicalImageMath.*`; `SemanticSpine.*`; `PngMetadataWriter.*`. Live owners: `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`; `EditorModulePersistence.cpp`; `EditorModuleRendering.cpp`; `src/Editor/EditorModuleTypes.h`; `src/Renderer/RenderPipeline.h`; `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`; `RenderPipelineNodePasses.cpp`; `RenderPipelinePrograms.cpp`; `RenderPipelineResources.cpp`; `src/Editor/UI/EditorViewport.cpp`. | Phase 2 now owns descriptor propagation and cache identities, non-converting PNG/JPEG source color inspection, explicit technical math, direct output policy, and stable semantic analysis. Reinspect before expanding formats or profile backends. |
| 2 — Graph/persistence/inspection | `src/Editor/NodeGraph/NodeGraphTypes.h`; `NodeGraphModelTypes.h`; `NodeGraphPayloads.h`; `EditorNodeGraph.cpp`; `EditorNodeGraphDefinitions.cpp`; `EditorNodeGraphSerializer.cpp`; `Model/EditorNodeGraphMutation.cpp`; `Model/EditorNodeGraphTraversal.cpp`; `Model/EditorNodeGraphLayoutValidation.cpp`; `Serialization/EditorNodeGraphImageSerialization.*`; `Serialization/EditorNodeGraphUtilitySerialization.*`; `UI/EditorNodeGraphUINodes.cpp`; `UI/EditorNodeGraphUILinksAndGroups.cpp`; `UI/EditorNodeGraphUINodeBrowser.cpp`; `src/Editor/Internal/EditorModuleNodeBrowserThumbnails.cpp`. | Technical Image nodes, source descriptors, graph JSON version 4, permissive connectivity, round-trip persistence, wire tooltips, output notices, and browser previews live here. |
| 3 — Definitions and values | Contract: `src/NodeMath/FirstClassValue.*`. Live registry: `src/Editor/NodeGraph/UnifiedNodeDefinitionRegistry.*`; `EditorNodeGraphDefinitions.*`; `src/Editor/LayerRegistry.cpp`; `src/Editor/Timeline/TimelineAnimation.*`. Graph/persistence: `NodeGraphTypes.h`; `NodeGraphPayloads.h`; `NodeGraphModelTypes.h`; `EditorNodeGraph.*`; `EditorNodeGraphSerializer.cpp`; graph mutation/layout/traversal and UI files. Renderer binding: `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`. Tests: `tools/node_math_phase3_tests.cpp`; `tools/graph_behavior_tests.cpp`. | Phase 3 now owns exact values, field-vs-mask typing, the selected Scalar Value → Exposure binding, graph schema 5 exact definition resolution, and the unified live definition/parameter registry. Runtime-expanded Data Math/MFSR ports remain definition-governed dynamic rules; temporary Mask/scalar adapters remain staged debt. |
| 4 — IR/fusion/resources | Contract/compiler/planner: `src/NodeMath/PointwiseIR.*`. Live lowering/execution: `src/Renderer/Internal/RenderPipelineGraphPointwiseFusion.cpp`; `RenderPipelineGraphExecution.cpp`; `RenderPipelineGraphExecutionHelpers.*`; `RenderPipelineGraphDataMathNode.cpp`. Resources/programs: `RenderPipelineGraphTextureCache.cpp`; `RenderPipelineGraphRenderTargets.cpp`; `RenderPipelineResources.cpp`; `src/Renderer/RenderPipeline.h`; `src/Renderer/GLHelpers.*`. Snapshot/inspection: `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`; `EditorModulePreviewState.cpp`. Tests: `tools/node_math_phase4_tests.cpp`; `src/App/Validation/Suites/NodeMathPhase4Validation.cpp`. | Phase 4 owns the NMR-109 typed ordered IR, safe optimization, conservative single-sampled-input lowering, generated GLSL/cache/limits, source-mapped fallback, target reuse, byte-budgeted graph cache, and execution inspection. Reinspect barriers and formula equivalence before adding another operation. |
| 5 — Compounds | Contract/templates: `src/NodeMath/CompoundDefinition.*`; `src/Editor/NodeGraph/EditorCompoundDefinitions.*`. Graph/lifecycle/persistence: `EditorNodeGraphCompound.cpp`; `EditorNodeGraph.*`; `NodeGraphTypes.h`; `NodeGraphModelTypes.h`; `NodeGraphPayloads.h`; `EditorNodeGraphSerializer.cpp`; `EditorNodeGraphSelectionExport.cpp`; graph mutation/validation/traversal; `UI/EditorNodeGraphUIClipboard.cpp`. Execution/app: `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`; `EditorModuleReferenceSources.cpp`; `EditorModuleGraphMutation.cpp`; `src/Editor/EditorModule.*`. UI/registry: node browser, context menu, node visuals, and `UnifiedNodeDefinitionRegistry.*`. Tests: `tools/node_math_phase5_tests.cpp`; `tools/graph_behavior_tests.cpp`; `src/App/Validation/Suites/NodeMathPhase5Validation.cpp`. | Phase 5 owns exact embedded definitions/instances and closure, graph JSON version 6, stable typed interfaces, selected scalar promotion, explicit lifecycle actions, temporary recursive expansion, optimized-equivalence evidence, and RAW opacity. Reinspect authoring coverage and interface compatibility before broadening supported nodes or control types. |
| 6 — Neighborhood/geometry/reduction | Shared contracts: `src/NodeMath/RegionPlanning.*`; `ReductionMath.*`; `GeometryMath.*`; `SpecializedPlanning.*`; `ContractTypes.*`; `DescriptorSerialization.cpp`; `SemanticSpine.*`. Planning/execution: `src/Renderer/RenderTiling.*`; `src/Renderer/RenderPipeline.h`; `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`; `RenderPipelineGraphPointwiseFusion.cpp`; `RenderPipelineNodePasses.cpp`; `RenderPipelinePrograms.cpp`; `RenderPipelineReadback.cpp`; `src/Editor/EditorRenderWorker.*`; `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`; `EditorModuleRendering.cpp`; `EditorModulePreviewState.cpp`. Graph/definition owners: `src/Editor/NodeGraph/EditorNodeGraph.*`; `EditorNodeGraphDefinitions.cpp`; `EditorNodeGraphSerializer.cpp`; graph mutation/traversal/UI; `UnifiedNodeDefinitionRegistry.cpp`. Tests: `tools/node_math_phase6_tests.cpp`; `tools/graph_behavior_tests.cpp`; `src/App/Validation/Suites/NodeMathPhase6Validation.cpp`. | Phase 6A owns finite regions, ROI/halo, Gaussian/Box support, cancellation-safe publication, and tiled/full equivalence. Phase 6B owns exact Field Mean and scalar caching. Phase 6C owns true Reformat extent/sampling, hard extent mismatch planning, typed specialized stages, and non-mutating consumer boundaries. Phase 6 is complete; inspect these owners before any Phase 7 definition uses their capabilities. |
| 6 — Specialized | Contract/planner: `src/NodeMath/SpecializedPlanning.*`; `src/Renderer/RenderTiling.*`. Existing backends: `src/Raw`; `src/Renderer/Frequency/GpuFft.cpp`; `src/Renderer/Internal/RenderPipelineGraphFrequencyNodes.cpp`; RAW graph-renderer files; denoise/model paths. Consumers: `src/Renderer/Internal/RenderPipelineReadback.cpp`; `src/Renderer/RenderPipeline.h`. | RAW decode/development/external, multi-frame, FFT/inverse FFT, retained-spectrum, scope, preview, and export now declare typed region, scale, cancellation, and failure boundaries. This is planning integration, not RAW decomposition or a claim that every specialized backend is tileable. |
| 7 — Public library | `src/Editor/LayerRegistry.*`; `src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp`; browser/UI files; selected `src/Editor/Layers` and renderer programs; operation reference files | Which nodes are visible, stable, hidden, experimental, misleading, defective, or missing? Which selected definitions have exact formulas and tests? |

## Phase 5B-C Corrective Owners

- Authored output and reference traversal:
  `src/Editor/NodeGraph/Model/EditorNodeGraphTraversal.cpp`,
  `src/Editor/NodeGraph/EditorNodeGraphCompound.cpp`,
  `src/Editor/NodeGraph/EditorNodeGraph.cpp`, and
  `src/Editor/Internal/EditorModuleRendering.cpp`.
- Socket schema and glossary: `NodeGraphTypes.h`, `SocketPresentation.h`, and
  `EditorNodeGraphDefinitions.*` through `Graph::GetSockets`/`FindSocket`.
- Node presentation: `EditorNodeGraphUI.*`,
  `UI/EditorNodeGraphUINodes.cpp`, and
  `UI/EditorNodeGraphUIHitTesting.cpp`. Phase 5B-C removes the on-node
  Connections section and Image/Output cursor-following preview tooltip, and
  routes node sizing through appearance preferences.
- Wire presentation: `GraphConnectionPresentation.h` and
  `UI/EditorNodeGraphUILinksAndGroups.cpp`. These now own tangent-following
  upright text, rotated bounds/hit testing, and target-anchored wire cards.
- Persisted program appearance: `src/App/settings/AppearanceTheme.*` and
  `src/App/AppSettingsPopup.cpp`; version 9 owns connection-text size/sizing/
  outline and node width/UI/grab-height preferences.
- Structural connection mutation and messaging:
  `src/Editor/NodeGraph/Model/EditorNodeGraphConnectionRules.h`,
  `src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp`,
  `src/Editor/Internal/EditorModuleGraphMutation.cpp`, and UI connection/browser
  owners. These reject full-image-to-scalar-field connections without inserting
  an extractor; explicitly authored Luminance Mask or channel paths remain.
  NMR-145 makes the shared connection-rules header the single owner of
  Channel-to-declared-Mask/ScalarField compatibility used by authoring,
  validation, and renderer scheduling. Do not reintroduce separate frequency,
  validation, or render-link exceptions for the same Channel-role bridge.
- Tests and catalog generation: `tools/graph_behavior_tests.cpp`,
  `src/App/Validation/Suites/NodeMathPhase5Validation.cpp`, and
  `src/App/Validation/ValidationCommandRunner.cpp`.
- Maintained outputs: `generated-node-socket-inventory-2026-07-17.md`,
  `../03-technical-contracts/graph-connections/compound-output-and-connection-ui-contract.md`, and
  `review-checklists/graph-connections-visual-review.md`.

## Current Test And Validation Owners

The current working tree builds these validation programs; the node-math
reference program was added by Phase 0 and the contract program by Phase 1
after the dated audit:

- `StackNodeMathContractTests.exe`
- `StackNodeMathPhase2Tests.exe`
- `StackNodeMathPhase3Tests.exe`
- `StackNodeMathPhase4Tests.exe`
- `StackNodeMathPhase5Tests.exe`
- `StackNodeMathPhase6Tests.exe`
- `StackNodeMathReferenceTests.exe`
- `StackGraphBehaviorTests.exe`
- `StackOptimizerSelectionTests.exe`
- `StackPreciseCandidateTests.exe`
- `StackPreciseDryRunTests.exe`
- `StackPreciseIntegrationTests.exe`
- `StackRawEvidenceTests.exe`
- `StackRenderedFeatureTests.exe`

Phases 0 through 6C changed the CTest statement. CMake now registers the CPU
reference and GPU reference tests; Phase 1 through Phase 6 CPU contract tests;
and the Phase 4 pointwise, Phase 5 compound, and Phase 6 region live-GPU tests.
The reference pair covers the selected generated
Identity/Add/Multiply formulas and both authored orders, not general
production-node, color, or alpha behavior. The Phase 1 test covers the isolated
contracts. The Phase 2 test covers descriptor/source metadata, explicit CPU
math, alpha/compositing, semantic analysis, cache fingerprints, and PNG
metadata policy; `StackGraphBehaviorTests.exe` additionally covers live
Technical Image connectivity and graph persistence. The Phase 3 test covers
the typed value envelope and rules; graph behavior additionally covers the
unified live registry, scalar-field sockets, Scalar Value → Exposure binding,
graph-schema-5 round trips, typed Missing recovery, and exact-definition
mismatch failure. The Phase 4 CPU test covers validation, ordered identity,
constant folding, dead-expression removal, ordered CSE, limits, alpha guards,
physical planning, and deterministic LRU selection. Its live test runs the
production renderer in a hidden OpenGL context and compares CPU, ordinary GPU,
and fused GPU output while checking authored order, intermediate
materialization, source-mapped fallback, program reuse, target reuse, and
measured materialization bytes. The Phase 5 CPU test covers definition and
instance identity, exact resolution, persistence, dependency closure,
versioning, Make Unique, optimized evidence, opaque definitions, and recursive
catalog rejection. Graph behavior covers live compound templates, typed
interfaces, save/load/copy/nesting, unresolved shells, lifecycle actions,
temporary expansion, scalar boundaries, group preservation, and shared source
pixels. The Phase 5 live test compares canonical CPU, materialized GPU, and
optimized-equivalent fused GPU execution while checking exact internal order
and the declared tolerance. Phase 5B additionally builds the real authored
two-compound chain, requires nonzero output before Unpack, compares the
authored and unpacked graphs, audits normalized socket families and pin/wire
geometry, and checks appearance version 7-to-8 migration. Phase 5B-C extends
that coverage with wire tangents/rotated bounds, Fixed versus Zoom-Aware sizing,
appearance version 8-to-9 migration, and explicit image-to-mask rejection.
The Phase 6 CPU test covers descriptor-v2 origin/migration, region validation,
pointwise/neighborhood/reduction mappings, scaled Gaussian support, and exact
Field Mean arithmetic/failure cases. Graph behavior covers locality
classification, accumulated planner halo, legal full-frame fallback,
cancellation outcomes, the Field Mean definition/sockets, explicit extraction,
Exposure binding, persistence, and Reduction planning. Phase 6C graph behavior
adds exact Reformat definition/settings persistence, completed-chain traversal,
Sample/Resample extent propagation, explicit mismatch failure, and typed RAW
stage coverage. The Phase 6 live test
compares production full-frame and manually stitched planner-derived tiled
Gaussian output on an odd-sized generated image, then builds the real authored
editor graph for Channel Split -> Field Mean -> Exposure, verifies the exact
mean and persistent scalar-cache reuse, and compares it with a constant-EV
reference. Phase 6C additionally builds a saved/reloaded 4x3-to-7x5 Reformat
plus Exposure graph, compares it with the CPU reference, records typed
scope/preview/export consumers, and executes an authored FFT-to-inverse-FFT
round trip with distinct forward/inverse full-frame plans. Dedicated
`StackNodeMathReductions.Phase6B` and
`StackNodeMathReductions.LiveGpu` CTest names expose that reduction gate while
reusing the complete Phase 6 validation programs.
Regenerate the
maintained catalog with `Stack.exe --write-node-socket-catalog
<absolute-output-path>`. The other validation
executables listed above remain directly runnable and are not implied to be
CTest registrations.

For Windows build verification, repository guidance prefers:

```powershell
.\build.cmd
```

Do not run a large build merely to update documentation. When implementation
starts, select validation proportional to the slice and record it in
`detailed-progress-log.md`.

## Phase 0 Revalidation Checklist — Completed

Before the first code pass, Phase 0 required the following recheck:

- active source selection and excluded/inactive files;
- current graph JSON and project-container versions;
- actual ordinary source texture internal format;
- intermediate/output formats and readback paths;
- viewport sRGB/framebuffer/display state;
- ordinary versus RAW preview precision path;
- export formats, bit depths, and embedded metadata;
- current alpha/blend formulas and forced-alpha nodes;
- cache target count, byte ownership, and pruning;
- `Graph::Validate` call sites and diagnostic behavior;
- current visible registry/catalog counts and known stubs;
- current build/test registration and runtime shader coverage; and
- whether user changes after 2026-07-12 already alter any audit conclusion.

Record relevant architecture changes as an audit delta. Do not edit the dated
audit to make it appear current, and do not treat the delta as a historical
pixel-output compatibility commitment.

Phase 0 completed this checklist in
`../06-completed-work/phase-00-verification/architecture-and-test-baseline-2026-07-15.md`.
