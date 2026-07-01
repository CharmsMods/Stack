# Phase 5: Per-Image Project Lifecycle

## Purpose

Make folder-backed RAW editing persist as one project per edited RAW file.

## Status

Complete on 2026-06-26.

Phase 5 is implemented as durable per-image project plumbing plus minimal lifecycle UI. Final RAW editing panels remain Phase 6, and managed/custom graph semantics remain Phase 7.

Current note:

- Later usability stabilization changed active RAW edit persistence behavior: recipe edits no longer synchronously save on every control tick, and time-based idle save timers for active RAW Workspace projects are disabled. Explicit Save, save-before-source-switch, save-before-tab-leave, save-before-clear/close, and close-drained save-worker paths remain part of the lifecycle contract.

## Prerequisites

- Phase 1 Workspace root/source record state exists.
- Phase 4 recipe model and compact `RAW Development` graph output exist.
- The app has a way to identify image-affecting recipe edits, even if Phase 6 has not built final controls yet.

## Owns

- Project discovery in `Stack RAW Projects`.
- Create-on-first-edit behavior.
- Linked RAW source references.
- Lifecycle save plumbing, including first durable save paths plus later explicit and boundary-triggered saves.
- Explicit Save and `Ctrl+S`.
- Save-before-switch.
- Rename/move relink and repair.
- Explicit bake/embed action.

## Does Not Own

- Final RAW panel design.
- Managed decomposition internals.
- Batch setting sync.
- Edited/after thumbnails.

## Implementation Tasks

1. Discover existing `.stack` projects under `Stack RAW Projects`, mirroring source subfolders.
2. Associate projects to source RAW records by:
   - expected relative project path,
   - stored RAW source metadata,
   - optional fingerprint/hash.
3. Keep selection preview-only until first image-affecting edit.
4. On first image-affecting edit, create one `.stack` project for that RAW file.
5. Name the project from the RAW filename stem.
6. Store the RAW source as a linked external source by default.
7. Store the RAW development recipe and downstream editor graph in the project.
8. Persist or reserve mode state:
   - recipe-backed,
   - managed decomposed,
   - custom graph.
   Phase 5 should make project files capable of preserving these states. Phase 7 owns the full managed/custom graph semantics.
9. Add save handling after edits. Initial Phase 5 supports save plumbing; later active RAW behavior saves at explicit/lifecycle boundaries instead of every control tick or idle timer.
10. Add explicit Save command and `Ctrl+S`.
11. Before switching from one RAW to another, save unsaved edits.
12. Add relink/repair:
    - try metadata/fingerprint repair first,
    - show relink UI when automatic repair is uncertain.
13. Add explicit bake/embed action:
    - user chooses the project,
    - UI explains linked vs embedded storage consequences,
    - baking does not silently change every project in the folder.

## Suggested Implementation Order

1. Add project path mapping and project discovery.
2. Add source-to-project association and conflict handling.
3. Add preview-only vs edited/project-backed state.
4. Add first-edit project creation using the Phase 4 recipe.
5. Add linked RAW source persistence.
6. Add lifecycle save handling and explicit Save/`Ctrl+S`.
7. Add save-before-switch.
8. Add relink/repair.
9. Add bake/embed action.
10. Add tests around every transition that can create, save, switch, relink, or embed a project.

## Handoff To Phase 6

Phase 5 should expose clean APIs/events for "this control changed the recipe" and "ensure a project exists before applying this edit." Phase 6 should call those APIs instead of reimplementing project creation.

## Minimum Project Mode Fields

Phase 5 should make the project schema capable of preserving ownership state even before Phase 7 implements every mode. These fields started as Phase 5 storage slots; in the current implementation, Phase 7 now populates the managed/custom ownership payloads when those modes are active. Minimum fields:

- `rawWorkspaceSchemaVersion`
- `rawWorkspaceMode`: `recipe-backed`, `managed-decomposed`, or `custom-graph`
- `rawSourceRef`: linked source path plus source metadata/fingerprint fields
- `rawRecipe`: the Phase 4 recipe payload
- `downstreamGraph`: the normal editor graph after RAW development
- `managedRawSection`: nullable managed ownership payload slot; Phase 7 stores graph-section identity and field mappings here when `managed-decomposed` is active
- `customRawSection`: nullable custom graph ownership payload slot; Phase 7 stores custom section identity/ownership metadata here when `custom-graph` is active
- `readOnlyReason`: nullable text/status used when RAW tab editing is disabled

