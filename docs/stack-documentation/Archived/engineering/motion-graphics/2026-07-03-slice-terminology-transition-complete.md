# Slice Terminology Transition Complete

- Archived: 2026-07-03
- Status: complete
- Scope: editor and graph UI wording
- Verification: partially-verified

The July 3, 2026 slice-terminology idea is no longer an active planning item
for the editor and graph import surfaces. The current program already uses
slice-oriented wording in the main editor and graph UI where users import or
activate still-image content.

Verified examples:

- `src/Editor/UI/EditorViewport.cpp`: composite canvas context menu uses
  `Import Slice`.
- `src/Editor/NodeGraph/UI/EditorNodeGraphUIContextMenu.cpp`: graph context
  menu uses `Import Slice`.
- `src/Editor/Internal/EditorModuleGraphImageNodes.cpp`: file dialog title and
  drop/import status strings use `slice` and `slices`.
- `src/Editor/UI/EditorSidebar.cpp` and
  `src/Editor/NodeGraph/UI/EditorNodeGraphUINodes.cpp`: active-source labels
  use `slice`.

This archive only covers the wording transition for the current still-image
editor and graph flows. It does not mean video-backed slices, shared slice
metadata and persistence, slice inspector behavior, timeline work, keyframing,
or video export are complete.

## Remaining Active Docs

- `docs/stack-documentation/Current/engineering/editor/2026-07-03-slice-import-video-and-inspector-direction.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/README.md`
- `docs/stack-documentation/Current/engineering/motion-graphics/implementation-tracker.md`
