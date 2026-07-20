# Composition and Validation Rules

Status: proposed semantic contract, not current behavior  
Stack ground-truth date: 2026-07-12

## Purpose

Stack should remain permissive like a shader graph without treating every numerically connectable texture as the same kind of image. The graph must distinguish three questions:

1. **Can these values be connected structurally?**
2. **Is the operation mathematically defined for the values it receives?**
3. **Is the connection technically suspicious for the declared image meaning?**

The first two can make execution impossible and justify a hard error. The third should normally produce a warning rather than an automatic conversion or prohibition. This preserves deliberate experimentation while making accidental encoded/linear, alpha, extent, and range mistakes visible.

Validation is analysis. It must not silently alter pixels. A conversion that changes values must be represented by an explicit node or explicit compound stage.

## Current Stack baseline

The returned audit verifies that Stack currently enforces direction, five coarse socket types, Raw isolation, selected scalar/image broadcast rules, one link per input, channel exclusivity, and cycle prevention. It does **not** place color, transfer, range, alpha, resolution, precision, sampling, or scene/display meaning on generic image connections. Ordinary encoded RGBA8 and RAW-derived scene-linear RGB both become `Image`. A warning system cannot truthfully infer what a wire means until that descriptor exists.

The rules below therefore describe a target contract. Existing projects should initially load with explicit `legacy/unknown` descriptors; they must not be silently reinterpreted.

## Required value and image descriptors

Every edge should have a value type. Image-like edges additionally need a semantic descriptor.

### First-class value types

- Boolean and integer
- Scalar and bounded scalar
- 2D/3D/4D vector
- Color value
- Matrix
- Curve/ramp
- LUT handle and declared LUT domain
- Coordinate/UV field and displacement field
- Image and image collection
- Mask/coverage field
- Histogram/table/reduction result
- Metadata/profile/calibration descriptor
- Raw source or Raw stage
- Analysis sink/result

An implementation may use the same GPU texture representation for several types. Storage equivalence is not semantic equivalence.

### Image semantic descriptor

At minimum:

- Channel layout and roles
- Numerical storage/precision
- Declared finite range, whether negative/HDR values are permitted, and whether the range is known
- RGB primaries and white point, when applicable
- Transfer function or explicit linear-light state
- Scene-referred, display-referred, data/noncolor, or unknown state
- Alpha presence and straight/premultiplied association
- Logical full/display window and valid data window
- Resolution, pixel aspect, origin, and coordinate convention
- Sampling and boundary defaults where a sampled view is part of the value
- Time/frame/view identity when relevant
- Provenance flags such as tagged, assumed, converted, generated, or legacy-unknown

Descriptors should propagate as derived analysis data, not be hand-authored on links. A source establishes a descriptor; operations transform, preserve, invalidate, or consume parts of it.

## Diagnostic levels

### Hard error

Use a hard error only when the graph is structurally invalid, the operation cannot produce its promised result, or executing it would rely on information that is absent and cannot be replaced by an explicit declared default.

Examples:

- A curve is connected where an image is required and the node has no defined overload.
- A cycle is introduced into an ordinary acyclic image dependency graph.
- A required input is missing.
- A Raw calibration operation receives an ordinary RGB image without the required sensor metadata.
- A strict compositing operator requires premultiplied inputs and receives a value explicitly tagged straight-alpha.
- Two image extents differ and a combine operation has no alignment/canvas policy.
- A compound instance references a missing definition or a port ID removed without migration.
- A transform that promises an inverse is singular for the current parameters and has no defined fallback.
- A logarithm node is configured with strict real-domain behavior and receives a range proved to include nonpositive values.

A hard-error node does not silently pass through. It returns a typed failure result and a graph diagnostic with a precise repair action.

### Warning

Use a warning when execution is numerically defined but the declared meaning makes the result likely unintended, nonstandard, destructive, or ambiguous.

Examples:

- Exposure-style multiplication is applied to sRGB-encoded RGB.
- Rec.709 coefficients are applied to an image whose primaries/transfer are unknown.
- A traditional artistic blend mode is evaluated in scene-linear RGB when its expected appearance is encoded/display-referred.
- Straight-alpha RGB is blurred without premultiplication, risking colored transparent fringes.
- A `[0,1]` clamp follows a scene-linear HDR buffer.
- A LUT's table domain is valid but its intended input/output color spaces are undeclared.
- A half-float stage receives a range that may overflow or lose necessary precision.
- A global-statistics node appears in a tiled branch and will force whole-region evaluation.
- A node drops metadata that downstream output requires.
- An old project depends on a known legacy formula or undefined alpha convention.

