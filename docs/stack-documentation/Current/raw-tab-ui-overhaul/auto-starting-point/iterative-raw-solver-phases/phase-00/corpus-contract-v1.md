# Corpus Contract v1

## Inventory

Phase 00 located two user-owned local layers:

| Layer | Files seen | Unique identity | Role |
| --- | ---: | ---: | --- |
| Sony ARW gallery | 356 | 178 unique filename/size pairs; canonical copies are SHA-256 addressed | Photographic development and validation discovery |
| Tennis DNG session | More than 8 | 8 initial SHA-256-addressed records frozen | Sensor metadata, orientation, and partial-channel clipping fixtures |

Every ARW name appears twice at the same byte size: once in `all copied
images` and once in a dated/original subtree. The manifest contains one
canonical content identity and a duplicate count of two. Paths are stored as
aliases, not personal absolute paths.

## Storage Policy

- RAW files remain outside Git.
- The repository may contain SHA-256 hashes, byte counts, non-secret path
  aliases, metadata summaries, tags, derived metrics, and review records.
- A path never defines identity. `source_sha256` does.
- A byte-for-byte duplicate inherits the same `source_group` and partition.
- Burst frames, brackets, same-scene edits, and adjacent captures stay in one
  `session_group`, even when hashes differ.
- Redistribution defaults to `no`; permission is user-owned local evaluation
  only unless explicitly changed.
- A missing file does not invalidate past results, but prevents rerunning them.

Path aliases in v1:

```text
user-raw://sony-canonical/<filename>
user-raw://tennis/<filename>
```

## Manifest Schema

`corpus-manifest-v1.tsv` uses:

| Field | Meaning |
| --- | --- |
| `record_id` | Stable Phase 00 key, not a filesystem path |
| `source_sha256` | Uppercase SHA-256 of original bytes |
| `bytes` | Original byte count |
| `path_alias` | Non-secret resolver key |
| `format` | RAW container/type |
| `session_group` | Capture/session leakage boundary |
| `source_group` | Duplicate and derivative identity boundary |
| `partition` | `development`, `validation`, or `locked` |
| `duplicate_count` | Known byte-copy count |
| `permission` | Local-use/redistribution policy |
| `labels_status` | `coarse`, `reviewed`, or `pending` |

The review sidecar adds camera, ISO, shutter, aperture, orientation, active and
crop rectangles, CFA/color-plane order, black/white metadata, metadata
capabilities, scene/failure tags, intent, and review-record link.

## Split Rules

Current local files were already browsed during discovery. They cannot be
honestly relabeled as a never-seen locked test.

- `development`: files and controlled fixtures used to implement and debug.
- `validation`: session-grouped files used to accept/reject features, bounds,
  and policies; they may not set a result and then be reported as untouched.
- `locked`: new capture sessions that no solver author reviews before a frozen
  version is evaluated.

ARW v1 assignment uses whole dated/session groups. Tennis initial records are
development because they have already been inspected. The manifest's locked
partition is intentionally empty in v1.

### Locked-Test Acquisition Plan

Before Phase 07, acquire never-reviewed sessions from at least three camera
families and reserve, at minimum, two independent source groups for each
critical family. One image may cover multiple families, but no family may be
represented by only one session. Required families are listed in the failure
taxonomy.

The locked manifest is written and hashed before running a frozen solver. If a
locked result changes code, a bound, a feature, or a policy, that set becomes
validation for the next version and must be replaced or versioned.

## Coverage Decision

Phase 00 coverage is layered rather than pretending one folder proves every
case:

- controlled fixtures cover exact black/white normalization, non-linearity,
  per-plane clipping, all-plane clipping, noise propagation, orientation,
  graph constraints, and halo profiles;
- the Tennis DNG session supplies real DNG, orientation, bit-depth, metadata,
  no-clip, and partial-channel-clip development records;
- the ARW gallery supplies coarse real-photograph coverage for ordinary
  daylight, foliage/texture, sky/wide-range boundaries, dark/night and neon or
  saturated light, and rotated compositions;
- any unconfirmed semantic or intent family remains a required review/acquire
  label and cannot be claimed from filename or histogram alone.

This is enough to start Phase 01 diagnostics. It is not enough to tune a
photographic objective or declare production readiness.

## Ingest Procedure

1. Hash original bytes and reject duplicate partition leakage.
2. Resolve session/near-duplicate grouping before assigning a split.
3. Extract metadata without applying a recipe.
4. Assign only observable technical tags automatically.
5. Require a reviewer for scene intent, acceptable range, and aesthetic tags.
6. Record missing metadata as capabilities, not zeros.
7. Freeze manifest hash with every experiment record.

## Versioning

Any added/removed hash, changed session group, partition move, or changed
permission creates `corpus-manifest-v2` or later. Label-only corrections may
increment the review-sidecar version while retaining the byte manifest.
