# Phase 8A: Local Range Tone Equalizer

## Purpose

This branch replaced the former vague Local Exposure slider experience with a graph-driven, measurable local range tool.

The tool should help solve Stack's long-running RAW problem:

- very dark regions need to come up,
- very bright regions need to come down or stay protected,
- local contrast and edges must survive,
- the image should then be finished with the existing `Finish Tone` and `View Transform` controls.

This is a Phase 8 workflow branch-off. Passes 1-8 were implemented in small passes rather than as one large rewrite; future Local Range extensions should keep that pattern.

Current status as of 2026-06-29:

- Passes 1-8 are implemented in code and covered by synthetic/model validation where practical.
- The current recipe schema is version `5` after Pass 7 added Local Range region-mask state.
- Native/manual RAW UI smoke is still pending: run `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 1 plus Scenario 6 through Scenario 9 and record results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Do not start broader Phase 8 workflow features until Phase 7 Scenario 2 through Scenario 5 and Phase 8A Scenario 1 plus Scenario 6 through Scenario 9 rows are recorded and the handoff is updated.

## Core Pipeline Order

The intended compact RAW order is:

```text
RAW Decode / White Balance / RAW Exposure
-> Local Range Tone Equalizer
-> Finish Tone
-> View Transform
-> Display
```

`Local Range` comes before `Finish Tone`.

The local tool's job is mechanical scene-range balancing:

```text
Bring selected dark and bright scene ranges closer together while preserving detail.
```

`Finish Tone` remains the main global look-making control:

```text
Place contrast, shape mids, define tonal personality, and make the whole image look good.
```

`View Transform` remains the final scene-to-display compression/rolloff stage.

## Why This Is Different From Finish Tone

Finish Tone is a global curve:

```text
output = f(input)
```

Every pixel with the same scene brightness gets the same output value, regardless of where it is in the image.

Local Range is a local exposure field:

```text
sceneEv = log2(sceneLuma / middleGrey)
deltaEv = localRangeCurve(sceneEv)
smoothedDeltaEv = edgeAwareSmooth(deltaEv, guide = sceneLuma)
output = input * exp2(strength * smoothedDeltaEv)
```

The important difference is that the local tool should operate on a low-frequency, edge-aware exposure map so it can balance broad regions without flattening fine texture or bleeding across strong boundaries.

## Product Direction

Implemented direction as of 2026-06-29:

- The old slider-shaped `Local Exposure` editor is no longer the primary interface for new/default projects.
- The RAW tab's main local balancing UI is a graph-first `Local Range` section backed by `RawDevelopmentRecipe.localRange`.
- Legacy `localExposure` remains readable/renderable for old projects and appears only as a compatibility drawer when needed.

The primary interface is a `Local Range` graph:

- x-axis: scene brightness in EV relative to middle grey.
- y-axis: local exposure change in EV.
- points: user-owned exposure adjustments for tone zones.
- disabled/default state: identity/no-op.

Example intent:

```text
dark foreground zone: +1.3 EV
midtones: +0.2 EV
bright sky zone: -0.8 EV
specular highlights: 0.0 EV / protected
```

The graph is paired with preview/trust feedback in the implemented V1:

- graph editing through add/drag/delete/reset EV points,
- preview-only `Affected Tones`, `EV Delta Map`, and `Region Mask` overlays,
- click/drag image targeting through sampled pre-finish scene EV,
- slider-driven V1 region masks for `Linear Gradient`, `Radial Gradient`, and `Luminance Range`.

Native/manual smoke still needs to prove that the overlays, target gestures, masks, and full-resolution preview settle behavior match the math on real RAW files.

## Non-Goals

- Do not make Local Range the main photographic finishing control.
- Do not replace `Finish Tone` or `View Transform`.
- Do not hide automatic look generation inside Local Range.
- Do not ship a click-target UI that cannot be measured against sampled scene EV changes.
- Do not add AI subject/sky detection in the first implementation passes.
- Do not make local range edits silently decompose into managed graph nodes until the managed graph validator can round-trip the state exactly.

## Naming

User-facing name recommendation:

```text
Local Range
```

Alternate names to consider later:

- `Local Tone Balance`
- `Local Dodge & Burn`
- `Tone Equalizer`

Avoid `Local Exposure` as the main label once the graph exists. It sounds like a generic slider group and does not explain the tone-zone mapping.

## Data Model Direction

Implemented data model:

- Local Range uses a dedicated `localRange` recipe block rather than expanding the older slider-shaped `localExposure` block.
- Schema version `4` introduced Local Range; schema version `5` added region-mask fields.
- New/default recipes serialize a disabled identity `localRange` block.
- Existing recipes without `localRange` load with disabled identity Local Range state.

Current block shape:

```json
{
  "localRange": {
    "enabled": false,
    "strength": 1.0,
    "middleGrey": 0.18,
    "minEv": -8.0,
    "maxEv": 6.0,
    "points": [
      { "ev": -8.0, "deltaEv": 0.0 },
      { "ev": 0.0, "deltaEv": 0.0 },
      { "ev": 6.0, "deltaEv": 0.0 }
    ],
    "smoothness": 0.65,
    "edgeProtection": 0.75,
    "detailProtection": 0.80,
    "highlightProtection": 0.50,
    "maskPreviewMode": "none",
    "regionMaskEnabled": false,
    "regionMaskMode": "linear-gradient",
    "regionMaskInvert": false,
    "regionMaskCenterX": 0.5,
    "regionMaskCenterY": 0.5,
    "regionMaskAngleDegrees": 0.0,
    "regionMaskSize": 0.65,
    "regionMaskFeather": 0.35,
    "regionMaskLowEv": -8.0,
    "regionMaskHighEv": 6.0
  }
}
```

Existing `localExposure` projects remain readable. Current compatibility behavior:

- Legacy `localExposure` data is not automatically converted on load, so existing project output does not silently change.
- Projects with active/non-default legacy state can show a compatibility drawer.
- `Convert To Local Range` is an explicit UI action that maps simple legacy shadow/highlight budgets into equivalent `localRange.points` and disables the legacy block.

Do not drop old edits.

Implemented decision as of 2026-06-29:

- `RawDevelopmentRecipe` introduced Local Range in schema version `4`; the current schema is version `5` after manual region-mask fields.
- A disabled identity `localRange` block is serialized for new recipes.
- Existing recipes without `localRange` still load with disabled identity Local Range state.
- Existing `localExposure` data remains readable and is converted only by explicit user action.
- Active/non-default Local Range state is not representable as a managed decomposed graph stage until a managed mapping exists.

## Implementation Passes

### Pass 1 - Contract, Recipe, And Identity Model

Status: Implemented on 2026-06-29.

Owned and implemented:

- Add a documented `localRange` recipe model.
- Introduce recipe schema version `4` for the first durable `localRange` block.
- Keep legacy `localExposure` readable.
- Add helpers for default identity `localRange`, sanitization, equality, and hashing.
- Add CPU tests for identity/default behavior and migration.

Does not own:

- New UI.
- Image click targeting.
- Edge-aware rendering.

Done when:

- Done: saving/loading preserves `localRange`.
- Done: disabled/default `localRange` is treated as no-op by the recipe contract.
- Done: compact RAW render identity changes when enabled Local Range points change because `localRange` participates in serialized recipe identity.
- Done: existing projects with `localExposure` still load without silently creating an active Local Range edit.

### Pass 2 - Graph Evaluator And Renderer V1

Status: Implemented on 2026-06-29.

Owned and implemented:

- Implement scene EV evaluation:

```text
sceneEv = log2(max(luma, epsilon) / middleGrey)
deltaEv = evaluatePointCurve(sceneEv)
```

- Apply local range before Finish Tone and View Transform.
- Keep disabled/default path visually identical to current output.
- Add render/cache fingerprint coverage for `localRange`.
- Add synthetic tests where practical.

Does not own:

- Fancy UI.
- Image target tool.
- Manual masks.

Done when:

- Done: a simple synthetic gradient gets predictable EV changes by zone through CPU evaluator coverage.
- Done: identity/disabled Local Range remains a no-op and the compact renderer skips the Local Range pass unless enabled points have non-zero EV deltas.
- Done: changing a Local Range point invalidates the compact RAW render through serialized recipe identity coverage.
- Done: compact recipe-backed RAW rendering applies Local Range after legacy Local Exposure and before Finish Tone/View Transform.

Implementation note:

- This pass originally used direct per-pixel scene-EV mapping and intentionally left edge-aware smoothing, overlay trust views, target sampling, and manual masks to later passes. As of the current docs, those later scopes are implemented by Passes 4 through 7, so do not treat this Pass 2 note as a current renderer limitation.
- Pass 2 initially left `smoothness`, `edgeProtection`, `detailProtection`, and `highlightProtection` as durable recipe fields before pixel behavior existed. Pass 6 now applies those fields in the edge-aware Local Range map.

### Pass 3 - Local Range Graph UI

Status: Implemented on 2026-06-29.

Owned and implemented:

- Replace the former main Local Exposure sliders with a graph.
- x-axis: scene EV.
- y-axis: local EV change.
- Add/edit/delete/drag points.
- Show axis labels and selected point values.
- Keep controls plain-language:
  - `Strength`
  - `Smoothness`
  - `Edge Protection`
  - `Detail Protection`
  - `Reset`
- Move legacy/technical sliders to a collapsed compatibility or advanced area only if still needed.

Does not own:

- Click-on-image targeting.
- Manual masks.
- AI selection.

Done when:

- Done: the RAW tab's main local-range editor is now a graph-first `Local Range` section backed by `RawDevelopmentRecipe.localRange`.
- Done: users can add, drag, delete, and reset EV/delta-EV points from the graph; graph edits auto-enable Local Range.
- Done: the graph is identity by default and uses the v4 default EV anchors.
- Done: axis labels and the selected point's scene EV/local EV values are visible.
- Done: the plain controls are labeled `Strength`, `Smoothness`, `Edge Protection`, `Detail Protection`, and `Reset`.
- Done: the old slider-shaped `localExposure` editor moved into a collapsed legacy compatibility drawer.

Implementation note:

- `Smoothness`, `Edge Protection`, and `Detail Protection` initially edited durable recipe fields before pixel behavior existed; Pass 6 now applies them in the edge-aware local-map quality work.
- Pass 3 by itself did not implement image click targeting, overlays, or masks. Current implementations for those scopes live in Passes 4, 5, and 7.

### Pass 4 - Trust Overlays

Status: Implemented on 2026-06-29. Scenario 8 native/manual RAW UI smoke with real RAW files is still pending and should be recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

Owned and implemented:

- Add preview overlays for:
  - affected tone mask,
  - local EV delta map,
  - optional before/after local range toggle.
- Overlay must be tied to the pre-finish local range state, not the final display output only.
- Add panel status text so the user knows what overlay is being shown.

Does not own:

- Image targeting.
- Brush/gradient masks.

Done when:

- Done: the RAW tab has a preview-only `Trust Overlay` selector in the `Local Range` section with `Off`, `Affected Tones`, and `EV Delta Map`.
- Done: overlay rendering is generated from the pre-finish texture immediately before Local Range is applied, so dark-zone positive EV points and highlight-zone negative EV points map to the same scene tones that the graph affects.
- Done: positive EV/local lift zones draw as teal-tinted overlay regions; negative EV/highlight compression zones draw as pink/red overlay regions.
- Done: turning the overlay `Off` clears the overlay texture and does not mutate the RAW recipe or project dirty state.
- Done: overlays are side-channel preview artifacts only; compact RAW output, recipe serialization, saved project data, and export output remain the normal developed image.
- Needs Scenario 8 native/manual RAW UI smoke: verify on real RAW files that `Affected Tones` and `EV Delta Map` are current-source only, visually match the affected tone zones, clear safely on source switches, and do not dirty or alter saved recipe output. Record results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

Implementation note:

- Pass 4 did not implement the optional before/after local-range split. Keep it as an optional follow-up only after the two trust overlays are smoke-tested with real RAW files.

### Pass 5 - Image Target Tool

Status: Implemented on 2026-06-29. Scenario 7 native/manual RAW UI smoke with real RAW files is still pending and should be recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

Owned and implemented:

- Add target mode for Local Range.
- Clicking the image samples pre-finish scene EV using a small robust patch sample, preferably median or trimmed mean rather than one pixel.
- Click-drag up/down creates or edits a graph point at the sampled EV.
- Show a marker on the graph for the sampled tone.
- Dragging should update the graph and preview through the same recipe/render path as direct graph edits.

Does not own:

- Subject/sky detection.
- Brush masks.
- Auto solve.

Done when:

- Done in code: clicking the accepted live RAW preview in `Target From Image` mode requests a small pre-finish scene-EV patch sample from the RAW Development texture immediately before Local Range is applied.
- Done in code: dragging up/down applies a positive/negative Local Range EV point at the sampled scene EV through `ApplyRawWorkspaceRecipeEditForSelectedSource`, using the same recipe/render path as direct graph edits.
- Done in code: the Local Range graph draws a marker line for the last accepted same-source sampled EV.
- Done in code: target sampling uses capped fast-preview renders while active and final release/edit behavior still routes through the existing fast-then-full RAW preview settling path.
- Needs Scenario 7 native/manual RAW UI smoke: verify on real RAW files that dark-region drag-up and bright-region drag-down visibly affect the sampled zone before Finish Tone without moving unrelated tone zones by the same amount. Record results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

Implementation note:

- The renderer reads a tiny float patch from the pre-Local-Range texture and uses a trimmed luma mean to avoid one-pixel hot/dead samples driving the target point.
- The target mode is transient editor UI state; saved project schema and export output only change when the target gesture writes a normal `localRange` recipe point.
- Thumbnail/neutral preview images are intentionally not targetable; the tool only samples accepted live RAW preview renders for the selected active source.

### Pass 6 - Edge-Aware Local Map Quality

Status: Implemented on 2026-06-29. Scenario 7 and Scenario 8 native/manual RAW UI smoke with real RAW files are still pending and should be recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

Owned and implemented:

- Improve the exposure map so it balances broad regions while preserving local contrast.
- Use or adapt an edge-aware/guided/bilateral-style smoothing strategy.
- Add controls only if needed:
  - `Smoothness`
  - `Edge Protection`
  - `Detail Protection`
- Add synthetic edge tests where feasible.

Does not own:

- Manual brush UI.
- Batch sync.

Done when:

- Done in code: the compact RAW Local Range shader now evaluates the graph from a broader edge-aware scene-EV map instead of each pixel's direct luma only.
- Done in code: `Smoothness`, `Edge Protection`, `Detail Protection`, and `Highlight Protection` now affect the pixel shader and trust overlay shader.
- Done in code: the edge-aware map uses log-luma neighborhood samples, rejects strong cross-edge EV differences, and intentionally includes small texture-level differences so texture does not become a separate exposure zone.
- Done in code: synthetic CPU coverage verifies that dark regions can keep a positive lift beside bright samples when edge protection is high, and that detail-aware smoothing reduces texture-driven EV variation compared with the direct evaluator.
- Needs Scenario 7 and Scenario 8 native/manual RAW UI smoke: verify on real RAW files that dark/bright edges do not show obvious halos, fine texture remains natural, and performance is acceptable during capped previews and final full renders. Record results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

Implementation note:

- This pass remains a single fullscreen shader pass for the compact recipe-backed RAW path. It is not a manual mask system and does not add brush/gradient/region UI.
- The trust overlays use the same edge-aware effective delta as the output pass, so overlay colors continue to represent what the renderer is applying.
- Automated validation includes CPU behavior tests and app/build validations; there is no repo RAW fixture, so native real-RAW shader smoke remains required.

### Pass 7 - Manual Region Masks

Status: Implemented on 2026-06-29. Scenario 8 native/manual RAW UI smoke with real RAW files is still pending and should be recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

Owned and implemented:

- Add optional region gating after the graph/mask math is stable.
- Candidate region tools:
  - brush,
  - linear gradient,
  - radial gradient,
  - luminance range mask,
  - edge-aware selection refinement.
- Region masks should multiply or gate the tone-zone local range effect.

Does not own:

- AI sky/subject selection.
- Batch editing.

Done when:

- Done in code: the user can constrain Local Range to a region without changing the global tone-zone graph by enabling a V1 Region Mask on the Local Range recipe.
- Done in code: V1 supports slider-driven `Linear Gradient`, `Radial Gradient`, and `Luminance Range` masks; brush painting, drawn/polygon masks, and edge-refined selection masks remain deferred.
- Done in code: mask visibility is clear through a preview-only `Region Mask` trust overlay, while `Affected Tones` and `EV Delta Map` continue to show the region-gated effective Local Range delta.
- Done in code: save/load round-trips masks through schema v5 `localRange.regionMask*` fields, with sanitization/clamping coverage.
- Done in code: managed/custom graph ownership rules remain intact; active/non-default Local Range recipes, including masked ones, are still rejected from managed graph decomposition until exact round-trip mapping exists.
- Needs Scenario 8 native/manual RAW UI smoke: verify runtime GLSL compilation/execution on real RAW files and judge gradient/radial/luminance mask ergonomics and visual quality. Record results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

Implementation note:

- Pass 7 implemented manual region-mask gating as a compact-recipe V1 rather than a full mask authoring system. The output shader multiplies the edge-aware Local Range EV delta by the selected region mask, so the global tone-zone graph stays unchanged and the region acts only as a gate.
- Region Mask overlay generation is allowed from the pre-Local-Range texture even while the graph itself is neutral, but normal developed output still applies Local Range only when the EV graph has a non-zero effect.
- Automated coverage includes mask serialization/deserialization, sanitization, hash/render-identity participation, and CPU behavior tests for linear/radial/luminance masks. The repo still has no RAW fixture that directly exercises the compact GLSL mask path at runtime.

### Pass 8 - Compatibility, Presets, And Deprecation

Status: Implemented on 2026-06-29. Scenario 1 and Scenario 6 through Scenario 9 native/manual RAW UI smoke with real RAW files are still pending and should be recorded in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

Owned and implemented:

- Legacy `localExposure` UI policy: hidden for untouched/default projects, compatibility drawer only when active or non-default legacy state exists.
- Explicit migration/compatibility display for old projects through `Convert To Local Range`, without automatic output-changing migration on load.
- Starter presets:
  - `Open Shadows`
  - `Hold Highlights`
  - `Compress Range`
  - `Reset`
- Documentation for how Local Range should be used with RAW Exposure, Finish Tone, and View Transform.

Does not own:

- Broader copy/paste/sync batch workflows.

Done when:

- Done in code: existing projects remain stable because legacy `localExposure` serialization/rendering remains supported and old projects with legacy state still display a compatibility drawer.
- Done in code: new users see the graph-first Local Range workflow because the legacy slider drawer is hidden for untouched/default projects, and starter controls apply Local Range graph presets instead.
- Done in code: the old slider set no longer competes with the graph as a second primary editor; it appears only for legacy state and includes an explicit `Convert To Local Range` action that maps the old EV budgets into graph points and disables the legacy block.
- Done in code: starter presets exist for `Open Shadows`, `Hold Highlights`, `Compress Range`, and `Reset`, backed by recipe helpers and behavior tests.
- Done in docs: the Manual Workflow Target below documents how to use RAW Exposure, Local Range, Finish Tone, View Transform, and region masks together.
- Needs Scenario 1 and Scenario 6 through Scenario 9 native/manual RAW UI smoke: verify the hidden/default legacy drawer, conversion action, and starter preset buttons in the actual RAW tab with real projects. Record results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.

Implementation note:

- Pass 8 keeps the old `localExposure` recipe readable and renderable for project stability, but it no longer offers Local Exposure as a discoverable new-edit control. The compatibility drawer appears only when a project carries active or non-default legacy state.
- `LocalRangeRecipeFromLocalExposure()` is an explicit migration helper for UI conversion, not an automatic project migration. This avoids changing existing project output without user action.
- Starter presets are graph presets: they write normal Local Range points and keep the graph editable after application. `Reset` restores the disabled identity Local Range state.

## Anti-Hype Acceptance Tests

The feature should not be considered done because the UI looks impressive. It must satisfy measurable behavior.

Native UI checklist:

- Use `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` for the repeatable real-RAW smoke protocol.
- Scenario 1 plus Scenario 6 through Scenario 9 map these acceptance tests to active preview stability, RAW tab interactions, overlays, target gestures, masks, Finish Tone, and full-resolution preview settle behavior.
- Record Scenario 1 plus Scenario 6 through Scenario 9 results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md` before calling Local Range natively verified.

