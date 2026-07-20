# Stack MFSR Spec Pack

## Purpose
This folder defines the first implementation plan for Stack's Multi-Frame Super Resolution feature. It is intentionally short and strict so Codex can follow it without turning the task into one giant ambiguous implementation.

## Product direction
MFSR is both:

1. A graph node that accepts a reference image plus one or more supporting images.
2. A dedicated top-level tab that controls the active MFSR node, shows diagnostics, and runs expensive analysis/render jobs.

The tab must not become a separate disconnected workflow. It is a controller/editor for an MFSR node. The MFSR node output should behave like a normal image output in the editor graph after the MFSR render is complete.

## Core technical direction
Use existing Stack RAW extraction/develop code as the entry point for camera files. LibRaw remains the extractor for RAW data and metadata; Stack remains responsible for rendering and all algorithmic work.

MFSR must support two families of input, but never mix them in a single MFSR node:

- RAW burst mode: ARW/DNG/etc. where Stack can access mosaiced RAW or linear RAW-like data.
- Raster burst mode: lossless or high-quality normal images such as PNG/TIFF/EXR, after color management and linearization.

RAW burst mode is the priority because it preserves sensor sampling and CFA/Bayer information. Raster mode is useful but should be treated as lower ceiling.

## Design principles
- Default to automatic behavior.
- Keep advanced settings collapsed.
- Make the heavy render explicit and cacheable.
- Store high-bit-depth scene-linear results for downstream graph work.
- Apply the display/view transform for preview/export display, not as the internal data representation.
- Build CPU correctness first, then GPU acceleration behind an interchangeable backend.
- Prefer safe partial implementation over a giant all-at-once algorithm.

## Documents
Future implementation passes should open the operational docs first: `MFSR_STATUS.md`, `MFSR_REPO_NOTES.md`, and `MFSR_PASS_GUIDE.md`.

- `01_MFSR_PRODUCT_UI_SPEC.md`: node and tab behavior.
- `02_MFSR_PIPELINE_SPEC.md`: algorithm stages, input contracts, RAW/raster differences, output contracts.
- `03_MFSR_IMPLEMENTATION_PLAN.md`: phase-by-phase implementation checkpoints.
- `04_CODEX_GOAL_RULES.md`: Codex goal-mode rules, status log format, boundaries, and validation loop.
- `MFSR_REPO_NOTES.md`: current Stack code map, confirmed capabilities, gaps, and likely integration points.
- `MFSR_STATUS.md`: live phase/status note for future Codex passes.
- `MFSR_PASS_GUIDE.md`: operational pass boundaries and acceptance criteria for phases 0 through 10.

## Research anchors
- Google Research, "Handheld Multi-Frame Super-Resolution" / Pixel Super Res Zoom: RAW burst alignment and fusion before normal demosaicing.
- Fruchter & Hook, "Drizzle": dithered/undersampled frame reconstruction with weighted samples.
- LibRaw documentation: extraction of RAW data and metadata, including CFA/Bayer pattern and black level information.
- OpenCV documentation: geometric transforms, remapping, and CUDA optical-flow/warping references for possible acceleration patterns.
