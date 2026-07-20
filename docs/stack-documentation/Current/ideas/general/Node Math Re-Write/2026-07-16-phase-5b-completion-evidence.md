# Phase 5B Completion Evidence

- Phase: 5B — Compound Output Repair and Typed Connection UI
- Completed: 2026-07-16
- Status: implementation and automated gates complete; native visual checklist
  remains for user confirmation
- Decision: NMR-133
- Next phase status: Phase 6 was not started

## Corrective UI Follow-Up

Native review produced a bounded Phase 5B-C correction recorded in
`2026-07-16-phase-5b-c-corrective-completion-evidence.md` and NMR-134. That
follow-up rotates wire text with the wire, stabilizes target-anchored cards,
removes the on-node Connections section and media cursor tooltip, adds text and
node sizing preferences, defaults the outline off, and replaces implicit
image-to-mask extraction with an explicit rejection. Those final presentation
choices supersede the corresponding UI descriptions below; the compound
execution evidence remains unchanged.

## Correction To Phase 5A

The first Phase 5 live validator constructed an already expanded renderer graph.
It proved canonical-versus-fused math, but it bypassed the authored graph's
output-chain traversal. The authored renderer gate therefore failed to recognize
a compound as a valid upstream node and submitted no viewport render until the
compound was unpacked.

Phase 5B changes the validator to build the real authored chain:

```text
Image
→ Add, Then Multiply
→ Exposure, Then Premultiply
→ Output
```

The chain now reports connected before Unpack, produces a nonzero viewport
texture, and matches the fully expanded canonical graph within `2.5e-3`.

## Implementation Result

- Output and reference-source traversal carry exact output socket IDs and follow
  the selected compound public output's canonical dependencies.
- Missing required inputs, invalid connected optional inputs, and unresolved
  exact definitions block execution for explicit reasons.
- Unresolved authored output produces a one-time render notification rather than
  looking like an ordinary disconnected graph.
- All live socket lookup passes through one presentation normalizer with role,
  logical type, declared shape, units, optionality, and visibility tier.
- Ordinary image layout stays Unknown unless declared; R/G/B/A sockets are
  consistently scalar fields.
- Expanded and compact nodes expose short pin labels, delayed detail cards,
  progressive advanced-pin reveal, and a full Connections inspector section.
- Wires support persisted Adaptive/Always/Interaction Only/Off visibility,
  Floating/Break Line layout, deterministic collision handling, shared
  hover/selection, two-line semantic text, and 0.7-second detail cards.
- Appearance settings advanced to version 8; version 7 migrates to Adaptive and
  Floating without changing project serialization.
- `node-socket-catalog.md` is reproducibly generated from the live registry with
  `--write-node-socket-catalog`.

## Focused Verification

The following passed after implementation:

```powershell
cmake --build build --config Release --target Stack
cmake --build build --config Release --target StackGraphBehaviorTests
.\build\StackGraphBehaviorTests.exe
.\build\Stack.exe --validate-node-math-phase5
```

The graph behavior suite covers authored shipped compounds, reference-source
resolution, save/load, nested custom compounds, Make Unique, explicit updates,
Unpack, unresolved diagnostics, and dynamic channel consistency. The live Phase
5 validator covers the real two-compound authored chain, nonzero output,
canonical CPU/GPU behavior, unpacked equivalence, authored order, socket-family
formatters, registry metadata, pin geometry, adaptive thresholds, break-line
fallback, and appearance persistence/migration.

The broader gates also passed:

```powershell
ctest --test-dir build -C Release --output-on-failure -R StackNodeMath
.\build\Stack.exe --validate-layer-registry
.\build.cmd
```

Results: 9/9 registered Node Math tests passed, layer-registry validation
passed, and the repository-preferred full Windows build produced `build\Stack.exe`
and all validation executables.

## Pixel And Scope Assessment

This corrective pass changes whether a valid authored compound graph reaches
the renderer; it does not change the compounds' formulas. Unpacking the same
canonical graph does not visibly change the result. Socket and wire metadata are
informational and do not tighten connection compatibility.

No color conversion, alpha conversion, viewport transform, RAW decomposition,
ROI, halo, neighborhood, reduction, tiling, or Phase 6 infrastructure was added.
Old-project compatibility remains out of scope.

## Human Review

Automation verifies structure and deterministic geometry, not final visual
density. Use `phase-5b-visual-test-checklist.md` in a native Stack session,
especially across Classic, Black Nodes, and Spotlight modes and dense graphs.

## Stop

Phase 5B is complete. Phase 6 remains pending and requires a separate explicit
activation.
