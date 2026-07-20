# Pass 02: Auto Base View Transform

## Purpose

Make RAW images load into a readable display state by automatically fitting the View Transform from current-frame scene-linear stats.

This pass addresses the main observed workflow problem:

- Very dark RAW files with a bright sky are difficult to judge when the View Transform remains at a generic default.
- Pressing Auto Fit first often stabilizes the editing workflow.
- The report says this is technically coherent because View Transform is display rendering, not sensor exposure.

Pass 02 turns that insight into a safe default.

## Scope

Auto-apply only:

- View Transform Auto Fit on RAW load.
- Visible auto indicators.
- Auto Base summary.
- One-click revert for values changed by Auto Base.

Do not auto-apply content-derived RAW exposure in this pass.

## Existing Code To Reuse

Use and extend:

- `src/Editor/Internal/EditorModuleRawWorkspace.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/Editor/EditorModule.h`
- `src/Editor/EditorRenderWorker.cpp`
- `src/Editor/EditorRenderWorker.h`
- `src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp`
- `src/Renderer/Internal/RenderPipelineReadback.cpp`
- `src/Raw/RawDevelopmentRecipe.h`
- `src/Raw/RawDevelopmentRecipe.cpp`

Current relevant state:

- RAW tab already has `Auto Fit Current Frame`.
- Current-frame stats are captured immediately before the View Transform.
- The View Transform recipe already stores black EV, white EV, middle grey, exposure, contrast, toe, shoulder, hue preservation, and related settings.

## Organization Requirements

Do not implement the Auto Base summary, ownership state, or fit application as another large block inside `EditorModuleRawWorkspace.cpp`.

Use this split:

- `src/Raw/RawAutoBase.h/.cpp`
  - `FitViewTransformFromAnalysis(...)`
  - `ComputeHighlightRisk01(...)`
  - `ComputeShadowCompressionRisk01(...)`
  - `BuildAutoBaseViewFitDecision(...)`

- `src/Editor/Internal/EditorModuleRawWorkspaceAutoBase.cpp`
  - Render Auto Base summary/buttons.
  - Track editor-local Auto Base ownership and revert snapshot.
  - Apply returned `RawAutoBase` decisions to the active recipe.
  - Clear/freeze ownership when the user manually edits controls.

- `src/Editor/Internal/EditorModuleRawWorkspaceControls.cpp`
  - Own the overall RAW controls panel and call the Auto Base section renderer.
  - Keep View Transform control drawing here or split it further if the section is large.

Avoid adding new loose fields to `EditorModule`. Extend the grouped `RawWorkspaceAutoBaseUiState` introduced in Pass 01.

## User-Facing Behavior

When a RAW image is loaded:

1. Stack renders enough of the RAW pipeline to compute current-frame stats.
2. Stack computes a conservative View Transform fit.
3. Stack applies the fit to the visible RAW development recipe.
4. Stack marks the affected View Transform controls as auto-set.
5. Stack shows a compact summary.

Suggested summary:

```text
Auto Base applied: View fit from current frame. RAW Exposure unchanged.
```

If technical baseline and camera WB are later added:

```text
Auto Base applied: View fit, Camera WB, Technical baseline. RAW Exposure unchanged.
```

If stats are unavailable:

```text
Auto Base pending: render preview to analyze the frame.
```

## Auto Ownership

Add an internal ownership marker so Stack knows which values Auto Base changed.

Recommended type:

```cpp
enum class RawAutoValueOwner {
    None,
    AutoBase,
    User
};

struct RawAutoBaseState {
    bool hasAppliedViewFit = false;
    bool hasRevertSnapshot = false;
    Stack::RawRecipe::RawDevelopmentRecipe beforeAutoBase;

    RawAutoValueOwner viewTransformOwner = RawAutoValueOwner::None;
    uint64_t sourceHash = 0;
    uint64_t appliedAnalysisHash = 0;
    std::string summary;
};
```

This state can live in `EditorModule` at first. Do not persist it in project files in this pass.

Prefer declaring this state in `EditorModuleTypes.h` and storing it as a single member:

```cpp
Stack::EditorModuleTypes::RawWorkspaceAutoBaseUiState m_RawWorkspaceAutoBaseUi;
```

If the existing code style makes a private nested struct easier for the first pass, keep the same grouped shape and move it later. Do not add separate members such as `m_RawWorkspaceAutoBaseHasAppliedViewFit`, `m_RawWorkspaceAutoBaseSummary`, and `m_RawWorkspaceAutoBaseSourceHash`.

Manual edit rule:

- If the user manually changes any View Transform control after Auto Base applied it, set `viewTransformOwner = User`.
- Do not auto-refit again for that source unless the user clicks `Apply Auto Base` or `Auto Fit Current Frame`.

This prevents the analyzer from fighting the user.

## Fit Algorithm

Input:

- `RawImageAnalysis.currentFrameStats`
- `HighlightRiskReport`
- existing View Transform defaults

Use EV-domain statistics:

```cpp
m = p50Ev;
b = p01Ev;
w = p999Ev if valid, else p99Ev;
```

Set:

```cpp
middleGrey = exp2(m);
whiteEV = clamp(w - m + whiteMargin, 2.5f, 10.0f);
blackEV = clamp(m - b + blackMargin, 4.0f, 14.0f);
```

Initial margins:

```cpp
whiteMargin = 0.35f;
blackMargin = 0.30f;
```

Adjust margins:

```cpp
if (highlight.anyChannelClipPercent > 0.05f ||
    currentFrameStats.dynamicRangeEv > 10.0f) {
    whiteMargin += 0.25f;
}

if (analysis.effectiveNoiseScore > 0.65f) {
    blackMargin += 0.20f;
}
```

Remaining controls:

```cpp
rangeEv = whiteEV + blackEV;
contrast = clamp(1.15f - 0.04f * (rangeEv - 10.0f), 0.90f, 1.20f);

highlightRisk = ComputeHighlightRisk01(analysis);
shadowRisk = ComputeShadowCompressionRisk01(analysis);

shoulder = Lerp(0.20f, 0.60f, highlightRisk);
toe = Lerp(0.15f, 0.45f, shadowRisk);
exposure = 0.0f;
preserveHue = true;
```

The exact View Transform field names should match `RawDevelopmentRecipe`.

## Risk Scores

Add pure helper functions in `RawAutoBase.cpp`.

```cpp
float ComputeHighlightRisk01(const RawImageAnalysis& analysis)
{
    float risk = 0.0f;
    risk = std::max(risk, Remap01(analysis.currentFrameStats.dynamicRangeEv, 8.0f, 13.0f));
    risk = std::max(risk, Remap01(analysis.highlight.hdrPixelPercent, 0.5f, 5.0f));
    risk = std::max(risk, Remap01(analysis.highlight.anyChannelClipPercent, 0.02f, 0.25f));
    risk = std::max(risk, Remap01(analysis.highlight.displayClipPercent, 0.5f, 5.0f));
    return std::clamp(risk, 0.0f, 1.0f);
}
```

```cpp
float ComputeShadowCompressionRisk01(const RawImageAnalysis& analysis)
{
    const auto& s = analysis.currentFrameStats;
    float shadowMassRisk = Remap01(s.p50Ev - s.p05Ev, 1.0f, 4.0f);
    float deepShadowRisk = Remap01(-s.p05Ev, 6.0f, 12.0f);
    return std::clamp(std::max(shadowMassRisk, deepShadowRisk), 0.0f, 1.0f);
}
```

Use a shared helper:

```cpp
float Remap01(float value, float low, float high)
{
    if (high <= low) return value >= high ? 1.0f : 0.0f;
    return std::clamp((value - low) / (high - low), 0.0f, 1.0f);
}
```

## Application Timing

Auto Base View Fit should run once per RAW source load.

Trigger candidates:

- After RAW workspace source selection finishes.
- After first successful RAW workspace render returns valid current-frame stats.

Do not block image loading while waiting for analysis. It is acceptable for the first preview to appear, then update into the fitted view once stats arrive.

Rules:

- If no source hash is available, use the RAW source path plus dimensions as a temporary key.
- If the source changes, clear Auto Base state.
- If the user already manually edited View Transform for this source, do not auto-fit again.
- If a project file contains an existing saved recipe, do not overwrite it automatically unless it is a new RAW workspace with default recipe values. Show `Analyze Image` instead.
- If current-frame stats are invalid or only available after display rendering, do not auto-fit. Show `Auto Base pending` or `analysis unavailable`.
- If Auto Base has already applied a view fit for this source and the user changes RAW Exposure or Local Range, recompute stats but do not silently refit unless the View Transform owner is still `AutoBase` and the user has not touched View Transform.

Default-on-load rule:

```cpp
canApplyOnLoad =
    recipeLooksDefault &&
    !sourceHasExistingSavedProject &&
    !userEditedViewTransformForSource &&
    currentFrameStats.valid;
```

If any field is unknown, treat it as false.

## New UI

Add a small Auto Base summary area near the top of the RAW controls, above or near View Transform.

Suggested layout:

```text
Auto Base
[Analyze Image] [Apply Auto Base] [Revert Auto Base]
Auto Base applied: View fit from current frame. RAW Exposure unchanged.
```

