# Pass 04: Local Range Suggestions and Color Qualification

## Purpose

Use the analysis foundation to offer editable Local Range suggestions.

This pass turns image understanding into visible optional actions:

- Open shadows
- Protect sky
- Open backlit subject
- Recover highlights
- Brighten foliage

The pass must not silently create Local Range edits by default.

## Current Stack Baseline

Stack already has:

- Local Range luminance/EV targeting.
- Target-from-image drag behavior.
- Robust target sample readback.
- Local Range color qualification using sampled scene-linear RGB.
- Range mask overlay.
- Recipe serialization and tests for the color qualifier.

That means Pass 04 should focus on suggestion generation and UI, not inventing a separate masking system.

## Policy

Default:

- Generate suggestion actions for the RAW suggestions popout/expander.
- Do not apply them automatically.

When a suggestion is accepted:

- Create normal editable Local Range recipe values.
- Use the existing Local Range UI to display and adjust the result.
- Enable the range/color overlay when useful.

Never:

- hide local edits in analysis state
- apply skin or subject edits silently
- create many small fragile edits
- use display-referred HSV masks
- imply Local Range can recover sensor-clipped RAW data
- overwrite existing user Local Range points without explicit merge behavior

## Existing Code To Reuse

Use and extend:

- `src/Raw/RawDevelopmentRecipe.h`
- `src/Raw/RawDevelopmentRecipe.cpp`
- `src/Raw/RawAutoBase.h`
- `src/Raw/RawAutoBase.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspace.cpp`
- `src/Renderer/Internal/RenderPipelinePrograms.cpp`
- `src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp`
- `src/Renderer/Internal/RenderPipelineReadback.cpp`
- `tools/graph_behavior_tests.cpp`

Organization requirement:

- Put suggestion synthesis and component classifiers in `src/Raw/RawAutoBase.cpp` or a dedicated `src/Raw/RawAutoBaseLocalSuggestions.cpp` if the file grows past roughly 1000 lines.
- Put Local Range suggestion application helpers near recipe logic, preferably `src/Raw/RawAutoBase.cpp` if they are pure recipe transforms.
- Put suggestion action UI in `src/Editor/Internal/EditorModuleRawWorkspaceAutoBase.cpp`, using the RAW suggestions popout/expander pattern from the UI overhaul docs.
- Put Local Range controls, overlay controls, target interaction, and suggestion-to-overlay behavior in `src/Editor/Internal/EditorModuleRawWorkspaceLocalRange.cpp`.
- Keep `EditorModuleRawWorkspace.cpp` limited to high-level orchestration after the split.

## Data Model

Add local suggestions to `AutoBaseRecommendations`.

```cpp
enum class SuggestedLocalAdjustmentKind {
    OpenShadows,
    ProtectSky,
    OpenBacklitSubject,
    RecoverHighlights,
    BrightenFoliage
};

struct SuggestedLocalAdjustment {
    bool valid = false;
    SuggestedLocalAdjustmentKind kind = SuggestedLocalAdjustmentKind::OpenShadows;

    float targetEv = 0.0f;
    float deltaEv = 0.0f;
    float widthEv = 2.0f;
    float feather = 0.6f;

    bool protectHighlights = true;

    bool colorQualifierEnabled = false;
    float targetSceneR = 0.0f;
    float targetSceneG = 1.0f;
    float targetSceneB = 0.0f;
    float colorWidth = 0.32f;
    float colorFeather = 0.35f;
    float neutralGuard = 0.08f;

    float confidence = 0.0f;
    float affectedAreaPercent = 0.0f;
    std::string label;
    std::string rationale;
};
```

Use existing recipe fields when applying:

- `RawLocalRangeRecipe.enabled`
- Local Range curve/points or equivalent current model
- `colorMaskEnabled`
- `colorMaskTargetR/G/B`
- `colorMaskHueWidth`
- `colorMaskFeather`
- `colorMaskMinChroma`

If the Local Range model does not yet support all synthesis fields directly, map to the nearest existing controls and document the mapping in code.

## Shared Component Analysis

Create low-resolution component masks only inside the recommendation engine.

Required temporary buffers:

- valid mask
- luma EV
- simple chroma
- optional approximate hue
- sky candidate mask
- foliage candidate mask
- shadow mass mask

For v1, implement component detection on CPU from the low-resolution analysis image.

