# Node Math Rewrite Implementation Progress

- Updated: 2026-07-17
- Program status: Phase 6 complete — stopped before Phase 7; no active slice

## Current Checkpoint

```text
Current phase: Phase 6 — complete; Phase 7 is pending and inactive
Active implementation slice: none
Completed implementation slice: Phase 6A — spatial/ROI contract and Gaussian Blur tiled-equivalence path
Completed implementation slice: Phase 6B — exact Field Mean reduction and Scalar-to-Exposure live proof
Completed implementation slice: Phase 6C — extent-changing Reformat and typed specialized-stage/consumer boundaries
Current code baseline: commit 152ebe4d206f4f436a1d826bf750dc0ae8a7a886 on main with substantial pre-existing user changes; Phase 2 and Phase 3 owners and boundaries are recorded in their dated evidence files and `code-source-map.md`
Current ground-truth snapshot: audit dated 2026-07-12
Completed slice: Phase 0A generated 4x1 RGBA32F values; double-precision CPU reference; GPU Identity/Add/Multiply and both Add/Multiply orders; exact float readback; labeled GL/shader failures; target-byte accounting; synchronized pass timing
Exit decision: Phase 0 passed on 2026-07-15; evidence and limitations are recorded below and in 2026-07-15-phase-0-current-architecture-delta.md
Completed slice: Phase 1A compiled `phase-1-contract-v1.md` without wiring it into the live graph, renderer, persistence, project files, viewport, export, or UI
Exit decision: Phase 1 passed on 2026-07-16; evidence is recorded in `2026-07-16-phase-1-completion-evidence.md`
Completed slice: Phase 2A compiled and proved descriptor serialization, source-profile inspection, exact technical-image math, non-mutating semantic analysis, PNG metadata writing, and generated cases
Completed slice: Phase 2B integrated those contracts into the live graph, renderer, caches, persistence, viewport, PNG export, graph inspection, and node browser without entering Phase 3
Exit decision: Phase 2 implementation and automated gates passed on 2026-07-16; native human UI confirmation is recorded as a follow-up in `2026-07-16-phase-2-completion-evidence.md`
Completed slice: Phase 3A implemented the exact first-class value envelope, explicit uniform/field/resource/handle distinction, scalar-field channel slice, and Scalar Value → Exposure EV binding.
Completed slice: Phase 3B established one validated live definition/parameter registry for layer-backed and explicit catalog nodes, browser/static-socket metadata, exact identities, and graph-schema-5 save/load resolution.
Exit decision: Phase 3 automated gates passed on 2026-07-16; native human UI confirmation is recorded as a follow-up in `2026-07-16-phase-3-completion-evidence.md`.
Completed slice: Phase 4A established the NMR-109 typed pointwise IR, conservative live fusion, ordered source maps, generated-program limits/cache, transient reuse, persistent byte budgeting, execution inspection, and CPU/unfused-GPU/fused-GPU evidence.
Exit decision: Phase 4 automated gates passed on 2026-07-16; exact scope, measurements, commands, and native UI follow-up are recorded in `2026-07-16-phase-4-completion-evidence.md`.
Pixel result: compatible authored chains may execute in one RGBA16F pass instead of one pass per node. The operation and operand sequence is unchanged; no existing formula, hidden clamp, color conversion, or output repair was introduced.
Completed slice: Phase 5A established exact embedded compound definitions and instances, stable typed interfaces, project/copy closure, transparent and optimized-equivalent examples, nesting, unresolved shells, authoring/lifecycle actions, temporary execution expansion, RAW opacity, and Phase 5 UI.
Exit decision: Phase 5 automated gates passed on 2026-07-16; scope, commands, equivalence evidence, limitations, and native UI follow-up are recorded in `2026-07-16-phase-5-completion-evidence.md`.
Completed slice: Phase 5B repaired authored compound traversal/reference-source resolution, added explicit unresolved output diagnostics, normalized every socket presentation, generated the maintained socket catalog, and added pin labels, delayed detail cards, advanced-pin discovery, and persisted adaptive/break-line wire text.
Correction result: the original Phase 5 live validator bypassed authored compound traversal. The Phase 5B validator now builds the actual two-compound authored graph, requires a nonzero viewport texture before Unpack, and compares it with the unpacked canonical graph.
Exit decision: Phase 5B automated gates passed on 2026-07-16; scope, correction, commands, and visual follow-up are recorded in `2026-07-16-phase-5b-completion-evidence.md`.
Completed slice: Phase 5B-C rotates connection text with its wire, stabilizes target-anchored delayed cards, removes the on-node Connections dropdown and media cursor tooltip, adds appearance version 9 text/node sizing controls, and requires explicit image-to-mask extraction instead of auto-spawning Luminance Mask.
Exit decision: Phase 5B-C focused graph tests, live Phase 5 validation, Node Math CTests, registry validation, and the preferred Windows build passed on 2026-07-16; scope and native visual follow-up are recorded in `2026-07-16-phase-5b-c-corrective-completion-evidence.md`.
Completed slice: Phase 6A standardized finite regions, full/data windows, origin, pixel aspect, render scale, ROI/halo mapping, named clamp sampling, planner-derived tile halo, cancellation-safe tile publication, and live tiled/full-frame equivalence for the existing Gaussian Blur path.
Exit decision: Phase 6A automated gates and the preferred Windows build passed on 2026-07-17; scope, exact behavior, commands, equivalence evidence, and limitations are recorded in `2026-07-17-phase-6a-completion-evidence.md`.
Completed contract: NMR-136 and `phase-6b-reduction-contract-v1.md` own one exact full-frame `ScalarField -> Field Mean -> Scalar` path, its runtime cache/failure diagnostics, and a live Exposure-EV proof. Completion evidence is `2026-07-17-phase-6b-completion-evidence.md`.
Completed contract: NMR-137/NMR-138 and `phase-6c-geometry-and-specialized-contract-v1.md` own the exact Reformat vertical slice, extent mismatch failure, and typed RAW/frequency/scope/preview/export planning boundaries. Completion evidence is `2026-07-17-phase-6c-and-phase-6-completion-evidence.md`.
Exit decision: Phase 6 passed on 2026-07-17. The selected pointwise, geometry, neighborhood, reduction, and specialized examples share the semantic/diagnostic/planning framework, and tiled/full-frame Gaussian equivalence remains exact.
Stop boundary: every other reduction, automatic luminance/exposure policy, ROI or masks for reductions, GPU reduction trees, public kernel/morphology/sampler values, additional geometry and reconstruction, pyramids, Phase 7 public-library expansion, Divide correction, curves/LUT resources, and unrelated renderer rewrites remain unauthorized.
```

