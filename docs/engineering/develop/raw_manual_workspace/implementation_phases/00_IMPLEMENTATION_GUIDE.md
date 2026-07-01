# RAW Manual Workspace Implementation Guide

This folder turns the RAW Manual Workspace plan into implementation slices. Each phase should leave Stack in a coherent state and should avoid inventing parallel state outside the plan.

If you opened this file directly, first read `../README.md` and `../CURRENT_HANDOFF.md`. The README is the folder entrypoint and the handoff file records the latest resume state.

## Non-Negotiable Invariants

- Per-image `.stack` project files are the project truth once an image is edited.
- RAW folder selection and gallery preview are not project creation.
- RAW files are linked by default. Embedding is an explicit bake action.
- The compact `RAW Development` node is the default graph representation.
- `Managed Decomposed Mode` can sync RAW tab controls and graph nodes only while structural validation passes.
- `Custom Graph Mode` means the custom graph section owns the render and RAW tab editing is read-only for that image.
- `Develop (Advanced Auto)` remains loadable for compatibility and advanced graph/debug surfaces only; do not delete or rename serialized `RawDevelop` kinds.
- Do not reintroduce the old automatic RAW Develop workflow as normal RAW workspace behavior.
- User-facing RAW tab copy calls the opened RAW folder the `Workspace`.
- The primary empty-state action is `Open RAW Folder`.
- Base Phase 6 used `Preview & Output` copy for output/display mapping; later graph-quality finish work also exposes explicit `View Transform` controls in the RAW tab. Keep RAW-tab labels aligned with the implemented recipe state and keep managed graph `View Transform` mapping exact.
- The final `White Balance` panel includes `As Shot`, `Auto`, custom temperature/tint, and gray-point/eyedropper picking. Implementation may stage backend support, but the planned end-state is the full set.
- Expert graph-first RAW editing is supported only when the chain can be validated/adopted; otherwise it is custom graph work.

## Phase Order

1. `01_folder_catalog_foundation.md`
2. `02_thumbnail_pipeline.md`
3. `03_gallery_and_library.md`
4. `04_recipe_and_compact_node.md`
5. `05_project_lifecycle.md`
6. `06_raw_workspace_panels.md`
7. `07_managed_decomposition.md`
8. `08_later_workflow_upgrades.md`

Cross-cutting references:

- `00_MODE_CONTRACT.md`
- `00_STORAGE_LAYOUT.md`
- `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` and `../NATIVE_RAW_UI_SMOKE_RESULTS.md` when native/manual RAW UI smoke is required

## How To Pick Up Work

