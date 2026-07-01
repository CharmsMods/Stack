# RAW Side Panel Redesign Spec

## Goal

Replace the current long "Dear ImGui form stack" with a workflow-oriented editing panel that stays compact, explains Auto Base clearly, and keeps advanced diagnostics available without letting them dominate the main editing path.

## Core Decision

The side panel should be organized by editing workflow, not strictly by render pipeline order.

Pipeline order still matters and should be visible in labels or an optional pipeline inspector. But for human editing, the first question is often "can I see the RAW properly?" That means View Transform / Display Fit needs to be near the top even though it happens after RAW Exposure, Local Range, and Finish Tone.

The current preferred direction is a hybrid layout:

- Compact top orientation/status area.
- Main controls grouped together.
- Graph controls grouped together.
- Advanced controls hidden per section.
- Diagnostics and project-management detail moved out of the default editing path.
- Icon-first commands wherever an icon can communicate the action more compactly than a text button.
- A graph/visual-dominant center editing surface that can show output, masks, overlays, and intermediate info layers.

This means the side panel does not have to be a single pipeline-ordered list. It can be a small cockpit: status at the top, direct controls in the middle, and details on demand.

## Proposed Side Panel Regions

### 0. Program Header Bar Responsibilities

Purpose:

- Carry realtime application/project state that is useful everywhere, not just inside the RAW controls pane.
- Reduce duplicated status text inside the side panel.

Good candidates:

- Current project/source name when a RAW is active.
- Save/load/render busy state.
- Active workspace root, shortened.
- Current render/preview state.
- Warning badge count.
- Background task status.
- RAW-specific compact badges when relevant:
  - Auto Base applied
  - User edited
  - Suggestions available
  - Highlight risk

Bad candidates:

- Detailed editing controls.
- Auto Base apply/revert buttons.
- Long file paths.
- Full recommendation rationale.
- Anything the user needs beside the exact control it affects.

Rules:

- The header bar should orient and alert; it should not become a second controls panel.
- A status shown in the header can be expanded in the RAW side panel or diagnostics.
- If header status is global/realtime, the side panel can use shorter local summaries.
- Header status should be terse enough to remain useful while the user is focused on the image.

### 1. Compact RAW Top Area

Purpose:

- Identify the selected RAW.
- Show project state.
- Keep file/project actions available without taking over the panel.
- Show Auto Base/readiness state.

Recommended content:

- Filename, one line, ellipsized, unless already clear in the program header.
- Compact badges:
  - New project / Existing project / Embedded
  - Recipe / Managed graph / Custom graph
  - Read-only / Saving / Loading when relevant
- Small action menu for:
  - Open In Graph
  - Save
  - Decompose To Nodes
  - Validate RAW Chain
  - Re-adopt Graph As RAW Recipe
  - Detach From RAW Tab
  - Repair RAW Chain
  - Relink
  - Bake / Embed

Rules:

- Do not show every project action as a full-width button by default.
- Show destructive or mode-changing actions in a `More` menu or compact management drawer.
- Keep critical loading/error text visible, but collapse normal source metadata.

### 2. Auto Base / Readiness Strip

Purpose:

- Show whether the image has been analyzed.
- Show whether Auto Base applied a view fit.
- Provide Apply/Revert/Analyze commands.
- Summarize suggestions without dumping every rationale.

Recommended content:

- One-line status:
  - `Readable: Auto fit`
  - `Readable: pending analysis`
  - `Readable: user edited`
  - `Existing project: Auto Base not applied`
- Compact badges:
  - `RAW Exposure unchanged`
  - `WB as shot`
  - `3 suggestions`
  - `Highlight risk`
- Primary command row:
  - Analyze
  - Apply
  - Revert
- Details disclosure:
  - Full Auto Base rationale.
  - What changed.
  - What did not change.
  - Confidence details.

Rules:

- The strip should not render all suggestion rationale inline.
- The strip should provide the main suggestion count/entry point, not a long list of suggestion rows.
- Keep safety facts visible as badges, not repeated paragraphs.
- Auto Base should not visually imply it applied Local Range, WB, or denoise when it did not.
- If existing saved recipe blocks auto-apply, state that in one compact line.

### 2A. Suggestions Popout / Expander

Purpose:

- Give suggestions a dedicated place instead of scattering long rows through the panel.
- Let the user preview possible changes without committing them.
- Keep the side panel compact while still making Stack's reasoning discoverable.

Recommended content:

- A compact entry point in the always-visible top area:
  - `3 suggestions`
  - `1 warning`
  - `Auto Base applied`
- A popout or expander containing the active suggestions:
  - title/action, such as `Open shadows`, `Protect sky`, `Brighten foliage`
  - affected section, such as `Base Light`, `Local Range`, `White Balance`
  - confidence or caution only when it changes the decision
  - short rationale
  - optional mask/info preview affordance when relevant

Interaction rules:

- Hovering a suggestion should temporarily preview it when render cost allows.
- Leaving the suggestion should restore the current edit.
- Clicking a suggestion should apply it as normal editable recipe values.
- Hover preview must not dirty the project or permanently mutate the recipe.
- Preview state must be clearly marked as temporary, such as `Previewing: Open shadows`.
- Expensive previews may start with fast preview quality and settle if the user pins or holds the preview.

