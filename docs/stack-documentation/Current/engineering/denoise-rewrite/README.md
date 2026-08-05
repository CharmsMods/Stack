# Denoise Rewrite

## Status

This folder is the entry point for Stack's next denoise implementation.

On 2026-07-24, the unfinished ONNX/Restormer experiment was retired from the
active application so the next implementation can begin from a deliberate
pipeline contract instead of inheriting an accidental one.

The first replacement slice is now implemented: Stack's existing classical
pre-demosaic filter has an explicit DNG noise-profile mode, a legacy
compatibility mode, tested shot/read-variance math, and live-GPU coverage.
Real-image acceptance and tuning are still open, so it is a foundation rather
than a completed product denoiser. See
[classical-pre-demosaic-foundation.md](classical-pre-demosaic-foundation.md).

The second replacement slice is also implemented as a disabled-by-default,
recipe-backed post-demosaic stage. It uses a three-scale scene-linear GPU
filter with separate Color Noise, Luminance Noise, and Detail Protection
controls. It is deliberately classical and deterministic; it is the fallback
and comparison baseline for future public AI packages. See
[classical-post-demosaic-foundation.md](classical-post-demosaic-foundation.md).

The third replacement slice now has a working local-development foundation: a
new optional Restormer V1 package, external helper, and scene-linear adapter
contract. Both official candidate checkpoints load and run locally without
reviving the retired neural system. Public checkpoint redistribution remains
blocked pending written authorization. See
[restormer-rgb-denoise-v1/README.md](restormer-rgb-denoise-v1/README.md).

## Current Product State

- **Bilateral Filter remains supported.** Its editor-graph type, saved settings,
  execution, and browser entry are unchanged.
- **Median, Mean, and Non-Local Means remain available.** This pass did not make
  a product decision about those general-purpose filters.
- **Classical RGB Denoise and Scene Denoise are legacy load-only nodes.** They
  are hidden from the node browser but retain their existing execution so old
  projects do not silently change pixels.
- **Linear RGB Neural Denoise is a legacy bypass node.** Old nodes and their
  settings still load and save, but inference is disabled and pixels pass
  through unchanged.
- **RAW Neural Denoise is a legacy pass-through graph node.** It is no longer
  offered for new graphs. Its graph kind and serialized settings remain so old
  projects can load without discarding unknown data.
- **RAW Lab now has an experimental CFA Denoise tool.** It authors the shared
  recipe-backed pre-demosaic settings and appears immediately before Exposure.
  Original RAW remains unchanged.
- **RAW Lab now has a separate RGB Denoise tool.** It runs after demosaic,
  white balance, and working-space conversion, but before authored RAW
  Exposure. New and migrated recipes keep it disabled.
- **The technical Develop RAW controls expose the classical mode choice.**
  The new mode uses DNG shot/read variance only on a Truthful V1 Bayer source
  with valid profile metadata and otherwise retains the legacy fallback.
- **The old model-pack discovery and ONNX Runtime loader are removed from the
  active source tree.**
- **Stack's local custom-model training playground is preserved outside the
  application source contract.** It was not deleted, promoted, or represented
  as production-ready.

The exact local-artifact record is in [artifact-inventory.md](artifact-inventory.md).

## Compatibility Rules

The retirement deliberately separates *load compatibility* from *feature
availability*:

1. Existing serialized node kinds and settings remain readable.
2. Retired nodes are not offered when authoring a new graph.
3. Classical legacy nodes retain their previous output behavior.
4. Neural legacy nodes fail safe as explicit pass-through operations.
5. Loading and saving an old project must preserve its retired denoise settings.
6. No local model, runtime DLL, Python environment, or checkpoint is required
   for Stack to start, load an image, or open an old project.

Do not delete the compatibility types merely because the old runtime has been
removed. Their purpose is to prevent destructive project migration.

## Boundary for the New System

The next denoise system must use a new, versioned contract. It must not quietly
reactivate the legacy `NeuralDenoiseSettings` schema or treat the old
`denoise/manifest.json` format as current.

Before selecting a model or writing UI, the new work needs explicit decisions
for:

- pipeline location: sensor mosaic, demosaiced scene-linear RGB, or both;
- the truth represented by preview versus final-quality processing;
- noise estimation and camera/ISO metadata assumptions;
- chroma/luma/detail controls and their actual mathematical meaning;
- tiling, overlap, cancellation, cache, and GPU-memory behavior;
- deterministic fallback when a model or accelerator is unavailable;
- model/runtime licensing, redistribution, notices, and update ownership;
- external package boundaries so optional inference remains separate from the
  main executable;
- saved-project behavior when the selected package is missing or changes.

## Recommended Delivery Order

1. Establish real noisy/clean test images and measurable acceptance criteria.
   **Open.**
2. Define the stage contract and a versioned recipe schema.
   **CFA, classical RGB, and Restormer V1 recipe/adapter contracts complete.**
3. Measure a transparent classical baseline.
   **CFA and RGB implementations, focused tests, and first real-image smoke
   measurement complete; broader corpus acceptance and tuning remain open.**
4. Research candidate public models and Stack's custom-model path against the
   same data, including licenses and redistribution constraints.
   **Restormer selected; checkpoint redistribution authorization remains
   open.**
5. Design the external package/runtime boundary.
   **Implemented for local development; release trust remains closed.**
6. Implement a headless reference path and pixel tests.
   **Adapter, conversion equivalence, package validation, and real-model helper
   smoke tests pass.**
7. Add preview/final scheduling, cancellation, and cache behavior.
   **Implemented foundation; PBO transfer and real-image performance tuning
   remain open.**
8. Add the RAW/RAW Lab interaction surface only after the processing contract
   is stable.

## Historical Material

The old implementation documents are retained as evidence, not instructions:

- [Restormer ONNX Integration](../../../Archived/neural-denoise/RESTORMER_ONNX_INTEGRATION.md)
- [Restormer Export Workflow](../../../Archived/neural-denoise/RESTORMER_EXPORT_WORKFLOW.md)
- [Neural Denoise System Plan](../../../Archived/top-level/NEURAL_DENOISE_SYSTEM_PLAN.md)
