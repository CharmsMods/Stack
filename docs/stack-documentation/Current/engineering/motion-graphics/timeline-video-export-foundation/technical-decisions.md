# Technical Decisions

- Captured: 2026-07-06 00:38
- Updated: 2026-07-06 Pass 6
- Sources:
  - `docs/stack-documentation/Archived/source-notes/2026/2026-07-06-0038-timeline-technical-answers.md`
  - `docs/stack-documentation/Archived/source-notes/2026/2026-07-06-1226-frame-export-ffmpeg-confirmation.md`
  - `docs/stack-documentation/Archived/source-notes/2026/2026-07-06-2158-timeline-ux-keyframe-controls.md`
- Verification: user-confirmed product direction, partly researched against
  official FFmpeg docs and local Stack dependency/release patterns, not yet
  verified against timeline/render code

This file records decisions that are settled enough to guide code research.
They may still be refined by current-code findings, but future agents should
not reopen them casually.

## Decision 1 - Timeline Rows Anchor To Final Output Nodes

Timeline rows should be anchored internally to final output nodes.

The final output node gets its rendered result from the connected chain that
feeds it. The timeline row therefore represents an output object chain: a final
output plus the upstream graph region that defines that output.

Implementation implication:

- output node identity must be stable across save/load
- each output node should be able to resolve its upstream connected chain
- timeline row metadata should use output-node identity as its primary anchor
- chain names may be generated from output nodes, but display names must not be
  the durable identity

## Decision 2 - Shared Upstream Keyframes Affect All Downstream Outputs

If a node is shared upstream of multiple outputs, keyframing that node changes
the data flowing into every downstream branch that depends on it.

Example:

- image input
- brightness node
- branch one continues to contrast, then output A
- branch two continues to Gaussian blur, then output B

If the brightness node is animated, both output A and output B should change
because both branches receive animated brightness data before their separate
branch-specific processing.

Implementation implication:

- keyframes belong to actual node parameters, not to a copied row-local value
- a keyframed shared node may need visible indicators on more than one output
  row
- the graph dependency model should decide affected outputs by downstream
  traversal, not by timeline-row ownership alone

## Decision 3 - Timeline Rows Are Created Automatically From Graph Structure

Stack should automatically create timeline rows per output object chain from
the graph structure.

Users should not have to manually create timeline rows just to see or animate
the graph's output chains.

Implementation implication:

- the timeline needs an automatic row-discovery pass
- manual row creation is not the base model
- the UI can still allow naming, filtering, collapsing, or hiding rows later,
  but row existence starts from graph outputs
- disconnected graph islands without output nodes should not become primary
  rows unless a later design explicitly supports that

## Decision 4 - The Documentation Describes The End Goal

The planning language should avoid framing this as separate `v1`, `v2`, and
`v3` feature sets.

Implementation may still happen in passes, but the documentation should define
the end goal and then describe implementation sequencing as coverage progress
toward that goal.

Implementation implication:

- avoid arbitrary partial behavior that makes the architecture hard to extend
- avoid documenting temporary limitations as the product model
- use "implementation pass" or "node-group coverage pass" when staging is
  necessary

## Decision 5 - Animatable Coverage Should Be Node-Group Complete

The end goal is to keyframe almost anything that is an actual adjustable value
in the graph.

Coverage should grow by node group, not by arbitrary individual sliders. For
each supported node group, all real adjustable sliders or values in that group
should be addressable, keyframeable, and interpolated through the timeline.

Implementation implication:

- do not wire random one-off adjustable values into the timeline
- define an animatable parameter registry or equivalent metadata layer
- each node group needs a coverage checklist
- keyframed values should be the same adjustable values users already edit in
  node UI or inspector UI
- implementation can progress group by group, but each group should feel
  internally complete

Pass 3 implementation note:

- the first node-group-complete coverage is the split adjustment layer family:
  Brightness, Contrast, Saturation, Warmth, and Sharpen
- coverage is explicit registry metadata in `Stack::Timeline`, not automatic
  scraping of ImGui widgets
- target identity is `nodeId + stable parameterId`
- runtime keyframes are not persisted until graph-node identity repair rules
  are designed

## Decision 6 - Frame Evaluation Uses A Stable Evaluation Context

