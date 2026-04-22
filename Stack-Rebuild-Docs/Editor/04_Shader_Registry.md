# OpenGL Rendering Modules (Shaders Registry)

> **C++ Architecture Context:** Standard OpenGL natively requires Fragment Shaders (written in GLSL) to execute parallel operations on the GPU. While an application can compile them from inline C++ strings, keeping `.frag` files as external, interchangeable assets is a common and required architecture convention for a shader-driven engine. Therefore, these exact shaders must be migrated, compiled via `glCompileShader`, and executed by the native stack pipeline.

This document catalogs every single node operation within the engine, treating them by their proper name as sequential **Rendering Modules**.

### Adjustments `adjust.frag`
- **Role:** Base Engine Module
- **Execution Description:** Core tonal and sharpening controls.
- **Controllable Engine Parameters:** Brightness, Contrast, Saturation, Warmth, Sharpening, Sharpen Threshold

### Mask Component / GPU Utility for Adjustments `adjustMasked.frag`
- **Role:** Associated Sub-Module
- **Execution Description:** Internal rendering utility shader for masking variations or sub-passes.

### Airy Bloom `airyBloom.frag`
- **Role:** Base Engine Module
- **Execution Description:** Diffraction-inspired bloom and glow.
- **Controllable Engine Parameters:** Enable, Intensity, Aperture, Threshold, Threshold Fade, Cutoff

### Core Engine Utility `alphaProtect.frag`
- **Role:** Core Math Shader
- **Execution Description:** Low-level shader pipeline component like masking math, raw copying, or final sRGB conversions.

### Analog Video `analog.frag`
- **Role:** Base Engine Module
- **Execution Description:** VHS and CRT-inspired video degradation.
- **Controllable Engine Parameters:** Enable, Tape Wobble, Color Bleed, CRT Curve, Scanline Noise

### Background Remover & Patcher `bgPatcher.frag`
- **Role:** Base Engine Module
- **Execution Description:** Color-key transparency removal with protected colors, contiguous masking, and square patch tools.
- **Controllable Engine Parameters:** Flood Fill, Explicit Patches, Tolerances, Defringe Radios

### Bilateral Filter `bilateral.frag`
- **Role:** Base Engine Module
- **Execution Description:** Edge-aware smoothing with optional masks.
- **Controllable Engine Parameters:** Enable, Radius, Sigma Color, Sigma Space, Kernel, Edge Mode, Iterations

### Blur `blur.frag`
- **Role:** Base Engine Module
- **Execution Description:** Gaussian, box, and motion blur with masking support.
- **Controllable Engine Parameters:** Enable, Amount, Type

### Core Engine Utility `brush.frag`
- **Role:** Core Math Shader
- **Execution Description:** Low-level shader pipeline component like masking math, raw copying, or final sRGB conversions.

### Cell Shading `cell.frag`
- **Role:** Base Engine Module
- **Execution Description:** Posterization, banding, and outline rendering.
- **Controllable Engine Parameters:** Enable, Shading Levels, Contrast Bias, Gamma, Quantization Mode, Band Mapping, Edge Method, Edge Strength, Edge Thickness, Color Preserve, Show Edges

### Chromatic Aberration `chroma.frag`
- **Role:** Base Engine Module
- **Execution Description:** Chromatic split, edge blur, and center control.
- **Controllable Engine Parameters:** Amount, Edge Blur, Zoom Blur, Link Falloff to Blur, Center Pin, Radius, Falloff, Reset Center

### 3-Way Color Grade `colorGrade.frag`
- **Role:** Base Engine Module
- **Execution Description:** Three-way color balance for shadows, midtones, and highlights.
- **Controllable Engine Parameters:** Strength

### Core Engine Utility `colorMask.frag`
- **Role:** Core Math Shader
- **Execution Description:** Low-level shader pipeline component like masking math, raw copying, or final sRGB conversions.

### Core Engine Utility `composite.frag`
- **Role:** Core Math Shader
- **Execution Description:** Low-level shader pipeline component like masking math, raw copying, or final sRGB conversions.

### Compression `compression.frag`
- **Role:** Base Engine Module
- **Execution Description:** Lossy block compression artifacts and degradation.
- **Controllable Engine Parameters:** Enable, Method, Quality, Block Size, Blend, Iterations

### Core Engine Utility `copy.frag`
- **Role:** Core Math Shader
- **Execution Description:** Low-level shader pipeline component like masking math, raw copying, or final sRGB conversions.

### Corruption `corruption.frag`
- **Role:** Base Engine Module
- **Execution Description:** Digital breakup and corruption effects.
- **Controllable Engine Parameters:** Enable, Algorithm, Quality

### Crop / Rotate / Flip `cropTransform.frag`
- **Role:** Base Engine Module
- **Execution Description:** Non-destructively crops, rotates, and mirrors the image while keeping the current canvas size.
- **Controllable Engine Parameters:** Crop Left (%), Crop Right (%), Crop Top (%), Crop Bottom (%), Rotation (deg), Flip Horizontally, Flip Vertically

### Denoising `denoise.frag`
- **Role:** Base Engine Module
- **Execution Description:** Spatial noise reduction for cleanup and smoothing.
- **Controllable Engine Parameters:** Enable, Mode, Search Radius, Patch Radius, Filter Strength, Blend

### Dithering `dither.frag`
- **Role:** Base Engine Module
- **Execution Description:** Bit depth reduction and ordered or stochastic dithering.
- **Controllable Engine Parameters:** Enable, Bit Depth, Palette Size, Strength, Scale, Type, Use Studio Palette, Gamma Correct

