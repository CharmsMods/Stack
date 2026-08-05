# Phase 7B1 Partial-Image Foundation Completion Evidence

> **Historical completion record:** This file describes the state when this
> bounded slice ended. Current status and current code/tests supersede its
> point-in-time language.

- Date: 2026-07-28
- Phase 7B1 result: complete
- Program state after this pass: Phase 7B2 Output inspection active
- Accepted decision: NMR-142
- Contract: `../../03-technical-contracts/channel-system/partial-image-output-and-constant-alpha-contract-v1.md`

## Result

Semantic descriptor schema 3 carries a dedicated four-bit R/G/B/A Image
component-presence set. Its JSON is a readable duplicate-free array in
canonical order. The field participates in equality, strict matching,
canonical serialization, semantic fingerprints, diagnostics, external field
policies, and compact Image labels.

Known component presence must agree with the legacy channel layout and alpha
mode. Empty, invalid, duplicate, or contradictory sets fail validation instead
of producing ambiguous cache or execution identities. Schema 1 and 2 migrate
known RGB, RGBA, and recognized named layouts deterministically; unrecognized
Image layouts remain Unknown.

Channel Split now has a reusable semantic description that produces a Channel
only for a component known present and names an absent requested component in a
hard failure. Image Combine describes exactly the connected R/G/B/A set,
requires at least one color Channel for an executable result, and rejects
unknown or unequal finite spatial contracts with explicit Reformat guidance.
It does not infer semantic presence from fixed RGBA fallback samples.

The live graph snapshot now derives an Image Combine descriptor from its exact
incoming component links. Identity, generic arithmetic, and Reformat preserve
the component set. Compact graph-facing labels expose forms such as
`Image · R, B`.

No renderer formula, shader, texture layout, Output socket, export policy,
Constant Channel, automatic Alpha, RAW behavior, or unrelated node-library
behavior changed in this pass.

## Evidence

`StackNodeMathChannelImageTests` proves:

- all 15 nonempty R/G/B/A subsets validate, serialize in canonical order,
  round-trip exactly, and fingerprint distinctly;
- schema-2 RGB and named partial layouts migrate without inventing components;
- duplicate and empty schema-3 sets fail;
- component/layout and component/alpha contradictions fail;
- identity, generic arithmetic, and Reformat preserve exact presence;
- present split succeeds while absent or unknown presence fails explicitly;
- Combine derives exact R+B and R+A results;
- Alpha-only, wrong-type, unknown-extent, and mismatched-extent inputs fail;
  and
- disagreeing non-spatial metadata remains explicit Unknown.

The graph behavior suite saves and reloads:

```text
Image -> Split -> R+B Combine -> Output
```

The loaded Combine retains R and B links, keeps G and A absent, and reproduces
the same partial-Image descriptor identity.

## Commands And Results

```powershell
.\build.cmd
.\build\StackGraphBehaviorTests.exe
ctest --test-dir build -C Release --output-on-failure
.\build\Stack.exe --validate-layer-registry
```

Results:

- repository-preferred Windows build: passed;
- focused Phase 7B1 CPU suite: passed;
- graph behavior suite: passed;
- full CTest: 22/22 passed, including five GPU suites;
- layer-registry validation: passed; and
- targeted tracked/untracked diff checking: clean apart from line-ending
  conversion warnings.

Native visual review was not performed because this goal explicitly forbids
computer use. Partial-Image wire text remains a later manual visual QA item.

## Main Owners

- `src/NodeMath/ContractTypes.*`
- `src/NodeMath/DescriptorSerialization.cpp`
- `src/NodeMath/DescriptorPropagation.cpp`
- `src/NodeMath/ChannelImageSemantics.*`
- `src/NodeMath/TechnicalImageMath.cpp`
- `src/Editor/Internal/EditorModuleGraphSnapshot.cpp`
- `tools/node_math_channel_image_tests.cpp`
- `tools/graph_behavior_tests.cpp`
- `CMakeLists.txt`
- `cmake/StackSources.cmake`

## Stop Boundary

Phase 7B1 ends here. Output definition/socket changes, saved Channel inspection,
viewport mapping, Channel export rejection, and graph schema 8 belong to
Phase 7B2. Constant Channel, automatic opaque Alpha creation, transactional
undo/redo, and live Combine extent execution belong to Phase 7B3. C4-C8 and
unrelated Phase 7 work remain inactive.
