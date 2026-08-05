# Compound Node Behavior — Research Plan

> **Research plan:** This file defines questions and deliverables; it does not
> contain completed findings. New evidence may propose a recorded design
> revision, but it does not silently change accepted direction.

- Started: 2026-07-17
- Refocused: 2026-07-20
- Authority: `../../02-channel-based-design/accepted-product-direction.md`
- Status: action names and honesty classes accepted; interaction details remain

## Fixed Product Boundary

Inspect, Edit/Make Unique, and Dissolve/Unpack are distinct. Primitive,
Transparent Compound, Optimized Compound, and Structured Specialized Operation
are the user-facing honesty classes. Research should refine the unresolved
interaction details; materially contrary evidence may propose a recorded
revision rather than changing these choices implicitly.

## Remaining Research

- How mature systems present read-only inspection versus destructive unpacking.
- How shared definitions are edited or versioned without changing saved
  projects silently.
- Stable public ports and promoted parameters across internal changes.
- Optional/advanced outputs and channel-bundle presentation.
- Canonical graph equivalence across CPU, GPU, fused, tiled, and specialized
  implementations.
- Tolerances and nondeterministic algorithm evidence.
- Graph-explosion, nesting, recursion, feedback, and iteration safeguards.
- What a specialized operation should expose when full decomposition is not
  truthful.

## Preferred Sources

Use official Nuke, Blender, MaterialX, Houdini, and Fusion documentation, plus
primary graph/compiler specifications where useful.

## Stack Case Studies

- pointwise adjustment;
- Saturation;
- Gaussian Blur;
- masked adjustment;
- output finishing; and
- RAW Development or neural denoise as an honest specialized boundary.

## Deliverable

The C8 implementation contract: exact action behavior, shared-definition
rules, interface stability, optimized-equivalence evidence, graph-size policy,
and channel-aware UI.
