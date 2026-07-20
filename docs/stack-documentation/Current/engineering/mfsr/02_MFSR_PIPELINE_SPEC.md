# Stack MFSR Pipeline Spec

## Purpose
Define the MFSR processing order, data contracts, and RAW/raster behavior.

## Shared pipeline order
1. Validate inputs.
2. Decode frame sources into MFSR frame packets.
3. Normalize orientation, active crop, black/white levels, and linear light assumptions.
4. Build low-resolution analysis proxies.
5. Select or confirm reference frame.
6. Rough brightness/exposure normalization.
7. Coarse global alignment.
8. Refined brightness/color matching using aligned overlap.
9. Fine sub-pixel/local alignment.
10. Generate motion/error/confidence masks.
11. Fuse onto high-resolution output grid.
12. Fill low-confidence gaps from reference fallback.
13. Produce high-bit-depth scene-linear result plus display preview.
14. Cache result and diagnostics.

## Frame packet contract
Every decoded input must become an `MfsrFramePacket` or equivalent internal structure.

Required fields:
- Source identity: file path/asset id, modified time/hash if available.
- Input class: RAW_MOSAIC, RAW_LINEAR, or RASTER_LINEAR.
- Width/height and active area.
- Orientation transform already resolved or explicitly stored.
- Bit depth / numeric range.
- Black level and white level where applicable.
- CFA/Bayer pattern where applicable.
- Camera/profile/color metadata where applicable.
- Linear image/proxy buffer for alignment.
- Full-quality source buffer or lazy tile access handle.

## RAW burst mode
RAW burst mode should prefer mosaiced sensor data when available. LibRaw should be used for unpacking and metadata extraction, but Stack owns the rendering path.

Compatibility requirements:
- Same camera model or explicitly compatible sensor layout.
- Same or compatible dimensions and active crop.
- Same CFA/Bayer pattern after orientation/crop normalization.
- Compatible black level, white level, and color metadata.
- Same exposure/focus/aperture preferred; exposure differences can be corrected only within limits.

MVP approach:
- Use existing Stack RAW develop/extraction to produce linear RGB frames for initial MFSR fusion, while preserving architecture for future CFA-aware fusion.
- Keep a `RawMosaicFrame` pathway in the data model so the project can later implement CFA-aware fusion without redesigning the UI/node layer.

Target approach:
- Fuse RAW CFA samples directly into the high-resolution reconstruction before the normal demosaic/color pipeline. This is the higher-ceiling version.

## Raster burst mode
Raster mode accepts already-rendered image files only if they can be converted into a known linear working space.

Compatibility requirements:
- Same dimensions or predictable crop/scale relationship. MVP may require exact dimensions.
- Same color space or successfully converted to the same working space.
- Lossless/high-bit-depth strongly preferred.
- Reject mixed gamma/unknown color data unless the user explicitly overrides in a future advanced mode.

Expected quality:
- Raster MFSR can improve detail/noise when the frames contain real sub-pixel shifts and are not already heavily sharpened/compressed.
- Raster mode has a lower ceiling than RAW mode because demosaicing and camera processing may already have thrown away or invented sampling information.

## Alignment strategy
MVP:
- Use grayscale/green-channel proxies.
- Estimate global translation/rotation/scale or affine transform.
- Refine to sub-pixel precision.
- Compute per-frame alignment error.

Future:
- Add tile-based local alignment or dense optical flow for scenes with slight perspective/parallax/motion.
- Store alignment diagnostics so users can see which frames contributed.

## Photometric matching
Brightness matching must happen twice:
- Rough global gain before alignment, so matching has a fair chance.
- Refined gain/per-channel/local matching after overlap is known.

Do not use clipped highlights, deep noisy shadows, or detected moving regions to estimate gains. Prefer robust statistics.

## Fusion strategy
MVP:
- Create output grid at Auto/1.25x/1.5x/2.0x.
- Use weighted gather or splat accumulation.
- Maintain color sum, weight sum, sample count, and confidence buffers.
- Downweight clipped, misaligned, noisy, blurred, or locally inconsistent samples.
- Fill gaps from an upscaled reference fallback.

Future:
- Add drizzle-like variable-pixel reconstruction.
- Add edge-aware/directional merging.
- Add CFA-aware direct RAW fusion.

## GPU strategy
Do not make GPU mandatory for correctness. Implement a CPU reference path first, then add GPU backends for the expensive data-parallel work.

Good GPU candidates:
- Linearization and normalization.
- Preview/proxy generation.
- Warping/remapping.
- Optical flow or tile matching.
- Gain-map application.
- Confidence masks.
- Accumulation/fusion.
- Tiled final render.

## Memory strategy
Full-frame bursts are large. Avoid loading every full-resolution RGB float frame plus every accumulator at once.

Required approach:
- Lazy decode/tile access where possible.
- Downscaled analysis proxies for initial work.
- Crop-based preview.
- Tiled final render.
- Cache intermediate analysis results separately from final pixel data.

## Output contract
The final MFSR node output should be high-bit-depth scene-linear image data, not a monitor-crushed preview. The node may expose a display-transformed preview surface for UI rendering.