## Phase Status

| Phase | Status | Current result | Next gate |
| --- | --- | --- | --- |
| Direction setup | Complete | Entry point, contract, decisions, roadmap, tracker, research routing, and source map created. | Complete. |
| Phase 0 — Forward Verification | Complete | Generated CPU/GPU reference harness, CTest registration, architecture recheck, observability subset, review pattern, evidence, and limitations recorded. | Complete. |
| Phase 1 — Contracts | Complete | Canonical v1 contract, isolated compiled implementation, canonical identities, propagation, diagnostics, forward envelope/resolver, tests, and exit evidence completed on 2026-07-16. | Complete. |
| Phase 2 — Semantic spine | Complete — 2026-07-16 | Descriptor-bearing live image spine, explicit technical image/alpha math, permissive diagnostics, source metadata retention, direct viewport state, and explicit PNG metadata policy are implemented and tested. Native human UI confirmation remains a recorded follow-up. | Complete; see `2026-07-16-phase-2-completion-evidence.md`. |
| Phase 3 — First-class values | Complete — 2026-07-16 | Exact first-class values, scalar-field channel paths, Scalar Value → Exposure EV, one live definition/parameter registry, and graph-schema-5 exact resolution are implemented and tested. Native human UI confirmation remains a recorded follow-up. | Complete; see `2026-07-16-phase-3-completion-evidence.md`. |
| Phase 4 — IR/fusion/resources | Complete — 2026-07-16 | The first order-preserving pointwise IR, conservative live fusion, generated-program policy, resource reuse/budgeting, inspection, and CPU/unfused/fused evidence are implemented under NMR-109. | Complete; see `2026-07-16-phase-4-completion-evidence.md`. |
| Phase 5 — Compounds | Complete with corrective Phase 5B-C — 2026-07-16 | NMR-110 owns exact compound definitions/lifecycle; NMR-133 repairs authored execution and introduces typed connection presentation; NMR-134 completes the reviewed wire/card/node design, appearance v9 controls, and explicit image-to-mask extraction boundary. | Complete; see the Phase 5, Phase 5B, and Phase 5B-C dated evidence files. |
| Phase 6 — Region/specialized | Complete — 2026-07-17 | Shared spatial/ROI planning, Gaussian equivalence, exact Field Mean, true Reformat extent propagation, typed specialized plans, and non-mutating consumer boundaries satisfy the Phase 6 exit gate. | Complete; see `2026-07-17-phase-6c-and-phase-6-completion-evidence.md`. |
| Phase 7 — Library expansion | Pending and inactive | Research catalog remains reference-only. | Requires explicit activation and product selection. |

## Completed Direction Passes

| Pass | Date | Scope | Result | Code changed | Verification |
| --- | --- | --- | --- | --- | --- |
| Direction 001 | 2026-07-15 | Read documentation protocol, full dated audit, foundational research, and relevant non-catalog contracts; independently triage package; establish durable program documents. | Direction and phase order recorded. | No | Documentation cross-read and path review. |
| Direction 002 | 2026-07-15 | Apply user annotations on compatibility, forward testing, permissive color behavior, authored-order fusion, alpha flexibility, RAW specialization, compounds, and viewport information. | Program changed to a forward-only rewrite; source-label and direct-viewport choices were prepared for user confirmation. | No | Cross-document decision and terminology validation. |
| Direction 003 | 2026-07-15 | Resolve source labeling and viewport presentation from user annotations. | Embedded profiles are retained as descriptive metadata without conversion; untagged sources remain `Unknown`; the viewport directly shows the graph output with its color state in the footer and has no separate preview transform. | No | Cross-document decision and intake validation. |

## Implementation Pass Ledger