Button behavior:

- `Analyze Image`: requests analysis and updates diagnostics only.
- `Apply Auto Base`: applies safe Auto Base decisions to visible recipe controls.
- `Revert Auto Base`: restores `beforeAutoBase` snapshot if available.

Disable `Revert Auto Base` if no snapshot exists.

Implementation note:

- Put the rendering function in `EditorModuleRawWorkspaceAutoBase.cpp`.
- Suggested method name:

```cpp
void EditorModule::RenderRawWorkspaceAutoBasePanel(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe,
    float controlWidth);
```

- The method may call `ApplyRawWorkspaceRecipeEditForSelectedSource(...)`, but the fit math must come from `RawAutoBase.cpp`.

Mark View Transform controls:

```text
Middle Grey  [Auto]
Black EV     [Auto]
White EV     [Auto]
Shoulder     [Auto]
Toe          [Auto]
```

If the user edits one of these controls:

- Remove `[Auto]` from the View Transform group or switch to `[Edited]`.
- Keep the Auto Base summary but note:

```text
View Transform edited manually.
```

Auto Base must also mark what it did not change:

```text
RAW Exposure unchanged.
White balance unchanged.
No local edits applied.
```

This prevents users and future implementers from treating View Transform Auto Fit as a hidden RAW exposure operation.

## Tooltip Copy

For `Apply Auto Base`:

```text
Fits the display rendering for this RAW file using robust scene-linear frame statistics. This makes the image readable without changing RAW Exposure.
```

For `RAW Exposure`:

```text
Changes scene-linear exposure before display rendering. Use it for real exposure placement, not just preview brightness.
```

For `View Transform`:

```text
Maps scene-linear RAW values into the display range. Auto Fit sets middle grey and black/white bounds from the current frame.
```

For `Revert Auto Base`:

```text
Restores the recipe values from before Auto Base was applied.
```

## Recipe Interaction

Do not add auto ownership fields to the persisted recipe yet unless necessary. Keep the feature state editor-local in this pass.

Rationale:

- Auto ownership is UI/session state.
- Persisting it needs a more complete project behavior decision.
- The recipe values themselves already persist normally.

However, if Stack project lifecycle needs to know whether a value is auto-set, add a separate optional metadata block later. Do not overload core recipe math fields.

## Tests

Add pure function tests for the fit algorithm.

Required cases:

1. Normal exposure image:
   - p01/p50/p999 separated by moderate range.
   - Fit should produce middle grey near p50 luma.
   - whiteEV and blackEV inside clamps.

2. Dark foreground bright sky:
   - p50 very low, p999 high, dynamic range high.
   - Fit should increase shoulder.
   - RAW exposure recommendation remains out of scope and unchanged.

3. Low dynamic range image:
   - p01/p99 close.
   - Fit should not create extreme blackEV/whiteEV.

4. Clipped highlight image:
   - highlight risk high.
   - shoulder increases and white margin widens.

5. No valid stats:
   - no crash.
   - no auto-apply.

Add UI-adjacent tests only if the existing project has a pattern. Otherwise test the state machine as pure C++.

## Manual Validation Images

Use these images once available:

- dark foreground with bright sky
- low-key indoor image
- sunset
- snow scene
- high dynamic range landscape
- underexposed RAW

Manual validation checklist:

- Image becomes readable after Auto Base.
- RAW Exposure remains unchanged.
- highlights are not aggressively blown by Auto Base.
- View Transform controls show auto values.
- manual control edit removes auto ownership.
- Revert restores the previous recipe.

## Acceptance Criteria

This pass is complete when:

- Auto Base view-fit math lives in `RawAutoBase.cpp`, not in an ImGui file.
- Auto Base UI glue lives in `EditorModuleRawWorkspaceAutoBase.cpp` or another focused RAW workspace UI file, not in the main RAW workspace catch-all file.
- Auto Base state is grouped in one editor state struct.
- New RAW loads auto-fit View Transform once when the recipe is still default.
- Existing saved recipes are not silently overwritten.
- The UI explicitly says RAW Exposure and Local Range were not changed by View Fit.
- Auto Base summary appears.
- Revert Auto Base works.
- RAW Exposure is not auto-lifted by this pass.
- Manual View Transform edits stop further automatic refits for that source.
- Fit math is covered by tests.
- Existing graph behavior tests pass.
- Main app target builds.

## Explicit Deferrals

Do not implement these in Pass 02:

- WB suggestions.
- RAW exposure suggestions.
- Local Range chips.
- highlight reconstruction suggestions.
- noise/detail defaults.

Those belong in later passes.
