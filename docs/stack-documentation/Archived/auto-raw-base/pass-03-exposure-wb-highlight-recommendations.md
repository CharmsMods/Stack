# Pass 03: Exposure, White Balance, and Highlight Recommendations

## Purpose

Add the first recommendation layer on top of the analysis foundation.

This pass should produce visible, confidence-rated suggestions for:

- conservative RAW Exposure EV changes
- alternate white balance candidates
- highlight risk and reconstruction/protection actions

The pass should not silently apply strong content-derived changes.

## Policy

Auto-apply:

- metadata baseline exposure normalization only when metadata explicitly provides it and the current recipe has not already captured it
- camera/as-shot white balance as the default when available

Suggest:

- content-derived RAW exposure lift or reduction
- alternate auto white balance
- highlight reconstruction mode
- achromatic highlight handling

Never silently apply:

- positive RAW exposure when clipping risk exists
- non-camera WB in stylized lighting
- heavy highlight reconstruction
- content-derived RAW exposure in v1 unless a separate user-facing preference explicitly enables it
- alternate WB over an existing user custom/manual WB

Important interpretation:

`autoApplyAllowed` means the recommendation engine believes an action is technically safe enough for a future opt-in automation mode. In this pass, the default RAW tab behavior should still present content-derived RAW exposure and alternate WB as visible suggestions unless the action is metadata-backed or the camera metadata is absent/invalid.

## Existing Code To Reuse

Use and extend:

- `src/Raw/RawImageAnalysis.h`
- `src/Raw/RawImageAnalysis.cpp`
- `src/Raw/RawAutoBase.h`
- `src/Raw/RawAutoBase.cpp`
- `src/Raw/RawDevelopmentRecipe.h`
- `src/Raw/RawDevelopmentRecipe.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspace.cpp`
- `src/Editor/EditorRenderWorker.cpp`
- `src/Editor/EditorRenderWorker.h`
- `src/Raw/RawImageData.h`
- `src/Raw/RawLoader.cpp`

Organization requirement:

- Add recommendation math only to `src/Raw/RawAutoBase.cpp`.
- Add recommendation structs only to `src/Raw/RawAutoBase.h` unless they are purely UI state.
- Render recommendation actions in the RAW suggestions popout/expander from `src/Editor/Internal/EditorModuleRawWorkspaceAutoBase.cpp`, with compact local markers near affected controls.
- Do not add exposure/WB/highlight algorithms to `EditorModuleRawWorkspace.cpp`.
- Do not append individual recommendation fields to `EditorRenderWorker::Result`; put them under the grouped RAW workspace result payload from Pass 01.

## Data Model

Add recommendation structures.

```cpp
namespace Stack::RawAutoBase {

enum class RecommendationKind {
    RawExposure,
    WhiteBalance,
    HighlightProtection,
    HighlightReconstruction
};

enum class RecommendationAction {
    None,
    ApplyVisibleRecipeValue,
    CreateSuggestionOnly
};

struct RawExposureRecommendation {
    bool valid = false;
    float currentEv = 0.0f;
    float suggestedEv = 0.0f;
    float deltaEv = 0.0f;
    float confidence = 0.0f;
    bool autoApplyAllowed = false;
    bool blockedByHighlightRisk = false;
    std::string rationale;
};

struct WhiteBalanceRecommendation {
    enum class Method {
        CameraAsShot,
        GrayWorld,
        ShadesOfGray,
        GreyEdge
    };

    bool valid = false;
    Method method = Method::CameraAsShot;
    float gainsR = 1.0f;
    float gainsG = 1.0f;
    float gainsB = 1.0f;
    float confidence = 0.0f;
    float neutralResidualBefore = 0.0f;
    float neutralResidualAfter = 0.0f;
    bool autoApplyAllowed = false;
    std::string rationale;
};

struct HighlightRecommendation {
    bool valid = false;
    bool recommendProtectiveViewShoulder = false;
    bool recommendNoPositiveRawExposure = false;
    bool recommendReconstruction = false;
    bool recommendAchromaticClip = false;
    float confidence = 0.0f;
    std::string rationale;
};

struct AutoBaseRecommendations {
    RawExposureRecommendation exposure;
    WhiteBalanceRecommendation whiteBalance;
    HighlightRecommendation highlight;
};

}
```

Attach this to the editor state or render-worker result after Pass 01:

```cpp
Stack::RawAutoBase::AutoBaseRecommendations rawWorkspaceRecommendations;
```

In code, this should be:

```cpp
EditorRenderWorker::Result::rawWorkspace.recommendations
```

or equivalent grouped RAW workspace payload, not a new top-level render-worker field.

## RAW Exposure Recommendation

### Inputs

Use:

- `RawImageAnalysis.technicalStats`
- `RawImageAnalysis.highlight`
- scene classification flags if available; if not, use dynamic range/highlight risk only
- current recipe RAW Exposure EV
- metadata baseline exposure EV if available

### Algorithm

Content-derived RAW exposure should be conservative.

