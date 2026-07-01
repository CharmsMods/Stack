# Auto RAW Base Research-To-Implementation Crosswalk

This document maps the original Deep Research recommendations to the implementation passes. Use it as a final alignment checklist before coding.

If a pass document appears to contradict this crosswalk or the research report, follow the safer interpretation:

- visible controls
- suggestion-first for interpretive edits
- no hidden processing
- no silent overwrite of user work
- view-transform-first for automatic readability

## 1. Transparent Technical Assistant, Not Auto Enhance

Research recommendation:

- Auto Base should be a transparent technical assistant.
- It should not become a one-click style or enhancement filter.
- Every automation must be visible, explainable, editable, and overridable.

Implementation location:

- `README.md`, `Non-Negotiable Auto Base Contract`
- `pass-02-auto-base-view-transform.md`, Auto Base summary and revert
- `pass-03-exposure-wb-highlight-recommendations.md`, suggestion-only recommendation behavior
- `pass-04-local-range-suggestions-and-color.md`, visible Local Range chips
- `pass-05-noise-detail-validation.md`, no hidden denoise/detail behavior

Hard guardrail:

- Do not implement a hidden image look, hidden local adjustment, hidden denoise, hidden WB override, or hidden exposure correction.

## 2. Metadata Normalization

Research recommendation:

- Auto-apply deterministic technical metadata normalization:
  - active area
  - masked/invalid sensor areas
  - black levels
  - white levels
  - camera/as-shot WB ingest
  - baseline exposure only if explicitly defined

Implementation location:

- `pass-01-analysis-foundation.md`, metadata summary, valid-pixel mask, clipping analysis
- `pass-03-exposure-wb-highlight-recommendations.md`, metadata baseline exposure and camera WB rules

Hard guardrail:

- Do not double-apply black/white normalization or baseline exposure if the current RAW pipeline already applies it.
- If the implementation cannot prove whether baseline exposure is already applied, do not apply it again.

## 3. Two Analysis Stages

Research recommendation:

- Use a technical analysis stage for exposure/WB/clipping/noise/local suggestions.
- Use a current-frame stage immediately before View Transform for View Transform Auto Fit.

Implementation location:

- `pass-01-analysis-foundation.md`, Technical Analysis Stage and Current-Frame Analysis Stage
- `pass-02-auto-base-view-transform.md`, fit from current-frame stats
- `pass-04-local-range-suggestions-and-color.md`, technical-stage requirement for classifiers

Hard guardrail:

- Do not drive exposure, WB, denoise, or Local Range suggestions from display-referred pixels.
- If the correct technical stage is unavailable, show diagnostics and suppress unsafe recommendations.

## 4. View Transform Is The Primary Automatic Readability Tool

Research recommendation:

- Auto-fit View Transform on load by default for new/default RAW recipes.
- This makes dark/HDR/backlit RAWs readable without changing scene exposure.
- View Transform maps scene-linear data into display-referred output.

Implementation location:

- `pass-02-auto-base-view-transform.md`

Required math:

```cpp
m = p50Ev;
b = p01Ev;
w = p999Ev if valid, else p99Ev;

middleGrey = exp2(m);
whiteEV = clamp(w - m + whiteMargin, 2.5f, 10.0f);
blackEV = clamp(m - b + blackMargin, 4.0f, 14.0f);

contrast = clamp(1.15f - 0.04f * ((whiteEV + blackEV) - 10.0f), 0.90f, 1.20f);
shoulder = Lerp(0.20f, 0.60f, highlightRisk);
toe = Lerp(0.15f, 0.45f, shadowRisk);
```

Hard guardrail:

- The UI must explicitly say RAW Exposure and Local Range were not changed by View Fit.
- Do not auto-fit existing saved recipes unless the user explicitly applies Auto Base.

## 5. RAW Exposure Must Be Conservative

Research recommendation:

- RAW Exposure is scene-linear exposure scaling.
- Do not blindly brighten dark images.
- Dark image plus bright sky often needs View Transform plus local suggestions, not global exposure lift.
- Content-derived exposure should be suggestion-first.

Implementation location:

- `pass-03-exposure-wb-highlight-recommendations.md`

Required math:

```cpp
targetMedianEv = fittedSceneWhiteEv - 2.7f;
rawDelta = clamp(targetMedianEv - subjectMedianEv, -0.5f, +1.0f);

confidence = 1.0f;
if (highlight.anyChannelClipPercent > 0.05f) confidence -= 0.45f;
if (highlight.allChannelClipPercent > 0.005f) confidence -= 0.30f;
if (technicalStats.dynamicRangeEv > 10.0f) confidence -= 0.20f;
if (highlight.partialClipColorRisk) confidence -= 0.25f;

autoApplyAllowed =
    confidence >= 0.85f &&
    abs(rawDelta) <= 0.5f &&
    !highlight.blocksPositiveRawExposure;
```

Hard guardrail:

- In default v1 behavior, content-derived RAW Exposure remains a suggestion even when `autoApplyAllowed` is true.
- Positive RAW exposure must be blocked when sensor clipping metrics are unknown or risky.

## 6. White Balance Respects Camera Intent

Research recommendation:

- Camera/as-shot WB is the default.
- Gray World and Shades of Gray are candidate suggestions, not universal replacements.
- Stylized lighting should lower confidence.

Implementation location:

- `pass-03-exposure-wb-highlight-recommendations.md`

Required math:

Gray World:

