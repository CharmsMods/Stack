# Node Library Marker Audit

This pass audits graph/node-library entries that are marked as `Needs Fix`,
`Experimental`, `Hidden`, `Deprecated`, placeholder, pass-through, or similar.
It separates actual missing behavior from labels that appear to be policy or
channel-safety warnings.

The catalog sweep found 22 node types or entries with visible marker/status
text. Twenty-one are covered below as lifecycle, hidden/deprecated, placeholder,
or runtime-incomplete entries. `Output` can also display a `Deactivated` runtime
state, but that is normal user-controlled state rather than a fix marker.

## Marker Sources

- `src/Editor/LayerRegistry.h` defines lifecycle markers:
  `Stable`, `NeedsFix`, `Experimental`, `Deprecated`, and `Hidden`.
- `src/Editor/LayerRegistry.cpp` assigns those markers to layer descriptors.
- `src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp` appends
  `(Experimental)` or `(Needs Fix)` to visible layer labels in the node browser.
- `src/Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.cpp` and
  `src/Editor/UI/EditorSidebar.cpp` show lifecycle and channel-policy notes on
  node surfaces.
- `src/Editor/NodeGraph/NodeGraphPayloads.h`,
  `src/Editor/UI/EditorSidebar.cpp`, and
  `src/Renderer/Internal/RenderPipelineGraphExecution.cpp` contain explicit
  non-layer placeholder/pass-through behavior for MFSR.

`NodeCatalogPreviewStrategy::FallbackOnly` and `NoPreview` were not treated as
fix markers. Those only control node-browser thumbnail generation.

## Actual Missing Or Incomplete Behavior

### MFSR

- Marker: explicit placeholder status, visible node.
- Evidence:
  - `src/Editor/NodeGraph/NodeGraphPayloads.h:112` defines
    `Phase 2 placeholder: output passes through Reference; no MFSR reconstruction yet.`
  - `src/Renderer/Internal/RenderPipelineGraphExecution.cpp:1127` evaluates the
    reference input and returns it directly.
  - `src/Editor/UI/EditorSidebar.cpp:345` displays placeholder/cache status.
  - `docs/engineering/mfsr/MFSR_REPO_NOTES.md:38` confirms the renderer is an
    inert reference pass-through.
- What is wrong:
  - The node is selectable as `MFSR`, but it performs no multi-frame fusion.
  - Settings are fingerprinted and serialized, but do not affect output.
  - The graph node surface is status/cache only, so persisted MFSR settings are
    effectively not editable from the normal node UI.
- Fix plan:
  - Short-term: mark the catalog label as `MFSR (Placeholder)` or
    `MFSR (Experimental)` so the add-node browser tells the truth.
  - Implementation pass: follow `docs/engineering/mfsr/03_MFSR_IMPLEMENTATION_PLAN.md`,
    starting with the MFSR tab shell and then CPU reference fusion before GPU
    acceleration.
  - Thread MFSR settings through a real control surface or hide/defer settings
    that cannot yet influence execution.
  - Acceptance: connected burst frames produce a cached output that differs from
    the reference when useful sub-pixel/detail data exists, and placeholder
    status is removed only after that output path exists.

### RAW/CFA Neural Denoise

- Marker: UI runtime text says bypass/pass-through; visible node.
- Evidence:
  - `src/Editor/Internal/EditorModuleRawBasicControls.cpp:157` displays
    `Execution: bypass / pass-through until real inference is implemented.`
  - `src/Renderer/Internal/RenderPipelineGraphRawStages.cpp:26` walks through a
    `RawNeuralDenoise` node to find the original upstream RAW source.
  - `src/Renderer/Internal/RenderPipelineGraphExecution.cpp:1064` produces no
    image texture for the node.
- What is wrong:
  - The raw-stage node has model selection UI, but the render path ignores the
    selected model and leaves the RAW packet effectively unchanged.
  - Runtime choices include provider options that the ONNX backend rejects, and
    the serialized `allowCpuFallback` setting is not exposed on the node surface.
