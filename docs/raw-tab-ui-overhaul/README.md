# RAW Tab UI Overhaul Notes

## Purpose

This packet began as planning/design documentation and now includes an implementation contract for the first RAW tab UI overhaul passes. The goal is to make the RAW tab easier to understand, faster to operate, and more honest about what Auto Base is doing before we spend too much time testing image behavior through the current UI.

The current RAW tab gained a lot of technical capability quickly: Auto Base, view fitting, suggestion actions, Local Range targeting, color qualification, analysis readouts, project actions, and graph-backed tone controls. The next step is to give those pieces a clearer spatial model.

## What This Is Not

- Not a code-editing pass.
- Not a visual polish pass over the current layout.
- Not a promise to keep the current Dear ImGui stacking order.
- Not a plan to hide technical detail from the user.
- Not an excuse to turn Auto Base into a hidden auto-enhance feature.
- Not permission to implement every exploratory idea at once.

## Files Inspected

Primary RAW tab shell and side panel:

- `src/Editor/Internal/EditorModuleRawWorkspace.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspaceAutoBase.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspaceLocalRange.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspaceAnalysis.cpp`
- `src/Editor/EditorModule.h`
- `src/Editor/EditorModuleTypes.h`

Shared UI helpers:

- `src/Utils/ImGuiExtras.h`
- `src/Editor/Internal/EditorModuleRawControlShared.h`

Relevant Auto Base specs:

- `docs/auto-raw-base/README.md`
- `docs/auto-raw-base/pass-02-auto-base-view-transform.md`
- `docs/auto-raw-base/pass-04-local-range-suggestions-and-color.md`
- `docs/auto-raw-base/pass-05-noise-detail-validation.md`

## Current Design Thesis

The RAW tab should feel like an editing cockpit, not a settings dump.

The strongest existing interaction surfaces are the Local Range graph and Finish Tone graph. They are not just prettier controls; they compress complex editing concepts into direct manipulation surfaces. The rest of the side panel should move toward that same idea:

- summary first
- direct manipulation where possible
- details on demand
- suggestions in a dedicated popout/expander, with compact local markers near the controls they affect
- technical diagnostics available but not constantly occupying the main editing path

## Documentation Map

- `implementation-contract.md`: controlling defaults for the first UI implementation passes.
- `implementation-passes.md`: pass-by-pass implementation order and acceptance criteria.
- `ui-validation-checklist.md`: manual/UI validation checklist for layout and interaction passes.
- `pass-11-validation-notes.md`: automated evidence, manual visual gaps, and accepted deferred behavior from the first UI overhaul implementation set.
- `current-ui-map.md`: how the UI is currently assembled and where the pressure points are.
- `side-panel-redesign-spec.md`: proposed rules for ordering, density, visibility, text, spacing, and control grouping.
- `interaction-ideas.md`: concrete ideas for turning existing controls into better interaction surfaces.
- `auto-starting-point/`: automatic/foundational RAW control research, including ordering, ownership, recompute timing, and the proposed staged sampling model for a one-click starting point button.
- `human-workflow-notes.md`: plain-language workflow model, including suggestion preview behavior.
- `missed-angles-audit.md`: extra design angles checked before the implementation contract was written.
- `open-questions.md`: previous open questions, now mostly resolved into implementation defaults.

## Working Principles

1. Preserve technical accuracy.
2. Prefer compact status badges over repeated explanatory sentences.
3. Keep safety-critical information visible, but move long rationale into details/tooltips.
4. Put suggestion details in a dedicated popout/expander, with compact markers near the feature they change.
5. Keep Auto Base visible and reversible.
6. Make graphs first-class controls, not decorative previews.
7. Use progressive disclosure based on mode, user edits, warnings, and active suggestions.
8. Let the side panel be wider or more flexible if the controls genuinely need it.
9. Do not make the RAW tab feel like a node graph squeezed into a form.
10. Do not remove text merely because there is a lot of it; remove, relocate, or compress it based on user need.

## Current Direction From Design Discussion

Use a hybrid layout:

- A compact always-visible top area for orientation, current image/project state, Auto Base status, and active warnings.
- Collapsible workflow sections below.
- Main/global controls grouped together.
- Graph-based editing surfaces grouped together, especially Local Range and Finish Tone.
- Advanced controls hidden inside compact per-section dropdowns instead of becoming separate always-visible sections.
- The application/header bar may carry realtime project or render state so the left RAW panel does not need to repeat every piece of status text.
- The RAW UI should move heavily toward icon-first controls, with text kept for values, truly clarifying labels, and tooltips/accessibility labels for icon commands.
- The center editing surface should become more visual/graph dominant over time, including optional views for masks, analysis/info layers, and other intermediate editing layers, not only the final main image.
- View Transform should support the user's exposure-first workflow as automatic or assistive display fitting. It should not be framed as replacing RAW Exposure, Local Range, or Tone Curve as the main editing path.
- The plain-language `Auto`, `Manual`, `Suggested`, `Advisory`, `Live`, and `Settled` model in `human-workflow-notes.md` is the canonical wording for the redesign.
- Suggestions should use a dedicated popout/expander pattern with hover preview and click-to-apply, while owning sections show compact local markers or applied state.
- Before implementation, fold in the missing-angle audit around preview state, undo/revert behavior, Display Fit staleness, center workspace modes, and suggestion-chip/popout wording.

The finalized implementation defaults now live in `implementation-contract.md`. The design docs are still useful context, but implementation should not treat every exploratory note as required scope.

## Important Caution

Some current text is noisy, but some of it prevents real misunderstandings. For example, the UI repeatedly says RAW Exposure is unchanged because Auto Base is intentionally fitting View Transform first. That can become a compact status badge, but it should not simply disappear.
