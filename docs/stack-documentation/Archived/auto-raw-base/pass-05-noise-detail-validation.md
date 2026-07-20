# Pass 05: Noise, Detail Defaults, and Validation

## Purpose

Finish the first Auto Base rollout by adding conservative noise/detail recommendations and a serious validation framework.

This pass should keep image quality safe. Noise and detail are subjective, and bad automation here is very visible. The default should be conservative.

## Policy

Auto-apply only:

- minimal chroma cleanup for clearly high-noise RAWs, if the UI marks it as auto-set
- metadata-backed baseline noise/sharpness interpretation, if available

Suggest:

- stronger denoise
- reduced sharpening for high ISO or underexposed files
- shadow-noise protection when local shadow lift is suggested

Never silently auto-apply:

- strong luma denoise
- strong sharpening
- creative clarity/dehaze
- texture/skin smoothing

Default v1 behavior:

- Treat noise/detail as suggestions unless visible RAW-tab controls already exist and the validation corpus shows the auto value is safe.
- Do not add hidden denoise behavior just because the recommendation engine computes a score.

## Existing Code To Inspect

Use and extend whatever current denoise/detail paths exist:

- `src/Editor/Layers/LinearRgbNeuralDenoiseLayer.cpp`
- `src/Editor/Layers/LinearRgbNeuralDenoiseLayer.h`
- `src/Editor/Layers/SceneDenoiseLayer.cpp`
- `src/Editor/Layers/SceneDenoiseLayer.h`
- `src/Editor/Layers/ToneLayers.cpp`
- `src/Raw/RawImageData.h`
- `src/Raw/RawLoader.cpp`
- `src/Raw/RawAutoBase.h`
- `src/Raw/RawAutoBase.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspace.cpp`

Do not assume those files expose the exact controls needed. This pass starts with recommendation data and UI, then maps into existing controls only where clean.

Organization requirement:

- Put noise/detail scoring in `src/Raw/RawAutoBase.cpp` or `src/Raw/RawAutoBaseNoiseDetail.cpp` if Auto Base code has grown large by this pass.
- Put UI chips in `EditorModuleRawWorkspaceAutoBase.cpp`.
- Put any RAW-tab denoise/detail controls in a focused controls file, not in `EditorModuleRawWorkspace.cpp`.
- If validation helpers become substantial, create `tools/raw_auto_base_tests.cpp` instead of continuing to grow `tools/graph_behavior_tests.cpp`.

## Data Model

Add:

```cpp
struct NoiseDetailRecommendation {
    bool valid = false;

    float iso = 0.0f;
    float baselineNoise = 1.0f;
    float effectiveNoiseScore = 0.0f;
    float shadowLiftEv = 0.0f;

    bool suggestChromaDenoise = false;
    bool suggestLumaDenoise = false;
    bool suggestReduceSharpening = false;
    bool autoApplyMinimalChromaDenoise = false;

    float chromaDenoiseAmount = 0.0f;
    float lumaDenoiseAmount = 0.0f;
    float sharpeningScale = 1.0f;

    float confidence = 0.0f;
    std::string rationale;
};
```

Attach to `AutoBaseRecommendations`.

## Effective Noise Score

Compute from metadata first.

```cpp
isoFactor = sqrt(max(iso, 100.0f) / 100.0f);
baseline = hasBaselineNoise ? baselineNoise : 1.0f;
liftFactor = exp2(max(0.0f, shadowLiftEv) * 0.5f);
effectiveNoise = baseline * isoFactor * liftFactor;
```

Normalize:

```cpp
effectiveNoiseScore = Remap01(effectiveNoise, 1.5f, 8.0f);
```

If ISO is missing:

- estimate from image statistics only if a reliable flat dark patch estimator already exists
- otherwise set confidence low and suggest nothing

## Shadow Lift Interaction

If Pass 04 suggests or applies shadow lifting, include that in `shadowLiftEv`.

Rules:

- A +1 EV shadow lift increases visible noise risk.
- Do not suggest strong sharpening on an image where Auto Base also suggests opening shadows.
- For high ISO plus shadow lift, suggest denoise before sharpening.

## Recommendation Rules

```cpp
if (iso < 800 && effectiveNoiseScore < 0.25f) {
    suggestChromaDenoise = false;
    suggestLumaDenoise = false;
    sharpeningScale = 1.0f;
}
```

```cpp
if (iso >= 800 && iso < 3200) {
    suggestChromaDenoise = true;
    chromaDenoiseAmount = 0.20f;
    lumaDenoiseAmount = 0.05f;
    sharpeningScale = 0.90f;
}
```

