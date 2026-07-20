# Open Questions

This file is for user-facing decisions and code-research questions that should
be answered before implementation.

## Product And Model

Answered:

1. Timeline rows should be anchored internally to final output nodes.
2. The final output node gets its final form from the connected chain feeding
   it.
3. Timeline rows should be created automatically from graph structure per
   output object chain.
4. Keyframes on shared upstream nodes should affect every downstream output
   branch that depends on the animated node.

Still open:

1. Should shared upstream keyframes be visually shown on every affected output
   row, on a separate shared/source row, or both?
2. Should every chain be user-renamable from the timeline, the graph, or both?
3. What exact graph object counts as a final output node in the current code?

Pass 1 code answer:

- The current final output graph object is
  `EditorNodeGraph::NodeKind::Output`.

## Timeline Layout

Answered for Pass 1:

1. The timeline panel sits as a full-width bottom band inside the editor
   workspace, below both graph and viewport panes.
2. Timeline height reduces both graph and viewport height together.
3. The timeline remembers its last runtime height while Stack is open, but does
   not persist it to projects or app settings yet.
4. `Ctrl+Shift+Tab` only toggles the timeline in Pass 1.

Answered for Pass 5:

1. The first playback controls live in the timeline header.
2. Playback advances the current frame using the timeline FPS value.
3. The first loop behavior is a runtime timeline toggle.

Answered for Pass 6:

1. Timeline duration, FPS, current frame, tracks, targets, keyframes, values,
   and interpolation are persisted in project pipeline data.
2. Timeline open/height and playback state are not persisted in Pass 6.

Answered by 2026-07-06 design intake:

1. The timeline header/control strip should stay fixed while the row list
   scrolls vertically.
2. Header controls should avoid clipped/truncated text.
3. The dense timeline surface should avoid unnecessary explanatory text and
   should move toward icon-first controls where appropriate.

Answered for Pass 7:

1. The current timeline header renders in a fixed child above the ruler and
   scrolling row list.
2. Duration and FPS controls moved into a simple timeline settings popup to
   reduce header crowding.
3. The first compact transport controls use symbolic buttons plus tooltips.

Still open:

1. Should timeline open/closed state and height be persisted later in app
   settings rather than project data?
2. Should `Ctrl+Shift+Tab` focus the timeline in a later keyboard-navigation
   pass?
3. Which exact controls remain in the fixed timeline header once a dedicated
   timeline/video settings popup exists?
4. Which controls should become icons, and which need text labels to remain
   understandable?

## Keyframe Creation

Answered for Pass 3:

1. The first keyframe flow starts in the timeline header for the selected
   output row: choose a supported target and press `+ Key`.
2. The first node group with complete animatable coverage is the split
   adjustment layer family: Brightness, Contrast, Saturation, Warmth, and
   Sharpen.

Answered for Pass 9:

1. Editing a registered split-adjustment node/property while the timeline is
   open and the playhead is on an existing matching keyframe updates that
   keyframe.
2. Existing-keyframe editing is separate from broad auto-keyframing; it does
   not create missing keyframes.
3. The viewport/composite preview is refreshed after the matching keyframe is
   updated.

Answered for Pass 10:

1. If the playhead is not on a matching keyframe, editing a registered animated
   parameter should still preview the live node value normally without saving a
   keyframe.
2. Timeline evaluation should take over again when the playhead moves or
   playback starts.

Answered for Pass 11:

1. The next complete animatable-parameter coverage pass covers the blur/focus
   float-slider group: Box Blur, Gaussian Blur, Hankel/Optical Blur, and
   Tilt-Shift Blur.
2. Discrete/enum/combo values in that area, such as Tilt-Shift Blur filter
   type, remain future work pending a non-float or stepped-keyframe model.

Answered for Pass 12:

1. The next broad coverage direction is numeric non-RAW layer controls, leaving
   RAW/develop controls alone.
2. Integer slider controls can use the existing numeric keyframe storage as
   long as frame evaluation rounds and clamps them before writing JSON.
3. Pass 12 covers split corruption, split compression, split denoising, split
   edge effects, crop/rotate numeric transform controls, and heat/ripple
   distortion numeric controls.