| Pass | Phase | Objective | Allowed scope | Pixel policy | Status | Evidence | Next stop |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Phase 0A | 0 | Establish a deterministic generated-value CPU/GPU reference path and prove authored Add/Multiply order is distinguishable. | `CMakeLists.txt`; `cmake/StackSources.cmake`; new `tools/node_math_reference_*`; Phase 0 workstream documentation and audit delta only. | No product behavior or pixel change. Test-only generated RGBA32F data. | Complete — 2026-07-15 | `build.cmd` passed; CTest CPU/GPU 2/2 passed; direct GPU maximum absolute error `7.947286e-08` within `2e-6`; CPU and GPU authored-order separation `4`; existing `StackGraphBehaviorTests.exe` passed; architecture delta completed. | Stop reached. Phase 1 requires separate activation; no production-node, schema, IR, fusion, or UI work is authorized. |
| Phase 1A | 1 | Establish one compiled, validated v1 contract for semantic values/descriptors, node definitions, stable identities, diagnostics, representative propagation, and forward project resolution. | New `src/NodeMath/*`; new `tools/node_math_contract_tests.cpp`; additive `CMakeLists.txt` and `cmake/StackSources.cmake`; Node Math Rewrite documentation only. | No production output, pixel, UI, or current project-format change. | Complete — 2026-07-16 | `StackNodeMathContractTests`: 143 checks passed; CTest node-math 3/3 passed; `StackGraphBehaviorTests.exe` passed; `Stack.exe --validate-layer-registry` passed; `build.cmd` passed; requirement-by-requirement audit in `2026-07-16-phase-1-completion-evidence.md`. | Stop reached. Phase 2 live propagation/integration remains forbidden until separately activated. |
| Phase 2A | 2 | Compile the semantic-spine foundation: canonical descriptor JSON/fingerprints, source color-metadata inspection/retention, exact technical image math, non-mutating graph analysis, stable diagnostics, export metadata policy, and generated reference cases. | New Phase 2 files under `src/NodeMath`; new `tools/node_math_phase2_tests.cpp`; additive CMake/test registration; Node Math Rewrite documentation only. | No live product behavior in this slice. CPU reference math defines the explicit live nodes integrated by Phase 2B. | Complete — 2026-07-16 | `StackNodeMathPhase2Tests`: 30/30 checks; `StackNodeMathSemantic.Phase2` passed; isolated contracts were then eligible for the explicitly requested remainder of Phase 2. | Stop reached; Phase 2B remained inside the user-requested Phase 2 boundary. |
| Phase 2B | 2 | Carry Phase 2 descriptors and exact technical operations through the live graph, renderer, cache identity, persistence, inspection UI, direct viewport, and PNG output. | Phase 2 live owners listed in `code-source-map.md`; graph serialization version 4; explicit technical nodes and alpha modes; targeted graph tests; Phase 2 documentation. | Pixels change only through user-placed Technical Image or explicit Source Over modes. No hidden source, viewport, or output transform/repair. PNG remains an explicit 8-bit export boundary. | Complete — 2026-07-16 | Phase 2 CTest plus all Node Math CTests 4/4 passed; `StackGraphBehaviorTests.exe` passed with Technical Image/source-metadata round trip; `cmake --build ... --target Stack` passed; repository-preferred `build.cmd` passed. | Stop reached. Phase 3 and Phase 4 are unstarted. Native visual checklist remains recorded for human confirmation. |
| Phase 3A | 3 | Establish the complete first-class value envelope and one live scalar/field vertical slice without a new renderer planner. | New Phase 3 NodeMath value files and tests; graph value payload/type/sockets; Channel Split/Combine scalar fields; Scalar Value → Exposure EV; graph serialization/fingerprints/UI; targeted renderer binding; Phase 3 docs. | Only an explicitly connected Scalar Value may override Exposure EV. All other selected type changes are semantic/model/UI changes. | Complete — 2026-07-16 | 72 focused value checks; graph scalar-field/type/round-trip checks; renderer-facing scalar resolution; full evidence in `2026-07-16-phase-3-completion-evidence.md`. | Completed into Phase 3B inside the same authorized phase. |
| Phase 3B | 3 | Make layer-backed and explicit catalog nodes resolve through one validated live definition and declarative parameter registry. | Unified definition owner plus integrations with layer/catalog metadata, sockets, parameter/animation declarations, exact forward identities, validation, serialization tests, and Phase 3 docs. | No additional pixel/formula changes. | Complete — 2026-07-16 | Unified registry validation is part of `StackGraphBehaviorTests` and `Stack.exe --validate-layer-registry`; exact schema-5 save/load mismatch tests pass. | Stop reached. Phase 4 remains forbidden pending NMR-109 and explicit activation. |
| Phase 4A | 4 | Establish the first typed semantic expression IR and make a vetted linear pointwise graph slice execute as fewer physical passes without changing authored order. | `src/NodeMath` pointwise IR/planning; live single-input RGBA Data Math and selected Technical Image fusion; source maps; GLSL generation/cache/limits; bounded transient and persistent resource policies; execution inspection; Phase 4 tests and workstream documentation. | Fused and unfused forms must match the adopted formulas within recorded tolerances. No formula correction, hidden clamp/conversion, or operation reordering. | Complete — 2026-07-16 | 35 focused IR checks; live CPU/unfused-GPU/fused-GPU validation; 7/7 Node Math CTests; graph behavior and registry validation; live Stack target and `build.cmd` passed. Full evidence: `2026-07-16-phase-4-completion-evidence.md`. | Stop reached. Phase 5 compounds remain unauthorized. |
| Phase 5A | 5 | Establish executable, exact, project-portable compound definitions and instances without entering region-aware or public-library expansion work. | Phase 5 compound contract/model; graph schema and exact embedded dependency closure; stable instance/interface identities; transparent and optimized-equivalent pointwise examples; nesting; unresolved state; create-from-selection/version-edit APIs; explicit update, Make Unique, Unpack; temporary execution flattening; focused UI; Phase 5 tests and documentation. | Canonical internal order is exact. Flattening/fusion may change scheduling only within recorded tolerance. No unrelated formula, color, alpha, output, ROI, or specialized-stage change. | Complete — 2026-07-16 | 21 focused contract checks; Phase 5 graph lifecycle/persistence coverage; live canonical CPU/materialized GPU/fused GPU equivalence; 9/9 Node Math CTests; graph behavior and registry validation; `build.cmd` passed. Full evidence: `2026-07-16-phase-5-completion-evidence.md`. | Stop reached. Phase 6 was not started and remains unauthorized. |
| Phase 5B | 5 | Repair authored compound output submission and make socket meaning discoverable before Phase 6. | Compound output/reference traversal and diagnostics; socket presentation glossary/catalog; pin labels/detail cards/advanced reveal/Connections inspector; appearance v8 adaptive and break-line wire labels; tests and Phase 5B docs. | No formula, conversion, viewport transform, RAW decomposition, ROI, halo, neighborhood, reduction, tiling, or Phase 6 change. The authored compound result must match Unpack within existing tolerance. | Complete — 2026-07-16 | Exact authored two-compound live render produces a nonzero texture and matches Unpack; graph behavior covers nested/custom/copy/unique/save/reload/unresolved execution; socket/geometry/formatter/settings checks pass. Full evidence: `2026-07-16-phase-5b-completion-evidence.md`. | Stop reached. Phase 6 was not started and remains unauthorized. |
| Phase 5B-C | 5 | Correct the native-review problems in typed graph connection presentation without starting Phase 6. | Rotated wire text; target-anchored card lifecycle; removal of Connections dropdown/media cursor tooltip; appearance v9 connection text and node sizing controls; explicit full-image-to-scalar rejection; focused tests and docs. | No formula, conversion, viewport transform, RAW decomposition, ROI, halo, neighborhood, reduction, tiling, or Phase 6 change. Structurally executable semantic experimentation stays permissive. | Complete — 2026-07-16 | Graph behavior rejection/explicit-extractor coverage; rotated geometry and appearance migration validation; live authored compound output; Node Math CTests; registry and full Windows build. Full evidence: `2026-07-16-phase-5b-c-corrective-completion-evidence.md`. | Stop reached. Phase 6 was not started and remains unauthorized. |
| Phase 6A | 6 | Establish the first shared region-aware planning contract and prove it through the existing Gaussian Blur without publishing new nodes. | Descriptor spatial v2 origin; render region/scale/ROI/halo primitives; graph locality plan; exact Gaussian/Box support discovery; planner-derived tile halo; tile cancellation/publication safety; generated CPU and live GPU full/tiled equivalence; Phase 6A contract/evidence. | Preserve the existing Gaussian and Box formulas, clamp border, authored order, RGBA16F materialization, and full-frame result within declared tolerance. No new public operation or formula correction. | Complete — 2026-07-17 | 19 focused region checks; graph planner/cancellation coverage; 11/11 Node Math CTests; live full/tiled Gaussian maximum difference `0` under `2.5e-3`; registry validation; `build.cmd` passed. Full evidence: `2026-07-17-phase-6a-completion-evidence.md`. | Completed into Phase 6B/6C; the retained Gaussian evidence remains part of the final Phase 6 gate. |
| Phase 6B | 6 | Prove the first reusable global reduction as a real graph value. | Exact Field Mean contract; typed ScalarField input and uniform Scalar output; deterministic full-frame reduction; cache/failure inspection; planner capability; graph/persistence/registry tests; live Exposure-EV equivalence. | No implicit image-to-luminance conversion, stale value reuse, non-finite scrubbing, ROI/mask policy, other reductions, geometry, or Phase 7 expansion. | Complete — 2026-07-17 | 24 focused Phase 6 checks; authored editor graph measured 0.5 from 32 samples; persistent cache reuse; constant-EV max difference 0; 13/13 Node Math CTests; graph behavior; registry; `build.cmd`. Full evidence: `2026-07-17-phase-6b-completion-evidence.md`. | Completed into Phase 6C; the retained Field Mean evidence remains part of the final Phase 6 gate. |
| Phase 6C | 6 | Close the Phase 6 exit gate with one true extent-changing geometry path and representative typed specialized boundaries. | Exact Reformat definition/math/UI/persistence; semantic and physical extent propagation; hard mismatched-extent planning diagnostics; typed RAW, external, multi-frame, FFT/IFFT, scope, preview, and export policies; consumer traces; generated, graph, and live evidence. | Reformat changes pixels only through explicit Nearest/Linear Clamp sampling. No hidden alignment, color/alpha conversion, RAW decomposition, broader node-library expansion, or Phase 7 work. | Complete — 2026-07-17 | 39 Phase 6 CPU checks; 13/13 CTests; graph behavior and registry passed; 4x3→7x5 Reformat max difference `0.000351787`; FFT round trip `0.000451922`; Gaussian tiled/full difference `0`; socket catalog and `build.cmd` passed. Full evidence: `2026-07-17-phase-6c-and-phase-6-completion-evidence.md`. | Stop reached. Phase 6 is complete; Phase 7 is inactive pending explicit product selection. |

