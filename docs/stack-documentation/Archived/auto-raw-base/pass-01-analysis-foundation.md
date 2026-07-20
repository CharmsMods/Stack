# Pass 01: Analysis Foundation

## Purpose

Build the shared analysis infrastructure that every later Auto Base feature depends on.

This pass should not add smart suggestions yet. It should make Stack able to answer these questions reliably:

- Which pixels are valid for analysis?
- What are the robust luminance percentiles?
- How wide is the scene dynamic range?
- Are there RAW/channel clipped pixels?
- Are there near-sensor-saturation pixels?
- Are there display-clipped pixels in the current view?
- What metadata did the RAW file provide?
- Which analysis results are technical baseline results versus current-frame results?

The output of this pass is an internal model and diagnostics UI. It is the foundation for later auto-apply and suggestion behavior.

## Required Organization Work

This pass must create the boundaries that prevent Auto Base from making the RAW workspace files larger and more tangled.

### Split Before Adding

Before adding analysis UI or new Auto Base state, extract at least these responsibilities out of `src/Editor/Internal/EditorModuleRawWorkspace.cpp`:

1. `src/Editor/Internal/EditorModuleRawWorkspaceControls.cpp`
   - Move `RenderRawWorkspaceControlsPanel`.
   - Move control-section helpers that are only used by the controls panel.
   - Keep the public `EditorModule::RenderRawWorkspaceControlsPanel(...)` method if that minimizes call-site churn.

2. `src/Editor/Internal/EditorModuleRawWorkspacePreview.cpp`
   - Move `RenderRawWorkspacePreviewPanel`.
   - Move preview image fit/draw helpers if they are only preview concerns.
   - Move overlay drawing code if it is only used by the preview panel.

3. `src/Editor/Internal/EditorModuleRawWorkspaceLocalRange.cpp`
   - Move Local Range overlay label/index helpers.
   - Move Local Range target sample state adoption.
   - Move `HandleRawWorkspaceLocalRangeTargetInteraction`.
   - Move `ApplyRawWorkspaceLocalRangeTargetDelta`.
   - Move `ClearRawWorkspaceLocalRangeTargetState`.

If doing all three extractions in one commit is too disruptive, do the controls split first. Analysis diagnostics and Auto Base summary must then be added to the new controls/Auto Base file, not to the old catch-all file.

### Keep Pure Code Out Of Editor Files

All analysis math belongs in `src/Raw/RawImageAnalysis.h/.cpp`.

All recommendation and Auto Base decision math belongs in `src/Raw/RawAutoBase.h/.cpp`.

Editor files may:

- request analysis
- store latest analysis state
- render diagnostics
- apply returned decisions to visible recipes

Editor files must not:

- compute connected components
- compute percentiles
- evaluate WB candidates
- synthesize Local Range suggestions
- own algorithm thresholds except UI display constants

### Group EditorModule State

Do not add many independent `m_RawWorkspaceAuto...` fields directly to `EditorModule`.

Create grouped state structs, preferably in `src/Editor/EditorModuleTypes.h` if that is consistent with the existing type-splitting pattern:

```cpp
struct RawWorkspaceAnalysisUiState {
    bool valid = false;
    Stack::RawAnalysis::RawImageAnalysis latest;
    std::uint64_t acceptedRenderGeneration = 0;
    std::string sourceKey;
};

struct RawWorkspaceAutoBaseUiState {
    bool hasAppliedViewFit = false;
    bool hasRevertSnapshot = false;
    Stack::RawRecipe::RawDevelopmentRecipe beforeAutoBase;
    std::uint64_t sourceHash = 0;
    std::uint64_t appliedAnalysisHash = 0;
    std::string summary;
};
```

Then `EditorModule` should hold one or two grouped members, not a dozen loose booleans and strings.

### Group Render Worker Payloads

Before adding analysis and recommendations to the render worker, replace the current loose RAW workspace fields with grouped payloads.

Target shape:

```cpp
struct RawWorkspaceSnapshot {
    std::string sourceKey;
    std::string localRangeOverlayMode;
    bool localRangeTargetSampleRequested = false;
    float localRangeTargetSampleU = 0.0f;
    float localRangeTargetSampleV = 0.0f;
    bool analysisRequested = false;
};

struct RawWorkspaceResult {
    std::string sourceKey;
    std::string localRangeOverlayMode;
    std::vector<unsigned char> localRangeOverlayPixels;
    int localRangeOverlayWidth = 0;
    int localRangeOverlayHeight = 0;
    RenderTextureStats viewTransformInputStats;
    Stack::RawAnalysis::RawImageAnalysis analysis;
    // Keep target sample fields grouped here too.
};
```

