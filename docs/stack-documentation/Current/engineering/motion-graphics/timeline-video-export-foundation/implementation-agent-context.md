# Implementation Agent Context

This is a compact handoff file for future implementation agents. Read
`implementation-progress.md` first so completed implementation passes are not
duplicated.

## Current Instruction

Pass 1 has implemented the visible runtime timeline foundation. Pass 2 has
implemented the optional FFmpeg provider and packaging foundation. Pass 3 has
implemented the runtime keyframe model and first animatable-parameter registry
coverage. Pass 4 has implemented the first non-mutating frame evaluation
context and render snapshot override path for covered split adjustment
parameters. Pass 5 has implemented runtime playback controls and
frame-specific graph/raster entry points for future export. Pass 6 has
implemented project persistence for timeline settings and keyframe tracks.
Pass 7 has implemented first timeline header polish, a simple timeline settings
popup, guarded timeline shortcuts, loop-on default state, and
loop-to-last-keyframe playback behavior. Passes 9 and 10 have implemented
existing-keyframe edits and off-keyframe live edit preview for registered
animated layer parameters. Pass 11 has extended the animatable float registry
to the first blur/focus node group. Pass 12 has broadened the registry from
float-only to numeric float/integer parameters and covered another sweep of
normal non-RAW layer nodes. Pass 13 has added registered numeric array-element
support and another broad effects/generate/stylize numeric node sweep. Pass 14
has added typed bool/enum scalar parameter support and focused non-RAW typed
coverage. Pass 15 has added Background Patcher scalar coverage for its simple
serialized controls. Pass 16 has added the first hierarchical timeline row
foundation. Do not rebuild these shells unless the user explicitly asks for a
redesign. Future passes should start from the existing timeline state, layout
reservation, output-chain row source, manifest-gated app-local FFmpeg provider
contract, `Stack::Timeline` model, `FrameEvaluationContext` snapshot override
path, playback helpers, frame-specific raster path, `editorTimeline`
persistence schema, Pass 7 timeline header/shortcut behavior, and the
registered scalar-parameter path used by split adjustments, blur/focus nodes,
the Pass 12/13 numeric sweeps, the Pass 14 bool/enum scalar coverage, and the
Pass 15 Background Patcher scalar coverage, plus the Pass 16 chain/node/
property row renderer.

The 2026-07-06 timeline UX/keyframe-controls intake adds additional design
direction but is not itself a code pass. Before implementing timeline UI or
keyframe layout changes beyond the current shell, read
`2026-07-06-timeline-ux-keyframe-controls.md` and `research-backlog.md`.
Research-needed topics from that intake must be investigated before being
treated as implementation requirements.

## Intent

Stack needs a timeline foundation that can later support video export,
keyframing, interpolation, video-backed slices, and richer Composite-style
animation.

The requested initial UI direction is:

- a timeline window below the graph
- expandable/collapsible bottom placement
- pushes the graph editor and right-side main viewport as it changes height
- toggled with `Ctrl+Shift+Tab`
- rows correspond to connected chains or chain-derived timeline objects
- rows are anchored internally to final output nodes
- rows are created automatically from graph structure per output object chain
- vertical scrolling through graph chains
- keyframe marks visible on rows
- first keyframe hierarchy is implemented as chain sections, node rows, and
  property/value rows
- frame rate and video export settings available before export
- export at current main-canvas resolution

## Implementation Values

- Keep the model extensible.
- Avoid hard-coding only one node type or one export path.
- Keep timeline data separate from transient UI state.
- Use stable IDs for graph, chain/output, node, parameter, and keyframes.
- Treat keyframes as changes to actual node parameters, not row-local copies.
- Let shared upstream node keyframes affect every downstream output branch.
- Grow animatable coverage by coherent node groups where possible. When the
  goal is broad node coverage, numeric slider/drag controls can land before
  harder edge-case value types, but color/palette/action/curve/path/model
  controls must remain explicitly documented as deferred until designed.
- Prefer a frame evaluation overlay/context over mutating the live graph during
  playback or export.