Do not store component masks in `EditorModule`. The editor should receive compact suggestion structs and optional debug metrics, not large per-pixel buffers. If masks are needed for a debug overlay later, transport them through an explicit debug-only payload.

Technical-stage requirement:

- Component classifiers must run on scene-linear data after technical WB/input profile and before View Transform.
- Do not run sky, foliage, or shadow classifiers on display-referred pixels.
- If only current-frame post-Local-Range data is available, generate no local suggestions and show diagnostics explaining why.

Resolution target:

- longest edge 512 pixels
- lower is acceptable for the first implementation if existing readback is smaller

## Sky Detection

Input:

- technical analysis low-res RGB after WB and input profile
- valid mask

Compute:

```cpp
topPrior = 1.0f - clamp(y / (height * 0.65f), 0.0f, 1.0f);
brightPrior = Remap01(ev, p50Ev, p95Ev);
smoothPrior = 1.0f - Remap01(localVarianceLuma, lowVar, highVar);
blueCyanPrior = HueBlueCyanScore(rgb);
```

First implementation for `HueBlueCyanScore` can use RGB direction:

```cpp
blueDominance = Remap01(B - max(R, G), 0.02f, 0.25f);
cyanBalance = 1.0f - clamp(abs(G - B) / max(max(G, B), epsilon), 0.0f, 1.0f);
score = max(blueDominance, 0.6f * cyanBalance * Remap01(B - R, 0.02f, 0.25f));
```

Sky score:

```cpp
skyScore =
    0.30f * topPrior +
    0.25f * brightPrior +
    0.25f * blueCyanPrior +
    0.20f * smoothPrior;
```

Candidate:

```cpp
skyCandidate = skyScore > 0.62f;
```

Post-process:

1. Connected components.
2. Keep components touching the top edge.
3. Drop components smaller than 3 percent of valid pixels.
4. Morphological close radius 2.

Report:

```cpp
skyAreaPercent
skyMedianEv
skyP70Ev
skyMedianRgb
```

## Foliage Detection

Input:

- technical analysis RGB
- valid mask
- sky mask to exclude

First implementation can use RGB-direction heuristics because Stack's current color qualifier uses the same representation.

Candidate:

```cpp
greenDominance = G - max(R, B);
yellowGreen = G > B && G >= R * 0.75f;
chroma = (max(R,G,B) - min(R,G,B)) / max(max(R,G,B), epsilon);

foliageCandidate =
    !sky &&
    chroma > 0.10f &&
    greenDominance > 0.015f &&
    yellowGreen;
```

Add texture guard:

```cpp
localVarianceLuma > textureMin
```

This reduces false positives on flat green walls or color casts.

Post-process:

1. Connected components.
2. Keep components larger than 2 percent of valid pixels.
3. Merge nearby foliage components if their median RGB directions are close.

Report:

```cpp
foliageAreaPercent
foliageMedianEv
foliageMedianRgb
foliageChromaMedian
```

Future version:

- Convert to OKLab/OKLCh after technical WB and input profile.
- Use hue 100 deg to 170 deg for foliage.
- Store target hue/chroma or convert back to scene RGB for current recipe.

## Shadow Mass Detection

Compute:

```cpp
shadowThresholdEv = p50Ev - 2.0f;
shadowCandidate = ev < shadowThresholdEv;
```

Exclude invalid pixels and severe clipped regions.

Report:

```cpp
shadowAreaPercent
shadowMedianEv
shadowP25Ev
```

Trigger `Open shadows` when:

```cpp
shadowAreaPercent > 20.0f &&
highlight.anyChannelClipPercent < 0.05f;
```

If highlight risk exists, still allow suggestion, but lower delta and set rationale:

```text
Shadow lift suggested as a local adjustment because global RAW exposure has highlight risk.
```

## Backlit Subject Detection

Use sky or bright background plus dark center/lower region.

Compute:

```cpp
centralRegion = x in [0.25w, 0.75w] and y in [0.35h, 0.85h];
centerMedianEv = median(ev over valid centralRegion);
brightBackgroundEv = skyMedianEv if sky exists else p95Ev;
contrastEv = brightBackgroundEv - centerMedianEv;
```

Trigger when:

```cpp
skyAreaPercent > 10.0f &&
contrastEv > 2.5f;
```

or:

