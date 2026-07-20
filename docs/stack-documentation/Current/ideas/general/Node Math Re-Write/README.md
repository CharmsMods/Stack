# Node Math Rewrite

This folder is the active direction, architecture, and eventual implementation
workspace for making Stack's graph nodes mathematically definitive, semantically
reliable, inspectable, and efficient without removing familiar high-level tools.

Direction planning and Phases 0 through 6 are complete. Phase 6A established
spatial/ROI planning, Phase 6B added the reusable
`ScalarField -> Field Mean -> Scalar` reduction, and Phase 6C closed the phase
with true Reformat extent propagation and typed specialized-stage boundaries.
Phase 7 public-library expansion remains inactive and requires an explicit
product-selection pass.

## Current Status

- Direction pass: complete as of 2026-07-15
- Phase 0 — Forward Verification Foundation: complete as of 2026-07-15
- Phase 1 — Semantic And Node-Definition Contracts: complete as of 2026-07-16
- Phase 2 — Semantic Spine, Diagnostics, And Technical Image Boundaries:
  complete as of 2026-07-16; native human UI confirmation remains recorded as
  a non-code follow-up
- Phase 3 — First-Class Values And Unified Definitions: complete as of
  2026-07-16; native human UI confirmation remains recorded as a non-code
  follow-up
- Phase 4 — Semantic IR, Fusion, And Resource Planning: complete as of
  2026-07-16; native execution-inspection review remains recorded as a
  non-code follow-up
- Phase 5 — Executable Compound Definitions: complete as of 2026-07-16
- Phase 5B — Compound Output Repair and Typed Connection UI: complete as of
  2026-07-16; native visual confirmation remains recorded in
  `phase-5b-visual-test-checklist.md`
- Phase 5B-C — Graph Connection UI Correction: complete as of 2026-07-16;
  rotated labels, stable cards, simplified node surfaces, appearance v9 sizing,
  and explicit image-to-mask extraction await the same native checklist
- Phase 6 — Region-Aware And Specialized Infrastructure: complete as of
  2026-07-17
- Completed implementation slice: Phase 6A — shared spatial/ROI planning and
  the existing Gaussian Blur tiled-equivalence path, completed 2026-07-17
- Completed implementation slice: Phase 6B — exact full-frame `Field Mean`
  reduction and live Scalar-to-Exposure proof, completed 2026-07-17
- Completed implementation slice: Phase 6C — extent-changing `Reformat`, hard
  extent mismatch diagnostics, typed RAW/frequency/external planning, and
  non-mutating scope/preview/export boundaries, completed 2026-07-17
- Active implementation slice: none
- Next phase: Phase 7 — pending and inactive
- Current-state authority: `STACK_IMAGE_MATH_AND_PIPELINE_AUDIT as of
  7-12-26.md`
- Architecture research authority: the foundational documents identified in
  `research-index.md`

## Direction In One Paragraph

Stack should keep an authored graph that explains what the user intends, lower
that graph into a typed semantic intermediate representation, and separately
create a physical GPU/CPU execution plan. A node definition should promise a
specific mathematical or algorithmic result, typed inputs and outputs, domain
and range behavior, alpha/color/extent requirements, diagnostics, tests, and a
stable forward version story. The visible graph must not be treated as a
literal list of full-image render passes. Fusion may combine compatible work,
but it must preserve the exact operation order authored by the user. This
separation is the prerequisite for both small factual primitives and friendly
inspectable compound nodes.

## Why The First Pass Is Not A Node-Catalog Pass

The 2026-07-12 audit established that Stack currently routes encoded ordinary
RGB, RAW-derived scene-linear RGB, and other incompatible meanings through the
same coarse `Image` type. It also established that normal nodes materialize
separate full-canvas `GL_RGBA16F` results, graph JSON versioning is weak, alpha
is undeclared, display/output handling is incomplete, and GPU formula/color/
alpha reference tests are absent.

Adding many small public nodes first would make the graph larger without making
it definitive, and would multiply render passes and cached textures. The first
implementation work should establish a forward-looking test path for the new
definitions: generated numerical inputs, CPU/GPU comparisons where useful,
build validation, and user visual review as each change lands. It does not need
to preserve pre-rewrite project files or historical pixel output.

## Authority And Read Order

Read these files in order for any future direction or implementation pass:

1. `README.md`
2. `implementation-progress.md`
3. `program-contract.md`
4. `decision-register.md`
5. `phase-roadmap.md`
6. The active phase's required research from `research-index.md`
7. The phase-specific code areas in `code-source-map.md`
8. Current code, tests, and build configuration