Required checks:

1. Identity check:
   - Disabled/default Local Range produces the same output as no Local Range.

2. Synthetic gradient check:
   - A point at `-3 EV` with `+1 EV` delta brightens the matching zone by roughly one stop before Finish Tone.
   - Midtones and highlights remain near identity unless their curve zone overlaps.

3. Bright/dark split check:
   - A dark half can lift while a bright half stays stable.
   - Edge halo must stay below a visible threshold.

4. Real RAW shadow check:
   - Click/target a dark patch, drag up.
   - The sampled pre-finish patch should measure brighter by the requested EV delta within tolerance.

5. Real RAW highlight check:
   - Click/target a bright patch, drag down.
   - The sampled pre-finish patch should compress without globally dimming unrelated midtones.

6. Finish Tone / View Transform independence check:
   - After Local Range brings the scene closer together, the existing Finish Tone curve still behaves predictably and remains the main look control.
   - View Transform still owns display rolloff, contrast, saturation, and false-color inspection.

7. Performance check:
   - Dragging graph points or target tool gestures uses capped previews while active and settles to uncapped full render after quiet.

## Manual Workflow Target

The intended user flow is:

1. Set `RAW Exposure` so important highlights retain headroom.
2. Use `Local Range` only if the scene has problem dark/bright regions that cannot be globally mapped cleanly.
3. Use `Finish Tone` in `RGB` + `Log Scene` for global contrast and taste.
4. Use `View Transform` for display rolloff, contrast, saturation, and false-color inspection.
5. Use manual region masks only for subject/area-specific refinements.

