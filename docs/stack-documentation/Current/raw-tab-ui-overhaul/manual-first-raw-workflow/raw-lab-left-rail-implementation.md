# RAW Lab Left-Rail Implementation

Last updated: July 31, 2026.

## Outcome

RAW Lab remains a second presentation of Stack's existing RAW workspace, but
its first full-width bottom workbench has been replaced by an image-first
left-rail layout.

It does not create a second RAW engine, recipe, render pipeline, selected
source, project, save lifecycle, or thumbnail cache. Original RAW remains
available as the protected comparison baseline.

## Implemented Layout

RAW Lab now renders:

```text
quiet Open / Rescan / Clear command strip and project status
wrapping CFA Denoise / RGB Denoise / Exposure / Zones / Curve / View text island
vertically resizable borderless left editing rail
large borderless image preview in the right column
floating Preview menu and Lower toggle
optional lower grading placeholder below the image only
optional bottom Gallery filmstrip
```

The rail defaults to 340 logical pixels. Its preferred range is 280-420 pixels
while the layout preserves a 480-pixel preview where the window permits. The
splitter is invisible until hover or drag, when it paints a short-lived accent
line.

Zones, Curve, and View graphs are capped near 320 pixels wide and use a compact
4:3 proportion. They no longer inherit a short full-width workbench and no
longer become extremely wide and vertically compressed.

## Shared Editing Contract

`RawWorkspaceEditContext` still acquires the selected source, complete resolved
recipe, project mode, editability, and load error. A Lab tool edits the recipe
copy and commits through
`ApplyRawWorkspaceRecipeEditForSelectedSource()`.

This preserves fields RAW Lab does not expose, including white balance,
demosaic, processing version, crop/rotation, source metadata, preview/output
state, and technical assumptions.

Tool switching, Preview-menu use, layout resizing, Gallery switching,
secondary-sheet opening, and lower-shelf toggling do not commit a recipe.

## Tool Surfaces

### Denoise

- experimental pre-demosaic Fast Denoise enable switch
- DNG NoiseProfile and Fixed compatibility comparison
- green-plane and red/blue-plane strength
- edge protection and hot-pixel suppression
- secondary radius, iteration, and hot-pixel-threshold controls

The tool authors the shared schema-9 RAW recipe and executes the same
pre-demosaic path used by the Develop technical control. New and migrated
recipes keep denoise disabled, so merely opening RAW Lab does not change
pixels. Changing DNG versus Fixed participates in RAW placement cache identity.

### RGB Denoise

- disabled-by-default Denoise switch
- Color Noise
- Luminance Noise
- Detail Protection

This is a separate schema-10 post-demosaic stage. It runs after demosaic,
white balance, and working-space conversion and before authored RAW Exposure.
It does not reuse or reactivate the legacy denoise nodes. The text island wraps
when both denoise tool names do not fit the current rail width.

### Exposure

- scene-linear RAW Exposure
- explicit EV readout and reset
- zero marker
- preview-attached exposure HUD while another tool is active

The histogram/headroom area remains reserved for later work.

### Zones

- switchable `Overall Tones` and `Targeted Areas` views
- Overall Tones keeps Open Shadows, Hold Highlights, Compress, and direct
  scene-EV curve editing separate from independent target zones
- Overall Tones can show a Luma or overlaid RGB histogram behind the graph.
  Its bins are aligned to the graph's middle-grey-relative EV axis and come
  from the scene-linear texture immediately before Local Range.
- Targeted Areas provides named target rows plus selected-target Enabled,
  Exposure, Brightness, Reach, Feather, Selected Areas / All Matches, Color
  Match, Color Reach, Color Feather, overlap, reset, and delete controls
- Targeted Areas exposes Final, Affected, Delta, and combined Mask previews in
  the rail while image editing continues to use the sparse target outline
- leaving Targeted Areas or switching away from Zones disarms target gestures
- Strength remains shared by the complete Local Range stage
- secondary Smoothness, Edge Protection, Detail Protection, Color Target, and
  Region Mask controls

The recipe continues to use `RawLocalRangeRecipe` and its existing
sanitizer/presets. This pass adds no recipe schema or Local Range render math.
The existing linear, radial, and luminance region mask remains a mask for the
overall Local Range block; it is not presented as a per-target mask.

### Curve

- compact frameless sampled Finish Tone graph
- direct point add/drag/remove with a drag threshold, active-point highlight,
  right-click deletion, and active-curve reset
- UI-only `Point / R / G / B` text segments; every authored curve remains
  active at the same time and edited surfaces show a dot
- `Scene / Log` text segments
- Luma or overlaid RGB histogram behind Point, with the selected channel
  emphasized and other channels dimmed on R/G/B
- subtle channel/complement graph tints: red/cyan, green/magenta, blue/yellow
- editable normalized 0-255 Input/Output coordinates and Log EV equivalents
- graph EV bounds in the secondary sheet

Schema 13 owns a four-component point-curve set. The shared renderer composes
composite before the matching channel in one 4096-sample RGBA32F LUT. Missing
components become identity. Schema-12 `RGB/R/G/B` curves preserve their
prepared and editable responses through component `basePoints`; `Y` is a
compatibility-only pre-curve luma stage with an explicit visible reset.

