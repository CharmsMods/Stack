# Primitive-Node Taxonomy

Status: proposed organization, not an implementation specification  
Stack ground-truth date: 2026-07-12

## Recommendation

Stack should use a small **primary family** for browsing and color, then attach orthogonal tags for information scope, numerical domain, execution class, range behavior, alpha behavior, and extent behavior. No single tree can truthfully express all of those facts.

The recommended public primitive families are:

1. **Value Math**
2. **Channels and Representation**
3. **Tone and Color Mapping**
4. **Masks and Combine**
5. **Sampling and Geometry**
6. **Neighborhood Processing**
7. **Analysis and Reduction**
8. **Context and Specialized Processing**

These names describe what a node computes. They avoid pretending that a friendly editing term uniquely defines an algorithm. A node named `Multiply` belongs to Value Math whether a user employs it for exposure, mask gain, or something unusual. A high-level `Exposure` node may be shown in a separate convenience-tool browser even if its canonical decomposition uses a linear-light conversion and a multiply operation.

Color is only a navigation aid. Every family must also have a written name, icon, search tokens, and accessible pin/link styling. Two nodes must never become indistinguishable solely because a user cannot distinguish their accent colors.

## Why one hierarchy is not enough

An operation can be classified in several independently correct ways. Gaussian blur is a filter, a convolution-like neighborhood operation, a spatially local algorithm, a sampler consumer, a pass with a halo, and—under Stack's current renderer—a separate full-canvas target. None of those descriptions replaces the others.

Stack therefore needs to keep these concepts separate:

| Concept | What it answers | Where it belongs |
|---|---|---|
| Primary family | Where can a person find the node? | Browser, node accent, documentation |
| Value type | What can connect to this port? | Typed sockets and graph validation |
| Semantic descriptor | What do these numbers mean? | Buffer/value metadata and wire badges |
| Information scope | What data does one result depend on? | Documentation, planner, search filters |
| Execution class | What kind of work must the renderer schedule? | Compiler/lowering layer |
| UI control | How is a parameter best edited? | Parameter schema and node inspector |
| Convenience category | What familiar task does this help accomplish? | Optional high-level tool browser |

Treating any one of these as the master taxonomy would recreate the ambiguity the project is intended to remove.

## Taxonomy schemes considered

### Scheme A — familiar editor categories

Examples: Light, Color, Detail, Effects, Optics, Geometry, Masking, Output.

This is easy for Lightroom- or Photoshop-oriented users. It is poor for primitives because the same operation may serve many tasks and familiar names are not mathematical definitions. `Power` could implement gamma-like mapping, part of a transfer function, or arbitrary data math. `Matrix Multiply` could perform a color transform, channel mix, or coordinate transform.

**Use:** high-level convenience browser and search aliases.  
**Do not use:** as the authoritative primitive classification.

### Scheme B — mathematical technique

Examples: Arithmetic, Functions, Interpolation, Matrix Operations, Convolution, Morphology, Statistics, Optimization.

This is factual and educational. It becomes overly fragmented in a node browser, and some operations span several techniques. A bilateral filter uses neighborhood sampling, range weighting, exponentials, accumulation, and normalization. A dehaze tool may combine a local estimator, guided filtering, division, clipping, and tone/color behavior.

**Use:** subcategories, documentation, and search tags.  
**Do not use:** as dozens of top-level colors.

### Scheme C — information scope

The package uses the scope codes `P`, `V`, `N`, `C`, `G`, `M`, `X`, `I`, and `L` for current-pixel, vector/channel, neighborhood, coordinate, global, multi-resource, metadata, iterative, and learned requirements.

This predicts implementation complexity better than friendly categories. It also directly challenges the false assumption that all image math is one-pixel math. It is awkward as the sole browser because users do not naturally search for “global reduction” when they want automatic levels.

**Use:** mandatory tags, compiler planning, documentation, filters.  
**Do not use:** as the only visible hierarchy.

