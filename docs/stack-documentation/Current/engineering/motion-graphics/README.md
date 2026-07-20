# Motion Graphics Workstream

This folder is the active planning home for adding motion graphics, timeline,
keyframe, video-slice, and video export workflows to Stack.

The purpose of this workstream is to organize the feature before implementation
starts. It should connect product intent, architecture decisions, research
needs, and implementation phases without moving or silently merging existing
docs.

## Current Intent

Stack should grow from still-image editing and spatial composition toward a
motion-capable creative workspace where:

- imported stills and videos can be treated as slices
- slices can appear on a canvas or in a layered composite
- a timeline can own tracks, playhead state, and keyframe markers
- keyframes can drive transforms, opacity, processing parameters, and later
  richer media properties
- still-frame export and rendered-video export are planned together
- per-frame graph evaluation is understood before code changes begin

## Folder Map

- `scope-and-terms.md`: vocabulary, scope boundaries, and initial product model.
- `open-decisions.md`: active decisions and research questions.
- `implementation-phases.md`: staged implementation outline.
- `implementation-tracker.md`: lightweight pass/status tracker for future work.
- `source-map.md`: existing docs and future code areas to inspect before
  implementation.
- `timeline-video-export-foundation/`: focused 2026-07-06 planning workspace
  for bottom timeline UI, connected-chain timeline rows, keyframe UX, FFmpeg
  packaging, and video export foundations.

## Related Existing Docs

Do not move or merge these yet. Treat them as source context for this
workstream.

- [Video Slice Input And Inspector Direction](../editor/2026-07-03-slice-import-video-and-inspector-direction.md)
- [Timeline, Keyframes, And Video Export Research](../../../Info/engineering/composite/2026-07-03-timeline-keyframe-and-video-export-research.md)
- [Compositing Workflow, Canvas Mode, And Graph Direction Questions](../architecture/2026-07-03-compositing-workflow-and-graph-direction-questions.md)
- [CompositeAnimationSpecs](../../../Info/engineering/composite/CompositeAnimationSpecs.md)
- [Unified Workspace Architecture](../architecture/UNIFIED_WORKSPACE_ARCHITECTURE.md)

## Routing Rules

- Active plans, pass notes, and implementation status for this feature belong in
  this folder.
- Deeper research notes that are not yet implementation plans should stay in or
  move to `docs/stack-documentation/Info/engineering/`, likely under a future
  `motion-graphics/` research folder.
- Original source notes remain under
  `docs/stack-documentation/Archived/source-notes/`.
- Existing docs should only be merged or superseded after an explicit decision.

## Current Status

Status: planning, with one completed background terminology change.

Editor and graph UI wording has already shifted from image-oriented import
language toward `slice` and `Import Slice`, and that completed terminology
transition is archived under
`docs/stack-documentation/Archived/engineering/motion-graphics/2026-07-03-slice-terminology-transition-complete.md`.
Video slices, slice data and persistence rules, timeline work, keyframing, and
export planning remain active. The next useful pass is still to inspect current
code paths for image import, Composite layers, Editor graph imports,
viewport/canvas transforms, export, and persistence, then update the source map
and implementation tracker.

The 2026-07-06 timeline/video export foundation folder narrows the next pass:
read `timeline-video-export-foundation/README.md` before researching the
current code for timeline panel placement, connected chain discovery,
keyframing UX, frame evaluation, frame-producer export, and app-local approved
FFmpeg executable packaging.
