# Multi-Image RAW Project State Foundation

Last updated: August 2, 2026.

## Implemented Boundary

RAW Workspace schema 3 adds source-set projects without adding a second active
project. `RAW`, `RAW Lab`, and `Editor` continue to present one project UUID and
one canonical graph. Gallery browsing and multi-selection do not implicitly
replace that project.

This pass implements organization, persistence, recovery, graph binding, and
the first mosaiced-RAW Multi-Frame Denoise (MFD) pre-processing contract. It
deliberately does not implement registration, alignment, MFSR fusion, burst
denoising, demosaic placement inside the eventual processor, color processing,
or final result contracts. Every managed MFD node therefore returns the typed
status `Result unavailable`; it never passes through a reference frame or
manufactures a processed result.

## Schema 3 Model

Schema 3 retains `metadata.projectKind = "raw"` and adds:

```text
rawWorkspaceSchemaVersion = 3
rawProjectModel = "source-sets"
```

A snapshot owns a stable project UUID, content-addressed embedded asset
manifest, ordered source sets, canonical graph/pipeline JSON, RAW Workspace
JSON, cover thumbnail, active-set and active-frame UI state, separate MFD-input
and post-recipe revisions, dirty revision, and storage revision. Source sets
have stable set/frame UUIDs, one homogeneous RAW or raster family, an optional
reference frame, an MFSR or Burst Denoise intent, reserved settings, and one
managed graph binding UUID.

There is no serialized frame-count limit. Sizes and revisions are 64-bit.
The same content may be used by multiple sets, but the same asset cannot occur
twice inside one set. Burst Denoise accepts RAW sets only. MFSR accepts
homogeneous RAW or supported raster sets. Fewer than two enabled frames is a
valid Draft state.

MFD operation schema 2 remains the loadable non-processing placeholder.
Schema 3 pins the RA-CFA V1 algorithm identity, canonical mosaic/output
contracts, and complete versioned parameter object while retaining
`processingImplemented = false`. Each embedded MFD original carries a typed
capture summary used for compatibility and future planning; it is not a
derived processing cache. Math implementation progress is tracked in
`../../MFD/STATUS.md`.

## Storage

New projects default to a `.stackbundle` directory. Its current manifest is
atomically replaced, its previous manifest is retained, and exact originals
live under `media/` by SHA-256 and byte length. Staged imports are not named by
the manifest until the whole set and graph revision commit.

Portable `.stack` v3 stores immutable streamed media records, record checksums,
append-only manifest generations, and a footer pointing to the last valid
generation. Ordinary saves append metadata instead of rewriting originals or
checksumming the complete project file. `Optimize Project` rewrites a compact
temporary file and atomically replaces the original.

Both implementations use the same lazy `ProjectStore` stream/transaction
interface. Opening a project materializes manifests, graph state, and the cover
thumbnail, not every original. Save As converts bundle and portable stores
while preserving exact bytes, IDs, graph bindings, and unknown metadata.
Derived analysis, proxies, fusion output, and caches are not authoritative and
do not travel with either format.

## Session And Recovery Rules

Lifecycle phases are `Loading`, `ReadyClean`, `ReadyDirty`, `Importing`,
`Saving`, `SaveFailed`, `Conflict`, and `ReadOnlyRecovery`. Commits compare an
expected storage revision. Stale save callbacks are scoped to project UUID and
save generation and cannot clear newer edits.

An invalid current bundle manifest falls back to the retained previous
manifest. That session is read-only and must use `Save Repaired Copy`.
Portable open scans backward for the latest valid footer, so an interrupted
append is ignored. External revision conflicts block in-place save and present
`Reload`, `Save Copy`, and `Cancel`. Failed saves leave the project open and
dirty.

## RAW Lab And Graph

RAW, RAW Lab, and Library share these gallery selection rules:

- one click selects one source
- Ctrl-click toggles membership
- Shift-click extends a contiguous range from the anchor
- double-click or `Open in RAW` explicitly replaces the editing project; clean
  projects switch directly and dirty projects require Save, Discard, or Cancel

With two or more selected RAW files, `Create New MFD Project` remains available
even while another project is active. An active MFD project separately offers
`Add to Current Burst`; these actions never substitute for one another.
Workspace browsing never changes project identity. Creation asks for project
name, bundle location, and reference frame; the format defaults to an embedded
`.stackbundle`. The new bundle commits before deferred activation, so an open or
activation failure cannot destroy the prior session.

Before staging any bytes, a header-only LibRaw probe checks every selection.
The first MFD path accepts only supported still-mosaiced Bayer/CFA RAWs. It
rejects a demosaiced/Linear RGB DNG with an explicit future-work explanation.
Frames must agree on camera model, CFA pattern, sample bit depth, RAW and
visible dimensions, and sensor margins. Exposure, ISO, timestamp, and
orientation are retained as capture evidence but do not require equality.

RAW Lab's focused MFD project view owns frame selection, ordering, enable
state, reference selection, removal, and CFA-preserving orientation
metadata. Excluded frames remain project members. Normal per-image RAW Lab
tools are unavailable while this MFD surface is active, preventing accidental
pre-combine tone or color edits. An explicit Workspace browser is still
available for adding captures without replacing the active project.

The project browser lists `.stackbundle` and portable v3 projects independently
of source files. Source cards show many-to-many project membership counts;
membership first uses the informational origin and then exact SHA-256/size when
an original has moved.

Each MFD frame owns one protected `RAW Project Frame` node, permanently linked
to a stable dynamic socket on exactly one protected MFD node. The reference
designation and included/excluded state are visible on both surfaces; excluded
frames stay linked and dimmed so topology does not churn. The MFD image output
is serializable for downstream graph authoring but remains typed unavailable.
Selecting a frame or MFD node returns to the matching RAW Lab project surface.
Deleting a managed frame or MFD node uses the same project confirmation
transaction as deleting it in RAW Lab.

Missing managed nodes or links are recreated from the manifest. Duplicate or
orphan bindings are quarantined and require `Save Repaired Copy`; user graph
content is not silently discarded. Existing schema-1 source-set nodes remain
loadable and are not silently rewritten.

MFD uses its internal RAW View Transform by default, so connecting its output
does not spawn a duplicate graph transform. Turning the internal transform off
marks the MFD output scene-linear and places one View Transform immediately
before Output. Re-enabling the internal transform removes only a safe,
single-consumer View placement and preserves its settings for later reuse.

## Legacy Boundary

Schema 1/2 single-RAW projects open and save unchanged. Their linked/embedded
policy is not rewritten. `Save As Upgraded Project` creates a separate
`.stackbundle`, embeds the exact available original, preserves the existing
graph, and adds one Draft source set and managed binding. A missing linked
source blocks upgrade until relinked. The legacy file is never modified.

Existing serialized MFSR placeholder nodes remain loadable and are not
rewritten or reactivated by this foundation.

## Validation

Run:

```text
.\build\Stack.exe --validate-mfd-phase0
.\build\Stack.exe --validate-mfd-project-foundation
.\build\Stack.exe --validate-multi-source-projects
```

The MFD suite covers mosaiced-only eligibility, structural incompatibility and
Linear RGB rejection, reference/active-frame/revision persistence, Draft state,
stable dynamic RAW sockets, managed link ownership, unavailable-result graph
round trips, and internal-versus-graph View classification. The broader suite
also covers unbounded/64-bit model round trips, deduplication and cross-set
reuse, stale save completion, import rollback, both storage formats, exact
streamed bytes, bidirectional conversion, expected-revision conflicts,
interrupted portable append recovery, optimize, and previous-manifest recovery.
