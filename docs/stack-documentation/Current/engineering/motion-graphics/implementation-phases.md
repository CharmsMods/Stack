# Implementation Phases

This is an initial staged outline. It should be revised after the first code
inspection pass.

## Phase 0 - Code And Behavior Map

Goal: understand current implementation boundaries before code changes.

Tasks:

- Map still-image import paths in Editor, graph, Library, and Composite.
- Map Composite layer state, transform state, draw path, and export path.
- Map current project persistence for Editor and Composite documents.
- Map existing inspector, double-click, context menu, and graph interaction
  patterns.
- Identify validation commands that can cover future changes.

Exit criteria:

- `source-map.md` identifies concrete files and responsibilities.
- `implementation-tracker.md` records verified current behavior.
- Phase 1 is rewritten from code reality rather than only product intent.

## Phase 1 - Slice Vocabulary And Data Contract

Goal: introduce a stable planning contract for slices before video support.

Possible tasks:

- Define common slice metadata.
- Document the already-shipped still-image terminology shift to `slice` /
  `Import Slice`.
- Define whether a video slice can share the same persisted object envelope as a
  still slice.
- Identify any remaining UI labels or workflows that still need slice alignment
  after the editor and graph rename.

Exit criteria:

- Slice model is documented.
- Backward compatibility concerns are listed.
- The already-shipped editor and graph terminology change is documented
  separately from the unresolved video and data-model work, so the docs do not
  imply broader completion than exists.

## Phase 2 - Timeline Foundation

Goal: add the smallest usable timeline model.

Possible tasks:

- Project frame rate and duration settings.
- Integer-frame playhead state.
- Track model with one initial target kind.
- Keyframe storage for a small set of properties.
- Scrubbing that updates preview state without export.

Exit criteria:

- A project can store and restore timeline state.
- Scrubbing frame `N` applies deterministic property values.
- Still images can be animated without video decode.

## Phase 3 - Canvas/Object Transform Animation

Goal: animate spatial properties before graph-wide parameter animation.

Possible tasks:

- Position, scale, rotation, and opacity keyframes.
- Linear interpolation first, with easing deferred unless the data model is
  ready.
- Keyframe add/remove/select interactions.
- Keyframe property inspector.

Exit criteria:

- A still slice or canvas object can move over time.
- Scrubbing and playback show stable transform changes.
- Persistence round-trips animated transforms.

## Phase 4 - Still-Frame Export At Time

Goal: make export respect timeline state for a single frame.

Possible tasks:

- Render/export current frame.
- Allow frame number or time entry for still export.
- Ensure export does not permanently mutate editing state.

Exit criteria:

- Exported still frame matches the visible timeline frame.
- Existing still-image export behavior remains compatible when no timeline is
  active.

## Phase 5 - Video Slice Preview And Scrubbing

Goal: import video as a slice and display the correct source frame during
scrubbing.

Possible tasks:

- Video metadata probe.
- Frame decode strategy.
- Preview/cache policy.
- Missing/unsupported codec behavior.

Exit criteria:

- A video slice can be placed in the workspace.
- Scrubbing selects the intended source frame.
- Still-image slices continue to work.

## Phase 6 - Video Export

Goal: render a frame range to video or an intermediate frame sequence.

Possible tasks:

- Frame-by-frame evaluation loop.
- Output dimensions, frame rate, background, and duration settings.
- FFmpeg or image-sequence backend.
- Progress/cancel/error surface.

Exit criteria:

- A simple animated composition exports to video.
- Missing encoder behavior is clear.
- Long exports do not corrupt the editing session.

## Phase 7 - Animated Graph Parameters

Goal: allow keyframes to drive processing parameters after transform animation
is stable.

Possible tasks:

- Parameter target addressing.
- Graph snapshot/evaluation at frame.
- Dirty tracking for time-varying graph parameters.
- UI affordance for auto-keyframing selected properties.

Exit criteria:

- A small set of graph parameters can be keyframed and exported.
- Time-varying graph evaluation is isolated from normal still-image editing.