```cpp
brightTopAreaPercent > 15.0f &&
contrastEv > 3.0f;
```

Suggestion:

```text
Open backlit subject
```

This should create a broad shadow-side Local Range lift, not a global RAW exposure change.

## Recover Highlights Suggestion

Trigger when:

```cpp
highlight.anyChannelClipPercent > 0.10f ||
highlight.partialClipColorRisk ||
highlight.displayClipPercent > 2.0f;
```

If sensor clipping is low but display clipping is high:

- Suggest View Transform highlight protection, not highlight reconstruction.

If sensor clipping or partial channel clipping is real:

- Suggest reconstruction or achromatic handling from Pass 03.

If mapping to Local Range:

- Use a negative high-luma adjustment only for recoverable display highlights.
- Do not imply clipped RAW data is recovered by Local Range.
- Label it `Protect display highlights`, not `Recover RAW highlights`.

UI copy must distinguish:

```text
Protect display highlights
```

from:

```text
Reconstruct clipped RAW highlights
```

## Suggestion Synthesis

### Open Shadows

```cpp
targetEv = shadowMedianEv;
deltaEv = Lerp(0.3f, 1.0f, Remap01(shadowAreaPercent, 20.0f, 60.0f));
widthEv = 2.5f;
feather = 0.70f;
protectHighlights = true;
colorQualifierEnabled = false;
confidence = clamp(Remap01(shadowAreaPercent, 20.0f, 50.0f), 0.0f, 1.0f);
```

### Protect Sky

```cpp
targetEv = skyP70Ev;
deltaEv = -Lerp(0.3f, 0.8f, Remap01(skyAreaPercent, 10.0f, 40.0f));
widthEv = 1.5f;
feather = 0.60f;
colorQualifierEnabled = true;
targetSceneR/G/B = skyMedianRgb;
colorWidth = 0.38f;
colorFeather = 0.45f;
neutralGuard = 0.03f;
confidence = min(Remap01(skyAreaPercent, 8.0f, 25.0f), Remap01(contrastEv, 1.5f, 4.0f));
```

For skies with lots of white clouds, low neutral guard may include whites. If the report shows false positives, split blue sky and cloud highlight suggestions later.

Do not use sky protection as a substitute for highlight reconstruction. It is a local display/scene adjustment suggestion only.

### Open Backlit Subject

```cpp
targetEv = centerMedianEv;
deltaEv = Lerp(0.5f, 1.2f, Remap01(contrastEv, 2.5f, 5.0f));
widthEv = 3.0f;
feather = 0.75f;
protectHighlights = true;
colorQualifierEnabled = false;
confidence = Remap01(contrastEv, 2.5f, 5.0f);
```

Clamp:

```cpp
deltaEv = min(deltaEv, 1.0f);
```

for v1.

### Brighten Foliage

```cpp
targetEv = foliageMedianEv;
deltaEv = Lerp(0.2f, 0.6f, Remap01(foliageAreaPercent, 4.0f, 25.0f));
widthEv = 1.5f;
feather = 0.60f;
colorQualifierEnabled = true;
targetSceneR/G/B = foliageMedianRgb;
colorWidth = 0.26f;
colorFeather = 0.35f;
neutralGuard = 0.10f;
confidence = min(Remap01(foliageAreaPercent, 3.0f, 15.0f), Remap01(foliageChromaMedian, 0.10f, 0.25f));
```

This directly uses the color-qualified Local Range feature added before this packet.

## Suggestion Ranking

Do not show more than 4 local suggestions at once in v1.

Recommended priority:

1. Open backlit subject
2. Protect sky
3. Open shadows
4. Brighten foliage
5. Protect display highlights

Suppress duplicates:

- If `Open backlit subject` is shown, do not also show `Open shadows` unless shadow mass is severe.
- If `Protect sky` is shown and highlight report already shows `Protect highlights`, merge the rationale where possible.

## UI

Add Local Range suggestion actions to the RAW suggestions popout/expander, anchored from the compact top area or Auto Base/readiness strip:

```text
Suggestions
[Open backlit subject] [Protect sky] [Brighten foliage]
```

Each suggestion item should have:

- label
- confidence indicator if easy
- tooltip rationale
- apply button behavior

Example tooltip:

```text
Creates a visible editable Local Range lift centered around the dark foreground EV. RAW Exposure stays unchanged to protect highlights.
```