### Mask Component / GPU Utility for DNG Develop `dngComposite.frag`
- **Role:** Associated Sub-Module
- **Execution Description:** Internal rendering utility shader for masking variations or sub-passes.

### DNG Develop `dngDevelop.frag`
- **Role:** Base Engine Module
- **Execution Description:** Develops embedded DNG raw sources into the Editor raster pipeline.
- **Controllable Engine Parameters:** Full RAW Decoder Params (Exposure, Tint, Interpretation, etc.)

### Edge Effects `edge.frag`
- **Role:** Base Engine Module
- **Execution Description:** Edge overlays and saturation-mask style processing.
- **Controllable Engine Parameters:** Enable, Blend, Mode, Strength, Tolerance, Saturation Mask Controls

### Expander `expander.frag`
- **Role:** Base Engine Module
- **Execution Description:** Adds equal outfill on every side without scaling the image content.
- **Controllable Engine Parameters:** Padding Factor, RGBA Outfill Data

### Core Engine Utility `final.frag`
- **Role:** Core Math Shader
- **Execution Description:** Low-level shader pipeline component like masking math, raw copying, or final sRGB conversions.

### Glare Rays `glareRays.frag`
- **Role:** Base Engine Module
- **Execution Description:** Directional bloom streaks and starburst response.
- **Controllable Engine Parameters:** Enable Rays, Intensity, Ray Count, Ray Length, Ray Blur

### Halftoning `halftone.frag`
- **Role:** Base Engine Module
- **Execution Description:** Print-like dot and screen stylization.
- **Controllable Engine Parameters:** Enable, Dot Size, Intensity, Sharpness, Pattern, Color Mode, Sampling, Grayscale Input, Lock Dot Grid, Invert

### Radial Hankel Blur `hankelBlur.frag`
- **Role:** Base Engine Module
- **Controllable Engine Parameters:** Enable Blur, Intensity, Radius, Quality

### Heatwave & Ripples `heatwave.frag`
- **Role:** Base Engine Module
- **Execution Description:** Static refractive warping and ripple distortion.
- **Controllable Engine Parameters:** Enable, Intensity, Phase, Scale, Direction

### Image Breaks `imageBreaks.frag`
- **Role:** Base Engine Module
- **Execution Description:** Shifts rows and columns of pixels to create a broken image glitch effect.
- **Controllable Engine Parameters:** Enable, Columns, Rows, Horizontal Shift, Vertical Shift, Shift Edge Blur, Random Seed, Square Density, Grid Size, Square Distance, Square Edge Blur

### Core Engine Utility `invert.frag`
- **Role:** Core Math Shader
- **Execution Description:** Low-level shader pipeline component like masking math, raw copying, or final sRGB conversions.

### Lens Distortion `lens.frag`
- **Role:** Base Engine Module
- **Execution Description:** Barrel and pincushion style optical distortion.
- **Controllable Engine Parameters:** Distortion Amount, Scale Base

### Light Leaks `lightLeaks.frag`
- **Role:** Core Math Shader
- **Execution Description:** Mathematically generated additive color streaks and film leak burns.

### Core Engine Utility `mask.frag`
- **Role:** Core Math Shader
- **Execution Description:** Low-level shader pipeline component like masking math, raw copying, or final sRGB conversions.

### Mask Component / GPU Utility for Blur `maskedBlur.frag`
- **Role:** Associated Sub-Module
- **Execution Description:** Internal rendering utility shader for masking variations or sub-passes.

### Mask Component / GPU Utility for Dithering `maskedDither.frag`
- **Role:** Associated Sub-Module
- **Execution Description:** Internal rendering utility shader for masking variations or sub-passes.

### Noise `noise.frag`
- **Role:** Base Engine Module
- **Execution Description:** Procedural grain and texture generation.
- **Controllable Engine Parameters:** Noise Strength, Noise Type, Sat Strength, Sat Impact, Param A, Param B, Param C, Scale (Size), Blurriness, Blend Mode, Opacity, Skin Protection

### Palette Reconstructor `palette.frag`
- **Role:** Base Engine Module
- **Execution Description:** Restricts output to a curated color palette.
- **Controllable Engine Parameters:** Enable, Global Blend, Smoothing Type, Palette Smoothing, Extract Count

### Radial Math Generator `radial.frag`
- **Role:** Core Math Shader
- **Execution Description:** Radial optical overlays and vignette gradient falloffs.

### Text Overlay Compositor `textOverlay.frag`
- **Role:** Core Math Shader
- **Execution Description:** Explicitly composites an affine-transformed glyph surface texture.

### Tilt-Shift Blur `tiltShiftBlur.frag`
- **Role:** Base Engine Module
- **Execution Description:** Adjustable focal point Depth-of-Field blur with variable radius scaling.
- **Controllable Engine Parameters:** Enable Tilt-Shift Blur, Edit Focus Point, Blur Filter Type, Blur Strength, Focus Radius, Focus Falloff Transition

### Vignette & Focus `vignette.frag`
- **Role:** Base Engine Module
- **Execution Description:** Framing, darkening, and focus shaping.
- **Controllable Engine Parameters:** Intensity, Radius, Softness, Color

### Global Plane Vertex Geometry `vs-quad.vert`
- **Role:** Vertex Shader
- **Execution Description:** The singular 2D geometry vertex that maps the flat plane across screen space for fragments to draw onto.

