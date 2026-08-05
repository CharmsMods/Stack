# Unified Node Library And Future Growth

- Started: 2026-07-17
- Consolidated: 2026-07-20
- Authority: `../accepted-product-direction.md`
- Verification: current layer registry and Node Math definition contracts inspected on 2026-07-20

This file owns the migration from legacy layer organization to one expandable,
channel-aware node-definition system.

## One Definition Contract

Every primitive, compound, migrated legacy operation, generator, and
specialized stage uses one versioned contract for:

- exact formula, named algorithm, or honest staged description;
- stable identity, ports, parameters, defaults, and serialization;
- Value, Channel, Image, Data, and Specialized inputs/outputs;
- component presence and channel-relationship requirements;
- role, units/range, color, transfer, reference, and alpha propagation;
- numeric domain, clamp/wrap/non-finite, and precision behavior;
- extent, sampling, border, neighborhood, and full-frame policy;
- Mask and per-pixel parameter formula position;
- execution capability and failure behavior;
- compound/decomposition class and optimized equivalence;
- lifecycle/version policy; and
- reference, generated, GPU, persistence, and visual evidence.

The node's old category or implementation path cannot substitute for this
contract.

## Consistent Inputs

Where mathematically meaningful, nodes use:

- main Image or Channel input;
- optional Mask input;
- Value controls;
- declared `Value/Channel` per-pixel controls;
- explicit secondary Images; and
- advanced component, color, alpha, sampling, or method controls only when they
  materially change the promise.

Not every slider becomes a connectable input. Promotion must preserve an exact,
understandable formula.

## Browser Organization

The primary browser is task-oriented and shows a curated mix of friendly
compounds and useful primitives. Technical tags support search and inspection:

- `per-channel`;
- `RGB relationship`;
- `neighborhood`;
- `scene-linear`;
- `mask`;
- `generator`; and
- `specialized`.

Legacy groups such as Effects/Damage may remain aliases or tags, not semantic
or execution architecture. Experimental or provider-dependent nodes must state
availability and lifecycle clearly.

## Migration Slice

Do not migrate the full legacy registry at once. The first Phase 7 selection
contains:

1. one pointwise Channel-safe operation;
2. one RGB-relationship operation;
3. one neighborhood operation;
4. one geometry operation; and
5. one specialized/opaque operation.

Each proves the shared definition, Channel/Image UI, alpha/color diagnostics,
planning, compound class, and tests. The result becomes the template for later
migration.

## Current Code Boundary

The unified definition registry and Node Math contracts provide much of the
foundation, but the legacy layer library still carries old categories and
incomplete semantic policies. Phase 7 is inactive. R4 in
`../work-still-to-define.md` owns the first public selection after C1–C6 stabilize.

## Related Authority

- `../../04-implementation/roadmap-and-phase-gates.md`
- `../../01-start-here/decision-log.md` — NMR-002, NMR-016, NMR-120, NMR-121
- `compound-nodes-and-unpacking.md`
- `connections-errors-and-visual-feedback.md`
- `execution-and-performance.md`