Warnings must not insert a conversion. They should offer explicit fixes such as “Insert sRGB Decode,” “Premultiply before blur,” “Choose canvas policy,” or “Assign LUT color contract.”

### Allowed state with information badge

Use an information badge for unusual but fully declared behavior.

Examples:

- Negative or greater-than-one float values are intentionally preserved.
- A blend is deliberately performed in linear light.
- RGB channels are treated as generic data rather than color.
- A nearest-neighbor sampler is intentionally used for pixel art.
- An operation preserves transparent RGB because the image is explicitly straight-alpha.
- A compound uses an opaque specialized implementation equivalent to its documented decomposition within a stated tolerance.

### Runtime fault

Static analysis cannot prove all pixel values. NaN, infinity, half-float overflow, invalid external resources, shader failure, or unexpected dynamic extent require runtime diagnostics. Runtime faults should name the first node/pass observed, affected region, and whether the output was aborted, substituted, or partially produced.

## Port requirement policy

Each port requirement should declare one of three policies:

- **Agnostic:** the operation treats the input as generic numbers and preserves compatible descriptor fields.
- **Recommended:** execution is defined for other states, but a mismatch creates a warning.
- **Strict:** a mismatch is a hard error because the promised operation is not defined without that state.

For example, generic `Multiply` is color-domain agnostic. `Exposure EV` should recommend scene-linear RGB and warn on encoded input. `sRGB Decode` strictly expects sRGB-encoded numeric RGB; applying it to already-linear data is not a second creative interpretation of the same operation.

Unknown metadata is not equal to matching metadata. When a requirement matters, `unknown` produces at least a warning and may be a hard error for calibration/output operations.

## Composition rules by concern

### 1. Type, dimensionality, and units

1. Exact type matches are allowed.
2. Defined, visible overloads may accept several types.
3. Scalar-to-vector or scalar-to-image broadcast is allowed only when the node definition promises it; the UI should mark the broadcast.
4. Image-to-scalar conversion always requires an explicit extraction or reduction.
5. Mask, alpha, opacity, luma, and arbitrary scalar fields are not interchangeable merely because all are one channel.
6. Angle, pixel distance, normalized coordinate, EV, percent, and code-value units should be declared. Incompatible units require an explicit conversion.
7. Complex/frequency data cannot travel as a generic color image without an explicit pack/unpack or view operation.

### 2. Numerical domain and range

Each operation declares its mathematical domain and output range behavior:

- `preserve`: no intentional clamp or normalization
- `bounded`: output is mathematically bounded by the formula
- `clamp`: explicitly clips to a named interval
- `normalize`: derives scale from declared local/global data
- `wrap`: applies modular behavior
- `undefined/strict`: invalid inputs are an error
- `guarded`: uses a documented epsilon or fallback

Rules:

1. Clamping is an explicit destructive operation, not a generic safety mechanism.
2. Divide must define zero behavior and preserve denominator sign if it uses an epsilon. The audited `max(abs(b),1e-5)` guard loses sign and should not become the general primitive rule.
3. Log, square root, and fractional power must define negative/zero handling. “Clamp until it works” is one possible named mode, not an invisible default.
4. A range analysis may prove safe composition; otherwise Stack warns or inserts runtime instrumentation according to the port policy.
5. NaN/Inf propagation, scrubbing, visualization, and replacement are separately selectable policies. Debug mode should preserve and highlight the first invalid value instead of hiding it through a later clamp.
6. Precision lowering must be deliberate. A half-safe node may remain RGBA16F; reductions, large accumulations, matrices, or frequency work may require float32.

### 3. Color representation

1. RGB primaries, white point, and transfer encoding are separate fields.
2. A matrix that changes primaries does not decode or encode a transfer function.
3. Transfer decode/encode, chromatic adaptation, RGB-space conversion, view transform, look transform, and output transform remain distinct operations even if a convenience compound chains them.
4. Color-agnostic arithmetic preserves the descriptor only when the operation preserves its interpretation. A nonlinear arbitrary channel operation may invalidate a claim that the values remain a standard encoded color signal; provenance should reflect that.
5. Color-aware nodes declare strict or recommended domains. Saturation in Oklab, saturation in HSV, and chroma scaling in linear RGB are separate definitions.
6. Data images bypass color warnings only when explicitly tagged noncolor/data.
7. Blend-space choice is part of the blend definition. Stack must not infer linear versus encoded from a node's friendly name.
8. A view transform affects viewing. It must not silently rewrite the working graph's buffer descriptor unless it is an explicit in-graph transform producing a display-referred value.

### 4. Alpha and compositing

