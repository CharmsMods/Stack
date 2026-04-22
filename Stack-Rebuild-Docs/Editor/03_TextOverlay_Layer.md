# Text Overlay (`textOverlay`) Architecture

## Purpose
The `textOverlay` module injects user-defined typography directly into the node-based rendering stack. It is fundamentally non-destructive and renders in-stream, meaning downstream layers (like `noise`, `blur`, or `cropTransform`) will manipulate the rasterized text seamlessly along with the rest of the image.

In the Stack C++ rebuild, this layer bypasses traditional static UI labels and requires an explicit rendering path from raw font files (e.g., FreeType) into the active OpenGL context. 

---

## 1. The Rendering Pipeline

The current implementation treats text rendering as a hybrid CPU-to-GPU process, executing in three primary steps. The C++ rebuild must adopt this to guarantee the text interacts properly with the rendering dimensions.

### Step 1: Glyph Generation & Measurement (`ensureTextLayerAsset`)
1. **Measurement:** The engine receives parameters: `textContent`, `textFontFamily`, `textFontSize`, `textColor`. It calculates the `width`, `ascent`, and `descent` using a logical context.
2. **Bounds Rasterization:** A tight-fitting 2D buffer is created explicitly matching these bounding dimensions. The engine draws the text tightly onto this transparent buffer. 

*C++ Implementation Note: Use FreeType (or `ImFont`) to build an atlas or a perfectly sized texture buffer of the specific text sequence. The exact bounding box metrics are cached to a hash derived from the explicit text string and font properties.*

### Step 2: Surface Projection (`ensureTextLayerSurfaceAsset`)
To ensure text scales appropriately regardless of the source image density, it calculates projection factors against the engine’s **logical** render chain vs the actual **physical** output resolution.
1. **Transformation:** The system constructs a new transparent buffer that matches the exact physical dimensions of the active render step (`inputResolution.w` by `inputResolution.h`).
2. **CPU Affine Transforms:** It shifts the context to `(textX + width*0.5, textY + height*0.5)`, rotates the context by `-textRotation` degrees, and places the tightly bound glyph texture from Step 1 onto this full-screen buffer.
3. **Upload:** It uploads the entire screen-sized, pre-transformed transparent buffer into an OpenGL texture (`surfaceTexture`).

*C++ Implementation Note: Performing affine transforms on the CPU per-frame is inefficient in C++. In the native rebuild, you should optimize this bypass: Create the tightly-bound OpenGL texture from Step 1, and map it to a flat 3D quad inside the scene, utilizing standard OpenGL Model-View-Projection matrices to translate, rotate, and scale the quad perfectly, outputting directly to the FBO.*

### Step 3: GPU Compositing (`textOverlay.frag`)
The Fragment Shader is exceptionally lightweight because the heavy lifting (rasterization and transformation) was completed off-pipeline.
- **Inputs:** `u_base` (the image context so far), `u_overlay` (the uploaded surface texture), `u_opacity`.
- **Composite Math:** Standard alpha lerping.
  ```glsl
  vec4 overlay = texture(u_overlay, v_uv);
  float alpha = clamp(overlay.a * u_opacity, 0.0, 1.0);
  vec3 rgb = (overlay.rgb * alpha) + (base.rgb * (1.0 - alpha));
  float outAlpha = alpha + (base.a * (1.0 - alpha));
  ```
- **Scale Safety:** The shader accepts `u_rotationDegrees`, but currently defaults to `0` because rotation is executed in Step 2.

---

## 2. Interactive UX / Bounding Box (UI Layer)

Currently, users do not interact with text positions via a sidebar tab (like `Selected` sliders). Instead:
- An absolute-positioned, invisible DOM overlay synchronizes with the `textX`, `textY`, `width`, and `height` properties in the engine's coordinate space.
- Users manipulate a physical bounding box (drag to translate, pull corner handles to scale, pull rotational handle to spin).
- When dragged, these delta values fire UI dispatched updates to the layer registry, modifying `textX`/`textY`/`textRotation` directly.

*C++ Implementation Note: You must build an ImGui invisible interactive gizmo that hovers over the active viewport stage. This gizmo must maintain a direct two-way mapping between ImGui window coordinates and the internal node-engine image coordinates, accounting for the current `zoom` and `pan` states of the canvas.*