For a new architecture pass, also read the full dated audit. For a focused
implementation slice, use the audit as a dated map and re-check the relevant
code rather than trusting historical line numbers.

If documents conflict, use this order:

```text
Current code and reproducible tests
-> current dated audit for facts about the audited tree
-> program-contract.md for program invariants
-> recorded decisions in decision-register.md
-> phase-roadmap.md for dependency order
-> research package for proposals and reference definitions
-> operation catalogs for on-demand formula research
```

The research package does not override current code facts. Current accidental
behavior does not automatically become the future contract.

## Folder Map

| Document or folder | Owns |
| --- | --- |
| `README.md` | Entry point, current direction, read order, and document ownership. |
| `program-contract.md` | Product boundary, architectural invariants, node-definition requirements, and definition of done. |
| `decision-register.md` | Confirmed constraints, adopted planning direction, unresolved product choices, and the phases they block. |
| `phase-roadmap.md` | Required phase order, deliverables, pixel-change policy, and exit gates. |
| `implementation-progress.md` | The only live status and implementation-pass ledger in this folder. |
| `research-index.md` | Foundational versus phase-specific versus reference-only reading routes and update ownership. |
| `code-source-map.md` | Dated map from each phase to likely source and test owners; never a substitute for code inspection. |
| `2026-07-15-phase-0-current-architecture-delta.md` | Phase 0 recheck of the audited integration facts, reference-harness decision, evidence, and limitations. |
| `phase-1-contract-v1.md` | Accepted canonical value/descriptor, definition, identity, project-generation, diagnostics, and propagation contract. |
| `2026-07-16-phase-1-completion-evidence.md` | Phase 1 requirement coverage, compiled implementation inventory, verification results, boundaries, and exit decision. |
| `2026-07-16-phase-2-completion-evidence.md` | Phase 2 semantic-spine/live-integration inventory, exact technical math and output decisions, verification results, limitations, and exit decision. |
| `2026-07-16-phase-3-completion-evidence.md` | Phase 3 first-class value contract, live scalar/field slice, unified definition registry, forward persistence evidence, tests, limitations, and exit decision. |
| `phase-4-ir-contract-v1.md` | Accepted Phase 4 typed, ordered pointwise IR, optimization, barrier, resource, and equivalence contract. |
| `2026-07-16-phase-4-completion-evidence.md` | Phase 4 IR/fusion implementation, live execution and resource behavior, verification results, limitations, and exit decision. |
| `phase-5-compound-contract-v1.md` | Accepted Phase 5 exact embedded compound definition, instance, lifecycle, storage, and execution contract. |
| `2026-07-16-phase-5-completion-evidence.md` | Phase 5 compound implementation, exact persistence/lifecycle behavior, canonical-versus-optimized evidence, limitations, and exit decision. |
| `phase-5b-typed-connection-ui-contract.md` | Accepted authored-compound traversal repair, normalized socket presentation, pin-label, detail-card, advanced-pin, and wire-label contract. |
| `node-socket-catalog.md` | Generated maintained inventory of live, hidden, non-browser, advanced, and shipped-compound sockets. |
| `phase-5b-visual-test-checklist.md` | Native review steps for compound output, pin labels, advanced sockets, detail cards, themes, zoom, and wire layouts. |
| `2026-07-16-phase-5b-completion-evidence.md` | Phase 5A validator correction, Phase 5B implementation inventory, automated evidence, boundaries, and stop decision. |
| `2026-07-16-phase-5b-c-corrective-completion-evidence.md` | Native-review correction for rotated wire text, anchored cards, simplified node surfaces, text/node settings, and explicit image-to-mask extraction. |
| `phase-6-region-contract-v1.md` | Accepted finite-region, full/data-window, origin, render-scale, ROI/halo, tile-planning, and cancellation contract for Phase 6A. |
| `2026-07-17-phase-6a-completion-evidence.md` | Phase 6A implementation inventory, generated region cases, live tiled/full-frame equivalence, build results, limitations, and stop decision. |
| `phase-6b-reduction-contract-v1.md` | Accepted exact Field Mean formula, typed interface, full-frame scheduling, caching, failure, and vertical-slice proof for Phase 6B. |
| `2026-07-17-phase-6b-completion-evidence.md` | Phase 6B public node, exact reduction math, full-frame execution/cache behavior, authored graph proof, tests, limitations, and stop decision. |
| `phase-6c-geometry-and-specialized-contract-v1.md` | Accepted Reformat sampling/extent contract and typed RAW, frequency, external, scope, preview, and export planning boundaries for the Phase 6 exit slice. |
| `2026-07-17-phase-6c-and-phase-6-completion-evidence.md` | Phase 6C implementation inventory, Reformat CPU/GPU evidence, specialized FFT round trip, full test/build results, and final Phase 6 exit audit. |
| `2026-07-15-forward-only-rewrite-decisions.md` | User-confirmed forward-only, permissive-graph, fusion-order, alpha, RAW, and viewport direction from the second planning intake. |
| `STACK_IMAGE_MATH_AND_PIPELINE_AUDIT as of 7-12-26.md` | Evidence-backed ground truth for the 2026-07-12 working tree. |
| `Stack_Image_Math_Research_Package/` | Architecture research, operation definitions, validation proposals, implementation mapping, and sources. |
| Root `.txt` files | Historical prompts, user-intent context, and early operation tables; not implementation authority. |

