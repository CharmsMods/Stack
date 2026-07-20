# Source Map

This source map starts with documentation references. Code paths should be added
after an explicit inspection pass.

## Documentation Context

### Active Planning And Architecture

- `docs/stack-documentation/Current/engineering/editor/2026-07-03-slice-import-video-and-inspector-direction.md`
  - Remaining video slice import direction.
  - Double-click slice settings.
- `docs/stack-documentation/Current/engineering/architecture/2026-07-03-compositing-workflow-and-graph-direction-questions.md`
  - Graph directionality.
  - Layered composite versus free canvas workflow.
  - Per-object graph versus canvas transform ownership question.
- `docs/stack-documentation/Current/engineering/architecture/UNIFIED_WORKSPACE_ARCHITECTURE.md`
  - Global graph plus Canvas Object node recommendation.
  - Texture-space processing versus canvas-space transforms.
  - Unified workspace phase outline.
- `docs/stack-documentation/Current/engineering/editor/ADVANCED_NODE_GRAPH_COMPOSITOR_GUIDE.md`
  - Editor graph evolution toward typed sockets, masks, mix nodes, and full graph
    evaluation.
- `docs/stack-documentation/Current/engineering/editor/NODE_GRAPH_INTERACTION_GUIDE.md`
  - ImGui graph interaction ownership rules that future timeline/keyframe UI
    should not break.

### Research And Reference

- `docs/stack-documentation/Info/engineering/composite/2026-07-03-timeline-keyframe-and-video-export-research.md`
  - Timeline tracks.
  - Keyframes.
  - Frame-first timing.
  - Per-frame graph evaluation.
  - Still/video export research.
- `docs/stack-documentation/Info/engineering/composite/CompositeAnimationSpecs.md`
  - Composite-layer transform properties.
  - Keyframe engine research.
  - Timeline UI research.
  - FFmpeg export notes.
- `docs/stack-documentation/Info/engineering/composite/Composite_Web_Parity_Implementation_Guide.md`
  - Composite document schema and web parity reference.
  - Composite rendering/export concepts.
- `docs/stack-documentation/Info/engineering/SystemOverview.md`
  - High-level module overview and listed extension topics for animation and
    video rendering.
- `docs/stack-documentation/Info/engineering/ui/Dear ImGui Desktop UI Reference.md`
  - Immediate-mode animation constraints.
  - State ownership and draw-list guidance.

### Archived Source Notes

- `docs/stack-documentation/Archived/engineering/motion-graphics/2026-07-03-slice-terminology-transition-complete.md`
  - Completed editor and graph terminology shift to `slice` / `Import Slice`.
  - Boundary between shipped wording and unfinished video/data-model work.
- `docs/stack-documentation/Archived/source-notes/2026/2026-07-03-0133-slice-video-graph-notes.md`
  - Original preserved source note for the first slice/video/timeline/graph
    planning capture.
- `docs/stack-documentation/Archived/source-notes/2026/2026-07-06-0008-timeline-video-export-foundation.md`
  - Original preserved source note for bottom timeline UI, connected-chain
    timeline rows, keyframe UX, FFmpeg packaging, and video export setup.

### Focused Planning Folder

- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/`
  - Entry point for the 2026-07-06 foundation pass.
  - Defines connected-chain row questions, keyframing UX options, export and
    packaging research, and the next code-research plan.

## Code Areas To Inspect Later

No broad code inspection has been performed for this workstream yet. The only
verified implementation check so far was the narrow editor and graph
terminology pass used to archive the completed `slice` wording transition.
Likely areas:

- `src/Composite/`
  - Composite layer model, stage rendering, transforms, export, and persistence.
- `src/Editor/`
  - Editor graph import flow, image nodes, viewport, inspector/sidebar, project
    save/load, and render request handling.
- `src/Editor/NodeGraph/`
  - Graph model, graph UI, node interaction, links, pins, context menus, and
    serialization.
- `src/Renderer/`
  - Graph evaluation, render snapshots, cache behavior, and future
    frame-at-time evaluation.
- `src/Library/`
  - Asset import, thumbnail generation, project packaging, and persistence for
    media-backed slices.
- `src/App/`
  - Module routing, validation commands, file-drop dispatch, and future
    timeline/export entry points.
- `src/Persistence/`
  - Project envelope and binary/package format implications.
- `stack-tools.cmd`
  - Future FFmpeg setup or packaging implications if video export depends on an
    approved FFmpeg binary.

## Inspection Questions

- Where does a dropped or opened image first become editable state?
- Where would a video file be accepted or rejected today?
- Which current model most closely matches a future slice?
- Can Composite export already render arbitrary frame-like states, or only the
  current static document?
- Which UI surfaces own double-click, right-click, and selected-object
  inspectors?
- What validation path can prove a future timeline change without relying only
  on visual inspection?