- Keep export architecture as frame producer plus replaceable sinks.
- Treat FFmpeg as an external/replaceable executable encoder provider.
- The normal packaged experience should use an approved app-local
  `ffmpeg.exe`; user-selected and `PATH` providers are fallbacks.
- Do not link FFmpeg DLLs/libraries in the planned implementation contract.
- Preserve current still-image behavior when no timeline exists.
- Keep frame-at-time evaluation from corrupting the editing session.
- Keep licensing and packaging decisions explicit.
- Keep the timeline header fixed while timeline rows scroll.
- Avoid unnecessary explanatory text in the dense timeline panel.
- Prefer icon buttons with tooltips for compact timeline actions where the
  meaning is standard.
- Do not place keyframes for different nodes on the same final horizontal row
  in the end-goal keyframe layout.

## Known Existing Docs

Read these before code changes:

- `docs/stack-documentation/Current/engineering/motion-graphics/README.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/implementation-phases.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/README.md`
- `docs/stack-documentation/Current/engineering/architecture/UNIFIED_WORKSPACE_ARCHITECTURE.md`
- `docs/stack-documentation/Info/engineering/composite/CompositeAnimationSpecs.md`
- `docs/stack-documentation/Info/engineering/composite/2026-07-03-timeline-keyframe-and-video-export-research.md`
- `docs/stack-documentation/Current/engineering/editor/NODE_GRAPH_INTERACTION_GUIDE.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/2026-07-06-timeline-ux-keyframe-controls.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/research-backlog.md`

## Verified Code Paths

- graph model: `src/Editor/NodeGraph/NodeGraphModelTypes.h`,
  `src/Editor/NodeGraph/NodeGraphTypes.h`
- chain traversal:
  `src/Editor/NodeGraph/Model/EditorNodeGraphTraversal.cpp`
- completed-chain cache and labels:
  `src/Editor/Internal/EditorModuleRendering.cpp`,
  `src/Editor/Internal/EditorModuleComposite.cpp`
- editor layout and hotkeys: `src/Editor/EditorModule.cpp`
- timeline UI shell: `src/Editor/Internal/EditorModuleTimeline.cpp`
- timeline runtime state: `src/Editor/EditorModuleTypes.h`,
  `src/Editor/EditorModule.h`
- timeline animation model and first registry:
  `src/Editor/Timeline/TimelineAnimation.h`,
  `src/Editor/Timeline/TimelineAnimation.cpp`
- frame evaluation and render snapshot overrides:
  `src/Editor/Timeline/TimelineAnimation.h`,
  `src/Editor/Timeline/TimelineAnimation.cpp`,
  `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`,
  `src/Editor/Internal/EditorModuleTimeline.cpp`
- playback and frame producer helpers:
  `src/Editor/Timeline/TimelinePlayback.h`,
  `src/Editor/Timeline/TimelinePlayback.cpp`,
  `src/Editor/Timeline/TimelineFrameProducer.h`,
  `src/Editor/Timeline/TimelineFrameProducer.cpp`
- timeline header, shortcuts, settings popup, and playback range helpers:
  `src/Editor/Internal/EditorModuleTimeline.cpp`,
  `src/Editor/EditorModule.cpp`,
  `src/Editor/EditorModuleTypes.h`
- timeline persistence:
  `src/Editor/Timeline/TimelinePersistence.h`,
  `src/Editor/Timeline/TimelinePersistence.cpp`,
  `src/Editor/Internal/EditorModulePersistence.cpp`,
  `src/Editor/Internal/EditorModuleTimeline.cpp`
- frame-specific graph/raster entry points:
  `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`,
  `src/Editor/Internal/EditorModuleRendering.cpp`
- graph behavior tests: `tools/graph_behavior_tests.cpp`
- FFmpeg provider probe: `src/Video/FFmpegProvider.h`,
  `src/Video/FFmpegProvider.cpp`
- FFmpeg provider validation:
  `src/App/Validation/Suites/FFmpegProviderValidation.cpp`,
  `src/App/Validation/ValidationCommandRunner.cpp`,
  `src/App/Validation/ValidationSuites.h`
- optional FFmpeg packaging:
  `tools/stack_workflow.ps1`, `tools/create_release.ps1`,
  `tools/stack_menu.ps1`, `tools/ffmpeg-provider.example.json`