Then `EditorRenderWorker::Snapshot` should contain:

```cpp
RawWorkspaceSnapshot rawWorkspace;
```

and `EditorRenderWorker::Result` should contain:

```cpp
RawWorkspaceResult rawWorkspace;
```

This prevents Pass 01 through Pass 05 from turning the general render worker into a long list of RAW-specific scalar fields.

## Existing Code To Reuse

Use and extend these existing files:

- `src/Renderer/RenderPipeline.h`
- `src/Renderer/Internal/RenderPipelineReadback.cpp`
- `src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp`
- `src/Editor/EditorRenderWorker.h`
- `src/Editor/EditorRenderWorker.cpp`
- `src/Editor/Internal/EditorModuleRendering.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspace.cpp`
- `src/Raw/RawDevelopmentRecipe.h`
- `src/Raw/RawDevelopmentRecipe.cpp`
- `src/Raw/RawLoader.h`
- `src/Raw/RawLoader.cpp`
- `src/Raw/RawImageData.h`
- `src/Editor/EditorModuleTypes.h`

Existing useful behavior:

- `RenderTextureStats` already stores output/readback statistics.
- RAW View Transform input stats are already sampled immediately before the View Transform.
- Local Range target readback already captures robust scene EV, luma, and RGB from a patch.
- The render worker already transports RAW workspace render state back to the editor.
- `src/Editor/Internal/EditorModuleRawBasicControls.cpp` and `src/Editor/Internal/EditorModuleRawManagedGraph.cpp` are useful examples of the existing split-file style for editor functionality.

## New Files

Create these files unless a nearby existing type file becomes clearly better:

- `src/Raw/RawImageAnalysis.h`
- `src/Raw/RawImageAnalysis.cpp`
- `src/Raw/RawAutoBase.h`
- `src/Raw/RawAutoBase.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspaceControls.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspacePreview.cpp`
- `src/Editor/Internal/EditorModuleRawWorkspaceLocalRange.cpp`

Keep pure math and recommendation code out of ImGui/editor files. UI should consume analysis results, not compute them.

## Data Model

Add a small set of plain C++ structs. Keep them serializable in the future, but do not persist them in project files in this pass unless required.

```cpp
namespace Stack::RawAnalysis {

struct PercentileStats {
    bool valid = false;

    float p001Ev = 0.0f;
    float p01Ev = 0.0f;
    float p05Ev = 0.0f;
    float p50Ev = 0.0f;
    float p95Ev = 0.0f;
    float p99Ev = 0.0f;
    float p999Ev = 0.0f;

    float p001Luma = 0.0f;
    float p01Luma = 0.0f;
    float p05Luma = 0.0f;
    float p50Luma = 0.0f;
    float p95Luma = 0.0f;
    float p99Luma = 0.0f;
    float p999Luma = 0.0f;

    float logAverageLuma = 0.0f;
    float dynamicRangeEv = 0.0f;
    float validPixelPercent = 0.0f;
};

struct HighlightRiskReport {
    bool valid = false;

    float anyChannelClipPercent = 0.0f;
    float allChannelClipPercent = 0.0f;
    float redClipPercent = 0.0f;
    float greenClipPercent = 0.0f;
    float blueClipPercent = 0.0f;

    float anyChannelNearNonlinearPercent = 0.0f;
    float displayClipPercent = 0.0f;
    float hdrPixelPercent = 0.0f;

    bool severeSensorClip = false;
    bool partialClipColorRisk = false;
    bool blocksPositiveRawExposure = false;
};

struct RawMetadataSummary {
    bool hasCameraWhiteBalance = false;
    bool hasBaselineExposure = false;
    bool hasBaselineNoise = false;
    bool hasBaselineSharpness = false;
    bool hasActiveArea = false;
    bool hasMaskedAreas = false;
    bool hasLinearResponseLimit = false;

    float cameraWbR = 1.0f;
    float cameraWbG = 1.0f;
    float cameraWbB = 1.0f;
    float baselineExposureEv = 0.0f;
    float baselineNoise = 1.0f;
    float baselineSharpness = 1.0f;
    float iso = 0.0f;
    float shutterSeconds = 0.0f;
    float aperture = 0.0f;
    float linearResponseLimit = 1.0f;
};

struct RawImageAnalysis {
    bool valid = false;
    uint64_t sourceHash = 0;
    uint64_t recipeStageHash = 0;

    RawMetadataSummary metadata;
    PercentileStats technicalStats;
    PercentileStats currentFrameStats;
    HighlightRiskReport highlight;

    float invalidBorderPercent = 0.0f;
    float effectiveNoiseScore = 0.0f;

    std::string statusMessage;
};

}
```

