# Timeline UX Keyframe Controls

- Captured: 2026-07-06 21:58
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-06-2158-timeline-ux-keyframe-controls.md`
- Type: idea
- Topic: motion-graphics
- Verification: partially-verified

This note captures a design-intake session for the timeline panel, keyframe
row structure, timeline settings, keyboard shortcuts, and future research
handling. It is not a single-pass implementation contract. Some items are
near-term polish, some are end-goal design direction, and some need research
before becoming implementation guidance.

## Near-Term Timeline Panel Polish

The row of buttons, drop-downs, and text at the top of the expandable timeline
panel needs a design cleanup pass.

Desired direction:

- The header/control strip should stay fixed while the timeline row list
  scrolls vertically.
- Vertical scrolling between output chains must not scroll the header away.
- Text in the header/control strip should not clip or truncate.
- Controls should be prepared for icon-first buttons where appropriate,
  especially playback and timeline actions.
- The panel should avoid unnecessary static text, explanatory text, and
  redundant notification text that consumes space without adding clarity.
- Icons should be paired with tooltips or accessible labels where the meaning
  is not already obvious from standard UI convention.

This looks like a good candidate for an early polish pass before deeper export
work, because it improves the existing visible timeline without requiring the
full future row model.

## End-Goal Timeline Row Structure

The current single-row-per-output-chain model is not enough for professional
keyframe editing once multiple nodes and parameters are animated.

Target direction:

- The timeline should be organized into chain sections.
- Each chain section represents one connected output chain from the graph.
- Inside each chain section, nodes in that chain should appear as separate
  horizontal rows.
- Keyframes for two different nodes should not be placed on the same
  horizontal timeline row.
- Selecting or expanding a node row should reveal separate child/property rows
  for that node's animatable values.
- Keyframes should ultimately live on the specific parameter/value row they
  animate, not only on the chain row.
- Keyframe marks need clear color differentiation so users can visually
  distinguish animated targets and states.

This implies a future hierarchy similar to:

```text
Chain section: Output / connected chain
  Node row: Brightness
    Property row: Brightness amount
  Node row: Contrast
    Property row: Contrast amount
  Node row: Sharpen
    Property row: Amount
    Property row: Threshold
```

The existing Pass 1 output-chain rows are still useful as the top-level chain
section anchor, but later passes should not keep adding unrelated node
keyframes to one flat chain row.

## Animatable Coverage Direction

The timeline should eventually support deeply integrated keyframing for
essentially any real adjustable node value.

Important constraints:

- Do not create animation-only duplicates of values that already exist on
  nodes.
- Continue growing support by node group.
- For a covered node group, expose all real adjustable values in that group.
- The timeline row hierarchy, keyframe marks, parameter registry, frame
  evaluation context, persistence, and export path all need to agree on the
  same target identity model.

The user specifically suggested that additional high-effort code research may
be useful here, including using multiple focused research agents when the time
comes.

## Timeline And Video Settings Window

Timeline/video settings should move toward a dedicated pop-up window that feels
similar to Stack's normal settings window.

Likely contents:

- timeline FPS
- output resolution
- current-canvas-resolution mode or future resolution override
- export format/container
- encoder/provider settings
- video quality or bitrate settings
- output path or export target settings
- future alpha/background handling

This should be treated as a project/timeline settings surface, not as a dense
collection of controls squeezed into the timeline header.

## Frame Rate Lock And Retiming

Once the user adds a keyframe, Stack should treat the current timeline frame
rate as important project timing data.

Desired direction:

- Adding the first keyframe should notify the user that the current timeline
  frame rate is becoming the timeline's committed frame rate.
- Changing the timeline FPS after keyframes exist should not be a casual text
  edit.
- A later settings flow could allow deliberate retiming, but it needs a clear
  dialog that explains the consequences.

Possible retiming modes to research:

- multiply the timeline FPS by a compatible factor and interpolate between
  existing keyframes
- change the FPS without interpolation by materializing additional keyframes so
  existing held values do not unexpectedly interpolate
- refuse unsupported FPS changes once keyframes exist
- allow export-time FPS changes without changing the timeline's internal frame
  grid

This needs deeper technical and UX research before implementation.

## Keyboard Shortcuts And Playback Defaults

The timeline should eventually support direct keyboard control.

Ideas to research:

- `Q` and `E` as global previous-frame and next-frame shortcuts.
- `Space` as timeline play/pause when the mouse is hovering the timeline.
- Preserve existing viewport resizing behavior for `Space` when the mouse is
  not hovering the timeline.
- Timeline playback should likely loop by default.
- Looping may need an option to loop to the last keyed frame instead of the
  full timeline duration.
- If there is only one keyframe, the playback/loop behavior should not stop in
  a way that feels broken or pointless.

Shortcut handling must be checked against existing Stack hotkeys before any of
these become implementation requirements.

## Visual Density And Responsiveness

The timeline needs careful visual design because large graphs can create many
chains, nodes, and property rows in a small vertical space.

Design expectations:

- row spacing should be compact but readable
- chain, node, and property hierarchy should be visually separable
- expansion/collapse should feel smooth and predictable
- keyframe marks should remain readable at small sizes
- color should help differentiate keyframes without becoming the only signal
- the UI should stay responsive on extremely large graphs
- text should not overlap, clip, or consume space that should belong to time
  editing

This is a major UI design problem, not a small styling tweak.

## Documentation Handling Rule

When the user says a topic needs more research, future agents should preserve
it in a research backlog or research-oriented doc instead of turning it into
immediate implementation guidance.

Research-needed items should keep enough of the user's original detail that a
future research pass can investigate pros, cons, code constraints, and design
tradeoffs without rediscovering the intent from scratch.

## Related Docs

- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/research-backlog.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/keyframing-ux-options.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/open-questions.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/technical-decisions.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/timeline-video-export-foundation/implementation-agent-context.md`
