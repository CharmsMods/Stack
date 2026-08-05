# Current Stack RAW UI Audit

Status: first code audit complete; this preserves the pre-research baseline.

## Product Surface to Map

The priority controls named for this redesign are:

1. scene-linear RAW Exposure;
2. the Local Range/Local Exposure graph;
3. the Finish Tone/Tone Curve graph; and
4. View Transform/Display Fit.

The audit will distinguish these creative controls from source diagnostics,
technical RAW interpretation, project/gallery actions, and less-used controls
that should not dictate the new layout.

## Current Layout Structure

After the project/gallery action row, the workspace creates an ImGui dockspace
and programmatically seeds three docked windows:

```text
Controls (27%) | Image (remaining center) | Tone Graphs (30%)
```

The split ratios are defaults, not a fixed composition. The user can move and
re-dock these windows because the dockspace uses normal dock-node behavior.
There is also a `Reset Panels` control whose purpose is to reconstruct this
three-window arrangement. The Gallery is a fourth, separate floating window
with a custom drag zone.

The window titles and default layout make the surface understandable, but the
underlying window model is the source of seams, docking behavior, and
window-oriented spacing. Hiding docking buttons or title text does not change
that structural problem.

## Current Controls Window

The Controls window is a vertical form. Its visible hierarchy begins with
source identity, several status badges, Save and overflow actions, then a
separator and `Main Controls`. The editing controls are grouped in expandable
headers:

- `RAW Pipeline`
  - processing-version and working-space text;
  - Demosaic dropdown;
  - Apply DNG Baseline Exposure checkbox;
  - explanatory paragraphs.
- `Base Light`
  - RAW Exposure slider;
  - a `View Transform / Display Fit` text label;
  - Display Exposure, Black EV, White EV, and Middle Grey sliders;
  - an `Advanced` tree containing Reset, Shoulder, Toe, Contrast, Saturation,
    Preserve Hue, false-color, and output-encoding controls.
- `White Balance`
  - summary text;
  - mode dropdown;
  - custom multiplier controls and legacy migration states.
- Additional technical, preview, source, and output controls continue below.

The important design problem is not merely that the list is long. RAW Exposure
and View Transform are different stages but are nested in the same `Base Light`
section, while the graph tools live in another window. The hierarchy therefore
explains code ownership better than it explains the user's editing flow.

## Current Tone Graphs Window

The Tone Graphs window is also vertically scrollable. It contains:

### Local Range

- an expandable `Local Range` header;
- Enable checkbox;
- Target/Stop Target and Reset buttons;
- three preset buttons;
- a graph framed by an outer rounded rectangle plus an inner plot border;
- optional target/color summary;
- a four-button overlay-mode row;
- Strength slider;
- an `Advanced` tree for smoothing/protection parameters;
- expandable Color Target and Region Mask groups.

### Finish Tone

- an expandable `Finish Tone` header;
- a text summary;
- five large channel-mode buttons;
- Curve Domain dropdown;
- a framed tone-curve graph;
- an `Advanced` tree for graph EV bounds and reset.

These are already the strongest direct-manipulation surfaces in the RAW tab,
but both are surrounded by form controls, disclosure layers, button
containers, text summaries, and visible graph frames. The graph window can
also scroll, so neither graph is guaranteed to remain spatially stable.

## Current Preview Region

The Image window is the visual center and has its own preview-mode controls and
overlays. Because it is a docked ImGui window, it remains a peer window rather
than the continuous canvas around which tools are arranged.

## Local Precedent: Editor Tab

The Editor tab does not use a dockspace for its main graph/viewport
relationship. It:

- creates fixed `EditorGraphPane` and `EditorViewportPane` child regions;
- sets child-border size to zero;
- gives adjacent panes the same workspace background or a transparent
  wallpaper surface;
- positions panes directly using screen coordinates;
- makes an invisible 32-pixel-wide overlay the resize target;
- draws only a small circular handle when useful;
- supports edge snapping and animated reveal without turning either pane into
  a movable window;
- uses spacing rather than separator lines in floating drawers; and
- uses transparent/backgroundless selectable rows with color or text changes
  for hover and selection.

This is the right implementation family for the RAW redesign: fixed regions,
direct size ownership, an invisible generous drag target, and minimal visual
chrome.

## What Should Survive the Redesign

- Direct manipulation of Local Range and Finish Tone graphs.
- The explicit distinction between scene-linear RAW Exposure and display
  mapping, even though their current visual grouping should change.
- Targeting from the image into Local Range.
- Preview overlays and before/after or neutral/developed inspection.
- Status and source facts, but not as a permanently dominant badge block.
- A resizable preview and graph/tool regions.

## What Should Not Dictate the New Layout

- The current three-window dockspace.
- `Reset Panels` as a primary action.
- One long Controls scroll.
- Expandable headers as the main navigation system.
- Persistent explanatory prose inside the everyday editing surface.
- Borders around every graph, row, group, and button.
- Controls grouped solely because they share a backing JSON object or source
  function.

## Design Constraints Established Before Research

1. Major RAW regions should be fixed in role and resizable in size.
2. Resizing should use invisible or nearly invisible splitter hit areas.
3. Tool families should be selected from compact icon/text-icon rails rather
   than stacked disclosure headers.
4. At least the active graph and its essential controls should remain
   spatially stable instead of moving in a long scroll.
5. Source/setup and technical controls should be accessible without competing
   continuously with the four priority editing surfaces.
6. Graph state should be expressed through the curve, points, tint, lightweight
   labels, and overlays—not multiple nested boxes.
7. Advanced parameters need a deliberate secondary surface; removing
   disclosure triangles must not mean dumping advanced controls into the main
   view.
