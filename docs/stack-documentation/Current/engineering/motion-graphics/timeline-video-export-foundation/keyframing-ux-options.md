# Keyframing UX Options

This document preserves the first keyframe interaction ideas without choosing a
final design too early.

Latest design-intake source:

- `docs/stack-documentation/Archived/source-notes/2026/2026-07-06-2158-timeline-ux-keyframe-controls.md`

## UX Problem

The user wants to animate or auto-keyframe almost any changeable value in a
full connected chain. The timeline should show marks for those keyframes. The
open question is where the user should discover, create, and edit those
keyframes visually.

The end-goal documentation should not frame support as arbitrary `v1`, `v2`,
and `v3` cuts. Implementation can happen in passes, but the product model is
that Stack can eventually keyframe the real adjustable values on supported node
groups.

## Option A - Node-Local Keyframe Controls

Add keyframe controls near animatable values where the value already lives,
such as inside a node, node-expanded control surface, or existing inspector.

Potential strengths:

- The user sees the keyframe affordance next to the value being animated.
- Parameter editing stays close to existing node behavior.
- Auto-keyframing can be scoped to values the user is actively changing.

Potential risks:

- Dense nodes may become visually noisy.
- It may be hard to see all animated values across a full chain.
- Values that live outside nodes, such as canvas transforms, may need a parallel
  interaction pattern.

Research needed:

- Which existing node controls are compact enough to host keyframe buttons?
- Is there already an expanded node/inspector pattern that can carry animation
  controls cleanly?
- Are node parameters stored in a uniform enough way to generate keyframe
  controls without hand-writing every node?

## Option B - Timeline Right-Click Add Keyframe Flow

Let the user right-click a frame position in the timeline and choose
`Add Keyframe`. That action could open a chain/node/value picker.

One proposed shape:

- right-click a specific frame or timeline spot
- choose `Add Keyframe`
- open an expanding side panel or picker
- list all current full chains first
- after selecting a chain, show the nodes in that chain
- allow choosing a node and one of its values
- place the keyframe mark on the timeline row at the clicked frame

Potential strengths:

- The timeline remains the clear place where time-based editing begins.
- Chain selection can teach users the chain model.
- It supports keyframing values without hunting through a large graph.

Potential risks:

- A deep picker may be slow if users need to keyframe many values.
- The picker depends on good chain naming and stable chain identity.
- It could duplicate node inspector behavior if not carefully scoped.

Research needed:

- Where can a bottom-panel context menu safely interact with graph focus and
  viewport focus?
- Does Stack already have left-side expanding panels or picker panels that this
  should match?
- How many nodes and parameters might appear in real projects, and does the
  picker need search/filtering from the first pass?

## Option C - Hybrid Timeline And Inspector Flow

Use the timeline for time, rows, keyframe marks, selection, and scrubbing, while
using node or inspector surfaces for value-specific editing.

Possible first version:

- Selecting a timeline row selects or highlights its connected chain.
- Selecting a graph node exposes animatable parameters in the existing
  parameter surface.
- Each animatable parameter has an add-keyframe control.
- Right-clicking the timeline can add a keyframe for the currently selected
  parameter or open the chain/node/value picker when no target is selected.
- Auto-keyframing only affects a selected or explicitly armed target.

Potential strengths:

- It keeps broad navigation in the timeline and precise value editing near the
  value.
- It can grow toward curves and property subtracks later.
- It avoids making the first implementation depend on one perfect panel.

Potential risks:

- Selection rules must be very clear.
- Auto-keyframing can surprise users if the armed state is too subtle.
- The first implementation still needs a reliable parameter registry.

## Auto-Keyframing Questions

Open questions:

- Should auto-keyframing exist in the first implementation, or should all
  keyframes be manual at first?
- If auto-keyframing exists, should it be disabled by default?
- Should changing a value at a nonzero frame always create a keyframe, only
  when auto-key is armed, or only when the value already has at least one
  keyframe?
- Should editing frame `0` change the base value instead of creating a
  keyframe?
- Should Stack show an obvious timeline-level auto-key toggle?
- Should auto-key apply to all animatable values, selected chain only, selected
  node only, or selected parameter only?

## Animatable Parameter Coverage Direction

The user-confirmed direction is node-group-complete coverage.

For a supported node group, Stack should expose the actual adjustable sliders
and values already used by that node group as animatable parameters. The
timeline should keyframe and interpolate those real values, not a separate set
of arbitrary animation-only controls.

Implementation can progress by node group, but each covered group should be
internally complete:

- identify every real adjustable value in the node group
- give each value a stable parameter identity
- define its type, display name, default, range, and interpolation behavior
- connect timeline keyframes to the same value users edit in the node UI or
  inspector UI
