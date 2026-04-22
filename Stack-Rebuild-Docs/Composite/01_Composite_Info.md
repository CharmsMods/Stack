# Composite Features and Layout

## What Can Be Done
Composite is a 2D rendering tab that allows users to arrange, layer, and bound a multitude of different assets into a defined composition board.

**Features:**
- **Layering System:** Supports dropping in completely saved Editor projects, raw images, text, and primitive shapes (circles, squares, triangles) as individual layers.
- **Dimensional Reasoning:** Layers respect explicit dimensional logic. A 4k image doesn't stretch randomly when moved. Users interact inside an infinite world-canvas that allows deep local panning/zooming around the composed set.
- **Exporting Options:** Supports auto-bounds (just shrink wrapping the items) or custom draggable bounding boxes to export explicitly sized crops of the canvas combination.
- **Transformation Handling:** Handles grouped object selections visually with group-scale dragging. Allows individual scale anchors (Uniform, Stretch).

## Layout Design
- **Workspace Paradigm:** Shares the structured Shell UI pattern (Neumorphic Style).
- **Sidebar Structure:**
  - **Layers:** Ordering, locking, visibility.
  - **Selected:** Replaces the generic 'transform' views, giving context-heavy options per item. Allows replacing items contextually.
  - **View:** Checkered-background toggles, sizing details.
  - **Export:** Bound definitions, bounding drag triggers, explicit 'Use View As Export' utilities.
- **Viewport Structure:** Features an "Infinite Canvas" pattern which has its own local interaction session so direct drag/pan operations don't write constantly to the Redux store on every mouse-tick.
