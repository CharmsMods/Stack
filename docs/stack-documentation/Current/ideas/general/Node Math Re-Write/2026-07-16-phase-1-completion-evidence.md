# Phase 1 Semantic And Node-Definition Contracts - Completion Evidence

- Date: 2026-07-16
- Phase: 1
- Slice: Phase 1A
- Result: complete
- Pixel/UI/current-project impact: none
- Next phase: Phase 2 is eligible but not active

## Outcome

Phase 1 turned the accepted planning contract into a compiled, validated model
that later phases can integrate. It defines what graph values mean, what a node
definition must promise, how exact identities work, how descriptors propagate
through representative operations, how diagnostics are classified, and how the
new forward-only graph generation resolves exact definitions.

This phase deliberately did not connect the model to Stack's current graph or
renderer. It changes no current node formula, image, viewport behavior, export,
project file, or UI. It makes those future changes definable and testable.

## Implemented Source Boundary

New isolated contract owners:

- `src/NodeMath/ContractTypes.h/.cpp`
  - logical value family;
  - semantic descriptor fields and applicability;
  - `Known`, `Unknown`, and `NotApplicable` states;
  - structural versus strict semantic comparison;
  - ID, semantic-version, UUID, and SHA-256 validation;
  - portable SHA-256 content identity; and
  - stable four-stage diagnostic registry and acknowledgement policy.
- `src/NodeMath/NodeDefinition.h/.cpp`
  - definition, port, parameter, instance, connection, and implementation
    contracts;
  - explicit numerical/alpha/spatial/metadata policies;
  - external field-disposition completeness;
  - canonical length-delimited definition and implementation content;
  - content-derived exact SHA-256 validation; and
  - 15 representative definitions covering the current major paths.
- `src/NodeMath/DescriptorPropagation.h/.cpp`
  - identity, generic arithmetic, exposure, mask, geometry, color transform,
    composite, reduction, direct output, and unknown/external propagation;
  - permissive warnings for numerically defined unusual work; and
  - hard failure only where the declared operation lacks execution-critical
    meaning or a structurally required policy.
- `src/NodeMath/ProjectSchema.h/.cpp`
  - `stack.node-graph` generation 1/schema 1 envelope;
  - exact definition manifest;
  - explicit unsupported-pre-rewrite, unsupported-generation, and malformed
    results; and
  - exact/missing-ID/version-mismatch/hash-mismatch resolution.
- `tools/node_math_contract_tests.cpp`
  - 143 focused contract, invariant, propagation, identity, definition,
    diagnostic, JSON round-trip, and resolver checks.
- `CMakeLists.txt` and `cmake/StackSources.cmake`
  - additive `StackNodeMathContractTests` target and
    `StackNodeMathContract.Phase1` CTest registration.

The normal application source glob compiles the new source, which proves it is
compatible with Stack's build. No existing production owner includes or calls
the new contract APIs.

## Roadmap Deliverable Coverage

| Phase 1 requirement | Evidence | Result |
| --- | --- | --- |
| One canonical logical-value and image-descriptor schema | `phase-1-contract-v1.md`; `ContractTypes.*`; descriptor applicability tests across every v1 logical type | Complete |
| Mandatory, derived, unknown, and deferred meaning without hidden defaults | Explicit state on every applicable field; strict match rejects Unknown-as-equality; deferred breadth listed in the canonical contract | Complete |
| One canonical definition/instance/implementation schema | `NodeDefinition.*`; definition/instance/connection validators; canonical content hashes | Complete |
| Representative descriptor propagation | `DescriptorPropagation.*`; source/identity/math/exposure/mask/geometry/color/composite/reduction/output/external tests | Complete |
| New project generation and exact-version resolution | `ProjectSchema.*`; generation-1 JSON round trip and four exact resolver outcomes | Complete |
| Unsupported pre-rewrite behavior and forward envelope | Explicit `UnsupportedPreRewrite`, `UnsupportedGeneration`, `Malformed`, exact resolution, and unresolved-without-fallback policy | Complete |
| Stable diagnostic IDs and staged analysis | Unique registry across Connection, Semantic, Lowering, Runtime; severity and acknowledgement tests | Complete |
| Property/reference test plan | Canonical contract verification plan plus compiled 143-check suite and retained Phase 0 CPU/GPU oracle | Complete |

## Current Major-Path Representation

The representative catalog proves that the schema can describe current paths
without claiming current coarse sockets already carry this meaning:

