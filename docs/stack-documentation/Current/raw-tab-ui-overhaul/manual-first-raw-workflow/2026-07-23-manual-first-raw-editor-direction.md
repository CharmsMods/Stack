# Manual-First RAW Editor Direction

- Captured: 2026-07-23 01:16
- Source: docs/stack-documentation/Archived/source-notes/2026/2026-07-23-0116-manual-first-raw-editor.md
- Type: update
- Topic: manual-first-raw-workflow
- Verification: partially-verified

Stack's automatic RAW-editor goal is retired. The existing automatic Starting
Point and Develop Auto work should be preserved as history, not continued as
the active product direction.

The replacement direction is to import a RAW image with the most transparent
and truthful practical baseline Stack can provide, then expose an intentional
manual workflow. The UI should move away from a very long scroll of controls
and dropdowns toward a spatial workspace built around windows, graphs, and
direct manipulation. Local Range and Finish Tone are the strongest existing
interaction models to build from.

The first implementation step is to archive automatic recipe-writing surfaces
in the RAW workspace and Develop node. The automatic backend should remain
preserved until compatibility and deletion boundaries are understood. After
the UI archive is complete, the next work should audit the current RAW import
pipeline, define a neutral baseline, and design the editing workflow from first
principles.

## Verification

The current code was inspected. Automatic edit entry points exist in both the
RAW workspace and the Develop node. The RAW workspace exposes Build Starting
Point, Precise/Fast modes, suggestions, Display Fit, per-control automatic
readouts, and automatic diagnostics. The Develop node defaults to Auto and can
continuously update its authored RAW, scene-prep, and finish-tone state.

## Related Docs

- `README.md`
- `implementation-progress.md`
- `automatic-ui-archive-inventory.md`
- `docs/stack-documentation/Archived/raw-tab-ui-overhaul/automatic-raw-editor-2026-07-23/README.md`