4. Bools, enums/combos, colors, palette banks, randomize/action buttons, text,
   file/path values, model/provider selectors, point lists, and curve editors
   remain edge-case value types for future design.

Answered for Pass 13:

1. The next broad numeric sweep covers Bilateral Filter, Noise numeric sliders,
   split dither numeric sliders, HDR, Color Grade strength, Vignette,
   Chromatic Aberration, Lens Distortion, Glare Rays, Airy Bloom, Halftoning,
   Cell Shading, Image Breaks, Analog Video, Expander padding, and Palette
   Reconstructor blend/smoothing.
2. Numeric array-element support is allowed for explicit numeric targets such
   as Chromatic Aberration center X/Y, but does not imply color/palette array
   keyframing.
3. Remaining node-support work is mostly typed edge cases: bools,
   enums/combos, colors, palettes, randomize/action buttons, text/path/model/
   provider values, curve/point editors, hidden/deprecated nodes, RAW/develop/
   scene-workflow nodes, and external-model/cache/action-heavy nodes.

Answered for Pass 14:

1. The first typed scalar implementation supports bool and enum/combo values
   through the existing keyframe storage schema by keeping stored values
   numeric and writing typed JSON at frame evaluation time.
2. Bool and enum/combo tracks evaluate as hold/stepped values between
   keyframes, not as linear interpolation through intermediate states.
3. Pass 14 covers focused non-RAW typed scalar controls on already partially
   supported nodes: Tilt-Shift Blur filter type, Flip horizontal/vertical,
   Bilateral Filter kernel/edge mode, Noise type/blend mode, split dither
   gamma/palette toggles, Chromatic Aberration falloff link, Halftoning
   pattern/color/bool controls, Cell Shading mode/bool controls, and Palette
   Reconstructor smoothing type.

Answered for Pass 15:

1. The remaining visible non-RAW registry node with simple serialized scalar
   controls is Background Patcher.
2. Pass 15 covers Background Patcher removed-area opacity, color tolerance,
   edge smoothing, edge shift, defringe, keep-selected-range, and visualizer
   toggle.
3. Background Patcher target color, sampled picker state, masks, patching
   state, and placeholder anti-aliasing state remain edge-case/non-scalar work.

Answered for Pass 16:

1. The first row hierarchy replacing the flat chain rows is: output-chain
   section rows, animatable node rows, and registered property rows.
2. Actual keyframe diamonds render on property rows. Chain and node rows render
   dim summary marks for affected keyed frames.
3. Chain and node expansion state is runtime-only for now and is not persisted.
4. The hierarchy uses only already-registered animatable parameters; unsupported
   or non-scalar values do not get placeholder property rows in this pass.

Still open:

1. Should the end-goal main keyframe flow start in the node/inspector,
   timeline right-click menu, timeline header, or a hybrid of these?
2. Should auto-keyframing exist in the end-goal workflow?
3. If auto-keyframing exists, should it be off by default?
4. Should changing a value at frame `0` change the base value, create a
   keyframe, or depend on auto-key state?
5. Which remaining edge-case family should be implemented first after the
   Background Patcher scalar pass: colors/palettes, action buttons,
   curve/point editors, model/path/provider values, scene-workflow nodes, or
   external-model/cache-heavy nodes?
6. How should keyframes be color-coded across target identity, node type,
   parameter type, selected state, and interpolation state?
7. Should shared upstream keyframes eventually show any extra cross-chain
   linking or target-origin indicator beyond the Pass 16 summary/property marks?
8. How should update-existing-keyframe edits be grouped for undo/redo,
   especially during continuous slider drags?
9. Should update-existing-keyframe behavior eventually apply when the timeline
   is closed, or only while the timeline panel is open?

## Timeline Frame Rate And Retiming

Answered by 2026-07-06 design intake:

1. Adding the first keyframe should make the current timeline FPS important
   committed timing data, not a casual editable display value.
2. Changing FPS after keyframes exist should require an explicit user-facing
   flow that explains the consequences.

Still open:

1. Should timeline FPS be fully locked after the first keyframe, or should
   retiming be supported immediately?
2. If retiming is supported, should it alter keyframe frame numbers, preserve
   real time, or only affect export sampling?