## Risks

- Local range math can create halos if the exposure map is too sharp or crosses edges.
- It can flatten texture if the exposure map follows fine detail too closely.
- It can make Finish Tone feel unpredictable if it is placed after the curve or if the user cannot see what it changed.
- A click-target tool without overlays and measurement will feel magical in the bad way.
- Too many sliders will recreate the earlier Local Exposure confusion that led to this branch.

## Current Verification Start

Passes 1-8 have already been implemented. The next work is not another Local Range code pass; it is native/manual verification:

- Run `00_NATIVE_RAW_UI_SMOKE_CHECKLIST.md` Scenario 1 plus Scenario 6 through Scenario 9 against real RAW files.
- Record those scenario results in `../NATIVE_RAW_UI_SMOKE_RESULTS.md`.
- Update `../CURRENT_HANDOFF.md` with the scenario results before calling Phase 8A natively verified.

The implemented pass order was:

```text
data contract -> renderer math -> graph UI -> trust overlays -> target tool -> edge-aware map quality -> region masks -> presets/legacy compatibility
```

That order kept each pass testable and prevented the feature from becoming a polished UI wrapped around unclear behavior. Keep that discipline for future Local Range extensions: write the contract first, then renderer behavior, then UI, then native/manual RAW UI smoke evidence.
