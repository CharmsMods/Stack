# 11 — Model- and Intent-Based Operations: Scope Boundary

## Why this document exists

The original operation catalog included learned and semantic tools because they are still mathematical operations. Their usual form is:

$$y=f_\theta(x,c)$$

where $\theta$ contains learned parameters and $c$ may contain prompts, masks, references, or other context. The arithmetic is real, but the behavior is not fully described by a small human-readable formula such as `RGB × 2`.

This package does **not** recommend turning Stack’s primitive node system into an AI-workflow system. These entries define the boundary. They also prevent a misleading claim that every familiar modern editing feature can be reconstructed from a handful of transparent scalar nodes.

When Stack already contains a model-backed node, it should be treated as an external/specialized operation with a strict input/output contract, model identity, and deterministic fallback policy—not as a mathematical primitive.

## Model contract

Any model-based node should record:

- Model family, exact model hash/version, license, and provenance.
- Input color space/transfer/range, alpha convention, channel order, and normalization.
- Required dimensions/multiples, tile size, overlap/halo, and stitching method.
- Runtime/provider and numerical precision.
- Determinism/seed behavior.
- Whether the model estimates existing information or invents plausible detail.
- Confidence/uncertainty if available.
- Failure and unavailable-model behavior.
- Output semantic descriptor and any forced clamp/quantization.

## Canonical boundary table

| ID | Operation | General mathematical form | Scope | External knowledge | What it does/common uses | Transparent/classical relatives | Stack status, 2026-07-12 |
|---|---|---|---|---|---|---|---|
| `MODEL.DENOISE` | Neural denoising | $\hat x=f_\theta(y,\sigma,\ldots)$ | `L+N/I` | Trained clean/noisy-image prior | Removes noise using learned image statistics | Bilateral, NLM, wavelet shrinkage, BM3D, sensor noise models | **Existing/conditional:** Linear RGB neural workflow needs model pack; RAW/CFA neural path effectively bypasses without external workflow |
| `MODEL.SUPERRES` | Neural super-resolution | $\hat x_{hi}=f_\theta(x_{lo},s)$ | `L+N/I` | Learned high-resolution prior | Upscales and may synthesize detail | Lanczos/bicubic, multi-frame SR, deconvolution | **Missing** single-image neural node; MFSR shell is pass-through |
| `MODEL.FACE_RESTORE` | Face restoration | Detect/align face then apply learned restoration/generation | `L+G/N/I` | Face distribution/identity prior | Repairs small/compressed faces | Deblur, denoise, local contrast cannot reconstruct lost identity detail | **Missing/out of current focus** |
| `MODEL.OBJECT_REMOVE` | Semantic/generative object removal | Segment target, then conditionally synthesize hole content | `L+G+N/I` | Semantic and image-generation prior | Removes objects | Classical PDE/patch inpainting and clone/heal remain separate conventional tools | **Missing/out of scope** |
| `MODEL.GEN_FILL` | Generative fill/outpainting | Sample $y\sim p_\theta(y\mid x,m,text)$ | `L+G+I` | Large generative prior | Fills/extends regions from prompt/context | Classical inpainting can fill texture but not infer arbitrary semantic content | **Missing/out of scope** |
| `MODEL.SKY` | Sky replacement | Segment sky, estimate boundary/lighting, composite replacement | `L+G+M` | Sky segmentation/scene prior | Automated sky swaps | Color key, range mask, manual mask, compositing are transparent components | **Missing/out of scope** |
| `MODEL.SUBJECT` | Subject selection | $m=f_\theta(x)$ semantic segmentation | `L+G/N` | Object/subject labels | Automatic primary-subject mask | Color/luma/range/edge/region masks | **Missing/out of scope** |
| `MODEL.SEM_MASK` | Semantic masking | Produce class/instance masks from learned recognition | `L+G/N` | Semantic class model | Select hair, skin, sky, clothing, etc. | Handcrafted keys and region growing do not encode object meaning | **Missing/out of scope** |
| `MODEL.SKIN` | Skin detection | Heuristic classifier or learned probability $m=f(x)$ | `V/G`, optionally `L` | Chosen skin-color/semantic model | Skin protection/selection | Hue/chroma key with broad range; manual sample masks | **Missing**; heuristic variant could be conventional but culturally/illumination sensitive |
| `MODEL.DEPTH` | Monocular depth estimation | $d=f_\theta(x)$ up to model-specific scale/shift | `L+G/N` | Learned scene geometry | Depth blur, compositing, relighting | Stereo, structured light, known geometry | **Missing/out of scope** |
| `MODEL.RELIGHT` | Learned relighting | $y=f_\theta(x,l,target)$ | `L+G/N` | Learned shape/material/lighting prior | Changes illumination from one image | Normal/depth-based physical shading if geometry is available | **Missing/out of scope** |
| `MODEL.AUTO_GRADE` | Automatic grading | Predict parameters/transform from image and learned aesthetic target | `L+G` | Training preferences and style distribution | One-click look/correction | Explicit auto exposure/WB/contrast solvers with declared targets | **Stack has Advanced Auto RAW heuristics**, not necessarily learned; no general learned grade node |
| `MODEL.STYLE` | Neural style transfer | Optimize/transform content features toward style features | `L+M+I` | Feature network/style representation | Artistic style application | Palette/texture/statistical reference transfer are classical alternatives | **Missing/out of scope** |
| `MODEL.CAPTION_EDIT` | Language-guided image editing | $y\sim p_\theta(y\mid x,text,m)$ | `L+G/I` | Vision-language and generative model | Natural-language edits | Explicit graph operations remain the preferred Stack approach | **Missing/out of scope** |
| `MODEL.DEBLUR` | Learned deblurring | $\hat x=f_\theta(y)$ | `L+N/I` | Learned blur/image prior | Repairs motion/defocus blur | Wiener, Richardson–Lucy, blind/classical deconvolution | **Missing**; “deblurring” itself is not necessarily learned |
| `MODEL.COLORIZE` | Automatic colorization | Predict chroma distribution conditioned on grayscale/content | `L+G/N` | Object/color statistics | Adds plausible color to monochrome images | Manual color layers/masks; reference-based transfer | **Missing/out of scope**; colors are inferred, not recovered |
| `MODEL.NEURAL_DEMOSAIC` | Neural demosaicing | $RGB=f_\theta(CFA,metadata)$ | `L+N+X` | Learned sensor/image prior | RAW reconstruction | Bilinear, AHD, LMMSE and other classical demosaicers | **Stack uses classical/specialized RAW GPU path; neural RAW path not active without external workflow** |
| `MODEL.NEURAL_MATTE` | Learned matting | Estimate alpha/foreground/background from image and optional trimap | `L+G/N` | Learned boundary/transparency prior | Hair/fur/translucency extraction | Chroma key, difference matte, closed-form/optimization matting | **Missing/out of scope** |
| `MODEL.INVENT_DETAIL` | Generative/invented-detail upscale | Sample high-frequency details consistent with low-res input | `L+N/I` | Generative texture prior | Visually sharp enlargement | Must be labeled differently from measured multi-frame reconstruction | **Missing/out of scope** |