### Scheme D — execution pass class

Examples: fusable pointwise expression, texture sampler, neighborhood kernel, resample, reduction, multipass/iterative, CPU/external, source/sink.

This is the right view for a compiler. It is not stable enough to define a user's node library: the same mathematical node can be inlined into a fused shader in one graph and materialized as a pass at a fan-out, debug preview, cache, or precision boundary in another.

**Use:** lowering and performance inspection.  
**Do not use:** as the semantic identity of a node.

### Scheme E — data-domain or port-type hierarchy

Examples: Scalar, Vector, Color, Mask, Image, Curve, Matrix, Histogram, Metadata, Raw.

These distinctions are essential for connections, but they classify values rather than operations. `Remap` can act on a scalar, channel vector, mask, or image. `Apply Matrix` can act on RGB or coordinates.

**Use:** sockets, compatibility rules, overload resolution, badges.  
**Do not use:** as the top-level operation browser.

## Recommended primary families

### 1. Value Math

Pure numerical building blocks that do not inherently carry a photographic interpretation.

Suggested subcategories:

- Arithmetic: add, subtract, multiply, divide, fused multiply-add
- Functions: absolute value, sign, floor, ceiling, fractional part, minimum, maximum
- Powers and logs: power, square root, exponential, logarithm
- Conditions: compare, step, smoothstep, select
- Range mapping: clamp, saturate, normalize, remap, wrap, quantize
- Interpolation: mix, inverse lerp, spline evaluation

Typical tags: `P`; scalar/vector overload; fusable pointwise; preserves extent. Domain requirements such as “positive input” belong in the node contract, not the family name.

### 2. Channels and Representation

Operations that change component organization or the numerical representation in which an operation is expressed.

Suggested subcategories:

- Split, combine, swizzle, extract, insert
- Vector construction, dot product, length, normalization
- Matrix application and channel mixing
- RGB/HSV/HSL/Oklab/XYZ conversions
- Transfer-function decode/encode
- RGB-primary and white-point transforms
- Premultiply and unpremultiply
- Pack/unpack and precision conversion
- Spatial/frequency representation conversion

This family must not imply that all conversions are interchangeable. Transfer encoding, RGB primaries, chromatic adaptation, alpha association, and a scene-to-display view transform are distinct contracts.

### 3. Tone and Color Mapping

Pointwise or curve-driven mappings whose explicit purpose is to change tone or color appearance.

Suggested subcategories:

- One-dimensional curves and ramps
- Exposure-style linear-light scaling
- Defined contrast/pivot mappings
- Lift/gamma/gain families
- Hue/chroma/lightness mappings
- Color balance and tonal weighting
- LUT evaluation
- Tone and gamut mapping

This family does not license vague formulas. `Brightness`, `Contrast`, `Saturation`, and `Vibrance` must still name their exact algorithm, expected color representation, range policy, and alpha behavior. High-level versions may be compounds rather than primitives.

### 4. Masks and Combine

Operations that create or combine coverage/selection fields or combine multiple resources.

Suggested subcategories:

- Mask remap, invert, threshold, boolean-like combine
- Matte and alpha operations
- Interpolation by mask
- Blend modes
- Porter–Duff compositing
- Image arithmetic with multiple inputs
- Merge, difference, reference comparison
- Multi-frame accumulation

Mask coverage, opacity, straight alpha, premultiplied alpha, and an arbitrary grayscale field must be distinct semantic descriptors even if all are stored as one channel.

### 5. Sampling and Geometry

Operations whose meaning depends on coordinates, extent, or resampling.

Suggested subcategories:

- Coordinate generation and conversion
- Translate, rotate, scale, crop, canvas, fit, and align
- Affine, projective, lens, displacement, and arbitrary warp
- Sample image/field
- Resize and reconstruction
- Tiling and boundary handling

