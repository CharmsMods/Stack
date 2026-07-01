# MFSR Repo Notes

## Current MFSR State
- Phase 0 docs foundation exists in this folder.
- Phase 1 inert contracts live in `src/MFSR/MFSRTypes.h` and `src/MFSR/MFSRTypes.cpp`.
- Phase 1 focused validation tests live in `tools/graph_behavior_tests.cpp`.
- Phase 2 inert MFSR graph node shell exists.
- No MFSR tab, decode, alignment, fusion, real render/cache, or GPU implementation exists yet.

## Graph Model Map
- Core graph API: `src/Editor/NodeGraph/EditorNodeGraph.h` and `src/Editor/NodeGraph/EditorNodeGraph.cpp`.
- Graph node kinds, socket ids, socket types: `src/Editor/NodeGraph/NodeGraphTypes.h`.
- Editor graph payload structs: `src/Editor/NodeGraph/NodeGraphPayloads.h`.
- Node metadata, socket definitions, node catalog entries, and prototype nodes: `src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp`.
- Connection rules and socket validation: `src/Editor/NodeGraph/Model/EditorNodeGraphMutation.cpp`.
- Completed-chain traversal and reference-source resolution: `src/Editor/NodeGraph/Model/EditorNodeGraphTraversal.cpp`.
- Graph validation and render-chain classification: `src/Editor/NodeGraph/Model/EditorNodeGraphLayoutValidation.cpp`.
- Save/load serialization: `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp` plus helpers in `src/Editor/NodeGraph/Serialization/`.

## Editor Integration Map
- Node creation wrappers are in `src/Editor/Internal/EditorModuleGraphProcessingNodes.cpp`.
- Node add/connect/remove helpers and scene-path analysis are in `src/Editor/Internal/EditorModuleGraphMutation.cpp`.
- The node browser and context-menu entry points are in `src/Editor/NodeGraph/UI/EditorNodeGraphUINodeBrowser.cpp`, `src/Editor/NodeGraph/UI/EditorNodeGraphUICanvas.cpp`, and `src/Editor/NodeGraph/UI/EditorNodeGraphUIContextMenu.cpp`.
- Node rendering/labels/styles are in `src/Editor/NodeGraph/UI/EditorNodeGraphUINodes.cpp` and `src/Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.cpp`.
- Render snapshot conversion from editor graph to renderer graph is in `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`.

## Renderer Map
- Render graph node kinds and payloads live in `src/Renderer/MaskRenderTypes.h`.
- Render graph execution and fingerprinting are centered in `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`.
- HDR Merge is the nearest existing multi-input image-render node pattern: `src/Renderer/Internal/RenderPipelineGraphHdrMerge.cpp` and `src/Renderer/Internal/RenderPipelineHdrMergePass.cpp`.
- RAW Develop is the nearest RAW-to-image path pattern: `src/Renderer/Internal/RenderPipelineGraphRawDevelopNode.cpp` and RAW stage helpers under `src/Renderer/Internal/RenderPipelineGraphRawStages.cpp`.

## Phase 2 Integration Notes
- Editor `NodeKind::Mfsr` and renderer `RenderGraphNodeKind::Mfsr` are wired.
- Stable serialized kind text is `MFSR`; deserialization also accepts `Mfsr`.
- MFSR exposes `reference`, dynamic `frameN` support inputs up to `kMaxMfsrInputCount`, and `imageOut`.
- MFSR inputs are image sockets. RAW-derived inputs come through existing RAW Decode/Develop image outputs; direct RAW packet/decode work is deferred.
- Render execution is an inert placeholder that passes through the reference input texture. This is not MFSR fusion.
- Connection validation rejects scalar-image streams and mixed RAW-derived/raster-derived MFSR inputs.

## Test Map
- Focused graph tests build as `StackGraphBehaviorTests` from `tools/graph_behavior_tests.cpp`.
- Phase 1 added MFSR validation tests in that file.
- Phase 2 added focused graph tests for node creation, dynamic sockets, connection rejection, save/load, and render-chain compatibility without requiring real MFSR pixels.