Playback and export should evaluate animated values through a first-class
`FrameEvaluationContext` or equivalent evaluation context.

The evaluation context computes temporary frame-specific values for frame `N`
and passes those values to rendering without overwriting the live graph state.
This should be implemented as a reliable render/evaluation contract, not as a
fragile patch over normal node settings.

Implementation implication:

- the live graph stores base values and keyframes
- frame evaluation computes interpolated overrides for the requested frame
- render/export reads through the frame context when animated values are active
- playback/export should not dirty undo/redo or persist frame-specific values
- if code research proves this cannot be made reliable immediately, the
  fallback to research is rendering from a graph snapshot/clone, not mutating
  and restoring the live graph

Pass 4 implementation note:

- the first code path samples runtime keyframe tracks into
  `Stack::Timeline::FrameEvaluationContext`
- covered split adjustment parameters are applied to copied render snapshot
  `RenderGraphNode::layerJson` in `BuildGraphSnapshot()`
- live `LayerBase` objects and graph node settings are not overwritten by
  frame evaluation
- this is currently runtime-only and covers the split adjustment layer group;
  playback/export loops and project serialization still need later passes

## Decision 7 - Export Uses A Frame Producer With Replaceable Sinks

Stack should have one reusable frame producer that can evaluate and render frame
`N`, then pass that frame to different export sinks.

Required sink directions:

- still-frame export
- image-sequence export
- app-local external FFmpeg executable export
- future encoder/library export only if later licensing and build complexity
  justify it

Image-sequence export should remain a real supported fallback/debug export path,
not just an internal temporary mechanism.

Implementation implication:

- video export should not be the only consumer of frame-at-time rendering
- FFmpeg should not own the render loop
- still export, sequence export, and video export should share the same
  frame-evaluation contract
- export error handling should distinguish render failure from encoder failure

Pass 5 implementation note:

- `TimelinePlayback.*` handles runtime FPS-based playhead advancement
- `TimelineFrameProducer.*` normalizes frame requests and builds frame
  evaluations
- `BuildGraphSnapshotForTimelineFrame(...)` allows frame N graph snapshots
  without moving the UI playhead
- `BuildSingleOutputTimelineFrameRaster(...)` is the first reusable
  single-output frame raster entry point
- export loops and sinks are still future work; this pass only creates the
  foundation they should reuse

## Decision 8 - FFmpeg Uses An App-Local Approved External Executable

The normal user experience should not require system-wide FFmpeg installation
or manual user configuration.

The preferred direction is an app-local approved external `ffmpeg.exe` packaged
with Stack release artifacts after the exact build, license state, source
availability, configure flags, and notices are reviewed.

Provider order:

1. Use Stack's packaged approved `ffmpeg.exe`.
2. If missing, allow a user-configured `ffmpeg.exe` path.
3. Optionally discover `ffmpeg.exe` from `PATH`.
4. If no encoder is available, disable direct video encoding but keep
   image-sequence export available.

FFmpeg should not be integrated as linked/loaded FFmpeg DLLs for the initial
architecture. Calling an app-local executable as a separate process keeps the
encoder boundary simpler and closer to Stack's existing optional external
runtime pattern while avoiding a hard system dependency.

Implementation implication:

- design an encoder-provider boundary around an executable process provider
- do not link against FFmpeg libraries in the planned implementation contract
- package FFmpeg only from an approved build manifest
- release packaging must copy the executable, matching license/source/notice
  materials, and any required build metadata
- `stack-tools.cmd` may later help install or refresh the approved build, but
  the normal packaged app should be able to run with its app-local encoder
  already present
- user-selected and `PATH` FFmpeg are fallback providers, not the primary
  product experience

Pass 2 implementation note:

- runtime discovery probes `<Stack executable directory>\tools\ffmpeg`
- release packaging copies `_workspace\ffmpeg-provider` into packaged
  `tools\ffmpeg` only after `ffmpeg-provider.json` passes checks
- missing FFmpeg is supported and should not block Stack launch or release
  packaging
- present-but-unapproved or unsafe provider manifests fail validation and
  packaging

## Decision 9 - Timeline Project Data Uses A Schema-Versioned Payload

Timeline project data should be saved as editor-owned project metadata under a
clear schema version, separate from transient timeline UI state.