Names can be adjusted to match local naming style, but keep the concepts separate:

- metadata summary
- technical stats
- current-frame stats
- highlight risk
- invalid area
- noise estimate

## Technical Analysis Versus Current-Frame Analysis

Implement two analysis stages.

### Technical Analysis Stage

Purpose:

- Exposure suggestions.
- White balance suggestions.
- RAW clipping risk.
- noise/detail defaults.
- local suggestion classifiers.

Input stage:

- Low-resolution image after RAW black/white normalization, technical WB, and input-profile conversion to Stack working RGB.
- Before user RAW exposure if possible.
- Before Local Range.
- Before View Transform.

If the current pipeline cannot easily sample exactly this stage yet, create a temporary first version that samples the earliest available scene-linear RAW development texture and explicitly documents the limitation in code comments.

Fallback rule:

- A technical-analysis fallback may support diagnostics.
- A technical-analysis fallback may support View Transform fitting only if it samples scene-linear data before View Transform.
- A technical-analysis fallback must not drive RAW exposure, WB, clipping, denoise, or local suggestions if it samples after user creative edits or from display-referred output.
- Diagnostics must show a short status such as `Technical analysis stage: fallback from current scene texture`.

### Current-Frame Analysis Stage

Purpose:

- View Transform Auto Fit.
- display readability stats.
- post-RAW-exposure, post-Local-Range current preview analysis.

Input stage:

- Low-resolution image immediately before View Transform.

Current implementation already captures this for RAW workspace View Transform stats. This pass should normalize that into `RawImageAnalysis.currentFrameStats`.

## Valid-Pixel Mask

All analysis must use a valid-pixel mask.

### Metadata First

Use RAW metadata when available:

- active area
- masked sensor areas
- crop rectangles
- alpha if present
- known invalid margins

Source fields may come from LibRaw or existing `RawImageData`. If `RawImageData` does not expose them yet, add optional fields there without changing existing decode behavior.

### Residual Border Detection

After metadata exclusions, detect leftover invalid borders in the low-resolution analysis image.

Use this exact first-pass heuristic:

```cpp
candidateInvalid =
    edgeConnected(pixel) &&
    max(rgb) < 0.0005f &&
    localVariance(luma, radius = 2) < 1e-6f;
```

Then:

1. Find connected components.
2. Keep only components touching an image edge.
3. Morphological open with radius 1.
4. Morphological close with radius 2.
5. Exclude those pixels from stats.

Do not classify arbitrary dark interior areas as invalid. Edge connection is required.

### Acceptance Criteria

- A legitimate low-key image with dark content throughout is still mostly valid.
- A black border around a panorama is excluded from stats.
- Masked DNG sensor margin areas are excluded when metadata exposes them.
- `invalidBorderPercent` is reported in diagnostics.

## Percentile Math

For every valid pixel:

```cpp
luma = max(dot(rgb, workingSpaceLumaWeights), epsilon);
ev = log2(luma);
```

Use the actual working-space luminance coefficients if Stack has them. If not, use Rec.709 coefficients only as a temporary fallback:

```cpp
Y = 0.2126f * R + 0.7152f * G + 0.0722f * B;
```

Use:

```cpp
epsilon = 1e-8f;
```

Compute:

- p0.1
- p1
- p5
- p50
- p95
- p99
- p99.9
- log-average luminance
- dynamic range EV

Dynamic range:

```cpp
dynamicRangeEv = p99Ev - p01Ev;
```

Log average:

```cpp
logAverageLuma = exp(sum(log(epsilon + luma)) / validCount);
```

Do not use raw min/max for fitting decisions except as diagnostics.

## Highlight Risk

The report requires three separate highlight concepts:

1. Sensor/channel clipping in RAW.
2. Scene-linear values above display white but not clipped in RAW.
3. Display clipping caused only by the current View Transform.

Implement the model in that order.

### RAW Sensor Clipping

Use normalized RAW values when available:

```cpp
normalized = (raw - black) / max(epsilon, white - black);
clip = normalized >= (1.0f - clipDelta);
```

Start with:

```cpp
clipDelta = 0.005f;
```

If per-channel white levels are available, use them. If only a shared white level is available, use it and record that confidence is lower.

Set:

```cpp
anyChannelClipPercent
allChannelClipPercent
redClipPercent
greenClipPercent
blueClipPercent
```

Rules:

```cpp
severeSensorClip = allChannelClipPercent > 0.01f ||
                   anyChannelClipPercent > 0.25f;

partialClipColorRisk = anyChannelClipPercent > 0.05f &&
                       allChannelClipPercent < anyChannelClipPercent * 0.5f;

blocksPositiveRawExposure = anyChannelClipPercent > 0.05f ||
                            partialClipColorRisk;
```

