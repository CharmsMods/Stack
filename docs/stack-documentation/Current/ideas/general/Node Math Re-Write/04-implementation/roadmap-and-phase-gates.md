# Node Math Rewrite Phase Roadmap

- Status: Phases 0–6 complete; Phase 7 inactive; code-planning sequence only
- Updated: 2026-07-21 (current status and documentation scope)

This roadmap controls dependency order for code work. It does not require the
user to discuss, document, or research topics in this order. The later
channel-based C1–C8 contract program must be resolved before Phase 7
implementation selection, even though candidate research may begin earlier.

## Gate Rule

A phase is complete only when its decisions, deliverables, automated checks,
generated reference cases where useful, user visual-review status, limitations,
and exit decision are recorded. Code existing is not sufficient.

Each phase can and should be divided into small passes. Every pass must preserve
the phase's pixel-change policy and stop at its stated boundary.

## Phase Index

| Phase | Name | Pixel-change policy | Required outcome |
| --- | --- | --- | --- |
| 0 | Forward Verification Foundation | No product behavior change | A small generated-value/reference harness, current architecture recheck, and per-pass user/automated validation workflow for the intended rewrite. |
| 1 | Semantic And Node-Definition Contracts | No production output changes | Accepted minimal descriptors, stable forward definition schema, project generation policy, and testable propagation rules. |
| 2 | Semantic Spine, Diagnostics, And Technical Image Boundaries | Changes are explicit and user-controlled | Descriptor-bearing source→graph→view/output path, independent alpha state, permissive diagnostics, and no hidden color/view/output correction. |
| 3 | First-Class Values And Unified Definitions | Breaking pre-rewrite changes are allowed; conversions remain explicit | Scalar/vector/matrix/curve and selected typed resources stop masquerading as image/mask textures; node metadata and parameters share one definition system. |
| 4 | Semantic IR, Fusion, And Resource Planning | Optimized output must preserve authored order and match canonical semantics within tolerance | Small vetted pointwise graphs fuse, materialization becomes planned, and memory/pass behavior is bounded and inspectable. |
| 5 | Executable Compound Definitions | No silent instance updates after the rewritten format begins | Stable definition/instance/implementation model with nesting, promoted parameters, portability, forward versioning, Make Unique, and Unpack. |
| 6 | Region-Aware And Specialized Infrastructure | Match declared full-frame/proxy/tile contracts | ROI/halo/extent/sampling and selected reduction/neighborhood/specialized paths participate in one planner/diagnostic model. |
| 7 | Deliberate Public Library Expansion | Each correction/addition is versioned and tested | A selective primitive browser and tested high-level compounds replace ambiguous coverage without turning the research catalog into a checklist. |

## Dependency Flow

```text
Phase 0 forward verification
  -> Phase 1 contracts and stable identities
    -> Phase 2 semantic image spine and technical boundaries
      -> Phase 3 first-class values and unified definitions
        -> Phase 4 semantic IR/fusion/resources
          -> Phase 5 real compounds
            -> Phase 6 region-aware specialized stages
              -> Phase 7 public library expansion
```

Phase research may overlap when read-only. Production implementation does not
skip the dependency order.

## Phase 0 — Forward Verification Foundation

### Purpose

Create the smallest reusable test and review path needed to prove new node math,
descriptor behavior, authored order, and GPU execution as the rewrite advances.
This phase does not preserve historical output and does not create an old
image/project corpus.

### Entry Conditions

- An explicit request activates a bounded Phase 0 slice.
- `detailed-progress-log.md` names the active slice and stop rule.
- Relevant current code is re-inspected because the audit is dated.

### Required Deliverables

- A small deterministic harness that can feed generated numeric values or tiny
  in-memory images through selected reference math and GPU execution.
- Initial order-sensitive cases, including `Add -> Multiply` and
  `Multiply -> Add`, that prove the harness distinguishes authored order.
- A current-architecture delta showing which 2026-07-12 integration facts still
  match the code relevant to the first rewrite slice.
- Minimum observability needed by later work: exact-float inspection/readback,
  shader/GL failure visibility, target byte accounting, pass timing, or a
  documented bounded subset selected by the active slice.
