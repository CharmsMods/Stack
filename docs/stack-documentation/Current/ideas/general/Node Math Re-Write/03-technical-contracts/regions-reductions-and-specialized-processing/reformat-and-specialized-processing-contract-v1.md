# Phase 6C Geometry And Specialized-Stage Contract v1

- Status: accepted and implemented; Phase 6C and Phase 6 complete 2026-07-17
- Accepted: 2026-07-17
- Owning decisions: NMR-137 and NMR-138
- Scope: one true extent-changing geometry node and typed boundaries for the
  specialized systems required by the Phase 6 exit gate

## Purpose

Phase 6C closes the remaining Phase 6 gaps without starting the broad public
node-library work in Phase 7. It proves that Stack can change an image's real
downstream raster extent and can describe non-pointwise systems honestly in the
same planner used by pointwise, neighborhood, and reduction work.

The one geometry vertical slice is the public `Reformat` node:

```text
Image -> Reformat -> Image with an explicitly declared width and height
```

RAW, frequency, external, scope, preview, and export work remain specialized
boundaries. They are not flattened into pointwise math or decomposed into
misleading ordinary nodes.

## Reformat Definition

- Definition ID: `stack:geometry/reformat`
- Definition version: `1.0.0`
- Input: one required `ColorImage` image socket.
- Output: one `ColorImage` image socket.
- Parameters: integer Width and Height from 1 through 32768 pixels, and a
  non-animatable reconstruction selection.
- Reconstruction in v1: `Nearest` or `Linear`.
- Border in v1: explicit `Clamp` only.
- Execution capability: `SampleResample`.
- Physical execution: a full-frame materialization and pointwise-fusion
  barrier in v1.

The output full window has the declared width and height. Its origin, raster
origin convention, and pixel aspect come from the input when known. The output
data window is the complete output full window. Reformat changes no color,
transfer, reference, range, precision, or alpha-association meaning.

## Sampling Math

For output pixel index `x` and corresponding input width `Wi` and output width
`Wo`, the source coordinate is:

```text
sx = ((x + 0.5) / Wo) * Wi - 0.5
```

The Y coordinate uses the same rule. This is inverse pixel-center mapping.

- Nearest selects `floor(s + 0.5)` with coordinates clamped to the nearest
  source pixel.
- Linear evaluates the four surrounding texels with separable linear weights
  and clamps every fetched coordinate to the source bounds.
- R, G, B, and A are sampled independently. Stack does not premultiply,
  unpremultiply, clamp values, repair transparent color, or insert a color
  conversion.

The CPU reference and production GLSL use the same mapping. Linear
minification in v1 is reconstruction only; it does not claim an area,
pyramid, EWA, or other antialiasing prefilter.

## Region And Extent Planning

Reformat declares an inverse ROI mapping with reconstruction support and Clamp
border behavior. The planner propagates its new spatial descriptor through
downstream nodes. Because the current renderer materializes Reformat as a
full-frame boundary, a graph containing it is valid but not tileable through
that stage.

When a multi-image operation receives different spatial descriptors and has no
declared alignment policy, planning fails before renderer submission. The
diagnostic tells the user to add an explicit Reformat. Stack does not stretch,
crop, align, or choose a reference extent silently.

Scale-1 authored graph execution is the live v1 path. The contract can map a
declared render scale, but general proxy execution through arbitrary graphs is
not claimed by this slice.

## Specialized Stage Plans

Every selected specialized boundary records its capability, logical input and
output family, region requirement, render-scale policy, cancellation policy,
and an explanatory reason.

| Boundary | Typed contract | Region/scale | Cancellation |
| --- | --- | --- | --- |
| RAW decode | `Raw -> DataImage` | Full frame; proxy allowed | Between stages |
| RAW development | `Raw -> ColorImage` | Full frame; proxy allowed | Between stages |
| RAW neural/external | `Raw -> Raw` | Full frame; exact requested scale | Cooperative external |
| Multi-frame merge | `ColorImage -> ColorImage` over the declared frame set | Full frame; full quality | Between stages |
| FFT | `DataImage -> ComplexSpectrum` | Full frame; exact scale | Between stages |
| Inverse FFT | `ComplexSpectrum -> DataImage` | Full frame; exact scale | Between stages |
| Frequency operation | `ComplexSpectrum -> ComplexSpectrum` for the representative retained-spectrum path | Full frame; exact scale | Between stages |
| Scope analysis | `ColorImage -> Analysis` | Declared bounded proxy/global population | Between stages |
| Preview readback | `ColorImage -> presentation pixels` | Bounded proxy allowed | Between stages |
| Export readback | `ColorImage -> output pixels` | Full quality | Between stages |

RAW nodes remain opaque and nondeconstructible. Missing external RAW neural
execution and failed frequency execution publish explicit diagnostics instead
of looking like a valid empty result.

Scope, preview, and export plans are consumer boundaries outside graph math.
They record their input/output extents and quality policy in execution stats,
and `changesGraphResult` is always false. Preview or scope downsampling does not
mutate the authored graph, its semantic descriptor, or export pixels.

## Required Proof

The geometry live graph is authored through the real editor graph, serialized,
reloaded, lowered, and executed:

```text
4x3 Image -> 7x5 Linear Reformat -> Exposure 0.25 EV -> Output
```

It must publish a 7x5 nonzero viewport texture and match the CPU reference
within `2.5e-3`. Save/load must preserve the exact definition and settings.
Preview, scope, and export calls must record typed non-mutating consumer
boundaries.

The specialized live graph is:

```text
8x4 Image -> FFT -> Inverse FFT -> Output
```

The planner must report one forward and one inverse typed full-frame multipass
stage. The live round trip must match the source within `7.5e-3` and publish no
specialized failure.

Generated and graph tests also cover settings validation, odd extents,
nonzero-origin propagation, ROI mapping, nearest/linear RGBA behavior,
descriptor preservation, explicit mismatch failure, RAW stage classification,
and the scope/preview/export scale policies.

## Phase Boundary

Phase 6C does not add Crop, Canvas Resize, Rotate, affine/projective transforms,
coordinate fields, arbitrary samplers, higher-order reconstruction, minification
prefilters, pyramids, morphology, more reductions, more public primitives, RAW
decomposition, color or alpha conversion, viewport transforms, or historical
project compatibility. Those are separate product choices. Phase 7 remains
inactive until explicitly started.