### Phase 6C And Phase 6 Completion Handoff

- Geometry result: `Reformat` is a public Geometry / Transform node with exact
  definition `stack:geometry/reformat` version `1.0.0`. It produces a real
  downstream width/height using inverse pixel-center Nearest or Linear sampling
  with Clamp border.
- Semantic result: Reformat preserves input color/transfer/alpha meaning and
  known origin, raster convention, and pixel aspect while changing the finite
  extent and data window. It inserts no conversion or pixel repair.
- Planning result: Reformat is a full-frame Sample/Resample barrier in v1.
  Multi-image inputs with different extents and no declared alignment policy
  fail before execution and request an explicit Reformat.
- Specialized result: RAW decode/development, RAW neural/external, multi-frame,
  FFT/inverse FFT, retained-spectrum operations, scopes, preview, and export
  now declare typed region, scale, cancellation, and failure policies. RAW
  remains opaque; scope/preview/export do not change graph pixels.
- Live result: the authored 4x3 → 7x5 Reformat plus Exposure graph matched its
  CPU reference within `0.000351787`; authored FFT → inverse FFT matched within
  `0.000451922`. Existing Field Mean and Gaussian evidence stayed green.
- Verification result: 39 Phase 6 checks, all 13 registered CTests, graph
  behavior, layer-registry validation, socket-catalog generation, live OpenGL
  validation, selected builds, and the repository-preferred `build.cmd` passed.