Relationship to owning controls:

- The popout/expander owns the full suggestion list and rationale.
- Owning control sections should show compact local markers, pending badges, ghost graph points, or applied states.
- Do not duplicate long suggestion explanations in both the popout and the owning section.
- Once a suggestion is applied, the owning section should make the resulting manual/editable value visible.

Implementation default:

- Use a suggestion badge/button in the RAW top area as the primary entry point.
- The program header can mirror the count.
- Open the UI as an anchored popout or inline expander from the RAW top area in v1.
- Persistent/pinned drawer behavior is deferred.

### 3. Main Controls Group

This group owns the global controls that establish the image before graph-based local/tone work.

Recommended order:

1. Base Light: RAW Exposure paired with assistive Display Fit / View Transform state.
2. White Balance.
3. Detail / Noise advisory when relevant.

Rationale:

- The user's normal workflow is exposure-first: raise global RAW Exposure enough to place the image while protecting the brightest important values.
- View Transform should help make the image judgeable, not obscure what RAW Exposure is doing.
- Display Fit / View Transform can still be automatically initialized or refit, but it should feel paired with exposure placement rather than presented as a replacement for exposure work.
- White Balance belongs near the base technical setup.
- Noise/detail is global, but in v1 it is advisory unless visible editable controls exist.

Rules:

- Consider a combined `Base Light` section where RAW Exposure is the primary manual control and Display Fit / View Transform is an assistive mapping strip.
- Keep `View Transform` visible as the technical name, but avoid implying the user must manually operate it before exposure/local/tone work.
- Keep the technical term visible so the UI is not dumbed down.
- Show display-fit ownership and refit state inside this region, not only in Auto Base.
- Show Auto Base exposure/WB suggestion markers inside their owning controls, while full suggestion details live in the suggestions popout/expander.
- Put section-specific advanced controls inside each section's `Advanced` dropdown.

### 4. Graph Controls Group

This group owns the editing surfaces where the graph is the control.

Recommended order:

1. Local Range.
2. Finish Tone.

Rationale:

- These are the RAW tab's strongest custom interaction surfaces.
- Keeping them near each other makes it clear that both are curve/graph-based edits, even though they affect different stages.
- A grouped graph area can be visually larger and less interrupted by ordinary form controls.

Rules:

- The graphs should not be buried below long status text.
- Each graph section should have a compact main row plus an advanced drawer.
- The graph should remain visually dominant inside its section.
- Suggestions that create graph points should be previewable from the suggestions popout/expander and may show ghost points or compact markers immediately adjacent to the relevant graph.
- Treat stacked open graphs as an acceptable power-editor target, not merely as a height problem. Use icon rows, compact summaries, and optional center-surface views to make the height workable.

### 4A. Local Range

Recommended content:

- Local Range graph.
- Target From Image / Stop Target button.
- Overlay mode as compact segmented control.
- Presets as compact buttons or a preset menu.
- Color Qualification as a compact sub-panel with swatch and width/feather controls.
- Region Mask as advanced, collapsed unless active.
- Legacy Local Exposure only when existing recipe requires it.

Rules:

- Keep the graph visible when Local Range is active or when suggestions exist.
- Local Range suggestions should be visible from the suggestions popout/expander and mirrored as compact graph-adjacent markers when useful.
- Color-qualified suggestions should visually show a swatch or color chip.
- Region Mask should not compete with the tone graph unless enabled.
- Color Qualification and Region Mask are section-level advanced drawers unless active.

### 4B. Finish Tone

This section owns Finish Tone.

Recommended content:

- Finish Tone graph.
- Channel/mode buttons as compact icon/text toggles.
- Domain control near the graph.
- Black/white EV graph bounds only when log domain is active.
- Reset as a small secondary action, not a full-width primary button.

Rules:

- Do not hide the graph behind an advanced disclosure.
- Distinguish Finish Tone from Display Fit visually and linguistically.
- Channel/domain controls are main controls.
- Graph bounds, reset behavior, and curve internals are advanced unless needed.

### 5. Detail / Noise

This region is mostly future-facing in the RAW workspace.

Recommended content now:

- Conservative noise/detail recommendations only when relevant.
- "Suggested only" badge if no visible editable controls exist.
- High ISO/shadow-lift warning near Local Range or Detail, depending on context.

Rules:

- Do not show disabled full-width denoise buttons that look broken.
- If a recommendation cannot be applied, render it as an advisory row, not a disabled action.
- Do not create a false sense that hidden denoise has been applied.
- Prefer placing this in Main Controls when a real global detail/noise control exists; until then, render only relevant advisories.

### 6. Geometry

This region owns Crop & Rotate.

Recommended content:

- Collapsed by default unless crop is enabled.
- Summary row:
  - `Crop off`
  - `Crop 84% x 91%, rotate 90`
- Full controls only when expanded.

Rules:

- Crop should not be in the primary default path unless enabled.

### 7. Output

This region owns Preview & Output.

Recommended content:

