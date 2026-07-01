# Develop Decisions

Last updated: 2026-06-22

Reading note:

Older decision records describe what was intentionally out of scope for that pass. Newer decision records supersede older non-goals when they explicitly implement a previously deferred item.

## 2026-06-22 - Split Renderer Graph Raw Detail Node Owner

Decision:

Move graph-side Raw Detail Auto Mask / Fusion execution out of `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` into `src/Renderer/Internal/RenderPipelineGraphRawDetailNode.cpp`. The new owner handles Raw Detail Auto Mask and Fusion mask-output dispatch, Fusion image-output dispatch, auto-mask-source setting inheritance, debug-preview routing, generated-mask handoff, and Pre-Local Exposure summary publication. Keep traversal, fingerprinting, generic graph-cache store/release, and node-family routing in `RenderPipelineGraphExecution.cpp`; keep low-level Auto Gain, Raw Detail Auto Mask, Raw Detail Fusion, and Pre-Local Exposure shader/pass primitives in `RenderPipelineRawDetailPasses.cpp`.

Rationale:

Raw Detail graph execution changes for Raw Detail Auto Mask / Fusion node dispatch, inherited apply settings, debug-preview routing, generated-mask reuse, and Pre-Local Exposure summary writeback reasons. Graph execution changes for recursive traversal, cache/fingerprint ordering, and node routing reasons. Separating the Raw Detail node body keeps scene-prep/local-exposure graph behavior out of the central recursive evaluator while preserving the existing pass-primitive owner.

Validation:

- Initial `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` after adding the new source regenerated CMake and failed at link because the current MSBuild pass did not compile the regenerated source list.
- Rerun `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed and compiled `RenderPipelineGraphRawDetailNode.cpp`.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No Raw Detail Auto Mask behavior changes, Raw Detail Fusion behavior changes, Pre-Local Exposure behavior changes, graph traversal changes, graph cache behavior changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No implementation of new Scene Prep controls, local-exposure renderer redesign, graph controls, candidate gallery UI, Manual-to-Auto handoff, staged renderer behavior, View Transform changes, or new RAW/HDR/LUT processing behavior.

## 2026-06-22 - Split Renderer Graph DataMath Node Owner

Decision:

Move graph-side DataMath node execution out of `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` into `src/Renderer/Internal/RenderPipelineGraphDataMathNode.cpp`. The new owner handles scalar/image input resolution, multi-input Average accumulation/division, optional mask/base blending, blank base creation, and simple two-input math dispatch. Keep traversal, fingerprinting, generic graph-cache store/release, and node-family routing in `RenderPipelineGraphExecution.cpp`; keep input-list and scalar-vs-image graph analysis in `RenderPipelineGraphAnalysis.cpp`; keep the low-level `RenderDataMath` shader pass in `RenderPipelineNodePasses.cpp`.

Rationale:

DataMath node execution changes for math input resolution, Average accumulation, scalar/image handling, mask/base blending, and target ownership reasons. Graph execution changes for recursive traversal, cache/fingerprint ordering, and node routing reasons. Separating the DataMath node body keeps a full node-family algorithm out of the central recursive evaluator while preserving the graph-analysis and shader-pass boundaries.

Validation:

- Initial `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` after adding the new source regenerated CMake and failed at link because the current MSBuild pass did not compile the regenerated source list; rerun passed and compiled `RenderPipelineGraphDataMathNode.cpp`.
- Final `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` after moving the remaining scalar-output DataMath path into the owner passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No DataMath behavior changes, scalar socket behavior changes, graph traversal changes, graph cache behavior changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No implementation of graph controls, candidate gallery UI, Manual-to-Auto handoff, staged renderer behavior, View Transform changes, or new RAW/HDR/LUT processing behavior.

## 2026-06-22 - Split Renderer Graph Layer Node Owner

Decision:

Move graph-side Layer node execution out of `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` into `src/Renderer/Internal/RenderPipelineGraphLayerNode.cpp`. The new owner handles layer-registry instantiation, layer JSON deserialization, GL layer execution, ToneCurve auto rewrite feedback collection, default ToneCurve blank-output guardrails, and layer mask blending. Keep graph traversal, fingerprinting, generic graph-cache store/release, and node-family routing in `RenderPipelineGraphExecution.cpp`; keep layer-specific UI/model/serialization/rendering in the layer files; keep reusable target/FBO helpers in `RenderPipelineGraphRenderTargets.cpp`.

Rationale:

Layer node execution changes for layer dispatch, layer lifecycle, ToneCurve feedback, default ToneCurve safety, and layer mask behavior reasons. Graph execution changes for recursive traversal, cache/fingerprint ordering, and node routing reasons. Separating the Layer node body keeps the central evaluator from owning layer-registry and layer-runtime details while preserving generic graph-cache behavior.

Validation:

- First `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` regenerated CMake for the new source file and failed at link because the current MSBuild pass did not compile the regenerated source list.
- Rerun `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed and compiled `RenderPipelineGraphLayerNode.cpp`.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No Layer rendering behavior changes, ToneCurve behavior changes, layer mask behavior changes, graph traversal changes, graph cache behavior changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No implementation of new layer types, new ToneCurve controls, Guide 08 tone strategy changes, graph controls, candidate gallery UI, Manual-to-Auto handoff, View Transform changes, or new RAW processing behavior.

## 2026-06-22 - Split Renderer Graph RawDevelop Node Owner

Decision:

Move graph-side RawDevelop pre-finish/final rendering out of `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` into `src/Renderer/Internal/RenderPipelineGraphRawDevelopNode.cpp`. The new owner handles hidden pre-finish stage-cache lookup, scene-prep rendering, integrated ToneCurve finish execution, finish-mask blending, auto rewrite feedback collection, hidden pre-finish stage-cache publication, and RawDevelop black-output guardrails. Keep graph traversal, fingerprinting, generic graph-cache store/release, and node-family routing in `RenderPipelineGraphExecution.cpp`; keep shared RAW source lookup/loading/base rendering in `RenderPipelineGraphRawStages.cpp`; keep RawDevelop stage-cache memory policy in `RenderPipelineRawDevelopStageCache.cpp`; keep Raw Detail pass primitives in `RenderPipelineRawDetailPasses.cpp`.

Rationale:

RawDevelop pre-finish/final rendering changes for Develop scene-prep, integrated finish tone, finish mask, hidden pre-finish, and black-output guardrail reasons. Graph execution changes for recursive traversal, cache/fingerprint ordering, and node routing reasons. Separating the RawDevelop node body removes the most Develop-specific branch from the central recursive evaluator while preserving the existing boundaries around shared RAW base rendering and stage-cache memory policy.

Validation:

- First `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` regenerated CMake for the new source file and failed at link because the current MSBuild pass did not compile the regenerated source list.
- Rerun `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed and compiled `RenderPipelineGraphRawDevelopNode.cpp`.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No RawDevelop rendering behavior changes, RAW Decode behavior changes, scene-prep math changes, integrated ToneCurve behavior changes, finish-mask behavior changes, graph traversal changes, graph cache behavior changes, stage-cache memory-policy changes, guide status changes, or deferred feature status changes.
- No implementation of a full staged renderer, physical RAW-global/scene-prep/finish controller split, sidecar stats bus, graph controls, candidate gallery UI, Manual-to-Auto handoff, View Transform changes, or new RAW processing behavior.

## 2026-06-22 - Split Renderer Graph LUT Node Owner

Decision:

Move graph-LUT node dispatch and reusable graph render-target helpers out of `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` into `src/Renderer/Internal/RenderPipelineGraphLutNode.cpp` and `src/Renderer/Internal/RenderPipelineGraphRenderTargets.cpp`. The LUT node owner handles image/channel input resolution, channel-combine fallback, LUT cache requests, shader uniform binding/draw, optional mask blending, and temporary texture ownership cleanup. The render-target helper owner handles graph target texture creation and render-into-target FBO state restore. Keep graph traversal, fingerprinting, cache adoption, and node-family routing in `RenderPipelineGraphExecution.cpp`; keep LUT texture upload/cache lifecycle in `RenderPipelineLutTextureCache.cpp`; keep shader/program creation in `RenderPipelinePrograms.cpp`.

Rationale:

LUT node behavior changes for input source resolution, channel-combine fallback, LUT application uniforms, mask blending, and temporary texture ownership. Graph execution changes for recursive traversal, cache/fingerprint ordering, and node routing. The generic render-target wrapper is reused by multiple node branches and should not live as inline boilerplate inside traversal. Separating these owners removes a complete node-family operation from the recursive evaluator without changing LUT rendering or cache policy.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No LUT rendering behavior changes, LUT hash/cache behavior changes, shader/program changes, graph traversal changes, graph cache behavior changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No implementation of new LUT controls, color graph controls, color-management redesign, View Transform changes, graph controls, candidate gallery UI, Manual-to-Auto handoff, or new RAW processing behavior.

## 2026-06-22 - Split Renderer Graph RAW Stage Owner

Decision:

Move graph-side RAW source traversal, RAW data loading, and shared RAW base rendering out of `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` into `src/Renderer/Internal/RenderPipelineGraphRawStages.cpp`. The new file owns upstream RAW source lookup through RAW Neural Denoise links, embedded-vs-file RAW data cache hydration, shared RAW base rendering for RAW Decode and Develop, stage-cache hit adoption, raw-base graph-cache publication, and RAW load/render failure logging. Keep graph traversal, fingerprinting, socket evaluation, Develop integrated tone, scene prep, hidden pre-finish handling, and finish-mask dispatch in `RenderPipelineGraphExecution.cpp`.

Rationale:

RAW source/base-stage work changes for RAW file/cache loading, RAW GPU pipeline invocation, and raw-base cache reuse reasons. Graph execution changes for traversal, recursive socket evaluation, render target ordering, and node-family dispatch reasons. Separating the shared RAW base stage removes a substantial RAW-specific operation from the recursive evaluator while preserving the clean boundary: the evaluator chooses when a RawDecode or RawDevelop base is needed, and the raw-stage owner handles how that base is found, loaded, rendered, and cached.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed after expected glob regeneration and rerun.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No RAW Decode behavior changes, RawDevelop rendering changes, RAW load/cache behavior changes, graph traversal changes, graph cache behavior changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No implementation of a full staged renderer, physical RAW-global/scene-prep/finish cache split, sidecar stats bus, graph controls, candidate gallery UI, Manual-to-Auto handoff, View Transform changes, or new RAW processing behavior.

## 2026-06-22 - Split Renderer Graph Analysis Helpers

Decision:

Move graph-structure analysis helpers out of `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` into `src/Renderer/Internal/RenderPipelineGraphAnalysis.cpp`. The new file owns Data Math average-input collection, first Data Math average-input lookup, channel-socket classification, and recursive scalar-vs-image socket classification. Keep `RenderPipelineGraphExecution.cpp` responsible for fingerprinting, recursive image/mask evaluation, cache adoption, and node dispatch.

Rationale:

Scalar socket and Data Math input analysis changes for graph structure and socket semantics reasons. Graph execution changes for render traversal, cache/fingerprint use, GL target creation, and node-family dispatch reasons. Separating graph analysis removes a recursive preamble from the execution body while keeping behavior shared by fingerprinting, evaluation, and HDR representative-source lookup.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed after expected glob regeneration and rerun.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No scalar socket behavior changes, Data Math input behavior changes, graph traversal changes, graph cache behavior changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No implementation of graph controls, candidate gallery UI, Manual-to-Auto handoff, staged renderer behavior, or new RAW/HDR/LUT processing behavior.

## 2026-06-22 - Split Renderer Graph HDR Merge Resolver

Decision:

Move graph-side HDR Merge source/metadata resolution out of `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` into `src/Renderer/Internal/RenderPipelineGraphHdrMerge.cpp`. The new file owns representative source lookup through graph nodes, first Data Math average input source tracing for source selection, HDR input context resolution from RAW metadata and Develop exposure, and metadata/manual exposure reliability resolution. Keep HDR Merge node dispatch in `RenderPipelineGraphExecution.cpp`; keep GL rendering, alignment, feature readback, and deghost setup in `RenderPipelineHdrMergePass.cpp`.

Rationale:

HDR Merge graph metadata resolution changes for source tracing, RAW metadata interpretation, exposure normalization, and reliability defaults. Graph execution changes for traversal, fingerprinting, cache adoption, and node dispatch ordering. Separating the resolver keeps HDR-specific source-walk and exposure policy out of the main recursive graph execution body without moving the render pass.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed after expected glob regeneration and rerun.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No HDR Merge render behavior changes, exposure/reliability math changes, graph traversal changes, graph cache behavior changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No implementation of graph controls, candidate gallery UI, Manual-to-Auto handoff, View Transform changes, color-management redesign, or new RAW/HDR processing behavior.

## 2026-06-22 - Split Renderer Graph Texture Cache Owner

Decision:

Move generic graph image/mask texture cache lifecycle out of `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` and `src/Renderer/Internal/RenderPipelineResources.cpp` into `src/Renderer/Internal/RenderPipelineGraphTextureCache.cpp`. The new file owns cache-entry deletion, full cache destruction, per-key release, store/replace, and inactive-node pruning for `m_GraphImageCache` and `m_GraphMaskCache`. Keep graph traversal/evaluation decisions in `RenderPipelineGraphExecution.cpp`; keep full-cache invalidation orchestration in `RenderPipelineResources.cpp`.

Rationale:

Generic graph cache lifecycle changes for texture ownership, stale node cleanup, and cache entry replacement reasons. Graph execution changes for traversal, fingerprinting, node dispatch, and render ordering reasons, while resource lifecycle changes for source/output/FBO ownership reasons. Separating the generic graph texture-cache owner keeps future cache-lifetime fixes out of the main graph execution body.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed after expected glob regeneration and rerun.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No graph cache behavior changes, texture ownership rule changes, graph traversal changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No implementation of a staged renderer, sidecar stats bus, GPU memory telemetry, graph controls, candidate gallery UI, Manual-to-Auto handoff, View Transform changes, or new RAW processing behavior.

## 2026-06-22 - Split Renderer LUT Texture Cache Owner

Decision:

Move graph-LUT texture cache operations out of `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` into `src/Renderer/Internal/RenderPipelineLutTextureCache.cpp`. The new file owns LUT stage hashing, 1D/3D GL texture upload, per-stage texture replacement, cache-key clearing, texture-entry deletion, and inactive-node LUT cache pruning. Keep LUT shader/program setup in `RenderPipelinePrograms.cpp`; keep LUT node dispatch and uniform binding in `RenderPipelineGraphExecution.cpp`.

Rationale:

LUT texture upload/cache behavior changes for GPU resource, LUT payload, and stale texture lifecycle reasons. Graph execution changes for traversal, fingerprinting, cache adoption, and node-family dispatch reasons. Separating the LUT texture-cache owner makes future LUT upload/cache fixes easier to review without reopening the full graph execution body.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed after expected glob regeneration and rerun.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No LUT rendering behavior changes, LUT hash behavior changes, GL texture upload parameter changes, graph traversal changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No implementation of new LUT controls, color graph controls, color-management redesign, View Transform changes, graph controls, candidate gallery UI, Manual-to-Auto handoff, or new RAW processing behavior.

## 2026-06-21 - Split RawDevelop Stage Cache Owner

Decision:

Move RawDevelop stage snapshot cache operations out of `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` and `src/Renderer/Internal/RenderPipelineResources.cpp` into `src/Renderer/Internal/RenderPipelineRawDevelopStageCache.cpp`. The new file owns texture cloning, fingerprint lookup with MRU promotion, store/replace, entry deletion, byte accounting, per-key and global budget trimming, cache destruction, and validation-facing cache sizing wrappers. Keep the shared raw-stage cache size/budget constants in `src/Renderer/Internal/RenderPipelineGraphExecutionHelpers.*`; keep graph traversal and RawDevelop branch dispatch in `RenderPipelineGraphExecution.cpp`; keep generic source/FBO/output resource lifecycle in `RenderPipelineResources.cpp`.

Rationale:

RawDevelop stage-cache behavior changes for memory policy, texture ownership, eviction, and candidate-feedback reuse reasons. Graph execution changes for traversal, fingerprinting, and node-family dispatch reasons, while resource lifecycle changes for source/output texture ownership reasons. Separating the stage-cache owner makes future cache-policy fixes easier to review without reopening the full graph execution body or generic resource lifecycle file.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed after the expected glob regeneration and one transient tab-icon bake retry.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No RawDevelop render behavior changes, stage-cache sizing threshold changes, cache-hit telemetry changes, candidate-feedback behavior changes, graph traversal changes, source/output resource lifecycle changes, guide status changes, or deferred feature status changes.
- No implementation of the full staged renderer, physical RAW-global/scene-prep/finish cache split, sidecar stats bus, GPU memory telemetry, candidate gallery UI, Manual-to-Auto handoff, graph controls, View Transform changes, or new RAW processing behavior.

## 2026-06-21 - Split Graph Processing Node Helpers

Decision:

Move RAW/HDR/LUT graph processing-node creation and manual RAW full-tree construction from `src/Editor/Internal/EditorModuleGraphMutation.cpp` into `src/Editor/Internal/EditorModuleGraphProcessingNodes.cpp`. The new file owns RAW Neural Denoise, Develop, RAW Decode, Raw Detail Auto Mask/Fusion, HDR Merge, and LUT adders, Develop auto-state trigger hashing/update gates, Raw Detail hybrid conversion, and `AddFullRawTreeToSource`. Keep general graph/layer mutation, layer/channel split, tone-finish helper flows, link/node removal, utility/output adders, and graph metadata refresh in `EditorModuleGraphMutation.cpp`; keep image/RAW import in `EditorModuleGraphImageNodes.cpp`; keep scope/mask creation in `EditorModuleGraphMaskNodes.cpp`.

Rationale:

Processing-node creation changes for RAW pipeline, Develop defaults, HDR/LUT setup, auto-state trigger, and manual RAW-chain reasons. General graph mutation changes for topology, layer order, links, node removal, and metadata reasons. Separating the processing-node owner keeps Develop/RAW defaults and chain builders out of the broad mutation file and makes future RAW/HDR/LUT fixes easier to audit.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 1` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

Non-goals:

- No RAW Neural Denoise behavior changes, Develop node default changes, RAW Decode behavior changes, Raw Detail behavior changes, HDR Merge behavior changes, LUT behavior changes, Develop auto-state behavior changes, manual RAW full-tree behavior changes, graph topology behavior changes, graph serialization changes, guide status changes, or deferred feature status changes.
- No implementation of graph controls, candidate gallery UI, Manual-to-Auto handoff, new RAW processing behavior, HDR/LUT redesign, or any broader graph feature change.

## 2026-06-21 - Split Graph Mask Node Helpers

Decision:

Move graph mask-node creation and Tone Curve scoped-mask graph construction from `src/Editor/Internal/EditorModuleGraphMutation.cpp` into `src/Editor/Internal/EditorModuleGraphMaskNodes.cpp`. The new file owns scope node creation, mask generator/combine/utility node creation, Custom Mask payload initialization, Image-to-Mask node creation, `CreateToneCurveSelectionMask`, and the `ToGraphMaskCombineMode` helper. Keep general graph/layer mutation, tone-finish helper flows, link/node removal, output/composite graph mutation, and graph metadata refresh in `EditorModuleGraphMutation.cpp`; RAW/HDR/LUT processing-node creation and RAW full-tree construction now live in `EditorModuleGraphProcessingNodes.cpp`.

Rationale:

Mask node creation and scoped-mask graph construction change for mask-family and Tone Curve targeting reasons. General graph mutation changes for topology, links, node families, layer order, and graph metadata reasons. Separating the mask owner keeps Tone Curve scoped-mask graph edits and Custom Mask creation defaults out of the broad mutation file.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 1` passed after a parallel asset-bake retry.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

Non-goals:

- No scope/mask node creation behavior changes, Custom Mask payload default changes, Image-to-Mask behavior changes, Tone Curve scoped-mask graph construction changes, Develop finish scoped-mask behavior changes, graph topology behavior changes, graph serialization changes, guide status changes, or deferred feature status changes.
- No implementation of graph controls, candidate gallery UI, Manual-to-Auto handoff, new mask behavior, new scoped-mask behavior, or any broader graph feature change.

## 2026-06-21 - Split Graph Image Node Helpers

Decision:

Move image/RAW graph import helpers from `src/Editor/Internal/EditorModuleGraphMutation.cpp` into `src/Editor/Internal/EditorModuleGraphImageNodes.cpp`. The new file owns image file decode, imported image payload PNG storage bytes, RAW source import, graph-drop image-chain import scheduling/application, active image-source connection, and image-node rotation. Keep general graph/layer mutation, link/node removal, tone-finish helpers, output/composite graph mutation, and graph metadata refresh in `EditorModuleGraphMutation.cpp`; RAW/HDR/LUT processing-node creation and RAW full-tree construction now live in `EditorModuleGraphProcessingNodes.cpp`, and mask/tone-scope graph mutation lives in `EditorModuleGraphMaskNodes.cpp`.

Rationale:

Image/RAW import changes for file dialog, stb decode/encode, RAW runtime, async drop-import, and runtime image payload reasons. General graph mutation changes for topology, links, node families, layer order, and graph metadata reasons. Separating image-node import/mutation removes file IO and import scheduling dependencies from the broad graph mutation file and makes future image import and image-node rotation fixes easier to audit.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

Non-goals:

- No image import behavior changes, RAW source import behavior changes, graph-drop chain import scheduling behavior changes, image payload PNG byte changes, image-node rotation behavior changes, active source connection behavior changes, graph topology behavior changes, Develop behavior changes, graph serialization changes, guide status changes, or deferred feature status changes.
- No implementation of graph controls, candidate gallery UI, Manual-to-Auto handoff, new image import behavior, new RAW loading behavior, or any broader graph feature change.

## 2026-06-21 - Split Tone Curve UI Helpers

Decision:

Move Tone Curve standalone and Develop-integrated ImGui surfaces from `src/Editor/Layers/ToneLayers.cpp` into `src/Editor/Layers/ToneCurveLayerUI.cpp`. The new file owns the expanded manual node surface, curve graph/editor drawing, resettable Tone sliders, prepared/final graph panels, on-image targeting controls, scoped mask controls, and Tone Curve node surface spec. Keep effective tone/foundation math and region-target mutation in `ToneLayers.cpp`; keep point/model/evaluation in `ToneCurveLayerModel.cpp`, auto-analysis/rewrite in `ToneCurveLayerAuto.cpp`, JSON persistence in `ToneCurveLayerSerialization.cpp`, and shader/GL/LUT rendering in `ToneLayerRendering.cpp`.

Rationale:

Tone Curve UI changes for panel layout, controls, targeting, graph editor drawing, and scoped-mask interaction reasons. Effective settings, curve model behavior, auto solving, persistence, and shader execution have different reasons to change and already have focused owners. Separating the UI owner makes future Tone Curve panel and interaction fixes easier to audit without reopening rendering, serialization, or auto-analysis code.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

Non-goals:

- No Tone Curve UI behavior changes, button/slider label changes, graph editor behavior changes, targeting/scoped-mask behavior changes, effective tone/foundation math changes, serialization key/schema/content changes, shader/LUT behavior changes, auto calibration behavior changes, Develop integrated Finish Tone behavior changes, standalone Tone Curve behavior changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of the Guide 08 tone redesign, mode-specific final tone strategy, graph controls, Manual-to-Auto handoff, View Transform changes, candidate gallery UI, user picker/merge controls, denoise redesign, color graph controls, or any broader tone behavior changes.

## 2026-06-21 - Split Tone Curve Serialization Helpers

Decision:

Move Tone Curve JSON helpers and `ToneCurveLayer::Serialize`/`Deserialize` from `src/Editor/Layers/ToneLayers.cpp` into `src/Editor/Layers/ToneCurveLayerSerialization.cpp`. The new file owns point-array JSON, persisted auto-authored-state JSON, Tone Curve schema keys, and load defaults/fallbacks. Keep UI surfaces, effective tone/foundation settings, point/model/evaluation helpers, auto-analysis/rewrite helpers, and shader/GL/LUT rendering outside this serialization file. Tone Curve UI surfaces were later moved to `src/Editor/Layers/ToneCurveLayerUI.cpp`.

Rationale:

Tone Curve persistence changes for schema, defaults, compatibility, and fallback reasons. UI controls, shader execution, auto solving, and curve editing have different reasons to change and now have focused owners. Separating serialization makes future graph compatibility and JSON schema fixes easier to audit without reopening the UI-heavy Tone implementation.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

Non-goals:

- No Tone Curve JSON key/schema/content changes, load default/fallback behavior changes, point serialization behavior changes, persisted auto-authored-state behavior changes, shader/LUT behavior changes, auto calibration behavior changes, Develop integrated Finish Tone behavior changes, standalone Tone Curve behavior changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of the Guide 08 tone redesign, mode-specific final tone strategy, graph controls, Manual-to-Auto handoff, View Transform changes, candidate gallery UI, user picker/merge controls, denoise redesign, color graph controls, or any broader tone behavior changes.

## 2026-06-21 - Split Tone Layer Rendering Helpers

Decision:

Move tone-family shader sources, passthrough rendering, GL initialize/execute methods, render-resource destructors, and Tone Curve LUT upload from `src/Editor/Layers/ToneLayers.cpp` into `src/Editor/Layers/ToneLayerRendering.cpp`. The new file owns render execution for Tone Mapper, Tone Curve, Tone Equalizer, View Transform, and Shadows/Highlights. Keep UI surfaces, effective tone/foundation settings, point/model/evaluation helpers, and auto-analysis/rewrite helpers outside this render file. Tone Curve serialization was later moved to `src/Editor/Layers/ToneCurveLayerSerialization.cpp`; Tone Curve UI surfaces were later moved to `src/Editor/Layers/ToneCurveLayerUI.cpp`.

Rationale:

Tone shader text, GL uniform binding, passthrough fallback, and LUT upload change for rendering reasons. The remaining `ToneLayers.cpp` changes for UI and effective setting reasons, while `ToneCurveLayerModel.cpp`, `ToneCurveLayerAuto.cpp`, and `ToneCurveLayerSerialization.cpp` own the curve model, auto solver, and JSON persistence. Separating the render owner makes future shader and GL fixes easier to audit without reopening the UI-heavy Tone implementation.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

Non-goals:

- No shader source text changes, GL uniform binding behavior changes, passthrough behavior changes, Tone Curve LUT value changes, auto calibration behavior changes, serialization key/schema/content changes, Develop integrated Finish Tone behavior changes, standalone Tone Curve behavior changes, View Transform behavior changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of the Guide 08 tone redesign, mode-specific final tone strategy, graph controls, Manual-to-Auto handoff, View Transform changes, candidate gallery UI, user picker/merge controls, denoise redesign, color graph controls, or any broader tone behavior changes.

## 2026-06-21 - Split Tone Curve Auto Helpers

Decision:

Move Tone Curve auto-analysis and rewrite helpers from `src/Editor/Layers/ToneLayers.cpp` into `src/Editor/Layers/ToneCurveLayerAuto.cpp`. The helper owns auto calibration request bookkeeping, scene readback/statistics, `AutoToneIntent` solving, authored auto-state construction, preservation of user adjustments, and pending rewrite feedback capture/application. Keep effective tone/foundation settings, standalone node UI, Develop integrated Finish Tone UI, and point/model/evaluation helpers outside this file. Shader/LUT upload and GL execution were later moved to `src/Editor/Layers/ToneLayerRendering.cpp`; Tone Curve serialization was later moved to `src/Editor/Layers/ToneCurveLayerSerialization.cpp`; Tone Curve UI surfaces were later moved to `src/Editor/Layers/ToneCurveLayerUI.cpp`.

Rationale:

Tone Curve auto calibration changes for scene-analysis, intent-solver, authored-state preservation, and rewrite feedback reasons. The larger Tone implementation also changes for GL execution, shader/LUT upload, serialization, and UI reasons, while `ToneCurveLayerModel.cpp` changes for curve point/model behavior. Separating the auto owner makes future auto-analysis and rewrite fixes easier to audit without reopening the full layer implementation.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

Non-goals:

- No Tone Curve auto-analysis math changes, auto calibration request behavior changes, authored-state preservation behavior changes, rewrite feedback content changes, shader/LUT behavior changes, serialization key/schema/content changes, Develop integrated Finish Tone behavior changes, standalone Tone Curve behavior changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of the Guide 08 tone redesign, mode-specific final tone strategy, graph controls, Manual-to-Auto handoff, View Transform changes, candidate gallery UI, user picker/merge controls, denoise redesign, color graph controls, or any broader tone behavior changes.

## 2026-06-21 - Split Tone Curve Model Helpers

Decision:

Move Tone Curve point/model/evaluation helpers from `src/Editor/Layers/ToneLayers.cpp` into `src/Editor/Layers/ToneCurveLayerModel.cpp`. The helper owns point reset/presets, active point selection, point sanitization, point add/delete/move, curve-input/scene-value coordinate conversion, viewport probe/target state mutation, and curve evaluation. Keep standalone node UI and Develop integrated Finish Tone UI in `ToneLayers.cpp` at the time of this split; auto scene analysis/rewrite was later moved to `src/Editor/Layers/ToneCurveLayerAuto.cpp`, shader/LUT upload plus GL execution were later moved to `src/Editor/Layers/ToneLayerRendering.cpp`, Tone Curve serialization was later moved to `src/Editor/Layers/ToneCurveLayerSerialization.cpp`, and Tone Curve UI surfaces were later moved to `src/Editor/Layers/ToneCurveLayerUI.cpp`.

Rationale:

Tone Curve point/model changes are likely bug-fix work around editing, targeting, coordinate conversion, and curve response. The larger Tone implementation also changes for GL execution, ImGui surfaces, serialization, and auto-analysis reasons. Separating the point/model/evaluation owner makes future Tone Curve edits easier to review without reopening the full layer implementation.

Validation:

- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

Non-goals:

- No Tone Curve point behavior changes, curve evaluation math changes, coordinate conversion math changes, viewport targeting behavior changes, auto-analysis/rewrite behavior changes, shader/LUT behavior changes, serialization key/schema/content changes, Develop integrated Finish Tone behavior changes, standalone Tone Curve behavior changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of the Guide 08 tone redesign, mode-specific final tone strategy, graph controls, Manual-to-Auto handoff, View Transform changes, candidate gallery UI, user picker/merge controls, denoise redesign, color graph controls, or any broader tone behavior changes.

## 2026-06-21 - Split Develop Rendered Feedback Analysis Helpers

Decision:

Move rendered duplicate and stage-boundary analysis from `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp` into `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackAnalysis.h/.cpp`. The helper owns `DevelopRenderedDuplicateDecision`, `EvaluateDevelopRenderedCandidateDuplicate`, `ClassifyDevelopRenderedStageBoundary`, the rendered duplicate distance threshold, and the pre-finish-distinct threshold. Keep rendered result aggregation, relative-score writeback, rendered rejection memory, pair/ensemble suggestion orchestration, feedback action/stop-reason decision flow, Auto-solve request handoff, and integrated Tone JSON application in `EditorModuleDevelopCandidateFeedback.cpp`.

Rationale:

Rendered duplicate detection and final/pre-finish stage-boundary classification change for analysis-threshold and stage-targeting reasons, while the feedback coordinator changes for result aggregation, persistence, rejection memory, and follow-up decision policy. Separating the analysis helpers makes future duplicate-threshold and stage-label bug-fix passes easier to audit without reopening the broader feedback decision flow.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No rendered metric distance threshold changes, duplicate/pre-finish-distinct behavior changes, stage-boundary label changes, validation wrapper behavior changes, rendered feedback decision policy changes, rendered-feedback JSON key/schema/content changes, candidate generation changes, scoring math changes, convergence threshold changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of richer candidate families, candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, denoise redesign, or broader rendered-feedback behavior changes.

## 2026-06-21 - Split Develop Candidate Score Components Helpers

Decision:

Move authored parameter score-component JSON helpers from `src/Editor/Internal/EditorModuleDevelopCandidateScoring.cpp` into `src/Editor/Internal/EditorModuleDevelopCandidateScoreComponents.h/.cpp`. The helper owns `BuildDevelopAutoCandidateScoreComponents` and `BuildFallbackDevelopAutoCandidateScoreComponents`, including `ParameterScoreComponentsV1`, continuation-bias component records, continuation-expansion component records, signal/dimension/risk JSON, and fallback authored-state component JSON. Keep candidate solve/result structs, candidate id/stage taxonomy, continuation-bias scoring helpers, candidate fingerprints, scalar parameter scoring, nearest-survivor distance, and scalar candidate-damage rejection in `EditorModuleDevelopCandidateScoring.*`.

Rationale:

Scalar scoring changes for selection and candidate-classification reasons, while score-component JSON changes for schema, diagnostics, explanation, and validation reasons. Separating the component builder lets future `ParameterScoreComponentsV1` dimension/risk/signal fixes be reviewed without reopening the candidate taxonomy and scalar score core.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No candidate generation changes, candidate id changes, candidate taxonomy changes, scalar scoring math changes, score-component JSON key/schema/content changes, fallback component behavior changes, continuation-bias policy changes, duplicate selection changes, rendered-feedback policy changes, convergence threshold changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of richer candidate families, candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, denoise redesign, or broader solver behavior changes.

## 2026-06-21 - Split Develop Rendered Feedback Records Helpers

Decision:

Move rendered-feedback record/writeback helpers from `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp` into `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackRecords.h/.cpp`. The helper owns `DevelopCandidateRenderMetricsToJson`, `AppendDevelopCandidateRenderedFeedbackHistory`, `WriteDevelopCandidateRenderedFeedbackLoopRecord`, and the JSON hash used to detect persisted feedback changes. Keep rendered result aggregation, duplicate/stage-boundary analysis, feedback action/stop-reason decision flow, rendered rejection memory, pair/ensemble suggestion orchestration, Auto-solve request handoff, and integrated Tone JSON application in `EditorModuleDevelopCandidateFeedback.cpp` for this split; duplicate/stage-boundary analysis was later moved to `EditorModuleDevelopRenderedFeedbackAnalysis.*`.

Rationale:

Rendered feedback records change for schema and diagnostic-writeback reasons, while candidate feedback aggregation changes for selection, duplicate/stage-boundary, rejection-memory, and feedback-policy reasons. Separating record serialization makes future `RenderMetricsV1`, bounded history, and `RenderedFeedbackLoopV1` schema fixes easier to audit without reopening the main feedback decision flow. The later analysis split gives duplicate/stage-boundary thresholds their own owner too.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No rendered metric changes, rendered metric JSON key/schema changes, feedback-history content or bound changes, `RenderedFeedbackLoopV1` content changes, duplicate/stage-boundary behavior changes, rendered-feedback decision policy changes, candidate generation changes, scoring math changes, convergence threshold changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of richer candidate families, candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, denoise redesign, or broader rendered-feedback behavior changes.

## 2026-06-21 - Split Develop Candidate Render Payload Helpers

Decision:

Move copied candidate render payload mutation from `src/Editor/Internal/EditorModuleDevelopCandidateRequests.cpp` into `src/Editor/Internal/EditorModuleDevelopCandidateRenderPayload.h/.cpp`. The helper owns current authored guidance readback, RAW cleanup/detail payload deltas, Scene Prep/local-exposure payload deltas, stage-constraint freezing, white-balance probe application, and per-candidate Tone JSON diagnostics. Keep budget/admission, quiet-window gating, option ranking/diversity/reservation, stage scheduler ordering, and request construction in `EditorModuleDevelopCandidateRequests.cpp`.

Rationale:

Request scheduling and payload mutation have different reasons to change. Scheduling changes for budget, queueing, diversity, stage-order, and fairness policy; payload mutation changes for stage-freeze math, candidate probe field mapping, cleanup/detail deltas, white-balance probes, and diagnostic mirrors. Separating those owners makes future Guide 03 bug-fix passes easier to review without reopening the request scheduler for every payload-field adjustment.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No candidate render selection changes, request budget changes, quiet-window timing changes, candidate id changes, payload math changes, stage-constraint behavior changes, white-balance probe behavior changes, JSON key/schema changes, rendered-feedback policy changes, convergence threshold changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of richer candidate families, candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, denoise redesign, or broader rendered-feedback behavior changes.

## 2026-06-21 - Split Develop Rendered Candidate Scoring Helpers

Decision:

Move rendered candidate score/ranking helpers from `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp` into `src/Editor/Internal/EditorModuleDevelopRenderedCandidateScoring.h/.cpp`. The new helper owns `ScoreDevelopRenderedCandidateMetrics`, `DevelopRenderedRelativeComparison`, `CompareDevelopRenderedCandidateToSelected`, `ClassifyDevelopRenderedCandidateDamage`, and `ResolveDevelopRenderedRefineIntent`. Keep feedback result aggregation, duplicate/stage-boundary analysis, rendered feedback history, feedback-loop JSON, and integrated Tone JSON application in `EditorModuleDevelopCandidateFeedback.cpp` for this split; duplicate/stage-boundary analysis was later moved to `EditorModuleDevelopRenderedFeedbackAnalysis.*`.

Rationale:

Rendered scoring and classifier thresholds change for ranking/refine-policy reasons, while candidate feedback writeback changes for history, diagnostics, loop-state, and JSON schema reasons. Separating the score/classifier owner makes future threshold and ranking bug-fix passes easier to review without reopening the feedback history/application file. The later analysis split keeps duplicate thresholds and boundary labels out of the coordinator as well.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No rendered metric scoring math changes, relative-comparison math changes, damage classifier threshold changes, refine-intent policy changes, duplicate/stage-boundary behavior changes, feedback-history behavior changes, rendered-feedback JSON key/schema changes, convergence threshold changes, candidate generation changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of a candidate gallery, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, spatial/perceptual damage maps, or broader rendered-feedback behavior changes.

## 2026-06-21 - Split Develop Selected Solve Stage Owners

Decision:

Split the selected Auto solve application helper into narrower owners. Keep `src/Editor/Internal/EditorModuleDevelopAutoSolveApplication.h/.cpp` as the selected-solve orchestration and mode/intent guidance owner; move shared selected-solve scalar derivation into `src/Editor/Internal/EditorModuleDevelopAutoSolveApplicationContext.h/.cpp`; move authored RAW setting application into `src/Editor/Internal/EditorModuleDevelopAutoSolveRawApplication.cpp`; move authored Scene Prep/local-exposure application into `src/Editor/Internal/EditorModuleDevelopAutoSolveScenePrepApplication.cpp`; and move selected-solve requested/achieved Tone JSON writeback, subject request mirrors, stage diagnostics, and calibration queueing into `src/Editor/Internal/EditorModuleDevelopAutoSolveToneApplication.cpp`.

Rationale:

Selected-solve application has three different reasons to change: RAW/global placement and cleanup behavior, Scene Prep/local-exposure behavior, and integrated Tone/stage diagnostic writeback. Keeping those formulas in separate owners makes future RAW, Scene Prep, and diagnostic bug-fix passes easier to audit without reopening candidate generation, scoring, rendered feedback, or the public Auto solve entry point.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No Auto solve behavior changes, Auto intent/mode behavior changes, RAW setting math changes, Scene Prep math changes, integrated Tone JSON key/schema changes, candidate generation changes, scoring math changes, rendered-feedback policy changes, convergence threshold changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of richer candidate families, candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, RAW solver redesign, Scene Prep redesign, Tone redesign, or broader rendered-feedback behavior changes.
- No new public API for stage owners; these remain internal helpers behind `ApplyDevelopSelectedAutoSolve`.

## 2026-06-21 - Split Develop Selected Solve Application Helpers

Decision:

Move Auto intent-profile shaping, mode-aware guidance construction, selected authored guidance application, requested/achieved Tone JSON writeback, RAW settings application, Scene Prep/local-exposure application, calibration queueing, and staged solve diagnostic invocation from `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp` into `src/Editor/Internal/EditorModuleDevelopAutoSolveApplication.h/.cpp`. Keep `EditorModule::ApplyDevelopAutoSolve` in `EditorModuleDevelopAutoSolve.cpp` as the thin public coordinator that normalizes requested guidance, ensures integrated tone exists, reads current stats, builds the candidate solve, and delegates selected-solve application.

Rationale:

Candidate generation changes for authored-search reasons, while selected-solve application changes for authored RAW placement, hidden cleanup/denoise settings, Scene Prep/local-exposure settings, integrated Tone writeback, calibration, and staged diagnostics. Separating the selected application owner makes RAW/Scene Prep/Tone bug-fix passes easier to audit without reopening candidate-family generation or the public Auto solve entry point.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No Auto solve behavior changes, Auto intent/mode behavior changes, RAW setting math changes, Scene Prep math changes, integrated Tone JSON key/schema changes, candidate generation changes, scoring math changes, rendered-feedback policy changes, convergence threshold changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of richer candidate families, candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, RAW solver redesign, Scene Prep redesign, or broader rendered-feedback behavior changes.
- No split of RAW application versus Scene Prep/local-exposure application inside `EditorModuleDevelopAutoSolveApplication.*` in this step.

## 2026-06-21 - Split Develop Candidate Generation Helpers

Decision:

Move `BuildDevelopAutoCandidateSolve` and its private authored-candidate helper set from `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp` into `src/Editor/Internal/EditorModuleDevelopCandidateGeneration.h/.cpp`. The new file owns remembered same-context/rendered rejection lookup, rendered dynamic-range evidence source selection, continuation expansion, RAW white-balance probes, cleanup/detail probes, mode-neighbor probes, finish-tone probes, exposure-placement probes, rendered-local families, survivor carry-forward, initial parameter-space merge construction, and the preliminary rendered-feedback application call. Keep public `EditorModule::ApplyDevelopAutoSolve`, mode-aware guidance shaping, selected guidance application to RAW/Scene Prep/Tone settings, and calibration queueing in the Auto solver coordinator.

Rationale:

Candidate family generation changes for Guide 03 authored-search reasons, while applying the selected solve to RAW, Scene Prep, and integrated Tone changes for authored settings/output reasons. Giving candidate generation its own owner makes future probe-family, continuation, and carry-forward fixes easier to review without editing the public Auto solve coordinator or the rendered-feedback application file.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after one transient font-bake retry.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No candidate family behavior changes, scoring math changes, rendered-feedback policy changes, convergence threshold changes, JSON key/schema changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of richer candidate families, candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, or broader rendered-feedback behavior changes.
- No split of selected RAW/Scene Prep/Tone application in this step.

## 2026-06-21 - Split Develop Rendered Feedback Application

Decision:

Move authored rendered-feedback application from `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp` into `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackApplication.h/.cpp`, and move the shared guidance adjustment/blending plus selected white-balance probe bookkeeping helpers into `src/Editor/Internal/EditorModuleDevelopCandidateGuidance.h/.cpp`. The rendered-feedback application file now owns the solve mutation after rendered metrics are available: direct winner adoption, rendered-local refinement, selected-vs-best merge, pair merge, ensemble merge, admission bookkeeping, and responsible revision-stage/reason assignment. Keep candidate family generation, remembered rejection lookup, survivor carry-forward, initial authored merge construction, and authored RAW/Scene Prep/Tone application in the Auto solver coordinator.

Rationale:

Rendered-feedback application changes for follow-up solve mutation reasons, while candidate generation changes for authored search/scoring reasons and rendered candidate feedback changes for metric scoring/history reasons. Giving rendered-feedback application a focused owner makes adoption/refine/merge bug fixes easier to audit without editing the candidate-family generator or the diagnostics writer. The shared guidance helper file prevents both owners from depending on private utilities inside the coordinator.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No rendered-feedback policy changes, convergence threshold changes, candidate generation changes, candidate scoring math changes, rendered metric scoring/classification changes, JSON key/schema changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, richer candidate families, sidecar stats bus, or broader rendered-feedback behavior changes.
- No split of candidate family generation in this step.

## 2026-06-21 - Split Develop Subject Scene Intent Resolver

Decision:

Move `ResolveDevelopSubjectSceneIntent` from `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp` into `src/Editor/Internal/EditorModuleDevelopSubjectImportance.h/.cpp`. The subject-importance file now owns subject/scene intent resolution together with subject-map interpretation, refined-map application, solve-note JSON, subject-importance normalization, and selected-node subject viewport/brush bridge methods. Keep candidate family generation, authored rendered-feedback adoption/refinement/merge mutation, and authored RAW/Scene Prep/Tone application in the Auto solver coordinator.

Rationale:

Subject/scene intent resolution changes for Guide 05 reasons: user marks, interpreted/refined subject maps, readability/protection/mood pressure, and solve-note diagnostics. Keeping it in the same owner as subject-map interpretation makes future subject-bias fixes easier to review and leaves `EditorModuleDevelopAutoSolve.cpp` focused on coordinating candidate families and authored solver state.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No subject/scene intent behavior changes, subject-importance behavior changes, brush behavior changes, viewport overlay behavior changes, Auto solve policy changes, candidate scoring math changes, JSON schema changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of true image-edge refinement, edge visualization, semantic/AI detection, graph-control UI, candidate gallery UI, Manual/Auto subject-bias handoff, richer candidate families, or broader rendered-feedback behavior changes.
- No split of candidate family generation or authored rendered-feedback mutation in this step.

## 2026-06-21 - Split Develop Auto Solve Diagnostics Helpers

Decision:

Move broad Develop Auto solve diagnostic writeback from `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp` into `src/Editor/Internal/EditorModuleDevelopAutoSolveDiagnostics.h/.cpp`. The new file owns candidate status serialization, rejected-candidate memory writeback, `ParameterCandidatesV1` / `ParameterScoreComponentsV1` diagnostic aliases, `CandidateOutcomeLearningV1`, dynamic-range and subject top-level aliases, rendered-feedback loop/continuation/convergence aliases, and `StagedAutoSolveV1` logical stage diagnostics. Keep candidate family generation, authored rendered-feedback adoption/refinement/merge mutation, subject/scene intent resolution, and authored RAW/Scene Prep/Tone application in the Auto solver coordinator.

Rationale:

The main Auto solver file was still mixing stateful authored solve construction with a large block of schema-shaped JSON writeback. Diagnostic writeback changes for reporting/schema reasons, while candidate generation and authored mutation change for solver behavior reasons. Giving the diagnostic writer a focused owner reduces review risk for future status/telemetry/schema fixes and leaves `EditorModuleDevelopAutoSolve.cpp` closer to being a coordinator for authored settings.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No candidate generation changes, candidate scoring math changes, rendered-feedback policy changes, convergence threshold changes, JSON key/schema changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, richer candidate families, sidecar stats bus, or broader rendered-feedback behavior changes.
- No split of candidate family generation, authored rendered-feedback mutation, or subject/scene intent resolution in this step.

## 2026-06-21 - Split Develop Rendered Feedback Convergence Helpers

Decision:

Move rendered-feedback convergence/readback helpers from `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp` into `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackConvergence.h/.cpp`, and make `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp` reuse the same helper for loop constants, continuation-policy JSON, reverse-adoption checks, and stop-reason convergence classification. The new file owns rendered-feedback loop version constants, `RenderedContinuationV1` policy builders, `ConvergenceAdmissionV1` policy calculation, `ConvergenceEvidenceV1` JSON construction, rendered metric JSON readback, last-history metric lookup, trend convergence checks, same-intent monotonic refine guards, repeated adoption/refinement stop checks, and the shared converged stop-reason classifier. Keep candidate family generation, authored rendered-feedback adoption/refinement/merge mutation, pair/ensemble merge construction, subject/scene intent resolution, and broad solve diagnostic writeback in the Auto solver coordinator.

Rationale:

The main Auto solver file was still mixing authored candidate mutation with rendered-feedback readback, history trend analysis, convergence admission, continuation-policy records, and stop classification. Candidate feedback also had a private copy of loop constants and continuation-policy record construction. Giving convergence/readback a focused owner makes future anti-oscillation and rendered-metric bug fixes easier to review while keeping actual candidate mutation in the coordinator that owns authored solve state.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No rendered-feedback policy changes, candidate generation changes, candidate scoring math changes, convergence threshold changes, JSON schema changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, richer candidate families, or broader rendered-feedback behavior changes.
- No split of candidate family generation, authored rendered-feedback mutation, subject/scene intent resolution, or broad diagnostic writeback in this step.

## 2026-06-20 - Split Develop Candidate Scoring Helpers

Decision:

Move Develop candidate scoring and taxonomy helpers from `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp` into `src/Editor/Internal/EditorModuleDevelopCandidateScoring.h/.cpp`. The new file owns candidate solve/result structs, white-balance probe id helpers, candidate id/revision-stage classification, rendered refine preferred-candidate helpers, continuation-bias profile/bonus application, candidate context/guidance/final fingerprints, scalar parameter scoring, `ParameterScoreComponentsV1` JSON, fallback score components, nearest-survivor distance, and scalar parameter-damage rejection. Keep candidate family generation, remembered/rendered rejection reads, prior survivor carry-forward, authored merge construction, rendered-feedback convergence, subject/scene intent resolution, and broad solve diagnostic writeback in the main Auto solver coordinator.

Rationale:

The main Auto solver file was still mixing candidate family construction, scoring formulas, candidate-stage taxonomy, continuation-bias bookkeeping, rendered-feedback convergence, subject intent, and diagnostic writeback. Candidate scoring math and candidate-family classification change for ranking/taxonomy reasons, while generation and rendered feedback change for orchestration reasons. Giving scoring/taxonomy a focused file makes future candidate ranking fixes easier to review without disturbing the generation or convergence loop.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No candidate generation changes, candidate scoring math changes, continuation-bias policy changes, rendered-feedback policy changes, JSON schema changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, or broader rendered-feedback behavior changes.
- No split of rendered-feedback convergence or candidate family generation in this step.

## 2026-06-20 - Split Develop Dynamic Range Strategy Helpers

Decision:

Move Guide 04 dynamic-range/local-exposure strategy code from `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp` into `src/Editor/Internal/EditorModuleDevelopDynamicRangeStrategy.h/.cpp`. The new file owns `DevelopToneAutoStats`, compact rendered metrics-to-`DynamicRangeRegionEvidenceV1` mapping, `DynamicRangeRegionEvidenceV1` JSON, `DynamicRangeStrategyV1`, `DynamicRangeStrategyMapV1`, `LocalExposureStrategyV1`, and related JSON helpers. Keep rendered-history candidate selection, candidate generation/scoring, rendered-feedback convergence, subject/scene intent resolution, and broad solve diagnostics in the main Auto solver coordinator.

Rationale:

The main Auto solver file was still mixing strategy formulas, candidate search, rendered-feedback convergence, subject intent, and diagnostic writeback. Guide 04 dynamic-range and local-exposure formulas change for image-strategy reasons, while candidate solving and rendered feedback change for orchestration reasons. Giving the Guide 04 strategy records their own file makes future range/local-exposure bug fixes easier to review without disturbing candidate convergence logic.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No dynamic-range behavior changes, local-exposure behavior changes, candidate generation changes, candidate scoring math changes, rendered-feedback policy changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of true spatial maps, visual overlays, graph controls, subject-aware range priority, clipped-data reconstruction, deeper Scene Prep/local exposure renderer redesign, denoise redesign, or Guide 08 tone redesign.
- No split of candidate generation/scoring or rendered-feedback convergence in this step.

## 2026-06-20 - Split Develop Subject Importance Solver Helpers

Decision:

Move Guide 05 subject-importance map interpretation and viewport handoff code from `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp` into `src/Editor/Internal/EditorModuleDevelopSubjectImportance.h/.cpp`. The new file owns the compact interpreted/refined map helper types, `SubjectImportanceMapV1`, `SubjectRefinedMapV1`, `SubjectImportanceSolveNotesV1`, subject-importance summary/normalization helpers, `DevelopSubjectSceneIntentToJson`, and the selected-node subject viewport/brush bridge methods.

Rationale:

The main Auto solver file was mixing broad solve orchestration, dynamic-range strategy, candidate scoring, rendered-feedback convergence, subject-map interpretation, and viewport mutation bridge code. Subject importance changes follow Guide 05 map/brush/diagnostic behavior, while the broader solve coordinator changes for Auto staging and candidate selection. Separating this owner makes future subject-map fixes easier to reason about and reduces the chance that a brush or diagnostic change disturbs candidate convergence logic.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No subject-importance behavior changes, brush behavior changes, viewport overlay behavior changes, Auto solve policy changes, candidate scoring math changes, graph serialization changes, Develop guide status changes, or deferred feature status changes.
- No implementation of true image-edge refinement, edge visualization, semantic/AI subject detection, graph-control UI, candidate gallery UI, or Manual/Auto handoff.
- No split of dynamic-range/local-exposure strategy, candidate generation/scoring, or rendered-feedback convergence in this step.

## 2026-06-20 - Split Renderer Custom Mask Pass

Decision:

Move Custom Mask CPU rasterization from `src/Renderer/Internal/RenderPipelineNodePasses.cpp` into `src/Renderer/Internal/RenderPipelineCustomMaskPass.cpp`. The new file owns `RenderPipeline::GenerateCustomMaskTexture`, payload flattening, raster/object combination, rectangle/ellipse/polygon/freeform evaluation, blur/morph post-processing, and single-channel texture upload. Keep generated mask texture rendering, mask blend/combine, mix, data math, utility/image-to-mask, and channel split/combine pass implementations in `RenderPipelineNodePasses.cpp`.

Rationale:

Custom Mask has a CPU raster/object geometry path with different dependencies and change pressure than the remaining GPU node-pass helpers. Keeping it in its own renderer-internal pass file makes future custom-mask bug fixes easier to review without touching unrelated mask generator, mix, data math, utility, or channel code.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No Custom Mask behavior changes, generated mask behavior changes, render math changes, shader source changes, graph execution changes, cache behavior changes, serialization changes, Develop guide status changes, or deferred feature status changes.
- No split of the remaining generated mask, mix, data math, utility/image-to-mask, or channel pass families in this step.

## 2026-06-20 - Split Node Graph Canvas Orchestration

Decision:

Move the graph canvas render entry and orchestration from `src/Editor/NodeGraph/EditorNodeGraphUI.cpp` into `src/Editor/NodeGraph/UI/EditorNodeGraphUICanvas.cpp`. The new file owns `EditorNodeGraphUI::Render`, `RenderGraphCanvas`, `FitGraphPreviewToCanvas`, `RenderStaticGraphPreview`, `RenderGraphZoomDial`, and the canvas-only node-drop insertion helpers. Keep `EditorNodeGraphUI.cpp` focused on layout-cache construction, socket-anchor lookup, render ordering, front-order stamps, and node footprint sizing.

Rationale:

After the node-body and link/group drawing splits, the main graph UI file still owned the canvas render entry, draw ordering, static-preview path, zoom dial, drag/drop target routing, and layout cache. Canvas orchestration changes for draw-order and view-transform reasons, while layout-cache construction changes for node geometry reasons. Splitting canvas orchestration leaves the main file as a small layout/render-order implementation and gives future canvas or static-preview fixes a direct owner.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No graph UI behavior changes, canvas draw-order changes, drag/drop behavior changes, graph view transform changes, static preview behavior changes, zoom dial changes, node layout behavior changes, serialization changes, Develop guide status changes, or deferred feature status changes.
- No split of the remaining layout-cache construction, socket-anchor placement, render-order, or node-footprint sizing code in this step.

## 2026-06-20 - Split Node Graph Node Body Rendering

Decision:

Move the `EditorNodeGraphUI::RenderNode` implementation from `src/Editor/NodeGraph/EditorNodeGraphUI.cpp` into `src/Editor/NodeGraph/UI/EditorNodeGraphUINodes.cpp`. Keep the existing member signature, keep layout-cache construction and canvas orchestration in the main graph UI file at the time of this split, and keep node presentation primitives in `EditorNodeGraphUIVisuals.*`. A later canvas orchestration split moved the render entry/canvas/static-preview/zoom code into `EditorNodeGraphUICanvas.cpp`.

Rationale:

`RenderNode` was the largest remaining body in the main graph UI file and mixed frameless media nodes, compact square nodes, expanded inline controls, socket label drawing, preview sizing, per-node measurement, and content hover/active capture. Those concerns change when node surfaces and inline controls change, while canvas ordering and interaction handling change for different reasons. Splitting node rendering gives future node UI fixes a clear file without touching the interaction state machine in `EditorNodeGraphUIInteraction.cpp`.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.

Non-goals:

- No graph UI behavior changes, node layout changes, inline-control changes, socket drawing semantic changes, canvas ordering changes, interaction changes, serialization changes, Develop guide status changes, or deferred feature status changes.
- No split of layout-cache construction in this step. The later canvas orchestration decision above moved `RenderGraphCanvas`, static preview rendering, and zoom dial drawing into `EditorNodeGraphUICanvas.cpp`.

## 2026-06-20 - Fix Composite Text Aspect Handling and Add Text Backdrop Blur Controls

Decision:

Keep composite text generators in the scalable-raster path, but stop treating them as square scene items. Text generator composites should size their scene quad from the actual uploaded/requested text raster aspect unless the source explicitly keeps a full square raster frame. Add text-generator backdrop controls as generator settings (`textBackdropBlur`, `textBackdropOpacity`, `textBackdropPadding`) and render that backdrop directly in the generated text texture using `colorB` as the tint. For Mix nodes, prefer a non-generated image/RAW/custom reference canvas over a generated text/scalable reference when both inputs are connected.

Rationale:

Composite-mode text was being rendered through the same scalable-generator scene sizing path used by square/circle generators. That path assumes a 256x256 square scene base, which stretches cropped text rasters and can make narrow text effectively disappear at some placements/scales. A second issue showed up in image-plus-text composites: Mix reference-canvas resolution used input A first, so a text generator on input A could force preview/export to render on the text generator's scalable canvas instead of the underlying image canvas. Separating scalable raster sizing from square scene sizing and preferring real image/RAW/custom canvases for Mix fixes the aspect and export mismatch without regressing square/circle generator behavior. Rendering the blur as part of the generated text texture makes the new control visible consistently in the composite canvas and export path without first building a full composite-stage backdrop-blur system.

Validation:

- `cmake --build build_codex_verify --config Debug --target Stack --parallel 6` passed after the initial text/backdrop pass and again after the Mix reference-canvas correction.

Non-goals:

- No true live backdrop blur of already-composited lower layers.
- No composite-stage postprocess stack, new node kind, or render-worker redesign.
- No Develop guide status changes or deferred-scope ownership changes.

## 2026-06-20 - Split Node Graph Link and Group Drawing

Decision:

Move node graph link drawing, pending connection drag visuals, and group rectangle/header/rename drawing from `src/Editor/NodeGraph/EditorNodeGraphUI.cpp` into `src/Editor/NodeGraph/UI/EditorNodeGraphUILinksAndGroups.cpp`. Keep the existing `EditorNodeGraphUI` method signatures and, at the time of this split, leave canvas orchestration and node body drawing in the main graph UI file. A later node-rendering split moved `RenderNode` into `EditorNodeGraphUINodes.cpp`; `RenderInteraction` lives in `EditorNodeGraphUIInteraction.cpp`.

Rationale:

Links and groups change for visual graph-readability reasons, while node body composition and interaction handling change for different reasons. Moving `RenderLinks`, `RenderPendingOutputLinkDrag`, `RenderPendingInputLinkDrag`, and `RenderGroups` into a focused implementation file makes future graph drawing fixes easier to review without mixing them into drag/drop, selection, or canvas orchestration work.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

Non-goals:

- No graph UI behavior changes, link visual semantic changes, group interaction changes, pending-link drag behavior changes, canvas ordering changes, serialization changes, Develop guide status changes, or deferred feature status changes.
- No split of canvas orchestration in this step. The node body renderer was split in the later node-rendering decision above, and canvas orchestration was split in the later canvas decision above.

## 2026-06-20 - Split Develop Scene Prep, Finish Tone, and Payload Comparison

Decision:

Move the remaining large Manual-mode Develop panel sections and pure payload comparison helpers out of `src/Editor/Internal/EditorModuleDevelopControls.cpp`. `EditorModuleDevelopScenePrepControls.h/.cpp` owns the Develop-specific Scene Prep bridge over shared Pre-Local Exposure controls. `EditorModuleDevelopFinishToneControls.h/.cpp` owns integrated Finish Tone UI rendering, transient ToneCurve state restore/store, upstream-change notification, scoped-mask bridge, and integrated Tone JSON writeback. `EditorModuleDevelopPayloadComparison.h/.cpp` owns exact Develop payload equality checks. Keep `EditorModule::RenderRawDevelopControls` as the public coordinator for defaults, mode flow, helper ordering, interaction recording, and render dirty marking.

Rationale:

Scene Prep, Finish Tone, and payload equality change for different reasons. Scene Prep changes when local-exposure controls or summary display changes. Finish Tone changes when integrated ToneCurve UI/state handoff changes. Payload equality changes when serialized Develop state changes. Splitting these boundaries keeps future fixes reviewable and prevents the public Develop controls entry point from growing back into a mixed UI/comparison/state file.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.

Non-goals:

- No Scene Prep behavior changes, integrated Finish Tone behavior changes, ToneCurve transient-state behavior changes, payload dirty detection semantic changes, Auto/Manual mode changes, graph schema changes, render math changes, Develop identity changes, guide status changes, or deferred feature status changes.
- No Scene Prep algorithm redesign, integrated tone strategy redesign, graph controls, candidate gallery UI, Manual handoff, or subject-edge refinement work.

## 2026-06-20 - Split Develop Auto Guidance Controls

Decision:

Move Develop Auto guidance controls from `src/Editor/Internal/EditorModuleDevelopControls.cpp` into `src/Editor/Internal/EditorModuleDevelopAutoGuidanceControls.h/.cpp`. The new helper owns Auto intent selection, Auto Calibrate / Reset Auto buttons, guidance sliders, Subject / Scene Intent and Mood / Readability sliders, right-click slider reset handling, draft edit/commit policy, Subject Importance control delegation, and changed/reanalysis/interaction reporting. Keep `EditorModule::RenderRawDevelopControls`, Auto/Manual flow, Auto solve update, compact Auto status readouts, Manual RAW controls, Scene Prep bridge, and integrated Finish Tone bridge in `EditorModuleDevelopControls.cpp`.

Rationale:

Auto guidance controls change when the user-facing Auto steering surface, draft timing, or interaction quiet-window reporting changes. Auto status readouts, Manual RAW controls, Scene Prep controls, and integrated Finish Tone controls change for different reasons. Splitting guidance UI makes future Auto steering fixes easier to review without scanning the whole Develop panel.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.

Non-goals:

- No Auto guidance behavior changes, Auto/Manual mode changes, Subject Importance behavior changes, draft commit timing changes, Auto reanalysis policy changes, interaction quiet-window changes, graph schema changes, solver behavior changes, render math changes, Develop identity changes, guide status changes, or deferred feature status changes.
- No graph controls, candidate gallery UI, Manual handoff, subject-edge refinement, or Auto solve algorithm work.

## 2026-06-20 - Split Develop Manual RAW Controls

Decision:

Move Develop Manual RAW controls from `src/Editor/Internal/EditorModuleDevelopControls.cpp` into `src/Editor/Internal/EditorModuleDevelopManualRawControls.h/.cpp`. The new helper owns Manual RAW color/exposure controls, the RAW exposure draft UI and interaction reporting, highlight reconstruction, demosaic status, input-level overrides, orientation, cleanup, mosaic denoise, camera transform, and debug-view controls. Keep `EditorModule::RenderRawDevelopControls`, Auto/Manual flow, Auto guidance sliders, Scene Prep bridge, and integrated Finish Tone bridge in `EditorModuleDevelopControls.cpp`.

Rationale:

Manual RAW controls change when RAW decode/editing controls, metadata readouts, exposure-draft behavior, cleanup, denoise, camera transform, or debug views change. Scene Prep and integrated Finish Tone change for different reasons and remain in the coordinator for now so their ordering and payload comparison stay visible. Splitting RAW controls makes future Manual RAW fixes easier to review without scanning Auto guidance, Scene Prep, and Tone Curve code.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.

Non-goals:

- No Manual RAW behavior changes, RAW exposure draft timing changes, Auto/Manual mode changes, Scene Prep behavior changes, integrated Finish Tone behavior changes, graph schema changes, render math changes, Develop identity changes, guide status changes, or deferred feature status changes.
- No Manual-to-Auto bias preservation, locks, expert handoff controls, RAW exposure solver redesign, demosaic/denoise redesign, or color-management changes.

## 2026-06-20 - Split Develop Auto Status Controls

Decision:

Move compact Develop Auto status/readout UI from `src/Editor/Internal/EditorModuleDevelopControls.cpp` into `src/Editor/Internal/EditorModuleDevelopAutoStatusControls.h/.cpp`. The new helper renders selected-candidate status, rendered-feedback metrics, candidate timing/readback diagnostics, current solve placement, dynamic-range/local-exposure strategy readouts, subject-map/solve-note summaries, and candidate-feedback quiet-window state. Keep `EditorModule::RenderRawDevelopControls`, Auto/Manual flow, Auto guidance sliders, Manual RAW controls, Scene Prep bridge, and integrated Finish Tone bridge in `EditorModuleDevelopControls.cpp`.

Rationale:

Auto status text changes when diagnostics, solver explanations, rendered-feedback records, or timing/readback telemetry change. Auto guidance sliders and Manual RAW controls change for user-editing reasons. Splitting the readout helper makes future diagnostic fixes easier to review without navigating the full Develop panel.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.

Non-goals:

- No Auto solve behavior changes, candidate feedback scheduling changes, quiet-window timing changes, status text semantics changes, graph schema changes, JSON schema changes, render math changes, Develop identity changes, guide status changes, or deferred feature status changes.
- No requested-vs-achieved explanation UI, score-component panels, candidate timeline, graph controls, candidate gallery UI, or Manual handoff work.

## 2026-06-20 - Split Develop Subject Controls

Decision:

Move Develop Subject Importance controls from `src/Editor/Internal/EditorModuleDevelopControls.cpp` into `src/Editor/Internal/EditorModuleDevelopSubjectControls.h/.cpp`. The new helper renders region/brush editing, overlay toggles, interpreted/refined map display toggles, per-stroke/per-region editing, and returns changed/reanalysis/interaction state to `EditorModule::RenderRawDevelopControls`. Keep the public `RenderRawDevelopControls` entry point, Auto/Manual mode flow, Auto guidance sliders/status readouts, Manual RAW controls, Scene Prep bridge, and integrated Finish Tone bridge in `EditorModuleDevelopControls.cpp`.

Rationale:

Subject Importance controls change for different reasons than RAW exposure, scene prep, and finish tone controls. They depend on region/stroke editing, viewport overlay display state, and subject-solver interaction timing. Moving them into a named subject-controls helper makes future Guide 05 UI fixes and brush/region QA easier to review without navigating the whole Develop panel.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.

Non-goals:

- No Subject Importance behavior changes, brush behavior changes, overlay behavior changes, Auto reanalysis policy changes, interaction quiet-window changes, graph schema changes, solver behavior changes, render math changes, Develop identity changes, guide status changes, or deferred feature status changes.
- No edge-aware subject refinement, semantic/AI subject detection, graph-control UI, candidate gallery UI, or Manual handoff work.

## 2026-06-20 - Split Renderer HDR Merge Pass

Decision:

Move HDR Merge rendering and alignment helpers from `src/Renderer/Internal/RenderPipelineNodePasses.cpp` into `src/Renderer/Internal/RenderPipelineHdrMergePass.cpp`. The new file owns `RenderPipeline::RenderHdrMerge`, texture feature readback, feature-image construction, reference selection, translation/threshold alignment scoring, alignment confidence, deghost setup, and HDR merge shader uniform binding. Keep generated mask, custom-mask, mix, data math, mask utility, image-to-mask, and channel split/combine pass implementations in `RenderPipelineNodePasses.cpp` at the time of this split; a newer decision above moved Custom Mask rasterization into `RenderPipelineCustomMaskPass.cpp`.

Rationale:

HDR merge alignment has a distinct dependency and review surface from generic utility node passes: it reads input textures back to CPU feature buffers, chooses references, scores translations, and configures deghost/alignment uniforms. Moving it into a named HDR merge pass file makes future alignment fixes easier to audit without scanning custom-mask rasterization, mix/data math, or channel utility code. The custom-mask rasterization concern is now also isolated by the newer custom-mask split above.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.

Non-goals:

- No HDR merge behavior changes, alignment math changes, reference-selection changes, deghost policy changes, shader source changes, render math changes, graph execution changes, cache behavior changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No redesign of HDR merge alignment, the physical staged renderer, sidecar stats, candidate gallery, or View Transform behavior.

## 2026-06-20 - Split Renderer Image Generator Pass

Decision:

Move Image Generator texture creation plus generated text-image helpers from `src/Renderer/Internal/RenderPipelineNodePasses.cpp` into `src/Renderer/Internal/RenderPipelineImageGeneratorPass.cpp`. The new file owns `RenderPipeline::GenerateImageTexture`, CPU text canvas generation, UTF-8 text decoding, embedded font access, and stb_truetype rasterization. Keep generated mask, custom-mask, mix, data math, mask utility, image-to-mask, and channel split/combine pass implementations in `RenderPipelineNodePasses.cpp` at the time of this split; HDR merge ownership is superseded by the newer HDR Merge pass split above, and custom-mask rasterization ownership is superseded by the newer Custom Mask pass split above.

Rationale:

Generated text images have different dependencies and change pressure than the rest of the node-pass file: they pull in embedded font data, UTF-8 decoding, CPU glyph layout/rasterization, and texture upload behavior. Moving them into a named image-generator pass file reduces renderer pass coupling and makes future generated-image fixes easier to review without traversing mask, data math, and channel code.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.

Non-goals:

- No image-generator behavior changes, text layout changes, font changes, shader source changes, render math changes, graph execution changes, cache behavior changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No redesign of HDR merge alignment, generated-image features, text shaping, View Transform behavior, or the physical staged renderer.

## 2026-06-20 - Split Tone Curve Viewport Helpers

Decision:

Move tone-curve viewport stats probing, focused-node resolution, curve-input/final-preview pixel sampling, probe clear/update behavior, and target-drag lifecycle from `src/Editor/Internal/EditorModuleRendering.cpp` into `src/Editor/Internal/EditorModuleToneCurveViewport.cpp`. Keep render snapshots, dirty-state orchestration, worker submission/result consumption, tone-curve auto rewrite feedback, and output/cache adoption in `EditorModuleRendering.cpp`.

Rationale:

Tone Curve viewport interaction changes when canvas probing, target dragging, sampling basis behavior, or integrated-tone transient state changes. Render orchestration changes when snapshots, worker requests, completed textures, or cache adoption changes. Splitting those responsibilities makes future Tone Curve interaction fixes easier to review without navigating the render result pipeline.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.

Non-goals:

- No tone-curve math changes, sampling-basis behavior changes, target-drag behavior changes, integrated-tone JSON/schema changes, render snapshot changes, render submission policy changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No redesign of the Tone Curve surface, View Transform behavior, render worker queue, candidate feedback scheduling, or Manual/Auto handoff.

## 2026-06-20 - Split Editor Render Request Builders

Decision:

Move preview-like deferral/admission checks, composite output request construction, and preview request construction from `src/Editor/Internal/EditorModuleRendering.cpp` into `src/Editor/Internal/EditorModuleRenderRequests.cpp`. Keep render snapshots, dirty-state orchestration, worker submission/result consumption, tone-curve auto rewrite feedback, and output/cache adoption in `EditorModuleRendering.cpp`.

Rationale:

Render request construction changes when preview breadth, composite raster sizing, reference-source fallback, or deferral policy changes. Worker result consumption changes when completed textures, preview caches, composite uploads, or feedback application changes. Splitting those responsibilities makes render scheduling changes easier to review without mixing them into the large result-processing loop.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.

Non-goals:

- No preview/composite request behavior changes, render submission policy changes, dirty-generation changes, cache behavior changes, tone-curve feedback changes, Develop behavior changes, guide status changes, or deferred feature status changes.
- No redesign of the render worker queue, interruptible rendering, preview cache model, composite output sizing policy, or candidate feedback scheduling.

## 2026-06-20 - Split Develop Candidate Request Construction

Decision:

Move Develop candidate render request construction out of `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp` into `src/Editor/Internal/EditorModuleDevelopCandidateRequests.cpp`. The new request file owns request-budget/admission policy, the 0.60 second quiet-window scheduling gate, subject metric sampling, candidate render payload mutation, adaptive render-budget validation helpers, and `BuildDevelopCandidateRenderRequests`. Move the small shared candidate-stage and stage-cache vocabulary into `src/Editor/Internal/EditorModuleDevelopCandidateShared.h/.cpp` so request selection and rendered-feedback writeback use the same stage names, dirty-boundary expectations, and candidate id classification helpers.

Rationale:

The candidate-feedback implementation had become two subsystems in one file: choosing/rendering candidate probes, then consuming rendered metrics and writing feedback JSON. Request scheduling and payload mutation change for different reasons than rendered scoring/history/writeback, so separating them makes future scheduler, candidate-family, and stage-cache fixes easier to audit without risking feedback-application behavior.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.

Non-goals:

- No candidate selection changes, render budget changes, quiet-window timing changes, candidate ids, JSON key/schema changes, rendered metric thresholds, solver behavior, render math, Develop guide status changes, or deferred feature status changes.
- No candidate gallery UI, background queue, staged render controller redesign, subject brush behavior, learning application, or Manual handoff work.

## 2026-06-20 - Split Render Pipeline Node and Raw Detail Passes

Decision:

Move renderer pass implementations out of `src/Renderer/RenderPipeline.cpp` into renderer-internal files. At the time of this split, `src/Renderer/Internal/RenderPipelineNodePasses.cpp` took mask/custom-mask, mix, data math, HDR merge, mask utility, image-to-mask, image-generator, and channel split/combine passes plus their local helper code. Later decisions split Image Generator into `RenderPipelineImageGeneratorPass.cpp`, HDR Merge into `RenderPipelineHdrMergePass.cpp`, and Custom Mask rasterization into `RenderPipelineCustomMaskPass.cpp`. `src/Renderer/Internal/RenderPipelineRawDetailPasses.cpp` owns Auto Gain scene stats, effective Raw Detail Fusion settings, Pre-Local Exposure summaries, Raw Detail Auto Mask, Raw Detail Fusion, and `GetPreLocalExposureSummary`.

Rationale:

After previous resource, readback, shader-program, and graph-execution splits, `RenderPipeline.cpp` still carried a large collection of render-pass implementations. Moving pass families by domain leaves the central file as the thin layer-stack/masked-graph execution shell and gives future renderer work a more obvious ownership boundary for node-pass math versus Raw Detail / Pre-Local Exposure behavior.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.

Non-goals:

- No render math changes, shader source changes, cache key changes, graph traversal changes, resource lifecycle changes, Develop guide status changes, or deferred feature status changes.
- No redesign of Raw Detail Fusion, Pre-Local Exposure, HDR merge alignment, image generator text rendering, or the physical staged renderer.

## 2026-06-20 - Split Node Graph UI Visual Helpers

Decision:

Move node graph visual presentation helpers from `src/Editor/NodeGraph/EditorNodeGraphUI.cpp` into `src/Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h/.cpp`. The split covers graph style tokens, node family styles, node layout metrics, node presentation profiles, link visual styles, socket/link colors, mini preview/spotlight drawing helpers, and node/graph label helpers.

Rationale:

`EditorNodeGraphUI.cpp` was still carrying theme, metrics, labels, and drawing primitives alongside canvas orchestration and interaction state. Keeping those visual helpers in a named UI submodule gives future graph feature and bug-fix passes an obvious place for presentation changes without mixing them into the canvas renderer or event handling body.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.

Non-goals:

- No graph UI behavior changes, node layout changes, visual design changes, interaction changes, serialization changes, Develop guide status changes, or deferred feature status changes.
- No split of the remaining canvas draw loop in this step. Later handoffs moved canvas orchestration to `EditorNodeGraphUICanvas.cpp`, node body rendering to `EditorNodeGraphUINodes.cpp`, link/group drawing to `EditorNodeGraphUILinksAndGroups.cpp`, and interaction handling to `EditorNodeGraphUIInteraction.cpp`.

## 2026-06-20 - Split Develop Auto-Solve Validation Scenarios

Decision:

Split the remaining large solver-only Auto-solve validation body out of `src/App/Validation/Suites/DevelopAutoSolveValidation.cpp` into named scenario fragments under `src/App/Validation/Suites/DevelopAutoSolveValidationScenarios/`. The wrapper keeps the public `Stack::Validation::ValidateDevelopAutoSolveBehavior` entrypoint unchanged and includes six scenario phases: initial solve/memory, regional/subject, candidate payload/scheduler, rendered feedback, core dynamic-range profiles, and final result aggregation/reporting.

Rationale:

`ValidateDevelopAutoSolveBehavior` had become a 5,000-line wall that mixed every solver scenario, support fixture, rendered-feedback path, and failure-report expression in one file. The scenario phases intentionally share local state and assertion order, so this pass used included scenario fragments as a low-risk organization step instead of rewriting the validation suite into a new API. Future passes can now edit the relevant scenario file directly, and later refactors can convert individual fragments into real functions once their data boundaries are clearer.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.

Non-goals:

- No validation assertion changes, scenario-order changes, failure-report content changes, helper behavior changes, solver behavior changes, candidate ids, JSON schemas, render math, CLI flag changes, Develop guide status changes, or deferred feature status changes.
- No conversion of the scenario fragments into independent functions in this step.

## 2026-06-20 - Split Develop Candidate Feedback and Defaults

Decision:

Move Develop rendered candidate request selection, admission/deferred feedback gates, rendered metric scoring/classification helpers, rendered feedback JSON application, and related validation helper implementations from `src/Editor/Internal/EditorModuleRendering.cpp` into `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp`. Move shared Develop default construction into `src/Editor/Internal/EditorModuleDevelopDefaults.h`, including default integrated ToneCurve JSON and RAW metadata-to-default-settings construction.

Rationale:

`EditorModuleRendering.cpp` should coordinate render snapshots, worker submission, preview/composite requests, and result consumption. Develop candidate feedback is a separate solver-feedback subsystem with its own request budget, stage scheduler, quiet-window gate, rendered metric classifiers, and feedback JSON contract. Keeping it in a focused file makes candidate-scheduler changes easier to review without wading through general render orchestration. Centralizing Develop defaults removes repeated local copies in graph mutation, Auto solve, and RAW decode controls, reducing drift risk when future RAW metadata behavior changes.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the candidate feedback split.
- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the shared defaults consolidation.

Non-goals:

- No candidate selection changes, scoring changes, rendered metric threshold changes, JSON key/schema changes, quiet-window timing changes, render math changes, graph snapshot behavior changes, RAW default value changes, UI behavior changes, serialized kind changes, node title changes, Develop guide status changes, or deferred feature status changes.
- No split of the remaining `ValidateDevelopAutoSolveBehavior` scenario body, no candidate gallery UI, no staged render controller redesign, and no replacement of the existing render worker queue.

## 2026-06-20 - Split Develop Auto Solver and Subject Brush Implementation

Decision:

Move the large Develop Auto solver, candidate-scoring/feedback helpers, dynamic-range/local-exposure strategy helpers, subject-importance interpretation/refinement helpers, and subject viewport/brush bridge from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp`.

