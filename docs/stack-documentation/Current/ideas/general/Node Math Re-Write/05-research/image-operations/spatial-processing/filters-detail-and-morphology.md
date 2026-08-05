# 06 — Neighborhood Filtering, Detail, and Morphology

> **Reference status:** Formulas and sources are technical reference. Any Stack
> status in this file is a 2026-07-12 snapshot, not current state or a release
> backlog.

## Scope

These operations cannot compute an output pixel from only the input pixel at the same coordinate. They sample a neighborhood, propagate state, build a pyramid, or traverse connected pixels.

That distinction matters in Stack. A pointwise formula can eventually be fused with adjacent pointwise formulas. A neighborhood operator needs a sampling footprint, border policy, resolution, halo/region-of-interest propagation, and often one or more materialized intermediate images.

“Convolution” is also frequently used loosely. Mathematical convolution flips the kernel. Many graphics and vision APIs perform cross-correlation without flipping it. A Stack node must state which convention it uses.

## Required contract for every neighborhood node

Every operation in this file needs these declared properties:

- Sample domain: RGB, luma/lightness, scalar mask, depth, or another vector space.
- Radius/support and its unit: pixels, normalized extent, percentage, or world units.
- Border mode: clamp, mirror, wrap, constant, transparent, crop, or valid-only.
- Filter/reconstruction precision and intermediate range.
- Per-channel versus vector behavior.
- Straight/premultiplied alpha behavior.
- Whether the kernel is normalized.
- Whether the node is separable, multi-pass, iterative, or pyramid-based.
- ROI/halo rule needed to evaluate a tile correctly.

## Canonical operation table

