# RAW Tab UI Missed Angles Audit

## Purpose

This document captures design angles that were either missing from the RAW tab UI overhaul packet or present only implicitly. The selected defaults from this audit have been folded into `implementation-contract.md`; keep this file as supporting rationale and a source of later follow-up work.

The current direction is still right:

- hybrid layout
- compact always-visible RAW top area
- main controls grouped together
- graph controls grouped together
- suggestions in a dedicated popout/expander
- compact local markers near affected controls
- diagnostics available without dominating the edit
- icon-first controls with technical terms preserved where they matter

The items below are the places that needed extra thinking before implementation. The first-pass decisions are now in `implementation-contract.md`.

## 1. Suggestion Preview State Machine

The docs say hover previews a suggestion and click applies it. That is directionally good, but implementation needs a precise state model so previews do not accidentally dirty the project or race with render results.

Needed rules:

- Hover preview is temporary recipe state, not a committed edit.
- Hover preview must never mark the project dirty.
- Hover preview must be visually labeled, such as `Previewing: Open shadows`.
- Moving away, pressing Esc, switching suggestions, or closing the popout cancels the preview.
- Clicking applies the suggestion as normal recipe values and creates a normal undo step.
- If a render result returns after the preview has been canceled, it must be ignored if its preview token no longer matches the active preview.
- Expensive previews can start with fast preview quality, then settle only if pinned or held.
- Pinned preview is still not committed until applied.

Open design decision:

- Should hover preview start instantly, or after a short delay so the UI does not flicker while the pointer crosses the list?

Recommended default:

- Use a short hover delay for image-changing previews.
- Let simple graph ghost markers appear instantly.

## 2. Undo, Revert, and Applied Suggestions

The docs clearly say applied suggestions become normal editable values, but they do not yet define how this fits with undo/history and Auto Base revert.

Needed rules:

- Applying one suggestion should be one undoable recipe edit.
- Reverting Auto Base should restore only the values Auto Base actually changed automatically, not every later manual edit.
- Applied suggestions should not be silently removed from history when Auto Base is reverted.
- If an applied suggestion changed a control later edited by the user, Auto Base revert must not overwrite the user's later manual value unless the user explicitly asks for a full recipe restore.
- Applied suggestions can remain visible as `Applied` in the suggestions popout until the source or analysis changes.
- Dismissing a suggestion should hide that suggestion for the current source/analysis hash without changing recipe values.

Recommended default:

- Treat `Revert Auto Base`, `Undo`, `Applied suggestion`, and `Dismissed suggestion` as separate concepts in the UI and state model.

## 3. Persisted Auto State Across Sessions

The Auto Base implementation docs intentionally keep some ownership state editor-local at first. The UI redesign should acknowledge what happens after reopening a project.

Potential issue:

- If the user saves a recipe after Auto Base set Display Fit, the values persist, but the session-only `Auto` marker may disappear on reload.
- If the marker disappears, the user may not know whether Display Fit is camera/default/manual/Auto Base.

Needed rules:

- Decide which state is persisted as recipe/project metadata and which state is session-only.
- At minimum, persisted recipes should distinguish `Existing recipe` from `Auto Base can safely auto-apply on load`.
- The UI should not claim `Auto Base applied` after reload unless that fact is known.
- If ownership metadata is absent, use neutral language like `Display Fit set` or `Existing recipe`.

Recommended default:

- Keep recipe math fields as normal values.
- Add optional project-side provenance later for user-facing badges, not for rendering behavior.

## 4. Suggestion Scope Per Source

The RAW workspace has gallery and filmstrip workflows. Suggestions are image-specific, but the UI docs mostly describe a single selected image.

Needed rules:

- Suggestions belong to a source key plus analysis hash.
- Switching images should immediately switch suggestion state, preview state, diagnostics, and local markers.
- A hover preview on image A must cancel when image B is selected.
- The header suggestion count should always refer to the currently selected RAW unless a batch mode is explicitly active.
- Gallery thumbnails may show tiny badges for warnings/suggestions later, but that should not make the main count ambiguous.

Recommended default:

