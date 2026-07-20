# Stack MFSR Product + UI Spec

## Purpose
Define how MFSR appears in Stack without forcing the full feature into an ordinary real-time node.

## User-facing concept
MFSR creates a better base image from a burst of nearly identical images. The user connects the burst to an MFSR node, opens the MFSR tab, chooses or auto-detects a reference frame, checks alignment/quality diagnostics, and renders a cached result. The rendered result then behaves like a normal image source downstream in the node graph.

## Graph node behavior
Node name: `Multi-Frame Super Resolution` or `MFSR`.

Inputs:
- `Reference` input at the top. This is the anchor frame.
- `Frame 2`, `Frame 3`, etc. as dynamic additional inputs.
- Optional future input list/burst container, but do not require this for MVP.

Output:
- `Image` output: high-bit-depth scene-linear result, treated like any other Stack image output.
- Optional future diagnostic outputs: confidence map, alignment map, sample coverage map.

Validation rules:
- Once a RAW input is connected, only compatible RAW-like inputs may be connected.
- Once a raster input is connected, only compatible raster inputs may be connected.
- Mixed RAW + PNG/TIFF/EXR in one node must be rejected with a clear UI message.
- Inputs must share compatible dimensions, orientation, camera profile/color interpretation, and bit depth assumptions. For MVP, reject rather than silently guess.
- JPEG/lossy formats should be rejected in MVP or accepted only under an explicit degraded-quality warning.

## MFSR tab behavior
Add a new top-level tab with an icon. The tab edits the currently selected MFSR node. If no MFSR node exists, show a simple empty state with a button to create one in the active graph.

Main sections:

1. Input Set
   - Shows connected frames as a list with thumbnail, filename, type, dimensions, exposure metadata if available, sharpness score, alignment status, and enabled/disabled toggle.
   - Shows which frame is the reference.
   - Allows `Auto-select reference` and manual reference override.

2. Analysis
   - Button: `Analyze Burst`.
   - Displays per-frame compatibility, brightness gain, estimated shift/transform, sharpness, and confidence.
   - Does not run full-resolution fusion automatically.

3. Preview
   - Shows reference preview, MFSR preview crop, difference/alignment overlay, and confidence overlay.
   - Preview should be downscaled or crop-based to keep UI responsive.

4. Render Settings
   - Quality preset: `Preview`, `Balanced`, `High Quality`, `Experimental`.
   - Output scale: `Auto`, `1.25x`, `1.5x`, `2.0x`.
   - Alignment: `Global`, `Global + Local`.
   - Brightness matching: `Auto`, `Global gain`, `Per-channel gain`, `Local tile gain`.
   - Motion handling: `Normal`, `Strict`, `Relaxed`.
   - Post-fusion cleanup: `Off` by default. Optional future controls for mild deconvolution/cleanup, but do not make this required for MVP.

5. Render + Cache
   - Button: `Render MFSR Result`.
   - Show progress by stage: decode, normalize, analyze, align, confidence, fuse, finalize, cache.
   - Result is cached by input file identity/hash + settings + Stack algorithm version.

## Preview/display transform
The node's internal output should remain high-bit-depth scene-linear data. The UI should apply a view transform for display so the preview does not look crushed/clipped on a monitor. Do not bake the display transform into the downstream node output unless the user is explicitly exporting to a display-referred format.

## MVP non-goals
- No AI hallucination/upscaling.
- No real-time full-resolution rendering.
- No mixed RAW/raster burst processing.
- No full manual expert panel with dozens of parameters.
- No required denoise/deblur pass.
