# Timeline Video Export Foundation

This folder is the active planning workspace for the first serious design pass
toward timeline-driven animation and video export in Stack.

This folder is now both the planning workspace and the implementation ledger
for the first timeline/video-export foundation passes. Future implementation
agents should read the progress file before editing so completed passes are not
duplicated.

## Status

- Created: 2026-07-06
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-06-0008-timeline-video-export-foundation.md`
- Parent workstream: `docs/stack-documentation/Current/engineering/motion-graphics/`
- Current phase: Pass 16 timeline row hierarchy foundation
  code complete; automated validation passed; native/manual UI smoke pending
- Latest design intake: 2026-07-06 timeline UX/keyframe controls source note
  routed into this folder; no code changes were made for that intake
- Code changes: runtime-only bottom timeline shell, output-chain rows,
  `Ctrl+Shift+Tab` toggle, split-output completed-chain test, app-local
  FFmpeg provider probe, provider validation command, and manifest-gated
  release packaging support, runtime keyframe model, first animatable
  parameter registry coverage for split adjustment layer nodes, timeline
  target selection, keyframe marks, frame sampling, temporary frame evaluation
  context, non-mutating render snapshot overrides for the covered split
  adjustment parameters, runtime playback controls, FPS-based playhead
  advancement, frame-specific graph/raster entry points, and project
  save/load for timeline settings plus keyframe tracks, fixed timeline header
  polish, compact transport controls, a simple timeline settings popup,
  guarded timeline shortcuts, loop-to-last-keyframe playback behavior,
  off-keyframe live edit preview, first blur/focus float slider coverage,
  integer numeric timeline parameter support, non-RAW numeric layer node
  coverage sweeps, numeric array-element support for registered values, typed
  bool/enum scalar support for focused non-RAW layer controls, Background
  Patcher scalar support, and the first hierarchical chain/node/property
  timeline rows

## Read Order

Use this file as the entry point for this focused planning folder. A future
agent should read these files in order:

1. `docs/stack-documentation/README.md`
2. `docs/stack-documentation/IDEA_INTAKE_PROTOCOL.md`
3. `docs/stack-documentation/Current/engineering/motion-graphics/README.md`
4. `docs/stack-documentation/Current/engineering/motion-graphics/scope-and-terms.md`
5. `docs/stack-documentation/Current/engineering/motion-graphics/open-decisions.md`
6. `docs/stack-documentation/Current/engineering/motion-graphics/source-map.md`
7. `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/requirements-capture.md`
8. `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/technical-decisions.md`
9. `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/chain-and-track-model.md`
10. `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/keyframing-ux-options.md`
11. `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/2026-07-06-timeline-ux-keyframe-controls.md`
12. `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/research-backlog.md`
13. `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/technical-explanations.md`
14. `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/export-backend-and-packaging.md`
15. `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/code-research-plan.md`
16. `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/open-questions.md`
17. `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/implementation-progress.md`
18. `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/implementation-agent-context.md`

## Folder Map

- `requirements-capture.md`: user-stated requirements, constraints, and
  non-assumptions from the 2026-07-06 planning prompt.
- `technical-decisions.md`: user-confirmed technical direction for output-node
  anchoring, automatic rows, shared upstream keyframes, end-goal framing, and
  node-group-complete animatable coverage.
- `chain-and-track-model.md`: initial language for connected chains, chain
  naming, split-output edge cases, and timeline row ownership.
- `keyframing-ux-options.md`: candidate UX models for adding keyframes and
  auto-keyframing node and chain values.
- `2026-07-06-timeline-ux-keyframe-controls.md`: routed design-intake note for
  static timeline header behavior, hierarchical keyframe rows, icon controls,
  timeline settings, FPS locking, shortcuts, and visual density.
- `research-backlog.md`: list of research-needed topics that should not be
  treated as implementation scope until a research pass resolves them.
- `technical-explanations.md`: plain-language explanation of frame-at-time
  evaluation, frame-first export architecture, and FFmpeg dependency strategy.
- `export-backend-and-packaging.md`: video export, FFmpeg, settings, packaging,
  and license research checklist.
- `code-research-plan.md`: exact code areas and questions to inspect before
  turning this into an implementation contract.
- `open-questions.md`: questions for the user and code-research outcomes.
- `implementation-progress.md`: active implementation pass ledger. Read this
  before code edits and update it as implementation chunks and validation land.
- `implementation-agent-context.md`: compact handoff for a future
  implementation agent after research and decisions mature.

## Ground Rules

- Do not duplicate completed passes recorded in `implementation-progress.md`.
- Treat user-stated behavior as stronger than inferred behavior.
- Record answers in structured, implementation-ready language.
- Preserve alternate UX options until the user or code reality narrows them.
- When the user says a topic needs more research, put it in
  `research-backlog.md` before treating it as implementation guidance.
- Keep chain, timeline, keyframe, export, and packaging decisions separate
  enough that any one of them can change without rewriting the whole plan.
- If an existing motion-graphics doc should be merged or superseded, ask first.

## Core Intent

The first target is a bottom timeline window that can expand and contract below
the graph. It should push the graph editor and the right-side main viewport up
and down as its height changes. For now, the timeline should toggle open and
closed with `Ctrl+Shift+Tab`.

Timeline rows should represent connected graph chains or chain-derived objects.
The user should be able to scroll through rows, keyframe values from any node in
each chain, interpolate those values, choose frame rate and video export
settings, and export at the current main-canvas resolution.

Later work may add richer multi-object animation, curves, deeper Composite mode
support, video-backed slices, and more advanced export behavior.

## Current Implementation

Pass 1 adds the visible runtime timeline foundation only:

- timeline state is runtime-only
- the bottom panel lives inside `StackEditorWorkspace`
- timeline height reduces the graph and viewport pane height together
- rows are automatic and anchored to completed output chains
- row identity is the final output node ID
- no keyframes, playback, export, or persistence exists yet

Pass 2 adds the optional FFmpeg provider foundation only:

- Stack probes for an approved app-local provider at `tools\ffmpeg`
- release packaging looks for a local source provider at
  `_workspace\ffmpeg-provider`
- the provider is copied into release artifacts only when
  `ffmpeg-provider.json` passes manifest checks
- missing FFmpeg is a supported state and should not block app launch,
  validation, or release packaging
- the provider foundation does not invoke FFmpeg, download FFmpeg, expose video
  export UI, or link FFmpeg DLLs/libraries

Pass 3 adds the first runtime keyframe and animatable-parameter foundation:

- timeline animation data is runtime-only and not serialized
- parameter targets use node ID plus stable parameter ID
- the first complete covered node group is the split adjustment layer family:
  Brightness, Contrast, Saturation, Warmth, and Sharpen
- the timeline can choose a supported parameter for the selected output chain
  and add a keyframe at the current frame
- keyframe marks appear on every output row whose completed chain contains the
  keyed node, including shared upstream nodes
- keyframes still do not evaluate, interpolate into rendering, play back, or
  export

Pass 4 adds the first frame evaluation foundation:

- runtime tracks can sample deterministic float values at a requested frame
- exact keyframes, linear interpolation, hold interpolation, and range clamping
  are covered by `Stack::Timeline`
- `FrameEvaluationContext` stores temporary per-node/per-parameter values
- covered split adjustment parameters apply to copied render snapshot
  `layerJson`, not to live layer objects
- scrubbing a keyed timeline frame and adding a keyframe request a render
  refresh; affected composite output rows are marked dirty
- this pass does not add playback transport, project serialization,
  image-sequence/video export, curve editing, or broader node coverage

Pass 5 adds the first playback and frame-producer foundation:

- timeline state now tracks play/pause, loop playback, and frame accumulation
  at runtime only
- the timeline header exposes play/pause, stop, previous frame, next frame, and
  loop controls
- playback advances `currentFrame` using the configured FPS and requests render
  refreshes when frames change
- `BuildGraphSnapshotForTimelineFrame(...)` can build graph snapshots for frame
  N without moving the UI playhead
- `BuildSingleOutputTimelineFrameRaster(...)` is the first reusable
  single-output raster entry point for future still/image-sequence/video export
- this pass does not add timeline serialization, image-sequence export, FFmpeg
  invocation, export settings UI, or playback keyboard shortcuts

Pass 6 adds the first timeline persistence foundation:

- timeline data is written under `editorTimeline` inside the existing pipeline
  project payload
- schema version 1 stores duration frames, FPS, current frame, tracks, targets,
  keyframes, values, and interpolation
- load validates timeline targets against the loaded graph and current
  animatable registry, dropping missing or unsupported targets safely
- invalid frames are ignored or clamped into the loaded timeline duration
- transient UI/playback state remains runtime-only: open height, play/pause,
  loop state, and playback accumulator are not persisted
- this pass does not add image-sequence export, FFmpeg invocation, export
  settings UI, curve editing, auto-keyframing, or broader node coverage

Passes 7 through 16 add first timeline header, shortcut, realtime editing, and
additional node-coverage polish:

- the timeline control strip renders as a fixed header above the ruler and
  scrolling row list
- compact transport controls replace the longer play/stop/step text buttons
  and expose tooltips
- duration, FPS, current frame, and loop controls are available in a simple
  timeline settings popup
- the header keeps the current-frame control, target picker, and add-keyframe
  command compact
- `Q` and `E` step backward/forward one frame when the timeline is open and no
  item is active
- `Space` plays/pauses while the mouse is hovering the timeline, and active
  timeline playback is paused before `Space` can trigger existing viewport/split
  resize behavior elsewhere
- `Q`/`E` frame stepping wraps across the current playback range; exact
  visible-window wrap remains future timeline zoom/windowing work
- loop playback defaults on
- looping playback uses the last distinct keyframe frame when at least two
  distinct keyframe frames exist; zero/one-keyframe timelines use full duration
- registered split-adjustment layer parameter edits update an existing matching
  keyframe at the current playhead frame and refresh the preview; they do not
  create missing keyframes
- when editing a registered animated parameter away from a matching keyframe,
  Stack temporarily previews the live node value normally; timeline evaluation
  takes over again when the playhead moves or playback starts
- the blur/focus float slider group is now covered: Box Blur amount, Gaussian
  Blur amount, Hankel/Optical Blur radius, quality, and intensity, plus
  Tilt-Shift Blur strength, focus radius, focus falloff, focus X, and focus Y
- the numeric registry now supports integer metadata for slider-style
  parameters; stored keyframe values remain numeric, while frame evaluation
  rounds/clamps integer targets before writing render snapshot JSON
- the next non-RAW numeric sweep covers split corruption, split compression,
  split denoising, split edge effects, crop/rotate numeric transform controls,
  and heat/ripple distortion numeric controls
- the registry can target supported numeric array elements, currently used for
  Chromatic Aberration center X/Y without opening broad color/palette array
  keyframing
- another numeric sweep covers Bilateral Filter, Noise numeric sliders, split
  dither numeric sliders, HDR, Color Grade strength, Vignette numeric sliders,
  Chromatic Aberration numeric sliders and center coordinates, Lens
  Distortion, Glare Rays, Airy Bloom, Halftoning numeric sliders, Cell Shading
  numeric sliders, Image Breaks, Analog Video, Expander padding, and Palette
  Reconstructor blend/smoothing
- the scalar registry now supports typed bool and enum/combo values without a
  project schema bump; these values store numeric keyframe values but evaluate
  as hold/stepped targets and write typed JSON scalars
- the first typed scalar coverage includes Tilt-Shift Blur filter type, Flip
  horizontal/vertical, Bilateral Filter kernel/edge mode, Noise type/blend
  mode, split dither gamma/palette toggles, Chromatic Aberration falloff link,
  Halftoning pattern/color/bool controls, Cell Shading mode/bool controls, and
  Palette Reconstructor smoothing type
- Background Patcher now exposes its simple serialized scalar controls:
  removed-area opacity, color tolerance, edge smoothing, edge shift, defringe,
  keep-selected-range, and visualizer toggle
- the timeline row body now has a first hierarchical foundation: output-chain
  section rows, animatable node rows, and registered property rows
- chain and node rows can be expanded/collapsed at runtime; expansion state is
  intentionally transient for this pass
- actual keyframe diamonds render on property rows, while chain and node rows
  render dim summary marks for affected keyed frames
- clicking chain rows selects the output chain/object; clicking node/property
  rows selects the graph node and, for property rows, the parameter target
- these passes do not add full icon-font integration, keyframe dragging,
  keyframe color systems, per-row context menus, timeline zoom/windowing,
  persisted expansion state, color/palette keyframing, randomize/action
  keyframing, Background Patcher target-color/picker/mask/patch-state
  keyframing, additional typed controls outside the focused scalar sets,
  RAW/develop control coverage, external-model/action/cache denoise nodes,
  Tone Curve/View Transform scene workflow controls, FPS retiming dialogs,
  export UI, image-sequence export, FFmpeg invocation, or full all-node
  coverage

After Pass 16 validation, the remaining node-support work is mostly harder
edge cases and intentionally deferred areas: colors, palette arrays, action
buttons, Background Patcher target-color/picker/mask/patch state,
text/path/model values, curve/point editors, additional unregistered typed
scalar controls, RAW/develop and scene-workflow nodes, hidden/deprecated nodes,
and external-model/cache-heavy nodes. The remaining timeline-row work is now
interaction and polish on top of the hierarchy foundation: keyframe dragging,
row context menus, color/state differentiation, zoom/windowing, row density,
and persisted expansion behavior.
