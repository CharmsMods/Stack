# Node Graph Interaction Guide

This guide documents the current custom Dear ImGui node graph behavior after the interaction and sizing stabilization work. It is intended for future developers and AI agents working on the graph so they can extend it without accidentally reintroducing the old bugs around sliders, dropdowns, dragging, pin connections, and clipped node content.

## Current Architecture

The Editor node graph is a custom Dear ImGui graph, not `imgui-node-editor` or `imnodes`.

The graph deliberately separates three interaction layers:

1. Canvas gestures
2. Custom graph geometry, such as nodes, pins, and links
3. Real ImGui widgets inside node content

The canvas is passive. It reserves space with `ImGui::Dummy`, then graph hover is resolved with `ImGui::IsMouseHoveringRect(m_CanvasMin, m_CanvasMax)`. The canvas must not use a full-size `InvisibleButton` because that creates an ImGui active ID over the whole graph and breaks node widgets.

Node frames, headers, sockets, socket labels, links, and the grid are custom drawn. Node controls are real ImGui controls placed in the node content rect. This is intentional: custom visuals are allowed, but sliders, combos, inputs, color edits, buttons, and checkboxes must remain normal ImGui widgets so ImGui can own hover, active, popup, and editing state.

The main interaction owner is `GraphMouseOwner` in `EditorNodeGraphUI`. It is computed once per frame by `ResolveMouseOwner`, then graph actions use that owner instead of each action doing separate ad hoc hit testing.

## Mouse Owner Rules

`GraphMouseOwner` has these states:

- `None`: Mouse is outside the graph.
- `Canvas`: Mouse is over empty graph canvas.
- `NodeFrame`: Mouse is over node chrome/gutter/frame, but not over real node controls.
- `NodeHeader`: Mouse is over the node header area.
- `NodeContent`: Mouse is over active or hover-capturing ImGui controls inside a node.
- `InputPin`: Mouse is over an input socket.
- `OutputPin`: Mouse is over an output socket.
- `Link`: Mouse is over a connection link.
- `Popup`: A non-node-browser ImGui popup is open.

The intended priority is:

1. Popup
2. Pins
3. Node header
4. Node frame/gutter
5. Node content when ImGui wants the mouse
6. Link
7. Canvas

This priority is important. Pins must stay usable even when they are near node content. Headers and frame/gutters must be draggable even if a previous frame had a widget active. Node content must protect real controls from graph-level dragging and selection. Links come after node geometry so a link behind a node cannot steal node interaction.

Do not let stale state such as `m_NodeContentActive` classify the header or frame as `NodeContent`. That was one of the reasons node dragging broke.

## Canvas Interactions

The canvas supports:

- Mouse wheel zoom while hovered, unless a popup is open.
- Middle mouse drag panning when the owner is `Canvas`.
- Left mouse drag box selection when the owner is `Canvas`.
- Left click empty canvas clears selection unless additive selection is active.
- Right click canvas opens the graph context menu.
- Tab opens the node browser when the graph or graph window is focused and no popup is open.

The canvas must remain passive. Do not add a full-canvas `InvisibleButton`, `ButtonBehavior`, or other active ImGui item over the graph.

## Node Interactions

Nodes support:

- Left click on `NodeHeader` or `NodeFrame` selects a node.
- Left drag from `NodeHeader` or `NodeFrame` moves the selected nodes.
- Double click on `NodeHeader` expands or collapses the node.
- Right click on `NodeHeader` or `NodeFrame` opens the node context menu.
- Additive selection uses Ctrl or Shift.
- Selecting a layer node also selects the corresponding layer in the editor.

Node dragging is capture-based once started. After `m_DragNodeId` is set, dragging continues until left mouse release, even if the cursor moves over node content during the drag. This prevents a drag from cancelling as soon as the cursor crosses controls inside the node.

Controls inside a node must not start node dragging. Sliders, combos, inputs, color edits, buttons, checkboxes, previews, and other real ImGui controls should be treated as `NodeContent` when ImGui reports an item hovered, active, edited, or wanting mouse capture.

## Pin And Link Interactions

Pins are custom-drawn graph geometry, not real ImGui widgets.

Pin behavior:

- Left click an output pin starts an output connection drag.
- Left click an input pin starts an input connection drag.
- Releasing a dragged output on an input attempts to connect sockets.
- Releasing a dragged input on an output attempts to connect sockets.
- Releasing a pin drag on empty graph canvas opens the node browser in a connect-from-output or connect-from-input mode.

Links are custom-drawn and hit-tested geometrically.

Link behavior:

- Left click a link selects it.
- Double left click a link removes it.
- Right click a link opens the link context menu.

Links are rendered behind nodes, but after nodes have refreshed their measured layout. This lets link endpoints follow resized nodes immediately while still drawing visually behind node bodies.

## Node Content And Sizing

Expanded node height is content-driven.

The old static max-height clamp was removed. Node height now uses:

```text
header height + measured ImGui content height + bottom padding
```

The measured content height is stored per node in `m_NodeMeasuredBaseHeights`. `NodeSize` uses that measurement when available, falling back to `ExpandedContractHeight` only before a node has been measured.

Node content is measured from the actual ImGui-rendered controls. The node content region should not be a fixed-height child that clips itself before measurement. If the content is constrained by the old node height, the graph cannot know that controls were clipped.