- Fix plan:
  - Short-term: mark the node-browser label as `RAW/CFA Neural Denoise
    (Placeholder)` or hide it until raw inference exists.
  - Surface or disable unsupported provider choices, and expose
    `allowCpuFallback` if it remains part of the settings contract.
  - Implementation pass: add a real RAW/CFA denoise stage between RAW source and
    decode/develop, probably using the existing `NeuralDenoise` manager and ONNX
    backend where possible.
  - Acceptance: enabling the node changes the RAW/develop result for a valid
    mosaiced CFA input, disabled mode is identity, and unavailable model/provider
    states fail honestly without silently pretending to denoise.

### Text Overlay

- Marker: `Hidden`; not visible in add-node browser.
- Evidence:
  - `src/Editor/LayerRegistry.cpp:103` hides it until C++ text texture
    generation is implemented.
  - `src/Editor/Layers/TextOverlayLayer.cpp:67` creates a 1x1 transparent
    placeholder texture.
  - `src/Editor/Layers/TextOverlayLayer.cpp:93` labels the UI as a placeholder.
- What is wrong:
  - Saved projects can still load the layer, but it renders no actual text.
- Fix plan:
  - Implement text rasterization into `m_OverlayTexture` from the serialized text
    state, font size, opacity, rotation, and position.
  - Keep hidden until rendering, serialization, and preview/export all agree.

### Background Remover

- Marker: `Needs Fix`; visible layer node.
- Evidence:
  - `src/Editor/LayerRegistry.cpp:65` says brush/flood/AA/patch state is
    incomplete.
  - `src/Editor/Layers/BackgroundPatcherLayer.cpp:160` disables flood-mask use.
  - `src/Editor/Layers/BackgroundPatcherLayer.cpp:161` disables brush-mask use.
- What is wrong:
  - The color-range matte works as an advanced selection/removal tool, but
    advertised brush/flood mask paths are not wired into rendering.
- Fix plan:
  - Either remove unused flood/brush affordances from this node and rely on
    `Custom Mask`, or wire flood/brush masks into the shader and serialization.
  - Add edge/AA acceptance tests or a visual validation scene before clearing
    `NeedsFix`.

### Fixed-Canvas Transform Nodes: Crop, Rotate, Expand Canvas

- Markers: `Crop (Needs Fix)`, `Rotate (Needs Fix)`, `Expand Canvas
  (Experimental)`.
- Evidence:
  - `src/Editor/LayerRegistry.cpp:61` says Crop does not resize the raster.
  - `src/Editor/LayerRegistry.cpp:62` says Rotate does not expand bounds.
  - `src/Editor/LayerRegistry.cpp:64` says Expand Canvas simulates padding
    inside the current fixed canvas.
  - `src/Editor/Layers/SplitTransformLayers.cpp:74` renders Crop/Rotate through
    a same-size full-screen shader.
  - `src/Editor/Layers/ExpanderLayer.cpp:62` states the C++ layer keeps input
    and output at the same resolution.
- What is wrong:
  - These nodes do not perform real canvas/reformat operations. They modify
    pixels within the existing render target.
- Fix plan:
  - Short-term: rename labels or notes to `Fixed Canvas Crop`, `Fixed Canvas
    Rotate`, and `Fixed Canvas Expand` if we are not ready for variable-size graph
    outputs.
  - Implementation pass: add graph support for resolution-changing nodes, render
    target allocation by node output size, persistence of output dimensions, and
    downstream preview/export dimension handling.

## Experimental But Not Necessarily Broken

### Classical RGB Denoise

- Marker: `Experimental`.
- Evidence: `src/Editor/LayerRegistry.cpp:81` calls it a CPU prototype with an
  explicit `Run Denoise` cache workflow.
- Assessment:
  - This appears intentional rather than broken. It is an offline/cached node,
    not a normal live shader effect.
- Fix plan:
  - Keep experimental until cache invalidation, cancel/progress behavior, and
    full-image validation are solid.

### Linear RGB Neural Denoise

- Marker: `Experimental`.
- Evidence: `src/Editor/LayerRegistry.cpp:83` requires external ONNX model packs.
- Assessment:
  - The RGB neural denoise layer has real ONNX backend plumbing, but availability
    depends on local model/runtime packs.
- Fix plan:
  - Keep experimental until model-pack install/discovery UX is reliable and
    provider fallback rules are fully validated.

