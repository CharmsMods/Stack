# Code Research Plan

This began as the next-pass checklist. Pass 1 has now answered the timeline
foundation questions needed for the first visible UI shell; the remaining
sections still guide later keyframe, frame-evaluation, export, and packaging
research.

## Goal

Map current Stack code well enough to turn the timeline and video export idea
into an implementation contract.

## Pass 1 Verified Findings

- Completed chain discovery already exists in
  `src/Editor/NodeGraph/Model/EditorNodeGraphTraversal.cpp`.
- `EditorNodeGraph::Graph::GetCompletedChains()` returns
  `CompletedChainInfo` records anchored by `outputNodeId`.
- The current final output graph object is
  `EditorNodeGraph::NodeKind::Output`.
- `EditorModule` already caches completed chains in
  `m_CachedCompletedChains`, with labels available through
  `BuildCompositeChainLabel(...)`.
- The editor workspace layout is in `src/Editor/EditorModule.cpp` inside
  `StackEditorWorkspace`; graph and viewport pane heights can be reduced
  together by subtracting a bottom timeline height.
- `Ctrl+Tab` already toggles Presets. Pass 1 adds `Ctrl+Shift+Tab` for the
  timeline and guards Presets with `!KeyShift`.
- The Pass 1 timeline renderer lives in
  `src/Editor/Internal/EditorModuleTimeline.cpp`.

## Pass 2 Verified Findings

- Stack did not have existing FFmpeg references before Pass 2.
- `tools/create_release.ps1` is the release staging script used by
  `stack-tools.cmd`.
