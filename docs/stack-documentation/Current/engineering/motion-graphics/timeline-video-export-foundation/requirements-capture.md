# Requirements Capture

- Captured: 2026-07-06 00:08
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-06-0008-timeline-video-export-foundation.md`
- Type: idea/update
- Verification: partially-verified against existing docs, not yet verified
  against current code

## User-Stated Requirements

- Begin documentation foundations for adding video export to Stack.
- Do not implement code in this pass.
- Create a new documentation implementation-planning folder inside the
  documentation home.
- Use the new folder as the workspace for exact implementation intent before
  code changes begin.
- Build around a timeline system that appears below the graph.
- Let the timeline expand and contract on the bottom of the program screen.
- When the timeline expands or contracts, it should push the graph editor and
  right-side main viewport up and down.
- For now, toggle the timeline open and closed with `Ctrl+Shift+Tab`.
- Represent connected graph chains as horizontal timeline rows or row-like
  objects.
- Allow vertical scrolling through the chains that exist on the graph.
- Let the user keyframe any changeable value on any node in any chain.
- Show keyframe marks in the timeline.
- Support interpolation between keyframes.
- Let the user set frame rate and video export settings.
- Export video at the current resolution of the main canvas.
- Design with later support for many animated objects, curve editing, and better
  Composite mode support.
- Keep the timeline header/control strip fixed while timeline rows scroll.
- Avoid clipped/truncated header text and unnecessary explanatory text in the
  dense timeline panel.
- Move toward icon-first timeline controls where appropriate.
- Evolve the timeline from flat output rows toward chain sections, node rows,
  and property/value rows.
- Do not place keyframes for different nodes on the same final horizontal row
  in the end-goal keyframe editor.
- Move timeline/video settings toward a dedicated popup similar to Stack's
  normal settings window.
- Treat timeline FPS changes after keyframes exist as a deliberate retiming
  problem, not a casual text edit.
- Include FFmpeg setup, packaging, license, and distribution concerns in the
  documentation plan.
- Include video export settings panels and UI consistency requirements in the
  documentation plan.

## Connected Chain Definition From The User

A connected chain is a full path that starts with an input somewhere, continues
through whatever nodes are connected after that, and reaches an output of some
sort.

Multiple chain cases include:

- multiple independent full chains
- one chain that connects to two separate outputs at the end
- one chain that may split somewhere in the middle and reach multiple outputs

The user wants these cases to be visible as separate timeline objects where
appropriate.

## Explicit Non-Assumptions

These are not decided yet:

- whether the final model is a global graph with chain tracks, per-object
  graphs, or a hybrid
- whether a timeline row binds to a full chain, terminal output, canvas object,
  graph output, graph node parameter, or another stable target
- whether keyframe creation starts from node UI, timeline right-click UI,
  inspector UI, a side panel, or a hybrid flow
- whether auto-keyframing is always manual, opt-in with an armed mode, or
  context-sensitive
- whether FFmpeg is bundled, discovered externally, downloaded by a tool, or
  optional
- which FFmpeg build, codec set, or license configuration is acceptable for
  Stack distribution
- which video export formats are first-class in the initial implementation
- how video-backed slices relate to still-image slices

## Design Constraints

- The implementation should be easy to update and extend.
- The docs should preserve changing user ideas without pretending everything is
  already final.
- The eventual code should not hard-code a narrow one-off timeline model that
  blocks later curve editing, multiple object animation, or deeper Composite
  integration.
- UI should match Stack's existing design language and interaction patterns.
- Implementation agents should receive structured context with current-code
  findings before they write code.
- When the user marks an idea as needing research, preserve it in a research
  backlog before turning it into implementation guidance.

## Existing Documentation Context

This folder sits under the existing Motion Graphics workstream, which already
tracks timeline, keyframe, video-slice, and video export planning:

- `docs/stack-documentation/Current/engineering/motion-graphics/README.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/implementation-phases.md`
- `docs/stack-documentation/Info/engineering/composite/2026-07-03-timeline-keyframe-and-video-export-research.md`
- `docs/stack-documentation/Info/engineering/composite/CompositeAnimationSpecs.md`
- `docs/stack-documentation/Current/engineering/architecture/UNIFIED_WORKSPACE_ARCHITECTURE.md`

This folder does not replace those documents. It narrows the next planning pass
around timeline row ownership, keyframe UX, and export foundations.
