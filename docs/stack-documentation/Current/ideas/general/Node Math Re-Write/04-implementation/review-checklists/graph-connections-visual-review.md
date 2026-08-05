# Phase 5B Visual Test Checklist

- Status: ready for native user review
- Updated: 2026-07-16
- Scope: compound output and typed connection presentation, including the
  Phase 5B-C corrective UI pass

## Compound Output

1. Add an Image, `Add, Then Multiply`, `Exposure, Then Premultiply`, and Output.
2. Connect them in that exact order.
3. Confirm the viewport renders before either compound is unpacked.
4. Change Add, Multiply, and Exposure controls and confirm the viewport updates.
5. Unpack one compound, then both; confirm the visible result does not change.
6. Save, reload, copy/paste, Make Unique, and nest a compound; confirm each
   authored form renders before Unpack.
7. Open a project whose exact compound definition is deliberately unavailable;
   confirm the unresolved node and explanatory error remain visible and no
   substitute output appears.

## Pin Labels And Detail Cards

1. Expand ordinary layer, mask, channel, value, frequency, RAW, Output, and
   compound nodes.
2. Confirm every pin center and visible ring is fully outside its node body.
3. Hover a node and confirm every displayed input label appears beyond its pin
   to the left and every displayed output label appears beyond its pin to the
   right.
4. Confirm long labels ellipsize inside compact contrast pills without covering
   pins, node bodies, node controls, or wire labels.
5. Move off the node and confirm its socket labels disappear. Start a connection
   drag and confirm labels remain on the source and compatible targets only.
6. Confirm socket-label pills do not capture clicks, block pin dragging, or
   enlarge body selection bounds.
7. Hover a pin for less than 0.7 seconds; confirm no detail card appears.
8. Continue hovering beyond 0.7 seconds; confirm role, direction, requirement,
   type, shape, storage, units, known state, and compatibility information.
9. Confirm the card opens next to the hovered pin without first flashing above
   or below it.
10. Move to another pin and then move the cursor outside Stack; confirm the old
   card closes and does not follow the cursor or remain stuck over another app.
11. Check an ordinary image socket with no declared layout; it must say Unknown,
   not assume RGB or RGBA.
12. Confirm R/G/B/A sockets say channel/scalar field rather than mask.
13. Select and hover an Image node, Output node, and ordinary node; confirm no
    selected-node preview tooltip follows the cursor. Double-click the Image
    node and confirm its existing media interaction still works.

## Advanced Connections

1. Inspect Output or another node with advanced sockets.
2. Click `+ N Inputs` or `+ N Outputs` and confirm the sockets reveal for the
   current editor session.
3. Connect an advanced socket, collapse/reopen the node, and confirm the
   connected socket remains visible.
4. Confirm there is no on-node `Connections` dropdown and no invisible click
   target extending beyond the node.
5. Hover each revealed pin for its anchored detail card.

## Wire Labels And Layout

1. In Settings > Graph, test Connection labels: Adaptive, Always, Interaction
   Only, and Off.
2. Test Connection text layout: Floating and Break Line.
3. Change Connection text size and confirm it updates without moving pins.
4. Compare Zoom-Aware and Fixed sizing while zooming the graph.
5. Toggle Connection text outline; confirm Off has no black border and On adds
   the optional readability outline.
6. Restart Stack and confirm all connection-text preferences persist.
7. Test Classic, Black Nodes, and Spotlight appearance modes.
8. Test curved/straight and solid/dotted connection styles. Confirm each label
   is parallel to the local wire direction and stays upright rather than
   becoming upside down.
9. Test dark/light backgrounds and dense/sparse graphs.
10. At zoom 0.75 or above on a long wire, confirm both lines appear when clear.
11. At zoom 0.55 or above on a medium wire, confirm only the primary line.
12. Zoom below 0.55; confirm ordinary labels hide and reappear on hover or
   selection.
13. Hover and click the rotated text; confirm it behaves as the same wire.
14. Hold the wire/text hover beyond 0.7 seconds; confirm the detail card appears
    once at the wire-label anchor without jumping between positions.
15. In Break Line, confirm enough visible wire remains near both pins.
16. Confirm short or node-obstructed wires automatically use Floating.
17. Confirm labels do not visibly flicker between candidate positions.

## Canonical Node Layout

1. Confirm Settings > Graph no longer contains node-size presets, width, UI
   scale, or grab-height controls.
2. Inspect an all-node gallery containing every node kind in collapsed and
   expanded states, including Constant Channel, Technical Image, Compound,
   Layer, Composite, both frequency nodes, and variable-input nodes.
3. At zoom values 0.16, 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, and 4.5, confirm the
   entire node scales as one object. Titles, summaries, controls, previews,
   sockets, and labels must not reflow, hide, overlap, clip, or change relative
   position.
4. Confirm titles and primary summaries are centered. Long titles may wrap to
   two centered lines; only pathological second-line overflow may ellipsize and
   it must expose a full-title tooltip.
5. Confirm form labels and values remain left aligned and wrapped or stacked
   control rows contribute to the node height.
6. Compare Classic, Black, and Spotlight. Geometry, wrapping, control stacking,
   sockets, and all bounds must remain identical; only palette and decorative
   treatment may differ.
7. Pan nodes against every viewport edge and confirm detached sockets are not
   culled. Run autofocus, fit-to-graph, capture, group overlap, and connection
   dragging and confirm each uses the complete persistent visual bounds.
8. Box-select beside an input socket and drag the node near a socket. Confirm
   body selection and dragging remain owned by the body while the enlarged
   invisible pin target remains usable.
9. Repeat representative zoom checks at each supported DPI scale. Raster
   antialiasing may differ, but structure and line breaks must not.

## Explicit Image-To-Mask Extraction

1. Add an Image node and a Brightness, Mix, or other node with a Mask or
   single-channel input.
2. Drag the Image output directly to that Mask/scalar-field input.
3. Confirm the connection is rejected, no node is created, and Stack explains
   that an explicit extraction node is required.
4. Add `Luminance Mask` manually. Connect Image to Luminance Mask, then its Mask
   output to the destination Mask input; confirm that explicit path connects.
5. Repeat with a Channel Split output and confirm an explicit channel path also
   connects.

## Usability Guard

Confirm node titles, controls, previews, pin hit targets, wire dragging,
selection, context menus, copy/paste, Make Unique, Unpack, and ordinary viewport
rendering remain usable. Record any node whose label density or advanced-socket
grouping needs a node-specific adjustment before Phase 6 begins.