- `tools/stack_workflow.ps1` owns shared build/release helper functions.
- Existing release staging already copies `THIRD_PARTY_NOTICES.md`,
  `licenses\`, optional `libraw.dll`, and optional Windows App Runtime
  bootstrap files.
- Pass 2 adds `src/Video/FFmpegProvider.*` for app-local provider probing.
- Pass 2 adds `Stack.exe --validate-ffmpeg-provider`.
- Pass 2 adds manifest-gated release copying from `_workspace\ffmpeg-provider`
  to packaged `tools\ffmpeg`.

## Pass 3 Verified Findings

- Layer node values are a practical first animatable registry target because
  layer settings already serialize to stable JSON keys.
- The split adjustment layer family lives in
  `src/Editor/Layers/SplitAdjustmentsLayers.*`.
- Pass 3 adds the timeline animation model and first registry in
  `src/Editor/Timeline/TimelineAnimation.*`.
- Pass 3 runtime keyframes are editor state only and are not serialized.
- Keyframe marks can be displayed for shared upstream values by checking
  whether a track target's node ID appears in a completed chain's `nodeIds`.
- Pass 11 renames the first registry table to a general float-parameter table
  and adds blur/focus coverage there: Box Blur, Gaussian Blur,
  Hankel/Optical Blur, and Tilt-Shift Blur float sliders.
- Pass 12 renames the table to a numeric parameter registry, adds integer
  metadata, and rounds/clamps integer targets before writing render snapshot
  JSON.
- Pass 12 covers additional non-RAW numeric layer controls in split
  corruption, split compression, split denoising, split edge effects,
  crop/rotate numeric transform controls, and heat/ripple distortion numeric
  controls.
- Pass 13 adds explicit numeric array-element support and covers another
  effects/generate/stylize numeric sweep: Bilateral Filter, Noise numeric
  sliders, split dither numeric sliders, HDR, Color Grade strength, Vignette,
  Chromatic Aberration, Lens Distortion, Glare Rays, Airy Bloom, Halftoning,
  Cell Shading, Image Breaks, Analog Video, Expander padding, and Palette
  Reconstructor blend/smoothing.
- Pass 14 renames the registry concept to scalar layer parameters and adds
  bool/enum value metadata. Bool and enum values keep the existing numeric
  keyframe storage shape, but evaluate as hold/stepped tracks and write typed
  JSON booleans or rounded/clamped integer options.
- Pass 15 compares the visible layer registry against the timeline scalar
  registry and covers Background Patcher, the remaining visible non-RAW
  registry node with simple serialized scalar controls. It intentionally
  covers only target alpha, tolerance, smoothing, edge shift, defringe,
  keep-selected range, and visualizer state.
- Pass 16 replaces the flat timeline row body with a first hierarchical
  renderer in `src/Editor/Internal/EditorModuleTimeline.cpp`: output-chain
  section rows, animatable node rows, and registered property rows. The row
  hierarchy is runtime UI only; it reuses existing completed-chain data,
  animatable parameter definitions, and timeline tracks.
- New node groups should extend the registered scalar parameter table or
  replace it with a richer metadata registry; they should not create a
  parallel one-off timeline target path.

## Pass 4 Verified Findings

- `EditorModule::BuildGraphSnapshot()` serializes layer nodes to
  `RenderGraphNode::layerJson` before the renderer executes the graph.
- `RenderPipeline` fingerprints `RenderGraphNode::layerJson`, so frame-specific
  JSON values participate in render cache identity.
- Pass 4 adds frame sampling, `FrameEvaluationContext`, and render-snapshot
  layer JSON override helpers to `src/Editor/Timeline/TimelineAnimation.*`.
- Covered split adjustment parameters can be applied to copied render snapshot
  JSON without mutating live `LayerBase` objects.
- Timeline scrubbing with tracks and `+ Key` creation now request render
  refreshes; affected completed-chain output rows are marked dirty for
  composite refresh.

## Pass 5 Verified Findings

- Runtime timeline playback state lives in `TimelineUiState` and is not
  serialized.
- Playback stepping and frame request normalization live in
  `src/Editor/Timeline/TimelinePlayback.*` and
  `src/Editor/Timeline/TimelineFrameProducer.*`.
- The timeline panel exposes play/pause, stop, previous/next frame, and loop
  controls.
- `BuildGraphSnapshotForTimelineFrame(...)` can build a graph snapshot for a
  requested frame without changing `m_TimelineUi.currentFrame`.
- `BuildSingleOutputTimelineFrameRaster(...)` provides the first reusable
  single-output frame raster entry point for future frame producer/export work.

## Pass 6 Verified Findings

- Stack project pipeline data is a JSON object; editor-owned extras already
  live beside the graph payload, for example `editorComposite`.
- Pass 6 adds `editorTimeline` as schema version 1 in the pipeline payload.
- Timeline persistence helpers live in
  `src/Editor/Timeline/TimelinePersistence.*`.
- `SerializePipeline()` writes timeline data after graph serialization.
- `FinalizeDeserializedPipeline(...)` restores timeline data after graph and
  layer deserialization, so load-time target validation can inspect the loaded
  graph and current animatable registry.
- Playback/open-panel UI state remains intentionally transient.

## Pass 7 Verified Findings

- The timeline header and ruler are rendered outside the vertical row-scrolling
  child region in `src/Editor/Internal/EditorModuleTimeline.cpp`.
- Pass 7 makes that separation explicit through a fixed `TimelineHeader` child.
- Timeline duration and FPS can be edited through a simple modal timeline
  settings popup instead of the dense header strip.
- Timeline shortcuts are handled in `src/Editor/EditorModule.cpp` after the
  bottom timeline reserved height is known, so `Space` can be scoped to mouse
  hover over the timeline panel.
- `Q` and `E` frame stepping currently require the timeline to be open and no
  ImGui item to be active.
- Loop-to-last-keyframe behavior uses helper queries in
  `src/Editor/Timeline/TimelineAnimation.*`; it only uses the last keyed frame
  when at least two distinct keyframe frames exist.

## 2026-07-06 Realtime Editing Follow-Up Findings

- Pass 9 implements the first scoped update-existing-keyframe path: registered
  animatable layer parameter edits are detected through
  `EditorModule::RenderLayerControlsWithDirtyTracking(...)`, matching
  keyframes at the current frame are updated, and the timeline frame preview is
  refreshed.
- The helper is intentionally "update existing only"; it does not create
  missing tracks or keyframes.
- Pass 10 adds a target-specific live edit preview bypass for off-keyframe
  edits. The render snapshot copies the frame evaluation context and removes
  live-edited targets for the current frame before applying layer JSON
  overrides.
- That temporary bypass is cleared when timeline motion resumes, so scrubbing
  or playback returns the viewport to the value evaluated for the new frame.
- Broader research remains for all-node coverage, property-row interaction
  polish, keyframe editing undo/redo grouping, and whether a separate explicit
  auto-key mode should create new keyframes.
- Pass 11 confirms that newly registered float layer parameters inherit the
  existing keyframe creation, persistence, frame evaluation, existing-keyframe
  update, and off-keyframe live preview behavior.
- Pass 12 confirms that newly registered numeric float/integer layer
  parameters inherit the same behavior. Integer targets remain stored as
  numeric keyframe values but are rounded/clamped when applied to frame JSON.
- Pass 13 confirms that explicit numeric array-element targets and the newly
  registered effects/generate/stylize numeric parameters inherit keyframe
  creation, persistence, frame evaluation, existing-keyframe update, and
  off-keyframe live preview behavior.
- Pass 14 confirms that newly registered bool/enum scalar parameters inherit
  the same keyframe creation, persistence, existing-keyframe update, and
  off-keyframe live preview behavior, while frame evaluation writes typed JSON
  and holds discrete values between keyframes.
- Pass 15 confirms that Background Patcher's registered scalar controls
  inherit the same behavior. Target color, picker state, masks, and patch
  state remain intentionally outside this scalar coverage.
- Pass 16 confirms that the existing chain and target data can drive
  hierarchical rows without a new timeline persistence schema. Chain and node
  rows summarize keyed frames, while exact keyframe diamonds belong on
  property rows.

## Documentation To Keep Open

- `docs/stack-documentation/Current/engineering/motion-graphics/source-map.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/open-decisions.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/implementation-phases.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/chain-and-track-model.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/keyframing-ux-options.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/export-backend-and-packaging.md`
- `docs/stack-documentation/Info/engineering/ui/Dear ImGui Desktop UI Reference.md`
- `docs/stack-documentation/Current/engineering/editor/NODE_GRAPH_INTERACTION_GUIDE.md`

## Research Areas

### Graph And Chain Discovery

Find:

- graph data model files
- node ID and link ID types
- output node types and export/preview targets
- source/input node types
- existing traversal helpers
- graph validation helpers
- serialization format for nodes, links, and node parameters
- copy/paste or duplicate behavior

Answer:

- How can Stack derive connected input-to-output chains today? Answered for
  Pass 1: use `Graph::GetCompletedChains()`.
- Are outputs explicit enough to use as timeline row anchors? Answered for
  Pass 1: yes, use `CompletedChainInfo::outputNodeId`.
- How can split chains be represented without duplicating shared upstream
  nodes? Answered for Pass 1 rows: each final output receives its own row; the
  shared upstream node appears in each affected completed-chain record.
- Are node parameters discoverable through a common API?
  Answered for Pass 3 first coverage: not globally yet. The first common
  registry is explicit metadata for split adjustment layer nodes.

### Timeline Panel Layout

Find:

- main Editor tab layout code
- bottom graph panel sizing and split behavior
- right-side main viewport sizing behavior
- keyboard shortcut handling
- window/panel collapse or resize patterns

Answer:

- Where would a bottom timeline panel live? Answered for Pass 1: as a
  full-width bottom band inside `StackEditorWorkspace`.
- Can it push both the graph editor and the right-side viewport using existing
  split-layout logic? Answered for Pass 1: yes, subtract the timeline height
  before rendering both panes.
- Is `Ctrl+Shift+Tab` already used? Answered for Pass 1: no conflicting use
  was found; existing `Ctrl+Tab` is Presets.
- What state object should own timeline open/closed and height? Answered for
  Pass 1: runtime-only `TimelineUiState` in `EditorModule`.

### Keyframe Targeting And UI

Find:

- node parameter widgets
- inspector or selected-node controls
- graph context menus
- node double-click behavior
- right-click behavior in graph and viewport
- reusable popup, menu, or side-panel patterns

Answer:

- Where can keyframe buttons appear without crowding the node graph?
  Pass 3 first code answer: the timeline header exposes a target selector for
  the selected output row and a compact `+ Key` action.
- Can the current parameter UI expose a stable `AnimatableParameter` concept?
  Pass 3 first code answer: yes for split adjustment layer nodes through
  explicit registry metadata in `Stack::Timeline`.
- How should selected chain, selected node, and selected parameter state be
  represented?
  Pass 3 first code answer: selected output row remains `selectedOutputNodeId`;
  selected parameter is `AnimatableParameterTarget { nodeId, parameterId }`.
- What UI pattern should the first `Add Keyframe` flow use?
  Pass 3 first code answer: select a row, choose a supported target in the
  timeline header, then press `+ Key` at the current frame.

### Timeline UX, Row Hierarchy, And Shortcuts

Find:

- timeline header child/window structure
- row scrolling child/window structure
- existing icon button and tooltip helpers
- graph, viewport, and timeline hover/focus handling
- existing global hotkeys and text-input guards
- settings popup/window patterns
- existing Composite export settings UI patterns
- graph chain traversal order suitable for chain/node/property hierarchy

Answer:

- How should the timeline keep the header fixed while rows scroll?
- Which current header controls are clipping/truncating, and which should move
  to icons or a settings popup?
- What data structure should represent visible chain sections, node rows, and
  property/value rows?
- Can row hierarchy be generated from current completed-chain data plus
  animatable registry metadata, or does it need a new timeline view model?
- How should keyframes for shared upstream nodes appear across affected chains
  without placing multiple node targets on one row?
- Which existing hotkeys conflict with proposed timeline shortcuts such as
  `Q`, `E`, and `Space`?
- Should the timeline/video settings popup reuse app settings styling,
  Composite export settings patterns, or a new project-settings pattern?
- Which FPS-lock/retiming choices are technically feasible with the current
  `editorTimeline` schema?

### Render At Frame

Find:

- render request model
- graph evaluation entry points
- async render worker behavior
- dirty tracking
- preview texture ownership
- canvas/object transform application

Answer:

- What is the smallest safe way to evaluate frame `N`?
  Answered for Pass 4 first coverage: sample runtime tracks into
  `Stack::Timeline::FrameEvaluationContext`, then apply matching split
  adjustment overrides to serialized render snapshot layer JSON.
- Does evaluation mutate persistent editing state today?
  Answered for Pass 4 first coverage: no. The live graph/layer state remains
  unchanged; only copied `RenderGraphNode::layerJson` receives frame values.
- Can frame-specific values be applied as overlays, or must node data be
  temporarily changed?
  Answered for Pass 4 first coverage: frame-specific values can be overlaid on
  render snapshot JSON for covered layer parameters.
- How can export avoid corrupting the interactive session?
  Partly answered by Pass 4: use the same non-mutating frame context/snapshot
  override path. Export-specific frame producer and cancellation behavior still
  need implementation.
- Can the current render snapshot model accept parameter overrides without
  mutating the live graph?
  Answered for covered Pass 4 parameters: yes.

### Still And Video Export

Find:

- still-image export path
- current main-canvas resolution owner
- renderer readback code
- export progress/status UI
- background task framework
- validation tests or commands for export

Answer:

- Can existing still export be parameterized by frame?
  Answered for Pass 5 single-output path: yes, through
  `BuildSingleOutputTimelineFrameRaster(...)`. Existing still export continues
  to call the current-frame wrapper.
- What buffer format does export produce?
  Partly answered for Pass 5 single-output path: the raster path returns
  top-left-row-order RGBA pixels from `RenderPipeline::GetOutputPixels(...)`,
  matching existing file/export consumers.
- Where would an encoder process receive frames?
  Still open; Pass 5 intentionally does not invoke FFmpeg or define a process
  pipe/file-sequence sink.
- How should progress, cancel, and errors appear?
- Can export be structured as a frame producer with separate sinks for still
  frame, image sequence, and external encoder process?
- What process wrapper is safest for app-local `ffmpeg.exe` invocation?
- How should Stack surface missing app-local encoder versus fallback provider
  availability?

### Build, Tools, And Notices

Find:

- `stack-tools.cmd`
- build/package scripts
- third-party notice docs
- current bundled binary policy
- current dependency download/setup behavior
- existing optional runtime dependencies such as LibRaw
- release packaging paths for app-local tools

Answer:

- Is FFmpeg already referenced anywhere?
- How are third-party licenses currently packaged? Answered for Pass 2:
  release staging copies `THIRD_PARTY_NOTICES.md` plus `licenses\` from the
  build output, and optional runtime files beside `Stack.exe`.
- Where should FFmpeg license/source notices be added if FFmpeg is bundled?
  Answered for Pass 2 provider packaging: exact provider license/source files
  live inside `_workspace\ffmpeg-provider` and are copied to packaged
  `tools\ffmpeg` with the reviewed provider. General policy is documented in
  `THIRD_PARTY_NOTICES.md`.
- What app-local path should hold an approved `ffmpeg.exe` provider? Answered
  for Pass 2: packaged/runtime `tools\ffmpeg`; local source
  `_workspace\ffmpeg-provider`.
- How should release scripts copy the approved encoder, its license files, its
  build manifest, and matching-source notes? Answered for Pass 2: only through
  `Copy-OptionalFfmpegProvider` after manifest checks pass.
- Can the LibRaw runtime availability pattern be reused for an external
  `ffmpeg.exe` provider?
- Does the existing release script need a manifest-driven way to copy optional
  encoder tools and their licenses? Answered for Pass 2: yes, implemented
  through `ffmpeg-provider.json`.

## Expected Outputs Of The Research Pass

After research, update:

- `chain-and-track-model.md` with verified graph ID/traversal findings
- `keyframing-ux-options.md` with UI findings and recommended first flow
- `export-backend-and-packaging.md` with verified export/build findings
- `open-questions.md` with answered, narrowed, and newly discovered questions
- `research-backlog.md` with completed research notes or unresolved follow-up
  prompts
- `implementation-agent-context.md` with concrete files and next-pass guidance
- parent `source-map.md` with code paths
- parent `implementation-tracker.md` with a Phase 0 research pass entry