- Collapsed by default.
- Preview intent and output color space.
- Any future export/soft-proof controls.

Rules:

- Output settings should not interrupt the base RAW editing flow.

### 8. Diagnostics

Purpose:

- Preserve technical transparency.
- Avoid flooding the main editing stack.

Recommended content:

- Image Analysis panel.
- Auto Base detailed rationale.
- Current-frame percentile stats.
- Highlight signals.
- Metadata normalization notes.

Rules:

- Diagnostics should be one collapsed drawer or a tab/inspector, not several inline paragraphs across different sections.
- Diagnostics can be opened automatically when there is a blocking warning.

## Visibility Rules

### Always Visible

- Source bar.
- Auto Base / Readiness strip.
- Display Fit summary.
- RAW Exposure summary.
- White Balance summary.
- Local Range summary or graph when active/suggested.
- Finish Tone graph.

### Visible When Active

- Local Range color qualification.
- Region Mask.
- Crop controls.
- Legacy Local Exposure.
- Noise/detail advisory.
- Project mode management actions.

### Visible When Suggested

- RAW Exposure marker near RAW Exposure; full suggestion in the popout/expander.
- WB marker near White Balance; full suggestion in the popout/expander.
- Local Range marker or ghost point near Local Range; full suggestion in the popout/expander.
- Highlight protection marker near Display Fit or Highlight/Local section; full suggestion in the popout/expander.
- Noise/detail advisory near Detail/Noise, with full rationale in the popout/expander or Diagnostics when needed.

### Diagnostics Only

- Percentile tables.
- Full Auto Base rationales.
- Long confidence explanations.
- Raw metadata notes unless they change behavior.

## Text Rules

### Keep Inline

- Current state.
- Current value.
- Warnings that affect safety.
- What an action will actually change.

### Move To Tooltip

- Detailed explanations of sliders.
- Formula-like rationale.
- Rare terminology definitions.

### Move To Diagnostics

- Percentile details.
- Full recommendation rationale.
- Long metadata notes.
- Internal stage names unless debugging.

### Remove Or Compress

- Repeated "unchanged" lines should become badges.
- Repeated source/path lines should become a compact source bar.
- Disabled action text should become state labels where no action is possible.

## Spacing and Layout Rules

These are design targets. `implementation-contract.md` defines the first implementation defaults.

- Root RAW tab padding: `12-16 px`, not `20 x 18`, unless wider padding is needed at very large viewport sizes.
- Pane gutter: `10-14 px`.
- Controls panel minimum width: `340 px`.
- Controls panel default width: `420 px`.
- Controls panel preferred range: `380-460 px`.
- Controls panel should be user-resizable or adaptive.
- Controls panel should not shrink graph surfaces below useful sizes.
- Local Range graph height: `190-230 px`, with aspect/height tied to panel width.
- Finish Tone graph height: `170-220 px`.
- Preview pane should receive remaining space and remain visually dominant.
- Gallery should be optional/collapsible and should not force the controls panel into cramped width.
- Icon buttons should generally be square and stable-size, roughly `28-34 px` in the side panel depending on theme scale.
- Icon rows should use tight, consistent gaps, roughly `4-8 px`.
- Text buttons should be reserved for ambiguous or high-impact actions where an icon alone would be unclear.
- Every icon command needs a tooltip. Destructive or structural actions also need clear confirmation or menu grouping.

## Center Visual Workspace Rules

The center viewport should evolve from "main output only" into a controllable visual workspace.

View modes to plan for:

- Main output.
- Before/after or base/edited comparison.
- Local Range affected tones.
- Local Range EV delta map.
- Local Range color/region mask.
- Highlight risk / clipping map.
- WB neutral candidate view.
- Noise/detail risk map if implemented later.
- Analysis/info overlays.

Rules:

- The user should be able to choose whether they are inspecting the finished image or the intermediate layers that explain the edit.
- Mask/info views should feel like first-class editor views, not hidden debug overlays.
- These center views can reduce the need for text-heavy explanations in the left panel.
- The gallery should not be the only alternate center content; editing views and masks are equally important.

## Interaction Rules

- If a control is affected by Auto Base, show ownership near that control.
- If a suggestion changes a control, show a compact marker, ghost state, or applied state next to that control; keep the full suggestion in the popout/expander.
- If a suggestion creates a Local Range point, the Local Range graph should immediately reflect the pending/created point.
- If a recommendation cannot be applied because no visible controls exist, show it as advice, not as a disabled button.
- If the user manually edits a control, preserve the existing "user wins" rule and visibly mark that Auto Base no longer owns that value.

## First Documentation-To-Implementation Slices

Use `implementation-passes.md` as the authoritative pass order. The high-level sequence is:

1. Layout shell and resizable/adaptive left panel.
2. Compact RAW top area and project action compaction.
3. Auto Base / readiness strip.
4. Main Controls and `Base Light`.
5. Graph Controls group.
6. Local Range targeting and Color Target UI.
7. Suggestions popout shell.
8. Diagnostics drawer.
9. Center visual workspace toolbar.
10. Icon vocabulary and density polish.
11. UI validation and cleanup.
