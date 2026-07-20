# Phase 3 Completion Evidence

- Phase: 3 — First-Class Values And Unified Definitions
- Completed: 2026-07-16
- Status: implementation and automated gates complete; native human visual
  confirmation recorded as a follow-up
- Compatibility policy: forward-only; no pre-rewrite project migration or
  historical-output emulation was added
- Next phase status: Phase 4 is pending, NMR-109 remains open, and no IR,
  fusion, constant-folding, or physical-resource work was started

## Completion Decision

Phase 3 is complete. Stack now has an exact serialized value envelope instead
of treating every non-image result as an image or mask texture, and it has one
validated live definition/parameter registry for layer-backed and explicit
catalog nodes. The selected production slice is deliberately small: per-pixel
channel data is a `ScalarField`, and a known uniform Scalar Value can drive the
existing Exposure EV operation.

This pass does not add a reduction scheduler, a large primitive library, or an
optimizer. It establishes the types and exact definitions those later systems
need. Pixels change only when the user explicitly connects a Scalar Value to
Exposure EV; all other Phase 3 changes affect graph meaning, validation,
persistence, or presentation.

## First-Class Value Contract

`src/NodeMath/FirstClassValue.*` owns the Phase 3 value envelope.

Every value records independent axes:

- logical type;
- storage class: `Uniform`, `PerPixelField`, `StructuredResource`, or
  `SpecializedHandle`;
- availability: `Known`, `Unknown`, `Missing`, or `Failure`;
- units; and
- the type-appropriate payload and explanatory message.

The schema supports exact envelopes for Boolean, integer, scalar, two/three/
four-component vectors, 3x3/4x4 matrices, 2D coordinates, curves, histograms,
statistics, metadata/resource references, and specialized handles. A value's
stable fingerprint is computed from its canonical JSON representation.

Unknown, Missing, and Failure are not aliases. Unknown means the value is not
known; Missing means required data is absent; Failure means an attempted
operation failed. None is silently replaced with zero or another guessed
payload.

The compiled rules also define:

- unit compatibility;
- explicit scalar broadcast eligibility;
- explicit uniform scalar broadcast;
- uniform vector component extraction; and
- uniform vector sum/mean/minimum/maximum reduction.

These rules do not automatically insert graph nodes. No per-pixel or global
reduction scheduler exists in this phase.

## Live Scalar And Field Slice

The graph now distinguishes these meanings:

- `Scalar` — one uniform number;
- `ScalarField` — one number at every pixel;
- `Mask` — a semantic mask/coverage value; and
- structured resource/handle socket types.

Channel Split outputs and Channel Combine inputs use `ScalarField`. Scalar
Average also uses `ScalarField`. Existing mask-as-scalar workflows retain
temporary compatibility where removal would require a broader staged rewrite;
that adapter is not a pre-rewrite persistence promise.

The node browser exposes selected uniform Value nodes:

- Boolean and Integer;
- Scalar;
- Vector 2, 3, and 4;
- Matrix 3x3 and 4x4;
- Coordinate; and
- Curve.

Known numeric uniform values have native node controls. The Curve node exposes
its current resource summary in this phase; a full public curve editor is a
later product/UI slice, not an implied Phase 3 primitive expansion.

## Scalar Value To Exposure EV

Exposure has an optional typed `Scalar` input named EV. Exact type matching is
required, so Vector-to-Scalar is rejected until a user places a future explicit
extraction or reduction node.

When a known uniform Scalar is connected:

1. the graph stores the typed link separately from image render links;
2. the renderer-snapshot boundary resolves the exact double value;
3. that value explicitly replaces the Exposure node's saved fallback EV; and
4. the existing Exposure implementation evaluates in its authored graph
   position.

The fallback slider is disabled while the typed input is connected and remains
saved for use after disconnection. Unknown, Missing, Failure, wrong-type, or
invalid payloads do not resolve as an execution value. Graph validation reports
an unavailable execution-critical connected value instead of guessing.

## Unified Live Definition Registry

`src/Editor/NodeGraph/UnifiedNodeDefinitionRegistry.*` is the Phase 3 live
owner for resolved definitions. It covers every layer descriptor, every
explicit browser entry, and internal project-facing source/specialized shells
needed by the current graph.

Each resolved definition owns:

- deterministic `stack:...` definition ID;
- semantic version `1.0.0`;
- SHA-256 content identity;
- graph kind and variant;
- browser label/category/preview policy;
- stable static port IDs, directions, types, labels, optionality, and
  visibility; and
- declarative parameter IDs, types, units or explicit unspecified state,
  defaults, numeric domains, UI hints, serialization ownership, animation
  policy, and storage key.

The browser and thumbnail system now obtain their catalog from this registry.
Static graph sockets also resolve from it. Data Math, MFSR, and dynamic channel
input presentation still expand a registry-defined base interface according to
live connection state; those runtime expansion rules are not duplicated as
independent definitions.

