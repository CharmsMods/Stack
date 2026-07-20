# Phase 4: Recipe And Compact RAW Development Node

## Purpose

Introduce the structured RAW development recipe and the compact graph node that outputs the recipe result.

## Prerequisites

- Phase 1 source records exist.
- Phase 3 browsing can select an active RAW source.
- Existing manual RAW chain behavior still works and must remain compatible.
- Phase 4 used `Preview & Output` as the user-facing output/display summary. Later graph-quality finish work exposes explicit `View Transform` state in the RAW tab and managed graph; treat the Phase 4 wording below as historical baseline guidance.

## Owns

- RAW recipe data model.
- Stable default stage order.
- Recipe renderer entry point.
- Compact `RAW Development` node.
- Recipe-backed graph output.
- Recipe serialization shape, but not full project lifecycle behavior.

## Does Not Own

- Folder project save lifecycle.
- Full RAW tab control panels.
- Managed graph decomposition.
- Custom Graph Mode.

## Implementation Tasks

1. Define a versioned RAW development recipe model.
2. Include initial recipe fields needed for the known manual baseline:
   - source RAW reference,
   - RAW exposure EV / pre-tone linear exposure,
   - white balance/camera transform fields, with schema room for `As Shot`, `Auto`, custom temperature/tint, and gray-point/eyedropper picking even if all modes are not wired immediately,
   - tone curve state,
   - preview/output state that can map internally to graph `View Transform`,
   - crop/rotation state, with unsupported pieces left as explicit later-phase limits rather than hidden placeholders.
3. Encode the stable default stage order:
   - load/metadata/orientation,
   - calibration/reconstruction/demosaic/camera transform as supported,
   - white balance,
   - pre-tone RAW exposure EV,
   - tone shaping,
   - color/detail/crop/output stages as supported,
   - display/output transform.
4. Add a renderer path that can render the recipe result for preview and graph output.
5. Add a compact `RAW Development` graph node.
6. The compact node should expose:
   - RAW filename/status,
   - edited/dirty/saved/project state if available from Phase 5, otherwise omit or show a neutral unknown state,
   - `Edit In RAW Tab`,
   - no visible `Decompose To Nodes` action in the Phase 4 compact-node MVP,
   - basic read-only summary values if useful.
7. Make downstream graph editing start after the compact node by default.
8. Keep direct RAW import/open/drop manual baseline behavior working as transitional behavior.
9. Add serialization/deserialization support for the recipe data shape in project structures. Phase 5 owns when projects are created, saved, switched, repaired, or baked.

## Suggested Implementation Order

1. Inventory current RAW node parameters and renderer paths used by the manual baseline chain.
2. Define the smallest recipe model that can reproduce the baseline path.
3. Add recipe defaults and versioning.
4. Add recipe rendering for preview/graph output.
5. Add compact `RAW Development` node and graph output contract.
6. Add serialization round-trip for the recipe shape.
7. Keep existing manual RAW imports working while adding the compact-node path.
8. Add graph/renderer tests before moving to project lifecycle.

## Handoff To Phase 5

Phase 4 should leave a recipe that can be serialized, deserialized, and rendered. It does not need to decide when a per-image project is created or saved. Phase 5 uses this recipe model to implement durable per-image projects.

## Compact Node Surface Mapping

The compact node is a graph-facing summary of the recipe, not a second RAW editor. Initial surface mapping should be conservative:

- Source path/filename: displayed from the recipe source reference.
- RAW exposure EV: optional read-only summary value.
- Tone curve: optional read-only summary such as `Tone curve edited` or `Default tone curve`.
- Preview/output: optional read-only summary if the renderer already exposes it clearly. Do not label this `View Transform` in the compact summary unless the graph node itself is decomposed.
- Edit command: `Edit In RAW Tab` opens the RAW tab for this image.
- Decomposition command: hidden in the Phase 4 MVP. Phase 7 later exposed `Decompose To Nodes` as the managed graph decomposition entry point.

Do not put the full `Basic`, `Exposure`, `Tone`, or other RAW tab panels inside this node.

## MVP Recipe Control Set

The original Phase 4 MVP recipe prioritized:

- `RAW Exposure EV`
- white balance, planned for `As Shot`, `Auto`, custom temperature/tint, and gray-point/eyedropper picking by the final RAW workspace
- tone curve
- crop
- rotate
- preview/output behavior

Current compact recipes have since expanded beyond this initial baseline with graph-quality `finishTone`, explicit `viewTransform`, and `localRange` state, including region-mask fields in schema version `5`.

At Phase 4 time, color/detail/local exposure/denoise were intentionally deferred until their recipe fields, UI meaning, and graph mapping were clear. In the current implementation, Local Range is the primary recipe-backed local balancing branch, legacy `localExposure` remains compatibility/supporting state, and denoise/detail work remains deferred.

## V1 Recipe Schema Contract

The first recipe schema should be versioned and explicit. Names can be adapted to existing code style, but the persisted meaning should match this contract.

Current schema note: this section is the Phase 4 version-1 contract. The current recipe schema is version `5`; it still reads/writes the legacy `toneCurve` block for compatibility and migration, but active compact finishing is represented by `finishTone` and `viewTransform`, with Local Range stored in `localRange`. New compact-renderer work should target those current fields rather than reviving `toneCurve` as the primary finish path.

Required top-level fields:

| Field | Type | Default | Meaning |
| --- | --- | --- | --- |
| `rawRecipeVersion` | integer | `1` | Recipe schema version. |
| `sourceRef` | object/reference | required | Linked RAW source identity from the project `rawSourceRef`. |
| `whiteBalance` | object | `asShot` | White balance state. |
| `exposureEv` | number | `0.0` | Pre-tone scene-linear exposure offset in EV/stops. |
| `toneCurve` | object | default/neutral curve | Historical Phase 4 manual tone-shaping curve state; current recipes retain it for compatibility/migration into `finishTone`. |
| `cropRotate` | object | no crop, no user rotation | User crop and rotation state. |
| `previewOutput` | object | default display/output behavior | User-facing Preview & Output state; may map internally to `View Transform`. |

Suggested field details:

- `whiteBalance.mode`: `asShot`, `auto`, `custom`, or `sampledGrayPoint`. If existing code already uses `camera`, treat it as an adapter/load-time alias for `asShot`, not as a separate UI mode.
- `whiteBalance.temperatureK`: number or null; only meaningful for custom/derived modes.
- `whiteBalance.tint`: number or null; only meaningful for custom/derived modes.
- `whiteBalance.multipliers`: optional renderer-specific channel multipliers when the renderer uses them directly.
- `whiteBalance.samplePoint`: optional normalized image coordinate and sample metadata for gray-point/eyedropper picking.
- `exposureEv`: numeric EV/stops value. Positive values multiply scene-linear decoded values before tone shaping.
- `toneCurve.points`: ordered normalized control points, where input/output are in `[0, 1]` unless the renderer later defines a scene-linear curve domain. Current active finish points live in serialized `finishTone` layer state.
- `toneCurve.mode`: `default`, `custom`, or another explicit local mode. Current active finish mode/domain live in serialized `finishTone` layer state.
- `cropRotate.cropEnabled`: boolean.
- `cropRotate.cropRect`: normalized rectangle `{ x, y, width, height }` in oriented image coordinates.
- `cropRotate.userRotationDegrees`: number; initially prefer `0`, `90`, `180`, or `270` unless arbitrary rotation is already supported.
- `previewOutput.intent`: user-facing output/display intent, initially `default`.
- `previewOutput.internalViewTransform`: optional internal graph/rendering identifier; do not expose this name in the RAW tab UI.

Required defaults:

- A new preview-only RAW has an in-memory default recipe but no project yet.
- First image-affecting edit persists the recipe through Phase 5.
- Default recipe values must be deterministic so thumbnails/previews/tests do not change between sessions without a version bump.

## Validation

- Test default recipe creation from a RAW source.
- Test recipe serialization round trip.
- Test compact node outputs a renderable image result.
- Test downstream nodes can connect after the compact node.
- Regression test that existing `RawDevelop` projects still load.

## Done When

- Stack has a structured recipe model.
- The recipe can render a developed result.
- The Editor graph can consume that result through compact `RAW Development`.
- The full RAW internals are not shown by default.
- Phase 5 can store the recipe in folder-backed `.stack` projects without changing the recipe schema immediately.

## Implementation Status

Status: Complete on 2026-06-26.

Current note:

- Phase 4 introduced the initial recipe schema version `1`. Later RAW Workspace passes evolved the current recipe schema to version `5` for graph-quality finish state and Local Range region-mask state. Treat the schema-v1 bullets above as historical Phase 4 completion details, not the current full recipe surface.
- The legacy `toneCurve` block remains readable/writable for compatibility and migration, but active compact rendering uses `finishTone` plus `viewTransform` after RAW develop/decode and optional local balancing.
- The original Phase 4 `Preview & Output` label guidance predates the graph-quality finish branch. Current compact recipes persist explicit `viewTransform` state and the RAW tab exposes a `View Transform` section for those controls.

Completed:

- Added `Stack::RawRecipe::RawDevelopmentRecipe` with schema version `1`, source reference data, deterministic stage order, pre-tone exposure EV, white balance modes/placeholders, tone curve state, crop/rotation state, and preview/output mapping.
- Added JSON serialization/deserialization and mapping from the recipe to the existing RAW GPU renderer settings.
- Added compact `RAW Development` editor/render graph node payloads, sockets, serializer support, graph snapshot mapping, render graph kind, and renderer entry point.
- Added downstream image-output graph behavior while keeping legacy RAW sockets limited to the existing `RawSource`/`RawDecode`/`RawDevelop` chain.
- Added compact node browser/context-menu support. `Edit In RAW Tab` requests the RAW root tab and selects the recipe source key when available. During Phase 4, `Decompose To Nodes` remained hidden; Phase 7 later exposed it for managed decomposition.
- Added graph behavior tests for recipe defaults/round-trip, compact-node downstream/serialization contract, and legacy `RawDevelop` serialization compatibility.

Deferred to later phases:

- Phase 5 owns project discovery, first-edit project creation, save lifecycle, save-before-switch, relink/repair, and bake/embed.
- Phase 6 owns final RAW workspace panels and image-affecting edit APIs.
- Phase 7 owns managed decomposition/custom graph mode and the visible `Decompose To Nodes` command; its V1 implementation is now documented in `07_managed_decomposition.md`.

Validation:

- Passed: `cmake --build build --config Debug --target StackGraphBehaviorTests`
- Passed: `build\StackGraphBehaviorTests.exe`
- Passed: `cmake --build build --config Debug --target Stack`
- Passed: automated `Stack.exe` launch and normal close smoke test, exit code 0.