- Keep all suggestion actions single-image in the first UI overhaul.
- Defer batch Auto Base actions until the single-image state model is stable.

## 5. Center Visual Workspace Contract

The docs correctly say the center viewport should show masks/info layers, not only final output. The missing piece is a contract for what the user is looking at and how that state is controlled.

Needed rules:

- The viewport should always have one active view mode, such as `Final`, `Before/After`, `Affected`, `Delta`, `Mask`, `Highlight Risk`, or `Diagnostics`.
- Overlay/mask modes should have stable icon buttons and tooltips.
- A suggestion hover may temporarily switch or augment the center view, but it should restore the previous view afterward unless pinned.
- The current viewport mode should be visible without a long text explanation.
- Local Range overlay modes and center workspace modes should be unified or deliberately bridged so the same concept is not controlled in two places.
- Color and mask overlays need legends or minimal labels when color encodes meaning.

Recommended default:

- Treat Local Range overlays as early center workspace modes.
- Keep the left panel control row compact, but make the center view mode explicit.

## 6. Color And Luminance Targeting Interaction

The original user goal was not only "make suggestions." It was also to drag on the image and lift a luminance range while optionally limiting that lift to the selected color family, such as bright green grass without bright blue sky.

The docs mention color qualification, but the interaction model should be more explicit.

Needed rules:

- Targeting should sample luminance and color together when color limiting is enabled.
- The sampled point should show both target EV and color target/swatch.
- Drag up/down should primarily change Local Range delta EV.
- Horizontal or modifier interaction could adjust width, color width, or feather later, but v1 should keep it simple unless the current interaction already supports it cleanly.
- The user should be able to toggle `Use Color Target` without losing the sampled color.
- The mask view should make it obvious whether the change is affecting tones only, color only, or tone plus color.
- The UI must state or imply that the swatch is display-mapped for preview, not exact scene-linear color.

Recommended default:

- Keep `Target` as the main action.
- Show a compact row under the Local Range graph when a sample exists:

```text
Target EV -2.1    Delta +0.5    Color Target [swatch] On
```

## 7. Display Fit Staleness And Refit Thresholds

The docs say Display Fit should not live-refit continuously, and can show `Needs Refit`. The missing angle is when that state appears.

Needed rules:

- Define which edits can make Display Fit stale:
  - RAW Exposure
  - Local Range
  - Finish Tone if the fit source is post-tone, or not if the fit source is pre-tone
  - White Balance if it materially shifts luminance/channels
- Define a threshold so `Needs Refit` does not appear after every tiny movement.
- `Needs Refit` should be advisory, not a warning, unless clipping/readability is clearly bad.
- `Refit Display` should be explicit and reversible.
- If Display Fit is manual/locked, do not nag with `Needs Refit`; show a quieter stale/stats state in diagnostics.

Recommended default:

- Mark `Needs Refit` after settled analysis if current-frame median or white percentile moved meaningfully from the applied fit hash.
- Do not mark it during live drag.

## 8. Error, Pending, and Unavailable States

The current docs define good happy-path states, but the UI needs honest states for missing analysis, fallback analysis, unsupported metadata, or worker failures.

Needed states:

- `Analyzing`
- `Analysis unavailable`
- `Using fallback stats`
- `Metadata incomplete`
- `Preview pending`
- `Suggestion stale`
- `Source mismatch`
- `Render failed`

Needed rules:

- Unsafe suggestions should be suppressed when the analysis stage is wrong or unavailable.
- Suppressed recommendations can appear in Diagnostics as `withheld`, but should not look like broken buttons.
- The suggestions badge should not count suppressed or unsafe recommendations as applyable suggestions.
- Advisory counts and suggestion counts should remain separate.

Recommended default:

- Top area shows applyable suggestion count.
- Diagnostics shows withheld/suppressed reasoning.

## 9. Accessibility And Icon-First Constraints

The UI direction is icon-first, which is good for density, but icon-only UIs fail quickly without rules.

Needed rules:

- Every icon button needs a tooltip and accessible label.
- Toggle icons need visible active/inactive state beyond color alone.
- Warning, mask, and graph colors must not rely on hue alone.
- Hit targets should remain stable and large enough for repeated editing.
- Keyboard focus order should follow workflow order.
- Esc should close/cancel targeting, popouts, and temporary previews in a predictable order.
- The same icon should mean the same action across RAW, graph, and future develop UI.

Recommended default:

- Define a small RAW icon vocabulary before implementation, even if the first pass uses existing ImGui buttons internally.

## 10. Responsive Layout And Scroll Anchoring

The docs mention a resizable panel, but not how the UI behaves when the panel is narrow, short, or scrolled deep into graphs.

Needed rules:

- The compact RAW top area should remain visible or easily returnable when the panel scrolls.
- Popouts should not open off-screen.
- Graph heights should have stable min/max sizes and should not collapse unpredictably.
- If both graphs are open, the UI should preserve context with sticky section headers or clear section boundaries.
- On narrow windows, gallery collapses before the preview or graph controls become unusable.
- The side panel width should be persisted per workspace or globally.

Recommended default:

- Make the top RAW status area sticky within the left panel if Dear ImGui makes that reasonably maintainable.
- If sticky is too costly, provide a compact persistent header bar mirror for source/suggestion/warning state.

## 11. Local Range Suggestion Conflicts

The Auto Base pass docs say not to overwrite existing Local Range points, but the UI overhaul docs should surface the user interaction.

Needed rules:

- If a suggestion overlaps an existing point, do not silently merge.
- Show a compact conflict state:

```text
Overlaps existing Local Range point
```

- Offer choices only if the merge operation is safe and understandable:
  - add anyway
  - merge with existing
  - cancel
- If no conflict UI exists yet, suppress apply and explain in the suggestion details.

Recommended default:

- For v1, do not mutate a nearby existing point. Show the suggestion as advisory/details-only until conflict handling exists.

## 12. Validation For The UI, Not Just The Math

The Auto Base docs have strong math and behavior tests. The UI overhaul needs its own validation checklist.

Needed validation:

- Screenshot or manual checks for narrow, normal, and wide layouts.
- Check top area density with long filenames and long paths.
- Check icon rows with active, disabled, warning, and loading states.
- Check suggestion popout placement near top, middle, and bottom scroll positions.
- Check hover preview cancellation and no dirty state.
- Check graph readability with both Local Range and Finish Tone open.
- Check center workspace modes for masks/highlight overlays.
- Check low-confidence/no-suggestion images so the UI does not feel empty or broken.

Recommended default:

- Add a UI validation checklist before implementation starts, separate from Auto Base algorithm tests.

## 13. Cross-Doc Mismatch To Reconcile

The newer UI docs now prefer a suggestions popout/expander with local markers. The original research report and some older wording use `suggestion chips` near Auto Base or near controls.

This is not a behavior contradiction if interpreted correctly:

- Old `chip` means a visible suggestion action.
- New UI direction says the primary list of those actions lives in the popout/expander.
- Owning controls show compact markers, ghost points, or applied state.

Before implementation, keep the Auto Base pass docs annotated so agents do not build both a full chip list and a full popout, or scatter long suggestion rows throughout the panel.

Recommended wording:

```text
Suggestions are reviewed in the suggestions popout/expander. Affected controls show compact local markers, ghost graph points, or applied state. Do not duplicate full rationale in each section.
```

## Priority Summary

Highest priority before implementation:

1. Suggestion preview state machine.
2. Undo/revert/applied suggestion rules.
3. Display Fit staleness/refit behavior.
4. Center workspace mode contract.
5. Cross-doc reconciliation around suggestion chips versus popout.

Important but can trail the first layout pass:

1. Persisted Auto Base provenance.
2. Per-source suggestion scope and gallery badges.
3. Accessibility/icon vocabulary.
4. Responsive/sticky behavior.
5. UI validation checklist.

Worth keeping visible because it came from the original user goal:

1. Tone plus color targeting in Local Range.
2. Clear color-mask preview.
3. Foliage/sky separation without pretending the color swatch is exact scene-linear display color.