Implementation implication:

- timeline save/load should not be mixed into transient window layout state
- saved targets should use stable node/parameter identities
- load should validate targets after the graph and layers are available
- unsupported or missing targets should be dropped safely rather than blocking
  project load
- future schema changes should be gated by explicit version handling
- playback state, panel open/closed state, and resize height should remain
  runtime-only unless a later settings decision moves them to app settings

Pass 6 implementation note:

- project save/load now writes `editorTimeline` schema version 1 in the
  existing pipeline payload
- schema version 1 stores duration frames, FPS, current frame, tracks, targets,
  keyframes, values, and interpolation
- deserialization validates targets against the loaded graph and current
  animatable registry
- invalid targets and negative keyframes are dropped; out-of-range frames are
  clamped into the loaded duration
- unsupported future schemas are ignored and repaired back to default timeline
  state
- graph-node identity repair beyond current node IDs and copy/paste remapping
  still need future design

## Decision 10 - Timeline Header Stays Separate From Row Scrolling

The timeline's top control strip should remain visible while the user scrolls
vertically through timeline rows.

Implementation implication:

- the timeline header/control strip should not live inside the vertically
  scrolling row child region
- controls should avoid clipped or truncated labels
- future header controls should prefer compact icon buttons where standard
  icons exist
- icon-only controls need tooltips or equivalent accessible labels
- explanatory text that repeats obvious control meaning should be removed or
  moved out of the dense timeline surface
- timeline/video export settings should move toward a dedicated settings popup
  instead of crowding the header

Pass 7 implementation note:

- timeline controls now render in a fixed header child above the ruler and
  scrolling row list
- transport controls use compact symbolic buttons with tooltips
- duration and FPS moved into a simple timeline settings popup
- full icon-font integration and final visual polish remain future work

## Decision 11 - Broad Keyframing Requires Chain, Node, And Property Rows

The flat Pass 1 row model is a foundation, not the final keyframe layout.
Broad multi-node animation should use a hierarchy of chain sections, node rows,
and parameter/property rows.

Implementation implication:

- completed output chains remain the top-level anchor
- each visible chain section can contain the nodes that participate in that
  chain
- each node row can expand to reveal separate rows for animatable values
- keyframes for different nodes should not share one horizontal row
- keyframes should ultimately be placed on the specific property/value row they
  animate
- chain-level rows may still show summary indicators for animated descendants
  or shared upstream keyframes
- the timeline row model, keyframe data model, target registry, persistence,
  and frame evaluation context must continue to agree on stable target
  identity

## Decision 12 - Timeline Settings Belong In A Dedicated Project Popup

Timeline and video settings should move toward a dedicated project/timeline
settings popup similar in feel to Stack's normal settings window.

Implementation implication:

- resolution, timeline FPS, video format, encoder settings, output quality,
  and related export options should not all be squeezed into the timeline
  header
- the popup needs to distinguish project-persistent timeline settings from
  export-job-only settings and app preferences
- the existing `editorTimeline` payload may need future schema additions for
  project-persistent timeline settings
- FFmpeg/provider settings must continue respecting the optional provider and
  packaging policy

Pass 7 implementation note:

- the first timeline settings popup contains current frame, duration frames,
  FPS, and loop playback
- the popup is runtime UI only beyond the already-persisted timeline settings
  from Pass 6
- video encoding settings and export settings are still future work

## Remaining Code-Research Validations

The following topics no longer need broad product direction, but still require
current-code research before implementation:

- what additional node groups need frame-context read/apply support after the
  split adjustment layer group
- whether any future node type requires a graph snapshot/clone fallback instead
  of direct render snapshot payload overrides
- what output buffer format the frame producer should emit
- which exact FFmpeg build can be approved for app-local packaging
- how release scripts should package the encoder and its notices
- how keyframed timeline tracks should be repaired or remapped when graph nodes
  are duplicated, pasted, renamed, or migrated by future schema changes
- how to evolve the visible timeline from flat output rows to chain/node/value
  hierarchy without breaking the existing output-node anchors
- how timeline FPS locking and retiming should work after keyframes exist
- how timeline keyboard shortcuts should interact with existing graph,
  viewport, and text-entry shortcuts
