# Phase 7: Managed Decomposition And Custom Graph Mode

## Implementation Status

Status: V1 implemented at model/build level on 2026-06-27, with hardening and documentation cleanup through 2026-06-29. Phase 7 remains partial only because `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 2 through Scenario 5 are still open.

Implemented:

- `managedRawSection` metadata with schema/version, source ref, group id, owned node ids, locked foundation ids, section output handle, recipe field mappings, and validator version.
- Conservative structural validator for `RAW Source -> RAW Decode -> Tone Curve -> View Transform`, with downstream graph/output remaining outside the managed RAW section.
- `Decompose To Nodes` from an active recipe-backed RAW Workspace project, preserving downstream render links where possible.
- RAW tab actions for decompose, validate, repair, re-adopt, and detach.
- Compact `RAW Development` node context-menu entry for `Decompose To Nodes`.
- RAW tab -> managed graph sync for currently representable recipe fields through the managed `RAW Decode`, `Tone Curve`, and `View Transform` nodes.
- Managed graph -> RAW recipe sync for exposure, white-balance mode/multipliers, rotation, graph-quality Finish Tone, and View Transform state when validation passes.
- Automatic transition to `Custom Graph Mode` when managed graph structure or unmanaged `RAW Decode` settings break validation.
- Project save/load hooks for `ManagedDecomposed` and `CustomGraph` mode payloads.
- Graph-first adoption for an already-valid `RAW Source -> RAW Decode -> Tone Curve -> View Transform` chain.
- Conservative mechanical repair for accidental missing required managed-chain links when all owned nodes, source identity, decode settings, layer types, and boundary rules still match the managed RAW contract.
- Pre-confirmation warnings before graph socket connects, link removals, or node removals that would customize or break an active managed RAW chain.
- Phase 7 V1 decision: flexible stage reordering is intentionally not supported; attempted stage-order metadata or graph-link reorder is rejected instead of treated as RAW-tab editable.
- Regression coverage in `StackGraphBehaviorTests` for managed-section serialization/validation, graph-first adoption, graph-to-recipe sync, unsupported recipe blocking, unsupported decode setting blocking, custom internal node insertion, conservative missing-link repair/refusal, pre-confirmation warning boundary rules, V1 reorder rejection, and file-level reload preservation for recipe-backed, managed-decomposed, and custom graph ownership.

Intentionally conservative / still open:

- Flexible stage reordering is intentionally disabled and accepted for Phase 7 V1. It should move to a later pass only if the recipe, UI explanation, serialization, and validator are expanded together.
- Repair is deliberately narrow: it can reconnect missing required links in an otherwise intact managed RAW chain, but does not recreate missing nodes, rewrite metadata, repair downstream graph topology, or remove/bypass custom internal edits.
- Pre-confirmation warnings are implemented for the main managed-chain graph mutation entry points; Scenario 3 and Scenario 4 hands-on native UI smoke is still needed to verify popup ergonomics across keyboard deletion, context menus, and socket drag/drop. Record results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Project reload preservation now has a dedicated file-level regression test, but Scenario 5 hands-on native UI smoke still needs to be recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Native visual QA with a real RAW Workspace is still required before marking Phase 7 complete. Use `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md`, especially Scenario 2 through Scenario 5.
- Automated native-smoke preflight passed on 2026-06-29 and is recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`; it does not replace the interactive Scenario 2 through Scenario 5 gate.

## Purpose

Support expert graph workflows without losing RAW tab integrity.

## Prerequisites

- Phase 4 recipe and compact `RAW Development` node exist.
- Phase 5 project files can persist mode state and graph data.
- Phase 6 RAW tab has editable recipe controls and read-only UI support.
- Existing manual RAW chain behavior still works as an advanced/decomposition reference.
- Expert graph-first RAW chains may exist and should be adopted only when they validate against this phase's contract.

## Owns

- `Decompose To Nodes`.
- Managed RAW graph section.
- Structural validator.
- Two-way sync while valid.
- Supported flexible-stage reordering contract; V1 intentionally supports no reorderable slots.
- `Custom Graph Mode`.
- Repair, re-adopt, and detach actions.
- Adoption of valid expert graph-first RAW chains.

## Does Not Own

- Folder scanning.
- Thumbnail generation.
- Basic project lifecycle.
- Normal RAW tab panel design.
- New RAW algorithms unless needed to represent existing recipe stages.

## Implementation Tasks

