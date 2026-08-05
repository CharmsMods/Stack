# 02 — Channel and Vector Operations

> **Reference status:** Formulas and sources are technical reference. Any Stack
> status in this file is a 2026-07-12 snapshot, not current state or a release
> backlog.

## Purpose

An RGB(A) pixel is a small vector, not four unrelated sliders. Channel/vector operations extract, construct, rearrange, combine, or measure those components at one coordinate. They are still pointwise unless a later section explicitly says otherwise.

These operations are valuable as **primitives**, but their numbers have no colorimetric meaning without state. The vector `(0.5, 0.2, 0.1)` could be encoded sRGB, scene-linear RGB, camera-native samples, XYZ, OKLab, a normal vector, or arbitrary data. A 3×3 matrix can only be called a “color transform” when its source/destination primaries, white point, transfer state, and orientation are known.

Stack currently supports channel split/combine and several node-local RGB formulas, but generic image links do not record channel layout, color space, transfer, alpha convention, or numeric range. Split channels are represented as coarse `Mask` textures, and no first-class vector or matrix value can travel through the graph.

## Structural channel primitives

| Stable ID | Operation | Canonical formula / algorithm | Scope | Required domain / state | What it does / common uses | Composition cautions | Current Stack status based on audit |
|---|---|---|---|---|---|---|---|
| `CHAN.SPLIT` | Split / component extraction | `(R,G,B,A) → R`, `→G`, `→B`, `→A` | Pointwise structural fan-out | Known component layout and alpha meaning | Expose components for independent processing | Extraction does not convert luma, color space, or straight/premultiplied alpha | **Implemented** as Channel Split; outputs are Mask-typed scalar textures |
| `CHAN.MERGE` | Merge / vector construction | `(r,g,b,a) → vec4(r,g,b,a)` | Pointwise structural fan-in | Declared missing-channel defaults and compatible extents | Reassemble processed channels or arbitrary vector data | Default alpha should be explicit; merging unrelated spaces produces meaningless colors | **Implemented** as Channel Combine; missing-channel defaults are evaluator-defined |
| `CHAN.SWIZZLE` | Swizzle | `outᵢ = in[indexᵢ]`, e.g. `BGR`, `RRG`, `AAA` | Pointwise structural | Known component count; index/constants per output | Reorder, duplicate, or drop channels compactly | A swizzle is not a color-space conversion; preserve/declare alpha | **Composable** with Split + Combine; no direct swizzle node |
| `CHAN.COPY` | Copy / set channel from channel | `out=in; out_j=in_k` | Pointwise structural | Source and destination components in compatible units | Replace one component, repair or synthesize channels | Copying green into blue is not white balance; clarify whether original vector is passed through | **Composable** with Split + Combine; no direct node |
| `CHAN.REMOVE` | Remove / neutralize channel | `out_j = neutral_j`; other components unchanged | Pointwise structural | A declared neutral value for the data model | Zero RGB component, reset alpha, remove data plane | “Neutral” is semantic: RGB additive black is 0, alpha opaque is 1, chroma channels may use 0 or midpoint | **Composable** with constants + Combine; no direct node |
| `CHAN.SET` | Set channel | `out_j = value_or_plane` | Pointwise structural | Known target component and scalar/image input | General replacement primitive underlying copy/remove | Must define extent sampling, broadcast, and alpha state | **Composable**; recommended direct structural primitive |

## Componentwise vector math