- node parameters: Pass 3 covers split adjustment layer nodes; Pass 11 covers
  blur/focus float sliders for Box Blur, Gaussian Blur, Hankel/Optical Blur,
  and Tilt-Shift Blur; Pass 12 covers another non-RAW numeric sweep including
  split corruption, split compression, split denoising, split edge effects,
  crop/rotate numeric transform controls, and heat/ripple distortion numeric
  controls; Pass 13 covers Bilateral Filter, Noise numeric sliders, split
  dither numeric sliders, HDR, Color Grade strength, Vignette, Chromatic
  Aberration, Lens Distortion, Glare Rays, Airy Bloom, Halftoning, Cell
  Shading, Image Breaks, Analog Video, Expander padding, and Palette
  Reconstructor blend/smoothing; Pass 14 covers focused typed scalar controls
  including Tilt-Shift Blur filter type, Flip H/V, Bilateral Filter
  kernel/edge mode, Noise type/blend mode, split dither gamma/palette toggles,
  Chromatic Aberration falloff link, Halftoning pattern/color/bool controls,
  Cell Shading mode/bool controls, and Palette Reconstructor smoothing type;
  Pass 15 covers Background Patcher removed-area opacity, color tolerance,
  edge smoothing, edge shift, defringe, keep-selected range, and visualizer
  state; remaining node work is mostly color/palette/action,
  hidden/deprecated, RAW/develop, scene-workflow, external-model/cache/action,
  curve/point, path/model/provider, or other edge-case coverage. After Pass
  15, a direct visible registry versus scalar registry comparison leaves only
  `AlphaHandling`, `ClassicalRgbDenoise`, `LinearRgbNeuralDenoise`,
  `SceneDenoise`, `TextOverlay`, `ToneCurve`, `ToneEqualizer`, and
  `ViewTransform` without scalar entries.
- timeline row hierarchy: Pass 16 renders completed output chains as
  expandable section rows, registered animatable nodes as child rows, and
  registered properties as target rows. Actual keyframe diamonds render on
  property rows; chain and node rows render summary marks.
- still export: still needs frame producer research
- canvas resolution: still needs export-path research
- persistence: Pass 6 persists `editorTimeline` schema version 1 for timeline
  settings and keyframe tracks; app/window UI state remains transient
- third-party notices: `THIRD_PARTY_NOTICES.md` documents optional FFmpeg
  provider policy; exact provider notices live beside a reviewed provider
  before packaging
- timeline UX intake: static header, icon-control preparation, hierarchical
  row model, timeline/video settings popup, FPS locking/retiming, shortcut
  design, loop defaults, and dense-row visual design are documented. Static
  header, shortcut basics, settings popup, loop defaults, and the first
  hierarchy foundation are implemented; color/state systems, dragging, row
  context menus, zoom/windowing, and density polish remain future work

## First Implementation Contract Draft

Implemented by Pass 1:

- runtime-only timeline open/closed state and resizable panel height
- `Ctrl+Shift+Tab` timeline toggle
- graph/viewport layout reservation for the bottom timeline
- frame, duration, and FPS UI controls
- ruler and draggable playhead shell
- automatic rows from completed output chains
- focused graph behavior test for split branches feeding two output nodes

Not implemented by Pass 1:

- keyframe storage
- animatable-parameter registry
- interpolation or curves
- playback/frame evaluation
- still-frame rendering at selected frame
- video/image-sequence export
- FFmpeg provider discovery or packaging
- timeline persistence

Implemented by Pass 2:

- runtime probe for app-local `tools\ffmpeg\ffmpeg-provider.json`
- `Stack.exe --validate-ffmpeg-provider`
- manifest-gated copy from `_workspace\ffmpeg-provider` to packaged
  `tools\ffmpeg`
- `stack-tools.cmd` visibility through validation and a provider-status menu
  item
- provider template at `tools/ffmpeg-provider.example.json`

Not implemented by Pass 2:

- FFmpeg download/install automation
- FFmpeg process invocation
- video export UI or export settings
- image-sequence export
- user-configured or `PATH` fallback providers
- FFmpeg DLL/library linking

