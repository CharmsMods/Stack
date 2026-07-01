# RAW Tab UI Decisions And Remaining Questions

Most questions in this file have now been resolved into implementation defaults. Use `implementation-contract.md` as the controlling source for the first implementation passes.

## Current Leaning / Decisions

The preferred layout direction is now:

- Hybrid, not pure single-stack and not full tabbed-only.
- Compact always-visible top area.
- Main controls grouped together.
- Graph controls grouped together.
- Advanced controls hidden inside compact per-section dropdowns.
- Program/header bar may carry realtime project/render status to reduce repeated text inside the left panel.
- Header bar may show selected RAW/project name, save/load/render state, workspace name/path, warning/suggestion count, and compact Auto Base/User Edited badges.
- The editor should move toward icon-first controls wherever that makes layout more compact without making actions ambiguous.
- The center viewport should be allowed to become a visual workspace for output, masks, info layers, and analysis views, not only a final-image preview.
- View Transform should be integrated as assistive display fitting around an exposure-first workflow, not simply promoted above RAW Exposure as the user's first manual step.

The remaining questions are deferred design choices, not blockers for the first implementation passes.

## 1. Workflow Order vs Pipeline Order

Decision:

- Use workflow grouping in the side panel.
- Keep pipeline order visible in labels/details.
- Use a `Base Light` concept where RAW Exposure is primary and View Transform / Display Fit is an automatic or assistive companion.

Rejected alternative for v1:

- Keep strict pipeline order:
  - RAW Exposure
  - White Balance
  - Local Range
  - Finish Tone
  - View Transform

Tradeoff:

- Strict pipeline order is technically tidy but may reinforce the old problem where View Transform feels like an optional conversion instead of the primary readability fit.
- Continuous or over-aggressive display refitting could hide the visible effect of RAW Exposure edits, so any automatic integration needs clear lock/refit/manual states.

## 2. Suggestions Popout Placement

Decision:

- Suggestions should have a dedicated popout/expander.
- Hovering a suggestion should temporarily preview it when possible.
- Clicking a suggestion should apply it as normal editable recipe values.
- The side panel should not duplicate long suggestion explanations in every section.
- Owning sections should still show compact local markers, ghost points, pending badges, or applied state.

- Put the primary `Suggestions` badge/button in the always-visible RAW top area.
- Let the program header mirror the suggestion count when useful.
- Open the suggestion list as an anchored popout or inline expander from that top-area badge.
- Defer persistent/pinned drawer behavior.

Tradeoff:

- RAW top area is closest to Auto Base and the editing controls.
- Header placement is globally visible but may feel detached from the RAW-specific controls.
- Persistent drawer is strongest for comparing suggestions, but it consumes panel or viewport space.

## 3. How Much Text Should Stay Inline?

Decision:

- Keep state and warnings inline.
- Move long rationale to details/tooltip/diagnostics.

Always visible when relevant:

- RAW Exposure unchanged
- Existing recipe not auto-overwritten
- Highlight risk
- Auto/User ownership
- Missing metadata

Details only:

- percentile statistics
- exact recommendation rationale
- long tooltip-style explanations
- baseline exposure notes unless relevant

## 4. Controls Panel Resizing

Decision:

- Yes.
- Store the width in app state.
- Default wider than today.

- Default around `420 px`, minimum `340 px`, maximum around `520 px`.
- Persist in app state if straightforward.
- If persistence is risky, implement runtime resizing first and persist later.

## 5. Default Open Sections

Decision:

- Source Bar: always visible.
- Auto Base strip: always visible.
- Display Fit / View Transform: open.
- RAW Exposure: compact/open.
- White Balance: compact/open.
- Local Range: open when active or suggested; otherwise compact.
- Finish Tone: open.
- Diagnostics: collapsed.
- Crop/Output: collapsed unless active.

This is the first-pass default. Adjust after real use.

## 6. Gallery Placement Default

Decision:

- Keep the gallery collapsible and preserve both right-gallery and bottom-filmstrip modes.
- Collapse gallery automatically on narrow viewports.

Tradeoff to monitor:

- Right gallery competes with preview width.
- Bottom filmstrip competes with preview height.
- Hidden gallery gives the best editing canvas but slows switching images.

## 7. Local Range Complexity

Decision:

- Keep Local Range graph prominent.
- Keep color qualification close to targeting.
- Keep region masks collapsed unless enabled.

Tradeoff to monitor:

- One large section keeps the workflow together.
- Smaller subsections reduce height but may split related concepts.

## 8. Naming

Decision:

- Use combined names where technical clarity matters:
  - `Display Fit / View Transform`
  - `RAW Exposure`
  - `Local Range`
  - `Finish Tone`

Allowed pattern:

```text
Display Fit
View Transform controls
```

## 9. Noise / Detail

Decision:

- Do not show disabled action buttons.
- Show advisory rows until visible editable RAW-tab controls exist.

- Do not show a permanent placeholder unless real editable controls exist.
- Show only when the image has a relevant high-ISO/shadow-lift/noise-detail advisory.

## 10. Diagnostics Location

Decision:

- One Diagnostics drawer at the bottom or accessible from the Auto Base strip.

- Keep Diagnostics as a collapsed side-panel drawer for v1.
- Separate inspector/popup is deferred.

## 11. First Implementation Aggressiveness

Decision:

- Start with layout and text hierarchy before inventing entirely new widgets.
- Preserve the Local Range and Finish Tone graphs.
- Add new graph-like surfaces only where they solve a real editing problem.

- First implementation should keep the same recipe controls and reorganize presentation/state hierarchy.
- Add new control models only where the existing feature already supports them cleanly, such as Local Range color target display.
