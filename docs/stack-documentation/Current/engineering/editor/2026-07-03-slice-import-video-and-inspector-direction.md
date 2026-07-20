# Video Slice Input And Inspector Direction

- Captured: 2026-07-03 01:33
- Source: docs/stack-documentation/Archived/source-notes/2026/2026-07-03-0133-slice-video-graph-notes.md
- Type: idea
- Topic: editor
- Verification: partially-verified

The editor and graph terminology transition from imported images toward
`slices` and `Import Slice` is already complete in the currently verified
surfaces and is archived separately in
`docs/stack-documentation/Archived/engineering/motion-graphics/2026-07-03-slice-terminology-transition-complete.md`.

The remaining work in this note is to expand slice-oriented media support so
both still images and video can enter the system through the same slice-first
workflow. Before implementation, confirm that the environment and graph are
ready for video-backed slices rather than only single-image flows.

Double-clicking a slice should reveal its basic settings. This should reuse the
existing pattern where some nodes or objects open deeper controls on
double-click, but applied to imported slice media.

## Follow-Up

- Decide whether video slices share the same base object model as still-image slices or require a separate media-backed slice type.
- Verify which slice surfaces should own basic settings and whether
  double-click is still the right entrypoint.
- Confirm the remaining import paths that still need to accept video-backed
  slices.

## Related Docs

- docs/stack-documentation/Archived/engineering/motion-graphics/2026-07-03-slice-terminology-transition-complete.md
- docs/stack-documentation/Current/engineering/editor/ADVANCED_NODE_GRAPH_COMPOSITOR_GUIDE.md
- docs/stack-documentation/Current/engineering/architecture/UNIFIED_WORKSPACE_ARCHITECTURE.md
