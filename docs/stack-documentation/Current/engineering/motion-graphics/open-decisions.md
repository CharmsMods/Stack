# Open Decisions

This file tracks decisions that need research or user confirmation before
implementation starts.

## Architecture

### Global Graph Versus Per-Object Graphs

Question: should Stack use one global graph with `Canvas Object` nodes, or
should each canvas object own its own appearance graph?

Current context:

- `UNIFIED_WORKSPACE_ARCHITECTURE.md` recommends one global graph with Canvas
  Object nodes.
- The routed source note still preserves a competing thought: a given object may
  own the nodes that make up how it looks, while canvas placement owns
  transforms and animation.

Needed:

- Inspect current Editor graph and Composite module code before deciding.
- Define how a timeline targets graph-owned properties versus canvas-owned
  transforms.

### Compositing Workflow Versus Free Canvas Workflow

Question: should layered compositing and free-layout canvas work be one mode
with strong defaults, or separate workflows?

Needed:

- Define default behavior when multiple imported slices have the same
  dimensions or aspect ratio.
- Decide whether same-sized imports should auto-stack, remain separate canvas
  objects, or prompt the user.

### Slice Object Model

Question: do still-image slices and video slices share one base object model, or
does video require a separate media-backed type?

Needed:

- Inspect import paths, project persistence, thumbnail generation, and
  Composite/Editor bridge behavior.
- Decide what metadata every slice must carry.

## Timeline And Timebase

### Canonical Time Unit

Question: is the canonical timeline unit always integer frames?

Current preference:

- Use frame-first timing.
- Display and accept seconds as a convenience.
- Convert seconds to frames using the project frame rate and round to the
  nearest valid frame.

Needed:

- Define frame-rate ownership in project settings.
- Define behavior when frame rate changes after keyframes exist.

### Track Ownership

Question: what does a timeline track bind to?

Possibilities:

- Connected chain or terminal chain output
- Slice
- Canvas object
- Graph output
- Graph node parameter
- Layer/object group

Needed:

- Define the first supported track target.
- Avoid designing every future target before the first path is proven.
- Inspect whether current graph outputs are stable enough to anchor timeline
  rows, especially when one input chain splits to multiple outputs.
- Decide how user-renamed chains should relate to graph nodes, outputs, canvas
  objects, or timeline-only metadata.

### Keyframe Inspector

Question: should keyframe property editing appear as a right-click popup,
dockable inspector, side panel, or timeline drawer?

Needed:

- Compare with existing node double-click expansion, Editor inspector panels,
  and graph context menu rules.
- Preserve ImGui input ownership rules from the node graph interaction guide.
- Compare node-local keyframe controls, timeline right-click `Add Keyframe`,
  and a hybrid timeline-plus-inspector flow.
- Define whether auto-keyframing exists in the first implementation and what
  target scope it affects.

## Rendering And Export

### Per-Frame Evaluation Contract

Question: what is the minimal contract for evaluating a project at frame `N`?

Current direction:

- Use a reliable frame evaluation context that computes temporary frame values
  without mutating the live graph.
- If current code cannot support that reliably, research graph snapshot/clone
  rendering as the fallback.

Needed:

- Define how animated canvas transforms are applied.
- Define how video slice frame selection works.
- Inspect where graph/render evaluation can receive frame/time context.

### Video Export Backend

Question: should first video export use FFmpeg piping, image sequence export, or
both?

Current direction:

- Use a frame producer with replaceable export sinks.
- Keep image-sequence export as a supported fallback/debug path.
- Use an approved app-local external `ffmpeg.exe` as the normal packaged video
  encoder provider.
- Do not link FFmpeg DLLs/libraries in the planned implementation contract.
- User-configured and `PATH` FFmpeg providers are fallbacks, not the primary
  product experience.

Needed:

- Verify dependency expectations for Windows builds.
- Define failure behavior when no approved app-local or fallback FFmpeg
  provider is available.
- Define blocking versus background export behavior.
- Select and verify the exact FFmpeg build, configure flags, notices, source
  availability, and distribution model before release.

## Research Needs

- Current code map for Composite layer rendering and export.
- Current code map for Editor graph image imports and graph snapshots.
- Persistence schema implications for slices, tracks, keyframes, and project
  frame-rate settings.
- Timeline UI patterns that fit Dear ImGui without breaking existing graph and
  panel interactions.
- Practical video decode strategy for preview, scrubbing, and export.
