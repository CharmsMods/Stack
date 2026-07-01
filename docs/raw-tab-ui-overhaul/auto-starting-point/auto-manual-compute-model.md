# Auto, Manual, and Computed State Model

Folder note: this file is part of the automatic/foundational RAW control
packet. Read it with `auto-controls-ordering-research.md` and
`auto-starting-point-sampling-design.md` before changing Auto Base, Display Fit,
or starting-point behavior.

## Purpose

This document separates four different ideas that are currently easy to blur in the RAW tab:

1. Values Stack actually changes automatically.
2. Values the user owns manually.
3. Values Stack computes as recommendations, diagnostics, previews, or overlays.
4. Values that should recompute live while editing versus after the user pauses.

This separation should drive UI labels, status badges, icon states, and implementation rules.

For user-facing language, treat `human-workflow-notes.md` as canonical. This document is the technical support model behind those plain labels.

## Definitions

### Auto-Applied

Stack writes a visible recipe/control value without a direct click on that exact control.

Rules:

- Must be visible.
- Must be reversible.
- Must be marked as auto-owned.
- Must stop auto-changing after the user manually edits that value.

### Suggested

Stack computes a proposed change, but the user must apply it.

Rules:

- Show in the suggestions popout/expander.
- Mirror only compact markers, ghost previews, or applied state near the control it affects.
- Do not make it look already applied.
- Store enough rationale for diagnostics/details.
- Applying the suggestion creates normal editable recipe values.

### Advisory

Stack computes a useful warning or recommendation, but there is no safe visible control to apply it yet.

Rules:

- Show as an advisory row or badge, not as a disabled action button.
- Keep the language honest: "suggested" or "risk", not "applied".

### Manual / User-Owned

The user changed the value directly.

Rules:

- Manual values win.
- Auto Base should not keep refitting or rewriting user-owned values unless the user explicitly asks it to.
- Show a compact `Manual`, `Edited`, or lock badge only where it helps.

### Computed Live

Derived information that is cheap enough or necessary enough to update during drag/scrub/editing.

Examples:

- The rendered preview at fast preview resolution.
- Slider values and value readouts.
- Graph curve redraws.
- Local Range selected point readout.
- Active target drag delta.

### Computed After Settle

Derived information that should update after a short pause or after the full render is ready.

Examples:

- Full-resolution preview.
- Auto Base recommendations.
- Local Range suggestion component analysis.
- Highlight and percentile diagnostics.
- Noise/detail risk score.

### Computed On Request

Derived information or actions that should run when the user asks.

Examples:

- Analyze Image.
- Apply Auto Base.
- Revert Auto Base.
- Refit Display after manual edits, if display fitting is not live.
- Generate or refresh expensive mask/info views if not already current.

## Current Code Behavior Observed

The current code already has the beginning of this model:

- Recipe edits call `ApplyRawWorkspaceRecipeEditForSelectedSource(...)`.
- If a control is active, `NoteRawWorkspaceRecipePreviewEdit(true)` enables a fast preview window.
- After a quiet period, `UpdateRawWorkspaceSettledPreviewRender(...)` requests a full-resolution preview.
- Rendering updates `m_RawWorkspaceAnalysis`, View Transform input stats, local suggestion images, overlays, and recommendations from render results.
- `TryApplyRawWorkspaceAutoBaseOnAnalysis()` may auto-apply the initial View Transform fit for a new/default recipe.
- `MarkRawWorkspaceViewTransformUserEdited()` changes View Transform ownership to user-owned when the user edits it.

This is a useful foundation. The UI needs clearer vocabulary around it.

## Proposed Ownership Table