Implemented by Pass 3:

- runtime-only `TimelineAnimationState`
- stable target identity using node ID plus parameter ID
- float keyframes with interpolation placeholder
- sorted keyframe insertion and replace-at-frame behavior
- complete first node-group coverage for split adjustment layer nodes:
  Brightness, Contrast, Saturation, Warmth, and Sharpen
- timeline header target picker for supported parameters on the selected output
  chain
- `+ Key` runtime keyframe creation
- row keyframe marks for chains affected by the keyed node, including shared
  upstream nodes

Not implemented by Pass 3:

- project serialization for keyframes
- frame evaluation or render-time parameter overrides
- playback
- curve editing
- auto-keyframing
- broad registry coverage beyond the first split adjustment layer group
- video/image-sequence export

Implemented by Pass 4:

- deterministic runtime sampling of timeline tracks at a requested frame
- exact key, linear interpolation, hold interpolation, and nearest-key range
  clamping
- `FrameEvaluationContext` carrying temporary per-node/per-parameter float
  values
- non-mutating application of covered split adjustment frame values to copied
  render snapshot `layerJson`
- current-frame render refresh when scrubbing with tracks or adding keyframes
- focused graph behavior tests for sampling and JSON override behavior

Not implemented by Pass 4:

- project serialization for timeline tracks or duration/FPS
- playback transport controls
- frame producer/export loops
- still-frame, image-sequence, or video export
- FFmpeg process invocation
- curve editor UI or easing UI
- broader animatable registry coverage beyond split adjustment layer nodes

Implemented by Pass 5:

- runtime-only play/pause, loop, and playback accumulator state
- timeline header controls for play/pause, stop, previous frame, next frame,
  and loop
- FPS-based playhead advancement with stop-at-end and loop behavior
- `BuildGraphSnapshotForTimelineFrame(...)` for frame-specific graph snapshots
  without changing the UI playhead
- `BuildSingleOutputTimelineFrameRaster(...)` for single-frame raster output
  using the same non-mutating frame evaluation path
- focused graph behavior tests for playback stepping and frame request
  normalization

Not implemented by Pass 5:

- timeline project serialization
- image-sequence export
- FFmpeg process invocation
- video export settings UI
- playback keyboard shortcuts
- multi-output/composite timeline frame export
- curve editor UI or broad node coverage

Implemented by Pass 6:

- `editorTimeline` schema version 1 in the existing pipeline project payload
- persisted timeline duration frames, FPS, current frame, tracks, targets,
  keyframes, values, and interpolation
- load-time target validation against the loaded graph and current animatable
  registry
- safe repair behavior for missing nodes, unsupported parameter IDs, negative
  frames, out-of-range frames, and unsupported future schemas
- focused graph behavior tests for timeline persistence round-trip and repair

Not implemented by Pass 6:

- persistence for timeline open/closed height or playback state
- graph-node identity repair beyond current node IDs
- copy/paste or duplication remapping for keyframed tracks
- image-sequence export
- FFmpeg process invocation
- curve editor UI or broad node coverage

Documented after Pass 6 but not implemented:

- color-differentiated keyframes
- FPS lock/retiming flow after keyframes exist
- dense visual design for very large graphs

Implemented by Pass 7:

- fixed timeline header area above the ruler and scrolling rows
- compact symbolic transport controls with tooltips
- simple timeline settings popup for current frame, duration, FPS, and loop
  playback
- `Q` and `E` one-frame stepping while the timeline is open and no item is
  active
- `Space` play/pause only while the mouse is hovering the timeline
- loop playback defaults on in runtime/reset/load state
- loop playback uses the last distinct keyframe frame when at least two
  distinct keyed frames exist; zero/one-keyframe timelines use full duration
- focused graph behavior coverage for last-keyframe/distinct-frame helpers

Implemented by Pass 8:

- `Space` pauses active timeline playback before existing viewport/split resize
  handling can start
- `Q`/`E` frame stepping wraps across the playback range; exact visible-window
  wrap remains future timeline zoom/windowing work
- focused graph behavior coverage for the pure step-wrap helper