- A test architecture decision for CPU reference math versus GPU output.
- A per-pass review pattern: automated definition tests, build validation, and
  user visual testing on real images chosen at the time of the change.

### Exit Gate

- Generated reference cases prove intended equations and operation order.
- The harness can compare a selected CPU/reference result with GPU output when
  that comparison is useful.
- No saved old project or historical-pixel guarantee has been introduced.
- Known nondeterminism and hardware dependence are explicit.

### Recommended First Slice

Implement one narrow generated-value reference path for Identity, Add, and
Multiply, including both possible Add/Multiply orders. Stop after the harness,
its build/test integration, and documentation are proven; do not use this slice
to redesign production nodes.

## Phase 1 — Semantic And Node-Definition Contracts

### Purpose

Turn the broad research proposals into one minimal accepted contract instead of
letting files 12, 14, and 17 become competing schemas.

### Required Decisions

- NMR-100 minimum semantic descriptor v1
- NMR-106 rewritten project generation and forward version resolution
- NMR-108 stable definition/port/parameter identity
- Initial portion of NMR-107 diagnostic policy
- Apply the NMR-125 CPU/GPU reference policy established in Phase 0 to the
  accepted definition-test contract

### Required Deliverables

- One canonical logical-value and image-descriptor schema with explicit
  mandatory, optional, derived, unknown, and deferred fields.
- One canonical node-definition/instance/implementation schema.
- Descriptor propagation rules for a small representative set: source,
  identity, arithmetic, exposure, mask, geometry, color transform, composite,
  reduction, output, and unknown external operator.
- New project-generation identity and exact-version resolution rules.
- Clear unsupported-pre-rewrite-project behavior and a forward serialization/
  version envelope.
- Diagnostic rule IDs and a connection/analysis/runtime staging contract.
- Property/reference test plan for definitions and propagation.

### Exit Gate

- Every current major path can be represented without inventing meaning.
- The first implementation schema is intentionally smaller than the maximal
  research type lists but can evolve without invalidating its identity model.
- No unresolved blocking choice is hidden in a field default.

## Phase 2 — Semantic Spine, Diagnostics, And Technical Image Boundaries

### Purpose

Make image meaning observable from source through graph, viewport, and output
without turning semantic knowledge into a connection restriction or hidden
correction. Users remain free to perform math and conversions in any order.

### Resolved Product Policy

- NMR-102 retains embedded profiles as non-converting source metadata and marks
  untagged ordinary sources as `Unknown` until explicit assignment/conversion.
- NMR-105 displays the connected graph output directly, provides no separate
  preview transform, and shows that output's current color state in the
  viewport footer.

### Required Deliverables

- Descriptor-bearing image edges and cache fingerprints.
- Known/unknown source-state propagation, retained embedded-profile metadata,
  and visible provenance without automatic pixel conversion.
- A non-mutating graph semantic analysis result with stable diagnostics.
- Source labels plus explicit, user-placed transfer/color conversion behavior;
  no required graph-wide working space.
- Explicit graph output, direct viewport display with no separate preview
  transform, and export boundaries with no hidden normalization or tone/gamut
  repair.
- Independent R/G/B/A access, explicit straight/premultiplied state,
  Premultiply/Unpremultiply, and mathematically defined compositing nodes.
- Profile/transform dependency identity and output metadata policy.
- Compact wire inspection plus the connected output's current color state in
  the viewport footer.
- Informational diagnostics that do not prevent deliberate unusual pipelines.

### Exit Gate

- Generated and user-reviewed cases have declared color, alpha, range, extent,
  provenance, view, and output behavior for the supported first contract.
- An encoded image can still connect to an Exposure-style multiplier; Stack
  explains the state and runs the authored math.
- Clipping or unusual appearance is allowed and reported rather than silently
  repaired.
- Tagged sources retain their profile label without conversion, and untagged
  ordinary sources remain visibly `Unknown` until an explicit user action.
- The main viewport has no independent preview transform and identifies the
  color state connected to its output.
- No diagnostic inserts a hidden pixel conversion.