Use:

```cpp
targetMedianRelativeToWhiteEv = -2.7f;
targetMedianEv = fittedSceneWhiteEv + targetMedianRelativeToWhiteEv;
rawDelta = clamp(targetMedianEv - subjectMedianEv, -0.5f, +1.0f);
```

If subject median is not implemented yet, use technical p50:

```cpp
subjectMedianEv = technicalStats.p50Ev;
```

Estimate fitted scene white:

```cpp
fittedSceneWhiteEv = technicalStats.p999Ev;
```

Initial confidence:

```cpp
confidence = 1.0f;
```

Apply penalties:

```cpp
if (highlight.anyChannelClipPercent > 0.05f) confidence -= 0.45f;
if (highlight.allChannelClipPercent > 0.005f) confidence -= 0.30f;
if (technicalStats.dynamicRangeEv > 10.0f) confidence -= 0.20f;
if (technicalStats.dynamicRangeEv > 12.0f) confidence -= 0.20f;
if (highlight.partialClipColorRisk) confidence -= 0.25f;
```

Clamp:

```cpp
confidence = clamp(confidence, 0.0f, 1.0f);
```

Auto-apply rule:

```cpp
autoApplyAllowed =
    confidence >= 0.85f &&
    abs(rawDelta) <= 0.5f &&
    !highlight.blocksPositiveRawExposure;
```

Suggestion rule:

```cpp
valid = abs(rawDelta) >= 0.15f;
```

If positive delta is blocked by highlight risk, create a suggestion rationale:

```text
RAW exposure lift not auto-applied because highlights are near clipping. Use View Transform or Local Range instead.
```

### UI Copy

Suggestion item:

```text
Raise RAW Exposure +0.3 EV
```

or:

```text
Lower RAW Exposure -0.4 EV
```

Tooltip:

```text
Suggested from scene-linear luminance statistics. Positive RAW exposure is blocked when sensor highlight risk is detected.
```

If blocked:

```text
RAW exposure lift blocked by highlight risk. Try Open shadows or View Transform shoulder instead.
```

## Metadata Baseline Exposure

If metadata exposes baseline exposure:

- Apply it only once for a new default recipe.
- Mark it as technical baseline, not content-derived auto exposure.
- Include it in the summary:

```text
Technical baseline exposure applied from RAW metadata.
```

Do not apply it repeatedly on every render.

If Stack already normalizes this earlier in the RAW pipeline, do not duplicate it in recipe exposure. Document where it is applied.

If the implementation cannot prove whether baseline exposure is already applied, do not apply it again. Show a diagnostics note instead.

## White Balance Recommendation

### Default

Use camera/as-shot WB by default when present.

Do not replace it automatically with Gray World or Shades of Gray for normal files.

If the user has chosen manual/custom WB, do not replace it and do not show the alternate WB suggestion as a primary action. Show it only in diagnostics or behind `Analyze Image`.

### Candidate Eligibility Mask

Compute candidates only from pixels that are:

- valid
- not clipped
- not near nonlinear
- not too dark
- not too bright
- not too saturated

Start with:

```cpp
luma in [p05, p95]
saturation < 0.65f
```

Saturation:

```cpp
sat = (max(R, G, B) - min(R, G, B)) / max(max(R, G, B), epsilon);
```

### Candidate Methods

Implement in this order:

1. Camera/as-shot WB.
2. Gray World.
3. Shades of Gray.

Grey Edge can be deferred if compute or code complexity is too high.

Gray World:

```cpp
meanR = average(R over eligible pixels);
meanG = average(G over eligible pixels);
meanB = average(B over eligible pixels);
target = cbrt(meanR * meanG * meanB);
gainR = target / max(meanR, epsilon);
gainG = target / max(meanG, epsilon);
gainB = target / max(meanB, epsilon);
```

Shades of Gray:

```cpp
p = 6.0f;
meanRp = pow(average(pow(R, p)), 1.0f / p);
meanGp = pow(average(pow(G, p)), 1.0f / p);
meanBp = pow(average(pow(B, p)), 1.0f / p);
target = cbrt(meanRp * meanGp * meanBp);
gainR = target / max(meanRp, epsilon);
gainG = target / max(meanGp, epsilon);
gainB = target / max(meanBp, epsilon);
```

Normalize gains so green is 1.0:

```cpp
gainR /= gainG;
gainB /= gainG;
gainG = 1.0f;
```

### Confidence

Compute neutral residual before and after candidate WB.

For v1, use simple RGB chroma residual:

```cpp
residual = average((max(R,G,B) - min(R,G,B)) / max(max(R,G,B), epsilon));
```

Compute on low-saturation medium-luma eligible pixels.

Candidate improvement:

```cpp
improvement = residualBefore - residualAfter;
```

Confidence starts from:

```cpp
confidence = Remap01(improvement, 0.02f, 0.12f);
```

Penalties:

```cpp
if (eligiblePixelPercent < 5.0f) confidence -= 0.35f;
if (sceneLooksStylized) confidence -= 0.45f;
if (candidateGainsAreExtreme) confidence -= 0.35f;
```

