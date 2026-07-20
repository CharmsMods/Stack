# Phase 5B Typed Connection Presentation Contract

- Status: implemented and verified, including the Phase 5B-C corrective UI pass
- Adopted: 2026-07-16
- Decision: NMR-133
- Scope: corrective Phase 5 pass only

## Purpose

Phase 5B repairs authored compound execution and makes graph connections
understandable without inventing channel layouts or silently changing data.
Socket text explains what Stack knows. Connections remain permissive when the
underlying value shapes are structurally executable; a full image cannot be
treated as a one-channel scalar field without an explicit extraction node.

## Authored Compound Execution

Output-chain and reference-canvas traversal carry both the node ID and exact
output socket ID. When traversal reaches a compound, it resolves the exact
embedded definition and public output, follows that output's canonical internal
dependencies, and maps only the contributing public inputs back to the authored
graph. Contributing required inputs must be connected. Connected optional inputs
must also lead to executable sources.

Unresolved, invalid, or missing exact definitions never masquerade as valid
output. They retain their saved public interface and connections, block
execution, and produce a stable explanatory diagnostic.

## Normalized Socket Presentation

Every socket returned by the live graph receives presentation metadata:

- existing logical value type;
- reusable semantic role key;
- declared channel or component shape;
- units when applicable; and
- visibility tier: required, common optional, or advanced.

Presentation wording is centralized in
`src/Editor/NodeGraph/SocketPresentation.h`. It is excluded from semantic
definition hashes. If a future audit changes an actual logical port contract,
the owning definition must be deliberately versioned and re-hashed.

Stack reports `Unknown` when it does not know an image layout, units, alpha,
range, or another semantic state. Ordinary `Image` sockets are not silently
called RGB or RGBA. Channel sockets are consistently scalar fields rather than
sometimes being presented as masks.

`node-socket-catalog.md` is the generated maintained inventory. It includes
visible catalog nodes, hidden layer definitions, non-browser/generated nodes,
all declared sockets (including hidden ones), and shipped dynamic compound
interfaces.

## Node Presentation

Expanded nodes show concise input labels immediately to the right of left-side
pins and output labels immediately to the left of right-side pins. Labels are
ellipsized inside measured usable space and never replace the pin hit target.

Compact, preview, collapsed, and summary nodes show short labels while selected
and reveal the relevant label on node or pin hover. A shared detail card opens
only after 0.7 seconds of continuous pin or wire hover.

The detail card reports direction, role, required/optional/advanced status,
logical type, channel/component count, storage class, units, known semantic
state, and compatibility family. It is anchored to the hovered pin or wire
label after 0.7 seconds rather than following the cursor. Its timer and visible
state reset when the graph loses hover/focus, an interaction begins, or the
hover target changes, so it does not flash between provisional positions or
remain stranded outside Stack.

Required and common optional sockets stay visible. Unconnected advanced sockets
collapse behind `+ N Inputs` or `+ N Outputs`; clicking reveals them for the
current editor session. Connected advanced sockets always remain visible. The
on-node `Connections` dropdown is not part of the final design: socket
discovery is pin-first through visible labels, progressive advanced-pin reveal,
and anchored detail cards. This also removes the unbounded dropdown hit area.

Image and Output nodes do not show a selected-node cursor-following preview
tooltip. Their existing double-click and node interaction paths remain the way
to inspect or act on the media.

Appearance settings version 9 adds node surface controls. `Compact`,
`Comfortable`, and `Spacious` presets set width, internal UI scale, and the
minimum title/grab height; advanced sliders can adjust those values separately
and produce a Custom preset. The default is Comfortable, which gives the node a
larger drag surface while preserving the established content layout.

## Wire Presentation

Appearance settings version 8 added program-level, non-project preferences:

- Connection labels: Adaptive, Always, Interaction Only, or Off.
- Connection text layout: Floating or Break Line.

Appearance settings version 9 adds:

- Connection text size: 8 to 20 pixels, default 11.
- Connection text sizing: Zoom-Aware or Fixed, default Zoom-Aware.
- Connection text outline: on or off, default off.
- Node size preset and advanced node width/UI/grab-height controls.

Version 7 migrates through the version 8 defaults. Version 8 migrates to the
version 9 defaults listed above. Default labels are two lines with no pill,
filled background, or outline:

```text
Color image · Unknown channels
sRGB · encoded · straight alpha
```

The lower line uses units, storage, range, or domain for non-image data when
known. Text rotates with the local straight-line or Bezier tangent and remains
upright, so it stays parallel to its wire without becoming upside down. It is
colored by data family. The optional one-pixel readability outline is disabled
by default. Text and line share hover and selection.

Adaptive visibility is deterministic:

- zoom 0.75 or greater and wire length 140 screen pixels or greater: two lines;
- zoom 0.55 or greater and wire length 90 screen pixels or greater: primary
  line;
- below those thresholds: ordinary labels are hidden; and
- hovered, selected, or endpoint-selected wires reveal their available text.

Placement tries the midpoint, deterministic along-wire offsets, and offsets
normal to the local wire direction. Rotated text bounds participate in
collision and hit testing. Placement then drops the lower line and finally
hides a non-interacted label. Interaction overrides ordinary collision hiding.

Break Line measures the wider line, adds 8 screen pixels per side, and keeps at
least 24 visible pixels between each endpoint and the gap. It supports curved,
straight, solid, and dotted wires. Short or obstructed wires fall back to
Floating for that wire.

## Boundaries

This contract does not change formulas, color or alpha conversion, viewport
transforms, RAW decomposition, ROI, halo, neighborhood, reduction, tiling, or
Phase 6 infrastructure. It rejects the structurally invalid full-image to
single-channel/scalar-field shortcut and never auto-spawns a Luminance Mask;
the user must add an explicit Luminance Mask, Channel Split, or another
extraction node. Color, transfer, alpha, range, and other semantic mismatches
that remain structurally executable are still informative rather than hidden
conversions. Old-project compatibility remains out of scope; the small
independent appearance preference migrations are retained.
