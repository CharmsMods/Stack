# Background Patcher (bgPatcher) Layer Architecture

## Purpose
The `bgPatcher` is a heavy, specialized Editor module primarily designed to remove backgrounds or isolate solid colors. Unlike simple chroma-keys, it implements a highly interactive workflow allowing users to set color tolerances, flood logic, manual brush overrides, edge modifications, and brute-force solid color overlay "patches" to scrub imagery clean of artifacts.

In the Stack C++ rebuild, this layer must be implemented as a hybrid system: complex region-growing and stroke-drawing operate on the CPU by writing to masks, which are then passed into a singular complex WebGL (OpenGL) fragment shader to calculate the final pixel outputs dynamically per frame.

---

## 1. CPU-Side Pre-Processing

The engine performs preliminary masking logic before dispatching the `bgPatcher` shader. The C++ renderer should maintain two primary floating/byte buffers per-instance for these masks.

### A. The Flood Fill Mask (`floodMaskTex`)
- **Action:** If the user enables `bgPatcherFloodFill`, the engine fires a CPU-side flood-fill algorithm.
- **Queue Logic:** Starting from user-provided sample points (`sampleX`, `sampleY`, plus additional points via an eyedropper array), a BFS queues neighboring pixels. 
- **Distance Match:** The pixel’s RGB distance from `targetColorHex` must be `≤ maxDistanceSq` (derived from `Tolerance` + `Smoothing`). 
- **Protected Colors Stop:** If the pixel falls within the tolerance range of any of the 8 user-defined *Protected Colors*, the flood immediately halts at that pixel boundary.
- **Output:** The result is copied into an 8-bit `GL_R8` texture mask representing contiguous, matching sections.

### B. The Brush Mask (`brushMaskTex`)
- **Action:** Users can manually draw explicit lines to force inclusion (`keep`) or exclusion (`remove`).
- **Rendering:** The path geometry is rendered onto an off-screen FBO. 
  - `Red (1.0, 0, 0)` implies immediate removal.
  - `Green (0, 1.0, 0)` guarantees pixel retention.
- **Parameters:** Affected dynamically by `brushRadius` and `brushHardness`.

---

## 2. GPU Shader Implementation (`bgPatcher.frag`)

The main heavy-lifting occurs in the fragment shader. The logic executes in strict sequential priority.

### Phase 1: Explicit Patching
At the very top of `main()`, the shader tests up to 32 explicit, user-defined `u_patchRects`. 
- **Logic:** If the current pixel coordinate falls within the rectangular bounds of a patch, the shader immediately bypasses all other logic, outputs the specific solid `u_patchColors`, and returns. This enables brutal clean-up of stubborn background chunks.

### Phase 2: Selection Base Mask (`getMask`)
For a given coordinate, it calculates a `centerMask`:
1. **Base Distance:** `distance(sampleRgb, u_targetColor)`
2. **Tolerance Falloff:** `smoothstep(u_tolerance, u_tolerance + u_smoothing + 0.001, dist)` is inverted to generate the `selection` value.
3. **Flood Check:** If the `u_floodMask` indicates the pixel wasn't reached, `selection = 0.0`.
4. **Invert Option:** If `u_keepSelectedRange = 1`, the selection inverts.
5. **Protected Override:** Limits the mask based on proximity to `u_protectedColors[8]` and its respective tolerances. 
6. **Brush Override:** The mask is bumped to `1.0` if hit by the red brush channel, or zeroed out if hit by the green brush channel.

### Phase 3: Morphology (Edge Shift)
If `u_edgeShift` is non-zero, it performs morphological dilation/erosion depending on the geometry of the surrounding mask.
- It iterates a maximum radius (up to 10 pixels around the UV).
- It tests `getMask` results for adjacent pixels within `u_edgeShift`.
- It calculates an `averageMask` and `maxMask` boundary, then mixes (`chokes`) the current `mask` state to physically shrink or bloat the alpha edge bounds by `u_edgeShift` pixels.

### Phase 4: Defringing
Once the exact opacity removal is known (`removedAlpha`), the shader performs color defringing to strip halos entirely. 
- It attempts to pull out the influence of `u_targetColor` based on the volume of alpha removed.
- **Math:** `(color.rgb - u_targetColor * removedAlpha * u_defringe) / max(1.0 - removedAlpha * u_defringe, 0.0001)`
- This normalizes the colors on semi-transparent edges, preventing dark/light rings.

### Phase 5: Anti-Aliasing & Color Spread
If `u_aaEnabled` is on and `u_antialias > 0.0` (acting as a spread radius), a second heavier loop evaluates all edge pixels within a 10px radius constraint.
- It hunts for nearby opaque foreground pixels (`foreground > 0.01`).
- It calculates an alpha-weighted average of nearby foreground pixels to construct a `spreadColor`. 
- It actively smears the true foreground colors outward over the transparent void left by the removed background, mixing it with a `generatedAlpha` falloff. This generates incredibly smooth, aliasing-free edges, completely filling ugly cut-outs.

### Phase 6: Output / Debug Visualization
If `u_showMask == 1`, the shader intercepts the final color output:
- Draws the main image as pure grayscale luminosity.
- Displays heavily keyed / removed areas dynamically as solid hot Pink or Cyan (indicating distances), so users visibly isolate problematic tolerance limits.
- Finally, if the mask isn't being debugged, it outputs `vec4(finalRgb, finalAlpha)`.
