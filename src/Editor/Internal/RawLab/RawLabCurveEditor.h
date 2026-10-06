#pragma once

#include "Editor/EditorModuleTypes.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Renderer/RenderPipeline.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct ImRect;

namespace Stack::Editor::RawLabInternal {

// The tone graph is deliberately denser than the compact graph itself. This
// keeps the displayed distribution smooth when a fitted range is expanded to
// the full width of the graph.
inline constexpr std::size_t kRawLabHistogramBinCount = 256;

struct RawLabGraphHistogram {
    bool valid = false;
    std::array<float, kRawLabHistogramBinCount> luma {};
    std::array<float, kRawLabHistogramBinCount> red {};
    std::array<float, kRawLabHistogramBinCount> green {};
    std::array<float, kRawLabHistogramBinCount> blue {};
};

// A display-only crop of the curve's authored 0..1 coordinate system. It is
// deliberately separate from the Finish Tone recipe so zooming the graph
// cannot alter a curve point or the rendered image.
struct RawLabToneGraphViewRange {
    float minimum = 0.0f;
    float maximum = 1.0f;
};

RawLabGraphHistogram BuildRawLabZonesHistogram(
    const RawDevelopmentGraphScopeReadback& scope,
    float minimumEv,
    float maximumEv,
    float middleGrey);

RawLabGraphHistogram BuildRawLabFinishToneHistogram(
    const RawDevelopmentGraphScopeReadback& scope,
    RawDevelopmentGraphScopeStage expectedStage,
    Raw::RawWorkingSpace workingSpace,
    int domain,
    float minimumEv,
    float maximumEv,
    float middleGrey);

RawLabToneGraphViewRange BuildRawLabToneGraphViewRange(
    const RawLabGraphHistogram& histogram,
    int activeCurve,
    float zoom);

RawLabToneGraphViewRange ApplyRawLabToneGraphViewZoom(
    const RawLabToneGraphViewRange& fittedRange,
    float zoom);

RawLabGraphHistogram CropRawLabGraphHistogram(
    const RawLabGraphHistogram& histogram,
    const RawLabToneGraphViewRange& viewRange);

// A direct-manipulation control with no thumb. Dragging shifts the repeated
// pill pattern beneath the pointer, making it suitable for graph view and
// strength controls without suggesting a separate slider track.
bool DrawRawLabRingDial(
    const char* id,
    float& value,
    float width,
    const char* tooltip,
    float doubleClickValue = 1.0f);

void ClearCurveSegmentSelection(
    EditorModuleTypes::RawCurveGraphUiState& state,
    int& selectedPoint);

bool DrawBezierCurveInteraction(
    const ImRect& rect,
    std::vector<RawRecipe::RawBezierCurvePoint>& points,
    EditorModuleTypes::RawCurveGraphUiState& state,
    int& selectedPoint,
    ImU32 normalCurveColor,
    ImU32 pointColor,
    int maximumPoints,
    const std::function<RawRecipe::RawBezierSegment(const std::vector<RawRecipe::RawBezierCurvePoint>&, std::size_t)>& segmentBuilder = {},
    bool preserveEndpointX = true,
    float* exposureEv = nullptr);

bool DrawFramelessToneCurveBezier(
    const char* id,
    RawRecipe::RawPointCurveComponent& component,
    EditorModuleTypes::RawCurveGraphUiState& interaction,
    int& selectedPoint,
    int activeCurve,
    const RawLabGraphHistogram& histogram,
    bool rgbHistogram,
    const ImVec2& size,
    const RawLabToneGraphViewRange& viewRange = {});

bool DrawFramelessLocalRangeBezier(
    const char* id,
    RawRecipe::RawLocalRangeRecipe& localRange,
    EditorModuleTypes::RawCurveGraphUiState& interaction,
    int& selectedPoint,
    std::string& selectedTargetZoneId,
    const RawLabGraphHistogram& histogram,
    bool rgbHistogram,
    const ImVec2& size,
    bool drawTargetZones,
    float* exposureEv = nullptr,
    bool* curveChanged = nullptr);

} // namespace Stack::Editor::RawLabInternal
