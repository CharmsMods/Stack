# RAW Tab Interaction Ideas

This file collects possible interaction changes. These are not final requirements yet.

## 1. Auto Base As A Readiness Strip

Current problem:

- Auto Base uses a lot of text because it is trying to be honest about safety and reversibility.
- That honesty is good, but the panel reads like a log.

Idea:

Render Auto Base as a compact readiness strip:

```text
Readable: Auto fit    RAW Exposure unchanged    WB as shot    3 suggestions
[Analyze] [Apply] [Revert] [Details]
```

States:

- `Pending analysis`
- `Ready`
- `Auto fit`
- `User edited`
- `Existing recipe`
- `Blocked`

Details drawer:

- Full summary.
- What changed.
- What did not change.
- Suggestion rationale.
- Confidence values.

Pushback:

- We should not remove the safety information entirely. The current verbosity is ugly, but the underlying facts are important.

## 2. Suggestions Popout With Local Markers

Current problem:

- Suggestions live together in Auto Base, then the controls they affect appear below.
- Fully duplicating suggestions beside every control can make the panel noisy.

Idea:

Use a dedicated suggestions popout or expander as the primary place for suggestion review:

```text
3 suggestions
```

The compact top area should expose that as a badge/button. Opening it shows the active suggestions with enough detail to decide what to preview or apply.

Owning sections should still show small local markers or state, but should not duplicate every long explanation:

- RAW Exposure can show a pending/applicable marker if a base-light suggestion exists.
- White Balance can show a pending/applicable marker if an alternate WB suggestion exists.
- Display Fit / View Transform can show highlight or fit suggestions.
- Local Range can show suggested graph points or a small marker near the graph.
- Detail / Noise can show advisory state when relevant.

Auto Base strip can still show a count:

```text
3 suggestions
```

Full suggestion detail should live in the popout/expander. The owning editable surface should show only the compact state needed to explain what changed or what can be applied.

Preferred interaction:

- Hover a suggestion to temporarily preview it.
- Move off the suggestion to return to the current edit.
- Click a suggestion to apply it as normal editable recipe values.
- Pin/compare may optionally keep a preview visible while deciding.

Hover preview rules:

- Hover preview is temporary.
- It must not dirty the project.
- It must show clear "Previewing" state.
- Leaving hover cancels it.
- Clicking applies it as normal editable values.
- Expensive previews can use fast preview first.

Placement still to decide:

- Popout anchored to the RAW top area suggestion badge.
- Popout anchored to a header-bar suggestion count.
- Expander directly under the Auto Base / readiness strip.
- Floating side-panel drawer that can stay open while editing.

## 3. Display Fit / View Transform Reframe

Current problem:

- "View Transform" sounds like a conversion utility.
- In practice, Auto Base currently uses it as a readability tool.
- The user's manual workflow is usually exposure-first: raise global RAW Exposure until important highlights are protected, then use Local Range and Tone Curve, while View Transform is mostly ignored.

Idea:

Do not simply move View Transform above RAW Exposure as if it replaces exposure. Instead, pair it with exposure in a `Base Light` area:

```text
Base Light
RAW Exposure +0.00 EV      Display Fit Auto
```

Summary row:

```text
RAW Exposure +0.35 EV    scale x1.27    Display Fit: Auto Base
```

Controls:

- RAW Exposure
- Auto Fit Current Frame
- Refit Display
- Lock/Manual Display Fit
- View Transform core controls in Advanced
- Input stats in Diagnostics

Possible micro interaction:

- A small horizontal EV range strip showing black anchor, middle grey, white anchor, and highlight shoulder pressure.
- The strip could show how RAW Exposure moves scene values while Display Fit maps them into the screen range.

Pushback:

- Do not rename it only to "Display Fit" and lose the technical term. The user wants technical accuracy, and "View Transform" is the correct pipeline concept.
- Do not make Display Fit so automatic that exposure edits become visually invisible. If the transform refits continuously, the user may lose feedback about what RAW Exposure actually changed.