- Stop result: Phase 6 is complete. Phase 7 has not started and requires an
  explicit product-selection pass.

### Phase 6B Completion Checkpoint

- Product result: `Field Mean` is a public Analysis / Measure node with exact
  definition `stack:analysis/field-mean` version `1.0.0`. It accepts an explicit
  one-channel per-pixel ScalarField and returns one uniform Scalar.
- Explicitness result: full images are rejected at the Field input. Users
  choose `Channel Split` or `Luminance Mask`; Stack does not silently choose
  luminance or auto-insert an extractor.
- Math result: deterministic Neumaier-compensated float64 accumulation computes
  the exact full-population arithmetic mean of materialized float samples.
  Empty, NaN, and infinity inputs fail without clamping, replacement, or stale
  value publication.
- Planning result: Field Mean is a full-frame Reduction capability and a fusion
  barrier. Its definition, socket, algorithm version, input fingerprint, and
  extent participate in the persistent runtime scalar-cache identity.
- Live result: the real authored editor graph
  `Image -> Channel Split -> R -> Field Mean -> Exposure EV` measured `0.5`
  from 32 samples, reused the cached scalar after a downstream-only change, and
  matched a constant-EV `0.5` graph with maximum difference `0`.
- Verification result: 24 Phase 6 CPU checks, all 13 Node Math CTests, graph
  behavior, layer-registry validation, socket-catalog generation, live OpenGL
  validation, selected target builds, and `build.cmd` passed.
- Checkpoint result: Phase 6B was complete while geometry and specialized-stage
  work remained inactive. Phase 6C later closed those selected gaps; Phase 7
  remains inactive.

### Phase 6A Completion Handoff

- Contract result: NMR-135 and `phase-6-region-contract-v1.md` define half-open
  finite regions, separate full/data windows, explicit raster origin, pixel
  aspect, render scale, channel span, ROI mapping, named border behavior, and
  incomplete-result cancellation.
- Descriptor result: semantic descriptor schema 2 records bottom-left or
  top-left raster origin. Schema-1 descriptor JSON migrates deterministically
  to bottom-left; project graph schema and old-project compatibility policy do
  not change. Descriptor fingerprints and dependent cache identities advance.
- Planning result: the renderer classifies supported pointwise and existing
  Gaussian/Box stages, carries requested regions upstream, adds neighborhood
  support across chains, and falls back to an explained full-frame plan for a
  legal unsupported boundary rather than presenting it as an invalid graph.
- Tile result: Gaussian/Box correctness halo is derived from
  `int(max(1, amount))`; the user setting is now an optional extra minimum.
  Canceled tile traversal destroys partial textures and never publishes an
  incomplete result.
- Pixel result: no blur formula, border rule, authored order, color/alpha
  conversion, or public node changed. The generated 530x270 live chain
  `Image -> Gaussian Blur -> Technical Exposure -> Output` matched manually
  stitched planner tiles to full-frame output exactly (`max difference 0`,
  tolerance `2.5e-3`).
- Inspection result: Graph Performance reports whether the region plan is
  tileable or full-frame, its required X/Y halo, and any fallback reason. The
  setting is labeled `Extra Tile Halo` to distinguish preference from the
  planner's correctness requirement.
- Verification result: 19 focused Phase 6 checks, graph behavior, all 11 Node
  Math CTests, live Phase 6 OpenGL validation, layer-registry validation, the
  serial target build, and repository-preferred `build.cmd` passed. One first
  parallel build attempt encountered the existing generated-font-header race;
  its clean retry passed without a source change.
- Visual-review status: automated geometry, planner, persistence, cancellation,
  and live rendering checks are complete. Native review of the revised setting
  label and Graph Performance line remains an optional human confirmation.
- Historical stop result: Phase 6A completed with no Phase 6B slice active at
  that time. Phase 6B was later activated and completed under NMR-136.
  Additional reductions, public kernels/morphology/samplers, true geometry
  extent changes, proxies/pyramids, and broader specialized-stage integration
  remain later work.

### Phase 5 Completion Handoff

- Phase 5B correction: the Phase 5A live validator proved expanded numerical
  equivalence but bypassed the authored traversal that gates renderer
  submission. Output traversal now carries exact output socket IDs, resolves
  compound public-output dependencies, and the real authored chain renders
  before Unpack.
- Typed connection result: all socket presentation runs through one glossary;
  the generated `node-socket-catalog.md` inventories visible, hidden,
  non-browser, advanced, and shipped-compound ports without guessing Unknown
  shape or units.
- UI result: expanded and interactive compact pin labels, progressive advanced
  reveal, adaptive/floating/break-line wire text, and 0.7-second cards are
  implemented under NMR-133. NMR-134 rotates labels with their wire, anchors
  cards to their target, removes the Connections dropdown and cursor-following
  media tooltip, adds appearance version 9 text/node sizing controls, and makes
  the outline default off. Projects do not store these preferences.
- Connection result: a full image cannot connect directly to a scalar-field or
  Mask input and no longer auto-spawns Luminance Mask. The rejection directs
  users to add an explicit extractor; structurally executable color, transfer,
  alpha, and range mismatches remain permissive and diagnostic.

- Contract result: NMR-110 and `phase-5-compound-contract-v1.md` define exact,
  self-contained project/copy embedding, stable public and internal identities,
  deliberate versions, typed unresolved shells, nonrecursive dependencies,
  and transparent/optimized/opaque definition classes.
- Product result: Stack can create supported compounds from a selection, show
  promoted scalar controls, save/copy/nest exact instances, Make Unique,
  Unpack, and explicitly update one instance to a newer embedded version.
