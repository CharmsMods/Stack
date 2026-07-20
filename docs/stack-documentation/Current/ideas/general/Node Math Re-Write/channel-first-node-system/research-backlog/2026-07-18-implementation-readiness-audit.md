# Channel-First Implementation Readiness Audit

- Captured: 2026-07-18 17:27
- Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-18-1727-implementation-readiness-audit.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-18-1801-output-scenarios-and-explicit-alpha.md`
- Follow-up Source: `docs/stack-documentation/Archived/source-notes/2026/2026-07-19-1332-channel-output-export-and-alpha-parity.md`
- Type: research
- Topic: implementation-readiness
- Verification: verified against current Node Math Rewrite authority documents, current code, and focused tests

## Question

Has the channel-first discussion answered enough product questions to begin
implementation work without asking an implementer to invent missing behavior?

## Short Verdict

**Yes, for two bounded slices. No, for the complete channel-first system.**

The current documentation is sufficient to activate:

1. the already prepared signed Divide correctness fix; and
2. a presentation-only Channel/Mask vocabulary slice that changes user-facing
   descriptions without changing graph types, saved data, connections, or
   pixels.

The standalone Neutral Channel preview, durable role propagation, manual role
assignment, general `Value/Channel` inputs, alpha participation defaults,
source dissolve behavior, color workflow, and Phase 7 library unification are
not yet complete implementation contracts.

This audit authorizes no code changes. Node Math Rewrite currently has no active
implementation slice, and Phase 7 remains inactive.

## Current Authority Boundary

`implementation-progress.md` says Phase 6 is complete, there is no active
slice, Divide remains outside the completed scope, and Phase 7 requires an
explicit product-selection pass. The phase roadmap requires every Phase 7
definition to specify formulas, types, descriptor effects, execution class,
tests, implementation mapping, and forward-version notes.

That means a small correction may be activated independently, but the broad
node-library expansion must not begin merely because the architectural
foundation exists.

## What Current Stack Already Has

Several useful foundations are real rather than hypothetical:

- `ValueDescriptor` schema v2 already separates logical type, channel layout,
  numeric range, units, color, transfer, alpha, spatial state, sampling,
  precision, and provenance.
- Socket definitions already carry `semanticRoleKey`, declared channels, and
  declared units for connection presentation.
- Channel Split exposes R/G/B/A `ScalarField` outputs. Channel Combine exposes
  four R/G/B/A `ScalarField` inputs and one Image output.
- The Channel Combine shader already uses zero for an absent R, G, or B input
  and opaque `1.0` for absent alpha.
- The Output connection normalization currently routes a split component to
  its matching component socket. A red Channel dropped on Output becomes the R
  input; it does **not** yet implement the newly desired Neutral grayscale
  default.
- The definition model already has broadcast policy metadata, and the
  first-class value layer can explicitly represent a uniform-to-field
  broadcast. This is a foundation, not a general live `Value/Channel` port.
- Exact compounds, Create Compound From Selection, Make Unique, and Unpack are
  implemented. General decomposition of ordinary library nodes is not.

These facts explain why the next work can be incremental. They also explain why
renaming or merging types casually would be unsafe: current execution and tests
still distinguish `Mask`, `ScalarField`, and `Image` in multiple layers.

## Ready To Activate

### 1. Signed Divide Correction

**Readiness: ready.**

The current production shader still calculates:

```glsl
result = a / max(abs(b), vec4(0.00001));
```

This loses the sign of a negative denominator. The existing implementation
packet defines the sign-preserving epsilon behavior, required cases,
definition/version consequence, focused GPU/CPU evidence, build verification,
and explicit non-goals.

Starting this work requires an explicit activation entry in
`implementation-progress.md`, but it does not require another product-design
conversation.

### 2. Presentation-Only Channel Vocabulary

**Readiness: ready if kept to the boundary below.**

The guided discussion has established enough language to implement this exact
non-pixel slice:

- normal mask pins continue to say **Mask**;
- detailed mask presentation says **Channel · Mask**;
- user-facing descriptions of `ScalarField` say **Channel**;
- internal names such as `LogicalValueType::ScalarField`,
  `SocketType::ScalarField`, and mathematical `scalar` may remain unchanged;
- no logical types are merged;
- no saved graph schema, compatibility, renderer behavior, role propagation,
  or pixels change; and
- focused presentation/catalog tests plus the existing native visual checklist
  verify the result.

Current code centralizes relevant detailed vocabulary in
`SocketPresentation.h`, where `ScalarField` still displays as “Scalar field”
and Mask displays as a separate logical name. This makes a narrow presentation
slice feasible without pretending the larger semantic migration is complete.

## Nearly Ready, But Still Missing A Contract

### Image Combine And Standalone Channel Output

The desired experience is substantially understood, but implementation still
needs exact answers for:

- whether missing R/G/B is an execution failure, a non-blocking incomplete-node
  state, or a legal partial Image with zero-filled components;
- whether a standalone Channel dropped on Output creates three contextual
  connections, one neutral presentation connection, or an explicit viewing
  boundary;
- whether `View Channel As` is stored on Output, on a connection, in viewport
  state, or in some combination; and
- how the new viewing behavior coexists with the adopted direct-viewport rule
  and saved-project reproducibility.

Current behavior is concrete but different: all four Channel Combine inputs are
marked optional, absent RGB becomes zero, absent alpha becomes one, and a split
R Channel dropped on Output normalizes to the R socket.

The first guided contract-closure round was issued on 2026-07-18. It narrows
this section to two decisions: Output-owned versus viewport/connection-owned
single-Channel interpretation, and invalid missing RGB versus explicit
constant component defaults. The owning question text is in
`../discussion-topics/2026-07-17-channel-first-data-model-and-language.md`.

The first response changes the second decision rather than selecting either
earlier option. Unconnected color sockets should mean semantic Channel absence,
while fixed-component execution may read absent color as zero. Missing alpha
should be supplied by a visible, auto-connected, reusable opaque Alpha Channel.
This is not implementation-ready until Stack defines the authored type of a
partial component bundle, the Constant Channel extent contract and automatic
creation timing, and the downstream compatibility rules that distinguish
absent from present-zero Channels. Output interpretation ownership is also
still awaiting the user's answer after a scenario comparison.

The 2026-07-19 response closes the broad viewing-versus-construction direction:
standalone Channel viewing may use Neutral grayscale, but RGB/RGBA export does
not inherit that inspection mapping. A grayscale RGB result requires the same
Channel to be authored into R, G, and B. Before implementation, the remaining
Output contract must say exactly what the export command does when Output is
only inspecting one Channel, and the graph/UI must expose distinct inspection
and component-construction boundaries without ambiguity.

### Durable Channel Roles

The user experience is directionally settled: roles affect downstream meaning
and UI, obvious operations preserve them, and ambiguous math should not annoy
the user. Implementation still needs:

- the descriptor schema shape for semantic purpose versus component origin,
  units/range, and preview interpretation;
- exact propagation rules for unary, matching-role, and mixed-role operations;
- manual role-assignment ownership, persistence, versioning, and branch scope;
- connection compatibility and diagnostic consequences; and
- one selected vertical slice with exact tests.

Current code has socket role keys and channel-role strings, but no manual Set
Channel Role operation or persistent general purpose field. Generic arithmetic
currently preserves one input descriptor; it does not implement the proposed
binary role-conflict policy.

### General `Value/Channel` Inputs

The mathematical rule and optimization are understood. The underlying model
supports declared broadcasts. What remains undecided is which public ports
accept both forms, how that overload appears and serializes, how spatial extent
is selected, and which compiler/runtime paths implement it. It is ready for a
focused contract pass, not broad implementation.

## Not Ready For Implementation

The following still require significant product decisions or research:

- merging Mask and ScalarField into one underlying public Channel system;
- the graph-wide/per-node alpha participation default;
- automatic or visible source color preparation and the complete display path;
- imported-image `Dissolve Into Channels` graph mutation details;
- classification and decomposition of existing high-level/legacy nodes;
- broad legacy-library migration; and
- Phase 7 browser families and first public primitive/compound selection.

## Recommended Work Order

1. Activate and implement the signed Divide correction.
2. Activate the non-pixel Channel/Mask presentation vocabulary slice.
3. Finish one contract for standalone Channel Output plus Image Combine
   completeness and persistence.
4. Implement one durable role vertical slice, preferably Neutral and Mask only,
   before adding Alpha, component-origin, EV, depth, or other roles.
5. Select a deliberately small Phase 7 public node set only after those graph
   rules are stable.

This order produces real progress without committing Stack to an unfinished
role schema or broad library migration.

## Evidence Map

- `../../implementation-progress.md`
- `../../program-contract.md`
- `../../decision-register.md`
- `../../phase-roadmap.md`
- `../../phase-1-contract-v1.md`
- `../../phase-5b-typed-connection-ui-contract.md`
- `../implementation-ready/2026-07-17-signed-divide-correction.md`
- `../discussion-topics/2026-07-17-channel-first-data-model-and-language.md`
- `../discussion-topics/2026-07-17-graph-rules-and-visual-feedback.md`
- `src/NodeMath/ContractTypes.h`
- `src/NodeMath/DescriptorPropagation.cpp`
- `src/NodeMath/FirstClassValue.cpp`
- `src/NodeMath/NodeDefinition.h`
- `src/Editor/NodeGraph/NodeGraphTypes.h`
- `src/Editor/NodeGraph/SocketPresentation.h`
- `src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp`
- `src/Renderer/Internal/RenderPipelinePrograms.cpp`
- `tools/graph_behavior_tests.cpp`