Percent values are in image percent, not unit fractions. For example `0.05f` means 0.05 percent.

If normalized RAW values or reliable white levels are unavailable:

- Set RAW sensor clipping fields to invalid/unknown.
- Do not infer sensor clipping from display output.
- Block positive RAW exposure auto-application.
- Allow display clipping diagnostics to continue separately.

### Near Nonlinear

If metadata exposes `LinearResponseLimit`, classify:

```cpp
nearNonlinear = normalized >= (linearResponseLimit - 0.01f);
```

If not available, skip and mark `hasLinearResponseLimit = false`.

### Display Clip

Use current output/readback stats for display clipping. Keep this separate from sensor clipping. A display clip can be fixed by View Transform and is not necessarily lost sensor data.

## Worker Plumbing

Add analysis fields to `EditorRenderWorker::Result`:

```cpp
Stack::RawAnalysis::RawImageAnalysis analysis;
```

This field should live inside the grouped `EditorRenderWorker::RawWorkspaceResult`, not directly on the general `Result` struct.

Rules:

- Fill this only for RAW workspace renders.
- Clear it on source mismatch.
- Preserve existing render result behavior.
- Do not block preview rendering on background recommendation work.
- Include an analysis status/confidence field so UI can distinguish `complete`, `fallback`, and `unavailable`.

## Editor State

Add editor state to `EditorModule`:

```cpp
Stack::EditorModuleTypes::RawWorkspaceAnalysisUiState m_RawWorkspaceAnalysisUi;
```

Rules:

- Update from render worker result.
- Clear when RAW source changes or preview output is cleared.
- Current-frame stats can update more frequently than technical stats.
- Keep this state grouped; do not add one editor member per analysis field.

## Diagnostics UI

Add a collapsed section in the RAW tab:

Label:

```text
Analysis
```

Show:

- valid pixel percent
- invalid border percent
- technical p01 / p50 / p99 EV
- current-frame p01 / p50 / p99 EV
- dynamic range EV
- HDR pixel percent
- display clip percent
- RAW clipped highlights
- near sensor saturation
- partial channel clipping risk

Do not make this the main user flow. It is a diagnostics section for v1 and a confidence-building tool.

Suggested copy:

```text
Technical stats analyze the normalized scene before view rendering.
Current-frame stats analyze the image immediately before View Transform.
```

If a stage is approximate, show it. Example:

```text
Technical stats are using the earliest available scene-linear preview; RAW clipping and exposure suggestions are disabled.
```

## Tests

Add unit tests to `tools/graph_behavior_tests.cpp` or a new focused analysis test target if the project already has a suitable pattern.

Required tests:

1. Percentile ordering is stable and monotonic.
2. Border exclusion removes edge-connected black border.
3. Border exclusion does not remove interior black subject.
4. Dynamic range EV equals p99Ev - p01Ev.
5. RAW clipping report distinguishes any-channel and all-channel clipping.
6. Display clipping does not set sensor clipping fields.
7. Empty/invalid image returns `valid = false` and no crash.
8. Missing RAW clipping metadata blocks positive RAW exposure auto-application in downstream recommendation tests.
9. Display-referred fallback is rejected for technical recommendations.

Synthetic image test cases:

- constant gray image
- black border around gray center
- dark low-key image with no border
- single clipped red channel patch
- all-channel clipped white patch
- HDR gradient from 0.0001 to 16.0

## Acceptance Criteria

This pass is complete when:

- `EditorModuleRawWorkspace.cpp` has been reduced by moving at least controls, preview, or Local Range code into focused files.
- RAW workspace render-worker payloads are grouped instead of adding more loose fields to `Snapshot` and `Result`.
- RAW workspace renders produce a valid `RawImageAnalysis` for normal RAW files.
- Diagnostics show technical and current-frame stats separately.
- Stats exclude invalid borders.
- Sensor clipping and display clipping are not conflated.
- Missing RAW clipping metadata produces unknown sensor clipping, not false safety.
- Any fallback analysis stage is surfaced in diagnostics and disables unsafe downstream recommendations.
- Existing RAW workspace rendering still works.
- `StackGraphBehaviorTests` passes.
- `Stack.exe --validate-layer-registry` passes.

## Explicit Deferrals

Do not add these in Pass 01:

- Auto-applying View Transform on load.
- RAW exposure suggestions.
- WB suggestions.
- Local Range suggestion actions.
- noise/detail auto defaults.

This pass is the measuring instrument. Later passes use it.
