# Phase 5 Completion Evidence

- Phase: 5 - Executable Compound Definitions
- Completed: 2026-07-16
- Status: implementation and automated gates complete; native compound-UI
  presentation recorded as a follow-up
- Compatibility policy: forward-only; no old-project migration, fixture
  corpus, or historical-output emulation was added
- Next phase status: Phase 6 is pending and was not started

## Correction Recorded By Phase 5B

The original live validator below constructed an already expanded renderer
graph. It proved canonical-versus-fused numerical equivalence but did not prove
that the authored graph's output-chain traversal recognized a compound. Native
testing found that omission: a chain containing a compound produced no viewport
output until Unpack.

Phase 5B repaired authored output/reference traversal and replaced the live
fixture with the real `Image -> Add, Then Multiply -> Exposure, Then
Premultiply -> Output` authored graph. It now requires a nonzero texture before
Unpack and equivalence with the unpacked canonical graph. This correction does
not invalidate the numerical evidence below; it narrows what the first
validator actually proved. See `2026-07-16-phase-5b-completion-evidence.md`.

## Completion Decision

Phase 5 is complete. Stack now has real compound nodes: one visible node can
own a stable public interface while its saved definition contains an exact,
ordered internal graph. Compound instances can be saved, copied, nested,
updated deliberately, made unique, or unpacked without turning visual graph
groups into executable objects.

The renderer does not execute a compound as an unexplained new formula. It
expands resolved graph-defined compounds into a temporary graph copy before
building the render snapshot. The canonical internal node order is therefore
the result authority. Phase 4 may fuse a compatible expanded pointwise chain,
but it may not reorder its operations.

This phase did not add ROI/halo planning, neighborhood infrastructure,
reductions, broad public-node expansion, RAW decomposition, formula changes,
or hidden color/alpha/output behavior.

## Product Functionality Added

- The node browser includes two shipped reference compounds:
  `Add, Then Multiply` and `Exposure, Then Premultiply`.
- A graph selection can be turned into a custom compound when the selected
  node kinds and boundary sockets are supported by this first authoring slice.
- Compound nodes expose stable typed input/output sockets and promoted scalar
  controls instead of exposing every internal node.
- The node context menu provides Make Unique, Unpack, an editable unique-graph
  workflow, and an explicit update to a newer embedded version.
- Missing or invalid definitions remain visible as unresolved typed compound
  nodes. Their saved interface and existing graph links are retained, and
  rendering stops with an explanatory diagnostic rather than substituting a
  different definition.
- Visual graph groups keep their existing layout-only behavior.

## Definition, Instance, And Storage Contract

`src/NodeMath/CompoundDefinition.*` owns the Phase 5 schema and validation.
Each definition has:

- a stable definition UUID;
- an exact definition ID, semantic version, and SHA-256 content hash;
- a transparent, graph-defined optimized-equivalent, or opaque-specialized
  classification;
- stable typed public port and promoted-parameter IDs;
- bindings from those public IDs to stable internal node UUIDs and socket or
  parameter IDs;
- exact compound dependencies;
- a canonical internal graph; and
- optimization tolerance/evidence when an optimized-equivalent path is
  declared.

Each instance has its own UUID, exact pinned definition reference, promoted
parameter overrides, resolution state, and saved interface snapshot. Project
graph JSON version 6 serializes definitions and instances. Selection copy carries
the exact transitive definition closure needed by copied compounds, and pasted
nodes receive new instance UUIDs.

There are no version ranges, automatic latest-version changes, or fallback to
similarly named definitions. Absent, mismatched, invalid, and recursive exact
dependencies remain unresolved. Adding a newer embedded definition does not
move an existing instance; an explicit interface-checked update is required.

## Authoring And Lifecycle Behavior

- **Create from selection** captures the canonical selected subgraph, supported
  typed boundary ports, exact nested compound dependencies, and selected
  promotable scalar controls.
- **Make Unique** copies the exact definition and canonical math to a new
  definition family at version `1.0.0`, then rebinds only the selected node.
- **Unpack** replaces one resolved, unpackable compound with fresh canonical
  internal nodes, applies promoted overrides, and reconnects external links by
  stable port ID. Nested child compounds remain compounds until separately
  unpacked.
- **Definition editing** has a core API that creates a deliberate new exact
  version without mutating older definitions. The current UI's editing route
  makes the instance unique and unpacks it for ordinary graph editing; a
  dedicated nested definition editor is not part of this phase.
- **Explicit update** rebinds one instance only after connected public port
  types and stored promoted overrides remain compatible.

## Execution And Equivalence

`Graph::ExpandAllCompoundNodes` creates the temporary executable graph. It
records the outer compound instance and the ordered internal node identities
in its expansion result for diagnostics and tests; the renderer snapshot uses
the ordered internal identities after expansion. The saved authored graph is
not destructively changed.

The optimized-equivalent reference compound is exactly:

```text
Image -> Exposure EV -> Premultiply -> Image
```

Its canonical materialized form and its Phase 4 fused form were compared to
the same CPU math. The accepted absolute tolerance is `2.5e-3`, matching the
current RGBA16F live-render boundary. The optimized path reported authored node
order `[Exposure, Premultiply]`; no fixed or reordered operation sequence was
introduced.

