# Operation Scope And Performance

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1632-channel-ui-confirmations-and-uniform-reuse.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-18-1801-output-scenarios-and-explicit-alpha.md`
- Type: question
- Topic: execution-capabilities
- Verification: partially-verified against current IR, region, reduction, and specialized planning contracts

## User Question

Does Stack gain real performance or reliability from knowing whether an
operation uses one pixel, nearby pixels, a complete image, several images, or a
specialized pipeline? Or is this complexity merely making Stack “smart” for
display purposes?

## Why The Distinction Has Real Execution Value

The capability does not define the artistic meaning of a node, but it tells the
planner what work is safe:

- **Pointwise:** adjacent compatible operations can be fused into one shader;
  tiles need no halo; output pixels can be recomputed independently.
- **Neighborhood:** tile requests need an expanded input halo; border behavior
  and support radius must be known; adjacent filters may require
  materialization or combined-support analysis.
- **Sample/resample or geometry:** output coordinates map to different input
  coordinates; extents and reconstruction filters matter.
- **Reduction:** a result depends on a complete population or declared region;
  it is a fusion and scheduling boundary and may require CPU/GPU aggregation.
- **Global transform:** FFT and similar work require the whole domain and
  specialized storage/synchronization.
- **Multi-image:** input alignment, matching extents, frame availability,
  caching, and failure behavior differ from a one-image operator.
- **Specialized/external:** RAW, neural, or provider-backed work needs explicit
  capability, cancellation, resource, and failure boundaries.

Without these facts, Stack must conservatively render full frames, retain more
intermediates, or risk incorrect tiled output. The distinctions therefore
support fusion, tiling, cache policy, preview scaling, cancellation, parallel
work, progress reporting, and correct failure behavior.

## Keep The User Language Simpler Than The Planner

The graph does not need seven permanent top-level categories or badges. A small
user-facing explanation could be:

- works on each channel/pixel independently;
- uses surrounding image area;
- uses the complete image or several images; or
- specialized operation.

The definition and planner can retain more precise internal capability fields.
Browser categories should remain independent from execution classification.

## Uniform Value Reuse Versus A Constant Texture

When an input explicitly accepts `Value/Channel`, a connected Value can be
reused for every pixel without first creating a Channel texture filled with
copies of that number. This is both a semantic broadcast rule and a likely
optimization:

- no full-resolution constant texture allocation;
- no pass that writes the repeated value into that texture;
- less intermediate memory and cache traffic; and
- a shader may receive the number as a uniform or equivalent backend constant.

The node definition promises that every pixel observes the same Value. It does
not promise a particular GPU representation or measured speedup; the planner
and backend remain free to choose an equivalent implementation. A materialized
constant Channel should be introduced only when a real operation needs a
standalone spatial resource rather than merely the same parameter at every
pixel.

### Concrete Constant Channel Workflow — 2026-07-18

The proposed visible opaque-alpha source is now a concrete reason to expose a
standalone Constant Channel. The graph value needs to behave as a real Channel
so users can branch it, pass it through compatible operations, or insert nodes
before Image Combine.

That does not require eager texture allocation. The planner may represent the
Channel as a constant Value plus resolved spatial extent, broadcast `1.0` in a
shader, and materialize a texture only at a boundary that genuinely needs
storage. The graph promise is per-location Channel behavior; the backend
representation remains an optimization detail.

The missing contract is extent resolution: a constant Channel that participates
in an Image needs dimensions and sampling identity even if it contains only one
unique numeric value.

## Minimum Mechanically Useful Capability Fields

- dependency class;
- input-to-output region mapping;
- finite neighborhood support or full-frame requirement;
- extent and sampling policy;
- fusion/materialization eligibility;
- scale/preview policy;
- cancellation granularity;
- CPU, GPU, multipass, or external capability;
- deterministic/cacheable/stateful behavior; and
- failure publication policy.

These fields should be factual planner inputs, not vague labels such as
“complex” or “advanced.”

## Questions For The Next Conversation

- Which capability facts must every node declare, and which may safely default
  to conservative full-frame execution?
- Should a node be publishable before it has optimized tiling support if its
  full-frame behavior is correct?
- Can the same canonical compound lower differently for preview, final render,
  CPU, and GPU while preserving its promise?
- How should Stack explain a full-frame or external performance boundary to a
  user without cluttering normal graphs?
- When can an image bundle operation be fused across independent channels, and
  when must channel relationships materialize together?

## Related Docs

- `2026-07-17-graph-rules-and-visual-feedback.md`
- `2026-07-17-compound-nodes-and-decomposition.md`
- `../../phase-4-ir-contract-v1.md`
- `../../phase-6-region-contract-v1.md`
- `../../phase-6b-reduction-contract-v1.md`
- `../../phase-6c-geometry-and-specialized-contract-v1.md`