| Feature | Current/Target Type | Recompute Timing | UI State Names | Notes |
|---|---|---|---|---|
| RAW Exposure | Manual control; suggestions only | Preview live while editing; recommendations after settle | `Manual`, `Suggested`, `Blocked` | Do not silently auto-raise dark images. |
| Display Fit / View Transform | Auto-applied initial fit for new/default recipes; then user-owned after edit | Fit on load/analyze/apply; stats after render; refit on request | `Auto Fit`, `Needs Refit`, `Locked`, `Manual` | Pair with RAW Exposure in `Base Light`. Avoid live refit that hides exposure feedback. |
| White Balance | Camera/as-shot default; alternate WB suggested | Recommendation after analysis/settle | `As Shot`, `Suggested`, `Custom`, `Manual` | Camera WB remains default when available. |
| Local Range graph | Manual control; suggestions apply visible graph points | Preview live while editing; suggestions after settle or analysis image update | `Off`, `On`, `Suggested`, `Edited` | Suggestion detail lives in the popout; graph can show ghost points or compact markers. |
| Local Range target sample | User-commanded sampling mode | Sample on click/drag request; preview/point feedback live | `Targeting`, `Sampled`, `Waiting` | Icon should likely be crosshair/eyedropper. |
| Local Range overlays | Computed visual views | After render or on overlay mode change | `Overlay Off`, `Affected`, `Delta`, `Mask` | Should become center visual workspace modes. |
| Finish Tone graph | Manual graph control | Preview live while editing | `Edited`, `Default` | Graph-first section, advanced hidden. |
| Highlight protection | Suggestion; possible visible View Transform apply | After analysis/settle | `Risk`, `Protect`, `Clipped` | Separate sensor clipping from display clipping. |
| Noise/detail | Advisory until visible controls exist | After analysis/settle | `Risk`, `Suggested Only` | Do not show disabled action buttons. |
| Image Analysis | Diagnostics | After full/settled render | `Current`, `Pending`, `Unavailable` | Move out of default editing stack. |
| Crop/Rotate | Manual control | Preview live while editing | `Off`, `On`, `Edited` | Collapsed unless active. |
| Preview/Output | Manual output settings | Preview/render update on edit | `Preview`, `Output` | Not part of main correction loop. |

## Base Light Ownership Model

The `Base Light` section should pair RAW Exposure with Display Fit / View Transform without making either one confusing.

Recommended behavior:

1. On a new/default RAW, Auto Base can apply an initial Display Fit.
2. RAW Exposure remains manual unless the user applies a suggestion.
3. When the user changes RAW Exposure, Local Range, or Finish Tone:
   - preview updates live/fast
   - Display Fit does not continuously rewrite itself by default
   - UI may mark Display Fit as `Needs Refit` if analysis indicates it no longer matches the current frame
4. User can click `Refit Display` to update View Transform from the current frame.
5. If the user edits View Transform controls directly, state becomes `Manual` or `Locked`.

Why not live-refit continuously?

- It may make RAW Exposure feel like it does nothing, because the display mapping keeps compensating.
- It can hide clipping/readability feedback while the user is trying to place exposure.
- It risks Auto Base feeling like it is fighting the user.

## Recompute Timing Rules

### While Dragging / Scrubbing

Do:

- Update preview at fast preview resolution.
- Update visible slider values.
- Update graph drawing.
- Update local target delta while dragging.
- Update simple badges from known current state.

Do not:

- Recompute expensive Auto Base suggestions every tick.
- Re-run local component classification every tick unless it is cheap and already available.
- Refit View Transform every tick by default.
- Replace user-owned values.

### After Quiet / Settled Render

Do:

- Render full-resolution preview.
- Update current-frame stats.
- Update Auto Base suggestions.
- Update highlight risk.
- Update noise/detail advisory.
- Update mask/info overlays if their view is active.
- Mark Display Fit as `Needs Refit` if the displayed fit is stale.

### On Explicit User Action

Do:

- Analyze Image.
- Apply Auto Base.
- Revert Auto Base.
- Refit Display.
- Apply RAW Exposure suggestion.
- Apply WB suggestion.
- Apply Local Range suggestion.
- Show or regenerate a specific visual workspace layer.

## Label / Name Recommendations

These are proposed user-facing names. Technical names can remain in tooltips/details.