1. Every RGBA image declares straight or premultiplied alpha; `unknown` is transitional legacy state only.
2. Premultiply and unpremultiply are explicit representation conversions. Unpremultiply must define behavior near alpha zero.
3. Porter–Duff operators use normative equations for a declared association and must be tested with translucent foreground and backdrop values. The current audited `Alpha Over` is neither correct straight nor premultiplied source-over and should be versioned as legacy behavior until corrected.
4. An ordinary color adjustment normally preserves alpha and adjusts RGB according to the declared association. It must not force alpha to one.
5. Neighborhood filtering of premultiplied RGBA can process associated channels together. Filtering straight RGB requires a declared edge policy and normally warrants a warning.
6. A layer mask is coverage controlling interpolation; it is not automatically image alpha and should not be mixed into all four RGBA channels.
7. Additive light effects must state whether they modify alpha. “Adds RGB and alpha because the shader is componentwise” is not an acceptable implicit contract.
8. Export must explicitly choose alpha association and convert to what the target format expects.

The W3C compositing specification is a useful normative reference for source-over and premultiplication concepts: <https://www.w3.org/TR/compositing-1/>.

### 5. Extent, data window, and alignment

Stack should distinguish a logical full/display window from the valid data window, following the separation used by professional image APIs such as OpenImageIO and OpenEXR.

1. Each image declares origin, resolution, pixel aspect, full window, and data window.
2. Pointwise operations preserve both windows unless documented otherwise.
3. Geometry operations declare how output extent is derived: preserve, explicit canvas, transformed bounds, crop, union, or intersection.
4. Multi-image operations require an alignment policy. Different extents must not be silently normalized into the current global canvas.
5. Pixels outside the data window require a boundary value/policy; absence is not the same as transparent black unless declared.
6. Empty, infinite/unknown, and dynamically derived regions are distinct states.
7. ROI is an evaluation request, not a selection mask. A render planner may ask for a rectangle without changing the image's visual coverage.

OpenImageIO's ROI and image-window contracts are documented at <https://openimageio.readthedocs.io/en/latest/imagebuf.html> and <https://openimageio.readthedocs.io/en/latest/imagebufalgo.html>.

### 6. Sampling, coordinates, and neighborhood support

1. Coordinate space and pixel-center convention are explicit.
2. Geometric sampling uses inverse mapping unless the node specifies a splat/forward method.
3. Reconstruction filter is explicit: nearest, linear, cubic, Lanczos, or another named kernel.
4. Boundary mode is explicit: constant, clamp, mirror, wrap, or invalid.
5. A neighborhood node declares support/halo as a function of parameters and scale. Gaussian radius, sharpen support, and morphology radius are not planner guesses.
6. A transform maps a requested output ROI back to required input ROI. A global reduction declares that it needs the full relevant region. An unbounded or content-adaptive operator must say so.
7. Render scale and proxy behavior are part of the contract for radius, frequency, and coordinate parameters.
8. Tile equivalence must be tested: a tiled render with required halos must match an untiled render within the declared tolerance.

OpenFX formalizes output region-of-definition, input regions-of-interest, render windows, and clip preferences: <https://openfx.readthedocs.io/en/main/Reference/ofxImageEffectActions.html>.

### 7. Metadata and provenance

Each operation declares a metadata policy:

- **Preserve** unchanged
- **Transform** according to a defined mapping
- **Consume** to produce pixels or another value
- **Invalidate** because the meaning no longer holds
- **Generate** a new declared value
- **Drop intentionally**, with a warning if downstream output depends on it

Rules:

1. RAW camera/CFA/black/white/calibration metadata stays with Raw stages until an explicit operation consumes it. It cannot be reconstructed from generic RGB.
2. Color profile and transfer metadata should survive import or be converted into an explicit working descriptor.
3. LUT table domain is not a complete color contract. Expected primaries, white point, transfer, range, and output meaning remain separate.
4. A copied or compounded branch preserves provenance and definition version.
5. Export declares target format, bit depth, color encoding/profile, alpha association, and range behavior. Untagged RGBA8 must be an explicit legacy/export mode, not the invisible default.

### 8. Noncommutativity and ordering

The DAG establishes an exact order, but correct connection types do not imply operations commute. Stack should display education-oriented warnings for known high-impact cases without prohibiting them:

- exposure before versus after transfer encoding
- blur before versus after geometry/resampling
- premultiply before versus after color adjustment
- clamp before versus after blend
- tone mapping before versus after compositing
- gamut mapping before versus after creative grading
- reductions before versus after masks/crops

A compound's canonical order is part of its versioned definition.

## Example diagnostic matrix

