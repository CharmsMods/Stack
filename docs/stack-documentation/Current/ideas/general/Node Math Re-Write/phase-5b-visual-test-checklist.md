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
2. Confirm every displayed left pin has a label to its right and every displayed
   right pin has a label to its left.
3. Confirm long labels ellipsize without covering the pin or node controls.
4. Select and hover compact, preview, collapsed, and summary nodes; confirm the
   relevant short labels appear immediately.
5. Hover a pin for less than 0.7 seconds; confirm no detail card appears.
6. Continue hovering beyond 0.7 seconds; confirm role, direction, requirement,
   type, shape, storage, units, known state, and compatibility information.
7. Confirm the card opens next to the hovered pin without first flashing above
   or below it.
8. Move to another pin and then move the cursor outside Stack; confirm the old
   card closes and does not follow the cursor or remain stuck over another app.
9. Check an ordinary image socket with no declared layout; it must say Unknown,
   not assume RGB or RGBA.
10. Confirm R/G/B/A sockets say channel/scalar field rather than mask.
11. Select and hover an Image node, Output node, and ordinary node; confirm no
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

## Node Surface Sizing

1. In Settings > Graph, select Compact, Comfortable, and Spacious.
2. Confirm width, control spacing, and the title/grab surface change together
   without producing blank clickable space outside the visible node.
3. Drag each node from the title/header surface and confirm it is easier to
   acquire in Comfortable and Spacious.
4. Adjust Node width, Node UI size, and Grab area height separately; confirm the
   preset becomes Custom and controls, previews, labels, and pins stay inside
   the node.
5. Restart Stack and confirm the node sizing choice persists.

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
