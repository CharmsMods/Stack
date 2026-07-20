# Timeline Realtime Editing Followups

- Captured: 2026-07-06 22:50
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-06-2250-timeline-realtime-editing-followups.md`
- Type: idea/update/fix
- Topic: motion-graphics
- Verification: partially-verified against docs and current timeline code

This note captures follow-up timeline behavior ideas and a current editing
problem noticed after Pass 7. These are not all implementation-ready, but the
realtime editing issue should be treated as a product behavior problem rather
than acceptable long-term behavior.

## Space While Timeline Is Playing

Desired behavior:

- If the timeline is playing, pressing `Space` while hovering the timeline
  should always stop/pause timeline playback first.
- After playback has stopped, the user can press `Space` again for the normal
  viewport/split resize behavior when the mouse is not over the timeline.

Current Pass 7 behavior:

- `Space` toggles play/pause only while hovering the timeline.
- Existing viewport/split behavior remains outside the timeline.

Follow-up:

- Tighten behavior so `Space` while timeline playback is active cannot
  accidentally trigger viewport/split resizing first.

## Frame-Step Wraparound

Desired behavior:

- When stepping left from the start of the current visible timeline range, the
  playhead should wrap to the other side of the currently visible timeline.
- Clarification from the user: "pressing E to go to the left" was a wording
  slip. The direction mapping should remain `Q` for previous/left and `E` for
  next/right.

Research needed:

- Pass 7 has no timeline zoom or visible time-window model yet, so "currently
  visible timeline" needs a concrete range concept before wraparound can be
  implemented exactly.
- Pass 8 implements the near-term fallback: previous/next frame stepping wraps
  across the current playback range. This is not the same as future
  visible-range wraparound.

## Draggable Keyframes

Desired behavior:

- Keyframe marks should be draggable along the timeline.
- Dragging should update the keyframe frame position.
- Dragging should preserve the keyframe's target, value, and interpolation
  unless the user performs a separate edit.

Research needed:

- How should dragging interact with snapping, frame quantization, overlapping
  keyframes, selected rows, shared upstream keyframes, and the future
  chain/node/property row hierarchy?
- Should dragging keyframes immediately request frame render refresh when the
  playhead is on or affected by the dragged keyframe?

## Realtime Viewport And Keyframe Editing Problem

Observed problem:

- While the timeline is open, adjusting values does not reliably update the
  viewport as expected unless the user presses `+ Key`.
- The current docs mention that scrubbing keyed frames and adding keyframes
  request render refreshes, but they do not explicitly describe this as a
  current editing limitation.

Current implementation reason:

- Pass 3 added manual keyframe creation through `+ Key`.
- Pass 4 added frame evaluation from stored keyframe tracks into render
  snapshot overrides.
- Pass 9 added a first update-existing-keyframe path for registered
  split-adjustment layer parameter edits while the timeline is open.
- The Pass 9 path updates matching keyframes at the current playhead frame,
  refreshes the preview, and does not create missing tracks or keyframes.
- Pass 10 added the off-keyframe live preview behavior: if the edit is not on
  a matching keyframe, the edited target temporarily bypasses timeline
  evaluation so the live node value can be viewed normally.
- The Pass 10 bypass clears when the playhead moves, playback starts, project
  timeline state resets, or `+ Key` writes the value.
- Pass 11 broadened the registered float-parameter coverage to blur/focus
  sliders, so this realtime behavior now applies to Box Blur, Gaussian Blur,
  Hankel/Optical Blur, and Tilt-Shift Blur float values too.
- Pass 12 broadened the registered numeric-parameter coverage to additional
  non-RAW float/integer slider controls, so the same realtime behavior applies
  to that numeric sweep too.
- Pass 13 broadened the registered numeric-parameter coverage to another
  effects/generate/stylize sweep and added explicit numeric array-element
  targets such as Chromatic Aberration center X/Y.
- Pass 14 broadened registered coverage to focused bool and enum/combo scalar
  controls. Those controls inherit the same realtime edit behavior, while
  frame evaluation holds discrete values between keyframes and writes typed
  JSON scalars.
- Pass 15 broadened registered coverage to Background Patcher's simple
  serialized scalar controls. Those controls inherit the same realtime edit
  behavior; target color, picker state, masks, and patch state remain outside
  the current scalar model.
- Broader node coverage, undo/redo grouping, and any future auto-key mode
  remain separate future work.

Desired behavior:

- If the playhead is on a keyframe for a node/property, changing that same
  node/property value should update that keyframe.
- If the playhead is on any keyframe for that same property row, editing the
  property should update the keyframe at that playhead frame.
- The viewport should update in realtime using the keyframed value and the
  rest of the downstream chain.
- If the playhead is not on a matching keyframe for that property, editing the
  value should not silently save a new keyframe.

Important nuance:

- This is not the same as broad auto-keyframing. The requested behavior is
  "update an existing keyframe when editing over that keyframe," not "create
  new keyframes whenever values change."
- A future explicit auto-key mode can still exist separately.

Related code/docs:

- `src/Editor/Internal/EditorModuleTimeline.cpp`
- `src/Editor/Timeline/TimelineAnimation.*`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/keyframing-ux-options.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/research-backlog.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/open-questions.md`