| ID | Operation | Canonical formula or staged algorithm | Scope | Domain/state | What it does and common uses | Composition cautions | Stack status, 2026-07-12 |
|---|---|---|---|---|---|---|---|
| `FILTER.BOX` | Box blur / mean filter | $O(p)=\frac1{\lvert K\rvert}\sum_{q\in K(p)}I(q)$ | `N` | Usually linear scalar/RGB | Uniform smoothing, quick low-pass, mask feathering | Radius and border mode are part of the operation; alpha should normally be premultiplied before filtering | **Existing:** Box Blur and Utility Mean; node-specific loops |
| `FILTER.GAUSSIAN` | Gaussian blur | $O(p)=\sum_q G_\sigma(p-q)I(q)/\sum_qG_\sigma$ | `N` | Usually linear scalar/RGB | Natural low-pass, denoise, bloom bases, mask feathering | Specify $\sigma$, finite support, normalization, border; separable implementation should be equivalent within tolerance | **Existing:** one nonseparable square pass; Tilt-Shift has private separable passes |
| `FILTER.MEDIAN` | Median filter | $O(p)=\operatorname{median}\{I(q):q\in K(p)\}$ | `N` | Scalar/per-channel or vector median | Removes impulse noise while preserving edges | Per-channel median can create colors absent from the input; vector median is different | **Needs fix:** visible radius is ignored by fixed 3×3 shader |
| `FILTER.PERCENTILE` | Rank/percentile filter | $O(p)=Q_r(K(p))$ | `N` | Scalar or declared channel strategy | Generalizes min, median, and max; robust local statistics | Define rank interpolation, ties, vector handling | **Missing** generic primitive |
| `FILTER.MIN` | Minimum filter | $O(p)=\min_{q\in K(p)}I(q)$ | `N` | Scalar/mask, sometimes per channel | Dark-region expansion; grayscale erosion | Equivalent to erosion only under the chosen foreground convention | **Partial:** mask/math minimum exists pointwise, not neighborhood-generic |
| `FILTER.MAX` | Maximum filter | $O(p)=\max_{q\in K(p)}I(q)$ | `N` | Scalar/mask, sometimes per channel | Bright-region expansion; grayscale dilation | Border constant changes geometry | **Partial:** no generic neighborhood maximum |
| `FILTER.BILATERAL` | Bilateral filter | $O(p)=\frac{\sum_qG_{\sigma_s}(p-q)G_{\sigma_r}(I(p)-I(q))I(q)}{\sum_qG_{\sigma_s}G_{\sigma_r}}$ | `N+V` | Named color-distance space | Edge-aware smoothing, denoise, tone/detail separation | Range sigma depends on numeric encoding and color metric; nonlinear and normally nonseparable | **Existing:** dedicated Bilateral Filter; not reusable infrastructure |
| `FILTER.GUIDED` | Guided filter | Fit $O_i=a_kG_i+b_k$ in each window by regularized least squares, then average overlapping coefficients | `N+M` | Scalar or color guide | Edge-aware smoothing, mask refinement, detail manipulation | Radius, regularization $\epsilon$, guide channels, and boundary averaging matter | **Missing** |
| `FILTER.SURFACE` | Surface blur | Weighted local average rejecting samples whose range distance exceeds a tolerance | `N+V` | Named range metric | Smooths similar regions while retaining strong edges | Not one standard formula; should be a convenience node built on a declared edge-aware kernel | **Partial:** bilateral/NLM cover related behavior |
| `FILTER.NLM` | Non-local means | $O(p)=\sum_qw(p,q)I(q)/\sum_qw$, $w=\exp(-\lVert P_p-P_q\rVert^2/h^2)$ | `N` | Patch distance in declared space | Repeated-texture-aware denoising | Search radius, patch radius, noise scale, normalization, and complexity must be explicit | **Existing:** bounded shader implementation |
| `FILTER.WIENER` | Wiener filter | Frequency form $\hat X=\frac{H^*}{\lvert H\rvert^2+S_n/S_x}Y$ or local-statistics approximation | `N/G` | Noise/blur estimates | Restoration and denoise | Requires noise and blur assumptions; not a generic sharpen | **Missing** |
| `FILTER.BM3D` | BM3D | Match similar patches → 3D transform shrinkage → aggregate; repeat with Wiener stage | `N+G+I` | Noise model and scene encoding | High-quality classical denoise | Complex global search and repeated passes; not a simple node chain of point operations | **Missing**; Scene/Classical denoise are different specialized systems |
| `FILTER.ANISODIFF` | Anisotropic diffusion | $\partial_t I=\nabla\cdot(c(\lvert\nabla I\rvert)\nabla I)$ iterated | `N+I` | Usually scalar or vector PDE | Edge-preserving smoothing | Time step and conductance must satisfy stability rules; iteration is semantic | **Missing** generic iteration facility |
| `FILTER.MOTION_BLUR` | Linear motion blur | Integrate samples along a vector: $O(p)=\int w(t)I(p-tv)dt$ | `N+C` | Linear color, declared alpha | Directional blur and camera-motion effect | Sample spacing, shutter profile, border, and premultiplied alpha matter | **Partial:** Tilt-Shift private motion mode; no general primitive |
| `FILTER.RADIAL_BLUR` | Radial/spin blur | Average samples along angular paths about center | `N+C` | Coordinates + image | Rotational motion effect | Center, angular extent, sampling density, border | **Partial:** specialized effects only |
| `FILTER.ZOOM_BLUR` | Zoom blur | Average $I(c+(p-c)s_t)$ over scale samples | `N+C` | Coordinates + image | Radial streaking toward/from a center | Must use inverse sampling and define scale/shutter distribution | **Partial:** specialized glare/distortion code |
| `FILTER.LENS_BLUR` | Aperture/lens blur | Convolve with an aperture PSF, possibly spatially varying | `N`, sometimes `M` | Linear premultiplied color; optional depth | Defocus simulation | A fixed convolution is not depth-of-field; highlights, occlusion, and depth require more structure | **Partial:** Optical/Airy/Tilt-Shift are special approximations |
| `FILTER.BOKEH_DEPTH` | Depth-aware bokeh | Scatter/gather using circle of confusion from depth and aperture | `N+M+I` | Image + depth + camera parameters | Synthetic depth of field | Occlusion ordering and foreground leakage make a naive blur incorrect | **Missing** generic depth-aware path |
| `FILTER.HIGHPASS` | High-pass | $H=I-L(I)$ for chosen low-pass $L$ | `N` | Usually linear or log-like luminance/RGB | Detail extraction, sharpening, frequency separation | “High-pass” is defined by the chosen low-pass; may be signed and exceed 0–1 | **Partial:** sharpen and frequency tools; no generic reusable image subtraction graph type yet |
| `FILTER.LOWPASS` | Low-pass | $L=K*I$ | `N` | Declared color/data domain | Removes high frequencies | Kernel and cutoff define the operation | **Existing through individual blurs**, not a single abstract node |
| `FILTER.UNSHARP` | Unsharp mask | $O=I+a(I-L(I))$ | `N` | Usually linear luminance or RGB | Edge/detail sharpening | Amount, radius, threshold, halos, alpha, and clipping policy are required | **Existing:** Sharpen uses four-neighbor average and threshold; fixed formula |
| `FILTER.LAPLACIAN_SHARP` | Laplacian sharpening | $O=I-a\nabla^2I$ using a declared discrete kernel | `N` | Scalar/per-channel | Fine edge sharpening | Kernel sign convention differs; highly noise-sensitive | **Missing** generic derivative/convolution node |
| `FILTER.CONV` | Convolution | $O(p)=\sum_qK(q)I(p-q)$ | `N` | Scalar/vector | General linear filtering | Kernel flip, normalization, anchor, border, separability, precision must be explicit | **Missing** generic convolution; many nodes embed kernels |
| `FILTER.CORR` | Cross-correlation | $O(p)=\sum_qK(q)I(p+q)$ | `N` | Scalar/vector | Template/feature filtering; many APIs call this convolution | Do not silently treat a correlation kernel as convolution | **Missing** generic primitive |
| `FILTER.DECONV` | Deconvolution | Solve $\arg\min_X\lVert K*X-I\rVert^2+\lambda R(X)$ or use a named inverse algorithm | `N+I/G` | Blur PSF + noise/prior | Deblur and restoration | Ill-posed; algorithm and regularizer are essential, not optional detail | **Missing** |
| `FILTER.SOBEL` | Sobel gradient | $G_x=K_x*I$, $G_y=K_y*I$, magnitude/direction from $(G_x,G_y)$ | `N` | Named scalar/luma | Edge maps, masks, focus metrics | Luma conversion and magnitude norm matter | **Existing privately:** Edge Overlay uses Rec.709 Sobel |
| `FILTER.SCHARR` | Scharr gradient | Optimized 3×3 derivative kernels for better rotational symmetry | `N` | Scalar/luma | Edge/gradient estimation | Still requires scale, border, and magnitude convention | **Missing** generic primitive |
| `FILTER.PREWITT` | Prewitt gradient | Convolve with simple 3×3 derivative/smoothing kernels | `N` | Scalar/luma | Basic edges | Lower isotropy than Scharr; output is signed | **Missing** |
| `FILTER.ROBERTS` | Roberts cross | Two 2×2 diagonal derivative kernels | `N` | Scalar/luma | Very small-footprint edge estimate | Sensitive to noise and pixel-center convention | **Missing** |
| `FILTER.CANNY` | Canny edges | Gaussian smooth → gradient → nonmaximum suppression → double threshold → hysteresis | `N+I/G` | Scalar/luma | Thin connected edge detection | Not one convolution; thresholds and hysteresis connectivity are part of the definition | **Missing** |
| `FILTER.EMBOSS` | Emboss | Directional derivative/correlation plus bias | `N` | Encoded or display RGB often intentional | Stylized relief | UI effect, not a neutral scientific primitive; bias/range mapping required | **Missing** as generic effect |
| `FILTER.GABOR` | Gabor filter | Gaussian envelope times sinusoid at frequency/orientation | `N` | Scalar/luma | Texture, orientation, frequency analysis | Kernel phase, wavelength, aspect, orientation, normalization | **Missing** |
| `FILTER.LOCAL_CONTRAST` | Local contrast | $O=I+a(I-L(I))$ in a selected scale/domain, often on luminance | `N` | Declared luminance/log domain | Makes local differences more visible | Overlaps unsharp mask; domain and halo suppression define the appearance | **Partial:** several effects; no truthful generic node |
| `FILTER.CLARITY` | Clarity | Mid-frequency local-contrast enhancement, commonly edge-aware and luma-weighted | `N+I` | Luma/lightness with tonal gating | Adds midtone structure | No universal formula; needs filter scale, tonal mask, halo control | **Missing as named node; decomposable only with added filter infrastructure** |
| `FILTER.TEXTURE` | Texture adjustment | Enhance/suppress a finer band than clarity, often $I+a(B_{small}-B_{large})$ | `N` | Luma/detail bands | Fine-detail control | Product name, not a standard algorithm | **Partial** via sharpen/compression/frequency tools |
| `FILTER.FREQSEP` | Frequency separation | $L=L(I)$, $H=I-L$ or divisive/log variant; reconstruct from $L,H$ | `N+M` | Signed/high-range intermediates | Retouching, detail/base separation | Additive and divisive variants differ; encoding matters | **Partial:** FFT and blurs exist, but no clean multi-output spatial primitive |
| `FILTER.PYRAMID_GAUSS` | Gaussian pyramid | Repeated low-pass + downsample | `N+C+I` | Image/mask | Multiscale analysis, blending | Prefilter and alignment between levels required | **Missing** generic pyramid construct |
| `FILTER.PYRAMID_LAPLACE` | Laplacian pyramid | $L_k=G_k-\operatorname{expand}(G_{k+1})$ | `N+C+I` | Signed detail levels | Multiband blending, tone/detail work | Reconstruction filter and boundary handling must match decomposition | **Missing** generic facility |
| `FILTER.WAVELET` | Wavelet decomposition | Apply named analysis filters and downsampling across scales | `N+C+I` | Signed coefficient bands | Compression, denoise, multiscale editing | Wavelet family, boundary extension, levels, threshold policy | **Partial:** “Wavelet Compression” is only an approximation |
| `MORPH.DILATE` | Dilation | $(I\oplus B)(p)=\max_{b\in B}I(p-b)$ | `N` | Mask/scalar | Expands bright/foreground regions | Structuring element, anchor, foreground convention, border | **Partial:** Custom Mask has private expand; no generic node |
| `MORPH.ERODE` | Erosion | $(I\ominus B)(p)=\min_{b\in B}I(p+b)$ | `N` | Mask/scalar | Shrinks bright/foreground regions | Same contract requirements as dilation | **Missing** generic node |
| `MORPH.OPEN` | Opening | $(I\ominus B)\oplus B$ | `N+I` | Mask/scalar | Removes small bright details, smooths contours | Requires two ordered passes | **Missing** |
| `MORPH.CLOSE` | Closing | $(I\oplus B)\ominus B$ | `N+I` | Mask/scalar | Fills small holes, joins gaps | Requires two ordered passes | **Missing** |
| `MORPH.GRADIENT` | Morphological gradient | $\operatorname{dilate}(I)-\operatorname{erode}(I)$ | `N+I` | Mask/scalar | Outline/edge thickness | Signed/range policy | **Missing** |
| `MORPH.TOPHAT` | White top-hat | $I-\operatorname{open}(I)$ | `N+I` | Scalar/mask | Extracts small bright features | Structuring element sets scale | **Missing** |
| `MORPH.BLACKHAT` | Black-hat | $\operatorname{close}(I)-I$ | `N+I` | Scalar/mask | Extracts small dark features | Structuring element sets scale | **Missing** |
| `MORPH.HITMISS` | Hit-or-miss | Match foreground and background structuring elements | `N` | Binary mask | Pattern detection/thinning components | Binary convention and exact connectivity required | **Missing** |
| `FILTER.DISTANCE` | Distance transform | $D(p)=\min_{q:I(q)=0}d(p,q)$ | `N+G/I` | Binary mask + metric | Feathering, signed distance fields, geometry | Exact Euclidean, chamfer, Manhattan, and signed variants differ | **Missing** |
| `FILTER.FLOOD` | Flood fill | Traverse connected neighbors satisfying a criterion | `N+I` | Image/mask + seed | Region selection, mask fill | Connectivity, tolerance, color metric, visit order, limits | **Partial:** Background Remover flood path is forced off/incomplete |
| `FILTER.REGION_GROW` | Region growing | Repeatedly add neighbors meeting a region statistic/threshold | `N+G+I` | Image + seeds | Segmentation and selection | Result depends on criterion and update policy | **Missing** |
| `FILTER.CONNECTED` | Connected components | Label connected foreground regions | `N+G+I` | Binary mask | Object counting, cleanup, size filters | 4/8 connectivity, label type, tiling merge | **Missing** |
| `FILTER.WATERSHED` | Watershed | Flood a gradient/topographic surface from markers | `N+G+I` | Scalar gradient + markers | Segmentation | Marker and plateau handling are essential | **Missing** |
| `FILTER.INPAINT` | Classical inpainting | Propagate/interpolate boundary structure or solve a PDE/patch search over holes | `N+I/G` | Image + hole mask | Repair small damage/object removal | Not one formula; distinguish PDE, patch, and learned methods | **Missing** generic conventional version |
| `FILTER.DEMOSAIC` | Demosaicing | Reconstruct missing channels from CFA neighborhood using a named algorithm | `N+X`, sometimes `I` | RAW mosaic + CFA/levels/calibration | RAW-to-RGB reconstruction | Cannot be derived from ordinary RGB alone; algorithm and CFA metadata required | **Specialized:** RAW GPU path; stored method currently forced to Bilinear |
| `FILTER.CHROMA_RECON` | Chroma reconstruction | Interpolate subsampled chroma at declared siting/filter | `N+X` | YCbCr/chroma metadata | Decode 4:2:x imagery, compression effects | Chroma siting, range, matrix, filter, and transfer are required | **Partial:** Chroma compression effect, not a general decoder contract |
| `DITHER.ORDERED` | Ordered dither | Quantize $I+D(x,y)$ using a threshold matrix | `P+C` | Encoded/output domain usually | Reduces visible banding with a deterministic pattern | Matrix scale, bit depth, channel correlation, output transfer | **Existing:** Bayer 2×2/4×4/8×8 |
| `DITHER.NOISE` | Random/blue-noise dither | Quantize $I+n(x,y)$ where noise amplitude matches one code step | `P+C` | Final output encoding | Hides quantization bands | Noise distribution/spectrum and seeding matter; apply near final quantization | **Existing:** white/interleaved variants; no declared output contract |
| `DITHER.ERROR` | Error diffusion | Quantize sequentially and distribute residual to future pixels; Floyd–Steinberg uses $7/16,3/16,5/16,1/16$ | `N+I` | Output code space | High-quality low-bit-depth conversion | Scan order and recurrence are essential; difficult to tile/fuse naively | **Needs fix:** current “Error Diffusion” is not recursive diffusion |