Rationale:

Develop Auto is a distinct subsystem inside the editor: it owns authored RAW/Scene Prep/Tone solve decisions, candidate families, subject map diagnostics, and viewport subject-guidance handoff. Keeping those helpers together in one focused editor-internal file removes the largest Develop-specific island from the central coordinator while avoiding a premature cross-module API redesign. At the time of this split, `EditorModuleGraphMutation.cpp` kept `UpdateDevelopAutoState` and the trigger hashes as the live graph-mutation gate for deciding when a solve should run; those graph-side trigger helpers now live with processing-node creation in `EditorModuleGraphProcessingNodes.cpp`.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No solver behavior changes, candidate id changes, scoring changes, JSON key/schema changes, trigger-hash behavior changes, subject brush behavior changes, viewport overlay behavior changes, render math changes, serialized kind changes, node title changes, or Develop guide status changes.
- No consolidation of the duplicated `BuildDefaultIntegratedToneLayerJson` or `BuildRawDevelopSettingsFromMetadata` helpers in this step.
- No move of `BuildDevelopCandidateRenderRequests` or `ApplyDevelopCandidateRenderFeedback` out of `EditorModuleRendering.cpp` in this step.

## 2026-06-20 - Split Editor Preview State

Decision:

Move editor preview/scope revision helpers, cached preview pixel lookup, Auto Gain mask preview toggling, workspace color fallback, and the Graph Performance overlay from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModulePreviewState.cpp`.

Rationale:

These methods manage preview-facing editor state and readouts rather than graph mutation, project lifecycle, toolbar UI, or Develop solver logic. Keeping preview revision handoff and performance overlay drawing in one focused editor-internal file makes preview/cache bugs easier to trace while preserving the same public `EditorModule` method surface used by viewport, scopes, and render submission.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No preview refresh policy changes, cache key changes, graph performance metric changes, Auto Gain mask preview behavior changes, workspace color/theme changes, render graph changes, Develop behavior changes, or deferred feature status changes.
- No Develop guide status changes.

## 2026-06-20 - Move Composite Chain Helpers

Decision:

Move composite chain label/fingerprint helpers and deprecated composite-node shim methods from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleComposite.cpp`.

Rationale:

Composite chain labels and fingerprints are composite ownership concerns. The existing composite implementation already owns scene items, z-order, persistence, export settings, canvas interaction, and composite render helpers, so keeping the remaining chain helper methods there reduces central coordinator size without changing completed-chain cache traversal or render scheduling.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No completed-chain cache changes, composite label/fingerprint behavior changes, composite render behavior changes, graph traversal changes, Develop behavior changes, or deferred feature status changes.
- No Develop guide status changes.

## 2026-06-20 - Split Editor Floating Toolbar

Decision:

Move editor floating toolbar rendering, embedded toolbar icon texture loading, popup-safe toolbar switching helpers, and subwindow switching methods from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleToolbar.cpp`.

Rationale:

The floating toolbar is an editor UI shell concern. Keeping its icon resource loading, toolbar draw loop, popup handoff, and tab/complex-node subwindow transitions in one focused file reduces central coordinator size without touching graph model behavior, renderer inputs, or Develop solver logic.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No toolbar UX changes, icon changes, subwindow transition behavior changes, node graph behavior changes, render graph changes, Develop behavior changes, or deferred feature status changes.
- No Develop guide status changes.

## 2026-06-20 - Split Editor Render Graph Snapshot Assembly

Decision:

Move editor-to-render graph layer/mask/snapshot assembly and render payload conversion helpers from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`.

Rationale:

Snapshot assembly translates editor graph nodes into renderer graph payloads and is distinct from editor UI, graph mutation, project lifecycle, source/reference pixel bridging, and Develop solver logic. Keeping layer/step/mask snapshot construction with the local payload conversion helpers makes renderer-boundary changes easier to review without adding more responsibility to the central editor coordinator body.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No render graph schema changes, payload value changes, graph traversal or link-filtering changes, renderer input changes, Develop behavior changes, candidate scheduling changes, or render math changes.
- No Develop guide status or deferred feature status changes.

## 2026-06-20 - Split Editor Reference Source Pixel Bridge

Decision:

Move editor reference/source pixel bridge methods from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleReferenceSources.cpp`. The split includes shared image pixel aliasing, render image payload construction, copied fallback pixels for image/RAW-domain sources, and reference-source buffer/pixel/dimension resolution.

Rationale:

These methods adapt graph source nodes into renderer/preview/reference pixel inputs. They are not Auto solve, UI drawing, graph mutation, or project lifecycle code, so keeping them in a focused editor-internal file makes source/reference pixel bugs easier to find without adding another island to the central coordinator body.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No reference-source selection changes, pixel content changes, RAW orientation/dimension behavior changes, render graph schema changes, preview behavior changes, or Develop behavior changes.
- No Develop guide status or deferred feature status changes.

## 2026-06-20 - Move Detached Preview View Logic

Decision:

Move detached preview monitor placement, fullscreen toggle/close state, and detached preview window rendering from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleView.cpp`.

Rationale:

Detached preview is view/window coordination and belongs with graph view transform, auto-focus, split-pane, and viewport presentation code. This keeps the central coordinator from owning another UI/windowing island while preserving public `EditorModule` methods and callers.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No preview UX changes, window placement changes, viewport rendering changes, shortcut/menu behavior changes, or Develop behavior changes.
- No Develop guide status or deferred feature status changes.

## 2026-06-20 - Split Develop Auto-Solve Validation Helpers

Decision:

Move reusable helper logic from the solver-only Develop Auto-solve validation suite into focused validation helper files. `DevelopAutoSolveValidationHelpers.h/.cpp` owns staged-state lookup, guidance JSON extraction from tone/candidate records, and finish-tone/white-balance probe id checks. `DevelopAutoSolveValidationCandidateProbes.h/.cpp` owns the initial candidate-family JSON scan through `DevelopAutoSolveCandidateProbeSummary` / `BuildDevelopAutoSolveCandidateProbeSummary`. `DevelopAutoSolveValidationRenderedMetrics.h/.cpp` owns rendered-metric JSON fixture serialization and synthetic rendered-metric fixture/classifier probe construction through `DevelopAutoSolveRenderedMetricFixtures` / `BuildDevelopAutoSolveRenderedMetricFixtures`.

Rationale:

`ValidateDevelopAutoSolveBehavior` is still a large scenario body, but several helper routines, candidate-solve probe scans, and rendered-metric fixture setup blocks were pure support code rather than scenario assertions. Moving them into focused helper files gives future per-guide validation splits stable utility boundaries while keeping the scenario order, diagnostic checks, fixture values, and thresholds unchanged.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` regenerated for the new helper source files and passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No validation assertion changes, solver behavior changes, serialized diagnostic changes, candidate id changes, rendered-metric threshold changes, CLI flag changes, or public validation API changes.
- No candidate-probe detection rule changes.
- No rendered-metric fixture value changes or classifier expectation changes.
- No per-guide/scenario extraction of the remaining large Auto-solve validation body in this step.
- No Develop guide status or deferred feature status changes.

## 2026-06-20 - Split Node Graph UI State Accessors

Decision:

Move `EditorNodeGraphUI` active graph/layer accessors, layer-surface classification, dedicated/sidebar-only editor classification, rich-layer detection, and preset preview graph-cache lookup from `src/Editor/NodeGraph/EditorNodeGraphUI.cpp` into `src/Editor/NodeGraph/UI/EditorNodeGraphUIState.cpp`.

Rationale:

These helpers are coordinator/state access points used by several graph UI surfaces, not canvas drawing code. Moving them creates a focused boundary for graph/layer override state and node-surface classification while leaving the main canvas render and interaction bodies unchanged.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` regenerated for the new state source file and passed.

Non-goals:

- No graph selection, rendering, node-surface behavior, preset preview behavior, or layer editor behavior changes.
- No move of the main `RenderGraphCanvas`, `RenderNode`, `RenderLinks`, `RenderGroups`, or `RenderInteraction` bodies in this step.
- No Develop guide status or deferred feature status changes.

## 2026-06-20 - Split Node Graph Connection Helpers

Decision:

Move `EditorNodeGraphUI` auto-connect and channel-routing helpers from `src/Editor/NodeGraph/EditorNodeGraphUI.cpp` into `src/Editor/NodeGraph/UI/EditorNodeGraphUIConnections.cpp`. The split includes `ConnectOutputToBestInput`, `ConnectBestOutputToInput`, and `GetUpstreamChannel`.

Rationale:

The main graph UI file still owns the large canvas renderer and interaction state machine. Socket auto-wiring is used by drag/drop, node insertion, and node-browser flows, but it is not canvas drawing. Moving it into a focused UI helper file reduces the main file and makes future link/routing bug fixes easier to review without entering the render loop body.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` regenerated for the new connection source file and passed.

Non-goals:

- No socket compatibility, channel routing, drag/drop, node insertion, graph document, or node browser behavior changes.
- No move of the main `RenderInteraction`, `RenderNode`, `RenderLinks`, or `RenderGroups` bodies in this step.
- No Develop guide status or deferred feature status changes.

## 2026-06-20 - Split Render Pipeline Program Setup

Decision:

Move `RenderPipeline` shader/program setup methods from `src/Renderer/RenderPipeline.cpp` into `src/Renderer/Internal/RenderPipelinePrograms.cpp`. The split includes the existing lazy `Ensure*Program` methods and their embedded GLSL strings for mask generation/blending, mix, LUT, data math, HDR merge, utility/image-to-mask/image generator, channel split/combine, Auto Gain stats, and Raw Detail Fusion programs.

Rationale:

After graph execution, readback, and resource lifecycle were split, `RenderPipeline.cpp` still mixed render-pass helpers with large shader-source blocks. Moving program setup creates a focused ownership boundary for shader strings and lazy program construction while keeping the actual render-pass functions in place, which avoids draw-order or uniform-binding behavior changes.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` regenerated for the new program source file and passed.

Non-goals:

- No shader source edits, render math changes, uniform binding changes, render-pass helper moves, cache behavior changes, or public `RenderPipeline` API changes.
- No split of individual shader families into per-domain files in this step.
- No Develop guide status or deferred feature status changes.

## 2026-06-20 - Split Render Pipeline Resource Lifecycle

Decision:

Move `RenderPipeline` resource lifecycle methods from `src/Renderer/RenderPipeline.cpp` into `src/Renderer/Internal/RenderPipelineResources.cpp`. The split includes construction/destruction, initialization, FBO cleanup/resize, source image and pixel loading, clearing output/source state, external output texture ownership, shared output publication, graph-cache invalidation, RawDevelop stage-cache cleanup, and validation-facing RawDevelop stage-cache sizing helpers.

Rationale:

`RenderPipeline.cpp` still owns a large shader/program and render-helper surface, but texture/source/FBO lifecycle is a cleaner independent boundary. Moving that code gives resource ownership a dedicated file beside the existing readback and graph-execution splits, while avoiding a higher-risk shader extraction where program builders and render helpers are still interleaved.

Validation:

- `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` regenerated for the new resource source file and passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No render math, shader source, cache key, cache policy, image output, source fingerprint policy, public `RenderPipeline` method, or validation behavior changes.
- No graph execution, readback/statistics, or shader/program ownership split in this step.
- No Develop guide status or deferred feature status changes.

## 2026-06-20 - Split Node Graph Interaction Support Helpers

Decision:

Move node graph interaction support helpers from `src/Editor/NodeGraph/EditorNodeGraphUI.cpp` into `src/Editor/NodeGraph/UI/EditorNodeGraphUIInteraction.cpp`. The split includes graph hover/mouse-owner classification, channel-split confirmation state helpers, node header/draggable hit tests, validation status overlay, channel-split confirmation prompt, and the interaction debug overlay.

Rationale:

`EditorNodeGraphUI.cpp` was still the largest UI hotspot at this point. The full canvas renderer and `RenderInteraction` state machine were more sensitive because draw order, hover ownership, and drag state can regress silently, so this step created an interaction-support boundary first without moving the main canvas draw loop or the large event-handling body. Later handoffs moved `RenderInteraction` into this interaction file, moved canvas orchestration into `EditorNodeGraphUICanvas.cpp`, and split node/link/group drawing into focused files.

Validation:

- The first `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` regenerated for the new interaction source and linked once before compiling the new file, producing the expected temporary unresolved symbols.
- The immediate rerun of `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` compiled `EditorNodeGraphUIInteraction.cpp` and passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No graph UI behavior, draw ordering, node/link/group rendering, selection semantics, drag semantics, context-menu behavior, or graph document behavior changes.
- No move of the main `RenderInteraction` body yet.
- No Develop guide status or deferred feature status changes.

## 2026-06-20 - Move Develop Auto-Solve Validation Into Suite Folder

Decision:

Move the solver-only `ValidateDevelopAutoSolveBehavior` implementation into `src/App/Validation/Suites/DevelopAutoSolveValidation.cpp`. Keep the public `Stack::Validation::ValidateDevelopAutoSolveBehavior` function declared through `src/App/Validation/ValidationSuites.h` and keep `ValidationCommandRunner.cpp` as CLI flag routing only.

Rationale:

The remaining validation implementation was a nearly 6k-line solver-only suite sitting at the top of the validation folder after Tone Curve and Develop smoke validation had already moved under `Suites/`. Moving it under the suite folder gives future validation work an accurate ownership boundary before any finer per-guide validation split is attempted.

Validation:

- The first `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` regenerated for the source move and still tried the removed old source path once.
- The immediate rerun of `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` compiled `DevelopAutoSolveValidation.cpp` and passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No validation logic, solver behavior, serialized diagnostics, render math, CLI flag, or public validation function changes.
- No Develop guide status or deferred feature status changes.
- No split of the large Auto-solve validation body into smaller per-guide files in this step.

## 2026-06-20 - Rename Remaining Develop Control Surface

Decision:

Rename the remaining `src/Editor/Internal/EditorModuleRawUI.cpp` implementation to `src/Editor/Internal/EditorModuleDevelopControls.cpp` after RAW basic controls, HDR Merge controls, and Pre-Local Exposure controls were split into their own files. Keep `EditorModule::RenderRawDevelopControls` and existing sidebar/node graph callers unchanged.

Rationale:

The old `EditorModuleRawUI.cpp` name no longer described the file after the previous organization splits. At the time of this rename, the file owned the Develop panel specifically: Auto/Manual mode controls, Auto guidance/status readouts, subject-importance controls, manual RAW controls, the scene-prep bridge, and integrated tone panels. Later decisions split subject-importance controls into `EditorModuleDevelopSubjectControls.*` and compact Auto status readouts into `EditorModuleDevelopAutoStatusControls.*`. Renaming the file made future Develop UI work start at an accurately named boundary while keeping behavior and public editor APIs stable.

Validation:

- The first `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` regenerated for the source rename and still attempted to compile the removed old source path.
- The immediate rerun of `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` compiled `EditorModuleDevelopControls.cpp` and passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No Develop UI behavior, serialized state, solver behavior, render math, or public method changes.
- No Develop guide status or deferred feature status changes.
- No split of the remaining monolithic Develop panel sections in this step.

## 2026-06-20 - Split Pre-Local Exposure Controls From Develop UI

Decision:

Move the shared Pre-Local Exposure / Auto Gain helper cluster, Develop Scene Prep helper controls, and standalone Raw Detail Auto Mask / Raw Detail Fusion panels from `src/Editor/Internal/EditorModuleRawUI.cpp` into `src/Editor/Internal/EditorModulePreLocalExposureControls.h/.cpp`. Keep `EditorModule::RenderRawDevelopControls`, `EditorModule::RenderRawDetailAutoMaskControls`, `EditorModule::RenderRawDetailFusionControls`, and existing sidebar/node graph callers unchanged.

Rationale:

After the RAW basic controls and HDR Merge controls splits, `EditorModuleRawUI.cpp` still mixed the Develop panel with a large reusable Pre-Local Exposure control surface. Pre-Local Exposure is shared by Develop Scene Prep, Raw Detail Auto Mask, and standalone Raw Detail Fusion, so it deserves its own file/folder boundary. This makes future local-exposure bug fixes and guide work easier to review without digging through Develop Auto guidance, manual RAW controls, and integrated tone UI. Subject-importance controls were split later into `EditorModuleDevelopSubjectControls.*`.

Validation:

- The first `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` regenerated for the new source/header and linked before compiling the new implementation, producing the expected temporary unresolved symbols.
- The immediate rerun of `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` compiled `EditorModulePreLocalExposureControls.cpp` and passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No Pre-Local Exposure behavior, serialized settings, render output, UI text, or Develop scene-prep behavior changes.
- No Develop guide status or deferred feature status changes.
- No local exposure algorithm redesign, local-EV map UI, graph controls, or clipped-data recovery claim.
- No split of the remaining monolithic Develop panel sections in this step.

## 2026-06-20 - Isolate HDR Merge Control Panel

Decision:

Move the HDR Merge node settings panel from `src/Editor/Internal/EditorModuleRawUI.cpp` into `src/Editor/Internal/EditorModuleHdrMergeControls.cpp`, along with its private settings comparison, clamping, and label helpers. Keep `EditorModule::RenderHdrMergeControls` and existing sidebar/node graph callers unchanged.

Rationale:

`EditorModuleRawUI.cpp` was still carrying Develop, Pre-Local Exposure, and HDR Merge panels after the basic RAW controls split. The HDR Merge panel is self-contained, uses HDR status data through existing `EditorModule` methods, and can be isolated without changing node identity, serialized settings, render math, or UI behavior. Keeping the public method stable preserves caller compatibility while making future HDR Merge UI/status changes easier to review separately from Develop Auto and local-exposure work.

Validation:

- The first `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` regenerated for the new source file and linked before compiling it, producing the expected temporary unresolved `RenderHdrMergeControls` symbol.
- The immediate rerun of `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` compiled `EditorModuleHdrMergeControls.cpp` and passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.

Non-goals:

- No HDR Merge behavior, serialization, render output, status contract, or UI text changes.
- No Develop guide status or deferred feature status changes.
- No extraction of the remaining Develop Auto or Pre-Local Exposure UI sections in this step.

## 2026-06-19 - Create Develop Type Home and Validation Utility Boundary

Decision:

Start the organization pass by creating low-risk ownership boundaries rather than moving solver/render behavior first: Develop-owned payload and guidance types now live in `src/Develop/DevelopTypes.h`; basic node graph primitives, payload structs, and document structs each have focused headers; Develop subject-importance graph serialization, RAW/HDR graph serialization, LUT graph serialization, Custom Mask graph serialization, utility-node graph serialization, and image-node PNG persistence each have focused serializer-family files; passive editor state/view-model structs now live in `src/Editor/EditorModuleTypes.h`; HDR merge status/topology helpers live in an editor-internal implementation file; basic RAW node control panels live separately from the Develop-heavy RAW UI implementation; node graph clipboard/preset import behavior lives in its own UI implementation file; renderer readback/export/stat helpers live in a renderer-internal implementation file; validation command dispatch is separated from validation suites; the Tone Curve validation suite lives in `src/App/Validation/Suites/ToneCurveValidation.cpp`; Develop smoke and optional real-RAW validation live in `src/App/Validation/Suites/DevelopSmokeValidation.cpp`; reusable validation image/readback helpers live in `src/App/Validation/ValidationImageUtils.h/.cpp`; and source inventory lives in `cmake/StackSources.cmake`.

Rationale:

The largest future refactors depend on stable ownership boundaries. Moving pure Develop type definitions first gives later solver extraction a real domain folder while preserving the existing `EditorNodeGraph::...` names that many files still use. Moving Develop subject-importance graph serialization into a serializer-family file separates persisted region/stroke map mechanics from the central graph payload serializer while preserving the existing `developSubjectImportance` JSON contract. Moving RAW metadata/settings, RAW Detail Fusion scene-prep settings, and HDR merge settings serialization into a RAW/HDR serializer-family file separates durable RAW/HDR JSON mechanics from top-level graph payload assembly while preserving the existing schema. Moving LUT payload and LUT stage serialization into a LUT serializer-family file keeps color-LUT import/use-mode JSON rules near the LUT payload instead of the central graph serializer. Moving Custom Mask payload and raster/object serialization into a Custom Mask serializer-family file keeps its binary raster encoding, tool settings, object fallbacks, and reference-mode rules away from unrelated graph assembly code. Moving utility-node serialization into a utility serializer-family file keeps scope/mask generator/mask utility/image-to-mask/image generator/mix/data math string and settings fallbacks together instead of mixing them with top-level graph assembly. Moving image-node PNG byte persistence into an image serializer-family file keeps STB encode/decode and storage-orientation conversion out of graph document assembly. Moving editor state structs out of `EditorModule.h` reduces coordinator-header ownership without forcing a risky call-site rename, because the old `EditorModule::...` spellings remain aliases. Moving HDR merge status out of `EditorModule.cpp` removes a cross-UI/render status block from the central editor coordinator while preserving the methods already used elsewhere. Moving the basic RAW Source, RAW Neural Denoise, and RAW Decode panels out of the Develop UI file separates manual RAW-chain surfaces from the larger Develop control panel while keeping the same `EditorModule` methods and callers. Moving node graph clipboard/preset import/export code out of the main canvas UI file reduces UI-file coupling without changing graph interaction behavior. Moving renderer readback out of the main pipeline body separates UI/export sampling and statistics from graph execution, shader setup, and texture ownership without changing public `RenderPipeline` methods. Splitting validation helpers reduces the size and coupling of the validation runner; moving self-contained Tone Curve and Develop smoke validation suites into a suite folder proves the pattern before touching the larger solver-only Auto-solve validation body. Moving source inventory out of the root CMake file gives future source organization a dedicated place and keeps target wiring easier to review.

Implemented:

- Added `src/Develop/DevelopTypes.h` for `RawDevelopPayload`, `RawDevelopUiMode`, Auto intent/guidance, subject-importance modes, and subject-importance map structs.
- Kept `EditorNodeGraph::RawDevelopPayload`, `EditorNodeGraph::RawDevelopUiMode`, `EditorNodeGraph::DevelopAutoIntent`, `EditorNodeGraph::DevelopAutoGuidance`, and `EditorNodeGraph::DevelopSubjectImportance*` as compatibility aliases/wrappers.
- Added `src/Editor/NodeGraph/NodeGraphTypes.h` for socket ids, DataMath socket helpers, `Vec2`, node/socket enums, custom-mask primitive enums, and `SocketDefinition`.
- Added `src/Editor/NodeGraph/NodeGraphPayloads.h` for image/raw/LUT payload aliases, RAW decode/neural/detail/HDR payloads, mask/custom-mask/image-to-mask/image-generator settings, and data-math settings.
- Added `src/Editor/NodeGraph/NodeGraphModelTypes.h` for `Node`, `Link`, `NodeGroup`, `CompletedChainInfo`, `LinkRole`, and `ValidationResult`.
- Added `src/Editor/NodeGraph/Serialization/EditorNodeGraphDevelopSerialization.h/.cpp` for Develop subject-importance map serialization/deserialization, leaving `EditorNodeGraphSerializer.cpp` responsible for top-level graph payload assembly.
- Added `src/Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.h/.cpp` for RAW metadata/settings, RAW Detail Fusion scene-prep settings, and HDR merge settings serialization/deserialization, leaving `EditorNodeGraphSerializer.cpp` responsible for top-level graph payload assembly.
- Added `src/Editor/NodeGraph/Serialization/EditorNodeGraphLutSerialization.h/.cpp` for LUT payload, 1D/3D stage, import-format, use-mode, and transfer-function serialization/deserialization, leaving `EditorNodeGraphSerializer.cpp` responsible for top-level graph payload assembly.
- Added `src/Editor/NodeGraph/Serialization/EditorNodeGraphCustomMaskSerialization.h/.cpp` for Custom Mask payload, object, raster, reference-mode, operation, and editor-tool serialization/deserialization, leaving `EditorNodeGraphSerializer.cpp` responsible for top-level graph payload assembly and image-node PNG byte persistence.
- Added `src/Editor/NodeGraph/Serialization/EditorNodeGraphUtilitySerialization.h/.cpp` for Scope, Mask Generator, Mask Combine, Mask Utility, Image-to-Mask, Image Generator, Mix, and Data Math serialization/deserialization, leaving `EditorNodeGraphSerializer.cpp` responsible for top-level graph payload assembly and image-node PNG byte persistence.
- Added `src/Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.h/.cpp` for image-node PNG byte persistence, storage row-flipping, STB encode/decode, and binary JSON byte reads, leaving `EditorNodeGraphSerializer.cpp` responsible for top-level graph payload assembly.
- Added `src/Editor/EditorModuleTypes.h` for passive editor coordinator types such as composite export/snap state, graph preview/performance stats, HDR merge status, Develop subject viewport state, node-browser thumbnail state, deferred project-load state, and small runtime cache structs.
- Kept the existing `EditorModule::...` type names as aliases in `EditorModule.h` so existing editor, viewport, sidebar, node graph UI, validation, and rendering call sites continue to compile unchanged.
- Added `src/Editor/Internal/EditorModuleHdrMergeStatus.cpp` for HDR merge input topology, status, metadata exposure summaries, readiness messages, and active-output state checks, preserving the existing `EditorModule` methods and callers.
- Added `src/Editor/Internal/EditorModuleRawBasicControls.cpp` for RAW Source, RAW Neural Denoise, and RAW Decode control panels, plus `src/Editor/Internal/EditorModuleRawControlShared.h` for shared RAW display/default/reset helpers used by basic RAW panels and the Develop panel.
- Added `src/Editor/NodeGraph/UI/EditorNodeGraphUIClipboard.cpp` for `EditorNodeGraphUI` clipboard, graph-text copy/paste, duplicate, and preset-payload import behavior, reducing the main `EditorNodeGraphUI.cpp` file while preserving the existing `EditorNodeGraphUI` methods.
- Added `src/Renderer/Internal/RenderPipelineReadback.cpp` for `RenderPipeline` output/source/preview/scope pixel export, cached graph image readback, output pixel sampling, and output texture statistics, reducing `RenderPipeline.cpp` while preserving the existing `RenderPipeline` public methods.
- Added `src/App/Validation/ValidationImageUtils.h/.cpp` for validation pixel stats, fine-noise stats, OpenGL texture readback, RAW validation path resolution, preview stem sanitizing, and validation PNG writing.
- Added `src/App/Validation/ValidationSuites.h/.cpp` so `ValidationCommandRunner.cpp` only routes CLI validation flags and no longer owns the full validation suite implementation.
- Added `src/App/Validation/Suites/ToneCurveValidation.cpp` for `ValidateToneCurveAutoIntegration` and its local fixtures/hash helpers.
- Added `src/App/Validation/Suites/DevelopSmokeValidation.cpp` for `ValidateDevelopNodeSmoke`, `ValidateDevelopRealRawSmoke`, graph-state serialization validation, synthetic RAW fixtures, and smoke/real-RAW render helpers.
- Added `cmake/StackSources.cmake` and included it from `CMakeLists.txt` so app/test source inventory has a focused build-system home.
- Added `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md` as a short standing note for future feature/bug-fix passes to consider file and folder boundaries before adding to large existing files.

Explicit non-goals:

- No behavior changes to Develop Auto, Manual mode, scene prep, integrated tone, subject maps, validation command flags, graph serialization, render math, or node identity.
- No rename of `NodeKind::RawDevelop`, `RenderGraphNodeKind::RawDevelop`, serialized kind `"RawDevelop"`, or the user-visible title `Develop`.
- No split of the Develop solver body out of `EditorModule.cpp` in this pass.
- No split of the Develop-heavy `EditorModule.cpp` implementation body in this pass; the HDR merge status split is a narrow editor-status extraction only.
- No split of the remaining solver-only `ValidateDevelopAutoSolveBehavior` body into per-feature files yet.
- No split of top-level graph document assembly out of `EditorNodeGraphSerializer.cpp` yet.
- No split of the remaining node graph canvas-rendering/layout and input-interaction behavior yet.
- No full split of the remaining Develop, Pre-Local Exposure, and HDR Merge control code in `EditorModuleRawUI.cpp` yet.
- No split of renderer shader/program ownership or texture lifetime management yet.
- No claim that any deferred guide feature moved status.

Validation:

- `cmake -S . -B build` passed using the existing local dependency cache.
- `$env:CL='/FS'; cmake --build build --config Debug --target StackGraphBehaviorTests -- /m:1` passed.
- `build\StackGraphBehaviorTests.exe` passed.
- The normal `build\Stack.exe` relink later became blocked by a running local `build\Stack.exe` process, so final app verification used the side tree.
- `cmake -S . -B build_codex_verify` passed.
- `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` passed.
- After the node graph clipboard split, `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` passed again.
- After the HDR merge status split, `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` passed again.
- After the renderer readback split, `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` reconfigured once for the new source file and then passed.
- After the RAW basic controls split, `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` reconfigured once for the new source files and then passed.
- After the Tone Curve validation suite split, `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` reconfigured once for the new source file and then passed.
- After the Develop smoke validation suite split, `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` reconfigured once for the new source file, then passed after the new smoke suite called the public Auto-solve validation boundary.
- After the Develop subject-importance serializer split, `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` reconfigured once for the new source file and then passed.
- After the RAW/HDR serializer split, `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` reconfigured once for the new source file and then passed.
- After the LUT serializer split, the first `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` regenerated for the new source file and linked before compiling it; the immediate rerun compiled `EditorNodeGraphLutSerialization.cpp` and passed.
- After the Custom Mask serializer split, one build attempt regenerated for the new source file, one attempt hit a transient tab-icon bake write error, and the next compile exposed that image-node PNG deserialization still needed a central `ReadBinaryJson` helper. After restoring that helper beside the PNG code, `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` passed.
- After the utility-node serializer split, the first `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` regenerated for the new source file and linked before compiling it; the immediate rerun compiled `EditorNodeGraphUtilitySerialization.cpp` and passed.
- After the image-node serializer split, the first `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` regenerated for the new source file and linked before compiling it; the immediate rerun compiled `EditorNodeGraphImageSerialization.cpp` and passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs and limitations:

- The app still uses a broad source glob, now centralized in `cmake/StackSources.cmake`; fully explicit per-module source lists remain future work.
- The new Develop header is a type home only. Solver logic, diagnostics, candidate scoring, and subject-map interpretation still need later extraction from editor-owned files.
- A fresh side configure in `build_codex_organization_verify` could not complete because FetchContent failed to clone ImGui from GitHub, so verification used the existing `build` tree with populated local `_deps`.

## 2026-06-16 - Split Graph Execution Core and Reuse Prepared Candidate Graphs Per Develop Node

Decision:

Refactor the hottest renderer-adjacent execution path by extracting the graph execution core out of `RenderPipeline.cpp`, and split Develop candidate probe rendering out of `EditorRenderWorker.cpp` while adding per-snapshot lookup reuse and prepared candidate-graph reuse.

Rationale:

`RenderPipeline.cpp` and `EditorRenderWorker.cpp` had both accumulated large, tightly related execution blocks that were hard to navigate and harder to optimize surgically. The user asked for a carefully scoped renderer-adjacent split rather than a full renderer rewrite. The safest leverage points were the graph execution core and the Develop candidate-render path because both contained obvious repeated lookup work and graph-copy setup that could be improved without changing render math, cache semantics, or quality.

Implemented:

- Added `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` and moved the graph-execution body from `RenderPipeline::ExecuteGraph(...)` into `RenderPipeline::ExecuteGraphImpl(...)`.
- Added a file-local per-execution context that preindexes graph nodes by `nodeId` and input links by `(toNodeId, toSocketId)` and then reuses those indexes throughout traversal, fingerprinting, scalar classification, RAW/HDR input resolution, and final evaluation.
- Replaced the old active-node tree-set bookkeeping in the execution pass with active-node membership checks backed by the indexed node lookup.
- Kept RawDevelop stage-cache policy intact but changed one trim/store pass to maintain a local running byte total instead of recomputing total cache bytes on every eviction step.
- Added `src/Editor/Internal/EditorRenderWorkerCandidateRendering.cpp` and moved Develop candidate probe rendering into `EditorRenderWorker::RenderDevelopCandidateRequests(...)`.
- Preindexed Develop nodes once per snapshot in the worker and reused one mutable prepared graph per Develop node per snapshot, including the synthetic final/pre-finish output nodes, then only rewrote the `rawDevelop` payload and request revision for each candidate render.

Explicit non-goals:

- No shader-math, LUT, RAW, tone, or cache-key changes.
- No public API changes, project/schema/save-load changes, or graph model changes.
- No readback/export path rewrite.
- No candidate gallery, queue redesign, or profiler UI.
- No full breakup of `RenderPipeline.cpp`; shader-kernel setup, texture ownership, and readback/export helpers stay in place for now.

Validation:

- `cmake -S . -B build_codex_verify -G "Visual Studio 17 2022"` passed.
- `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` passed.
- `build_codex_verify\StackGraphBehaviorTests.exe` passed.
- `build_codex_verify\Stack.exe --validate-layer-registry` passed.
- `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.
- `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs and limitations:

- The normal `build\Stack.exe` binary was locked by a running local app process, so final verification used a clean side build tree instead of forcing the live session closed.
- The new graph execution file still carries several file-local helper copies to keep the split low-risk and self-contained. A later pass can reduce duplication once the new boundaries settle.
- This pass improves navigation and removes repeated lookup/setup work, but it intentionally does not claim a finished performance story for readback, texture allocation, or shader execution.

## 2026-06-16 - Extract Validation Harness and EditorModule Mutation/Lifecycle Files

Decision:

Refactor the largest handwritten navigation hotspots by shrinking `src/main.cpp` to a real launcher and moving its validation harness into `src/App/Validation/ValidationCommandRunner.cpp`, while also extracting `EditorModule` project-lifecycle and graph/layer mutation implementations into dedicated `src/Editor/Internal/*.cpp` files.

Rationale:

`main.cpp` and `EditorModule.cpp` had both grown into multi-responsibility files that were easy to get lost in, especially for smaller-context tooling and quick human navigation. The boundaries were already fairly clean: validation/smoke dispatch is not app-shell startup, and the editor already uses `Internal/EditorModule*.cpp` splits for other domains. Finishing those separations makes the repo easier to navigate without taking on risky renderer or serialization changes.

Implemented:

- Reduced `src/main.cpp` to working-directory setup, validation dispatch, and normal `AppShell` startup/shutdown.
- Added `src/App/Validation/ValidationCommandRunner.h/.cpp` with `TryRunValidationCommand(int argc, char** argv, int& exitCode)` and the existing validation/smoke helpers behind it.
- Added `src/Editor/Internal/EditorModuleProjectLifecycle.cpp` for blank-project reset, notification queue helpers, render submission reset, and lifecycle popup handling.
- Added `src/Editor/Internal/EditorModuleGraphMutation.cpp` for graph/layer mutation paths such as node creation, link mutation, image/raw/lut/output node helpers, and RAW full-tree creation; later organization passes moved image/RAW import to `EditorModuleGraphImageNodes.cpp`, mask helpers to `EditorModuleGraphMaskNodes.cpp`, and RAW/HDR/LUT processing-node creation plus RAW full-tree construction to `EditorModuleGraphProcessingNodes.cpp`.
- Kept `EditorModule` public APIs, command-line flags, graph behavior, rendering behavior, and save/load behavior unchanged.

Explicit non-goals:

- No command-line behavior changes.
- No `EditorModule` public API renames.
- No graph model, serialization, or render-output changes.
- No `RenderPipeline.cpp` split in this pass.
- No edits to generated asset blobs or third-party sources.

Validation:

- `cmake --build build --config Debug --target Stack StackGraphBehaviorTests` passed.
- `build\\StackGraphBehaviorTests.exe` passed.
- `build\\Stack.exe --validate-layer-registry` passed.
- `build\\Stack.exe --validate-tone-curve-auto` passed.
- `build\\Stack.exe --validate-develop-auto-solve` passed.
- `build\\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs and limitations:

- This is a navigation-first refactor, not a runtime optimization by itself.
- Some file-local helpers were duplicated or co-located with extracted methods to keep the split low-risk; a later cleanup pass can reduce duplication if it becomes a maintenance burden.
- `RenderPipeline.cpp` remains intentionally deferred because it has tighter performance and coupling concerns than these safer extraction targets.

## 2026-06-15 - Extract Manual RAW Decode Foundation and Restore Lean Standalone Tone Curve

Decision:

Add a separate `RAW Decode` node for the manual RAW-to-scene-linear foundation, restore standalone `Tone Curve` as a lean manual finish-curve node, and change RAW-source `Add full tree` to build `RAW Source -> RAW Decode -> Tone Curve -> View Transform -> Output`, while keeping `Develop` as the merged auto workflow.

Rationale:

The current `Develop` node is intentionally carrying the merged auto workflow with RAW decode, scene prep, and integrated finish tone. The user now needs a clean manual RAW chain that does not inherit unfinished auto-processing behavior. Splitting out `RAW Decode` gives the graph a stable manual RAW boundary without renaming or multi-moding `Develop`, and restoring standalone `Tone Curve` provides the manual finish-curve surface that naturally follows scene-linear decode before `View Transform`.

Implemented:

- Added `NodeKind::RawDecode` / `RenderGraphNodeKind::RawDecode` with editor/render payloads backed by `Raw::RawDevelopSettings`.
- Added `Graph::AddRawDecodeNode(...)`, `EditorModule::AddRawDecodeNodeAt(...)`, serialization as `"RawDecode"`, graph/browser/context-menu/sidebar/UI integration, and RAW-chain validation/traversal support.
- Factored the shared RAW base render path out of `RawDevelop` into a helper used by both `RawDecode` and `RawDevelop`.
- `RAW Decode` now renders only the RAW base stage through `RawGpuPipeline`: RAW normalization, white balance, demosaic, highlight reconstruction, orientation, camera transform, and exposure into unclamped scene-linear RGB.
- Added lean `RAW Decode` controls for source summary, white balance, `RAW Exposure / EV`, highlight reconstruction, demosaic/orientation status, and camera transform.
- Restored standalone `Tone Curve` as a first-class addable manual node, removed the legacy-only restriction, and slimmed its graph-node surface to the actual manual finish curve: curve editor, point editing/context menu, channel buttons, domain control, reset actions, and existing canvas targeting.
- Changed `EditorModule::AddFullRawTreeToSource(...)` to build `RAW Source -> RAW Decode -> Tone Curve -> View Transform -> Output`.

Explicit non-goals:

- No rename of `RawDevelop`, `RenderGraphNodeKind::RawDevelop`, serialized kind `"RawDevelop"`, or user-visible title `Develop`.
- No conversion of `Develop` into a mode switch between manual decode and merged auto processing.
- No change to `Develop` integrated finish-tone JSON ownership, hidden `preFinishImageOut`, scene prep, or Auto solve behavior.
- No Guide 09 Manual-to-Auto bias preservation, locks, or expert handoff controls.
- No full Guide 08 finish-tone redesign, tone graph controls, or shoulder/toe redesign inside `Develop`.

Validation:

- `cmake --build build --config Debug --target StackGraphBehaviorTests Stack` passed.
- `build\StackGraphBehaviorTests.exe` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-tone-curve-auto` passed.

Tradeoffs and limitations:

- `RAW Decode` reuses `Raw::RawDevelopSettings` as a pragmatic shared settings substrate. It is a separate node identity and workflow boundary, but not yet a deeply specialized manual RAW decode settings model.
- Standalone `Tone Curve` is intentionally lean again. It restores the manual finish-curve workflow without claiming the broader Guide 08 integrated Develop tone strategy.
- The new manual chain is explicit graph structure, not a Manual-to-Auto handoff system. It gives the user a clean manual path without teaching `Develop` how to preserve, lock, or re-interpret those edits yet.

## 2026-06-13 - Record Candidate Render Timing Telemetry

Decision:

Develop rendered candidate feedback should record compact timing telemetry for graph execution, readback, CPU metric analysis, hidden pre-finish work, total elapsed candidate time, and the slowest candidate.

Rationale:

Recent performance work reduced avoidable graph copying, but the next meaningful optimizations should be guided by measured cost. Candidate feedback can be slow for different reasons: shader/graph execution, texture allocation/readback, CPU pixel analysis, or pre-finish fallback rendering. Timing telemetry gives future passes and manual stress tests evidence before deeper renderer restructuring.

Implemented:

- Added timing fields to `DevelopCandidateRenderResult`.
- `EditorRenderWorker::RenderSnapshot` measures final graph execution, final readback, final analysis, pre-finish graph execution, pre-finish readback, pre-finish analysis, and total elapsed time with `std::chrono::steady_clock`.
- `ApplyDevelopCandidateRenderFeedback` writes per-candidate `CandidateRenderTimingV1` fields plus aggregate `autoCandidateRendered*Ms` totals and slowest-candidate diagnostics to integrated Tone JSON.
- `RenderRawDevelopControls` shows compact timing totals in Auto status.

Explicit non-goals:

- No full profiler UI, graph scheduler, sidecar stats bus, queue timeline, candidate gallery, or automatic scheduling policy.
- Timing diagnostics do not affect candidate scoring, convergence, or render output.
- No View Transform behavior changes.

Validation:

- Initial build hit `LNK1168` because `build\Stack.exe` was still running; after closing that local process, `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs and limitations:

- Timing uses coarse wall-clock measurements around existing worker steps. It is useful for direction but is not GPU timer-query telemetry.
- The Auto UI shows compact totals only; detailed per-candidate timing remains in integrated Tone JSON for diagnostics/future UI.

## 2026-06-13 - Reuse Candidate Graph for Pre-Finish Fallback

Decision:

Each Develop candidate feedback request should build one mutable candidate graph copy and reuse it for both the final output render and hidden pre-finish fallback render.

Rationale:

The candidate worker needs an isolated graph copy so it can apply the candidate `RawDevelop` payload and add synthetic output nodes without mutating the editor snapshot. However, the final render and the pre-finish fallback render are two output choices over the same candidate payload. Rebuilding the copied graph and finding/reapplying the candidate node for each socket adds avoidable CPU copy/setup churn during multi-candidate RAW feedback.

Implemented:

- `EditorRenderWorker::RenderSnapshot` now creates one mutable candidate graph per request after confirming the Develop node exists.
- The candidate payload and request revision are applied once to that copied graph.
- Separate final and pre-finish synthetic output nodes are attached once with distinct node ids.
- The final render and pre-finish fallback render reuse the graph by switching `outputNodeId`.

Explicit non-goals:

- No graph pooling, reusable graph builder, downscaled graph execution, GPU memory telemetry, sidecar scheduler, or candidate gallery.
- No removal of the one isolated mutable graph copy per candidate request.
- No change to `RawDevelop` identity, serialization, or View Transform behavior.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs and limitations:

- Keeping separate synthetic output node ids preserves final/pre-finish output-cache separation, but the graph now carries both synthetic outputs for the request. Only the selected output is rendered for each pass.
- This reduces repeated graph setup and copying; it does not address shader cost, texture allocation cost, or the remaining one graph copy per candidate.

## 2026-06-13 - Remove Redundant Candidate Worker Graph Copy

Decision:

The Develop candidate feedback worker should not clone the full render graph just to check whether a requested Develop node exists before rendering a candidate.

Rationale:

Candidate feedback can render several authored states per Auto solve, and each state may carry a large graph snapshot with RAW/Develop payload data. The actual candidate render needs a mutable graph copy so it can inject the candidate payload and synthetic output safely. The earlier preflight node-presence check did not need mutation, so copying the graph there created avoidable CPU allocation/copy churn during exactly the heavy RAW paths we are trying to stabilize.

Implemented:

- `EditorRenderWorker::RenderSnapshot` now checks `snapshot.graph.nodes` directly for the Develop node before candidate rendering.
- At the time, the actual candidate render still cloned `snapshot.graph` inside `renderCandidateSocket`, preserving isolated candidate mutation and existing render semantics. A later same-day decision now keeps one isolated mutable graph copy per candidate request and reuses it for final plus pre-finish fallback outputs.
- Existing Develop validations and real RAW smoke still pass.

Explicit non-goals:

- No candidate graph pooling or reusable mutable graph builder yet.
- No downscaled graph execution, GPU memory telemetry, sidecar scheduler, or candidate gallery.
- No change to `RawDevelop` node identity, serialization, or View Transform behavior.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs and limitations:

- This removed one avoidable graph copy per candidate request. A later same-day pass reduced repeated socket-render graph setup further by reusing one isolated mutable graph copy for final plus pre-finish fallback outputs. Future performance work should profile whether the remaining one graph copy per candidate, texture allocation, or shader execution is the dominant cost.

## 2026-06-13 - Name Active Develop Candidate Probes in Render Progress

Decision:

The existing non-blocking render progress HUD should name active Develop candidate-feedback probes using the candidate's human label and revision stage when that data is available.

Rationale:

Guide 03 candidate feedback can spend noticeable time rendering and measuring candidate states, especially on large RAW files. A count-only message such as `Measuring Develop feedback 2/4` confirms work is happening but does not tell the user what kind of solver probe is active. Including the label and revision stage makes the background work more understandable without building the full future candidate gallery or diagnostic timeline.

Implemented:

- Added a bounded `BuildDevelopCandidateProgressLabel` formatter and validation-facing wrapper.
- Normal and no-source/error candidate paths in `EditorRenderWorker::RenderSnapshot` now use the same formatter.
- Progress labels now include `N/M`, the candidate label, and the candidate revision stage when present.
- `ValidateDevelopAutoSolveBehavior` checks that labels are readable, include the stage, and stay bounded.

Explicit non-goals:

- No candidate thumbnail gallery, picker, side-by-side comparison, queue timeline, or requested-vs-achieved panel.
- No new render worker scheduling model and no blocking overlay.
- No View Transform behavior changes.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs and limitations:

- The HUD still shows only the current worker label. It does not expose every queued candidate, per-stage timing, stale-skip reason, or score-component explanation.
- Candidate labels are trimmed to keep the HUD readable.

## 2026-06-13 - Expose Candidate Feedback Waiting State in Develop Auto

Decision:

Develop Auto should show compact candidate-feedback waiting state in the existing Auto status readout when rendered candidate feedback is deferred by the recent-edit quiet window.

Rationale:

Guide 03 needs candidate rendering and feedback to continue after the user stops editing, but the stability work intentionally suppresses expensive candidate probes during rapid slider/intent changes. Without a visible status, that correct pause can look like a freeze or broken Auto analysis. A small inline status line keeps the behavior understandable while preserving normal viewport rendering and avoiding a modal loading overlay.

Implemented:

- Added a quiet-window remaining-time helper shared by validation and UI-facing status.
- Added `GetDevelopCandidateFeedbackDeferredStatus` so the Develop UI can ask whether the selected node has deferred candidate feedback and how long remains before feedback can resume.
- `RenderRawDevelopControls` now reports `Candidate feedback: waiting for edits to settle` with remaining seconds, then `queued after edits settled` until the deferred refresh dirties the node.
- `ValidateDevelopAutoSolveBehavior` covers remaining-time behavior inside and after the quiet window.

Explicit non-goals:

- No full Guide 10 diagnostic panel, graph visualization, candidate timeline, sidecar scheduler, or candidate gallery.
- No blocking progress modal and no View Transform behavior changes.
- No interrupting an already-running OpenGL graph execution.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs and limitations:

- The readout is intentionally compact. It explains the current wait/queued state, but it does not yet show candidate names, queue depth, stale-skip events, or requested-vs-achieved solver explanations.
- The status appears only while a node has deferred candidate feedback. Active candidate-probe progress still uses the existing render worker progress HUD and compact candidate render metrics line.

## 2026-06-13 - Suppress Candidate Render Admission During Recent Develop Edits

Decision:

Develop candidate render feedback should not schedule expensive candidate probes while the edited Develop node is still inside the per-node 0.60 second quiet window. The main viewport render may continue updating, but candidate feedback renders should wait until the authored state settles.

Rationale:

Rendered feedback was already gated on apply: stale interaction serials are dropped, and current-serial results are deferred during recent edits. That protected correctness, but the worker could still spend GPU time rendering candidate probes that could not be applied yet. Guide 03 wants iterative rendered feedback from the latest authored state, so scheduling candidate probes during active slider/intent edits is wasteful and contributes to the freezing/crash pressure the user observed.

Implemented:

- Added a shared quiet-window helper used by both feedback application and candidate render admission.
- `BuildDevelopCandidateRenderRequests` now skips candidate feedback requests for a Develop node when candidate metrics are needed but the node has been edited inside the quiet window.
- The skip schedules deferred Develop candidate feedback so `RefreshDeferredDevelopCandidateFeedbackIfReady` re-dirties the node and submits one fresh feedback pass after edits settle.
- `ValidateDevelopAutoSolveBehavior` now covers that admission is deferred inside the quiet window and opens after the quiet window.

Explicit non-goals:

- No interrupting an already-running OpenGL graph execution.
- No full background render queue, candidate gallery, user picker, sidecar stats bus, or graph diagnostics UI.
- No View Transform behavior changes and no `RawDevelop` identity/serialization changes.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Scoped `git diff --check` reported only line-ending warnings.

Tradeoffs and limitations:

- Candidate feedback appears after edits settle instead of during active dragging. This is intentional; the viewport still renders the authored state while candidate analysis waits.
- This avoids scheduling known-unusable probes, but it does not cancel a graph call already in progress.
- A later same-day pass surfaced this state in the Auto status readout. Future Guide 10 work should build richer diagnostics around candidate names, queue state, stale skips, and requested-vs-achieved explanations.

## 2026-06-13 - Bound RawDevelop Stage Snapshot Texture Residency

Decision:

RawDevelop raw-base and hidden pre-finish stage snapshots should remain useful owned cache boundaries, but their retention must be bounded by estimated RGBA16F texture size instead of only a fixed entry count.

Rationale:

Guide 03 needs cloned stage snapshots because `RawGpuPipeline` owns and reuses its output texture, and candidate renders can overwrite the persistent graph cache. However, every stage snapshot is a full-resolution RGBA16F texture. On large RAWs, a fixed six-entry cache per raw-base/pre-finish key can retain hundreds of megabytes or more of extra GPU texture memory during candidate churn. Stability matters more than cache-hit rate when images are large.

Implemented:

- Added a shared byte estimator for RawDevelop stage snapshots: `width * height * 8`, matching the RGBA16F storage used by `GLHelpers::CreateEmptyTexture`.
- Added validation-facing helpers on `RenderPipeline` for estimated bytes, size-aware per-key retention, and whether a stage snapshot should be cached at all.
- `storeRawDevelopStageCacheEntry` now preflights the render size before cloning. Small snapshots keep up to six MRU fingerprints per key; medium/large/huge snapshots keep three, two, or one; oversized single snapshots are skipped.
- After each stage snapshot store, the RawDevelop stage cache trims least-recent snapshots across all stage keys to a 512 MiB soft estimated-byte budget.
- `ValidateDevelopAutoSolveBehavior` covers the memory policy, including the oversized-snapshot skip case.

Explicit non-goals:

- No real GPU memory telemetry or driver-budget query.
- No downscaled candidate graph execution.
- No candidate thumbnail/gallery UI, user picker, sidecar stats bus, or standalone staged render controller.
- No View Transform behavior changes and no `RawDevelop` identity/serialization changes.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Scoped `git diff --check` reported only line-ending warnings.

Tradeoffs and limitations:

- Large RAWs may intentionally miss stage-cache reuse sooner than small RAWs. That can cost additional render time, but avoids retaining multiple huge full-resolution stage boundaries.
- The byte budget is an estimate of texture storage, not a driver-reported total memory budget.
- This reduces cache residency pressure; it does not interrupt expensive in-flight shader work or replace the existing worker scheduling model.

## 2026-06-13 - Cap Large-RAW Candidate Metric Readback

Decision:

Rendered Develop candidate feedback may cap final and hidden pre-finish metric readback on large RAW sources while still rendering the candidate through the normal graph at the actual source dimensions.

Rationale:

Guide 03 needs rendered feedback to compare authored candidate states, but candidate metrics are diagnostic probes, not user-facing gallery images. Reading full-resolution RGBA buffers for every final and pre-finish candidate can create large CPU allocations, long GPU readbacks, and UI stalls on high-megapixel RAW files. Capping the readback above 16 MP keeps feedback representative enough for solver metrics while reducing per-candidate memory pressure.

Implemented:

- Added capped, vertically flipped readback overloads for `RenderPipeline::GetOutputPixels` and `RenderPipeline::GetCachedGraphImagePixels`.
- `BuildDevelopCandidateRenderRequests` assigns a source-size-aware `metricReadbackMaxDimension`: uncapped below 16 MP, 1800 px above 16 MP, 1536 px above 30 MP, and 1280 px above 50 MP.
- `EditorRenderWorker` uses the cap for final candidate metrics and hidden pre-finish metrics, including cached pre-finish reuse and fallback pre-finish renders.
- Per-candidate and aggregate `autoCandidateRenderedMetricReadback*` diagnostics record the cap and downsample counts.
- The Develop Auto status readout shows capped metric readback only when capping was active.
- Validation covers the source-size policy through `ValidateDevelopAutoSolveBehavior`.

Explicit non-goals:

- No downscaled candidate graph execution; the candidate graph still renders at the real graph dimensions.
- No candidate thumbnail/gallery UI, user picker, sidecar stats bus, or full physical staged render controller.
- No View Transform behavior changes and no `RawDevelop` identity/serialization changes.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Scoped `git diff --check` reported only line-ending warnings.

Tradeoffs and limitations:

- Metrics on large RAWs become representative sampled metrics rather than full-pixel metrics. This is acceptable for current compact solver feedback but should be revisited before building a user-visible candidate gallery.
- GPU texture residency is still driven by the real candidate render size; future work should profile/limit texture pressure separately.
- Capping reduces readback and CPU analysis pressure but does not interrupt expensive in-flight shader work.

## 2026-06-13 - Skip Stale Develop Feedback When Newer Renders Are Queued

Decision:

Allow newer single-output editor snapshots to supersede stale in-flight Develop feedback work. The render worker should finish the current unavoidable GL graph operation, then skip obsolete Develop candidate feedback/previews and avoid publishing superseded output textures when a newer generation is already pending.

Rationale:

Guide 03 expects Auto feedback to iterate from the latest authored state. During rapid Develop edits, finishing every old candidate-feedback probe wastes GPU time, increases texture churn, and can make the editor feel frozen even though the user has already moved on. Skipping stale background feedback at safe worker boundaries improves responsiveness without inventing a second renderer or unsafe cross-thread OpenGL interruption.

Implemented:

- `EditorModule::SubmitRenderIfReady` can submit a replacement single-output snapshot while a previous render is still pending when the graph has become dirty again.
- `EditorRenderWorker::Submit` marks replacement work as `Queued newer render...` for the existing progress HUD.
- `EditorRenderWorker::ShouldAbortStaleSnapshot` detects a pending newer generation under the worker mutex.
- `RenderSnapshot` skips stale Develop candidate feedback and preview-like background work at safe boundaries, and avoids publishing superseded main/layer-stack shared textures after a newer snapshot is queued.
- Validation covers no-abort, same-generation pending, newer-generation pending, and shutdown abort cases.

Explicit non-goals:

- No interrupting an in-flight shader or `RenderPipeline::ExecuteGraph` call.
- No full background render queue, candidate gallery, user candidate picker, sidecar stats bus, or physical staged render controller.
- No View Transform behavior changes and no `RawDevelop` identity/serialization changes.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Scoped `git diff --check` reported only line-ending warnings.

Tradeoffs and limitations:

- Cancellation only happens at safe CPU-side boundaries after the current graph operation returns, so a single very expensive render can still occupy the worker until the active GL call finishes.
- The progress HUD is still approximate worker-step progress, not per-shader GPU progress.
- Future Guide 03/10 work should add richer queue/admission diagnostics and user-facing candidate/progress surfaces.

## 2026-06-13 - Add Refined Subject Map Diagnostics

Decision:

Develop Auto should write a compact, versioned `SubjectRefinedMapV1` record and selected-node viewport overlay as the next subject-importance diagnostic layer over `SubjectImportanceMapV1`.

Rationale:

Guide 05 asks for refined importance/confidence diagnostics and edge-aware interpretation. The current code already has user-authored regions/strokes, a compact interpreted map, rendered marked-region metrics, candidate scoring, and solve notes. A refined map gives the solver and UI a durable confidence/readability/protection/mood map that future image-edge refinement can extend, while avoiding a premature semantic detector or hard mask.

Implemented:

- Added `SubjectRefinedMapV1` with per-cell importance, confidence, readability, protection, mood-preservation, low-priority, and boundary-hint fields.
- `ResolveDevelopSubjectSceneIntent` now builds the refined map for pending and normal subject-intent states.
- `DevelopSubjectSceneIntentToJson` serializes `refinedImportanceMap` and scalar `refinedMap*` fields.
- `WriteDevelopAutoCandidateSolveDiagnostics` mirrors refined map data into top-level `autoSubjectSceneRefinedMap*` diagnostics.
- `BuildDevelopAutoCandidateScoreComponents` carries the refined map and signals, and refined confidence lightly biases subject-readable, protection, and mood scoring dimensions.
- Auto UI exposes visual-only `Show Refined Map` and `Refined Opacity` controls plus a compact status readout.
- The viewport draws the refined map beneath editable strokes/regions, preferring solved diagnostics and falling back to current-mark derived cells when needed.
- Validation covers graph serialization/defaults, visual-only fingerprint behavior, viewport state, top-level diagnostics, candidate score-component diagnostics, and real RAW smoke.

Explicit non-goals:

- No true image-edge refinement/segmentation, edge visualization, AI/semantic subject detection, graph-control UI, candidate gallery UI, Finish Mask reuse, final-pixel blending, or Manual/Auto subject-bias handoff.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs / limitations:

- Boundary hints are derived from mark/grid structure and neighbor contrast, not from image-edge analysis.
- The map is intentionally compact and conservative so it can guide candidate scoring without acting as a hard mask.
- Future Guide 05 work should refine `SubjectRefinedMapV1` with actual image-edge evidence instead of creating a parallel subject-map contract.

## 2026-06-13 - Add Subject Importance Solve Notes

Decision:

Develop Auto should write a compact, versioned `SubjectImportanceSolveNotesV1` record explaining how user subject/importance guidance affected candidate scoring.

Rationale:

Guide 05 asks for solve notes that teach the user what happened without requiring code knowledge. The current solver already has subject/scene intent axes, user-marked regions/strokes, an interpreted map, candidate scoring, and rendered subject-marked metrics, but the status readouts mostly exposed numbers. A bounded note record gives future diagnostic UI and current Auto status a human-readable explanation while keeping the data structured and versioned.

Implemented:

- Added `BuildDevelopSubjectSolveNotes` and `SubjectImportanceSolveNotesV1`.
- `ResolveDevelopSubjectSceneIntent` now builds notes for pending-evidence and normal solved subject/scene states.
- `DevelopSubjectSceneIntentToJson` serializes `solveNotesVersion` and `solveNotes` inside `SubjectSceneIntentV1`.
- `WriteDevelopAutoCandidateSolveDiagnostics` mirrors notes into top-level `autoSubjectSceneSolveNotesVersion`, `autoSubjectSceneSolveNotes`, `autoSubjectSceneSolveNoteCount`, and `autoSubjectScenePrimarySolveNote`.
- `BuildDevelopAutoCandidateScoreComponents` carries the same notes inside each candidate's `subjectSceneIntent` score-component record.
- Auto status shows up to two `Subject note:` lines near the existing Subject / Scene and Importance Map diagnostics.
- Validation checks that region-guided subject importance writes nested notes, top-level note aliases, a primary note, and candidate score-component notes.

Explicit non-goals:

- No edge-aware refinement, refined importance/confidence map, edge visualization, semantic/AI subject detection, graph-control UI, candidate gallery UI, candidate explanation panel, Finish Mask reuse, final-pixel blending, or Manual/Auto subject-bias handoff.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Scoped `git diff --check` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- Notes are intentionally compact and heuristic. They explain the active bias signals but do not yet explain every generated/rejected candidate decision.
- The status panel shows only the first two notes to stay readable. Future Guide 10 diagnostic UI can expose the full note array and relate it to candidate outcomes.
- The notes are generated from the current subject/importance intent record and existing score paths; they do not introduce a new subject detection system or new solver branch.

## 2026-06-13 - Add Compact Interpreted Map Viewport Diagnostic Overlay

Decision:

Develop should expose the existing non-edge-aware `SubjectImportanceMapV1` as a compact selected-node viewport diagnostic overlay before building the future edge-aware/refined map system.

Rationale:

Guide 05 asks for subject-importance diagnostics that show what the solver thinks the user marks mean. The current pass already had a durable 5x5 interpreted map contract in solver diagnostics, but users could only see the authored marks and text readouts. Drawing the compact grid makes the current interpretation inspectable without inventing a second mask format, without claiming semantic detection, and without waiting for the larger edge-aware refinement work.

Implemented:

- Added visual-only `showInterpretedMapOverlay` and `interpretedMapOpacity` fields to `DevelopSubjectImportanceMap`.
- Graph JSON serializes/deserializes the new display fields; old graphs default the interpreted map overlay off.
- `GetDevelopSubjectImportanceViewportState` copies `InterpretDevelopSubjectImportanceMap` cells into `DevelopSubjectViewportMapCell` records when the diagnostic overlay is enabled.
- Auto UI exposes `Show Interpreted Map` and `Map Opacity` controls near the existing subject overlay controls.
- `EditorViewport` draws the compact 5x5 map under editable strokes and regions. Cell color follows the dominant interpreted channel, including low-priority/reduce marks.
- Validation checks serialization/defaulting, viewport-state assembly, and that visual-only overlay/map settings do not change the Auto candidate context fingerprint.

Explicit non-goals:

- No edge-aware refinement, refined confidence/importance map, edge visualization, semantic/AI subject detection, graph-control UI, candidate gallery UI, Finish Mask reuse, final-pixel blending, or Manual/Auto subject-bias handoff.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Scoped `git diff --check` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- The overlay is intentionally coarse and uses the normalized authored region/stroke geometry. It is a diagnostic view of the current solver contract, not the future edge-refined subject map.
- The overlay is selected-node and single-output-preview scoped, matching the existing subject region/stroke overlay path.
- Display toggles are serialized so users keep their viewport preference, but they deliberately do not record RawDevelop interactions or force Auto reanalysis.

## 2026-06-13 - Add Compact Interpreted Subject Importance Map Contract

Decision:

Develop Auto should interpret existing user-authored subject regions and brush strokes into a compact solver map contract, `SubjectImportanceMapV1`, before building edge-aware refinement or visual diagnostic map views.

Rationale:

Guide 05 says painted subject importance should be interpreted rather than treated as a hard mask. The existing region/stroke data already captures user intent and rendered metrics already measure marked areas after candidate renders, but the solver lacked a durable spatial map record that future edge-aware refinement and graph diagnostics can extend. A bounded 5x5 grid gives Auto a cheap, stable, inspectable map substrate without pretending to solve the full refinement/UI problem.

Implemented:

- Added `SubjectImportanceMapV1` as a compact interpreted 5x5 grid from enabled `DevelopSubjectImportanceMap` regions and strokes.
- The map records per-cell importance, reveal, protect, preserve-mood, and low-priority weights. Reduce/ignore strokes remain explicit low-priority coverage rather than disappearing.
- `SubjectSceneIntentV1` now carries the nested map and scalar map coverage, positive/low-priority coverage, mode coverage, peak, confidence, center-bias, and edge-bias fields.
- Candidate score components and top-level integrated-tone diagnostics write `autoSubjectSceneImportanceMap*` and `autoRequestedSubjectImportanceMap*` aliases.
- The Auto status readout shows compact map status, coverage, peak, low-priority coverage, and center bias.
- Validation covers region marks, brush strokes, disabled strokes, and reduce/ignore strokes writing expected map diagnostics and score-component signals.

Explicit non-goals:

- No edge-aware refinement, visual diagnostic map view, edge visualization, semantic/AI subject detection, graph-control UI, candidate gallery UI, hard masking, Finish Mask reuse, final-pixel blending, or Manual/Auto subject-bias handoff.

Validation:

- First build attempt compiled but link failed with `LNK1168` because `build\Stack.exe` process `36600` was holding the executable. Stopped that workspace debug process and reran.
- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- `git diff --check -- src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- The grid is intentionally coarse and solver-facing. It is useful for diagnostics and candidate scoring, but it is not a refined subject mask.
- Cell sampling uses normalized authored geometry, not image edges. Future work should refine from this contract rather than create a parallel subject-map format.
- Map influence is conservative so user marks guide candidate ranking without bypassing clipping, noise, halo, mood, or color safeguards.

## 2026-06-13 - Add Compact Subject-Marked Rendered Metrics

Decision:

Develop rendered candidate feedback should measure user-marked subject-importance regions and brush strokes as compact sampled metrics, then feed those measurements into rendered scoring and relative comparison without turning the marks into hard masks.

Rationale:

Guide 05 says subject importance should influence candidate scoring and local/global tradeoffs, especially when the user has marked what matters. The existing region/stroke model already told the authored solver what the user wanted, but rendered feedback could not tell whether those marked areas actually landed well in a candidate. Bounded sampled metrics close that loop while preserving the current RawDevelop/render-worker architecture and avoiding the cost/scope of edge-aware interpreted maps.

Implemented:

- Added `DevelopSubjectMetricSampling` as a compact render-worker request copy of enabled subject regions and strokes.
- Added `BuildDevelopSubjectMetricSampling` to cap copied regions, strokes, and stroke points before candidate render analysis.
- Added subject-marked `RenderMetricsV1` fields for coverage, positive/reveal/protect/mood/low-priority coverage, marked luma, shadow/highlight/clipped fractions, contrast, readability, protection risk, mood-preservation score, and low-priority brightness pressure.
- Final and hidden pre-finish candidate metrics use the same sampling spec.
- Rendered metric JSON/readback, rendered metric distance, standalone rendered scoring, and selected-baseline relative comparison consume the new fields conservatively.
- Added validation for marked positive/reveal metrics, ignore/low-priority rendered pressure, no-sampling defaults, and metric-distance sensitivity.

Explicit non-goals:

- No edge-aware interpreted importance map, raster mask editor, diagnostic map view, semantic/AI detection, graph-control UI, candidate gallery UI, final-pixel blending, or Manual/Auto handoff.

Validation:

- First build attempt compiled but link failed with `LNK1168` because an existing `build\Stack.exe` process was holding the executable. Stopped that stale workspace process and reran.
- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs / limitations:

- Sampling is intentionally bounded and downsampled for large images. It is useful for candidate scoring/diagnostics but is not a full spatial quality map.
- Reduce/ignore marks soften overlapping positive marks in metric sampling; future edge-aware interpretation can define richer overlap semantics.
- The scorer uses the fields lightly so marked areas help resolve candidate decisions without bypassing clipping, noise, halo, color, or mood guardrails.

## 2026-06-13 - Add Basic Subject Brush Stroke Management

Decision:

Develop Auto should expose whole-stroke management for persisted subject-importance brush strokes before building edge-aware interpreted maps or graph-style subject controls.

Rationale:

Guide 05 treats the user-guided brush as the strongest honest signal for what matters. Once strokes are persisted, users need to inspect and correct them without clearing all painted guidance. Whole-stroke controls give the current brush system a practical editing loop while keeping the data model solver-facing and Develop-owned.

Implemented:

- Added Auto UI controls for each persisted stroke: select, enable/disable, delete, reduce/normal toggle, mode, strength, size, and soft edge.
- Selecting a stroke highlights it through `activeStrokeId` and copies its settings back to the brush tool so new painting can continue with the same intent.
- Disabled strokes are ignored by the requested/solved stroke summary, while reduce-only strokes now count as active user guidance through low-priority/ignore pressure.
- Added validation for disabled-stroke ignoring and reduce-stroke active guidance/score diagnostics.

Explicit non-goals:

- No point-level stroke editing, stroke history/undo stack, edge-aware interpreted map, diagnostic map view, semantic/AI detection, graph-control UI, candidate gallery UI, final-pixel blending, or Manual/Auto handoff. A later same-day pass added compact subject-marked rendered metrics as sampled solver feedback; edge-aware maps and diagnostic views remain non-goals for this older decision.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Scoped `git diff --check` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- Management is at whole-stroke granularity. This is intentionally simpler than editing individual points or building a raster mask editor.
- Reduce strokes remain solver-bias guidance, not eraser geometry over a final interpreted map. Future edge-aware interpretation should decide how overlapping positive and reduce strokes combine spatially.

## 2026-06-13 - Add Persisted Subject Importance Brush Strokes

Decision:

Develop Auto should store viewport-painted subject-importance brush marks as bounded normalized stroke paths inside `DevelopSubjectImportanceMap`, using the same stable mode vocabulary as soft regions plus a subtract/reduce flag.

Rationale:

Guide 05 treats the user-guided brush as the strongest honest signal for what matters in the image. The existing region overlay made subject guidance spatial, but it still forced users to author ellipses. Persisted brush strokes let users paint important, reveal, protect, preserve-mood, or reduce/ignore guidance directly in the viewport while keeping the data Develop-owned and solver-facing. The strokes bias Auto solving and candidate scoring; they are not hard masks and do not replace Finish Mask.

Implemented:

- Added `DevelopSubjectImportanceStroke` and `DevelopSubjectImportanceStrokePoint` to `DevelopSubjectImportanceMap`, including stable mode, enabled, subtract/reduce, radius, feather, strength, and bounded normalized points.
- Added graph JSON round-trip/default/fallback behavior for brush settings, active stroke, next stroke id, and stroke paths.
- Added Auto UI controls for Brush Edit, Reduce, Brush Mode, Brush Size, Brush Strength, Brush Soft Edge, and Clear Brush.
- Added `EditorModule` viewport brush APIs for begin/append/end stroke. Dragging records RawDevelop interaction feedback gating; the expensive Auto reanalysis/render dirty mark happens when a stroke ends.
- Added viewport drawing for soft stroke overlays and a brush cursor, with brush edit mode taking precedence over region selection.
- Added stroke counts and mode weights into `SubjectSceneIntentV1`, requested/top-level diagnostics, Auto solve trigger hashes, candidate fingerprints, and score-component signals.
- Added validation for stroke serialization/default/fallback behavior and for brush strokes affecting solve diagnostics and candidate context fingerprints.

Explicit non-goals:

- No edge-aware interpreted importance map, diagnostic map view, semantic/AI detection, graph-control UI, candidate gallery UI, Finish Mask reuse, final-pixel blending, per-stroke management UI beyond clear-all, or Manual/Auto handoff. A later same-day pass added compact subject-marked rendered metrics as sampled solver feedback; edge-aware maps and diagnostic views remain non-goals for this older decision.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs / limitations:

- Strokes are normalized paths with soft radius and strength, not raster masks. This is enough for current solver bias and viewport feedback, but future edge-aware interpretation should build a refined importance map from these strokes plus image structure.
- Reduce/erase is represented as subtractive strokes and clear-all for now. Per-stroke deletion, editing, history, and true eraser semantics remain future UI work.
- Brush settings themselves do not force a solve until they produce or change a stroke; visual overlay visibility/opacity stays out of solver hashes.

## 2026-06-13 - Add Viewport Editing for Subject Importance Regions

Decision:

Develop Auto should expose the existing `DevelopSubjectImportanceMap` regions directly in the single-output viewport as soft editable ellipses before building the full freehand brush system. The map now persists `activeRegionId` so the sidebar and viewport share the same selected region.

Rationale:

Guide 05 treats the importance brush as a major user-guided signal, but the current shipped substrate was still sidebar-only. Drawing and editing the same region records in the viewport makes the guidance spatial and usable without inventing a second mask model or reusing Finish Mask. This moves the system toward the brush direction while preserving the constraint that subject importance is a solver bias, not a hard cutout.

Implemented:

- Added `activeRegionId` to `DevelopSubjectImportanceMap`, graph JSON serialization/deserialization, safe old-graph defaults, invalid-id fallback, and smoke validation.
- Added Auto UI controls for overlay visibility, overlay opacity, and active-region selection.
- Added `EditorModule` viewport APIs to read selected-Develop overlay state, set the active region, and update region center/size from viewport gestures.
- Added single-output viewport overlay drawing with mode-colored soft ellipses, active-region emphasis, disabled-region faint display, select/move interaction, and edge resize interaction. Geometry edits record RawDevelop interaction, force Auto reanalysis when metadata is available, and mark the Develop render dirty.
- Removed purely visual overlay visibility/opacity from the subject-importance solver hash so display settings do not masquerade as changed image intent.

Explicit non-goals:

- No freehand brush painting, erase/reduce workflow, edge-aware interpreted maps, visual diagnostic map view, AI/semantic subject detection, graph-control UI, candidate gallery UI, Finish Mask reuse, final-pixel blending, or Manual/Auto handoff. A later same-day pass added compact subject-marked rendered metrics as sampled solver feedback; edge-aware maps and diagnostic views remain non-goals for this older decision.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Scoped `git diff --check` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- The overlay edits simple normalized elliptical regions. It is spatial and direct, but it is not a painted mask, does not refine to image edges, and cannot yet use separate erase/reduce strokes.
- The viewport overlay is available for the selected Develop node in single-output preview. Candidate gallery and diagnostic-map surfaces remain future work.

## 2026-06-13 - Add Subject Importance Region Guidance

Decision:

Develop Auto should store subject-importance guidance as Develop-owned soft regions before building the full freehand brush/overlay system. The region map uses stable mode vocabulary: `Important`, `Reveal`, `Protect`, `PreserveMood`, and `Ignore`.

Rationale:

Guide 05 says subject importance should be user-guided and confidence-weighted, but not a hard command that overrides the whole scene. A persisted region model gives Auto a concrete user-intent substrate that can bias the existing solver, candidate fingerprints, and diagnostics without reusing Finish Mask semantics or pretending a full painted, edge-aware, semantic subject map exists.

Implemented:

- Added `DevelopSubjectImportanceMap`, `DevelopSubjectImportanceRegion`, and `DevelopSubjectImportanceMode` to the `RawDevelopPayload`.
- Added graph JSON serialization/deserialization with old-graph disabled/no-region defaults and unknown-mode fallback to `Important`.
- Added Auto UI region controls for enable/add/clear/delete, mode, strength, center, size, and soft edge.
- Added normalization plus summary weighting into `SubjectSceneIntentV1`, requested diagnostics, candidate score components, trigger hashes, candidate context/guidance fingerprints, and status readouts.
- Added validation for region-guided solve diagnostics, region-driven context fingerprint changes, serialization round-trip, legacy defaulting, and unknown-mode fallback.

Explicit non-goals:

- No freehand viewport brush painting, actual soft overlay rendering, erase/reduce paint workflow, edge-aware interpreted maps, AI/semantic subject detection, graph-control UI, candidate gallery UI, Finish Mask reuse, final-pixel blending, or Manual/Auto handoff.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs / limitations:

- The UI currently edits simple soft elliptical regions instead of painting directly in the viewport. This is intentional substrate: future passes should build the brush/overlay on top of `DevelopSubjectImportanceMap`, not invent a separate subject mask model.
- Region guidance is summarized into solver biases and diagnostics. It is not a hard mask and does not claim semantic subject understanding.

## 2026-06-13 - Add Subject Intent Candidate Probes

Decision:

Develop Auto should turn `SubjectSceneIntentV1` and the user subject/scene axes into named authored candidate probes before building the future brush or candidate gallery. The first probes are `subjectReadableMids` and `sceneMoodPreservation`.

Rationale:

Guide 05 says subject importance should generate alternatives when treatment is ambiguous, such as revealing a likely subject versus preserving silhouette or low-key mood. The existing axes and score dimensions proved intent could be recorded, but they did not create clear candidate alternatives. These probes make the solver actually test both sides through the current authored candidate/render-feedback architecture.

Implemented:

- Added `subjectReadableMids` as a Scene Prep probe that opens likely or user-marked important mids while preserving highlight/noise/halo guardrails.
- Added `sceneMoodPreservation` as a Scene Prep counter-probe that preserves scene hierarchy, silhouette, or low-key mood instead of automatically lifting every likely subject.
- Added scoring, mode-intent fit, score-component diagnostics, duplicate preservation, rendered-continuation relevance, render-request diversity priority, scene-prep stage classification, and stage-constrained render-payload mapping for both probes.
- Extended validation so both probes must generate, remain eligible, write subject diagnostics, and render as Scene Prep candidates with RAW placement frozen.

Explicit non-goals:

- No user importance brush, brush modes, paint storage, overlay, edge-aware maps, semantic/AI detector, graph-control UI, candidate gallery UI, final-pixel blending, or Manual/Auto handoff.
- No claim that these probes locate a precise subject. They are authored solver alternatives driven by weak automatic evidence and/or user intent axes.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs / limitations:

- The probes are Scene Prep branches over the current RawDevelop architecture. They do not store a mask, and they cannot yet treat separate painted regions differently.
- Because Scene Prep values can hit clamps on extreme fixtures, validation checks branch-specific non-cap fields and stage tags rather than assuming every requested bias can increase numerically without bounds.

## 2026-06-13 - Add User-Guided Subject / Scene Intent Axes

Decision:

Develop Auto should expose first-pass user-guided subject/scene intent as two durable guidance axes: `subjectSceneBias` and `moodReadabilityBias`. They are solver-bias controls, not painted masks or graph widgets.

Rationale:

Guide 05 says subject importance should be user-guided and confidence-weighted, while also warning against subject logic becoming a dictator. The existing `SubjectSceneIntentV1` automatic-only substrate gave Auto a place to record weak subject/scene evidence. Adding neutral-default user axes lets the user guide the solver toward global scene integrity versus likely subject priority, and preserve mood versus improve readability, without pretending Stack has the future importance brush, edge-aware map, or semantic detector.

Implemented:

- Added `DevelopAutoGuidance::subjectSceneBias` and `DevelopAutoGuidance::moodReadabilityBias`, with graph JSON serialization, old-graph neutral defaults, and validation round-trip coverage.
- Added Auto UI controls for `Subject / Scene Intent` and `Mood / Readability`, with tooltips and compact status/readout coverage.
- Included the axes in Auto solve trigger hashes, candidate context/guidance fingerprints, candidate guidance JSON, rendered-feedback guidance distance/readback, authored tone JSON guidance, and candidate learning records.
- Extended `SubjectSceneIntentV1` to record active user intent controls with `userGuidanceStatus = "intentControls"`, user bias values, user guidance strength, and non-automatic status.
- Let the axes conservatively bias candidate scoring and score-component diagnostics without bypassing clipping, noise, halo, range, or mood safeguards.
- Recorded Auto Mode / Intent, Auto Calibrate, Reset Auto, and committed Auto slider edits through the RawDevelop interaction gate so rendered feedback waits for recent edits to settle.

Explicit non-goals:

- No user importance brush, brush modes, paint storage, soft overlay, edge-aware subject maps, visual diagnostics, AI/ML subject detection, face/person/object detection, graph-control UI, candidate gallery, final-pixel blending, or Manual/Auto subject-bias handoff.
- No claim that the new axes create a hard mask or semantic subject map. They are authored solver intent.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- `git diff --check -- src/Editor/NodeGraph/EditorNodeGraph.h src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- Sliders are a bridge, not the final Guide 10 graph-style Subject / Scene Intent Map. Future graph controls should reuse these axes instead of inventing a parallel model.
- The axes can bias candidate scoring, but without brush data they still cannot locate or refine a specific subject region.
- Reset Auto resets these axes to neutral while preserving the selected Auto mode, because these axes are adjustable guidance values and the mode is the broader user intent.

## 2026-06-13 - Add Subject / Scene Intent Foundation

Decision:

Develop Auto should carry `SubjectSceneIntentV1`, a compact automatic-only subject/scene intent record derived from rendered candidate evidence. The record should bias the existing solver conservatively, expose diagnostics, and reserve vocabulary for future user-guided subject/scene controls without claiming a brush, semantic map, or detector exists.

Rationale:

Guide 05 treats subject importance as a confidence-weighted bias, not an override that blindly lifts or protects one region at the expense of the whole scene. Earlier Guide 04 work created dynamic-range and local-exposure strategies that refer to readable shadows, protected highlights, and mood. Those signals needed a named subject/scene substrate before future passes interpret them as subject-aware behavior.

Implemented:

- Added compact rendered subject/scene metrics for center/detail prior, readability pressure, protection pressure, mood-preservation pressure, and subject-importance confidence.
- Serialized/read those metrics through `RenderMetricsV1` and carried them into `DynamicRangeRegionEvidenceV1`.
- Added `SubjectSceneIntentV1` with subject/scene and mood/readability axes, confidence, subject priority, readability, protection, mood-preservation, `automaticOnly = true`, `userGuidanceStatus = "notAvailable"`, and deferred brush status.
- Used the intent as a conservative candidate-scoring bias and wrote score-component dimensions/risks for subject priority, readability, protection, mood, over-lift, and tradeoff pressure.
- Added compact Auto diagnostics, status readout coverage, and synthetic validation fixtures.

Explicit non-goals:

- No user importance brush, brush modes, soft overlay, edge-aware subject maps, visual diagnostics, AI/ML subject detection, face/person/object detection, graph-control UI, candidate gallery, final-pixel blending, or Manual/Auto subject-bias handoff.
- No claim that the weak automatic prior understands semantic subjects. It is compact rendered evidence for solver bias only.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- `git diff --check -- src/Editor/EditorRenderWorker.h src/Editor/EditorRenderWorker.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md docs/engineering/develop/DEVELOP_SOURCE_MAP.md docs/engineering/develop/DEVELOP_DECISIONS.md docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- The automatic prior is intentionally weak and can be wrong. It should not be made authoritative until user-guided importance data and better spatial maps exist.
- Future Guide 05 work should start from `SubjectSceneIntentV1` instead of creating a parallel subject/scene data model.

## 2026-06-13 - Add Compact Local Exposure Damage Profile Evidence

Decision:

Develop Auto should distinguish compact local exposure failure modes in rendered evidence: highlight crowding, shadow crowding, halo stress, flatness risk, and aggregate local exposure damage risk. These fields should flow from rendered candidate metrics into `DynamicRangeRegionEvidenceV1`, `DynamicRangeStrategyV1`, `LocalExposureStrategyV1`, score diagnostics, and the Auto status readout.

Rationale:

Guide 04 asks local exposure to preserve believable range without halos, noise lift, fake HDR, or gray flattening. A single local-risk scalar is too vague for the solver to know whether to compress highlights, open shadows, raise halo guards, or back away from local redistribution. Splitting the compact rendered evidence into named risks gives later Guide 04/10 passes a durable handoff while staying inside the existing RawDevelop candidate/render-feedback architecture.

Implemented:

- Added `localExposureHighlightCrowding`, `localExposureShadowCrowding`, `localExposureHaloStress`, `localExposureFlatnessRisk`, and `localExposureDamageRisk` to rendered candidate metrics.
- Serialized the fields through `RenderMetricsV1`, read them back into Auto-side rendered metrics, and carried them into `DynamicRangeRegionEvidenceV1` / `DynamicRangeStrategyV1`.
- Used the profile to bias `LocalExposureStrategyV1`: raise useful local range/highlight/shadow pressure when evidence supports it, and reduce unsafe redistribution when halo or aggregate damage pressure is high.
- Added score-component signals/risks, duplicate-distance weighting, top-level diagnostics, Auto status readout coverage, and validation fixtures for the profile.

Explicit non-goals:

- No true spatial local-EV/noise/halo maps, visual overlays, subject-aware priority, graph controls, clipped-data recovery, new local exposure renderer, or new View Transform behavior.
- No claim that compact rendered metrics are a final perceptual model. They are bounded solver evidence until later spatial-map and diagnostics work exists.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- `git diff --check -- src/Editor/EditorRenderWorker.h src/Editor/EditorRenderWorker.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- The profile is compact and heuristic. It makes local exposure damage less opaque, but future Guide 04/10 work still needs true spatial maps, overlays, and richer local-exposure diagnostics.
- Without Guide 05 subject importance, shadow and highlight priorities are still based on rendered structure/readability evidence, not semantic subject understanding.

## 2026-06-13 - Add Local Exposure Strategy Contract to Guide 04

Decision:

Develop Auto should carry a durable local exposure strategy contract, `LocalExposureStrategyV1`, derived from `DynamicRangeStrategyV1`, `DynamicRangeStrategyMapV1`, stats, mode intent, and compact rendered regional evidence. The contract should author existing Scene Prep settings and candidate Scene Prep probes instead of remaining a diagnostic-only note.

Rationale:

Guide 04 says local exposure is scene preparation, not magic recovery. The solver needs to coordinate local range redistribution, highlight compression, shadow opening, noise protection, halo safety, and texture protection together. A named contract lets future passes continue local exposure work without adding another parallel set of hidden sliders or losing the difference between "show more range" and "avoid fake HDR/gray noisy shadows."

Implemented:

- Added `LocalExposureStrategyV1` fields for range redistribution, highlight compression, shadow opening, noise guard, halo guard, texture guard, shadow/highlight EV budgets, and strength target.
- Serialized the contract inside `DynamicRangeStrategyV1` and as top-level `autoDynamicRangeLocalExposure*` aliases.
- Applied the contract in `ApplyDevelopAutoSolve` so authored Scene Prep settings move across local EV limits, highlight protection, noise protection, shadow-lift limits, halo/gradient/edge protection, and texture sensitivity together.
- Carried the contract into candidate render payloads through `autoCandidateLocalExposure*` diagnostics and used it to bias existing broad-highlight, local-range, halo-safe, shadow-readability, and shadow-floor Scene Prep probes.
- Added a compact Auto status readout and validation for diagnostics, authored Scene Prep handoff, and candidate payload carry-through.

Explicit non-goals:

- No true spatial clipping/local-EV/noise/halo maps, visual overlays, graph controls, subject-importance brush, clipped-data recovery, new View Transform, or new `RawDevelop` identity.
- No new local exposure renderer; this is a solver contract over the current Scene Prep path.
- No claim that the contract can recover missing clipped data or understand semantic subject importance.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- `git diff --check -- src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- The contract is compact and heuristic. It makes local exposure intent durable and actionable, but true spatial evidence maps and graph controls still belong to later Guide 04/10 work.
- Without Guide 05 subject importance, "shadow visibility" still means readable-shadow evidence, not a user- or subject-aware priority.
- The Scene Prep renderer remains the existing renderer; future work can improve its internals while preserving this contract as the solver handoff.

## 2026-06-13 - Add Internal Dynamic Range Strategy Map Diagnostics

Decision:

Develop Auto should carry an internal dynamic-range strategy map that matches Guide 04's future graph-control axes without building the graph UI yet. `DynamicRangeStrategyV1` now writes nested `DynamicRangeStrategyMapV1` coordinates for highlight priority versus shadow visibility and natural contrast versus maximum visible range.

Rationale:

Guide 04 names a two-axis strategy map because a single Dynamic Range slider cannot describe whether Auto should protect the sky, reveal a subject, keep dramatic contrast, or maximize visible range. The current implementation already has compact highlight, shadow, halo, gray-highlight, and local-EV evidence. The map turns that evidence into a stable solver coordinate system that can bias existing candidates now and later become a natural input/output contract for graph-style controls.

Implemented:

- Added `DynamicRangeStrategyMapV1` fields to `DevelopDynamicRangeStrategy` and serialized them into integrated tone JSON as a nested `strategyMap` plus top-level `autoDynamicRangeStrategyMap*` aliases.
- Derived `highlightShadowAxis`, `contrastRangeAxis`, `highlightPriority`, `shadowVisibility`, `naturalContrast`, and `visibleRange` from existing stats, mode intent, rendered regional evidence, and dynamic-range strategy pressures.
- Used the derived weights to conservatively bias existing candidate generation/scoring for broad-highlight protection, readable shadows, local range guard, maximum visible range, natural contrast guard, and flatter editing tone.
- Forwarded the map into `ParameterScoreComponentsV1` and added score dimensions for strategy highlight/shadow/range/contrast fit.
- Added an Auto status diagnostic line for the two map axes and validation that the map JSON, aliases, and score components are written.

Explicit non-goals:

- No graph-control UI, draggable point, visual map, candidate gallery, subject-importance brush, true spatial clipping/local-EV/halo maps, or clipped-data recovery.
- No new render algorithm, View Transform behavior, `RawDevelop` identity change, or Guide 08 tone redesign.
- The map coordinates are solver diagnostics and candidate bias, not user-authored graph controls yet.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- `git diff --check -- src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- The map is compact and heuristic. It gives the solver a durable coordinate contract, but it does not yet expose user-adjustable graph controls or spatial evidence maps.
- Without Guide 05 subject importance, the shadow/subject side of the map is still based on readability and regional evidence rather than true subject semantics.
- Future Guide 10 work should reuse this record instead of creating a parallel graph-control data model.

## 2026-06-13 - Add Local EV Conflict Evidence to Guide 04 Strategy

Decision:

Develop Auto should use compact rendered local-EV conflict evidence when deciding whether local exposure needs a guarded Scene Prep probe. `RenderMetricsV1` now carries `localEvSpreadStops` and `localEvConflict`; `DynamicRangeRegionEvidenceV1` and `DynamicRangeStrategyV1` forward that evidence into local range strategy, candidate generation/scoring, diagnostics, and Auto status.

Rationale:

Guide 04 asks the solver to preserve believable local lighting and avoid fake HDR/halo behavior. The existing compact local metrics could say that a candidate had highlight pressure, shadow pressure, or halo risk, but they did not name the specific case where bright and dark local regions are fighting across several stops. A compact local-EV conflict signal gives `localRangeGuard` and `haloSafeLocalRange` a clearer reason to test controlled local redistribution instead of broad global range pressure.

Implemented:

- Added `localEvSpreadStops` and `localEvConflict` to `DevelopCandidateRenderMetrics`, rendered metric JSON/readback, rendered metric distance, and validation fixtures.
- Derived local-EV spread from the 3x3 rendered luma tile range, and derived local-EV conflict from local EV spread, mixed dark/bright tile coverage, local luma spread, local damage risk, edge contrast, and halo risk.
- Fed local-EV conflict into `DynamicRangeRegionEvidenceV1`, `DynamicRangeStrategyV1`, range compression pressure, local halo guard pressure, `localRangeGuard` strategy selection, `maximumRange` / `localRangeGuard` / `haloSafeLocalRange` scoring, candidate score-component signals/risks, and top-level diagnostics.
- Auto status now shows compact local range conflict, local-EV conflict, and local-EV spread.

Explicit non-goals:

- No true local-EV map, clipping/noise/halo map, visual overlay, subject-aware local exposure priority, graph control, or clipped-data recovery.
- No new render algorithm and no change to View Transform, `RawDevelop` identity, or serialized kind.
- No claim that the compact 3x3 evidence can locate every halo or local exposure failure.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- `git diff --check -- src/Editor/EditorRenderWorker.h src/Editor/EditorRenderWorker.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- The signal is deliberately compact and stable enough for the current candidate loop; true local-EV maps and overlays remain future Guide 04/10 work.
- The metric can steer candidate selection and scoring, but it cannot yet distinguish subject-critical local contrast from unimportant background contrast without Guide 05 subject-importance work.
- The validation covers synthetic local-EV evidence plus real RAW smoke, but hands-on UI stress testing with varied RAWs remains valuable before calling Guide 04 complete.

## 2026-06-13 - Add Meaningful Highlight Structure Evidence to Guide 04 Strategy

Decision:

Develop Auto should use compact rendered tile/structure evidence to distinguish broad meaningful highlight regions from tiny isolated glints. `RenderMetricsV1` now carries `highlightTileCoverage`, `highlightStructureScore`, and `meaningfulHighlightPressure`; `DynamicRangeRegionEvidenceV1` and `DynamicRangeStrategyV1` forward that evidence into highlight strategy, scoring, rendered feedback, and Auto status.

Rationale:

Guide 04 says highlight protection should be importance-aware and should distinguish small point-source clipping from large meaningful highlight failure. The current architecture does not yet have true spatial masks or subject semantics, but it already has rendered candidate pixels and 3x3 local summaries. A compact structure-pressure signal gives the solver a real next step: protect broad structured highlights more aggressively while still allowing tiny speculars to remain bright when that is the believable choice.

Implemented:

- Added meaningful-highlight fields to `DevelopCandidateRenderMetrics`, rendered metric JSON/readback, rendered metric distance, and validation fixtures.
- Derived `highlightTileCoverage` from a 3x3 highlight-band coverage pass, `highlightStructureScore` from highlighted tile contrast/coverage, and `meaningfulHighlightPressure` from area, coverage, structure, highlight brightness, gray risk, and a tiny-specular discount.
- Fed meaningful-highlight pressure into `DynamicRangeRegionEvidenceV1`, `DynamicRangeStrategyV1`, broad-highlight guard pressure, small-specular allowance gating, score components, rendered score, relative regression penalties, damage rejection, and rendered refine intent.
- Added top-level diagnostics `autoDynamicRangeMeaningfulHighlightPressure`, `autoDynamicRangeHighlightTileCoverage`, and `autoDynamicRangeHighlightStructureScore`.
- Auto status now shows compact `meaning` regional evidence, and validation separates gray-highlight, meaningful-highlight, and local spatial-risk fixtures.

Explicit non-goals:

- No true spatial clipping map, semantic highlight-importance map, subject detector, visual overlay, graph control, or clipped-data recovery.
- No new render algorithm and no change to View Transform, `RawDevelop` identity, or serialized kind.
- No claim that every broad highlight is important; this is a compact heuristic evidence signal until later spatial/subject passes exist.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- `git diff --check -- src/Editor/EditorRenderWorker.h src/Editor/EditorRenderWorker.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- The 3x3 structure evidence is deliberately compact; it helps the current solver but should be replaced or augmented by real local-EV/highlight/importance maps in later Guide 04 and Guide 10 work.
- Strong structured highlight pressure can preempt older generic spatial-hotspot refine reasons, so validation now uses separate fixtures for structured highlight pressure versus local spatial damage.
- The signal can steer broad-highlight protection but still cannot know semantic subject value without Guide 05 subject-importance work.

## 2026-06-13 - Add Rendered Highlight Grayness Evidence to Guide 04 Strategy

Decision:

Develop Auto should measure broad highlight brightness feeling from rendered candidate pixels, not only infer it from scalar highlight pressure or generic flat-gray risk. `RenderMetricsV1` now carries `highlightBandFraction`, `highlightMeanLuma`, `highlightLowSaturationFraction`, and `highlightGrayRisk`; `DynamicRangeRegionEvidenceV1` and `DynamicRangeStrategyV1` forward that evidence into range strategy, score diagnostics, and Auto status.

Rationale:

Guide 04 explicitly says bright things should stay bright and that gray highlight protection is a realism failure. The previous `luminousHighlightAnchor` and `naturalContrastGuard` candidates gave the solver actions to test, but the rendered feedback path needed direct evidence that a candidate's broad highlights were becoming dim and low-saturation. This keeps highlight detail, highlight brightness feeling, and whole-image flatness as related but distinct signals.

Implemented:

- Added highlight-band grayness fields to `DevelopCandidateRenderMetrics`, rendered metric JSON/readback, rendered metric distance, and validation fixtures.
- Added `highlightGrayRisk` to `DynamicRangeRegionEvidenceV1` and `DynamicRangeStrategyV1`, with top-level diagnostics `autoDynamicRangeHighlightGrayRisk`, `autoDynamicRangeHighlightBandFraction`, `autoDynamicRangeHighlightMeanLuma`, and `autoDynamicRangeHighlightLowSaturationFraction`.
- Fed highlight-gray evidence into brightness-hierarchy risk, natural-contrast guard need, bright-highlight rolloff need, luminous-highlight anchor need, candidate score components, rendered score, relative regression penalties, damage rejection, and rendered refine intent.
- Auto status now shows compact `gray` regional evidence beside highlight, shadow, and local-conflict pressure.

Explicit non-goals:

- No true spatial highlight map, highlight-importance map, subject-aware highlight priority, graph controls, or Guide 08 finish-tone redesign.
- No claim that fully clipped highlight detail is recovered.
- No new render algorithm and no change to View Transform, `RawDevelop` identity, or serialized kind.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- `git diff --check -- src/Editor/EditorRenderWorker.h src/Editor/EditorRenderWorker.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- The signal is compact rendered evidence over the current candidate output, not a semantic highlight mask.
- The risk is gated by broad rendered highlight presence to avoid treating ordinary low-key/dark images as gray-highlight failures.
- Severe gray-highlight rendered results can be rejected or steered toward contrast/highlight separation, but final tone-shape design still belongs to Guide 08.

## 2026-06-13 - Add Luminous Highlight Anchor as a Guide 04 Finish-Tone Candidate

Decision:

Develop Auto should be able to test a `luminousHighlightAnchor` candidate when protected broad highlights risk flattening toward gray. The candidate is a finish-tone constrained probe: it freezes RAW/global placement and Scene Prep, then tests downstream highlight character and contrast shape so bright regions still feel bright.

Rationale:

Guide 04 says highlight protection should preserve the feeling of light, not merely compress bright areas into gray. Existing branches could roll off bright highlights, protect broad highlight regions in Scene Prep, tolerate tiny speculars, or restore general contrast, but the solver needed a distinct branch for "these protected highlights still need a luminous anchor." `luminousHighlightAnchor` gives Auto that branch without changing View Transform behavior or claiming clipped-data recovery.

Implemented:

- Added `highlightBrightnessAnchorNeed` to `DynamicRangeStrategyV1` and top-level `autoDynamicRangeHighlightBrightnessAnchorNeed` diagnostics.
- Added the `Luminous Highlight Anchor` strategy/candidate path when highlight importance, broad-highlight pressure, brightness-hierarchy risk, and mode intent suggest protected highlights may be going gray.
- Added `luminousHighlightAnchor` candidate generation, scoring, mode-intent fit, `luminousHighlightAnchor` score dimension, `highlightBrightnessSignal`, duplicate-clustering preservation, continuation expansion, and rendered relevance for `protectHighlights` / `addContrast`.
- Candidate render payloads classify `luminousHighlightAnchor` as `finishTone`, freeze RAW/global settings and Scene Prep, and write `autoCandidateFinishToneProbe = "luminousHighlightAnchor"`.
- Auto status now shows compact `lum` pressure beside broad-highlight, readable-shadow, halo, separation, specular-highlight, and shadow-floor strategy values.

Explicit non-goals:

- No true highlight map, brightness-hierarchy map, subject-aware highlight priority, graph controls, or Guide 08 finish-tone redesign.
- No claim that fully clipped highlight detail is recovered.
- No new render algorithm and no change to View Transform, `RawDevelop` identity, or serialized kind.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- `git diff --check -- src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.

Tradeoffs / limitations:

- The branch is driven by compact scalar/rendered evidence, not true spatial highlight or subject maps.
- It is deliberately finish-tone-only during candidate renders, so it cannot hide upstream RAW or Scene Prep problems by moving earlier stages.
- This improves highlight brightness feeling inside Guide 04, but the full shoulder/toe/tone graph remains Guide 08/10 future work.

## 2026-06-13 - Add Halo-Safe Local Range as a Guide 04 Scene Prep Candidate

Decision:

Develop Auto should be able to test a `haloSafeLocalRange` candidate when compact rendered regional evidence warns that local exposure pressure may create edge glow, halos, or artificial relighting. The candidate is a Scene Prep constrained probe: it freezes RAW/global placement and finish-tone intent, then backs away from local max-EV pressure while raising existing anti-halo, smooth-gradient, and edge-aware guardrails.

Rationale:

Guide 04 treats local exposure as a believable-lighting problem, not a histogram-stretch problem. Existing Guide 04 candidates could protect broad highlights, open readable shadows, hold noisy shadows down, or shape finish tone, but the solver also needed a safety branch for "the local adjustment itself may be causing artifacts." `haloSafeLocalRange` gives Auto that branch without adding true spatial maps, a new render algorithm, or graph controls.

Implemented:

- Added `localHaloGuardNeed` to `DynamicRangeStrategyV1` and top-level `autoDynamicRangeLocalHaloGuardNeed` diagnostics.
- Added the `Halo-Safe Local Range` strategy/candidate path when rendered regional halo/local-range evidence crosses conservative thresholds.
- Added `haloSafeLocalRange` candidate generation, scoring, mode-intent fit, `localHaloSafety` score dimension, duplicate-clustering preservation, continuation expansion, and rendered relevance for local shadow/highlight refinement.
- Candidate render payloads classify `haloSafeLocalRange` as `scenePrep`, preserve RAW/global placement, preserve finish-tone intent through the stage constraint, reduce local max-EV pressure, and raise halo guard, smooth-gradient protection, edge awareness, and texture sensitivity.
- Auto status now shows a compact `halo` value beside broad-highlight, readable-shadow, separation, specular-highlight, and shadow-floor strategy values.

Explicit non-goals:

- No true halo map, local EV map, visual overlay, graph controls, or subject-aware local exposure priority.
- No Guide 07 denoise redesign, Guide 08 finish-tone redesign, or View Transform changes.
- No claim that clipped, halo-damaged, or noise-buried data can be recovered.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs / limitations:

- The branch uses compact rendered regional evidence and existing Scene Prep controls, so it is conservative and may miss localized halo issues that require a true spatial map.
- Validation accepts guard values that are already saturated at `1.0` as satisfying the intended safety increase.
- The rendered no-improvement-trend validation fixture was adjusted so the trend stop path is not preempted by the earlier stable-metrics stop, keeping both convergence branches covered.

## 2026-06-13 - Add Natural Contrast Guard as a Guide 04 Finish-Tone Candidate

Decision:

Develop Auto should be able to test a `naturalContrastGuard` candidate when range compression or flat-gray rendered evidence threatens believable lighting hierarchy. The candidate is a finish-tone constrained probe: it freezes RAW/global placement and Scene Prep, then tests downstream contrast and highlight-character shaping.

Rationale:

Guide 04 says dynamic range should not become fake HDR or gray compression. The existing solver could protect highlights, allow tiny speculars, open readable shadows, or hold noisy shadows down, but it needed an explicit positive branch for restoring separation after range compression. `naturalContrastGuard` gives Auto that option without changing View Transform behavior or pretending to implement the full Guide 08 finish-tone redesign.

Implemented:

- Added `naturalContrastGuardNeed` to `DynamicRangeStrategyV1` and top-level `autoDynamicRangeNaturalContrastGuardNeed` diagnostics.
- Added the `Natural Contrast Guard` strategy label/reason when flat-gray or brightness-hierarchy evidence asks for separation.
- Added the `naturalContrastGuard` candidate, scoring, mode-intent fit, `naturalContrastGuard` score dimension, duplicate-clustering preservation, continuation expansion, and rendered `addContrast` relevance.
- Candidate render payloads classify `naturalContrastGuard` as `finishTone`, freeze RAW/global settings and Scene Prep, and write `autoCandidateFinishToneProbe = "naturalContrastGuard"`.
- Auto status now shows a compact separation value beside broad-highlight, readable-shadow, specular-highlight, and shadow-floor strategy values.

Explicit non-goals:

- No full Guide 08 tone/contrast/finish redesign.
- No graph controls, candidate gallery, visual maps, subject-aware hierarchy scoring, or View Transform changes.
- No claim that clipped or noise-buried data can be recovered.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs / limitations:

- The candidate is driven by compact rendered flat-gray/brightness-hierarchy evidence, not true spatial hierarchy maps.
- It is deliberately finish-tone-only during candidate renders so it cannot hide an upstream exposure or Scene Prep problem by moving earlier stages.

## 2026-06-13 - Add Shadow Readability Lift as a Guide 04 Scene Prep Candidate

Decision:

Develop Auto should be able to test a `shadowReadabilityLift` candidate when clean, readable shadow evidence supports local opening. The candidate is a Scene Prep constrained probe: it keeps RAW/global placement stable while testing local shadow and midtone opening, and it stays separate from the shadow-noise-floor path that intentionally holds low-value dark regions down.

Rationale:

Guide 04 says dark areas should keep believable mood and noisy shadows should not be lifted into gray mush, but readable clean shadows also need a positive local-opening path. `shadowReadabilityLift` gives Auto that option without turning the RAW baseline into a brightness catch-all and without claiming subject-aware shadow maps or denoise recovery.

Implemented:

- Added `shadowReadabilityLiftNeed` to `DynamicRangeStrategyV1` and top-level `autoDynamicRangeShadowReadabilityLiftNeed` diagnostics.
- Added the `Shadow Readability Lift` strategy label/reason when shadow evidence is clean/readable enough to test local opening.
- Added the `shadowReadabilityLift` candidate, scoring, mode-intent fit, `shadowReadabilityLift` score dimension, duplicate-clustering preservation, continuation expansion, and rendered `openShadows` relevance.
- Candidate render payloads classify `shadowReadabilityLift` as `scenePrep`, freeze RAW/global settings, raise local shadow/midtone opening while keeping noise/halo guardrails active, and write `autoCandidateScenePrepProbe = "shadowReadabilityLift"`.
- Auto status now shows a compact readable-shadow value beside broad-highlight, specular-highlight, and shadow-floor strategy values.

Explicit non-goals:

- No true shadow/noise maps, subject-aware shadow classification, subject brush, graph controls, or Guide 07 denoise redesign.
- No claim that missing clipped or noise-buried data can be recovered.
- No new render algorithm and no change to View Transform, `RawDevelop` identity, or serialized kind.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs / limitations:

- The candidate is driven by compact scalar/rendered evidence, not a true spatial shadow or subject-importance map.
- The render payload deliberately keeps noise protection active, so this is a readable-shadow probe rather than a blanket shadow lift.

## 2026-06-13 - Add Broad Highlight Guard as a Guide 04 Scene Prep Candidate

Decision:

Develop Auto should be able to test a `broadHighlightGuard` candidate when broad meaningful bright regions need local highlight compression. The candidate is a Scene Prep constrained probe: it keeps RAW/global placement stable while testing local highlight shaping, and it stays separate from tiny-specular tolerance and from global exposure cuts.

Rationale:

Guide 04 distinguishes broad highlights that matter from tiny specular glints that can remain bright. The existing solver had finish-tone highlight rolloff and tiny-specular tolerance, but it needed a local Scene Prep option for "protect this broad bright area without dragging the whole image down." `broadHighlightGuard` gives Auto that option while preserving honest language: it can compress or preserve visible range, but it does not recover fully clipped data.

Implemented:

- Added `broadHighlightGuardNeed` to `DynamicRangeStrategyV1` and top-level `autoDynamicRangeBroadHighlightGuardNeed` diagnostics.
- Added the `Broad Highlight Guard` strategy label/reason when broad highlight evidence should be handled separately from tiny glints.
- Added the `broadHighlightGuard` candidate, scoring, mode-intent fit, `broadHighlightControl` score diagnostics, duplicate-clustering preservation, continuation expansion, and rendered-refine relevance.
- Candidate render payloads classify `broadHighlightGuard` as `scenePrep`, freeze RAW/global settings, lower local highlight min-EV shaping, keep highlight protection at least as strong as the base solve, and write `autoCandidateScenePrepProbe = "broadHighlightGuard"`.
- Auto status now shows a compact broad-highlight guard value beside specular-highlight and shadow-floor strategy values.

Explicit non-goals:

- No true clipping maps, highlight-importance maps, subject-aware highlight classification, graph controls, or Guide 08 finish-tone redesign.
- No claim that fully clipped highlight detail is recovered.
- No new render algorithm and no change to View Transform, `RawDevelop` identity, or serialized kind.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs / limitations:

- The candidate is driven by compact scalar/rendered evidence, not a true spatial highlight map.
- When Scene Prep highlight protection is already saturated, validation checks the local min-EV shift and stage constraints instead of requiring an impossible protection-bias increase.

## 2026-06-13 - Add Specular Highlight Tolerance as a Guide 04 Finish-Tone Candidate

Decision:

Develop Auto should be able to test a `specularHighlightTolerance` candidate when the current evidence looks like tiny point-source clipping rather than broad meaningful highlight failure. The candidate is a finish-tone constrained probe: it keeps RAW/global placement and Scene Prep stable while testing a brighter highlight-character/contrast shape around tiny glints.

Rationale:

Guide 04 says small specular clipping can be normal and that protecting every tiny highlight can make an image dull. Existing highlight candidates could protect broad highlights or shape luminous rolloff, but there was no explicit candidate for "these are tiny glints, do not drag the whole image down for them." `specularHighlightTolerance` gives Auto that option while keeping broad-highlight protection separate and while using honest clipped-data language.

Implemented:

- Added `specularHighlightToleranceNeed` to `DynamicRangeStrategyV1` and top-level `autoDynamicRangeSpecularHighlightToleranceNeed` diagnostics.
- Added the `Specular Highlight Tolerance` strategy label/reason when tiny-specular evidence is high and broad highlight pressure is low.
- Added the `specularHighlightTolerance` candidate, scoring, mode-intent fit, score-component diagnostics, duplicate-clustering preservation, continuation expansion, and rendered-refine relevance.
- Candidate render payloads classify `specularHighlightTolerance` as `finishTone`, freeze RAW/global settings and Scene Prep, and write `autoCandidateFinishToneProbe = "specularHighlightTolerance"`.
- Auto status now shows a compact specular-highlight tolerance value beside highlight/shadow/noise/floor strategy values.

Explicit non-goals:

- No true clipping maps, highlight-importance maps, subject-aware highlight classification, graph controls, or Guide 08 finish-tone redesign.
- No claim that fully clipped highlight detail is recovered.
- No new render algorithm and no change to View Transform, `RawDevelop` identity, or serialized kind.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs / limitations:

- The candidate is driven by compact scalar/rendered evidence, not a true spatial highlight map.
- Maximum Range / Detail and Flat Editing Base intentionally push back because those modes prefer more visible range and editing headroom; future Guide 04 work should use stronger spatial/subject evidence before making bolder local clipping decisions.

## 2026-06-13 - Add Shadow Noise Floor as a Guide 04 Scene Prep Candidate

Decision:

Develop Auto should be able to test a `shadowNoiseFloor` candidate when shadow lift is likely to turn noisy or low-value dark regions into gray mush. The candidate is a Scene Prep constrained probe: it keeps RAW/global placement and finish tone stable while reducing local shadow-opening pressure and increasing noise/halo protection.

Rationale:

Guide 04 says shadow lift, noise, tone, and mood are a coupled decision. A good Auto result may leave some shadows dark because lifting them is lower quality than preserving the dark floor. Existing candidates could clean shadows through RAW cleanup or darken the final toe through finish tone, but there was no explicit Scene Prep-local candidate for "do less local shadow opening here." `shadowNoiseFloor` gives the solver that option without pretending to recover data or implementing the full spatial map system.

Implemented:

- Added `shadowNoiseFloorNeed` to `DynamicRangeStrategyV1` and top-level `autoDynamicRangeShadowNoiseFloorNeed` diagnostics.
- Added the `Shadow Noise Floor` strategy label/reason when noise, shadow-lift risk, or dark intent makes held shadows preferable.
- Added the `shadowNoiseFloor` candidate, scoring, mode-intent fit, score-component diagnostics, duplicate-clustering preservation, continuation expansion, and rendered-refine relevance.
- Candidate render payloads classify `shadowNoiseFloor` as `scenePrep`, freeze RAW/global settings, lower scene-prep shadow-opening pressure, raise noise protection and shadow-lift limits, and write `autoCandidateScenePrepProbe = "shadowNoiseFloor"`.
- Auto status now shows a compact shadow-floor pressure value beside highlight/shadow/noise strategy values.

Explicit non-goals:

- No true shadow/noise maps, subject-aware shadow classification, subject brush, graph controls, or Guide 07 denoise redesign.
- No claim that underexposed shadows contain usable recoverable detail.
- No new render algorithm and no change to View Transform, `RawDevelop` identity, or serialized kind.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs / limitations:

- The candidate is driven by compact scalar/rendered regional metrics, not true spatial maps.
- It may be conservative in Bright Natural, Flat Editing Base, and Maximum Range / Detail because those modes intentionally prefer visibility; future Guide 04 work should use subject/importance evidence before making stronger local shadow decisions.

## 2026-06-12 - Feed Rendered Regional Evidence into Guide 04 Dynamic Range Strategy

Decision:

Develop Auto should use the existing compact rendered local metrics as `DynamicRangeRegionEvidenceV1`, feed that evidence into `DynamicRangeStrategyV1`, and generate a first Scene Prep constrained `localRangeGuard` candidate when local highlight/shadow/halo pressure needs a guarded range-shaping probe.

Rationale:

Guide 04 needs local highlight/shadow evidence before it can grow into a full local-exposure strategy. The current renderer already computes compact 3x3 local luma, pressure, and damage-risk summaries for candidate feedback, so the safest next step is to use that evidence as a small solver signal instead of inventing a parallel map system. `localRangeGuard` keeps the scope narrow: it tests a Scene Prep-local adjustment while preserving RAW/global placement and finish-tone intent.

Implemented:

- Added `DynamicRangeRegionEvidenceV1` from compact rendered candidate metrics and writes it into integrated tone JSON.
- Auto status now reports compact regional evidence alongside the existing dynamic-range strategy.
- Added `localRangeGuard` as a Guide 04 candidate, preserved it through duplicate clustering when it carries distinct local-range intent, and scored it with local-range evidence.
- Candidate render payloads treat `localRangeGuard` as a Scene Prep probe: RAW/global settings stay frozen, while local range/highlight/halo scene-prep biases get a small conservative nudge.
- Rendered feedback now accepts metrics from the immediate previous solve fingerprint when regional evidence changes the next preliminary fingerprint before feedback application, and repeated same rendered adoption stops with `renderedAdoptionNoFurtherGain`.

Explicit non-goals:

- No true clipping maps, highlight-importance maps, noise-risk maps, local EV maps, halo-risk maps, or visual overlays.
- No graph-style controls, subject-aware range priority, candidate gallery, user picker, learning application, or full iterative convergence engine.
- No claim that missing clipped data can be recovered.

Tradeoffs / limitations:

- The evidence is compact and metric-based, not a true spatial map.
- `localRangeGuard` is a narrow Scene Prep probe; it is not the complete Guide 04 local-exposure strategy.
- Allowing immediate previous-fingerprint rendered feedback is intentional for this regional-evidence loop, but future passes should keep watching stale-feedback diagnostics if the solver grows more asynchronous.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

## 2026-06-12 - Start Guide 04 with Dynamic Range Strategy and Bright Highlight Rolloff

Decision:

Develop Auto should write a durable `DynamicRangeStrategyV1` record for the current highlight/shadow/range strategy, show that strategy in the Auto status UI, and generate a first `brightHighlightRolloff` finish-tone candidate when highlight protection risks making bright areas feel dull or gray.

Rationale:

Guide 04 says dynamic range work should preserve believable lighting relationships, not chase a flat histogram. Bright things should still feel bright when possible, dark scenes should not be forced into gray mids, and small specular clipping can be acceptable when the broader image benefits. A compact strategy record gives later passes a place to extend highlight/shadow/local-exposure reasoning before building true maps or graph controls, while the `brightHighlightRolloff` candidate gives the current solver one concrete way to test downstream rolloff without changing RAW placement or Scene Prep.

Implemented:

- Added `DynamicRangeStrategyV1` diagnostics with strategy id/label/reason, highlight and shadow policies, highlight importance, shadow readability, noise constraint, range compression, brightness-hierarchy risk, bright-highlight rolloff need, and small-specular-clipping allowance.
- Added Auto status UI text for the selected range strategy.
- Added the `brightHighlightRolloff` finish-tone candidate and classified it as a finish-tone probe so candidate renders freeze RAW and Scene Prep.
- Added `brightnessHierarchy` to candidate score diagnostics and validation.

Explicit non-goals:

- No true clipping maps, highlight-importance maps, noise-risk maps, local EV maps, halo-risk maps, or visual overlays.
- No graph-style dynamic-range controls.
- No candidate gallery, user candidate picker, subject brush, learning application, full iterative convergence engine, or color graph.
- No full Guide 08 finish-tone redesign and no claim that missing clipped data can be recovered.

Tradeoffs / limitations:

- The strategy is heuristic and scalar, using existing stats/rendered feedback rather than spatial maps.
- `brightHighlightRolloff` is a conservative downstream tone-shape probe, not a complete highlight/shadow strategy.
- Future Guide 04 work should replace or augment this with true spatial/perceptual highlight and shadow evidence before exposing richer controls.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

## 2026-06-12 - Bound Heavy Develop Render Feedback and Show Non-Blocking Progress

Decision:

Ordinary editor rendering should expose lightweight progress without blocking interaction, and Develop rendered-feedback probes should narrow their diagnostic breadth on large sources before they can create avoidable GPU/CPU pressure.

Rationale:

Heavy RAW Develop rendering can look frozen when the worker is running multiple main/candidate/preview passes with no visible progress. The current candidate feedback path is diagnostic and solver-assistive; it should not render the same broad candidate set on a very large RAW as it would on a small source when doing so risks stalls or crashes. Separately, completed worker results own shared output textures, so stale completed results should not be retained alongside newer results longer than necessary.

Implemented:

- Added approximate `EditorRenderWorker::RenderProgress` tracking for main output, composite output, Develop rendered-feedback probes, and preview renders.
- Added a non-blocking editor render progress HUD via `ImGuiExtras::RenderProgressOverlay`.
- Released stale completed worker output textures before queuing a newer completed result.
- Drained unconsumed completed worker results during shutdown while the worker GL context is still current.
- Added source-size-aware limits for Develop rendered-feedback candidate requests. Large sources narrow diagnostic candidate renders while keeping the main viewport render unchanged.

Explicit non-goals:

- No candidate gallery, user picker, graph controls, View Transform changes, final-pixel candidate blending, or new render algorithm.
- No change to `RawDevelop` node identity or serialization.
- No claim that this completes the full Guide 03 scheduler/convergence engine.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.

Tradeoffs and limitations:

- The progress HUD is approximate. It reports worker steps, not per-shader GPU progress.
- Large-source throttling protects stability by reducing diagnostic rendered-feedback breadth; it may take a later settled render/solve pass to inspect more candidate alternatives.

## 2026-06-12 - Keep Editor Graph Access Out-of-Line and Throttle Startup Thumbnail Work

Decision:

`EditorModule` graph access used by external UI translation units must go through out-of-line methods compiled in `EditorModule.cpp`, and library thumbnail uploads must not synchronously decode large asset thumbnails before the main window is usable.

Rationale:

The Editor tab crash reproduced as an impossible `m_NodeGraph` node count inside the sidebar/node graph UI. Diagnostics showed `EditorModule.cpp` and `EditorSidebar.cpp` disagreed on the private `m_NodeGraph` offset by 48 bytes when using the inline accessor from the header. Centralizing the accessor removes that cross-translation-unit layout dependency. Separately, startup responsiveness was blocked by synchronous library texture upload/asset thumbnail decoding before `glfwShowWindow`, and some asset PNGs are large enough to make that look like a frozen app.

Implemented:

- Moved `GetNodeGraph`, const `GetNodeGraph`, `IsGraphOutputConnected`, and `ClearCompositeSelection` out of `EditorModule.h` and into `EditorModule.cpp`.
- Removed the pre-window `UploadLibraryTextures` call from `AppShell::Initialize`.
- Added a main-window shown timestamp and per-frame thumbnail upload budgets.
- Added `AssetEntry::thumbnailLoadAttempted` and an oversized-file guard so giant asset thumbnails are not decoded synchronously every frame.

Explicit non-goals:

- No new Develop solver behavior.
- No candidate gallery, graph controls, View Transform changes, or render algorithm rewrite.
- No change to RawDevelop node identity or serialization.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- Clean launch/click smoke reached the `Stack` window, clicked the Editor tab, and stayed responsive for 20 seconds.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs and limitations:

- Oversized asset cards may show the existing placeholder instead of a generated thumbnail. That is preferable to blocking startup or tab switching on full-size synchronous PNG decode.
- Future code should avoid re-inlining `GetNodeGraph` or other large-private-layout accessors in headers unless the layout mismatch is understood and verified across all UI translation units.

## 2026-06-12 - Synchronize Editor Render Worker Startup

Decision:

`EditorRenderWorker::Initialize` must not return until the worker thread has made its shared OpenGL context current, loaded GL functions, and initialized its persistent `RenderPipeline`.

Rationale:

The app could exit before the main window appeared after only part of the expected GL startup logging. Diagnostic traces showed the failure happened immediately after render-worker startup, and adding timing made the issue disappear. That points to a startup race between the main editor initialization path and the worker thread's shared OpenGL pipeline setup.

Implemented:

- Added worker initialization state: complete, succeeded, and error message.
- `Initialize` now waits on the worker condition variable for initialization completion.
- `ThreadMain` reports initialization success/failure after GL loader and persistent pipeline setup.
- Failed worker initialization now logs a concise error, joins the thread, destroys the worker window, and returns false so the editor can continue with `m_RenderWorkerAvailable = false`.

Explicit non-goals:

- No new Develop solver behavior.
- No candidate gallery, graph controls, View Transform changes, or render algorithm rewrite.
- No change to RawDevelop node identity or serialization.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- Seven repeated app launches reached a responsive `Stack` main window.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- `build\Stack.exe --validate-layer-registry` passed.

## 2026-06-12 - Gate Rendered Feedback During Develop Edits

Decision:

Rendered Develop candidate feedback must not author a follow-up Auto solve while the user is actively or recently adjusting Develop controls. RawDevelop raw-base and hidden pre-finish stage reuse must prefer cloned stage-cache snapshots over persistent graph-cache entries for those boundaries.

Rationale:

The current Develop Auto path can render candidates asynchronously and then write rendered metrics back into the Develop node. Without an edit-aware gate, a candidate result from the same broad solve can request a second full Auto solve immediately after a slider adjustment, which can look like every other edit flips the viewport dark or bright. Separately, RawGpuPipeline owns and reuses its output texture, so durable stage reuse must use cloned snapshots rather than trusting a persistent graph-cache entry that may point at a reused texture object.

Implemented:

- Added per-node Develop interaction serial tracking.
- Candidate render requests/results carry the interaction serial.
- `ApplyDevelopCandidateRenderFeedback` drops stale-serial results and defers current results during a 0.60 second quiet window after Develop edits.
- Deferred Develop candidate feedback schedules a fresh render after the quiet window instead of mutating the node from the old result.
- RawDevelop raw-base and hidden pre-finish boundary reuse now prefers cloned stage-cache entries.
- Integrated Develop ToneCurve and scene prep pass through the previous nonblank stage if they produce an effectively black output.

Explicit non-goals:

- No candidate gallery, user picker, graph controls, new View Transform behavior, new node identity, or new render algorithm.
- No change to the meaning of Auto/Manual mode.
- No claim that this completes Guide 03 convergence or Guide 10 diagnostics.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- `build\Stack.exe --validate-layer-registry` passed.

Tradeoffs and limitations:

- The automated validation covers the feedback-gate decision logic and existing synthetic Develop render/cache smoke paths, but it does not fully simulate a human drag session on every Auto slider.
- Candidate renders may still be computed during active edits, but their feedback is not allowed to mutate the Develop payload until a fresh post-quiet render is scheduled.

## 2026-06-11 - Use Convergence Evidence to Narrow Late Candidate Render Budgets

Decision:

Extend `AdaptiveRenderBudgetV1` so late, non-targeted continuation passes can narrow candidate render breadth from four to three focused renders when `ConvergenceEvidenceV1` says the solve is still waiting for fresh rendered metrics and `ConvergenceAdmissionV1` has already tightened admission.

Rationale:

Guide 03 defines convergence as evidence that further passes are no longer making meaningful improvements under the selected mode and user intent. The current solver already expands active continuation renders when there is a concrete repair target, responsible stage, adoption, or merge to validate. The opposite case also matters: if the loop is late, still awaiting metrics, and admission is already stricter, continuing to render the default breadth of broad alternatives wastes work and can keep exploratory families alive longer than the evidence justifies. A focused budget makes convergence evidence affect the next render request set before rendered feedback is applied.

Implemented:

- `ResolveDevelopAdaptiveRenderBudgetPolicy` now reads `autoCandidateConvergenceEvidence` and `autoCandidateConvergenceAdmissionTightened`.
- Late, non-targeted awaiting-metrics continuation passes narrow to three candidate renders with reason `convergenceEvidenceFocusedValidation`.
- Active refine, responsible-stage, adoption, and merge validation still use the existing expansion path up to six renders.
- Worker request/result diagnostics carry narrowed state plus convergence state/decision/reason.
- Rendered feedback writes `autoCandidateRenderedAdaptiveBudgetNarrowed*` and `autoCandidateRenderedAdaptiveBudgetConvergence*` diagnostics.
- Validation synthesizes the late awaiting-metrics state and checks the narrowed budget behavior.

Explicit non-goals:

- No full background render queue.
- No user-visible candidate gallery or thumbnails.
- No sidecar stats bus.
- No full candidate-family suppression policy.
- No applied learning or user candidate picker.
- No final-pixel blending.

Tradeoffs:

- The narrowed budget is conservative and only applies when there is no active repair/stage/merge/adoption target. This avoids starving targeted validation, but it means broad family suppression is still mostly budget-based rather than a full per-family policy.
- The threshold is intentionally three renders, not one or two, so the selected baseline plus a small number of meaningful alternatives can still be compared.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

## 2026-06-11 - Add Convergence Admission Policy

Decision:

Add `ConvergenceAdmissionV1` so the current Guide 03 convergence evidence can actively tune rendered-feedback admission thresholds for continued solves.

Rationale:

Guide 03 defines convergence as evidence that additional passes are no longer making meaningful improvements under the selected mode and user intent. The current loop already writes `ConvergenceEvidenceV1`, `RenderedContinuationV1`, pass counts, rendered feedback history, and no-improvement stops. Keeping that evidence purely diagnostic would still allow marginal rendered improvements to consume another pass after a continuation. A small admission policy makes the loop more convergence-aware without creating a parallel solver, candidate gallery, or background scheduler.

Implemented:

- Added `ResolveDevelopConvergenceAdmissionPolicy` in `src/Editor/EditorModule.cpp`.
- Continued non-refine solves that are waiting for fresh rendered metrics with pass > 0 now require a stronger improvement than the base rendered-feedback threshold.
- The first continuation raises the minimum improvement from `0.025` to `0.035`; later continuation raises it to `0.045`.
- Marginal candidates that clear the base threshold but fail the continuation threshold stop with `convergenceAdmissionNoMeaningfulImprovement`.
- The shared rendered-feedback stop classifier treats `convergenceAdmissionNoMeaningfulImprovement` as a converged/no-useful-improvement stop.
- Diagnostics write `ConvergenceAdmissionV1` as top-level `autoCandidateConvergenceAdmission*` fields and inside `autoCandidateConvergenceEvidence.admission`.
- Validation synthesizes a continued solve awaiting rendered metrics, injects a marginal rendered challenger, and checks the tightened threshold, stop reason, convergence state, and admission diagnostics.

Explicit non-goals for this pass:

- No full scheduled convergence engine or background render queue.
- No candidate gallery, thumbnails, hover/side-by-side preview, or user picker.
- No sidecar stats bus.
- No final-pixel blending.
- No applied user preference-learning system.
- No graph-style diagnostics UI.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs and limitations:

- The thresholds are intentionally conservative and only affect non-refine continuation admission. Refine intents continue to use their dedicated repeated-refine and monotonic risk guards.
- This is the first active controller use of `ConvergenceEvidenceV1`; future passes should extend this admission policy for stage-specific thresholds, render-budget tuning, and candidate-family suppression instead of creating another convergence controller.

## 2026-06-11 - Add Convergence Evidence Summary

Decision:

Add `ConvergenceEvidenceV1` as the current Guide 03 convergence-controller summary over the existing parameter and rendered-feedback loop.

Rationale:

Guide 03 defines convergence as evidence that more passes are not making meaningful improvements under the selected intent, not simply a repeated fingerprint or a histogram target. The current implementation already had several useful stop/continue signals: parameter fingerprint stability, rendered feedback stop reasons, stability distance, trend history, monotonic risk guards, pass limits, and `RenderedContinuationV1`. Those signals were spread across branch-local diagnostics. A single evidence record gives future passes one durable contract to extend before building a fuller scheduler.

Implemented:

- Added `BuildDevelopAutoConvergenceEvidenceRecord` in `src/Editor/EditorModule.cpp`.
- Wrote `autoCandidateConvergenceEvidenceVersion`, `autoCandidateConvergenceState`, `autoCandidateConvergenceDecision`, `autoCandidateConvergenceReason`, `autoCandidateConvergenceShouldContinue`, and nested `autoCandidateConvergenceEvidence` JSON.
- The evidence record summarizes parameter solve stability, rendered metric readiness, rendered feedback applied/stopped state, rendered stop convergence classification, stability/trend/monotonic evidence, pass budget, loop state, and `RenderedContinuationV1` decision/stage/evidence.
- Added validation for awaiting metrics, active continuation after rendered adoption, stable rendered convergence, trend/no-improvement convergence, pass-limit stopped state, and no-rendered-best stopped state.

Explicit non-goals for this pass:

- No full scheduled convergence engine or background render queue.
- No candidate gallery, thumbnails, hover/side-by-side preview, or user picker.
- No sidecar stats bus.
- No final-pixel blending.
- No applied user preference-learning system.
- No graph-style diagnostics UI.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs and limitations:

- `ConvergenceEvidenceV1` is currently a controller summary and extension point. It does not yet tune thresholds, schedule background passes, or replace the existing stop/apply branch logic.
- Future passes should extend this evidence record and then use it to drive admission/threshold decisions rather than creating another parallel convergence summary.

## 2026-06-11 - Add Continuation-Aware Candidate Expansion

Decision:

Add `ContinuationCandidateExpansionV1` as a bounded candidate-family generation hook driven by the existing `RenderedContinuationV1` policy.

Rationale:

Guide 03 expects rendered feedback to affect what the next Auto solve tries, not only how existing candidates are ranked. `ContinuationCandidateBiasV1` gave the current solver a safe scoring nudge, but when first-pass scene stats did not emit a relevant family, the continuation evidence could not create the comparison it needed. This pass keeps the existing RawDevelop architecture and uses the current continuation profile to add a small set of missing authored candidate families for the responsible stage.

Implemented:

- Extended `BuildDevelopAutoCandidateSolve` so active rendered continuation can add missing candidates for RAW/global highlight placement, scene-prep range/readability, finish-tone shape, or RAW cleanup/texture checks.
- Kept expansion duplicate-aware with `HasDevelopAutoCandidateId`, so ordinary first-pass candidates are not re-added under a second name.
- Added top-level `autoCandidateContinuationExpansion*` diagnostics, including separate eligible/active/added-count fields.
- Added per-candidate `continuationExpansion*`, `scoreComponents.renderedContinuationExpansion`, and `dimensions.renderedContinuationCoverage` diagnostics.
- Added validation where a finish-tone continuation generates finish-tone candidate families even though calm first-pass stats would not have generated them.

Explicit non-goals for this pass:

- No full adaptive candidate generator across all Guide 03 and future-guide families.
- No candidate gallery, thumbnails, hover/side-by-side preview, or user picker.
- No background render queue or full staged convergence controller.
- No user preference-learning application.
- No graph-style diagnostics UI.
- No final-image pixel blending.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs and limitations:

- Expansion is intentionally narrow and stage-oriented. It makes missing comparison families available for the current continuation loop, but it does not yet decide convergence from richer trend/stage evidence or build a full multi-pass candidate search space.
- The expansion profile reuses the continuation-bias resolver so future passes should extend that shared profile rather than creating a parallel continuation generator.

## 2026-06-11 - Add Continuation-Aware Candidate Bias

Decision:

Add `ContinuationCandidateBiasV1` as a compact bridge from rendered continuation policy to the next authored candidate solve.

Rationale:

Guide 03 describes Develop Auto as an iterative solver: rendered evidence should influence the next candidate set and not merely be recorded after the fact. `RenderedContinuationV1` already states whether the loop should continue and what stage/refine intent needs validation. A small bounded score bias lets that continuation evidence affect existing authored candidates without introducing a second solver, gallery UI, background queue, or final-pixel blending.

Implemented:

- Added `ResolveDevelopContinuationCandidateBiasProfile` and related candidate-match helpers in `src/Editor/EditorModule.cpp`.
- When `RenderedContinuationV1` says to continue, the next Auto solve gives matching responsible-stage or active-refine candidate families a capped score bonus.
- The bias can target finish-tone, scene-prep, RAW/global, RAW cleanup, multi-stage, or specific rendered refine intents such as highlight protection, brighter mids, contrast shape, cleaner shadows, and texture preservation.
- Bias diagnostics are written as top-level `autoCandidateContinuationBias*` fields, per-candidate `continuationBias*` fields, and `scoreComponents.renderedContinuationBias` / `renderedContinuationFit`.
- Pending rendered-metrics solves carry the same bias forward until metrics are consumed, preserving the solve fingerprint that candidate renders were generated from.
- Validation checks finish-tone continuation bias diagnostics and candidate boost behavior while preserving existing rendered-feedback monotonic/convergence behavior.

Explicit non-goals for this pass:

- No new solver architecture or independent candidate generator.
- No candidate gallery, thumbnails, hover/side-by-side preview, or user picker.
- No background render queue or full staged convergence controller.
- No user preference-learning application.
- No graph-style diagnostics UI.
- No final-image pixel blending.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs and limitations:

- The bias is intentionally modest and diagnostic. It helps the current solver spend attention on the stage/refine family rendered feedback named, but it does not replace richer future candidate-family generation or convergence thresholds.
- Carrying the bias while waiting for rendered metrics is required for fingerprint stability; future passes should preserve that behavior when expanding continuation policy.

## 2026-06-11 - Add Adaptive Candidate Render Budget

Decision:

Add `AdaptiveRenderBudgetV1` as a compact scheduler hook that lets the rendered continuation policy influence how many candidate renders the next Develop Auto pass requests.

Rationale:

Guide 03 defines Auto as an iterative solver that renders candidates, analyzes the rendered result, adjusts, and re-renders until no meaningful improvement remains. The previous increment made continuation explicit with `RenderedContinuationV1`, but candidate render scheduling still used the same fixed per-node budget for first-pass pending solves and active continuation solves. Active rendered-feedback passes often need one or two extra survivors to validate a responsible stage, refine intent, adoption, or merge. Expanding only those continuation passes moves the actual processing loop closer to the documented solver without implementing the future gallery or background queue.

Implemented:

- Added `ResolveDevelopAdaptiveRenderBudgetPolicy` in `src/Editor/Internal/EditorModuleRendering.cpp`.
- Kept the default pending-solve budget at four candidate renders per active Develop node.
- Allowed active continuation passes to expand up to six candidate renders when the current solve needs rendered metrics, `RenderedContinuationV1` says to continue, enough survivors exist, and the pass has active stage/refine/adoption/merge validation work.
- Kept a bounded graph-wide candidate render cap.
- Added adaptive budget fields to `DevelopCandidateRenderRequest` and `DevelopCandidateRenderResult`.
- `BuildDevelopCandidateRenderRequests` now uses the resolved budget in selected/survivor scheduling and request admission.
- `ApplyDevelopCandidateRenderFeedback` writes per-candidate and aggregate `AdaptiveRenderBudgetV1` diagnostics into integrated tone JSON.
- Added validation for default four-candidate scheduling, adaptive six-candidate continuation scheduling, and initial awaiting-metrics scheduling that remains at the default budget.

Explicit non-goals for this pass:

- No candidate gallery, thumbnails, hover/side-by-side preview, or user picker.
- No background/adaptive render queue outside the existing worker snapshot path.
- No sidecar stats bus or new render algorithm.
- No final-pixel blending.
- No graph-style diagnostics UI.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs and limitations:

- The expanded budget is deliberately conservative and only applies to active continuation cases. It improves actual rendered validation but does not replace the future full convergence scheduler.
- The policy currently changes render count, not candidate generation itself. Future Guide 03 work should let continuation evidence tune candidate-family generation and stop thresholds before adding UI.

## 2026-06-11 - Add Explicit Rendered Continuation Policy

Decision:

Add `RenderedContinuationV1` to the current Guide 03 rendered-feedback path so the compact multi-render loop records whether it is waiting for rendered metrics, continuing with another rendered solve, or stopping, plus the reason, next step, pass budget, stage focus, and evidence.

Rationale:

Guide 03 needs convergence and anti-oscillation behavior to be durable across passes. The existing `RenderedFeedbackLoopV1` already recorded loop state, but the continuation decision was still spread across candidate-render feedback and follow-up Auto-solve branch logic. Making the continuation policy explicit gives future passes a single extension point for adaptive scheduling, richer convergence, and diagnostics without inventing a parallel controller or reimplementing the current rendered-feedback loop.

Implemented:

- Added `BuildDevelopRenderedContinuationPolicyRecord` in both the Auto-solve diagnostic path and the rendered-feedback application path.
- Wrote `autoCandidateRenderedContinuationVersion = RenderedContinuationV1` and `autoCandidateRenderedContinuationPolicy` into integrated tone JSON.
- Embedded the same policy inside `autoCandidateRenderedFeedbackLoop.continuationPolicy`.
- Recorded `waitForRenderedMetrics` for new candidate solves, `continue` for applied rendered-feedback solves, and `stop` for stable/no-improvement/pass-limit/no-viable outcomes.
- Captured bounded pass metadata, stage focus/reason, required next evidence, and compact evidence such as improvement, stage-boundary signal, relative comparison status, stability/trend status, and monotonic guard status where available.
- Added validation for awaiting metrics, active continuation, stable stop, no-improvement trend stop, and pass-limit stop.

Explicit non-goals for this pass:

- No candidate gallery, thumbnails, hover/side-by-side preview, or user picker.
- No adaptive background render queue or full staged convergence scheduler.
- No sidecar stats bus or new render algorithm.
- No final-pixel blending.
- No user preference-learning application.
- No graph-style diagnostics UI.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs and limitations:

- The policy is a compact JSON contract over the current loop; it does not by itself schedule additional background passes.
- `waitForRenderedMetrics` is intentionally treated as a continuation state so the first solve can say what evidence is required next without implying the Auto solver has already consumed rendered metrics.
- Future Guide 03 passes should extend this policy before adding a separate continuation mechanism.

## 2026-06-11 - Compare Rendered Survivors Relative to Selected Baseline

Decision:

Add `RenderedRelativeComparisonV1` to the current Guide 03 rendered-feedback path so actual rendered survivors are ranked against the selected rendered baseline and active repair intent, while preserving the standalone rendered score for diagnostics.

Rationale:

Guide 03 says convergence is "no meaningful improvements" under the selected mode and user intent. A standalone rendered score can overvalue a candidate that looks globally tidy while moving away from the specific repair that the previous pass requested, such as protecting highlights or cleaning noisy shadows. The solver now needs a compact relative layer before full gallery/perceptual work exists: keep the existing standalone score, then adjust it with bounded bonuses for fixing the active repair target and bounded penalties for regressions relative to the selected render.

Implemented:

- Added `CompareDevelopRenderedCandidateToSelected` in `src/Editor/Internal/EditorModuleRendering.cpp`.
- `ApplyDevelopCandidateRenderFeedback` now writes per-candidate `standaloneRenderScore`, adjusted `renderScore`, `relativeComparisonStatus`, `relativeRepairMetric`, `relativeMetricDistance`, `relativeRepairDelta`, `relativeRepairBonus`, `relativeRegressionPenalty`, `relativeDistanceBonus`, and `relativeComparisonReason`.
- Top-level rendered feedback now writes `autoCandidateRenderedRelativeComparisonVersion`, active repair intent, selected standalone score, and best-candidate relative comparison fields.
- Adjusted scores are used for rendered survivor ordering, duplicate representative ordering, best selection, pair/ensemble merge suggestions, and no-meaningful-improvement checks.
- `ApplyRenderedCandidateFeedbackToSolve` recognizes marginal best candidates marked `regressedAgainstSelected` or `missedActiveRepair` as `renderedBestRelativeRegression`.
- Added validation through `ScoreDevelopRenderedCandidateRelativeToSelectedForValidation`: a lower-standalone highlight-protecting candidate must outrank a higher-standalone candidate that worsens highlights under `protectHighlights`.

Explicit non-goals for this pass:

- No candidate gallery, thumbnails, hover/side-by-side preview, or user picker.
- No final-pixel blending.
- No full perceptual scoring model or spatial EV maps.
- No subject, skin, memory-color, or denoise-specific repair maps.
- No full staged convergence controller or sidecar stats bus.
- No user preference-learning application.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.
- Scoped `git diff --check -- src/Editor/Internal/EditorModuleRendering.cpp src/Editor/EditorModule.cpp src/Editor/EditorModule.h src/main.cpp docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md docs/engineering/develop/DEVELOP_SOURCE_MAP.md docs/engineering/develop/DEVELOP_DECISIONS.md docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` passed with only existing LF-to-CRLF warnings. Full `git diff --check` remains blocked by generated `src/App/Resources/EmbeddedTabIcons.h` trailing whitespace from the broader dirty/build state.

Tradeoffs and limitations:

- The repair deltas are compact scalar proxies, not a perceptual evaluator. Thresholds are intentionally conservative and bounded so a single relative metric cannot dominate the whole solver.
- `renderedBestRelativeRegression` is treated as an intent-relative convergence/no-useful-improvement stop, because the candidate did not improve the selected render under the active repair direction.
- Future passes should extend the evidence feeding this comparison rather than creating a parallel ranking path.

## 2026-06-11 - Add Compact Rendered Color-Cast Metrics

Decision:

Add compact rendered color-cast/channel-balance metrics to the current Develop candidate render feedback path, and use them conservatively in rendered scoring, duplicate distance, damage rejection, JSON diagnostics, and metric readback.

Rationale:

Guide 03 needs actual rendered candidates to be analyzed for damage, duplicates, and convergence, and Guide 06 calls out color/WB/mood risks such as unnatural white-balance shifts. The existing rendered metrics already covered luma, range, saturation wash, halo/edge risk, shadow texture pressure, and compact local damage risk, but they did not preserve enough color-channel evidence to distinguish a neutral result from an extreme magenta/green cast. This pass adds bounded color evidence without claiming full skin, memory-color, mood, or color-graph understanding.

Implemented:

- Added mean red/green/blue, warm-cool bias, magenta-green bias, channel imbalance, and `colorCastRisk` to `EditorRenderWorker::DevelopCandidateRenderMetrics`.
- Computed those fields in `AnalyzeDevelopCandidatePixels`.
- Serialized the fields through `DevelopCandidateRenderMetricsToJson` and read them back in `ReadDevelopRenderedMetricsFromJson`.
- Included color-cast evidence in rendered metric distance so color-only rendered differences can avoid duplicate collapse.
- Applied a small color-plausibility term in `ScoreDevelopRenderedCandidateMetrics`.
- Added conservative `ClassifyDevelopRenderedCandidateDamage` rejection for extreme casts/channel imbalance while avoiding ordinary warm/cool mood rejection.
- Added validation for metric population, color-only metric distance, and extreme-cast rejection with the safe fixture still accepted.

Explicit non-goals for this pass:

- No full Guide 06 color strategy.
- No skin or memory-color protection.
- No color graph controls.
- No perceptual color scoring or visual color maps.
- No camera-profile or color-management redesign.
- No candidate gallery, user picker, or user-driven color repair controls.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs and limitations:

- The thresholds intentionally treat magenta/green and severe channel imbalance as stronger damage signals than warm/cool bias, because a warm or cool scene mood may be intentional.
- The metrics are image-wide compact evidence, not subject-aware color protection or a spatial color damage map.
- This was followed by `RenderedRelativeComparisonV1`, which evaluates rendered survivors against the selected baseline and active repair intent. Future passes should build on that layer rather than reimplementing it.

## 2026-06-11 - Use Per-Develop Candidate Render Budgets

Decision:

Schedule bounded rendered-candidate probes per active Auto `RawDevelop` node, with a conservative total snapshot cap, instead of using one graph-global four-request cap.

Rationale:

Guide 03 depends on actual rendered evidence: candidates must be rendered before the solver can reject damage, cluster duplicates, merge authored intent, or converge. The previous graph-global four-request cap could be consumed entirely by the first active Develop node encountered on the output path, leaving later active Develop nodes without rendered feedback. Per-node budgeting preserves the existing compact four-candidate solve shape for each node while avoiding starvation in multi-Develop graphs.

Implemented:

- Added named candidate-render budget constants in `src/Editor/Internal/EditorModuleRendering.cpp`.
- Added shared `CanScheduleDevelopCandidateRenderRequest` logic and `EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation`.
- Updated `BuildDevelopCandidateRenderRequests` so each active-path Auto Develop node can schedule up to four candidate renders.
- Kept a conservative total cap for a render snapshot. A later 2026-06-12 stability pass made the effective total/per-node cap source-size-aware for large RAWs.
- Added validation that the old starvation case, total request count already at four with a fresh node count of zero, remains schedulable.

Explicit non-goals for this pass:

- No visual candidate gallery.
- No user picker, hover preview, or user merge controls.
- No expanded adaptive background render queue.
- No standalone staged render controller or sidecar stats bus.
- No final-pixel blending.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

## 2026-06-11 - Fix Rendered Feedback Stop/Convergence Contract

Decision:

Use a shared rendered-feedback stop-reason classifier so the compact Guide 03 rendered-feedback loop only reports convergence for stable, no-improvement, or anti-oscillation stops. No-viable-rendered-candidate states must remain explicit stopped/failed states.

Rationale:

The rendered-feedback path has useful `RenderedFeedbackLoopV1` state, rendered metrics, rejection, merge, refinement, and stability records. A code audit found that it could still report `converged` when rendered candidates existed but no acceptable rendered winner was available, such as damage-only or no-best cases. Guide 03 defines convergence as no meaningful improvement remaining, not merely "we rendered candidates and found no acceptable winner."

Implemented:

- Added `EditorModule::IsDevelopRenderedFeedbackStopConvergedReason`.
- Updated `ApplyDevelopCandidateRenderFeedback` to set `autoCandidateRenderedConverged` only through that classifier.
- Updated `ApplyRenderedCandidateFeedbackToSolve` so follow-up Auto diagnostics use the same classifier.
- Kept `renderedMetricsStable`, rendered no-improvement trend stops, selected-still-best, no-meaningful-improvement, repeated-refine no-improvement, merge/adoption no-gain, and monotonic risk stops eligible for convergence.
- Kept `noRenderedBestCandidate`, `allRenderedCandidatesRejectedForDamage`, `renderedBestBelowQualityFloor`, `candidateRendersFailed`, `renderedFeedbackPassLimit`, and unavailable-candidate states as stopped/failed, non-converged outcomes.
- Added smoke validation for the classifier and a synthetic no-best rendered-feedback solve.

Explicit non-goals for this note:

- No claim that the full Guide 03 convergence engine, candidate gallery, staged controller, or learning application is complete.
- This stop-contract note did not change candidate render budgeting, gallery UI, final-pixel blending, or user preference-learning application; candidate render budget fairness is handled by the newer decision above.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

## 2026-06-11 - Add First-Pass RAW White-Balance Candidate Probes

Decision:

Add first-pass RAW white-balance candidate probes to the existing Guide 03 authored-candidate and rendered-candidate pipeline, while marking Guide 06 as only narrowly partial.

Rationale:

Guide 03 calls for real candidate families that can be rendered and compared, and Guide 06 calls for color/WB behavior that respects natural correction and scene mood. The existing solver had exposure, tone, mode-neighbor, and cleanup/detail probes, but no categorical RAW WB alternatives. Adding WB probes gives Auto a real processing choice over existing `Raw::WhiteBalanceMode` values without creating a new render algorithm or pretending the full color guide is done.

Implemented:

- Added `wbDaylightCorrection`, `wbNeutralCorrection`, and `wbCameraMood` probe mapping.
- Mapped daylight correction to `Raw::WhiteBalanceMode::Auto`, neutral correction to `Neutral`, and camera mood to `AsShot`.
- Gated daylight/neutral probe generation on meaningful camera/daylight/neutral metadata differences.
- Classified WB probes as `rawGlobal` so candidate render payloads change RAW WB rather than downstream tone only.
- Added color score dimensions `colorPlausibility` and `moodColorPreservation`.
- Wrote `rawOverrides.whiteBalanceMode`, `changes.whiteBalanceMode`, selected/authored WB probe/mode diagnostics, and render payload `autoCandidateWhiteBalanceProbe` / `autoCandidateWhiteBalanceMode`.
- Included WB probe state in candidate fingerprints and carried/read-back candidate state.
- Kept WB probes categorical: merge/refinement paths do not numerically blend white-balance modes.
- Added `--validate-develop-auto-solve` for solver-only validation, and updated smoke validation for WB probe generation, diagnostics, and render-payload RAW WB divergence.
- Fixed a candidate-merge vector invalidation issue by copying top candidates before appending a merged candidate; the larger candidate set made the previous reference-after-push pattern unsafe.

Explicit non-goals for this pass:

- No full Guide 06 color/WB strategy.
- No skin or memory-color protection.
- No color graph controls.
- No camera-profile or color-management rewrite.
- No candidate gallery, user picker, or user WB merge UI.
- No final-pixel blending.

Validation:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

Tradeoffs and limitations:

- `wbCameraMood` is mapped/scored/diagnosed, but current generation may not produce it often because the base authored solve already preserves As Shot when camera WB metadata exists.
- The color dimensions are first-pass scoring signals, not a perceptual color engine.
- Full Guide 06 remains deferred until a later pass implements mood, skin, memory-color, and color-graph behavior explicitly.

## 2026-06-11 - Add Finish-Tone Candidate Probes to Rendered Solver

Decision:

Add first-pass finish-tone candidate probes to the existing Guide 03 authored-candidate and rendered-candidate pipeline.

Rationale:

Guide 03 calls for image-driven candidate families, including tone candidates, and Guide 08 says useful alternatives include softer, punchier, flatter, brighter/darker, and range-aware tone shapes. The current solver had generic contrast and mode-neighbor probes, but it did not explicitly generate downstream tone-shape candidates that could be rendered while preserving the same RAW and scene-prep state. Adding these probes gives rendered feedback a real finish-tone comparison surface without creating UI, final-pixel blending, or a separate render algorithm.

Implemented:

- Added `toneSofterRolloff`, `tonePunchierShape`, `toneFlatterEditing`, and `toneDarkerToe` authored candidates in `BuildDevelopAutoCandidateSolve`.
- Added score, mode-fit, and score-component handling for the new tone probes.
- Classified the probes as `finishTone` so candidate render payload mapping freezes RAW and Scene Prep.
- Added render-request priority and active-stage/refine relevance for finish-tone probes.
- Added `autoCandidateFinishToneProbe` diagnostics on candidate render payloads.
- Smoke validation now checks generation, eligibility, human-readable labeling, meaningful tone deltas, finish-tone stage constraint mapping, and active-stage/refine relevance.

Explicit non-goals for this pass:

- No full Guide 08 finish-tone redesign.
- No tone graph controls.
- No candidate gallery, thumbnail cache, user picker, or user-driven merge UI.
- No final-image pixel blending.
- No claim that tone rolloff recovers missing clipped data.

Tradeoffs and limitations:

- The probes are conservative authored guidance states over the existing integrated ToneCurve auto pathway. They improve actual candidate processing but do not replace the current tone engine.
- Tone probe scoring still uses compact scalar stats and existing rendered metrics, not full perceptual tone analysis or subject/color-aware scoring.

## 2026-06-11 - Stop Same-Intent Rendered Refinement When Protected Risk Worsens

Decision:

Add a monotonic risk guard to the Guide 03 rendered-feedback loop: when Auto repeats the same rendered refinement intent, compare current selected rendered metrics to the previous same-intent pass and stop if the risk that intent is supposed to protect gets materially worse without a clear score gain.

Rationale:

Guide 03 explicitly calls out oscillation risks such as lifting shadows, revealing noise, backing off, then lifting again. The current loop already had pass limits, repeated-refine stops, trend convergence, and rendered rejection memory. It still needed a more image-specific rule that can say, "this same repair direction is making the protected rendered risk worse; stop here."

Implemented:

- Auto-side rendered metric readback now includes `localDamageRiskScore3x3`, `localDamageRiskMean`, `localDamageRiskPeak`, and `localDamageRiskPeakTile`.
- Added `TryReadLastSameIntentRefineMetrics` and `EvaluateRenderedRefineMonotonicGuard` in `src/Editor/EditorModule.cpp`.
- Shadow-opening / cleaner-shadow repeats stop with `renderedRefineMonotonicShadowRisk` when shadow texture, local shadow pressure, or local damage risk worsens.
- Brightness/highlight, contrast, and texture repeats use analogous monotonic checks for clipping/highlight pressure, halo/contrast risk, and texture risk.
- Diagnostics write monotonic guard status, metric, previous/current values, reference id, and compact loop-record fields.
- Smoke validation covers a synthetic cleaner-shadow repeat that worsens shadow texture risk and must converge/stop.

Explicit non-goals for this pass:

- No new candidate families.
- No gallery or user candidate picker.
- No full staged convergence scheduler.
- No full spatial/perceptual risk map.
- No learning application.

Tradeoffs and limitations:

- Thresholds are conservative and only guard repeated same-intent refinements. They do not replace broader perceptual scoring or future guide-specific subject/color/denoise analysis.
- A strong score gain can still allow a small risk increase; severe risk increases stop regardless.

## 2026-06-11 - Reserve Candidate Render Slot for Active Refine Intent

Decision:

Reserve one bounded Develop candidate render request for the active rendered refine intent when the selected render set does not already cover that intent.

Rationale:

Guide 03 frames Auto as an iterative solver that renders alternatives, analyzes the rendered result, then validates the next repair. The render request selector already considered score, diversity, responsible stage, cleanup/detail, mode-neighbor, and exposure-placement probes. Once rendered feedback identifies a specific repair intent, the next candidate render set should spend at least one scarce slot validating that repair family when possible.

Implemented:

- Added a refine-intent relevance map for existing authored/rendered-local families.
- `BuildDevelopCandidateRenderRequests` now reserves one non-selected slot for the active refine intent before generic diversity filling.
- Candidate render request/result structs carry active refine intent match/reservation fields.
- `RenderMetricsV1` diagnostics write per-candidate `activeRefineIntent`, `activeRefineIntentMatch`, `refineIntentReservedRequest`, plus aggregate active/refine-reserved request counts.
- Smoke validation covers the refine-intent relevance map.

Explicit non-goals for this pass:

- No increased candidate render budget.
- No new repair families.
- No gallery, picker, or visual compare UI.
- No full staged convergence controller.

Tradeoffs and limitations:

- This is a bounded slot-selection heuristic. If no matching survivor exists, no reservation is made.
- The full convergence scheduler should eventually reason across more candidate families, spatial maps, and user-visible history.

## 2026-06-11 - Route Spatial Risk Into Rendered Refinement

Decision:

Use compact rendered spatial-risk hotspots as input to the existing rendered-local refinement intents.

Rationale:

The previous pass made localized damage risk measurable, scoreable, rejectable, and serializable. Guide 03 also expects rendered feedback to adjust candidates and converge, not merely score bad output. Reusing existing authored follow-up intents keeps the implementation inside the current RawDevelop Auto solve path while making spatial damage evidence actionable.

Implemented:

- Bright-region spatial hotspots route to `protectHighlights`.
- High-contrast spatial hotspots route to `protectHighlights` to reduce highlight/local contrast pressure.
- Shadow hotspots route to `cleanShadows` when texture/noise pressure is present, otherwise to `openShadows`.
- Flat-gray spatial hotspots route to `addContrast` when there is no clipping pressure.
- Smoke validation checks all new spatial-risk refine branches and verifies the reasons came from the spatial path.

Explicit non-goals for this pass:

- No new rendered-local candidate family beyond existing authored refine intents.
- No full local EV map, subject-region map, perceptual damage map, or heatmap overlay.
- No gallery, user picker, or final-pixel blending.

Tradeoffs and limitations:

- The routing is heuristic and conservative. It improves the current feedback loop but is not a substitute for the richer spatial/perceptual analysis planned in later guides.
- Using existing intents avoids creating a parallel solver path, but future Guide 04/10 work may split these into richer local-map-aware repairs.

## 2026-06-11 - Add Compact Rendered Spatial Risk Metrics

Decision:

Add compact 3x3 regional damage-risk metrics to the existing Develop rendered candidate analysis path.

Rationale:

Guide 03 asks the solver to eliminate damaged candidates using rendered evidence, including halos, washed-out flattening, noisy shadows, lost brightness hierarchy, and local-map differences. The current rendered metrics already measured global luma/range and coarse local luma/contrast. Adding a bounded regional risk summary makes actual rendered analysis more sensitive to localized damage without introducing the deferred full spatial map UI or a new render algorithm.

Implemented:

- `DevelopCandidateRenderMetrics` now stores `localDamageRiskScore`, `localDamageRiskMean`, `localDamageRiskPeak`, and `localDamageRiskPeakTile`.
- `AnalyzeDevelopCandidatePixels` computes the compact risk from highlight crowding, shadow crowding, local edge/contrast stress, and flat-gray pressure over the existing 3x3 tiles.
- Rendered metric distance, rendered candidate scoring, and rendered damage rejection now include the new local risk signal conservatively.
- `RenderMetricsV1` diagnostics serialize the local damage-risk fields for final and pre-finish candidate metrics.
- Smoke validation covers metric population and localized hotspot rejection.

Explicit non-goals for this pass:

- No full spatial EV or perceptual damage map.
- No visual heatmap, overlay, candidate gallery, hover preview, side-by-side UI, or picker.
- No subject-region, skin, memory-color, or semantic artifact detector.
- No final-pixel candidate blending.

Tradeoffs and limitations:

- The risk score is a compact heuristic over RGBA readback tiles, not a perceptual model.
- Thresholds are deliberately conservative; future Guide 04/10 work should replace or augment this with richer local maps and user-facing diagnostics.
- This adds solver evidence and diagnostics only. It does not change Develop's scene-linear output or View Transform behavior.

## 2026-06-11 - Add Rendered Ensemble Merge Feedback

Decision:

Add `renderedFeedbackEnsembleMerge` as a conservative three-survivor merge path in the existing Develop rendered-feedback solver.

Rationale:

Guide 03 says rendered candidates should be compared, combined where useful, and converged as authored settings or intent states rather than final-pixel blends. Pair merge already reconciled two strong rendered survivors. The next processing step is to let three strong, distinct rendered survivors create one broader authored solve when the rendered evidence is close enough to combine and different enough to matter.

Implemented:

- `ApplyDevelopCandidateRenderFeedback` now writes ensemble-merge suggestion diagnostics when the top three non-damaged, non-duplicate rendered survivors pass conservative score-spread and metric-spread checks.
- `TryApplyRenderedEnsembleMergeToSolve` synthesizes `renderedFeedbackEnsembleMerge` from the three source candidates, using normalized/clamped rendered-score weights.
- Merge diagnostics and outcome-learning events now include optional third source id/weight fields.
- Smoke validation covers the ensemble selected id, source ids, normalized weights, rendered-feedback action, pass/fingerprint bookkeeping, and follow-up render status.

Explicit non-goals for this pass:

- No candidate gallery, thumbnail cache, hover preview, side-by-side UI, or user picker.
- No manual user-driven merge controls.
- No final-image pixel blending.
- No expansion of the bounded rendered-feedback pass budget.
- No full staged render controller or sidecar stats bus.
- No user preference-learning application.

Tradeoffs and limitations:

- Ensemble merge is still heuristic and compact. It uses rendered score and compact metric/guidance distance evidence, not a full perceptual similarity or spatial damage model.
- The merged state is an authored guidance blend that runs through the normal RAW + Scene Prep + integrated Tone solve. It does not average pixels or preserve every local-map difference from the source candidates.

## 2026-06-11 - Record Rendered Feedback Loop State

Decision:

Add `RenderedFeedbackLoopV1` as a compact JSON state record inside Develop's integrated tone JSON for the current rendered-feedback candidate loop.

Rationale:

Guide 03 describes Auto as an iterative render-feedback solver that renders multiple candidate states, analyzes them, applies adoption/merge/refinement, and stops when no meaningful improvement remains. The implementation already had pass counters, rendered metrics, feedback history, stop reasons, and convergence checks, but those fields were spread across several diagnostics. A single loop record makes the active state durable and gives future passes one place to continue the convergence controller without reinventing pass/status fields.

Implemented:

- `WriteDevelopAutoCandidateSolveDiagnostics` now writes `autoCandidateRenderedFeedbackLoopVersion = RenderedFeedbackLoopV1` and `autoCandidateRenderedFeedbackLoop`.
- `ApplyDevelopCandidateRenderFeedback` now writes a preliminary loop record when actual candidate renders request another Auto solve or stop without applying another solve.
- The loop record includes state, action, stop reason, next step, current pass, next pass, max passes, solve/render/applied fingerprints, selected/best ids and scores, revision stage/reason, history count, carried-forward count where available, and booleans for whether another render or Auto solve is required.
- Smoke validation covers awaiting rendered metrics, active applied feedback, stable convergence, and trend convergence states.

Explicit non-goals for this pass:

- No new candidate gallery, thumbnail cache, hover preview, side-by-side UI, or user picker.
- No final-pixel blending.
- No expansion of the pass budget or replacement of the existing rendered-feedback heuristics.
- No full staged render controller or sidecar stats bus.
- No user preference-learning application.

Tradeoffs and limitations:

- This is a durable state contract over the current rendered-feedback loop, not the final convergence engine.
- The loop record intentionally mirrors existing decisions rather than changing candidate scoring or render scheduling in this pass.

## 2026-06-11 - Preserve RawDevelop Boundary Snapshots Across Candidate Cache Churn

Decision:

Add a bounded owned RawDevelop stage snapshot cache in `RenderPipeline` for raw-base and hidden pre-finish boundary textures keyed by their graph fingerprints.

Rationale:

Guide 03 calls for reprocessing only the necessary stages when comparing rendered candidates. `StageSchedulerV1` improves candidate order, but the existing graph image cache still stores only one entry per `nodeId:socketId`. A RAW-dirty candidate can therefore replace a raw-base or pre-finish boundary that a later finish-tone or scene-prep probe should reuse. RawGpuPipeline also owns and reuses its output texture, so retaining older raw-base states requires cloning instead of aliasing the pipeline's internal texture.

Implemented:

- Added `m_RawDevelopStageImageCache` for bounded raw-base/pre-finish stage snapshots.
- Added width/height metadata to cached graph textures and made cached readback use stored dimensions when available.
- Cloned raw-base and hidden pre-finish textures before storing them in the stage cache.
- Stage-cache hits repopulate the existing graph image cache as non-owned entries and mark the normal cache-hit telemetry, so current rendered candidate diagnostics continue to work.
- Stage snapshots are invalidated with graph caches and pruned for inactive nodes. The cache originally used a fixed six-entry cap per boundary key; the newer 2026-06-13 stage-residency decision supersedes that with size-aware per-key caps, oversized-snapshot skipping, and a soft estimated-byte budget.
- Smoke validation now churns the single graph cache and verifies raw-base and pre-finish reuse are still observed.

Explicit non-goals for this pass:

- No full RAW/global/scene-prep/finish staged controller.
- No sidecar stats bus.
- No candidate gallery, thumbnail persistence, user picker, or merge UI.
- No final-image pixel blending.
- No View Transform behavior change.

Tradeoffs and limitations:

- This is a real physical texture snapshot cache for two current RawDevelop boundaries, not the final Guide 03 state machine.
- The cache stores full RGBA16F textures and is bounded to reduce memory risk; future passes should revisit memory policy when thumbnail/gallery or richer staged previews exist.
- RAW exposure and cleanup still live inside the raw-base boundary, so this does not split RAW-global from RAW-cleanup processing.

## 2026-06-11 - Schedule Candidate Renders by Expected Dirty Boundary

Decision:

Order actual Develop candidate renders with `StageSchedulerV1` after the bounded candidate set is selected. The selected authored candidate renders first, finish-tone probes render next, scene-prep probes render after that, and RAW/global, RAW cleanup, or multi-stage probes render last.

Rationale:

Guide 03 calls for staged reprocessing and avoiding unnecessary rerenders. The current graph cache stores RawDevelop raw-base and hidden pre-finish outputs by node/socket, so an upstream-dirty candidate can replace the reusable cache boundary that a downstream-only probe should have reused. Scheduling downstream probes immediately after the selected baseline makes the existing render worker behave more like the desired staged solver without introducing a new renderer.

Implemented:

- Added scheduler helper functions for expected dirty boundary, rank, and plain-language reason.
- Added scheduler fields to candidate render requests/results.
- `BuildDevelopCandidateRenderRequests` now keeps selected candidates first and sorts non-selected candidate renders by stage reuse order.
- `ApplyDevelopCandidateRenderFeedback` writes per-candidate scheduler diagnostics and top-level `autoCandidateRenderedStageSchedulerVersion = StageSchedulerV1` counts/status.
- Smoke validation checks selected/finish-tone/scene-prep/RAW/multi-stage rank ordering and expected dirty-boundary labels.

Explicit non-goals for this pass:

- No physical RAW/global/scene-prep/finish cache split.
- No standalone staged render controller or sidecar stats bus.
- No candidate gallery, thumbnail persistence, user picker, or merge UI.
- No final-image pixel blending.

Tradeoffs and limitations:

- This is a real scheduling step over the existing persistent render worker, but cache entries are still single graph-cache entries per node/socket. The scheduler improves the order in which candidate renders ask for reuse; it does not guarantee reuse if fingerprints differ or if earlier constraints are wrong.
- RAW/global, RAW cleanup, and multi-stage candidates remain broad because current RAW exposure, cleanup, denoise, demosaic, and camera transform behavior still live inside the raw-base boundary.

## 2026-06-11 - Validate Candidate Stage Cache Expectations

Decision:

Use candidate final-render cache-hit telemetry to derive an observed dirty boundary and validate whether stage-constrained candidate probes reused the expected upstream boundaries. Scene-prep probes should reuse the raw-base boundary, while finish-tone probes should reuse both raw-base and pre-finish boundaries.

Rationale:

Guide 03 calls for staged reprocessing and for revising only the earliest responsible stage when possible. After adding cache-hit telemetry, the next useful step is to turn those raw hits into a stage-level signal the solver can reason about. This catches cases where a candidate labeled as scene-prep or finish-tone accidentally dirties an earlier boundary, without claiming that the full physical staged scheduler exists.

Implemented:

- Added observed dirty-boundary classification from raw-base/pre-finish cache hits.
- Added stage-cache expectation validation for `scenePrep` and `finishTone` candidate stages.
- Rendered candidate diagnostics now include expected boundary, expected reuse fields, expectation status, and validation reason.
- Top-level rendered diagnostics now include observed dirty-boundary counts and stage-cache validation counts/status.
- Smoke validation checks the classifier and expected met/missed cases.

Explicit non-goals for this pass:

- No physical RAW/global/scene-prep/finish cache split.
- No staged render scheduler.
- No sidecar stats bus.
- No cache-control UI or gallery/thumbnail cache.

Tradeoffs and limitations:

- Validation is currently strongest for the downstream-constrained stages where expected reuse is unambiguous. `rawGlobal`, `rawCleanup`, and `multiStage` remain broad in the current physical render path because RAW exposure and cleanup still live inside the raw-base render boundary.
- The validation records behavior; it does not change render scheduling or force a candidate to be rejected solely because a cache expectation missed.

## 2026-06-11 - Record Candidate Stage Cache Hit Telemetry

Decision:

Expose graph-image cache hit telemetry for candidate final renders. `RenderPipeline` now records which graph image cache keys were hit during the latest `ExecuteGraph` call, and Develop candidate render results persist raw-base and pre-finish cache-hit booleans into `RenderMetricsV1` diagnostics.

Rationale:

Guide 03 asks Develop Auto to render multiple candidate versions and move toward staged processing. The current render pipeline already has useful graph-image caches for RawDevelop raw-base and hidden pre-finish outputs, and the candidate worker reuses a persistent `RenderPipeline`, but prior diagnostics could not tell whether those caches were actually hit during candidate final renders. Recording cache-hit evidence makes the existing reuse visible and gives future staged-scheduler work a concrete baseline without pretending the full cache architecture is finished.

Implemented:

- Added `RenderPipeline::WasGraphImageCacheHit` for the latest graph execution.
- Recorded generic graph image cache hits and RawDevelop raw-base fast-path hits.
- Carried raw-base and pre-finish final-render cache-hit booleans through candidate render results.
- Wrote per-candidate `rawBaseCacheHitDuringFinalRender` and `preFinishCacheHitDuringFinalRender`.
- Wrote aggregate raw-base/pre-finish final-render cache-hit counts and statuses.

Explicit non-goals for this pass:

- No physical RAW/global/scene-prep/finish cache split.
- No staged render scheduler.
- No sidecar stats bus.
- No cache-control UI, gallery cache, or performance timeline.

Tradeoffs and limitations:

- This is diagnostic observability over the current renderer. It does not change candidate selection, render output, or cache invalidation behavior.
- The telemetry reports hits from the most recent `ExecuteGraph` call only, which is enough for the worker to capture candidate-final-render evidence before any fallback pre-finish render occurs.
- Existing smoke validation covers the integrated build and Develop smoke path; there is not yet a dedicated GL cache-hit unit test.

## 2026-06-11 - Remember Rendered-Damaged Candidate States

Decision:

When actual rendered metrics reject a candidate as damaged, record that rejection in bounded rendered-rejection memory keyed by the candidate id and authored guidance fingerprint. The next Auto solve should reject the same authored candidate state from rendered memory before it can survive into selection.

Rationale:

Guide 03 says the solver should store why candidates were rejected so it does not regenerate them, and rendered feedback is stronger evidence than scalar pre-render heuristics. Existing parameter rejection memory handled same-context scalar rejects, but a candidate rejected only after actual rendered analysis could reappear in a follow-up pass. Keying memory by the full guidance vector avoids broad blacklisting while still preventing the solver from retesting a candidate state it already rendered and classified as damaged.

Implemented:

- Added per-candidate `guidanceFingerprint` diagnostics for authored candidates.
- Carried `guidanceFingerprint` through candidate render requests and results.
- `ApplyDevelopCandidateRenderFeedback` writes bounded `autoCandidateRenderedRejectionMemory` entries for `renderedRejectedDamage` candidates.
- `BuildDevelopAutoCandidateSolve` suppresses matching candidate id + guidance fingerprint pairs with `renderedMemoryRejected`.
- Learning/diagnostic JSON records rendered-memory suppression counts.

Explicit non-goals for this pass:

- No user-driven rejection controls.
- No preference-learning application.
- No gallery UI.
- No full spatial damage map.

Tradeoffs and limitations:

- Memory suppresses only the exact authored state. A same-named candidate with different guidance can still render, which is intentional while the solver is still evolving.
- Only rendered damage rejections are remembered here. Rendered duplicates are not globally suppressed because their usefulness depends on the current candidate set and representative.

## 2026-06-11 - Reserve a Candidate Render Slot for the Active Responsible Stage

Decision:

When previous rendered feedback names a responsible revision stage, bounded candidate render request selection should ensure that stage is actually represented by at least one rendered survivor when possible. The request builder now reserves one available render slot for the best active-stage-relevant survivor if the selected render set does not already cover the active stage.

Rationale:

Guide 03 asks Auto to render, compare, and revise the earliest responsible stage. The prior implementation gave candidates from that stage a score bonus, but a bonus alone could still lose to scalar score, diversity, or category priorities. That meant the solver could identify a responsible stage without rendering any candidate that tested it on the next pass. Reserving one bounded slot makes the staged feedback loop materially stronger while still keeping the request count capped and avoiding a new gallery or scheduler.

Implemented:

- Added `IsDevelopCandidateRelevantToRevisionStage` and a validation wrapper.
- Candidate render request selection now tracks each candidate's own revision stage and active-stage match.
- `BuildDevelopCandidateRenderRequests` reserves one available slot for an active-stage-relevant survivor when the selected set does not already include one.
- Candidate render requests/results carry `candidateRevisionStage`, `activeRevisionStage`, `activeStageMatch`, and `stageReservedRequest`.
- `RenderMetricsV1` diagnostics write per-candidate stage fields plus aggregate active-stage request and reserved-slot counts.

Explicit non-goals for this pass:

- No full physical staged render scheduler.
- No candidate gallery or user selection UI.
- No new candidate family beyond existing authored candidates.
- No final-pixel blending.

Tradeoffs and limitations:

- The reservation only applies within the existing bounded candidate render budget and only when a relevant survivor exists.
- This makes stage validation more reliable, but it is still a compact rendered-feedback loop over the existing RawDevelop pipeline, not the final staged processing architecture.

## 2026-06-11 - Reuse Cached Pre-Finish Boundary for Candidate Metrics

Decision:

Candidate final renders should reuse the hidden `preFinishImageOut` texture that the RawDevelop final render already computes before integrated finish tone. The render worker now reads that cached graph texture directly for pre-finish candidate metrics and falls back to a second `preFinishImageOut` graph render only if the cache read is unavailable.

Rationale:

Guide 03 asks the solver to render multiple versions, analyze stage boundaries, and avoid unnecessary reprocessing when only downstream stages need validation. The current RenderPipeline already caches RawDevelop raw-base and pre-finish graph outputs. Previous candidate metric work measured pre-finish state, but it always asked the graph to render the hidden pre-finish output after rendering the final candidate. Reusing the final render's cached pre-finish boundary makes the current staged behavior more real without introducing the full physical staged scheduler yet.

Implemented:

- Added `RenderPipeline::GetCachedGraphImagePixels` for reading cached graph image textures.
- `EditorRenderWorker::RenderSnapshot` now tries cached `preFinishImageOut` readback after each candidate final render.
- The worker keeps the existing second graph render as a fallback on cache miss.
- `DevelopCandidateRenderResult` records `preFinishReusedFromFinalRender`.
- `RenderMetricsV1` diagnostics write per-candidate reuse plus `autoCandidateRenderedPreFinishReuseCount` and `autoCandidateRenderedPreFinishReuseStatus`.

Explicit non-goals for this pass:

- No full RAW/global/scene-prep/finish physical cache split.
- No staged render scheduler.
- No thumbnail/gallery cache.
- No final-pixel blending.

Tradeoffs and limitations:

- The cached readback depends on the final render path having populated `preFinishImageOut`; when it has not, the worker deliberately falls back to the existing graph render.
- This reduces redundant candidate pre-finish graph execution, but it does not yet give the solver a full stage-by-stage render scheduler or sidecar stats bus.

## 2026-06-11 - Preserve Pre-Finish-Distinct Rendered Candidates During Duplicate Clustering

Decision:

Rendered candidate duplicate clustering now considers both final output metrics and hidden `preFinishImageOut` metrics. A candidate whose final render is near-duplicate to a higher-scoring representative remains a duplicate when pre-finish metrics are also close. If the final render is near-duplicate but pre-finish metrics differ meaningfully, the candidate survives with diagnostics.

Rationale:

Guide 03 asks the Auto solver to analyze rendered states and revise the earliest responsible stage. After adding pre-finish metrics, final-output-only duplicate clustering could still discard candidates whose upstream scene-prep/RAW result differed but whose final tone happened to mask the difference. Preserving pre-finish-distinct survivors keeps stage evidence available for rendered feedback without adding a gallery or a new render pipeline.

Implemented:

- Added `EvaluateDevelopRenderedCandidateDuplicate` and a validation wrapper, `ShouldTreatDevelopRenderedCandidateAsDuplicateForValidation`.
- `ApplyDevelopCandidateRenderFeedback` now uses the helper during rendered duplicate clustering.
- Duplicate diagnostics include final and pre-finish metric distances.
- Survivors kept because of pre-finish difference record `preFinishDistinct*` fields and top-level `autoCandidateRenderedPreFinishDistinctSurvivorCount`.
- Smoke validation covers both final-plus-pre-finish duplicate rejection and final-masked pre-finish-distinct survivor preservation.

Explicit non-goals for this pass:

- No candidate gallery or user picker.
- No physical staged cache split.
- No spatial damage map.
- No final-pixel blending.

Tradeoffs and limitations:

- This may keep an extra rendered survivor when final output looks similar, but only when both candidates have hidden pre-finish metrics and those metrics differ beyond the same conservative threshold used by the stage-boundary classifier.
- Pair-merge still depends on final rendered distinction; pre-finish-distinct survivors are primarily preserved as stage evidence for follow-up Auto feedback.

## 2026-06-11 - Add Pre-Finish Candidate Metrics and Stage-Boundary Feedback

Decision:

Measure the hidden pre-finish Develop boundary for rendered Auto candidates in addition to the final rendered output. Use selected-vs-best final/pre-finish metric distances to classify whether a rendered difference belongs to finish tone only, is already present before finish tone, or is stable/missing. Preserve a `finishTone` follow-up revision target when final metrics diverge but pre-finish metrics stay close.

Rationale:

Guide 03 and the first-think pseudocode both require the solver to render, analyze, and revise the earliest responsible stage. The prior staged diagnostics named logical boundaries, and stage-constrained payloads prevented some candidate probes from changing earlier stages accidentally. But rendered feedback still only measured the final output, so a final-tone-only improvement could be misread as a RAW/scene-prep change. Measuring `preFinishImageOut` gives the current solver actual evidence at the boundary between scene prep and finish tone without waiting for the full physical cache refactor.

Implemented:

- `DevelopCandidateRenderRequest` now defaults to measuring pre-finish metrics.
- `DevelopCandidateRenderResult` carries pre-finish success, dimensions, and compact metrics.
- The render worker renders candidate payloads through both the normal image output and `preFinishImageOut`.
- `ApplyDevelopCandidateRenderFeedback` writes per-candidate pre-finish metrics, final-vs-pre-finish distance, selected-vs-best final/pre-finish distances, and `autoCandidateRenderedStageBoundarySignal`.
- `ClassifyDevelopRenderedStageBoundary` classifies `finishToneOnly`, `preFinishChanged`, `preFinishChangedButFinalMasked`, `stable`, and missing-metric cases.
- `ApplyRenderedCandidateFeedbackToSolve` preserves a `finishTone` revision stage when the previous rendered feedback proved a finish-tone-only boundary.
- Smoke validation covers the classifier and a synthetic rendered-feedback adoption that keeps the finish-tone stage override.

Explicit non-goals for this pass:

- No physical staged cache scheduler.
- No sidecar stats bus.
- No full spatial damage/local EV map.
- No candidate gallery or user selection UI.
- No final-pixel blending.

Tradeoffs and limitations:

- This adds a second candidate render per candidate, so it is a processing-quality increment with extra work. The request count is still bounded, and it uses existing render graph sockets rather than adding a separate renderer.
- Pre-finish metrics are compact luma/color/visual-risk proxies. They are useful stage evidence, not a replacement for future spatial maps or subject-aware diagnostics.

## 2026-06-11 - Add Stage-Constrained Candidate Render Payloads

Decision:

Apply the responsible candidate stage to copied Develop candidate render payloads. Scene-prep candidate renders preserve RAW-stage placement, finish-tone candidate renders preserve RAW plus scene-prep settings, and cleanup/detail candidate renders preserve global RAW placement while varying cleanup/detail fields. Record the constraint in integrated tone JSON as `autoCandidateStageConstraint*` diagnostics.

Rationale:

Guide 03 says Auto should revise the earliest responsible stage and avoid letting downstream stages hide upstream mistakes. The previous rendered-feedback work classified the responsible stage and biased render-slot selection, but the actual candidate payload mapper still let many candidate probes move RAW exposure/highlight, scene prep, and finish tone together. Stage-constraining copied render payloads makes rendered comparisons more truthful: a finish-tone probe measures finish tone, a scene-prep probe measures local exposure/scene prep, and a cleanup probe measures cleanup/detail without being confounded by a fresh global EV shift.

Implemented:

- `ApplyDevelopGuidanceToCandidateRenderPayload` reads the stage from `DevelopRenderedRevisionStageForCandidateId`.
- Scene-prep candidates such as `renderedLocalBrightenMids` freeze RAW settings while keeping scene prep and integrated tone guidance movable.
- Finish-tone candidates such as `renderedLocalContrastShape` freeze RAW settings and scene-prep settings while keeping integrated tone guidance movable.
- Cleanup/detail candidates preserve global RAW placement and carry through only cleanup/detail RAW fields such as mosaic denoise, false-color cleanup, defringe, highlight-edge cleanup, and preserve-real-color.
- Candidate payload JSON records `autoCandidateStageConstraint`, `autoCandidateStageConstraintApplied`, `autoCandidateStageConstraintFrozenRaw`, `autoCandidateStageConstraintFrozenScenePrep`, and a reason when a constraint freezes earlier stages.
- Smoke validation covers scene-prep and finish-tone candidate stage constraints through `BuildDevelopCandidateRenderPayloadForValidation`.

Explicit non-goals for this pass:

- No physical RAW/global/scene-prep/finish cache split.
- No staged render scheduler or sidecar stats bus.
- No multi-candidate gallery or user picker.
- No final-pixel blending.
- No new denoise/demosaic engine.

Tradeoffs and limitations:

- This is a payload-level processing guard, not a physical render-cache scheduler. The current renderer still executes the existing RawGpuPipeline and RenderPipeline path.
- RAW-global and multi-stage candidates may still move several downstream settings together because those probes intentionally test broader authored states.

## 2026-06-11 - Add Logical Staged Auto Solve Record

Decision:

Write a logical staged Auto solve record into Develop's integrated tone JSON as `StagedAutoSolveV1`. The record names the documented Auto stages, stores per-stage fingerprints for the actual authored payload, records pass-budget metadata, distinguishes earliest dirty cache boundary from responsible rendered-feedback revision state, and records final validation state.

Rationale:

Guide 03 and the first-think document say Auto should render, analyze, revise the earliest responsible stage, and converge with bounded passes. Recent work already renders actual candidates and classifies responsible rendered-feedback stages, but future cache splitting or a staged scheduler would still have to infer which logical boundary changed. A durable staged record lets the current solver expose RAW/base, global, scene-prep, pre-finish, finish-tone, and final-validation boundaries without pretending the physical renderer has already been split.

Implemented:

- `BuildDevelopAutoStageFingerprints` derives fingerprints for metadata, raw base, raw/global, scene prep, finish tone, and final validation from the current authored settings, scene prep settings, Auto guidance, candidate solve result, and available stats.
- `WriteDevelopAutoStageSolveDiagnostics` writes the documented state sequence from `NEED_SOURCE` through `CONVERGED`, plus `autoStageEarliestDirtyStage`, `autoStageResponsibleRevisionState`, `autoStagePassBudget`, `autoStageCurrentPassKind`, `autoStageRenderedMetricsRequired`, and `autoStageValidationState`.
- The record explicitly notes that RAW exposure is still inside the current raw-base render boundary through `autoStageCurrentRawExposureInsideRawBase` and `autoStageCacheSplitStatus`.
- Smoke validation checks that staged diagnostics exist, fingerprints are nonzero, expected named states are present, and rendered scene-prep / raw-cleanup feedback maps to the responsible staged state.

Explicit non-goals for this pass:

- No physical RAW/global/scene-prep/finish cache split.
- No sidecar stats bus.
- No staged render scheduler that rerenders only one physical boundary.
- No visual stage preview UI or graph control surface.
- No candidate gallery or user candidate picker.

Tradeoffs and limitations:

- This is a logical processing contract and diagnostic substrate over the current render path. It improves correctness and continuation for future staged processing, but the renderer still executes the existing RawGpuPipeline and RenderPipeline boundaries.
- For some refinements, the responsible revision state can be `SOLVE_SCENE_PREP` while the actual earliest dirty cache boundary remains `RENDER_RAW_BASE`, because current RAW exposure is still authored inside the raw-base GPU render.

## 2026-06-11 - Add Stage-Aware Rendered Feedback Targeting

Decision:

Classify rendered-feedback follow-up work by responsible revision stage and use that stage to bias the next bounded candidate render selection. Store the stage in integrated tone JSON as `autoCandidateRenderedRevisionStage` with a companion reason field.

Rationale:

The Guide 03 and first-think documents both say Auto should revise the earliest necessary stage rather than letting finish tone hide upstream mistakes or re-rendering unrelated alternatives. The current implementation already renders actual candidate versions and can adopt, merge, or refine them. Adding a lightweight stage contract lets rendered metrics say whether the next pass is mainly about RAW/global placement, scene prep/local exposure, finish tone, cleanup/detail, or multiple stages. That improves the actual convergence loop without attempting the full staged cache/state-machine refactor in this pass.

Implemented:

- `ApplyDevelopCandidateRenderFeedback` writes provisional `autoCandidateRenderedRevisionStage` / `autoCandidateRenderedRevisionReason` when rendered metrics request a follow-up solve.
- `ApplyRenderedCandidateFeedbackToSolve` classifies applied adoption, merge, pair-merge, and refine actions into `rawGlobal`, `scenePrep`, `finishTone`, `rawCleanup`, or `multiStage`.
- `BuildDevelopCandidateRenderRequests` adds a small render-priority bonus for surviving candidates that match the latest responsible revision stage.
- `CandidateOutcomeLearningV1` records the stage and reason on rendered-feedback-applied events.
- Smoke validation checks that rendered adoption writes a stage, brighten-mids refinement targets `scenePrep`, and cleaner-shadow refinement targets `rawCleanup`.

Explicit non-goals for this pass:

- No full named Auto state machine.
- No RAW/global/scene-prep/finish cache split or stats bus refactor.
- No gallery UI, user candidate picker, or user merge control.
- No final-pixel blending.

Tradeoffs and limitations:

- Stage classification is intentionally compact and heuristic. It improves follow-up render targeting, but the renderer still executes the current graph path rather than a fully stage-incremental cache model.
- Some merged candidates remain `multiStage` because their authored guidance legitimately changes several stages at once.

## 2026-06-11 - Carry Forward Rendered Survivors Across Feedback Passes

Decision:

During an active rendered-feedback iteration, carry forward a bounded set of successful prior rendered survivor candidates into the next Auto solve as authored guidance candidates. Keep the behavior inside the existing Guide 03 candidate/feedback path and record `autoCandidateRenderedCarriedForwardCount` for diagnostics.

Rationale:

Guide 03 wants Auto to compare rendered candidate versions and converge through actual rendered evidence. Before this change, the follow-up solve could preserve the previously selected/rendered-best state and synthetic merge/refine candidates, but other strong rendered survivors could disappear before the next pass compared or combined them. Carrying a small scored survivor set forward lets the solver compare more actual rendered alternatives across passes without adding a gallery, a second solver, or final-pixel blending.

Implemented:

- `CollectDevelopRenderedSurvivorCandidateIdsForCarryForward` reads successful entries from `autoCandidateRenderedSolves`, skips damaged and rendered-duplicate candidates, sorts by rendered score, and returns a bounded id list.
- `BuildDevelopAutoCandidateSolve` rehydrates those ids from prior `autoCandidateSolves` only while rendered-feedback iteration is active.
- Existing selected, rendered-best, and rendered pair-merge source carry-forward behavior remains in place.
- Integrated tone JSON writes `autoCandidateRenderedCarriedForwardCount`.
- Smoke validation covers a synthetic prior rendered survivor and the diagnostic count.

Explicit non-goals for this pass:

- No thumbnail gallery, side-by-side compare UI, or user candidate picker.
- No final-image pixel blending; survivors remain authored settings/intent states.
- No applied user-choice learning.
- No full scheduled convergence controller across every future candidate family.

Tradeoffs and limitations:

- Carry-forward is intentionally bounded and score-ordered to avoid unbounded candidate growth.
- Survivors must still be present in prior authored candidate diagnostics to be rehydrated.
- This makes rendered feedback more durable across passes, but it is still a compact convergence substrate rather than the final Guide 03 candidate workflow.

## 2026-06-11 - Add Highlight-Protected Mids Exposure Candidate

Decision:

Add `highlightProtectedMids` as a Guide 03 authored exposure-placement candidate. The candidate tests lower RAW/global placement with local midtone support for broad highlight/HDR pressure, using the existing RAW + Scene Prep + integrated Tone solve and candidate render-request system.

Rationale:

Guide 03 asks Auto to render and compare actual candidate versions, while Guide 02 describes difficult exposure placements such as lower highlight-preserving global exposure plus local midtone lift. The current solver already has candidate generation, scoring, rendered metrics, and bounded feedback. Adding a single conservative exposure-placement family improves real processing coverage without inventing a second solver, thumbnail gallery, graph controls, or final-pixel blending.

Implemented:

- `BuildDevelopAutoCandidateSolve` now generates `highlightProtectedMids` when HDR spread, highlight pressure, or under-bright broad-highlight evidence warrants it.
- `ScoreDevelopAutoCandidate` and `DevelopAutoCandidateModeIntentFit` score the candidate with readable intent bias instead of treating it as a preset.
- `BuildDevelopCandidateRenderRequests` gives exposure-placement probes a small render-request priority category through `IsExposurePlacementCandidateIdForRenderRequest`.
- `ApplyDevelopGuidanceToCandidateRenderPayload` maps the probe into the existing render payload by lowering RAW placement through guidance deltas and nudging local midtone/highlight support.
- Smoke validation covers candidate generation, eligibility, meaningful guidance deltas, and render-payload forwarding.

Explicit non-goals for this pass:

- No full global/local exposure-placement search.
- No thumbnail gallery, user candidate picker, or user merge UI.
- No spatial highlight/shadow damage map or local EV-map comparison.
- No subject-priority exposure brush or automatic subject detection.
- No claim that clipped source data can be recovered.

Tradeoffs and limitations:

- The candidate is conservative and stat-triggered; it is a first exposure-placement family, not the complete Guide 02/03 placement system.
- Scene Prep values may already be clamped in difficult scenes, so validation checks faithful candidate-guidance forwarding plus lower RAW placement rather than requiring impossible additional local-bias headroom.

## 2026-06-11 - Add Rendered Feedback Trend Convergence

Decision:

Add a compact rendered-feedback trend evaluator to the Develop Auto solve loop. Use the existing bounded rendered-feedback history to stop repeated feedback passes when the same/similar rendered best candidate is no longer producing meaningful improvement.

Rationale:

Guide 03 defines convergence as visual/data stability under intent, not a single histogram target. The current processing path already writes compact rendered metrics and bounded history; using that history during the next solve lets Auto avoid spending another pass on a rendered winner/refinement that has already repeated with a flat score trend. This strengthens the actual processing loop without adding UI, thumbnail galleries, or a separate render algorithm.

Implemented:

- `EvaluateDevelopRenderedFeedbackTrend` reads recent rendered-feedback history, current rendered-best metrics, score spread, and same-best repetition.
- The solver now stops with `renderedFeedbackNoImprovementTrend`, `renderedRefineNoImprovementTrend`, or `renderedFeedbackStableTrend` when repeated rendered feedback has stalled.
- Integrated tone JSON records trend diagnostics: history count, same-best count, score spread, nearest metric distance, reference id, and trend status.
- Candidate outcome learning records these trend fields on rendered-feedback stopped events.
- Smoke validation covers a repeated rendered-history fixture that converges through the new trend stop.

Explicit non-goals for this pass:

- No full scheduled convergence controller with user-visible candidate history.
- No thumbnail gallery or user candidate selection/merge UI.
- No spatial damage map, perceptual local EV-map comparison, or full Guide 04/10 diagnostics UI.
- No preference-learning application from repeated rendered outcomes.

Tradeoffs and limitations:

- The trend evaluator is intentionally conservative: it only stops after multiple prior feedback passes and requires a flat rendered-score trend plus repeated/stable rendered metrics.
- This improves anti-oscillation and no-improvement stopping, but it is still not the final multi-render convergence engine described by Guide 03.

## 2026-06-11 - Add Parameter Score Component Diagnostics

Decision:

Add `ParameterScoreComponentsV1` diagnostics to Develop Auto parameter candidates. Keep the existing scalar candidate score for selection, but persist a compact multi-dimensional breakdown next to each candidate.

Rationale:

Guide 03 says the solver should not chase one universal score and that candidates should carry a compact explanation. The current solver already uses multiple signals such as highlight pressure, clipping, HDR spread, darkness, shadow rescue, noise risk, texture confidence, flat-scene need, mode intent, and uniqueness. Persisting those dimensions makes the existing processing more inspectable and gives future convergence, graph-control, and diagnostics passes a stable data surface without changing the render pipeline or adding UI first.

Implemented:

- `BuildDevelopAutoCandidateScoreComponents` writes `scoreComponents` for each generated parameter candidate.
- `scoreComponents` include input signals, guidance deltas, final score, score source, candidate id, Auto intent, dimensions, and risks.
- Dimensions currently include midtone placement, highlight integrity, shadow cleanliness, dynamic range fit, contrast shape, noise/texture quality, local artifact safety, mode-intent fit, and candidate uniqueness.
- Risk terms currently include highlight damage risk, shadow noise risk, flattening risk, and data-risk penalty.
- `DevelopAutoCandidateNearestSurvivorDistance` adds uniqueness after duplicate clustering, so the value reflects the survivor set rather than only the initial generated candidates.
- Carried-forward candidates preserve existing score components when available; older carried-forward candidates use an authored-state fallback.
- Smoke validation verifies the selected candidate has the score-component version, final score, required dimensions, risk fields, and bounded uniqueness.

Explicit non-goals for this pass:

- No full perceptual image-quality score.
- No subject, skin, memory-color, or color-harmony score model.
- No score-component graph UI or requested-vs-achieved panel.
- No new render candidate family or final-pixel blending path.

Tradeoffs and limitations:

- Components explain the current parameter heuristic. They are useful for debugging and future convergence, but they should not be mistaken for a final perceptual model.
- Some future dimensions, such as subject importance and color plausibility, remain placeholders for later guide-owned work.

## 2026-06-11 - Record Candidate Outcome Learning Without Applying It

Decision:

Add a compact `CandidateOutcomeLearningV1` record to Develop Auto's integrated tone JSON. Record solver outcomes separately from learned-bias application, and keep current-image and future-image learning application explicitly disabled in this pass.

Rationale:

Guide 03 says learning should be recorded separately from whether it is applied. The current processing path already knows which candidate was selected, which candidates survived or were rejected, whether authored merges happened, and whether rendered feedback applied or stopped. Capturing that evidence now gives future learning and diagnostics passes durable data without silently changing image style or introducing preference behavior before user controls exist.

Implemented:

- `BuildDevelopAutoCandidateLearningRecord` writes `autoCandidateLearningRecord` with version `CandidateOutcomeLearningV1`.
- The record includes candidate selected/survived/rejected events, guidance vectors, scores, reject reasons, merge events, rendered-feedback applied/stopped events, and convergence events where available.
- `autoCandidateLearningStatus` is now `recordedNotApplied`.
- `autoCandidateLearningApplied`, `autoCandidateLearningAppliedToCurrentImage`, and `autoCandidateLearningAppliedToFutureImages` are all false.
- Current-image learning is marked `recordedOnly`; future-image learning is marked `notApplied`; user-choice learning is marked `deferredUntilCandidateSelectionUi`.
- Smoke validation verifies the record version/status, selected outcome event, event count, and unapplied current/future learning flags.

Explicit non-goals for this pass:

- No user candidate picker, rejection UI, or user selection event source.
- No preference model or global learning store.
- No learning settings, reset controls, or learned-bias summary UI.
- No application of learned bias to current or future images.

Tradeoffs and limitations:

- This is outcome learning, not preference learning. It records what the solver did and why, but it does not infer what the user prefers.
- Because there is no candidate gallery/user picker yet, user-choice learning remains deferred to the guide/pass that implements that interaction.

## 2026-06-11 - Confirm Hardened Develop Tracking Acceptance

Decision:

Keep `AGENTS.md` as the short entrypoint and use the tracker, source map, decisions, pass protocol, and deferred-scope files as the authoritative continuation system for Develop node work.

Rationale:

The current repository already has Guide 01 complete and Guides 02/03 partially implemented. Future sessions need to continue from verified tracker/code truth, not from stale prompt assumptions that would cause reimplementation, skipped requirements, or false completion claims.

Implementation notes:

- `AGENTS.md` points to the context file, guide index, tracker, source map, decisions, pass protocol, deferred scope, and active numbered guide workflow.
- `DEVELOP_IMPLEMENTATION_TRACKER.md` contains the ten-guide status table, a tracking-hardening requirement checklist, requirement tracking rules, the complete Guide 01 checklist, and "Do Not Re-Implement / Already Done" notes for Guide 01 Auto intent/mode work.
- `DEVELOP_PASS_PROTOCOL.md` defines start, during, and end-of-pass checklists, including the rule that a guide is not `Complete` unless code, docs, and validation entries are updated.
- Documentation-only tracking passes should still leave an acceptance audit in the tracker and decisions file, with an explicit note when no code validation is required.
- `DEVELOP_DEFERRED_SCOPE.md` maps intentionally deferred features to owning guides and prevents documentation-only preparation from being claimed as implementation.
- The hardening acceptance note preserves verified partial guide progress instead of stale prompt assumptions. Historical status examples in this decision were true at the time written; the current guide table and status guard in `DEVELOP_IMPLEMENTATION_TRACKER.md` supersede them after later passes.
- Feature-level deferred-scope rows may be `Partial` because another guide created reusable substrate. That is not a guide-level status change unless the owning guide row and requirement checklist are also updated.

Tradeoffs and limitations:

- This is Markdown-based tracking only. It intentionally adds no scripts, database, or enforcement tooling.
- The docs remain authoritative only if future passes update them in the same pass as code changes.

## 2026-06-11 - Add Append-Only Develop Pass Handoff Rules

Decision:

Require every future Develop numbered-guide pass to leave an append-only handoff note in `DEVELOP_IMPLEMENTATION_TRACKER.md`, and require status reconciliation before changing guide status.

Rationale:

Long Develop conversations can lose chat context, and older prompts can contain stale assumptions about what is already complete. A dated handoff note gives the next pass a durable starting point without needing to infer progress from partial code diffs or conversation memory.

Implementation notes:

- `AGENTS.md` now explicitly says not to reset `Complete` or `Partial` Develop work without verifying the tracker/code truth.
- `DEVELOP_IMPLEMENTATION_TRACKER.md` now requires dated handoff notes with status delta, changed locations, validation, remaining work, deferred-scope changes, and the next recommended starting point.
- `DEVELOP_PASS_PROTOCOL.md` now has a `Status Reconciliation` section and includes the handoff note in the end-of-pass checklist.
- `DEVELOP_DEFERRED_SCOPE.md` now defines `Not Started`, `Partial`, and `Deferred`, and requires same-pass tracker/decision/source-map updates when a deferred item changes state.

Tradeoffs and limitations:

- This still relies on disciplined Markdown updates. It intentionally avoids adding scripts or a database.
- The tracker remains the best continuation record; decisions explain why the rules exist but should not be used as the only progress ledger.

## 2026-06-10 - Add Guide 03 Mode-Neighbor Candidate Probes

Decision:

Add mode-neighbor candidate probes to the Develop Auto solver as authored settings-space alternatives. These probes test nearby intent tradeoffs without changing the selected Auto mode and without becoming a preset stack.

Rationale:

Guide 03 calls out mode-neighbor candidates as a way for difficult images to compare nearby answers such as Natural More Range, Natural Brighter Subject, or Natural More Contrast. The existing candidate system already renders selected/surviving authored states and can adopt, merge, or refine them from rendered metrics. Adding adjacent intent-vector probes gives that rendered comparison path more meaningful alternatives before any future gallery UI exists.

Implemented:

- Natural Finished can generate Natural More Range, Natural Brighter Mids, and Natural More Contrast from HDR, highlight, shadow/midtone, texture, and noise signals.
- Other Auto modes now generate one neighboring authored probe where useful: Bright Highlight Safe, Dark Readable Mids, Punchy Safer Range, Range Natural Shape, Flat Natural Shape, and Clean Texture Check.
- Mode-neighbor candidates are scored by the existing multi-signal parameter scorer and written into `autoCandidateSolves` with human labels and change deltas.
- Generic duplicate pruning prefers a labeled mode-neighbor probe over a near-identical generic family, while still allowing redundant mode-neighbor probes to cluster against each other.
- Candidate render request selection gives mode-neighbor probes a small priority bonus so limited render slots can compare adjacent intent tradeoffs.
- Repeated same-intent rendered refinement now stops when the previous feedback already refined that intent and the current rendered list does not prove improvement.
- Smoke validation covers mode-neighbor generation, eligibility, human-readable labeling, meaningful guidance delta, and the repeated-refine stop behavior.

Explicit non-goals for this pass:

- No graph-style mode controls.
- No rendered candidate gallery, user candidate picker, user-driven merge UI, or candidate learning controls.
- No final-image pixel blend.
- No completion claim for color/WB, subject-priority, or full denoise/detail candidate engines.

Tradeoffs and limitations:

- Mode-neighbor probes are still heuristic authored-guidance deltas. They improve the processing substrate for rendered comparison, but they are not a full intent graph and do not expose user-controlled candidate exploration yet.
- The current render slot limit is still bounded. Mode-neighbor priority makes adjacent tradeoffs more likely to be rendered, but it does not guarantee every generated candidate is rendered.

## 2026-06-10 - Add Guide 03 Rendered Damage Rejection

Decision:

Reject obviously damaged rendered Develop candidates before they can become the rendered best representative, a duplicate-clustering representative, or a pair-merge source. Keep this as compact rendered-metric classification inside the existing Guide 03 candidate feedback path.

Rationale:

Guide 03 says bad candidates should be removed automatically before the user has to compare them. The rendered metrics path already measures highlight clipping/crowding, saturation wash, edge/halo risk, shadow texture pressure, and brightness hierarchy. Using those existing probes to reject severe rendered damage prevents damaged outputs from driving adoption, merge, or convergence feedback while preserving the current authored-settings solve architecture.

Implemented:

- `ClassifyDevelopRenderedCandidateDamage` flags broad highlight clipping/crowding, strong halo or edge-glow risk, washed-out gray flattening, noisy lifted shadows, collapsed brightness hierarchy, and overly bright highlight-heavy hierarchy.
- `ApplyDevelopCandidateRenderFeedback` marks damaged candidates as `renderedRejectedDamage`, writes `rejectReason`, excludes them from duplicate clustering/best selection/pair merge, and records `autoCandidateRenderedDamageCount`.
- If all rendered candidates are damaged, the loop either requests a selected-candidate refinement when compact metrics provide a clear refine direction or stops with `allRenderedCandidatesRejectedForDamage`.
- Smoke validation calls `EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation` and checks broad clipping, halo, gray flattening, shadow-noise damage, and a safe fixture.

Explicit non-goals for this pass:

- No true spatial damage map, halo overlay, skin/memory-color artifact classifier, local denoise map, or perceptual thumbnail model.
- No user-visible candidate rejection UI or rendered gallery.
- No final-image pixel blending.
- No claim that missing clipped source data can be recovered.

Tradeoffs and limitations:

- The classifier is intentionally conservative and metric-based. It should catch severe rendered failures that would poison feedback, but future Guide 03/04/07/10 work still needs richer spatial and perceptual diagnostics.
- Rejected rendered candidates are not learned as user preferences. User preference learning remains deferred.

## 2026-06-10 - Add Guide 03 Rendered Pair Merge Feedback

Decision:

Allow the rendered-candidate analysis pass to suggest an authored merge between the two strongest non-duplicate rendered survivors when they are both good, close enough in score, and meaningfully different. The follow-up Auto solve creates `renderedFeedbackPairMerge` by interpolating authored guidance, not by blending final pixels.

Rationale:

Guide 03 and Guide 08 both describe merging candidates as an intent/settings-space operation. The previous rendered-feedback path could adopt a clear winner or merge the current selected candidate with the rendered-best survivor. That helped selected-vs-best feedback, but it did not yet use rendered analysis to reconcile two strong alternate survivors, which is the processing foundation needed before a future gallery or user merge UI.

Implemented:

- Rendered duplicate clustering still runs before merge suggestion, so pair merge uses unique rendered representatives.
- `ApplyDevelopCandidateRenderFeedback` records `autoCandidateRenderedMergeSuggested`, pair source ids/labels/scores, metric distance, and per-candidate `renderedMergeRole` diagnostics.
- `ApplyRenderedCandidateFeedbackToSolve` synthesizes `renderedFeedbackPairMerge` when the pair suggestion survives authored-candidate lookup and guard checks.
- Pair merge uses `BlendDevelopAutoCandidateGuidance` and keeps the normal RAW + Scene Prep + integrated Tone render path.
- Repeated rendered-merge no-gain stopping recognizes both `renderedFeedbackMerge` and `renderedFeedbackPairMerge`.
- Smoke validation covers the pair-merge selection source, merge ids, rendered feedback action, pass, and applied fingerprint.

Explicit non-goals for this pass:

- No rendered thumbnail gallery, hover preview, side-by-side comparison, or user candidate picker.
- No user-driven merge UI.
- No final-image pixel blending.
- No full scheduled multi-pass convergence engine across every future candidate family.

Tradeoffs and limitations:

- Pair merge is still heuristic and bounded. It only uses compact rendered metrics plus authored-guidance distance, not a perceptual thumbnail similarity model or spatial damage map.
- This makes the processing layer more capable of combining actual rendered alternatives, but future UI work is still required before users can select or merge candidates directly.

## 2026-06-10 - Add Guide 03 Rendered Cleanup / Texture Refinement Intents

Decision:

Let compact rendered metrics request cleanup/detail-specific authored refinement families inside the existing Guide 03 rendered-feedback loop. Keep the implementation in current RAW cleanup, mosaic denoise, Scene Prep, and integrated Tone settings.

Rationale:

The previous cleanup/detail probe increment made `cleanShadows` and `preserveTexture` render differently, but rendered feedback could only refine brightness, shadow opening, highlight restraint, or contrast. Guide 03 expects the solver to look at rendered consequences, and Guide 07 says noise/detail tradeoffs should avoid plastic smoothing. The next conservative step is to let measured shadow texture pressure or subdued fine separation choose a cleanup/detail refinement without implementing the full Guide 07 denoise redesign.

Implemented:

- Added rendered refine intents `cleanShadows` and `preserveTexture`.
- Added rendered-local authored candidates `renderedLocalCleanShadows` and `renderedLocalPreserveTexture`.
- `ResolveDevelopRenderedRefineIntent` can now choose cleaner-shadow refinement when shadow texture/noise pressure is high without matching highlight trouble.
- `ResolveDevelopRenderedRefineIntent` can choose texture preservation when compact metrics show safe tones with subdued fine separation.
- Candidate render request classification treats the new rendered-local cleanup/detail candidates as both rendered-local and cleanup probes.
- Candidate render payload mapping reuses the existing cleanup/detail deltas so the new candidates materially change mosaic denoise, cleanup, preserve-real-color, and Scene Prep texture/noise protection.
- `ApplyRenderedCandidateFeedbackToSolve` now recognizes cleanup/detail refine intents and accepts refine feedback that matches the rendered base solve fingerprint. This keeps stale ordinary adoption/merge feedback guarded while allowing newly generated rendered-local refine families to be considered.
- Smoke validation covers intent resolution and `renderedLocalCleanShadows` feedback selection.

Explicit non-goals for this pass:

- No full texture detector, local denoise map, or perceptual detail scoring system.
- No post-demosaic denoise engine.
- No Guide 07 denoise/demosaic/detail overhaul.
- No thumbnail gallery, user candidate picker, or candidate learning model.

Tradeoffs and limitations:

- The cleanup/detail rendered intents are compact proxies, not a final perceptual texture system. They can notice shadow texture pressure and low fine separation, but they cannot reliably identify skin, fabric, stars, hair, or subject-important detail.
- The refine fingerprint rule intentionally distinguishes refine feedback from adoption/merge feedback. Future passes should preserve that distinction when adding richer rendered-local candidate families.

## 2026-06-10 - Add Guide 03 Cleanup / Texture Render Probes

Decision:

Add a first clean-versus-textured candidate probe inside the existing Guide 03 candidate/render-metrics path. Keep it authored in existing RAW cleanup, mosaic denoise, Scene Prep, and integrated Tone settings rather than introducing a new denoise engine or pixel-blend path.

Rationale:

Guide 03 and Guide 07 both say the solver must compare noise/detail tradeoffs and should prefer natural texture over plastic smoothing. The existing candidate system already renders selected/surviving authored payloads, but `cleanShadows` mostly changed guidance rather than forcing a materially different cleanup render. Adding a paired `preserveTexture` candidate and candidate-id-specific render-payload cleanup deltas lets rendered metrics compare a real cleaner-shadow probe against a real texture-preserving probe while staying inside the current architecture.

Implemented:

- `preserveTexture` candidate family generated from texture-confidence and noise-risk stats.
- Candidate scoring now gives `cleanShadows` and `preserveTexture` opposite noise/detail preferences.
- Cleanup/detail probe candidates are preserved through generic duplicate pruning.
- Candidate render request selection gives cleanup/detail probes a small priority bonus.
- Candidate render payloads now alter actual hidden processing for cleanup/detail probes:
  - cleaner-shadow probes enable/strengthen mosaic denoise, false-color cleanup, defringe, highlight-edge cleanup, preserve-real-color, and scene-prep noise protection;
  - preserve-texture probes reduce smoothing pressure, raise edge/detail/texture protection, and preserve more real color/detail.
- Candidate render payloads write `autoCandidateCleanupProbe` diagnostics.
- Smoke validation proves clean-versus-texture probes are generated and their render payloads diverge.

Explicit non-goals for this pass:

- No full pre/post demosaic denoise redesign.
- No new demosaic method.
- No post-demosaic denoise engine.
- No local denoise maps, texture maps, or subject-protected detail maps.
- No thumbnail gallery, user candidate picker, or candidate merge UI.
- No final-image pixel blending.
- No Guide 07 completion claim.

Tradeoffs and limitations:

- This is a processing increment for Guide 03 using existing controls. It makes candidate renders meaningfully different for cleanup/detail, but the rendered metrics are still compact proxies and cannot fully judge texture quality, skin detail, stars, fabric, or local denoise artifacts.
- The new validation proves payload divergence, not final perceptual quality. Future Guide 07 work still needs a deeper denoise/detail model and richer diagnostics.

## 2026-06-10 - Add Guide 03 Rendered Candidate Metrics Path

Decision:

Add a bounded worker-side rendered candidate metrics path for Develop Auto candidates, and allow those metrics to trigger a capped follow-up authored solve when a rendered survivor clearly beats the current selected candidate. Keep candidate selection/merge authored in settings/intent space and leave gallery UI for a later pass.

Rationale:

Guide 03 calls for candidates to be rendered, analyzed, adjusted, and converged, not only generated as parameter guesses. The existing render worker and render graph can already execute copied `RawDevelop` payloads. A conservative next step is to render selected/surviving candidate payloads through synthetic output nodes, measure compact rendered-state metrics, and let a clearly better rendered survivor drive one bounded follow-up solve without introducing a new node kind or a final-image pixel blending system.

Implemented:

- `EditorRenderWorker` now has Develop candidate render request/result types and compact rendered metrics.
- `BuildDevelopCandidateRenderRequests` builds at most four requests for selected/surviving candidates on the active output path.
- Candidate render request selection always includes the selected authored state, then chooses additional survivors with a score-plus-authored-guidance-diversity priority so the worker spends limited render slots on meaningfully different candidate versions instead of only the top scalar scores.
- Rendered-local mismatch candidates receive explicit request priority because they were seeded by measured output mismatch.
- Candidate render payloads are copied from the current solved Develop payload and lightly biased from candidate guidance before rendering.
- The worker renders each candidate through the normal render graph with a synthetic output node.
- Rendered metrics currently include mean/median/p10/p90 luma, shadow fraction, highlight fraction, clipped fraction, and contrast span.
- Rendered metrics also include compact visual-risk proxies: mean saturation, low-saturation fraction, edge contrast, halo-risk fraction, and shadow texture risk.
- Rendered metrics now include a compact 3x3 local summary: tile mean luma, tile contrast span, local luma spread, local contrast peak, local shadow/highlight pressure, and center-region luma/shadow/highlight pressure. These values feed rendered candidate scoring and duplicate-distance comparison.
- The same local summary now steers rendered refinement intent. Concentrated local highlight pressure chooses `protectHighlights`, center/local shadow crowding chooses `openShadows`, and flat local separation can choose `addContrast` even when global averages are less clear.
- Rendered candidates receive a compact rendered metric score, and the best rendered candidate id/label/score is recorded for diagnostics/future solve use.
- Rendered candidates are clustered by compact metric distance before choosing the rendered best candidate. Near-duplicates are marked `renderedDuplicate` with `duplicateOf`, while the best representative remains eligible for adoption/merge/refinement feedback.
- Rendered scoring now lightly penalizes gray-wash risk, edge/halo risk, and noisy shadow texture pressure without treating those proxies as full spatial maps.
- Parameter candidates now write bounded `autoCandidateRejectedMemory` keyed by an image/state context fingerprint. A repeated solve with the same context suppresses candidates already rejected for damage or duplication instead of treating them as fresh attempts.
- `ApplyDevelopCandidateRenderFeedback` writes `autoCandidateRenderedVersion = RenderMetricsV1`, rendered counts, failure counts, best rendered candidate, and metrics to integrated tone JSON.
- If the rendered best survivor beats the currently selected candidate, `ApplyDevelopCandidateRenderFeedback` records `solveRequested` and requests a forced Auto solve; `ApplyRenderedCandidateFeedbackToSolve` either adopts a clear rendered winner or synthesizes `renderedFeedbackMerge` for a modest rendered win in authored settings space while preserving the normal RAW + Scene Prep + integrated Tone render path.
- While a rendered-feedback iteration is active, the next parameter solve can rehydrate prior authored `renderedFeedbackMerge` / `renderedFeedbackRefine` candidates from tone JSON so the loop can compare the actual authored state it just rendered instead of forgetting synthetic feedback candidates between passes.
- If the selected rendered candidate is still the best but compact metrics show obvious mismatch, `ResolveDevelopRenderedRefineIntent` can request a damped authored refinement intent.
- `BuildDevelopAutoCandidateSolve` now turns rendered refine intents into rendered-local authored candidate families: `renderedLocalBrightenMids`, `renderedLocalShadowOpening`, `renderedLocalHighlightRestraint`, and `renderedLocalContrastShape`.
- Rendered-local candidate families are preserved through generic duplicate clustering because they are seeded by measured output mismatch, and `ApplyRenderedCandidateFeedbackToSolve` prefers the matching rendered-local family over the older synthetic `renderedFeedbackRefine` fallback when it survives.
- Selected-render refinement can also react to compact edge/halo risk by requesting more highlight/local-contrast restraint.
- Repeated same-direction rendered refinements are suppressed when the previous refinement did not improve enough, or after a repeated intent has already had a chance to prove improvement.
- Repeated rendered merge/adoption attempts now stop explicitly when the same comparison pair has converged or is not reducing the rendered score gap enough. Stop reasons include `renderedMergeConverged`, `renderedMergeDidNotImprove`, and `renderedAdoptionNoFurtherGain`.
- Stale candidate render results are ignored unless their dirty generation and solve fingerprint still match the current Develop node.
- Rendered feedback writes a bounded `autoCandidateRenderedFeedbackHistory` with selected/best ids, rendered scores, action, stop reason, and refinement reason when applicable. The solve diagnostics separately record `autoCandidateRenderedFeedbackAction` as `adopted`, `merged`, or `refined`.
- Rendered feedback history now stores compact selected/best metric snapshots so the next solve can compare actual rendered states across passes.
- Follow-up solves now stop with `renderedMetricsStable` when the current rendered best has no meaningful score gain and its compact rendered metrics are effectively unchanged from the previous rendered-best state.
- Stop reasons now explicitly cover failed renders, no rendered best, selected candidate still best, score below quality floor, no meaningful rendered improvement, pass limit, already-applied feedback, and immediate reversal of recent rendered feedback.
- Stop reasons also cover repeated refinement cases: `renderedRefineDidNotImprove` and `renderedRefineRepeatedIntent`.
- Auto status readout now shows candidate render metrics status/counts.
- Smoke validation covers rejected-candidate memory persistence/suppression, rendered metric duplicate-distance separation, local rendered refine-intent resolution, direct rendered-feedback adoption, authored rendered-feedback merge, carried-forward rendered-feedback candidate continuity, authored rendered-feedback refinement, repeated-refinement suppression, and no-improvement stopping.

Explicit non-goals for this pass:

- No persistent thumbnail cache.
- No candidate gallery, hover preview, side-by-side compare, or user candidate picker.
- No user merge UI. Only automatic authored merge for compatible solver feedback exists.
- No final-image pixel blending.
- No learning from accepted/rejected candidates.
- No full scheduled iterative convergence loop.
- No full spatial damage maps or perceptual scoring UI.

Tradeoffs and limitations:

- Candidate rendered metrics can adopt a better survivor, synthesize an authored merge, refine the selected candidate through a rendered-local authored candidate family / capped fallback solve, or stop on compact rendered metric stability. Repeated failed refinements are suppressed, but this still does not drive a full user-visible candidate gallery or keep searching until no global improvement remains across richer candidate families.
- Visual-risk metrics are compact rendered-output proxies from RGBA pixels. They are useful for ranking and refinement pressure, but future Guide 03/04/10 passes still need proper spatial damage maps, local EV-map comparison, and richer perceptual diagnostics.
- The 3x3 local summary is a coarse rendered comparison/refinement substrate. It helps distinguish candidates with different local brightness/contrast structure and directs damped authored refinements, but it is not a true local EV map, subject-region analyzer, or user-visible spatial diagnostic.
- Rendered duplicate clustering is based on a compact weighted metric distance. It prevents obvious redundant rendered candidates from driving duplicate feedback, but it is not a perceptual thumbnail-similarity system.
- Prior rendered-feedback candidates are only carried forward while rendered-feedback iteration is active. That guard keeps synthetic convergence candidates from leaking into unrelated normal Auto recalibrations.
- Rejected-candidate memory is intentionally scoped to a quantized candidate context fingerprint. Mode/guidance/stat changes can reconsider a candidate; this is loop prevention, not a permanent preference or learning system.
- Candidate render payloads bias the existing solved payload from candidate guidance. They do not re-run the full Auto solve inside the worker.
- Only selected/surviving candidates are rendered; rejected/duplicate candidates are documented but skipped to avoid wasted render time. Survivor render slots are diversity-aware, but still bounded and heuristic rather than a final perceptual gallery-selection system.
- The existing smoke validation covers rejected-candidate memory, rendered-feedback candidate adoption, authored merge, authored refinement, repeated-refinement suppression, and no-improvement stopping, but there is not yet a dedicated candidate-render thumbnail/gallery validation path.

## 2026-06-10 - Add Guide 03 Parameter Candidate Solve Foundation

Decision:

Add the first Develop Auto candidate-solving layer as authored parameter candidates inside the existing `ApplyDevelopAutoSolve` pathway, rather than creating a separate render node kind or a final-image pixel blending system.

Rationale:

Guide 03 says Auto should explore, compare, eliminate, converge, and expose authored settings. The current code already has a useful render-feedback loop through integrated ToneCurve scene stats, and Develop already authors RAW settings, Scene Prep settings, and integrated ToneCurve JSON. A conservative first step is to generate candidate authored guidance from the current stats, select or merge the best candidate in settings space, then drive the existing RAW + Scene Prep + Tone solve from that selected candidate.

Implemented:

- Candidate families currently supported by available stats/settings: Base Solve, Protect Highlights, Brighter Mids, More Range, Preserve Mood, More Contrast, and Cleaner Shadows.
- Candidate scoring uses current scalar signals: highlight pressure, clipping ratio, HDR spread, darkness/shadow rescue need, noise risk, texture confidence, flat-scene need, and Auto mode intent.
- Damaged candidates can be rejected for highlight, noise, or flattening risk.
- Near-duplicate candidates are clustered using authored-guidance distance.
- Close compatible top candidates can be merged by interpolating authored guidance, not by blending pixels.
- Selected/merged candidate guidance becomes the authored state that drives the existing RAW exposure/cleanup, Scene Prep, and integrated Tone guidance.
- Candidate diagnostics and convergence metadata are written to integrated ToneCurve JSON.
- The Auto status readout shows the selected candidate summary.

Explicit non-goals for this pass:

- No rendered thumbnail/gallery system.
- No user candidate picker, hover preview, side-by-side compare, or manual candidate merge UI.
- No final-image pixel blending.
- No full scheduled multi-pass render convergence engine.
- No subject-importance brush or automatic subject detection.
- No WB/color candidate engine.
- No denoise/demosaic overhaul.
- No candidate choice/rejection learning model or settings controls.
- No graph controls.

Tradeoffs and limitations:

- This is a parameter-candidate foundation, not the final rendered candidate workflow described in Guide 03.
- Candidate scoring is heuristic and scalar; it does not yet use spatial halo maps, skin/memory-color checks, subject regions, perceptual thumbnail similarity, or rendered image comparisons.
- Candidate convergence currently records selected-candidate fingerprint/pass stability. It does not schedule repeated render passes until no further improvement remains.
- Live render-feedback stats are still intentionally excluded from Auto trigger hashes to avoid oscillation. The solver can use the latest stats when an Auto solve is requested, but those stats do not continuously retrigger solves by themselves.

## 2026-06-10 - Use Durable Tracking Docs for Develop Continuation

Decision:

Use the Develop tracker, source map, decisions file, pass protocol, and deferred-scope file as the durable continuation record for all future Develop node passes.

Rationale:

Develop work spans many planning guides and long-running sessions. A guide-level table alone is not enough to prevent rework, skipped guide requirements, or accidental claims that deferred features already exist. The tracking set gives future passes a repeatable start/end protocol, exact source locations, guide requirement checklists, durable decisions, and a central deferred-feature ownership map.

Tracking files:

- `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`
- `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`
- `docs/engineering/develop/DEVELOP_DECISIONS.md`
- `docs/engineering/develop/DEVELOP_PASS_PROTOCOL.md`
- `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`

Rules:

- `AGENTS.md` is the short entrypoint and must point to the deeper Develop tracking docs.
- The tracker records current truth, not original pass assumptions. If a later guide has become `Partial`, future passes must continue from that state instead of resetting it to `Not Started`.
- A numbered guide should not be marked `Complete` unless code, requirement checklist, validation, source-map updates, and decision/deferred notes are current.
- Deferred features may be mentioned or prepared for, but they must not be claimed implemented until their owning guide/pass updates the tracker and deferred-scope file.
- If code and planning docs disagree, verify code and `docs/engineering/develop/spec_sources/DEVELOP_NODE_CONTEXT.txt`, then document the mismatch rather than silently reinterpreting the guide.
- If a later prompt contains stale status assumptions, continue from tracker/code truth and document the discrepancy instead of resetting or reimplementing completed/partial work.

## 2026-06-10 - Separate Auto Brightness Intent from Manual RAW EV Naming

Decision:

For the Guide 02 partial implementation, keep the existing serialized `DevelopAutoGuidance::exposureBias` field for graph compatibility, but present it in Auto UI as `Brightness Intent`. Present Manual exposure as `RAW Exposure / EV` with explicit scene-linear scale help.

Rationale:

Guide 02 requires Stack to stop collapsing rendered brightness and literal RAW/data exposure into one vague user concept. Auto controls are solver-facing and may coordinate RAW EV, local exposure, and tone together. Manual should expose the precise authored RAW EV multiplication. Renaming the serialized field would create unnecessary save/load risk, so this pass changes user-facing semantics and diagnostics while preserving storage compatibility.

Implementation notes:

- Auto UI label changed from `Exposure Bias` to `Brightness Intent`.
- Auto tooltip explains that the control may adjust RAW EV, local exposure, and tone together.
- Manual UI label changed from `RAW Baseline Exposure` to `RAW Exposure / EV`.
- Manual help states that +1 EV doubles scene-linear values and -1 EV halves them before later rendering stages.
- Auto status readout now shows brightness intent, RAW scale, local EV distribution, tone contrast, and tone placement where available.
- Integrated ToneCurve JSON receives `autoBrightnessIntent` and `autoRawExposurePreferenceEv` as diagnostic/future-use aliases.
- Integrated ToneCurve JSON also receives first-pass exposure diagnostic aliases for authored RAW EV/scale, local EV min/max bias, clipping ratio, highlight pressure, noise risk, HDR spread, and recommended base EV.
- Auto status readout shows numeric clipping/noise/HDR diagnostics from existing tone scene stats. Spatial maps and candidate damage metrics are deferred.

Explicit non-goals for this pass:

- No serialized field rename.
- No new exposure candidate solver.
- No graph-style exposure/range control.
- No spatial clipping/noise map or visual damage metric UI.
- No View Transform or Develop output change.
- No claim that local exposure or tone can recover missing clipped data.

## 2026-06-10 - Add Auto Intent / Mode Framework to Develop

Decision:

Add a durable Auto intent/mode framework inside Develop Auto guidance while preserving the existing `RawDevelop` node identity, `Auto` / `Manual` UI mode split, current RAW + Scene Prep + integrated ToneCurve architecture, and scene-linear output behavior.

Rationale:

Develop Auto is a solver guided by user intent, not a static preset stack. The default should be Natural Finished so adding a Develop node can produce a realistic, usable image immediately. Other modes should bias the solver's choices across existing RAW exposure, scene prep, cleanup, and integrated tone guidance without becoming separate render algorithms.

Modes added:

- Natural Finished
- Clean Base
- Flat Editing Base
- Bright Natural
- Dark Natural
- Punchy / High Contrast
- Maximum Range / Detail

Implementation notes:

- `NaturalFinished` is the default for new Develop nodes and old serialized graphs that do not contain the new field.
- Auto intent is serialized as a stable string in `developAutoGuidance.autoIntent`.
- Unknown intent strings safely fall back to `NaturalFinished`.
- Reset Auto resets numeric guidance but preserves the selected intent because intent is a user choice.
- Auto Calibrate re-solves through the selected intent.
- Changing intent changes the auto solve trigger hash and forces a meaningful re-solve.
- Mode profiles currently apply conservative biases over the existing `ApplyDevelopAutoSolve` pathways. They do not replace the solver.
- Integrated ToneCurve JSON receives the selected intent and mode-adjusted guidance so downstream auto tone behavior has the same intent context.

Explicit non-goals for this pass:

- Multi-candidate render gallery.
- Candidate merging.
- Learning from accepted or rejected candidates.
- Subject importance brush or edge-aware subject overlay.
- Graph-style controls.
- Full iterative convergence loop.
- Pre/post demosaic denoise redesign.
- Advanced demosaic method overhaul.
- Full color-management rewrite.
- New View Transform behavior.
- AI/ML subject detection.
- Renaming `RawDevelop` serialized kind or node identity.
- Removing or disabling forced Scene Prep / integrated Tone.

Tradeoffs and limitations:

- The current mode profiles are first-pass target-profile biases, not finished image-quality tuning.
- Maximum Range / Detail is implemented as visible range fitting and stronger highlight/shadow protection. It does not claim to recover missing clipped data.
- Manual mode remains the same render pipeline with direct authored controls. Full Manual-to-Auto bias preservation is deferred to Guide 09.
- Graph-style intent controls remain a documented future direction. This pass uses a combo selector so the architecture has stable intent state before graph controls are built.
- Because the repo already had broad uncommitted changes, this pass kept edits scoped to Develop Auto state, solver biasing, UI, serialization, tests, and docs.


