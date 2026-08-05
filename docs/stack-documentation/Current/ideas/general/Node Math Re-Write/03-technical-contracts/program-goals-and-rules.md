# Node Math Rewrite Program Contract

- Status: direction contract; Phases 0 through 6 complete; no active slice
- Established: 2026-07-15
- Dated baseline: Stack audit dated 2026-07-12

This contract governs implementation behavior and proof requirements. It does
not restrict plain-language design discussion, documentation organization, or
research. The accepted channel-based product direction supplements it for work
that was designed after Phases 0–6.

## Mission

Make every Stack node definitive enough that a user, the graph validator, the
renderer, and a test all agree on what the node promises.
Retain a polished high-level editing experience while allowing technical users
to inspect and compose the lower-level mathematics where that representation is
truthful.

The program succeeds by separating meaning from scheduling, not by replacing
all high-level tools with hundreds of visible arithmetic nodes.

## Product Model

Stack should support two user-facing levels:

1. **Explicit primitives** with precise, testable behavior and declared input
   and output meaning.
2. **Convenience or compound nodes** with focused controls, documented
   decompositions, and an honest classification as graph-defined,
   optimized-equivalent, or opaque specialized work.

Those levels compile through three separate representations:

1. **Authored graph:** stable user intent, layout, node instances, visible
   conversions, compound boundaries, and project serialization.
2. **Semantic IR:** resolved definitions, typed operations, descriptor
   propagation, validation, compound expansion, and source mapping.
3. **Physical execution plan:** shaders, passes, formats, materialization,
   tiling/ROI, cache points, CPU or external work, and synchronization.

The authored graph promises results. It does not promise one draw call or one
full-size texture per visible node.

## Minimum Node-Definition Contract

A production node definition is incomplete until it owns all applicable fields
below. `node-and-data/node-data-and-definition-contract-v1.md` is the accepted exact v1 schema.

| Concern | Required contract |
| --- | --- |
| Identity | Stable definition ID, namespace, semantic version, and implementation/content identity. |
| Meaning | Exact formula, named algorithm, or staged semantic description; friendly labels are not sufficient. |
| Ports | Stable port IDs, logical types, arity, optionality, broadcast/overload rules, and required units. |
| Parameters | Stable parameter IDs, type, units, default, validity domain, UI range/widget hint, and serialization behavior. |
| Input state | Agnostic, informative, or structurally required information for color, transfer, reference state, alpha, range, extent, sampling, precision, metadata, or other relevant descriptors. Semantic mismatch normally informs rather than blocks. |
| Output state | Which descriptor fields are preserved, transformed, consumed, generated, invalidated, or intentionally dropped. |
| Numerical policy | Domain, range, clamp/wrap/normalize behavior, zero and non-finite handling, and precision requirements. |
| Alpha/composite policy | Straight/premultiplied requirements, coverage behavior, and whether alpha is preserved, changed, or absent. |
| Spatial policy | Extent/data-window behavior, coordinate convention, reconstruction, border mode, and ROI/halo rule where relevant. |
| Capability class | Pointwise, sample/resample, neighborhood, reduction, multipass/iterative, or specialized/external. This informs lowering but is not the semantic identity. |
| Diagnostics | Structural hard errors, semantic information/warnings, runtime faults, and suggested explicit repairs. A semantic mismatch alone does not block numerically valid graph math. |
| Implementations | Canonical graph/expression and any optimized backends, target constraints, equivalence tolerance, and source mapping. |
| Evidence | Reference test IDs, generated numerical/property cases, CPU/GPU comparisons where useful, user visual-review status, performance limits, and known limitations. |
| Forward lifecycle | Version rules for definitions created by the rewrite, missing-definition behavior, and whether a future correction changes promised output. Pre-rewrite compatibility is not required. |
| Presentation | Technical name, friendly aliases, browser family/tags, documentation, accessible icon/text treatment, and inspectability level. |

Definition, instance, and implementation are different objects. A definition
owns the stable promise. An instance owns connections and overrides. An
implementation satisfies the promise on a target backend.

## Program Invariants

1. **Meaning and storage remain separate.** Reusing `RGBA16F` does not make two
   images semantically equivalent.
2. **Meaning and schedule remain separate.** Fusion, caching, tiling, or
   materialization may change without changing the saved graph's promised
   result beyond declared tolerance.
3. **Authored order is exact.** Lowering and fusion preserve the graph's
   dependency and operation order. Reordering is permitted only when a
   mathematical equivalence has been proved for the exact definitions and does
   not change the authored result.
