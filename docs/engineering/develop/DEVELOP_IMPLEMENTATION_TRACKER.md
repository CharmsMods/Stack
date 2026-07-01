# Develop Implementation Tracker

Last updated: 2026-06-22

Planning source:

- `docs/engineering/develop/spec_sources/DEVELOP_NODE_CONTEXT.txt`
- `docs/engineering/develop/spec_sources/Stack_Develop_Node_Detailed_Guides/00_INDEX_README.txt`
- `docs/engineering/develop/spec_sources/Stack_Develop_Node_Detailed_Guides/`

Current status guard:

- Guide 01 is complete and should not be reimplemented.
- Guides 02, 03, 04, 05, 06, and 08 are partial and must be continued from their requirement checklists and decision records, not reset to `Not Started`.
- Guide 09 remains `Not Started`; the separate graph-side manual chain (`RAW Decode -> Tone Curve -> View Transform`) is real substrate, but it is not yet Manual-to-Auto bias preservation, locks, or expert handoff inside `Develop`.
- Latest Guide 05 processing increment: Develop Auto now writes `SubjectRefinedMapV1`, a compact refined subject importance/confidence diagnostic derived from `SubjectImportanceMapV1`, neighbor support, and solved subject/readability/protection/mood axes. It is serialized inside `SubjectSceneIntentV1`, mirrored as top-level `autoSubjectSceneRefinedMap*` diagnostics, carried into candidate score components/signals, exposed in Auto status, and visible through selected-node `Show Refined Map` viewport controls. This is a real refined diagnostic/scoring substrate, not true image-edge segmentation, semantic/AI subject detection, graph-control UI, candidate gallery, or Manual handoff.
- Latest Guide 03 performance/stability increment: Develop candidate feedback now records compact `CandidateRenderTimingV1` telemetry for rendered candidate probes. The worker reports final graph, final readback, final analysis, pre-finish graph/readback/analysis, total elapsed, and slowest candidate timing into integrated Tone JSON; the Auto status readout shows total graph/readback/analysis timing and the slowest candidate.
- Latest renderer RAW stage organization increment: graph-side upstream RAW source traversal, embedded/file RAW data loading, shared RAW base rendering for RAW Decode and Develop, stage-cache hit adoption, and raw-base graph-cache publication now live in `src/Renderer/Internal/RenderPipelineGraphRawStages.cpp`. `RenderPipelineGraphExecution.cpp` keeps graph traversal and delegates RawDevelop pre-finish/final rendering to `RenderPipelineGraphRawDevelopNode.cpp`. This is structural only and does not change RAW Decode behavior, RawDevelop behavior/status, guide status, or deferred feature status.
- Latest renderer graph-analysis organization increment: Data Math average-input collection, first Data Math average-input lookup, channel-socket classification, and recursive scalar-vs-image socket classification now live in `src/Renderer/Internal/RenderPipelineGraphAnalysis.cpp`. `RenderPipelineGraphExecution.cpp` keeps fingerprint/evaluation/cache-use decisions that consume those helpers. This is structural only and does not change graph socket behavior, Data Math behavior, Develop behavior/status, guide status, or deferred feature status.
- Latest renderer DataMath node organization increment: graph-side DataMath node execution now lives in `src/Renderer/Internal/RenderPipelineGraphDataMathNode.cpp`, including scalar/image input resolution, multi-input Average accumulation/division, optional mask/base blending, blank base creation, and simple two-input math dispatch. `RenderPipelineGraphAnalysis.cpp` keeps DataMath input-list and scalar-vs-image analysis, `RenderPipelineNodePasses.cpp` keeps the low-level `RenderDataMath` shader pass, and `RenderPipelineGraphExecution.cpp` keeps traversal/cache publication. This is structural only and does not change DataMath behavior, graph socket behavior, Develop behavior/status, guide status, or deferred feature status.
- Latest renderer HDR organization increment: graph-side HDR Merge representative-source lookup, HDR input metadata context resolution, and metadata/manual exposure reliability resolution now live in `src/Renderer/Internal/RenderPipelineGraphHdrMerge.cpp`. `RenderPipelineGraphExecution.cpp` keeps HDR node dispatch, and `RenderPipelineHdrMergePass.cpp` keeps GL rendering/alignment/deghost work. This is structural only and does not change HDR Merge rendering, Develop behavior/status, guide status, or deferred feature status.
- Latest renderer graph-cache organization increment: generic graph image/mask cache entry deletion, full cache destruction, per-key release, store/replace, and inactive-node pruning now live in `src/Renderer/Internal/RenderPipelineGraphTextureCache.cpp`. `RenderPipelineGraphExecution.cpp` keeps cache-use decisions inside graph traversal/evaluation, and `RenderPipelineResources.cpp` keeps full-cache invalidation orchestration. This is structural only and does not change graph cache behavior, Develop behavior/status, guide status, or deferred feature status.
- Latest renderer RawDevelop node organization increment: hidden pre-finish stage-cache lookup, scene-prep rendering, integrated ToneCurve finish execution, finish-mask blending, auto rewrite feedback collection, hidden pre-finish stage-cache publication, and RawDevelop black-output guardrails now live in `src/Renderer/Internal/RenderPipelineGraphRawDevelopNode.cpp`. `RenderPipelineGraphExecution.cpp` keeps traversal and delegates the RawDevelop branch; `RenderPipelineGraphRawStages.cpp` keeps shared RAW source/base rendering; `RenderPipelineRawDevelopStageCache.cpp` keeps stage-cache memory policy. This is structural only and does not change RawDevelop rendering, RAW Decode behavior, Develop behavior/status, guide status, or deferred feature status.
- Latest renderer Raw Detail node organization increment: graph-side Raw Detail Auto Mask / Fusion execution now lives in `src/Renderer/Internal/RenderPipelineGraphRawDetailNode.cpp`, including Raw Detail Auto Mask and Fusion mask-output dispatch, Fusion image-output dispatch, auto-mask-source setting inheritance, debug-preview routing, generated-mask handoff, and Pre-Local Exposure summary publication. `RenderPipelineRawDetailPasses.cpp` keeps low-level Auto Gain, Raw Detail Auto Mask, Raw Detail Fusion, and Pre-Local Exposure shader/pass primitives, and `RenderPipelineGraphExecution.cpp` keeps traversal/cache publication. This is structural only and does not change Raw Detail behavior, Pre-Local Exposure behavior, Develop behavior/status, guide status, or deferred feature status.
- Latest renderer Layer node organization increment: graph-side Layer node execution now lives in `src/Renderer/Internal/RenderPipelineGraphLayerNode.cpp`, including layer-registry instantiation, layer JSON deserialization, GL layer execution, ToneCurve auto rewrite feedback collection, default ToneCurve blank-output guardrails, and layer mask blending. `RenderPipelineGraphExecution.cpp` keeps traversal and delegates the Layer branch. This is structural only and does not change Layer rendering, ToneCurve behavior, Develop behavior/status, guide status, or deferred feature status.
- Latest renderer LUT node organization increment: graph-LUT node dispatch, channel-combine fallback, LUT cache requests, shader uniform binding/draw, optional mask blending, and temporary texture cleanup now live in `src/Renderer/Internal/RenderPipelineGraphLutNode.cpp`. Reusable graph target texture/FBO wrappers now live in `src/Renderer/Internal/RenderPipelineGraphRenderTargets.cpp`. `RenderPipelineGraphExecution.cpp` keeps traversal and delegates the LUT branch. This is structural only and does not change LUT rendering, Develop behavior/status, guide status, or deferred feature status.
- Latest renderer LUT organization increment: graph-LUT stage hashing, 1D/3D GL texture upload, texture replacement, cache-key clearing, texture-entry deletion, and inactive-node LUT cache pruning now live in `src/Renderer/Internal/RenderPipelineLutTextureCache.cpp`. `RenderPipelineGraphLutNode.cpp` owns LUT node dispatch/uniform binding, and `RenderPipelinePrograms.cpp` keeps LUT shader/program setup. This is structural only and does not change LUT rendering, Develop behavior/status, guide status, or deferred feature status.
- Latest renderer organization increment: RawDevelop stage snapshot cache operations now live in `src/Renderer/Internal/RenderPipelineRawDevelopStageCache.cpp`. RawDevelop node/base-stage owners call the cache owner for raw-base/pre-finish lookup and store, `RenderPipelineGraphExecutionHelpers.*` keeps shared size/budget constants, and `RenderPipelineResources.cpp` keeps generic resource lifecycle and cache invalidation orchestration. This is structural only and does not change RawDevelop rendering, cache sizing thresholds, candidate feedback, guide status, or deferred feature status.
- Latest graph processing-node organization increment: RAW Neural Denoise, Develop, RAW Decode, Raw Detail Auto Mask/Fusion, HDR Merge, LUT node creation, Develop auto-state trigger/update helpers, Raw Detail hybrid conversion, and manual RAW full-tree construction now live in `src/Editor/Internal/EditorModuleGraphProcessingNodes.cpp`. `src/Editor/Internal/EditorModuleGraphMutation.cpp` keeps general graph/layer mutation, layer/channel split, tone-finish helper flows, link/node removal, utility/output adders, and graph metadata refresh. This is structural only and does not change graph topology, Develop behavior/status, RAW Decode behavior, HDR/LUT behavior, or guide status.
- Latest graph mask organization increment: scope/mask node creation, Custom Mask payload initialization, Image-to-Mask creation, and Tone Curve / Develop finish scoped-mask graph construction now live in `src/Editor/Internal/EditorModuleGraphMaskNodes.cpp`. `src/Editor/Internal/EditorModuleGraphMutation.cpp` keeps general graph/layer mutation, tone-finish helper flows, link/node removal, utility/output adders, and graph metadata refresh; RAW/HDR/LUT processing-node creation now lives in `src/Editor/Internal/EditorModuleGraphProcessingNodes.cpp`. This is structural only and does not change graph mask behavior, Tone Curve scoped-mask behavior, or Develop behavior/status.
- Latest graph mutation organization increment: image/RAW file import, graph-drop image-chain import scheduling, image payload PNG storage bytes, image-source connection, and image-node rotation now live in `src/Editor/Internal/EditorModuleGraphImageNodes.cpp`. `src/Editor/Internal/EditorModuleGraphMutation.cpp` keeps general graph/layer mutation, tone-finish helper flows, link/node removal, utility/output adders, and graph metadata refresh; RAW/HDR/LUT processing-node creation and RAW full-tree construction now live in `src/Editor/Internal/EditorModuleGraphProcessingNodes.cpp`; mask/tone-scope graph mutation lives in `src/Editor/Internal/EditorModuleGraphMaskNodes.cpp`. This is structural only and does not change graph import, RAW source, image rotation, or Develop behavior/status.
- Latest organization increment: Tone Curve point/model/evaluation, auto-analysis/rewrite, UI surfaces, serialization, and tone-family rendering logic are now split from the large Tone layer implementation. `src/Editor/Layers/ToneCurveLayerModel.cpp` owns point editing, coordinate conversion, viewport target/probe state, and curve evaluation; `src/Editor/Layers/ToneCurveLayerAuto.cpp` owns auto calibration request bookkeeping, scene readback/analysis, auto intent solving, authored-state construction/preservation, and auto rewrite feedback capture; `src/Editor/Layers/ToneCurveLayerUI.cpp` owns standalone and Develop-integrated Tone Curve ImGui surfaces, resettable Tone sliders, on-image targeting panels, scoped mask panel, graph preview/editor drawing, and expanded node-surface controls; `src/Editor/Layers/ToneCurveLayerSerialization.cpp` owns Tone Curve JSON serialization/deserialization, point-array JSON helpers, and persisted auto-authored-state JSON; `src/Editor/Layers/ToneLayerRendering.cpp` owns tone-family shader sources, passthrough rendering, GL initialize/execute/destructor methods, and Tone Curve LUT upload; `src/Editor/Layers/ToneLayers.cpp` keeps smaller non-Curve tone layer UI/serialization bodies plus Tone Curve effective tone/foundation settings and non-rendering control math. This is structural only and does not change Guide 08 behavior/status.
- Older dated handoff notes are historical snapshots. Use the guide table, current requirement checklist, and `DEVELOP_DEFERRED_SCOPE.md` for current status when they disagree with an older note.

## 2026-06-22 Organization Refactor Handoff - Renderer Graph Raw Detail Node Owner

- Status delta: no Develop guide status changed. This pass is structural only and does not change Raw Detail Auto Mask behavior, Raw Detail Fusion behavior, Pre-Local Exposure behavior, graph traversal, graph cache behavior, Develop behavior, guide status, or deferred feature status.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, `src/Renderer/Internal/RenderPipelineGraphRawDetailNode.cpp`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, and `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: moved graph-side Raw Detail Auto Mask and Fusion mask-output dispatch, Fusion image-output dispatch, auto-mask-source setting inheritance, debug-preview routing, generated-mask handoff, and Pre-Local Exposure summary publication into `RenderPipelineGraphRawDetailNode.cpp`.
- Implemented: kept graph traversal, fingerprinting, generic graph-cache store/release, and node-family routing in `RenderPipelineGraphExecution.cpp`; kept low-level Auto Gain, Raw Detail Auto Mask, Raw Detail Fusion, and Pre-Local Exposure shader/pass primitives in `RenderPipelineRawDetailPasses.cpp`.
- Remaining work: future Raw Detail graph-dispatch or inherited-setting fixes should stay in `RenderPipelineGraphRawDetailNode.cpp`; future scene-prep/local-exposure render math should stay in `RenderPipelineRawDetailPasses.cpp`. Future renderer splits should target another complete node-family owner only if the boundary is clear and validation coverage is appropriate.
- Deferred-scope changes: none. This did not implement new Scene Prep controls, local-exposure renderer redesign, graph controls, candidate gallery UI, Manual-to-Auto handoff, staged renderer behavior, View Transform changes, or new RAW/HDR/LUT processing behavior.
- Validation run: initial `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` after adding the new source regenerated CMake and failed at link because the current MSBuild pass did not compile the regenerated source list; rerun of the same build command passed and compiled `RenderPipelineGraphRawDetailNode.cpp`; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-22 Organization Refactor Handoff - Renderer Graph DataMath Node Owner

- Status delta: no Develop guide status changed. This pass is structural only and does not change DataMath behavior, scalar socket behavior, graph traversal, graph cache behavior, Develop behavior, guide status, or deferred feature status.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, `src/Renderer/Internal/RenderPipelineGraphDataMathNode.cpp`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, and `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: moved graph-side DataMath scalar/image input resolution, multi-input Average accumulation/division, optional mask/base blending, blank base creation, and simple two-input math dispatch into `RenderPipelineGraphDataMathNode.cpp`.
- Implemented: kept graph traversal, fingerprinting, generic graph-cache store/release, and node-family routing in `RenderPipelineGraphExecution.cpp`; kept DataMath input-list and scalar-vs-image analysis in `RenderPipelineGraphAnalysis.cpp`; kept the low-level `RenderDataMath` shader pass in `RenderPipelineNodePasses.cpp`.
- Remaining work: future DataMath render-execution fixes should stay in `RenderPipelineGraphDataMathNode.cpp`; future DataMath input-list or scalar-socket classification fixes should stay in `RenderPipelineGraphAnalysis.cpp`; future low-level DataMath shader-pass fixes should stay in `RenderPipelineNodePasses.cpp`. Future renderer splits should target another complete node-family owner only if the boundary is clear and validation coverage is appropriate.
- Deferred-scope changes: none. This did not implement graph controls, candidate gallery UI, Manual-to-Auto handoff, staged renderer behavior, View Transform changes, or new RAW/HDR/LUT processing behavior.
- Validation run: initial `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` after adding the new source regenerated CMake and failed at link because the current MSBuild pass did not compile the regenerated source list; rerun of the same build command passed and compiled `RenderPipelineGraphDataMathNode.cpp`; final build after moving the remaining scalar-output DataMath path into the owner passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-22 Organization Refactor Handoff - Renderer Graph Layer Node Owner

- Status delta: no Develop guide status changed. This pass is structural only and does not change Layer rendering, ToneCurve behavior, layer mask behavior, graph traversal, graph cache behavior, Develop behavior, guide status, or deferred feature status.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, `src/Renderer/Internal/RenderPipelineGraphLayerNode.cpp`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, and `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: moved layer-registry instantiation, layer JSON deserialization, GL layer execution, ToneCurve auto rewrite feedback collection, default ToneCurve blank-output guardrails, and layer mask blending into `RenderPipelineGraphLayerNode.cpp`.
- Implemented: kept graph traversal, fingerprinting, generic graph-cache store/release, and node-family routing in `RenderPipelineGraphExecution.cpp`; kept layer-specific UI/model/serialization/rendering in the layer files; kept reusable target/FBO helpers in `RenderPipelineGraphRenderTargets.cpp`.
- Remaining work: future graph-side Layer execution fixes should stay in `RenderPipelineGraphLayerNode.cpp`; future ToneCurve model/UI/serialization/rendering fixes should stay in the focused ToneCurve/ToneLayer files. Future renderer splits should target another complete node-family owner only if the boundary is clear and validation coverage is appropriate.
- Deferred-scope changes: none. This did not implement new layer types, new ToneCurve controls, Guide 08 tone strategy changes, graph controls, candidate gallery UI, Manual-to-Auto handoff, View Transform changes, or new RAW processing behavior.
- Validation run: first `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` regenerated CMake for the new source and failed at link because the current MSBuild pass did not compile the regenerated source list; rerun of the same build command passed and compiled `RenderPipelineGraphLayerNode.cpp`; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-22 Organization Refactor Handoff - Renderer Graph RawDevelop Node Owner

- Status delta: no Develop guide status changed. This pass is structural only and does not change RawDevelop rendering, RAW Decode behavior, scene-prep math, integrated ToneCurve behavior, finish-mask behavior, graph traversal, graph cache behavior, stage-cache memory policy, guide status, or deferred feature status.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, `src/Renderer/Internal/RenderPipelineGraphRawDevelopNode.cpp`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, and `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: moved hidden pre-finish stage-cache lookup, scene-prep rendering, integrated ToneCurve finish execution, finish-mask blending, auto rewrite feedback collection, hidden pre-finish stage-cache publication, and RawDevelop black-output guardrails into `RenderPipelineGraphRawDevelopNode.cpp`.
- Implemented: kept graph traversal, fingerprinting, generic graph-cache store/release, and node-family routing in `RenderPipelineGraphExecution.cpp`; kept shared RAW source lookup/loading/base rendering in `RenderPipelineGraphRawStages.cpp`; kept RawDevelop stage-cache memory policy in `RenderPipelineRawDevelopStageCache.cpp`; kept Raw Detail pass primitives in `RenderPipelineRawDetailPasses.cpp`.
- Remaining work: future Develop pre-finish/final renderer behavior should stay in `RenderPipelineGraphRawDevelopNode.cpp`; future RAW source/base-stage fixes should stay in `RenderPipelineGraphRawStages.cpp`; future stage-cache memory/lifecycle fixes should stay in `RenderPipelineRawDevelopStageCache.cpp`. Future renderer splits should target another complete node-family owner only if the boundary is as clear as these splits.
- Deferred-scope changes: none. This did not implement a full staged renderer, physical RAW-global/scene-prep/finish controller split, sidecar stats bus, graph controls, candidate gallery UI, Manual-to-Auto handoff, View Transform changes, or new RAW processing behavior.
- Validation run: first `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` regenerated CMake for the new source and failed at link because the current MSBuild pass did not compile the regenerated source list; rerun of the same build command passed and compiled `RenderPipelineGraphRawDevelopNode.cpp`; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-22 Organization Refactor Handoff - Renderer Graph LUT Node Owner

- Status delta: no Develop guide status changed. This pass is structural only and does not change LUT rendering, LUT hash/cache behavior, shader/program behavior, graph traversal, graph cache behavior, Develop behavior, guide status, or deferred feature status.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, `src/Renderer/Internal/RenderPipelineGraphLutNode.cpp`, `src/Renderer/Internal/RenderPipelineGraphRenderTargets.cpp`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, and `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: moved graph-LUT image/channel input resolution, channel-combine fallback, LUT texture cache requests, shader uniform binding/draw, optional mask blending, and temporary texture cleanup into `RenderPipelineGraphLutNode.cpp`.
- Implemented: moved reusable graph target texture creation and render-into-target FBO state-save/restore wrapper into `RenderPipelineGraphRenderTargets.cpp`.
- Implemented: kept graph traversal, fingerprinting, cache adoption, and node-family routing in `RenderPipelineGraphExecution.cpp`; kept LUT texture upload/cache lifecycle in `RenderPipelineLutTextureCache.cpp`; kept LUT shader/program creation in `RenderPipelinePrograms.cpp`.
- Remaining work: future LUT node behavior should stay in `RenderPipelineGraphLutNode.cpp`; future LUT GPU upload/cache lifecycle fixes should stay in `RenderPipelineLutTextureCache.cpp`; future renderer splits should target another complete node-family owner only when validation coverage is strong enough.
- Deferred-scope changes: none. This did not implement new LUT controls, color graph controls, color-management redesign, View Transform changes, graph controls, candidate gallery UI, Manual-to-Auto handoff, or new RAW processing behavior.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-22 Organization Refactor Handoff - Renderer Graph RAW Stage Owner

- Status delta: no Develop guide status changed. This pass is structural only and does not change RAW Decode behavior, RawDevelop rendering, RAW loading/cache semantics, graph traversal, graph cache behavior, Develop behavior, guide status, or deferred feature status.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, `src/Renderer/Internal/RenderPipelineGraphRawStages.cpp`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, and `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: moved upstream RAW source traversal, embedded-vs-file RAW data cache hydration, shared RAW base rendering for RAW Decode and Develop, stage-cache hit adoption, raw-base graph-cache publication, and RAW load/render failure logging into `RenderPipelineGraphRawStages.cpp`.
- Implemented: kept graph traversal, RawDecode/RawDevelop branch dispatch, Develop integrated ToneCurve, scene prep, hidden pre-finish handling, finish-mask blending, and black-output guardrails in `RenderPipelineGraphExecution.cpp`.
- Remaining work: future RAW source/base-stage fixes should stay in `RenderPipelineGraphRawStages.cpp`; future Develop pre-finish/final renderer behavior should stay in `RenderPipelineGraphRawDevelopNode.cpp`; future RawDevelop stage-cache memory/lifecycle fixes should stay in `RenderPipelineRawDevelopStageCache.cpp`; future renderer splits should target another clear node-family owner only if its boundary stays clean.
- Deferred-scope changes: none. This did not implement a full staged renderer, physical RAW-global/scene-prep/finish cache split, sidecar stats bus, graph controls, candidate gallery UI, Manual-to-Auto handoff, View Transform changes, or new RAW processing behavior.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed after expected glob regeneration and rerun; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-22 Organization Refactor Handoff - Renderer Graph Analysis Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change scalar socket classification behavior, Data Math input behavior, graph traversal, graph cache behavior, Develop behavior, guide status, or deferred feature status.
- Files changed: `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, `src/Renderer/Internal/RenderPipelineGraphAnalysis.cpp`, `src/Renderer/Internal/RenderPipelineGraphExecutionHelpers.h`, `src/Renderer/Internal/RenderPipelineGraphHdrMerge.cpp`, `src/Renderer/RenderPipeline.h`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, and `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: moved Data Math average-input collection, first Data Math average-input lookup, channel-socket classification, and recursive scalar-vs-image socket classification into `RenderPipelineGraphAnalysis.cpp`.
- Implemented: updated HDR representative-source lookup to reuse the graph-analysis first Data Math input helper instead of owning a duplicate private `RenderPipeline` method.
- Remaining work: future graph socket-classification and Data Math input-list fixes should stay in `RenderPipelineGraphAnalysis.cpp`; future renderer splits should still target a whole RawDevelop/RawDecode branch or a clear node-family owner only when nearby validation coverage is strong.
- Deferred-scope changes: none. This did not implement graph controls, candidate gallery UI, Manual-to-Auto handoff, staged renderer behavior, or new RAW/HDR/LUT processing behavior.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed after expected glob regeneration and rerun; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-22 Organization Refactor Handoff - Renderer Graph HDR Merge Resolver

- Status delta: no Develop guide status changed. This pass is structural only and does not change HDR Merge rendering, exposure/reliability math, graph traversal, graph cache behavior, Develop behavior, guide status, or deferred feature status.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, `src/Renderer/Internal/RenderPipelineGraphHdrMerge.cpp`, `src/Renderer/Internal/RenderPipelineGraphExecutionHelpers.h`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, and `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: moved representative graph source lookup, first Data Math average-input source tracing, HDR input context construction from RAW metadata / Develop exposure, and HDR metadata/manual exposure reliability resolution into `RenderPipelineGraphHdrMerge.cpp`.
- Implemented: kept HDR Merge node dispatch and texture evaluation in `RenderPipelineGraphExecution.cpp`; kept HDR Merge GL rendering, alignment, feature readback, and deghost setup in `RenderPipelineHdrMergePass.cpp`.
- Remaining work: future HDR Merge graph metadata/source-walk fixes should stay in `RenderPipelineGraphHdrMerge.cpp`; future HDR Merge render-pass fixes should stay in `RenderPipelineHdrMergePass.cpp`. Future renderer splits should still target a whole RawDevelop/RawDecode branch or a clear node-family owner only when nearby validation coverage is strong.
- Deferred-scope changes: none. This did not implement graph controls, candidate gallery UI, Manual-to-Auto handoff, View Transform changes, color-management redesign, staged renderer behavior, or new RAW/HDR processing behavior.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed after expected glob regeneration and rerun; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-22 Organization Refactor Handoff - Renderer Graph Texture Cache Owner

- Status delta: no Develop guide status changed. This pass is structural only and does not change graph cache behavior, texture ownership rules, graph traversal, Develop behavior, guide status, or deferred feature status.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, `src/Renderer/Internal/RenderPipelineGraphTextureCache.cpp`, `src/Renderer/Internal/RenderPipelineResources.cpp`, `src/Renderer/Internal/RenderPipelineRawDevelopStageCache.cpp`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, and `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: moved generic graph image/mask cache entry deletion, full cache destruction, per-key release, store/replace, and inactive-node pruning into `RenderPipelineGraphTextureCache.cpp`.
- Implemented: moved RawDevelop stage-cache inactive-node pruning into `RenderPipelineRawDevelopStageCache.cpp`, keeping that specialized cache lifecycle with its clone/store/trim owner.
- Remaining work: future generic graph image/mask texture cache ownership fixes should stay in `RenderPipelineGraphTextureCache.cpp`; future RawDevelop stage-cache memory/lifecycle fixes should stay in `RenderPipelineRawDevelopStageCache.cpp`. Future renderer splits should still target a whole RawDevelop/RawDecode branch or a clear node-family owner only when nearby validation coverage is strong.
- Deferred-scope changes: none. This did not implement a staged renderer, sidecar stats bus, GPU memory telemetry, graph controls, candidate gallery UI, Manual-to-Auto handoff, View Transform changes, or new RAW processing behavior.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed after expected glob regeneration and rerun; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-22 Organization Refactor Handoff - Renderer LUT Texture Cache Owner

- Status delta: no Develop guide status changed. This pass is structural only and does not change LUT rendering behavior, LUT hash behavior, GL upload parameters, graph traversal, Develop behavior, guide status, or deferred feature status.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, `src/Renderer/Internal/RenderPipelineLutTextureCache.cpp`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, and `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: moved graph-LUT stage hashing, 1D/3D GL texture upload, per-stage texture replacement, cache-key clearing, texture-entry deletion, and inactive-node LUT cache pruning into `RenderPipelineLutTextureCache.cpp`.
- Implemented: kept LUT node dispatch and shader uniform binding in `RenderPipelineGraphExecution.cpp`; kept LUT shader/program creation in `RenderPipelinePrograms.cpp`; kept shared graph execution hash helpers in `RenderPipelineGraphExecutionHelpers.*`.
- Remaining work: future LUT GPU upload/cache lifecycle fixes should stay in `RenderPipelineLutTextureCache.cpp`. Future renderer splits should still target a whole RawDevelop/RawDecode branch or a clear node-family owner only when nearby validation coverage is strong.
- Deferred-scope changes: none. This did not implement new LUT controls, color graph controls, color-management redesign, View Transform changes, graph controls, candidate gallery UI, Manual-to-Auto handoff, or new RAW processing behavior.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed after expected glob regeneration and rerun; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - RawDevelop Stage Cache Owner

- Status delta: no Develop guide status changed. This pass is structural only and does not change RawDevelop render behavior, cache sizing thresholds, cache-hit telemetry, candidate feedback, graph traversal, source/output resource lifecycle, guide status, or deferred feature status.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, `src/Renderer/Internal/RenderPipelineResources.cpp`, `src/Renderer/Internal/RenderPipelineRawDevelopStageCache.cpp`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, and `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: moved RawDevelop stage-cache texture cloning, lookup/MRU promotion, store/replace, entry deletion, byte accounting, per-key/global trimming, cache destruction, and validation-facing sizing wrappers into `RenderPipelineRawDevelopStageCache.cpp`.
- Implemented: kept RawDevelop render-branch dispatch and cache hit/store call sites in `RenderPipelineGraphExecution.cpp` at the time of this older handoff; current RawDevelop pre-finish/final rendering now lives in `RenderPipelineGraphRawDevelopNode.cpp`. Shared size/budget helpers remain in `RenderPipelineGraphExecutionHelpers.*`, and generic resource lifecycle/cache invalidation orchestration remains in `RenderPipelineResources.cpp`.
- Remaining work: future RawDevelop stage-cache memory policy, texture ownership, or validation-wrapper fixes should stay in `RenderPipelineRawDevelopStageCache.cpp`. Future renderer splits should target a whole RawDevelop/RawDecode branch or a clear node-family owner only when nearby validation coverage is strong.
- Deferred-scope changes: none. This did not implement the full staged renderer, physical RAW-global/scene-prep/finish cache split, sidecar stats bus, GPU memory telemetry, candidate gallery UI, Manual-to-Auto handoff, graph controls, View Transform changes, or new RAW processing behavior.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests --parallel 1` passed after expected glob regeneration and one transient tab-icon bake retry; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - Graph Processing Node Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change RAW Neural Denoise, Develop, RAW Decode, Raw Detail Auto Mask/Fusion, HDR Merge, LUT, Develop auto-state, Raw Detail hybrid conversion, manual RAW full-tree, graph topology, graph serialization, guide status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleGraphMutation.cpp`, `src/Editor/Internal/EditorModuleGraphProcessingNodes.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`.
- Implemented: moved RAW Neural Denoise, Develop, RAW Decode, Raw Detail Auto Mask/Fusion, HDR Merge, LUT adders, `UpdateDevelopAutoState` trigger hashing, `ConvertRawDetailFusionToHybrid`, `FindUpstreamRawSourceNode`, `NodeKindHasImageOutput`, and `AddFullRawTreeToSource` from `EditorModuleGraphMutation.cpp` into `EditorModuleGraphProcessingNodes.cpp`.
- Implemented: kept general graph/layer mutation, layer/channel split, tone-finish helper flows, link/node removal, utility/output adders, and graph metadata refresh in `EditorModuleGraphMutation.cpp`; image/RAW import remains in `EditorModuleGraphImageNodes.cpp`; scope/mask creation remains in `EditorModuleGraphMaskNodes.cpp`.
- Remaining work: future RAW/HDR/LUT processing-node adders, Develop auto-state trigger/update gates, and manual RAW chain-builder fixes should stay in `EditorModuleGraphProcessingNodes.cpp`; future graph topology command families can be split from `EditorModuleGraphMutation.cpp` by node/link/group/preset owner when a nearby pass touches them.
- Deferred-scope changes: none. This did not implement graph controls, candidate gallery UI, Manual-to-Auto handoff, new RAW processing behavior, HDR/LUT behavior changes, RAW Decode behavior changes, or any Develop feature behavior.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 1` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

## 2026-06-21 Organization Refactor Handoff - Graph Mask Node Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change scope/mask node creation behavior, Custom Mask payload defaults, Image-to-Mask behavior, Tone Curve scoped-mask graph construction, Develop finish scoped-mask behavior, graph topology behavior, graph serialization, guide status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleGraphMutation.cpp`, `src/Editor/Internal/EditorModuleGraphMaskNodes.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`.
- Implemented: moved `AddScopeNodeAt`, `AddMaskNodeAt`, `AddMaskCombineNodeAt`, `AddMaskUtilityNodeAt`, `AddCustomMaskNodeAt`, `AddImageToMaskNodeAt`, `CreateToneCurveSelectionMask`, and the `ToGraphMaskCombineMode` helper from `EditorModuleGraphMutation.cpp` into `EditorModuleGraphMaskNodes.cpp`.
- Implemented: kept general graph/layer mutation, Develop/RAW/LUT/output/link topology, RAW full-tree construction, tone-finish helper flows, link/node removal, output/composite graph mutation, and graph metadata refresh in `EditorModuleGraphMutation.cpp`.
- Remaining work: future mask-node creation, Image-to-Mask creation, Custom Mask creation defaults, and Tone Curve / Develop scoped-mask graph construction fixes should stay in `EditorModuleGraphMaskNodes.cpp`; future graph topology command families can be split from `EditorModuleGraphMutation.cpp` by node/link/group/preset owner when a nearby pass touches them.
- Deferred-scope changes: none. This did not implement graph controls, candidate gallery UI, Manual-to-Auto handoff, new mask behavior, new scoped-mask behavior, or any Develop feature behavior.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 1` passed after a parallel asset-bake retry; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

## 2026-06-21 Organization Refactor Handoff - Graph Image Node Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change image import behavior, RAW source import behavior, graph-drop chain import scheduling behavior, image payload PNG bytes, image-node rotation behavior, active source connection behavior, graph topology behavior, Develop behavior, graph serialization, guide status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleGraphMutation.cpp`, `src/Editor/Internal/EditorModuleGraphImageNodes.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_HOTSPOT_MAP.md`.
- Implemented: moved image decode/PNG storage helpers, `PromptAddImageNodeAt`, `AddImageNodeFromFile`, `AddRawSourceNodeFromFile`, `AddImageNodeFromPayload`, `AddRawSourceNodeFromPayload`, `RequestGraphImageChainImports`, `StartGraphImageChainImport`, `ConnectGraphImageNode`, and `RotateImageNode` from `EditorModuleGraphMutation.cpp` into `EditorModuleGraphImageNodes.cpp`.
- Implemented: kept general graph/layer mutation, Develop/RAW processing node topology, RAW full-tree construction, link/node removal, tone-finish helpers, mask/tone-scope graph mutation, output/composite graph mutation, and graph metadata refresh in `EditorModuleGraphMutation.cpp`.
- Remaining work: future image/RAW import and runtime image-node mutation fixes should stay in `EditorModuleGraphImageNodes.cpp`; future graph topology command families can be split from `EditorModuleGraphMutation.cpp` by node/link/group/preset owner when a nearby pass touches them.
- Deferred-scope changes: none. This did not implement graph controls, candidate gallery UI, Manual-to-Auto handoff, new image import behavior, new RAW loading behavior, or any Develop feature behavior.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

## 2026-06-21 Organization Refactor Handoff - Tone Curve UI Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change Tone Curve UI behavior, button/slider labels, graph editor behavior, targeting/scoped-mask behavior, Develop integrated Finish Tone behavior, standalone Tone Curve behavior, effective tone/foundation math, serialization keys/schemas/content, shader/LUT behavior, auto calibration behavior, graph serialization, Guide 08 status, or deferred feature status.
- Files changed: `src/Editor/Layers/ToneLayers.cpp`, `src/Editor/Layers/ToneCurveLayerUI.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `RenderUI`, `RenderDevelopBridgeControls`, `RenderDevelopFinishGraphPanel`, `RenderDevelopPreparedControlsPanel`, `RenderDevelopFoundationControlsPanel`, `RenderDevelopTargetingPanel`, `RenderDevelopScopedMaskPanel`, `RenderScopedMaskPanel`, `RenderDevelopPreparedGraphPreviewPanel`, `NotifyUpstreamDevelopChanged`, `GetNodeSurfaceSpec`, `RenderExpandedNodeSurface`, and `RenderCurveEditor` plus Tone Curve UI helper labels, resettable sliders, and curve drawing helpers from `ToneLayers.cpp` into `ToneCurveLayerUI.cpp`.
- Implemented: kept effective tone/foundation math and region-target mutation in `ToneLayers.cpp`; point/model/evaluation in `ToneCurveLayerModel.cpp`; auto-analysis/rewrite in `ToneCurveLayerAuto.cpp`; serialization in `ToneCurveLayerSerialization.cpp`; rendering in `ToneLayerRendering.cpp`.
- Remaining work: future Tone Curve panel, graph editor, on-image targeting, scoped-mask, and expanded node-surface changes should stay in `ToneCurveLayerUI.cpp`. Future effective-setting math can become a separate owner later if it grows.
- Deferred-scope changes: none. This did not implement the Guide 08 tone redesign, mode-specific final tone strategy, graph controls, Manual-to-Auto handoff, View Transform changes, candidate gallery UI, user picker/merge controls, denoise redesign, color graph controls, or any behavior change.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

## 2026-06-21 Organization Refactor Handoff - Tone Curve Serialization Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change Tone Curve JSON keys/schemas/content, load defaults/fallback behavior, point serialization behavior, persisted auto-authored-state behavior, shader/LUT behavior, auto calibration behavior, Develop integrated Finish Tone behavior, standalone Tone Curve behavior, graph serialization, Guide 08 status, or deferred feature status.
- Files changed: `src/Editor/Layers/ToneLayers.cpp`, `src/Editor/Layers/ToneCurveLayerSerialization.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved Tone Curve point-array JSON helpers, auto-authored-state JSON helpers, and `ToneCurveLayer::Serialize`/`Deserialize` from `ToneLayers.cpp` into `ToneCurveLayerSerialization.cpp`.
- Implemented: kept point/model/evaluation in `ToneCurveLayerModel.cpp`, auto-analysis/rewrite in `ToneCurveLayerAuto.cpp`, shader/GL/LUT rendering in `ToneLayerRendering.cpp`, and effective tone settings/non-rendering controls in `ToneLayers.cpp`. Visible Tone Curve UI surfaces were later moved to `ToneCurveLayerUI.cpp`.
- Remaining work: future Tone Curve schema/default/fallback changes should stay in `ToneCurveLayerSerialization.cpp`; Tone Curve ImGui surface changes now live in `ToneCurveLayerUI.cpp`.
- Deferred-scope changes: none. This did not implement the Guide 08 tone redesign, mode-specific final tone strategy, graph controls, Manual-to-Auto handoff, View Transform changes, candidate gallery UI, user picker/merge controls, denoise redesign, color graph controls, or any behavior change.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

## 2026-06-21 Organization Refactor Handoff - Tone Layer Rendering Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change tone shader source text, GL uniform binding behavior, passthrough behavior, Tone Curve LUT values, auto calibration behavior, serialization keys/schemas/content, Develop integrated Finish Tone behavior, standalone Tone Curve behavior, View Transform behavior, graph serialization, Guide 08 status, or deferred feature status.
- Files changed: `src/Editor/Layers/ToneLayers.cpp`, `src/Editor/Layers/ToneLayerRendering.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved tone-family shader strings, passthrough shader/program helpers, `InitializeGL`, `Execute`, render-resource destructors, and `ToneCurveLayer::UpdateLut` from `ToneLayers.cpp` into `ToneLayerRendering.cpp` for Tone Mapper, Tone Curve, Tone Equalizer, View Transform, and Shadows/Highlights.
- Implemented: kept Tone Curve point/model/evaluation in `ToneCurveLayerModel.cpp`, auto-analysis/rewrite in `ToneCurveLayerAuto.cpp`, and the visible UI surfaces, Tone Curve serialization glue, effective tone/foundation settings, and non-rendering controls in `ToneLayers.cpp` at the time of this render split. Tone Curve serialization was later moved to `ToneCurveLayerSerialization.cpp`; Tone Curve UI surfaces were later moved to `ToneCurveLayerUI.cpp`.
- Remaining work: future shader source, GL uniform binding, passthrough, and LUT upload changes should stay in `ToneLayerRendering.cpp`; future Tone Curve schema/default/fallback changes should stay in `ToneCurveLayerSerialization.cpp`; future Tone Curve ImGui surface changes should stay in `ToneCurveLayerUI.cpp`.
- Deferred-scope changes: none. This did not implement the Guide 08 tone redesign, mode-specific final tone strategy, graph controls, Manual-to-Auto handoff, View Transform changes, candidate gallery UI, user picker/merge controls, denoise redesign, color graph controls, or any behavior change.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

## 2026-06-21 Organization Refactor Handoff - Tone Curve Auto Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change Tone Curve auto-analysis math, auto calibration request behavior, authored-state preservation, rewrite feedback contents, shader/LUT behavior, serialization keys/schemas/content, Develop integrated Finish Tone behavior, standalone Tone Curve behavior, graph serialization, Guide 08 status, or deferred feature status.
- Files changed: `src/Editor/Layers/ToneLayers.cpp`, `src/Editor/Layers/ToneCurveLayerAuto.cpp`, `src/Editor/Layers/ToneCurveLayerModel.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `SetAutoRewriteRenderContext`, `SetDevelopScenePrepToneBudget`, `TakePendingAutoRewriteFeedback`, `ApplyAutoRewriteFeedback`, `RequestAutoCalibration`, `UpdateAutoSceneAnalysis`, `SolveAutoToneIntent`, `BuildAutoAuthoredStateFromIntent`, `CaptureCurrentAutoAuthoredState`, `ApplyUserAdjustmentsToAutoAuthoredState`, `ApplyAuthoredStateForRender`, `CapturePendingAutoRewriteFeedback`, and `ClearPendingAutoRewriteFeedback` from `ToneLayers.cpp` into `ToneCurveLayerAuto.cpp`, with file-local auto helper/readback functions.
- Implemented: kept layer execution, shader source usage, LUT texture upload, serialization/deserialization glue, effective tone/foundation settings, standalone node UI, Develop integrated Finish Tone UI, and point/model/evaluation owner boundaries unchanged at the time of this auto split. Shader/source and GL execution ownership was later moved to `ToneLayerRendering.cpp`; Tone Curve serialization was later moved to `ToneCurveLayerSerialization.cpp`; Tone Curve UI surfaces were later moved to `ToneCurveLayerUI.cpp`.
- Remaining work: future auto scene analysis/rewrite and authored auto-state preservation changes should stay in `ToneCurveLayerAuto.cpp`; point/model/evaluation fixes should stay in `ToneCurveLayerModel.cpp`; shader/source and GL execution fixes should stay in `ToneLayerRendering.cpp`; Tone Curve schema/default/fallback fixes should stay in `ToneCurveLayerSerialization.cpp`; Tone Curve ImGui surface fixes should stay in `ToneCurveLayerUI.cpp`.
- Deferred-scope changes: none. This did not implement the Guide 08 tone redesign, mode-specific final tone strategy, graph controls, Manual-to-Auto handoff, View Transform changes, candidate gallery UI, user picker/merge controls, denoise redesign, color graph controls, or any behavior change.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

## 2026-06-21 Organization Refactor Handoff - Tone Curve Model Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change Tone Curve point behavior, curve evaluation math, coordinate conversion math, viewport targeting behavior, auto-analysis/rewrite behavior, shader/LUT behavior, serialization keys/schemas/content, Develop integrated Finish Tone behavior, standalone Tone Curve behavior, graph serialization, or deferred feature status.
- Files changed: `src/Editor/Layers/ToneLayers.cpp`, `src/Editor/Layers/ToneCurveLayerModel.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved Tone Curve point reset/presets, point sanitization, point add/delete/move, curve-input/scene-value conversion, viewport probe/target state helpers, and curve evaluation helpers from `ToneLayers.cpp` into `ToneCurveLayerModel.cpp`.
- Implemented: kept Tone Curve layer execution, shader source usage, LUT texture upload, serialization/deserialization glue, standalone node UI, and Develop integrated Finish Tone UI in `ToneLayers.cpp` at the time of this model split. Auto scene analysis/rewrite was later moved to `ToneCurveLayerAuto.cpp`; shader/source and GL execution ownership was later moved to `ToneLayerRendering.cpp`; Tone Curve serialization was later moved to `ToneCurveLayerSerialization.cpp`; Tone Curve UI surfaces were later moved to `ToneCurveLayerUI.cpp`.
- Remaining work: future point editing, coordinate conversion, targeting/probe state, and curve evaluation fixes should stay in `ToneCurveLayerModel.cpp`. Future auto-analysis/rewrite fixes should stay in `ToneCurveLayerAuto.cpp`; future shader/source and GL execution fixes should stay in `ToneLayerRendering.cpp`; future Tone Curve schema/default/fallback fixes should stay in `ToneCurveLayerSerialization.cpp`; future Tone Curve ImGui surface fixes should stay in `ToneCurveLayerUI.cpp`.
- Deferred-scope changes: none. This did not implement the Guide 08 tone redesign, mode-specific final tone strategy, graph controls, Manual-to-Auto handoff, View Transform changes, candidate gallery UI, user picker/merge controls, denoise redesign, color graph controls, or any behavior change.
- Validation run: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed.

## 2026-06-21 Organization Refactor Handoff - Develop Rendered Feedback Analysis Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change rendered metric distance thresholds, duplicate/pre-finish-distinct behavior, stage-boundary labels, validation wrapper behavior, rendered feedback decision policy, rendered-feedback JSON keys/schemas/content, candidate generation, scoring math, convergence thresholds, graph serialization, Guide 03 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp`, `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackAnalysis.h`, `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackAnalysis.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `DevelopRenderedDuplicateDecision`, `EvaluateDevelopRenderedCandidateDuplicate`, `ClassifyDevelopRenderedStageBoundary`, and the rendered duplicate/pre-finish-distinct threshold constants from `EditorModuleDevelopCandidateFeedback.cpp` into `EditorModuleDevelopRenderedFeedbackAnalysis.h/.cpp`.
- Implemented: kept rendered result aggregation, relative-score writeback, rendered rejection memory, pair/ensemble suggestion orchestration, feedback action/stop-reason decision flow, Auto-solve request handoff, and integrated Tone JSON application in `EditorModuleDevelopCandidateFeedback.cpp`.
- Remaining work: future duplicate thresholds, pre-finish-distinct preservation logic, and selected-vs-best stage-boundary labels should stay in `EditorModuleDevelopRenderedFeedbackAnalysis.*`. Future rendered feedback aggregation and decision-flow work should stay in `EditorModuleDevelopCandidateFeedback.cpp`.
- Deferred-scope changes: none. This did not implement richer candidate families, candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, denoise redesign, or broader rendered-feedback behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - Develop Candidate Score Components Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change candidate generation, candidate ids, candidate taxonomy, scalar scoring math, score-component JSON keys/schemas/content, fallback component behavior, continuation-bias policy, duplicate selection, rendered-feedback policy, convergence thresholds, graph serialization, Guide 03 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopCandidateScoring.h`, `src/Editor/Internal/EditorModuleDevelopCandidateScoring.cpp`, `src/Editor/Internal/EditorModuleDevelopCandidateScoreComponents.h`, `src/Editor/Internal/EditorModuleDevelopCandidateScoreComponents.cpp`, `src/Editor/Internal/EditorModuleDevelopCandidateGeneration.cpp`, `src/Editor/Internal/EditorModuleDevelopAutoSolveDiagnostics.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `BuildDevelopAutoCandidateScoreComponents` and `BuildFallbackDevelopAutoCandidateScoreComponents` from `EditorModuleDevelopCandidateScoring.cpp` into `EditorModuleDevelopCandidateScoreComponents.h/.cpp`.
- Implemented: kept candidate solve/result structs, candidate id/stage taxonomy, continuation-bias scoring helpers, candidate fingerprints, scalar parameter scoring, nearest-survivor distance, and scalar damage rejection in `EditorModuleDevelopCandidateScoring.*`.
- Remaining work: future `ParameterScoreComponentsV1` schema/signal/dimension/risk changes should stay in `EditorModuleDevelopCandidateScoreComponents.*`. Future scalar score math, candidate classification, fingerprint, and damage-rejection changes should stay in `EditorModuleDevelopCandidateScoring.*`. Rendered duplicate/stage-boundary analysis now has its own owner in `EditorModuleDevelopRenderedFeedbackAnalysis.*`.
- Deferred-scope changes: none. This did not implement richer candidate families, candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, denoise redesign, or broader solver behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - Develop Rendered Feedback Records Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change rendered metrics, rendered metric JSON keys/schemas, feedback-history contents or bounds, `RenderedFeedbackLoopV1` contents, duplicate/stage-boundary behavior, rendered-feedback decision policy, candidate generation, scoring math, convergence thresholds, graph serialization, Guide 03 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp`, `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackRecords.h`, `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackRecords.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `DevelopCandidateRenderMetricsToJson`, `AppendDevelopCandidateRenderedFeedbackHistory`, `WriteDevelopCandidateRenderedFeedbackLoopRecord`, and the rendered-feedback JSON hash helper from `EditorModuleDevelopCandidateFeedback.cpp` into `EditorModuleDevelopRenderedFeedbackRecords.h/.cpp`.
- Implemented: kept result aggregation, selected/best feedback summaries, duplicate and stage-boundary analysis, feedback action/stop-reason decision flow, rendered rejection memory, pair/ensemble suggestion orchestration, Auto-solve request handoff, and integrated Tone JSON application in `EditorModuleDevelopCandidateFeedback.cpp` at the time of this split. Duplicate/stage-boundary analysis was later moved to `EditorModuleDevelopRenderedFeedbackAnalysis.*`.
- Remaining work: future rendered-feedback record/schema changes should stay in `EditorModuleDevelopRenderedFeedbackRecords.*`. Future feedback aggregation and decision-flow work should stay in `EditorModuleDevelopCandidateFeedback.cpp`. The larger score-components split identified by the mini researcher was completed in the later handoff above.
- Deferred-scope changes: none. This did not implement richer candidate families, candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, denoise redesign, or broader rendered-feedback behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - Develop Candidate Render Payload Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change candidate render selection, request budgets, quiet-window timing, candidate ids, payload math, stage-constraint behavior, white-balance probe behavior, JSON keys/schemas, rendered-feedback policy, convergence thresholds, graph serialization, Guide 03 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopCandidateRequests.cpp`, `src/Editor/Internal/EditorModuleDevelopCandidateRenderPayload.h`, `src/Editor/Internal/EditorModuleDevelopCandidateRenderPayload.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `ReadDevelopAuthoredGuidanceFromToneJson`, `ClampCandidateMosaicDenoiseSettings`, `ClampCandidateScenePrepSettings`, `PreserveCandidateRawCleanupSettings`, and `ApplyDevelopGuidanceToCandidateRenderPayload` from `EditorModuleDevelopCandidateRequests.cpp` into `EditorModuleDevelopCandidateRenderPayload.h/.cpp`.
- Implemented: kept request budget calculation, quiet-window/deferred gating, candidate option scoring/diversity selection, active refine/stage slot reservation, stage scheduler ordering, and request construction in `EditorModuleDevelopCandidateRequests.cpp`.
- Remaining work: future request-scheduler changes should stay in `EditorModuleDevelopCandidateRequests.cpp`; future copied payload mapping, stage-freeze, cleanup/detail, white-balance probe, and per-candidate Tone JSON diagnostics should stay in `EditorModuleDevelopCandidateRenderPayload.*`.
- Deferred-scope changes: none. This did not implement richer candidate families, candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, denoise redesign, or broader rendered-feedback behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - Develop Rendered Candidate Scoring Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change rendered metric scoring math, relative-comparison math, damage classifier thresholds, refine-intent policy, duplicate/stage-boundary analysis, rendered-feedback JSON keys/schemas, convergence thresholds, candidate generation, graph serialization, Guide 03 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp`, `src/Editor/Internal/EditorModuleDevelopRenderedCandidateScoring.h`, `src/Editor/Internal/EditorModuleDevelopRenderedCandidateScoring.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `ScoreDevelopRenderedCandidateMetrics`, `DevelopRenderedRelativeComparison`, `CompareDevelopRenderedCandidateToSelected`, `ClassifyDevelopRenderedCandidateDamage`, and `ResolveDevelopRenderedRefineIntent` from `EditorModuleDevelopCandidateFeedback.cpp` into `EditorModuleDevelopRenderedCandidateScoring.h/.cpp`.
- Implemented: kept rendered result aggregation, selected/best feedback summaries, duplicate and stage-boundary analysis, rendered feedback history, feedback loop record writeback, and integrated Tone JSON application in `EditorModuleDevelopCandidateFeedback.cpp` at the time of this split. Duplicate/stage-boundary analysis was later moved to `EditorModuleDevelopRenderedFeedbackAnalysis.*`.
- Remaining work: the new scoring helper should remain the home for ranking/classifier/refine-threshold fixes; duplicate/stage-boundary thresholds and labels belong in `EditorModuleDevelopRenderedFeedbackAnalysis.*`; rendered metric/history schema changes belong in `EditorModuleDevelopRenderedFeedbackRecords.*`.
- Deferred-scope changes: none. This did not implement richer candidate families, a candidate gallery, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, spatial/perceptual damage maps, or broader rendered-feedback behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - Develop Selected Solve Stage Owners

- Status delta: no Develop guide status changed. This pass is structural only and does not change Auto solve behavior, Auto intent/mode behavior, RAW setting math, Scene Prep math, integrated Tone JSON keys/schemas, candidate generation, scoring math, rendered-feedback policy, convergence thresholds, graph serialization, Guide 03 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopAutoSolveApplication.cpp`, `src/Editor/Internal/EditorModuleDevelopAutoSolveApplicationContext.h`, `src/Editor/Internal/EditorModuleDevelopAutoSolveApplicationContext.cpp`, `src/Editor/Internal/EditorModuleDevelopAutoSolveRawApplication.cpp`, `src/Editor/Internal/EditorModuleDevelopAutoSolveScenePrepApplication.cpp`, `src/Editor/Internal/EditorModuleDevelopAutoSolveToneApplication.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: split the previous selected-solve application helper by ownership. `EditorModuleDevelopAutoSolveApplication.cpp` now keeps Auto intent-profile shaping, mode-aware guidance, and selected-solve orchestration only. `EditorModuleDevelopAutoSolveApplicationContext.*` owns shared stats/intent/local-exposure scalar derivation. `EditorModuleDevelopAutoSolveRawApplication.cpp` owns authored RAW setting application. `EditorModuleDevelopAutoSolveScenePrepApplication.cpp` owns authored Scene Prep/local-exposure application. `EditorModuleDevelopAutoSolveToneApplication.cpp` owns requested/achieved Tone JSON writeback, selected-solve diagnostics, stage diagnostics, subject request mirrors, and calibration queueing.
- Implemented: preserved the public `ApplyDevelopSelectedAutoSolve` API and kept candidate generation, scoring, rendered-feedback application, convergence, broad diagnostics, and public `EditorModule::ApplyDevelopAutoSolve` boundaries unchanged.
- Remaining work: the selected-solve stage owners are now small enough for focused bug-fix passes. Future organization should avoid splitting again until a specific owner grows; likely next broader hotspots are rendered feedback scoring/writeback in `EditorModuleDevelopCandidateFeedback.cpp` or the standalone Tone Curve layer file if nearby work resumes there.
- Deferred-scope changes: none. This did not implement richer candidate families, a candidate gallery, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, RAW solver behavior changes, Scene Prep behavior changes, Tone redesign, or broader rendered-feedback behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - Develop Selected Solve Application Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change Auto solve behavior, Auto intent/mode behavior, RAW setting math, Scene Prep math, integrated Tone JSON keys/schemas, candidate generation, scoring math, rendered-feedback policy, convergence thresholds, graph serialization, Guide 03 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp`, `src/Editor/Internal/EditorModuleDevelopAutoSolveApplication.h`, `src/Editor/Internal/EditorModuleDevelopAutoSolveApplication.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved Auto intent-profile shaping, mode-aware guidance construction, selected authored guidance application, requested/achieved Tone JSON writeback, RAW settings application, Scene Prep/local-exposure application, calibration queueing, and staged solve diagnostic invocation from `EditorModuleDevelopAutoSolve.cpp` into `EditorModuleDevelopAutoSolveApplication.*`.
- Implemented: kept `EditorModuleDevelopAutoSolve.cpp` as the thin public coordinator for `EditorModule::ApplyDevelopAutoSolve`: normalize requested Auto/subject guidance, ensure integrated tone exists, read current tone stats, build the candidate solve, and delegate selected-solve application.
- Implemented: kept authored candidate family generation in `EditorModuleDevelopCandidateGeneration.*`, candidate solve/result structs and scoring in `EditorModuleDevelopCandidateScoring.*`, rendered-feedback mutation in `EditorModuleDevelopRenderedFeedbackApplication.*`, convergence/readback helpers in `EditorModuleDevelopRenderedFeedbackConvergence.*`, broad diagnostic serialization helpers in `EditorModuleDevelopAutoSolveDiagnostics.*`, and subject/scene intent resolution in `EditorModuleDevelopSubjectImportance.*`.
- Remaining work: `EditorModuleDevelopAutoSolveApplication.*` is now the largest remaining Develop Auto authored-settings owner. If future RAW/Scene Prep/Tone application work grows it further, split RAW application, Scene Prep/local-exposure application, and Tone/stage diagnostic writeback into narrower owners.
- Deferred-scope changes: none. This did not implement richer candidate families, a candidate gallery, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, RAW solver behavior changes, Scene Prep behavior changes, or broader rendered-feedback behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - Develop Candidate Generation Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change candidate families, scoring math, rendered-feedback policy, convergence thresholds, JSON keys/schemas, graph serialization, Guide 03 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp`, `src/Editor/Internal/EditorModuleDevelopCandidateGeneration.h`, `src/Editor/Internal/EditorModuleDevelopCandidateGeneration.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `BuildDevelopAutoCandidateSolve` and its private authored-candidate helper set from `EditorModuleDevelopAutoSolve.cpp` into `EditorModuleDevelopCandidateGeneration.*`, including remembered same-context/rendered rejection lookup, rendered dynamic-range evidence source selection, continuation expansion, RAW white-balance probes, cleanup/detail probes, mode-neighbor probes, finish-tone probes, exposure-placement probes, rendered-local families, survivor carry-forward, initial parameter-space merge construction, and the preliminary rendered-feedback application call.
- Implemented: kept candidate solve/result structs and scoring in `EditorModuleDevelopCandidateScoring.*`, shared guidance adjustment/blending and selected white-balance probe bookkeeping in `EditorModuleDevelopCandidateGuidance.*`, rendered-feedback mutation in `EditorModuleDevelopRenderedFeedbackApplication.*`, convergence/readback helpers in `EditorModuleDevelopRenderedFeedbackConvergence.*`, broad diagnostic writeback in `EditorModuleDevelopAutoSolveDiagnostics.*`, and subject/scene intent resolution in `EditorModuleDevelopSubjectImportance.*`.
- Implemented: kept `EditorModuleDevelopAutoSolve.cpp` as the public `EditorModule::ApplyDevelopAutoSolve` coordinator that normalizes requested guidance, builds mode-aware guidance, calls candidate generation, writes selected guidance into integrated Tone JSON, applies RAW/Scene Prep/Tone settings, and queues calibration.
- Remaining work: `EditorModuleDevelopAutoSolve.cpp` still owns authored RAW/Scene Prep/Tone application and mode/intent guidance shaping. The next high-value solver split is selected-guidance application once that area is touched.
- Deferred-scope changes: none. This did not implement richer candidate families, a candidate gallery, user picker/merge controls, applied learning, a full staged controller, graph controls, sidecar stats bus, or broader rendered-feedback behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after one transient font-bake retry; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - Develop Rendered Feedback Application

- Status delta: no Develop guide status changed. This pass is structural only and does not change rendered-feedback policy, convergence thresholds, candidate generation, candidate scoring math, rendered metric scoring/classification, JSON keys/schemas, graph serialization, Guide 03 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp`, `src/Editor/Internal/EditorModuleDevelopCandidateGuidance.h`, `src/Editor/Internal/EditorModuleDevelopCandidateGuidance.cpp`, `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackApplication.h`, `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackApplication.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved authored rendered-feedback application from `EditorModuleDevelopAutoSolve.cpp` into `EditorModuleDevelopRenderedFeedbackApplication.*`, including direct rendered winner adoption, rendered-local refine selection/fallback, selected-vs-best merge, pair merge, ensemble merge, feedback admission bookkeeping, improvement/stability/trend/monotonic guard result capture, and revision-stage/reason assignment.
- Implemented: moved shared candidate guidance adjustment/blending helpers and selected-solve white-balance probe bookkeeping into `EditorModuleDevelopCandidateGuidance.*` so candidate generation and rendered-feedback application share the same narrow utility boundary.
- Implemented: kept candidate family generation, remembered/rendered rejection reads, prior survivor carry-forward, initial authored merge construction, rendered dynamic-range evidence source selection, and authored RAW/Scene Prep/Tone application in `EditorModuleDevelopAutoSolve.cpp`.
- Remaining work: `EditorModuleDevelopAutoSolve.cpp` still owns candidate family generation and first-pass authored merge construction. The next high-value solver split is candidate family generation once that area is touched.
- Deferred-scope changes: none. This did not implement a candidate gallery, user picker/merge controls, applied learning, a full staged controller, graph controls, richer candidate families, sidecar stats bus, or broader rendered-feedback behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - Develop Subject Scene Intent Resolver

- Status delta: no Develop guide status changed. This pass is structural only and does not change subject/scene intent behavior, subject-importance behavior, brush behavior, viewport overlay behavior, Auto solve policy, candidate scoring math, JSON keys/schemas, graph serialization, Guide 05 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp`, `src/Editor/Internal/EditorModuleDevelopSubjectImportance.h`, `src/Editor/Internal/EditorModuleDevelopSubjectImportance.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `ResolveDevelopSubjectSceneIntent` from `EditorModuleDevelopAutoSolve.cpp` into `EditorModuleDevelopSubjectImportance.*`, beside Guide 05 subject-map interpretation, refined-map application, solve-note generation, and selected-node subject viewport/brush bridge code.
- Implemented: kept candidate family generation, remembered/rendered rejection reads, prior survivor carry-forward, authored merge construction, authored rendered-feedback adoption/refinement/merge mutation, and authored RAW/Scene Prep/Tone application in `EditorModuleDevelopAutoSolve.cpp`.
- Remaining work: `EditorModuleDevelopAutoSolve.cpp` still owns candidate family generation and authored rendered-feedback mutation. The next high-value solver split is candidate family generation or authored rendered-feedback adoption/refinement/merge mutation once either area is touched.
- Deferred-scope changes: none. This did not implement true image-edge refinement, edge visualization, semantic/AI detection, graph-control UI, candidate gallery UI, Manual/Auto subject-bias handoff, richer candidate families, or broader rendered-feedback behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - Develop Auto Solve Diagnostics Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change candidate generation, candidate scoring math, rendered-feedback policy, convergence thresholds, JSON keys/schemas, graph serialization, Guide 03 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp`, `src/Editor/Internal/EditorModuleDevelopAutoSolveDiagnostics.h`, `src/Editor/Internal/EditorModuleDevelopAutoSolveDiagnostics.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved broad Develop Auto diagnostic writeback from `EditorModuleDevelopAutoSolve.cpp` into `EditorModuleDevelopAutoSolveDiagnostics.*`, including candidate status serialization, rejected-candidate memory writeback, `ParameterCandidatesV1`, `ParameterScoreComponentsV1` top-level candidate aliases, `CandidateOutcomeLearningV1`, dynamic-range/subject top-level aliases, rendered-feedback loop/continuation/convergence aliases, and `StagedAutoSolveV1` logical stage diagnostics.
- Implemented: kept candidate family generation, remembered/rendered rejection reads, prior survivor carry-forward, authored merge construction, authored rendered-feedback adoption/refinement/merge mutation, subject/scene intent resolution, and authored RAW/Scene Prep/Tone application in `EditorModuleDevelopAutoSolve.cpp`.
- Remaining work: `EditorModuleDevelopAutoSolve.cpp` still owns candidate family generation, authored rendered-feedback mutation, and subject/scene intent resolution. The next high-value solver split is candidate family generation or subject/scene intent resolution once either area is touched.
- Deferred-scope changes: none. This did not implement a candidate gallery, user picker/merge controls, applied learning, a full staged controller, graph controls, richer candidate families, sidecar stats bus, or broader rendered-feedback behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-21 Organization Refactor Handoff - Develop Rendered Feedback Convergence Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change rendered-feedback policy, candidate generation, candidate scoring math, convergence thresholds, JSON schemas, graph serialization, Guide 03 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp`, `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp`, `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackConvergence.h`, `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackConvergence.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved rendered-feedback loop version constants, continuation policy builders, `ConvergenceAdmissionV1` policy calculation, `ConvergenceEvidenceV1` JSON construction, rendered metric JSON readback, history metric lookup, rendered trend detection, same-intent monotonic guard evaluation, repeated adoption/refinement stop checks, and the converged stop-reason classifier into `EditorModuleDevelopRenderedFeedbackConvergence.*`.
- Implemented: `EditorModuleDevelopCandidateFeedback.cpp` now reuses the shared continuation-policy builder, loop constants, reverse-adoption check, and converged stop-reason classifier instead of keeping private duplicates.
- Implemented: kept candidate family generation, authored rendered-feedback adoption/refinement/merge mutation, pair/ensemble merge construction, subject/scene intent resolution, and broad solve diagnostic writeback in `EditorModuleDevelopAutoSolve.cpp`.
- Remaining work: `EditorModuleDevelopAutoSolve.cpp` still owns candidate family generation, authored rendered-feedback mutation, and subject/scene intent resolution. The later Auto Solve Diagnostics handoff above moved broad JSON diagnostic writeback into `EditorModuleDevelopAutoSolveDiagnostics.*`.
- Deferred-scope changes: none. This did not implement a candidate gallery, user picker/merge controls, applied learning, a full staged controller, graph controls, richer candidate families, or broader rendered-feedback behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-20 Organization Refactor Handoff - Develop Candidate Scoring Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change candidate generation, candidate scoring math, continuation-bias policy, rendered-feedback policy, JSON schemas, graph serialization, Guide 03 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp`, `src/Editor/Internal/EditorModuleDevelopCandidateScoring.h`, `src/Editor/Internal/EditorModuleDevelopCandidateScoring.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved Develop candidate solve/result structs, white-balance probe id helpers, candidate distance, candidate id/revision-stage taxonomy, rendered refine preferred-candidate helpers, continuation-bias profile/bonus application, candidate context/guidance/final fingerprints, parameter candidate scoring, `ParameterScoreComponentsV1` JSON, fallback score components, nearest-survivor distance, and scalar parameter-damage rejection from `EditorModuleDevelopAutoSolve.cpp` into `EditorModuleDevelopCandidateScoring.*`.
- Implemented: kept candidate family generation, remembered/rendered rejection reads, prior survivor carry-forward, authored merge construction, rendered-feedback convergence/adoption/refinement, subject/scene intent resolution, and broad solve diagnostic writeback in `EditorModuleDevelopAutoSolve.cpp`.
- Remaining work: `EditorModuleDevelopAutoSolve.cpp` still owns candidate family generation and rendered-feedback convergence logic. The next high-value solver split is rendered-feedback convergence/readback or candidate family generation once either area is touched for a feature or bug-fix pass.
- Deferred-scope changes: none. This did not implement candidate gallery UI, user picker/merge controls, applied learning, a full staged controller, graph controls, richer candidate families, or broader rendered-feedback behavior changes.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-20 Organization Refactor Handoff - Develop Dynamic Range Strategy Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change dynamic-range behavior, local-exposure behavior, candidate generation, candidate scoring math, rendered-feedback policy, JSON schemas, graph serialization, Guide 04 status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp`, `src/Editor/Internal/EditorModuleDevelopDynamicRangeStrategy.h`, `src/Editor/Internal/EditorModuleDevelopDynamicRangeStrategy.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `DevelopToneAutoStats`, `ReadDevelopToneAutoStats`, compact rendered metrics-to-`DynamicRangeRegionEvidenceV1` mapping, `DynamicRangeRegionEvidenceV1` JSON, `DynamicRangeStrategyV1`, `DynamicRangeStrategyMapV1`, `LocalExposureStrategyV1`, and dynamic-range/local-exposure strategy JSON helpers from `EditorModuleDevelopAutoSolve.cpp` into `EditorModuleDevelopDynamicRangeStrategy.*`.
- Implemented: kept rendered-history candidate selection, `ResolveDevelopDynamicRangeRegionEvidence`, candidate family generation/scoring, rendered-feedback convergence, subject/scene intent resolution, and broad solve diagnostics in `EditorModuleDevelopAutoSolve.cpp`.
- Remaining work: `EditorModuleDevelopAutoSolve.cpp` still owns candidate generation/scoring, rendered-feedback convergence logic, subject/scene intent resolution, and large JSON diagnostic writeback. The next high-value solver split is candidate generation/scoring or rendered-feedback convergence.
- Deferred-scope changes: none. This did not implement true spatial maps, visual overlays, graph controls, subject-aware range priority, clipped-data reconstruction, deeper Scene Prep/local exposure renderer redesign, denoise redesign, or the Guide 08 tone redesign.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-20 Organization Refactor Handoff - Develop Subject Importance Solver Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change subject-importance behavior, brush behavior, viewport overlay behavior, Auto solve policy, candidate scoring math, JSON schemas, graph serialization, Manual handoff status, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp`, `src/Editor/Internal/EditorModuleDevelopSubjectImportance.h`, `src/Editor/Internal/EditorModuleDevelopSubjectImportance.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved Guide 05 subject-importance constants, map-cell structs, `SubjectImportanceMapV1` interpretation, `SubjectRefinedMapV1` construction/application/JSON, `SubjectImportanceSolveNotesV1`, `DevelopSubjectSceneIntentToJson`, and subject-importance summary helpers from `EditorModuleDevelopAutoSolve.cpp` into `EditorModuleDevelopSubjectImportance.*`.
- Implemented: moved `EditorModule::NormalizeDevelopSubjectImportance`, selected-node subject viewport state construction, and subject viewport region/brush mutation bridge methods into `EditorModuleDevelopSubjectImportance.cpp`.
- Remaining work: `EditorModuleDevelopAutoSolve.cpp` still owns the broader Auto solve coordinator, candidate generation/scoring, rendered-feedback convergence logic, and subject/scene intent resolution. Later handoffs above moved Guide 04 strategy helpers, rendered-feedback convergence helpers, candidate scoring helpers, and broad diagnostic writeback into focused files.
- Deferred-scope changes: none. This did not implement true image-edge refinement, edge visualization, semantic/AI detection, graph-control UI, candidate gallery UI, or Manual/Auto subject-bias handoff.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-20 Organization Refactor Handoff - Renderer Custom Mask Pass

- Status delta: no Develop guide status changed. This pass is structural only and does not change Custom Mask behavior, generated mask behavior, render math, shader source, graph execution, cache behavior, graph schemas, Develop identity, or deferred feature status.
- Files changed: `src/Renderer/Internal/RenderPipelineNodePasses.cpp`, `src/Renderer/Internal/RenderPipelineCustomMaskPass.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved Custom Mask CPU raster flattening, object evaluation, blur/morph post-processing, and `RenderPipeline::GenerateCustomMaskTexture` from `RenderPipelineNodePasses.cpp` into `RenderPipelineCustomMaskPass.cpp`.
- Implemented: kept generated mask texture rendering, mask blend/combine, mix, data math, mask utility, image-to-mask, and channel split/combine pass implementations in `RenderPipelineNodePasses.cpp`.
- Remaining work: later renderer organization passes should split another remaining pass family only when it grows into a clear owner; keep future Custom Mask rasterization fixes in `RenderPipelineCustomMaskPass.cpp`.
- Deferred-scope changes: none. This did not implement mask UX changes, candidate solving, graph controls, staged renderer work, View Transform behavior changes, or any other deferred feature.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-20 Organization Refactor Handoff - Node Graph Canvas Orchestration

- Status delta: no Develop guide status changed. This pass is structural only and does not change graph UI behavior, canvas draw ordering, drag/drop behavior, graph view transform behavior, static preview behavior, zoom dial behavior, node layout behavior, graph schemas, Develop identity, or deferred feature status.
- Files changed: `src/Editor/NodeGraph/EditorNodeGraphUI.cpp`, `src/Editor/NodeGraph/UI/EditorNodeGraphUICanvas.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `EditorNodeGraphUI::Render`, `RenderGraphCanvas`, `FitGraphPreviewToCanvas`, `RenderStaticGraphPreview`, and `RenderGraphZoomDial` from the main graph UI file into `EditorNodeGraphUICanvas.cpp`.
- Implemented: moved canvas-only helper logic for node-drop insertion and add-node-from-browser-entry routing with the canvas file.
- Implemented: reduced `EditorNodeGraphUI.cpp` to the graph node layout-cache, socket-anchor, render-order, front-order, and node-footprint sizing implementation.
- Remaining work: later graph UI organization passes should only split `EditorNodeGraphUI.cpp` again if layout-cache construction, socket-anchor placement, or render-order behavior grows into a new clear owner.
- Deferred-scope changes: none. This did not implement graph controls, canvas behavior changes, node UI behavior changes, new interactions, candidate UI, or any Develop feature work.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-20 Organization Refactor Handoff - Node Graph Node Body Rendering

- Status delta: no Develop guide status changed. This pass is structural only and does not change graph UI behavior, node layout behavior, node inline-control behavior, socket drawing semantics, canvas ordering, interaction behavior, graph schemas, Develop identity, or deferred feature status.
- Files changed: `src/Editor/NodeGraph/EditorNodeGraphUI.cpp`, `src/Editor/NodeGraph/UI/EditorNodeGraphUINodes.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `EditorNodeGraphUI::RenderNode` from the main graph UI file into `EditorNodeGraphUINodes.cpp`, including frameless media nodes, compact square nodes, expanded inline controls, socket label drawing, content measurement, and node-content hover/active capture.
- Implemented: moved the private `FitPreviewRect` helper with the node renderer because it is now only used by node body preview drawing.
- Implemented: kept `EditorNodeGraphUI.cpp` responsible for graph render entry points, canvas orchestration, zoom dial/static preview drawing, layout-cache construction, node render order, and socket-anchor lookup at the time of the node-rendering split. The later canvas handoff above moved graph render entry/canvas orchestration/static preview/zoom drawing into `EditorNodeGraphUICanvas.cpp`. `RenderInteraction` already lives in `EditorNodeGraphUIInteraction.cpp`.
- Remaining work: later graph UI organization passes should only split `EditorNodeGraphUI.cpp` again if layout-cache construction, socket-anchor placement, or render-order behavior grows into a new clear owner.
- Deferred-scope changes: none. This did not implement graph controls, canvas behavior changes, node UI behavior changes, new interactions, candidate UI, or any Develop feature work.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split.

## 2026-06-20 Composite Text Pass - Text Aspect Fix and Backdrop Blur Controls

- Status delta: no Develop guide status changed. This pass fixes composite-mode generated text presentation and adds text-generator backdrop styling controls, but it does not change Develop guide ownership, Auto/Manual behavior, graph schemas outside image-generator settings, or deferred Develop feature status.
- Files changed: `src/Editor/NodeGraph/NodeGraphPayloads.h`, `src/Renderer/MaskRenderTypes.h`, `src/Editor/EditorModuleTypes.h`, `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphUtilitySerialization.cpp`, `src/Editor/NodeGraph/UI/EditorNodeGraphUIClipboard.cpp`, `src/Editor/Internal/EditorModuleNodeBrowserThumbnails.cpp`, `src/Editor/NodeGraph/EditorNodeGraphUI.cpp`, `src/Renderer/Internal/RenderPipelineImageGeneratorPass.cpp`, `src/Editor/Internal/EditorModuleComposite.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, and `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`.
- Implemented: text image-generator settings now serialize and round-trip `textBackdropBlur`, `textBackdropOpacity`, and `textBackdropPadding`, and the node UI exposes those controls for text generators.
- Implemented: generated text textures now build an optional soft tinted backdrop from the text alpha, using `colorB` as the backdrop tint with blur, opacity, and padding controls.
- Implemented: composite scene sizing now separates scalable raster requests from square scene-base assumptions. Text generators still request scalable raster sizes, but their composite transform now uses the actual text raster aspect instead of forcing a square 256x256 scene base, preventing stretched or effectively missing text in composite placement/export paths.
- Implemented: Mix reference-canvas selection now prefers a real image/RAW/custom canvas over a generated text/scalable source when both Mix inputs are connected. This keeps image-plus-text composites from exporting on the text generator's scalable canvas when the text generator happens to be plugged into Mix input A.
- Implemented: image-generator text, font size, and backdrop settings are included in composite-chain and render-graph fingerprints so text edits and backdrop-control edits refresh cached composite outputs reliably.
- Remaining work: this pass adds a generated soft text backdrop, not a true live blur of already-composited lower layers. If future work needs real backdrop blur from underlying composite content, that should be implemented as a composite-stage effect rather than a generator-texture effect.
- Deferred-scope changes: none. This did not implement a candidate gallery, staged renderer redesign, Develop guide work, or true composite backdrop-filter rendering.
- Validation run: `cmake --build build_codex_verify --config Debug --target Stack --parallel 6` passed after the initial text/backdrop pass and again after the Mix reference-canvas correction.

## 2026-06-20 Organization Refactor Handoff - Node Graph Link and Group Drawing

- Status delta: no Develop guide status changed. This pass is structural only and does not change graph UI behavior, link hit/visual semantics, group drawing behavior, pending link drag behavior, canvas ordering, interaction behavior, graph schemas, Develop identity, or deferred feature status.
- Files changed: `src/Editor/NodeGraph/EditorNodeGraphUI.cpp`, `src/Editor/NodeGraph/UI/EditorNodeGraphUILinksAndGroups.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `EditorNodeGraphUI::RenderLinks`, `RenderPendingOutputLinkDrag`, `RenderPendingInputLinkDrag`, and `RenderGroups` from the main graph UI file into `EditorNodeGraphUILinksAndGroups.cpp`.
- Implemented: kept `EditorNodeGraphUI.cpp` responsible for canvas orchestration and node body drawing at the time of the link/group split. Node body drawing was split in the later node-rendering handoff above, canvas orchestration was split in the later canvas handoff above, and `RenderInteraction` already lives in `EditorNodeGraphUIInteraction.cpp`.
- Remaining work: later graph UI organization passes should only split `EditorNodeGraphUI.cpp` again if layout-cache construction, socket-anchor placement, or render-order behavior grows into a new clear owner.
- Deferred-scope changes: none. This did not implement graph controls, canvas behavior changes, new node interactions, candidate UI, or any Develop feature work.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0.

## 2026-06-20 Organization Refactor Handoff - Develop Scene Prep, Finish Tone, and Payload Comparison

- Status delta: no Develop guide status changed. This pass is structural only and does not change Scene Prep behavior, integrated Finish Tone behavior, ToneCurve transient-state behavior, payload dirty detection semantics, Auto/Manual mode behavior, graph schemas, render math, Develop identity, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopControls.cpp`, `src/Editor/Internal/EditorModuleDevelopScenePrepControls.h`, `src/Editor/Internal/EditorModuleDevelopScenePrepControls.cpp`, `src/Editor/Internal/EditorModuleDevelopFinishToneControls.h`, `src/Editor/Internal/EditorModuleDevelopFinishToneControls.cpp`, `src/Editor/Internal/EditorModuleDevelopPayloadComparison.h`, `src/Editor/Internal/EditorModuleDevelopPayloadComparison.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved Develop Manual-mode Scene Prep bridge controls, live Pre-Local Exposure summary display, advanced Scene Prep controls, Scene Prep diagnostics controls, and Develop Scene Prep normalization/equality wrappers into `EditorModuleDevelopScenePrepControls.*`.
- Implemented: moved integrated Finish Tone panel rendering, integrated ToneCurve JSON construction, transient ToneCurve state restore/store, upstream Develop-change notification into auto-prepared tone, scoped-mask panel bridge, and integrated Tone JSON writeback into `EditorModuleDevelopFinishToneControls.*`.
- Implemented: moved exact Develop payload equality checks for RAW settings, Scene Prep settings, Auto guidance, Subject Importance regions/strokes, integrated tone JSON, and UI mode into `EditorModuleDevelopPayloadComparison.*`.
- Implemented: reduced `EditorModuleDevelopControls.cpp` to a 200-line public Develop controls coordinator that owns node/mode wiring, metadata/default setup, Auto/Manual flow, helper ordering, final interaction recording, and render dirty marking.
- Remaining work: keep future UI/edit logic in the focused helpers above. If `EditorModuleDevelopControls.cpp` grows again, split only new responsibilities with a clear owner; the obvious current Develop-panel subpanels have been separated.
- Deferred-scope changes: none. This did not implement Scene Prep algorithm changes, integrated tone strategy changes, graph controls, candidate gallery UI, Manual handoff, subject-edge refinement, or any other deferred feature work.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed.

## 2026-06-20 Organization Refactor Handoff - Develop Auto Guidance Controls

- Status delta: no Develop guide status changed. This pass is structural only and does not change Auto guidance behavior, Auto/Manual mode behavior, Subject Importance behavior, draft commit timing, Auto reanalysis policy, interaction quiet-window reporting, graph schemas, render math, Develop identity, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopControls.cpp`, `src/Editor/Internal/EditorModuleDevelopAutoGuidanceControls.h`, `src/Editor/Internal/EditorModuleDevelopAutoGuidanceControls.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved Develop Auto intent selection, Auto Calibrate / Reset Auto buttons, guidance sliders, Subject / Scene Intent and Mood / Readability sliders, right-click slider reset handling, draft edit/commit policy, Subject Importance control delegation, and Auto guidance changed/reanalysis/interaction reporting into `EditorModuleDevelopAutoGuidanceControls.*`.
- Implemented: kept `EditorModule::RenderRawDevelopControls`, Auto/Manual mode flow, `UpdateDevelopAutoState`, compact Auto status readouts, Manual RAW controls, Scene Prep bridge, and integrated Finish Tone bridge in `EditorModuleDevelopControls.cpp`.
- Remaining work: later organization passes can split scene-prep bridge controls, finish-tone controls, or pure payload comparison/build helpers from `EditorModuleDevelopControls.cpp`; keep future Auto guidance UI changes in `EditorModuleDevelopAutoGuidanceControls.*`.
- Deferred-scope changes: none. This did not implement graph controls, candidate gallery UI, Manual handoff, subject-edge refinement, Auto solve algorithm changes, or other deferred feature work.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed.

## 2026-06-20 Organization Refactor Handoff - Develop Manual RAW Controls

- Status delta: no Develop guide status changed. This pass is structural only and does not change Manual RAW behavior, RAW exposure draft timing, Auto/Manual mode behavior, Scene Prep behavior, integrated Finish Tone behavior, graph schemas, render math, Develop identity, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopControls.cpp`, `src/Editor/Internal/EditorModuleDevelopManualRawControls.h`, `src/Editor/Internal/EditorModuleDevelopManualRawControls.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved Develop Manual RAW color/exposure controls, RAW exposure draft UI, highlight reconstruction, demosaic status, input-level overrides, orientation, cleanup, mosaic denoise, camera transform, and debug-view controls into `EditorModuleDevelopManualRawControls.*`.
- Implemented: kept `EditorModule::RenderRawDevelopControls`, Auto/Manual mode flow, Auto guidance sliders, Scene Prep bridge, and integrated Finish Tone bridge in `EditorModuleDevelopControls.cpp`.
- Remaining work: later organization passes can split Auto guidance sliders, scene-prep bridge controls, finish-tone controls, or pure payload comparison/build helpers from `EditorModuleDevelopControls.cpp`; keep future Manual RAW control changes in `EditorModuleDevelopManualRawControls.*`.
- Deferred-scope changes: none. This did not implement Manual-to-Auto bias preservation, locks, expert handoff controls, RAW exposure solver redesign, demosaic/denoise redesign, color-management changes, or any other deferred feature work.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed.

## 2026-06-20 Organization Refactor Handoff - Develop Auto Status Controls

- Status delta: no Develop guide status changed. This pass is structural only and does not change Auto solve behavior, candidate feedback scheduling, quiet-window timing, status text semantics, JSON schemas, graph schemas, render math, Develop identity, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopControls.cpp`, `src/Editor/Internal/EditorModuleDevelopAutoStatusControls.h`, `src/Editor/Internal/EditorModuleDevelopAutoStatusControls.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved compact Develop Auto status/readouts for selected candidates, rendered feedback, timing/readback diagnostics, current solve placement, dynamic-range/local-exposure strategy, subject-map/solve-note summaries, and candidate-feedback quiet-window state into `EditorModuleDevelopAutoStatusControls.*`.
- Implemented: kept `EditorModule::RenderRawDevelopControls`, Auto/Manual mode flow, Auto guidance sliders, Manual RAW controls, Scene Prep bridge, and integrated Finish Tone bridge in `EditorModuleDevelopControls.cpp`.
- Remaining work: later organization passes can split Auto guidance sliders, scene-prep bridge controls, finish-tone controls, or pure payload comparison/build helpers from `EditorModuleDevelopControls.cpp`; keep future compact Auto readout changes in `EditorModuleDevelopAutoStatusControls.*`. Manual RAW controls were split in the later handoff above.
- Deferred-scope changes: none. This did not implement requested-vs-achieved explanation UI, score-component panels, candidate timelines, graph controls, candidate gallery UI, Manual handoff, or other Guide 10 deferred diagnostics work.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed.

## 2026-06-20 Organization Refactor Handoff - Develop Subject Controls

- Status delta: no Develop guide status changed. This pass is structural only and does not change Subject Importance behavior, brush behavior, overlay behavior, Auto reanalysis policy, interaction quiet-window reporting, graph schemas, solver behavior, render math, Develop identity, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopControls.cpp`, `src/Editor/Internal/EditorModuleDevelopSubjectControls.h`, `src/Editor/Internal/EditorModuleDevelopSubjectControls.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved Develop Subject Importance controls for region/brush editing, overlay toggles, interpreted/refined map display toggles, per-stroke/per-region editing, normalization trigger, and subject-edit interaction reporting into `EditorModuleDevelopSubjectControls.*`.
- Implemented: kept `EditorModule::RenderRawDevelopControls`, Auto/Manual mode flow, Auto guidance sliders, Manual RAW controls, Scene Prep bridge, and integrated Finish Tone bridge in `EditorModuleDevelopControls.cpp`.
- Remaining work: later organization passes can split Auto guidance sliders, scene-prep bridge controls, finish-tone controls, or pure payload comparison/build helpers from `EditorModuleDevelopControls.cpp`; keep future subject-importance UI changes in `EditorModuleDevelopSubjectControls.*`. Auto status readouts and Manual RAW controls were split in later handoffs above.
- Deferred-scope changes: none. This did not implement edge-aware subject refinement, semantic/AI subject detection, graph-control UI, candidate gallery UI, Manual handoff, or other Guide 05 deferred feature work.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed.

## 2026-06-20 Organization Refactor Handoff - Renderer HDR Merge Pass

- Status delta: no Develop guide status changed. This pass is structural only and does not change HDR merge behavior, alignment math, reference selection, deghost policy, shader source, render math, graph execution, cache behavior, Develop behavior, graph schemas, or deferred feature status.
- Files changed: `src/Renderer/Internal/RenderPipelineNodePasses.cpp`, `src/Renderer/Internal/RenderPipelineHdrMergePass.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `RenderPipeline::RenderHdrMerge`, HDR merge texture feature readback, feature-image construction, reference selection, translation/threshold alignment scoring, alignment confidence, and deghost uniform setup into `RenderPipelineHdrMergePass.cpp`.
- Implemented: left generated mask, custom-mask, mix, data math, mask utility, image-to-mask, and channel split/combine pass implementations in `RenderPipelineNodePasses.cpp` at the time of the HDR split. The later custom-mask handoff above moved custom-mask rasterization into `RenderPipelineCustomMaskPass.cpp`.
- Remaining work: keep HDR merge alignment/render-pass work in `RenderPipelineHdrMergePass.cpp`; later renderer organization passes should split another remaining pass family only when it grows into a clear owner.
- Deferred-scope changes: none. This did not implement HDR merge alignment redesign, staged renderer work, sidecar stats, candidate gallery, View Transform behavior changes, or any other deferred feature.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed.

## 2026-06-20 Organization Refactor Handoff - Renderer Image Generator Pass

- Status delta: no Develop guide status changed. This pass is structural only and does not change Image Generator behavior, generated text layout, font data, shader source, render math, graph execution, cache behavior, Develop behavior, graph schemas, or deferred feature status.
- Files changed: `src/Renderer/Internal/RenderPipelineNodePasses.cpp`, `src/Renderer/Internal/RenderPipelineImageGeneratorPass.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved `RenderPipeline::GenerateImageTexture`, generated text canvas construction, UTF-8 text decoding, embedded font access, and stb_truetype rasterization into `RenderPipelineImageGeneratorPass.cpp`.
- Implemented: left generated mask, custom-mask, mix, data math, mask utility, image-to-mask, and channel split/combine pass implementations in `RenderPipelineNodePasses.cpp` at the time of the Image Generator split; HDR merge was split into `RenderPipelineHdrMergePass.cpp` in a later handoff, and custom-mask rasterization was split into `RenderPipelineCustomMaskPass.cpp` in the newer custom-mask handoff above.
- Remaining work: keep generated image and text rendering changes in `RenderPipelineImageGeneratorPass.cpp`; later renderer organization passes should split another remaining pass family only when it grows into a clear owner.
- Deferred-scope changes: none. This did not implement generated-image feature changes, text shaping redesign, HDR merge alignment redesign, View Transform behavior changes, staged renderer work, or any other deferred feature.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed.

## 2026-06-20 Organization Refactor Handoff - Tone Curve Viewport Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change tone-curve math, viewport sampling behavior, target-drag behavior, integrated-tone JSON/schema behavior, render submission policy, cache behavior, Develop behavior, graph schemas, render math, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleToneCurveViewport.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved tone-curve viewport stats probing, focused-node resolution, curve-input/final-preview pixel sampling, probe clear/update behavior, and target-drag lifecycle into `EditorModuleToneCurveViewport.cpp`.
- Implemented: left render snapshots, dirty-state orchestration, render-worker submission/result consumption, tone-curve auto rewrite feedback, Develop feedback application, and output/cache adoption in `EditorModuleRendering.cpp`.
- Remaining work: later organization passes can split render-worker result consumption from `EditorModuleRendering.cpp` if that area grows again; keep new tone-curve viewport sampling or targeting changes in `EditorModuleToneCurveViewport.cpp`.
- Deferred-scope changes: none. This did not implement View Transform behavior changes, Tone Curve surface redesign, render worker queue redesign, candidate gallery, staged renderer, Manual/Auto handoff, or any other deferred feature.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed.

## 2026-06-20 Organization Refactor Handoff - Editor Render Request Builders

- Status delta: no Develop guide status changed. This pass is structural only and does not change preview/composite request behavior, render submission policy, dirty-generation behavior, cache behavior, tone-curve feedback behavior, Develop behavior, graph schemas, render math, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRenderRequests.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved preview-like refresh deferral/admission helpers, `BuildCompositeOutputRequests`, and `BuildPreviewRequests` into `EditorModuleRenderRequests.cpp`.
- Implemented: left render snapshot construction, dirty-state orchestration, render-worker submission/result consumption, tone-curve auto rewrite feedback, Develop feedback application, and output/cache adoption in `EditorModuleRendering.cpp`.
- Remaining work: later organization passes can split tone-curve viewport probing or render-worker result consumption if those areas grow again.
- Deferred-scope changes: none. This did not implement a render worker queue redesign, interruptible rendering, preview cache redesign, composite output sizing policy changes, candidate gallery, staged renderer, or any other deferred feature.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed after the split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed.

## 2026-06-20 Organization Refactor Handoff - Develop Candidate Request Construction

- Status delta: no Develop guide status changed. This pass is structural only and does not change candidate selection, render budgets, quiet-window timing, candidate ids, JSON schemas, rendered metric thresholds, solver behavior, render math, graph schemas, or deferred feature status.
- Files changed: `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp`, `src/Editor/Internal/EditorModuleDevelopCandidateRequests.cpp`, `src/Editor/Internal/EditorModuleDevelopCandidateShared.h`, `src/Editor/Internal/EditorModuleDevelopCandidateShared.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved candidate render request construction, request budget/admission policy, quiet-window scheduling/deferred feedback helpers, subject metric sampling, adaptive render budget validation helpers, request-side candidate classification, and copied candidate payload mutation into `EditorModuleDevelopCandidateRequests.cpp`.
- Implemented: kept rendered metric scoring/classification, duplicate and stage-boundary rendered analysis, rendered feedback history/continuation records, and feedback JSON application in `EditorModuleDevelopCandidateFeedback.cpp`.
- Implemented: introduced `EditorModuleDevelopCandidateShared.h/.cpp` for the narrow shared candidate-stage and stage-cache vocabulary used by both request construction and feedback writeback.
- Remaining work: later organization passes can split rendered scoring/classification from feedback JSON writeback if the feedback file grows again; keep new scheduler/payload work in the request file.
- Deferred-scope changes: none. This did not implement candidate gallery UI, background queue, staged render controller redesign, graph controls, subject brush behavior, learning application, Manual handoff, or any other deferred feature.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed.

## 2026-06-20 Organization Refactor Handoff - Render Pipeline Node and Raw Detail Passes

- Status delta: no Develop guide status changed. This pass is structural only and does not change render math, shader source, cache keys, graph traversal, resource lifecycle, graph schemas, Develop behavior, or deferred feature status.
- Files changed: `src/Renderer/RenderPipeline.cpp`, `src/Renderer/Internal/RenderPipelineNodePasses.cpp`, `src/Renderer/Internal/RenderPipelineRawDetailPasses.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: initially moved mask/custom-mask, mix, data math, HDR merge, utility/image-to-mask/image-generator, and channel split/combine render-pass methods plus their local helper code into `RenderPipelineNodePasses.cpp`; later handoffs split Image Generator into `RenderPipelineImageGeneratorPass.cpp`, HDR Merge into `RenderPipelineHdrMergePass.cpp`, and Custom Mask rasterization into `RenderPipelineCustomMaskPass.cpp`.
- Implemented: moved Auto Gain scene stats, effective Raw Detail Fusion setting resolution, Pre-Local Exposure summary construction, Raw Detail Auto Mask, Raw Detail Fusion, and `GetPreLocalExposureSummary` into `RenderPipelineRawDetailPasses.cpp`.
- Implemented: reduced `RenderPipeline.cpp` to the layer-stack execution shell, `ExecuteGraph` forwarding, and masked layer execution path.
- Remaining work: if renderer pass code grows again, split another remaining pass family only when it has a clear owner; keep custom-mask rasterization in `RenderPipelineCustomMaskPass.cpp`, generated image/text rendering in `RenderPipelineImageGeneratorPass.cpp`, HDR merge work in `RenderPipelineHdrMergePass.cpp`, and scene-prep/local-exposure math in `RenderPipelineRawDetailPasses.cpp`.
- Deferred-scope changes: none. This did not implement a physical staged renderer, sidecar stats bus, candidate gallery, spatial maps, View Transform behavior, or any other deferred feature.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed.

## 2026-06-20 Organization Refactor Handoff - Node Graph UI Visual Helpers

- Status delta: no Develop guide status changed. This pass is structural only and does not change graph UI behavior, node layout, visual styling, interaction behavior, graph schemas, render math, or deferred feature status.
- Files changed: `src/Editor/NodeGraph/EditorNodeGraphUI.cpp`, `src/Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h`, `src/Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: moved graph style tokens, node family styles, node metrics, presentation profiles, link visual styles, socket/link color helpers, mini preview/spotlight drawing helpers, and node/graph label helpers out of the central node graph UI file into a focused visual presentation module.
- Implemented: kept the main `EditorNodeGraphUI.cpp` responsible for canvas rendering orchestration, node body composition, groups, links, and interaction ordering so this split remains presentation-only.
- Remaining work: later organization passes should only split `EditorNodeGraphUI.cpp` again if layout-cache construction, socket-anchor placement, or render-order behavior grows into a new clear owner. Canvas orchestration, node body drawing, link/group rendering, and interaction handling live in focused files above.
- Deferred-scope changes: none. This did not implement graph controls, candidate gallery UI, subject brush changes, learning, View Transform changes, or any other deferred feature.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed.

## 2026-06-20 Organization Refactor Handoff - Auto-Solve Validation Scenarios

- Status delta: no Develop guide status changed. This pass is structural only and does not change Develop solver behavior, validation assertions, scenario order, render math, serialized kind strings, node titles, graph schemas, or deferred feature status.
- Files changed: `src/App/Validation/Suites/DevelopAutoSolveValidation.cpp`, `src/App/Validation/Suites/DevelopAutoSolveValidationScenarios/01_initial_solve_and_memory.inl`, `src/App/Validation/Suites/DevelopAutoSolveValidationScenarios/02_regional_subject_scenarios.inl`, `src/App/Validation/Suites/DevelopAutoSolveValidationScenarios/03_candidate_payload_and_scheduler.inl`, `src/App/Validation/Suites/DevelopAutoSolveValidationScenarios/04_rendered_feedback_scenarios.inl`, `src/App/Validation/Suites/DevelopAutoSolveValidationScenarios/05_core_dynamic_range_profiles.inl`, `src/App/Validation/Suites/DevelopAutoSolveValidationScenarios/06_result_aggregation_and_report.inl`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`.
- Implemented: reduced `DevelopAutoSolveValidation.cpp` to the suite wrapper and six named scenario includes so future bug-fix passes can edit initial solve/memory, regional/subject, candidate payload/scheduler, rendered feedback, core dynamic-range profile, or final aggregation/report code without navigating one 5,000-line function body.
- Implemented: kept the scenario fragments inside the same validation function scope intentionally, preserving shared local validation state, assertion order, failure-report variables, and the public `--validate-develop-auto-solve` behavior.
- Remaining work: later organization passes can convert individual scenario fragments into true helper functions with result structs once each fragment's data boundary is clear. The next highest-value code split is now graph UI layout/drawing orchestration, Develop controls subpanels, or candidate request ownership, depending on nearby work.
- Deferred-scope changes: none. This did not implement candidate gallery UI, graph controls, learning application, Manual handoff, spatial maps, or any other deferred feature.
- Validation run: `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\StackGraphBehaviorTests.exe` passed.

## 2026-06-19 Organization Refactor Handoff - Develop Type Home + Validation Utilities

- Status delta: no Develop guide status changed. This pass is structural only and does not change Develop behavior, solver contracts, render math, serialized kind strings, node titles, graph schemas, or deferred feature status.
- Files changed: `CMakeLists.txt`, `cmake/StackSources.cmake`, `.gitignore`, `src/Develop/DevelopTypes.h`, `src/Editor/EditorModuleTypes.h`, `src/Editor/EditorModule.h`, `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleView.cpp`, `src/Editor/Internal/EditorModuleHdrMergeStatus.cpp`, `src/Editor/Internal/EditorModuleHdrMergeControls.cpp`, `src/Editor/Internal/EditorModulePreLocalExposureControls.h`, `src/Editor/Internal/EditorModulePreLocalExposureControls.cpp`, `src/Editor/Internal/EditorModuleRawBasicControls.cpp`, `src/Editor/Internal/EditorModuleRawControlShared.h`, `src/Editor/Internal/EditorModuleDevelopControls.cpp`, `src/Editor/NodeGraph/NodeGraphTypes.h`, `src/Editor/NodeGraph/NodeGraphPayloads.h`, `src/Editor/NodeGraph/NodeGraphModelTypes.h`, `src/Editor/NodeGraph/EditorNodeGraph.h`, `src/Editor/NodeGraph/EditorNodeGraphUI.cpp`, `src/Editor/NodeGraph/UI/EditorNodeGraphUIClipboard.cpp`, `src/Editor/NodeGraph/UI/EditorNodeGraphUIConnections.cpp`, `src/Editor/NodeGraph/UI/EditorNodeGraphUIInteraction.cpp`, `src/Editor/NodeGraph/UI/EditorNodeGraphUIState.cpp`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphDevelopSerialization.h`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphDevelopSerialization.cpp`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.h`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.cpp`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphLutSerialization.h`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphLutSerialization.cpp`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphCustomMaskSerialization.h`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphCustomMaskSerialization.cpp`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphUtilitySerialization.h`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphUtilitySerialization.cpp`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.h`, `src/Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.cpp`, `src/Renderer/RenderPipeline.cpp`, `src/Renderer/Internal/RenderPipelineReadback.cpp`, `src/Renderer/Internal/RenderPipelineResources.cpp`, `src/Renderer/Internal/RenderPipelinePrograms.cpp`, `src/App/Validation/ValidationCommandRunner.cpp`, `src/App/Validation/ValidationSuites.h`, `src/App/Validation/Suites/DevelopAutoSolveValidation.cpp`, `src/App/Validation/Suites/DevelopAutoSolveValidationHelpers.h`, `src/App/Validation/Suites/DevelopAutoSolveValidationHelpers.cpp`, `src/App/Validation/Suites/DevelopAutoSolveValidationCandidateProbes.h`, `src/App/Validation/Suites/DevelopAutoSolveValidationCandidateProbes.cpp`, `src/App/Validation/Suites/DevelopAutoSolveValidationRenderedMetrics.h`, `src/App/Validation/Suites/DevelopAutoSolveValidationRenderedMetrics.cpp`, `src/App/Validation/Suites/DevelopSmokeValidation.cpp`, `src/App/Validation/Suites/ToneCurveValidation.cpp`, `src/App/Validation/ValidationImageUtils.h`, `src/App/Validation/ValidationImageUtils.cpp`, `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Files changed (continued): `src/Editor/Internal/EditorModuleReferenceSources.cpp` now owns editor reference/source pixel bridge methods that previously lived in `src/Editor/EditorModule.cpp`.
- Files changed (continued): `src/Editor/Internal/EditorModuleGraphSnapshot.cpp` now owns editor-to-render graph layer/mask/snapshot assembly and render payload conversion helpers that previously lived in `src/Editor/EditorModule.cpp`.
- Files changed (continued): `src/Editor/Internal/EditorModuleToolbar.cpp` now owns editor floating toolbar rendering, embedded toolbar icon texture loading, toolbar popup handling, and subwindow switching methods that previously lived in `src/Editor/EditorModule.cpp`.
- Files changed (continued): `src/Editor/Internal/EditorModulePreviewState.cpp` now owns editor preview/scope revision helpers, cached preview pixel lookup, Auto Gain mask preview toggling, workspace color fallback, and the Graph Performance overlay that previously lived in `src/Editor/EditorModule.cpp`.
- Files changed (continued): `src/Editor/Internal/EditorModuleComposite.cpp` now owns composite chain label/fingerprint helpers and deprecated composite-node shim methods that previously lived in `src/Editor/EditorModule.cpp`.
- Files changed (continued): `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp` now owns the large Develop Auto solver implementation, candidate scoring/feedback helpers, dynamic-range/local-exposure strategy helpers, subject-importance interpretation/refinement helpers, and selected-node subject viewport/brush bridge methods that previously lived in `src/Editor/EditorModule.cpp`.
- Files changed (continued): `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp` now owns Develop rendered candidate request selection, candidate-feedback quiet-window/deferred gates, rendered metric scoring/classification helpers, rendered feedback JSON application, and candidate-feedback validation helper implementations that previously lived in `src/Editor/Internal/EditorModuleRendering.cpp`.
- Files changed (continued): `src/Editor/Internal/EditorModuleDevelopDefaults.h` now owns shared Develop default integrated ToneCurve JSON construction and RAW metadata default settings construction. `src/Editor/Internal/EditorModuleRawControlShared.h` delegates RAW Decode default settings to the same helper so graph mutation, Auto solve, and RAW controls do not keep separate copies.
- Implemented: created `src/Develop/DevelopTypes.h` as the first real Develop-domain type home for `RawDevelopPayload`, Auto intent/guidance, Auto/Manual mode, and subject-importance map types.
- Implemented: kept existing `EditorNodeGraph::RawDevelopPayload`, `EditorNodeGraph::DevelopAutoIntent`, `EditorNodeGraph::DevelopAutoGuidance`, `EditorNodeGraph::DevelopSubjectImportance*`, and related string helper names as compatibility aliases/wrappers so call sites and serialization behavior stay stable.
- Implemented: split reusable validation image/readback/path helpers out of `src/App/Validation/ValidationCommandRunner.cpp` into `src/App/Validation/ValidationImageUtils.h/.cpp`.
- Implemented: split validation CLI dispatch from validation suite implementation. `src/App/Validation/ValidationCommandRunner.cpp` now owns command routing, while `src/App/Validation/ValidationSuites.h` is the public suite boundary for focused implementations under `src/App/Validation/Suites/`.
- Implemented: split the Tone Curve validation suite out of `src/App/Validation/ValidationSuites.cpp` into `src/App/Validation/Suites/ToneCurveValidation.cpp`, preserving `--validate-tone-curve-auto` behavior.
- Implemented: split Develop graph-state serialization checks, synthetic Develop node smoke, and optional real-RAW smoke validation out of the validation suite implementation into `src/App/Validation/Suites/DevelopSmokeValidation.cpp`.
- Implemented: moved the solver-only `ValidateDevelopAutoSolveBehavior` body into `src/App/Validation/Suites/DevelopAutoSolveValidation.cpp`, leaving `src/App/Validation/ValidationSuites.h` as the public validation-suite declaration boundary.
- Implemented: split reusable Develop Auto-solve validation helpers for staged-state lookup, guidance JSON extraction, and candidate probe id checks into `src/App/Validation/Suites/DevelopAutoSolveValidationHelpers.h/.cpp`, leaving the scenario order and assertions in `DevelopAutoSolveValidation.cpp`.
- Implemented: split the initial solver-only Auto-solve candidate-family JSON scan into `DevelopAutoSolveCandidateProbeSummary` / `BuildDevelopAutoSolveCandidateProbeSummary` in `src/App/Validation/Suites/DevelopAutoSolveValidationCandidateProbes.h/.cpp`, covering cleanup/detail, mode-neighbor, highlight-protected mids, finish-tone, first-pass tone, and white-balance probe detection.
- Implemented: split synthetic rendered-metric fixture construction, rendered classifier probes, relative comparison probe setup, and rendered-metric JSON fixture serialization into `DevelopAutoSolveRenderedMetricFixtures` / `BuildDevelopAutoSolveRenderedMetricFixtures` in `src/App/Validation/Suites/DevelopAutoSolveValidationRenderedMetrics.h/.cpp`.
- Implemented: created `src/Editor/NodeGraph/NodeGraphTypes.h` for basic node graph primitives such as socket ids, node/socket enums, `Vec2`, `SocketDefinition`, and small DataMath socket helpers.
- Implemented: created `src/Editor/NodeGraph/NodeGraphPayloads.h` for node payload/settings structs such as image/raw/LUT aliases, raw decode/neural/detail/HDR payloads, mask settings, custom mask payloads, image-to-mask settings, image generator settings, and data-math settings.
- Implemented: created `src/Editor/NodeGraph/NodeGraphModelTypes.h` for graph document structs such as `Node`, `Link`, `NodeGroup`, `CompletedChainInfo`, `LinkRole`, and `ValidationResult`, leaving `EditorNodeGraph.h` focused on the `Graph` API.
- Implemented: split Develop subject-importance graph serialization helpers out of `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp` into `src/Editor/NodeGraph/Serialization/EditorNodeGraphDevelopSerialization.h/.cpp`, preserving the existing `developSubjectImportance` JSON keys, limits, defaults, and active-id fallback behavior.
- Implemented: split RAW metadata/settings, RAW Detail Fusion scene-prep settings, and HDR merge settings serialization helpers out of `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp` into `src/Editor/NodeGraph/Serialization/EditorNodeGraphRawSerialization.h/.cpp`, preserving existing JSON keys, defaults, and fallback behavior.
- Implemented: split LUT payload, 1D/3D stage, import-format, use-mode, and transfer-function serialization helpers out of `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp` into `src/Editor/NodeGraph/Serialization/EditorNodeGraphLutSerialization.h/.cpp`, preserving existing LUT JSON keys and legacy `lookupMode` fallback behavior.
- Implemented: split Custom Mask payload, object, raster, reference-mode, operation, and editor-tool serialization helpers out of `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp` into `src/Editor/NodeGraph/Serialization/EditorNodeGraphCustomMaskSerialization.h/.cpp`, preserving existing Custom Mask JSON keys and legacy shape-feather fallback behavior.
- Implemented: split utility-node serialization helpers for Scope, Mask Generator, Mask Combine, Mask Utility, Image-to-Mask, Image Generator, Mix, and Data Math out of `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp` into `src/Editor/NodeGraph/Serialization/EditorNodeGraphUtilitySerialization.h/.cpp`, preserving existing JSON keys and legacy string fallbacks.
- Implemented: split image-node PNG byte persistence helpers out of `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp` into `src/Editor/NodeGraph/Serialization/EditorNodeGraphImageSerialization.h/.cpp`, keeping image row-flip, STB encode/decode, and binary JSON byte handling separate from top-level graph assembly.
- Implemented: created `src/Editor/EditorModuleTypes.h` for passive editor state and view-model structs/enums such as composite export state, graph preview/performance stats, HDR merge status, Develop subject viewport state, node-browser thumbnail state, deferred project apply state, and related runtime caches. `EditorModule.h` now keeps the previous `EditorModule::...` names as aliases so existing call sites remain stable while the coordinator header no longer owns those type definitions.
- Implemented: moved detached preview monitor placement, fullscreen toggle/close state, and detached preview window rendering from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleView.cpp`, preserving the public `EditorModule` methods and callers.
- Implemented: moved editor reference/source pixel bridge methods for shared image pixels, render image payloads, copied image fallback pixels, and reference-source dimensions from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleReferenceSources.cpp`, preserving the public/private `EditorModule` method signatures and callers.
- Implemented: moved editor-to-render graph layer/mask/snapshot assembly and render payload conversion helpers from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`, preserving `EditorModule` method signatures, snapshot contents, graph link filtering, and render payload mapping.
- Implemented: moved editor floating toolbar rendering, toolbar icon texture loading, popup-safe toolbar switching helpers, and subwindow switching methods from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleToolbar.cpp`, preserving existing toolbar behavior and call sites.
- Implemented: moved editor preview/scope revision helpers, cached preview pixel lookup, Auto Gain mask preview toggling, workspace color fallback, and the Graph Performance overlay from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModulePreviewState.cpp`, preserving existing `EditorModule` method signatures and preview/render state behavior.
- Implemented: moved composite chain label/fingerprint helpers and the deprecated composite-node shim methods from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleComposite.cpp`, preserving completed-chain cache inputs, labels, fingerprints, and existing composite callers.
- Implemented: moved the Develop Auto solver and subject-guidance bridge from `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp`, preserving public `EditorModule` method signatures, Auto JSON keys, candidate ids, subject overlay state, subject brush behavior, and graph mutation trigger ownership.
- Implemented: moved Develop rendered candidate request selection and feedback application out of `src/Editor/Internal/EditorModuleRendering.cpp` into `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp`, preserving public `EditorModule` method signatures, candidate request/result fields, JSON diagnostics, feedback gates, and render-worker callers.
- Implemented: centralized duplicated Develop default helpers in `src/Editor/Internal/EditorModuleDevelopDefaults.h`, preserving default integrated ToneCurve payloads and RAW metadata default settings while removing local copies from Auto solve and graph mutation code.
- Implemented: split HDR merge status/topology helpers out of `src/Editor/EditorModule.cpp` into `src/Editor/Internal/EditorModuleHdrMergeStatus.cpp`, preserving the existing `EditorModule` methods used by render submission, RAW controls, and node graph UI.
- Implemented: split the HDR Merge node settings panel and its private label/clamp/change-detection helpers out of `src/Editor/Internal/EditorModuleRawUI.cpp` into `src/Editor/Internal/EditorModuleHdrMergeControls.cpp`, preserving `EditorModule::RenderHdrMergeControls` and sidebar/node graph callers.
- Implemented: split the shared Pre-Local Exposure/Auto Gain helper cluster, Develop Scene Prep helper controls, and the standalone Raw Detail Auto Mask / Raw Detail Fusion node panels out of `src/Editor/Internal/EditorModuleRawUI.cpp` into `src/Editor/Internal/EditorModulePreLocalExposureControls.h/.cpp`, preserving existing `EditorModule` methods and callers.
- Implemented: renamed the remaining Develop panel implementation from `src/Editor/Internal/EditorModuleRawUI.cpp` to `src/Editor/Internal/EditorModuleDevelopControls.cpp` after the RAW basic, HDR Merge, and Pre-Local Exposure panels were split out; `RenderRawDevelopControls` and existing callers remain unchanged.
- Implemented: split RAW Source, RAW Neural Denoise, and RAW Decode control panels out of `src/Editor/Internal/EditorModuleRawUI.cpp` into `src/Editor/Internal/EditorModuleRawBasicControls.cpp`, with shared RAW-control helpers in `src/Editor/Internal/EditorModuleRawControlShared.h`.
- Implemented: split node graph clipboard, graph-text copy/paste, duplicate, and preset-payload import behavior out of `src/Editor/NodeGraph/EditorNodeGraphUI.cpp` into `src/Editor/NodeGraph/UI/EditorNodeGraphUIClipboard.cpp`, leaving the main UI file focused on canvas rendering, node drawing, and interaction.
- Implemented: split node graph auto-connect and channel-routing helpers out of `src/Editor/NodeGraph/EditorNodeGraphUI.cpp` into `src/Editor/NodeGraph/UI/EditorNodeGraphUIConnections.cpp`, preserving existing `EditorNodeGraphUI` helper signatures and callers.
- Implemented: split node graph interaction support helpers out of `src/Editor/NodeGraph/EditorNodeGraphUI.cpp` into `src/Editor/NodeGraph/UI/EditorNodeGraphUIInteraction.cpp`, covering graph hover/mouse-owner classification, node drag-region hit tests, validation status overlay, channel-split confirmation prompt, and the interaction debug overlay while leaving the main canvas draw loop and interaction state machine in place at that time. Later handoffs moved `RenderInteraction` into the interaction file and canvas orchestration into `EditorNodeGraphUICanvas.cpp`.
- Implemented: split node graph active graph/layer accessors, layer-surface classification, complex-editor classification, and preset preview graph cache lookup out of `src/Editor/NodeGraph/EditorNodeGraphUI.cpp` into `src/Editor/NodeGraph/UI/EditorNodeGraphUIState.cpp`.
- Implemented: split renderer pixel readback, cached graph image readback, preview/source/scope exports, output pixel sampling, and texture statistics out of `src/Renderer/RenderPipeline.cpp` into `src/Renderer/Internal/RenderPipelineReadback.cpp`, preserving the existing `RenderPipeline` public methods.
- Implemented: split renderer resource lifecycle, source image/pixel loading, FBO cleanup/resize, output texture publishing, graph-cache invalidation, and validation-facing RawDevelop stage-cache sizing helpers out of `src/Renderer/RenderPipeline.cpp` into `src/Renderer/Internal/RenderPipelineResources.cpp`, preserving the existing `RenderPipeline` public methods and cache policy.
- Implemented: split renderer shader/program creation out of `src/Renderer/RenderPipeline.cpp` into `src/Renderer/Internal/RenderPipelinePrograms.cpp`, preserving the existing `Ensure*Program` methods and all embedded shader source strings while leaving render-pass helpers in place.
- Implemented: moved app and graph-test source inventory from the root `CMakeLists.txt` into `cmake/StackSources.cmake`, and added a `.gitignore` exception so project CMake includes are not hidden by the broad `*.cmake` ignore.
- Implemented: added `docs/engineering/architecture/ARCHITECTURE_ORGANIZATION_GUIDE.md`, a short standing rule for future passes to consider file/folder boundaries before adding to existing large files.
- Validation: fresh `build_codex_organization_verify` configure was blocked by network failure while cloning ImGui, before project code compiled. Existing `build` reconfigured successfully with local `_deps`; `$env:CL='/FS'; cmake --build build --config Debug --target StackGraphBehaviorTests -- /m:1` passed; `build\StackGraphBehaviorTests.exe` passed. The normal `build\Stack.exe` relink later became blocked by a running local `build\Stack.exe` process, so final app verification used `build_codex_verify`: `cmake -S . -B build_codex_verify` passed; `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` passed before and after `EditorModuleTypes.h`, after the node graph clipboard split, after the HDR merge status split, after the renderer readback split, after the RAW basic controls split, after the Tone Curve validation suite split, after the Develop smoke validation suite split, after the Develop subject-importance serializer split, after the RAW/HDR serializer split, after the LUT serializer split, after the Custom Mask serializer split, after the utility-node serializer split, after the image-node serializer split, after the HDR Merge controls split, after the Pre-Local Exposure controls split, after the Develop controls file rename, after the Develop Auto-solve validation suite move, after the node graph interaction support split, after the renderer resource lifecycle split, after the renderer shader/program setup split, after the node graph connection helper split, after the node graph state/access helper split, and after the Develop Auto-solve validation helper split; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed.
- Validation: after the candidate-probe summary and rendered-metric fixture extractions, including the focused helper-file split, `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- Validation: after moving detached preview view/window lifecycle code into `src/Editor/Internal/EditorModuleView.cpp`, `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- Validation: after moving editor reference/source pixel bridge methods into `src/Editor/Internal/EditorModuleReferenceSources.cpp`, `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- Validation: after moving editor-to-render graph snapshot assembly into `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`, `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- Validation: after moving floating toolbar and subwindow switching code into `src/Editor/Internal/EditorModuleToolbar.cpp`, `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- Validation: after moving preview/cache/performance state into `src/Editor/Internal/EditorModulePreviewState.cpp` and composite chain helpers into `src/Editor/Internal/EditorModuleComposite.cpp`, `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- Validation: after moving the Develop Auto solver and subject-guidance bridge into `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp`, `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed.
- Validation: after moving Develop candidate request/feedback ownership into `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp` and consolidating shared defaults in `src/Editor/Internal/EditorModuleDevelopDefaults.h`, `cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` exited 0; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; scoped `git diff --check` passed with only LF-to-CRLF warnings; trailing-whitespace search over touched source/docs returned no matches.
- Remaining work: this does not split the large solver-only `ValidateDevelopAutoSolveBehavior` suite into smaller per-guide/scenario files, split renderer render-pass helper families further, split top-level graph document assembly out of the central node graph serializer, finish splitting the large node graph canvas renderer / `RenderInteraction` state machine, or replace the app source glob with fully explicit per-module source lists. The Develop panel now lives in `src/Editor/Internal/EditorModuleDevelopControls.cpp`; later handoffs split Subject Importance controls into `EditorModuleDevelopSubjectControls.*` and Auto status readouts into `EditorModuleDevelopAutoStatusControls.*`, but Auto guidance sliders, manual RAW controls, scene-prep bridge, and integrated tone sections remain future organization targets.
- Deferred-scope changes: added a structural note only. This pass intentionally creates organization boundaries without changing deferred feature status.
- Next recommended starting point: continue extracting Develop pure logic into `src/Develop/` behind the compatibility aliases, or continue the node graph UI split by separating canvas rendering/layout from input interaction now that clipboard/preset import is isolated.

## 2026-06-16 Renderer-Adjacent Refactor Handoff - Graph Execution Core + Candidate Worker Reuse

- Status delta: no Develop guide status changed. This pass is an internal renderer/worker structure cleanup with low-risk execution-path optimizations; it does not change solver contracts, payload schemas, rendered output targets, or user-facing Develop behavior.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/RenderPipeline.cpp`, `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/Internal/EditorRenderWorkerCandidateRendering.cpp`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`.
- Implemented: extracted the main graph hot path out of `RenderPipeline.cpp` into `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`, leaving shader-kernel setup, texture ownership, and readback/export helpers in the original file.
- Implemented: added a per-execution graph context that preindexes render nodes by `nodeId` and input links by `(toNodeId, toSocketId)`, then uses those indexes throughout graph execution, fingerprinting, scalar checks, RAW/HDR input resolution, and final output traversal.
- Implemented: replaced the old active-node tree set inside graph execution with active-node checks backed by the node index, keeping cache/fingerprint semantics unchanged while removing repeated membership bookkeeping.
- Implemented: cleaned up RawDevelop stage-cache trimming so one trim/store pass keeps a local running byte total instead of recomputing total cache bytes from scratch on every eviction step. Cache eligibility thresholds, fingerprints, and ownership rules are unchanged.
- Implemented: extracted Develop candidate rendering out of `EditorRenderWorker.cpp` into `src/Editor/Internal/EditorRenderWorkerCandidateRendering.cpp`.
- Implemented: preindexed `snapshot.graph.nodes` once per worker snapshot for candidate rendering and reused one mutable prepared candidate graph per Develop node per snapshot, appending the synthetic final/pre-finish outputs once and mutating only the `rawDevelop` payload plus request revision for each candidate render.
- Validation: `cmake -S . -B build_codex_verify -G "Visual Studio 17 2022"` passed; `cmake --build build_codex_verify --config Debug --target Stack StackGraphBehaviorTests -- /m:1` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed. The normal `build\Stack.exe` relink remains blocked while a live local `build\Stack.exe` process is running.
- Remaining work: this does not split the readback/export half of `RenderPipeline.cpp`, does not redesign candidate scheduling or gallery UI, and does not change render math, cache keys, or image quality.
- Deferred-scope changes: none. This pass intentionally keeps Guide 03/04/05/08/09 behavior and deferred boundaries intact.
- Next recommended starting point: take the next renderer-adjacent pass through readback/export and preview-side helper seams, or profile the now-isolated graph execution path to decide whether texture allocation, readback, or downstream shader cost is the next bottleneck.

## 2026-06-16 Navigation Refactor Handoff - main.cpp Launcher Split + EditorModule Extraction

- Status delta: no Develop guide status changed. This pass is structural only and is meant to improve navigation without changing Develop behavior, rendering, serialization, or command-line behavior.
- Files changed: `src/main.cpp`, `src/App/Validation/ValidationCommandRunner.h`, `src/App/Validation/ValidationCommandRunner.cpp`, `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleProjectLifecycle.cpp`, `src/Editor/Internal/EditorModuleGraphMutation.cpp`, `docs/engineering/develop/spec_sources/DEVELOP_NODE_CONTEXT.txt`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`.
- Implemented: reduced `src/main.cpp` to working-directory setup, validation command dispatch, and `AppShell` startup/shutdown.
- Implemented: moved the existing validation/smoke harness behind `TryRunValidationCommand(...)` in `src/App/Validation/ValidationCommandRunner.cpp`, keeping the existing CLI flags and validation routines wired the same way.
- Implemented: extracted `EditorModule` blank-project/lifecycle helpers into `src/Editor/Internal/EditorModuleProjectLifecycle.cpp`.
- Implemented: extracted `EditorModule` graph/layer mutation methods into `src/Editor/Internal/EditorModuleGraphMutation.cpp`; at the time this included node creation helpers and the RAW full-tree builder, which now live in `src/Editor/Internal/EditorModuleGraphProcessingNodes.cpp`.
- Validation: `cmake --build build --config Debug --target Stack StackGraphBehaviorTests` passed; `build\StackGraphBehaviorTests.exe` passed; `build\Stack.exe --validate-layer-registry` passed; `build\Stack.exe --validate-tone-curve-auto` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed.
- Remaining work: this does not yet split `RenderPipeline.cpp`, and it does not try to deduplicate every file-local helper that was kept near extracted methods for safety.
- Deferred-scope changes: none. This pass changes file organization only.
- Next recommended starting point: if navigation cleanup continues, take a narrower second pass through one renderer-adjacent hotspot with stronger coupling analysis first, rather than broad extraction.

## 2026-06-15 Manual RAW Workflow Handoff - RAW Decode + Lean Tone Curve

- Status delta: Guide 02 remains `Partial`; Guide 08 is now `Partial` as standalone manual finish-curve substrate; Guide 09 remains `Not Started`. This pass adds a separate manual RAW workflow foundation without changing `Develop` identity, integrated finish-tone behavior, or Manual-to-Auto handoff semantics.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/EditorModule.h`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/Editor/LayerRegistry.cpp`, `src/Editor/Layers/ToneLayers.cpp`, `src/Editor/NodeGraph/EditorNodeGraph.cpp`, `src/Editor/NodeGraph/EditorNodeGraph.h`, `src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp`, `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`, `src/Editor/NodeGraph/EditorNodeGraphUI.cpp`, `src/Editor/NodeGraph/Model/EditorNodeGraphLayoutValidation.cpp`, `src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp`, `src/Editor/NodeGraph/Model/EditorNodeGraphTraversal.cpp`, `src/Editor/NodeGraph/UI/EditorNodeGraphUIContextMenu.cpp`, `src/Editor/NodeGraph/UI/EditorNodeGraphUINodeBrowser.cpp`, `src/Editor/UI/EditorSidebar.cpp`, `src/Library/LibraryManager.cpp`, `src/Renderer/MaskRenderTypes.h`, `src/Renderer/RenderPipeline.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: added a new graph node `RAW Decode` with editor/render payloads backed by `Raw::RawDevelopSettings`, stable serialization as `RawDecode`, RAW input plus image output sockets, and graph/browser/context-menu/sidebar/UI integration.
- Implemented: factored the shared RAW base render path out of `RawDevelop` so `RawDecode` and `RawDevelop` both reuse the same upstream RAW traversal and `RawGpuPipeline::Render(...)` setup. `RAW Decode` now stops at unclamped scene-linear RGB after RAW normalization, white balance, demosaic, highlight reconstruction, orientation, camera transform, and exposure.
- Implemented: added lean `RAW Decode` controls for source summary, white balance, `RAW Exposure / EV`, highlight reconstruction, demosaic/orientation status, and camera transform, while intentionally omitting Auto/Manual Develop mode, scene prep, finish tone, subject guidance, cleanup/denoise/input-level overrides, and debug views.
- Implemented: restored standalone `Tone Curve` as a first-class addable manual node, removed the legacy-only restriction, and slimmed its graph surface to the actual manual finish-curve editor: curve graph, channel buttons, domain control, reset actions, point editing/context menu behavior, and existing canvas targeting. Legacy standalone auto/baseline/foundation/local-mask sections and â€œDevelop owns this nowâ€ messaging were removed from that surface.
- Implemented: `Add full tree` on RAW sources now builds `RAW Source -> RAW Decode -> Tone Curve -> View Transform -> Output`.
- Validation: `cmake --build build --config Debug --target StackGraphBehaviorTests Stack` passed; `build\StackGraphBehaviorTests.exe` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-tone-curve-auto` passed.
- Remaining work: `Develop` is still the merged auto workflow, Guide 08 integrated finish-tone redesign is still not complete, and Guide 09 Manual-to-Auto bias preservation/locks/handoff controls are still not implemented.
- Deferred-scope changes: the manual RAW chain and standalone tone-curve substrate are now real. Full integrated finish-tone redesign remains `Partial`/deferred under Guide 08, and Manual-to-Auto handoff remains `Not Started` under Guide 09.
- Next recommended starting point: continue manual RAW workflow by extending true manual decode/render controls around `RAW Decode`, or continue integrated `Develop` work without folding the new manual chain back into `RawDevelop`.

## 2026-06-13 Guide 03 Processing Handoff - Candidate Render Timing Telemetry

- Status delta: Guide 03 remains `Partial`; this adds measured performance telemetry for the current rendered-feedback path and does not claim a profiler UI, graph scheduler, sidecar stats bus, candidate gallery, or automatic optimization policy.
- Files changed: `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DevelopCandidateRenderResult` now carries diagnostic timing fields for final graph execution, final pixel readback, final CPU analysis, pre-finish graph execution, pre-finish readback, pre-finish CPU analysis, and total elapsed candidate time.
- Implemented: `EditorRenderWorker::RenderSnapshot` records those timings with `std::chrono::steady_clock` around the existing candidate render/readback/analysis steps. These timings are diagnostic only and do not affect scoring or candidate selection.
- Implemented: `ApplyDevelopCandidateRenderFeedback` writes per-candidate `CandidateRenderTimingV1` fields and aggregate `autoCandidateRendered*Ms` totals into integrated Tone JSON, including the slowest candidate label and elapsed time.
- Implemented: `RenderRawDevelopControls` shows compact feedback timing in Auto status: total, graph, readback, analysis, and slowest candidate.
- Validation: initial build was blocked by a running `build\Stack.exe` (`LNK1168`); after closing that local build process, `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed. `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: this is telemetry, not a fix by itself. It should guide the next optimization toward remaining graph-copy/setup cost, texture allocation/readback cost, CPU analysis cost, or shader execution, based on observed timing.
- Deferred-scope changes: performance diagnostics moved forward as compact timing telemetry; full profiler UI, sidecar stats bus, graph scheduler, and candidate timeline remain deferred.
- Next recommended starting point: use the new timing fields during manual large-RAW stress tests to decide whether to optimize readback/analysis, candidate graph execution, or texture residency next.

## 2026-06-13 Guide 03 Processing Handoff - Candidate Graph Reuse for Pre-Finish Fallback

- Status delta: Guide 03 remains `Partial`; this is a worker memory/performance cleanup in the current rendered-feedback path and does not claim graph pooling, downscaled candidate execution, a full background queue, staged controller, sidecar stats bus, or candidate gallery.
- Files changed: `src/Editor/EditorRenderWorker.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: each Develop candidate render request now creates one mutable candidate graph copy, applies the candidate `RawDevelop` payload once, and pre-attaches separate synthetic output nodes for the visible final output and hidden pre-finish output.
- Implemented: the final candidate render and the pre-finish fallback render reuse that same mutable candidate graph by switching `outputNodeId`, instead of rebuilding the candidate graph through the helper for each socket.
- Implemented: distinct synthetic output ids remain in use for final and pre-finish fallback outputs, preserving cache/output separation while avoiding repeated graph setup.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: this still creates one mutable graph copy per candidate request. Future work can investigate graph pooling/builders, downscaled candidate execution, or stronger texture/CPU allocation telemetry.
- Deferred-scope changes: candidate-render setup cost is reduced in the current worker path; full physical staged controller, graph pooling, sidecar scheduler, and downscaled candidate execution remain deferred.
- Next recommended starting point: profile candidate-feedback passes on large RAWs to decide whether the remaining per-candidate graph copy, texture allocation, or shader execution is the dominant cost before deeper renderer restructuring.

## 2026-06-13 Guide 03 Processing Handoff - Candidate Worker Graph-Copy Reduction

- Status delta: Guide 03 remains `Partial`; this is a worker memory/performance cleanup in the current rendered-feedback path and does not claim the full background queue, staged controller, sidecar stats bus, candidate gallery, or downscaled candidate render engine.
- Files changed: `src/Editor/EditorRenderWorker.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: removed an extra `RenderGraphSnapshot graph = snapshot.graph` copy inside the per-candidate render loop. The preflight Develop-node existence check now scans `snapshot.graph.nodes` directly.
- Implemented: at the time, the actual candidate render still created its own mutable graph copy in `renderCandidateSocket`, preserving isolated candidate mutation. Superseded later the same day by the candidate graph reuse increment, which still keeps one isolated mutable graph copy per request but reuses it for final plus pre-finish fallback outputs.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: this reduces avoidable CPU memory/copy churn but does not remove the necessary mutable graph copy used for each actual candidate render, does not pool candidate graph snapshots, and does not downscale graph execution or add GPU memory telemetry.
- Deferred-scope changes: candidate-render memory pressure is slightly reduced in the current worker path; full physical staged controller, sidecar scheduler, and downscaled candidate execution remain deferred.
- Next recommended starting point: continue performance work by profiling whether the remaining one isolated graph copy per candidate, texture allocation, or shader execution dominates multi-candidate passes on large RAWs.

## 2026-06-13 Guide 03 Processing Handoff - Candidate Probe Progress Labels

- Status delta: Guide 03 remains `Partial`; this improves the existing render-worker progress HUD for active Develop candidate feedback and does not claim a full diagnostics panel, candidate timeline, gallery, sidecar scheduler, or graph controls.
- Files changed: `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: added `BuildDevelopCandidateProgressLabel`, which formats the existing `Measuring Develop feedback N/M` progress message with the candidate's human label and revision stage when present.
- Implemented: both the normal candidate-render path and the no-source/error candidate path now use the same progress-label formatter, keeping active probe feedback consistent.
- Implemented: `ValidateDevelopAutoSolveBehavior` now checks that candidate progress labels include index/count, a readable candidate label, the revision stage, and remain bounded in length.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: the progress HUD still reports one active worker label, not a queue timeline, candidate thumbnails, user picker, per-stage timings, or requested-vs-achieved explanations. Those remain Guide 10/Guide 03 future UI work.
- Deferred-scope changes: progress visibility for active candidate probes moved forward as compact HUD text; full candidate diagnostics UI remains deferred.
- Next recommended starting point: continue with either per-node candidate-feedback queue/admission diagnostics in Auto status, or deeper memory/performance work around actual candidate graph execution and texture pressure.

## 2026-06-13 Guide 03 Processing Handoff - Candidate Feedback Waiting Status

- Status delta: Guide 03 remains `Partial`; this exposes the existing candidate-feedback quiet-window/deferred-admission behavior in the Develop Auto status panel and does not claim a full diagnostics panel, progress overlay redesign, background queue, candidate gallery, sidecar stats bus, or graph controls.
- Files changed: `src/Editor/EditorModule.h`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: added a single quiet-window remaining-time helper shared by validation and scheduler status. `GetDevelopCandidateFeedbackDeferredStatus` reports whether a Develop node has deferred candidate feedback and how long remains before the queued feedback pass can resume.
- Implemented: the Develop Auto status readout now shows `Candidate feedback: waiting for edits to settle` with remaining seconds while recent edits are inside the quiet window, then `queued after edits settled` until the deferred refresh dirties the node and schedules one fresh candidate-feedback pass.
- Implemented: `ValidateDevelopAutoSolveBehavior` now covers the quiet-window remaining-time calculation, including mid-edit remaining time and zero remaining time after the quiet window or for invalid backwards time.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: the status line is intentionally compact. Full requested-vs-achieved explanations, candidate timeline UI, graph diagnostics, render queue visualization, and gallery/picker UI remain future Guide 10/Guide 03 work.
- Deferred-scope changes: solver diagnostics and progress visibility move forward as partial UI status, while the full diagnostic surface remains deferred.
- Next recommended starting point: continue Guide 03/10 by adding richer render-progress state for active candidate probes and stale-skip outcomes, preferably reusing `EditorRenderWorker::RenderProgress` and the existing Auto status panel.

## 2026-06-13 Guide 03 Processing Handoff - Quiet-Window Candidate Render Admission

- Status delta: Guide 03 remains `Partial`; this is a render-admission/scheduling improvement over the existing Develop candidate feedback path and does not claim a full background queue, candidate gallery, user picker, sidecar stats bus, or interruptible GPU cancellation.
- Files changed: `src/Editor/EditorModule.h`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `BuildDevelopCandidateRenderRequests` now checks the same 0.60s per-node quiet window used by rendered-feedback application before scheduling candidate probes. If the Develop node was edited recently and candidate metrics are still needed, the main output render continues but candidate feedback requests are skipped.
- Implemented: skipped candidate feedback scheduling calls `ScheduleDeferredDevelopCandidateFeedback`, so `RefreshDeferredDevelopCandidateFeedbackIfReady` marks the node dirty after the quiet window and one fresh candidate-feedback pass resumes from the latest authored state.
- Implemented: `ClassifyDevelopCandidateFeedbackGateForValidation` and the request-admission check now share the same quiet-window helper, and `ValidateDevelopAutoSolveBehavior` covers that recent edits defer candidate render admission and that admission opens after the quiet window.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; scoped `git diff --check` reported only line-ending warnings.
- Remaining work: this does not cancel an already-running GL graph operation, does not provide a full multi-render queue, does not expose candidate gallery/progress controls, and does not add sidecar scheduler telemetry. Those remain future Guide 03/10 work.
- Deferred-scope changes: candidate feedback admission during active edits moved from implicit stability risk to implemented scheduler behavior. Full background queue, gallery, picker, sidecar stats bus, graph diagnostics, and interruptible GPU cancellation remain deferred.
- Next recommended starting point: continue Guide 03/10 by adding richer user-visible progress/admission diagnostics so the UI can say when Auto is waiting for edits to settle, rendering candidate probes, or skipping stale feedback; keep it connected to the existing progress HUD rather than making a separate blocking overlay.

## 2026-06-13 Guide 03 Processing Handoff - Bounded Stage Cache Texture Residency

- Status delta: Guide 03 remains `Partial`; this is a memory/performance stability increment for the existing RawDevelop stage snapshot cache and does not claim the full physical staged renderer, sidecar stats bus, candidate gallery, user picker, downscaled graph execution, or real GPU memory profiler.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/RenderPipeline.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: RawDevelop stage-cache snapshots now use an explicit RGBA16F byte estimator (`width * height * 8`) because `GLHelpers::CreateEmptyTexture` stores these boundaries as RGBA16F textures.
- Implemented: `storeRawDevelopStageCacheEntry` now preflights the current render size before cloning. Small stage snapshots can keep up to six MRU fingerprints per raw-base/pre-finish key; medium/large/huge snapshots are reduced to three, two, or one entry per key; snapshots above the single-entry limit are skipped instead of cloning a risky full-size texture.
- Implemented: after each store, the RawDevelop stage cache trims least-recent stage snapshots across all RawDevelop stage keys to a 512 MiB soft estimated texture budget. This trades cache hits for stability during large-RAW candidate churn.
- Implemented: `RenderPipeline` exposes validation-only helpers for the stage-cache byte estimate, per-key max-entry policy, and cache/no-cache decision. `ValidateDevelopAutoSolveBehavior` now covers the policy, including the oversized-snapshot skip case.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; scoped `git diff --check` reported only line-ending warnings.
- Remaining work: true GPU memory telemetry, per-node cache diagnostics UI, downscaled/offscreen candidate graph execution, fuller queue/admission control, candidate thumbnail/gallery UI, sidecar stats bus, user picker/merge controls, and a standalone staged render controller remain future Guide 03/10 work.
- Deferred-scope changes: RawDevelop stage snapshot cache memory bounding moved from a known stability risk to implemented policy. Full GPU profiling, downscaled graph execution, gallery/picker, sidecar scheduler, and graph controls remain deferred.
- Next recommended starting point: continue Guide 03 in `src/Editor/EditorRenderWorker.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, and `src/Renderer/RenderPipeline.cpp` by adding richer render-progress/admission diagnostics or a candidate-family queue policy that can avoid scheduling work when a newer interaction is already likely to supersede it.

## 2026-06-13 Guide 03 Processing Handoff - Capped Candidate Metric Readback

- Status delta: Guide 03 remains `Partial`; this is a performance/memory stability increment for rendered candidate feedback and does not claim the full scheduled convergence engine, gallery, sidecar stats bus, user picker, or physical staged render controller.
- Files changed: `src/Renderer/RenderPipeline.h`, `src/Renderer/RenderPipeline.cpp`, `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/EditorModule.h`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `RenderPipeline` now has capped, vertically flipped RGBA8 readback overloads for the current output and cached graph-image textures. These are used for diagnostic metrics and preserve the orientation expected by subject-region/stroke metric sampling.
- Implemented: `BuildDevelopCandidateRenderRequests` assigns a metric-readback cap from source size: uncapped below 16 MP, 1800 px max edge at 16 MP+, 1536 px at 30 MP+, and 1280 px at 50 MP+. Main rendering still uses the real graph dimensions; only the diagnostic readback/analysis buffer is capped.
- Implemented: `EditorRenderWorker` applies the cap to final candidate metrics and hidden pre-finish metrics, including cached pre-finish reuse and fallback pre-finish renders. `DevelopCandidateRenderResult` records the cap and whether final/pre-finish metrics were downsampled.
- Implemented: `ApplyDevelopCandidateRenderFeedback` writes per-candidate `metricReadbackMaxDimension`, `metricsReadbackDownsampled`, and `preFinishMetricsReadbackDownsampled`, plus aggregate `autoCandidateRenderedMetricReadback*` diagnostics. The Auto status panel shows a compact metric-readback line only when capping was active.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed, including metric-readback budget policy coverage; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; scoped `git diff --check` reported only line-ending warnings.
- Remaining work: true GPU texture memory profiling, downscaled/offscreen candidate render execution, fuller queue/admission policy, candidate thumbnail/gallery UI, user candidate picker/merge controls, sidecar stats bus, and richer progress diagnostics remain future Guide 03/10 work.
- Deferred-scope changes: capped metric readback moved from implicit large-RAW pressure risk to implemented diagnostic readback behavior. Full downscaled render execution, gallery, graph controls, user picker, and sidecar scheduler remain deferred.
- Next recommended starting point: continue Guide 03 in `src/Editor/EditorRenderWorker.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, and `src/Renderer/RenderPipeline.cpp` by profiling/limiting GPU texture residency for candidate renders or adding stronger candidate-family admission before render requests are built.

## 2026-06-13 Guide 03 Processing Handoff - Stale Snapshot Render Cancellation

- Status delta: Guide 03 remains `Partial`; this adds stale in-flight snapshot cancellation for background Develop feedback without claiming the full scheduled convergence engine, candidate gallery, user picker, sidecar stats bus, or interruptible GPU shader cancellation.
- Files changed: `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `SubmitRenderIfReady` now allows a dirty single-output render to submit a newer worker snapshot even when a previous render is pending, instead of waiting for stale candidate-feedback work to fully drain.
- Implemented: `EditorRenderWorker` now detects a pending newer generation through `ShouldAbortStaleSnapshot`; after the current unavoidable graph operation finishes, it skips stale Develop candidate renders and preview-like background work, avoids publishing superseded main/layer-stack shared textures, and moves on to the newest pending snapshot.
- Implemented: progress text now distinguishes replaced work with `Queued newer render...` / `Newer render queued; skipping stale feedback...`, so the existing non-blocking progress HUD reflects why heavy feedback stopped early.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; scoped `git diff --check` reported only line-ending warnings.
- Remaining work: a full background render queue, cancelable graph execution, candidate thumbnail/gallery UI, user candidate picker/merge controls, sidecar stats bus, richer scheduler telemetry, and deeper memory profiling remain future Guide 03/10 work.
- Deferred-scope changes: stale snapshot cancellation moved from implicit stability risk to implemented worker behavior; full background queue/gallery/scheduler remains deferred.
- Next recommended starting point: continue Guide 03 in `src/Editor/Internal/EditorModuleRendering.cpp` and `src/Editor/EditorRenderWorker.cpp` by adding stronger queue/admission policy around candidate-family suppression, or continue performance work by measuring texture/memory pressure in `RenderPipeline` stage caches under large RAW graphs.

## 2026-06-13 Guide 05 Processing Handoff - Refined Subject Map Diagnostics

- Status delta: Guide 05 remains `Partial`; this adds `SubjectRefinedMapV1`, a compact refined importance/confidence map over current user marks and solved subject intent without claiming true image-edge refinement, semantic/AI detection, graph controls, candidate gallery UI, or Manual/Auto handoff.
- Files changed: `src/Editor/NodeGraph/EditorNodeGraph.h`, `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`, `src/Editor/EditorModule.h`, `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/Editor/UI/EditorViewport.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `SubjectRefinedMapV1` now blends the existing `SubjectImportanceMapV1` cells with neighbor support and solved subject/readability/protection/mood axes. It records refined importance, confidence, readability, protection, mood-preservation, low-priority, and boundary-hint cells plus coverage/confidence/peak/boundary summaries.
- Implemented: `SubjectSceneIntentV1` now carries `refinedImportanceMap` plus scalar `refinedMap*` aliases. `WriteDevelopAutoCandidateSolveDiagnostics` mirrors them as top-level `autoSubjectSceneRefinedMap*` diagnostics, and `BuildDevelopAutoCandidateScoreComponents` carries the refined map/signals inside every candidate score-component record.
- Implemented: refined-map confidence now lightly biases subject-readable, subject-protection, and mood-preservation candidate scoring/dimensions. It remains a bias layered over existing clipping/noise/halo/mood safeguards, not a hard mask.
- Implemented: Auto UI exposes `Show Refined Map` and `Refined Opacity` controls, serializes/defaults them safely, keeps them visual-only for solver fingerprints, and shows a compact refined-map status line in the Auto status readout.
- Implemented: selected Develop viewport rendering draws the refined map under editable brush strokes/regions. It prefers the solved `autoSubjectSceneRefinedMap` when available and falls back to a current-mark derived map immediately after display toggles.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: true image-edge refinement of subject maps, edge visualization, richer diagnostic/graph UI, stroke point/history editing beyond whole-stroke controls, AI/semantic detection if explicitly scoped later, candidate gallery UI, and Manual/Auto subject-bias handoff.
- Deferred-scope changes: compact refined subject map diagnostics moved from deferred to implemented as `SubjectRefinedMapV1`; true edge-aware refinement and graph controls remain deferred.
- Next recommended starting point: continue Guide 05 with actual image-edge/boundary refinement over `SubjectRefinedMapV1` or start a Guide 10 diagnostic panel that explains interpreted/refined map influence without building the full graph-control system.

## 2026-06-13 Guide 05 Processing Handoff - Subject Importance Solve Notes

- Status delta: Guide 05 remains `Partial`; this adds compact solve-note diagnostics explaining how subject/importance guidance affected Auto candidate scoring without claiming edge-aware refinement, semantic/AI detection, graph controls, candidate gallery UI, or Manual/Auto handoff.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `SubjectImportanceSolveNotesV1` now writes bounded note objects with `id`, `text`, and `strength` for interpreted-map bias, user intent controls, automatic weak priors, readability/reveal pressure, protection pressure, mood-preservation pressure, low-priority pressure, and subject/scene axis tilt.
- Implemented: `ResolveDevelopSubjectSceneIntent` builds notes for both pending-evidence and normal solved subject intent states. `DevelopSubjectSceneIntentToJson` serializes `solveNotesVersion` and `solveNotes` inside `SubjectSceneIntentV1`.
- Implemented: `WriteDevelopAutoCandidateSolveDiagnostics` mirrors notes into top-level `autoSubjectSceneSolveNotesVersion`, `autoSubjectSceneSolveNotes`, `autoSubjectSceneSolveNoteCount`, and `autoSubjectScenePrimarySolveNote`. Candidate score components also carry the same note record.
- Implemented: Auto status shows up to two compact `Subject note:` lines near the existing Subject / Scene and Importance Map readouts.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; scoped `git diff --check` reported no whitespace errors, only line-ending warnings.
- Remaining work: edge-aware refinement of interpreted maps, true refined importance/confidence maps, edge visualization, richer user-facing diagnostic/graph UI, stroke point/history editing beyond whole-stroke controls, AI/semantic detection if explicitly scoped later, candidate gallery UI, and Manual/Auto subject-bias handoff.
- Deferred-scope changes: compact solve-note diagnostics moved from deferred to implemented as `SubjectImportanceSolveNotesV1`; edge-aware/refined visual maps and graph controls remain deferred.
- Next recommended starting point: continue Guide 05 with edge-aware refinement over `SubjectImportanceMapV1` or refined importance/confidence map visualization. Keep the solve-note contract additive and do not replace it with unversioned prose fields.

## 2026-06-13 Guide 05 Processing Handoff - Interpreted Map Viewport Diagnostic Overlay

- Status delta: Guide 05 remains `Partial`; this makes the existing compact `SubjectImportanceMapV1` visible as a selected-Develop viewport diagnostic overlay without claiming edge-aware refinement, semantic/AI detection, graph controls, candidate gallery UI, or Manual/Auto handoff.
- Files changed: `src/Editor/NodeGraph/EditorNodeGraph.h`, `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`, `src/Editor/EditorModule.h`, `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/Editor/UI/EditorViewport.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DevelopSubjectImportanceMap` now has visual-only `showInterpretedMapOverlay` and `interpretedMapOpacity` fields. Graph JSON round-trips them, old graphs default safely with the map overlay off, and validation covers serialization/default behavior.
- Implemented: `EditorModule::GetDevelopSubjectImportanceViewportState` now derives compact viewport map cells from `InterpretDevelopSubjectImportanceMap` when the diagnostic overlay is enabled. The viewport state carries grid dimensions and per-cell importance/reveal/protect/preserve-mood/low-priority weights.
- Implemented: Auto UI now exposes `Show Interpreted Map` and `Map Opacity` near the existing subject overlay controls. These controls are explicitly diagnostic/display-only and stay out of solver fingerprints; validation checks that toggling map/overlay display settings does not change the Auto candidate context fingerprint.
- Implemented: `EditorViewport` draws the interpreted 5x5 map under the existing editable strokes/regions in single-output preview. Cell color reflects the dominant interpreted channel, including low-priority/reduce marks, while user-authored marks remain visually on top.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; scoped `git diff --check` reported no whitespace errors, only line-ending warnings.
- Remaining work: edge-aware refinement of the interpreted map, true refined importance/confidence maps, edge visualization, stroke point/history editing beyond whole-stroke controls, graph-style Subject / Scene Intent Map controls, AI/semantic detection if explicitly scoped later, candidate gallery UI, and Manual/Auto subject-bias handoff.
- Deferred-scope changes: compact visual diagnostic map view moved from deferred to implemented for `SubjectImportanceMapV1`; edge-aware/refined visual maps and solve-note UI remain deferred.
- Next recommended starting point: continue Guide 05 either with edge-aware refinement over `SubjectImportanceMapV1` or with better solve-note diagnostics explaining how the visible map affected subject/readability/protection candidate scoring. Keep Finish Mask separate.

## 2026-06-13 Guide 05 Processing Handoff - Interpreted Subject Importance Map Contract

- Status delta: Guide 05 remains `Partial`; this adds a compact interpreted subject-importance map contract from existing regions/strokes without claiming edge-aware refinement, visual diagnostic maps, graph controls, candidate gallery UI, AI/semantic detection, or Manual/Auto handoff.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `SubjectImportanceMapV1` now interprets enabled `DevelopSubjectImportanceMap` soft regions and brush strokes into a bounded 5x5 solver grid with per-cell importance, reveal, protect, preserve-mood, and low-priority values. Reduce/ignore strokes remain explicit low-priority coverage instead of disappearing as absent importance.
- Implemented: `SubjectSceneIntentV1` now carries the nested importance map plus coverage, positive coverage, low-priority coverage, mode coverage, peak, confidence, center-bias, and edge-bias aliases. These values lightly bias subject/scene/readability/mood/protection intent and candidate score components.
- Implemented: integrated tone diagnostics now write `autoSubjectSceneImportanceMap*` and `autoRequestedSubjectImportanceMap*` aliases. The Auto status readout shows map status, coverage, peak, low-priority coverage, and center bias near the existing Subject / Scene status.
- Implemented: validation now checks that region marks, brush strokes, disabled strokes, and reduce/ignore strokes write the expected `SubjectImportanceMapV1` diagnostics and score-component signals.
- Validation: first build attempt compiled but link failed with `LNK1168` because `build\Stack.exe` process `36600` was holding the executable; stopped that workspace debug process and reran successfully. `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `git diff --check -- src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.
- Remaining work: edge-aware refinement of the interpreted map, visual diagnostic map/overlay views, stroke point/history editing beyond whole-stroke controls, graph-style Subject / Scene Intent Map controls, AI/semantic detection if explicitly scoped later, candidate gallery UI, and Manual/Auto subject-bias handoff.
- Deferred-scope changes: `Subject / scene intent solver substrate` and `Subject importance brush and soft overlay` remain `Partial`; compact interpreted map substrate is now implemented, while edge-aware refinement and visual diagnostics remain deferred.
- Next recommended starting point: continue Guide 05 with an edge-aware refinement layer over `SubjectImportanceMapV1` or a visual diagnostic map view that reads the existing map contract. Keep Finish Mask separate and do not claim semantic detection.

## 2026-06-13 Guide 05 Processing Handoff - Subject-Marked Rendered Metrics

- Status delta: Guide 05 remains `Partial`; this adds compact rendered metrics for user-marked subject regions and brush strokes without claiming edge-aware interpretation, visual diagnostic maps, graph controls, candidate gallery UI, AI/semantic detection, or Manual/Auto handoff.
- Files changed: `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/EditorModule.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DevelopCandidateRenderRequest` now carries a bounded `DevelopSubjectMetricSampling` copy of the active Develop node's subject-importance regions/strokes. The copy caps region/stroke/point counts so dense brush histories do not create unbounded candidate analysis cost.
- Implemented: `AnalyzeDevelopCandidatePixels` now samples marked regions/strokes while it analyzes rendered candidate pixels. `RenderMetricsV1` records subject-marked coverage, positive/reveal/protect/mood/low-priority coverage, marked mean luma, shadow/highlight/clipped fractions, contrast span, readability score, protection risk, mood-preservation score, and low-priority brightness pressure.
- Implemented: final and hidden pre-finish candidate metrics use the same subject sampling spec. Rendered metric JSON serialization/readback, duplicate distance, standalone rendered scoring, and selected-baseline relative comparison now consume the subject-marked fields conservatively.
- Implemented: validation covers positive/reveal marked-region metrics, low-priority/ignore rendered pressure, absence of marked metrics without a sampling spec, and rendered metric distance sensitivity to subject-marked changes.
- Validation: first build attempt compiled but link failed with `LNK1168` because an existing `build\Stack.exe` process was holding the executable; stopped that stale workspace process and reran successfully. `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: edge-aware interpreted importance maps, true refined importance maps, diagnostic map views, stroke point/history editing beyond whole-stroke controls, graph-style Subject / Scene Intent Map controls, AI/semantic detection if explicitly scoped later, candidate gallery UI, and Manual/Auto subject-bias handoff.
- Deferred-scope changes: `Subject / scene intent solver substrate` and `Subject importance brush and soft overlay` remain `Partial`; compact subject-marked rendered metrics are now implemented as sampled solver feedback, while edge-aware maps and visual diagnostics remain deferred.
- Next recommended starting point: continue Guide 05 with the edge-aware interpreted-map contract or diagnostic map view over `DevelopSubjectImportanceMap` plus rendered subject metrics. Keep Finish Mask separate and do not claim semantic detection.

## 2026-06-13 Guide 05 Processing Handoff - Subject Brush Stroke Management

- Status delta: Guide 05 remains `Partial`; this makes the existing viewport-painted subject brush strokes manageable in Auto UI without claiming edge-aware interpretation, edge-aware subject-region render maps/diagnostics, graph controls, candidate gallery UI, AI/semantic detection, or Manual/Auto handoff.
- Files changed: `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/Editor/EditorModule.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: Auto UI now lists existing brush strokes. Each stroke can be selected for viewport emphasis, enabled/disabled, deleted, switched between normal/reduce behavior, retuned by mode, strength, size, and soft edge, and selected strokes copy their settings back to the current brush tool so new painting can continue the same intent.
- Implemented: reduce/subtract-only brush strokes now contribute to `userGuidanceStrength` through low-priority guidance, so painted reduce intent records as `importanceBrush` guidance instead of being ignored when it has no positive importance strength.
- Implemented: validation now covers disabled strokes being ignored by requested/solved stroke counts and reduce strokes remaining active user guidance with `autoSubjectSceneImportanceIgnore` and score-component signals.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; scoped `git diff --check` reported no whitespace errors, only line-ending warnings.
- Remaining work: edge-aware interpreted importance maps, true refined importance maps, diagnostic map views, stroke point/history editing beyond whole-stroke controls, graph-style Subject / Scene Intent Map controls, AI/semantic detection if explicitly scoped later, candidate gallery UI, and Manual/Auto subject-bias handoff.
- Deferred-scope changes: `Subject importance brush and soft overlay` remains `Partial`; basic per-stroke management is now implemented, while edge-aware refinement, interpreted maps, and diagnostic maps remain deferred.
- Next recommended starting point: continue Guide 05 with either subject-region/stroke rendered metrics or the edge-aware interpreted-map contract. Continue using `DevelopSubjectImportanceMap` as the owned data model and do not reuse Finish Mask semantics for subject guidance.

## 2026-06-13 Guide 05 Processing Handoff - Viewport Subject Region Overlay

- Status delta: Guide 05 remains `Partial`; this turns the existing persisted subject-importance region model into a visible/editable single-output viewport overlay without claiming the full freehand brush, erase/reduce painting, edge-aware maps, AI/semantic detection, graph controls, candidate gallery UI, or Manual/Auto handoff.
- Files changed: `src/Editor/NodeGraph/EditorNodeGraph.h`, `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/Editor/EditorModule.h`, `src/Editor/EditorModule.cpp`, `src/Editor/UI/EditorViewport.h`, `src/Editor/UI/EditorViewport.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DevelopSubjectImportanceMap` now stores `activeRegionId`, graph JSON round-trips it, old graphs default safely, and normalization keeps the active id valid as regions are added or removed.
- Implemented: Auto UI exposes Show Overlay, Overlay Opacity, and active-region selection alongside the existing region controls. Visual overlay settings are treated as viewport state, while region geometry/mode/strength remain solver-facing edits.
- Implemented: `EditorModule` now exposes a bounded viewport API for the selected Develop node: read subject-region overlay state, set active region, and update region center/size from viewport gestures. Geometry edits record RawDevelop interaction, force Auto reanalysis when metadata is available, and mark the Develop render dirty.
- Implemented: `EditorViewport` now draws clipped soft elliptical overlays over the rendered single-output preview. Region color reflects mode; the active region is emphasized; disabled regions draw faintly. Users can click/select, drag to move, and drag near the ellipse edge to resize; Shift-resize keeps the region circular. The overlay stays out of static compare, color picking, tone targeting, and Auto Gain mask preview interactions.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; scoped `git diff --check` reported no whitespace errors, only line-ending warnings.
- Remaining work: freehand brush painting, erase/reduce paint workflow, brush-size/strength cursor tools, edge-aware interpreted maps, visual diagnostics/diagnostic map views, graph-style Subject / Scene Intent Map controls, AI/semantic detection if explicitly scoped later, candidate gallery UI, and Manual/Auto subject-bias handoff.
- Deferred-scope changes: `Subject importance brush and soft overlay` remains `Partial`; actual soft region overlay/direct editing now exists, while freehand brush/erase/edge-aware interpretation remain deferred.
- Next recommended starting point: continue Guide 05 by adding a true paint/erase brush over `DevelopSubjectImportanceMap` or by adding compact rendered feedback that measures how marked regions land after candidate renders. Keep Finish Mask separate and do not introduce a parallel subject mask model.

## 2026-06-13 Guide 05 Processing Handoff - Subject Importance Region Guidance

- Status delta: Guide 05 remains `Partial`; this adds persisted user-guided subject-importance region data and solver handoff without claiming the full soft viewport brush, freehand paint overlay, edge-aware maps, AI/semantic detection, candidate gallery UI, graph controls, or Manual/Auto handoff.
- Files changed: `src/Editor/NodeGraph/EditorNodeGraph.h`, `src/Editor/EditorModule.h`, `src/Editor/EditorModule.cpp`, `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `RawDevelopPayload` now owns `DevelopSubjectImportanceMap`, containing normalized soft region records with stable `DevelopSubjectImportanceMode` values: `Important`, `Reveal`, `Protect`, `PreserveMood`, and `Ignore`.
- Implemented: graph JSON now serializes/deserializes `developSubjectImportance`; old graphs default to disabled/no regions, and unknown region mode strings fall back safely to `Important`.
- Implemented: Auto UI now exposes a compact `Subject Importance` region-guidance section with enable/add/clear/delete controls and per-region mode, strength, center, size, and soft-edge sliders. Edits record RawDevelop interactions and force Auto reanalysis.
- Implemented: `SubjectSceneIntentV1`, Auto solve trigger hashes, candidate context/guidance fingerprints, candidate score components, top-level diagnostics, and the Auto status readout now include subject-importance region summary data. Changing region guidance changes the candidate context fingerprint and causes a meaningful re-solve.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed.
- Remaining work: freehand viewport brush painting, true soft overlay rendering, erase/reduce paint workflow, edge-aware interpreted maps, visual diagnostics, graph-style Subject / Scene Intent Map controls, AI/semantic detection if explicitly scoped later, candidate gallery UI, and Manual/Auto subject-bias handoff.
- Deferred-scope changes: `Subject / scene intent solver substrate` remains `Partial`; `Subject importance brush and soft overlay` moves from `Not Started` to `Partial` only for persisted region guidance and mode vocabulary. Freehand brush/overlay/edge-aware interpretation remain deferred.
- Next recommended starting point: continue Guide 05 in `src/Editor/Internal/EditorModuleRawUI.cpp` and `src/Editor/UI/EditorViewport.cpp` for actual viewport paint/overlay interaction, while keeping Finish Mask semantics separate and reusing `DevelopSubjectImportanceMap` instead of creating a parallel subject mask model.

## 2026-06-13 Guide 05 Processing Handoff - Subject Intent Candidate Probes

- Status delta: Guide 05 remains `Partial`; this turns the existing subject/scene intent axes into actual Auto candidate alternatives without claiming the soft brush, edge-aware maps, AI/semantic detection, candidate gallery UI, graph controls, or Manual/Auto handoff.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `BuildDevelopAutoCandidateSolve` can now generate `subjectReadableMids`, a subject-priority/readability Scene Prep probe that opens likely or user-marked important mids while keeping highlight, noise, and halo guardrails visible.
- Implemented: `BuildDevelopAutoCandidateSolve` can now generate `sceneMoodPreservation`, a scene-integrity/mood Scene Prep counter-probe that keeps low-key or silhouette intent from being forced into gray midtones.
- Implemented: the new Guide 05 probes participate in parameter scoring, mode-intent fit, `ParameterScoreComponentsV1` subject dimensions, rendered-continuation refine matching, duplicate preservation, render request diversity/priority, scene-prep stage classification, and stage-constrained candidate render payloads.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: user-guided importance brush, brush modes, paint storage, soft overlay, edge-aware interpreted maps, visual diagnostics, subject/scene graph controls, AI/semantic detection if explicitly scoped later, candidate gallery UI, and Manual/Auto subject-bias handoff.
- Deferred-scope changes: `Subject / scene intent solver substrate` remains `Partial` and now includes named candidate probes; `Subject importance brush and soft overlay` and `Automatic subject/importance detection` remain `Not Started`.
- Next recommended starting point: continue Guide 05 by adding a real user importance data model/paint storage and soft overlay, or by improving compact subject/scene evidence and rendered metrics before the brush. Do not start semantic/AI detection or graph controls before the brush/data model decision is made.

## 2026-06-13 Guide 05 Processing Handoff - User Subject / Scene Intent Controls

- Status delta: Guide 05 remains `Partial`; this moves the subject/scene foundation from automatic-only evidence to first user-directed Auto intent controls without claiming the soft brush, edge-aware maps, AI/semantic detection, graph controls, candidate gallery, or Manual/Auto handoff.
- Files changed: `src/Editor/NodeGraph/EditorNodeGraph.h`, `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`, `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DevelopAutoGuidance` now carries `subjectSceneBias` and `moodReadabilityBias` as neutral-default user intent axes. Missing old serialized graphs default to neutral, and normal graph JSON round-trips both fields.
- Implemented: Auto UI now exposes `Subject / Scene Intent` and `Mood / Readability` controls near Auto Strength, with concise tooltips and status/readout text that distinguishes user-guided intent controls from future brush maps.
- Implemented: Auto solve fingerprints, candidate context/guidance fingerprints, candidate guidance JSON, rendered-feedback guidance distance/readback, authored tone JSON guidance, candidate outcome learning records, and integrated tone diagnostics now carry the two axes so changing them causes a meaningful solve and later feedback does not lose the authored intent.
- Implemented: `SubjectSceneIntentV1` now records active user intent controls through `userGuidanceStatus = "intentControls"`, `userGuidanceActive = true`, non-automatic status, user bias values, and bounded score biasing for subject priority, scene integrity, readability, and mood preservation.
- Implemented: Auto UI actions that can change Develop Auto state now record a RawDevelop interaction for the rendered-feedback edit gate: Auto Mode / Intent, Auto Calibrate, Reset Auto, and committed Auto slider edits.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `git diff --check -- src/Editor/NodeGraph/EditorNodeGraph.h src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.
- Remaining work: user-guided importance brush, brush modes, paint storage, soft overlay, edge-aware interpreted maps, visual diagnostics, candidate-gallery subject alternatives, subject/scene graph controls, AI/semantic detection if ever scoped, and Manual/Auto subject-bias handoff.
- Deferred-scope changes: `Subject / scene intent solver substrate` stays `Partial` but now includes user intent axes; `Subject importance brush and soft overlay` and `Automatic subject/importance detection` remain `Not Started`.
- Next recommended starting point: continue Guide 05 either by adding a real user importance data model/paint storage and soft overlay, or by extending `ResolveDevelopSubjectSceneIntent` / `BuildDevelopAutoCandidateSolve` to generate clearer subject-priority candidate alternatives from the new axes. Do not start AI detection before the brush/data model decision is made.

## 2026-06-13 Guide 05 Processing Handoff - Subject / Scene Intent Foundation

- Status delta: Guide 05 moves from `In Progress` to `Partial`; this adds the first durable subject/scene intent substrate without claiming the user-guided brush, edge-aware overlays, AI/semantic detection, true subject maps, graph controls, candidate gallery, or Manual/Auto handoff work.
- Files changed: `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `RenderMetricsV1` now records compact subject/scene fields from rendered candidate pixels: `subjectCenterPrior`, `subjectReadabilityPressure`, `subjectProtectionPressure`, `subjectMoodPreservationPressure`, and `subjectImportanceConfidence`.
- Implemented: rendered metric distance, rendered metric JSON serialization/readback, and `DynamicRangeRegionEvidenceV1` now carry the subject/scene fields so follow-up solves can preserve candidates with meaningfully different subject/scene behavior.
- Implemented: `SubjectSceneIntentV1` resolves weak automatic subject/scene axes (`subjectSceneAxis`, `moodReadabilityAxis`) and bounded intent weights for subject priority, readability, protection, mood preservation, and confidence. It explicitly records `automaticOnly = true`, `userGuidanceStatus = "notAvailable"`, and brush status `deferred`.
- Implemented: candidate scoring and `ParameterScoreComponentsV1` now include conservative subject/scene dimensions and risks, so subject/scene evidence can bias selection without overriding clipping, noise, halo, range, or mood safeguards.
- Implemented: Auto diagnostics and the Develop Auto status panel show compact subject/scene confidence and mood/readability pressure while distinguishing this automatic evidence from future user-painted importance.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `git diff --check -- src/Editor/EditorRenderWorker.h src/Editor/EditorRenderWorker.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md docs/engineering/develop/DEVELOP_SOURCE_MAP.md docs/engineering/develop/DEVELOP_DECISIONS.md docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` reported no whitespace errors, only line-ending warnings.
- Remaining work: user-guided importance brush, brush modes, soft overlay, edge-aware interpreted maps, visual diagnostics, subject/scene graph controls, AI/semantic detection if ever scoped, and Manual/Auto bias handoff.
- Deferred-scope changes: `Subject / scene intent solver substrate` is now `Partial`; `Subject importance brush and soft overlay` and `Automatic subject/importance detection` remain not implemented.
- Next recommended starting point: continue Guide 05 in `src/Editor/EditorRenderWorker.cpp` around `AnalyzeDevelopCandidatePixels` for better compact evidence, or in `src/Editor/EditorModule.cpp` around `ResolveDevelopSubjectSceneIntent`, `BuildDevelopAutoCandidateSolve`, and `BuildDevelopAutoCandidateScoreComponents` for user guidance handoff. The next useful slice is likely a user-importance data model or graph-control substrate, not AI detection.

## 2026-06-13 Guide 04 Processing Handoff - Local Exposure Damage Profile

- Status delta: Guide 04 remains `Partial`; this adds stronger compact rendered evidence for local exposure failure modes without claiming true local-EV/noise/halo maps, visual overlays, subject-aware range priority, graph controls, clipped-data recovery, or a new local exposure renderer.
- Files changed: `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `RenderMetricsV1` now records `localExposureHighlightCrowding`, `localExposureShadowCrowding`, `localExposureHaloStress`, `localExposureFlatnessRisk`, and `localExposureDamageRisk` from rendered pixel/tile evidence.
- Implemented: rendered metric distance now includes the local exposure damage profile so near-duplicate clustering can preserve candidates with different local damage behavior even when global metrics are similar.
- Implemented: Auto-side rendered metric readback, `DynamicRangeRegionEvidenceV1`, `DynamicRangeStrategyV1`, top-level `autoDynamicRangeLocalExposure*` diagnostics, score-component signals/risks, and the Auto status readout now carry the profile.
- Implemented: `ResolveDevelopLocalExposureStrategy` uses the profile to focus highlight/shadow local correction while reducing aggressive redistribution when halo stress or aggregate local damage is high.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `git diff --check -- src/Editor/EditorRenderWorker.h src/Editor/EditorRenderWorker.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.
- Remaining work: true spatial clipping/highlight/noise/local-EV/halo maps, visual overlays, subject-aware local range priority, graph-style dynamic-range controls, richer local exposure scoring, and deeper Scene Prep/local exposure renderer work.
- Deferred-scope changes: `Advanced dynamic range/highlight/shadow strategy` remains `Partial`; `Spatial clipping/noise exposure maps and visual damage metrics` remains `Partial`; graph controls remain Guide 10-owned and `Not Started` as UI.
- Next recommended starting point: continue Guide 04 in `src/Editor/EditorRenderWorker.cpp` around `AnalyzeDevelopCandidatePixels` if adding more compact spatial evidence, or start Guide 05 subject-importance work before further interpreting "shadow/subject visibility" as true subject priority.

## 2026-06-13 Guide 04 Processing Handoff - Local Exposure Strategy Contract

- Status delta: Guide 04 remains `Partial`; this completes a real local-exposure strategy contract over existing Scene Prep without claiming true local-EV maps, visual overlays, subject-aware range priority, graph controls, clipped-data recovery, or a new local exposure renderer.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `ResolveDevelopLocalExposureStrategy` now has a shipped `LocalExposureStrategyV1` contract with range redistribution, highlight compression, shadow opening, noise guard, halo guard, texture guard, shadow EV budget, highlight EV budget, and strength target.
- Implemented: `DynamicRangeStrategyV1` writes the local exposure strategy as nested JSON plus top-level `autoDynamicRangeLocalExposure*` aliases, and Auto status shows a compact local exposure diagnostic line.
- Implemented: `ApplyDevelopAutoSolve` uses the selected local exposure strategy to author Scene Prep settings as coordinated guardrail moves across strength, local min/max EV bias, highlight protection, noise protection, shadow-lift limits, halo/gradient/edge safety, and texture sensitivity.
- Implemented: candidate render payloads inherit `LocalExposureStrategyV1` and apply it to Scene Prep probes so broad-highlight, local-range, halo-safe, shadow-readability, and shadow-floor candidates validate the same strategy the authored solve used.
- Implemented: validation now checks local exposure strategy diagnostics, authored Scene Prep handoff, and candidate render payload carry-through.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `git diff --check -- src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.
- Remaining work: true spatial clipping/highlight/noise/local-EV/halo maps, visual overlays, subject-aware local range priority, graph-style dynamic-range controls, richer local exposure scoring, and any deeper Scene Prep/local exposure renderer redesign.
- Deferred-scope changes: `Advanced dynamic range/highlight/shadow strategy` remains `Partial`; graph controls remain Guide 10-owned and `Not Started` as UI; spatial maps and overlays remain deferred.
- Next recommended starting point: continue Guide 04 in `src/Editor/EditorModule.cpp` around `ResolveDevelopLocalExposureStrategy`, `BuildDevelopAutoCandidateSolve`, and `BuildDevelopAutoCandidateScoreComponents`; for rendered evidence and probe behavior continue in `src/Editor/Internal/EditorModuleDevelopCandidateRenderPayload.cpp` around `ApplyDevelopGuidanceToCandidateRenderPayload` and in `src/Editor/EditorRenderWorker.cpp` around `AnalyzeDevelopCandidatePixels`.

## 2026-06-13 Guide 04 Processing Handoff - Dynamic Range Strategy Map Diagnostics

- Status delta: Guide 04 remains `Partial`; this completes the internal strategy-map diagnostic and solver-bias slice without claiming graph controls, visual maps, subject-aware priority, clipped-data recovery, or the full local-exposure strategy.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DynamicRangeStrategyV1` now carries a nested `DynamicRangeStrategyMapV1` with horizontal `highlightShadowAxis` and vertical `contrastRangeAxis`, plus derived `highlightPriority`, `shadowVisibility`, `naturalContrast`, and `visibleRange` weights.
- Implemented: the map is written as nested strategy JSON plus top-level `autoDynamicRangeStrategyMap*` aliases, and Auto status now shows the two map coordinates as diagnostics.
- Implemented: existing Guide 04 candidates now use the map conservatively: highlight-priority bias can help broad-highlight probes, shadow-visibility bias can help readable-shadow probes, visible-range bias can help maximum-range/local-range/flatter-editing probes, and natural-contrast bias can help contrast-preserving finish-tone probes.
- Implemented: `ParameterScoreComponentsV1` now forwards the strategy map and records `strategyHighlightFit`, `strategyShadowFit`, `strategyVisibleRangeFit`, and `strategyNaturalContrastFit` dimensions.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `git diff --check -- src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.
- Remaining work: true spatial clipping/highlight/noise/local-EV/halo maps, visual overlays, subject-aware range priority, graph-style user controls, richer local exposure scoring, and the full Guide 04 local-exposure strategy.
- Deferred-scope changes: `Advanced dynamic range/highlight/shadow strategy` remains `Partial`; graph controls remain Guide 10-owned and `Not Started` as UI.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeStrategy`, `ResolveDevelopDynamicRangeStrategyMap`, `BuildDevelopAutoCandidateSolve`, and `BuildDevelopAutoCandidateScoreComponents`. A good next Guide 04 slice is richer local-exposure scoring against these map coordinates, or the first diagnostic map/overlay data contract, still without building the Guide 10 graph UI.
- Latest Guide 03 processing increment: `AdaptiveRenderBudgetV1` now consumes `ConvergenceEvidenceV1` / `ConvergenceAdmissionV1` to narrow late, non-targeted continuation render passes from four to three focused candidate renders when the loop is waiting for fresh rendered metrics and admission is already tightened. This is a compact convergence-aware scheduler decision over the current worker path, not a candidate gallery, background queue, user picker, applied learning system, sidecar stats bus, or full scheduled convergence engine.
- Guides 07 through 10 are `Not Started` unless a later pass updates this tracker, the requirement checklist, validation notes, source map, and decisions/deferred-scope files together.
- Feature-level rows in `DEVELOP_DEFERRED_SCOPE.md` may be `Partial` when another guide created reusable substrate, such as Guide 03 JSON diagnostics that will later support Guide 10. That does not mean the owning guide is started until its guide table row and requirement checklist are updated.
- If a future prompt or older plan assumes a different guide status, verify code and this tracker first; preserve the current truth and document the mismatch before continuing.

Continuation handoff rule:

- Every numbered-guide pass must leave a dated handoff in this tracker: active guide, status delta, code/doc locations touched, validation run, what remains incomplete, deferred-scope changes, and the best next source/function/doc area for the following pass.
- Documentation-only passes must still leave an acceptance audit here and in `DEVELOP_DECISIONS.md`, and must explicitly say when no build or source-map update was required.
- Do not delete or flatten older partial-progress notes when adding a new pass. Append a newer dated increment so future sessions can reconstruct the sequence.

## 2026-06-13 Guide 05 Processing Handoff - Viewport Subject Brush Strokes

- Status delta: Guide 05 remains `Partial`; this turns the existing subject-importance brush substrate into persisted viewport-painted strokes without claiming edge-aware interpretation, semantic detection, diagnostic maps, graph controls, candidate gallery UI, or Manual/Auto handoff.
- Files changed: `src/Editor/NodeGraph/EditorNodeGraph.h`, `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/Editor/EditorModule.h`, `src/Editor/EditorModule.cpp`, `src/Editor/UI/EditorViewport.h`, `src/Editor/UI/EditorViewport.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DevelopSubjectImportanceMap` now persists freehand-style `DevelopSubjectImportanceStroke` records with stable mode, reduce/subtract flag, radius, feather, strength, and bounded normalized stroke points.
- Implemented: graph JSON round-trips brush settings and strokes, old graphs default safely to no strokes, and unknown brush/stroke mode strings fall back to `Important`.
- Implemented: Auto UI exposes Brush Edit, Reduce, Brush Mode, Brush Size, Brush Strength, Brush Soft Edge, and Clear Brush while preserving existing region controls.
- Implemented: `EditorModule` exposes viewport brush begin/append/end APIs. Brush drags record the RawDevelop interaction gate during painting, append bounded points, and run one Auto reanalysis/render dirty mark when the stroke ends.
- Implemented: `EditorViewport` draws soft stroke overlays and a brush cursor in single-output preview. Brush edit mode takes precedence over region hit testing; existing region select/move/resize behavior remains available when brush edit is off.
- Implemented: strokes now feed `SummarizeDevelopSubjectImportance`, `SubjectSceneIntentV1`, Auto trigger and candidate fingerprints, requested/top-level diagnostics, and score-component signals. Visual-only overlay visibility/opacity remains out of solver hashes.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: edge-aware interpreted importance maps, erase/reduce stroke management beyond subtractive strokes and clear-all, per-stroke edit/delete UI, diagnostic map views, graph-style Subject / Scene Intent Map controls, AI/semantic detection if explicitly scoped later, candidate gallery UI, and Manual/Auto subject-bias handoff.
- Deferred-scope changes: `Subject importance brush and soft overlay` remains `Partial`; freehand-style stroke painting now exists, while edge-aware interpretation and richer diagnostics remain deferred.
- Next recommended starting point: continue Guide 05 either by adding per-stroke management and compact subject-marked rendered feedback, or by designing the edge-aware interpreted map contract. Reuse `DevelopSubjectImportanceMap` and do not reuse Finish Mask as the subject brush.

## 2026-06-13 Guide 04 Processing Handoff - Local EV Conflict Evidence

- Status delta: Guide 04 remains `Partial`; this completes the compact local-EV conflict evidence slice started after meaningful-highlight structure evidence, without claiming true local-EV maps, visual overlays, graph controls, subject-aware range priority, or clipped-data recovery.
- Files changed: `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `RenderMetricsV1` now records `localEvSpreadStops` and `localEvConflict` from rendered candidate pixels. The signal uses the existing compact 3x3 local luma evidence, mixed dark/bright tile coverage, local luma spread, local damage risk, edge contrast, and halo risk.
- Implemented: rendered metric JSON/readback, metric distance, `DynamicRangeRegionEvidenceV1`, `DynamicRangeStrategyV1`, top-level tone diagnostics, score-component signals/risks, and Auto status readouts now carry local-EV spread/conflict evidence.
- Implemented: `localRangeGuard`, `haloSafeLocalRange`, and `maximumRange` scoring/generation can now respond to local-EV conflict. `localRangeGuard` gets a clearer reason to test controlled Scene Prep redistribution when the rendered candidate shows bright/dark local conflict, rather than simply pushing broad global range.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `git diff --check -- src/Editor/EditorRenderWorker.h src/Editor/EditorRenderWorker.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.
- Remaining work: true local-EV/spatial clipping/highlight/noise/halo maps, visual overlays, subject-aware local range priority, richer local exposure scoring, graph-style dynamic-range controls, and the full Guide 04 local-exposure strategy.
- Deferred-scope changes: `Advanced dynamic range/highlight/shadow strategy` remains `Partial`; `Spatial clipping/noise exposure maps and visual damage metrics` remains `Partial`; graph controls, subject brush, denoise redesign, View Transform changes, and clipped-data reconstruction remain deferred.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeStrategy`, `BuildDevelopAutoCandidateSolve`, and `BuildDevelopAutoCandidateScoreComponents`; for rendered-pixel evidence, continue in `src/Editor/EditorRenderWorker.cpp` around `AnalyzeDevelopCandidatePixels`. The next useful Guide 04 slice is either richer local-exposure scoring from this evidence or visual/diagnostic substrate for future map-style controls, not another scalar-only rename.

## 2026-06-13 Guide 04 Processing Handoff - Meaningful Highlight Structure Evidence

- Status delta: Guide 04 remains `Partial`; this adds compact rendered evidence for broad/structured highlight regions without claiming true highlight maps, subject importance, graph controls, or clipped-data recovery.
- Files changed: `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `RenderMetricsV1` now records `highlightTileCoverage`, `highlightStructureScore`, and `meaningfulHighlightPressure` from rendered candidate pixels. This gives Auto a compact signal for broad structured highlights, so tiny glints can be treated differently from meaningful highlight regions.
- Implemented: rendered metric JSON/readback, metric distance, rendered scoring, relative regression checks, damage classification, and rendered refine intent now consume meaningful-highlight pressure. The new refine path can request highlight protection from structured broad-highlight pressure before treating the area as disposable specular clipping.
- Implemented: `DynamicRangeRegionEvidenceV1` and `DynamicRangeStrategyV1` forward meaningful-highlight pressure into highlight importance, broad-highlight guard need, specular tolerance gating, local highlight pressure, score components, and diagnostics. Top-level diagnostics include `autoDynamicRangeMeaningfulHighlightPressure`, `autoDynamicRangeHighlightTileCoverage`, and `autoDynamicRangeHighlightStructureScore`.
- Implemented: Auto status regional evidence now shows compact `meaning` pressure beside highlight-gray, shadow, and local-conflict evidence.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `git diff --check -- src/Editor/EditorRenderWorker.h src/Editor/EditorRenderWorker.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.
- Validation note: the synthetic rendered fixtures now separate highlight-gray evidence, meaningful broad-highlight structure evidence, and local spatial-risk evidence so one smarter refine reason does not mask another test branch.
- Remaining work: true spatial highlight/importance maps, subject-aware highlight priority, richer local exposure scoring, visual overlays, graph-style dynamic-range controls, and the full Guide 04 local-exposure strategy.
- Deferred-scope changes: `Advanced dynamic range/highlight/shadow strategy` remains `Partial`; `Spatial clipping/noise exposure maps and visual damage metrics` remains `Partial`; graph controls, subject brush, denoise redesign, View Transform changes, and clipped-data reconstruction remain deferred.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeStrategy`, `BuildDevelopDynamicRangeRegionEvidenceFromMetrics`, and `BuildDevelopAutoCandidateScoreComponents`; for rendered-pixel evidence, continue in `src/Editor/EditorRenderWorker.cpp` around `AnalyzeDevelopCandidatePixels`. The next useful Guide 04 slice is richer local-EV/halo/highlight-importance evidence, not another renamed slider.

## 2026-06-13 Guide 04 Processing Handoff - Rendered Highlight Grayness Evidence

- Status delta: Guide 04 remains `Partial`; this adds explicit rendered highlight-brightness evidence without claiming true highlight maps, clipped-data recovery, graph controls, or the Guide 08 tone/shoulder redesign.
- Files changed: `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `RenderMetricsV1` now records `highlightBandFraction`, `highlightMeanLuma`, `highlightLowSaturationFraction`, and `highlightGrayRisk` from rendered candidate pixels. The risk only becomes meaningful when a broad rendered highlight band exists, so low-key/dark images are not treated as failed highlight protection just because they are dark.
- Implemented: rendered metric JSON/readback, rendered candidate distance, rendered scoring, relative regression checks, damage classification, and rendered refine intent now understand highlight-gray risk. Candidates that make broad highlights flatter/grayer lose score; severe non-flat-intent gray-highlight results can be rejected; gray-highlight feedback asks for `addContrast` / highlight separation rather than another blind global exposure move.
- Implemented: `DynamicRangeRegionEvidenceV1` and `DynamicRangeStrategyV1` forward highlight-gray evidence into brightness-hierarchy risk, natural-contrast guard need, bright-highlight rolloff need, and luminous-highlight anchor need. Top-level diagnostics include `autoDynamicRangeHighlightGrayRisk`, `autoDynamicRangeHighlightBandFraction`, `autoDynamicRangeHighlightMeanLuma`, and `autoDynamicRangeHighlightLowSaturationFraction`.
- Implemented: Auto status regional evidence now shows compact `gray` evidence next to highlight, shadow, and local-conflict pressure.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `git diff --check -- src/Editor/EditorRenderWorker.h src/Editor/EditorRenderWorker.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.
- Remaining work: true spatial highlight/importance maps, richer brightness-hierarchy maps, subject-aware highlight priority, visual overlays, graph-style dynamic-range controls, and the full Guide 08 finish-tone shoulder/toe redesign.
- Deferred-scope changes: `Advanced dynamic range/highlight/shadow strategy` remains `Partial`; `Full finish-tone strategy and mode-specific final tone redesign` remains `Not Started`. This pass does not add a candidate gallery, graph controls, subject brush, denoise redesign, or clipped-data reconstruction.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeStrategy`, `BuildDevelopDynamicRangeRegionEvidenceFromMetrics`, and `BuildDevelopAutoCandidateScoreComponents`; for rendered-pixel evidence, continue in `src/Editor/EditorRenderWorker.cpp` around `AnalyzeDevelopCandidatePixels`. The next Guide 04 slice should probably add richer spatial highlight-importance/local-EV evidence rather than another scalar-only finish-tone candidate.

## 2026-06-13 Guide 04 Processing Handoff - Luminous Highlight Anchor

- Status delta: Guide 04 remains `Partial`; this adds a finish-tone highlight-brightness branch without claiming true highlight maps, clipped-data recovery, graph controls, or the full Guide 08 tone redesign.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DynamicRangeStrategyV1` now writes `highlightBrightnessAnchorNeed` / `autoDynamicRangeHighlightBrightnessAnchorNeed` from highlight importance, broad-highlight pressure, brightness-hierarchy risk, natural-contrast risk, bright-rolloff need, mode intent, and clipping/specular/halo safety.
- Implemented: Auto can generate `luminousHighlightAnchor` when protected broad highlights may flatten toward gray. It is scored as a Guide 04 finish-tone candidate with a `luminousHighlightAnchor` score dimension and `highlightBrightnessSignal` diagnostics.
- Implemented: `luminousHighlightAnchor` survives duplicate clustering, participates in finish-tone scheduling plus `protectHighlights` and `addContrast` rendered-refine relevance, and renders as a finish-tone constrained probe with RAW/global and Scene Prep frozen.
- Implemented: the render payload uses the existing integrated ToneCurve guidance path to raise highlight character and contrast while slightly lowering dynamic-range pressure, so it tests highlight brightness feeling without claiming to recover clipped data.
- Implemented: Auto status now shows compact `lum` pressure beside `broad`, `read`, `halo`, `sep`, `spec`, and `floor` range-probe values.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `git diff --check -- src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings.
- Remaining work: true spatial clipping/highlight-importance maps, brightness-hierarchy maps, subject-aware highlight priority, richer local exposure scoring, visual overlays, graph-style dynamic-range controls, and the full Guide 08 tone/shoulder redesign.
- Deferred-scope changes: `Advanced dynamic range/highlight/shadow strategy` remains `Partial`; `Full finish-tone strategy and mode-specific final tone redesign` remains `Not Started`. No candidate gallery, graph controls, subject brush, denoise redesign, or clipped-data reconstruction is claimed.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeStrategy`, `BuildDevelopAutoCandidateSolve`, and `BuildDevelopAutoCandidateScoreComponents`; likely next Guide 04 slice is a stronger rendered brightness-hierarchy or highlight-importance metric rather than another scalar-only finish-tone probe.

## 2026-06-13 Guide 04 Processing Handoff - Halo-Safe Local Range

- Status delta: Guide 04 remains `Partial`; this adds a local anti-halo Scene Prep safety branch without claiming true halo maps, visual overlays, graph controls, or the full local-exposure strategy.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DynamicRangeStrategyV1` now writes `localHaloGuardNeed` / `autoDynamicRangeLocalHaloGuardNeed` from compact rendered halo/local-range evidence, range pressure, and mode intent.
- Implemented: Auto can generate `haloSafeLocalRange` when rendered regional evidence warns about edge glow, halo risk, or unsafe local exposure pressure. It is scored as a Guide 04 Scene Prep candidate with `localHaloSafety` diagnostics.
- Implemented: `haloSafeLocalRange` survives duplicate clustering, participates in Scene Prep scheduling and highlight/shadow local-refine relevance, and renders as a Scene Prep constrained probe with RAW/global settings and finish-tone intent frozen.
- Implemented: the render payload backs off local max-EV pressure while raising halo guard, smooth-gradient protection, edge awareness, and texture sensitivity within existing Scene Prep settings. Saturated guard values are treated as already at the safety cap in validation.
- Implemented: Auto status now shows compact `halo` pressure beside `broad`, `read`, `sep`, `spec`, and `floor` range-probe values.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Validation note: the Auto solve validation fixture was adjusted so the rendered no-improvement-trend case is not preempted by the earlier stable-metrics stop; this keeps both convergence branches covered.
- Remaining work: true spatial clipping/highlight/noise/local-EV/halo maps, visual overlays, subject-aware local range priority, richer local exposure scoring, graph-style dynamic-range controls, and the full Guide 04 local-exposure strategy.
- Deferred-scope changes: `Advanced dynamic range/highlight/shadow strategy` remains `Partial`; no Guide 05 subject brush, Guide 07 denoise redesign, Guide 08 finish-tone redesign, or Guide 10 graph controls are claimed.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeStrategy`, `BuildDevelopAutoCandidateSolve`, and `BuildDevelopAutoCandidateScoreComponents`; for render behavior, continue in `src/Editor/Internal/EditorModuleDevelopCandidateRenderPayload.cpp` around `ApplyDevelopGuidanceToCandidateRenderPayload` and stage-constrained Scene Prep probes.

## 2026-06-13 Guide 04 Processing Handoff - Natural Contrast Guard

- Status delta: Guide 04 remains `Partial`; this adds a finish-tone brightness-hierarchy/flat-gray repair branch without claiming the full Guide 08 tone redesign or Guide 10 graph controls.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DynamicRangeStrategyV1` now writes `naturalContrastGuardNeed` / `autoDynamicRangeNaturalContrastGuardNeed` from brightness-hierarchy, flat-gray, texture, range, and rendered regional evidence.
- Implemented: Auto can generate a `naturalContrastGuard` candidate when range compression or flat-gray risk threatens believable lighting hierarchy. It is scored as a Guide 04 finish-tone candidate with a `naturalContrastGuard` score dimension.
- Implemented: `naturalContrastGuard` survives duplicate clustering as a distinct Guide 04 branch, participates in finish-tone scheduling and `addContrast` rendered-refine relevance, and is rendered as a finish-tone constrained probe with RAW/global and Scene Prep frozen.
- Implemented: Auto status shows a compact `sep` value beside `broad`, `read`, `spec`, and `floor` range-probe values.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: true spatial brightness-hierarchy maps, subject-aware importance, richer local exposure scoring, visual overlays, graph-style dynamic-range controls, and the full Guide 04 local-exposure strategy.
- Deferred-scope changes: `Advanced dynamic range/highlights/shadows strategy` remains `Partial`; no Guide 08 tone redesign, Guide 10 graph controls, candidate gallery, or subject brush are claimed.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeStrategy`, `BuildDevelopAutoCandidateSolve`, and `BuildDevelopAutoCandidateScoreComponents`; for rendered behavior, continue in `src/Editor/Internal/EditorModuleRendering.cpp` around finish-tone probe scheduling and rendered `addContrast` relevance.

## 2026-06-13 Guide 04 Processing Handoff - Shadow Readability Lift

- Status delta: Guide 04 remains `Partial`; this adds the positive readable-shadow Scene Prep branch without claiming subject-aware shadow maps, user-brushed importance, or a denoise redesign.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DynamicRangeStrategyV1` now writes `shadowReadabilityLiftNeed` / `autoDynamicRangeShadowReadabilityLiftNeed` and can name the strategy `Shadow Readability Lift` when shadow evidence is clean/readable enough to test local opening.
- Implemented: Auto can generate a `shadowReadabilityLift` candidate from shadow rescue/readability evidence with low enough noise/shadow-floor risk. It is scored as a Guide 04 Scene Prep candidate with a `shadowReadabilityLift` score dimension.
- Implemented: `shadowReadabilityLift` survives duplicate clustering, participates in Scene Prep scheduling and `openShadows` rendered-refine relevance, and is rendered as a Scene Prep constrained probe with RAW/global settings frozen.
- Implemented: the render payload raises local shadow/midtone opening, keeps noise/halo guardrails active, lowers the shadow-lift limit bias enough to test actual readability, and records `autoCandidateScenePrepProbe = "shadowReadabilityLift"` for diagnostics.
- Implemented: Auto status shows a compact `read` value beside `broad`, `spec`, and `floor` range-probe values.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: true spatial shadow/noise/importance maps, subject-aware readable-shadow classification, richer local exposure scoring, visual overlays, graph-style dynamic-range controls, and the full Guide 04 local-exposure strategy.
- Deferred-scope changes: `Advanced dynamic range/highlights/shadows strategy` remains `Partial`; no Guide 05 subject brush, Guide 07 denoise redesign, or Guide 10 graph controls are claimed.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeStrategy`, `BuildDevelopAutoCandidateSolve`, and `BuildDevelopAutoCandidateScoreComponents`; for rendered behavior, continue in `src/Editor/Internal/EditorModuleRendering.cpp` around Scene Prep probe payload mapping and rendered `openShadows` refine relevance.

## 2026-06-13 Guide 04 Processing Handoff - Broad Highlight Guard

- Status delta: Guide 04 remains `Partial`; this adds a real broad-highlight Scene Prep decision branch without claiming true clipping maps, highlight-importance maps, or clipped-data reconstruction.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DynamicRangeStrategyV1` now writes `broadHighlightGuardNeed` and can name the strategy `Broad Highlight Guard` when broad meaningful highlight pressure should be handled separately from tiny glints.
- Implemented: Auto can generate a `broadHighlightGuard` candidate from high highlight pressure, clipping pressure, HDR pressure, or compact rendered broad/local highlight evidence. It is scored as a Guide 04 Scene Prep candidate with `broadHighlightControl` score diagnostics.
- Implemented: `broadHighlightGuard` survives duplicate clustering, participates in Scene Prep scheduling and highlight-protection rendered-refine relevance, and is rendered as a Scene Prep constrained probe with RAW/global settings frozen.
- Implemented: the render payload lowers local highlight min-EV shaping, keeps highlight protection at least as strong as the base solve, adds halo/smooth-gradient protection where room remains, and records `autoCandidateScenePrepProbe = "broadHighlightGuard"` for diagnostics.
- Implemented: Auto status shows a compact `broad` value beside `spec` and `floor` range-probe values.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: true spatial clipping/highlight-importance maps, subject-aware highlight priority, richer local exposure scoring, visual overlays, graph-style dynamic-range controls, and the full Guide 04 local-exposure strategy.
- Deferred-scope changes: `Advanced dynamic range/highlights/shadows strategy` remains `Partial`; no Guide 08 tone redesign, graph controls, or clipped-data reconstruction is claimed.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeStrategy`, `BuildDevelopAutoCandidateSolve`, and `BuildDevelopAutoCandidateScoreComponents`; for rendered behavior, continue in `src/Editor/Internal/EditorModuleRendering.cpp` around Scene Prep probe payload mapping.

## 2026-06-13 Guide 04 Processing Handoff - Specular Highlight Tolerance

- Status delta: Guide 04 remains `Partial`; this adds a real tiny-specular highlight decision branch without claiming true clipping maps or full clipped-data handling.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DynamicRangeStrategyV1` now writes `specularHighlightToleranceNeed` and can name the strategy `Specular Highlight Tolerance` when the current evidence looks like tiny point-source clipping rather than broad meaningful highlight failure.
- Implemented: Auto can generate a `specularHighlightTolerance` candidate for Natural Finished, Bright Natural, and Punchy / High Contrast leaning cases. The candidate is a finish-tone probe that raises highlight character/contrast and lowers highlight guard/range pressure slightly so tiny glints can stay luminous without changing RAW placement.
- Implemented: `specularHighlightTolerance` is classified as `finishTone`, survives duplicate clustering as a distinct Guide 04 exception path, participates in highlight-protection rendered-refine relevance, and is rendered with RAW/global and Scene Prep frozen.
- Implemented: Auto status shows a compact `spec` value beside highlight/shadow/noise/floor range strategy values.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: true spatial clipping/highlight-importance maps, subject-aware highlight priority, richer local exposure scoring, visual overlays, graph-style dynamic-range controls, and the full Guide 04 local-exposure strategy.
- Deferred-scope changes: `Advanced dynamic range/highlight/shadow strategy` remains `Partial`; no Guide 08 tone redesign, graph controls, or clipped-data reconstruction is claimed.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeStrategy`, `BuildDevelopAutoCandidateSolve`, and `BuildDevelopAutoCandidateScoreComponents`; for rendered behavior, continue in `src/Editor/Internal/EditorModuleDevelopCandidateShared.h/.cpp` around finish-tone probe classification and `src/Editor/Internal/EditorModuleDevelopCandidateRenderPayload.cpp` around `ApplyDevelopGuidanceToCandidateRenderPayload`.

## 2026-06-13 Guide 04 Processing Handoff - Shadow Noise Floor

- Status delta: Guide 04 remains `Partial`; this adds a real shadow/noise dynamic-range decision branch without claiming the full local-exposure or denoise redesign.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DynamicRangeStrategyV1` now writes `shadowNoiseFloorNeed`, can name the strategy `Shadow Noise Floor`, and explains when Auto is testing whether noisy or low-value dark regions should stay darker instead of being lifted into gray noise.
- Implemented: Auto can generate a `shadowNoiseFloor` candidate from high noise, shadow-noise lift pressure, dark-scene pressure, or rendered regional shadow evidence. It is scored as a Guide 04 Scene Prep candidate, with mode intent pushing back in Bright Natural, Flat Editing Base, and Maximum Range / Detail.
- Implemented: `shadowNoiseFloor` survives duplicate clustering, participates in continuation/refine relevance for Scene Prep/open-shadows/clean-shadows cases, and is rendered as a Scene Prep constrained probe with RAW/global settings frozen.
- Implemented: the render payload lowers Scene Prep shadow-opening pressure, raises noise protection and shadow-lift limits, and records `autoCandidateScenePrepProbe = "shadowNoiseFloor"` for diagnostics.
- Implemented: Auto status shows a compact `floor` value beside highlight/shadow/noise range strategy values.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: true spatial clipping/highlight/noise/local-EV/halo maps, subject-aware shadow importance, richer shadow signal/noise classification, visual overlays, graph-style dynamic-range controls, and the full Guide 04 local-exposure strategy.
- Deferred-scope changes: `Advanced dynamic range/highlight/shadow strategy` remains `Partial`; no Guide 07 denoise redesign is claimed.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeStrategy`, `BuildDevelopAutoCandidateSolve`, and `BuildDevelopAutoCandidateScoreComponents`; for render behavior, continue in `src/Editor/Internal/EditorModuleDevelopCandidateRenderPayload.cpp` around `ApplyDevelopGuidanceToCandidateRenderPayload`.

## 2026-06-12 Guide 04 Processing Handoff - Regional Evidence and Local Range Guard

- Status delta: Guide 04 remains `Partial`; this extends the compact dynamic-range strategy with rendered regional evidence and one local range guard candidate without claiming the full Guide 04 local-exposure system.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DynamicRangeRegionEvidenceV1` is resolved from existing rendered candidate metrics, including compact 3x3 local highlight/shadow/flat-gray/halo/contrast pressure and local damage-risk summaries. The evidence is written into integrated tone JSON and summarized in the Auto status panel.
- Implemented: Auto can now generate `localRangeGuard` when regional highlight/shadow/halo pressure suggests that Scene Prep should test a more locally guarded range shape. Duplicate clustering preserves this candidate when it carries distinct local-range intent.
- Implemented: `localRangeGuard` candidate renders are constrained to the Scene Prep stage: RAW/global placement and finish-tone intent remain frozen, while scene-prep local range/highlight/halo biases get a small conservative nudge.
- Implemented: rendered feedback can consume metrics from the immediate previous solve fingerprint when regional evidence legitimately changes the next preliminary fingerprint before feedback is applied; repeated same rendered adoption now stops with `renderedAdoptionNoFurtherGain`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining work: true spatial clipping/highlight/noise/local-EV/halo maps, visual overlays, subject-aware highlight/shadow priority, richer local exposure scoring, graph-style dynamic-range controls, and the full Guide 04 local-exposure strategy.
- Deferred-scope changes: `Advanced dynamic range/highlight/shadow strategy` remains `Partial`; spatial maps/overlays and graph controls remain deferred to later Guide 04/10 work, and the full Guide 08 finish-tone redesign remains `Not Started`.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeRegionEvidence`, `BuildDevelopDynamicRangeRegionEvidenceFromMetrics`, `BuildDevelopAutoCandidateSolve`, and `ApplyRenderedCandidateFeedbackToSolve`; for payload constraints, continue in `src/Editor/Internal/EditorModuleDevelopCandidateRenderPayload.cpp` around `ApplyDevelopGuidanceToCandidateRenderPayload`.

## 2026-06-12 Guide 04 Processing Handoff - Dynamic Range Strategy and Bright Highlight Rolloff

- Status delta: Guide 04 moved from `Not Started` to `Partial`; this starts the dynamic-range/highlight/shadow strategy work without claiming the full Guide 04 local-exposure system.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `DynamicRangeStrategyV1` classifies the current Auto solve's range strategy using highlight importance, shadow readability, noise constraint, range compression, brightness-hierarchy risk, bright-highlight rolloff need, and a small-specular-clipping allowance.
- Implemented: integrated tone JSON now stores `autoDynamicRangeStrategy*` diagnostics, and the Auto status panel shows the selected range strategy label plus compact highlight/shadow/noise evidence.
- Implemented: Auto can generate a `brightHighlightRolloff` finish-tone candidate when highlight protection risks making bright areas feel dull or gray. The candidate adjusts downstream tone/highlight character while preserving RAW and Scene Prep during candidate renders, and its copy-payload diagnostics mark it as a finish-tone probe.
- Implemented: candidate score components now include a `brightnessHierarchy` dimension so highlight rolloff candidates can be scored and diagnosed without pretending clipped data can be recovered.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `git diff --check -- src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md` reported no whitespace errors, only line-ending warnings.
- Remaining work: true clipping/highlight/noise/local-EV/halo maps, visual overlays, subject-aware highlight/shadow priority, richer local exposure scoring, graph-style dynamic-range controls, and the full Guide 04 local-exposure strategy.
- Deferred-scope changes: `Advanced dynamic range/highlight/shadow strategy` is now `Partial`; spatial maps/overlays and graph controls remain deferred to later Guide 04/10 work, and the full Guide 08 finish-tone redesign remains `Not Started`.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ResolveDevelopDynamicRangeStrategy`, `BuildDevelopAutoCandidateSolve`, and `BuildDevelopAutoCandidateScoreComponents`; for rendered validation/payload constraints, continue in `src/Editor/Internal/EditorModuleDevelopCandidateShared.h/.cpp` around `IsFinishToneProbeCandidateIdForRenderRequest` and `src/Editor/Internal/EditorModuleDevelopCandidateRenderPayload.cpp` around `ApplyDevelopGuidanceToCandidateRenderPayload`.

## 2026-06-12 Non-Guide Stability Handoff - Develop Render Memory, Budget, and Progress

- Status delta: no numbered guide status changed; this was a Develop render stability/performance pass to reduce crash/freeze risk during heavy RAW rendering.
- Files changed: `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/EditorModule.h`, `src/Editor/EditorModule.cpp`, `src/Utils/ImGuiExtras.h`, `src/Utils/ImGuiExtras.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Implemented: `EditorRenderWorker` now tracks approximate render progress across main output, composite outputs, Develop rendered-feedback probes, and preview renders.
- Implemented: the editor shows a non-blocking render progress HUD instead of using the blocking busy overlay for ordinary background rendering.
- Implemented: stale completed worker results are drained and their shared output textures are released immediately before a newer completed result is queued, avoiding a temporary GPU texture retention spike.
- Implemented: worker shutdown drains any unconsumed completed render result while the worker GL context is still current.
- Implemented: Develop candidate rendered-feedback request budgets are now source-size-aware. Large RAW sources keep the main viewport render intact but narrow diagnostic candidate-render breadth to reduce GPU/CPU pressure.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed.
- Remaining risk: the progress HUD is approximate because the current render worker does not expose per-shader or per-node GPU completion. Manual UI QA should still drag Auto Develop controls on very large RAWs and confirm candidate feedback resumes after edits settle without long freezes.
- Deferred-scope changes: none to guide status. This does not implement a candidate gallery, graph controls, a new solver, a new View Transform, final-pixel candidate blending, or the full Guide 03 scheduler.
- Next recommended starting point: if heavy-render freezes persist, inspect `EditorRenderWorker::RenderSnapshot`, `BuildDevelopCandidateRenderRequests`, and the RawDevelop branch in `RenderPipeline::ExecuteGraph`, with special attention to candidate render counts, source dimensions, shared texture handoff, and raw-base/pre-finish cache pressure.

## 2026-06-12 Non-Guide Bugfix Handoff - Editor Tab Crash and Startup Thumbnail Responsiveness

- Status delta: no numbered guide status changed; this was an editor startup/tab stability fix before continuing Develop guide work.
- Files changed: `src/App/AppShell.cpp`, `src/App/AppShell.h`, `src/Library/LibraryManager.cpp`, `src/Library/LibraryManager.h`, `src/Library/ProjectData.h`, `src/Editor/EditorModule.cpp`, `src/Editor/EditorModule.h`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Implemented: the app no longer calls `LibraryManager::UploadLibraryTextures()` before the main window is shown. Runtime uploads are budgeted per frame, and full-size asset thumbnail decoding only runs after the main window has been visible and the Library tab is active.
- Implemented: asset thumbnail generation now records a runtime `thumbnailLoadAttempted` flag and skips synchronous thumbnail decoding for oversized asset files, allowing cards to use the existing placeholder instead of repeatedly blocking the UI thread.
- Implemented: `EditorModule::GetNodeGraph`, its const overload, `IsGraphOutputConnected`, and `ClearCompositeSelection` are out-of-line definitions compiled in `EditorModule.cpp`. This prevents cross-translation-unit object-layout drift from making sidebar/node graph UI read the wrong `m_NodeGraph` offset.
- Diagnosis: Editor-tab reproduction showed `EditorModule.cpp` saw `m_NodeGraph` at one address with zero nodes, while `EditorSidebar.cpp`'s inline accessor saw a graph address 48 bytes earlier with an impossible node count. Moving the accessor out of the header restored a single canonical layout.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; clean launch/click smoke reached the `Stack` window, clicked the Editor tab, and stayed responsive for 20 seconds with no stderr and no diagnostic trace file; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed.
- Remaining risk: this fixes the crash and removes the obvious synchronous startup thumbnail blockers, but manual QA should still verify very large asset libraries in the Library tab because intentionally skipped oversized asset thumbnails display placeholders until a heavier preview path is requested.
- Deferred-scope changes: none. This does not add candidate gallery UI, graph controls, a new solver, new View Transform behavior, or a new Develop render algorithm.
- Next recommended starting point: if Editor-tab crashes return, first inspect `EditorModule::GetNodeGraph`/header inline access and translation-unit layout before changing node graph rendering logic.

## 2026-06-12 Non-Guide Bugfix Handoff - Editor Render Worker Startup Readiness

- Status delta: no numbered guide status changed; this was a startup stability fix after the app began exiting before the main window appeared.
- Files changed: `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Implemented: `EditorRenderWorker::Initialize` now waits until the worker thread has made its shared GL context current, loaded GL functions, and initialized its persistent `RenderPipeline` before returning control to `EditorModule::Initialize`.
- Implemented: worker initialization now reports failure cleanly, joins the worker thread, destroys the worker window, and leaves `m_RenderWorkerAvailable` false instead of racing startup.
- Diagnosis: the crash reproduced as access violation `0xC0000005` after `Async::TaskSystem` initialization and immediately after render-worker startup; adding trace timing made the crash disappear, confirming a startup race.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; seven repeated `build\Stack.exe` launches reached a responsive `Stack` main window; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `build\Stack.exe --validate-layer-registry` passed.
- Remaining risk: manual UI QA should still confirm the editor tab and Develop sliders after this startup fix, but automated startup and Develop validation no longer reproduce the no-window launch failure.
- Deferred-scope changes: none. This does not add candidate gallery UI, graph controls, a new solver, or a new render algorithm.
- Next recommended starting point: if startup instability returns, inspect `EditorRenderWorker::Initialize`, `EditorRenderWorker::ThreadMain`, and shared OpenGL context creation before changing Develop solver logic.

## 2026-06-12 Non-Guide Bugfix Handoff - Develop Auto Render Stability

- Status delta: no numbered guide status changed; this was a stability bugfix for current Develop Auto behavior before continuing planning-guide work.
- Files changed: `src/Editor/EditorModule.h`, `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/Internal/EditorModuleRawUI.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/Internal/EditorModulePersistence.cpp`, `src/Editor/EditorModule.cpp`, `src/Renderer/RenderPipeline.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Implemented: rendered Develop candidate feedback now carries a per-node RAW Develop interaction serial and is dropped when stale.
- Implemented: rendered candidate feedback is deferred during a dedicated 0.60 second per-node quiet window after Auto/Manual Develop interactions, and a fresh render is scheduled after the quiet window instead of applying the old result.
- Implemented: Auto Develop sliders record interaction while active, not only after a payload commit, so background rendered feedback cannot author a follow-up solve while the user is dragging.
- Implemented: RawDevelop raw-base and hidden pre-finish cache reuse now prefers cloned stage-cache snapshots over persistent graph-cache entries for those stage boundaries.
- Implemented: integrated Develop ToneCurve and Develop scene prep now pass through the previous nonblank stage if they produce an effectively black output from a nonblank input.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `build\Stack.exe --validate-layer-registry` passed.
- Added validation coverage: feedback gate classifier covers stale drop, recent-edit defer, and post-quiet apply decisions; existing Develop node smoke continues to cover nonblank synthetic RAW rendering and raw-base/pre-finish stage-cache reuse.
- Remaining risk: full hands-on slider-drag QA should still be done in the running UI with a real RAW graph; the automated smoke validates the guardrails but does not simulate every ImGui drag path.
- Deferred-scope changes: none. This does not add a gallery, graph controls, new solver architecture, View Transform behavior, or a new render algorithm.
- Next recommended starting point: if the alternation returns, inspect `ApplyDevelopCandidateRenderFeedback`, `BuildDevelopCandidateRenderRequests`, and the RawDevelop branch in `RenderPipeline::ExecuteGraph`, especially the interaction serial, quiet-window scheduling, and raw-base/pre-finish cache-hit diagnostics.

Tracking hardening acceptance, 2026-06-11:

- `AGENTS.md` points to the current-behavior context, guide index, tracker, source map, decisions, pass protocol, deferred scope, and active numbered guide workflow.
- The Guide 01 checklist below represents the requested Auto intent/mode implementation requirements and is the durable "already done" guard for that work.
- Guides 02, 03, 04, 05, and 06 now have `Partial` progress. Guides 07 through 10 remain `Not Started`. Do not reset real partial guide progress to satisfy stale acceptance text that assumes later guides are untouched.
- Deferred features are owned in `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`; they may be documented or prepared for, but not claimed implemented until their owning pass updates the tracker, checklist, validation notes, decisions, and deferred-scope status together.
- Each future implementation pass must add or update a handoff note before the final response, so continuation does not depend on chat history surviving.
- Validation for this hardening pass: Markdown/source-truth audit only; no code changes or build run required. Checked `AGENTS.md`, `DEVELOP_PASS_PROTOCOL.md`, `DEVELOP_DEFERRED_SCOPE.md`, `DEVELOP_DECISIONS.md`, and this tracker for the required continuation pointers, Guide 01 checklist coverage, later-guide status truth, handoff rules, and deferred-feature ownership map.

## Tracking Hardening Requirement Checklist

| Requirement | Status | Evidence / doc location | Notes |
|---|---|---|---|
| Keep `AGENTS.md` as the short Develop entrypoint. | Complete | `AGENTS.md` | Points to source-truth context, guide index, tracker, source map, decisions, pass protocol, deferred scope, and active numbered guide workflow. |
| Define a start/during/end pass protocol with completion rules. | Complete | `docs/engineering/develop/DEVELOP_PASS_PROTOCOL.md` | Includes the rule that a guide is not `Complete` unless code, checklist, tracker, source map, decisions/deferred notes, and validation are current. |
| Preserve guide-level status tracking for all ten guides. | Complete | Guide table below | Guide 01 is `Complete`; Guides 02, 03, 04, 05, and 06 are truthfully `Partial`; Guides 07 through 10 are `Not Started`. |
| Add requirement-level tracking and require checklists for future guides. | Complete | Requirement Tracking Rules below | Future guide passes must add or update a checklist before claiming implementation progress. |
| Represent every Guide 01 implementation requirement in a checklist. | Complete | Guide 01 Requirement Checklist below | This is the durable "already done" guard for Auto intent/mode work. |
| Add explicit "Do Not Re-Implement / Already Done" notes for Guide 01. | Complete | `Do Not Re-Implement / Already Done` below | Prevents adding a second Auto intent enum, duplicate mode field, or duplicate candidate framework. |
| Require append-only pass handoff notes. | Complete | Continuation handoff rule above; `docs/engineering/develop/DEVELOP_PASS_PROTOCOL.md` | Future passes must record status delta, validation, remaining work, deferred-scope updates, and the next recommended starting point. |
| Centralize deferred feature ownership. | Complete | `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` | Candidate gallery, graph controls, subject brush, learning application, convergence engine, color graph, denoise overhaul, and View Transform changes are mapped to owners/statuses. |
| Record the tracking decision durably. | Complete | `docs/engineering/develop/DEVELOP_DECISIONS.md` | Decision record says the tracker, pass protocol, source map, decisions, and deferred-scope docs are the continuation system. |
| Preserve current truth when prompt assumptions are stale. | Complete | Current status guard above; tracking decision record | The hardening prompt assumed Guides 02-10 were all `Not Started`; this tracker preserves verified Guide 02/03/04/05/06 `Partial` progress and leaves Guides 07-10 `Not Started`. |
| Keep the hardening pass Markdown-only. | Complete | This checklist; tracking acceptance note above | No code changes, scripts, database, or build validation are required for this pass. |

## 2026-06-11 Tracking-Hardening Handoff

- Status delta: continuation tracking hardened; no numbered-guide implementation status changed.
- Files changed: `AGENTS.md`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_PASS_PROTOCOL.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Validation: Markdown/source-truth audit only. Confirmed the entrypoint docs, Guide 01 checklist, current guide statuses, deferred-scope ownership, pass protocol, and decision record are present. No build was required because this pass did not change source code.
- Remaining tracking work: future implementation passes must keep adding requirement checklists and dated handoff notes as they touch each guide.
- Deferred-scope changes: added status definitions and the same-pass update rule; no deferred feature moved to implemented.
- Next recommended starting point: for Guide 03 processing work, read the Guide 03 checklist below, the latest Guide 03 increment notes, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, and the source-map entries for `Guide 03 candidate parameter solve foundation` and `Guide 03 rendered candidate metrics path`.

## 2026-06-11 Tracking-Hardening Handoff - Guide vs Feature Status

- Status delta: documentation clarified the difference between guide-level status and cross-guide feature substrate status; no numbered-guide implementation status changed.
- Files changed: `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_PASS_PROTOCOL.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Validation: Markdown/source-truth audit only. Confirmed `AGENTS.md` still points to all durable Develop tracking docs, the Guide 01 checklist still covers all implementation requirements, deferred features remain mapped to owners, and the pass protocol now tells future sessions not to confuse a partial deferred-scope substrate row with guide-level start/completion.
- Remaining tracking work: future passes should keep guide-level rows and feature-level deferred-scope rows synchronized in the same pass when an owning guide actually starts.
- Deferred-scope changes: clarified status semantics only; no deferred feature moved to `Partial` or `Complete`.
- Next recommended starting point: before continuing any later guide, reconcile that guide's table row, its requirement checklist, and any related `DEVELOP_DEFERRED_SCOPE.md` feature rows separately.

## 2026-06-11 Guide 03 Processing Handoff - Convergence-Focused Render Budget

- Status delta: Guide 03 remains `Partial`; convergence evidence now affects candidate render scheduling breadth, not only rendered-feedback admission after metrics arrive.
- Files changed: `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/EditorModule.h`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed.
- Implemented: `ResolveDevelopAdaptiveRenderBudgetPolicy` now reads `autoCandidateConvergenceEvidence` and `autoCandidateConvergenceAdmissionTightened`.
- Implemented: late, non-targeted continuation solves that are waiting for fresh rendered metrics and already have tightened admission narrow the per-node candidate render budget from four to three, with reason `convergenceEvidenceFocusedValidation`.
- Implemented: active refine, responsible-stage, adoption, and merge validation still keep the existing expansion path up to six renders, so the focused-budget rule does not starve a named repair target.
- Implemented: worker request/result diagnostics now carry convergence state/decision/reason plus a narrowed flag, and rendered feedback writes `autoCandidateRenderedAdaptiveBudgetNarrowed*` and `autoCandidateRenderedAdaptiveBudgetConvergence*` fields.
- Validation coverage added: `ValidateDevelopAutoSolveBehavior` synthesizes late awaiting-metrics convergence evidence and verifies the budget narrows to three, remains unexpanded, and reports the focused-validation reason.
- Remaining work: this is not a full scheduler. It does not add a background render queue, candidate gallery, sidecar stats bus, persistent thumbnail history, user picker, full candidate-family suppression policy, or full stage-specific admission matrix.
- Deferred-scope changes: Guide 03 candidate generation/rendered metrics/convergence remains `Partial`; no deferred feature moved to `Complete`.
- Next recommended starting point: continue Guide 03 by adding stage-specific admission thresholds or stronger candidate-family suppression from `ConvergenceEvidenceV1` inside `ResolveDevelopConvergenceAdmissionPolicy`, `BuildDevelopCandidateRenderRequests`, and `ApplyRenderedCandidateFeedbackToSolve`.

## 2026-06-11 Guide 03 Processing Handoff - Convergence Admission Policy

- Status delta: Guide 03 remains `Partial`; `ConvergenceEvidenceV1` now influences actual rendered-feedback admission decisions instead of only summarizing the loop.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed.
- Implemented: `ConvergenceAdmissionV1` resolves a conservative rendered-feedback admission policy from prior convergence evidence, continuation policy, pass count, and refine-intent state.
- Implemented: non-refine continuation solves that are waiting for rendered metrics with pass > 0 require a clearer rendered-score improvement than the base first-pass threshold. First continuation raises the threshold from `0.025` to `0.035`; later continuation raises it to `0.045`.
- Implemented: marginal candidates that clear the base threshold but fail the stricter continuation threshold stop with `convergenceAdmissionNoMeaningfulImprovement`, which is classified as a converged/no-useful-improvement stop rather than a failed-render state.
- Implemented: diagnostics now write top-level `autoCandidateConvergenceAdmission*` fields and embed the admission record inside `autoCandidateConvergenceEvidence.admission`, including base threshold, active threshold, tightened flag, reason, evidence state/decision, and evidence pass.
- Validation coverage added: `ValidateDevelopAutoSolveBehavior` now synthesizes a continued solve awaiting fresh rendered metrics, injects a marginal rendered challenger, and checks that admission tightens the threshold, stops with `convergenceAdmissionNoMeaningfulImprovement`, records `ConvergenceAdmissionV1`, and leaves the broader convergence evidence as `converged`.
- Remaining work: this is not the full scheduled convergence engine. It does not add a background pass queue, gallery UI, sidecar stats bus, full perceptual/spatial score model, graph controls, user picker, or applied learning system.
- Deferred-scope changes: Guide 03 candidate generation/rendered metrics/convergence remains `Partial`; no deferred feature moved to `Complete`.
- Next recommended starting point: continue Guide 03 by using `ConvergenceEvidenceV1` / `ConvergenceAdmissionV1` to tune candidate render request budgets, candidate family suppression, and stage-specific stop/admission thresholds in `BuildDevelopCandidateRenderRequests`, `ApplyDevelopCandidateRenderFeedback`, and `ApplyRenderedCandidateFeedbackToSolve`.

## 2026-06-11 Guide 03 Processing Handoff - Convergence Evidence Summary

- Status delta: Guide 03 remains `Partial`; the existing compact rendered-feedback loop now has one durable evidence record that classifies wait/continue/converged/stopped states from the same evidence the loop uses.
- Files changed: `src/Editor/EditorModule.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed.
- Implemented: `ConvergenceEvidenceV1` writes top-level `autoCandidateConvergenceEvidenceVersion`, `autoCandidateConvergenceState`, `autoCandidateConvergenceDecision`, `autoCandidateConvergenceReason`, `autoCandidateConvergenceShouldContinue`, and nested `autoCandidateConvergenceEvidence` JSON.
- Implemented: the evidence record combines parameter solve fingerprint/pass stability, rendered-metrics readiness, rendered feedback applied/stopped state, rendered stop convergence classification, stability/trend/monotonic evidence, pass budget, rendered loop state, and `RenderedContinuationV1` decision/stage/evidence into one authoritative summary for future controller work.
- Validation coverage added: `ValidateDevelopAutoSolveBehavior` now checks `ConvergenceEvidenceV1` for initial awaiting-metrics state, active continuation after rendered adoption, stable rendered convergence, trend/no-improvement convergence, pass-limit stopped state, and no-rendered-best stopped state.
- Remaining work: this is a compact evidence summary and controller contract, not the full scheduled convergence engine. It does not create a background pass scheduler, candidate gallery, user picker, sidecar stats bus, full perceptual/spatial score model, or applied learning system.
- Deferred-scope changes: Guide 03 candidate generation/rendered metrics/convergence remains `Partial`; Guide 10 diagnostics remain `Partial` only for JSON diagnostics, not UI. No deferred feature moved to complete.
- Next recommended starting point: continue Guide 03 by letting `ConvergenceEvidenceV1` actively tune continuation thresholds or admission decisions in `ApplyRenderedCandidateFeedbackToSolve`, `EvaluateDevelopRenderedFeedbackTrend`, `WouldRepeatUnhelpfulRenderedRefinement`, and `BuildDevelopCandidateRenderRequests`, while keeping gallery UI and graph controls deferred.

## 2026-06-11 Guide 03 Processing Handoff - Continuation Candidate Expansion

- Status delta: Guide 03 remains `Partial`; rendered continuation can now create missing stage-relevant authored candidate families, not only bias the candidates that first-pass stats already emitted.
- Files changed: `src/Editor/EditorModule.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed.
- Implemented: `ContinuationCandidateExpansionV1` uses the existing `RenderedContinuationV1` stage/refine focus resolved by `ResolveDevelopContinuationCandidateBiasProfile` to add missing candidate families for RAW/global highlight placement, scene-prep range/readability, finish-tone shape, or RAW cleanup/texture checks when continuation asks for another rendered validation pass.
- Implemented: expansion is conservative and duplicate-aware. It uses `HasDevelopAutoCandidateId` so it only adds candidate families absent from the ordinary first-pass solve, records `continuationExpansion*` per candidate, writes `scoreComponents.renderedContinuationExpansion`, adds a `renderedContinuationCoverage` dimension, and writes top-level `autoCandidateContinuationExpansion*` fields with separate eligible/active/added-count diagnostics.
- Validation coverage added: `ValidateDevelopAutoSolveBehavior` creates a calm finish-tone continuation case where the ordinary stats gates would not generate finish-tone probes, then checks that continuation expansion adds a finish-tone family with `ContinuationCandidateExpansionV1` diagnostics.
- Remaining work: this is not the full adaptive candidate generator from Guide 03. It does not create a gallery, persistent thumbnail queue, user picker, applied preference-learning system, full convergence controller, subject/color/denoise future-guide families, or graph diagnostics UI.
- Deferred-scope changes: Guide 03 candidate generation/rendered metrics/convergence remains `Partial`; Guide 10 diagnostics remain `Partial` only for JSON diagnostics, not UI. No deferred feature moved to complete.
- Next recommended starting point: continue Guide 03 by making the convergence controller consume the richer rendered continuation/feedback evidence more directly, especially around `BuildDevelopAutoCandidateSolve`, `ApplyRenderedCandidateFeedbackToSolve`, `EvaluateDevelopRenderedFeedbackTrend`, `WouldRepeatUnhelpfulRenderedRefinement`, and `ApplyDevelopCandidateRenderFeedback`.

## 2026-06-11 Guide 03 Processing Handoff - Continuation Candidate Bias

- Status delta: Guide 03 remains `Partial`; the explicit rendered continuation policy now affects the next authored candidate solve's scoring, not only the next rendered-candidate budget.
- Files changed: `src/Editor/EditorModule.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed.
- Implemented: `ContinuationCandidateBiasV1` resolves a compact profile from `autoCandidateRenderedContinuationPolicy` plus the previous rendered-feedback revision stage/refine intent. When the policy says to continue, the next Auto solve applies a small bounded score bonus to authored candidate families that directly answer the responsible stage or active rendered refine intent.
- Implemented: the bias is written into integrated tone JSON as top-level `autoCandidateContinuationBias*` fields, per-candidate `continuationBias*` diagnostics, and `scoreComponents.renderedContinuationBias` plus a `renderedContinuationFit` dimension. This makes future passes able to tell when continuation evidence changed candidate ranking.
- Implemented: pending rendered-metrics solves carry the same bias forward until their rendered metrics are consumed, preserving candidate-solve fingerprint stability between the "awaiting metrics" and "metrics ready" phases.
- Validation coverage added: `ValidateDevelopAutoSolveBehavior` checks that a finish-tone continuation produces active `ContinuationCandidateBiasV1` diagnostics and boosts at least one finish-tone candidate without breaking existing rendered-feedback convergence/monotonic-guard behavior.
- Remaining work: this is a bounded continuation-aware scoring hint over existing authored candidates. It is not a full adaptive candidate generator, background queue, full staged convergence controller, user-visible gallery, graph diagnostic UI, or user preference-learning application.
- Deferred-scope changes: Guide 03 candidate generation/rendered metrics/convergence remains `Partial`; Guide 10 diagnostics remain `Partial` only for JSON diagnostics, not UI. No deferred feature moved to complete.
- Next recommended starting point: continue Guide 03 by letting richer rendered continuation evidence tune candidate-family generation or convergence thresholds, or by broadening actual rendered candidate families. Start in `BuildDevelopAutoCandidateSolve`, `ApplyRenderedCandidateFeedbackToSolve`, and `BuildDevelopCandidateRenderRequests`, while keeping gallery UI, graph controls, subject/color/denoise maps, and applied preference learning deferred to their owning guides.

## 2026-06-11 Guide 03 Processing Handoff - Rendered Continuation Policy

- Status delta: Guide 03 remains `Partial`; the current compact rendered-feedback loop now records an explicit continuation policy instead of leaving wait/continue/stop decisions implicit in branch logic.
- Files changed: `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/EditorModule.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed.
- Implemented: `RenderedContinuationV1` writes `autoCandidateRenderedContinuationPolicy` and `autoCandidateRenderedContinuationVersion` alongside `RenderedFeedbackLoopV1`. The policy records `decision`, `reason`, `nextStep`, required next evidence, pass/next-pass/max/remaining budget, stage focus/reason, and compact evidence for improvement, stability/trend/monotonic state, relative status, or stage-boundary signal where available.
- Implemented: the Auto-solve diagnostic path records `waitForRenderedMetrics` for a newly solved candidate set, `continue` for rendered feedback that should render the updated solve, and `stop` for stable/no-improvement/pass-limit/no-viable outcomes. The rendered-feedback application path writes the same policy into the loop record after actual candidate render metrics are consumed.
- Validation coverage added: `ValidateDevelopAutoSolveBehavior` checks the awaiting-metrics policy, active continuation after adoption, stable-stop policy, no-improvement-trend stop policy, and pass-limit stop policy.
- Remaining work: this is a compact continuation contract over the current loop, not the full scheduled convergence engine, adaptive background render queue, user-visible candidate history, thumbnail gallery, sidecar stats bus, or graph-style diagnostics UI.
- Deferred-scope changes: Guide 03 candidate generation/rendered metrics/convergence remains `Partial`; Guide 10 diagnostics remain `Partial` only for JSON diagnostics, not UI. No deferred feature moved to complete.
- Next recommended starting point: continue Guide 03 by turning the explicit continuation policy into a richer adaptive scheduler/continuation controller, or by expanding rendered evidence that feeds the policy, while keeping gallery UI, graph controls, full subject/color/denoise maps, and user preference-learning application deferred to their owning guides.

## 2026-06-11 Guide 03 Processing Handoff - Adaptive Render Budget

- Status delta: Guide 03 remains `Partial`; the candidate render scheduler now uses continuation-policy state to adapt the next per-node render budget for actual rendered feedback passes.
- Files changed: `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/EditorModule.h`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed.
- Implemented: `AdaptiveRenderBudgetV1` resolves a per-node candidate render budget from `autoCandidateRenderedContinuationPolicy`, solve/render fingerprints, candidate count, active revision stage, and active refine intent. Default pending solves keep the four-candidate budget; active continuation passes can expand to five or six candidate renders when there are enough survivors and the next pass needs responsible-stage, refine-intent, adoption, or merge validation.
- Implemented: `BuildDevelopCandidateRenderRequests` uses the adaptive budget in the actual selected/survivor scheduling loops and `CanScheduleDevelopCandidateRenderRequest`, while keeping a conservative total snapshot cap.
- Implemented: `DevelopCandidateRenderRequest` / `DevelopCandidateRenderResult` carry adaptive budget version, budget, reason, continuation decision, and expansion flag through the worker. `ApplyDevelopCandidateRenderFeedback` writes per-candidate and aggregate adaptive-budget diagnostics into `RenderMetricsV1` tone JSON.
- Validation coverage added: `ValidateDevelopAutoSolveBehavior` checks the default four-candidate behavior, adaptive six-candidate scheduling for a continued stage/refine validation pass, and the initial awaiting-metrics case that stays at the default budget.
- Remaining work: this is an adaptive budget/focus hook over the current worker path. It is not a full background render queue, thumbnail gallery, sidecar stats bus, full staged convergence scheduler, or user-facing candidate selection system.
- Deferred-scope changes: Guide 03 candidate generation/rendered metrics/convergence remains `Partial`; Guide 10 diagnostics remain `Partial` only for JSON fields, not UI. No deferred feature moved to complete.
- Next recommended starting point: continue Guide 03 by making the continuation policy adjust candidate-family generation or stop thresholds from richer rendered evidence, or by broadening actual rendered candidate families, while keeping gallery UI, graph controls, full subject/color/denoise maps, and preference-learning application deferred to their owning guides.

## 2026-06-11 Guide 03 Processing Handoff - Relative Rendered Survivor Comparison

- Status delta: Guide 03 remains `Partial`; rendered survivor ranking now uses selected-baseline and active-repair-intent evidence instead of relying only on standalone rendered score.
- Files changed: `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/EditorModule.cpp`, `src/Editor/EditorModule.h`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; scoped `git diff --check -- src/Editor/Internal/EditorModuleRendering.cpp src/Editor/EditorModule.cpp src/Editor/EditorModule.h src/main.cpp docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md docs/engineering/develop/DEVELOP_SOURCE_MAP.md docs/engineering/develop/DEVELOP_DECISIONS.md docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` passed with only existing LF-to-CRLF warnings. Full `git diff --check` is still blocked by generated `src/App/Resources/EmbeddedTabIcons.h` trailing-whitespace output from the broader dirty/build state.
- Implemented: `CompareDevelopRenderedCandidateToSelected` computes `RenderedRelativeComparisonV1` diagnostics for each successful rendered candidate: standalone score, adjusted score, selected metric distance, active repair metric, repair delta, repair bonus, regression penalty, distance bonus, status, and reason.
- Implemented: `ApplyDevelopCandidateRenderFeedback` writes standalone and relative comparison fields per rendered candidate, top-level selected/best relative fields, and uses the adjusted relative score for survivor ordering, duplicate representative ordering, best rendered candidate selection, pair/ensemble merge suggestions, and no-meaningful-improvement decisions.
- Implemented: relative comparison rewards candidates that improve the active repair target, such as highlight protection, shadow opening, midtone brightening, contrast repair, cleaner shadows, or texture preservation, and penalizes candidates that score well globally while worsening the selected render's clipping, highlight pressure, shadow/noise risk, halo risk, color cast, localized damage risk, or active repair direction.
- Implemented: `ApplyRenderedCandidateFeedbackToSolve` recognizes relative regression/missed-active-repair diagnostics as a no-useful-improvement stop when the adjusted best is only marginally above the selected baseline. `renderedBestRelativeRegression` is treated as an intent-relative convergence stop, not as a failed render.
- Validation coverage added: `ValidateDevelopAutoSolveBehavior` now checks a synthetic selected baseline, a higher-standalone highlight-regressing candidate, and a lower-standalone highlight-protecting candidate. The intent winner must outrank the raw-score winner after relative comparison.
- Remaining work: this is still compact metric comparison, not full perceptual scoring, local EV maps, subject/skin/memory-color analysis, candidate thumbnails, user candidate selection/merge UI, user preference-learning application, or a full multi-pass staged convergence controller.
- Deferred-scope changes: Guide 03 candidate generation/rendered metrics/convergence remains `Partial`; Guide 10 diagnostics remain `Partial` only for JSON diagnostics, not UI. No deferred feature moved to complete.
- Next recommended starting point: continue Guide 03 by adding richer rendered comparison evidence or an adaptive continuation policy around `ApplyDevelopCandidateRenderFeedback`, `ApplyRenderedCandidateFeedbackToSolve`, and `EvaluateDevelopRenderedFeedbackTrend`, while keeping gallery UI, graph controls, full subject/color/denoise maps, and user preference learning deferred to their owning guides.

## 2026-06-11 Guide 03/06 Processing Handoff - Rendered Color-Cast Metrics

- Status delta: Guide 03 remains `Partial`; Guide 06 remains narrow `Partial`. Rendered feedback now has compact color-cast/channel-balance evidence, but full color strategy, mood/skin/memory-color protection, color graph controls, and perceptual color scoring remain deferred.
- Files changed: `src/Editor/EditorRenderWorker.h`, `src/Editor/EditorRenderWorker.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/Editor/EditorModule.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed.
- Implemented: `DevelopCandidateRenderMetrics` now carries mean red/green/blue, warm-cool bias, magenta-green bias, channel imbalance, and `colorCastRisk`. `AnalyzeDevelopCandidatePixels` computes those values from rendered RGBA pixels.
- Implemented: rendered metric JSON writes and reads the color fields, so feedback history/readback can preserve the new evidence. Rendered scoring applies a small color-plausibility penalty for high cast risk, and rendered duplicate distance treats color-only metric differences as meaningful.
- Implemented: rendered damage rejection now rejects only extreme color casts/channel imbalances beyond the selected intent, with thresholds chosen to avoid treating ordinary warm/cool mood as damage. Validation covers color metric population, color-only metric distance, and extreme color-cast rejection while preserving a safe fixture.
- Remaining work: this does not implement full Guide 06 color strategy, skin or memory-color protection, color graph controls, camera-profile/color-management redesign, full perceptual color scoring, visual color maps, gallery UI, or user-driven candidate selection/merge.
- Deferred-scope changes: candidate generation/rendered metrics/convergence stays `Partial`; Guide 06 color/WB/mood remains `Partial` only for RAW WB probes and compact rendered color-cast evidence.
- Next recommended starting point at the time: add a relative rendered-survivor comparison layer. Status update: this was implemented later the same day as `RenderedRelativeComparisonV1`; future passes should build on that layer rather than reimplement it.

## 2026-06-11 Guide 03 Processing Handoff - Candidate Render Budget Fairness

- Status delta: Guide 03 remains `Partial`; the compact rendered-candidate loop now gives each active Develop node its own bounded render-request budget instead of letting the first active node consume the entire graph-global budget.
- Files changed: `src/Editor/EditorModule.h`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `git diff --check -- src/Editor/EditorModule.h src/Editor/Internal/EditorModuleRendering.cpp src/main.cpp` passed with only existing LF-to-CRLF warnings for source files.
- Implemented: `BuildDevelopCandidateRenderRequests` now uses named budget constants and a shared `CanScheduleDevelopCandidateRenderRequest` helper. Each active-path Auto `RawDevelop` node can schedule up to four selected/surviving candidate renders, while the full snapshot is still capped at sixteen candidate render requests.
- Implemented: the old early graph-global four-request break is removed, so a later active Develop node can still request rendered feedback after an earlier active Develop node schedules its own four probes.
- Validation coverage added: `ValidateDevelopAutoSolveBehavior` checks the scheduling predicate directly, including the old starvation case where total requests are already four but the next node has zero requests.
- Remaining work: this is fairer bounded scheduling over the current worker path. It is not the full visual gallery, user picker, expanded adaptive render queue, standalone staged scheduler, sidecar stats bus, or full multi-pass convergence controller.
- Deferred-scope changes: candidate generation/rendered metrics/convergence remains `Partial`; multi-candidate gallery and full scheduled convergence remain deferred, but the recorded multi-Develop-node starvation risk is no longer the recommended next blocker.
- Next recommended starting point: deepen actual rendered scoring/damage maps or add an adaptive continuation policy for hard images in `ApplyDevelopCandidateRenderFeedback` / `ApplyRenderedCandidateFeedbackToSolve`, while keeping gallery UI and graph controls deferred until their owning guides.

## 2026-06-11 Code-Audit Handoff - Rendered Feedback Convergence Contract Risk

- Status delta: Guide 03 remains `Partial`; no code was changed by this audit note.
- Docs changed: `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Audit finding, now resolved by the handoff below: `ApplyDevelopCandidateRenderFeedback` previously could report rendered feedback as converged when rendered candidates existed but no acceptable winner was found, including damage-only or no-best cases. Future passes should preserve the distinction between stable/no-improvement convergence and failed/no-viable-rendered-candidate stops.
- Primary source locations to inspect: `src/Editor/Internal/EditorModuleRendering.cpp` around `ApplyDevelopCandidateRenderFeedback`, `WriteDevelopCandidateRenderedFeedbackLoopRecord`, and the `autoCandidateRenderedConverged` / loop-state fields; validation starts in `src/main.cpp` around `ValidateDevelopAutoSolveBehavior`.
- Required follow-up validation: add smoke coverage for damage-only rendered feedback and no-best rendered feedback so those cases write an explicit stop/failure state instead of `converged`.
- Related risks to keep in view: stage-cache diagnostics are heuristic for non-scene-prep/finish-tone stages; integrated ToneCurve failure currently falls back to pre-tone output before Finish Mask blending. The later-active-Develop starvation risk from the old graph-global four-request cap is addressed by the `2026-06-11 Guide 03 Processing Handoff - Candidate Render Budget Fairness` above.
- Resolution: addressed by the `2026-06-11 Guide 03 Processing Handoff - Rendered Feedback Stop/Convergence Contract` below.

## 2026-06-11 Guide 03 Processing Handoff - Rendered Feedback Stop/Convergence Contract

- Status delta: Guide 03 remains `Partial`; the false-convergence risk from the code-audit handoff is fixed for the current compact rendered-feedback loop.
- Files changed: `src/Editor/EditorModule.h`, `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `git diff --check -- src/Editor/EditorModule.h src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/main.cpp docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md docs/engineering/develop/DEVELOP_SOURCE_MAP.md docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md docs/engineering/develop/DEVELOP_DECISIONS.md` passed with only existing LF-to-CRLF warnings for source files.
- Implemented: rendered-feedback stop reasons now use one shared convergence classifier. `renderedMetricsStable`, no-improvement trend stops, selected-still-best, no-meaningful-improvement, repeated-refine no-improvement, merge/adoption no-gain, and monotonic risk stops can still mark convergence. `noRenderedBestCandidate`, `allRenderedCandidatesRejectedForDamage`, `renderedBestBelowQualityFloor`, `candidateRendersFailed`, and `renderedFeedbackPassLimit` now remain explicit stopped/failed states and do not set `autoCandidateRenderedConverged`.
- Implemented: `ApplyDevelopCandidateRenderFeedback` now writes `autoCandidateRenderedRevisionStage = "none"` and loop `state = "stopped"` for no-viable rendered candidate stops instead of `converged`.
- Implemented: `ApplyRenderedCandidateFeedbackToSolve` now uses the same classifier when converting rendered feedback into the next Auto solve diagnostics, so no-best/below-quality states do not mark final stage convergence.
- Validation coverage added: `ValidateDevelopAutoSolveBehavior` checks the stop-reason classifier directly, including damage-only/no-best/below-quality/pass-limit as non-converged, and checks a synthetic no-best rendered-feedback solve writes `state = "stopped"`, `autoCandidateRenderedConverged = false`, and `revisionStage = "none"`.
- Remaining work: this does not implement the full scheduled convergence engine, visual candidate gallery, user selection/merge UI, user learning application, full physical staged controller, or broader perceptual/spatial damage maps.
- Deferred-scope changes: candidate generation/rendered metrics/convergence remains `Partial`, but the recorded false-convergence risk is no longer the next blocker.
- Next recommended starting point: continue Guide 03 by deepening actual rendered scoring/damage maps or adaptive continuation policy before expanding UI.

## 2026-06-11 Guide 03/06 Processing Handoff - RAW White-Balance Candidate Probes

- Status delta: Guide 03 remains `Partial`; Guide 06 moves to narrow `Partial` because first-pass RAW white-balance probes now exist. Full Guide 06 color, mood, skin, memory-color, and color-graph work remains deferred.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed. Earlier build attempts hit an MSVC PDB write race and a stale `Stack.exe` lock; stopping stale processes and using `/FS` resolved the environment issue.
- Implemented: Auto now has categorical RAW white-balance probes in the candidate solver. `wbDaylightCorrection` maps to `Raw::WhiteBalanceMode::Auto`, `wbNeutralCorrection` maps to `Neutral`, and `wbCameraMood` maps to `AsShot` for carried/diagnostic states. The solver generates daylight/neutral probes when camera/daylight/neutral metadata differs enough, classifies them as `rawGlobal`, scores them with `colorPlausibility` and `moodColorPreservation` dimensions, writes `rawOverrides.whiteBalanceMode`, selected/authored WB diagnostics, includes WB state in candidate fingerprints, and applies the selected probe to authored RAW settings. The render-request path reads WB overrides, applies actual RAW WB mode changes in copied payloads, writes `autoCandidateWhiteBalanceProbe` / `autoCandidateWhiteBalanceMode`, and gives WB probes bounded diversity/stage priority.
- Remaining work: full Guide 06 color/WB strategy, mood-preservation rules beyond the coarse WB probe signal, skin and memory-color protection, color graph controls, camera-profile/color-management redesign, user-visible candidate gallery, user selection/merge UI, and the full scheduled convergence engine.
- Deferred-scope changes: candidate generation/rendered metrics/convergence remains `Partial`; Guide 06 color/WB/mood scope is now `Partial` for first-pass RAW WB probes only.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `BuildDevelopAutoCandidateSolve`, `TryResolveDevelopWhiteBalanceProbeMode`, `BuildDevelopAutoCandidateScoreComponents`, and `WriteDevelopAutoCandidateSolveDiagnostics`; render-side follow-up starts in `src/Editor/Internal/EditorModuleDevelopCandidateRequests.cpp` around `TryResolveWhiteBalanceProbeCandidateModeForRenderRequest`, `TryReadCandidateWhiteBalanceOverrideForRenderRequest`, and `BuildDevelopCandidateRenderRequests`, and in `src/Editor/Internal/EditorModuleDevelopCandidateRenderPayload.cpp` around `ApplyDevelopGuidanceToCandidateRenderPayload`.

## 2026-06-11 Guide 03 Processing Handoff - Finish-Tone Candidate Probes

- Status delta: Guide 03 remains `Partial`; tone candidate coverage is stronger, but the full Guide 08 finish-tone redesign, gallery UI, user selection/merge UI, graph controls, and full convergence engine remain deferred.
- Files changed: `src/Editor/EditorModule.cpp`, `src/Editor/Internal/EditorModuleRendering.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Validation: `cmake --build build --config Release` passed; `build/Stack.exe --validate-develop-node-smoke` passed.
- Implemented: Auto now generates finish-tone authored candidates `toneSofterRolloff`, `tonePunchierShape`, `toneFlatterEditing`, and `toneDarkerToe` from current render-feedback stats and selected intent. These candidates score/mode-fit like other authored candidates, are classified as `finishTone`, get render-slot priority, freeze RAW and Scene Prep during candidate render payload mapping, and write `autoCandidateFinishToneProbe` diagnostics.
- Remaining work: full tone/finish strategy from Guide 08, mode-specific finish graphs, visual candidate gallery, user-directed selection/merge, richer perceptual scoring, and graph controls.
- Deferred-scope changes: candidate generation/rendered metrics/convergence remains `Partial`; full finish-tone strategy remains `Not Started` under Guide 08.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `BuildDevelopAutoCandidateSolve` / `ScoreDevelopAutoCandidate` for remaining candidate families, or in `src/Editor/Internal/EditorModuleRendering.cpp` around `BuildDevelopCandidateRenderRequests` if adding richer stage-specific render selection.

## 2026-06-11 Guide 03 Processing Handoff - Rendered Refine Monotonic Risk Guard

- Status delta: Guide 03 remains `Partial`; actual rendered-feedback convergence is stronger but the full gallery, staged controller, full spatial maps, and learning application remain deferred.
- Files changed: `src/Editor/EditorModule.cpp`, `src/main.cpp`, `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md`, `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`, `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`, `docs/engineering/develop/DEVELOP_DECISIONS.md`.
- Validation: `cmake --build build --config Release` passed; `build/Stack.exe --validate-develop-node-smoke` passed.
- Implemented: Auto-side rendered metric readback now includes compact spatial damage-risk fields, and repeated same-intent rendered refinements stop when the protected risk worsens without enough score gain.
- Remaining work: richer spatial/perceptual maps, broader no-improvement checks across future candidate families, formal staged controller, visual candidate gallery, user selection/merge UI, and preference-learning application.
- Deferred-scope changes: candidate generation / rendered metrics / convergence remains `Partial`; no deferred feature moved to `Complete`.
- Next recommended starting point: continue in `src/Editor/EditorModule.cpp` around `ApplyRenderedCandidateFeedbackToSolve`, `EvaluateRenderedRefineMonotonicGuard`, and `EvaluateDevelopRenderedFeedbackTrend`; for worker-side rendered evidence, continue in `src/Editor/Internal/EditorModuleRendering.cpp` around `ApplyDevelopCandidateRenderFeedback`.

| Guide | Status | Last touched | Code locations changed | What is implemented | Explicitly not implemented yet | Tests / validation run | Notes / risks |
|---|---|---:|---|---|---|---|---|
| 01 - Develop Philosophy, Auto Modes, and User Intent | Complete | 2026-06-10 | `src/Editor/NodeGraph/EditorNodeGraph.h`; `src/Editor/EditorModule.cpp`; `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`; `src/main.cpp` | Added durable Auto intent enum with Natural Finished default; stable serialization and safe fallback; Auto UI selector and descriptions; mode-aware profile layer biasing the existing auto solve; Reset Auto preserves selected intent; Auto Calibrate and mode changes re-solve; status readout includes mode; smoke coverage for default, serialization, fallback, and mode-aware solve behavior. | Candidate gallery, graph controls, subject brush, learning, full iterative convergence, color graph, denoise overhaul, new view transform behavior. | `cmake --build build --config Release` passed; `build/Stack.exe --validate-develop-node-smoke` passed. | Mode profiles are conservative first-pass biases over the current solver, not a final candidate-solving system. |
| 02 - RAW Data, Exposure EV, Brightness, and Scene-Linear Control | Partial | 2026-06-15 | `src/Editor/EditorModule.cpp`; `src/Editor/EditorModule.h`; `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/Editor/NodeGraph/EditorNodeGraph.h`; `src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp`; `src/Renderer/MaskRenderTypes.h`; `src/Renderer/RenderPipeline.cpp`; `src/main.cpp` | Implemented explicit exposure-vs-brightness semantics in the current `Develop` UI/readout layer: Auto presents `Brightness Intent`; Manual names literal `RAW Exposure / EV`; status shows RAW scale, local EV distribution, tone contrast, RAW placement, clipping ratio, highlight pressure, noise risk, and HDR spread; tone JSON receives brightness/exposure telemetry and diagnostic aliases. Added a separate `RAW Decode` node as the manual RAW-to-scene-linear foundation, reusing the same RAW base render math and exposing only decode essentials. `Add full tree` on RAW sources now builds the manual chain `RAW Source -> RAW Decode -> Tone Curve -> View Transform -> Output`. | Full exposure solver redesign, full suite of candidate exposure placements, graph-style exposure/range controls, subject-aware exposure priority, spatial clipping/noise maps and damage metrics, and any attempt to turn `Develop` into a mode switch instead of keeping a separate manual chain. | Latest run: `cmake --build build --config Debug --target StackGraphBehaviorTests Stack` passed; `build\StackGraphBehaviorTests.exe` passed; `build\Stack.exe --validate-develop-node-smoke` passed. | `RAW Decode` preserves the scene-linear EV contract in a separate manual chain. `Develop` keeps its existing identity and merged auto workflow; the manual chain is additive, not a rename or mode. |
| 03 - Iterative Auto Solve, Candidate Rendering, Convergence, and Learning | Partial | 2026-06-13 | `src/Editor/EditorModule.cpp`; `src/Editor/EditorModule.h`; `src/Editor/EditorRenderWorker.cpp`; `src/Editor/EditorRenderWorker.h`; `src/Editor/Internal/EditorModuleRendering.cpp`; `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/Renderer/RenderPipeline.cpp`; `src/Renderer/RenderPipeline.h`; `src/main.cpp` | Added a first candidate parameter-solve layer inside `ApplyDevelopAutoSolve`: current render-feedback stats generate authored guidance candidates, including a `highlightProtectedMids` exposure-placement candidate for broad highlight/HDR pressure and finish-tone candidates for softer highlight rolloff, punchier tone shape, flatter editing tone, and darker shadow toe. Candidates are scored by intent/range/highlight/noise/contrast/tone-shape signals, rejected or clustered when damaged/duplicate, remembered through bounded rejected-candidate decisions for the same image/state context, optionally merged in authored settings space, carried forward from prior selected/rendered-best/merge-source and bounded successful rendered-survivor authored candidates only while a rendered-feedback iteration session is active, written to candidate/convergence diagnostics including `ParameterScoreComponentsV1` score components and rendered revision-stage diagnostics in integrated tone JSON, recorded in `CandidateOutcomeLearningV1` solver outcome-learning events separately from whether learning is applied, and fed into the existing RAW + Scene Prep + integrated Tone solve. Added worker-side rendered candidate metrics for selected/surviving candidates on the active output path: copied candidate payloads render through the normal graph pipeline, measure luma/contrast/shadow/highlight/clipping, mean RGB, warm-cool bias, magenta-green bias, channel imbalance, compact color-cast risk, compact 3x3 local luma/contrast/pressure metrics, compact 3x3 regional damage-risk metrics for highlight crowding, shadow crowding, local edge stress, and flat-gray pressure, plus visual-risk metrics for saturation wash, edge/halo risk, and shadow texture pressure. Candidate render scheduling now gives each active-path Auto `RawDevelop` node a bounded rendered-feedback budget, with source-size-aware narrowing for large RAWs, avoiding the old graph-global four-request starvation of later Develop nodes while reducing diagnostic render pressure. Newer single-output snapshots can now supersede stale in-flight background feedback; the worker skips obsolete Develop candidate renders/previews at safe GL boundaries and avoids publishing superseded output textures. Large RAW candidate final/pre-finish metric readback is capped to bounded representative RGBA buffers, and per-candidate/aggregate diagnostics record the cap and downsample counts. Within each node budget, requests use score-plus-guidance diversity across cleanup/detail, finish-tone, mode-neighbor, rendered-local, and exposure-placement probes, bias scarce render slots toward the latest responsible rendered-feedback revision stage, reserve one render slot for an active-stage-relevant survivor when prior feedback names a responsible stage and the selected set does not already cover it, reserve one bounded render slot for the active rendered refine-intent repair family when possible, schedule the chosen render requests with `StageSchedulerV1`, keep bounded owned RawDevelop raw-base/pre-finish stage snapshots by fingerprint, rank rendered candidates with a compact metric score that includes conservative color plausibility, then applies `RenderedRelativeComparisonV1` to adjust survivor ranking against the selected rendered baseline and active repair intent before duplicate representative ordering, best selection, merge suggestions, and improvement checks, cluster metric-near rendered duplicates with pre-finish-aware preservation before selecting the rendered best candidate, write compact `RenderMetricsV1` diagnostics back to integrated tone JSON, adopt clear rendered wins directly, synthesize authored rendered merges, generate rendered-local authored candidate families from global/local rendered mismatch signals, route compact spatial-risk hotspots into rendered-local refinement intents, reject extreme rendered color casts/channel imbalance, record selected/best rendered metric snapshots in bounded feedback history, and write `RenderedFeedbackLoopV1` loop state for awaiting metrics, solve-requested, active, stopped, and converged states. The rendered-feedback stop contract now separates true convergence from no-viable-rendered-candidate stops: stable/no-improvement/anti-oscillation stops can mark converged, while no-best, all-damaged, below-quality, failed-render, pass-limit, and intent-relative regression outcomes remain stopped/failed or convergence stops according to the shared classifier. Auto status shows selected candidate, render-metrics status, and rendered best metric when available. | Full rendered multi-candidate gallery, persistent thumbnail cache, user candidate selection/merge UI, user choice/rejection preference-learning controls and application, true spatial/visual damage maps, subject/color/skin/memory/denoise-specific candidate engines, full staged RAW/global/scene-prep/finish controller and sidecar stats bus. | Latest run: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed, including relative rendered-survivor comparison, rendered color-cast metric population, color-only rendered metric distance, extreme color-cast rejection, candidate render budget fairness validation, rendered stop/convergence classifier validation, no-best rendered-feedback non-convergence validation, stale worker snapshot abort validation, metric-readback budget validation, and real RAW smoke on `RAW IMAGES\motion cam pro.dng`. Scoped `git diff --check` reported only line-ending warnings. Prior smoke coverage includes rendered metric distance validation, stage-aware rendered duplicate clustering validation, local rendered tile-metric validation, local refine-intent validation, rendered-local candidate-family validation, carried-forward rendered-feedback candidate validation, rendered survivor carry-forward validation, rendered revision-stage validation, active responsible-stage render-slot validation, highlight-protected mids exposure-placement candidate validation, parameter score-component validation, candidate outcome-learning record validation, rendered metric-stability convergence validation, rendered-feedback trend convergence validation, finish-tone probe validation, active refine-intent relevance validation, spatial-risk routing, localized hotspot rejection, rendered ensemble merge validation, `RenderedFeedbackLoopV1` state validation, bounded RawDevelop stage snapshot cache reuse, rendered stage-cache validation, and stage-scheduler validation. | Candidate render metrics are compact probes of selected/surviving authored states, not final gallery UI, not full spatial/color maps, not skin or memory-color protection, and not final-image pixel blending. Rendered metrics can now compare coarse local brightness/contrast/damage-risk shape, compact color-cast/channel-balance shape, and selected-baseline/active-repair relative deltas across a more diverse rendered candidate set, cluster near-duplicates while preserving final-masked pre-finish-distinct survivors, reuse the final render's cached pre-finish boundary for metrics when available, expose cache-hit telemetry, stage-cache validation, `StageSchedulerV1` ordering diagnostics, bounded stage snapshot reuse for raw-base/pre-finish graph boundaries, and a compact `RenderedFeedbackLoopV1` state record tying pass budget, action, next step, stop reason, and convergence evidence together. Stale-snapshot cancellation skips obsolete feedback only at safe worker boundaries after the current graph operation returns; it does not interrupt an in-flight shader. There is still no user-visible multi-pass convergence engine, gallery, full physical staged controller, sidecar stats bus, full perceptual comparison model, or user preference-learning system. Trigger hashes still intentionally avoid live tone stats to prevent oscillation. |
| 04 - Dynamic Range, Highlights, Shadows, and Local Exposure Strategy | Partial | 2026-06-13 | `src/Editor/EditorRenderWorker.h`; `src/Editor/EditorRenderWorker.cpp`; `src/Editor/EditorModule.cpp`; `src/Editor/Internal/EditorModuleRendering.cpp`; `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/main.cpp` | Added `DynamicRangeStrategyV1` diagnostics for Auto Develop; records highlight importance, shadow readability, noise constraint, range compression, brightness-hierarchy risk, highlight-gray risk, meaningful-highlight pressure, local-EV conflict, local exposure damage-profile evidence, local halo guard need, natural-contrast guard need, bright-highlight rolloff need, highlight-brightness anchor need, broad-highlight guard need, shadow-readability lift need, specular-highlight tolerance need, shadow noise-floor need, and small-specular-clipping allowance; added `DynamicRangeStrategyMapV1` solver coordinates and `LocalExposureStrategyV1` local exposure contract; Auto status shows strategy, strategy-map, compact local exposure, and compact regional evidence including gray-highlight, meaningful-highlight structure evidence, local-EV conflict/spread evidence, local exposure damage profile, broad-highlight pressure, luminous-highlight pressure, readable-shadow pressure, separation pressure, specular pressure, shadow-floor pressure, and halo-safety pressure; `LocalExposureStrategyV1` authors Scene Prep guardrails and is carried into candidate Scene Prep probes; added `brightHighlightRolloff`, `luminousHighlightAnchor`, `naturalContrastGuard`, and `specularHighlightTolerance` as finish-tone candidates that preserve RAW and Scene Prep during candidate renders; added `DynamicRangeRegionEvidenceV1` from compact rendered local metrics plus explicit rendered highlight-band grayness, meaningful-highlight structure, local-EV spread/conflict metrics, and local exposure damage-profile metrics; added `broadHighlightGuard` as a scene-prep-only candidate for broad meaningful bright regions; added `shadowReadabilityLift` as a scene-prep-only candidate for locally opening clean/readable shadows while preserving RAW placement; added `localRangeGuard` as a scene-prep-only candidate for regional highlight/shadow/halo/local-EV pressure; added `shadowNoiseFloor` as a scene-prep-only candidate for holding noisy/low-value dark regions darker; added `haloSafeLocalRange` as a scene-prep-only candidate for backing away from aggressive local EV moves while raising halo/smooth-gradient/edge guardrails; score diagnostics include `brightnessHierarchy`, `highlightGrayRisk`, `meaningfulHighlightControl`, `luminousHighlightAnchor`, `naturalContrastGuard`, `broadHighlightControl`, `shadowReadabilityLift`, `specularTolerance`, `localHaloSafety`, local exposure damage safety, local range fit, local-EV conflict, and shadow/noise risk; rendered feedback handles previous-fingerprint regional-evidence updates, highlight-gray and structured broad-highlight regressions/refinements, and repeated same-adoption stops. | Full highlight/shadow/local-exposure strategy redesign, true spatial maps/overlays, graph controls, subject-aware range priority, full local exposure scoring, full clipped-data reconstruction, deeper Scene Prep/local exposure renderer redesign, and the Guide 08 tone redesign. | Latest run: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; `git diff --check -- src/Editor/EditorRenderWorker.h src/Editor/EditorRenderWorker.cpp src/Editor/EditorModule.cpp src/Editor/Internal/EditorModuleRendering.cpp src/Editor/Internal/EditorModuleRawUI.cpp src/main.cpp` reported no whitespace errors, only line-ending warnings. | Current strategy is compact and heuristic over existing stats/rendered feedback. It shapes visible range, natural separation, broad-highlight guarding, meaningful-highlight structure pressure, local-EV conflict pressure, local exposure damage profile, gray-highlight avoidance, readable-shadow lifting, highlight rolloff, highlight brightness feeling, tiny-specular tolerance, shadow-floor pressure, anti-halo local range safety, and coordinated Scene Prep local exposure honestly; it does not recover missing clipped data, and it is not yet true spatial local-exposure mapping, subject-aware shadow importance, or denoise redesign. |
| 05 - Subject Importance, User-Guided Bias Brushes, and Scene Understanding | Partial | 2026-06-13 | `src/Editor/NodeGraph/EditorNodeGraph.h`; `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`; `src/Editor/EditorModule.h`; `src/Editor/EditorModule.cpp`; `src/Editor/EditorRenderWorker.h`; `src/Editor/EditorRenderWorker.cpp`; `src/Editor/Internal/EditorModuleRendering.cpp`; `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/Editor/UI/EditorViewport.h`; `src/Editor/UI/EditorViewport.cpp`; `src/main.cpp` | Added first solver substrate for subject/scene importance: compact rendered subject/scene metrics, `SubjectSceneIntentV1` diagnostics, top-level `autoSubjectScene*` aliases, subject/scene score dimensions/risks, conservative candidate scoring bias, and compact Auto status readouts. Added user-facing Auto intent axes, `subjectSceneBias` and `moodReadabilityBias`, plus named Scene Prep probes `subjectReadableMids` and `sceneMoodPreservation`. Added persisted `DevelopSubjectImportanceMap` region and stroke data with stable modes, active-region and active-stroke continuity, graph JSON serialization/defaults/fallbacks, Auto UI region controls, brush edit/reduce/mode/size/strength/soft-edge controls, viewport overlay controls, direct single-output viewport select/move/resize region editing, freehand-style normalized stroke painting, brush cursor/stroke overlay drawing, basic per-stroke select/enable/delete/retune controls, trigger/fingerprint inclusion for solver-facing region/stroke edits, region/stroke summary scoring/diagnostics, and validation that changing regions or strokes affects candidate context. Added compact subject-marked rendered candidate metrics: bounded region/stroke sampling in candidate requests, final/pre-finish marked-region pixel analysis, JSON readback, rendered scoring, relative comparison, duplicate distance, and validation for positive/reveal and low-priority marked feedback. Added `SubjectImportanceMapV1`, a compact 5x5 interpreted solver grid from existing regions/strokes with map diagnostics, score-component signals, top-level aliases, Auto status readout, a selected-Develop viewport diagnostic overlay, graph serialization for map display settings, and validation for region, brush, disabled-stroke, reduce/ignore-stroke, viewport-state, and visual-only fingerprint behavior. Added `SubjectRefinedMapV1`, a compact refined importance/confidence map derived from interpreted cells, neighbor support, and solved subject/readability/protection/mood axes; it writes top-level diagnostics, candidate score-component signals, a status readout, and selected-node viewport overlay controls. Added `SubjectImportanceSolveNotesV1`, compact note diagnostics inside `SubjectSceneIntentV1`, candidate score components, top-level `autoSubjectSceneSolveNotes*` aliases, and Auto status readout so subject/importance bias is explainable. | True image-edge refinement of subject maps, edge visualization, refined edge-aware visual maps, stroke point/history editing beyond whole-stroke controls, AI/ML subject detection, semantic face/person detection, candidate gallery UI, graph-style Subject / Scene Intent Map UI, and Manual/Auto subject-bias handoff. | Latest run: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed. | Finish Mask still masks integrated tone only; it has not been relabeled or reused as a subject brush. Automatic subject/scene evidence plus region/stroke guidance, compact interpreted map substrate, compact refined-map diagnostics, compact viewport overlays, compact solve notes, and compact subject-marked rendered metrics are solver-bias controls/diagnostics, not AI/semantic detection, a hard subject map, an image-edge-refined visual map, or a Manual/Auto handoff system. |
| 06 - Color, White Balance, Mood Preservation, Skin, and Memory Colors | Partial | 2026-06-11 | `src/Editor/EditorModule.cpp`; `src/Editor/EditorRenderWorker.cpp`; `src/Editor/EditorRenderWorker.h`; `src/Editor/Internal/EditorModuleRendering.cpp`; `src/main.cpp` | First-pass RAW white-balance candidate probes exist in the Guide 03 candidate pipeline: daylight correction, neutral correction, and camera-mood probe mapping, with metadata-gated generation, render-payload RAW WB overrides, score dimensions for color plausibility and mood color preservation, and diagnostics. Rendered candidate metrics now also include compact mean-RGB/channel-balance/color-cast evidence used by rendered scoring, duplicate distance, JSON readback, and conservative extreme-cast rejection. | Full color/WB strategy, mood preservation beyond coarse WB probes and compact cast evidence, skin/memory color protection, color graph controls, perceptual color scoring, and color-management/camera-profile redesign. | `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed; `build\Stack.exe --validate-develop-node-smoke` passed. | This is a narrow WB/rendered-color-evidence increment, not completion of Guide 06. Existing camera transform remains approximate per context document. |
| 07 - Denoise, Demosaic, Texture, Detail, and Cleanup | Not Started | 2026-06-10 | None | None. | Pre/post demosaic denoise redesign, advanced demosaic selection, texture/detail overhaul. | Not run. | Current public demosaic path is effectively bilinear only. |
| 08 - Tone, Contrast, Finish Feel, and Realistic Final Rendering | Partial | 2026-06-21 | `src/Editor/LayerRegistry.cpp`; `src/Editor/Layers/ToneLayers.cpp`; `src/Editor/Layers/ToneCurveLayerModel.cpp`; `src/Editor/Layers/ToneCurveLayerAuto.cpp`; `src/Editor/Layers/ToneCurveLayerUI.cpp`; `src/Editor/Layers/ToneCurveLayerSerialization.cpp`; `src/Editor/Layers/ToneLayerRendering.cpp`; `src/main.cpp` | Restored standalone `Tone Curve` as a first-class addable manual node for scene-linear workflows. The lean graph surface keeps the manual curve editor, point editing/context menu behavior, channel mode buttons, curve domain control, reset actions, and existing canvas targeting, while removing legacy standalone auto/baseline/foundation/local-mask sections and legacy standalone ownership messaging. Tone Curve point/model/evaluation code now lives in `ToneCurveLayerModel.cpp`; auto calibration, scene analysis, auto intent solving, authored-state preservation, and auto rewrite feedback now live in `ToneCurveLayerAuto.cpp`; standalone and Develop-integrated ImGui surfaces now live in `ToneCurveLayerUI.cpp`; JSON persistence now lives in `ToneCurveLayerSerialization.cpp`; shader sources, GL execution, passthrough, and LUT upload now live in `ToneLayerRendering.cpp`; effective tone/foundation settings remain in `ToneLayers.cpp`. | Full integrated finish-tone strategy, highlight shoulder/shadow toe redesign, mode-specific final tone graph controls, and broader Guide 08 tone redesign inside `Develop`. | Latest organization validation: `$env:CL='/FS'; cmake --build build_codex_verify --config Debug --target StackGraphBehaviorTests Stack --parallel 6` passed; `build_codex_verify\StackGraphBehaviorTests.exe` passed; `build_codex_verify\Stack.exe --validate-layer-registry` passed; `build_codex_verify\Stack.exe --validate-develop-node-smoke` passed; `build_codex_verify\Stack.exe --validate-tone-curve-auto` passed; `build_codex_verify\Stack.exe --validate-develop-auto-solve` passed. Earlier behavior validation: `cmake --build build --config Debug --target StackGraphBehaviorTests Stack` passed; `build\StackGraphBehaviorTests.exe` passed; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-tone-curve-auto` passed. | This is real manual tone-curve substrate plus organization-only Tone ownership cleanup, not completion of the broader Guide 08 finish-tone redesign. `Develop` still uses its existing integrated tone path. |
| 09 - Manual Mode, Auto Handoff, Bias Preservation, and Expert Controls | Not Started | 2026-06-15 | None | None. | Manual edits becoming persistent mathematical Auto biases, locks, advanced handoff controls. | Not run. | A separate graph-side manual chain now exists via `RAW Decode -> Tone Curve -> View Transform`, but `Develop` still has no Manual-to-Auto bias preservation, locks, or expert handoff controls. |
| 10 - Graph Controls, Solver Controls, Diagnostics, and User Education | Not Started | 2026-06-10 | None | None. | Graph-style intent controls, candidate diagnostics, requested-vs-achieved explanations, learning toggles. | Not run. | Graph controls remain an explicit future direction, not reduced to sliders in this pass. |

## Requirement Tracking Rules

- Before implementing any numbered guide, add a requirement checklist for that guide in this file.
- Each checklist item must be marked `Complete`, `Partial`, `Deferred`, `Not Started`, or `Not Applicable`.
- Do not mark a guide `Complete` in the guide table until its checklist, code locations, validation, decisions, and any deferred items are updated.
- If a future pass discovers that a completed checklist item no longer matches the code, change it to `Partial` and document the mismatch in Notes / risks before editing.
- If a future pass inherits an older acceptance list, update the checklist to match shipped behavior instead of reimplementing work that is already marked `Complete` or `Partial`.

## Guide 01 Requirement Checklist

| Requirement | Status | Evidence / code or doc location | Notes |
|---|---|---|---|
| Add durable Auto processing intent enum with Natural Finished, Clean Base, Flat Editing Base, Bright Natural, Dark Natural, Punchy / High Contrast, and Maximum Range / Detail. | Complete | `src/Editor/NodeGraph/EditorNodeGraph.h` | Stored as `DevelopAutoIntent` inside `DevelopAutoGuidance`. |
| Default new Develop nodes to Natural Finished. | Complete | `DevelopAutoGuidance` default in `src/Editor/NodeGraph/EditorNodeGraph.h` | Natural Finished is the default constructed intent. |
| Preserve existing `RawDevelop` identity, serialized kind, render kind, and user-visible `Develop` title. | Complete | `AGENTS.md`; `docs/engineering/develop/DEVELOP_SOURCE_MAP.md` | No identity rename was made. |
| Keep existing Auto / Manual `uiMode`; add intent inside Auto instead of replacing Auto / Manual. | Complete | `src/Editor/NodeGraph/EditorNodeGraph.h`; `src/Editor/Internal/EditorModuleRawUI.cpp` | Intent lives in Auto guidance; Manual remains a UI/workflow mode. |
| Serialize Auto intent using stable strings. | Complete | `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp` | `developAutoGuidance.autoIntent` stores stable values such as `NaturalFinished`. |
| Load old graphs without intent as Natural Finished. | Complete | `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`; `src/main.cpp` | Covered by `ValidateDevelopAutoIntentSerialization`. |
| Unknown serialized intent strings fall back safely to Natural Finished. | Complete | `src/Editor/NodeGraph/EditorNodeGraph.h`; `src/main.cpp` | Covered by `ValidateDevelopAutoIntentSerialization`. |
| Add Auto Mode / Intent UI selector with human-readable labels. | Complete | `src/Editor/Internal/EditorModuleRawUI.cpp` | Labels match the accepted product names. |
| Add concise mode help / tooltip text. | Complete | `src/Editor/NodeGraph/EditorNodeGraph.h`; `src/Editor/Internal/EditorModuleRawUI.cpp` | Descriptions are plain language and avoid false recovery claims. |
| Make current Auto solve mode-aware without adding a new solver. | Complete | `src/Editor/EditorModule.cpp` | `ResolveDevelopAutoIntentProfile` and `BuildModeAwareDevelopGuidance` bias existing solve pathways. |
| Allow modes to coordinate multiple hidden settings across RAW, scene prep, cleanup, and integrated tone guidance. | Complete | `src/Editor/EditorModule.cpp` | Profiles affect exposure, dynamic range, shadow/highlight behavior, contrast, scene prep, denoise/cleanup bias, and tone JSON guidance. |
| Keep mappings conservative and documented. | Complete | `src/Editor/EditorModule.cpp`; `docs/engineering/develop/DEVELOP_DECISIONS.md` | Each profile has short intent comments. |
| Reset Auto behavior is intentional and documented. | Complete | `src/Editor/Internal/EditorModuleRawUI.cpp`; `docs/engineering/develop/DEVELOP_DECISIONS.md` | Reset Auto resets numeric guidance and preserves selected intent. |
| Auto Calibrate re-solves using the selected mode. | Complete | `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/Editor/EditorModule.cpp` | Calibrate routes through `UpdateDevelopAutoState` / `ApplyDevelopAutoSolve` with current intent. |
| Changing mode forces meaningful reanalysis / re-solve. | Complete | `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/Editor/EditorModule.cpp` | Intent changes force full reanalysis and are included in trigger hashes. |
| Auto status readout includes selected Auto mode. | Complete | `src/Editor/Internal/EditorModuleRawUI.cpp` | Shows `Auto mode: <label>` when stats are available. |
| Add practical validation for defaults, serialization, legacy load, unknown fallback, mode-aware solve, and Auto/Manual preservation. | Complete | `src/main.cpp` | Covered by build and `--validate-develop-node-smoke`; Auto/Manual remains represented by unchanged `uiMode` and Manual non-goal docs. |
| Create or update root `AGENTS.md` with Develop guidance. | Complete | `AGENTS.md` | Includes source-of-truth docs, tracker, source map, pass protocol, and deferred scope. |
| Create implementation tracker for all ten guides. | Complete | `docs/engineering/develop/DEVELOP_IMPLEMENTATION_TRACKER.md` | Guide table is present for 01-10. |
| Create source map for important Develop code paths. | Complete | `docs/engineering/develop/DEVELOP_SOURCE_MAP.md` | Includes required areas from payload to render path. |
| Create decisions file with Guide 01 decision record and non-goals. | Complete | `docs/engineering/develop/DEVELOP_DECISIONS.md` | Includes rationale, modes, non-goals, and tradeoffs. |
| Do not implement candidate gallery, graph controls, subject brush, learning, convergence, denoise overhaul, color graph, or View Transform changes. | Complete | `docs/engineering/develop/DEVELOP_DECISIONS.md`; `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` | Deferred features are now centralized in deferred scope. |

## Do Not Re-Implement / Already Done

- Do not add a second Develop Auto intent enum or parallel mode field. Use `DevelopAutoGuidance::intent`.
- Do not rename `RawDevelop`, `RenderGraphNodeKind::RawDevelop`, serialized kind `"RawDevelop"`, or the user-visible title `Develop`.
- Do not replace Auto / Manual with the new mode selector. Auto intent is inside Auto.
- Do not add a second candidate framework when tuning Guide 01 mode profiles. Use the Guide 03 parameter-candidate foundation in `ApplyDevelopAutoSolve`, the authored `renderedFeedbackMerge` / `renderedFeedbackRefine` paths, and the worker-side `RenderMetricsV1` candidate metric path; keep rendered gallery UI tracked as deferred until it exists.
- Do not claim Maximum Range / Detail recovers missing clipped data. It fits/compresses visible range and uses existing highlight/shadow protections.
- Do not mark any later guide Complete until its own requirement checklist, code changes, validation, and decision notes are present.

## Guide 02 Requirement Checklist

| Requirement | Status | Evidence / code or doc location | Notes |
|---|---|---|---|
| Preserve the current scene-linear RAW exposure math: +1 EV means multiply by 2, -1 EV means multiply by 0.5. | Complete | `src/Raw/RawGpuPipeline.cpp`; `src/Editor/Internal/EditorModuleRawUI.cpp` | Render math remains `pow(2, exposureStops)` / `std::exp2`; Manual help explains the multiplication. |
| Make Auto UI distinguish rendered brightness intent from literal RAW/data exposure. | Complete | `src/Editor/Internal/EditorModuleRawUI.cpp` | Auto slider now displays `Brightness Intent` and tooltip says Auto may coordinate RAW EV, local exposure, and tone. Serialized field remains `exposureBias` for compatibility. |
| Make Manual UI expose literal RAW EV with precise naming and help text. | Complete | `src/Editor/Internal/EditorModuleRawUI.cpp` | Manual slider now displays `RAW Exposure / EV`, with tooltip and scene-linear scale readout. |
| Add readout/diagnostics that show how Auto distributed the solve across RAW EV, scene prep/local exposure, and tone guidance. | Complete | `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/Editor/EditorModule.cpp` | Auto status shows brightness intent, authored RAW EV/scale, local EV min/max bias, tone contrast, and tone placement when available. |
| Keep Auto controls solver-facing and allowed to coordinate RAW EV, local exposure, and tone together. | Complete | `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/Editor/EditorModule.cpp` | Stage-map text and tone telemetry describe coordinated behavior; no one-slider/one-setting restriction was added. |
| Forward explicit brightness/exposure telemetry for integrated tone diagnostics/future use. | Complete | `src/Editor/EditorModule.cpp`; `src/main.cpp` | Tone JSON now receives `autoBrightnessIntent` and `autoRawExposurePreferenceEv`; smoke validation checks both. |
| Add first-pass clipping/noise/HDR exposure diagnostics from existing scene/tone stats. | Complete | `src/Editor/EditorModule.cpp`; `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/main.cpp` | Tone JSON now records diagnostic clipping ratio, highlight pressure, noise risk, HDR spread, recommended base EV, authored RAW EV/scale, and local EV biases; Auto status displays the key values. |
| Avoid implementing candidate exposure placements or graph-style exposure/range controls in this pass. | Complete | `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` | Candidate solving remains Guide 03; graph controls remain Guide 10. |
| Keep Develop output scene-linear and View Transform downstream. | Complete | `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`; no render architecture changes | No changes were made to View Transform or Develop output architecture. |
| Add or update practical validation for exposure/brightness naming and/or solve distribution behavior. | Complete | `src/main.cpp` | Validation covers telemetry; UI text itself remains manually reviewed. |
| Add a separate `RAW Decode` node for manual RAW-to-scene-linear work without changing `Develop` identity. | Complete | `src/Editor/NodeGraph/EditorNodeGraph.h`; `src/Editor/NodeGraph/EditorNodeGraph.cpp`; `src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp`; `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`; `src/Renderer/MaskRenderTypes.h`; `src/Renderer/RenderPipeline.cpp`; `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/main.cpp` | `RAW Decode` reuses the shared RAW base render path and exposes only decode essentials. It does not add Auto guidance, scene prep, integrated finish tone, or a second `Develop` identity. |
| Make RAW-source `Add full tree` build the manual chain `RAW Source -> RAW Decode -> Tone Curve -> View Transform -> Output`. | Complete | `src/Editor/EditorModule.cpp`; `src/main.cpp` | Manual RAW workflows now default to the explicit scene-linear chain instead of dropping straight into the merged auto `Develop` node. |
| Implement full coupled global/local exposure solver with candidate placements. | Deferred | `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` | Broader solver work belongs to later Guide 02/03 passes. |
| Implement graph-style Exposure / Range Intent control. | Deferred | `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` | Graph controls belong to Guide 10. |
| Implement spatial exposure maps, visual damage metrics, and candidate comparison diagnostics. | Deferred | `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` | Requires later Guide 02/03/10 work; current diagnostics are numeric readouts from existing stats. |

## Guide 03 Requirement Checklist

| Requirement | Status | Evidence / code or doc location | Notes |
|---|---|---|---|
| Use convergence evidence to tune rendered-feedback admission thresholds. | Partial | `ResolveDevelopConvergenceAdmissionPolicy`; `ApplyRenderedCandidateFeedbackToSolve`; `BuildDevelopAutoConvergenceEvidenceRecord`; `WriteDevelopAutoCandidateSolveDiagnostics`; `IsDevelopRenderedFeedbackStopConvergedReason`; `ValidateDevelopAutoSolveBehavior` | `ConvergenceAdmissionV1` now tightens the minimum rendered-score improvement required after a continued non-refine solve is waiting for fresh rendered metrics. Marginal improvements that clear the base threshold but not the continuation threshold stop with `convergenceAdmissionNoMeaningfulImprovement`, and the admission policy is recorded both as top-level `autoCandidateConvergenceAdmission*` fields and inside `autoCandidateConvergenceEvidence.admission`. This is a first controller decision from convergence evidence; broader stage-specific admission remains future Guide 03 work. |
| Use convergence evidence to tune candidate render request budgets. | Partial | `ResolveDevelopAdaptiveRenderBudgetPolicy`; `BuildDevelopCandidateRenderRequests`; `EditorRenderWorker::DevelopCandidateRenderRequest`; `EditorRenderWorker::DevelopCandidateRenderResult`; `ApplyDevelopCandidateRenderFeedback`; `ValidateDevelopAutoSolveBehavior` | `AdaptiveRenderBudgetV1` now narrows late, non-targeted awaiting-metrics continuation passes to three focused candidate renders when `ConvergenceEvidenceV1` is still waiting for fresh metrics and `ConvergenceAdmissionV1` is already tightened. Active refine, responsible-stage, adoption, and merge validation can still expand to six renders. Diagnostics write narrowed/convergence fields per candidate render plus top-level `autoCandidateRenderedAdaptiveBudgetNarrowed*` and `autoCandidateRenderedAdaptiveBudgetConvergence*` fields. Broader candidate-family suppression remains future Guide 03 work. |
| Stop obsolete in-flight rendered feedback when a newer editor snapshot is queued. | Complete | `EditorRenderWorker::Submit`; `EditorRenderWorker::ShouldAbortStaleSnapshot`; `EditorModule::SubmitRenderIfReady`; `ValidateDevelopAutoSolveBehavior` | A dirty single-output render can now submit a replacement snapshot while the worker is busy. The worker abandons stale Develop candidate feedback and preview-like background work once the current graph operation returns, skips publishing superseded main/layer-stack textures, and validation covers the abort decision. This is safe-boundary cancellation, not interruptible shader execution or the full future background queue. |
| Suppress expensive candidate render admission while Develop edits are still inside the quiet window. | Complete | `ShouldDeferDevelopCandidateRenderRequest`; `BuildDevelopCandidateRenderRequests`; `ScheduleDeferredDevelopCandidateFeedback`; `RefreshDeferredDevelopCandidateFeedbackIfReady`; `ValidateDevelopAutoSolveBehavior` | Candidate feedback probes are no longer scheduled during the same 0.60s recent-edit quiet window that gates feedback application. The main viewport render remains free to update, and the node is re-dirtied for one fresh feedback pass after edits settle. This reduces wasted GPU work during slider/intent edits but is not a full background queue or interruptible shader cancellation. |
| Expose deferred candidate-feedback waiting state in Auto status. | Complete | `DevelopCandidateFeedbackQuietRemainingSecondsForValidation`; `GetDevelopCandidateFeedbackDeferredStatus`; `RenderRawDevelopControls`; `ValidateDevelopAutoSolveBehavior` | The Auto status panel now reports when candidate feedback is waiting for recent edits to settle and shows the remaining quiet-window time, then reports the queued state once quiet. This is compact progress/admission visibility, not the full Guide 10 diagnostics UI, candidate timeline, or gallery. |
| Name active candidate-feedback probes in the render progress HUD. | Complete | `BuildDevelopCandidateProgressLabelForValidation`; `EditorRenderWorker::RenderSnapshot`; `ImGuiExtras::RenderProgressOverlay`; `ValidateDevelopAutoSolveBehavior` | The existing non-blocking render progress HUD now includes the candidate label and revision stage while measuring Develop feedback probes. This is active-probe visibility, not a candidate queue timeline, thumbnail gallery, or requested-vs-achieved explanation panel. |
| Reduce avoidable render-graph copying in the candidate feedback worker. | Complete | `EditorRenderWorker::RenderSnapshot`; `src/Editor/EditorRenderWorker.cpp` | The candidate loop no longer copies the full `RenderGraphSnapshot` just to check whether the Develop node exists. It scans the source snapshot directly, then still creates the required mutable copy only inside the actual candidate render path. This reduces per-candidate CPU memory/copy churn but does not yet pool candidate graph setup or downscale graph execution. |
| Reuse one mutable candidate graph for final and pre-finish fallback candidate renders. | Complete | `EditorRenderWorker::RenderSnapshot`; `src/Editor/EditorRenderWorker.cpp` | Each candidate request now applies its copied `RawDevelop` payload once and pre-attaches distinct final/pre-finish synthetic outputs. The final render and pre-finish fallback render switch the graph output id instead of rebuilding the candidate graph helper path for each socket. This reduces repeated graph setup/copying but still keeps one isolated mutable graph copy per candidate request. |
| Record candidate render timing telemetry for performance diagnosis. | Complete | `DevelopCandidateRenderResult`; `EditorRenderWorker::RenderSnapshot`; `ApplyDevelopCandidateRenderFeedback`; `RenderRawDevelopControls` | `CandidateRenderTimingV1` records graph, readback, analysis, pre-finish, total, and slowest-candidate timing for rendered feedback probes. Auto status shows compact timing totals. This is diagnostic telemetry, not a full profiler UI, queue timeline, or automatic scheduling policy. |
| Bound rendered candidate metric readback on large RAWs. | Complete | `ResolveDevelopCandidateMetricReadbackMaxDimension`; `RenderPipeline::GetOutputPixels(..., maxDimension)`; `RenderPipeline::GetCachedGraphImagePixels(..., maxDimension)`; `DevelopCandidateRenderRequest::metricReadbackMaxDimension`; `DevelopCandidateRenderResult::metricsReadbackDownsampled`; `ApplyDevelopCandidateRenderFeedback`; `RenderRawDevelopControls`; `ValidateDevelopAutoSolveBehavior` | Candidate feedback still renders through the real graph, but final/pre-finish diagnostic readback and CPU metric analysis are capped above 16 MP. Diagnostics record the cap and downsample counts, and Auto status shows capped metric readback when active. This reduces per-candidate CPU/RGBA buffer pressure but is not a full downscaled render engine, GPU texture profiler, gallery, or sidecar scheduler. |
| Bound RawDevelop stage snapshot texture residency on large RAWs. | Complete | `RenderPipeline::EstimateRawDevelopStageCacheTextureBytesForValidation`; `RenderPipeline::ResolveRawDevelopStageCacheMaxEntriesForValidation`; `RenderPipeline::ShouldCacheRawDevelopStageTextureForValidation`; `RenderPipeline::StoreRawDevelopStageCacheEntry`; `ValidateDevelopAutoSolveBehavior` | Raw-base/pre-finish stage snapshots are still cloned owned boundaries, but retention is now size-aware: small snapshots keep more MRU fingerprints, large snapshots keep fewer, oversized single snapshots are skipped, and the total RawDevelop stage snapshot cache trims to a soft estimated-byte budget. This reduces GPU texture residency during candidate churn; it is not real GPU memory profiling or downscaled graph execution. |
| Read Guide 03 and related processing guidance before implementation. | Complete | `03_Iterative_Auto_Solve_Candidate_Rendering_Convergence_and_Learning.txt`; related Guide 02/04/05/06/07/08/09/10 passages | Guide 03 is the active source for candidate solving, convergence, merging, and learning; related guides provide scoring dimensions and ownership boundaries. |
| Keep Auto as a solver that explores possible authored states, not a preset stack. | Complete | `src/Editor/Internal/EditorModuleDevelopCandidateGeneration.h/.cpp`; `src/Editor/Internal/EditorModuleDevelopAutoSolveApplication.h/.cpp`; `src/Editor/Internal/EditorModuleDevelopAutoSolveApplicationContext.h/.cpp`; `src/Editor/Internal/EditorModuleDevelopAutoSolveRawApplication.cpp`; `src/Editor/Internal/EditorModuleDevelopAutoSolveScenePrepApplication.cpp`; `src/Editor/Internal/EditorModuleDevelopAutoSolveToneApplication.cpp`; `src/Editor/Internal/EditorModuleDevelopAutoSolve.cpp` | Candidate generation is inside `BuildDevelopAutoCandidateSolve`, called by `ApplyDevelopAutoSolve`, and selected-solve application feeds the existing authored RAW, Scene Prep, and integrated Tone path through focused selected-solve stage owners. |
| Generate image-driven candidate families for exposure placement, dynamic range, tone, noise/detail, color/WB, subject priority, and mode-neighbor alternatives. | Partial | `BuildDevelopAutoCandidateSolve`; `TryResolveDevelopWhiteBalanceProbeMode`; `IsDevelopWhiteBalanceProbeCandidateId`; `PreferredRenderedRefineCandidateId`; `IsRenderedLocalRefineCandidateId`; `IsDevelopModeNeighborCandidateId`; `IsDevelopFinishToneProbeCandidateId` in `src/Editor/Internal/EditorModuleDevelopCandidateGeneration.h/.cpp`; `src/Editor/Internal/EditorModuleDevelopCandidateScoring.h/.cpp` | Implemented current-stat-supported families: base solve, protect highlights, `highlightProtectedMids` exposure placement for lower RAW/global placement with local midtone support, brighter mids, more range, preserve mood, more contrast, RAW white-balance probes (`wbDaylightCorrection`, `wbNeutralCorrection`, with `wbCameraMood` mapping for carried/diagnostic states), finish-tone probes (`toneSofterRolloff`, `tonePunchierShape`, `toneFlatterEditing`, `toneDarkerToe`), cleaner shadows, preserve texture, and mode-neighbor probes such as Natural More Range, Natural Brighter Mids, Natural More Contrast, Bright Highlight Safe, Dark Readable Mids, Punchy Safer Range, Range Natural Shape, Flat Natural Shape, and Clean Texture Check. Rendered mismatch can also seed authored rendered-local families for brighter mids, shadow opening, highlight restraint, contrast shaping, cleaner shadows, and texture preservation. Full Guide 06 color/WB/mood/skin/memory-color strategy, subject-priority brush/detection, and advanced denoise/detail candidate engines remain deferred to Guides 05-07. |
| Score candidates across multiple dimensions instead of one universal score. | Partial | `ScoreDevelopAutoCandidate`; `ResolveDevelopContinuationCandidateBiasProfile`; `DevelopContinuationCandidateBiasBonus` in `src/Editor/Internal/EditorModuleDevelopCandidateScoring.h/.cpp`; `BuildDevelopAutoCandidateScoreComponents` in `src/Editor/Internal/EditorModuleDevelopCandidateScoreComponents.h/.cpp`; `AnalyzeDevelopCandidatePixels`; `ScoreDevelopRenderedCandidateMetrics`; `CompareDevelopRenderedCandidateToSelected`; `ResolveDevelopRenderedRefineIntent` in `src/Editor/Internal/EditorModuleDevelopRenderedCandidateScoring.h/.cpp`; `src/main.cpp` validation | Parameter scoring uses highlight pressure, clipping, HDR spread, darkness/shadow rescue, noise risk, texture confidence, flat-scene need, mode intent, nearby-mode tradeoff fit, first-pass WB color tradeoffs, and now a bounded `ContinuationCandidateBiasV1` bonus when the previous rendered continuation says a responsible stage or active refine intent needs another rendered pass. Each parameter candidate now writes `ParameterScoreComponentsV1` dimensions for midtone placement, highlight integrity, shadow cleanliness, dynamic range fit, contrast shape, noise/texture quality, local artifact safety, mode-intent fit, rendered-continuation fit, color plausibility, mood color preservation, and candidate uniqueness, plus risk terms for highlight damage, shadow noise, flattening, and data-risk penalty. Rendered candidate metrics now capture mean/median/p10/p90 luma, shadow/highlight/clipped fractions, contrast span, mean saturation, low-saturation fraction, edge contrast, halo-risk fraction, shadow texture risk, compact color-cast/channel-balance evidence, 3x3 local mean luma, local contrast span, local luma spread, local highlight/shadow pressure, local damage-risk score/mean/peak/peak tile, and center-region luma/pressure, and can drive capped rendered-feedback candidate adoption, authored merge, or damped refinement. `RenderedRelativeComparisonV1` adjusts rendered survivor scores against the selected baseline and active repair intent, so a candidate that protects highlights, opens shadows, brightens mids, adds contrast, cleans shadows, or preserves texture can outrank a higher standalone score that regresses the repair target. Local rendered metrics, including spatial damage-risk hotspots, now steer refinement intent, not only ranking/deduplication. This is still not a full perceptual/spatial scoring pass or user-visible gallery. |
| Let rendered continuation influence candidate-family generation, not only scoring. | Partial | `BuildDevelopAutoCandidateSolve`; `ResolveDevelopContinuationCandidateBiasProfile`; `WriteDevelopAutoCandidateSolveDiagnostics`; `BuildDevelopAutoCandidateScoreComponents`; `BuildFallbackDevelopAutoCandidateScoreComponents`; `ValidateDevelopAutoSolveBehavior` | `ContinuationCandidateExpansionV1` adds missing stage-relevant authored candidate families when `RenderedContinuationV1` says the next pass should continue and the first-pass stats gates did not generate those probes. Current mappings cover RAW/global highlight placement, scene-prep range/readability, finish-tone shape, and RAW cleanup/texture checks. Diagnostics distinguish expansion eligibility from actual added candidate count and write per-candidate `continuationExpansion*`, `scoreComponents.renderedContinuationExpansion`, and `dimensions.renderedContinuationCoverage`. This remains a bounded hook, not the full adaptive candidate generator. |
| Remove damaged candidates and cluster/reject near-duplicates. | Partial | `RejectDevelopAutoCandidateForDamage`; `DevelopAutoCandidateDistance` in `src/Editor/Internal/EditorModuleDevelopCandidateScoring.h/.cpp`; `TryReadRememberedCandidateRejection`; `TryReadRememberedRenderedCandidateRejection` in `src/Editor/Internal/EditorModuleDevelopCandidateGeneration.h/.cpp`; `AnalyzeDevelopCandidatePixels`; `CompareDevelopCandidateRenderMetrics` in `src/Editor/EditorRenderWorker.cpp`; `ClassifyDevelopRenderedCandidateDamage` in `src/Editor/Internal/EditorModuleDevelopRenderedCandidateScoring.h/.cpp`; `EvaluateDevelopRenderedCandidateDuplicate` in `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackAnalysis.h/.cpp`; `ApplyDevelopCandidateRenderFeedback` in `src/Editor/Internal/EditorModuleDevelopCandidateFeedback.cpp` | Parameter damage rejection uses current scalar highlight/noise/flattening risk; duplicate rejection uses authored-guidance distance; bounded rejected-candidate memory suppresses repeated same-context rejected attempts. Rendered scoring now includes compact saturation wash, edge/halo risk, shadow texture pressure proxies, mean RGB/channel-balance/color-cast evidence, coarse 3x3 local brightness/contrast shape, compact 3x3 local damage-risk shape, explicit rendered damage rejection with `renderedRejectedDamage` / `rejectReason`, and metric-distance duplicate clustering before choosing the rendered best representative. Rendered damage rejections now include conservative extreme color-cast/channel-imbalance rejection and write bounded `autoCandidateRenderedRejectionMemory` entries keyed by candidate guidance fingerprint, so the next Auto solve rejects the same authored candidate state from rendered memory instead of retesting it. Rendered duplicate clustering also checks pre-finish metrics, so final-near-duplicate candidates survive when the hidden pre-finish boundary differs enough to matter for stage-aware feedback. True spatial halo/skin/memory-color/detail damage maps remain deferred. |
| Preserve selected candidate as authored state that Manual can reveal. | Partial | `ApplyDevelopAutoSolve`; `ApplyDevelopSelectedAutoSolve`; `ApplyDevelopSelectedRawSettings`; `ApplyDevelopSelectedScenePrepSettings`; `WriteDevelopSelectedSolveToneDiagnostics`; `TryReadDevelopAutoCandidateFromToneJson`; `CollectDevelopRenderedSurvivorCandidateIdsForCarryForward`; `autoCandidateSelectionIsAuthoredState` tone JSON; `src/main.cpp` validation | Selected/merged/refined candidate guidance drives the same RAW, Scene Prep, and Tone authored settings through focused selected-solve stage owners. During active rendered-feedback iteration, prior selected, rendered-best, rendered pair-merge source, and bounded successful rendered-survivor candidates can be rehydrated from tone JSON so actual rendered alternatives remain comparable on the next pass, and rendered-local mismatch families can become the selected authored state. There is not yet a user-selectable candidate history UI in Manual. |
| Prefer merge/re-solve in settings/intent space rather than final pixel blending. | Partial | `BlendDevelopAutoCandidateGuidance`; `TryApplyRenderedEnsembleMergeToSolve`; `BuildDevelopAutoCandidateSolve`; `ApplyRenderedCandidateFeedbackToSolve`; `ApplyDevelopCandidateRenderFeedback` | Close compatible parameter candidates can auto-merge by interpolating authored guidance. Modest rendered-feedback wins can synthesize `renderedFeedbackMerge` from the current selected candidate and rendered-best survivor in authored settings space. Rendered analysis can also suggest `renderedFeedbackPairMerge` between the two strongest non-duplicate rendered survivors, or `renderedFeedbackEnsembleMerge` from three strong, distinct rendered survivors, when the rendered evidence is strong enough to reconcile them as a new authored solve. No final-image pixel blend is used. User-requested merge UI/re-solve is deferred. |
| Add explicit convergence state/memory and avoid oscillation. | Partial | `BuildDevelopAutoCandidateFingerprint`; `BuildDevelopAutoCandidateContextFingerprint`; `BuildDevelopAutoCandidateGuidanceFingerprint`; `autoCandidateConverged`; `autoCandidateConvergencePass`; `autoCandidateConvergenceEvidence`; `BuildDevelopAutoConvergenceEvidenceRecord`; `autoCandidateRejectedMemory`; `autoCandidateRenderedRejectionMemory`; `autoCandidateRenderedContinuationPolicy`; `autoCandidateContinuationBias*`; `autoCandidateRenderedFeedbackPass`; `autoCandidateRenderedFeedbackHistory`; `autoCandidateRenderedStopReason`; `autoCandidateRenderedFeedbackStopReason`; `autoCandidateRenderedRevisionStage`; `autoCandidateRenderedRevisionReason`; `autoCandidateRenderedCarriedForwardCount`; `autoCandidateRenderedStabilityDistance`; `autoCandidateRenderedStabilityStatus`; `autoCandidateRenderedTrendHistoryCount`; `autoCandidateRenderedTrendStatus`; `autoCandidateRenderedMonotonicGuardStatus`; `BuildDevelopRenderedContinuationPolicyRecord`; `IsDevelopRenderedFeedbackStopConvergedReason`; `EvaluateDevelopRenderedFeedbackTrend`; `EvaluateRenderedRefineMonotonicGuard`; `TryReadLastSameIntentRefineMetrics`; `CollectDevelopRenderedSurvivorCandidateIdsForCarryForward`; `WouldRepeatUnhelpfulRenderedRefinement`; `RepeatedRenderedRefinementStopReason`; `RepeatedRenderedChoiceStopReason`; trigger hash comments | Candidate fingerprint/pass metadata is written and stable repeated solves converge. Same-context rejected candidates are remembered and suppressed through bounded `autoCandidateRejectedMemory`; actual rendered-damage rejections are remembered through bounded `autoCandidateRenderedRejectionMemory` keyed by per-candidate guidance fingerprint so the same authored state is not retested after rendered analysis already rejected it. Rendered feedback adoption/merge/refinement is capped and keyed by solve fingerprint; active rendered-feedback iterations preserve prior authored selected/rendered-best/merge/refine and bounded successful rendered-survivor candidates for comparison on the next pass; rendered feedback history records solve-request/stop decisions, refinement reasons, compact selected/best metric snapshots, spatial damage-risk metric snapshots, and the responsible revision stage for the next pass. `RenderedContinuationV1` records the explicit wait/continue/stop policy, next step, pass budget, stage focus, and compact evidence beside `RenderedFeedbackLoopV1`, including pass-limit stops. `ConvergenceEvidenceV1` now summarizes the same parameter, rendered, loop, and continuation evidence into a single wait/continue/converged/stopped record so future passes can extend one convergence contract instead of rediscovering branch-local state. `ContinuationCandidateBiasV1` uses the continuation policy to bias matching candidate families and carries the same bias while a biased solve waits for rendered metrics so the rendered-metrics fingerprint remains stable. A shared stop-reason classifier now ensures stable/no-improvement/anti-oscillation stops can mark rendered feedback converged while no-best, all-damaged, below-quality, failed-render, pass-limit, and unavailable-candidate states remain explicit stopped/failed states. This is not a full scheduled convergence engine with spatial maps, a full staged cache controller, or no-improvement stopping across all future candidate families. |
| Add formal staged Auto state machine / cache-fingerprint model. | Partial | `WriteDevelopAutoStageSolveDiagnostics`; `BuildDevelopAutoStageFingerprints`; `DevelopAutoStageStateForRevisionStage`; `RenderPipeline::WasGraphImageCacheHit`; `ClassifyDevelopCandidateStageCacheForValidation`; `ClassifyDevelopCandidateStageScheduleForValidation`; `RenderPipeline::ResolveRawDevelopStageCacheMaxEntriesForValidation`; `autoStageSolveVersion = StagedAutoSolveV1`; `autoStageSolveStages`; `autoStageFingerprints`; `autoStageEarliestDirtyStage`; `autoStageResponsibleRevisionState`; `autoStageValidationState`; `autoCandidateRenderedRawBaseFinalCacheHitStatus`; `autoCandidateRenderedPreFinishFinalCacheHitStatus`; `autoCandidateRenderedObservedDirtyBoundaryCounts`; `autoCandidateRenderedStageCacheValidationStatus`; `autoCandidateRenderedStageSchedulerVersion`; `src/main.cpp` validation | Auto now writes a logical staged solve record with the documented states from `NEED_SOURCE` through `CONVERGED`, per-stage fingerprints for metadata, raw base, raw/global, scene prep, finish tone, and final validation, pass-budget metadata, earliest dirty cache boundary, and responsible rendered-feedback revision state. Candidate final renders also record whether existing graph caches were hit for the raw-base and pre-finish boundaries, derive an observed dirty boundary, validate whether scene-prep / finish-tone stage-constrained probes reused the expected upstream boundaries, and use `StageSchedulerV1` ordering so downstream probes render before RAW-dirty probes. `RenderPipeline` now also keeps owned raw-base/pre-finish stage snapshots keyed by fingerprint, so downstream candidate probes can reuse older RawDevelop boundaries after upstream-dirty candidate renders churn the single graph cache; retention is now size-aware and globally trimmed by estimated RGBA16F bytes so large RAWs trade cache reuse for stability. This is real staged-processing observability, first-pass candidate render scheduling, and a bounded physical boundary snapshot cache over the current renderer, but it is not yet a full RAW/global/scene-prep/finish controller, sidecar stats bus, or standalone staged render engine. |
| Use render-feedback stats when available without requiring a full new render pipeline. | Complete | `ReadDevelopToneAutoStats`; `BuildDevelopAutoCandidateSolve` | Uses existing integrated ToneCurve auto scene stats/feedback as current render-state input. |
| Render selected/surviving candidates as actual candidate outputs and analyze rendered state. | Partial | `BuildDevelopCandidateRenderRequests`; `ResolveDevelopAdaptiveRenderBudgetPolicy`; `DevelopCandidateRenderGuidanceDistance`; `DevelopCandidateRenderRevisionStageBonus`; `ClassifyDevelopCandidateStageScheduleForValidation`; `IsDevelopCandidateRelevantToRevisionStageForValidation`; `TryResolveWhiteBalanceProbeCandidateModeForRenderRequest`; `TryReadCandidateWhiteBalanceOverrideForRenderRequest`; `ApplyDevelopGuidanceToCandidateRenderPayload` and `PreserveCandidateRawCleanupSettings` in `src/Editor/Internal/EditorModuleDevelopCandidateRenderPayload.h/.cpp`; `EditorRenderWorker::RenderSnapshot`; `RenderPipeline::GetCachedGraphImagePixels`; `RenderPipeline::WasGraphImageCacheHit`; `ScoreDevelopRenderedCandidateMetrics`; `CompareDevelopRenderedCandidateToSelected`; `ClassifyDevelopRenderedCandidateDamage`; `ResolveDevelopRenderedRefineIntent` in `src/Editor/Internal/EditorModuleDevelopRenderedCandidateScoring.h/.cpp`; `EvaluateDevelopRenderedCandidateDuplicate`; `ClassifyDevelopRenderedStageBoundary` in `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackAnalysis.h/.cpp`; `ApplyDevelopCandidateRenderFeedback`; `TryApplyRenderedEnsembleMergeToSolve`; `ApplyRenderedCandidateFeedbackToSolve`; `CollectDevelopRenderedSurvivorCandidateIdsForCarryForward`; `EvaluateDevelopRenderedFeedbackTrend`; `autoCandidateRenderedVersion = RenderMetricsV1`; `autoCandidateRenderedStageSchedulerVersion = StageSchedulerV1`; `autoCandidateRenderedAdaptiveBudgetVersion = AdaptiveRenderBudgetV1` | The render worker now renders copied selected/surviving candidate payloads through synthetic outputs and measures both final rendered output and the hidden pre-finish boundary. When the final render has already populated the hidden pre-finish cache, candidate metrics read that cached texture directly instead of executing a second graph pass; the older pre-finish graph render remains as fallback if the cache read is unavailable. Candidate final renders now also record whether the graph/stage cache hit raw-base and pre-finish boundaries, expose whether reuse actually happened across the persistent worker pipeline, derive the observed dirty boundary (`rawBase`, `scenePrep`, or `finishTone`), and validate expected upstream reuse for stage-constrained scene-prep and finish-tone candidates. `RenderPipeline` stores bounded owned raw-base/pre-finish snapshots by fingerprint so a downstream probe can still hit an older boundary after another candidate replaced the single graph-cache entry. Candidate render request selection always includes the selected state, then uses score plus authored-guidance diversity, rendered-local mismatch priority, cleanup/detail priority, RAW white-balance probe priority, mode-neighbor priority, exposure-placement priority for `highlightProtectedMids`, and responsible-stage priority from the previous rendered-feedback revision to spend limited render slots on meaningfully different survivors instead of only the top scalar scores. Default pending solves still render up to four candidates per active Develop node; `AdaptiveRenderBudgetV1` can expand active continuation passes up to six candidates when `RenderedContinuationV1` says the next rendered pass needs responsible-stage, active-refine, adoption, or merge validation. When prior rendered feedback names an active responsible stage or active refine intent and the selected render set does not already cover it, bounded render slots are reserved for matching survivors. The chosen render set is then ordered by `StageSchedulerV1`: selected baseline first, finish-tone probes before scene-prep probes, then RAW/global, RAW cleanup, and multi-stage probes that can replace upstream cache boundaries. Candidate render payloads now apply stage constraints: RAW white-balance probes apply categorical RAW WB mode overrides as `rawGlobal`, scene-prep probes preserve RAW-stage placement, finish-tone probes preserve RAW plus scene prep, and cleanup/detail probes preserve global RAW placement while varying cleanup/detail fields. Rendered analysis measures compact rendered metrics, mean RGB/channel-balance/color-cast risk, 3x3 local luma/contrast summaries, compact 3x3 local damage-risk summaries, visual-risk proxies, pre-finish metrics, and selected-vs-best final/pre-finish stage-boundary distances; it rejects obvious rendered damage before best/duplicate/pair selection, clusters metric-near rendered candidates while preserving final-masked pre-finish-distinct survivors, records a rendered best representative id/score, can mark a strong distinct rendered-survivor pair or three-survivor ensemble for authored merge/re-solve, stores selected/best rendered metric snapshots in bounded feedback history, carries bounded successful prior rendered survivors into the next active feedback solve, and can request a full Auto re-solve that directly adopts clear rendered winners, preserves a finish-tone revision target when final metrics change while pre-finish metrics stay stable, creates `renderedFeedbackMerge` for modest selected-vs-best wins, creates `renderedFeedbackPairMerge` from two strong distinct rendered survivors, creates `renderedFeedbackEnsembleMerge` from three strong distinct rendered survivors, selects a generated rendered-local candidate family when the selected render itself looks too dark, shadow-crowded, highlight-crowded, edge-risky, locally highlight-crowded, locally flat, center-shadow-crowded, spatially highlight-damaged, spatially contrast-stressed, spatially shadow/noise-stressed, spatially flat-gray, noisy/textured in the shadows, or too subdued in fine separation by compact metrics, rejects extreme color-cast/channel-imbalance damage, or stops with `renderedMetricsStable`, `renderedFeedbackNoImprovementTrend`, `renderedRefineNoImprovementTrend`, or `renderedFeedbackStableTrend` when rendered history shows no meaningful movement. The older synthetic `renderedFeedbackRefine` remains as a fallback when no matching rendered-local family survives. It does not persist thumbnails, present a gallery, expose user selection, or run a full scheduled multi-pass convergence loop. |
| Add candidate diagnostics that explain why candidates were generated, changed, survived, merged, rejected, measured, adopted, refined, or stopped from rendered feedback. | Partial | `WriteDevelopAutoCandidateSolveDiagnostics`; `BuildDevelopAutoCandidateScoreComponents`; `ApplyDevelopCandidateRenderFeedback`; `EvaluateDevelopRenderedFeedbackTrend`; `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/main.cpp` | Tone JSON records candidate labels, reasons, score, `ParameterScoreComponentsV1` score dimensions/risks, per-candidate `guidanceFingerprint`, status, rejection reason, guidance deltas, `rawOverrides.whiteBalanceMode`, `changes.whiteBalanceMode`, selected/authored white-balance probe and mode fields, merge info including optional third merge source/weight, convergence info, selected candidate, selection source, rejected-memory entries/suppression count, rendered-rejection memory entries/suppression count, render metric status, rendered candidate count/unique count/damage count/duplicate count/failure count, rendered damage status and reject reason, rendered duplicate status and representative id plus final/pre-finish duplicate distances, pre-finish-distinct survivor count and reason fields, rendered luma/contrast/clipping/RGB/color-cast/saturation/edge/halo/shadow-texture/local-tile metrics, compact `localDamageRiskScore3x3` / `localDamageRiskMean` / `localDamageRiskPeak` / `localDamageRiskPeakTile` diagnostics, per-candidate `preFinishReusedFromFinalRender`, aggregate `autoCandidateRenderedPreFinishReuseCount` and `autoCandidateRenderedPreFinishReuseStatus`, per-candidate `rawBaseCacheHitDuringFinalRender` and `preFinishCacheHitDuringFinalRender`, per-candidate `observedDirtyBoundary`, `stageCacheExpectedBoundary`, `stageCacheExpectationEvaluated`, `stageCacheExpectationMet`, and `stageCacheValidationStatus`, aggregate `autoCandidateRenderedRawBaseFinalCacheHitCount`, `autoCandidateRenderedRawBaseFinalCacheHitStatus`, `autoCandidateRenderedPreFinishFinalCacheHitCount`, `autoCandidateRenderedPreFinishFinalCacheHitStatus`, `autoCandidateRenderedObservedDirtyBoundaryCounts`, and `autoCandidateRenderedStageCacheValidationStatus`, per-candidate `candidateRevisionStage`, `activeRevisionStage`, `activeStageMatch`, `stageReservedRequest`, `stageSchedulerOrder`, `stageSchedulerRank`, `stageSchedulerExpectedDirtyBoundary`, and `stageSchedulerReason`, aggregate `autoCandidateRenderedActiveStageRequestCount`, `autoCandidateRenderedStageReservedRequestCount`, `autoCandidateRenderedStageSchedulerStatus`, and `autoCandidateRenderedStageSchedulerExpectedBoundaryCounts`, rendered best id/score, rendered pair-merge suggestion ids/scores/metric distance, rendered ensemble-merge source ids/labels/scores/metric distances/spread, rendered-local candidate family reasons, rendered-feedback action/merged/refine intent/refine reason/pass/applied fingerprint, rendered-feedback improvement, responsible revision stage/reason, convergence status, stop reason, selected render score, rendered stability distance/score delta/reference/status, rendered trend history count/same-best count/score spread/nearest distance/status, rendered carry-forward count, `RenderedContinuationV1` continuation-policy decision/reason/next-step/pass-budget/stage-focus/evidence fields, and bounded feedback history with compact selected/best metric snapshots. Repeated failed refinement and repeated no-gain choice attempts use explicit stop reasons such as `renderedRefineDidNotImprove`, `renderedRefineRepeatedIntent`, `renderedMergeConverged`, `renderedMergeDidNotImprove`, `renderedAdoptionNoFurtherGain`, `renderedMetricsStable`, `renderedFeedbackNoImprovementTrend`, `renderedRefineNoImprovementTrend`, and `renderedFeedbackStableTrend`. UI only shows selected candidate and render-metrics summary. Full diagnostic UI belongs to Guide 10. |
| Record learning separately from whether learning is applied. | Partial | `BuildDevelopAutoCandidateLearningRecord`; `autoCandidateLearningRecord`; `autoCandidateLearningStatus = "recordedNotApplied"` in `src/Editor/EditorModule.cpp`; `src/main.cpp` validation | Solver outcome-learning events are now recorded from generated, selected, survived, rejected, merged, rendered-feedback, and convergence outcomes, with current-image and future-image application explicitly false. User choice/rejection preference learning, settings toggles, reset controls, and applying learned bias remain deferred. |
| Implement gallery-like candidate rendering/selection UI. | Deferred | `autoCandidateGalleryStatus = "deferred"` in tone JSON; `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` | No thumbnail gallery, hover preview, side-by-side compare, or user candidate picker exists yet. |
| Keep Manual mode as the exposed authored state, not a separate render algorithm. | Complete | Existing `RawDevelopUiMode`; unchanged render path; `src/main.cpp` smoke validation | Auto/Manual mode behavior remains intact; selected candidates author existing settings rather than switching render algorithms. |

## Guide 04 Requirement Checklist

| Requirement | Status | Evidence / code or doc location | Notes |
|---|---|---|---|
| Read Guide 04 and preserve the dynamic-range philosophy: believable lighting, mode intent, and honest clipped-data language over histogram chasing. | Complete | `04_Dynamic_Range_Highlights_Shadows_and_Local_Exposure_Strategy.txt` | This pass starts from Guide 04 without changing Develop's scene-linear output or View Transform contract. |
| Add a durable Auto dynamic-range strategy record that names the selected highlight/shadow/local-exposure approach. | Complete | `ResolveDevelopDynamicRangeStrategy`; `DevelopDynamicRangeStrategyToJson`; `WriteDevelopAutoCandidateSolveDiagnostics`; `src/Editor/Internal/EditorModuleRawUI.cpp` | Writes `DynamicRangeStrategyV1`, nested `autoDynamicRangeStrategy`, top-level strategy aliases, and an Auto status readout. |
| Add internal Dynamic Range Strategy Map coordinates that can later support graph-style controls. | Complete | `ResolveDevelopDynamicRangeStrategyMap`; `DynamicRangeStrategyMapV1`; `DevelopDynamicRangeStrategyToJson`; `WriteDevelopAutoCandidateSolveDiagnostics`; `BuildDevelopAutoCandidateSolve`; `ScoreDevelopAutoCandidate`; `BuildDevelopAutoCandidateScoreComponents`; `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/main.cpp` validation | Writes highlight-vs-shadow and contrast-vs-range coordinates into integrated tone JSON, shows a compact Auto status readout, forwards map data into score components, and uses the coordinates to bias existing candidate generation/scoring. This is internal solver substrate, not the Guide 10 graph control UI. |
| Add a local exposure strategy contract that maps strategy-map/evidence into authored Scene Prep and candidate guardrails. | Complete | `ResolveDevelopLocalExposureStrategy`; `LocalExposureStrategyV1`; `DevelopDynamicRangeStrategyToJson`; `BuildDevelopSelectedAutoSolveContext`; `ApplyDevelopSelectedScenePrepSettings`; `WriteDevelopSelectedSolveToneDiagnostics`; `WriteDevelopAutoCandidateSolveDiagnostics`; `ApplyDevelopSelectedAutoSolve`; `ApplyDevelopAutoSolve`; `ApplyDevelopGuidanceToCandidateRenderPayload`; `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/main.cpp` validation | Writes local range redistribution, highlight compression, shadow opening, noise guard, halo guard, texture guard, EV budgets, and strength target into integrated tone JSON; derives the selected-solve local-exposure context once; authors Scene Prep from that contract; carries it into candidate Scene Prep probes; and shows compact Auto diagnostics. This is still compact strategy over existing Scene Prep, not true local-EV maps, overlays, or graph controls. |
| Make Auto distinguish broad meaningful highlight pressure from acceptable small specular clipping. | Partial | `ResolveDevelopDynamicRangeStrategy`; `BuildDevelopDynamicRangeRegionEvidenceFromMetrics`; `autoDynamicRangeSmallSpecularClippingAllowed`; `autoDynamicRangeMeaningfulHighlightPressure`; `autoDynamicRangeBroadHighlightGuardNeed`; `autoDynamicRangeSpecularHighlightToleranceNeed`; `autoDynamicRangeHighlightImportance`; `autoDynamicRangeBrightHighlightRolloffNeed`; `broadHighlightGuard`; `specularHighlightTolerance` | Scalar heuristics now distinguish broad pressure from tiny specular allowance, compact rendered regional evidence can raise meaningful-highlight pressure through tile coverage/structure, a Scene Prep candidate can test broad-highlight local protection, and a finish-tone candidate can test tiny-specular tolerance; true clipping maps and highlight-importance maps remain deferred. |
| Add a first candidate/probe that preserves highlight brightness feeling while still using rolloff/guardrails. | Complete | `BuildDevelopAutoCandidateSolve`; `IsDevelopFinishToneProbeCandidateId`; `IsFinishToneProbeCandidateIdForRenderRequest`; `ApplyDevelopGuidanceToCandidateRenderPayload`; `src/main.cpp` validation | `brightHighlightRolloff` is a finish-tone probe and explicitly avoids claiming clipped-data recovery. |
| Add a brightness-feeling / gray-highlight finish-tone candidate for protected broad highlights. | Complete | `autoDynamicRangeHighlightBrightnessAnchorNeed`; `luminousHighlightAnchor`; `BuildDevelopAutoCandidateSolve`; `ScoreDevelopAutoCandidate`; `BuildDevelopAutoCandidateScoreComponents`; `DevelopAutoCandidateModeIntentFit`; `ApplyDevelopGuidanceToCandidateRenderPayload`; `src/main.cpp` validation | `luminousHighlightAnchor` freezes RAW/global placement and Scene Prep while testing downstream highlight character and contrast so broad/protected highlights stay luminous instead of flattening toward gray. It shapes visible range and does not claim clipped-data recovery or complete the Guide 08 tone redesign. |
| Add rendered highlight-grayness evidence so Auto can score highlight brightness feeling from candidate pixels. | Complete | `DevelopCandidateRenderMetrics`; `AnalyzeDevelopCandidatePixels`; `DevelopCandidateRenderMetricsToJson` in `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackRecords.h/.cpp`; `ReadDevelopRenderedMetricsFromJson`; `BuildDevelopDynamicRangeRegionEvidenceFromMetrics`; `ResolveDevelopDynamicRangeStrategy`; `ScoreDevelopRenderedCandidateMetrics`; `ClassifyDevelopRenderedCandidateDamage`; `ResolveDevelopRenderedRefineIntent` in `src/Editor/Internal/EditorModuleDevelopRenderedCandidateScoring.h/.cpp`; `src/main.cpp` validation | `RenderMetricsV1` now records highlight-band fraction, mean luma, low-saturation fraction, and `highlightGrayRisk`. Strategy diagnostics forward the evidence through `autoDynamicRangeHighlightGrayRisk` and related top-level fields. This is compact rendered evidence, not a true spatial highlight map or Guide 08 tone redesign. |
| Add rendered meaningful-highlight structure evidence so Auto can distinguish broad important highlights from tiny glints. | Complete | `DevelopCandidateRenderMetrics`; `AnalyzeDevelopCandidatePixels`; `DevelopCandidateRenderMetricsToJson` in `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackRecords.h/.cpp`; `ReadDevelopRenderedMetricsFromJson`; `BuildDevelopDynamicRangeRegionEvidenceFromMetrics`; `ResolveDevelopDynamicRangeStrategy`; `BuildDevelopAutoCandidateScoreComponents`; `ScoreDevelopRenderedCandidateMetrics`; `ClassifyDevelopRenderedCandidateDamage`; `ResolveDevelopRenderedRefineIntent` in `src/Editor/Internal/EditorModuleDevelopRenderedCandidateScoring.h/.cpp`; `src/main.cpp` validation | `RenderMetricsV1` now records `highlightTileCoverage`, `highlightStructureScore`, and `meaningfulHighlightPressure`. Strategy diagnostics forward the evidence through `autoDynamicRangeMeaningfulHighlightPressure`, `autoDynamicRangeHighlightTileCoverage`, and `autoDynamicRangeHighlightStructureScore`; validation separates gray-highlight, meaningful-highlight, and local spatial-risk fixtures. This is compact rendered evidence, not a true highlight-importance map or subject detector. |
| Add compact rendered local-EV conflict evidence for local exposure decisions. | Complete | `DevelopCandidateRenderMetrics`; `AnalyzeDevelopCandidatePixels`; `DevelopCandidateRenderMetricsToJson` in `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackRecords.h/.cpp`; `ReadDevelopRenderedMetricsFromJson`; `BuildDevelopDynamicRangeRegionEvidenceFromMetrics`; `ResolveDevelopDynamicRangeStrategy`; `BuildDevelopAutoCandidateScoreComponents`; `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/main.cpp` validation | `RenderMetricsV1` now records `localEvSpreadStops` and `localEvConflict` from compact 3x3 rendered luma evidence, mixed dark/bright tile coverage, local damage risk, edge contrast, and halo risk. Strategy diagnostics forward the evidence through `autoDynamicRangeLocalEvConflict` and `autoDynamicRangeLocalEvSpreadStops`; Auto status shows compact local-EV conflict/spread. This is not a true local-EV map or visual overlay. |
| Add compact local exposure damage-profile evidence for highlight crowding, shadow crowding, halo stress, and flattening. | Complete | `DevelopCandidateRenderMetrics`; `AnalyzeDevelopCandidatePixels`; `DevelopCandidateRenderMetricsToJson` in `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackRecords.h/.cpp`; `ReadDevelopRenderedMetricsFromJson`; `BuildDevelopDynamicRangeRegionEvidenceFromMetrics`; `ResolveDevelopLocalExposureStrategy`; `BuildDevelopAutoCandidateScoreComponents`; `src/Editor/Internal/EditorModuleRawUI.cpp`; `src/main.cpp` validation | `RenderMetricsV1` now records `localExposureHighlightCrowding`, `localExposureShadowCrowding`, `localExposureHaloStress`, `localExposureFlatnessRisk`, and `localExposureDamageRisk`. The profile is used by the local exposure strategy to focus correction and reduce unsafe redistribution when halo/damage pressure is high. This is compact rendered evidence, not true spatial maps or overlays. |
| Add a first Scene Prep candidate/probe for broad meaningful highlight pressure. | Complete | `broadHighlightGuard`; `BuildDevelopAutoCandidateSolve`; `ScoreDevelopAutoCandidate`; `BuildDevelopAutoCandidateScoreComponents`; `DevelopAutoCandidateModeIntentFit`; `ApplyDevelopGuidanceToCandidateRenderPayload`; `src/main.cpp` validation | `broadHighlightGuard` is a Scene Prep probe that freezes RAW/global placement while testing local highlight compression for broad bright regions. It explicitly does not claim clipped-data recovery. |
| Add a first candidate/probe for acceptable tiny specular clipping. | Complete | `specularHighlightTolerance`; `BuildDevelopAutoCandidateSolve`; `ScoreDevelopAutoCandidate`; `BuildDevelopAutoCandidateScoreComponents`; `DevelopAutoCandidateModeIntentFit`; `ApplyDevelopGuidanceToCandidateRenderPayload`; `src/main.cpp` validation | `specularHighlightTolerance` is a finish-tone probe that freezes RAW and Scene Prep while testing a brighter highlight-character shape for tiny glints. It explicitly does not claim clipped-data recovery. |
| Add a first local range guard candidate from rendered regional evidence. | Partial | `DynamicRangeRegionEvidenceV1`; `BuildDevelopDynamicRangeRegionEvidenceFromMetrics`; `BuildDevelopAutoCandidateSolve`; `ScoreDevelopAutoCandidate`; `BuildDevelopAutoCandidateScoreComponents`; `ApplyDevelopGuidanceToCandidateRenderPayload`; `src/main.cpp` validation | `localRangeGuard` is generated from compact rendered regional highlight/shadow/halo/local-EV pressure and rendered as a Scene Prep constrained probe. `localEvConflict` now contributes to generation, scoring, score-component signals/risks, and validation. This is not yet true local-EV maps, subject-aware local exposure, or graph controls. |
| Add a first anti-halo / local exposure safety Scene Prep candidate. | Complete | `autoDynamicRangeLocalHaloGuardNeed`; `haloSafeLocalRange`; `BuildDevelopAutoCandidateSolve`; `ScoreDevelopAutoCandidate`; `BuildDevelopAutoCandidateScoreComponents`; `DevelopAutoCandidateModeIntentFit`; `ApplyDevelopGuidanceToCandidateRenderPayload`; `src/main.cpp` validation | `haloSafeLocalRange` is generated from compact rendered halo/local-range evidence, backs off local max-EV pressure, and raises existing halo/smooth-gradient/edge guardrails while freezing RAW/global placement. This is not a true halo map, subject-aware local exposure model, or graph-control UI. |
| Add a first brightness-hierarchy / flat-gray finish-tone guard candidate. | Complete | `autoDynamicRangeNaturalContrastGuardNeed`; `naturalContrastGuard`; `BuildDevelopAutoCandidateSolve`; `ScoreDevelopAutoCandidate`; `BuildDevelopAutoCandidateScoreComponents`; `DevelopAutoCandidateModeIntentFit`; `ApplyDevelopGuidanceToCandidateRenderPayload`; `src/main.cpp` validation | `naturalContrastGuard` is generated when compact rendered evidence shows flat-gray or brightness-hierarchy risk. It freezes RAW/global and Scene Prep and tests downstream finish-tone separation; it is not the full Guide 08 tone redesign or graph-control UI. |
| Add a first positive readable-shadow Scene Prep candidate. | Complete | `autoDynamicRangeShadowReadabilityLiftNeed`; `shadowReadabilityLift`; `BuildDevelopAutoCandidateSolve`; `ScoreDevelopAutoCandidate`; `BuildDevelopAutoCandidateScoreComponents`; `DevelopAutoCandidateModeIntentFit`; `ApplyDevelopGuidanceToCandidateRenderPayload`; `src/main.cpp` validation | `shadowReadabilityLift` is generated when shadows appear readable/clean enough to open locally. It freezes RAW/global placement and keeps noise/halo guardrails active; it is not subject-aware shadow classification or denoise redesign. |
| Add a first shadow noise-floor candidate that can preserve dark regions instead of lifting noise. | Partial | `autoDynamicRangeShadowNoiseFloorNeed`; `shadowNoiseFloor`; `BuildDevelopAutoCandidateSolve`; `ApplyDevelopGuidanceToCandidateRenderPayload`; `src/main.cpp` validation | `shadowNoiseFloor` is generated from compact shadow/noise evidence and rendered as a Scene Prep constrained probe. It tests keeping noisy or low-value dark regions darker; it is not subject-aware shadow classification or Guide 07 denoise redesign. |
| Couple shadow lift with noise/texture risk in strategy diagnostics. | Partial | `ResolveDevelopDynamicRangeStrategy`; `autoDynamicRangeShadowReadability`; `autoDynamicRangeShadowReadabilityLiftNeed`; `autoDynamicRangeNoiseConstraint`; `autoDynamicRangeShadowNoiseFloorNeed`; existing `cleanShadows` / `preserveTexture` / `shadowReadabilityLift` / `shadowNoiseFloor` paths | Strategy diagnostics now expose both sides of the tradeoff: open readable shadows when clean enough, or hold noisy shadows down. Full texture/noise maps and Guide 07 denoise redesign remain deferred. |
| Preserve bright things as bright and dark things as plausibly dark by mode. | Partial | Existing mode profiles; `DynamicRangeStrategyV1`; `brightHighlightRolloff`; `luminousHighlightAnchor`; `naturalContrastGuard`; `broadHighlightGuard`; `specularHighlightTolerance`; `shadowReadabilityLift`; `shadowNoiseFloor`; `haloSafeLocalRange`; `toneDarkerToe` | Bright-highlight rolloff, luminous-highlight anchor, natural-contrast guard, broad-highlight guard, readable-shadow lift, tiny-specular tolerance, shadow-floor, and halo-safe local range candidates now exist; full tonal hierarchy redesign belongs to Guide 04/08 follow-up work. |
| Add true clipping maps, highlight importance maps, noise-risk maps, local EV maps, halo-risk maps, and visual overlays. | Deferred | `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` | Current compact numeric/rendered metrics and `DynamicRangeRegionEvidenceV1` remain substrate only. Visual map UI belongs to later Guide 04/10 passes. |
| Add graph-style Dynamic Range Strategy controls. | Deferred | `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` | Graph controls remain Guide 10-owned. This pass may document graph direction but should not build the graph UI. |
| Keep scene prep honest: local exposure shaping does not recover missing clipped data. | Complete | Existing context docs; `brightHighlightRolloff` and `luminousHighlightAnchor` candidate reasons; tracker/decision notes | New text uses preserve/shape/roll off visible range language rather than recovery claims. |

## Guide 05 Requirement Checklist

| Requirement | Status | Evidence / code or doc location | Notes |
|---|---|---|---|
| Read Guide 05 and preserve the philosophy that subject importance is a confidence-weighted bias, not a command that overrides the whole scene. | Complete | `05_Subject_Importance_User_Guided_Bias_Brushes_and_Scene_Understanding.txt` | This pass starts from soft solver evidence rather than a hard mask or detection claim. |
| Add a durable subject/scene importance strategy record that can later support graph-style Subject / Scene Intent Map controls. | Complete | `SubjectSceneIntentV1`; `ResolveDevelopSubjectSceneIntent`; `DevelopSubjectSceneIntentToJson`; `WriteDevelopAutoCandidateSolveDiagnostics`; `src/main.cpp` validation | Writes subject/scene strategy data, axes, confidence, user-guidance state, and top-level `autoSubjectScene*` aliases. This is solver substrate and diagnostics, not the Guide 10 graph UI. |
| Add compact automatic subject/scene evidence without claiming AI subject detection. | Complete | `DevelopCandidateRenderMetrics`; `AnalyzeDevelopCandidatePixels`; `DevelopCandidateRenderMetricsToJson` in `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackRecords.h/.cpp`; `ReadDevelopRenderedMetricsFromJson`; `BuildDevelopDynamicRangeRegionEvidenceFromMetrics`; `src/main.cpp` validation | Uses weak rendered center/detail/readability/protection/mood priors. It explicitly does not claim face/person/object/AI detection. |
| Add first user-guided Subject / Scene Intent controls before the brush exists. | Complete | `DevelopAutoGuidance::subjectSceneBias`; `DevelopAutoGuidance::moodReadabilityBias`; `EditorNodeGraphSerializer.cpp`; `RenderRawDevelopControls`; `ResolveDevelopSubjectSceneIntent`; `BuildDevelopAutoSolveTriggerHash`; `BuildDevelopAutoCandidateGuidanceFingerprint`; `ReadDevelopCandidateGuidanceFromJson`; `ReadDevelopAuthoredGuidanceFromToneJson`; `src/main.cpp` validation | Auto now exposes `Subject / Scene Intent` and `Mood / Readability` axes. They bias the solver and candidate scoring, round-trip through graph JSON, default to neutral in old graphs, and survive rendered-feedback guidance readback. This is not a painted mask, brush mode, or graph-widget implementation. |
| Let subject/scene evidence influence Auto candidate scoring or generation as a bias. | Complete | `ScoreDevelopAutoCandidate`; `BuildDevelopAutoCandidateScoreComponents`; `BuildDevelopAutoCandidateSolve`; `src/main.cpp` validation | Influence is conservative and confidence-weighted; score dimensions/risks include subject priority, readability, protection, mood, over-lift, and tradeoff risk without bypassing clipping, noise, halo, or mood safeguards. |
| Generate named subject/scene candidate alternatives from the intent axes. | Complete | `subjectReadableMids`; `sceneMoodPreservation`; `BuildDevelopAutoCandidateSolve`; `ScoreDevelopAutoCandidate`; `DevelopAutoCandidateModeIntentFit`; `BuildDevelopAutoCandidateScoreComponents`; `ApplyDevelopGuidanceToCandidateRenderPayload`; `BuildDevelopCandidateRenderRequests`; `src/main.cpp` validation | Auto now tests a subject-readable Scene Prep branch and a scene-mood preservation counter-branch. These are authored solver candidates and rendered-feedback probes, not a candidate gallery UI, subject mask, semantic detector, or brush. |
| Measure user-marked subject regions/strokes in rendered candidate metrics. | Partial | `DevelopSubjectMetricSampling`; `BuildDevelopSubjectMetricSampling`; `DevelopCandidateRenderRequest::subjectSampling`; `AnalyzeDevelopCandidatePixels`; `DevelopCandidateRenderMetricsToJson` in `src/Editor/Internal/EditorModuleDevelopRenderedFeedbackRecords.h/.cpp`; `ReadDevelopRenderedMetricsFromJson`; `ScoreDevelopRenderedCandidateMetrics`; `CompareDevelopRenderedCandidateToSelected` in `src/Editor/Internal/EditorModuleDevelopRenderedCandidateScoring.h/.cpp`; `CompareDevelopCandidateRenderMetrics`; `src/main.cpp` validation | Candidate renders now sample bounded copies of user-marked regions/strokes and record compact marked coverage, luma, readability, protection, mood, and low-priority pressure. This gives Auto actual rendered feedback for marked areas, but it is still sampled solver evidence, not an edge-aware interpreted importance map, diagnostic map view, semantic detector, or candidate gallery UI. |
| Add first compact interpreted subject-importance map contract from user marks. | Complete | `SubjectImportanceMapV1`; `InterpretDevelopSubjectImportanceMap`; `DevelopSubjectImportanceInterpretationToJson`; `SubjectSceneIntentV1.importanceMap`; `autoSubjectSceneImportanceMap*`; `autoRequestedSubjectImportanceMap*`; `BuildDevelopAutoCandidateScoreComponents`; `RenderRawDevelopControls`; `src/main.cpp` validation | Enabled regions and strokes now produce a bounded 5x5 solver grid with importance/reveal/protect/preserve-mood/low-priority cells plus coverage, peak, confidence, center-bias, and edge-bias diagnostics. This is a compact interpreted map contract; edge-aware refinement and visual diagnostic map UI remain deferred. |
| Add compact interpreted subject-importance diagnostic map overlay. | Complete | `DevelopSubjectImportanceMap::showInterpretedMapOverlay`; `DevelopSubjectImportanceMap::interpretedMapOpacity`; `EditorModule::GetDevelopSubjectImportanceViewportState`; `DrawDevelopSubjectInterpretedMapOverlay`; `RenderRawDevelopControls`; `ValidateDevelopAutoIntentSerialization`; `ValidateDevelopAutoSolveBehavior` | Selected Develop nodes can show the current `SubjectImportanceMapV1` 5x5 grid in the single-output viewport under editable strokes/regions. Display settings round-trip through graph JSON, old graphs default off, and validation checks visual-only settings stay out of solver fingerprints. This is a compact diagnostic map view, not edge-aware/refined map visualization. |
| Add first compact refined subject importance/confidence map diagnostics. | Complete | `SubjectRefinedMapV1`; `BuildDevelopSubjectRefinedMap`; `DevelopSubjectRefinedMapToJson`; `SubjectSceneIntentV1.refinedImportanceMap`; `autoSubjectSceneRefinedMap*`; `BuildDevelopAutoCandidateScoreComponents`; `DevelopSubjectImportanceMap::showRefinedMapOverlay`; `DrawDevelopSubjectRefinedMapOverlay`; `ValidateDevelopAutoSolveBehavior`; `ValidateDevelopAutoIntentSerialization` | The existing interpreted map now feeds a compact refined map with importance, confidence, readability, protection, mood-preservation, low-priority, and boundary-hint cells. It biases scoring lightly, appears in Auto status, round-trips viewport display settings, and draws as a selected-node viewport diagnostic. This is not true image-edge segmentation or AI/semantic detection. |
| Surface compact Auto diagnostics for subject/scene priority. | Complete | `WriteDevelopAutoCandidateSolveDiagnostics`; `RenderRawDevelopControls`; `src/main.cpp` validation | Auto diagnostics and the status panel show automatic subject/scene confidence, user-guidance status, subject/scene plus mood/readability axes, and interpreted-map status/coverage/peak/low-priority/center-bias values. |
| Add solve notes explaining how subject importance affected Auto candidates. | Complete | `SubjectImportanceSolveNotesV1`; `BuildDevelopSubjectSolveNotes`; `ResolveDevelopSubjectSceneIntent`; `DevelopSubjectSceneIntentToJson`; `WriteDevelopAutoCandidateSolveDiagnostics`; `BuildDevelopAutoCandidateScoreComponents`; `RenderRawDevelopControls`; `ValidateDevelopAutoSolveBehavior` | `SubjectSceneIntentV1` now carries compact note objects for interpreted-map bias, intent controls, readability/reveal, protection, mood preservation, low-priority, and subject/scene axis effects. Top-level `autoSubjectSceneSolveNotes*` aliases and candidate score components carry the same note record, and Auto status shows up to two short notes. This is diagnostic explanation, not graph controls or a candidate gallery. |
| Preserve Finish Mask semantics. | Complete | `docs/engineering/develop/spec_sources/DEVELOP_NODE_CONTEXT.txt`; `docs/engineering/develop/DEVELOP_SOURCE_MAP.md` | Finish Mask affects integrated finish tone only and is not a subject-importance brush. |
| Add user-guided Importance Brush with soft overlay. | Partial | `DevelopSubjectImportanceMap`; `DevelopSubjectImportanceRegion`; `DevelopSubjectImportanceStroke`; `RenderRawDevelopControls`; `EditorModule::GetDevelopSubjectImportanceViewportState`; `EditorModule::BeginDevelopSubjectImportanceBrushStroke`; `EditorModule::AppendDevelopSubjectImportanceBrushStroke`; `EditorModule::EndDevelopSubjectImportanceBrushStroke`; `EditorViewport::Render`; `EditorNodeGraphSerializer.cpp`; `ResolveDevelopSubjectSceneIntent`; `BuildDevelopSubjectMetricSampling`; `src/main.cpp` validation | Persisted soft region guidance, viewport region editing, freehand-style viewport brush painting, reduce/subtract strokes, brush cursor, brush size/strength/soft-edge controls, basic per-stroke select/enable/delete/retune controls, solver handoff, and compact subject-marked rendered metrics now exist. Edge-aware interpretation, diagnostic maps, stroke point/history editing, and graph controls remain future Guide 05/10 work. |
| Add brush modes: Important, Reveal, Protect, Preserve Mood, Ignore / Low Priority. | Complete | `DevelopSubjectImportanceMode`; `DevelopSubjectImportanceModeStableString`; `DevelopSubjectImportanceModeLabel`; `DevelopSubjectImportanceModeDescription`; `RenderRawDevelopControls`; `DevelopSubjectImportanceRegion`; `DevelopSubjectImportanceStroke`; `src/main.cpp` validation | Mode vocabulary, stable strings, UI selection, serialization fallback, and solver summary weighting now exist for both soft regions and brush strokes. Edge-aware painted-mode interpretation remains a future map/refinement feature, not a missing mode-vocabulary item. |
| Persist subject-importance region and stroke data and make it affect Auto re-solve. | Complete | `RawDevelopPayload::subjectImportance`; `NormalizeDevelopSubjectImportance`; `HashDevelopSubjectImportance`; `BuildDevelopAutoSolveTriggerHash`; `BuildDevelopAutoCandidateContextFingerprint`; `BuildDevelopAutoCandidateGuidanceFingerprint`; `ValidateDevelopAutoSolveBehavior`; `ValidateDevelopAutoIntentSerialization` | Old graphs load with disabled/no regions/no strokes, unknown region/brush/stroke mode strings fall back to `Important`, and changing region guidance or stroke points changes the candidate context fingerprint. Visual-only overlay visibility/opacity stays out of solver hashes. |
| Add edge-aware interpreted subject/importance maps and visual diagnostics. | Partial | `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md`; `SubjectImportanceMapV1` substrate; `SubjectRefinedMapV1`; `DrawDevelopSubjectInterpretedMapOverlay`; `DrawDevelopSubjectRefinedMapOverlay`; `SubjectImportanceSolveNotesV1` | Compact interpreted and refined/confidence map contracts plus selected-node viewport overlays now exist. True image-edge refinement, edge visualization, and richer graph/diagnostic UI remain future work. |
| Add face/person/AI subject detection. | Deferred | `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` | Guide 05 explicitly says AI may be useful later but is not the whole answer; do not add black-box detection in this pass. |
| Preserve subject importance into Manual and return-to-Auto handoff biases. | Not Started | `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` | Full Manual handoff also intersects Guide 09. |

## Guide 08 Requirement Checklist

| Requirement | Status | Evidence / code or doc location | Notes |
|---|---|---|---|
| Read Guide 08 and related Develop ownership notes before implementation. | Complete | `08_Tone_Contrast_Finish_Feel_and_Realistic_Final_Rendering.txt`; related Guide 02/09 passages | This pass restores manual finish-curve substrate without claiming the broader integrated `Develop` tone redesign. |
| Reuse the existing standalone `LayerType::ToneCurve` instead of inventing a second manual tone-curve node type. | Complete | `src/Editor/LayerRegistry.cpp`; `src/Editor/Layers/ToneLayers.cpp`; `src/Editor/EditorModule.cpp` | The existing standalone node is promoted back to first-class use. |
| Make standalone `Tone Curve` addable again in the node browser and add menus. | Complete | `src/Editor/LayerRegistry.cpp` | The legacy-only restriction was removed and the descriptor is visible again. |
| Keep the manual curve editor, point editing/context menu behavior, channel mode buttons, curve-domain control, reset actions, and existing canvas targeting on the standalone node surface. | Complete | `src/Editor/Layers/ToneCurveLayerUI.cpp`; `src/Editor/Layers/ToneCurveLayerModel.cpp`; `src/Editor/Layers/ToneCurveLayerAuto.cpp`; `src/Editor/Layers/ToneCurveLayerSerialization.cpp`; `src/Editor/Layers/ToneLayerRendering.cpp`; `src/Editor/Layers/ToneLayers.cpp` | The lean node still exposes the core manual finish-curve interaction surface. Visible standalone and Develop-integrated Tone Curve surfaces live in `ToneCurveLayerUI.cpp`; point/model/evaluation logic is split into `ToneCurveLayerModel.cpp`; auto-analysis/rewrite logic is split into `ToneCurveLayerAuto.cpp`; JSON persistence lives in `ToneCurveLayerSerialization.cpp`; shader/GL execution lives in `ToneLayerRendering.cpp`; effective tone/foundation settings remain in `ToneLayers.cpp`. |
| Remove legacy standalone Auto Calibrate / scene analysis / local baseline / foundation tone / scoped-mask sections from the standalone node surface. | Complete | `src/Editor/Layers/ToneLayers.cpp` | The graph surface now focuses on the actual manual finish curve rather than the older larger standalone stack. |
| Remove legacy â€œDevelop owns this nowâ€ messaging from the standalone node surface and descriptor text. | Complete | `src/Editor/Layers/ToneCurveLayerUI.cpp`; `src/Editor/LayerRegistry.cpp` | Descriptor/help text now frames `Tone Curve` as a normal manual scene-referred finish node that usually feeds `View Transform`. |
| Keep `Develop` integrated finish-tone payloads, serialization, hidden pre-finish output, and Auto solve behavior unchanged in this pass. | Complete | `docs/engineering/develop/DEVELOP_SOURCE_MAP.md`; `src/Renderer/RenderPipeline.cpp`; `src/Editor/EditorModule.cpp` | The standalone node work is additive; it does not replace or rename `Develop`'s embedded tone path. |
| Add practical validation for the restored standalone tone-curve node. | Complete | `src/main.cpp` | `--validate-tone-curve-auto` still passes, and the manual RAW chain smoke now routes through standalone `Tone Curve`. |
| Implement the full Guide 08 finish-tone strategy, highlight shoulder/shadow toe redesign, and mode-specific final tone graph controls. | Deferred | `docs/engineering/develop/DEVELOP_DEFERRED_SCOPE.md` | This pass restores manual finish-curve substrate only. The broader integrated finish-tone redesign remains future Guide 08 work. |

### Guide 03 Increment - RAW White-Balance Candidate Probes

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Added first-pass categorical RAW white-balance candidate probes: `wbDaylightCorrection`, `wbNeutralCorrection`, and `wbCameraMood` mapping.
- `wbDaylightCorrection` maps to `Raw::WhiteBalanceMode::Auto`, `wbNeutralCorrection` maps to `Neutral`, and `wbCameraMood` maps to `AsShot`.
- The solver generates daylight and neutral probes when RAW camera/daylight/neutral metadata differs enough to make a comparison meaningful.
- WB probes are classified as `rawGlobal`, included in candidate fingerprints, preserved through candidate readback/carry-forward where appropriate, and applied to final authored RAW settings when selected.
- Score components now include `colorPlausibility` and `moodColorPreservation`; WB probes use those dimensions without claiming full color understanding.
- Candidate diagnostics write `rawOverrides.whiteBalanceMode`, `changes.whiteBalanceMode`, selected WB probe/mode, and authored WB probe/mode fields.
- The render request path reads WB overrides, gives WB probes bounded priority/diversity bonuses, applies actual RAW WB mode changes in copied candidate payloads, and writes `autoCandidateWhiteBalanceProbe` / `autoCandidateWhiteBalanceMode`.
- A vector invalidation bug in the parameter merge path was fixed by copying top candidates before appending the merged candidate; additional WB candidates made the old reference-after-push pattern unsafe.
- Added `build\Stack.exe --validate-develop-auto-solve` as a solver-only validation entry point.

Explicitly not implemented yet:

- No full Guide 06 color/WB/mood/skin/memory-color engine.
- No color graph controls or camera-profile/color-management redesign.
- No subject-aware color protection, skin protection, or memory-color protection.
- No user-visible candidate gallery, candidate picker, or user WB merge UI.
- No final-pixel blending.

Tests / validation run:

- `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed.
- `build\Stack.exe --validate-develop-auto-solve` passed.
- `build\Stack.exe --validate-develop-node-smoke` passed.

Notes / risks:

- The probes are real RAW WB mode alternatives inside the current candidate solver, but they are intentionally categorical and are not blended in authored merge candidates.
- `wbCameraMood` is mapped/scored/diagnosed for carried or synthetic states, but current generation may not produce it often because the base authored solve already uses As Shot when useful camera WB metadata exists.
- This is the first Guide 06 processing foothold, not a complete color strategy.

### Guide 03 Increment - Finish-Tone Candidate Probes

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Added authored finish-tone candidate probes: `toneSofterRolloff`, `tonePunchierShape`, `toneFlatterEditing`, and `toneDarkerToe`.
- The probes are generated from existing render-feedback stats and selected Auto intent, then scored with the same parameter-candidate pipeline and `ParameterScoreComponentsV1` diagnostics.
- The probes are classified as `finishTone`, so candidate render payload mapping freezes RAW and Scene Prep and varies only downstream integrated Tone guidance.
- Candidate render request selection gives finish-tone probes a small priority and treats them as relevant to finish-tone revision-stage validation.
- Render payload diagnostics write `autoCandidateFinishToneProbe`.
- Smoke validation checks finish-tone probe generation, eligibility, human-readable labels/reasons, meaningful tone deltas, stage-constrained render payload mapping, and active-stage/refine relevance.

Explicitly not implemented yet:

- No full Guide 08 finish-tone redesign, tone graph controls, or mode-specific final tone graph.
- No candidate gallery, thumbnail cache, or user candidate picker.
- No user-driven tone candidate merge UI.
- No final-pixel blending.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including finish-tone probe generation and render-payload constraint validation.

Notes / risks:

- These probes are conservative authored guidance states over the existing integrated ToneCurve path. They improve actual rendered candidate comparison for downstream tone decisions, but they do not complete the broader tone strategy in Guide 08.

### Guide 03 Increment - Active Refine-Intent Render Reservation

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.h`
- `src/Editor/EditorRenderWorker.h`
- `src/Editor/EditorRenderWorker.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Added `IsDevelopCandidateRelevantToRenderedRefineIntent`, mapping each active rendered refine intent to the existing authored/rendered-local repair families that can validate it.
- `BuildDevelopCandidateRenderRequests` now reserves one bounded non-selected render slot for the active refine intent when the selected render set does not already cover it.
- Candidate render requests/results now carry `activeRefineIntent`, `activeRefineIntentMatch`, and `refineIntentReservedRequest`.
- `RenderMetricsV1` diagnostics write per-candidate refine-intent match/reservation fields and aggregate active-refine/reserved request counts.
- Smoke validation checks the refine-intent relevance map for highlight protection, highlight-protected mids, cleaner shadows, shadow opening, contrast shaping, and negative mismatches.

Explicitly not implemented yet:

- No expansion of the bounded candidate render budget.
- No new candidate families beyond the existing authored/rendered-local repairs.
- No gallery, user picker, or visual compare UI.
- No full staged controller or sidecar stats bus.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including active refine-intent candidate relevance validation.

Notes / risks:

- This makes the current rendered-feedback loop more likely to validate the repair it requested. It is still a bounded heuristic selection step over the existing render worker, not a full convergence scheduler.

### Guide 03 Increment - Rendered Refine Monotonic Risk Guard

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/main.cpp`

Implemented:

- `ReadDevelopRenderedMetricsFromJson` now reads `localDamageRiskScore3x3`, `localDamageRiskMean`, `localDamageRiskPeak`, and `localDamageRiskPeakTile` back from `RenderMetricsV1`, so Auto-side convergence/stability comparisons use the same compact spatial-risk signal that candidate rendering writes.
- Added `TryReadLastSameIntentRefineMetrics` and `EvaluateRenderedRefineMonotonicGuard` to compare the current selected rendered metrics against the last same-intent rendered refinement.
- Repeated `openShadows` / `cleanShadows` refinements now stop with `renderedRefineMonotonicShadowRisk` if shadow texture, local shadow pressure, or local damage risk worsens without a clear score gain.
- Repeated `brightenMids` / `protectHighlights`, `addContrast`, and `preserveTexture` refinements have analogous clipping/highlight, halo/contrast, and texture-risk guards.
- Diagnostics now write `autoCandidateRenderedMonotonicGuardStatus`, metric name, previous/current values, and reference id, and `RenderedFeedbackLoopV1` includes the compact monotonic guard summary.
- Smoke validation includes a synthetic same-intent cleaner-shadow refinement whose shadow texture risk worsens; Auto converges/stops instead of applying another refinement.

Explicitly not implemented yet:

- No new candidate families, render budget expansion, gallery UI, or visual risk maps.
- No full scheduled convergence controller or subject-aware spatial map.
- No user preference-learning application.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including rendered monotonic shadow-risk guard validation.

Notes / risks:

- Thresholds are conservative and only stop repeated same-intent refinements when the protected risk materially worsens. This is an anti-oscillation guard, not a complete perceptual convergence model.

### Guide 03 Increment - Rendered Spatial Risk Refinement

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- `ResolveDevelopRenderedRefineIntent` now uses compact local damage-risk peak/mean metrics as an active rendered-feedback signal, not only as a ranking/rejection signal.
- Bright-region spatial hotspots route to `protectHighlights` when local damage risk aligns with localized highlight pressure.
- High-contrast spatial hotspots route to `protectHighlights` when local damage risk aligns with edge or contrast stress.
- Shadow spatial hotspots route to `cleanShadows` when texture/noise pressure is present, or `openShadows` when the hotspot is shadow-crowded without matching highlight pressure.
- Flat-gray spatial hotspots route to `addContrast` when local damage risk aligns with low local contrast and low saturation without clipping pressure.
- Smoke validation now checks the spatial hotspot branches and requires their reasons to come from the spatial path.

Explicitly not implemented yet:

- No new rendered-local candidate family beyond the existing authored refine intents.
- No full local EV map, heatmap overlay, subject-region analysis, or visual diagnostic UI.
- No candidate gallery, user picker, or final-pixel blending.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including spatial-risk-driven rendered refine-intent routing.

Notes / risks:

- This makes the existing compact spatial metric actionable for follow-up solves. It remains heuristic and should be replaced or augmented by richer spatial/perceptual maps in later Guide 04/10 work.

### Guide 03 Increment - Rendered Spatial Risk Metrics

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorRenderWorker.h`
- `src/Editor/EditorRenderWorker.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- `DevelopCandidateRenderMetrics` now carries compact 3x3 `localDamageRiskScore` values plus mean, peak, and peak-tile summary fields.
- `AnalyzeDevelopCandidatePixels` derives the compact regional risk from existing tile statistics: highlight crowding, shadow crowding, local edge/contrast stress, and flat-gray pressure.
- `CompareDevelopCandidateRenderMetrics` includes local damage-risk distance so duplicate clustering and rendered stability checks can notice regional damage-risk changes.
- `ScoreDevelopRenderedCandidateMetrics` applies a conservative local safety penalty from damage-risk peak/mean fields.
- `ClassifyDevelopRenderedCandidateDamage` can reject candidates with a clear localized damage hotspot when regional risk aligns with highlight pressure, shadow pressure, or excessive local contrast.
- `RenderMetricsV1` diagnostics now serialize `localDamageRiskScore3x3`, `localDamageRiskMean`, `localDamageRiskPeak`, and `localDamageRiskPeakTile` for final and pre-finish candidate metrics.
- Smoke validation checks spatial-risk metric population and a synthetic localized hotspot rejection.

Explicitly not implemented yet:

- No full spatial EV map, perceptual damage map, halo overlay, or visual heatmap UI.
- No subject-region or skin/memory-color damage analysis.
- No candidate gallery, thumbnail cache, hover preview, side-by-side compare, or user picker.
- No final-pixel candidate blending.

Tests / validation run:

- First `cmake --build build --config Release` reached link and failed with `LNK1104` opening `build\Stack.exe`; no running `Stack` process was found, and a retry succeeded.
- `cmake --build build --config Release` passed on retry.
- `build/Stack.exe --validate-develop-node-smoke` passed, including rendered spatial-risk metric validation.

Notes / risks:

- This is a compact processing/diagnostic summary over the existing 3x3 tile metrics. It is intentionally not the full spatial damage map described by the guides.
- Thresholds are conservative so the new signal nudges ranking/rejection only when a regional hotspot is clearly stressed.

### Guide 03 Increment - Rendered Ensemble Merge Feedback

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Rendered candidate analysis now identifies a conservative three-survivor ensemble merge opportunity when the top three non-damaged, non-duplicate rendered survivors are strong, close enough in score, and meaningfully different in rendered metrics.
- Rendered diagnostics write `autoCandidateRenderedEnsembleMergeSuggested`, source ids/labels/scores, pairwise metric distances, metric spread, score spread, and per-candidate `renderedMergeRole` values for ensemble sources.
- `ApplyRenderedCandidateFeedbackToSolve` can synthesize `renderedFeedbackEnsembleMerge`, a weighted authored settings-space merge of the three rendered survivors.
- The ensemble merge uses normalized/clamped weights from rendered scores, records first/second/third merge ids and weights, and feeds the normal RAW + Scene Prep + integrated Tone solve.
- Smoke validation checks the ensemble selection source, selected candidate id, three source ids, normalized weights, rendered feedback action, pass/fingerprint bookkeeping, and pending follow-up render status.

Explicitly not implemented yet:

- No thumbnail gallery, persistent candidate cache, hover preview, side-by-side compare, or user candidate picker.
- No manual user-driven merge UI.
- No final-image pixel blending.
- No full scheduled convergence controller across all future candidate families.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including rendered ensemble-merge feedback validation.

### Guide 03 Increment - Rendered Feedback Loop State Record

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Added `RenderedFeedbackLoopV1`, a compact integrated-tone JSON record for the rendered-feedback loop.
- The solve side now writes loop state when it is awaiting rendered metrics, has applied rendered feedback, has stopped, or has converged.
- The render-feedback side now writes a preliminary loop record when actual candidate renders request another Auto solve or stop without applying another solve.
- The loop record includes pass, next pass, max passes, solve/rendered/applied fingerprints, action, stop reason, next step, selected/best ids and scores, revision stage/reason, history count, and whether another render or Auto solve is required.
- Smoke validation now checks the initial awaiting-metrics state, active adoption state, stable convergence state, and no-improvement trend convergence state.

Explicitly not implemented yet:

- No user-visible candidate gallery, thumbnail cache, hover preview, or picker.
- No final-image pixel blending.
- No full scheduled convergence controller with richer spatial/perceptual maps.
- No sidecar stats bus or standalone staged renderer.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including `RenderedFeedbackLoopV1` loop-state validation.

Notes / risks:

- This is a durable loop-state contract over the existing rendered-feedback system. It does not by itself increase the candidate pass budget or make the solver complete.
- Future convergence work should start from `autoCandidateRenderedFeedbackLoop` instead of inventing parallel pass/status fields.

### Guide 03 Increment - Bounded RawDevelop Stage Snapshot Cache

Last touched: 2026-06-13

Code locations changed:

- `src/Renderer/RenderPipeline.h`
- `src/Renderer/RenderPipeline.cpp`
- `src/main.cpp`

Implemented:

- Added `m_RawDevelopStageImageCache`, a bounded per-node/socket cache of owned RawDevelop raw-base and hidden pre-finish texture snapshots keyed by graph fingerprints.
- RawGpuPipeline output is still owned/reused by `RawGpuPipeline`, so raw-base stage cache entries are cloned before storage instead of aliasing the volatile internal output texture.
- Hidden pre-finish cache entries are also cloned before storage so finish-tone-only probes can reuse older pre-finish boundaries even after scene-prep or RAW-dirty candidate renders churn the single graph cache.
- Stage-cache hits repopulate the existing graph image cache as non-owned entries and contribute to `WasGraphImageCacheHit`, so current candidate cache-hit telemetry and stage-cache validation benefit without a new public API.
- Cached graph image entries now remember width/height, and cached readback uses those stored dimensions when available.
- Stage snapshots are invalidated with the existing graph caches and pruned for inactive graph nodes.
- Stage snapshot retention is now memory-aware: small RAWs can keep up to six fingerprints per raw-base/pre-finish key, medium/large/huge snapshots keep fewer entries, oversized single snapshots are skipped, and the total RawDevelop stage snapshot cache trims to a 512 MiB soft estimated RGBA16F byte budget.
- Develop smoke validation now deliberately churns the single graph cache, then verifies raw-base and pre-finish stage-cache reuse remains observable.
- Develop Auto validation now covers the stage-cache memory policy through validation-only byte-estimate, per-key-cap, and cache/no-cache helpers.

Explicitly not implemented yet:

- No full RAW/global/scene-prep/finish stage controller.
- No sidecar stats bus.
- No candidate thumbnail/gallery cache.
- No user-facing cache diagnostics, memory timeline, or graph-control UI.
- No final-pixel blending or staged View Transform behavior.

Tests / validation run:

- Historical initial implementation validation: first `cmake --build build --config Release` failed on a local C++ name collision in the new raw-base hit branch; fixed. `cmake --build build --config Release` passed. `build/Stack.exe --validate-develop-node-smoke` passed, including raw-base and pre-finish stage-cache reuse after graph-cache churn.
- 2026-06-13 memory-policy validation: `$env:CL='/FS'; cmake --build build --config Debug --target Stack -- /m:1` passed; `build\Stack.exe --validate-develop-auto-solve` passed, including stage-cache memory policy coverage; `build\Stack.exe --validate-develop-node-smoke` passed; `build\Stack.exe --validate-develop-real-raw-smoke "RAW IMAGES\motion cam pro.dng"` passed; scoped `git diff --check` reported only line-ending warnings.

Notes / risks:

- This is a real owned texture snapshot cache for the current RawDevelop raw-base and pre-finish boundaries, not the final Guide 03 staged processing architecture.
- Cache entries are bounded and invalidated with graph caches. Large RAWs may intentionally lose stage-cache hits sooner than small RAWs to avoid retaining too many full-resolution RGBA16F boundaries.
- There is still no cross-stage sidecar stats bus or standalone scheduler that can independently execute named solver states.

### Guide 03 Increment - Candidate Stage Scheduler

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.h`
- `src/Editor/EditorRenderWorker.h`
- `src/Editor/EditorRenderWorker.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Candidate render requests now carry scheduler metadata: expected dirty boundary, scheduler rank/order, and a plain-language scheduler reason.
- `BuildDevelopCandidateRenderRequests` keeps the selected authored candidate first, then orders non-selected candidates by expected staged reuse: finish-tone probes first, scene-prep probes next, then RAW/global, RAW cleanup, and multi-stage probes.
- The ordering is designed around the current graph cache behavior where raw-base and pre-finish cache entries are single entries per node/socket and can be replaced by upstream-dirty candidate renders.
- `ApplyDevelopCandidateRenderFeedback` writes per-candidate scheduler fields and top-level `StageSchedulerV1` counts/status into integrated tone JSON.
- Smoke validation now checks the scheduler rank/boundary classifier: selected first, finish-tone before scene-prep, scene-prep before RAW-dirty, and expected dirty-boundary labels.

Explicitly not implemented yet:

- No physical RAW/global/scene-prep/finish cache split.
- No sidecar stats bus or standalone staged render controller.
- No user-visible candidate gallery, thumbnail cache, or picker.
- No final-pixel candidate blending.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including stage-scheduler validation.

Notes / risks:

- This is an actual render scheduling step over the current worker pipeline, not a full staged renderer. Since the current graph cache still stores one entry per node/socket, downstream probes benefit most when they render immediately after the selected baseline and before upstream-dirty probes.

### Guide 03 Increment - Candidate Stage Cache Validation

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.h`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Candidate rendered feedback now derives an observed dirty boundary from final-render cache hits: `rawBase`, `scenePrep`, or `finishTone`.
- Scene-prep candidate renders validate that RAW base was reused while allowing pre-finish to rerender.
- Finish-tone candidate renders validate that both RAW base and pre-finish boundaries were reused.
- Per-candidate diagnostics now write `observedDirtyBoundary`, expected boundary/reuse fields, expectation-met fields, validation status, and a plain-language validation reason.
- Top-level rendered diagnostics now write observed dirty-boundary counts and stage-cache validation counts/status.
- Smoke validation now checks the dirty-boundary classifier and expected scene-prep / finish-tone cache-reuse outcomes.

Explicitly not implemented yet:

- No physical RAW/global/scene-prep/finish cache split.
- No staged render scheduler or sidecar stats bus.
- No cache-control UI or performance timeline.
- No thumbnail/gallery cache.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including rendered stage-cache validation.

### Guide 03 Increment - Candidate Stage Cache Hit Telemetry

Last touched: 2026-06-11

Code locations changed:

- `src/Renderer/RenderPipeline.h`
- `src/Renderer/RenderPipeline.cpp`
- `src/Editor/EditorRenderWorker.h`
- `src/Editor/EditorRenderWorker.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`

Implemented:

- `RenderPipeline` now records which graph image cache keys were hit during the latest `ExecuteGraph` call.
- Generic graph image cache hits and the RawDevelop raw-base fast path both contribute to the latest-hit set.
- Candidate final renders capture whether the raw-base boundary and hidden pre-finish boundary were cache hits during that final render.
- `RenderMetricsV1` diagnostics now write per-candidate `rawBaseCacheHitDuringFinalRender` and `preFinishCacheHitDuringFinalRender`.
- Top-level rendered diagnostics write raw-base and pre-finish final-render cache-hit counts plus `none` / `missed` / `partial` / `all` statuses.

Explicitly not implemented yet:

- No physical RAW/global/scene-prep/finish cache split.
- No staged render scheduler or sidecar stats bus.
- No cache-control UI or performance timeline.
- No thumbnail/gallery cache.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed. The cache-hit path is covered by compile-time integration and the existing render-worker path; there is not yet a dedicated GL cache-hit unit test.

### Guide 03 Increment - Rendered Rejection Memory

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/Editor/EditorRenderWorker.h`
- `src/Editor/EditorRenderWorker.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Added a per-candidate `guidanceFingerprint` for authored candidate diagnostics and rendered candidate requests/results.
- `ApplyDevelopCandidateRenderFeedback` now records bounded `autoCandidateRenderedRejectionMemory` entries when actual rendered metrics reject a candidate as damaged.
- Rendered rejection memory is keyed by candidate id plus guidance fingerprint, so only the same authored candidate state is suppressed; a future candidate with changed guidance can still be tested.
- `BuildDevelopAutoCandidateSolve` now reads rendered rejection memory and rejects matching candidates with `renderedMemoryRejected = true` before they can survive into selection.
- Candidate diagnostics and learning events now include the guidance fingerprint and rendered-memory suppression count.

Explicitly not implemented yet:

- No global/user preference-learning application.
- No gallery rejection UI or user-driven rejection memory.
- No full perceptual/spatial damage map.
- No physical staged render scheduler.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including a synthetic rendered-damage memory fixture that suppresses the same authored survivor on the next Auto solve.

### Guide 03 Increment - Active Stage Candidate Render Slot Reservation

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.h`
- `src/Editor/EditorRenderWorker.h`
- `src/Editor/EditorRenderWorker.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Added `IsDevelopCandidateRelevantToRevisionStage`, with a smoke-exposed validation wrapper, so candidate render selection can ask whether a survivor actually belongs to the current responsible revision stage.
- `BuildDevelopCandidateRenderRequests` now tracks each candidate's revision stage, whether it matches the active responsible stage, and whether it consumed the reserved stage slot.
- If previous rendered feedback names a responsible stage and the selected render set does not already include a relevant candidate, the bounded request builder reserves one remaining render slot for the best active-stage-relevant survivor before filling generic diversity slots.
- Candidate render requests/results now carry `candidateRevisionStage`, `activeRevisionStage`, `activeStageMatch`, and `stageReservedRequest`.
- `RenderMetricsV1` diagnostics write the per-candidate stage fields plus aggregate `autoCandidateRenderedActiveStageRequestCount` and `autoCandidateRenderedStageReservedRequestCount`.

Explicitly not implemented yet:

- No full physical staged render scheduler.
- No candidate gallery or user picker.
- No new candidate family beyond the existing authored candidate set.
- No final-pixel blending.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including active responsible-stage render-slot relevance validation.

### Guide 03 Increment - Cached Pre-Finish Candidate Metric Readback

Last touched: 2026-06-11

Code locations changed:

- `src/Renderer/RenderPipeline.h`
- `src/Renderer/RenderPipeline.cpp`
- `src/Editor/EditorRenderWorker.h`
- `src/Editor/EditorRenderWorker.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`

Implemented:

- Added `RenderPipeline::GetCachedGraphImagePixels`, a readback helper for cached graph image textures.
- Candidate final renders already compute the hidden `preFinishImageOut` boundary when integrated finish tone is active; the render worker now tries to read that cached pre-finish texture directly for pre-finish metrics.
- The worker falls back to the previous second graph execution for `preFinishImageOut` if the cached texture is unavailable.
- `DevelopCandidateRenderResult` now records whether pre-finish metrics reused the final render's cached boundary.
- `RenderMetricsV1` diagnostics now write per-candidate `preFinishReusedFromFinalRender`, plus top-level `autoCandidateRenderedPreFinishReuseCount` and `autoCandidateRenderedPreFinishReuseStatus`.

Explicitly not implemented yet:

- No full physical RAW/global/scene-prep/finish cache scheduler.
- No thumbnail/gallery cache.
- No visual stage preview UI.
- No final-pixel blending.

Tests / validation run:

- First `cmake --build build --config Release` attempt was blocked by a running `build/Stack.exe`; stopped the stale workspace process and reran successfully.
- `build/Stack.exe --validate-develop-node-smoke` passed.

### Guide 03 Increment - Pre-Finish-Aware Rendered Duplicate Clustering

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.h`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Added `EvaluateDevelopRenderedCandidateDuplicate`, a rendered-candidate duplicate decision helper that compares final metrics first and then checks hidden pre-finish metrics when the final render is near-duplicate.
- Final-near-duplicate candidates are still rejected when both final and pre-finish metrics are close.
- Final-near-duplicate candidates now survive when both candidates have pre-finish metrics and the hidden pre-finish boundary differs meaningfully, so finish tone cannot erase upstream candidate differences before stage-aware feedback sees them.
- Rendered candidate diagnostics now record `duplicateFinalMetricDistance`, `duplicatePreFinishMetricDistance`, `preFinishDistinctFrom`, `preFinishDistinctFinalMetricDistance`, `preFinishDistinctMetricDistance`, `preFinishDistinctReason`, `autoCandidateRenderedPreFinishDistinctDistance`, and `autoCandidateRenderedPreFinishDistinctSurvivorCount`.
- Smoke validation covers both the normal duplicate case and the final-masked pre-finish-distinct survivor case through `ShouldTreatDevelopRenderedCandidateAsDuplicateForValidation`.

Explicitly not implemented yet:

- No candidate gallery, thumbnail cache, or user picker.
- No physical staged cache split or staged render scheduler.
- No spatial EV/damage map.
- No final-pixel blending.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including stage-aware rendered duplicate clustering validation.

### Guide 03 Increment - Pre-Finish Candidate Metrics and Stage-Boundary Feedback

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorRenderWorker.h`
- `src/Editor/EditorRenderWorker.cpp`
- `src/Editor/EditorModule.cpp`
- `src/Editor/EditorModule.h`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Candidate render requests now default to measuring the hidden `preFinishImageOut` boundary as well as the final image output.
- `EditorRenderWorker::DevelopCandidateRenderResult` now carries `preFinishSuccess`, pre-finish dimensions, and compact pre-finish metrics.
- `ApplyDevelopCandidateRenderFeedback` writes per-candidate `preFinishMetrics`, `finalVsPreFinishMetricDistance`, top-level `autoCandidateRenderedPreFinishCount`, selected-vs-best final/pre-finish distances, and `autoCandidateRenderedStageBoundarySignal`.
- `ClassifyDevelopRenderedStageBoundary` classifies whether selected-vs-best rendered differences appear only after finish tone, already before finish, or are stable/missing.
- Rendered feedback now preserves a `finishTone` revision target when final metrics diverge but pre-finish metrics remain close, preventing a final-tone-only difference from being misattributed to RAW or scene prep.
- Pre-finish selected metrics can steer early-stage refinements when the pre-finish boundary already shows brightness, shadow, highlight, cleanup, or texture problems.
- Smoke validation covers finish-tone-only and pre-finish-changed boundary classification, plus the actual rendered-feedback-to-authored-solve stage override.

Explicitly not implemented yet:

- No physical RAW/global/scene-prep/finish cache split.
- No staged render scheduler that reuses only named cache boundaries.
- No full spatial EV/damage map or subject-region metric pass.
- No candidate gallery or user candidate picker.
- No final-pixel blending.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including pre-finish stage-boundary classifier and finish-tone stage override validation.

### Guide 03 Increment - Stage-Constrained Candidate Render Payloads

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- `ApplyDevelopGuidanceToCandidateRenderPayload` now uses the responsible candidate stage from `DevelopRenderedRevisionStageForCandidateId` to constrain what each rendered candidate probe is allowed to change.
- Scene-prep candidate renders such as `renderedLocalBrightenMids` preserve RAW-stage exposure/highlight/WB placement while still varying scene prep and integrated tone guidance.
- Finish-tone candidate renders such as `renderedLocalContrastShape` preserve RAW-stage placement and scene-prep settings while varying integrated finish/tone guidance.
- Cleanup/detail candidate renders preserve global RAW placement while allowing existing cleanup/detail fields such as mosaic denoise, false-color cleanup, defringe, highlight-edge cleanup, and preserve-real-color to vary.
- Candidate payload diagnostics now write `autoCandidateStageConstraint`, `autoCandidateStageConstraintApplied`, `autoCandidateStageConstraintFrozenRaw`, `autoCandidateStageConstraintFrozenScenePrep`, and, where useful, `autoCandidateStageConstraintReason`.
- Smoke validation now checks scene-prep and finish-tone stage constraints directly through `BuildDevelopCandidateRenderPayloadForValidation`.

Explicitly not implemented yet:

- No physical RAW/global/scene-prep/finish cache split.
- No staged render scheduler that rerenders only the constrained physical stage.
- No candidate gallery, user picker, or merge UI.
- No final-pixel blending.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including scene-prep and finish-tone candidate stage-constraint validation.

### Guide 03 Increment - Logical Staged Auto Solve Record

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/main.cpp`

Implemented:

- Auto now writes `autoStageSolveVersion = "StagedAutoSolveV1"` into the Develop integrated tone JSON.
- The stage record includes the documented stage sequence: `NEED_SOURCE`, `METADATA_BOOTSTRAP`, `RENDER_RAW_BASE`, `ANALYZE_RAW_BASE`, `SOLVE_GLOBAL`, `RENDER_GLOBAL_BASE`, `SOLVE_SCENE_PREP`, `RENDER_PREFINISH`, `ANALYZE_PREFINISH`, `SOLVE_FINISH_TONE`, `RENDER_FINAL`, `VALIDATE_FINAL`, and `CONVERGED`.
- `BuildDevelopAutoStageFingerprints` records logical fingerprints for metadata, raw base, raw/global, scene prep, finish tone, and final validation from the actually authored payload and solve result.
- `WriteDevelopAutoStageSolveDiagnostics` records pass-budget metadata, the current pass kind, the earliest dirty cache boundary, responsible rendered-feedback revision state, rendered-metrics requirement, and validation state.
- The record is honest about the current architecture: `autoStageCurrentRawExposureInsideRawBase` is true and `autoStageCacheSplitStatus` says the current RawGpuPipeline still renders RAW exposure inside the raw-base boundary.
- Smoke validation checks the stage sequence, nonzero fingerprints, staged validation state, and rendered feedback mappings for scene-prep and raw-cleanup refinements.

Explicitly not implemented yet:

- No physical RAW/global/scene-prep/finish cache split.
- No sidecar stats bus.
- No staged render scheduler that rerenders only one physical cache boundary.
- No visual stage preview UI.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including staged solve/fingerprint validation.

### Guide 03 Increment - Stage-Aware Rendered Feedback Targeting

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Rendered feedback now classifies the responsible revision stage as `rawGlobal`, `scenePrep`, `finishTone`, `rawCleanup`, `multiStage`, `converged`, or `none`.
- `ApplyRenderedCandidateFeedbackToSolve` writes stage/reason metadata when it adopts, merges, pair-merges, or refines from rendered metrics.
- `ApplyDevelopCandidateRenderFeedback` writes provisional stage/reason metadata as soon as rendered metrics request a follow-up solve.
- `BuildDevelopCandidateRenderRequests` gives a small render-slot priority bonus to surviving candidates that match the latest responsible stage, so the next actual rendered comparison is less likely to spend scarce slots on unrelated alternatives.
- `CandidateOutcomeLearningV1` rendered-feedback events now include the revision stage and reason.
- Smoke validation checks revision-stage diagnostics for rendered adoption, scene-prep refinement, and raw-cleanup refinement.

Explicitly not implemented yet:

- No physical staged RAW cache/fingerprint split.
- No staged render scheduler that rerenders only named states such as `RENDER_RAW_BASE`, `SOLVE_SCENE_PREP`, or `VALIDATE_FINAL`.
- No thumbnail gallery or user candidate picker.
- No final-pixel blending.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including rendered revision-stage validation.

### Guide 03 Increment - Rendered Survivor Carry-Forward

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/main.cpp`

Implemented:

- Active rendered-feedback iterations now collect up to four successful prior rendered survivor ids from `autoCandidateRenderedSolves`, ordered by rendered score.
- The next Auto solve can rehydrate those survivors from prior `autoCandidateSolves` as authored guidance candidates, while still skipping damaged and rendered-duplicate results.
- Existing carry-forward for the prior selected candidate, rendered-best candidate, and rendered pair-merge source candidates remains intact.
- Integrated tone diagnostics now write `autoCandidateRenderedCarriedForwardCount` so future passes can tell whether the carry-forward path contributed candidates.
- Smoke validation now covers a synthetic prior rendered survivor and checks that it is carried forward without being rejected as duplicate/damage.

Explicitly not implemented yet:

- No persistent thumbnail/gallery candidate history.
- No user candidate picker, merge UI, or manual survivor selection.
- No final-pixel blending; carried survivors remain authored settings/intent states.
- No full scheduled convergence controller across all future candidate families.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including rendered survivor carry-forward validation.

### Guide 03 Increment - Highlight-Protected Mids Candidate

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Added `highlightProtectedMids`, an authored exposure-placement candidate for broad highlight/HDR pressure.
- The candidate tests lower visible/global RAW placement while forwarding stronger dynamic-range, shadow-lift, and highlight-guard guidance so useful mids can be supported locally instead of simply darkening the image.
- Candidate scoring and mode-intent fit now recognize the family, with conservative bias toward Maximum Range / Detail, Flat Editing Base, and Dark Natural intents and a small penalty for Bright Natural.
- Candidate render request selection treats the family as an exposure-placement probe with a small priority bonus, so limited render slots can include it when it survives.
- Candidate render payload mapping keeps the existing render path, lowers RAW placement through the existing guidance delta, and nudges local midtone/highlight support without adding a separate render algorithm.
- Smoke validation checks that `highlightProtectedMids` is generated, eligible, meaningfully different, and maps to a distinct candidate render payload that lowers RAW placement and forwards its local-support guidance.

Explicitly not implemented yet:

- No full global/local exposure-placement search or candidate gallery.
- No spatial highlight/shadow damage map or local EV-map comparison.
- No subject-aware exposure priority or brush.
- No claim that clipped source data can be recovered.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including highlight-protected mids exposure-placement candidate validation.

### Guide 03 Increment - Rendered Feedback Trend Convergence

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/main.cpp`

Implemented:

- Added `EvaluateDevelopRenderedFeedbackTrend` to consume the bounded rendered-feedback history during the next Auto solve.
- The solver now records trend diagnostics in integrated tone JSON: `autoCandidateRenderedTrendHistoryCount`, `autoCandidateRenderedTrendSameBestCount`, `autoCandidateRenderedTrendScoreSpread`, `autoCandidateRenderedTrendNearestDistance`, `autoCandidateRenderedTrendReferenceId`, and `autoCandidateRenderedTrendStatus`.
- Repeated rendered feedback now stops as converged when multiple prior passes show the same/similar rendered best with a flat score trend.
- New stop reasons are `renderedFeedbackNoImprovementTrend`, `renderedRefineNoImprovementTrend`, and `renderedFeedbackStableTrend`.
- Candidate outcome learning records the trend fields on rendered-feedback stopped events.
- Smoke validation now creates a repeated rendered-history fixture and checks the no-improvement trend stop.

Explicitly not implemented yet:

- No full scheduled convergence controller with thumbnails/history UI.
- No full spatial damage map or perceptual local EV-map convergence.
- No user-facing graph/candidate gallery for inspecting trend history.
- No user preference-learning application from repeated rendered outcomes.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including rendered-feedback trend convergence validation.

### Guide 03 Increment - Parameter Score Components

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/main.cpp`

Implemented:

- Added `ParameterScoreComponentsV1` diagnostics for authored parameter candidates.
- Each serialized candidate now carries `scoreComponents` with the scalar final score, score source, candidate id, Auto intent, input signals, guidance deltas, dimensions, risks, and nearest-survivor distance.
- Dimensions currently include midtone placement, highlight integrity, shadow cleanliness, dynamic range fit, contrast shape, noise/texture quality, local artifact safety, mode-intent fit, and candidate uniqueness.
- Risk terms currently include highlight damage risk, shadow noise risk, flattening risk, and data-risk penalty.
- Carried-forward candidates preserve existing `scoreComponents` when present and fall back to an authored-state component record when loaded from older diagnostics.
- Smoke validation now checks the selected candidate's score component version, final score, required dimensions, required risk fields, and bounded uniqueness.

Explicitly not implemented yet:

- No full perceptual scoring model.
- No subject/skin/color-specific scoring dimensions beyond existing future-guide boundaries.
- No score-component UI panel or graph-control visualization.
- No spatial damage map or visual requested-vs-achieved explanation panel.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including parameter score-component validation.

### Guide 03 Increment - Candidate Outcome Learning Record

Last touched: 2026-06-11

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/main.cpp`

Implemented:

- Replaced the old `autoCandidateLearningStatus = "deferred"` placeholder with a compact `CandidateOutcomeLearningV1` record in integrated tone JSON.
- The record captures solver outcomes for generated candidates: selected, survived, rejected, remembered rejection, merged, rendered-feedback applied/stopped, and convergence events where present.
- Learning application is explicitly separated from recording: `autoCandidateLearningApplied`, `autoCandidateLearningAppliedToCurrentImage`, and `autoCandidateLearningAppliedToFutureImages` are all false.
- Current-image learning is marked `recordedOnly`; future-image learning is marked `notApplied`; user-choice learning is marked `deferredUntilCandidateSelectionUi`.
- Smoke validation checks the version, status, selected candidate event, bounded event count, and unapplied current/future learning flags.

Explicitly not implemented yet:

- No user candidate picker, rejection UI, or manual selection event source.
- No preference model, global learning store, settings toggles, reset controls, or learned-bias application.
- No future-image learning application.
- No UI for learned-bias summaries.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including candidate outcome-learning record validation.

### Guide 03 Increment - Mode-Neighbor Candidate Probes

Last touched: 2026-06-10

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Added mode-neighbor authored candidate probes inside the existing parameter-candidate solver.
- Natural Finished can now test Natural More Range, Natural Brighter Mids, and Natural More Contrast when stats indicate HDR spread, shadow/mid placement uncertainty, or safe separation opportunities.
- Other Auto modes now get a neighboring authored probe where useful: Bright Highlight Safe, Dark Readable Mids, Punchy Safer Range, Range Natural Shape, Flat Natural Shape, and Clean Texture Check.
- Mode-neighbor probes are labeled in human terms and remain authored settings; they do not change the selected Auto mode and do not introduce a pixel-blend path.
- Generic duplicate pruning prefers the labeled mode-neighbor probe over a near-identical generic candidate, while still allowing mode-neighbor candidates to cluster against each other if they are redundant.
- Candidate render request selection now gives mode-neighbor probes a small priority bonus so limited render slots can compare adjacent intent tradeoffs.
- Repeated same-intent rendered refinements now stop when previous feedback already refined that intent and the current rendered list lacks evidence that the prior move improved.
- Smoke validation now checks that a mode-neighbor candidate is generated, eligible, human-readable, and meaningfully different.

Explicitly not implemented yet:

- No graph-style mode controls or user candidate gallery.
- No user-driven merge UI, user candidate selection, or candidate learning.
- No full color/WB, subject-priority, or advanced denoise/detail candidate engine.
- No final-image pixel blending.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including mode-neighbor candidate validation and repeated-refine stop validation.

### Guide 03 Increment - Rendered Damage Rejection

Last touched: 2026-06-10

Code locations changed:

- `src/Editor/EditorModule.h`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Added a compact rendered damage classifier over existing candidate render metrics.
- Rendered candidates with broad highlight clipping/crowding, strong halo/edge-glow risk, washed-out gray flattening, noisy lifted shadows, collapsed brightness hierarchy, or overly bright highlight-heavy hierarchy are now marked `renderedRejectedDamage`.
- Damaged rendered candidates are excluded before duplicate clustering, rendered-best selection, and rendered pair-merge suggestion.
- Rendered diagnostics now include per-candidate `rejectReason`, `autoCandidateRenderedDamageCount`, and a unique count that excludes damaged candidates.
- If every rendered candidate is damaged, the loop either requests a selected-candidate refinement when compact metrics have a clear refine direction or stops explicitly with `allRenderedCandidatesRejectedForDamage`.
- Smoke validation now covers the rendered damage classifier through a public validation wrapper.

Explicitly not implemented yet:

- No true spatial damage map, halo overlay, skin/memory-color protection, local denoise map, or perceptual artifact detector.
- No user-visible candidate gallery or manual candidate rejection UI.
- No final-image pixel blending.
- No claim that clipped source data can be recovered.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including rendered damage classifier validation.

### Guide 03 Increment - Rendered Pair Merge Feedback

Last touched: 2026-06-10

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Rendered candidate analysis now identifies the two strongest non-duplicate rendered survivors and records a pair-merge suggestion when both are strong, close enough in score, and meaningfully different in rendered metrics or authored guidance.
- Rendered candidate diagnostics now include pair-merge source ids, labels, scores, metric distance, and per-candidate `renderedMergeRole` markers.
- `ApplyRenderedCandidateFeedbackToSolve` can synthesize `renderedFeedbackPairMerge`, an authored settings-space merge of the two rendered survivors.
- Pair merge uses guidance interpolation and feeds the normal RAW + Scene Prep + integrated Tone solve; it does not blend final pixels.
- Repeated rendered-merge no-gain stopping now recognizes both `renderedFeedbackMerge` and `renderedFeedbackPairMerge`.
- Smoke validation now covers the pair-merge selection source, authored merge ids, rendered feedback action, and pass/fingerprint bookkeeping.

Explicitly not implemented yet:

- No thumbnail gallery, persistent candidate cache, hover preview, side-by-side compare, or user candidate picker.
- No manual user-driven merge UI.
- No final-image pixel blending.
- No full scheduled multi-pass convergence engine across all future candidate families.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including rendered pair-merge feedback validation.

### Guide 03 Increment - Rendered Cleanup / Texture Refinement Intents

Last touched: 2026-06-10

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Added rendered refine intents for `cleanShadows` and `preserveTexture` alongside the existing brighten/shadow/highlight/contrast refine intents.
- Added rendered-local authored candidate families `renderedLocalCleanShadows` and `renderedLocalPreserveTexture`.
- Let compact rendered metrics request cleaner-shadow refinement when shadow texture/noise pressure is high without matching highlight trouble.
- Let compact rendered metrics request texture-preserving cleanup when tones are safe but fine separation looks subdued by the current proxies.
- Reused the existing cleanup/detail candidate render-payload mapping so rendered-local cleanup candidates materially change mosaic denoise, cleanup, Scene Prep noise/detail protection, and tone diagnostics during candidate rendering.
- Hardened rendered-feedback stale checking so refine feedback can match the solve fingerprint of the rendered base state, while ordinary adoption/merge feedback still requires the current preliminary solve fingerprint.
- Added smoke coverage for the new refine-intent resolution, generated rendered-local cleanup candidate, selected cleanup refinement, and existing repeated-refine no-improvement stop behavior.

Explicitly not implemented yet:

- No full texture detector, local denoise map, or perceptual detail map.
- No post-demosaic denoise engine or Guide 07 denoise/demosaic overhaul.
- No candidate gallery, thumbnail cache, or user candidate picker.
- No user preference learning from accepted/rejected cleanup/detail candidates.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including `cleanShadows` / `preserveTexture` rendered refine-intent checks and `renderedLocalCleanShadows` feedback selection validation.

### Guide 03 Increment - Cleanup / Texture Render Probes

Last touched: 2026-06-10

Code locations changed:

- `src/Editor/EditorModule.cpp`
- `src/Editor/EditorModule.h`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/main.cpp`

Implemented:

- Added `preserveTexture` as a first noise/detail candidate family generated from texture-confidence and noise-risk stats.
- Preserved cleanup/detail probes through generic duplicate pruning so `cleanShadows` and `preserveTexture` can remain available even when their visible guidance is near another family.
- Prioritized cleanup/detail probes in candidate render request selection so limited rendered slots can compare clean-versus-textured tradeoffs.
- Made candidate render payloads actually differ for cleanup/detail probes: cleaner-shadow probes strengthen mosaic denoise/cleanup and noise protection, while preserve-texture probes reduce smoothing pressure and increase texture/detail protection.
- Added `autoCandidateCleanupProbe` diagnostics on candidate render payloads.
- Added validation coverage proving `cleanShadows` and `preserveTexture` are generated and that their candidate render payloads diverge in mosaic denoise, false-color cleanup, preserve-real-color, and scene-prep texture sensitivity.

Explicitly not implemented yet:

- No full Guide 07 denoise/demosaic/detail redesign.
- No post-demosaic denoise engine.
- No local denoise map, subject-protected detail map, or texture map UI.
- No thumbnail gallery or user candidate picker.
- No final-image pixel blending.

Tests / validation run:

- `cmake --build build --config Release` passed.
- `build/Stack.exe --validate-develop-node-smoke` passed, including clean-versus-texture candidate render-payload divergence validation.