| Stable ID | Operation | Canonical formula / algorithm | Scope | Required domain / state | What it does / common uses | Composition cautions | Current Stack status based on audit |
|---|---|---|---|---|---|---|---|
| `CHAN.GAIN` | Per-channel gain | `out = in ⊙ g`, where `g=(gR,gG,gB[,gA])` | Pointwise componentwise | Numeric vector; named encoding/alpha policy | RGB balance, simple WB gains, channel intensity | On encoded RGB it is not exposure; changing alpha separately can break premultiplication | **Composable** with Split/DataMath/Combine; RAW WB and Warmth use node-local channel math |
| `CHAN.OFFSET` | Per-channel offset | `out = in + o` | Pointwise componentwise | Numeric vector; named encoding | Color cast, black-level/channel correction, printer-light-like building block | Offsets in encoded, log, and linear spaces look very different; preserve negatives until deliberate clamp | **Composable**; 3-Way Grade and Warmth contain specialized offsets |
| `CHAN.CLAMP` | Per-channel clamp | `outᵢ = clamp(inᵢ,loᵢ,hiᵢ)` | Pointwise componentwise | Numeric vector and bounds | Legal ranges, per-plane limiting | RGB-cube clamp is not general gamut mapping and can change hue | **Composable** with DataMath Clamp after Split; many nodes clamp internally |
| `CHAN.DIFF` | Channel difference | Examples: `R-G`, `B-G`, or `v_i-v_j` | Pointwise scalar output | Compatible component units | Opponent signals, channel artifacts, masks | Output is signed; clipping destroys direction; compare camera greens only after accounting for CFA/sample state | **Composable** with Split + Subtract |
| `CHAN.RATIO` | Channel ratio | Examples: `R/G`, `B/G` | Pointwise scalar output | Denominator policy; usually positive linear measurements | Chromaticity-like analysis, WB/scientific ratios | Extremely unstable near zero; encoded RGB ratios are usually not physically meaningful | **Composable but unsafe** because DataMath Divide has a verified sign defect and no canonical zero policy |
| `CHAN.MIN` | Smallest component | `m = min_i(v_i)` | Pointwise reduction to scalar | Ordered components | HSV/HSL helpers, masks, channel envelope | Not perceptual darkness; alpha should normally be excluded | **Composable** with Split + Minimum |
| `CHAN.MAX` | Largest component | `M = max_i(v_i)` | Pointwise reduction to scalar | Ordered components | HSV value helper, masks, dominant strength | Not luminance; alpha should normally be excluded | **Composable** with Split + Maximum |
| `CHAN.SUM` | Component sum | `s = Σᵢvᵢ` | Pointwise reduction to scalar | Numeric vector | Normalization, chromaticity, diagnostics | Components must share units; exclude alpha unless intended | **Composable** with Split + Add; no direct vector reduction |
| `CHAN.MEAN` | Component mean | `μ = (1/N)Σᵢvᵢ` | Pointwise reduction to scalar | Numeric vector, `N>0` | Simple gray/data average, diagnostics | Arithmetic RGB mean is not standard luminance or luma | **Composable** with Split + Average |
| `CHAN.RANK` | Channel ranking / argmax | Return sorted indices or `argmax_i(v_i)` | Pointwise categorical output | Ordered vector and tie policy | Dominant-channel classification, procedural keys | Requires integer/category output type and deterministic tie handling; ranking RGB is space-dependent | **Absent**; current graph lacks integer/category values |

## Linear algebra and vector measurements

| Stable ID | Operation | Canonical formula / algorithm | Scope | Required domain / state | What it does / common uses | Composition cautions | Current Stack status based on audit |
|---|---|---|---|---|---|---|---|
| `CHAN.DOT` | Dot product | `d = a·b = Σᵢaᵢbᵢ` | Pointwise scalar output | Equal-length vectors; compatible basis/units | Luma/luminance, projection, similarity building block | Dot product alone is not cosine similarity; color weights must match the encoding/primaries | **Absent** generically; Rec.709-weighted dots are hard-coded in several nodes |
| `CHAN.LENGTH` | Vector magnitude | `‖v‖₂ = √(v·v)` | Pointwise scalar output | Euclidean vector in a meaningful metric space | Normal magnitude, color distance building block, diagnostics | Euclidean distance in encoded RGB is not perceptually uniform; alpha exclusion must be explicit | **Absent**; recommended primitive |
| `CHAN.NORMALIZE` | Unit-vector normalize | `v̂ = v/max(‖v‖₂,ε)` | Pointwise vector output | Euclidean vector, zero-vector policy | Directions, normals, specialized color/procedural math | Destroys magnitude/brightness; not image “normalization” by min/max; choose zero fallback | **Absent** generically |
| `CHAN.DISTANCE` | Euclidean vector distance | `d(a,b)=‖a-b‖₂` | Pointwise two-vector scalar output | Both vectors in same metric space | Color keys, similarity, normal/depth comparison | For perceptual color use a named color-difference metric/space; RGB scale changes tolerance | **Node-specific** in target-color/background operations; no reusable primitive |
| `CHAN.COS_SIM` | Cosine similarity | `s=(a·b)/(‖a‖‖b‖)` with zero policy | Pointwise two-vector scalar output | Nonzero vectors in same basis | Direction/chromaticity similarity | Ignores magnitude; unstable at zero; often inappropriate for light values | **Absent**; useful but not essential for the first UI |
| `CHAN.CROSS` | 3D cross product | `a×b=(a_yb_z-a_zb_y, a_zb_x-a_xb_z, a_xb_y-a_yb_x)` | Pointwise 3-vector output | Oriented 3D vector basis | Normal/tangent calculations; rare procedural color use | RGB components are not usually a geometric basis; keep as advanced vector math | **Absent** |

