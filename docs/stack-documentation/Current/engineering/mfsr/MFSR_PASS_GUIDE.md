# MFSR Pass Guide

## Operating Rules
- Start every pass by reading `MFSR_STATUS.md`, `MFSR_REPO_NOTES.md`, this guide, and the relevant phase in `03_MFSR_IMPLEMENTATION_PLAN.md`.
- Keep each pass within the active phase. Do not start later-phase UI, decode, alignment, fusion, render, cache, GPU, or CFA-aware work early.
- Prefer small buildable changes with focused tests.
- Preserve existing RAW Develop, HDR Merge, node graph, project save/load, and render behavior.
- Update `MFSR_STATUS.md` and `MFSR_REPO_NOTES.md` when the repo truth changes.

## Phase 0: Repo Inventory
Acceptance:
- No feature code is required.
- `MFSR_REPO_NOTES.md` names actual files and integration points.

## Phase 1: Documentation And Types
Acceptance:
- MFSR contracts exist in `src/MFSR/`.
- Unit or focused behavior tests validate frame family compatibility, reference requirements, and cache-key fingerprints.
- Build/tests pass.

## Phase 2: MFSR Node Shell
Goal:
- Make the graph understand an inert MFSR node without implementing heavy rendering.

Allowed:
- Editor graph node kind, payload/state shell, sockets, catalog/prototype entry, context/browser creation, save/load, and render snapshot shell.
- Validation for invalid graph/socket combinations and mixed RAW/raster family rejection where Phase 1 summaries are available.
- Placeholder cached output/error status fields.
- Focused graph behavior tests.

Not Allowed:
- MFSR tab.
- Decode or file loading beyond existing image/RAW nodes.
- Alignment, photometric matching, confidence masks, fusion, final render, tiled render, GPU backend, or cache storage.
- View-transform or export behavior changes.

Acceptance:
- MFSR node can be created through graph APIs and UI entry points.
- It has a reference input, supporting input(s), and an image output.
- It can be saved and loaded with stable serialized kind/payload data.
- It can be connected only through valid image/RAW-compatible paths defined by this phase.
- Invalid or mixed RAW/raster input combinations produce clear rejection messages.
- Focused graph behavior tests pass.

## Phase 3: MFSR Tab Shell
Acceptance:
- A top-level MFSR tab exists and edits/selects the active MFSR node.
- Empty state can create/select an MFSR node.
- Selected-node changes update the tab.
- No analysis or render work is required.

## Phase 4: Decode And Analysis Preview
Acceptance:
- Connected sources become `MfsrFramePacket`-style summaries/proxies.
- Metadata and reference auto-selection are visible.
- Full-resolution fusion is still absent.

## Phase 5: Photometric Matching And Global Alignment
Acceptance:
- Simple static bursts report plausible global/sub-pixel offsets.
- Bad frames are rejected or marked low-confidence.

## Phase 6: MVP Fusion
Acceptance:
- CPU reference fusion produces the first real MFSR output.
- Single-frame fallback matches reference upscale.
- Output remains high-bit-depth scene-linear internally.

## Phase 7: Confidence Masks And Diagnostics
Acceptance:
- Moving or inconsistent regions are downweighted.
- Diagnostics explain low-confidence areas.

## Phase 8: Performance And Tiling
Acceptance:
- Crop preview, tiled final render, cancellation, and progress are present.
- Full-resolution render avoids loading every full-size float frame and accumulator at once.

## Phase 9: GPU Acceleration
Acceptance:
- GPU backend is optional.
- CPU/GPU outputs match within defined tolerance.

## Phase 10: Advanced RAW CFA-Aware Fusion
Acceptance:
- CFA-aware experiments fit the existing node/UI contracts.
- Quality result or blocker is documented honestly.
