# Implementation Tracker

This tracker records status for the Motion Graphics workstream. Keep entries
lightweight and factual.

## Status Summary

Overall status: Planning, with one completed background terminology transition.

Timeline, video-slice, and animation implementation have not started from this
workstream. The existing editor and graph `slice` terminology transition is
already in the program and is tracked here as completed background rather than
as proof that the broader motion work has started.

## Pass Log

| Date | Pass | Status | Notes |
| --- | --- | --- | --- |
| 2026-07-03 | Workstream creation | Complete | Created active planning folder and starter docs. Existing slice, timeline, graph/canvas, CompositeAnimationSpecs, and Unified Workspace Architecture docs were linked, not moved or merged. |
| 2026-07-03 | Slice terminology archival | Complete | Verified that the current editor and graph import UI already uses `slice` / `Import Slice`, archived that completed wording transition, and kept video slice, inspector, and data-contract work active. |
| 2026-07-06 | Timeline/video export foundation setup | Complete | Archived the new planning source note and created `timeline-video-export-foundation/` as the focused workspace for bottom timeline layout, connected-chain rows, keyframe UX, FFmpeg/export packaging, and the next code-research pass. |

## Phase Status

| Phase | Name | Status | Notes |
| --- | --- | --- | --- |
| 0 | Code And Behavior Map | Not Started | First implementation-adjacent pass should inspect code and update `source-map.md`. |
| 1 | Slice Vocabulary And Data Contract | Partial | Editor and graph UI wording already uses `slice` / `Import Slice`, but shared metadata, persistence expectations, and the video-slice object model still depend on import and persistence inspection. |
| 2 | Timeline Foundation | Not Started | Depends on timebase and target ownership decisions. |
| 3 | Canvas/Object Transform Animation | Not Started | Should come before broad graph-parameter animation. |
| 4 | Still-Frame Export At Time | Not Started | Requires deterministic evaluation at selected frame. |
| 5 | Video Slice Preview And Scrubbing | Not Started | Requires video import/decode research. |
| 6 | Video Export | Not Started | Requires encoder/export backend decision. |
| 7 | Animated Graph Parameters | Not Started | Deferred until transform animation and frame-at-time evaluation are stable. |

## Current Blockers

- Current code paths have not yet been inspected for this workstream.
- Verified terminology is ahead of the documented slice data contract, so the
  remaining docs must not treat the wording transition as video or model
  completion.
- Track ownership is undecided.
- Global graph versus per-object graph ownership needs confirmation against the
  current implementation.
- Video decode/export backend is undecided.

## Next Pass Candidate

Perform Phase 0 code and behavior mapping:

- inspect current image import and graph import paths
- inspect Composite layer model and export path
- inspect persistence boundaries
- inspect existing inspector/context-menu/double-click patterns
- inspect current graph chain/output identity and node parameter storage
- inspect bottom graph/right viewport split layout for a resizable timeline
- inspect keyboard shortcut handling for `Ctrl+Shift+Tab`
- inspect build/tooling and third-party notices for FFmpeg packaging
- update `source-map.md`, `open-decisions.md`, and this tracker with verified
  findings
