# Compound And Decomposition Research

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Type: research
- Topic: compound-decomposition
- Verification: partially-verified for current Stack; external comparison not yet performed

## Research Goal

Determine how mature node systems distinguish groups, reusable definitions,
gizmos/assets, canonical subgraphs, optimized implementations, opaque plugins,
and inspection versus destructive unpacking. Apply the findings without
weakening Stack's exact embedded compound and forward-version contracts.

## Questions

- Which applications support open/edit versus unpack/dissolve as separate
  actions?
- How do shared compound definitions update without silently changing saved
  projects?
- How are public ports and promoted parameters kept stable when internals
  change?
- How do systems expose channel bundles, optional pins, variadic inputs, and
  advanced outputs?
- Can one canonical graph have CPU, GPU, fused, tiled, and external optimized
  implementations?
- How are equivalence, tolerances, and nondeterministic algorithms handled?
- What do professional systems show for an operation that cannot truthfully be
  decomposed?
- How are graph explosion, nested compounds, recursion, iteration, and feedback
  controlled?
- Should a compound be a library asset, project-embedded definition, or both
  with explicit import/update behavior?

## Candidate Sources For A Later Research Pass

- Official Nuke group/gizmo documentation.
- Official Blender node-group and asset documentation.
- MaterialX node graph, node definition, and implementation specifications.
- Official Houdini digital asset/subnetwork documentation.
- Official DaVinci Resolve/Fusion macro and group documentation where the
  required behavior is documented.
- Shader compiler and graph-system material on canonical IR versus optimized
  backends.

## Stack-Specific Case Studies

- Add Then Multiply: transparent compound and order preservation.
- Exposure Then Premultiply: canonical graph with fused equivalent.
- Saturation: channel-relationship decomposition.
- Gaussian Blur: neighborhood and border contract.
- View/output finishing: color-stage decomposition.
- RAW Development or neural denoise: honest specialized boundary.

## Deliverable

A recommendation for Stack's user actions (`Open`, `Inspect`, `Make Unique`,
`Unpack/Dissolve`), definition classes, public interface rules, optimized
equivalence requirements, and channel-aware compound UI.