Image source payloads use shared immutable pixel storage while compound
expansion copies graph structure, avoiding a full CPU image-buffer copy solely
because a compound is lowered.

## RAW And Opaque Specialized Boundaries

The unified live definition registry now marks RAW operations as opaque
specialized definitions. They are not graph-defined compounds, cannot be
automatically unpacked by this system, and were not rewritten or decomposed in
Phase 5. Any future review of individual RAW nodes remains case-by-case work
after the main rewrite foundation.

## Automated Evidence

### Focused compound contract checks

```powershell
.\build\StackNodeMathPhase5Tests.exe
```

Result: passed, 21 checks. Coverage includes exact hash identity, definition
and instance serialization, stable interfaces, promoted overrides, missing/
version/hash resolution failures, transitive dependency closure, Make Unique,
deliberate edited versions, optimized-equivalent evidence requirements,
opaque non-unpackable definitions, and recursive or hash-inconsistent catalog
rejection.

### Graph behavior checks

```powershell
.\build\StackGraphBehaviorTests.exe
```

Result: passed. Phase 5 coverage includes shipped template validity, RAW
opacity, typed public connectivity, exact resolution, save/load, copied
definition closure, canonical expansion and authored order, nested compounds,
group preservation, Make Unique, deliberate version update, one-level Unpack,
missing-definition shell/link retention, invalid-definition failure,
optimized-equivalent expansion, scalar boundary typing, and shared image
payloads during expansion.

### Live optimized-equivalence validation

```powershell
.\build\Stack.exe --validate-node-math-phase5
```

Result: passed in a hidden OpenGL 4.3 context. Canonical CPU, materialized
canonical GPU, and optimized-equivalent fused GPU results agreed within
`2.5e-3`. The optimized form reported one fused group containing both nodes in
the exact `Exposure -> Premultiply` order; the forced canonical comparison did
not fuse.

### Registered Node Math tests

```powershell
ctest --test-dir build -C Release --output-on-failure -R StackNodeMath
```

Result: 9/9 passed:

- `StackNodeMathReference.Cpu`
- `StackNodeMathReference.Gpu`
- `StackNodeMathContract.Phase1`
- `StackNodeMathSemantic.Phase2`
- `StackNodeMathValues.Phase3`
- `StackNodeMathPointwise.Phase4`
- `StackNodeMathCompounds.Phase5`
- `StackNodeMathPointwise.LiveGpu`
- `StackNodeMathCompounds.LiveGpu`

### Wider application and build gates

```powershell
.\build\Stack.exe --validate-layer-registry
.\build.cmd
```

Results: layer-registry validation passed and the repository-preferred Windows
build completed, producing `build\Stack.exe` and all validation targets.

## Pixel, Project, And UI Assessment

- Compound math is the exact math of its canonical internal nodes. No existing
  public formula was corrected or redefined.
- The optimized-equivalent example can change physical pass scheduling and
  intermediate rounding only within its declared tolerance.
- Project graph serialization advances to schema 6 for exact embedded compound
  definitions and instances. Pre-rewrite compatibility remains out of scope.
- Copy/paste and project save/load can carry compound definitions; this is an
  intentional project-format change.
- The browser, compound node body, promoted controls, context actions, and
  unresolved-state presentation are product-visible UI additions.
- Native human confirmation is still needed for node sizing, labels, promoted
  controls, context-action wording, unresolved diagnostics, and browser
  presentation in real projects. Automation makes no visual-polish claim.

## Known Limits And Follow-Up

- Create from selection currently supports the vetted Data Math, Technical
  Image, and nested graph-compound slice. It is not a universal wrapper for
  every legacy or specialized node.
- Promoted controls in the current authoring/UI slice are scalar parameters.
  The underlying contract can describe the accepted parameter types, but a
  general promotion editor was not added.
- The optimized-equivalent example uses Phase 4's proven fusion of its
  canonical graph. Phase 5 did not add a separate hand-written shortcut.
- Shipped compound browser previews use the existing fallback preview path.
- There is no machine-wide mutable compound library, external dependency
  discovery, automatic update, or version-range resolver.
- There is no dedicated in-place nested compound-definition editor yet; the
  safe UI route is Make Unique plus Unpack, edit ordinary nodes, then create a
  new compound if desired.
- ROI, halo, border, neighborhood, geometry, reduction, specialized-stage,
  proxy/tile, and broader public-library work remains Phase 6 or Phase 7.

## Exit Decision And Stop

Every Phase 5 required deliverable and automated exit condition is satisfied.
The transparent and optimized-equivalent reference compounds survive the
required persistence, copy, nesting, deliberate version, missing dependency,
Make Unique, and Unpack cases. Their canonical order remains authoritative,
their optimized execution is tolerance-checked, RAW remains opaque, and graph
groups remain separate.

This Phase 5A pass stopped here. Phase 5B subsequently corrected authored
compound traversal and connection presentation as recorded at the top of this
file. Phase 6 was not started and remains separately unauthorized.