1. Read `../README.md`.
2. Read `../CURRENT_HANDOFF.md`.
3. Read `../RAW_MANUAL_WORKSPACE_PLAN.md`.
4. Read this implementation guide.
5. Read `00_MODE_CONTRACT.md` and `00_STORAGE_LAYOUT.md`.
6. If the active phase, active task, or handoff calls for native/manual RAW UI smoke, read `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` and `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.
7. If `../CURRENT_HANDOFF.md` identifies a validation or documentation task for an already-implemented phase, do that task before moving to broader feature work even if the next incomplete phase title is later in the list. Otherwise, find the earliest incomplete phase in the phase order.
8. Read the selected phase document, task document, or validation protocol completely.
9. Inspect the current codebase and recorded evidence for existing partial implementations before editing, running validation, or updating docs.
10. Do only the selected phase's owned scope, or do only the selected validation/documentation task.
11. Run the phase validation or task-specific checks plus the relevant build/test/docs commands.
12. Update `../CURRENT_HANDOFF.md` and the relevant phase document or final handoff with what is complete, what is deferred, and which phase or task should be next.

## How To Identify The Active Phase Or Task

Use the phase order above, then verify against the code:

1. Start at Phase 1.
2. Read the phase's `Prerequisites`, `Owns`, `Does Not Own`, and `Done When` sections.
3. Search the codebase for existing types, services, UI state, tests, or partial implementations related to that phase.
4. If every `Done When` item is already satisfied and validated, move to the next phase.
5. If the phase is partially implemented, finish or repair that phase before moving on.
6. If the phase is missing, implement that phase only.
7. If a later phase appears partly implemented, preserve it where possible, but do not expand it until earlier phases are complete.

The checkboxes in the main plan and the resume state in `CURRENT_HANDOFF.md` are guidance, not proof. The codebase and phase `Done When` sections decide what is actually complete. However, when `CURRENT_HANDOFF.md` records a pending validation/documentation gate for implemented work, that gate controls the next task until it is resolved or explicitly superseded by the user.

Before editing, running validation, or updating docs, write down the selected phase or task and why it is next. This can be a short note in the final response, a planning comment, or an implementation handoff note.

## Working Rules For Agents

- Before starting a phase or task, read the main plan and this guide.
- For a new conversation or resumed pass, read `../CURRENT_HANDOFF.md` and verify it against the codebase and recorded evidence before acting.
- Read the specific phase document, task document, or validation protocol and do only that phase/task scope.
- If a phase needs a lower phase that does not exist yet, stop and implement the lower phase first.
- If code already has partial behavior, preserve working behavior and adapt it to this contract.
- Do not replace the whole RAW renderer as part of this plan.
- Do not route the normal RAW workspace through the old Auto RAW / `Develop (Advanced Auto)` workflow.
- Do not make the RAW tab a copy of the Editor graph.
- Update the relevant phase document when a phase is completed or when a decision changes.
- Update `../CURRENT_HANDOFF.md` at the end of every implementation, validation, or documentation pass, even if the phase is only partially complete.

## Decision And Scope Rules

- Ask the user before changing a major project-shaping decision that is not already settled by these docs or the existing codebase.
- Major decisions include RAW persistence format, renderer ownership, graph ownership, dependency strategy, storage layout, thread/job model, and broad refactors across core systems.
- Do not ask the user to re-decide documented choices such as `Workspace` naming, `Open RAW Folder`, linked RAW files by default, compact `RAW Development` by default, or keeping old Auto RAW out of the normal RAW workspace.
- Ask targeted questions when a real unknown blocks implementation. Do not ask broad brainstorming questions when the phase contract is already specific enough.
- If implementation, validation, or documentation work reveals that a documented choice is technically wrong or unsafe, stop, explain the conflict, and update the plan only after the direction is agreed.

## Ownership And Placement Rules

- Before editing, decide whether the work belongs in an existing file, a new file, a new folder, or a narrower ownership boundary.
- Keep scanner/catalog logic separate from thumbnail jobs, project persistence, recipe rendering, RAW tab panel UI, and managed graph validation unless existing architecture already has a cleaner shared owner.
- Do not dump folder scanning, thumbnail generation, catalog persistence, project lifecycle, and UI state into one oversized file.
- Do not split code into tiny files with vague ownership. A new file should have a clear reason to exist and a clear owner in the architecture.
- Keep UI, persistence, renderer/recipe logic, background jobs, and graph sync from becoming carelessly mixed.
- If a temporary placeholder is needed, document the owner phase and removal/upgrade path in the phase handoff or code comment. Do not leave unrecorded fix-later structures.

## Runtime And Data Rules

- Treat recursive scanning, thumbnail generation, source fingerprinting, relink search, batch settings paste, and edited/after thumbnail generation as potentially expensive work.
- Expensive work should not freeze or visibly hitch the UI. Prefer background jobs, cancellable/progress-aware workflows where practical, and partial UI availability.
- Visible background work should show useful progress when silence would confuse the user.
- Keep durable user-authored state separate from cache and derived state:
  - per-image `.stack` projects own RAW recipes and downstream graphs,
  - `ratings.json` owns ratings/flags/labels,
  - neutral thumbnails are cache assets,
  - `catalog.json` is metadata/cache and recovery support, not hidden project truth.
- Use the human-readable managed folders defined in `00_STORAGE_LAYOUT.md`. Do not change runtime data placement accidentally.
- Choose efficient internal representations when needed, but do not violate the documented storage authority rules.

## Phase Boundary Rules

- Phases 1-3 are folder, cache, and browsing work. They must not create edit projects or build RAW editing controls.
- Phase 4 introduces the recipe and compact graph output, but not project save lifecycle polish.
- Phase 5 makes projects durable and owns first-edit creation, save lifecycle plumbing, save-before-switch, relink, and bake/embed. Later usability work disables active RAW per-control and idle-timer saves in favor of explicit and lifecycle-boundary saves.
- Phase 6 builds the user-facing RAW editing workspace on top of the existing recipe/project behavior.
- Phase 7 is the only phase that implements managed graph sync, structural validation, and `Custom Graph Mode` transitions.
- Phase 8 is a collection of later upgrades; each upgrade should be treated as its own small implementation pass. Do not start broader Phase 8 work while `CURRENT_HANDOFF.md` or `../NATIVE_RAW_UI_SMOKE_RESULTS.md` still identifies native/manual smoke as the active gate for Phase 7 Scenario 2 through Scenario 5 or Phase 8A Scenario 1 plus Scenario 6 through Scenario 9.

## Validation Expectations

Each phase should add the narrowest useful tests or smoke validations for its behavior. At minimum, an implementation pass should run the tests it touched and a build target that proves Stack still compiles.

Do not present guessed correctness as verified correctness. If validation was not run, say why and record the residual risk in `../CURRENT_HANDOFF.md`.

When native/manual RAW UI smoke is required:

- Use `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` as the source of truth for scenarios and pass criteria.
- Record scenario results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Summarize the latest native-smoke status in `../CURRENT_HANDOFF.md`.
- Do not mark a phase complete from automated preflight alone when the phase document requires native/manual UI smoke.

Recommended baseline commands, adjusted to the local build state:

```text
cmake --build build --config Debug --target Stack --parallel 1
cmake --build build --config Debug --target StackGraphBehaviorTests --parallel 1
build\StackGraphBehaviorTests.exe
```

If a phase is UI-heavy and no automated UI test exists yet, add a targeted smoke test when practical. For RAW Workspace native UI smoke, record actual results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`; the final response may summarize the result, but it is not the durable evidence store.

