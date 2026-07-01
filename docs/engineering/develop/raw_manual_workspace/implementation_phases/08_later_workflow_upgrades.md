# Phase 8: Later Workflow Upgrades

## Purpose

Add higher-level workflow features after the core RAW workspace is stable.

## Prerequisites

- Phases 1-7 are stable enough that folder browsing, recipe editing, project lifecycle, RAW panels, and managed/custom graph ownership all work.
- The current active native-smoke gate in `../CURRENT_HANDOFF.md` and `../NATIVE_RAW_UI_SMOKE_RESULTS.md` is recorded before starting any new broader Phase 8 workflow upgrade.
- Phase 7's native/manual managed-decomposition smoke rows have passed and are recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- If the upgrade depends on Local Range behavior, Phase 8A's Scenario 1 plus Scenario 6 through Scenario 9 native/manual smoke rows have passed and are recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Each upgrade has a clear owner and validation plan before implementation starts.

Current gate, 2026-06-29:

- Broader Phase 8 workflow upgrades should not start yet because the interactive native smoke rows are still pending.
- The already-implemented Phase 8A Local Range branch remains a branch-off stabilization effort until Scenario 1 and Scenario 6 through Scenario 9 are recorded as passing evidence.
- Phase 7 remains incomplete until Scenario 2 through Scenario 5 are recorded as passing evidence.
- Use `CURRENT_HANDOFF.md` and `../NATIVE_RAW_UI_SMOKE_RESULTS.md` as the resume authority before starting any new Phase 8 upgrade.

## Owns

- Edited/after thumbnails.
- Before/after hover behavior.
- Copy/paste/sync settings.
- Batch operations.
- Optional denoise/detail stages.
- Local Range follow-up extensions and legacy `localExposure` compatibility.
- Suggested-settings or helper automation that does not revive the old Auto RAW workflow.

## Does Not Own

- Core folder scanning.
- Core thumbnail generation.
- Core project lifecycle.
- Initial recipe/node architecture.
- Initial managed decomposition rules.

## Upgrade Areas

### Edited/After Thumbnails

1. Generate edited thumbnails for RAW files with projects.
2. Store them separately from neutral thumbnails or mark them clearly.
3. Default gallery display can show edited/after thumbnail.
4. Hover can reveal neutral/before thumbnail.
5. Staleness must account for recipe/project changes.

### Copy/Paste And Sync Settings

1. Add copy settings from selected RAW.
2. Add paste settings to another RAW, including clear handling for preview-only targets.
3. Add multi-select sync only after single-image paste is stable, and treat it as a batch operation.
4. Do not copy source identity fields.
5. Pasting onto preview-only images should auto-create projects, but only as an explicit batch action with visible progress and clear completion/failure feedback.
6. Batch paste/sync must not assume project creation is instant; creating projects for many images can take time.

### Batch Workflow

1. Batch thumbnail repair/regeneration.
2. Batch project discovery repair.
3. Batch settings sync.
4. Batch export if export workflow exists.
5. Batch project creation caused by settings paste/sync, with progress and cancellation/error handling when possible.

### Denoise, Local Range Extensions, And Local Exposure Compatibility

1. Add as explicit recipe capabilities or deliberate advanced graph stages.
2. Do not hide them inside automatic `Develop` / Auto RAW behavior.
3. Document ordering and whether each stage is reorderable.
4. Add validator support before allowing managed graph reordering.

Current scope split:

- Denoise/detail remain future explicit recipe or advanced graph stages.
- Local Range is already implemented as the graph-first compact recipe local balancing UI and should now be extended only through scoped follow-up passes.
- Legacy `localExposure` remains readable/renderable compatibility state and should not become a parallel primary editor again.

Detailed Local Range plan:

- Use `08A_local_range_tone_equalizer.md` for the scoped implementation sequence that replaces vague Local Exposure sliders with graph-driven Local Range/tone equalizer behavior.
- Passes 1-8 of that document are implemented as of 2026-06-29. Future Local Range extensions should still be implemented pass-by-pass and should not jump directly to UI before the data contract, renderer math, graph UI, and trust overlays are working.
- Use `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 1 plus Scenario 6 through Scenario 9 when Local Range passes call for native/manual real-RAW validation.
- Automated native-smoke preflight passed on 2026-06-29 and is recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Record Scenario 1 plus Scenario 6 through Scenario 9 results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md` before calling the Local Range branch natively verified.

