# 09 — Multi-Image, Reference, Stack, and Temporal Operations

> **Reference status:** Formulas and sources are technical reference. Any Stack
> status in this file is a 2026-07-12 snapshot, not current state or a release
> backlog.

## Scope

These operations need at least one additional image, frame, mask, depth field, flow field, reference statistic, or stack. Some are simple same-coordinate math. Others estimate correspondence, exposure, camera motion, depth, or a global transform before they can combine pixels.

The architectural rule is simple: **estimation and application are different operations**. Registration estimates a transform; a geometry node applies it. Motion estimation produces flow; a warp node consumes it. Histogram analysis produces a mapping; a pointwise curve applies it.

Stack currently supports several fixed multi-input nodes, but it has no general list/collection type, frame dependency contract, or reusable transform/flow/histogram value.

## Canonical operation table

| ID | Operation | Canonical formula or staged algorithm | Scope | Inputs/state | What it does/common uses | Composition cautions | Stack status, 2026-07-12 |
|---|---|---|---|---|---|---|---|
| `MULTI.BLEND` | Two-image blend | $O=(1-t)A+tB$ or a named blend/composite | `M+P` | Two compatible images + factor/mask | Crossfade, local adjustment, layer mix | Color state, extent, sampling, and alpha must be compatible; blend and source-over are distinct | **Existing:** Mix and generic mask blend; AlphaOver defective |
| `MULTI.DIFFERENCE` | Difference comparison | Signed $A-B$, absolute $\lvert A-B\rvert$, squared error, or perceptual difference | `M+P/V` | Source/reference in same domain | QA, change detection, residuals | “Difference” must name metric and preserve signed/HDR data when needed | **Existing:** DataMath abs difference; frequency “difference” has semantic caveat |
| `MULTI.MEAN_STACK` | Mean image stack | $O=\frac1N\sum_iI_i$ | `M+G/P` | Aligned images | Noise reduction, long-exposure simulation | Ghosting from motion; linear-light averaging for radiometric meaning | **Existing:** Average Images up to fixed inputs, multipass |
| `MULTI.MEDIAN_STACK` | Median stack | $O(p)=\operatorname{median}_i I_i(p)$ | `M+G/P` | Aligned images | Remove transient objects/noise | Per-channel median can create false colors; memory and list handling | **Missing** |
| `MULTI.WEIGHT_STACK` | Weighted stack | $O=\sum_iw_iI_i/\sum_iw_i$ | `M+P/G` | Images + weights/masks | Burst merge, fusion, focus/exposure blend | Weight normalization and zero-sum policy | **Partial:** HDR and RAW fusion have private weighting |
| `MULTI.HIST_MATCH` | Histogram matching | Estimate $T=F_{ref}^{-1}\circ F_{src}$ then apply $T$ | `M+G+P` | Source/reference | Match tone distributions | Cross-reference `STAT.HIST_MATCH`; per-channel use can distort color | **Missing** |
| `MULTI.COLOR_MATCH` | Color matching | Estimate named statistical or color transform from source/reference | `M+G+V+P` | Source/reference and space | Shot matching and compositing | Method, color space, regularization, gamut mapping must be explicit | **Missing** |
| `MULTI.LUT_INFER` | LUT/transform inference | Optimize transform parameters/table to minimize $\sum_p d(T(A_p),B_p)$ | `M+G+I` | Corresponding input/output pairs + model constraints | Approximate an existing look | Underdetermined; alignment, domain, transform family, smoothness and color space required | **Missing**; proposed future research idea |
| `MULTI.REGISTER` | Image registration | Estimate transform $T$ minimizing feature/intensity mismatch | `M+G+I` | Moving/reference images | Align scans, bursts, stacks | Output should be a transform, not an already-resampled image only | **Missing** first-class transform result |
| `MULTI.ALIGN` | Image alignment | Convenience wrapper around registration plus warp | `M+G+C+N` | Moving/reference images | Prepares merge/fusion | “Alignment” is not a separate canonical algorithm from registration; name method/model | **Missing** |
| `MULTI.FEATURE_MATCH` | Feature correspondence | Detect/describe features → nearest-neighbor matching → reject outliers | `M+G` | Two images | Panorama/registration | Detector, descriptor, matching metric, ratio test | **Missing** |
| `MULTI.HDR_RADIANCE` | HDR radiance merge | Linearize/estimate response; combine exposure-normalized samples with reliability weights | `M+G+P` | Bracketed exposures, exposure times, alignment | Reconstruct scene radiance | Distinct from exposure fusion; needs response/exposure metadata and deghosting | **Existing/partial:** fixed three-input HDR Merge; exact radiometric contract limited |
| `MULTI.EXPOSURE_FUSION` | Exposure fusion | Compute contrast/saturation/well-exposedness weights; multiresolution blend images | `M+N+G+I` | Aligned brackets | Display-oriented high-dynamic-range-looking result | Does not produce calibrated scene radiance | **Partial:** RAW/HDR fusion concepts, no general named node |
| `MULTI.DEGHOST` | Deghosting | Detect moving/inconsistent regions and choose/model source contributions | `M+G+N/I` | Exposure/burst stack | Removes HDR/stack ghosts | Method-specific; can discard data or require motion estimation | **Missing** |
| `MULTI.BURST` | Burst merge | Register frames → reject motion/outliers → weighted average/super-resolution | `M+G+N+I` | Frame sequence + exposure/noise metadata | Mobile low-light and denoise | A compound family, not one primitive | **Missing; MFSR shell is pass-through** |
| `MULTI.FOCUS_STACK` | Focus stacking | Align → compute local focus measure → select/fuse regions, often multiband | `M+N+G+I` | Focus bracket | Extends depth of field | Selection seams and breathing/alignment require handling | **Missing** |
| `MULTI.PANORAMA` | Panorama stitching | Features → robust camera/model estimation → projection → exposure/seam optimization → multiband blend | `M+G+C+N+I` | Overlapping images + optional metadata | Wide/360 panoramas | Compound pipeline; projection and alignment are separable suboperations | **Missing** |
| `MULTI.MULTIBAND` | Multiband blend | Laplacian pyramids of images + Gaussian mask pyramid; blend per level and reconstruct | `M+N+I` | Aligned images + mask | Seamless panorama/composite blending | Pyramid filters/levels/borders must match | **Missing** generic pyramid support |
| `MULTI.POISSON` | Poisson/seamless clone | Solve $\nabla^2O=\nabla\cdot v$ inside mask with boundary constraints | `M+N+G+I` | Source, destination, mask | Seamless cloning/gradient-domain compositing | Iterative/global solve; can alter color/illumination | **Missing** |
| `MULTI.HEAL` | Clone/heal | Copy or synthesize source patch, optionally match low-frequency color/gradient | `M+N/I` | Image + source/destination coordinates/mask | Retouch blemishes | Clone and heal are distinct; source transform and boundary policy | **Missing** |
| `MULTI.TEMP_DENOISE` | Temporal denoising | Motion-compensate frames → estimate consistency → robust weighted combine | `M+N+G+I` | Adjacent frames + motion/noise model | Video/burst noise reduction | Ghosting, temporal flicker, exposure changes | **Missing** sequence/frame dependency model |
| `MULTI.MOTION_EST` | Motion/optical-flow estimation | Estimate vector field minimizing brightness/feature and smoothness terms | `M+N+G+I` | Two or more frames | Tracking, interpolation, stabilization | Produces flow; brightness constancy can fail; method may be learned | **Missing** |
| `MULTI.FRAME_INTERP` | Frame interpolation | Estimate bidirectional motion/occlusion → warp endpoints → blend/synthesize | `M+N+C+I`, sometimes `L` | Previous/next frames | Slow motion and frame-rate conversion | Not simply average; occlusion/disocclusion is central | **Missing/out of current still-image focus** |
| `MULTI.MOTION_COMP` | Motion compensation | Warp a frame using known motion/flow | `M+C+N` | Image + flow/transform | Temporal filters and stabilization | Application is geometry; flow convention/time direction required | **Missing** coordinate-field type |
| `MULTI.STABILIZE` | Video stabilization | Estimate camera motion → smooth trajectory → warp/crop frames | `M+G+C+I` | Frame sequence | Removes camera shake | Compound temporal pipeline and extent management | **Missing** |
| `MULTI.MFSR` | Multi-frame super-resolution | Register subpixel shifts → solve/reconstruct higher-resolution image | `M+N+G+C+I` | Burst frames + degradation/noise model | Adds measured detail from multiple frames | Distinguish from learned single-image invented detail | **Needs implementation:** current MFSR returns Reference unchanged |
| `MULTI.BG_SUBTRACT` | Background subtraction | Compare frame to reference/background model and update model | `M+P/G/I` | Frame + background/model | Foreground matte, surveillance | Static plate and adaptive temporal model differ | **Missing** |
| `MULTI.DIFF_MATTE` | Difference matte | $m=f(d(I,B))$ using color/difference metric and thresholds | `M+P/V` | Foreground plate + clean background | Extracts subject when a clean plate exists | Noise, shadows, camera movement, encoded-space distance | **Partial:** Background Remover color-distance path, not plate-based |
| `MULTI.CHROMA_REFINE` | Chroma-key refinement | Key-color distance → matte shaping → despill → edge/detail refinement | `P/V+N`, optional `M` | Image, key color, optional clean plate | Green/blue-screen extraction | Does not inherently require a second image; distance space and alpha are crucial | **Partial:** Background Remover incomplete |
| `MULTI.DEPTH_COMP` | Depth compositing | Compare depth, combine colors/alpha using visibility rule | `M+P/V` | Images + depth maps | 2.5D compositing | Depth units, invalid values, antialiasing, transparency ordering | **Missing** depth type |
| `MULTI.DEPTH_BLUR` | Depth-aware blur | Use depth to compute spatially varying PSF/CoC | `M+N+I` | Image + depth + camera params | Synthetic depth of field | Cross-reference lens/bokeh; occlusion matters | **Missing** |
| `MULTI.NORMAL_LIGHT` | Normal-map lighting | $L_d=k_d\max(0,n\cdot l)$ plus named lighting model | `M+P/V` | Normal field + lights/material | Relighting/texture work | Normals are data, not color; coordinate space must be declared | **Missing** vector/data semantics |
| `MULTI.PHOTOMETRIC` | Photometric stereo | Solve surface normals/albedo from images under known lights | `M+G/V` | Multiple lighting views + light directions | Surface reconstruction | Requires calibration and reflectance assumptions | **Missing** |
| `MULTI.RELIGHT_DEPTH` | Relighting from depth/normals | Estimate/apply shading from geometry and lights | `M+P/N`, sometimes `L` | Image + depth/normals + light | Change illumination | Physical and learned relighting are different families | **Missing** |
| `MULTI.STEREO` | Stereo disparity/depth | Match corresponding epipolar regions and regularize disparity | `M+N+G+I` | Calibrated stereo images | Depth estimation | Calibration, occlusion, baseline and disparity units | **Missing** |
| `MULTI.TEXTURE_TRANSFER` | Classical texture transfer | Optimize/synthesize patches matching source texture and target structure | `M+N+G+I` | Texture reference + target | Stylization and synthesis | Classical patch method differs from learned style transfer | **Missing** |
| `MULTI.STYLE_REF` | Style transfer | Transform content toward reference statistics/features | `M+G/I`, often `L` | Content + style reference | Stylization | Cross-reference model scope; not normally transparent low-level math | **Out of recommended primitive focus** |
| `MULTI.MORPH` | Image morphing | Establish correspondences → warp both images → cross-dissolve | `M+C+N+I` | Two images + landmarks/flow | Face/object transitions | Correspondence is the hard stage; extent/alpha must match | **Missing** |

