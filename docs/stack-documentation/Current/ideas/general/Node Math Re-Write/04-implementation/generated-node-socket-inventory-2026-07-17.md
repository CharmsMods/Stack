# Generated Stack Node Socket Inventory

- Generated: 2026-07-17
- Generated from: live node catalog, hidden layer registry entries, non-browser node kinds, shipped compound definitions, and `EditorNodeGraph::Graph::GetSockets(..., false)`
- Regenerate: `Stack.exe --write-node-socket-catalog <absolute-output-path>`

This is a 2026-07-17 generated snapshot, not the live source of truth and not
the future channel-based vocabulary contract. Every row is normalized through
the presentation glossary used by the graph UI at that date. `Unknown` is
deliberate where Stack did not know a channel shape or units. Custom compounds
use their exact saved public interface and are audited at runtime; the shipped
compounds below demonstrate that dynamic path.

| Node | Category | Direction | Socket ID | Short label | Role key | Logical type | Channel/component shape | Units | Optionality | Visibility tier |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Output | Input / Output | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Output | Input / Output | Input | `r` | R | `red-channel` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Output | Input / Output | Input | `g` | G | `green-channel` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Output | Input / Output | Input | `b` | B | `blue-channel` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Output | Input / Output | Input | `a` | A | `alpha-channel` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Crop (Needs Fix) | Transform / Canvas | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Crop (Needs Fix) | Transform / Canvas | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Crop (Needs Fix) | Transform / Canvas | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Rotate (Needs Fix) | Transform / Canvas | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Rotate (Needs Fix) | Transform / Canvas | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Rotate (Needs Fix) | Transform / Canvas | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Flip | Transform / Canvas | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Flip | Transform / Canvas | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Flip | Transform / Canvas | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Expand Canvas (Experimental) | Transform / Canvas | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Expand Canvas (Experimental) | Transform / Canvas | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Expand Canvas (Experimental) | Transform / Canvas | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Background Remover (Needs Fix) | Composite | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Background Remover (Needs Fix) | Composite | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Background Remover (Needs Fix) | Composite | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Brightness | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Brightness | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Brightness | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Contrast | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Contrast | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Contrast | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Saturation (Needs Fix) | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Saturation (Needs Fix) | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Saturation (Needs Fix) | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Warmth (Needs Fix) | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Warmth (Needs Fix) | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Warmth (Needs Fix) | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Sharpen | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Sharpen | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Sharpen | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| 3-Way Color Grade | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| 3-Way Color Grade | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| 3-Way Color Grade | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| HDR Compressor (Needs Fix) | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| HDR Compressor (Needs Fix) | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| HDR Compressor (Needs Fix) | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Tone Curve (Needs Fix) | Color / Tone | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Tone Curve (Needs Fix) | Color / Tone | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Tone Curve (Needs Fix) | Color / Tone | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| View Transform | Color / Tone | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| View Transform | Color / Tone | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| View Transform | Color / Tone | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Box Blur | Blur / Focus | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Box Blur | Blur / Focus | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Box Blur | Blur / Focus | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Gaussian Blur | Blur / Focus | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Gaussian Blur | Blur / Focus | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Gaussian Blur | Blur / Focus | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Classical RGB Denoise (Experimental) | Blur / Focus | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Classical RGB Denoise (Experimental) | Blur / Focus | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Classical RGB Denoise (Experimental) | Blur / Focus | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Scene Denoise | Blur / Focus | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Scene Denoise | Blur / Focus | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Scene Denoise | Blur / Focus | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Linear RGB Neural Denoise (Experimental) | Blur / Focus | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Linear RGB Neural Denoise (Experimental) | Blur / Focus | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Linear RGB Neural Denoise (Experimental) | Blur / Focus | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Utility NLM Denoise | Blur / Focus | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Utility NLM Denoise | Blur / Focus | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Utility NLM Denoise | Blur / Focus | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Utility Median Denoise | Blur / Focus | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Utility Median Denoise | Blur / Focus | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Utility Median Denoise | Blur / Focus | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Utility Mean Denoise | Blur / Focus | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Utility Mean Denoise | Blur / Focus | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Utility Mean Denoise | Blur / Focus | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Bilateral Filter | Blur / Focus | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Bilateral Filter | Blur / Focus | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Bilateral Filter | Blur / Focus | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Noise | Texture / Generate | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Noise | Texture / Generate | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Noise | Texture / Generate | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Tilt-Shift Blur | Blur / Focus | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Tilt-Shift Blur | Blur / Focus | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Tilt-Shift Blur | Blur / Focus | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Optical Blur | Blur / Focus | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Optical Blur | Blur / Focus | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Optical Blur | Blur / Focus | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Ordered Dither 8x8 | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Ordered Dither 8x8 | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Ordered Dither 8x8 | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Error Diffusion Dither | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Error Diffusion Dither | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Error Diffusion Dither | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| White Noise Dither | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| White Noise Dither | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| White Noise Dither | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Ordered Dither 4x4 | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Ordered Dither 4x4 | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Ordered Dither 4x4 | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Ordered Dither 2x2 | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Ordered Dither 2x2 | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Ordered Dither 2x2 | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Interleaved Gradient Dither | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Interleaved Gradient Dither | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Interleaved Gradient Dither | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Halftone | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Halftone | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Halftone | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Cell Shading (Needs Fix) | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Cell Shading (Needs Fix) | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Cell Shading (Needs Fix) | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Palette Rebuild (Needs Fix) | Color | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Palette Rebuild (Needs Fix) | Color | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Palette Rebuild (Needs Fix) | Color | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Edge Overlay | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Edge Overlay | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Edge Overlay | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Edge Saturation Mask (Needs Fix) | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Edge Saturation Mask (Needs Fix) | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Edge Saturation Mask (Needs Fix) | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| DCT Compression | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| DCT Compression | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| DCT Compression | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Chroma Subsample Compression (Needs Fix) | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Chroma Subsample Compression (Needs Fix) | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Chroma Subsample Compression (Needs Fix) | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Wavelet Compression | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Wavelet Compression | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Wavelet Compression | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| JPEG Blocks | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| JPEG Blocks | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| JPEG Blocks | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Pixelation | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Pixelation | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Pixelation | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Color Bleed (Needs Fix) | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Color Bleed (Needs Fix) | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Color Bleed (Needs Fix) | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Block Shift | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Block Shift | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Block Shift | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Analog Video (VHS/CRT) | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Analog Video (VHS/CRT) | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Analog Video (VHS/CRT) | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Vignette | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Vignette | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Vignette | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Glare Rays | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Glare Rays | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Glare Rays | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Chromatic Aberration (Needs Fix) | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Chromatic Aberration (Needs Fix) | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Chromatic Aberration (Needs Fix) | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Lens Distortion | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Lens Distortion | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Lens Distortion | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Heatwave Distortion | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Heatwave Distortion | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Heatwave Distortion | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Ripple Distortion | Effects / Damage | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Ripple Distortion | Effects / Damage | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Ripple Distortion | Effects / Damage | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Airy Bloom | Blur / Focus | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Airy Bloom | Blur / Focus | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Airy Bloom | Blur / Focus | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Histogram | Input / Output | Input | `scopeIn` | Scope | `scope` | Analysis data | Not applicable | Not applicable | Required | Required |
| Vectorscope | Input / Output | Input | `scopeIn` | Scope | `scope` | Analysis data | Not applicable | Not applicable | Required | Required |
| RGB Parade | Input / Output | Input | `scopeIn` | Scope | `scope` | Analysis data | Not applicable | Not applicable | Required | Required |
| Preview | Input / Output | Input | `previewIn` | Image / Mask | `image-mask` | Analysis data | Not applicable | Not applicable | Required | Required |
| RAW Development | Input / Output | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| RAW/CFA Neural Denoise | Input / Output | Input | `rawIn` | RAW | `raw-image-data` | RAW image data | Not applicable | Not applicable | Required | Required |
| RAW/CFA Neural Denoise | Input / Output | Output | `rawOut` | RAW | `raw-image-data` | RAW image data | Not applicable | Not applicable | Required | Required |
| RAW Decode | Input / Output | Input | `rawIn` | RAW | `raw-image-data` | RAW image data | Not applicable | Not applicable | Required | Required |
| RAW Decode | Input / Output | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Develop (Advanced Auto) | Advanced RAW | Input | `rawIn` | RAW | `raw-image-data` | RAW image data | Not applicable | Not applicable | Required | Required |
| Develop (Advanced Auto) | Advanced RAW | Input | `maskIn` | Finish Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Develop (Advanced Auto) | Advanced RAW | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Develop (Advanced Auto) | Advanced RAW | Output | `preFinishImageOut` | Pre-Finish | `pre-finish` | Color image | Unknown channels | Not applicable | Required | Advanced |
| HDR Merge | Input / Output | Input | `image1` | Image 1 | `image-1` | Color image | Unknown channels | Not applicable | Required | Required |
| HDR Merge | Input / Output | Input | `image2` | Image 2 | `image-2` | Color image | Unknown channels | Not applicable | Optional | Optional |
| HDR Merge | Input / Output | Input | `image3` | Image 3 | `image-3` | Color image | Unknown channels | Not applicable | Optional | Optional |
| HDR Merge | Input / Output | Output | `imageOut` | Scene HDR | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| MFSR | Input / Output | Input | `reference` | Reference | `reference` | Color image | Unknown channels | Not applicable | Required | Required |
| MFSR | Input / Output | Input | `frame2` | Frame 2 | `frame-2` | Color image | Unknown channels | Not applicable | Optional | Optional |
| MFSR | Input / Output | Input | `frame3` | Frame 3 | `frame-3` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| MFSR | Input / Output | Input | `frame4` | Frame 4 | `frame-4` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| MFSR | Input / Output | Input | `frame5` | Frame 5 | `frame-5` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| MFSR | Input / Output | Input | `frame6` | Frame 6 | `frame-6` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| MFSR | Input / Output | Input | `frame7` | Frame 7 | `frame-7` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| MFSR | Input / Output | Input | `frame8` | Frame 8 | `frame-8` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| MFSR | Input / Output | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Custom Mask | Masks | Output | `maskOut` | Mask | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Solid Mask | Masks | Output | `maskOut` | Mask | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Linear Gradient Mask | Masks | Output | `maskOut` | Mask | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Radial Gradient Mask | Masks | Output | `maskOut` | Mask | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Noise Mask | Masks | Output | `maskOut` | Mask | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Add Mask | Mask / Math | Input | `maskA` | Mask A | `mask-a` | Mask | 1 channel | Unitless | Required | Required |
| Add Mask | Mask / Math | Input | `maskB` | Mask B | `mask-b` | Mask | 1 channel | Unitless | Required | Required |
| Add Mask | Mask / Math | Output | `maskOut` | Mask Out | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Subtract Mask | Mask / Math | Input | `maskA` | Mask A | `mask-a` | Mask | 1 channel | Unitless | Required | Required |
| Subtract Mask | Mask / Math | Input | `maskB` | Mask B | `mask-b` | Mask | 1 channel | Unitless | Required | Required |
| Subtract Mask | Mask / Math | Output | `maskOut` | Mask Out | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Intersect Mask | Mask / Math | Input | `maskA` | Mask A | `mask-a` | Mask | 1 channel | Unitless | Required | Required |
| Intersect Mask | Mask / Math | Input | `maskB` | Mask B | `mask-b` | Mask | 1 channel | Unitless | Required | Required |
| Intersect Mask | Mask / Math | Output | `maskOut` | Mask Out | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Difference Mask | Mask / Math | Input | `maskA` | Mask A | `mask-a` | Mask | 1 channel | Unitless | Required | Required |
| Difference Mask | Mask / Math | Input | `maskB` | Mask B | `mask-b` | Mask | 1 channel | Unitless | Required | Required |
| Difference Mask | Mask / Math | Output | `maskOut` | Mask Out | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Invert Mask | Mask / Math | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Invert Mask | Mask / Math | Output | `maskOut` | Mask Out | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Remap Mask | Mask / Math | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Remap Mask | Mask / Math | Output | `maskOut` | Mask Out | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Threshold Mask | Mask / Math | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Threshold Mask | Mask / Math | Output | `maskOut` | Mask Out | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Luminance Mask | Mask / Math | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Luminance Mask | Mask / Math | Output | `maskOut` | Mask Out | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Sampled Range Mask | Mask / Math | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Sampled Range Mask | Mask / Math | Output | `maskOut` | Mask Out | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Clamp | Mask / Math | Input | `imageA` | Data A | `data-a` | Color image | Unknown channels | Not applicable | Required | Required |
| Clamp | Mask / Math | Input | `imageB` | Data B | `data-b` | Color image | Unknown channels | Not applicable | Optional | Optional |
| Clamp | Mask / Math | Input | `baseIn` | Base | `base` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| Clamp | Mask / Math | Input | `maskIn` | Mask | `mask` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Clamp | Mask / Math | Output | `imageOut` | Data Out | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Add | Mask / Math | Input | `imageA` | Data A | `data-a` | Color image | Unknown channels | Not applicable | Required | Required |
| Add | Mask / Math | Input | `imageB` | Data B | `data-b` | Color image | Unknown channels | Not applicable | Optional | Optional |
| Add | Mask / Math | Input | `baseIn` | Base | `base` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| Add | Mask / Math | Input | `maskIn` | Mask | `mask` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Add | Mask / Math | Output | `imageOut` | Data Out | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Subtract | Mask / Math | Input | `imageA` | Data A | `data-a` | Color image | Unknown channels | Not applicable | Required | Required |
| Subtract | Mask / Math | Input | `imageB` | Data B | `data-b` | Color image | Unknown channels | Not applicable | Optional | Optional |
| Subtract | Mask / Math | Input | `baseIn` | Base | `base` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| Subtract | Mask / Math | Input | `maskIn` | Mask | `mask` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Subtract | Mask / Math | Output | `imageOut` | Data Out | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Multiply | Mask / Math | Input | `imageA` | Data A | `data-a` | Color image | Unknown channels | Not applicable | Required | Required |
| Multiply | Mask / Math | Input | `imageB` | Data B | `data-b` | Color image | Unknown channels | Not applicable | Optional | Optional |
| Multiply | Mask / Math | Input | `baseIn` | Base | `base` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| Multiply | Mask / Math | Input | `maskIn` | Mask | `mask` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Multiply | Mask / Math | Output | `imageOut` | Data Out | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Divide | Mask / Math | Input | `imageA` | Data A | `data-a` | Color image | Unknown channels | Not applicable | Required | Required |
| Divide | Mask / Math | Input | `imageB` | Data B | `data-b` | Color image | Unknown channels | Not applicable | Optional | Optional |
| Divide | Mask / Math | Input | `baseIn` | Base | `base` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| Divide | Mask / Math | Input | `maskIn` | Mask | `mask` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Divide | Mask / Math | Output | `imageOut` | Data Out | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Average | Mask / Math | Input | `imageA` | Scalar A | `scalar-a` | Scalar field | 1 channel | Unknown units | Required | Required |
| Average | Mask / Math | Input | `imageB` | Scalar B | `scalar-b` | Scalar field | 1 channel | Unknown units | Optional | Optional |
| Average | Mask / Math | Input | `imageC` | Scalar C | `scalar-c` | Scalar field | 1 channel | Unknown units | Optional | Optional |
| Average | Mask / Math | Output | `imageOut` | Scalar Out | `image` | Scalar field | 1 channel | Unknown units | Required | Required |
| Minimum | Mask / Math | Input | `imageA` | Data A | `data-a` | Color image | Unknown channels | Not applicable | Required | Required |
| Minimum | Mask / Math | Input | `imageB` | Data B | `data-b` | Color image | Unknown channels | Not applicable | Optional | Optional |
| Minimum | Mask / Math | Input | `baseIn` | Base | `base` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| Minimum | Mask / Math | Input | `maskIn` | Mask | `mask` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Minimum | Mask / Math | Output | `imageOut` | Data Out | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Maximum | Mask / Math | Input | `imageA` | Data A | `data-a` | Color image | Unknown channels | Not applicable | Required | Required |
| Maximum | Mask / Math | Input | `imageB` | Data B | `data-b` | Color image | Unknown channels | Not applicable | Optional | Optional |
| Maximum | Mask / Math | Input | `baseIn` | Base | `base` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| Maximum | Mask / Math | Input | `maskIn` | Mask | `mask` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Maximum | Mask / Math | Output | `imageOut` | Data Out | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Difference | Mask / Math | Input | `imageA` | Data A | `data-a` | Color image | Unknown channels | Not applicable | Required | Required |
| Difference | Mask / Math | Input | `imageB` | Data B | `data-b` | Color image | Unknown channels | Not applicable | Optional | Optional |
| Difference | Mask / Math | Input | `baseIn` | Base | `base` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| Difference | Mask / Math | Input | `maskIn` | Mask | `mask` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Difference | Mask / Math | Output | `imageOut` | Data Out | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Remap | Mask / Math | Input | `imageA` | Data A | `data-a` | Color image | Unknown channels | Not applicable | Required | Required |
| Remap | Mask / Math | Input | `imageB` | Data B | `data-b` | Color image | Unknown channels | Not applicable | Optional | Optional |
| Remap | Mask / Math | Input | `baseIn` | Base | `base` | Color image | Unknown channels | Not applicable | Optional | Advanced |
| Remap | Mask / Math | Input | `maskIn` | Mask | `mask` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Remap | Mask / Math | Output | `imageOut` | Data Out | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Average Images | Image Operations | Input | `imageA` | Data A | `data-a` | Color image | Unknown channels | Not applicable | Required | Required |
| Average Images | Image Operations | Input | `imageB` | Data B | `data-b` | Color image | Unknown channels | Not applicable | Optional | Optional |
| Average Images | Image Operations | Input | `imageC` | Data C | `data-c` | Color image | Unknown channels | Not applicable | Optional | Optional |
| Average Images | Image Operations | Output | `imageOut` | Data Out | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Boolean Value | Values | Output | `valueOut` | Value | `value` | Boolean | Not applicable | Not applicable | Required | Required |
| Integer Value | Values | Output | `valueOut` | Value | `value` | Integer | Not applicable | Not applicable | Required | Required |
| Scalar Value | Values | Output | `valueOut` | Value | `value` | Scalar | Not applicable | Unknown units | Required | Required |
| Vector 2 Value | Values | Output | `valueOut` | Value | `value` | 2-number vector | 2 components | Unknown units | Required | Required |
| Vector 3 Value | Values | Output | `valueOut` | Value | `value` | 3-number vector | 3 components | Unknown units | Required | Required |
| Vector 4 Value | Values | Output | `valueOut` | Value | `value` | 4-number vector | 4 components | Unknown units | Required | Required |
| Matrix 3x3 Value | Values | Output | `valueOut` | Value | `value` | 3 x 3 matrix | Not applicable | Not applicable | Required | Required |
| Matrix 4x4 Value | Values | Output | `valueOut` | Value | `value` | 4 x 4 matrix | Not applicable | Not applicable | Required | Required |
| Coordinate Value | Values | Output | `valueOut` | Value | `value` | 2D coordinate | 2 components | Unknown units | Required | Required |
| Curve Value | Values | Output | `valueOut` | Value | `value` | Curve | Not applicable | Not applicable | Required | Required |
| Field Mean | Analysis / Measure | Input | `fieldIn` | Field | `field` | Scalar field | 1 channel | Unknown units | Required | Required |
| Field Mean | Analysis / Measure | Output | `valueOut` | Mean | `mean` | Scalar | Not applicable | Unknown units | Required | Required |
| Reformat | Geometry / Transform | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Reformat | Geometry / Transform | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Assign sRGB | Image Technical | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Assign sRGB | Image Technical | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Assign Linear sRGB | Image Technical | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Assign Linear sRGB | Image Technical | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Assign Linear Display-P3 | Image Technical | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Assign Linear Display-P3 | Image Technical | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| sRGB Decode | Image Technical | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| sRGB Decode | Image Technical | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| sRGB Encode | Image Technical | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| sRGB Encode | Image Technical | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Linear sRGB to Display-P3 | Image Technical | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Linear sRGB to Display-P3 | Image Technical | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Linear Display-P3 to sRGB | Image Technical | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Linear Display-P3 to sRGB | Image Technical | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Exposure (EV) | Image Technical | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Exposure (EV) | Image Technical | Input | `evIn` | EV | `exposure` | Scalar | Not applicable | EV | Optional | Optional |
| Exposure (EV) | Image Technical | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Premultiply | Image Technical | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Premultiply | Image Technical | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Unpremultiply | Image Technical | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Unpremultiply | Image Technical | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| FFT | Frequency | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| FFT | Frequency | Output | `imageOut` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Inverse FFT | Frequency | Input | `imageIn` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Inverse FFT | Frequency | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Spectrum View | Frequency | Input | `imageIn` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Spectrum View | Frequency | Output | `imageOut` | View | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Low Pass | Frequency | Output | `maskOut` | Filter | `mask` | Mask | 1 channel | Unitless | Required | Required |
| High Pass | Frequency | Output | `maskOut` | Filter | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Band Pass | Frequency | Output | `maskOut` | Filter | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Band Stop | Frequency | Output | `maskOut` | Filter | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Notch | Frequency | Output | `maskOut` | Filter | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Gaussian | Frequency | Output | `maskOut` | Filter | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Butterworth | Frequency | Output | `maskOut` | Filter | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Filter Spectrum | Frequency | Input | `imageA` | Spectrum A | `spectrum-a` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Filter Spectrum | Frequency | Input | `imageB` | Spectrum B | `spectrum-b` | Complex spectrum | 2 components | Not applicable | Optional | Optional |
| Filter Spectrum | Frequency | Input | `maskIn` | Filter | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Filter Spectrum | Frequency | Output | `imageOut` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Add Spectra | Frequency | Input | `imageA` | Spectrum A | `spectrum-a` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Add Spectra | Frequency | Input | `imageB` | Spectrum B | `spectrum-b` | Complex spectrum | 2 components | Not applicable | Optional | Optional |
| Add Spectra | Frequency | Input | `maskIn` | Filter | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Add Spectra | Frequency | Output | `imageOut` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Subtract Spectra | Frequency | Input | `imageA` | Spectrum A | `spectrum-a` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Subtract Spectra | Frequency | Input | `imageB` | Spectrum B | `spectrum-b` | Complex spectrum | 2 components | Not applicable | Optional | Optional |
| Subtract Spectra | Frequency | Input | `maskIn` | Filter | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Subtract Spectra | Frequency | Output | `imageOut` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Spectrum Difference | Frequency | Input | `imageA` | Spectrum A | `spectrum-a` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Spectrum Difference | Frequency | Input | `imageB` | Spectrum B | `spectrum-b` | Complex spectrum | 2 components | Not applicable | Optional | Optional |
| Spectrum Difference | Frequency | Input | `maskIn` | Filter | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Spectrum Difference | Frequency | Output | `imageOut` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Magnitude | Frequency | Input | `imageIn` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Optional | Optional |
| Magnitude | Frequency | Input | `magnitude` | Magnitude | `magnitude` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Magnitude | Frequency | Input | `phase` | Phase | `phase` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Magnitude | Frequency | Output | `maskOut` | Component | `mask` | Scalar field | 1 channel | Unknown units | Required | Required |
| Magnitude | Frequency | Output | `imageOut` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Phase | Frequency | Input | `imageIn` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Optional | Optional |
| Phase | Frequency | Input | `magnitude` | Magnitude | `magnitude` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Phase | Frequency | Input | `phase` | Phase | `phase` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Phase | Frequency | Output | `maskOut` | Component | `mask` | Scalar field | 1 channel | Unknown units | Required | Required |
| Phase | Frequency | Output | `imageOut` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Recombine Magnitude/Phase | Frequency | Input | `imageIn` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Optional | Optional |
| Recombine Magnitude/Phase | Frequency | Input | `magnitude` | Magnitude | `magnitude` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Recombine Magnitude/Phase | Frequency | Input | `phase` | Phase | `phase` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| Recombine Magnitude/Phase | Frequency | Output | `maskOut` | Component | `mask` | Scalar field | 1 channel | Unknown units | Required | Required |
| Recombine Magnitude/Phase | Frequency | Output | `imageOut` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Spectrum Analyzer | Frequency | Input | `imageIn` | Spectrum | `image` | Complex spectrum | 2 components | Not applicable | Required | Required |
| Spectrum Analyzer | Frequency | Output | `scopeIn` | Analysis | `analysis` | Analysis data | Not applicable | Not applicable | Required | Required |
| Solid Color Image | Texture / Generate | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Color Gradient Image | Texture / Generate | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Square | Texture / Generate | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Circle | Texture / Generate | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Text | Texture / Generate | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Blend Images | Image Operations | Input | `imageA` | A | `a` | Color image | Unknown channels | Not applicable | Required | Required |
| Blend Images | Image Operations | Input | `imageB` | B | `b` | Color image | Unknown channels | Not applicable | Required | Required |
| Blend Images | Image Operations | Input | `factor` | Factor | `factor` | Mask | 1 channel | Unitless | Optional | Optional |
| Blend Images | Image Operations | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| LUT | Image Operations | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| LUT | Image Operations | Input | `r` | R | `red-channel` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| LUT | Image Operations | Input | `g` | G | `green-channel` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| LUT | Image Operations | Input | `b` | B | `blue-channel` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| LUT | Image Operations | Input | `a` | A | `alpha-channel` | Scalar field | 1 channel | Unknown units | Optional | Advanced |
| LUT | Image Operations | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| LUT | Image Operations | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Channel Split | Channels | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Channel Split | Channels | Output | `r` | R | `red-channel` | Scalar field | 1 channel | Unknown units | Required | Required |
| Channel Split | Channels | Output | `g` | G | `green-channel` | Scalar field | 1 channel | Unknown units | Required | Required |
| Channel Split | Channels | Output | `b` | B | `blue-channel` | Scalar field | 1 channel | Unknown units | Required | Required |
| Channel Split | Channels | Output | `a` | A | `alpha-channel` | Scalar field | 1 channel | Unknown units | Required | Required |
| Channel Combine | Channels | Input | `r` | R | `red-channel` | Scalar field | 1 channel | Unknown units | Optional | Optional |
| Channel Combine | Channels | Input | `g` | G | `green-channel` | Scalar field | 1 channel | Unknown units | Optional | Optional |
| Channel Combine | Channels | Input | `b` | B | `blue-channel` | Scalar field | 1 channel | Unknown units | Optional | Optional |
| Channel Combine | Channels | Input | `a` | A | `alpha-channel` | Scalar field | 1 channel | Unknown units | Optional | Optional |
| Channel Combine | Channels | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Alpha Protect (not shown in browser) | Composite | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Alpha Protect (not shown in browser) | Composite | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Alpha Protect (not shown in browser) | Composite | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Tone Equalizer (not shown in browser) | Color / Tone | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Tone Equalizer (not shown in browser) | Color / Tone | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Tone Equalizer (not shown in browser) | Color / Tone | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Text Overlay (not shown in browser) | Texture / Generate | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Text Overlay (not shown in browser) | Texture / Generate | Input | `maskIn` | Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| Text Overlay (not shown in browser) | Texture / Generate | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Image source | Non-browser / generated | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| RAW source | Non-browser / generated | Output | `rawOut` | RAW | `raw-image-data` | RAW image data | Not applicable | Not applicable | Required | Required |
| RAW detail auto mask | Non-browser / generated | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| RAW detail auto mask | Non-browser / generated | Output | `maskOut` | EV Map | `mask` | Mask | 1 channel | Unitless | Required | Required |
| RAW detail fusion | Non-browser / generated | Input | `imageIn` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| RAW detail fusion | Non-browser / generated | Input | `maskIn` | Hybrid Mask | `mask` | Mask | 1 channel | Unitless | Optional | Optional |
| RAW detail fusion | Non-browser / generated | Output | `imageOut` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| RAW detail fusion | Non-browser / generated | Output | `maskOut` | Gain Mask | `mask` | Mask | 1 channel | Unitless | Required | Required |
| Composite scene | Non-browser / generated | — | — | — | — | — | — | — | — | — |
| Add, Then Multiply | Compound (dynamic public interface) | Input | `image-in` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Add, Then Multiply | Compound (dynamic public interface) | Output | `image-out` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Exposure, Then Premultiply | Compound (dynamic public interface) | Input | `image-in` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
| Exposure, Then Premultiply | Compound (dynamic public interface) | Output | `image-out` | Image | `image` | Color image | Unknown channels | Not applicable | Required | Required |
