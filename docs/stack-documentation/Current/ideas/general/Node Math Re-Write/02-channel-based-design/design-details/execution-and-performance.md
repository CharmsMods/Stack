# Operation Scope And Performance

- Started: 2026-07-17
- Consolidated: 2026-07-20
- Authority: `../accepted-product-direction.md`
- Verification: current IR, region, reduction, geometry, and specialized planning contracts inspected on 2026-07-20

This file explains why Stack needs precise execution capabilities without
turning them into a complicated user-facing taxonomy.

## Capability Classes

| Capability | Dependency | Execution consequence |
| --- | --- | --- |
| Pointwise | Same coordinate only | Can often fuse; no tile halo |
| Neighborhood | Nearby samples | Requires support radius, border rule, and tile halo |
| Geometry / Resample | Remapped coordinates | Requires extent, sampling, and reconstruction policy |
| Reduction | Many pixels to one value | Full-region aggregation and fusion boundary |
| Global transform | Whole domain | Specialized storage and synchronization |
| Multi-image | Several images/frames | Alignment, extent, availability, and cache rules |
| Specialized / External | Dedicated domain/provider | Explicit capability, cancellation, resource, and failure boundary |

These are planner facts, not artistic categories. They make fusion, tiling,
caching, preview scale, cancellation, progress, and failure behavior correct.
Without them Stack must run conservatively or risk incorrect results.

## User-Facing Language

Normal UI usually needs only:

- works independently on each pixel/channel;
- uses surrounding image area;
- uses the complete image or several images; or
- specialized operation.

Detailed execution information belongs in inspection and performance tools.
Browser categories remain independent from capability class.

## Value Broadcast

When a port declares `Value/Channel`, a Value is reused at every pixel. The
backend may use a uniform or equivalent constant and avoid allocating, filling,
and sampling a full-resolution texture. The graph promises equivalent values,
not a particular optimization or speedup.

This differs from a standalone Constant Channel. A Constant Channel has a real
spatial extent and reusable Channel output. The opaque-Alpha workflow is a
valid use: users may branch or process it, while the planner still represents
it lazily until materialization is necessary.

## Required Definition Facts

Every operation declares, where applicable:

- dependency class;
- input-to-output region mapping;
- neighborhood support or full-frame requirement;
- extent, origin, sampling, reconstruction, and border policy;
- fusion and materialization eligibility;
- preview/scale policy;
- cancellation granularity;
- CPU, GPU, multipass, or external capability;
- deterministic, cacheable, or stateful behavior; and
- typed failure/publication policy.

Unknown or unsupported optimization facts default to correct conservative
execution. A node may ship with correct full-frame behavior before optimized
tiling only when that limitation is explicit and tested.

## Channel Relationships And Fusion

Component-independent operations may lower over each present Channel. An
operation that uses RGB relationships must observe the complete related bundle
at one semantic boundary. Partial-Image presence, requested intermediate
outputs, descriptor changes, masks, unsupported precision, and extent changes
may force materialization even when adjacent math is pointwise.

Optimized preview/final or CPU/GPU implementations may differ in scheduling
only when they preserve one versioned node/compound promise within declared
tolerance.

## Current Code Boundary

Phase 4 provides a small pointwise fusion slice. Phase 6 defines region,
neighborhood, reduction, geometry, and representative specialized boundaries.
The public library still lacks general `Value/Channel` ports, Constant Channel,
partial-Image planning, and broad channel-aware fusion.

Exact remaining work is C5 in `../work-still-to-define.md`; broader selection waits
for R4.

## Related Authority

- `../../03-technical-contracts/execution-and-performance/pointwise-math-execution-contract-v1.md`
- `../../03-technical-contracts/regions-reductions-and-specialized-processing/image-region-and-tiling-contract-v1.md`
- `../../03-technical-contracts/regions-reductions-and-specialized-processing/field-mean-reduction-contract-v1.md`
- `../../03-technical-contracts/regions-reductions-and-specialized-processing/reformat-and-specialized-processing-contract-v1.md`
- `connections-errors-and-visual-feedback.md`
- `compound-nodes-and-unpacking.md`