### View

- compact sampled View Transform mapping curve
- direct black/white EV markers
- Contrast and Saturation lines
- display exposure, black EV, white EV, middle grey, toe, shoulder, Preserve
  Hue, False Color, and sRGB output encoding in the secondary sheet

The graph still calls `EvaluateViewTransformDisplayLuma()`, the CPU scalar
reference of the current shader equation. It does not introduce new display
math.

## Preview and Lower Shelf

Final, Compare, Affected, Delta, Mask, and Highlight Risk no longer consume a
permanent horizontal row. They are preserved in one floating `Preview ...`
menu at the image's upper-right:

- Final clears the local overlay.
- Compare remains disabled until a trustworthy before-state exists.
- Affected and Delta require active Zones edits.
- Mask requires an active spatial or color mask.
- Highlight Risk opens the existing diagnostics.

`Lower` opens a grading placeholder beneath the image column only. It starts
closed for new state, defaults to 180 pixels when opened, is vertically
resizable, and remembers its open state and height. The left editing rail keeps
its full height. No color wheels or grading math were added in this pass.

## Gallery and Persistence

Filmstrip, Full Workspace, and Native Window Gallery hosts continue to share
selection and thumbnail caches. The filmstrip remains below the complete
left-rail/right-column layout.

Optional app-state fields now remember:

- active tool;
- active Point/R/G/B surface;
- left-rail width;
- lower-shelf open state and height;
- filmstrip height;
- last Gallery host; and
- Grid/List preference.

The old `rawLabWorkbenchHeight` reader remains for compatibility, but its value
is no longer used or migrated into the new lower shelf.

## Verification Performed

Passed:

```text
.\build.cmd
cmake --build build --config Release --target Stack StackGraphBehaviorTests -- /m
.\build\StackGraphBehaviorTests.exe
```

Focused automated coverage verifies missing/malformed optional state and
round-trip persistence for the rail, lower shelf, tool, active point-curve
surface, Gallery, and filmstrip.
The existing View Transform CPU/shader tolerance check remains unchanged.

The graph-scope regression additionally verifies the exact Zones and Curve
input boundaries on a real DNG. Each scope is limited to a 192-pixel longest
edge in the product (64 pixels in the focused smoke), and the analysis-free
interactive render is required to return no scope pixels.

Schema-13 behavior coverage verifies four identity defaults, malformed values,
duplicate inputs, endpoint repair, point limits, unknown-field retention,
schema-12 migration for all five legacy modes, compatibility base points,
Scene and Log evaluation, inverted and non-monotonic curves, no cubic
overshoot, composite-before-channel ordering, R/G/B isolation, 4096-sample LUT
accuracy at `2e-4`, and Finish Tone-only cache invalidation.

The Develop OpenGL smoke and real-DNG smoke pass. The real-DNG point-curve
fixture proves visible finite R/G/B effects, zero float-readback change in both
unedited channels, and upstream stage-cache reuse.

A native 1920x1080 pass verified:

- the left rail and text island;
- Exposure, Zones, and Curve switching without recipe edits;
- compact 320-by-240-class Zones and Curve graphs;
- the borderless floating Preview menu and its enablement states;
- lower-shelf open and closed layouts;
- the shelf occupying only the image column;
- the left rail retaining full height;
- the exposure HUD remaining attached to the preview; and
- no Dear ImGui cursor-boundary assertions after the final fixes.

The schema-13 Curve follow-up also passed at 1920x1080 and the current compact
minimum of 1282x832. It verified selector/edited-dot legibility, sampled graph
precision, restrained semantic tints, channel histogram emphasis, context-menu
placement, numeric/EV row fit, and the selection-versus-drag threshold. Exact
1280x720 is blocked by the current native minimum; high-DPI review remains
open because no high-DPI monitor was available.

The native pass restored Exposure as the active tool and closed the lower shelf
before shutdown.

## Not Yet Proven

Do not promote RAW Lab over original RAW until these are completed:

- serialized recipe equality after equivalent edits in both presentations;
- rendered-pixel equality after equivalent edits and save/reload;
- explicit automated hidden-field preservation checks through the Lab commit
  path;
- graph-drag commit-count instrumentation beyond the existing coalesced edit
  path;
- settled full-quality transition checks for every Lab tool;
- true 1280x720, high-DPI, and multi-monitor placement (restored-window
  coverage currently bottoms out at 1282x832);
- real interaction performance traces after the layout change;
- user approval of the left-rail placement; and
- final icon acquisition and visual replacement.

## Boundary

The original left-rail pass changed presentation and optional layout
persistence only. The first July 24 Denoise addition advanced the RAW recipe to
schema 9 so its pre-demosaic settings could be authored from RAW Lab. The
second addition advances the recipe to schema 10 and adds the new classical
post-demosaic stage before authored RAW Exposure. It does not change demosaic,
export, automatic solver behavior, or the original RAW presentation. Later
recipe work reached schema 12 before this Curve pass; the four-component Finish
Tone contract advances it to schema 13 without adding the feature to the older
RAW tab.