This list is the original Phase 7 implementation contract. The V1 model/build work is implemented as summarized in `Implementation Status` and `Current Done When Status`; the remaining Phase 7 gate is `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 2 through Scenario 5 recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

1. Define the managed RAW graph section metadata:
   - owning source RAW/project,
   - section start/end or node group identifier,
   - managed node ids,
   - recipe field mappings,
   - mode state.
2. Implement `Decompose To Nodes` from a recipe-backed project.
3. Generate a clearly labeled graph section, for example `RAW Development: image_0001.ARW`.
4. Include supported baseline nodes, initially matching the manual model:
   - `RAW Source`,
   - `RAW Decode`,
   - `Tone Curve`,
   - `View Transform`.
   The managed RAW section may stop at `View Transform`; the project's normal downstream graph/output endpoint remains outside the managed RAW section unless explicitly included by the graph model.
5. Preserve downstream normal graph connections after the decomposed section.
6. Implement structural validation:
   - required foundation stages exist,
   - locked foundation stages remain in required order,
   - managed parameters still map to recipe fields,
   - only supported flexible stages are reordered,
   - unknown/custom nodes inside the managed section fail validation unless explicitly supported.
7. Implement two-way sync while valid:
   - RAW tab control changes update managed node parameters,
   - managed node parameter changes update the recipe and RAW tab controls,
   - supported stage reordering updates recipe stage order when a future pass defines reorderable slots.
8. Add validation after graph edits that touch the managed section.
9. When validation fails, enter `Custom Graph Mode`.
10. In `Custom Graph Mode`:
    - custom graph section owns the render,
    - RAW tab controls are read-only,
    - RAW tab shows the required customized-chain message.
11. Add warning before intentional actions that would break managed mode.
12. Add `Repair RAW Chain` for mechanical fixes.
13. Add `Re-adopt Graph As RAW Recipe` only when validation passes.
14. Add `Detach From RAW Tab` for intentional full graph ownership.
15. Define the mode/section payload and use the project lifecycle hooks from Phase 5 to persist and reload it. Phase 7 owns the payload semantics; Phase 5 owns the save/load plumbing.
16. Add an explicit adoption path for expert graph-first RAW chains:
    - validate the chain,
    - map graph parameters to recipe fields,
    - enter `Managed Decomposed Mode` if valid,
    - enter or remain in `Custom Graph Mode` if invalid or intentionally detached.

## Suggested Implementation Order

1. Define the managed-section metadata and project serialization payload.
2. Implement structural validation against an existing graph section before adding decomposition.
3. Implement `Decompose To Nodes` from a recipe-backed project.
4. Preserve downstream graph connections when replacing compact output with managed nodes.
5. Add one-way recipe-to-node parameter sync.
6. Add node-to-recipe parameter sync.
7. Add validation after graph edits.
8. Add `Custom Graph Mode` transition and read-only RAW tab messaging.
9. Add repair/re-adopt/detach actions.
10. Add expert graph-first adoption after validation works for generated managed chains.
11. Add flexible-stage reordering only after the validator can prove the reordered chain round-trips.

## Handoff To Phase 8

Phase 7 should leave a conservative validator. Broader Phase 8 work should not start from this handoff until `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 2 through Scenario 5 pass and are recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

After that gate is recorded, Phase 8 may add new stages or more reorderable slots, but only by expanding the recipe, UI explanation, serialization, and validator together.

## Structural Validity Rules

Initial locked foundation stages should include the required source/decode path. Later renderer-specific early stages can be added, but do not allow arbitrary foundation ordering.

Initial flexible stage support should be conservative. Add reorderable stages only when:

- the math is understood,
- the UI can explain the behavior,
- the recipe can serialize the order,
- the validator can round-trip it.

## Managed Section Boundary Contract

A managed RAW graph section must have explicit metadata. Do not infer ownership only from visual node placement.

Minimum managed-section metadata:

- `sectionId`
- `projectId` or project-local identifier
- `sourceRawRef`
- ordered managed node ids
- locked foundation node ids
- flexible stage node ids and slot names
- section input/output socket ids or equivalent graph handles
- recipe field to node parameter mapping
- validator/schema version

The validator should read this metadata first, then verify the actual graph still matches it. If metadata and graph disagree, validation fails and the image enters `Custom Graph Mode` or repair flow.

## Initial Supported Slots

V1 managed decomposition should be conservative:

- Locked foundation slot: `RAW Source -> RAW Decode`.
- Tone slot: `Tone Curve`, representing the compact recipe's graph-quality `Finish Tone` state.
- Preview/output slot: `View Transform`, representing the compact recipe's serialized `viewTransform` state.
- Downstream graph/output endpoint: outside the managed RAW section unless the graph model explicitly includes it.

Do not allow arbitrary nodes inside the managed section in v1. Flexible reordering should start disabled unless the validator can prove a specific pair/order round-trips correctly.

## Decomposition Blocking Conditions

`Decompose To Nodes` should warn and stop, or require explicit custom graph/detach behavior, when:

- the recipe contains non-default fields that have no managed node mapping,
- white balance/custom camera transform cannot be represented and round-tripped,
- crop is enabled, or rotation is outside the supported 0/90/180/270-degree managed `RAW Decode` mapping,
- Finish Tone or View Transform state cannot map exactly to the managed `Tone Curve` / `View Transform` nodes,
- active/non-default Local Range or legacy `localExposure` state is present while no managed graph mapping and exact round-trip validation exist for those stages,
- the downstream graph cannot be reconnected safely,
- the project is already in unresolved custom graph/read-only state,
- source RAW relink/identity is unresolved.

