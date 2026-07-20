# Phase 3: RAW Gallery And Library Integration

## Purpose

Build the browsing UI around the folder-backed RAW workspace and integrate it with the Library's RAW-folder view.

## Prerequisites

- Phase 1 source records and Workspace root state exist.
- Phase 2 thumbnail status/progress exists, even if some thumbnails fail.
- The RAW tab can already display placeholder source records.
- User-facing labels call the opened RAW folder the `Workspace`.

## Owns

- RAW tab gallery browsing.
- Grid and list gallery modes.
- Right-side gallery and bottom filmstrip behavior.
- Library RAW-folder tab/view.
- Shared Workspace state between RAW tab and Library.
- Shared workspace labeling for the RAW tab and Library RAW-folder view.
- Project status display surfaces that can bind to project information once Phase 5 provides it.

## Does Not Own

- RAW edit controls.
- First-edit project creation.
- Recipe rendering.
- Managed decomposition.

## Implementation Tasks

1. Replace the then-current RAW tab editor-copy browsing area with folder-gallery UI.
2. Support grid mode:
   - borderless thumbnails floating on the background UI,
   - filename below thumbnail,
   - placeholder RAW icon before thumbnail generation completes.
3. Support compact list mode:
   - text-only or nearly text-only side panel list,
   - grouped by folder.
4. Support gallery placement modes:
   - expandable right gallery,
   - bottom filmstrip.
5. Enforce that right gallery and bottom filmstrip are not open at the same time.
6. Preserve source folder organization in both grid and list views.
7. Show scan/thumbnail progress from phases 1 and 2.
8. Add project status indicator surfaces. Before Phase 5, these should bind to an optional project-status field and may be hidden or show `Unknown`; do not scan `Stack RAW Projects` or infer real project state in Phase 3.
9. Add Library RAW-folder view/tab that reads the same RAW Workspace.
10. Let Library RAW-folder view:
    - show thumbnails,
    - show folder grouping,
    - show project status when available,
    - send selected RAW to the RAW tab for preview/editing.
11. Keep the normal Library's existing project/image workflow separate.
12. Use `Workspace` in user-facing labels for the opened RAW folder/root.

## Suggested Implementation Order

1. Build shared presentation models for folder groups, selected source, thumbnail status, and display mode.
2. Implement RAW tab grid/list browsing without changing Library yet.
3. Add right-gallery vs filmstrip placement state and mutual exclusion.
4. Add the Library RAW-folder view using the same Workspace state.
5. Add navigation from Library RAW-folder view to RAW tab preview.
6. Add project status surfaces as placeholders only; Phase 5 owns real project discovery and association.
7. Add UI and state tests for selection, layout mode changes, and no project creation.

## UI Requirements

- No browse/develop mode split inside the RAW tab.
- Selecting an image shows the same RAW controls area, even if controls are not fully implemented yet.
- The preview area should remain visually dominant.
- The RAW tab should feel like a designed photo workspace, not a graph editor clone.

## Validation

- Test Workspace state is shared between RAW tab and Library RAW-folder view.
- Test grid/list mode switching does not change selection or create projects.
- Test right gallery and bottom filmstrip mutual exclusion.
- Test Library RAW-folder view does not pollute the main Library model.
- Test project-status surfaces can render an unknown/empty state without scanning project files.

## Done When

- RAW tab can browse an opened Workspace with grid/list views.
- Library has a separate RAW-folder view using the same Workspace.
- Folder grouping, placeholders, thumbnails, and progress all display coherently.
- No editing or project creation is required for browsing.
- Phase 4 can add recipe/preview behavior without replacing the browsing structure.
- Phase 5 can later plug real project status into the existing status surfaces.

## Implementation Status

Completed on 2026-06-26.

Current note:

- Phase 5 now provides real project discovery/status association for these presentation surfaces. The Phase 3 boundary still matters: browsing UI must not become the owner of project creation or edit-state authority.

- Added shared RAW gallery presentation types for grouped source views, display mode labels, exclusive placement state, thumbnail summaries, and placeholder project status.
- Replaced the RAW tab source list with a folder-gallery browser, grid/list modes, an expandable right gallery, and a mutually exclusive bottom filmstrip.
- Added generated thumbnail texture loading/cleanup for RAW Workspace thumbnails while preserving placeholder RAW tiles before thumbnails are available.
- Added a separate Library `RAW Workspace` view that reads the same `EditorModule` Workspace state, renders grouped thumbnail tiles with project status placeholders, and can open the selected source in the RAW tab.
- Kept project lifecycle out of scope: Phase 3 does not scan `Stack RAW Projects`, create `.stack` projects, infer edited state, or add edit/recipe controls.

Validation recorded for completion:

- `cmake --build build --config Debug --target StackGraphBehaviorTests --parallel 1`
- `build\StackGraphBehaviorTests.exe`
- `cmake --build build --config Debug --target Stack --parallel 1`
- Automated `Stack.exe` launch and normal close smoke test, exit code 0.

## Project Status Boundary

Phase 3 status UI is a placeholder/presentation surface only. It may display:

- no status,
- `Unknown`,
- a catalog-provided cached status if one already exists,
- or a test/mock status.

Phase 3 must not scan `Stack RAW Projects`, create projects, or decide whether a RAW has been edited. Phase 5 owns real project discovery and authoritative project status.