- Persistence result: graph JSON version 6 stores compound definitions, instances,
  exact dependency closure, interface snapshots, promoted overrides, and new
  instance UUIDs on paste. Missing, invalid, mismatched, or recursive
  definitions remain unresolved without fallback.
- Execution result: resolved graph compounds expand into a temporary graph
  before renderer snapshot creation. The saved graph remains intact, authored
  internal order remains exact, and immutable source pixels are shared rather
  than copied solely for expansion.
- Equivalence result: the `Exposure -> Premultiply` optimized-equivalent
  reference matches canonical CPU and materialized GPU execution within
  `2.5e-3`; the fused group reports the same two-node order.
- Specialized result: RAW live definitions are explicitly opaque specialized
  and cannot be automatically unpacked by the compound system.
- Verification result: 21 focused contract checks, expanded Phase 5B graph
  behavior, all nine Node Math CTests, the real authored-chain OpenGL
  validation, registry validation, catalog generation, and the
  repository-preferred `build.cmd` passed.
- Visual-review status: use `phase-5b-visual-test-checklist.md` to confirm final
  density, themes, zoom behavior, controls, advanced-socket discovery, delayed
  cards, and floating/break-line text in the native application.
- Stop result: Phase 5B-C is complete. Phase 6 ROI/halo, neighborhood, reduction,
  and specialized-stage work was not started.

### Phase 4 Completion Handoff

- Contract result: NMR-109 and `phase-4-ir-contract-v1.md` define a typed,
  ordered, single-sampled-input RGBA expression program. The first operation
  set is deliberately small and excludes Divide, remap, reductions,
  multi-image expressions, curves/LUTs, matrices, and specialized work.
- Live result: eligible linear Data Math and Technical Image chains now lower
  to one generated GLSL pass. Fan-out, masks, unsupported nodes, scalar-field
  inputs, requested intermediate outputs, semantic boundaries, and hard
  program limits materialize or fall back conservatively.
- Order result: dependencies and operands remain in the user's authored
  sequence. Both `Add -> Multiply` and `Multiply -> Add` execute as fused
  programs and remain observably different.
- Optimization result: unreachable expressions are removed; uniform-only
  expressions fold; identical expressions reuse the same value only when
  their ordered operands match. Reassociation and operand sorting are absent.
- Resource result: generated programs use a 64-entry LRU cache, graph image
  and mask textures share a 512 MiB soft LRU budget, and compatible multipass
  targets reuse a dimension-matched transient pool.
- Inspection result: the Graph Performance popup reports ordered fused node
  groups, avoided passes, RGBA16F target bytes, submit timing, program-cache
  reuse, persistent/transient memory, evictions, and source-mapped failures.
- Verification result: 35 focused IR checks passed; all seven Node Math CTests
  passed; the hidden live OpenGL check matched CPU, ordinary GPU, and fused GPU
  within `2.5e-3`, reduced the representative chain from two materialized
  textures/passes to one, and reported 32 versus 64 persistent bytes for its
  4x1 test image. Graph behavior, registry validation, the Stack target, and
  `build.cmd` also passed.
- Visual-review status: the Graph Performance presentation still requires
  native human confirmation; no visual-polish claim is made by automation.
- Stop result: Phase 4 is complete. No executable compound definition,
  compound UI, general ROI planner, or expanded public primitive catalog was
  implemented.

### Phase 3 Completion Handoff

- Contract result: `FirstClassValue` is the exact serialized authority for
  logical type, storage class, availability, units, and payload. Known,
  Unknown, Missing, and Failure remain distinct.
- Live value result: Channel Split/Combine and scalar Average use
  `ScalarField` rather than `Mask`; temporary Mask/scalar compatibility remains
  only for staged existing paths. Value nodes expose selected uniform types.
- Pixel result: a known uniform Scalar Value connected to Exposure EV is
  resolved into the renderer snapshot and overrides the fallback EV. Typed
  vector-to-scalar conversion is rejected unless a future explicit conversion
  node is added.
- Registry result: browser metadata, static socket definitions, layer timeline
  parameters, declarative settings envelopes, animation policy, and exact
  definition identities resolve through `UnifiedNodeDefinitionRegistry`.
- Persistence result: graph JSON version 5 pins definition ID/version/hash and
  serializes typed values. Invalid typed payloads become typed Missing values;
  missing or mismatched exact definitions stay unresolved without fallback.
- Verification result: 72 Phase 3 value checks passed; all five Node Math
  CTests passed; graph behavior passed; unified registry validation passed in
  the application; the live Stack target and repository-preferred `build.cmd`
  passed. Commands and limitations are in
  `2026-07-16-phase-3-completion-evidence.md`.
- Visual-review status: native Value-node controls, typed pin appearance, and
  connected Exposure behavior still require human confirmation; no visual pass
  is claimed.
- Stop result: Phase 3 is complete. NMR-109 and Phase 4 IR/fusion/resource work
  were not started.

### Phase 2 Completion Handoff

- Authorization and boundary: the user explicitly requested completion of the
  next phase and only that phase. After the isolated Phase 2A contracts passed,
  the same Phase 2 pass continued through the required live integration and
  stopped before Phase 3.
- Contract owners: `DescriptorSerialization`, `SourceColorMetadata`,
  `TechnicalImageMath`, `SemanticSpine`, and `PngMetadataWriter` under
  `src/NodeMath`.
- Source result: PNG/JPEG embedded color information is inspected and retained
  descriptively without changing pixels. Untagged ordinary sources remain
  `Unknown`. Exact relevant payload bytes and a stable dependency identity are
  serialized with the source rather than inferred from the project-storage
  PNG.
- Graph result: live render links and graph outputs carry descriptors and
  stable content identities. Semantic analysis is non-mutating, deterministic,
  and permissive: encoded Exposure, unusual color ordering, and alpha-formula
  mismatches can execute with stable information/warnings.
