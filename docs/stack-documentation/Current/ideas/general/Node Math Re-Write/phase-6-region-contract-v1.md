# Phase 6 Region Contract v1

- Status: accepted and implemented for completed Phase 6A
- Accepted: 2026-07-17
- Scope: spatial descriptors, render requests, ROI/halo mapping, tile planning,
  and the existing Gaussian/Box Blur vertical slice

## Product Result

Phase 6A does not add a new node to the browser. It gives Stack one precise
language for describing which part of an image exists, which part is requested,
and which upstream pixels a stage must read. The first live proof is the
existing Gaussian Blur: tiled rendering must derive its own halo and match the
full-frame result without asking the user to guess a safe halo.

## Spatial Descriptor v2

Every known image-like spatial descriptor contains:

- a finite full window or an explicit Empty state;
- a finite data window contained by the full window;
- the integer origin carried by the window coordinates;
- an explicit raster origin convention (`BottomLeft` or `TopLeft`); and
- a finite, positive pixel aspect.

Stack's live OpenGL graph path is `BottomLeft`. File decoders may begin in a
different row order, but the descriptor records the convention after the
pixels enter the graph. Descriptor schema v1 data migrates to v2 as
`BottomLeft`, matching Stack's established graph upload convention.

Rectangles use signed integer coordinates and half-open bounds:
`[x, x + width) × [y, y + height)`. Empty and finite regions are different;
unknown/global planning requirements are not encoded as fake enormous
rectangles.

## Render Request

A render request declares:

- an output region;
- a channel span;
- a positive render scale in X and Y; and
- the quality identity supplied by its caller.

Render scale is evaluation state, not part of the authored image's native
extent. Cache/planning identities must distinguish different render scales.
Phase 6A's live ordinary tile path executes at `(1,1)`. The contract can
represent proxies now; integrating RAW proxies and other preview paths is a
later Phase 6 slice.

## Operation Mapping

- Pointwise: request the corresponding input region unchanged.
- Neighborhood: expand the output request by the declared support, then
  intersect the fetch with the input data window. Sampling outside that window
  follows the named border rule rather than pretending those pixels exist.
- Sample/resample: inverse-map the requested output bounds and add the named
  reconstruction support. The type is represented, but a live geometry
  vertical slice is not part of Phase 6A.
- Reduction: request the complete declared population. Reusable reduction
  execution remains Phase 6B or later under NMR-123.
- Specialized/global: declare a full-frame or unsupported boundary rather than
  being mislabeled tile-safe.

Pointwise and neighborhood mappings preserve the input full/data windows for
this slice. True Crop, Canvas, and transformed-bounds behavior is later work.

## Gaussian And Box Blur v1 Planning Contract

The live formula is not changed.

- Capability: bounded neighborhood.
- Sample domain: all four incoming RGBA components independently.
- Support: square radius `int(max(1, amount))` at full render scale.
- Gaussian weights: the existing normalized weight formula and square loop.
- Box weights: the existing normalized uniform square loop.
- Border: clamp to the nearest edge sample.
- Reconstruction/sample access: exact texel offsets from the current sampled
  input texture.
- Output extent: preserve full and data windows.
- Materialization: one existing RGBA16F pass; a connected graph mask remains a
  separate pointwise blend.
- Alpha: no association conversion or repair is inserted. Existing component
  math remains visible and descriptor diagnostics stay separate.

Pixel-radius parameters are defined in full-resolution pixels. At a uniform
proxy scale, future integrations must scale the effective radius/support by
that render scale. Phase 6A proves the scale-1 live path and tests the planning
rule at other scales without changing unrelated proxy rendering.

## Graph And Tile Planning

The renderer builds a locality plan from the reachable output graph. It records
each recognized pointwise or neighborhood stage, accumulates support along
dependency paths, and computes the maximum source halo required by a tile.
Two consecutive radius-3 neighborhood stages therefore require a six-pixel
source halo.

The Viewport Rendering `Tile Halo` preference becomes an extra minimum halo.
Correctness never depends on that preference: the effective halo is the larger
of the planner requirement and the user's extra amount.

A graph falls back to full-frame execution when a reachable stage has no
trusted region mapping, requires global state, changes extent without an
implemented mapping, or uses a specialized path not integrated by this slice.
Fallback is explicit in the plan reason; unsupported stages are not guessed.

## Cancellation And Publication

Cancellation is checked before every tile. A canceled or superseded plan:

- stops requesting further tiles;
- deletes already produced temporary shared tile textures;
- never marks an incomplete tile set complete; and
- never replaces the last complete viewport result with partial output.

This preserves the existing safe publication behavior while making it part of
the shared region-aware contract.

## Evidence And Tolerance

Phase 6A must cover:

- odd and nonzero-origin full/data windows;
- invalid window, origin, aspect, scale, channel, and region cases;
- pointwise identity mapping;
- Gaussian support at scale 1 and proxy scales;
- clamp-edge ROI mapping;
- accumulated support across multiple neighborhood stages;
- graph classification and exact effective tile halo;
- cancellation before and during tile iteration; and
- generated full-frame versus tiled live Gaussian output, including borders,
  within the existing RGBA16F absolute tolerance of `2.5e-3`.

## Deferred Boundary

Phase 6A does not add public kernel, structuring-element, sampler,
reconstruction, morphology, reduction, histogram, collection, pyramid,
coordinate-field, or geometry nodes. It does not change Gaussian/Box formulas,
alpha representation, color conversion, viewport transforms, RAW execution,
frequency execution, export, or the public library. Those require later
explicit Phase 6 slices and their own equivalence evidence.