Implemented by Pass 9:

- `Stack::Timeline::UpdateExistingKeyframeValue(...)` updates a matching
  keyframe without creating missing tracks or frames
- `EditorModule::RenderLayerControlsWithDirtyTracking(...)` now detects
  registered animatable layer parameter edits while the timeline is open
- registered split-adjustment parameter edits update an existing keyframe at
  the current playhead frame and refresh the timeline frame/composite preview
- focused graph behavior coverage proves existing-keyframe updates do not
  create missing timeline data

Implemented by Pass 10:

- registered animated parameter edits away from a matching keyframe temporarily
  bypass that target's frame-evaluation override so live node edits preview
  normally
- the bypass is target-specific; unrelated animated parameters still evaluate
  from the timeline
- the temporary preview is cleared when the playhead changes, playback starts,
  project timeline state resets, or the user writes `+ Key`
- focused graph behavior coverage proves a frame evaluation context can remove
  one target without affecting unrelated animated values

Implemented by Pass 11:

- the float-parameter registry now covers the first blur/focus node group:
  Box Blur, Gaussian Blur, Hankel/Optical Blur, and Tilt-Shift Blur
- Box Blur and Gaussian Blur expose `layer.blurAmount`
- Hankel/Optical Blur exposes `layer.hankelRadius`, `layer.hankelQuality`,
  and `layer.hankelIntensity`
- Tilt-Shift Blur exposes `layer.tiltShiftBlurStrength`,
  `layer.tiltShiftFocusRadius`, `layer.tiltShiftFocusFalloff`,
  `layer.tiltShiftFocusX`, and `layer.tiltShiftFocusY`
- these parameters use the existing target picker, `+ Key`, project
  persistence, frame sampling, render snapshot JSON overrides,
  update-existing-keyframe behavior, and off-keyframe live edit preview path
- focused graph behavior coverage proves parameter collection, current-value
  readback, and blur/focus frame JSON override behavior

Implemented by Pass 12:

- the registry now models numeric parameters as float or integer through
  `AnimatableValueType`
- keyframes still store numeric values, but frame evaluation rounds and clamps
  integer parameters before writing render snapshot JSON
- split corruption nodes expose `layer.corruptionScale`: JPEG Blocks,
  Pixelation, and Color Bleed
- split compression nodes expose quality, block size, blend, and integer
  iterations: DCT Compression, Chroma Subsample Compression, and Wavelet
  Compression
- split denoising nodes expose visible numeric controls: Non-Local Means
  Denoise, Median Denoise, and Mean Denoise
- split edge-effect nodes expose their visible numeric controls: Edge Overlay
  and Edge Saturation Mask
- Crop exposes crop-left/right/top/bottom; Rotate exposes rotation
- Heatwave Distortion and Ripple Distortion expose intensity, phase, and scale
- RAW/develop controls are not touched by this pass
- focused graph behavior coverage proves numeric parameter collection,
  integer current-value readback, and rounded/clamped frame JSON overrides

Implemented by Pass 13:

- registered numeric parameters can target a specific serialized array element
  when explicitly configured
- Chromatic Aberration center X/Y use numeric array-element readback and frame
  JSON overrides through the `center` array
- Bilateral Filter exposes radius, color sigma, and spatial sigma
- Noise exposes serialized numeric sliders: strength, blurriness, saturation
  strength, saturation impact, scale, and opacity
- split dither nodes expose bit depth, palette size, strength, and scale:
  Ordered 8x8, Error Diffusion, White Noise, Ordered 4x4, Ordered 2x2, and
  Interleaved Gradient
- HDR exposes tolerance and amount; Color Grade exposes strength
- Vignette exposes intensity, radius, and softness
- Chromatic Aberration exposes amount, edge blur, zoom blur, radius, falloff,
  center X, and center Y
- Lens Distortion exposes amount and scale; Glare Rays exposes intensity, ray
  count, length, and softness; Airy Bloom exposes intensity, aperture,
  threshold, threshold fade, and cutoff
- Halftoning exposes cell size, intensity, and sharpness
- Cell Shading exposes shading levels, contrast bias, gamma, edge strength,
  edge thickness, and color preserve
