# Phase 6: Dedicated RAW Workspace Panels

## Purpose

Replace the RAW tab's editor-copy scaffold with a dedicated UI-based RAW development workspace.

## Status

Implemented on 2026-06-27; audit hardening completed the same day.

Phase 6 is implemented as the first dedicated RAW development workspace UI. Subsequent stabilization passes made active RAW edits non-blocking, restored active full-quality render settle behavior, hardened project loading/saving, and added graph-quality Finish Tone/View Transform plus Local Range branch-off controls.

Earlier Phase 6 notes listed a selected project-backed preview limitation. That limitation was superseded by later stabilization work: selecting a RAW source queues active preview staging, existing stored projects load through the deferred project-load path, and preview-only selections stage a neutral same-source RAW preview. Thumbnail/placeholder fallback now applies to browser/gallery tiles, unavailable renders, or the short pending state while active preview/project loading is underway. Edited/after thumbnails or recipe-rendered gallery tiles remain later Phase 8 workflow work.

Phase 7 owns managed graph decomposition, structural validation, and full custom graph transition/repair semantics.

Native/manual RAW UI smoke for real RAW Workspace interactions is tracked through `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` and `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

## Prerequisites

- Phase 3 gallery/browsing UI exists.
- Phase 4 recipe model and preview rendering exist.
- Phase 5 project lifecycle can create projects on first image-affecting edit.
- User-facing labels call the opened RAW folder the `Workspace`.
- Base Phase 6 used `Preview & Output` copy instead of graph-stage labels. Later graph-quality finish work added explicit `View Transform` controls to the RAW tab; this phase doc keeps the original panel milestone but should not override the newer branch-off UI.

## Owns

- RAW tab layout.
- Preview priority.
- Recipe-owned control panels.
- Control naming and tooltips.
- Base Tone curve editor plus the current graph-quality `Finish Tone` curve surface.
- Open-in-Graph flow.
- Read-only handling for non-editable states.

## Does Not Own

- Folder scanner internals.
- Thumbnail generation internals.
- Project storage internals.
- Managed decomposition implementation beyond respecting its state.

## Implementation Tasks

1. Build the RAW tab layout:
   - controls on the left,
   - large preview centered,
   - expandable gallery on the right,
   - optional bottom filmstrip,
   - right gallery and filmstrip mutually exclusive.
2. Keep the preview as the visual priority.
3. Add no-folder selected state:
   - recent folders,
   - strong `Open RAW Folder` action.
4. Add recipe-owned panels:
   - `Basic`,
   - `White Balance`,
   - `Exposure`,
   - `Tone`,
   - `Crop & Rotate`,
   - `Preview & Output`.
   Later graph-quality finish work replaces the simple `Tone` editing surface with `Finish Tone` and expands the output/display area with explicit `View Transform` controls.
5. Wire controls to the active image's RAW recipe when editable.
6. Route every image-affecting control through the Phase 5 first-edit project service if the image is still preview-only. Phase 6 calls that service; it does not own project creation logic.
7. Use mathematically honest control names.
8. Add tooltips for controls whose stage/order matters, especially `RAW Exposure EV`.
9. Preview behavior:
   - no project: show the available neutral thumbnail immediately, then upgrade to a neutral RAW render when available,
   - recipe-backed project: recipe render,
   - managed decomposed: managed graph/recipe render,
   - custom graph: custom render with RAW controls read-only.
10. Add `Open In Graph` to show the current per-image project graph.
11. Add clear read-only messaging for `Custom Graph Mode`.
12. Keep `Develop (Advanced Auto)` out of the normal RAW workspace UI. It remains compatibility/advanced graph/debug functionality outside this panel set.
13. Shape the `White Balance` panel for the final feature set: `As Shot`, `Auto`, custom temperature/tint, and gray-point/eyedropper picking. If a backend feature is not ready in the first pass, keep the layout extensible instead of designing a permanently smaller panel.
14. In the base `Tone` panel, use a curve-first design. Later graph-quality finish work uses the editable `Finish Tone` curve canvas as the single active tone editor rather than rebuilding duplicate curve sliders.
15. Explore curve graphs, point graphs, and linked point controls for controls where a relationship is clearer than a standalone slider.

## Suggested Implementation Order

1. Replace the editor-copy RAW tab shell with the dedicated layout.
2. Keep existing Phase 3 gallery behavior working inside the new layout.
3. Add preview states for no folder, preview-only RAW, and recipe-backed project.
4. Add recipe panels with disabled or placeholder controls first.
5. Wire the smallest useful control set to recipe fields.
6. Route every image-affecting control through the Phase 5 first-edit project service.
7. Add tooltips and mathematically honest labels.
8. Add `Open In Graph`.
9. Add read-only UI behavior for non-editable states. Before Phase 7 exists, this can be limited to disabled/unsupported states; Phase 7 fills in full `Custom Graph Mode`.
10. Add UI smoke tests and targeted state tests.

## Handoff To Phase 7

Phase 6 should leave clear UI extension points for managed graph status, read-only custom graph messaging, and `Decompose To Nodes`. It should not implement structural validation or graph sync.

## Preview-Only Graph Behavior

Preview-only RAW selections do not have per-image project graphs yet. For those images:

- `Open In Graph` should be disabled.
- The tooltip/status should say: `Make an edit to create this RAW project first.`
- Clicking `Open In Graph` must not create a project by itself.
- A separate explicit command may create/open a project only if the UI says that it will create a project.

Once a first image-affecting edit creates the project through Phase 5, `Open In Graph` can open the compact `RAW Development` node graph.

Any graph entry point that creates a project must be an explicit action with copy that says a project will be created. Opening or focusing the graph must not be a hidden first-edit trigger.

## UI Requirements

- Do not add a browse/develop mode split.
- Do not make the RAW tab look like the Editor graph.
- Do not make the RAW tab a raw mirror of graph-stage names. The base Phase 6 output/display panel used `Preview & Output`; the later graph-quality finish branch intentionally exposes `View Transform` controls where the compact recipe owns that state directly.
- Do not expose `Develop (Advanced Auto)` in the normal RAW workspace UI.
- Do not expose arbitrary RAW foundation ordering in the normal UI.
- Do not create a project merely because a control panel became visible.

## MVP Control Set

The original Phase 6 MVP RAW control set included:

- `RAW Exposure EV`
- white balance, planned for `As Shot`, `Auto`, custom temperature/tint, and gray-point/eyedropper picking by the final RAW workspace
- tone curve
- crop
- rotate
- preview/output behavior

Current compact recipe controls have since expanded to graph-quality `Finish Tone`, `View Transform`, graph-first `Local Range`, and region-mask controls.

`Color`, `Detail`, and denoise remain later additions unless their recipe behavior is already explicit and validated. Local Range has since become the explicit recipe-backed local balancing surface; legacy `localExposure` is compatibility-only and should not return as a second primary local editor.

## Tone UI Direction

The original `Tone` panel direction was curve-first, not slider-only. Current graph-quality RAW editing uses `Finish Tone` as the active curve canvas. Future tone helpers may be added only when they preserve that single-editor model:

- a curve editor for direct tone shaping,
- sliders for common tone moves only when they visibly modify the curve or recipe state instead of becoming a second independent tone editor,
- future point/curve graph controls where multiple points represent a relationship better than independent sliders.

When a slider changes a curve, the curve should visibly update or the UI should otherwise make that relationship understandable rather than pretending the slider and curve are unrelated controls.

## Validation

- Manual UI smoke test for no-folder, folder selected, preview-only, edited project, and read-only custom graph states.
- Test controls mutate recipe fields.
- Test first control edit creates a project.
- Test `Open In Graph` opens/focuses the current per-image graph.
- Test the white-balance panel can represent the final modes: `As Shot`, `Auto`, custom temperature/tint, and gray-point/eyedropper picking, even if some modes are initially disabled until renderer support lands.
- Test read-only state blocks recipe mutation.

## Done When

- The RAW tab is a real photo-development workspace.
- Controls edit recipes, not arbitrary graph nodes.
- Preview, gallery, and controls are coherent across all core states.
- The old editor-copy RAW tab scaffold is gone or no longer user-facing.
- Phase 7 can add managed decomposition without redesigning the RAW tab layout.

## Completion Notes

Implemented:

- Replaced the RAW browser body with a dedicated layout: recipe controls on the left, preview as the central priority, and either right-gallery or bottom-filmstrip browsing.
- Added recipe-owned panels for `Basic`, `White Balance`, `Exposure`, `Tone`, `Crop & Rotate`, and `Preview & Output`.
- Later graph-quality finish and Local Range passes expanded the active RAW controls with `Finish Tone`, `View Transform`, graph-first `Local Range`, region-mask controls, and a compatibility-only legacy `localExposure` drawer.
- Wired image-affecting controls to `ApplyRawWorkspaceRecipeEditForSelectedSource`, so preview-only RAW selections create a project only when a control edit occurs.
- Added `RAW Exposure EV` with a scene-linear tooltip, white-balance modes for `As Shot`, `Auto`, custom temperature/tint/multipliers, and gray-point coordinates, a curve-first Tone panel later superseded by `Finish Tone`, crop/rotation controls, and preview/output recipe controls later expanded by `View Transform`.
- Added RAW panel state modeling for preview-only, recipe-backed, embedded, invalid, and `Custom Graph Mode` read-only states.
- Added `Open In Graph` behavior that is disabled for preview-only sources with the required tooltip, loads/focuses existing per-image projects, and requests the Editor tab.
- Reused active project render tiles in the RAW preview when available; selected stored projects load asynchronously through deferred project apply, selected preview-only sources stage a neutral same-source RAW preview, and browser/gallery or unavailable-render states fall back to neutral thumbnails/placeholders.
- Tightened first-edit lifecycle behavior so recipe edits on an already-active RAW project do not rebuild and wipe the downstream graph.
- Tightened the RAW tab switch path so it no longer invokes the legacy RAW Decode/Source focus helper while entering the dedicated RAW Workspace tab.
- Cached non-active project recipe metadata for the controls panel so existing project-backed selections are not re-read from disk every frame.
- Preserved existing RAW workspace metadata such as managed/custom sections, read-only reason, embedded RAW data, and future keys when saving active RAW projects.
- Added explicit unsupported/missing `rawWorkspaceMode` handling so malformed or future RAW projects remain invalid/read-only instead of silently becoming editable recipe-backed projects.
- Hardened RAW panel state so unsupported project modes remain read-only even if surfaced as an existing project.
- Staged first-edit project graph creation so active RAW project metadata is committed only after the default compact graph is built successfully.

Deferred:

- Edited/after thumbnails or recipe-rendered gallery tiles for non-selected project-backed RAWs remain Phase 8 workflow work; Phase 6's active selected-source path now stages/loads/renders through the live RAW preview path.
- Managed decomposition, graph validator, `Decompose To Nodes`, graph-to-recipe sync, custom graph repair/re-adoption, and detach actions remain Phase 7.
- Gray-point eyedropper picking is represented in the panel but the picker backend is still deferred.
- Color/detail/denoise panels remain later workflow work unless a specific hook is needed. Local Range is already implemented as the primary local balancing UI, while legacy `localExposure` remains a compatibility drawer only for old project state.

Validation:

- Passed: `cmake --build build --config Debug --target StackGraphBehaviorTests`
- Passed: `build\StackGraphBehaviorTests.exe`
- Passed: `cmake --build build --config Debug --target Stack`
- Passed: automated `Stack.exe` launch and normal close smoke test, exit code 0.
- Passed later automated native-smoke preflight on 2026-06-29, recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Not run: `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 1 hands-on native UI smoke with a real RAW Workspace for startup/loading/source switching and active-preview confidence. Record results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.
