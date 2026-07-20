# Timeline Video Export Research Backlog

- Created: 2026-07-06 21:58
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-06-2158-timeline-ux-keyframe-controls.md`
- Purpose: preserve research-needed timeline/video-export topics without
  treating them as immediate implementation instructions.

Use this file when the user says a topic needs more research, needs pros and
cons, or should be explored before becoming implementation scope. Keep entries
close enough to the user's wording that future research can recover the design
intent.

## How To Use This Backlog

- Add research prompts here before code changes when a topic is not settled.
- Keep the prompt, why it matters, and likely code/doc areas together.
- Move findings into the appropriate implementation docs only after research
  has been done.
- Do not treat an item in this file as approved implementation scope by itself.
- If a research pass resolves a topic, add a dated result note and link the
  decision or implementation doc that consumed it.

## Research Items

### Hierarchical Timeline Row Model

Prompt:

Research the best way to evolve the current output-chain row model into a
professional hierarchy of chain sections, node rows, and parameter/property
rows.

User intent:

- one section per connected output chain
- separate rows for nodes inside each chain
- expandable node rows that reveal separate value/property rows
- no keyframes for two different nodes on one horizontal row
- keyframes should ultimately appear on the specific value row they animate

Why it matters:

The pre-Pass 16 flat row model was acceptable for the early foundation, but
adding many keyframes for many nodes to one chain row would become confusing
quickly.

Likely code areas:

- `src/Editor/Internal/EditorModuleTimeline.cpp`
- `src/Editor/Timeline/TimelineAnimation.*`
- `src/Editor/Timeline/TimelinePersistence.*`
- `src/Editor/NodeGraph/Model/EditorNodeGraphTraversal.cpp`
- `tools/graph_behavior_tests.cpp`

Pass 16 result:

- The first implementation uses output-chain section rows, animatable node
  rows, and registered property rows.
- Actual keyframe marks render on property rows. Chain and node rows show dim
  summary marks for affected keyed frames.
- Chain and node expansion/collapse state is runtime-only and not persisted.
- Unsupported/non-scalar values do not get placeholder property rows yet.

Status: foundation implemented; row density, persisted expansion, context
menus, virtualization, drag behavior, and final visual polish remain open.

### Keyframe Color And Visual Differentiation

Prompt:

Research how keyframe marks should be colored and visually distinguished across
chain, node, and property rows.

User intent:

- keyframes need to be differentiable by color
- color should help separate targets and states
- the design should not rely only on color if other visual states are needed

Questions:

- Should colors map to node type, parameter type, selected state, interpolation
  state, or track identity?
- How should shared upstream keyframes appear on multiple affected chains?
- How should selected, hovered, disabled, missing-target, or conflicting
  keyframes look?

Status: open.

### Full Animatable Node Integration

Prompt:

Research how to expand animatable coverage so almost any real adjustable node
value can be keyframed without creating one-off timeline-only controls.

User intent:

- deeply integrate keyframing with node values
- support essentially any value on any node over time
- continue node-group-complete coverage rather than random individual sliders

Questions:

- Which node groups should be covered next?
- Which controls need non-float value support?
- Which node values are not safe to interpolate?
- What registry metadata is missing for display, grouping, ranges, defaults,
  interpolation, and persistence?
- Do multiple focused code-research agents make sense for mapping the current
  node/value surface?

Implemented foundation:

- Pass 3 implements the first registered float coverage group for split
  adjustment layer nodes.
- Pass 11 implements the next registered float coverage group for blur/focus
  nodes: Box Blur, Gaussian Blur, Hankel/Optical Blur, and Tilt-Shift Blur.
- Pass 12 broadens the registry from float-only to numeric float/integer
  parameters and covers another non-RAW numeric sweep: split corruption, split
  compression, split denoising, split edge effects, crop/rotate numeric
  transform controls, and heat/ripple distortion numeric controls.
- Pass 13 adds explicit numeric array-element targets and covers another
  effects/generate/stylize sweep: Bilateral Filter, Noise numeric sliders,
  split dither numeric sliders, HDR, Color Grade strength, Vignette,
  Chromatic Aberration, Lens Distortion, Glare Rays, Airy Bloom, Halftoning,
  Cell Shading, Image Breaks, Analog Video, Expander padding, and Palette
  Reconstructor blend/smoothing.
- Pass 14 adds the first bool and enum/combo scalar support. These targets use
  the existing numeric keyframe schema for storage, evaluate as hold/stepped
  tracks, and write typed JSON scalars. Covered controls include Tilt-Shift
  Blur filter type, Flip horizontal/vertical, Bilateral Filter kernel/edge
  mode, Noise type/blend mode, split dither gamma/palette toggles, Chromatic
  Aberration falloff link, Halftoning pattern/color/bool controls, Cell
  Shading mode/bool controls, and Palette Reconstructor smoothing type.
- Pass 15 adds Background Patcher scalar coverage for removed-area opacity,
  color tolerance, edge smoothing, edge shift, defringe, keep-selected range,
  and visualizer state. The node's target color, picker state, masks, patch
  state, and placeholder anti-aliasing state remain outside scalar coverage.
- Colors, palette banks, action/randomizer controls, point/curve editors,
  model/path/provider values, scene-workflow nodes, RAW/develop controls,
  hidden/deprecated nodes, external-model/cache-heavy nodes, and remaining
  node-specific non-scalar state remain outside the current registry coverage.

Status: partially implemented; remaining node-support research is mostly
non-scalar, scene-workflow, hidden/deprecated, RAW/develop, or
external-provider/cache/action-heavy edge cases beyond registered scalar
values.

### Timeline Header And Icon Control Strip

Prompt:

Research and design the static timeline header/control strip.

User intent:

- header stays fixed while rows scroll vertically
- no clipping or truncation in the top control row
- replace many text buttons with icons to improve spacing
- avoid unnecessary explanatory text and redundant labels
- use the space for actual timeline controls, not repeated descriptions

Questions:

- Which controls should remain visible in the header after a settings popup
  exists?
- Which controls should become icons?
- Which controls should move out of the header into timeline/video settings?
- What tooltip and accessibility labels are needed for icon-only controls?

Status: open.

### Timeline And Video Settings Popup

Prompt:

Research a project/timeline video settings popup similar to Stack's normal
settings window.

User intent:

- keep output resolution, encoding/video settings, and timeline FPS in a popup
- avoid cramming video settings into the timeline header
- settings should belong to the current open project/timeline

Questions:

- Which settings are project data and should persist?
- Which settings are app preferences?
- Which settings are export-job-only?
- How should this relate to the existing Composite export settings UI?
- Should timeline FPS move entirely into this popup once keyframes exist?

Status: open.

### Frame Rate Locking And Retiming

Prompt:

Research how Stack should handle timeline FPS once keyframes exist.

User intent:

- adding a keyframe should commit the current timeline frame rate
- the user should be notified when this happens
- changing FPS later should require an explicit dialog
- some FPS changes may be allowed through retiming or interpolation modes

Possible modes:

- lock FPS completely once keyframes exist
- allow multiplying FPS by compatible factors and interpolate between existing
  keyframes
- allow non-interpolating retime by materializing extra keyframes
- allow export-time FPS changes without changing the timeline grid

Questions:

- What exactly is the permanent timeline frame grid?
- Does changing FPS alter keyframe frame numbers, keyframe times, or export
  sampling only?
- How should hold keyframes behave during retiming?
- How should this interact with persisted `editorTimeline` schema version 1?

Status: open.

### Timeline Keyboard Shortcuts

Prompt:

Research timeline keyboard shortcut ownership and conflicts.

User intent:

- `Q` and `E` could step left/right by one frame globally
- `Space` could play/pause when hovering the timeline
- existing `Space` behavior for viewport sizing should continue when the
  timeline is not hovered

Questions:

- Which keys are currently used globally, in the graph, in the viewport, and in
  text inputs?
- Should Q/E always affect the timeline or only when the timeline is open?
- Should Space use hover, focus, or an active timeline-input mode?
- How should shortcuts behave while editing numeric fields or text boxes?

Status: open.

### Timeline Space Stop Priority

Prompt:

Research and implement the exact `Space` behavior when timeline playback is
active.

User intent:

- pressing `Space` while the timeline is playing should always stop or pause
  timeline playback first
- after playback is stopped, the user can press `Space` again to use the
  normal viewport/split resize behavior when the mouse is not over the timeline

Questions:

- Should timeline playback consume `Space` globally while playing, or only
  when the timeline is hovered?
- Should "stop" mean pause at the current frame or stop/reset to frame `0`?
- How should this interact with active text/numeric inputs and modal popups?

Status: open.

### Timeline Frame-Step Wraparound

Prompt:

Research frame-step wraparound behavior for previous/next shortcuts.

User intent:

- stepping left from the start of the current visible timeline should wrap to
  the other side of the currently visible timeline
- the user confirmed "E to go to the left" was a wording slip; keep `Q` mapped
  to previous/left and `E` mapped to next/right

Questions:

- What is the "currently visible timeline" before timeline zoom/windowing
  exists?
- Should near-term wraparound use full duration, loop range, last-keyframe
  range, selected chain range, or future visible window range?
- Should previous/next shortcuts wrap only when loop playback is enabled?

Implemented foundation:

- Pass 8 implements `Q`/`E` wraparound across the current playback range.
- Exact wraparound across a future visible timeline window remains open until
  timeline zoom/windowing exists.

Status: partially implemented; visible-window behavior remains open.

### Draggable Keyframes

Prompt:

Research draggable keyframe marks on the timeline.

User intent:

- keyframes should be draggable
- dragging a keyframe should move it to a new frame
- dragging should preserve the target, value, and interpolation unless the user
  performs a separate edit

Questions:

- How should dragging interact with snapping and integer frame quantization?
- What happens when a dragged keyframe lands on another keyframe for the same
  target?
- Should drag behavior differ for chain summary rows versus future
  property/value rows?
- How should dragging shared upstream keyframes appear across affected chains?

Status: open.

### Realtime Existing-Keyframe Editing

Prompt:

Research and design realtime viewport updates and existing-keyframe updates
when the user edits a node value while the playhead is on a matching keyframe.

User intent:

- if the playhead is on a keyframe for the edited node/property, changing the
  value should update that keyframe
- the viewport should update immediately with the new keyframed value and the
  downstream chain result
- if the playhead is not on a matching keyframe, editing should not silently
  create or save a new keyframe

Important distinction:

- This is not broad auto-keyframing. It is update-existing-keyframe behavior
  while editing over an existing keyframe.

Questions:

- Which node value-edit paths can report the edited animatable parameter
  target?
- Can the existing layer/node UI know when the playhead is on a matching
  keyframe?
- Should this behavior require an armed timeline/edit mode, or should it always
  update matching keyframes?
- How should render refresh be requested so the viewport updates while dragging
  or typing values?
- How should this interact with undo/redo, project dirty state, and persisted
  `editorTimeline` data?

Implemented foundation:

- Pass 9 implements update-existing-keyframe behavior for registered
  split-adjustment layer parameters through dirty-tracked layer edits.
- The implementation updates only matching keyframes at the current playhead
  frame and does not create missing tracks or keyframes.
- The preview is refreshed after an existing keyframe value changes.
- Pass 10 implements off-keyframe live edit preview for registered animated
  parameters: the edited target is temporarily removed from frame evaluation
  while the playhead remains on the same frame, so normal live node edits are
  visible without saving a keyframe.
- The temporary preview clears when the playhead changes, playback starts,
  project timeline state resets, or `+ Key` writes the value.
- Pass 11 broadens this realtime behavior to blur/focus float sliders because
  they use the same registered float-parameter path.
- Pass 12 broadens this realtime behavior to the registered non-RAW numeric
  float/integer slider controls because they use the same registered numeric
  parameter path.

Remaining research:

- broaden coverage beyond the registered split-adjustment, blur/focus, and
  Pass 12 numeric parameter families
- define undo/redo grouping for continuous edits
- decide whether the behavior should ever run while the timeline panel is
  closed
- design the separate broad auto-key mode that may create new keyframes

Status: partially implemented; broader behavior remains open.

### Playback Loop Defaults

Prompt:

Research default playback looping behavior for the timeline.

User intent:

- timeline may loop by default
- it may loop when it reaches the last keyframe
- if there is only one keyframe, playback should not stop in a way that feels
  pointless or broken

Questions:

- Should loop range be full duration, visible work area, selected chain range,
  or last-keyframe range?
- If different chains have different last keyframes, which one defines the
  loop?
- Should loop-to-last-keyframe be a mode separate from full-duration loop?
- What should happen when there are zero or one keyframes?

Status: open.

### Dense Timeline Visual Design

Prompt:

Research visual density, spacing, animation, and responsiveness for the future
large-graph timeline.

User intent:

- large graphs may create a lot of timeline rows
- spacing and animation quality matter
- users need to visually separate chain, node, and value rows
- the panel should cram lots of information into limited space without feeling
  chaotic

Questions:

- What row heights and indentation levels work at common panel heights?
- How should expand/collapse animation behave?
- How should row virtualization or clipping work for very large graphs?
- Which text can be removed, abbreviated, or moved to tooltips?
- What responsive behavior is needed when the timeline panel is narrow or
  short?

Status: open.