## Important corrections to the original category

- **Deblurring is not inherently learned.** Classical deconvolution belongs in filtering/restoration.
- **Object removal is not inherently generative.** Patch/PDE inpainting and clone/heal are conventional alternatives.
- **Relighting is not inherently learned.** With depth, normals, material, and lights it can be physically defined.
- **Skin detection may be heuristic or learned.** Either way, it embeds a classification assumption and should not be presented as pure universal color math.
- **Style transfer may be reference-statistical, patch-based, or learned.** The algorithm must be named.
- **Neural super-resolution and invented-detail upscaling overlap**, but an application should distinguish conservative reconstruction from generative hallucination.

## Recommended Stack policy

1. Keep model nodes in a separate **External/Model** family, not mixed with math primitives.
2. Require a visible badge that a result uses learned or generative inference.
3. Preserve the exact model/version/runtime in the project file.
4. Do not silently fall back to pass-through and call the operation complete; show unavailable/failed state.
5. Do not allow model I/O normalization to be hidden. Convert explicitly or display it in the node’s technical details.
6. Label invented content separately from reconstructed/measured content.
7. Keep conventional transparent alternatives available where practical.
8. Exclude these operations from the first primitive/compound architecture milestone.

## Stack-specific note

The audit found that Linear RGB Neural Denoise clamps model input to `[0,1]` and depends on an external pack, while RAW/CFA Neural Denoise is effectively a bypass without its external workflow. Those facts make model identity and failure state part of the node’s semantics. A pass-through result must not be visually indistinguishable from a successfully processed result.

## Sources

This file is a scope and architecture boundary rather than a proposal to implement models. For a general model interchange/runtime contract, see [ONNX](https://onnx.ai/onnx/intro/) and [ONNX Runtime execution providers](https://onnxruntime.ai/docs/execution-providers/). Stack-specific model behavior comes from the returned Codex audit.