- Technical-node result: the Image Technical browser family supplies Assign
  sRGB, Assign Linear sRGB, Assign Linear Display-P3, sRGB Decode/Encode,
  linear sRGB/Display-P3 conversions, Exposure (EV), Premultiply, and guarded
  Unpremultiply. Assignment changes metadata only; the other named operations
  change pixels exactly where placed by the user.
- Alpha result: straight and premultiplied Source Over are separate explicit
  Blend Images modes. RGB and alpha remain independently accessible. Guarded
  Unpremultiply maps alpha at or below `1e-6` to transparent black.
- View/output result: the viewport shows the direct graph result with a footer
  containing connected color/transfer/alpha state and notice count. PNG output
  performs no hidden color, tone, gamut, alpha, or normalization repair; it
  writes sRGB, Display-P3 cICP, or a retained PNG iCCP payload only when the
  connected descriptor justifies that metadata. Unknown output remains
  untagged; premultiplied PNG output is blocked until an explicit Unpremultiply.
- Persistence result: graph JSON version 4 stores Technical Image settings and
  original source color metadata. Pre-rewrite compatibility remains explicitly
  out of scope.
- Verification result: 30 Phase 2 checks passed; all four registered Node Math
  CTests passed; graph behavior passed; the live Stack target and `build.cmd`
  passed. Exact commands and limitations are recorded in
  `2026-07-16-phase-2-completion-evidence.md`.
- Visual-review status: native UI automation was unavailable in this session.
  Human confirmation of node controls, wire tooltips, footer, notices, and PNG
  messages is still requested; no visual pass is claimed.
- Stop result: Phase 2 code work is complete and no implementation pass is
  active. Phase 3 typed values and Phase 4 IR/fusion were not started.

### Phase 1A Starting Handoff

- Starting commit: `152ebe4d206f4f436a1d826bf750dc0ae8a7a886` on `main`.
- Dirty-tree context: substantial pre-existing user work affects graph model,
  graph serialization, renderer, RAW, editor, validation, CMake, and docs. The
  pass avoids editing the live graph/persistence owners and uses isolated new
  files plus additive build entries.
- Previous gate: Phase 0 passed every deliverable/exit condition on 2026-07-15.
- Governing decisions: NMR-003, NMR-004, NMR-008 through NMR-020, NMR-100,
  NMR-102, NMR-105 through NMR-108, and NMR-125.
- Phase 1 blockers resolved before code: NMR-100 descriptor v1, NMR-106 forward
  project generation/exact resolution, and NMR-108 stable identities are
  accepted in `phase-1-contract-v1.md`.
- Exact source scope: new `src/NodeMath/ContractTypes.*`,
  `NodeDefinition.*`, `DescriptorPropagation.*`, and `ProjectSchema.*`.
- Exact test/build scope: new `tools/node_math_contract_tests.cpp`; additive
  node-math source list, target, and CTest registration only.
- Exact documentation scope: the canonical Phase 1 contract, tracker, README,
  program contract, decision register, roadmap/research/source routing as
  needed, and one dated Phase 1 completion-evidence document.
- Forbidden changes: current `EditorNodeGraph` model/payloads/serializer,
  `StackBinaryFormat`, production renderer, node formulas, project files,
  caches, scheduling, viewport/export, and UI.
- Pixel/serialization impact: none. The new generation-1 envelope is a tested
  contract object, not installed into current MSTK/graph saving during Phase 1.
- User visual review: not applicable because no live pixels or UI change.
- Completion requirement: prove every roadmap deliverable and exit condition,
  including representability of all current major paths without invented
  meaning or hidden defaults.

### Phase 1A Completion Handoff

- Contract result: `phase-1-contract-v1.md` is implemented as an isolated
  compiled library under `src/NodeMath`. The application build compiles it, but
  no current graph, renderer, persistence, project, viewport, export, or UI
  path calls it.
- Descriptor result: all v1 values use explicit `Known`, `Unknown`, or
  `NotApplicable` field states. Tagged sources retain descriptive profile
  metadata without conversion; untagged sources remain `Unknown`; strict
  semantic matching never treats `Unknown` as proof of equality.
- Definition result: exact definition, port, parameter, instance, connection,
  and implementation identities validate. Definition and implementation
  SHA-256 values are computed from canonical length-delimited v1 content and
  fail validation after an undeclared content change.
- Propagation result: compiled representative rules cover source, identity,
  arithmetic, exposure, mask, geometry, color transform, composite, reduction,
  direct output, and unknown external work. Numerically defined unusual color
  work warns and runs; genuinely missing execution-critical meaning is a hard
  error. No conversion or pixel repair is inserted.
- Current-path result: 15 representative definitions cover ordinary source,
  RAW source/development, identity, generic math, exposure, mask, geometry,
  color transform, composite, reduction, direct output, frequency, multi-image,
  and external/specialized paths. RAW representatives remain opaque.
- Project result: the generation-1/schema-1 forward envelope round-trips JSON;
  pre-rewrite, future generation, and malformed documents are distinct; exact
  resolution distinguishes missing ID, version mismatch, and hash mismatch
  without fallback or auto-update.
- Diagnostic result: stable rules cover Connection, Semantic, Lowering, and
  Runtime stages. Only warnings/information may be acknowledged, and the
  acknowledgement fingerprint is a content identity.
- Verification result: 143 focused checks passed; the three node-math CTests
  passed; graph behavior, layer-registry validation, and the full preferred
  Windows build passed. Exact commands and exit assessment are in
  `2026-07-16-phase-1-completion-evidence.md`.
- Stop result: Phase 1 is complete. Phase 2 is eligible but not active; no live
  descriptor propagation, diagnostics UI, color/alpha boundary, project-format,
  renderer, viewport, or export integration was started.