- document unsupported values in the group only when there is a real technical
  reason

This strongly points toward an animatable parameter registry or equivalent
metadata layer.

Pass 3 code answer:

- The first registry-backed coverage group is the split adjustment layer family:
  Brightness, Contrast, Saturation, Warmth, and Sharpen.
- Stable parameter IDs are namespaced, for example `layer.brightness`, and are
  separate from current serialized storage keys such as `brightness`.
- Sharpen is treated as group-complete for this family by exposing both
  `layer.sharpening` and `layer.sharpenThreshold`.
- This pass intentionally does not add arbitrary one-off animation targets.

Pass 3 first keyframe flow:

- Select an output-chain row in the timeline.
- The timeline header exposes supported animatable parameters found in that
  completed chain.
- `+ Key` stores or replaces a runtime keyframe for the selected parameter at
  the current frame.
- The mark appears on every row whose completed chain contains the target node.

Pass 4 evaluation status:

- Stored keyframes for the covered split adjustment parameters now evaluate at
  the current frame.
- Scrubbing a keyed frame can request a render refresh using sampled values.
- The sampled value is applied to render snapshot JSON only; the live node UI
  value is not overwritten by frame evaluation.
- There is still no playback transport, curve editor, persistence, or
  auto-keyframing.

Pass 5 playback status:

- The first playback transport now lives in the timeline header.
- It exposes play/pause, stop, previous frame, next frame, and loop.
- Playback advances the current frame by the timeline FPS and uses the existing
  frame evaluation/render refresh path.
- Keyboard shortcuts, richer transport styling, curve editing, and persisted
  timeline settings are still future design work.

Pass 6 persistence status:

- Manual keyframes for the currently covered split adjustment layer parameters
  now persist in project files.
- The timeline stores keyframe targets by node ID plus stable parameter ID.
- Loading validates saved keyframes against the current graph and animatable
  registry, so deleted nodes or unsupported parameters are dropped safely.
- Auto-keyframing, curve handles, richer keyframe editing, and broad
  node-group coverage are still future design work.

Pass 7 timeline control status:

- The top timeline controls now live in a fixed header area above the scrolling
  row list.
- Transport controls use compact symbolic buttons with tooltips.
- Duration and FPS moved into a simple timeline settings popup instead of
  staying in the dense header row.
- `Q`/`E` frame stepping and timeline-hover `Space` play/pause are implemented
  with input guards.
- Loop playback defaults on and loops to the last distinct keyframe frame when
  there are at least two distinct keyed frames.
- Chain/node/property row hierarchy, keyframe color system, FPS retiming
  dialogs, and full icon-font integration remain future design work.

Pass 9 realtime edit foundation:

- The timeline now updates an existing matching keyframe when the user edits a
  registered split-adjustment layer parameter while the playhead is on that
  keyframe and the timeline is open.
- The preview is refreshed after the keyframe value changes so the downstream
  chain can update from the new keyed value.
- If the playhead is not on a matching keyframe, editing should not silently
  create or save a new keyframe.
- Pass 10 adds the preview side of this behavior: off-keyframe edits to
  registered animated parameters temporarily bypass that parameter's timeline
  override so the user can see the normal live node edit.
- The temporary live preview is cleared when the playhead moves, playback
  starts, project timeline state resets, or the user writes the value with
  `+ Key`; after that, timeline evaluation controls the value again.
- This should be treated separately from broad auto-keyframing, which may
  create new keyframes under an explicit auto-key mode.
- Pass 11 broadens the same registry, keyframe, persistence, frame-evaluation,
  existing-keyframe update, and off-keyframe live preview path to the first
  blur/focus float-slider group: Box Blur amount, Gaussian Blur amount,
  Hankel/Optical Blur radius, quality, and intensity, plus Tilt-Shift Blur
  strength, focus radius, focus falloff, focus X, and focus Y.
- Discrete/enum values in that area, such as Tilt-Shift Blur filter type, are
  not covered yet and should wait for a non-float or stepped-keyframe design.
- Pass 12 broadens coverage from float-only parameters to numeric parameters
  with float and integer metadata. Integer sliders still store numeric
  keyframe values, but frame evaluation rounds and clamps them before writing
  render snapshot JSON.
- Pass 12 covers additional non-RAW numeric node controls: split corruption,
  split compression, split denoising, split edge effects, crop/rotate numeric
  transform controls, and heat/ripple distortion numeric controls.
- Pass 13 adds registered numeric array-element support for explicit numeric
  targets such as Chromatic Aberration center X/Y. This is not broad
  color/palette array keyframing.