## Cross-Phase Regression Flow

As phases become available, preserve this end-to-end flow:

1. Open RAW folder as Workspace.
2. Scan Workspace and preserve subfolder grouping.
3. Generate/reuse neutral thumbnails without creating projects.
4. Select a RAW for preview-only viewing.
5. Apply first image-affecting edit and create exactly one project.
6. Mark the project dirty and exercise explicit Save or a lifecycle-boundary save path.
7. Switch to another RAW and verify save-before-switch.
8. Reopen the Workspace and rediscover projects from `Stack RAW Projects`.
9. Rename/move a RAW and exercise repair/relink.
10. Open an edited RAW in the graph through compact `RAW Development`.
11. Decompose to managed nodes when Phase 7 exists.
12. Break the managed chain and verify `Custom Graph Mode` read-only behavior.
13. Repair/re-adopt or detach and verify the mode transition.

Earlier phases should run the prefix of this flow that exists at that point. Later phases should keep the full flow passing.

## Handoff Template

At the end of an implementation, validation, or documentation pass, update `../CURRENT_HANDOFF.md` and report:

- Phase/task worked on.
- Files changed.
- What is complete.
- What is intentionally deferred.
- Validation run.
- Native/manual smoke result location, when applicable.
- Known risks.
- Recommended next phase/task.

If a phase or task is only partially complete, say which `Done When` bullets or task requirements are still open.

For a brand-new conversation, the next agent should be able to read `../README.md`, `../CURRENT_HANDOFF.md`, the main plan, this guide, the cross-cutting contracts, any required native-smoke evidence files, and the active phase/task document, then continue without relying on chat history.

## Completion State Rules

Use these status meanings in `../CURRENT_HANDOFF.md`:

- `Next / verify code`: recommended next phase or validation/documentation task, but the agent must inspect the code and recorded evidence before editing, running validation, or updating docs.
- `In progress`: some implementation exists, but at least one `Done When` item remains open.
- `Complete`: every `Done When` item is satisfied and validation is recorded.
- `Blocked`: the phase cannot continue without a specific user decision, dependency, or failing prerequisite being resolved.
- `Blocked by Phase N`: later phase is intentionally waiting for an earlier phase.
- `Later`: valid future work, but not part of the current implementation sequence.

Use these exact labels in the `Phase Status Snapshot` status column. Put qualifiers such as `V1 implemented`, `follow-up needed`, `branch-off`, or `native smoke pending` in the resume note instead of inventing a new status label.

Only move the current recommended active phase/task forward when the present phase is `Complete` or the required validation/documentation task is recorded. If the work is partial, blocked, or still missing required validation/documentation evidence, keep the current phase/task selected and make the remaining work explicit.