### Completion Status — 2026-07-16

Phase 2 implementation and automated exit checks are complete. The live graph
now carries descriptors and stable semantic fingerprints; explicit Technical
Image nodes own assignment, transfer, linear color conversion, Exposure,
Premultiply, and Unpremultiply; Source Over exposes separate straight and
premultiplied formulas; the viewport remains direct; and PNG output writes only
metadata justified by the connected descriptor. See
`../06-completed-work/phase-02-image-semantics-and-output/completion-record-2026-07-16.md` for the exact scope, tests, and the
recorded pending native human visual confirmation. Phase 3 was not started by
that Phase 2 pass; it was completed later as recorded below.

## Phase 3 — First-Class Values And Unified Definitions

### Purpose

Stop routing unrelated values through coarse image and mask textures, and make
node metadata and parameters declarative enough for reliable composition and
future compounds.

### Required Deliverables

- Incremental Boolean, integer, scalar, vector, matrix, curve/LUT, coordinate,
  histogram/statistics, metadata, and specialized handle support as justified
  by selected vertical slices.
- Explicit distinction between uniform values and per-pixel fields.
- Typed broadcast, extraction, reduction, and unit rules.
- One definition registry for existing layer-backed and explicit catalog nodes.
- Declarative parameter identities, types, units, domains, UI hints,
  serialization, and animation policy.
- Temporary internal adapters for Mask-as-scalar paths only where needed to
  stage the rewrite; they are not a pre-rewrite saved-project promise.

### Exit Gate

- A selected vertical slice no longer overloads Mask/Image for its scalar,
  vector, curve, or analysis values.
- Rewritten nodes receive stable forward saved identities.
- Definition validation and serialization tests cover missing/unknown values.

### Completion Status — 2026-07-16

Phase 3 implementation and automated exit checks are complete. The canonical
value envelope distinguishes Known, Unknown, Missing, and Failure values;
uniform values, per-pixel fields, structured resources, and specialized
handles; and explicit unit/broadcast/extraction/reduction rules. Channel
Split/Combine and scalar Average use `ScalarField`, while a uniform Scalar
Value can explicitly drive Exposure EV. Browser entries, static sockets,
layer animation declarations, declarative parameter metadata, and exact
forward saved identities resolve through one validated live registry. See
`../06-completed-work/phase-03-values-and-node-definitions/completion-record-2026-07-16.md`. Phase 4 IR, fusion, constant
folding, and resource planning were not started by that Phase 3 pass; they were
completed later as recorded below.

## Phase 4 — Semantic IR, Fusion, And Resource Planning

### Purpose

Make fine-grained authoring affordable and distinguish semantic nodes from
physical passes.

### Required Decision

- NMR-109 first pointwise IR scope and numerical policy

### Required Deliverables

- Typed expression IR for a deliberately small primitive set.
- Lowering with authored-node/port source maps.
- Constant folding, dead expression removal, safe common subexpressions, and
  compatible pointwise fusion.
- Exact preservation of authored dependency and operation order; order-changing
  rewrites are prohibited unless equivalence for the exact definitions is
  proved and tested.
- Explicit fusion/materialization barriers and on-demand debug previews.
- Generated-program limits, compile cache, deterministic fingerprints, and
  failure diagnostics.
- Transient target reuse and a byte-budgeted persistent cache policy.
- Execution inspection for fused groups, passes, formats, bytes, and timing.

### Exit Gate

- CPU reference, unfused GPU, and fused GPU results agree within recorded
  tolerance across generated domain/range/alpha cases.
- `Brightness -> Contrast` and `Contrast -> Brightness`, or equivalent
  noncommutative examples, remain observably distinct after fusion.
- Representative pointwise chains reduce passes and memory measurably.
- Failures point back to authored nodes after fusion.

### Completion Status — 2026-07-16

