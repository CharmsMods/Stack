# Timeline, Keyframes, And Video Export Research

- Captured: 2026-07-03 01:33
- Source: docs/stack-documentation/Archived/source-notes/2026/2026-07-03-0133-slice-video-graph-notes.md
- Type: research
- Topic: composite
- Verification: partially-verified

We need a timeline so tracks can hold keyframe markers. The rough interaction direction is:

- Create a new track.
- Expose a dropdown for each track that has any slice on it.
- Add keyframes on the track.
- Open a keyframe via right-click into a UI panel that expands like an inspector so the user can choose exactly which values or aspects of the image/video are being auto-keyframed.

For timeline timing, the current preference is to think in frames first, not seconds first. We probably still want second-based entry or display, but every time value should round to the nearest frame so keyframes always land on real frame boundaries.

When processing video, the system will effectively evaluate what each connected node does for every frame. That makes per-frame graph evaluation and export behavior an important part of the design.

Still-frame export and video export belong in the same research area. The system should support exporting a still frame as well as a rendered video.

## Research Needed

- Decide whether the canonical timeline unit is frame-first with second conversion, or a different hybrid model.
- Define how track ownership works for slices, objects, or graph outputs.
- Define how keyframed object transforms interact with graph-driven parameter changes.
- Compare right-click keyframe inspector behavior with existing Stack inspector patterns.
- Map the current single-frame Composite export path to a future frame-sequence or FFmpeg-backed video export path.

## Related Docs

- docs/stack-documentation/Info/engineering/composite/CompositeAnimationSpecs.md
- docs/stack-documentation/Info/engineering/SystemOverview.md
