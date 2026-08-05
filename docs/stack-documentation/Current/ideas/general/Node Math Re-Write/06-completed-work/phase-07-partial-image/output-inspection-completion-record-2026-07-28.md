# Phase 7B2 Output Inspection Completion Evidence

> **Historical completion record:** This file describes the state when this
> bounded slice ended. Current status and current code/tests supersede its
> point-in-time language.

- Date: 2026-07-28
- Phase 7B2 result: complete
- Program state after this pass: Phase 7B3 Constant Alpha transaction active
- Accepted decision: NMR-142
- Contract: `../../03-technical-contracts/channel-system/partial-image-output-and-constant-alpha-contract-v1.md`

## Result

Output definition v2 has one stable `imageIn` persistence ID presented as
`Result · Image or Channel`. It accepts only an exact Color Image or Channel.
The former hidden R/G/B/A construction inputs are gone from new graphs,
authoring rules, validation, traversal, renderer execution, and UI. Masks and
generic scalar values cannot masquerade as a Channel.

Output settings schema 1 saves Neutral, Red, Green, or Blue inspection.
For a Channel sample `c`, the viewport mapping is exactly:

```text
Neutral = (c, c, c, 1)
Red     = (c, 0, 0, 1)
Green   = (0, c, 0, 1)
Blue    = (0, 0, c, 1)
```

The mapping is renderer presentation only. The Channel descriptor, samples,
extent, and wire identity remain unchanged. Full Images continue through
Output unchanged.

PNG export is disabled in the Output UI and rejected before capture when the
connected result is a Channel. The diagnostic directs the user to Image
Combine; inspection never creates export components.

Graph schema 8 persists Output settings and exact definition v2 identity.
Schema-7 Output with one legacy component link migrates that link to
`imageIn` with Neutral inspection. Ambiguous multi-component legacy Output is
preserved losslessly as unresolved authored state until the user inserts an
explicit Image Combine. Layer synchronization no longer deletes those
preserved legacy links.

## Reliability And Performance Corrections

Connection validation initially performed scalar/channel provenance traversal
for every candidate link, including ordinary Image chains. The bridge now
checks the Output union target first, and general connection validation lazily
resolves scalar and LUT-channel provenance only for rules that need it. The
8,192-node and wide/shared graph behavior suite completes in 5.42 seconds in
the final Release gate; the regression build had consumed more than 334 CPU
seconds before this correction.

Older frequency live validators that assembled R/G/B/A directly on Output now
use either one direct Channel inspection link or an explicit Image Combine.
The heterogeneous multi-tree live test retains its split, Data Math,
FFT/IFFT, mask/geometry, reduction, cycle, and cache-switching coverage while
also checking all four Output inspection modes.

## Evidence

Focused CPU and graph checks prove:

- mode token parsing and exact four-mode mapping;
- exact Color Image/Channel acceptance and descriptor preservation;
- Mask rejection and hard Channel PNG rejection;
- Output definition v2's single union port;
- schema-8 Blue-mode save/load;
- schema-7 single-component migration to Neutral `imageIn`;
- schema-7 multi-component preservation as unresolved state; and
- full Image, direct Channel, typed frequency Channel, deep chain, and
  heterogeneous connection behavior.

Live GPU evidence proves Neutral, Red, Green, and Blue mappings with opaque
Alpha in one reusable multi-tree topology. It also preserves explicit
Image Combine reconstruction, frequency round-trip, cache reuse, independent
output switching, and selected-cycle failure isolation.

## Commands And Results

```powershell
.\build.cmd
.\build\StackNodeMathChannelImageTests.exe
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-node-math-phase6
ctest --test-dir build -C Release --output-on-failure
.\build\Stack.exe --validate-layer-registry
```

Results:

- repository-preferred Windows Release build: passed;
- focused Channel/Image suite: 87/87 passed;
- graph behavior and scale suite: passed in 5.42 seconds;
- live Phase 6/multi-tree GPU validation: passed;
- full CTest: 22/22 passed, including five GPU suites;
- layer-registry validation: passed; and
- targeted tracked/untracked whitespace checking: clean apart from line-ending
  conversion warnings.

Native visual review was not performed because this goal explicitly forbids
computer use. Output pin wording, mode control, union pin color, and disabled
export explanation remain manual visual QA items.

## Main Owners

- `src/NodeMath/OutputInspection.*`
- `src/NodeMath/TechnicalImageMath.cpp`
- `src/NodeMath/SemanticSpine.cpp`
- `src/NodeMath/NodeDefinition.*`
- `src/Editor/NodeGraph/NodeGraphTypes.h`
- `src/Editor/NodeGraph/Model/EditorNodeGraphConnectionRules.h`
- `src/Editor/NodeGraph/EditorNodeGraphSerializer.cpp`
- `src/Editor/NodeGraph/UnifiedNodeDefinitionRegistry.cpp`
- `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`
- `src/Editor/Internal/EditorModulePersistence.cpp`
- `src/Renderer/Internal/RenderPipelineGraphExecution.cpp`
- `tools/node_math_channel_image_tests.cpp`
- `tools/graph_behavior_tests.cpp`
- `src/App/Validation/Suites/NodeMathMultiTreeValidation.cpp`

## Stop Boundary

Phase 7B2 ends here. Constant Channel, automatic opaque Alpha creation,
transactional downstream connection, deletion suppression, restore/rematch,
undo/redo, persistence, and lazy Constant execution belong to Phase 7B3.
Temporary unsaved inspection override, durable Channel roles, C4-C8, and
unrelated Phase 7 work remain inactive.
