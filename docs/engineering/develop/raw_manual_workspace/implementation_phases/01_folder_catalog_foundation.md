# Phase 1: Folder And Catalog Foundation

## Purpose

Create the RAW Workspace root model without thumbnails, projects, or RAW edit controls.

## Implementation Status

Complete as of 2026-06-26.

Current note:

- Later phases replaced the temporary Phase 1 Workspace UI with the dedicated RAW Workspace panels/gallery. Phase 1 remains the source of the Workspace root/source-record/catalog foundation, not the current UI description.

Completed:

- Added a focused RAW Workspace scanner/catalog model in `src/Raw/RawWorkspace.h` and `src/Raw/RawWorkspace.cpp`.
- Added managed folder creation for `Stack RAW Thumbnails`, `Stack RAW Projects`, and `Stack RAW Catalog`.
- Added recursive source scanning with managed-folder exclusion, source records, folder grouping keys, scan progress, and catalog/rating skeleton files.
- Replaced the RAW tab bridge with a Phase 1 temporary Workspace UI that exposes `Open RAW Folder`, recent Workspaces, rescan/clear actions, source rows, and preview-only selection.
- Persisted app-level RAW Workspace state in `RawWorkspaceState.json` under the existing app settings directory.
- Added regression coverage for recursive scan, managed folder exclusion, folder grouping, catalog skeleton creation, and selection without `.stack` project creation.

Validation:

- `cmake --build build --config Debug --target StackGraphBehaviorTests --parallel 1`
- `build\StackGraphBehaviorTests.exe`
- `cmake --build build --config Debug --target Stack --parallel 1`

Deferred to later phases:

- Neutral thumbnail generation/reuse/progress belongs to Phase 2.
- Polished gallery/Library RAW-folder integration belongs to Phase 3.
- Per-image `.stack` project discovery/creation/save lifecycle belongs to Phase 5.
- RAW recipe, compact `RAW Development` node, editing panels, and managed decomposition remain out of scope for Phase 1.

## Prerequisites

- Read `00_IMPLEMENTATION_GUIDE.md`.
- Read `00_STORAGE_LAYOUT.md`.
- Confirm the current RAW tab scaffold and existing Library state patterns before adding new state.
- Use `Workspace` as the user-facing name for the opened RAW folder.

## Owns

- RAW Workspace root state.
- Recent RAW Workspaces.
- Recursive source scan.
- Source RAW records.
- Folder tree/grouping model.
- Managed folder creation/discovery.
- Preview-only selection state.
- Folder-level catalog/index skeleton.

## Does Not Own

- Thumbnail generation.
- Per-image project creation.
- RAW recipe model.
- Compact `RAW Development` node.
- Dedicated RAW editing controls.
- Managed decomposition.

## Implementation Tasks

1. Locate existing app state, Library state, RAW tab scaffold, file-dialog patterns, and any existing recent-file/recent-folder storage.
2. Add a RAW workspace state/service that can store:
   - workspace root path,
   - recent workspace root paths,
   - discovered RAW source records,
   - selected source record,
   - scan status/progress.
3. Add user actions for:
   - opening a RAW folder as a Workspace with primary UI copy `Open RAW Folder`,
   - reopening a recent Workspace,
   - clearing or changing the Workspace.
4. Create managed folders when a folder is selected:
   - `Stack RAW Thumbnails`,
   - `Stack RAW Projects`,
   - `Stack RAW Catalog`.
5. Recursively scan the workspace root for supported RAW files.
6. Exclude Stack-managed folders from source scanning.
7. Build source records with at least:
   - absolute source path,
   - relative path from workspace root,
   - filename/stem/extension,
   - parent folder grouping key,
   - file size and modification time,
   - optional future fingerprint field.
8. Preserve folder organization in the scan result.
9. Add a preview-only selection state. Selecting a RAW source must not create a project.
10. Add a folder catalog/index file skeleton under `Stack RAW Catalog`.
11. Persist and reload Workspace metadata and recent Workspaces.

## Suggested Implementation Order

1. Add the data model for workspace root, source records, scan status, and recent Workspaces.
2. Wire `Open RAW Folder` and managed folder creation.
3. Add recursive scan with managed-folder exclusion.
4. Surface scan results in the temporary RAW tab UI using rows/placeholders.
5. Add catalog skeleton persistence.
6. Add tests around scanning, exclusion, and no-project selection.

## UI Requirements

- When no Workspace is open, RAW tab should show recent RAW Workspaces and a strong open-folder action.
- The strong open-folder action should be labeled `Open RAW Folder`.
- User-facing labels should refer to the opened RAW folder as the `Workspace`.
- After a folder is selected, RAW files may appear as file rows/placeholders even before thumbnails exist.
- Scan progress should be visible if scanning is not instant.

## Validation

- Unit or integration test for recursive scanning.
- Test that managed folders are excluded from source scan.
- Test that selecting a RAW does not create a `.stack` project.
- Test that folder grouping preserves subfolders.

## Done When

- A user can select a folder and Stack discovers RAW files in that folder and subfolders.
- Stack shows source records without needing thumbnails.
- Stack creates/uses the managed folder layout.
- Selecting a RAW is preview-only.
- No project files are created by scan or selection.
- Phase 2 can consume source records and scan status without reworking Phase 1 state.