## Recommended collection and temporal types

Stack should not add a different fixed eight-input node for every stack operation. Add typed structures such as:

- `ImageList<ImageDescriptor>`
- `FrameSequence` with time, duration, and source transform
- `Transform2D` / `Homography`
- `FlowField` with direction, units, reference frame, and confidence
- `DepthField` with units, near/far convention, and validity
- `ImageStatistics` / `Histogram`

Compound nodes can still show a variable number of visible inputs, but their execution and serialization should use an actual collection contract.

## Stack-specific consequence

Current recursive multi-input evaluation is a useful base, but MFSR, HDR Merge, Average Images, RAW fusion, and future temporal tools should not each invent private list, alignment, extent, and metadata rules. The new types must participate in caching, ROI/frame dependency queries, and compound interfaces.

## Primary sources

- [Debevec and Malik, Recovering High Dynamic Range Radiance Maps from Photographs](https://www.pauldebevec.com/Research/HDR/debevec-siggraph97.pdf)
- [Mertens, Kautz, and Van Reeth, Exposure Fusion](https://doi.org/10.1109/PG.2007.23)
- [Brown and Lowe, Automatic Panoramic Image Stitching using Invariant Features](https://doi.org/10.1007/s11263-006-0002-3)
- [Burt and Adelson, A Multiresolution Spline with Application to Image Mosaics](https://doi.org/10.1145/800059.801126)
- [Pérez, Gangnet, and Blake, Poisson Image Editing](https://doi.org/10.1145/882262.882269)
- [OpenFX frame and region dependency actions](https://openfx.readthedocs.io/en/main/Reference/ofxImageEffectActions.html)