Phase 4 implementation and automated exit checks are complete under NMR-109.
Stack now lowers a deliberately small, typed, single-input RGBA pointwise
subset into ordered generated GLSL without changing the user's dependency or
operand sequence. Conservative barriers retain ordinary execution for fan-out,
requested intermediate results, unsupported math, masks, scalar fields,
multi-image work, specialized nodes, and limit/failure cases. Program caching,
transient target reuse, persistent byte budgeting, and execution inspection
are live. CPU reference, ordinary GPU, and fused GPU checks pass within the
recorded RGBA16F tolerance, both Add/Multiply orders remain distinct, and the
representative two-node chain uses one pass/materialization instead of two.
See `../06-completed-work/phase-04-math-execution/completion-record-2026-07-16.md`. Phase 5 was not started.

## Phase 5 — Executable Compound Definitions

### Purpose

Make high-level tools real, reusable versioned interfaces rather than visual
frames or copied presets.

### Required Decision

- NMR-110 compound storage and portability model

### Required Deliverables

- Stable definition, instance, port, and parameter IDs.
- Graph-defined, optimized-equivalent, and opaque-specialized implementation
  classifications.
- Nonrecursive nesting, promoted controls, definition editing, Make Unique,
  Unpack, and deliberate update behavior.
- Exact dependency resolution, project embedding/reference policy, unresolved
  instance state, and forward version/update behavior.
- Canonical compound versus optimized implementation equivalence tests.
- RAW nodes are opaque specialized operators by default and are not candidates
  for automatic decomposition or Unpack during this program.

### Exit Gate

- At least one transparent pointwise compound and one optimized-equivalent
  compound survive save/load, copy, nesting, version update, missing dependency,
  Make Unique, and Unpack tests.
- Visual groups remain unchanged as a separate organizational feature.

### Completion Status — 2026-07-16

Phase 5 implementation and automated exit checks are complete under NMR-110.
Stack now stores exact embedded compound definitions and instances with stable
typed interfaces, deliberate versions, copied transitive dependency closure,
nonrecursive nesting, preserved unresolved shells, Make Unique, Unpack,
explicit update, and supported create-from-selection behavior. Resolved graph
compounds expand into a temporary execution graph without changing the saved
authored graph. The transparent `Add -> Multiply` and optimized-equivalent
`Exposure -> Premultiply` references retain exact internal order; canonical
CPU, materialized GPU, and fused GPU evidence passes within the recorded
`2.5e-3` tolerance. RAW definitions remain opaque specialized, and visual
groups remain layout-only. See
`../06-completed-work/phase-05-compound-nodes-and-connections/compound-nodes-completion-record-2026-07-16.md`. Phase 6 was not started.

### Corrective Phase 5B Status — 2026-07-16

Phase 5B corrected a gap in the original exit evidence: the first live
validator started from an expanded renderer graph and therefore did not test
authored compound output traversal. Authored traversal now resolves the exact
public output and canonical contributing inputs, reference-canvas lookup uses
the same dependency path, and unresolved definitions retain an explicit render
diagnostic. The live validator builds the actual two-compound chain and requires
a nonzero output matching its unpacked canonical graph.

NMR-133 also establishes normalized socket metadata, concise node pin labels,
0.7-second shared detail cards, progressive advanced-socket discovery, and
persisted adaptive/floating-or-break-line wire text. See
`../03-technical-contracts/graph-connections/compound-output-and-connection-ui-contract.md` and
`../06-completed-work/phase-05-compound-nodes-and-connections/connection-ui-completion-record-2026-07-16.md`. Phase 6 was not started.

## Phase 6 — Region-Aware And Specialized Infrastructure

### Purpose

Bring operations that cannot fuse pointwise into a correct shared planning
contract without pretending every algorithm is the same kind of node.

### Required Deliverables

- Explicit full/data windows, origin, pixel aspect, render scale, and extent
  propagation.
- ROI and halo mapping with named sampling, reconstruction, and border rules.
- Selected reusable neighborhood/kernel, reduction, collection, or pyramid
  infrastructure justified by vertical slices.
- Typed integration boundaries for RAW, frequency, scopes, CPU/external work,
  preview, and export.
- Tile/proxy/full-frame equivalence and cancellation behavior.

### Exit Gate

- Selected pointwise, geometry, neighborhood, reduction, and specialized nodes
  use the same semantic/diagnostic/planning framework.
- Tiled and full-frame generated cases agree within declared tolerance.