## Likely False-Positive `Needs Fix` Labels

These visible nodes are marked `NeedsFix`, but the registry note points to
channel/full-image semantics rather than an obvious missing full-image
implementation:

- `Saturation`, `src/Editor/LayerRegistry.cpp:70`
- `Warmth`, `src/Editor/LayerRegistry.cpp:71`
- `HDR Compressor`, `src/Editor/LayerRegistry.cpp:74`
- `Tone Curve`, `src/Editor/LayerRegistry.cpp:75`
- `Cell Shading`, `src/Editor/LayerRegistry.cpp:99`
- `Palette Rebuild`, `src/Editor/LayerRegistry.cpp:100`
- `Edge Saturation Mask`, `src/Editor/LayerRegistry.cpp:102`
- `Chroma Subsample Compression`, `src/Editor/LayerRegistry.cpp:106`
- `Color Bleed`, `src/Editor/LayerRegistry.cpp:110`
- `Chromatic Aberration`, `src/Editor/LayerRegistry.cpp:116`

What is probably happening:

- The node browser uses lifecycle state as the visible warning.
- The graph uses `LayerChannelPolicy` only as advisory UI text.
- Full-image behavior may be acceptable, but channel/split-stream behavior is
  ambiguous, full-image-preferred, or full-image-only.

Fix plan:

1. Keep `LayerChannelPolicy` for channel warnings.
2. Reserve `LayerLifecycleStatus::NeedsFix` for real broken or incomplete
   behavior.
3. Add enforcement or at least stronger connection-time warnings for
   `FullImageOnly` and `ReworkBeforeExpose` nodes on scalar/channel streams.
4. After smoke tests, reclassify false positives to `Stable` while preserving
   their channel notes.

## Hidden Or Deprecated Registry Entries

These are not visible in the add-node browser because
`LayerRegistry::ShouldShowInNodeBrowser` filters `Hidden` and `Deprecated`:

- `Alpha Protect`, `src/Editor/LayerRegistry.cpp:66`
  - Deprecated, kept for saved project loading.
  - No fix needed unless old projects need a migration path.
- `Tone Equalizer`, `src/Editor/LayerRegistry.cpp:76`
  - Hidden scene-referred EV gain node.
  - No immediate evidence of breakage from the marker alone.
- `Text Overlay`, `src/Editor/LayerRegistry.cpp:103`
  - Hidden because text texture generation is not implemented.
  - Covered above as actual missing behavior.

## Intentional Runtime Status

- `Output`
  - Status text: disabled outputs are titled `Deactivated` and the node body says
    `This output is deactivated.`
  - Assessment: this is an intentional node state, not an implementation gap.
    No fix is needed unless the UX around toggling active outputs changes.

## Recommended Next Pass Order

1. Clean up truth-in-labeling.
   - Mark MFSR and RAW/CFA Neural Denoise as placeholder/experimental in the
     catalog, or hide them until their render paths exist.
   - Reclassify false-positive `NeedsFix` labels after minimal full-image smoke
     validation.

2. Make channel policy actionable.
   - Keep non-breaking warnings for `FullImagePreferred`.
   - Block or require an explicit insertion action for `FullImageOnly` and
     `ReworkBeforeExpose` nodes on scalar/channel streams.

3. Fix small, contained implementation gaps first.
   - Background Remover flood/brush mask truthfulness.
   - Text Overlay real texture generation if the layer should become visible.

4. Treat large architecture work as separate feature passes.
   - Real canvas-resizing Crop/Rotate/Expand requires variable output sizes in
     the graph renderer.
   - Real MFSR requires its planned tab, cache, diagnostics, and fusion pipeline.
   - RAW/CFA Neural Denoise requires a real RAW-stage inference integration.

## Verification To Run Next Pass

- `tools/graph_behavior_tests.cpp` build/run path after graph-policy edits.
- Node browser smoke: labels match actual availability.
- Channel-stream smoke: split a channel, insert each policy class, and verify
  warning/blocking behavior.
- Full-image smoke for all reclassified nodes.
- Feature-specific smoke for Background Remover, Text Overlay, MFSR, and
  RAW/CFA Neural Denoise when those implementations are touched.