Layer definitions reuse the same parameter catalog used by timeline animation.
Granular animatable parameters are declared directly, and the complete layer
JSON is represented by a non-animatable metadata settings envelope so
specialized controls are not falsely flattened into generic scalars. Existing
hand-authored layer UI remains in place; Phase 3 makes its contract declarative
but does not replace every specialized panel with generated UI.

Registry validation checks definition IDs, hashes, kind/variant uniqueness,
port identities, parameter identities, and numeric domains. It runs in focused
graph tests and as part of `Stack.exe --validate-layer-registry`.

## Forward Persistence

Graph JSON version 5 adds an exact definition reference to every saved node:

```text
definition ID + semantic version + SHA-256 content hash
```

Value nodes serialize the complete first-class value envelope. On load:

- an exact installed definition resolves;
- an invalid value payload becomes a typed Missing value with an explanation;
- Unknown and Missing remain distinct through round trip;
- a missing ID/version/hash remains unresolved;
- an unknown ID, version mismatch, or hash mismatch remains unresolved; and
- graph validation reports the unresolved node instead of selecting a fallback
  definition.

The existing reader can still parse older graph documents and stamps the
current in-memory definition when possible, but this is not a promise to
preserve pre-rewrite files or output. Schema 5 is the forward exact-identity
boundary established by this phase.

## Automated Evidence

### Phase 3 focused value checks

```powershell
cmake --build build --config Release --target StackNodeMathPhase3Tests
.\build\StackNodeMathPhase3Tests.exe
```

Result: passed, 72 checks. Coverage includes representative exact round trips
and fingerprints for every selected value family, Known/Unknown/Missing/
Failure separation, invalid envelope rejection, unit compatibility, explicit
broadcast, component extraction, and uniform-vector reduction.

### Live graph, registry, and persistence checks

```powershell
cmake --build build --config Release --target StackGraphBehaviorTests
.\build\StackGraphBehaviorTests.exe
```

Result: passed. Phase 3 coverage includes:

- unified registry validation;
- layer definition identity and declarative parameter coverage;
- Channel Split `ScalarField` ports;
- Scalar Value → Exposure EV connection and renderer-facing value resolution;
- implicit Vector-to-Scalar rejection;
- graph schema 5 and exact definition identity round trip;
- Known, Unknown, and Missing value round trip;
- malformed Scalar payload recovery as typed Missing; and
- definition hash mismatch remaining unresolved and invalid.

### All registered Node Math tests

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Result: 5/5 passed:

- `StackNodeMathReference.Cpu`
- `StackNodeMathReference.Gpu`
- `StackNodeMathContract.Phase1`
- `StackNodeMathSemantic.Phase2`
- `StackNodeMathValues.Phase3`

### Application registry and build

```powershell
.\build\Stack.exe --validate-layer-registry
```

Result: passed, including the unified definition registry.

```powershell
cmake --build build --config Release --target Stack
```

Result: passed; `build/Stack.exe` linked successfully.

```powershell
.\build.cmd
```

Result: passed using the repository-preferred Windows build path.

## Pixel And Compatibility Assessment

- Existing formulas and render ordering were not changed.
- Channel and scalar-field type changes do not change pixels.
- Definition metadata and graph schema changes do not change pixels.
- The only new pixel-affecting path is explicit Scalar Value → Exposure EV.
- The connected scalar uses the existing Exposure formula in the exact
  authored graph position.
- No hidden broadcast, conversion, reduction, color transform, normalization,
  or output repair was added.
- No pre-rewrite migration, old-project fixture corpus, or historical-output
  guarantee was added.

## Known Limitations And Follow-Up

- Exposure EV is the only production node input bound to a uniform typed value
  in this phase. Other typed sockets are contract/persistence groundwork.
- Histogram, statistics, metadata, and specialized-handle envelopes are tested
  but are not yet a broad public executable node library.
- The current Curve Value UI is informational; a full curve editor and curve-
  consuming operation must be separately selected.
- Unit semantics are represented and tested, but the first Value-node UI does
  not yet expose a general unit selector.
- Runtime-expanded Data Math, MFSR, and dynamic channel ports still use focused
  graph rules on top of the registered base definition.
- Temporary Mask/scalar-field adapters remain for existing staged workflows.
- Native visual confirmation is still needed for browser entries, Value node
  sizing/controls, typed pin and wire appearance, Exposure fallback disabling,
  and an actual image rendered with connected/disconnected Scalar EV.

These limitations do not fail the Phase 3 exit gate. They bound what was
implemented and route later work without claiming a public primitive library
or an optimizer.

## Exit Decision And Stop

All Phase 3 required deliverables and automated exit conditions are satisfied:
the selected vertical slice no longer overloads Mask/Image, rewritten nodes
carry stable forward identities, and validation/serialization cover unknown,
missing, malformed, and mismatched states.

Phase 3 is closed. Phase 4 remains pending. NMR-109 must be resolved and a new
bounded pass must be explicitly activated before any semantic IR, fusion,
constant folding, resource planning, or execution-inspection work begins.
