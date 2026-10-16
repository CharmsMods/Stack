#pragma once
#include "Raw/RawDevelopmentRecipe.h"
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"
#include <imgui.h>
#include <array>
#include <cstdint>
#include <optional>

namespace Stack::Editor::RawLabInternal {
struct CurveTonalSliderDomain;
enum class CurveGestureMode { Points, Select, Bend };
struct CurveCanvasGeometryCache {
    std::vector<RawRecipe::RawBezierCurvePoint> source;
    std::vector<RawRecipe::RawBezierSegment> segments;
    bool valid = false;
};
struct CurveCanvasState {
    CurveGestureMode mode = CurveGestureMode::Points;
    std::vector<bool> selected;
    std::vector<RawRecipe::RawBezierCurvePoint> before;
    ImVec2 press{};
    int dragged = -1;
    bool gesture = false, exposureGesture = false, gestureMoved = false;
    bool amountGesture = false;
    bool normalizationGesture = false;
    bool showTonalSliders = false, tonalSliderGesture = false;
    ImGuiID activeTonalSlider = 0;
    std::vector<RawRecipe::RawBezierCurvePoint> beforeTonalSliders;
    bool gestureApplied = false;
    ImVec2 appliedGestureDelta{};
    float appliedGestureRadius = 0;
    std::vector<RawRecipe::RawBezierCurvePoint> gesturePoints;
    int lastFrame = -1;
    float amountBefore = 1;
    float normalizationBefore = 0;
    float exposureBefore = 0, radius = .25f, pivot = .5f;
    std::optional<float> probe;
    std::vector<float> samples;
    std::uint64_t samplesRevision = 0, cachedSamplesRevision = ~std::uint64_t{0};
    std::size_t cachedSamplesSize = 0;
    std::array<int,128> sampleBins{};
    int samplePeak = 0;
    std::array<CurveCanvasGeometryCache,3> geometry;
    std::vector<RawRecipe::RawBezierCurvePoint> normalizationSource;
    float normalizationMean = 0;
    bool normalizationCached = false;
    std::vector<RawRecipe::RawBezierCurvePoint> effectiveSource, effectivePoints;
    float effectiveAmount = -1;
    bool effectiveDiagonal = false;
    std::array<float,kRawLabHistogramBinCount> histogramSource{}, histogramBins{};
    RawLabToneGraphViewRange histogramView{};
    std::uint64_t histogramRevision = 0;
    bool histogramCached = false, histogramValid = false;
    float histogramPeak = 0;
};
// Coordinates are normalized to the plotted domain. The caller owns authored
// values and commits edits; this state contains only this node's interaction.
bool DrawCurveCanvas(const char* id, std::vector<RawRecipe::RawBezierCurvePoint>& points,
    RawRecipe::CurveAmountState& amount, CurveCanvasState& state,
    bool diagonal, const RawLabGraphHistogram& histogram, ImVec2 size,
    int maximumPoints = 12, float* exposure = nullptr,
    RawLabToneGraphViewRange view = {}, bool amountDriven = false,
    float* normalization = nullptr, bool normalizationDriven = false,
    bool normalizationAvailable = true,
    const CurveTonalSliderDomain* tonalSliders = nullptr,
    std::uint64_t histogramRevision = 0);
}