Every operation in this family must declare pixel-center convention, coordinate system, output-extent rule, inverse/forward mapping, reconstruction filter, and boundary mode. Stack's current normalized-UV, fixed-global-extent, linear/clamp behavior should not remain an invisible universal default.

### 6. Neighborhood Processing

Operations that require surrounding samples or spatial propagation.

Suggested subcategories:

- Convolution and separable convolution
- Blur and sharpen
- Gradient and edge operators
- Rank filters and morphology
- Local statistics
- Local contrast and detail
- Classical denoise
- Distance and propagation operations

The contract must declare support radius or an ROI-mapping function, border behavior, normalization, separability, and whether the radius is bounded. An implementation may be one pass, several passes, a pyramid, FFT, or specialized compute; that schedule is not the node's semantic family.

### 7. Analysis and Reduction

Operations that reduce a region or whole image into reusable data.

Suggested subcategories:

- Sum, mean, variance, extrema, percentile
- Histogram and cumulative distribution
- Min/max location and bounding box
- Palette and distribution measurements
- Scope data
- Automatic-parameter estimators

The outputs should be typed values such as scalar, vector, histogram, table, or region—not an overloaded mask texture. A scope is normally a sink that visualizes analysis data; the underlying reduction may also be reusable by automatic levels or other compounds.

### 8. Context and Specialized Processing

Operations whose contract fundamentally requires metadata, calibration, temporal state, iteration, external resources, or a dedicated algorithm family.

Suggested subcategories:

- RAW decode, demosaic, black/white normalization, camera calibration
- Profile and display/output transforms
- Lens/camera calibration
- Iterative and multiresolution solvers
- Frequency transforms and structured-domain algorithms
- External model or plug-in operations
- Import, export, view, and diagnostic boundaries

This is deliberately a boundary category, not a miscellaneous drawer. A node belongs here only when reducing it to ordinary scalar/image primitives would discard required context or lie about its execution semantics. Learned and semantic-model tools remain outside the recommended conventional primitive library even though the architecture may eventually host them as specialized operators.

## Mandatory orthogonal tags

Each definition should carry machine-readable tags rather than relying on its folder name.

| Tag axis | Examples | Why it matters |
|---|---|---|
| Information scope | `P`, `V`, `N`, `C`, `G`, `M`, `X`, `I`, `L` | Explains required information and planning class |
| Accepted value type | scalar, vector, color, mask, image, curve, matrix, metadata, Raw | Prevents invalid connections |
| Numerical/color domain | generic numeric, scene-linear RGB, encoded RGB, perceptual, frequency, sensor | Makes formulas interpretable |
| Arity | unary, binary, variadic, dynamic collection | Guides sockets and lowering |
| Range policy | preserve, bounded, clamp, wrap, normalize, undefined outside domain | Prevents hidden clipping |
| Alpha policy | absent, preserve, process as data, requires straight, requires premultiplied, changes association | Prevents compositing errors |
| Extent policy | preserve, derive, union, intersection, explicit canvas, global | Prevents silent resizing |
| Sampling policy | none, nearest, linear, configurable reconstruction; clamp/mirror/wrap/constant | Makes geometry and filters reproducible |
| Execution capability | pointwise-fusable, sampler, reduction, multipass, CPU/external | Lets the compiler plan without redefining semantics |
| Precision need | half-safe, float32 preferred, integer-exact, complex | Avoids silent loss or overflow |

## Color-coding proposal

Stack already has verified Gray, Layer, Preview, Mask, Scope, Generator, and Merge accents. Preserve that visual language where practical instead of introducing a second unrelated rainbow. A compatible mapping is:

| Family | Suggested accent direction | Existing visual relationship |
|---|---|---|
| Value Math | neutral gray | Gray |
| Channels and Representation | cool blue | Generator/typed-data feel |
| Tone and Color Mapping | muted green | current Layer |
| Masks and Combine | purple for mask-producing nodes; teal for combine nodes | Mask and Merge |
| Sampling and Geometry | muted blue/olive | spatial distinction without a bright new accent |
| Neighborhood Processing | muted violet | distinct from pointwise mapping |
| Analysis and Reduction | warm brown/orange | Scope |
| Context and Specialized | subdued gold, with a family badge | Preview/Raw boundary feel |

If splitting Mask and Combine accents within one family is visually useful, the family name and icon remain the primary category while the accent indicates the output role. Pin color should continue to mean **value type**, not node family. Wire pattern or badges should communicate mask/Raw/semantic state. Node color must never claim that an image is linear, encoded, straight-alpha, or premultiplied; those are edge descriptors.

## Naming and discoverability rules

1. Primitive names should describe the operation: `Multiply`, `Smoothstep`, `Dot Product`, `Apply 3×3 Matrix`, `sRGB Decode`, `Premultiply`, `Sample Image`, `Gaussian Convolution`.
2. Ambiguous friendly names require a qualifier or documented definition: `Contrast — Pivot Scale`, not merely `Contrast` if several formulas exist.
3. Search aliases may include common editing terms without changing the factual name. Searching “exposure” may return linear-light multiply and the high-level Exposure compound.
4. Variant explosions should use one typed/parameterized definition when behavior is genuinely the same. They should use separate definitions when domain, range, or alpha semantics differ.
5. Implementation technique must not leak into the public name unless it changes promised behavior. A separable Gaussian implementation is still Gaussian blur; a box approximation must say so.
6. Misleading current names identified by the audit—such as the nonrecursive “Error Diffusion,” shadow-darkening “HDR Compressor,” and non-Hankel “Optical/Hankel Blur”—should be renamed or corrected before they become stable compound dependencies.

## Stack-specific adoption

The current Stack graph has only five coarse socket types and uses `Mask` for several unrelated scalar/field/frequency meanings. Its palette is partly registry-driven and partly hard-coded. Therefore taxonomy work should not begin as a mass category rename.

A safe order is:

1. Add definition metadata for family and tags without changing saved node identity.
2. Introduce first-class value types and semantic edge descriptors.
3. Make browser categories data-driven for registry and explicit nodes through one definition registry.
4. Add filters for scope, domain, and execution capability.
5. Add the compact family accents, icons, text labels, and accessible alternatives.
6. Move familiar convenience nodes into a separate optional browsing lens.
7. Expand the primitive catalog only after pointwise fusion prevents each tiny operation from becoming another full-canvas pass and cached RGBA16F texture.

The operation reference may remain encyclopedic. The default node browser should remain selective, searchable, and much smaller.

## Research basis

The recommendation to separate public family, typed interface, semantic descriptor, information scope, and execution class is consistent with established image/node systems:

- MaterialX separates node definitions and typed interfaces from graph/source implementations: [MaterialX Specification](https://github.com/AcademySoftwareFoundation/MaterialX/blob/main/documents/Specification/MaterialX.Specification.md) and [official API](https://materialx.org/docs/api/).
- Halide separates the mathematical image algorithm from execution scheduling decisions such as placement, fusion, storage, and locality: [Halide paper](https://people.csail.mit.edu/jrk/halide12/halide12.pdf) and [scheduling tutorials](https://halide-lang.org/tutorials/).
- OpenFX separates clip/type preferences, output definition, input ROI requirements, and render windows: [OpenFX image-effect actions](https://openfx.readthedocs.io/en/main/Reference/ofxImageEffectActions.html).
- OpenImageIO represents spatial and channel ROI explicitly and distinguishes image data from its broader full/display window: [ImageBuf](https://openimageio.readthedocs.io/en/latest/imagebuf.html) and [ImageBufAlgo](https://openimageio.readthedocs.io/en/latest/imagebufalgo.html).

These sources support the architecture pattern; they do not dictate Stack's UI labels or override the returned code audit.