- Image Breaks exposes all serialized numeric sliders, including seed
- Analog Video exposes wobble, bleed, curve, and noise
- Expander exposes padding; Palette Reconstructor exposes blend and smoothing
- focused graph behavior coverage proves parameter collection, array current
  value readback, and frame JSON overrides for float, integer, and array
  targets

Implemented by Pass 14:

- `AnimatableValueType` now includes `Boolean` and `Enum` scalar metadata
  alongside float and integer values
- typed scalar targets keep the existing numeric keyframe persistence shape,
  avoiding a timeline project schema bump
- bool and enum/combo targets evaluate as hold/stepped values between
  keyframes instead of linearly interpolating through intermediate states
- frame evaluation writes typed JSON scalars: booleans for bool targets and
  rounded/clamped integers for enum targets
- focused typed coverage includes Tilt-Shift Blur filter type, Flip
  horizontal/vertical, Bilateral Filter kernel/edge mode, Noise type/blend
  mode, split dither gamma/palette toggles, Chromatic Aberration falloff link,
  Halftoning pattern/color/bool controls, Cell Shading mode/bool controls, and
  Palette Reconstructor smoothing type
- focused graph behavior coverage proves parameter collection, bool current
  value readback, stepped evaluation, and typed JSON overrides

Implemented by Pass 15:

- Background Patcher exposes its simple serialized scalar controls through the
  existing scalar layer parameter registry
- covered float controls: removed-area opacity (`targetAlpha`), color
  tolerance, edge smoothing, edge shift, and defringe
- covered boolean controls: keep-selected range and visualizer/debug overlay
- frame evaluation writes clamped float JSON values and typed boolean JSON
  values for these controls
- focused graph behavior coverage proves parameter collection, current-value
  readback, clamping, boolean JSON overrides, and that target color remains
  outside this pass

Implemented by Pass 16:

- the scrollable timeline body now separates rows into completed-chain section
  rows, registered animatable node rows, and registered property rows
- chain rows remain anchored to completed output node IDs
- node rows appear only for nodes with registered animatable parameters
- property rows are the exact target rows for keyframe diamonds
- chain and node rows render dim summary keyframe marks so collapsed sections
  still show animation activity
- chain and node expansion/collapse state is runtime-only and does not alter
  `editorTimeline` persistence
- clicking chain rows selects the composite/output object; clicking node rows
  selects the graph node and first supported parameter; clicking property rows
  selects the graph node and exact parameter target

Not implemented by Pass 16:

- full icon-font integration
- keyframe dragging
- keyframe color system
- per-row context menus
- timeline zoom/windowing
- row virtualization or dense-row visual polish for very large graphs
- persisted expansion/collapse state
- exact visible-window frame-step wraparound before timeline zoom/windowing
- broad all-node realtime keyframe edit coverage
- broad auto-keyframing that creates new keyframes
- color/palette keyframing
- randomize/action-button keyframing such as seed randomizers
- Background Patcher target color, picker state, brush/flood masks, patch
  state, and placeholder anti-aliasing state
- additional unregistered bool/enum/combo controls beyond the focused scalar
  sets
- text, file/path, curve, point-list, scene-workflow, cache, and
  model/provider values
- RAW/develop controls
- hidden/deprecated nodes
- external-model/action-heavy denoise nodes
- FPS lock/retiming dialog
- image-sequence export
- FFmpeg process invocation
- broad node coverage

Known Pass 16 editing limits:

- The viewport refresh path is wired for timeline scrubbing with tracks,
  playback frame changes, explicit `+ Key` writes, and Pass 9 existing-keyframe
  updates for registered scalar layer parameters.
- If the playhead is not on a matching keyframe, the edit should not silently
  create or save a new keyframe; Pass 10 only previews that live edit until
  timeline motion resumes.
- Pass 10 does not yet define undo/redo grouping for continuous value drags.
- Pass 16 still does not provide a general non-scalar parameter model for
  color, palette, curve, point-list, text, action, model/provider, or
  file/path values.
- Numeric array-element support is intentionally explicit per parameter; it is
  not a general color/palette array keyframing system.