### Phase 6A Completion Status — 2026-07-17

At the Phase 6A checkpoint, Phase 6 had started but was not complete. The
bounded Phase 6A slice established
the shared finite-region, full/data-window, origin, render-scale, ROI/halo, and
cancellation contracts under NMR-135. The existing Gaussian/Box neighborhood
path now receives planner-derived halo, and the live Gaussian proof matched
full-frame and tiled output exactly (`max difference 0`, tolerance `2.5e-3`).

This satisfied the first neighborhood-planning vertical slice only. The
reusable-reduction gap was subsequently addressed by Phase 6B, and the
geometry/specialized gaps were subsequently closed by Phase 6C. See
`../03-technical-contracts/regions-reductions-and-specialized-processing/image-region-and-tiling-contract-v1.md` and
`../06-completed-work/phase-06-regions-reductions-and-specialized-processing/regions-and-tiling-completion-record-2026-07-17.md`.

### Phase 6B Completion Status — 2026-07-17

Phase 6B completed only the NMR-136 reusable reduction slice. It adds an
exact `ScalarField -> Field Mean -> Scalar` node, full-frame reduction
scheduling/cache behavior, and a live proof that its value can drive Exposure
EV. The authored graph measured 32 samples at exactly `0.5`, reused its
persistent scalar cache, and matched a constant-EV reference with maximum
difference `0`.

At that checkpoint, this closed the selected reusable-reduction gap but not the
Phase 6 exit gate. Phase 6C subsequently closed the selected geometry and
specialized-stage gaps. Other reductions and Phase 7 remain inactive.
Authority:
`../03-technical-contracts/regions-reductions-and-specialized-processing/field-mean-reduction-contract-v1.md` and
`../06-completed-work/phase-06-regions-reductions-and-specialized-processing/field-mean-completion-record-2026-07-17.md`.

### Phase 6C And Phase 6 Completion Status — 2026-07-17

Phase 6C implements NMR-137 and NMR-138. The public `Reformat` node is Stack's
first true extent-changing operation: it declares a finite width/height,
inverse pixel-center mapping, Nearest or Linear reconstruction, Clamp border,
independent RGBA sampling, and descriptor-preserving Sample/Resample planning.
Mismatched image extents now fail planning before execution and require an
explicit Reformat instead of receiving a hidden alignment.

RAW, RAW neural/external, multi-frame, FFT, inverse FFT, retained-spectrum,
scope, preview, and export work now have typed region, scale, cancellation, and
failure boundaries. The authored 4x3-to-7x5 Reformat graph matched its CPU
reference within `0.000351787`; the authored FFT-to-inverse-FFT graph matched
within `0.000451922`. Scope, preview, and export record non-mutating consumer
boundaries.

Together with the Phase 6A exact tiled/full-frame Gaussian result and Phase 6B
Field Mean result, the selected pointwise, geometry, neighborhood, reduction,
and specialized examples now share the semantic/diagnostic/planning framework.
The Phase 6 exit gate is complete. Phase 7 has not started. Authority:
`../03-technical-contracts/regions-reductions-and-specialized-processing/reformat-and-specialized-processing-contract-v1.md` and
`../06-completed-work/phase-06-regions-reductions-and-specialized-processing/reformat-and-specialized-processing-completion-record-2026-07-17.md`.

## Phase 7 — Deliberate Public Library Expansion

### Purpose

Publish the useful outcome: a factual, discoverable technical node set and
polished high-level tools whose behavior is no longer accidental.

### Required Deliverables

- Accepted browser families, aliases, tags, icons, accessibility, and density.
- A product-selected primitive set; not every research operation.
- Versioned corrections or renames for misleading current nodes.
- Tested high-level compounds with concise ordinary UI and technical details.
- For every shipped definition: formula/algorithm, types, domain/range,
  descriptor effects, execution class, reference tests, implementation mapping,
  and forward version notes.

### Exit Gate

- The public library is smaller than the encyclopedia, searchable, and useful.
- Each published primitive is semantically defined and operationally affordable.
- Each high-level node is honestly classified and inspectable at the supported
  level.