Extreme gain rule:

```cpp
candidateGainsAreExtreme =
    gainR < 0.45f || gainR > 2.20f ||
    gainB < 0.45f || gainB > 2.20f;
```

Auto-apply rule:

- If camera WB exists, do not auto-apply alternate WB in this pass.
- If camera WB is absent or invalid and confidence >= 0.85, auto-apply candidate and mark it as Auto WB.
- Otherwise show suggestion only.

Stylized-lighting guard:

If the scene has very high average saturation, a strong single-hue cast, or too few low-chroma eligible pixels, lower confidence and do not auto-apply. This guard is specifically for sunset, concert/stage, neon, underwater, fire, and similar scenes where the color cast may be intentional.

### UI Copy

Suggestion item:

```text
Suggested WB: Shades of Gray
```

Tooltip:

```text
This candidate reduces residual color in neutral-looking pixels. Camera WB remains the default unless you apply this suggestion.
```

Low confidence:

```text
Auto WB withheld because the scene appears intentionally colored or lacks reliable neutral pixels.
```

## Highlight Recommendation

### Inputs

Use:

- RAW clipping report
- near nonlinear report
- current-frame HDR percent
- display clip percent
- View Transform fit values

### Decision Rules

```cpp
recommendNoPositiveRawExposure =
    highlight.blocksPositiveRawExposure;
```

```cpp
recommendProtectiveViewShoulder =
    highlight.anyChannelClipPercent > 0.02f ||
    highlight.hdrPixelPercent > 1.0f ||
    highlight.displayClipPercent > 1.0f;
```

```cpp
recommendReconstruction =
    highlight.anyChannelClipPercent > 0.10f ||
    highlight.partialClipColorRisk;
```

```cpp
recommendAchromaticClip =
    highlight.partialClipColorRisk &&
    highlight.allChannelClipPercent < highlight.anyChannelClipPercent * 0.5f;
```

### UI Reporting

Add a Highlight Risk area in Analysis or Suggestions.

Labels:

```text
RAW clipped highlights
Near sensor saturation
Display clipping
Partial channel clipping risk
```

Suggestion items:

```text
Protect highlights
Reconstruct clipped highlights
Use achromatic highlight handling
```

Tooltips:

```text
RAW clipping means sensor data may be lost. View Transform clipping is display mapping and can often be fixed by shoulder or white EV.
```

```text
Partial channel clipping can create colored highlight artifacts after white balance.
```

## Applying Suggestions

Every suggestion must apply normal visible recipe values.

Examples:

- RAW exposure suggestion changes RAW Exposure EV.
- WB suggestion changes visible WB gains/temperature/tint controls, depending on current Stack recipe model.
- Highlight protection suggestion adjusts View Transform shoulder/white EV or enables a visible highlight option.

Do not create hidden flags that alter rendering without UI representation.

If the visible recipe model cannot represent a suggestion yet, the UI must not show an Apply button for it. Show the recommendation as read-only diagnostics until the visible control exists.

## Tests

Add tests for pure recommendation functions.

Required exposure tests:

1. Dark safe image suggests positive exposure.
2. Dark image with highlight clipping does not auto-apply positive exposure.
3. Low confidence HDR image creates suggestion, not auto application.
4. Small high-confidence exposure delta can auto-apply only when explicitly enabled for new default recipe.

Required WB tests:

1. Camera WB exists, alternate candidate high confidence -> suggestion only.
2. No camera WB, strong neutral evidence -> auto-apply allowed.
3. Few eligible pixels -> low confidence.
4. Extreme candidate gains -> low confidence.

Required highlight tests:

1. Single-channel clipping sets partial color risk.
2. all-channel clipping sets severe sensor clip.
3. display clipping does not set sensor clip.
4. highlight risk blocks positive RAW exposure.

## Acceptance Criteria

This pass is complete when:

- Exposure/WB/highlight recommendation logic is pure and unit-testable outside editor UI.
- Suggestion action rendering is isolated in the Auto Base UI file and follows the suggestions popout/expander pattern.
- No new recommendation scalar fields are added directly to `EditorModule` or top-level render-worker structs.
- Content-derived RAW exposure remains suggestion-first in default v1 behavior.
- alternate WB never overwrites camera/as-shot or manual/custom WB silently.
- baseline exposure is not double-applied.
- RAW Exposure suggestions are visible but conservative.
- positive RAW exposure is blocked by highlight risk.
- camera WB remains default when present.
- alternate WB appears as a suggestion with confidence/rationale.
- highlight risk report distinguishes sensor clipping, near saturation, and display clipping.
- suggestion application changes normal visible recipe controls.
- tests cover recommendation math.
- existing build and behavior tests pass.

## Explicit Deferrals

Do not implement:

- sky/foliage/backlit local suggestions
- OKLCh color qualifier replacement
- denoise/detail defaults
- learned AWB

Those belong to later passes.