```cpp
target = cbrt(meanR * meanG * meanB);
gainR = target / max(meanR, epsilon);
gainG = target / max(meanG, epsilon);
gainB = target / max(meanB, epsilon);
```

Shades of Gray:

```cpp
p = 6.0f;
meanCp = pow(average(pow(C, p)), 1.0f / p);
gainC = geometricMean(meanRp, meanGp, meanBp) / max(meanCp, epsilon);
```

Eligibility:

```cpp
luma in [p05, p95]
saturation < 0.65f
not clipped
not near nonlinear
valid pixel
```

Hard guardrail:

- Do not overwrite camera/as-shot WB silently.
- Do not overwrite manual/custom WB.
- Do not auto-apply AWB in sunsets, concerts, neon, underwater, fire, or other likely intentional color scenes.

## 7. Highlight Handling Has Three Separate Meanings

Research recommendation:

Stack must distinguish:

1. RAW sensor/channel clipping.
2. Scene-linear values above display white.
3. Display clipping caused by View Transform.

Implementation location:

- `pass-01-analysis-foundation.md`, sensor/display clipping model
- `pass-03-exposure-wb-highlight-recommendations.md`, highlight recommendations
- `pass-04-local-range-suggestions-and-color.md`, display-highlight local suggestions

Required math:

```cpp
normalized = (raw - black) / max(epsilon, white - black);
clip = normalized >= (1.0f - clipDelta);
nearNonlinear = normalized >= (linearResponseLimit - 0.01f);
```

Hard guardrail:

- Do not infer RAW sensor clipping from display output.
- Do not imply Local Range can recover sensor-clipped RAW data.
- Heavy highlight reconstruction is suggestion-only.

## 8. Local Range Suggestions Are Not Silent Edits

Research recommendation:

- Suggested local edits should appear as chips.
- Applying a chip creates normal editable Local Range recipe values.
- Start with a small vocabulary:
  - Open shadows
  - Protect sky
  - Open backlit subject
  - Recover/protect highlights
  - Brighten foliage

Implementation location:

- `pass-04-local-range-suggestions-and-color.md`

Hard guardrail:

- Do not auto-create Local Range edits unless the user opts into a future explicit setting.
- Do not overwrite existing Local Range points.
- Do not show Apply if the suggestion cannot be represented as visible editable recipe values.

## 9. Color Qualification

Research recommendation:

- Current scene-linear RGB direction plus chroma guard is acceptable for v1.
- Future improvement should use OKLab/OKLCh before display rendering.

Implementation location:

- `pass-04-local-range-suggestions-and-color.md`

Current method:

```cpp
u = rgb / length(rgb);
u0 = targetRgb / length(targetRgb);
d = acos(clamp(dot(u, u0), -1.0f, 1.0f));
qh = smoothstep(thetaOuter, thetaInner, d);

Cn = (max(R,G,B) - min(R,G,B)) / max(max(R,G,B), epsilon);
qn = smoothstep(Cmin, Cmax, Cn);

qColor = qh * qn;
```

Hard guardrail:

- Do not use display-referred HSV/HSL for Local Range color masks.
- Do not migrate persisted recipe color qualification to OKLCh until migration and tests are designed.

## 10. Noise And Detail

Research recommendation:

- Metadata first, image statistics second.
- High ISO can produce suggestions or minimal visible cleanup.
- Strong denoise and sharpening are subjective.

Implementation location:

- `pass-05-noise-detail-validation.md`

Required math:

```cpp
NoiseRel =
    BaselineNoise *
    sqrt(max(iso, 100.0f) / 100.0f) *
    exp2(max(0.0f, shadowLiftEv) * 0.5f);
```

Hard guardrail:

- Default v1 behavior is suggestion-first.
- Do not introduce hidden denoise.
- Do not increase sharpening on high-noise or shadow-lifted images.

## 11. Architecture And Organization

Research recommendation:

- Fast load-time pass plus background recommendation pass.
- Keep UI responsive.
- Store actions as recipe values or suggestions.

Implementation location:

- `README.md`, Organization Targets
- `pass-01-analysis-foundation.md`, Required Organization Work

Required organization:

- `src/Raw/RawImageAnalysis.h/.cpp`: pure statistics and masks.
- `src/Raw/RawAutoBase.h/.cpp`: pure decisions and recommendation math.
- `src/Editor/Internal/EditorModuleRawWorkspaceAutoBase.cpp`: UI glue.
- `src/Editor/Internal/EditorModuleRawWorkspaceLocalRange.cpp`: Local Range UI/interaction.
- `EditorRenderWorker::RawWorkspaceSnapshot` and `EditorRenderWorker::RawWorkspaceResult`: grouped payloads.

Hard guardrail:

- Do not make `EditorModuleRawWorkspace.cpp`, `EditorModule.h`, or `EditorRenderWorker::Result` the dumping ground for Auto Base.

## 12. Validation

Research recommendation:

- Use synthetic and real RAW tests.
- Validate edge cases before default-enabling automation.

Implementation location:

- `pass-05-noise-detail-validation.md`

Required real RAW categories:

- dark foreground bright sky
- high dynamic range landscape
- low-key portrait
- snow scene
- sunset/golden hour
- concert/stage lighting
- indoor tungsten
- high ISO night
- macro/foliage
- ocean/sky
- backlit subject
- black border/panorama edge
- clipped specular highlights
- underexposed RAW

Hard guardrail:

- Do not default-enable any risky automation without validation notes for the relevant image categories.