Local Range and legacy local exposure status, 2026-06-29:

- Compact recipe-backed RAW Workspace projects now serialize a schema-v5 `localRange` block. Local Range is the current graph-first local balancing UI for new/default projects.
- The older default-disabled `localExposure` recipe block remains readable/renderable for project compatibility, but it is no longer the discoverable primary local adjustment for untouched projects.
- Compact RAW rendering preserves legacy `localExposure` when active, then applies Local Range before recipe-owned `ToneCurveLayer` Finish Tone and recipe-owned `ViewTransformLayer`.
- Legacy `localExposure` state appears only through a compatibility drawer when a project carries active or non-default legacy data.
- `Convert To Local Range` is an explicit user action that maps simple legacy shadow/highlight budgets into graph points and disables the legacy block; old project output is not silently changed on load.
- Local Range starter presets are graph presets: `Open Shadows`, `Hold Highlights`, `Compress Range`, and `Reset`.
- Managed graph decomposition intentionally rejects active legacy `localExposure` or active/non-default Local Range recipes until exact managed stage mapping and validator support are added.
- Remaining validation need: run `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 1 plus Scenario 6 through Scenario 9 with real RAW files for responsiveness, preview quality, source switching, Save/reopen persistence, Local Range target/overlay/mask ergonomics, and photographic tuning. Record results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

Graph-quality finish status, 2026-06-29:

- Schema version `3` introduced explicit `finishTone` and `viewTransform` serialized layer state; the current recipe schema is now version `5` after Local Range region-mask state.
- Default compact finishing is `ToneCurveLayer` in `RGB` + `Log Scene` with identity points, followed by the current `ViewTransformLayer` defaults.
- Legacy v1/v2 recipe tone curves remain readable. Identity/simple legacy curves upgrade to the default Log Scene RGB finish stack; non-identity legacy curves migrate into finish tone points in the legacy scene-linear domain so edits are not dropped.
- The RAW tab's old simple Tone section is replaced by `Finish Tone`: mode buttons (`Y`, `RGB`, `R`, `G`, `B`), `Scene Linear`/`Log Scene` domain selector, editable point curve canvas, and Log Scene black/white EV controls.
- The RAW tab now has a `View Transform` section exposing Auto From Current Frame, Reset Defaults, exposure, black/white EV, middle grey, shoulder, toe, contrast, saturation, Preserve Hue, and EV False Color.
- Managed graph decomposition applies compact recipe finish/view state to real Tone Curve and View Transform nodes, and managed validation copies those layer states back into the recipe before comparing round-trip state.
- Known follow-up: `Auto From Current Frame` currently uses the available final render stats; if native/manual RAW UI smoke shows poor fitting, add a dedicated pre-view-transform probe for compact recipe RAW projects.

### Automation / Suggested Settings

Do not reintroduce the old Auto RAW / `Develop (Advanced Auto)` node as normal RAW workspace behavior.

Future automation may suggest a starting recipe or propose specific settings, but the user should be able to inspect, apply, reject, and undo those suggestions. The manual recipe remains the source of truth.

Any helper action must be clearly named and scoped. It must not own the whole RAW pipeline, hide stage order, or create duplicate controls that compete with the recipe-owned RAW workspace.

## Validation

Each upgrade needs its own tests. Do not treat this phase as one giant pass.

## Upgrade Process

For each later upgrade:

1. Write a small scoped implementation note or checklist before editing code.
2. Identify which existing phase contracts it touches.
3. Update recipe schema, UI, project persistence, and managed graph validation together when the upgrade affects RAW processing.
4. Add tests for the specific upgrade.
5. Keep the existing ownership model intact.

## Done When

The core workspace is stable, required native/manual smoke evidence is recorded, and these workflow upgrades are implemented incrementally without changing the ownership model.