3. Should compatible FPS multiplication create interpolated in-between frames?
4. Should a non-interpolating retime materialize additional keyframes to avoid
   unexpected value interpolation?
5. Should export-time FPS differ from timeline FPS without changing the
   persisted timeline frame grid?
6. How should hold interpolation behave during FPS retiming?

## Timeline Keyboard Shortcuts

Answered by 2026-07-06 design intake:

1. Timeline-specific shortcuts are desired, including frame stepping and
   play/pause.
2. Shortcut behavior may need to depend on whether the mouse is hovering the
   timeline panel.

Answered for Pass 7:

1. `Q` steps one frame backward and `E` steps one frame forward when the
   timeline is open and no item is active.
2. `Space` toggles timeline play/pause only when the mouse is hovering the
   timeline panel.
3. Existing `Space` viewport/split behavior remains active outside the
   timeline.
4. The user confirmed the `Q`/`E` direction mapping should stay as `Q` for
   previous/left and `E` for next/right.

Answered for Pass 8:

1. While timeline playback is active, `Space` pauses playback before existing
   viewport/split resize handling can run.
2. `Q`/`E` frame stepping wraps across the current playback range.

Still open:

1. Should `Q`/`E` eventually require timeline focus instead of just timeline
   open state?
2. Should shortcut behavior become configurable in app settings?
3. How should shortcuts behave when a text field, numeric input, combo box, or
   popup is active?
4. Should shortcut changes be grouped into a broader input/focus pass?
5. After timeline zoom/windowing exists, should previous/next frame shortcuts
   wrap across the visible window instead of the playback range?

## Keyframe Direct Manipulation

Answered by 2026-07-06 22:50 design intake:

1. Keyframes should be draggable in the timeline.

Still open:

1. Should keyframe dragging start with property rows only, or should summary
   marks on chain/node rows also be draggable in some mode?
2. Should keyframe dragging snap only to whole frames, or support subframe
   timing later?
3. What should happen when a dragged keyframe lands on an existing keyframe for
   the same target?
4. Should dragging a keyframe request immediate viewport refresh when the
   playhead is on or affected by that keyframe?

## Playback Loop Defaults

Answered by 2026-07-06 design intake:

1. Looping by default is desirable enough to research and likely include in a
   future behavior pass.
2. Looping to the last keyframe may be a useful mode, but needs edge-case
   design.

Answered for Pass 7:

1. Loop playback defaults on for runtime/reset/load state.
2. Looping playback uses the last distinct keyframe frame when there are at
   least two distinct keyed frames.
3. Zero/one-keyframe timelines continue using full duration so playback does
   not snap around a single frame.

Still open:

1. Should default loop range eventually be full timeline duration, visible work area, last
   keyframe, selected chain, or selected property track?
2. If different chains have different last keyframes, which one defines the
   loop endpoint?
3. Should loop-to-last-keyframe be a separate toggle from normal looping?
4. Should the eventual hierarchical timeline use selected chain/property rows
   to define loop range?

## Interpolation And Curves

Answered for Pass 4:

1. The first evaluation path supports exact keyframes, linear interpolation,
   hold interpolation, and nearest-key clamping outside the keyed range.
2. Interpolation is stored per keyframe, where the keyframe controls the segment
   that leaves that key.

Still open:

1. Is linear interpolation enough for the first user-facing playback/export
   implementation, or should easing be added before export UI ships?
2. Should the data model include easing/curve fields immediately even if the UI
   only exposes linear interpolation first?
3. Should interpolation remain per keyframe, move to per property track, or be
   global until
   curve editing exists?
4. What curve editor UI should eventually expose interpolation/easing?

## Export

Answered:

1. Playback/export should use a reliable `FrameEvaluationContext` that does
   not mutate live graph state.
2. If frame-context evaluation cannot be made reliable immediately, research a
   graph snapshot/clone fallback rather than mutating the live graph.
3. Export should use a frame producer with replaceable sinks.
4. Image-sequence export should remain a first-class fallback/debug export path.
5. Normal packaged video export should use an approved app-local external
   `ffmpeg.exe`, not system-wide FFmpeg setup.
6. FFmpeg DLL/library integration is not the planned initial architecture.
7. User-configured `ffmpeg.exe` and optional `PATH` discovery are fallback
   providers.