4. **No invisible repair.** Validation can report and suggest a conversion; a
   conversion that changes pixels appears as an explicit graph or output stage.
5. **The graph remains permissive.** Numerically valid operations remain
   connectable even when their color, alpha, range, or display meaning is
   unusual. Semantic state is information, not a hidden restriction.
6. **No forced working color space.** Color state belongs to values and wires.
   Users can insert conversions anywhere and can deliberately perform generic
   numeric math in any declared or unknown state.
7. **Independent channels remain available.** R, G, B, and A can be inspected
   and controlled separately. Straight and premultiplied alpha are supported
   explicitly rather than imposing one association everywhere.
8. **Unknown is first-class.** Unknown color, alpha, range, extent, or
   provenance is not silently promoted to a known state.
9. **Source labeling is descriptive.** An embedded color profile is retained
   as metadata without changing source pixels. An untagged ordinary source is
   `Unknown` until the user explicitly assigns a color meaning or converts it.
10. **The viewport is direct.** The main viewport displays the connected graph
    output without a separate preview transform. Its footer reports the
    connected output's current color state; any appearance-changing transform
    belongs in the graph.
11. **Diagnostics are typed.** A failed result, missing image, empty image, and
   valid transparent/black image are distinct states.
12. **High-level inspectability is honest.** Specialized RAW, FFT, denoise,
   profile, or model work is not represented as an editable primitive graph
   when that graph would omit essential state or execution.
    RAW nodes remain nondeconstructible by default and are reconsidered only
    case by case after the main rewrite.
13. **Optimizations require evidence.** Fused and specialized implementations
   must match their canonical semantics within recorded tolerances.
14. **Saved interfaces are stable from the rewrite forward.** Connections
   target stable definition and
   port identities; names are labels, not persistence keys.
15. **Tests prove intended behavior, not historical behavior.** Automated math
    checks, generated in-memory cases, performance evidence, and user visual
    review are added as the rewrite advances. No old image/project corpus is
    required.
16. **Uniforms, fields, and resources are different values.** A single number,
    a per-pixel scalar field, a semantic mask, a curve/statistics resource, and
    a specialized handle are not interchangeable merely because an existing
    renderer can store several of them in textures. Broadcast, extraction,
    reduction, and unit changes are explicit typed operations.

## Forward-Only Project Boundary

Compatibility with project files created before this rewrite is explicitly out
of scope. The user will retain the pre-rewrite executable for those projects.
The rewritten executable may introduce a breaking project/schema generation and
should fail clearly when asked to open an unsupported older project rather than
guessing or silently degrading it.

Once the rewritten system begins creating projects, its definitions, ports,
parameters, compounds, and project generation should be stable and versioned
for future work. That forward lifecycle is separate from preserving the current
pre-rewrite format.

## Start Line

Any new implementation slice begins only when
`../04-implementation/detailed-progress-log.md` names that exact slice as
active. Phase 0 is already complete; its original start condition is preserved
in the completed-work record. Research completion alone does not authorize
source edits, but research and documentation work do not require code-slice
activation.

## Finish Line

This program is complete only when all of the following are true:

- project-facing nodes resolve through stable, versioned definitions;
- graph values and image edges carry enough type and semantic information for
  their operations to be interpreted and validated;
- the connected graph-output state, direct viewport display boundary, and
  output encoding are explicit;
- alpha/compositing and extent/sampling behavior are declared and tested;
- a semantic IR and physical planner can fuse or materialize operations without
  changing their promised result;
- real compound definitions have stable interfaces, versions, dependencies,
  forward update rules, Make Unique, and Unpack behavior;
- the selected public primitive library is efficient, documented, searchable,
  and backed by reference tests;
- direct viewport display and export obey declared quality/scale/color/alpha
  contracts; and
- completed phases have build, automated math, generated-case, performance,
  and user/native UI evidence appropriate to their risk.

Completion does not require every operation in the research catalogs to become
a public node. Those catalogs are an encyclopedia, not a release checklist.

## Stop Conditions

Stop a pass and return to documentation when:

- a blocking product decision in `../01-start-here/decision-log.md` or the
  channel-system work queue is still open;
- a proposed schema cannot represent a required current path without guessing;
- a semantic claim lacks a reference test strategy;
- a fused or specialized implementation has no equivalence oracle;
- a fusion or optimization changes authored operation order or output;
- the next change belongs to a later phase; or
- implementation evidence contradicts current code/tests, an adopted contract,
  or dated evidence that must be reconciled explicitly.