For foliage:

```text
Creates a Local Range lift limited to the sampled green/yellow-green color range, so similarly bright sky pixels are less affected.
```

After applying:

- switch Local Range overlay to `Range Mask`
- mark the new edit as selected if the UI has selection
- show normal controls
- do not keep a hidden suggestion effect active

## Applying To Recipe

Create a helper:

```cpp
bool ApplySuggestedLocalAdjustment(
    const SuggestedLocalAdjustment& suggestion,
    Stack::RawRecipe::RawDevelopmentRecipe& recipe);
```

This helper should be pure and free of ImGui/editor state. The UI should call it, inspect the boolean result, then commit the edited recipe through the existing RAW workspace recipe edit path.

Rules:

- Enable Local Range if disabled.
- Add or update a local range point at `targetEv`.
- Do not destroy existing user points.
- If a point near `targetEv` already exists within 0.25 EV, ask through UI or merge conservatively.
- Enable color mask only for suggestions that explicitly use it.
- Preserve user region mask settings unless the suggestion needs a safer default.
- If merge UI does not exist yet, do not mutate a nearby existing point. Show a message that the suggestion overlaps an existing Local Range edit.

If current Local Range only supports a single curve or fixed set of controls, use the smallest mutation that expresses the suggestion and document the limitation.

## Color Qualification Future Upgrade

Do not replace current RGB-direction qualifier in the first local suggestion pass unless it is already cheap.

Create a follow-up task for OKLab/OKLCh:

- Convert scene-linear working RGB to OKLab before View Transform.
- Store sampled target hue/chroma or derive hue/chroma at analysis time.
- Map UI:
  - Color Width -> hue inner/outer angle
  - Feather -> difference between inner and outer thresholds
  - Neutral Guard -> minimum chroma
- Keep old recipe fields backward-compatible.

Possible recipe extension:

```cpp
enum class RawColorQualifierMode {
    LinearRgbDirection,
    OklchHueChroma
};
```

Do not change persisted recipe shape until tests and migration are ready.

## Tests

Add pure tests for suggestion synthesis.

Required tests:

1. Sky mask:
   - top blue smooth bright region triggers sky.
   - blue object in lower frame does not trigger sky protect.

2. Foliage:
   - green textured region triggers brighten foliage.
   - flat green border or wall has lower confidence.
   - blue sky excluded from foliage.

3. Backlit:
   - bright top sky plus dark center triggers open backlit subject.
   - evenly exposed landscape does not.

4. Shadow:
   - large shadow mass triggers open shadows.
   - highlight risk lowers confidence or lowers delta.

5. Recipe application:
   - applying foliage enables color mask with target RGB.
   - applying shadow lift does not enable color mask.
   - existing user point near target is not silently overwritten.

6. Serialization:
   - any new recipe fields round-trip.
   - existing v5/v6 recipes load with defaults.

## Manual Validation

Use real RAW images:

- dark foreground bright sky
- blue sky with white clouds
- green grass under bright sky
- forest path with dappled light
- backlit portrait or subject
- sunset with orange sky
- ocean/sky

Validation checklist:

- Suggestions are plausible and not excessive.
- Applying a suggestion creates visible Local Range controls.
- Range Mask overlay shows the expected region.
- Foliage brighten does not lift sky of similar luminance.
- Protect sky does not darken unrelated blue objects aggressively.
- Suggestions do not appear when confidence is weak.

## Acceptance Criteria

This pass is complete when:

- Local Range suggestion synthesis is outside editor UI files.
- Local Range target/overlay/control code is split into a focused RAW workspace Local Range file.
- Suggestion items appear from analysis in the suggestions popout/expander.
- No Local Range edits are auto-applied by default.
- Local suggestions are suppressed when only display-referred analysis is available.
- Existing Local Range edits are not overwritten.
- Applying a chip creates editable recipe changes.
- Foliage suggestion uses color qualification.
- Backlit/dark-sky cases prefer local suggestions over global RAW exposure.
- Tests cover component triggers and recipe application.
- Existing build and behavior tests pass.

## Explicit Deferrals

Do not implement:

- neural segmentation
- face/skin targeting
- learned suggestion ranking
- persistent suggestion history
- OKLCh persisted recipe migration

Those are future improvements after v1 is validated.
