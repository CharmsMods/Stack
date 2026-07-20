# Phase 4 Completion Evidence

- Phase: 4 — Semantic IR, Fusion, And Resource Planning
- Completed: 2026-07-16
- Status: implementation and automated gates complete; native execution-
  inspection presentation recorded as a follow-up
- Compatibility policy: forward-only; no old-project migration, fixture
  corpus, or historical-output emulation was added
- Next phase status: Phase 5 is pending; NMR-110 and executable compounds were
  not started

## Completion Decision

Phase 4 is complete. Stack now distinguishes the user's semantic node order
from the number of GPU passes required to execute a vetted pointwise chain.
Eligible operations can be translated into one typed internal expression and
one generated shader pass while retaining the exact dependency and operand
sequence authored on the graph.

This is a deliberately bounded compiler and renderer slice. It does not add a
large primitive library, executable compound nodes, a general multi-image
compiler, reductions, ROI/halo planning, or formula corrections. Unsupported
or ambiguous work remains on the existing ordinary execution path.

## Typed Pointwise IR

`src/NodeMath/PointwiseIR.*` owns the Phase 4 v1 intermediate representation,
optimizer, CPU evaluator, GLSL generator, physical-plan model, fingerprints,
and pure persistent-cache eviction selection.

The IR is SSA-style: every instruction produces one stable value that later
instructions reference. Values are either a uniform four-component constant
or an RGBA float field. The v1 program accepts one sampled RGBA field and these
ordered operations:

- identity;
- add, subtract, multiply, minimum, maximum, and absolute difference;
- explicit clamp;
- Exposure EV, preserving alpha;
- premultiply; and
- guarded unpremultiply using the accepted `1e-6` rule.

Each instruction retains authored node, definition, input-port, and output-
port sources. Validation rejects invalid IDs, arity, dependency order, value
classes, roots, multiple sampled inputs, and non-finite uniforms. Constant-
fold overflow is diagnosed instead of becoming a generated uniform.

## Order-Preserving Optimization

The optimizer performs only the accepted safe transformations:

- remove instructions unreachable from the requested result;
- fold expressions whose inputs are all uniform constants;
- reuse structurally identical expressions with the same ordered operands;
  and
- compact the resulting program into deterministic dense value IDs.

It does not sort operands, reassociate arithmetic, distribute expressions, or
combine constants across authored operations. Semantic fingerprints include
ordered operations, values, policies, and authored sources. Program-cache
fingerprints describe generated structure while constants remain uniforms, so
an ordinary parameter edit can reuse the compiled shader without losing the
new semantic identity.

## Live Lowering And Barriers

`src/Renderer/Internal/RenderPipelineGraphPointwiseFusion.cpp` lowers eligible
production graph chains and executes the generated program. The current live
subset is:

- Data Math Clamp, Add, Subtract, Multiply, Min, Max, and Difference when
  exactly one ordinary image input is connected and the other operand is the
  node's uniform constant; and
- Technical Image Assign operations as identity, Exposure, Premultiply, and
  Unpremultiply.

Normal execution requires at least two eligible authored operations before it
uses fusion. The first policy materializes or falls back at fan-out, masks,
scalar-field inputs, requested intermediate outputs, unsupported operations,
multiple sampled images, RAW/frequency/specialized nodes, semantic or extent
boundaries, 48 operations, or 64 KiB of generated source. Divide and Remap are
explicit barriers because their current formulas are not part of the accepted
v1 primitive definitions.

If lowering or generation fails, the renderer records the mapped authored
nodes and uses the existing unfused path. Generated lines retain their authored
sources; shader compilation and draw failures report the affected authored
group before fallback. Pixel NaN or infinity is not silently scrubbed, clamped,
or replaced.

## Resources And Inspection

Phase 4 adds these bounded runtime policies:

- generated pointwise programs use a 64-entry least-recently-used cache;
- graph image and mask caches share a 512 MiB soft byte budget and evict the
  least-recently-used unprotected owned texture;
- compatible multipass work reuses dimension-matched transient targets; and
- resize, clear, and destruction release the transient pool safely.

The Graph Performance popup now reports ordered fused groups, fused node and
avoided-pass counts, RGBA16F target bytes, optional CPU submit timing, program-
cache hits/misses, persistent bytes and budget, evictions, transient pool
allocations/reuse/bytes, semantic/program fingerprints, and source-mapped
failure information. Inspection timing is collected when that popup is
requested so normal graph presentation remains uncluttered.

