# Stack MFSR Implementation Plan

## Purpose
Implement MFSR in small testable phases. Do not attempt the full algorithm in one pass.

## Phase 0: Repo inventory
Goal: understand existing Stack architecture before writing feature code.

Tasks:
- Locate current RAW loading/extraction/develop code.
- Locate node graph data types and socket validation logic.
- Locate editor tab/header navigation code.
- Locate render/cache infrastructure.
- Write `MFSR_REPO_NOTES.md` in this folder with actual file names and architecture findings.

Acceptance:
- No feature code required.
- Repo notes identify the real integration points.

## Phase 1: Documentation and types
Goal: add docs and internal contracts.

Tasks:
- Keep this spec pack discoverable under `docs/engineering/mfsr/`.
- Add internal MFSR frame/input/result data structures in the appropriate project module.
- Add enum/types for RAW_MOSAIC, RAW_LINEAR, RASTER_LINEAR, and invalid/mixed input.

Acceptance:
- Build passes.
- Unit tests validate type compatibility rules.

## Phase 2: MFSR node shell
Goal: make the graph understand an MFSR node without implementing heavy rendering yet.

Tasks:
- Add MFSR node with reference input, dynamic supporting inputs, and image output.
- Enforce mixed RAW/raster rejection.
- Add placeholder cached output/error state.

Acceptance:
- Node can be created, saved, loaded, and connected.
- Invalid input combinations show clear UI errors.

## Phase 3: MFSR tab shell
Goal: create the dedicated tab as a controller for the active MFSR node.

Tasks:
- Add header icon/tab.
- Empty state creates/selects an MFSR node.
- When a node is selected, show input list, reference frame, settings panel, preview placeholder, diagnostics placeholder.

Acceptance:
- Tab and graph node remain linked.
- Changing selected node updates the tab state.

## Phase 4: Decode + analysis preview
Goal: use Stack RAW/raster code to create analysis frame packets and proxies.

Tasks:
- Decode connected sources into `MfsrFramePacket` instances.
- Extract metadata: dimensions, orientation, black/white level, CFA pattern when available, color/profile hints.
- Generate downscaled linear luminance/green-channel proxies.
- Add reference auto-selection using sharpness + clipping + exposure sanity.

Acceptance:
- Analysis runs without full render.
- UI shows per-frame metadata and reference selection.

## Phase 5: Photometric matching + global alignment
Goal: get stable burst alignment on simple static test images.

Tasks:
- Implement rough global gain/per-channel gain estimation.
- Implement coarse global alignment on proxies.
- Refine sub-pixel translation/affine parameters.
- Store alignment error metrics.

Acceptance:
- Test burst reports plausible sub-pixel offsets.
- Badly mismatched frames are marked low confidence or rejected.

## Phase 6: MVP fusion
Goal: produce the first real MFSR output.

Tasks:
- Implement CPU reference fusion at 1.25x and 1.5x.
- Use weighted accumulation and reference fallback.
- Cache output keyed by inputs/settings/version.
- Show final output downstream as normal image.

Acceptance:
- Static burst produces a visibly valid result.
- Single-frame fallback matches reference upscale.
- Output remains high-bit-depth scene-linear internally.

## Phase 7: Confidence masks and diagnostics
Goal: reduce ghosting and make failures explainable.

Tasks:
- Add confidence masks based on clipping, alignment error, local difference, blur, and motion disagreement.
- Add UI overlays for sample coverage/confidence.
- Add strict/normal/relaxed motion settings.

Acceptance:
- Moving regions contribute less instead of ghosting heavily.
- User can see why frames/regions were downweighted.

## Phase 8: Performance and tiled rendering
Goal: make the feature usable on large bursts.

Tasks:
- Add crop preview mode.
- Add tiled final render.
- Add cancellation/progress reporting.
- Add optional GPU backend interface.

Acceptance:
- Full-res render does not require loading every full-res float frame and accumulator at once.
- Render can be canceled safely.

## Phase 9: GPU acceleration
Goal: accelerate hot paths without changing output semantics.

Tasks:
- Accelerate warp/remap, gain application, confidence generation, and fusion accumulation.
- Keep CPU path as correctness reference.
- Add tolerance-based tests comparing CPU/GPU outputs.

Acceptance:
- GPU path is optional.
- CPU and GPU outputs match within defined tolerance.

## Phase 10: Advanced RAW CFA-aware fusion
Goal: move toward the higher-ceiling Google-style RAW burst algorithm.

Tasks:
- Implement or prototype CFA-aware accumulation/fusion before demosaic.
- Preserve per-color sample positions and CFA pattern handling.
- Compare against MVP linear-RGB fusion.

Acceptance:
- Existing UI and node contracts do not need redesign.
- Quality improves or the experiment is documented honestly as blocked/insufficient.

