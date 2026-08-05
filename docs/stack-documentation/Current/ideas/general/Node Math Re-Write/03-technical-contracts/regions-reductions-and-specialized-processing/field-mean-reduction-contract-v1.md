# Phase 6B Reduction Contract v1

- Status: accepted and implemented; Phase 6B complete 2026-07-17
- Accepted: 2026-07-17
- Owning decision: NMR-136
- Scope: one reusable full-frame reduction from a scalar field to a uniform scalar

## Purpose

Phase 6B proves that Stack can measure a per-pixel field once and use the
result as a normal graph value. The first public vertical slice is `Field
Mean`:

```text
Scalar field -> Field Mean -> Scalar
```

The result can drive an existing uniform input such as Exposure EV. A normal
image is not silently converted to luminance. The user first chooses an
explicit one-channel source, for example `Channel Split -> R` or `Luminance
Mask`, and then connects that field to `Field Mean`.

## Exact Node Contract

- Definition ID: `stack:analysis/field-mean`
- Definition version: `1.0.0`
- Input: `fieldIn`, required, logical type `ScalarField`, per-pixel, one
  channel, units Unknown in v1.
- Output: `valueOut`, logical type `Scalar`, uniform, one component, units
  Unknown in v1. The node performs no unit conversion.
- Population: every pixel in the connected field's current full/data window.
  Phase 6B has no mask, ROI, crop, or selection input.
- Formula: arithmetic mean, `sum(x_i) / N`, for `N > 0`.
- Materialized input: the renderer's current one-channel field texture. Stack
  reads the red component because scalar fields are materialized identically
  in all RGB components by the existing field paths.
- Accumulation: deterministic input order with compensated float64 summation.
  The source samples retain their materialized float precision.
- Non-finite policy: any NaN or infinity makes the reduction fail. Empty input
  also fails. Stack does not ignore, clamp, or replace bad samples.
- Failure behavior: the dependent render is not submitted; execution
  inspection names the reduction node and reason. No stale measured value is
  presented as current.

## Scheduling And Caching

`Field Mean` is a global `Reduction` capability. It requires the complete
connected scalar field and is a full-frame boundary in the Phase 6 region
planner. It is also a pointwise-fusion barrier.

The first execution path uses exact CPU readback of the materialized field in
bounded row chunks. This is deliberately a correctness path, not the final GPU
parallel-reduction design. Its runtime cache key includes the exact node
definition, input fingerprint, extent, and algorithm version. The measured
number is runtime data and is never serialized into the project.

## Required Vertical-Slice Proof

The authored live graph is:

```text
Image -> Channel Split -> R -> Field Mean -> Exposure EV
Image -------------------------------> Exposure Image -> Output
```

It must produce a nonzero output and match the same graph driven by the known
constant mean within the existing RGBA16F tolerance. A second execution must
reuse the reduction result when its definition, input, and extent are
unchanged. The planner must report a Reduction stage and require full-frame
execution.

Generated CPU cases cover ordinary values, cancellation-sensitive values,
empty input, and NaN/infinity failure. Graph tests cover socket metadata,
explicit extractor requirements, persistence, registry resolution, and the
scalar connection into Exposure EV.

## Phase Boundary

Phase 6B does not add weighted mean, min/max, sum, variance, percentiles,
histograms, statistics resources, masks, ROI, automatic luminance selection,
automatic exposure policy, GPU reduction trees, extent-changing geometry,
pyramids, specialized RAW/frequency integration, formula corrections, or
Phase 7 library expansion. Those require later, separately activated slices.
