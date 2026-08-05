# Phase 4 Pointwise IR Contract v1

- Status: accepted implementation contract
- Accepted: 2026-07-16
- Decision: NMR-109
- Scope: Phase 4 only

## Product Result

The authored graph continues to define the exact sequence the user chose. For
the vetted pointwise subset below, Stack may translate a linear chain into one
typed internal expression and one generated GPU pass. This changes the
schedule, not the promised math. Asking for an intermediate node result makes
that node an explicit materialization boundary for that execution request.

## First IR Boundary

The v1 IR is SSA-style and supports one sampled RGBA float field plus uniform
scalar or four-component constants. Every instruction has a logical value
class, exact ordered operands, numerical policy, and authored node/port source
map.

The first operations are:

- identity;
- add, subtract, multiply, minimum, maximum, and absolute difference;
- explicit clamp;
- Exposure EV (`rgb * exp2(EV)`, alpha preserved);
- premultiply; and
- guarded unpremultiply using the Phase 2 `1e-6` rule: unsafe RGB becomes
  black while the independently controlled alpha channel is preserved.

This is an implementation subset, not the final public primitive library.
Data Math Divide is excluded because its current sign-losing guard is not an
accepted primitive definition. Remap, comparisons, conditionals, curves/LUTs,
matrices, reductions, multiple sampled inputs, and specialized work are also
outside v1 and form barriers.

## Numerical Policy

- Generated GLSL arithmetic is float32.
- Live graph targets remain RGBA16F in this phase.
- CPU reference evaluation uses double precision; GPU comparisons record a
  tolerance appropriate to the generated float program and target format.
- Uniform constants must be finite. A non-finite uniform fails lowering and
  identifies the authored source; the renderer falls back to the existing
  unfused path rather than changing pixels.
- Pixel NaN and infinity are not automatically clamped, replaced, or hidden.
- Clamp and unpremultiply guards occur only when explicitly required by the
  selected operation.

## Order And Safe Optimization

Dependency and operand order are immutable. The optimizer may:

- remove instructions unreachable from the requested result;
- fold expressions whose inputs are all uniform constants;
- reuse structurally identical expressions with the same ordered operands;
  and
- fuse compatible pointwise instructions into one generated pass.

It may not sort commutative operands, reassociate arithmetic, distribute,
combine constants across authored operations, or otherwise change rounding or
noncommutative order. `Add -> Multiply` and `Multiply -> Add` remain distinct
programs and results.

## Materialization And Limits

Fusion stops at:

- sources and requested outputs/previews;
- unsupported, multi-input, sampled, neighborhood, reduction, RAW, FFT, CPU,
  external, or iterative operators;
- explicit debug/materialization requests;
- fan-out under this first conservative policy;
- incompatible type, semantic, extent, or precision boundaries; or
- 48 operations or 64 KiB of generated shader source.

Normal live execution uses generated fusion only for two or more eligible
operations. A requested intermediate result may compile a smaller temporary
plan ending at that node without changing the saved graph.

## Identity, Failure, And Resources

- Semantic fingerprints include ordered operations, values, policies, and
  source identity.
- Compile-cache fingerprints describe generated program structure while
  constants remain uniforms, allowing parameter edits to reuse a program.
- Generated lines map back to authored node and port IDs.
- Lowering, limit, compile, or execution failure records the mapped authored
  nodes and falls back to the existing unfused evaluator.
- Generated programs use a 64-entry LRU cache.
- Temporary pointwise/multipass targets use a reusable dimension-matched pool.
- Persistent graph image/mask caches share a 512 MiB byte budget and evict the
  least-recently-used unprotected owned textures.

## Phase Boundary

This contract does not authorize executable compounds, general ROI/halo
planning, reductions, a broad public primitive catalog, or changes to existing
node formulas. Those remain Phase 5 or later work.
