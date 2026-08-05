# RAW UI Design Synthesis

Status: first synthesis complete; conceptual only.

## Common Patterns Across Adobe and Resolve

Despite very different products, the useful common ground is strong:

1. **The image is the center of gravity.** Tools surround or contextualize the
   image; major adjustment regions are not arbitrary peer windows.
2. **Global structure is stable.** Users learn where the preview, tools,
   measurement, comparison, and image navigation live.
3. **Tool families switch context.** Adobe uses Edit/Mask/Crop-style tools;
   Resolve uses palette rows. Neither requires every tool family to remain
   vertically expanded.
4. **The active tool receives a dedicated surface.** Curves, masking, scopes,
   and camera RAW each receive interaction appropriate to their purpose.
5. **Measurement remains close.** Histograms, clipping, or scopes remain
   available while editing rather than becoming prose in an advanced panel.
6. **The preview owns view state.** Before/after, wipe, split, overlays, zoom,
   clipping, and target controls belong to the image context.
7. **Technical input controls have a separate home.** Resolve's Camera Raw
   palette and Adobe's profile/optics/calibration areas do not compete
   continuously with the primary tone interface.
8. **Responsive layout is application-managed.** Regions hide, merge, or
   reflow at lower widths instead of asking the user to repair a dock layout.
9. **State is communicated economically.** Tool dots, active tint, eye/bypass
   state, tooltips, and one active title carry meaning without a border around
   every control.

## Critical Comparison with Stack

Stack has the right ingredients but the wrong visual contract.

- The preview, controls, and graphs are peers in a dockspace, so the layout
  feels like windows rather than one editing instrument.
- The four priority stages are not the four visible work contexts. RAW
  Exposure and View Transform are merged into Base Light; Local and Finish
  graphs are merged into a scrolling Tone Graphs window.
- The strongest interfaces—Local and Finish graphs—are treated as framed
  widgets within expandable forms.
- Technical pipeline information and everyday creative controls compete in the
  same persistent vertical space.
- Text summaries and explanatory prose compensate for unclear hierarchy, but
  add density.
- Fixed preview modes already exist, but read as another button row rather than
  a coherent viewer-state system.

The redesign should change those relationships before polishing colors or
icons. Removing borders from the current dockspace would make it quieter but
would not make it coherent.

## Recommended Direction: Canvas + Precision Rail

This is the strongest fit for the user's direction and Stack's current scope.

```text
┌ quiet global actions / source identity ───────────────────────────┐
│                                                                   │
│                         IMAGE PREVIEW                tool  active  │
│                                                      rail   tool   │
│                                                                   │
│  optional exposure HUD / viewer states                            │
├ optional gallery filmstrip, hidden when unused ───────────────────┤
└───────────────────────────────────────────────────────────────────┘
```

The diagram shows roles, not visible boxes. The actual implementation should
use one continuous background and whitespace/positioning.

### Fixed regions

- A large preview owns most of the screen.
- A narrow bare-icon rail stays at the right edge.
- A resizable active-tool region sits beside the rail.
- A Gallery filmstrip is optional and application-positioned; it is not a
  draggable window.
- A compact scope can occupy the lower part of the active-tool region or appear
  as an optional preview-side tile.

### Tool rail

Initial tool families:

```text
Light   Local   Tone   View   Inspect   Setup
```

- `Light`: RAW Exposure, White Balance, and only the most common capture
  placement controls.
- `Local`: Local Exposure graph, targeting, strength, overlay state, and
  selected-point context.
- `Tone`: Finish Tone graph and its graph/channel/domain context.
- `View`: scene-to-display mapping and display/output inspection.
- `Inspect`: histogram/waveform/clipping, metadata, and warnings.
- `Setup`: processing version, demosaic, baseline exposure, camera/profile,
  output assumptions, and other low-frequency technical choices.

The active tool name appears once in the tool surface. Tool icons remain bare,
with hover tint, changed-state dot, and accessible tooltip.

### Persistent primary access

RAW Exposure is important enough to remain reachable while another tool is
active. Two plausible treatments:

1. a slim horizontal exposure HUD attached to the bottom of the preview; or
2. a one-line primary strip at the top of the active tool surface.

The first is more minimal and image-centric. It should use a bare line, handle,
EV value, and reset gesture—not a filled slider container.

### Advanced controls

Each active tool has one consistent `More` action that opens a temporary
secondary sheet or drawer. It should not expand the page downward. The sheet
may contain advanced Local protection, graph EV bounds, or detailed View
parameters. Closing it returns to the exact same spatial layout.

## Alternative A: Resolve-Style Lower Workbench

```text
┌──────────────────── IMAGE PREVIEW ────────────────────────────────┐
│                                                                   │
├ Light/WB ─────────── active Local/Tone/View graph ─── scope ──────┤
└ tool palette row ─────────────────────────────────────────────────┘
```

The horizontal boundary is resizable through an invisible splitter. A compact
palette row switches the center graph. RAW Exposure stays visible at lower
left, and an optional scope stays at lower right.

Strengths:

- graphs receive much more horizontal area;
- direct comparison between primary placement, curve, and scope;
- very stable spatial memory;
- close to Resolve's proven grading geometry.

Weaknesses:

- reduces preview height;
- can feel video-oriented;
- less comfortable on a narrow laptop;
- Local's additional controls need a careful contextual overlay or side strip.

This is a strong “graph focus” workspace or alternate layout, but probably not
the best sole default.