- Pass 13 covers another large non-RAW numeric sweep: Bilateral Filter, Noise
  numeric sliders, split dither numeric sliders, HDR, Color Grade strength,
  Vignette, Chromatic Aberration, Lens Distortion, Glare Rays, Airy Bloom,
  Halftoning, Cell Shading, Image Breaks, Analog Video, Expander padding, and
  Palette Reconstructor blend/smoothing.
- Pass 14 adds typed bool and enum/combo scalar support. These targets continue
  to store numeric keyframe values for schema compatibility, but frame
  evaluation treats them as hold/stepped values and writes typed JSON booleans
  or rounded/clamped integer options.
- Pass 14 covers focused non-RAW typed scalar controls: Tilt-Shift Blur filter
  type, Flip horizontal/vertical, Bilateral Filter kernel/edge mode, Noise
  type/blend mode, split dither gamma/palette toggles, Chromatic Aberration
  falloff link, Halftoning pattern/color/bool controls, Cell Shading mode/bool
  controls, and Palette Reconstructor smoothing type.
- Pass 15 covers Background Patcher simple serialized scalar controls:
  removed-area opacity, color tolerance, edge smoothing, edge shift, defringe,
  keep-selected range, and visualizer state.
- RAW/develop controls, colors, palette banks, randomize/action buttons, text,
  file/path values, model/provider selectors, point/curve editors, hidden or
  deprecated nodes, scene-workflow nodes, external-model/cache-heavy nodes, and
  non-scalar node-specific state remain outside the current parameter model.
- Remaining work: add property/value rows, define undo/redo grouping, and
  decide the eventual auto-key workflow.

## Direct Keyframe Manipulation

Future keyframe marks should support direct manipulation.

Direction from 2026-07-06 follow-up:

- keyframes should be draggable
- dragging should move the keyframe to a new frame
- dragging should preserve target, value, and interpolation unless another
  explicit edit changes those fields
- drag behavior needs research for snapping, collisions, shared upstream
  keyframes, summary rows, and future property/value rows

## Keyframe Mark Requirements

Each visible keyframe mark will eventually need to answer:

- which frame it is on
- which row or chain it belongs to
- which node/object/parameter target it drives
- whether it is selected
- whether it has interpolation/easing data
- whether it conflicts with a deleted or missing target

The first UI can be simple, but the data model should leave room for property
subtracks and curve editing later.

## Hierarchical Row Direction

The end-goal timeline should not keep unrelated node keyframes on one flat
chain row.

Current direction from the 2026-07-06 design intake:

- top-level sections represent connected output chains
- each chain section contains separate rows for nodes in that chain
- each node row can expand to reveal separate property/value rows
- keyframes for two different nodes should not occupy the same horizontal row
- keyframes should ultimately live on the property/value row they animate
- chain rows can still show summary indicators for affected downstream outputs

Illustrative hierarchy:

```text
Chain: Output A
  Node: Brightness
    Value: Brightness
  Node: Contrast
    Value: Contrast
  Node: Sharpen
    Value: Amount
    Value: Threshold
```

This does not invalidate the Pass 1 row anchor. Output nodes remain the
top-level row/section anchor, but the visible timeline needs deeper row
structure before broad multi-node animation becomes user-facing.

Pass 16 implemented the first version of this hierarchy:

- completed output chains render as expandable top-level section rows
- registered animatable nodes render under expanded chain sections
- registered parameter/property rows render under expanded node rows
- exact keyframe diamonds render on property rows
- chain and node rows render summary marks only
- expansion state is runtime-only; persistence, row virtualization, context
  menus, dragging, and final visual-density polish remain future work

## Keyframe Visual Differentiation

Keyframes need to be visually distinguishable beyond their frame position.

Design direction:

- keyframes should be differentiable by color
- keyframe color may need to communicate target, state, or interpolation class
- selected, hovered, disabled, missing-target, and conflicting keyframes may
  need separate visual states
- color should help recognition, but the final design should not rely only on
  color if other states matter

This requires design and code research before becoming a final color system.

## Timeline Header Direction

The top timeline control strip should be a fixed header while timeline rows
scroll vertically.

Direction:

- header controls stay visible when scrolling output chains, nodes, and
  property rows
- the header must avoid clipped or truncated text
- many text buttons should move toward icon buttons with tooltips
- video/timeline settings should move into a dedicated settings popup instead
  of expanding the header
- the timeline should avoid redundant explanatory text that repeats what icons
  or control state already communicate

## Current Leaning

The best candidate to research first remains a hybrid flow:

- timeline rows and marks make time visible
- node/inspector controls make parameter targeting understandable
- timeline right-click can open a chain/node/value picker as a secondary path

This should be tested against the current code before becoming an
implementation requirement.