## Automated Evidence

### Focused IR/compiler/planner checks

```powershell
cmake --build build --config Release --target StackNodeMathPhase4Tests
.\build\StackNodeMathPhase4Tests.exe
```

Result: passed, 35 checks. Coverage includes typed validation, semantic order,
CPU evaluation, constant folding, dead-expression removal, ordered common-
subexpression reuse, structural versus semantic fingerprints, finite-uniform
and fold-overflow failure, generated limits and authored sources, alpha guards,
materialization planning, byte estimates, and deterministic LRU eviction.

### Live production-renderer validation

```powershell
.\build\Stack.exe --validate-node-math-phase4
```

Result: passed in a hidden OpenGL 4.3 context. The validation exercises the
production graph snapshot, renderer, caches, generated shader, and readback.
It proves:

- CPU reference, ordinary unfused GPU, and fused GPU output agree within the
  recorded `2.5e-3` RGBA16F tolerance;
- `Add -> Multiply` and `Multiply -> Add` both fuse and remain observably
  different;
- a two-node representative chain becomes one generated pass, avoiding one
  pass and one persistent intermediate texture;
- the 4x1 validation graph reports 32 persistent bytes fused versus 64 bytes
  unfused;
- a parameter edit reuses the structural program cache;
- requesting the intermediate node materializes it instead of hiding it in a
  larger fusion group;
- Premultiply/Unpremultiply obey the selected alpha behavior;
- a non-finite uniform falls back and identifies its authored node; and
- repeated multipass Average work reuses its two transient targets.

### Registered Node Math tests

```powershell
ctest --test-dir build -C Release -L node-math --output-on-failure
```

Result: 7/7 passed:

- `StackNodeMathReference.Cpu`
- `StackNodeMathReference.Gpu`
- `StackNodeMathContract.Phase1`
- `StackNodeMathSemantic.Phase2`
- `StackNodeMathValues.Phase3`
- `StackNodeMathPointwise.Phase4`
- `StackNodeMathPointwise.LiveGpu`

### Wider graph and application gates

```powershell
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-layer-registry
cmake --build build --config Release --target Stack
.\build.cmd
```

Results: graph behavior passed, layer-registry validation passed, the live
Stack target linked, and the repository-preferred Windows build completed.

## Pixel And Compatibility Assessment

- Existing public formulas were not corrected or redefined in this phase.
- Compatible nodes still execute in their exact authored dependency and
  operand order.
- Fusion changes physical scheduling and intermediate rounding, so CPU,
  unfused GPU, and fused GPU are compared at the recorded float/RGBA16F
  tolerance rather than promised bit identity.
- No hidden clamp, normalization, color conversion, alpha conversion, or
  viewport/output repair was added.
- The old-project compatibility path remains the archived older executable;
  Phase 4 adds no migration or legacy fixture corpus.

## Known Limits And Follow-Up

- The live compiler accepts one sampled RGBA input. Two-image pointwise
  expressions remain materialized on the ordinary path.
- Fan-out always materializes under the conservative v1 policy; recompute
  versus materialize remains a later planning decision.
- Divide, Remap, scalar-field operands, reductions, curves/LUTs, matrices,
  neighborhood work, RAW, and frequency/specialized operators are barriers.
- Live targets remain RGBA16F. This phase does not select a broader internal
  precision strategy.
- Reported per-group timing is CPU submission time when inspection is enabled,
  not a synchronized GPU duration query.
- Protected live output/source textures may keep the soft persistent cache
  above budget when they cannot be evicted safely.
- Native human confirmation is still needed for Graph Performance popup
  readability with real projects and longer fused groups.

These limits bound Phase 4 and do not authorize Phase 5 or later work.

## Exit Decision And Stop

All Phase 4 required deliverables and automated exit conditions are satisfied:
the typed IR and lowering retain authored sources/order, safe optimizations and
explicit barriers are enforced, generated programs and resources are bounded,
inspection is live, CPU/unfused/fused results agree within the recorded
tolerance, operation order remains distinct, passes/memory are measurably
reduced, and failures point back to authored nodes.

Phase 4 is closed. Phase 5 remains pending and was not started. A separate
explicit pass and NMR-110 are required before implementing executable compound
definitions, promoted controls, Make Unique, Unpack, or compound versioning.