## Alternative B: Full-Canvas Floating Shelf

```text
┌──────────────────── IMAGE PREVIEW ────────────────────────────────┐
│                                                       tool rail   │
│       active controls/graph float over a faded edge shelf         │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

The active surface fades into the image background with a gradient rather than
a hard panel boundary, borrowing Stack's Editor drawer treatment.

Strengths:

- strongest floating/seamless feeling;
- maximum preview size;
- distinctive Stack identity.

Weaknesses:

- graph contrast can vary with the photo;
- controls may obscure important image content;
- background blur/tint can interfere with judging color;
- moving the shelf or preview to avoid obstruction would reintroduce layout
  complexity.

Use this treatment for compact drawers, viewer controls, or a temporary
focus mode—not as the only editing layout.

## Alternative C: Two Fixed Precision Columns

```text
┌──────────── preview ────────────┬ primary ┬ active graph ─────────┐
│                                │ Light   │ Local/Tone/View       │
│                                │ WB      │                       │
└────────────────────────────────┴─────────┴───────────────────────┘
```

This is Resolve's simultaneous-primary idea translated vertically. Both
tool columns are fixed in role and resizable only through invisible splitters.

Strengths:

- RAW Exposure and WB never disappear;
- the graph stays large and stable;
- straightforward to implement from Stack's Editor pane architecture.

Weaknesses:

- consumes more horizontal space;
- can become another dense inspector if the primary column grows;
- requires strong discipline about what qualifies as persistent.

This can work well on wide displays and collapse to Canvas + Precision Rail at
smaller widths.

## Graph Redesign Principles

### Shared graph language

Local, Tone, and View should share:

- the same frameless canvas;
- consistent point size and selection treatment;
- subdued grid hierarchy;
- hover crosshair and compact numeric readout;
- direct point dragging and keyboard adjustment;
- double-click reset or point creation where appropriate;
- optional histogram/scene-EV distribution beneath the graph;
- contextual channel/domain controls in a small row above the canvas; and
- generous invisible interaction targets around points and handles.

### Remove

- outer rounded graph frame;
- inner plot border;
- permanent summary sentence above the graph;
- container backgrounds around every graph-mode button;
- axis labels repeated in surrounding form controls; and
- advanced trees directly below the graph.

### Keep quietly

- faint major grid lines;
- even fainter minor divisions only when they aid judgment;
- edge labels such as scene EV and output level;
- clipping/limit cues;
- the zero/identity line;
- accent color only for the active curve/point or changed state.

### Local Exposure graph

- Make Target a small crosshair/picker action near the graph or preview.
- Put Affected/Delta/Mask into the preview-state strip.
- Show Strength as a bare scrub line or a scrubbable value close to the graph.
- Show point-specific EV/color context only when a point is selected.
- Put smoothing/protection and region/color-mask construction in the consistent
  secondary sheet.

### Finish Tone graph

- Keep Y/RGB/R/G/B as a compact graph-specific mode row.
- Use text or unmistakable channel marks; do not invent opaque icons.
- Replace the Curve Domain dropdown with a two-state segmented text control
  because the set is small and stable.
- Add an optional histogram underlay.
- Move graph black/white EV bounds into the secondary sheet or allow direct
  manipulation of edge handles.

### View Transform

The current View Transform is conceptually a mapping but visually a slider
list. A future design should test a graph from scene EV to display output:

- black, middle-gray, and white anchors;
- toe and shoulder shape handles;
- display-exposure offset;
- clipping/rolloff cues; and
- a compact numeric inspector for the selected handle.

This could make Display Fit understandable through direct visualization rather
than terminology alone. It must be validated against the actual math before it
becomes the authoritative control.

## Control Replacement Rules

Do not ban dropdowns categorically. Use the smallest honest interaction:

- two to five stable choices: inline segmented text/icon row;
- tool-family navigation: icon rail with active name and tooltip;
- binary reversible state: small switch or icon state with explicit label when
  meaning is not obvious;
- continuous scalar: direct graph handle, bare slider, or scrubbable number;
- large/dynamic choice set: searchable dropdown/popover;
- rare technical choice: Setup surface or popover;
- dangerous/destructive action: visible text confirmation, not icon-only;
- advanced parameters: one consistent temporary secondary surface.

## Fixed Resizing and Responsive States

Stack can adapt its Editor-tab technique:

- zero child borders;
- one continuous surface/background;
- manually positioned fixed-role children;
- invisible 12–24 pixel resize zones;
- a small handle or tint only on hover/drag;
- remembered widths with sane minimums;
- double-click splitter reset;
- no drag-to-rearrange;
- three designed width states rather than arbitrary collapse:
  - wide: persistent primary plus graph plus optional scope;
  - standard: preview plus rail plus active tool;
  - compact: preview plus rail, with tool surface overlaying/replacing part of
    the preview on demand.

## Recommended First Design Artifact

Before implementation, make one annotated wireframe for the recommended Canvas
+ Precision Rail direction in three widths. It should answer:

1. What is always visible?
2. What changes when each tool icon is selected?
3. Where does RAW Exposure live while Local/Tone/View is active?
4. Where do technical Setup choices live?
5. How do Gallery and Inspect appear without floating windows?
6. What visual state means active, changed, bypassed, warning, or focused?
7. How do advanced controls appear and disappear without moving the graph?

Only after those questions are settled should the current dockspace be replaced.