## Recommended primitive/convenience split

Expose a small technical core:

- Sample Neighborhood
- Convolution/Correlation
- Gaussian/Box separable filter
- Rank/Min/Max filter
- Gradient/Derivative
- Morphology with explicit structuring element
- Pyramid Decompose/Reconstruct
- Distance Transform

Keep Bilateral, Guided, NLM, Canny, Clarity, Lens Blur, Demosaic, BM3D, and inpainting as convenience or specialized nodes. Their behavior is still inspectable, but pretending each is one small formula would be misleading.

## Stack-specific design consequence

Stack presently embeds sampling loops inside individual shaders and gives most nodes one full RGBA16F target. A generic filter system should not begin as fifty copied shaders. It needs:

1. A kernel/structuring-element value type.
2. A declared border/sampling policy.
3. ROI/halo propagation for tiles.
4. Separable and multi-pass lowering.
5. Temporary-target planning and reuse.
6. A signed/high-range intermediate contract.
7. Reference tests for kernels, borders, alpha, and CPU/GPU parity.

## Primary sources

- [OpenCV filtering and morphology reference](https://docs.opencv.org/4.x/d4/d86/group__imgproc__filter.html)
- [Tomasi and Manduchi, Bilateral Filtering for Gray and Color Images](https://doi.org/10.1109/ICCV.1998.710815)
- [He, Sun, and Tang, Guided Image Filtering](https://doi.org/10.1109/TPAMI.2012.213)
- [Canny, A Computational Approach to Edge Detection](https://doi.org/10.1109/TPAMI.1986.4767851)
- [OpenImageIO ImageBufAlgo](https://openimageio.readthedocs.io/en/latest/imagebufalgo.html)
- [Floyd and Steinberg, An Adaptive Algorithm for Spatial Grey Scale](https://www.visgraf.impa.br/Courses/ip00/proj/Dithering1/floyd-steinberg.pdf)
- [Burt and Adelson, The Laplacian Pyramid as a Compact Image Code](https://doi.org/10.1109/TCOM.1983.1095851)
