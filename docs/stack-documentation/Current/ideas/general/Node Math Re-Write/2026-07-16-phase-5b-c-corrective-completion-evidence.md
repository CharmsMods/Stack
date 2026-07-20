# Phase 5B-C Corrective UI Completion Evidence

- Phase: 5B-C — Graph Connection UI Correction Pass
- Completed: 2026-07-16
- Status: implementation and automated gates complete; native visual checklist
  remains for user confirmation
- Decision: NMR-134
- Next phase status: Phase 6 was not started

## Reason For The Corrective Pass

Native review of Phase 5B found that wire labels stayed horizontal, delayed
cards could jump between cursor-derived positions, the on-node Connections
section was visually heavy and had an unbounded click area, media-node preview
tooltips could follow or outlive the cursor, the readability outline was always
present, node drag surfaces were small, and full-image-to-mask attempts could
silently insert Luminance Mask.

## Implemented Result

- Wire text follows the local straight or Bezier tangent, remains upright, and
  uses rotated bounds for collision and hit testing.
- Pin and wire cards use stable target anchors. Their 0.7-second timer and
  visible state reset on target change, focus/hover loss, graph interaction,
  popup activity, panning, dragging, or drawer activity.
- The on-node Connections dropdown and the Image/Output cursor-following preview
  tooltip are removed. Advanced sockets remain available through `+ N Inputs`
  and `+ N Outputs`; labels and anchored pin cards are the inspection surface.
- Appearance settings version 9 adds connection-text size, Zoom-Aware/Fixed
  sizing, an outline toggle that defaults off, node sizing presets, and advanced
  width/UI/grab-height controls. Version 8 migrates to the new defaults.
- The default Comfortable node size increases node width and title/grab height
  while preserving node controls, previews, labels, and pin hit targets.
- Full image outputs no longer auto-create Luminance Mask and cannot connect
  directly to single-channel scalar-field inputs. The rejection names explicit
  Luminance Mask, Channel Split, or another extraction node as the next step.
  Structurally executable semantic mismatches remain permissive and diagnostic.

## Automated Verification

The following passed after the corrective changes:

```powershell
cmake --build build --config Release --target StackGraphBehaviorTests
.\build\StackGraphBehaviorTests.exe
cmake --build build --config Release --target Stack -j 1
.\build\Stack.exe --validate-node-math-phase5
ctest --test-dir build -C Release --output-on-failure -R StackNodeMath
.\build\Stack.exe --validate-layer-registry
.\build.cmd
```

The graph behavior suite verifies that a rejected full-image-to-scalar-field
drag does not change node count, provides the explicit-extraction message, and
that a manually authored Luminance Mask path still connects. The Phase 5 live
validator verifies rotated text geometry, tangent normalization, fixed versus
zoom-aware text sizing, appearance version 9 persistence/migration, and the
existing authored two-compound nonzero render and Unpack equivalence.

Results: all 9 registered Node Math tests passed, layer-registry validation
passed, and the repository-preferred Windows build produced `build\Stack.exe`
and the validation executables. The application target was built serially once
after a parallel multi-target command made two independent asset-bake targets
write generated headers at the same time; the preferred `build.cmd` path then
completed normally.

## Native Review Boundary

Automated tests cannot establish final visual feel. Run
`phase-5b-visual-test-checklist.md` to inspect rotated-label readability,
card stability, theme contrast, node sizing, advanced-pin discovery, and the
explicit image-to-mask workflow in the native application.

## Scope And Stop

No formula, color/alpha conversion, viewport transform, RAW decomposition,
ROI, halo, neighborhood, reduction, tiling, or Phase 6 infrastructure changed.
Phase 5B-C stops here; Phase 6 remains pending and requires separate
authorization.