### Phase 0A Starting Handoff

- Starting commit: `152ebe4d206f4f436a1d826bf750dc0ae8a7a886` on `main`.
- Dirty-tree context: substantial pre-existing user work affects CMake, graph
  tests, renderer, RAW, editor, and documentation files. Preserve it. Phase 0A
  uses new test files and additive edits at the existing CMake test boundaries.
- Governing decisions: NMR-004, NMR-008, NMR-009, NMR-010, NMR-012, and
  NMR-014. At activation, NMR-125 was the only directly relevant open
  nonblocking decision; this pass resolved it.
- Exact source/test scope: `CMakeLists.txt`, `cmake/StackSources.cmake`,
  `tools/node_math_reference_harness.h`,
  `tools/node_math_reference_harness.cpp`, and
  `tools/node_math_reference_tests.cpp`.
- Exact documentation scope: this tracker, `README.md`, `program-contract.md`,
  `decision-register.md`, `phase-roadmap.md`, `code-source-map.md`, and one
  dated Phase 0 current-architecture delta.
- Forbidden changes: production pixels, node formulas, graph data structures,
  serialization, cache keys, project files, renderer scheduling, and UI.
- CPU/GPU strategy: a small independent double-precision CPU evaluator is the
  oracle for generated cases; the GPU path evaluates the same ordered formulas
  into RGBA32F and is compared after `GL_FLOAT` readback within a recorded
  tolerance. This closes the Phase 0 portion of NMR-125 without selecting the
  eventual full definition framework.
- Generated cases: finite negative, fractional, zero, and greater-than-one
  components; Identity; Add; Multiply; `(x + a) * m`; and `x * m + a`.
- Observability subset: exact RGBA32F readback, shader/link and labeled GL error
  visibility, source/output target byte counts, synchronized per-pass wall
  timing, GL vendor/renderer/version, and explicit context/readback failures.
- User visual review: not applicable to Phase 0A because it changes no product
  pixels or UI. The reusable handoff pattern will require real-image user review
  for later pixel-changing passes.

### Phase 0A Completion Handoff

- Source result: added a standalone `StackNodeMathReferenceTests` target. Its
  CPU mode validates five independently authored formulas. Its GPU mode creates
  a hidden OpenGL 4.3 context, evaluates the same generated values into
  RGBA32F, reads them back as `GL_FLOAT`, and compares them to the CPU oracle.
- Generated coverage: four RGBA values containing negative, fractional, zero,
  and greater-than-one components; Identity, Add, Multiply,
  `(x + addend) * multiplier`, and
  `x * multiplier + addend`.
- Test result: `StackNodeMathReference.Cpu` and
  `StackNodeMathReference.Gpu` passed through CTest (2/2 tests).
- GPU evidence on the tested machine: NVIDIA GeForce RTX 3060, OpenGL 4.3.0,
  driver-reported version `596.49`; maximum CPU/GPU absolute error
  `7.947286e-08` against tolerance `2e-6`; authored-order separation `4`.
- Observability evidence: labeled GL failures, shader compile/link diagnostics,
  exact float readback, source/output/peak test-target bytes, GL identity, and
  synchronized per-pass wall timings are emitted by the GPU run.
- Build/regression evidence: the repository-preferred `build.cmd` completed and
  the existing `StackGraphBehaviorTests.exe` passed.
- Product impact: no production source was changed. Product pixels, formulas,
  serialization, cache keys, project files, renderer scheduling, and UI remain
  unchanged.
- User visual review: not applicable because there is no product-visible change.
- Known limitations: the GPU test requires a functioning hidden OpenGL 4.3
  context; GPU results use float while the CPU oracle uses double; the tolerance
  is intentionally explicit; timings are synchronized wall times rather than
  GPU timer-query benchmarks; finite pointwise cases do not yet cover
  non-finite values, color, alpha, neighborhoods, reductions, or production
  node shaders.
- Architecture evidence: see
  `2026-07-15-phase-0-current-architecture-delta.md`.
- Exit decision: all Phase 0 deliverables and exit conditions pass. No stored
  historical image/project corpus or historical-pixel guarantee was introduced.

Do not use a phase name as permission for an unbounded rewrite.

## Required Pass Handoff

Every implementation pass must record:

- starting commit/working-tree context and relevant pre-existing changes;
- active decision IDs and unresolved blockers;
- exact source, test, generated-case, and documentation scope;
- whether pixels, serialization, cache keys, project files, or UI can change;
- build/test commands and results;
- reference vectors/in-memory cases, tolerance, and environment where
  applicable;
- user visual-review status for the behavior changed in that pass;
- intended new-project/definition result and forward version impact;
- known failures or hardware-dependent behavior;
- documents updated; and
- the next permitted action and explicit stop point.

## Activation Rule

To activate a phase:

1. confirm the previous phase's exit gate or document why a read-only research
   task does not depend on it;
2. resolve every blocking decision owned by the planned slice;
3. update the Current Checkpoint and add the pass-ledger row;
4. inspect the current code and dirty worktree;
5. name the smallest buildable/testable slice;
6. keep unrelated phase work out of scope; and
7. update this ledger before declaring the pass complete.

## Direction Pass Outcome

The documentation currently points to this first implementation order:

```text
forward verification and observability
-> minimal accepted semantic and definition contracts
-> descriptor-bearing image spine plus explicit technical boundaries
-> first-class values and unified definitions
-> pointwise IR/fusion/resource planning
-> real compound definitions
-> region-aware specialized stages
-> selective node-library expansion
```

The first code pass is therefore a generated-reference/test-foundation pass,
not an old-output capture pass and not yet a production-node rewrite.
