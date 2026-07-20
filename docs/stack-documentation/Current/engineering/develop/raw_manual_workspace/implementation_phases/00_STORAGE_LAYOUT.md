# RAW Workspace Storage Layout

The RAW workspace is rooted at a user-opened RAW folder. User-facing UI should call this folder the `Workspace`; implementation docs may call its filesystem path the `workspace root`. Stack should keep the managed files human-readable unless that conflicts with reliability.

## Folder Layout

```text
RAW Workspace/
  User subfolders and RAW files...
  Stack RAW Thumbnails/
  Stack RAW Projects/
  Stack RAW Catalog/
```

The managed folder names above are the documented implementation names. If changed, update every phase document and the main plan together.

## Source Scanning Rules

- Scan the workspace root and user subfolders recursively.
- Preserve the user's folder organization in UI models.
- Exclude Stack-managed folders from source RAW scanning:
  - `Stack RAW Thumbnails`
  - `Stack RAW Projects`
  - `Stack RAW Catalog`
- Use the existing RAW support path, such as `RawLoader`, as the authority for supported formats when possible.
- Supported examples include DNG, ARW, RAW, and any future format supported by the loader.

## Project Rules

- Projects live under `Stack RAW Projects`, mirroring the source folder structure.
- Project filenames are based on the RAW filename stem.
- Example: `Day 1/image_0001.ARW` maps to `Stack RAW Projects/Day 1/image_0001.stack`.
- RAW files are linked by default.
- A project can embed/bake RAW data only through an explicit user action.
- Projects must persist enough mode state to reopen recipe-backed, managed decomposed, or custom graph images without silently changing ownership.
- RAW Workspace project data currently includes versioned RAW recipe state, linked/embedded source state, downstream graph data, ownership mode payloads, and read-only/custom-mode reason data where applicable.
- As of 2026-06-29, the current RAW development recipe schema is version `5` after Local Range region-mask fields. Older recipe fields such as legacy `localExposure` remain readable/renderable for compatibility, and conversion into Local Range happens only through an explicit UI action, not during project load.

## Thumbnail Rules

- Neutral thumbnails live under `Stack RAW Thumbnails`, mirroring the source folder structure.
- Neutral thumbnails are cache/preview assets, not edit state.
- Edited/after thumbnails are a later extension and should not be implemented before neutral thumbnails are stable.

## Catalog Rules

Catalog files live under `Stack RAW Catalog`.

Default files:

- `catalog.json`: structural/index cache for source records, thumbnail status, project associations, fingerprints, scan recovery, and last selected source.
- `ratings.json`: user gallery metadata for ratings, flags, and labels.

Keep this split unless a future migration intentionally changes it. `catalog.json` may cache derived metadata summaries for speed, but it should not become the only durable owner of user ratings/flags/labels by accident.

The catalog may track:

- Source relative paths.
- Source fingerprints/hashes.
- Thumbnail status.
- Project association.
- Last selected workspace/source.

The catalog is metadata/cache, not the only source of truth. Stack must be able to rescan and recover when it is missing or stale.

## Storage Authority Rules

Use these authority rules when files disagree:

| Fact | Authoritative source | Cache/derived copies | Reconciliation rule |
| --- | --- | --- | --- |
| RAW source file exists | Filesystem under the Workspace root | `catalog.json` source records | Rescan wins. Missing files mark records unresolved, not deleted immediately. |
| RAW source identity | Source fingerprint plus relative path metadata | Project `rawSourceRef`, `catalog.json` | Exact fingerprint match beats path match. Path match with changed fingerprint needs user confirmation. |
| Neutral thumbnail file exists | Filesystem under `Stack RAW Thumbnails` | `catalog.json` thumbnail status | Filesystem plus cache signature wins. Stale catalog entries are repaired on scan. |
| Thumbnail validity | Thumbnail cache signature | `catalog.json` thumbnail status | Regenerate when the signature does not match. |
| Per-image project exists | `.stack` file under `Stack RAW Projects` | `catalog.json` project association | Project file wins. Catalog association is rebuilt from project scan. |
| RAW development recipe | Per-image `.stack` project | Compact node summaries, thumbnails, catalog summaries | Project wins. UI/cache summaries must update from project state. |
| Downstream editor graph | Per-image `.stack` project | None | Project wins. |
| RAW workspace mode | Per-image `.stack` project | UI state, catalog summary | Project wins. |
| RAW recipe schema/versioned fields | Per-image `.stack` project | Compact node summaries, UI/cache summaries | Project wins. Loaders may migrate/sanitize into the current in-memory recipe model, but must not drop old supported edits. |
| Ratings, flags, labels | `ratings.json` | `catalog.json` summaries, UI state | `ratings.json` wins. Catalog may cache summaries only. |
| Last selected Workspace/source | App settings or `catalog.json`, depending on existing app patterns | UI state | Most recent successful user selection wins. |

Do not make `catalog.json` the hidden authority for user edits. It is allowed to speed up discovery and recovery, but projects and `ratings.json` own durable user-authored state.

## Thumbnail Cache Signature

Neutral thumbnail validity should use a versioned signature, not filename alone. Initial signature fields:

- source relative path,
- source file size,
- source file modified time,
- optional source fingerprint when available,
- RAW loader/cache algorithm version,
- neutral preview/render settings version,
- thumbnail dimensions/version.

If RAW loading, neutral rendering, color management, or thumbnail sizing changes in a way that affects thumbnails, bump the relevant version and regenerate stale thumbnails.
