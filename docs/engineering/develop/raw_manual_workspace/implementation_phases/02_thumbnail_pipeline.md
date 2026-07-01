# Phase 2: Thumbnail Pipeline

## Purpose

Generate and reuse neutral RAW thumbnails in the RAW Workspace without creating projects or edit state.

## Prerequisites

- Phase 1 source records exist.
- RAW Workspace root state exists.
- Managed folder exclusion is already in place.
- `Stack RAW Thumbnails` can be created/discovered from the workspace root.

## Owns

- Neutral thumbnail cache.
- Thumbnail staleness checks.
- Background thumbnail jobs.
- Progress UI/state.
- Placeholder-to-thumbnail replacement.

## Does Not Own

- Edited/after thumbnails.
- Per-image project lifecycle.
- RAW development recipe editing.
- Library RAW tab polish beyond showing thumbnail status.

## Implementation Tasks

1. Build thumbnail path mapping from source record to `Stack RAW Thumbnails`, mirroring source subfolders.
2. Define thumbnail validity using the thumbnail cache signature from `00_STORAGE_LAYOUT.md`:
   - source path/relative path,
   - source file modification time,
   - source file size,
   - optional source fingerprint when available,
   - RAW loader/cache algorithm version,
   - neutral preview/render settings version,
   - thumbnail generation/dimensions version.
3. On scan, classify thumbnails as valid, missing, or stale.
4. Show RAW records immediately with placeholders/icons before thumbnails are ready.
5. Generate missing/stale neutral thumbnails asynchronously.
6. Keep the UI responsive during generation.
7. Report progress:
   - total discovered RAW files,
   - valid thumbnails,
   - queued thumbnails,
   - completed thumbnails,
   - failed thumbnails.
8. Store generated thumbnails in the managed thumbnail folder.
9. Record thumbnail status in the catalog/index if useful.
10. Handle failures per file without failing the whole folder.

## Suggested Implementation Order

1. Add thumbnail path and metadata helpers.
2. Add validity/staleness classification without generating thumbnails yet.
3. Add background job queue and progress state.
4. Wire neutral thumbnail generation.
5. Replace placeholders as thumbnails complete.
6. Persist thumbnail status in the catalog only if the catalog skeleton already supports it cleanly.
7. Add tests for path mapping, reuse, stale detection, and no project creation.

## Neutral Thumbnail Definition

A neutral thumbnail is decoded/rendered RAW data without user edits. It is a browsing preview, not a recipe render and not project state.

## Cache Identity

Thumbnail filenames should be derived from the source relative path so the cache mirrors user folder organization. Validity should come from the cache signature, not the filename alone.

If a RAW file is renamed or moved, Stack may reuse a thumbnail only when metadata/fingerprint checks prove it is the same source. Otherwise, generate a new neutral thumbnail.

## Implementation Status

Status: Complete as of 2026-06-26.

Current note:

- Later phases added gallery presentation, culling, thumbnail decode/upload batching, and real RAW Workspace loading preflight coverage. Neutral thumbnails remain cache/preview assets; edited/after thumbnails are still a later workflow extension.

Implemented:

- Thumbnail paths mirror source subfolders under `Stack RAW Thumbnails`.
- Sidecar JSON signatures track source relative path, source size, source modified ticks, optional fingerprint, RAW loader/cache algorithm version, neutral settings version, thumbnail version, and max dimension.
- Scan completion classifies thumbnails as valid, missing, or stale before queuing work.
- The RAW tab shows source rows immediately with thumbnail status placeholders and progress.
- Missing/stale/failed thumbnails are generated asynchronously through the existing task system.
- Valid cache entries are reused and skipped by the generator.
- Generated cache output is a neutral PNG plus signature sidecar.
- Thumbnail status/cache fields are serialized in the catalog skeleton.
- Per-file failures are recorded on the thumbnail record and do not fail the workspace.

Deferrals:

- Actual gallery image presentation and Library RAW Workspace polish belong to Phase 3.
- Edited/after thumbnails remain out of scope.
- Project lifecycle and first-edit project creation remain Phase 5.
- Recipe-aware thumbnails remain later-phase work after the compact RAW recipe model exists.

## Validation

- Test thumbnail path mirroring.
- Test valid thumbnails are reused.
- Test stale/missing thumbnails are queued.
- Test thumbnail generation does not create `.stack` projects.
- Manual or automated smoke test for partial gallery availability while jobs run.

Latest validation on 2026-06-26:

- Passed: `cmake --build build --config Debug --target StackGraphBehaviorTests --parallel 1`
- Passed: `build\StackGraphBehaviorTests.exe`
- Passed: `cmake --build build --config Debug --target Stack --parallel 1`

## Done When

- RAW files appear immediately with placeholders.
- Missing/stale thumbnails are generated in the background.
- Existing valid thumbnails are reused.
- Progress is visible.
- Thumbnail generation never blocks basic folder browsing.
- Phase 3 can render gallery/list UI from source records plus thumbnail status.