8. Pass 2 runtime discovery uses app-local `tools\ffmpeg`.
9. Pass 2 release packaging copies `_workspace\ffmpeg-provider` to packaged
   `tools\ffmpeg` only when `ffmpeg-provider.json` passes checks.
10. Missing FFmpeg is a supported state; present but unapproved/unsafe FFmpeg
    blocks packaging and provider validation.
11. Pass 4 runtime frame evaluation uses a non-mutating
    `FrameEvaluationContext` and render snapshot layer JSON overrides for the
    covered split adjustment parameters.
12. Pass 5 adds a frame-specific graph snapshot path and single-output raster
    entry point for future export work.

Still open:

1. Which exact FFmpeg build can be approved for app-local packaging?
2. Which output formats/codecs should be offered in the implementation plan?
3. Should export use the current canvas resolution only, or allow an override
   later?
4. Should video export block editing with a progress modal, or run in the
   background?
5. Should Stack refuse GPL/nonfree user-selected FFmpeg builds by policy,
   allow user-selected builds with warnings, or only support approved/provider
   manifests for all encoder sources?
6. Should image-sequence export be implemented before FFmpeg process export?
7. Which video/export settings belong in a project timeline settings popup,
   which belong to a one-off export job, and which belong in app preferences?

## Code Research Questions

1. Which current data model best maps to a future connected chain?
2. Which current object owns viewport/canvas resolution?
3. Which current export path is safest to reuse for frame-by-frame rendering?
4. Which UI code owns graph panel sizing and could host a resizable timeline?
5. Which current parameter controls can be made animatable without a one-off
   implementation per node?
6. What persistence format changes are required for timeline rows, keyframes,
   frame rate, and duration?
7. Which output-node IDs are stable enough to anchor automatic timeline rows?
8. Which existing hotkeys conflict with proposed timeline shortcuts such as
   `Q`, `E`, and `Space`?
9. What existing settings-window or popup patterns should the timeline/video
   settings popup reuse?

Pass 1 code answers:

- `EditorNodeGraph::CompletedChainInfo` is the current best map for row
  discovery.
- `CompletedChainInfo::outputNodeId` is stable enough for runtime automatic
  rows in Pass 1.
- Pass 6 now saves timeline tracks by graph node ID plus parameter ID. Future
  work still needs deeper repair/remap rules for node duplication, paste, and
  graph identity migrations.

Pass 3 code answers:

- Split adjustment layer controls are now discoverable through
  `Stack::Timeline::CollectAnimatableParametersForNode(...)`.
- Runtime keyframes use node ID plus stable parameter ID; Pass 6 persists that
  identity and validates it on load.

Pass 4 code answers:

- `FrameEvaluationContext` samples timeline tracks at the current frame.
- Covered split adjustment layer parameters are applied to copied
  `RenderGraphNode::layerJson` in `BuildGraphSnapshot()`, not to live layer
  objects.
- The renderer fingerprints layer JSON, so sampled frame values participate in
  graph render cache identity.
- At the end of Pass 4, playback transport, frame producer/export loops, and
  project serialization were still unimplemented. Passes 5 and 6 have since
  added playback/frame producer foundations and timeline project persistence.

Pass 5 code answers:

- Runtime playback transport now exists in the timeline header, but only as
  visible UI controls; keyboard shortcuts are still undecided.
- `BuildGraphSnapshotForTimelineFrame(...)` builds frame-specific graph
  snapshots without moving the UI playhead.
- `BuildSingleOutputTimelineFrameRaster(...)` can render one requested timeline
  frame to RGBA pixels for future export sinks.
- At the end of Pass 5, timeline tracks, duration, and FPS were still not
  persisted. Pass 6 now persists them; playback state remains runtime-only.

Pass 6 code answers:

- `editorTimeline` schema version 1 persists duration, FPS, current frame,
  tracks, node/parameter targets, keyframes, values, and interpolation.
- Timeline target validation runs after graph/layer load and drops missing or
  unsupported targets safely.
- Negative keyframe frames are ignored; frames beyond duration are clamped.
- Playback state, loop state, open/height UI state, and playback accumulator
  remain runtime-only.