## Program Boundaries

This workstream owns:

- the future node-definition contract;
- typed values and semantic image descriptors;
- validation and diagnostics for mathematically or semantically suspicious
  connections;
- the authored-graph, semantic-IR, and physical-plan separation;
- pointwise fusion and the resource rules needed to make primitives affordable;
- versioned compound definitions and inspectable optimized equivalents;
- mathematical reference tests and forward version behavior; and
- the deliberate expansion or correction of the public node library after the
  foundations exist.

It does not treat the following as first-milestone scope:

- a mass addition of equation-table nodes;
- preserving or migrating pre-rewrite project files;
- a wholesale renderer rewrite without staged verification;
- automatic repair of every unusual graph connection;
- generative, semantic, or general AI workflow design; or
- unrelated editor features that happen to use nodes.

## Working Rules

- No implementation phase becomes active implicitly. Record activation and the
  exact slice in `implementation-progress.md` first.
- Keep phases small. A phase can contain many buildable, reviewable passes.
- Pre-rewrite project compatibility is out of scope. The archived pre-rewrite
  executable is the way to open those projects.
- Test the behavior Stack is meant to have after each change. Prefer generated
  numeric vectors, small in-memory cases, automated checks, and user visual
  review over a stored corpus of old projects and images.
- Do not change pixels accidentally. Pixel-changing work must match the newly
  documented formula or algorithm and its forward version identity.
- Treat `Unknown` as an explicit semantic state, not as a match and not as a
  reason to guess.
- Retain an embedded source color profile as descriptive metadata without
  changing pixels. An untagged ordinary image enters the graph as `Unknown`
  until the user explicitly assigns a meaning or performs a conversion.
- Do not impose a hidden graph-wide working color space. Color state belongs to
  each source, wire, or explicit conversion, and users may place numeric and
  conversion nodes in any order they choose.
- Ordinary numeric operations remain plug-and-play. A color, alpha, range, or
  display mismatch normally produces information or a warning, not a blocked
  connection and not an automatic correction.
- Preserve independent access to R, G, B, and A. Support straight and
  premultiplied alpha through explicit state and conversion rather than forcing
  one representation everywhere.
- Fusion must preserve the authored dependency and operation order. It may not
  turn `Brightness -> Contrast` into `Contrast -> Brightness` or reorder any
  other noncommutative operations.
- The viewport displays the graph's connected output directly. It has no
  separate preview transform; the viewport footer shows the connected output's
  current color state so unusual or clipped results remain understandable.
- RAW nodes remain specialized and nondeconstructible by default. Revisit
  individual RAW nodes only after the main rewrite, case by case.
- Keep semantic meaning separate from GPU texture format and execution schedule.
- Keep visual groups separate from executable compounds.
- Keep friendly names separate from exact technical definitions.
- Every completed implementation slice records automated checks, generated
  cases where useful, user visual-review status, build results, known
  limitations, and the next stop point.
- Update only the document that owns a fact. Do not duplicate live status across
  the research package.

## Immediate Recommendation

Phase 6 is complete and the workstream is stopped with no active slice. Before
more implementation, explicitly activate Phase 7 and select a deliberately
small product-facing node/compound set. Do not treat Reformat, Field Mean, or
the specialized planning contracts as authorization to import the operation
tables wholesale. The earlier native graph-UI checks remain available in
`phase-5b-visual-test-checklist.md` as a separate human follow-up.
