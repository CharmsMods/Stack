# RAW Manual Workspace Docs

This folder is the handoff packet for Stack's folder-backed, manual-first RAW workspace. If you are an implementation, validation, or documentation agent opening this folder cold, start here and follow the order below before changing code, running validation, or updating docs.

## Read Order

1. Read this `README.md`.
2. Read `CURRENT_HANDOFF.md` for the last recorded resume state.
3. Read `RAW_MANUAL_WORKSPACE_PLAN.md`, especially `Read This First`, `Goal`, `Decisions`, `Non-Goals`, and `Implementation Phases`.
4. Read `implementation_phases/00_IMPLEMENTATION_GUIDE.md`.
5. Read the cross-cutting contracts:
   - `implementation_phases/00_MODE_CONTRACT.md`
   - `implementation_phases/00_STORAGE_LAYOUT.md`
   - `implementation_phases/00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` when a phase or task calls for native/manual RAW UI smoke
   - `NATIVE_RAW_UI_SMOKE_RESULTS.md` when running or resuming native/manual RAW UI smoke
6. Identify the active task from `CURRENT_HANDOFF.md`; if no validation or documentation task is active, identify the earliest incomplete phase whose prerequisites are satisfied.
7. Read the active phase document, task document, or validation protocol completely.
8. Inspect the existing code and recorded evidence before editing, running validation, or updating docs.
9. Do only that phase's owned scope, or do only the requested validation/documentation task.

Do not skip straight to later Lightroom-style UI behavior. The plan is intentionally split so each pass leaves Stack coherent.

## First Work Note

Before writing code, running validation, or updating docs, produce a short working note for yourself or the user:

```text
Active RAW workspace task/phase: Phase N - phase name, or validation/documentation task name
Why this task/phase is next:
- prerequisite status
- existing partial implementation found
- task/phase scope I will do now
What I will not do in this pass:
- later phase behavior
- unresolved or out-of-scope items
```

If you cannot identify the active task or phase, stop and re-read the implementation guide plus each phase's `Prerequisites` and `Done When` sections. Do not invent a parallel plan.

## New Conversation Resume

When a new conversation starts with no prior context, treat `CURRENT_HANDOFF.md` as the latest written handoff, then verify it against the code. The handoff file is allowed to be stale; the codebase and phase `Done When` sections are authoritative.

The first response in a resumed pass should identify:

- the active task or phase,
- why that task or phase is next,
- what evidence was found in the code,
- what will be intentionally left for later phases.

## Phase Selection Rule

For implementation/code work, choose the earliest phase in this order that is not complete in the codebase:

1. `implementation_phases/01_folder_catalog_foundation.md`
2. `implementation_phases/02_thumbnail_pipeline.md`
3. `implementation_phases/03_gallery_and_library.md`
4. `implementation_phases/04_recipe_and_compact_node.md`
5. `implementation_phases/05_project_lifecycle.md`
6. `implementation_phases/06_raw_workspace_panels.md`
7. `implementation_phases/07_managed_decomposition.md`
8. `implementation_phases/08_later_workflow_upgrades.md`

If `CURRENT_HANDOFF.md` identifies a validation or documentation task for an already-implemented phase, such as native/manual RAW UI smoke for Phase 7 Scenario 2 through Scenario 5 or Phase 8A Scenario 1 plus Scenario 6 through Scenario 9, do that task before moving to broader feature work. Always verify against the code and recorded evidence instead of trusting a phase title, checklist, or older status summary alone.

## Folder Map