| Current path | Representative definition or contract |
| --- | --- |
| Ordinary image source | `stack:image/source`; tagged and untagged source descriptor factories |
| RAW source and development | `stack:raw/source`, `stack:raw/develop`; opaque specialized classification |
| Identity | `stack:image/identity` |
| Generic image/scalar math | `stack:math/multiply` |
| Exposure-style math | `stack:color/exposure`; recommended linear state warns rather than blocks |
| Mask/channel-style output | `stack:mask/luminance`; explicit noncolor Mask output |
| Geometry/resampling | `stack:geometry/resample`; explicit spatial and sampling replacement |
| Explicit color conversion | `stack:color/transform`; requires known source meaning because conversion is otherwise undefined |
| Alpha compositing | `stack:composite/source-over`; exact straight-alpha formula and explicit extent policy |
| Reduction/analysis values | `stack:analysis/mean`; typed Vector3 output |
| Viewport/export boundary | `stack:output/direct`; preserves the graph result with no preview transform or repair |
| Frequency work | `stack:frequency/fft`; ComplexSpectrum output |
| Multi-image work | `stack:image/merge-many`; variadic inputs with saved authored order |
| CPU/model/external work | `stack:external/model-process`; complete explicit descriptor-field disposition |

Dedicated temporal, flow, depth, coordinate-field, collection, arbitrary
channel-set, physical-resource, ROI/halo, and scheduling breadth remains
deferred. V1 represents existing boundaries through typed images, metadata,
repeated ports, parameters, and capability classification instead of inventing
unsupported meaning.

## User-Direction Checks

- No legacy project compatibility or historical pixel corpus was added.
- Embedded source profile information is attached descriptively without
  converting samples.
- Untagged sources stay `Unknown`; no hidden sRGB or working-space default is
  introduced.
- The graph remains plug-and-play for defined numeric work. Exposure on known
  nonlinear or unknown transfer data produces a warning and remains executable.
- An explicit color-transform node blocks only when it cannot know the source
  meaning required to perform the requested conversion.
- Straight and premultiplied alpha remain explicit states. The representative
  Source Over formula rejects a mismatched alpha representation rather than
  silently changing channels.
- Direct output preserves the connected graph result. No optional preview
  transform, tone map, normalization, clamp, gamut map, or repair was added.
- RAW definitions are opaque specialized operators, not deconstructible
  compounds.
- Authored order remains part of every definition's explicit operation-order
  policy, and the Phase 0 noncommutative CPU/GPU cases remain passing.

## Exact Identity Evidence

- Definition IDs use lowercase `<namespace>:<path>` grammar.
- Port, parameter, implementation, operation, and diagnostic IDs use stable
  scoped-token grammar.
- Projects and instances use canonical lowercase UUID form.
- Versions are exact numeric `major.minor.patch` values; ranges and prerelease
  syntax are outside v1.
- Content identity uses SHA-256. The implementation matches the standard `abc`
  vector.
- Definition SHA-256 is computed over canonical v1 content excluding only the
  definition hash itself.
- Implementation SHA-256 is independently computed over canonical v1
  implementation-contract content, including its owning-definition binding.
- Tests prove that changing definition semantics or an implementation tolerance
  invalidates the old identity, and that both identities validate again only
  after deliberate rehashing.
- Projects pin exact ID/version/hash. Missing ID, wrong version, and wrong hash
  never fall back to the latest available definition.

## Verification Evidence

All commands ran from the repository root on 2026-07-16.

| Command | Result |
| --- | --- |
| `cmake --build build --config Release --target StackNodeMathContractTests` | Passed |
| `build/StackNodeMathContractTests.exe` | Passed: 143 Phase 1 checks |
| `ctest --test-dir build -C Release -R "StackNodeMath" --output-on-failure` | Passed: 3/3 (`Reference.Cpu`, `Reference.Gpu`, `Contract.Phase1`) |
| `cmake --build build --config Release --target StackGraphBehaviorTests` | Passed |
| `build/StackGraphBehaviorTests.exe` | Passed |
| `build/Stack.exe --validate-layer-registry` | Passed |
| `build.cmd` | Passed; `Stack.exe` and all configured validation targets built |
| `git diff --check` on the Phase 1 source/build scope | Passed; only existing line-ending notices from tracked CMake files |

User visual review is not applicable: the phase has no live UI or pixel path.
Phase 2 must add user review when descriptors, diagnostics, and technical image
boundaries become visible or affect rendering.

## Exit-Gate Assessment

1. Every current major path is representable without inventing meaning.
   Unknown evidence stays `Unknown`, and specialized work has explicit opaque
   typed boundaries.
2. V1 is smaller than the maximal research catalogs. Deferred types and fields
   can be added in a future schema generation without changing the accepted ID,
   version, or exact-resolution model.
3. No blocking choice is hidden in a default. Color, transfer, reference,
   alpha, range, precision, spatial state, sampling, units, provenance,
   operation order, extent policy, and external field effects are explicit or
   explicitly Unknown/NotApplicable.

All Phase 1 deliverables and exit conditions pass. Phase 1 is complete.

## Stop Boundary

Phase 2 is now eligible for a new bounded implementation pass. It is not active
and was not started here. In particular, this pass did not:

- attach descriptors to current graph sockets or payloads;
- change current graph validation or diagnostic UI;
- alter current MSTK or graph JSON saving/loading;
- change renderer caches, passes, texture formats, or pixels;
- add color/alpha conversion nodes to the live product;
- change viewport/footer/export behavior; or
- begin semantic IR, fusion, compound, region, or public-library work.