| Connection or state | Level | Reason | Suggested repair |
|---|---|---|---|
| Scalar → RGB multiply with declared broadcast | Allowed/info | Defined overload | Show broadcast badge |
| RGB image → scalar-only port | Hard error | No defined reduction | Insert channel extraction or reduction |
| Encoded sRGB → Exposure EV | Warning | Numerically valid, not light-proportional | Insert sRGB Decode or acknowledge |
| Linear RGB → sRGB Decode | Hard error | Strict transform receives wrong state | Remove or use sRGB Encode where intended |
| Unknown color state → colorimetric matrix | Warning or error by port policy | Meaning cannot be confirmed | Assign/convert color descriptor |
| Straight RGBA → premultiplied source-over | Hard error | Formula contract mismatch | Insert Premultiply |
| Straight RGBA → Gaussian blur | Warning | Transparent RGB may fringe | Premultiply, blur, then unpremultiply if needed |
| HDR image → explicit Clamp `[0,1]` | Allowed/info | Destruction is explicit | Show range-loss badge |
| HDR image → node that silently clamps | Definition error | Node contract and implementation disagree | Fix implementation/version definition |
| Different extents → Mix with no alignment mode | Hard error | Corresponding samples undefined | Choose canvas/alignment/resampler |
| Gaussian blur radius 20 in tiled render | Allowed | Halo can be derived | Planner requests 20-pixel support |
| Histogram in a tile-local branch | Warning/info | Forces global/relevant-region reduction | Show materialization/global-cost badge |
| RAW demosaic without CFA metadata | Hard error | Required calibration absent | Connect Raw source/stage |
| LUT with valid table but unknown color expectation | Warning | Numeric evaluation possible; meaning ambiguous | Assign LUT input/output contract |
| Legacy project `Image` descriptor | Warning | Current output meaning is unknown | Keep compatibility mode; offer explicit migration |

## Validation stages

### Connection time

Fast structural checks only: direction, type/overload, one-link policy, cycle prevention, definition availability, and obvious strict descriptor mismatch. Rejection text should name the incompatible fields.

### Graph semantic analysis

Propagate descriptors and ranges through the whole graph; report warnings, required conversions, extent conflicts, alpha association, precision risk, global-cost boundaries, and metadata loss. Diagnostics attach to stable node/link/port IDs and persist until the semantic fingerprint changes.

### Lowering and planning

Resolve compounds, verify implementation capabilities, calculate ROI/halos, choose precision, identify fusion groups, allocate targets, and reject unsupported schedules. A legal authored graph may still fail lowering on hardware that lacks a required capability; that is an execution-target diagnostic, not a type mismatch.

### Runtime

Capture shader/GL errors, invalid external resources, NaN/Inf/overflow instrumentation, allocation failure, cancellation, and CPU/GPU exceptions. Diagnostic propagation must distinguish “no image,” “transparent/black image,” and “failed image.”

## Warning acknowledgement

Advanced users may acknowledge a warning on a link or node. The acknowledgement should:

- store the diagnostic rule ID and semantic fingerprint
- remain visible in an inspector/history
- reset if the definition version, relevant descriptor, or connection changes
- never suppress hard errors
- not mutate the graph or insert hidden conversions

Global “disable all color warnings” switches should be avoided. A project-level permissive mode may reduce visual noise, but export/preflight should still list acknowledged semantic risks.

## Backward compatibility

Existing Stack outputs depend on untagged color, undefined alpha, global normalized sampling, inconsistent clamps, and some incorrect or misleading formulas. Migration must preserve reproducibility before improving meaning.

1. Load current projects into a pinned `legacy-v3` semantic mode.
2. Tag generic old images as `legacy/unknown`, not assumed-linear and not silently sRGB-decoded.
3. Preserve known legacy node formulas under versioned definitions, including current Alpha Over, until the user chooses migration.
4. Offer a migration report showing pixel-changing repairs: color assumptions, alpha conversion, extent policy, corrected formulas, and output transform.
5. Save migrated graphs with stable definition and port versions; never silently discard invalid links or replace unknown kinds with a generic layer.
6. Provide side-by-side or difference preview for migrations.

## Verification requirements

No semantic rule is complete until it has reference tests:

- CPU reference versus GPU implementation for each primitive
- property tests for range/domain and descriptor propagation
- translucent straight/premultiplied compositing fixtures
- tagged color import, transfer, view, and export fixtures
- odd-origin/data-window and mixed-extent fixtures
- border-mode and tiled-versus-full neighborhood tests
- NaN/Inf and half-float stress tests
- compound version and migration tests
- warning/error golden graphs
- legacy-output snapshots before any corrective migration

The current structural graph tests do not exercise GPU formula, color, alpha, or import/export correctness. Validation UI should not claim guarantees that the reference suite does not establish.