| Current / Technical | Proposed Short Name | Detail / Tooltip Name | Notes |
|---|---|---|---|
| View Transform | `Display Fit` or `Display Fit / View Transform` | View Transform display mapping | Use combined name where technical clarity matters. |
| Auto Fit Current Frame | `Refit Display` | Auto fit View Transform from current frame | Shorter and clearer when inside Base Light. |
| RAW Exposure | `RAW Exposure` | Scene-linear exposure before tone/local/display | Keep; it is accurate and important. |
| Scene-linear scale | `Scale` | Exposure multiplier from RAW Exposure | Compact value badge. |
| White Balance | `WB` in compact badges, `White Balance` in section title | Camera/as-shot or custom white balance | Use `WB` only where space is tight. |
| As Shot | `As Shot` | Camera/as-shot metadata | Keep. |
| Local Range | `Local Range` | Scene-EV local tone graph | Keep. |
| Target From Image | `Target` | Sample image tone/color and drag to edit Local Range | Icon can carry most of this. |
| Stop Image Target | `Stop Target` | Exit image targeting mode | Shorter. |
| Trust Overlay | `Overlay` or `View Mask` | Preview Local Range affected tones/masks | Current name is unclear. |
| Affected Tones | `Affected` | Tones affected by Local Range | Compact overlay name. |
| EV Delta Map | `Delta` | Exposure delta map | Compact overlay name. |
| Range Mask | `Mask` | Region/color qualification mask | Compact overlay name. |
| Color Qualification | `Color Limit` or `Color Target` | Limit Local Range by sampled scene color | "Qualification" is accurate but heavy. |
| Limit To Sampled Color | `Use Color Target` | Limit to sampled color family | Shorter. |
| Neutral Guard | `Neutral Guard` | Suppress neutral/low-chroma pixels | Keep unless it tests poorly. |
| Region Mask | `Region Mask` | Manual spatial/luminance mask | Keep. |
| Finish Tone | `Tone Graph` or `Finish Tone` | Final tone curve before display fit | Needs decision; `Tone Graph` is friendlier, `Finish Tone` matches pipeline. |
| Image Analysis | `Diagnostics` | Image analysis and Auto Base rationale | Move out of main stack. |
| Highlight Risk | `Highlight Risk` | Sensor/display clipping risk | Keep. |
| Noise / Detail | `Detail Risk` or `Noise / Detail` | ISO/shadow-lift noise/detail advisory | Use only when relevant. |
| Decompose To Nodes | `Convert to Nodes` | Create managed RAW graph section | More user-friendly. |
| Re-adopt Graph As RAW Recipe | `Use Graph as Recipe` | Convert compatible graph back to RAW recipe | Shorter. |
| Bake / Embed | `Embed` | Embed project/source assets | If bake has special meaning, keep in tooltip. |

## Icon State Recommendations

Icons should carry state as well as action.

Examples:

- Refit Display icon:
  - normal: available
  - highlighted dot: `Needs Refit`
  - locked: display fit locked/manual
- Target icon:
  - crosshair/eyedropper: target off
  - active accent: targeting on
  - spinner/dot: sample pending
- Overlay icon:
  - off: no overlay
  - active accent: center workspace showing overlay
- More icon:
  - contains structural project actions
- Warning icon:
  - opens Diagnostics or relevant section

## Names That Need User Decision

1. `Display Fit / View Transform` vs `Display Fit` with `View Transform` as subtitle.
2. `Base Light` vs `Exposure & Display` vs `Light Setup`.
3. `Finish Tone` vs `Tone Graph`.
4. `Color Qualification` vs `Color Target` vs `Color Limit`.
5. `Image Analysis` vs `Diagnostics`.

## Current Preferred Defaults

- `Base Light` for the combined RAW Exposure + Display Fit area.
- `Display Fit / View Transform` for early UI passes, then test whether `Display Fit` alone is too vague.
- `Local Range` unchanged.
- `Finish Tone` retained for now, with possible `Tone Graph` subtitle.
- `Diagnostics` instead of `Image Analysis`.
- `Overlay` instead of `Trust Overlay`.
- `Target` / `Stop Target` instead of `Target From Image` / `Stop Image Target` where icons are present.
