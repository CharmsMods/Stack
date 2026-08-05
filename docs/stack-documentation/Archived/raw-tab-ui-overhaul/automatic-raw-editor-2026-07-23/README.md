# Archived Automatic RAW Editor

Archived: July 23, 2026.

## Why This Packet Was Archived

The user retired Stack's product goal of automatically editing a RAW image.
Repeated implementation and validation work did not produce a dependable,
truthful editing experience. The successor direction is a manual-first RAW
editor built around explicit controls, graph-based tools, and a minimally
interpreted default import.

The original decision is preserved at:

```text
docs/stack-documentation/Archived/source-notes/2026/2026-07-23-0116-manual-first-raw-editor.md
```

## Archive Boundary

This packet preserves:

- the former current RAW-tab UI contract and validation records
- the complete automatic Starting Point research and implementation history
- the frozen Phase 06 precise-solver checkpoint
- plans for Phase 07, which was never activated

The backend implementation remains in source control for historical reference
and focused regression coverage, but it is no longer an active product
direction and must not be exposed by the current RAW editing UI.

## Successor Workstream

Current work begins at:

```text
docs/stack-documentation/Current/raw-tab-ui-overhaul/manual-first-raw-workflow/README.md
```
