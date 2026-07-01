# Current RAW Tab UI Map

## Render Flow

The RAW tab enters through `EditorModule::RenderRawWorkspaceUI()` in `EditorModuleRawWorkspace.cpp`.

Current high-level flow:

1. Load workspace state and pump background work.
2. Push root padding of `20 x 18`.
3. Render an empty state when no RAW workspace is open.
4. Otherwise render `RenderRawWorkspaceBrowser(...)`.
5. Render lifecycle popups.
6. Render busy/progress overlays for scan, project load/save, and thumbnails.

`RenderRawWorkspaceBrowser(...)` owns both the top workspace controls and the main pane layout.

## Current Main Layout

The tab has three possible spatial modes.

### Right Gallery Expanded

- Left controls pane.
- Center preview pane.
- Right gallery pane.

Current sizing:

- controls width: `clamp(bodyAvail.x * 0.24, 270, 340)`
- gallery width: `clamp(bodyAvail.x * 0.34, 300, 460)`
- preview width: remaining space, minimum `220`

### Right Gallery Collapsed

- Left controls pane.
- Center preview pane.
- Right gallery rail, approximately `46` px.

### Bottom Filmstrip

- Top work area with controls and preview.
- Bottom filmstrip.

Current sizing:

- filmstrip height: `clamp(bodyAvail.y * 0.28, 140, 190)`
- controls width still uses the same `270..340` clamp.

## Current Side Panel Order

`RenderRawWorkspaceControlsPanel(...)` currently renders one long vertical stack:

1. `RAW Controls` heading.
2. Selected filename.
3. Project status.
4. RAW project mode.
5. "First edit creates the project" message.
6. Loading/error/read-only messages.
7. Project action buttons:
   - Open In Graph
   - Save
   - Decompose To Nodes
   - Validate RAW Chain
   - Re-adopt Graph As RAW Recipe
   - Detach From RAW Tab
   - Repair RAW Chain
   - Relink
   - Bake / Embed
8. Auto Base panel.
9. Basic.
10. White Balance.
11. Exposure.
12. Local Range.
13. Finish Tone.
14. Image Analysis.
15. View Transform.
16. Crop & Rotate.
17. Preview & Output.

## Current Auto Base Panel

The Auto Base panel is compact in controls but verbose in text.

Current structure:

- Section heading.
- Three command buttons:
  - Analyze Image
  - Apply Auto Base
  - Revert Auto Base
- Summary paragraph.
- Several "unchanged" lines.
- Suggestions section.
- Local Range suggestions.
- Noise/detail suggestions.
- Exposure suggestion.
- WB suggestion.
- Highlight suggestion.
- Baseline exposure metadata note.

Pressure points:

- It appears before the controls it affects, but its suggestions affect several sections below it.
- It repeats safety text inline.
- It combines commands, status, diagnostics, and suggested edits in one place.
- Some suggestions are actionable and some are not, but they use similar visual weight.
- View Transform auto-fit appears here and again later in the View Transform section.

## Current Local Range Panel

The Local Range panel is one of the strongest parts of the RAW tab.

Current strengths:

- It has a real graph editor.
- It supports target-from-image interaction.
- It can show overlays.
- It supports color qualification and region masks.
- Legacy Local Exposure is hidden unless needed.

Pressure points:

- The graph is good, but surrounding controls are still stacked like a form.
- "Target From Image" mode and color qualification are conceptually linked but spatially separated.
- Overlay mode is labeled "Trust Overlay", which may not tell the user that it is a preview/debug view.
- Color qualification exposes scene-linear RGB values, which are accurate but visually noisy.
- Region Mask is a separate advanced concept but appears as another tree node in the same vertical stream.

## Current Finish Tone Panel

Current strengths:

- It has a graph editor.
- Channel buttons are compact.
- Curve domain controls are available.

Pressure points:

- Graph controls and mode/domain controls are visually similar to ordinary form controls.
- Reset is a full-width button with high visual weight.
- The distinction between Finish Tone and View Transform is technically important but not visually reinforced.

## Current View Transform Panel

Current strengths:

- It exposes the actual display-fit controls.
- Auto ownership is labeled on affected sliders with `[Auto]` and `[Edited]`.
- It has Auto Fit Current Frame.
- It shows current input luma stats.

Pressure points:

- It comes after Finish Tone and Image Analysis, even though it is now the primary readability control.
- It duplicates the "fit" concept already introduced in Auto Base.
- It contains a lot of explanatory text and diagnostics inline.
- The section name may still sound like a color-management conversion rather than the display-rendering step that makes the RAW readable.

## Current Image Analysis Panel

Current strengths:

- It exposes useful technical stats.
- It separates current-frame stats from highlight signals.

Pressure points:

- It is developer/diagnostic-oriented.
- It sits in the main editing stack.
- It uses many lines of low-priority text.

This should probably become a diagnostics drawer or inspector, not a normal editing section.

## Current Top Workspace Controls

`RenderRawWorkspaceBrowser(...)` renders:

- "RAW Workspace" title.
- workspace root path.
- Open RAW Folder, Rescan, Clear.
- Grid/List toggle.
- Right/Filmstrip placement toggle.
- Gallery collapse button.
- scan/thumbnail status lines.

Pressure points:

- The workspace/path/gallery controls compete with editing controls for vertical space.
- Mode toggles are text buttons.
- The path can be long and visually heavy.
- This top area is not clearly separated from the editing cockpit below.

## Key Current Problems

1. **The side panel has no strong hierarchy.**
   Nearly every section uses the same collapsing-header pattern and similar visual weight.

2. **Auto Base is both a command center and a diagnostics feed.**
   This makes it informative, but too large and too hard to scan.

3. **Suggestions are not colocated with their owning controls.**
   Exposure suggestions are far from Exposure, Local Range suggestions are above the Local Range graph, and noise/detail suggestions have no visible owning controls.

4. **The workflow order and pipeline order are mixed without explanation.**
   View Transform is technically late in the pipeline but operationally first for readability.

5. **Diagnostics live in the same stack as editing.**
   Useful for development, distracting for everyday editing.

6. **The controls pane may be too narrow for the amount of information it now owns.**
   A max width of `340` px forces wrapped text and makes graph controls feel cramped.

7. **Project/source management actions occupy prime editing space.**
   They are necessary, but not part of the normal image-adjustment loop.

## Things Worth Preserving

- Local Range graph.
- Finish Tone graph.
- Auto Base reversibility.
- Visible auto ownership markers.
- Source/project safety rules.
- Local Range target-from-image interaction.
- Color qualification as a first-class local targeting feature.
- Collapsed legacy controls.
- Real technical diagnostics somewhere accessible.