`contentUsedRect` is the source of truth for the real content bounds. It is used for hit testing and debug overflow reporting. If content would exceed the current frame, the measured height should cause the node to grow on the next layout pass.

Combo boxes, color pickers, and other ImGui popups are allowed to escape the node frame. These are popups, not normal node contents. Closed controls and non-popup node content should fit inside the node frame.

## Rendering Order

The graph render order is:

1. Reserve passive canvas space.
2. Draw canvas background and grid.
3. Clamp node positions.
4. Build initial node layout cache.
5. Render nodes and measure content.
6. Refresh layout after measurement.
7. Render links behind nodes using draw-list channels.
8. Process interactions using the current `GraphMouseOwner`.
9. Render validation status, debug overlay, context menus, and node browser.

Draw-list channels are used so links can be drawn visually behind nodes while still using up-to-date measured node layout.

Do not move link rendering back before node measurement unless you also provide another way to ensure socket anchors are refreshed before links draw.

## Debug Overlay

Press `Ctrl+Alt+G` to toggle the graph interaction debug overlay.

The overlay reports:

- Hovered node
- Hovered input pin
- Hovered output pin
- Hovered link
- Current `GraphMouseOwner`
- Active ImGui item ID
- Last node control ID
- Dragged node ID
- Measured and final node height
- Overflow status
- Node content hover/active state
- Popup state

Use this overlay before changing interaction code. If sliders or dropdowns stop working, check whether owner becomes `NodeContent` or `Popup` when expected. If node dragging stops working, check whether the header/frame becomes `NodeHeader` or `NodeFrame`, and whether `m_DragNodeId` remains set during the drag.

## Invariants Future Changes Must Preserve

These rules are the important part. Breaking any of them is likely to reintroduce graph interaction bugs.

- The canvas must remain passive. Do not put a full-canvas active ImGui item over the graph.
- Real node controls must remain real ImGui widgets. Do not replace sliders, combos, inputs, or buttons with draw-list-only controls unless you also implement full input ownership.
- Graph-level left-click actions must not start from active node content.
- Pin dragging must still work even though pins are custom drawn.
- Header/frame dragging must not be blocked by stale widget state from a previous frame.
- Once node dragging starts, it must continue until mouse release.
- Node content measurement must not be constrained by the old node height.
- Expanded node height must be derived from measured content height, not a static cap.
- Links must use refreshed layout data after node measurement.
- Popups may escape node bounds; normal node content should not.
- Debug overlay should continue to expose owner, drag state, measured height, final height, and overflow status.

## Common Failure Modes

If sliders cannot be dragged:

- Check whether a canvas-level active item was reintroduced.
- Check whether graph-level mouse handling runs while ImGui has an active item.
- Check whether node controls are inside a clipping child/window that prevents proper active IDs.

If dropdowns cannot open:

- Check whether popup state is classified as `Popup`.
- Check whether an ImGui clip rect or fixed child is suppressing the popup.
- Check whether graph code is closing or stealing focus from popups.

If nodes cannot be moved:

- Check `Ctrl+Alt+G`.
- Header hover should report `NodeHeader`.
- Frame/gutter hover should report `NodeFrame`.
- `m_DragNodeId` should become the node id on left click and stay set until release.
- Make sure `ownerIsContent` does not clear `m_DragNodeId` mid-drag.

If content is clipped:

- Check measured height versus final height in the debug overlay.
- Make sure the node content is not rendered inside a fixed-height child.
- Make sure new controls contribute to the ImGui group measurement.
- Avoid hard-coded node heights except as first-frame fallback estimates.

If links lag behind resized nodes:

- Check that node measurement refreshes `m_NodeLayoutCache`.
- Check that links render after layout refresh, even if visually behind nodes via draw-list channels.

## Adding New Node UI Safely

When adding new controls to a node:

1. Use normal ImGui widgets or `ImGuiExtras::Node*` helpers.
2. Render them inside the node content flow so they contribute to measured height.
3. Call the existing capture helper after custom controls if they do not go through `ImGuiExtras`.
4. Do not wrap them in fixed-height children unless the node is intentionally designed to scroll.
5. If a preview or custom visualization has a known size, reserve that exact size with `Dummy`, `Image`, or another measurable ImGui item.
6. Test with `Ctrl+Alt+G` and verify owner state over the new controls.

When adding new graph interactions:

1. Add or reuse a `GraphMouseOwner` state.
2. Update `ResolveMouseOwner` priority deliberately.
3. Route the action from the owner instead of independent hover checks.
4. Confirm it does not conflict with node content, pins, links, or popups.

## Files To Review Before Editing

Primary files:

- `src/Editor/NodeGraph/EditorNodeGraphUI.h`
- `src/Editor/NodeGraph/EditorNodeGraphUI.cpp`
- `src/Utils/ImGuiExtras.h`
- `src/Utils/ImGuiExtras.cpp`

Related split files:

- `src/Editor/NodeGraph/UI/EditorNodeGraphUIHitTesting.cpp`
- `src/Editor/NodeGraph/UI/EditorNodeGraphUINodeBrowser.cpp`
- `src/Editor/NodeGraph/EditorNodeGraph.cpp`
- `src/Editor/NodeGraph/EditorNodeGraph.h`

Layer node contents can come from individual layer `RenderUI` implementations under `src/Editor/Layers/`. Those UIs may change height dynamically, so layer node sizing must remain measurement-driven.