## 4. RAW Exposure Summary + Caution

Current problem:

- RAW Exposure is important but easy to overuse on dark/bright-sky images.

Idea:

Show a compact summary:

```text
RAW Exposure  +0.00 EV    scale x1.00
```

If Auto Base has an exposure suggestion:

```text
Suggestion: Raise +0.35 EV    highlight-safe
[Apply]
```

If blocked:

```text
Suggestion withheld: highlight risk
```

Avoid:

- Treating RAW Exposure as the first "make it brighter" control.

## 5. White Balance Summary

Current problem:

- WB controls are compact enough, but suggestions are elsewhere.

Idea:

Summary row:

```text
WB As Shot    Camera metadata
```

When alternate candidate exists:

```text
Suggested WB: Gray World    confidence 87%    [Apply]
```

Custom controls appear only when mode is custom.

Gray point picker remains hidden or disabled until implemented, but should not occupy full visual weight.

## 6. Local Range Control Surface

Current problem:

- The graph is good, but the surrounding controls are still a long list.

Idea:

Top of section:

```text
Local Range    On    Strength 1.00
[Target] [Overlay: Affected] [Preset]
```

Graph remains central.

Below graph:

- selected point compact row
- add/remove point controls
- target sample row when active

Color qualification:

```text
Color Qualification    On    [swatch] green/yellow-green
Width 0.26    Feather 0.35    Neutral Guard 0.10
```

Region mask:

- collapsed unless enabled
- show a one-line summary when enabled

Suggestion integration:

- Suggested Local Range actions are reviewed in the suggestions popout/expander.
- The Local Range section mirrors them as compact graph-adjacent markers, ghost points, or applied state.
- If the section is active, small EV/color chips can appear near the graph, but they should not duplicate the full rationale from the popout.
- Applying one should immediately select or highlight the created point.

## 7. Color Qualification Swatch

Current problem:

- Scene-linear RGB numbers are technically correct but not very approachable.

Idea:

Show:

- swatch
- target type, e.g. `sampled green`
- numeric RGB only in tooltip/details

Important caveat:

- Scene-linear HDR values can exceed 1.0, so the swatch may need normalized display mapping. The UI should not imply the swatch is exact display color.

## 8. Finish Tone Graph

Current problem:

- The graph is good, but reset and mode controls have too much ordinary-button weight.

Idea:

- Mode/channel row as compact segmented control.
- Domain selector as a small control near graph title.
- Reset in a small overflow action.
- Log black/white EV only visible for log domain.

Potential addition:

- graph background marks for shadows/mids/highlights.

## 9. Noise / Detail As Advisory Until Real Controls Exist

Current problem:

- Disabled full-width buttons look like broken actions.

Idea:

Render recommendations as advisory rows:

```text
High ISO detail risk    Suggested: mild chroma cleanup, reduce sharpening
No RAW-tab detail controls yet
```

or:

```text
Shadow noise risk after +1.00 EV local lift
```

When editable controls are eventually added:

- convert advisory rows into actual applyable suggestions
- show values beside visible controls

## 10. Image Analysis As Inspector

Current problem:

- Percentile stats are useful but developer-like.

Idea:

Move to a Diagnostics drawer:

```text
Diagnostics
Current-frame stats
Highlight signals
Metadata
Auto Base rationale
```

Auto-open only when:

- analysis failed
- highlight risk blocks exposure
- metadata is missing
- debugging mode is enabled

## 11. Source And Project Actions

Current problem:

- Project actions occupy prime vertical space.

Idea:

Source bar:

```text
IMG_0005.CR3    Existing project    Recipe-backed    [Save] [...]
```

Overflow menu:

- Open In Graph
- Decompose To Nodes
- Validate RAW Chain
- Re-adopt
- Detach
- Repair
- Relink
- Bake / Embed

Rules:

- Show `Save` directly only when there are unsaved changes or active project context.
- Show dangerous/structural actions behind the menu.

## 12. Layout And Pane Width

Current problem:

- Controls max out at 340 px, which is tight for graphs and explanations.

Ideas:

- Make controls pane resizable.
- Store width in app state.
- Default around 400 px.
- Clamp to 340 minimum and maybe 520 maximum.
- When viewport is narrow, prioritize preview plus selected section; collapse gallery first.

Pushback:

- Making the controls wider is not enough. Without hierarchy, it will only make the long form more comfortable, not clearer.

## 13. Main Controls And Graph Controls As Separate Groups

Current direction:

- Keep the hybrid layout.
- Group main/global controls together.
- Group graph-based controls together.
- Hide advanced controls inside each section rather than creating a separate advanced area.

Possible structure:

```text
Top Area
Auto Base / status / active warnings

Main Controls
Base Light: RAW Exposure + Display Fit / View Transform
White Balance
Detail / Noise advisory when relevant

Graphs
Local Range
Finish Tone

Other
Crop / Rotate
Output
Diagnostics
```

Why this may work:

- The main controls are global setup decisions.
- The graphs are direct manipulation surfaces and deserve more visual continuity.
- Advanced controls stay near their parent concept but do not consume default height.

Risk:

- Keeping the graphs together may slightly obscure pipeline order because Local Range happens before Finish Tone, while Display Fit happens after both. The UI needs subtle stage labels or a compact pipeline indicator so this does not become misleading.
- Making Display Fit too automatic may hide the feedback from RAW Exposure changes, so the Base Light design needs clear display-fit states such as Auto, Locked, Manual, and Refit available.

## 14. Program Header Bar As Status Surface

Current direction:

Use the program header bar for realtime state that is not itself an editing control.

Good uses:

- selected RAW/project name
- project save/load/render state
- workspace path or shortened workspace name
- warning/suggestion count
- background task progress

Avoid:

- moving actual image adjustment controls into the header
- making the header a second diagnostics panel
- long explanatory text

Why this may work:

- It frees the left panel from repeating file/project status on every scroll position.
- It keeps persistent state visible even when the side panel is scrolled down into Local Range or Finish Tone.

## 15. Icon-First RAW UI

Current direction:

Move heavily toward icons instead of text buttons wherever the action is familiar, repeated, or spatially constrained.

Good icon candidates:

- Analyze / refresh.
- Apply.
- Revert / undo.
- Save.
- More menu.
- Open in graph.
- Target from image / eyedropper/crosshair.
- Stop target.
- Overlay visibility.
- Reset.
- Collapse/expand.
- Pin/lock.
- Before/after.
- Mask view.
- Warning/details.

Keep text or icon+text for:

- unfamiliar commands
- destructive/structural project actions
- first-time feature discovery
- ambiguous technical actions

Spacing implications:

- Many current full-width buttons can become one-row icon toolbars.
- Section headers can carry icon buttons on the right.
- Suggestion actions can become compact icon+label rows or badges.
- Graph sections can reclaim height now spent on button text rows.

Rules:

- Every icon needs a tooltip.
- The icon vocabulary must be consistent across RAW, graph, and develop UI.
- Use stable square hit targets; compact does not mean tiny.
- If an icon cannot be understood after a tooltip once, keep text.

## 16. Center Visual Workspace / Layer Gallery

Current direction:

The center viewport should support more than "show final image." It should become a visual editing workspace where the user can choose which image, mask, or info layer is being inspected.

Possible modes:

- Final output.
- Original/base vs current.
- Local Range affected tones.
- Local Range EV delta.
- Color qualification mask.
- Region mask.
- Highlight risk/clipping.
- Auto Base analysis overlays.
- Noise/detail risk.
- Future subject/mask layers.

Possible presentation:

- A compact icon toolbar over or above the viewport.
- A gallery/strip of available views.
- Split view or single active view.
- Pin one mask/info layer while adjusting controls.

Why this matters:

- It lets technical information become visual instead of textual.
- It makes graph and mask editing feel central rather than side-panel-only.
- It supports the goal of a visual-dominant RAW editor.
