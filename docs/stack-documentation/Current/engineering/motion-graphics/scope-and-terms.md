# Scope And Terms

This document defines the early vocabulary and scope for the Motion Graphics
workstream.

## Working Definition

Motion graphics in Stack means time-based composition of still images, video
inputs, generated visuals, text, shapes, and graph-processed outputs. It is not
just video export, and it is not just UI animation. It is a project-level
workflow where visual objects can change over time and be rendered as still
frames or video.

## Core Terms

`Slice`
: A media-backed visual source that can represent a still image, video clip, or
future generated media item. The main editor and graph import surfaces already
use `slice` and `Import Slice` wording for current still-image flows. Video
slice behavior and the broader shared slice data model remain open work.

`Video slice`
: A slice whose source changes over time. A video slice needs decode, frame
selection, timing, caching, and export behavior that still-image slices do not.

`Canvas object`
: A spatial object placed on the canvas. It may point at a slice, graph output,
generated texture, text, shape, or embedded project result.

`Track`
: A timeline row that owns or references an animatable target such as a slice,
canvas object, graph output, or group of properties.

`Keyframe`
: A frame-bound marker that stores a value or value set for an animatable
property. Existing notes prefer frame-first timing, with seconds displayed or
entered as a conversion layer.

`Inspector`
: A context-sensitive property surface. Existing notes call for double-clicking
a slice to show basic settings and right-clicking a keyframe to open an
inspector-like property window.

`Graph evaluation at time`
: Evaluating the relevant node graph against a specific frame or time value.
This becomes necessary for video slices, animated parameters, and video export.

## In Scope

- Slice terminology and import direction for stills and videos.
- Timeline data model and interaction model.
- Track ownership and target binding.
- Frame-first timebase, seconds conversion, and rounding rules.
- Keyframe storage, interpolation, and inspector behavior.
- Animated canvas/object transforms such as position, scale, rotation, opacity,
  and ordering.
- Animated graph or layer parameters after the graph ownership model is clear.
- Still-frame export at a selected frame.
- Video export through a future frame-sequence or encoder path.
- Research and code inspection needed before implementation.

## Out Of Scope For The First Planning Pass

- Implementation code changes.
- Moving, merging, or retiring the existing slice/timeline/composite docs.
- Full replacement of Editor and Composite modules.
- Final UI visual design or Figma token decisions.
- Audio editing, audio waveform display, and audio export.
- 3D animation in the Render tab.
- A full non-linear video editor.

## Design Biases From Existing Notes

- Think in frames first, not seconds first.
- Keep seconds entry/display available, but round keyframe placement to real
  frame boundaries.
- Treat still-frame export and video export as related output workflows.
- Preserve the distinction between texture-space processing and canvas-space
  transforms.
- Do not collapse compositing, canvas layout, and motion planning into one
  ambiguous term too early.