This is intentionally strict. A managed graph that silently drops edits is worse than refusing to decompose.

## Initial Recipe-To-Node Mapping

Managed decomposition must be conservative. Do not silently drop recipe fields that cannot be represented in graph nodes.

Initial mapping:

| Recipe field or concept | Managed graph representation | Notes |
| --- | --- | --- |
| RAW source reference | `RAW Source` | Required locked foundation node. |
| Decode/camera metadata | `RAW Decode` | Required locked foundation node where current renderer support exists. |
| White balance / camera transform | `RAW Decode` if supported; otherwise keep recipe-backed and block decomposition for non-default values | Do not pretend unsupported values round-trip. |
| RAW Exposure EV / pre-tone exposure | `RAW Decode` exposure parameter or dedicated supported exposure node | Must remain before tone shaping. |
| Finish Tone / tone curve | `Tone Curve` | Graph-quality `finishTone` state maps to the managed graph Tone Curve node when it can round-trip exactly. |
| View Transform / preview-output recipe field | `View Transform` | Last managed RAW-stage output mapping before downstream editing. `View Transform` is both the graph/node term and the current compact recipe finish control. |
| Rotation | `RAW Decode` rotation setting | Supported only for 0, 90, 180, or 270 degrees and must round-trip exactly. |
| Crop | Dedicated supported crop node only if one exists; otherwise block decomposition when crop is enabled or leave as future work | Do not drop crop edits. |
| Local Range / legacy `localExposure` | Dedicated supported nodes only after managed graph mapping and exact validator support are added for those stages | Local Range recipe/UI/rendering support exists in the compact path, but active/non-default Local Range or legacy `localExposure` still blocks managed decomposition until exact graph mapping and validator round-trip support exist. |
| Color/detail/denoise | Dedicated supported nodes only after recipe, UI, graph mapping, and validator support exist for those stages | Not part of V1 managed decomposition unless explicitly implemented cleanly. |

If the active recipe contains non-default fields that cannot be represented by the managed node set, `Decompose To Nodes` should warn and stop, or require an explicit custom graph/detach path. It should not create a managed graph that looks valid but loses edits.

## Required Message

```text
This RAW chain has been customized in the graph.
RAW tab editing is read-only for this image until the chain is repaired or re-adopted.
```

## Validation

- Test `Decompose To Nodes` creates a managed section.
- Test RAW tab -> graph parameter sync.
- Test graph parameter -> RAW tab/recipe sync.
- Test V1 rejects flexible stage reordering instead of treating it as RAW-tab editable.
- Test locked-stage reorder enters `Custom Graph Mode`.
- Test custom node insertion inside managed section enters `Custom Graph Mode`.
- Test read-only RAW tab blocks edits in custom mode.
- Test graph-breaking managed RAW mutations show a warning before changing graph ownership.
- Test repair/re-adopt/detach transitions, including conservative missing-link mechanical repair and refusal to bypass custom internal edits.
- Test project reload preserves mode and ownership payloads for recipe-backed, managed-decomposed, and custom graph projects.
- Test valid graph-first RAW chain can be adopted into managed decomposed mode.
- Test invalid graph-first RAW chain remains custom/read-only instead of pretending to sync.
- Run the native UI smoke protocol in `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md`, especially managed decomposition, graph-breaking warning, repair/re-adopt/detach, and save/reopen ownership scenarios.
- Record native UI smoke results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`, then summarize them in `../CURRENT_HANDOFF.md`.

## Done When

- Experts can decompose a RAW recipe into graph nodes.
- The RAW tab stays editable while the graph remains structurally valid.
- Unsupported graph customization becomes explicit, understandable, and safe.
- No arbitrary graph mutation is silently treated as RAW tab editable.
- Native RAW UI smoke passes for managed decomposition, warning/custom-mode transitions, repair/re-adopt/detach, and saved ownership reload.
- Project reload preserves recipe-backed, managed decomposed, and custom graph ownership correctly.

## Current Done When Status

As of 2026-06-29, the Phase 7 V1 code/model implementation satisfies the model/build portions of the `Done When` list, but Phase 7 is not complete because `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 2 through Scenario 5 are still pending in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

- Decomposition is implemented and covered by graph behavior tests, including compact-node context menu entry and active project decomposition.
- RAW-tab/managed-graph sync is implemented for currently representable fields while validation passes, including RAW Decode fields plus graph-quality Finish Tone and View Transform state.
- Unsupported customization, custom-mode transition, warning boundaries, repair/re-adopt/detach surfaces, and V1 reorder rejection have model/build coverage.
- Project reload preservation for recipe-backed, managed-decomposed, and custom graph ownership has dedicated file-level regression coverage.
- Still open: run `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 2 through Scenario 5 against a real RAW Workspace and record results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Automated preflight alone is not completion evidence for Phase 7.
