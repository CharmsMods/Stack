# Channel System Readiness — 2026-07-20

- Updated: 2026-07-20
- Product authority: `../../02-channel-based-design/accepted-product-direction.md`
- Open contract queue: `../../02-channel-based-design/work-still-to-define.md`
- Verification: current code, Node Math Rewrite contracts, and focused test sources inspected on 2026-07-20

This is a dated readiness snapshot. The detailed progress log remains the only
implementation activation/status ledger.

## Verdict

One bounded slice—the signed Divide correction—has a complete standalone
implementation packet. A presentation-only Channel/Mask vocabulary slice is
small enough to package next, but it does not yet have an equal standalone
packet. The accepted broader design is ready for contract authoring, not broad
implementation. Phase 7 is inactive and this document activates no code.

## Verified Current Code

- Channel Split exposes R/G/B/A as `ScalarField` outputs.
- Channel Combine exposes optional R/G/B/A `ScalarField` inputs and one Image
  output.
- Its shader substitutes numeric zero for missing R/G/B and one for missing A;
  it does not publish semantic present-component information.
- Current Output connection normalization routes an extracted component to a
  matching component socket rather than the accepted standalone Neutral
  inspection boundary.
- Socket presentation still calls `ScalarField` “Scalar field”; current runtime
  types still distinguish Mask, ScalarField, and Image.
- The value/definition layer contains broadcast metadata and first-class value
  infrastructure, but not general live `Value/Channel` ports.
- Exact compounds, Make Unique, and Unpack exist; general decomposition of
  ordinary library nodes does not.
- Production Divide still uses `a / max(abs(b), epsilon)` and loses the sign of
  a negative denominator.

## Packet Complete, Still Not Activated

### Signed Divide Correction

The implementation packet in
`../ready-but-not-started/fix-negative-division.md` defines the
sign-preserving guard, version consequence, focused cases, and verification.
It still requires activation in the parent implementation tracker.

## Ready To Turn Into A Bounded Packet

### Presentation-Only Vocabulary

A non-pixel slice may:

- present `ScalarField` as **Channel** in user-facing descriptions;
- keep normal mask pins labeled **Mask**;
- show **Channel · Mask** in detailed presentation; and
- leave runtime types, persistence, compatibility, descriptors, and pixels
  unchanged.

This boundary must not be described as the completed semantic migration.

## Ready For Contract Authoring

### Output, Partial Image, And Constant Alpha

The product behavior is accepted. C1–C3 in `../../02-channel-based-design/work-still-to-define.md` must turn it into
one versioned vertical contract covering descriptors, UI, persistence,
execution, diagnostics, undo/redo, and tests before source edits begin.

### Durable Channel Roles

The propagation direction and manual Set Channel Role boundary are accepted.
C4 must define exact fields, first roles, persistence, and tests.

### Pointwise `Value/Channel` And Alpha Participation

The broadcast and creation-default policies are accepted. C5–C6 must select the
first nodes and specify UI, serialization, spatial behavior, and lowering.

## Research Or Later Contract Required

- imported-source dissolution (C7);
- compound interaction and decomposition (C8 and R3);
- color-management and monitor presentation (R1);
- broad legacy-library migration and Phase 7 node selection (R4).

## Required Work Order

1. Activate only a complete bounded packet when the user requests implementation.
2. Write and approve the combined C1–C3 Output/Image Combine contract.
3. Complete C4 before relying on durable roles throughout the library.
4. Complete C5–C6 before publishing general overloads or alpha-participation UI.
5. Continue through C7, C8, R1, and R4 in dependency order.

## Evidence Owners

- Parent status and activation: `../detailed-progress-log.md`
- Program invariants: `../../03-technical-contracts/program-goals-and-rules.md`
- Adopted decisions: `../../01-start-here/decision-log.md`
- Descriptor contract: `../../03-technical-contracts/node-and-data/node-data-and-definition-contract-v1.md`
- Typed connections: `../../03-technical-contracts/graph-connections/compound-output-and-connection-ui-contract.md`
- Region/extent behavior: `../../03-technical-contracts/regions-reductions-and-specialized-processing/image-region-and-tiling-contract-v1.md`
- Current definitions: `src/Editor/NodeGraph/EditorNodeGraphDefinitions.cpp`
- Current presentation: `src/Editor/NodeGraph/SocketPresentation.h`
- Current shaders: `src/Renderer/Internal/RenderPipelinePrograms.cpp`
- Current graph evidence: `tools/graph_behavior_tests.cpp`