- `RAW_MANUAL_WORKSPACE_PLAN.md`: durable product and architecture intent.
- `CURRENT_HANDOFF.md`: mutable resume state that should be updated at the end of each implementation, validation, or documentation pass.
- `NATIVE_RAW_UI_SMOKE_RESULTS.md`: mutable evidence log for native/manual RAW UI smoke runs.
- `implementation_phases/00_IMPLEMENTATION_GUIDE.md`: implementation order, non-negotiable invariants, validation expectations, and handoff template.
- `implementation_phases/00_MODE_CONTRACT.md`: ownership rules for preview-only, recipe-backed, managed decomposed, custom graph, and graph-first RAW work.
- `implementation_phases/00_STORAGE_LAYOUT.md`: human-readable workspace folders, catalog authority rules, thumbnail cache signatures, and storage conflict rules.
- `implementation_phases/00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md`: native UI smoke protocol for real RAW Workspace validation that automated tests cannot replace.
- `implementation_phases/01_folder_catalog_foundation.md`: Workspace root, recursive RAW scan, source records, managed folders, and catalog skeleton.
- `implementation_phases/02_thumbnail_pipeline.md`: neutral thumbnail generation, reuse, staleness, placeholders, and progress.
- `implementation_phases/03_gallery_and_library.md`: RAW tab gallery, list/grid/filmstrip behavior, and Library RAW-folder view.
- `implementation_phases/04_recipe_and_compact_node.md`: versioned RAW recipe, recipe renderer, and compact `RAW Development` node.
- `implementation_phases/05_project_lifecycle.md`: first-edit project creation, explicit and lifecycle-boundary saves, save-before-switch, relink, and bake/embed.
- `implementation_phases/06_raw_workspace_panels.md`: dedicated RAW tab UI, panels, preview behavior, controls, and read-only states.
- `implementation_phases/07_managed_decomposition.md`: `Decompose To Nodes`, graph validation, two-way sync, and `Custom Graph Mode`.
- `implementation_phases/08_later_workflow_upgrades.md`: broader later workflow bucket for edited thumbnails, copy/paste settings, batch operations, denoise/detail, scoped Local Range follow-ups, legacy local-exposure compatibility, and suggested-settings automation.
- `implementation_phases/08A_local_range_tone_equalizer.md`: implemented Phase 8 branch-off that replaces vague Local Exposure sliders with graph-driven Local Range/tone-equalizer behavior; model/build work is done, but Scenario 1 plus Scenario 6 through Scenario 9 native/manual smoke evidence is still pending.

## Invariants To Keep In Mind

- The RAW tab is a designed photo-development workspace, not a copy of the Editor graph.
- The compact `RAW Development` node is the default graph representation.
- The per-image `.stack` project is the project truth once an image is edited.
- Selecting a RAW, scanning folders, generating thumbnails, rating, flagging, or labeling must not create an edit project.
- The normal RAW workspace must not route through the old automatic `Develop (Advanced Auto)` workflow.
- Base Phase 6 used `Preview & Output` copy for output/display mapping; later graph-quality finish work also exposes explicit `View Transform` controls in the RAW tab. Keep labels aligned with the implemented recipe state and explain graph-stage meaning when it matters.
- Work one phase at a time unless the user explicitly asks for a broader planning pass.

## Quality Rules

- Ask before changing major project-shaping decisions that the RAW docs do not already settle, such as persistence format, renderer model, dependency strategy, graph ownership, or storage layout.
- Do not ask broad questions when the docs already define the direction. Do the documented phase or task.
- Keep ownership boundaries clean: scanner/catalog work, thumbnail jobs, project persistence, recipe rendering, RAW UI panels, and graph sync should not collapse into one vague helper area.
- Treat scanning, thumbnail generation, fingerprinting, batch operations, and future edited-thumbnail work as potentially expensive background work with visible progress when the user needs to understand what is happening.
- Keep durable user data separate from cache or derived state.
- Do not call implementation, validation, or documentation work verified unless the relevant build, test, manual validation, or docs check actually ran.
- When native/manual RAW UI smoke is the gate, record the scenario evidence in `NATIVE_RAW_UI_SMOKE_RESULTS.md` and summarize it in `CURRENT_HANDOFF.md`; do not leave the evidence only in chat or a final response.

## Handoff Rule

At the end of any implementation, validation, or documentation pass, update `CURRENT_HANDOFF.md` and the relevant phase document or final handoff with:

- what phase or task was worked on,
- files changed,
- what is complete,
- what is intentionally deferred,
- validation run,
- known risks,
- recommended next phase or task.

If a phase is complete, say which validation proves it. If a phase or task is partial, say which `Done When` items or task requirements remain open.

The next conversation must be able to continue from the docs alone. Before ending a pass, check that `CURRENT_HANDOFF.md` answers:

- what just changed,
- what validation ran,
- where native/manual smoke evidence was recorded, when applicable,
- whether the active phase/task is complete, partial, or blocked,
- what exact phase/task should happen next,
- what should not be attempted yet.
