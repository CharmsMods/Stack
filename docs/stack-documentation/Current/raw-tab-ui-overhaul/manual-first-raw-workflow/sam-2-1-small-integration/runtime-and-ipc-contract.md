# Runtime and IPC Contract

## Process Boundary

The target architecture is:

```text
Stack.exe
  owns UI, recipes, project state, masks, undo, RAW rendering
       |
       | authenticated local, versioned request/result protocol
       v
StackSubjectService.exe
  owns model session, embedding cache, inference, provider diagnostics
       |
       | loads one approved package with fixed paths and hashes
       v
ONNX Runtime/provider + SAM 2.1 Small model artifacts
```

The helper is not a public plugin API. It accepts a fixed Stack protocol and
must not discover arbitrary models, Python modules, DLL paths, or executable
commands from a manifest.

## Lifecycle

- Stack launches the helper only when the capability is requested.
- The helper may stay warm for the current editing session.
- Stack can cancel or supersede requests by generation.
- A helper crash, timeout, provider error, or invalid result leaves the current
  image and project unchanged.
- Stack terminates the helper on application shutdown and applies a job object
  or equivalent lifetime/resource boundary.
- The helper has no network access in the initial product.

## Analysis Image Request

Conceptual fields:

- protocol version;
- source and analysis-proxy fingerprints;
- request generation;
- width, height, row stride, channel order, dtype, and transfer description;
- fixed normalized RGB data in validated shared memory;
- optional R8 deterministic qualifier prior;
- byte length and content hash.

The helper must reject unknown versions, invalid dimensions, overflows,
unexpected strides, unsupported formats, and buffers larger than the approved
limit before allocating model tensors.

## Prompt Request

Conceptual fields:

- source fingerprint and embedding generation;
- prompt generation;
- positive normalized points;
- negative normalized points;
- optional normalized box;
- optional prior low-resolution logits only when the frozen model contract
  supports them;
- requested candidate-mask count.

Point and box limits are fixed by protocol. Arbitrary serialized objects are
not allowed.

## Result

Conceptual fields:

- protocol/source/prompt generations;
- result status;
- mask dimensions, stride, dtype, and byte length;
- R8/R16 mask or documented logits;
- confidence values with explicitly defined semantics;
- encoder, decoder, upload/download, and total timing;
- runtime/provider and model-pack identifiers;
- non-sensitive diagnostic code.

Stack validates every field and discards stale generations before publishing
an outline.

## Input Domain

The provisional analysis-proxy stage is:

```text
RAW normalization
  -> white balance
  -> demosaic
  -> camera-to-working-space transform
  -> fixed neutral analysis view
  -> bounded RGB model input
```

The exact neutral view, gamut handling, resolution, and transfer remain open
until reference experiments. The proxy must exclude the Local Exposure edit
being authored so its mask cannot recursively move when exposure changes.

## Cache Contract

- Cache embeddings by model hash plus analysis-proxy fingerprint.
- Never key only by filename or UI selection.
- Bound cache memory and expose eviction diagnostics.
- Prompt changes reuse an embedding.
- Recipe edits that do not alter the analysis proxy do not invalidate it.
- Source, WB, demosaic, camera transform, crop/geometry, or analysis-view
  changes invalidate it according to the approved fingerprint contract.

## Mask Transaction

- Proposed results are transient and do not dirty the project.
- Accept converts the proposed semantic result into Stack-owned mask data.
- Cancel discards all transient prompts/results.
- Add/Subtract/Intersect are represented explicitly and previewed before
  commit.
- Acceptance creates one undoable transaction.
- The baked mask is authoritative on reload and export.
- Recompute is optional and never silently replaces a baked mask.

## Package Security

- Package identity is allowlisted by Stack release metadata.
- Manifest and every artifact are signature/hash verified.
- Relative paths are canonicalized and constrained inside the installed pack.
- Unknown files do not become executable merely because they exist in the
  directory.
- Installation is staged, verified, and atomically activated.
- A health check must pass before the previous pack is retired.
- Rollback and uninstall leave baked project masks usable.