## Matrix transforms and scalar-to-color mappings

| Stable ID | Operation | Canonical formula / algorithm | Scope | Required domain / state | What it does / common uses | Composition cautions | Current Stack status based on audit |
|---|---|---|---|---|---|---|---|
| `CHAN.MAT3` | General 3×3 matrix / RGB matrix / channel mixer | `out = M·rgb`; `out_i=Σ_jM_ij·in_j` | Pointwise linear transform | Explicit row/column convention; source/destination bases; typically unpremultiplied RGB | Channel mixer, primary conversion, crosstalk, grayscale rows, linear color transforms | **Channel mixer and RGB matrix are the same primitive.** A matrix does not linearize a transfer function or adapt an unknown white point | **Absent** generically; RAW uses node-local camera matrices |
| `CHAN.AFFINE3` | Matrix plus offset | `out = M·rgb + b` | Pointwise affine transform | Same as matrix plus compatible offset units | ASC-CDL-like building block, YCbCr-style transforms, general channel mapping | Order matters; affine transform is not invertible when `M` is singular; clipping is separate | **Absent** generically; local shaders compose equivalent math |
| `CHAN.GRAYSCALE` | Grayscale / scalar extraction | `g = w·rgb`, with named weights and transfer state | Pointwise scalar or replicated RGB | Choose method: scene-linear luminance `Y`, encoded luma, mean, HSV value, etc. | Monochrome, masks, analysis | “Luminance-like” is too vague. Rec.709 coefficients on encoded values produce luma-like data, not physical luminance | **Implemented node-specifically** as Luminance Mask and in many shaders; current inputs are semantically untyped |
| `CHAN.FALSE_COLOR` | Scalar-to-color map | `rgb = F(s)` using a named analytic ramp or 1D LUT | Pointwise scalar→vector | Scalar range/domain and color-map definition | Heat maps, exposure/focus/debug views | Color map should be perceptually ordered where analysis matters; avoid implying recovered color | **Node-specific** in Spectrum View/scopes; no general false-color node |
| `CHAN.CHROMATICITY` | Sum-normalized chromaticity | `c = rgb/(R+G+B)` with zero policy | Pointwise vector output | Usually nonnegative linear RGB in known primaries | Separate relative channel proportions from magnitude | This is not CIE `xy` chromaticity and is unstable near black; negative wide-gamut RGB needs policy | **Absent**; optional analysis primitive |

## Clarifications and aliases

- **Channel mixer**, **RGB matrix**, and a three-row weighted channel sum are one canonical `CHAN.MAT3` primitive with different presets/UIs.
- **Copy channel** and **remove channel** are presets of `CHAN.SET`; **swizzle** is a compact multi-set operation.
- **Grayscale** must expose a method. For Rec.709/sRGB primaries, linear-light luminance uses the familiar `0.2126R + 0.7152G + 0.0722B` only after transfer decoding. Applying those coefficients directly to encoded channels produces a luma-like signal, which can still be useful but is not luminance.
- A matrix changes coordinates/basis. It does **not** by itself perform sRGB decode/encode, tone mapping, gamut mapping, or a general ICC/DCP transform.

## Recommended Stack treatment

- Add first-class `Scalar`, `Boolean`, `Vec2/3/4`, and `Mat3/4` values so channels stop pretending to be generic images.
- Implement extract, construct, swizzle/set, dot, length/distance, component reductions, `Mat3`, and affine `Mat3+b` as primitives.
- Offer Channel Mixer, Grayscale, RGB Balance, and simple WB gains as convenience nodes/presets over those primitives.
- Require a color-state declaration before presenting a matrix as a color-space conversion. Otherwise label it numeric channel math.
- Preserve alpha by default; a separate alpha/compositing document should own premultiply/unpremultiply behavior.

## Authoritative references

- [W3C CSS Color 4 — linear RGB, XYZ, Lab/OKLab conversions and sample code](https://www.w3.org/TR/css-color-4/)
- [OpenGL Shading Language 4.60 — vector constructors, swizzles, dot/cross/normalize and matrix semantics](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.pdf)
- [Adobe DNG 1.7.1 — camera matrices, illuminants, analog balance, and camera-to-XYZ processing model](https://helpx.adobe.com/content/dam/help/en/camera-raw/digital-negative/jcr_content/root/content/flex/items/position/position-par/download_section_733958301/download-1/DNG_Spec_1_7_1_0.pdf)
- [ICC.1:2022 profile specification — matrix/TRC and LUT color-transform models](https://www.color.org/specification/ICC.1-2022-05.pdf)