```cpp
if (iso >= 3200 || effectiveNoiseScore > 0.65f) {
    suggestChromaDenoise = true;
    suggestLumaDenoise = true;
    chromaDenoiseAmount = 0.35f;
    lumaDenoiseAmount = 0.18f;
    sharpeningScale = 0.75f;
}
```

Auto-apply:

```cpp
autoApplyMinimalChromaDenoise =
    iso >= 3200 &&
    effectiveNoiseScore > 0.55f &&
    chromaDenoiseAmount <= 0.35f;
```

If no visible RAW-tab denoise controls exist yet, do not auto-apply. Show suggestion only.

If the image has a user-applied or suggested shadow lift, reduce sharpening recommendations before increasing denoise. The goal is to avoid sharpening lifted noise, not to create a plastic/noiseless style.

## UI

Suggestion items:

```text
Mild chroma denoise
Reduce sharpening
Protect shadow noise
```

Tooltip:

```text
Suggested from ISO, metadata noise hints, and shadow lift risk. Strong denoise is not applied automatically.
```

If auto-applied minimal chroma denoise:

```text
Auto Base applied mild chroma denoise for high ISO. You can edit or turn it off.
```

## Validation Corpus

Create a local validation manifest later, for example:

```text
test-assets/raw-auto-base/manifest.json
```

Do not commit large RAW files unless the repository already has a policy for that. If assets are external, document the expected path and manifest schema.

Required categories:

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
- black border or panorama edge
- clipped specular highlights
- underexposed RAW

Manifest schema:

```json
{
  "images": [
    {
      "id": "dark_foreground_bright_sky_001",
      "path": "external/path/to/file.ARW",
      "category": "dark_foreground_bright_sky",
      "expected": {
        "autoViewFit": true,
        "rawExposureAutoApplied": false,
        "suggestions": ["OpenBacklitSubject", "ProtectSky"],
        "highlightRisk": true
      }
    }
  ]
}
```

## Automated Tests

Use synthetic tests for deterministic math.

Required synthetic tests:

1. Effective noise score increases with ISO.
2. Effective noise score increases with shadow lift.
3. Low ISO image does not suggest denoise.
4. High ISO image suggests chroma denoise.
5. High ISO plus shadow lift reduces sharpening scale.
6. Missing ISO lowers confidence and avoids auto-apply.

## Regression Tests

Add tests for Auto Base state behavior:

1. Auto Base does not overwrite manual View Transform edits.
2. Auto Base does not auto-apply RAW exposure when highlight risk blocks it.
3. Suggestions apply visible recipe values.
4. Revert restores pre-Auto Base recipe.
5. Recipe serialization remains backward-compatible.

## Manual Review Metrics

For validation images, record:

- Did Auto Base make the image readable?
- Were highlights protected?
- Was RAW Exposure left alone when it should be?
- Were suggestions useful?
- Were any suggestions clearly wrong?
- Did WB suggestion preserve intentional lighting?
- Did denoise preserve detail?

Use a simple CSV or markdown table at first:

```text
image_id, auto_base_readable, highlight_safe, exposure_ok, wb_ok, suggestions_useful, notes
```

## Quality Gates

Before enabling any Auto Base feature by default, require:

- app builds
- `StackGraphBehaviorTests` passes
- `Stack.exe --validate-layer-registry` passes
- synthetic recommendation tests pass
- manual validation on at least one image from each core category

Core categories for default enablement:

- dark foreground bright sky
- underexposed RAW
- high dynamic range landscape
- sunset/golden hour
- snow scene
- high ISO night
- black border/panorama edge

Before enabling any noise/detail auto-apply specifically, also require:

- visible editable controls for every changed value
- side-by-side validation on high ISO night, underexposed RAW, and foliage/detail images
- no default strong luma denoise
- no default sharpening increase on high-noise images

## Acceptance Criteria

This pass is complete when:

- noise/detail scoring is pure and testable outside UI files.
- validation tests have a focused target or clearly separated section.
- noise/detail recommendations exist and are conservative.
- hidden denoise/detail behavior is not introduced.
- high ISO images get useful suggestions.
- missing metadata does not create false confidence.
- validation corpus schema exists.
- synthetic tests cover noise/detail math.
- Auto Base regression tests cover ownership/revert/suggestion behavior.
- default-enabled features have manual validation notes.

## Future Work

After all five passes:

- OKLCh color qualification migration.
- learned WB candidate as an optional plugin/model.
- better highlight reconstruction.
- subject-aware suggestions.
- camera-profiled denoise tables.
- Auto Base quality scoring and per-camera tuning.
