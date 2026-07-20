# Unified Node Library And Future Growth

- Captured: 2026-07-17 14:18
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-17-1418-channel-first-node-system.md`
- Type: update
- Topic: node-library-unification
- Verification: partially-verified against the current layer registry and Node Math Rewrite contracts

## User Direction

The old layer library came from an earlier browser-based image editor and does
not need to keep its current categories, structure, or legacy mindset.

Stack should retain and expand its broad range of useful creative operations,
but all nodes should participate in one channel-aware, input-aware, honest
definition system. User-friendly presentation must not mislead users about the
math.

## Proposed Unification Principle

There should be one production node-definition contract, regardless of whether
the node began as a legacy layer, a primitive, a compound, a generator, a mask,
or a specialized stage.

Browser organization should be presentation metadata—families, tasks, tags,
search terms, favorites, complexity level—not a substitute for semantic or
execution classification.

For example, “Effects / Damage” may remain a useful search tag, but it should
not determine how a node's channels, alpha, color, spatial support, masks,
ports, or renderer are defined.

## Per-Node Migration Questions

Every legacy node eventually needs an explicit answer for:

- exact formula or named algorithm;
- friendly name and technical identity;
- Image, Channel, Value, mask/map, resource, and specialized ports;
- independent-channel versus channel-relationship behavior;
- default and selectable channel participation;
- alpha and compositing behavior;
- color/transfer/reference requirements or agnosticism;
- range, clamp, non-finite, and precision behavior;
- spatial extent, sampling, border, neighborhood, and full-frame requirements;
- mask application point and units;
- canonical primitive/compound representation or honest opaque boundary;
- optimized implementation and equivalence evidence;
- lifecycle/version policy; and
- reference, generated, GPU, and visual tests.

## Input Consistency Direction

Where mathematically meaningful, creative nodes should use a consistent input
language:

- main Image or Channel input;
- optional mask/map input;
- one-value controls that can later accept connected Values;
- optional per-pixel control channels where supported;
- explicit secondary images for blends, comparisons, or multi-image work; and
- advanced channel, color, alpha, sampling, and method controls only when they
  materially change the promise.

Not every slider should automatically become a connectable input. Promotion
should be deliberate and should preserve understandable defaults.

## Browser Organization Questions

- Should the primary browser be organized by user task, mathematical family,
  data shape, or a small hybrid?
- Should primitives and friendly compounds appear together or in selectable
  complexity views?
- How should search expose tags such as `per-channel`, `RGB relationship`,
  `neighborhood`, `scene-linear`, `mask`, `generator`, and `specialized`?
- Should experimental, incomplete, or provider-dependent nodes appear in the
  main browser?
- Can several friendly nodes refer to the same primitive or compound
  definition with different presets, or would that obscure identity?

## Migration Strategy To Discuss Later

1. Select a small representative slice rather than migrating all 55 legacy
   descriptors at once.
2. Include one pointwise channel-safe node, one RGB-relationship node, one
   neighborhood node, one spatial node, and one specialized/opaque node.
3. Write their exact definitions and decomposition classes.
4. Prove graph UI, channel participation, masks, color/alpha diagnostics,
   planning, and tests end to end.
5. Use the result as the migration template for later library expansion.

## Related Docs

- `2026-07-17-compound-nodes-and-decomposition.md`
- `2026-07-17-graph-rules-and-visual-feedback.md`
- `2026-07-17-operation-scope-and-performance.md`
- `../../phase-roadmap.md` — Phase 7
- `../../decision-register.md` — NMR-002, NMR-016, NMR-120, NMR-121