When implementing Phase 5 in isolation, new edited projects should normally write `rawWorkspaceMode: recipe-backed`. The managed/custom fields may be null or preserved from already-existing data, but Phase 5 should not invent managed decomposition behavior. Current managed/custom write semantics are owned by Phase 7.

## Mode Persistence Rules

- `rawWorkspaceMode` is stored and authoritative for the project's RAW ownership mode.
- `readOnlyReason` should be derived when possible from the current mode/validation result. It may be stored as a last-known UI explanation, but validation state wins after reload.
- In `recipe-backed` mode:
  - `rawRecipe` is authoritative for RAW development.
  - `managedRawSection` should be null.
  - `customRawSection` should be null.
  - the graph should contain the compact `RAW Development` node by default.
- In `managed-decomposed` mode:
  - `rawRecipe` and `managedRawSection` must both be present.
  - `managedRawSection` stores graph-section identity and field mappings.
  - validation on load decides whether the project remains managed or moves to `custom-graph`.
  - the compact node may be absent or represented only as a collapsed/summary concept, depending on graph implementation.
- In `custom-graph` mode:
  - `customRawSection` stores the custom section identity/ownership metadata.
  - `rawRecipe` may retain the last valid/adopted recipe for reference, but it is not authoritative for the rendered RAW result.
  - RAW tab editing is read-only until repair, re-adoption, or detach changes ownership.

Do not silently convert modes on load. If a saved mode cannot be validated, preserve the project and show a repair/relink/read-only state instead of flattening it into recipe-backed mode.

## Project Association Rules

- A `.stack` file under `Stack RAW Projects` is authoritative for the existence of an edited RAW project.
- `catalog.json` may cache project associations, but project discovery should rebuild those associations from actual `.stack` files.
- When both a path match and fingerprint match exist, prefer fingerprint for identity and path for human-readable organization.
- When a RAW source has moved or been renamed and fingerprint/metadata strongly match, offer automatic repair or a clear confirmation depending on confidence.

## First Edit Triggers

- Slider/control changes.
- Rotation.
- Crop selection.
- Any RAW recipe adjustment that changes the rendered image.

Not triggers:

- Selection.
- Rating/flagging/labeling.
- Thumbnail generation.
- Gallery layout changes.

## Validation

- Test preview selection creates no project.
- Test first image edit creates exactly one project.
- Test project path mirrors source subfolder.
- Test Save/`Ctrl+S` and lifecycle save paths persist recipe changes.
- Test switching images saves current unsaved project first.
- Test linked RAW source survives project reload.
- Test relink flow for renamed/moved RAW.

## Done When

- Every edited RAW has one matching `.stack` project.
- Projects live inside the RAW Workspace, not Stack's normal library storage.
- Linked RAW is the default.
- Explicit save and lifecycle boundary saves work.

## Completion Notes

Implemented:

- Project discovery scans `.stack` files under `Stack RAW Projects`, rebuilds source associations from project data, and records project status/path/mode in source records and catalog output.
- First image-affecting edit can call the Phase 5 lifecycle API to create or load the selected source's per-image project. Preview-only selection still does not create a project.
- New edited projects are saved under the RAW Workspace project tree, mirror source subfolders, default to linked RAW storage, and persist RAW source metadata, recipe data, downstream graph data, ownership mode fields, and read-only state. Phase 7 now populates the managed/custom ownership payloads that Phase 5 reserved.
- Initial save plumbing, explicit Save, `Ctrl+S`, save-before-export, save-before-load, save-before-switch, and app close prompts route through the RAW Workspace project save path when a RAW project is active. Later usability work disables active RAW idle-timer/per-control saves in favor of explicit and lifecycle-boundary saves.
- Minimal relink and bake/embed actions are available for the selected active project. Relink repairs the project to the selected source; embed stores source bytes in the project without changing every project in the folder.
- Regression coverage verifies preview-only selection, first-edit creation, subfolder project paths, linked source reload, project discovery, relink metadata, and embedded RAW metadata.

Deferred:

- Final image-affecting RAW controls and the dedicated RAW development panel layout are Phase 6.
- Rich relink browsing, confidence UI, and a broader orphan-project browser can be expanded later; Phase 5 currently provides selected-source repair confirmation.
- Full managed decomposed/custom graph validation, adoption, and repair semantics are Phase 7.

Validation:

- Passed: `cmake --build build --config Debug --target StackGraphBehaviorTests`
- Passed: `build\StackGraphBehaviorTests.exe`
- Passed: `cmake --build build --config Debug --target Stack`
- Passed: automated `Stack.exe` launch and normal close smoke test, exit code 0.
