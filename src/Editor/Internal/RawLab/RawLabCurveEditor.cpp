#include "Editor/Internal/RawLab/RawLabCurveEditor.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

namespace Stack::Editor::RawLabInternal {
using RawCurveGraphUiState = EditorModuleTypes::RawCurveGraphUiState;

namespace {

constexpr int kRawLabCurveMaxPoints = 12;
constexpr float kRawLabCurveFineDragScale = 0.18f;

} // namespace
ImVec2 GraphToScreen(const ImRect& rect, float x, float y) {
    return ImVec2(
        rect.Min.x + std::clamp(x, 0.0f, 1.0f) * rect.GetWidth(),
        rect.Max.y - std::clamp(y, 0.0f, 1.0f) * rect.GetHeight());
}

ImVec2 ScreenToGraph(const ImRect& rect, const ImVec2& screen) {
    return ImVec2(
        std::clamp((screen.x - rect.Min.x) / std::max(1.0f, rect.GetWidth()), 0.0f, 1.0f),
        std::clamp((rect.Max.y - screen.y) / std::max(1.0f, rect.GetHeight()), 0.0f, 1.0f));
}

template <typename CoordinateMapper>
RawLabGraphHistogram BuildRawLabGraphHistogram(
    const RawDevelopmentGraphScopeReadback& scope,
    RawDevelopmentGraphScopeStage expectedStage,
    Raw::RawWorkingSpace workingSpace,
    CoordinateMapper&& coordinateMapper) {
    RawLabGraphHistogram histogram;
    if (!scope.valid || scope.stage != expectedStage ||
        scope.width <= 0 || scope.height <= 0 ||
        scope.pixels.size() <
            static_cast<std::size_t>(scope.width) *
                static_cast<std::size_t>(scope.height) * 3u) {
        return histogram;
    }

    const bool rec2020 = workingSpace == Raw::RawWorkingSpace::LinearRec2020D65;
    const float lumaR = rec2020 ? 0.2627f : 0.2126f;
    const float lumaG = rec2020 ? 0.6780f : 0.7152f;
    const float lumaB = rec2020 ? 0.0593f : 0.0722f;
    auto addSample = [&](std::array<float, kRawLabHistogramBinCount>& bins, float value) {
        if (!std::isfinite(value)) {
            return;
        }
        const float coordinate = std::clamp(coordinateMapper(value), 0.0f, 1.0f);
        const std::size_t bin = std::min<std::size_t>(
            kRawLabHistogramBinCount - 1,
            static_cast<std::size_t>(
                coordinate * static_cast<float>(kRawLabHistogramBinCount)));
        bins[bin] += 1.0f;
    };

    const std::size_t pixelCount =
        static_cast<std::size_t>(scope.width) * static_cast<std::size_t>(scope.height);
    for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const std::size_t base = pixelIndex * 3u;
        const float red = scope.pixels[base];
        const float green = scope.pixels[base + 1];
        const float blue = scope.pixels[base + 2];
        if (std::isfinite(red) && std::isfinite(green) && std::isfinite(blue)) {
            addSample(histogram.luma, lumaR * red + lumaG * green + lumaB * blue);
        }
        addSample(histogram.red, red);
        addSample(histogram.green, green);
        addSample(histogram.blue, blue);
    }
    histogram.valid = true;
    return histogram;
}

RawLabGraphHistogram BuildRawLabFinishToneHistogram(
    const RawDevelopmentGraphScopeReadback& scope,
    RawDevelopmentGraphScopeStage expectedStage,
    Raw::RawWorkingSpace workingSpace,
    int domain,
    float minimumEv,
    float maximumEv,
    float middleGrey) {
    const float evSpan = std::max(0.1f, maximumEv - minimumEv);
    const float safeMiddleGrey = std::max(0.000001f, middleGrey);
    return BuildRawLabGraphHistogram(
        scope,
        expectedStage,
        workingSpace,
        [&](float value) {
            if (domain == 0) {
                return value;
            }
            const float ev = std::log2(
                std::max(0.000001f, value) / safeMiddleGrey);
            return (ev - minimumEv) / evSpan;
        });
}

namespace {

const std::array<float, kRawLabHistogramBinCount>& HistogramBinsForCurve(
    const RawLabGraphHistogram& histogram,
    int activeCurve) {
    switch (std::clamp(activeCurve, 0, 3)) {
        case 1:
            return histogram.red;
        case 2:
            return histogram.green;
        case 3:
            return histogram.blue;
        default:
            return histogram.luma;
    }
}

std::array<float, kRawLabHistogramBinCount> CropHistogramBins(
    const std::array<float, kRawLabHistogramBinCount>& source,
    const RawLabToneGraphViewRange& viewRange) {
    std::array<float, kRawLabHistogramBinCount> cropped {};
    const float minimum = std::clamp(viewRange.minimum, 0.0f, 1.0f);
    const float maximum = std::clamp(
        std::max(minimum + 0.0001f, viewRange.maximum),
        0.0f,
        1.0f);
    const float viewSpan = std::max(0.0001f, maximum - minimum);
    constexpr float binWidth = 1.0f /
        static_cast<float>(kRawLabHistogramBinCount);

    for (std::size_t sourceIndex = 0;
         sourceIndex < kRawLabHistogramBinCount;
         ++sourceIndex) {
        const float count = source[sourceIndex];
        if (count <= 0.0f) {
            continue;
        }
        const float sourceMinimum = static_cast<float>(sourceIndex) * binWidth;
        const float sourceMaximum = sourceMinimum + binWidth;
        const float clippedMinimum = std::max(sourceMinimum, minimum);
        const float clippedMaximum = std::min(sourceMaximum, maximum);
        if (clippedMaximum <= clippedMinimum) {
            continue;
        }
        const int firstDestination = std::clamp(
            static_cast<int>(std::floor(
                (clippedMinimum - minimum) / viewSpan *
                static_cast<float>(kRawLabHistogramBinCount))),
            0,
            static_cast<int>(kRawLabHistogramBinCount) - 1);
        const int lastDestination = std::clamp(
            static_cast<int>(std::ceil(
                (clippedMaximum - minimum) / viewSpan *
                static_cast<float>(kRawLabHistogramBinCount))) - 1,
            0,
            static_cast<int>(kRawLabHistogramBinCount) - 1);
        for (int destinationIndex = firstDestination;
             destinationIndex <= lastDestination;
             ++destinationIndex) {
            const float destinationMinimum = minimum +
                static_cast<float>(destinationIndex) * binWidth * viewSpan;
            const float destinationMaximum = destinationMinimum +
                binWidth * viewSpan;
            const float overlap = std::max(
                0.0f,
                std::min(clippedMaximum, destinationMaximum) -
                    std::max(clippedMinimum, destinationMinimum));
            cropped[static_cast<std::size_t>(destinationIndex)] +=
                count * overlap / binWidth;
        }
    }
    return cropped;
}

} // namespace

RawLabToneGraphViewRange BuildRawLabToneGraphViewRange(
    const RawLabGraphHistogram& histogram,
    int activeCurve,
    float zoom) {
    RawLabToneGraphViewRange fitted;
    if (!histogram.valid) {
        return fitted;
    }

    const auto& bins = HistogramBinsForCurve(histogram, activeCurve);
    float total = 0.0f;
    for (const float count : bins) {
        total += std::max(0.0f, count);
    }
    if (total <= 0.0f) {
        return fitted;
    }

    // Ignore isolated histogram noise while retaining meaningful clipped black
    // and white pixels. The range is still data-derived, not recipe-derived.
    constexpr float ignoredTailFraction = 0.001f;
    const float ignoredTail = total * ignoredTailFraction;
    std::size_t lowerBin = 0;
    float accumulated = 0.0f;
    for (; lowerBin + 1 < bins.size(); ++lowerBin) {
        accumulated += std::max(0.0f, bins[lowerBin]);
        if (accumulated >= ignoredTail) {
            break;
        }
    }
    std::size_t upperBin = bins.size() - 1;
    accumulated = 0.0f;
    for (; upperBin > 0; --upperBin) {
        accumulated += std::max(0.0f, bins[upperBin]);
        if (accumulated >= ignoredTail) {
            break;
        }
    }

    const float binWidth = 1.0f /
        static_cast<float>(kRawLabHistogramBinCount);
    float minimum = static_cast<float>(lowerBin) * binWidth;
    float maximum = static_cast<float>(upperBin + 1) * binWidth;
    constexpr float minimumVisibleSpan = 8.0f /
        static_cast<float>(kRawLabHistogramBinCount);
    if (maximum - minimum < minimumVisibleSpan) {
        const float center = (minimum + maximum) * 0.5f;
        minimum = std::clamp(
            center - minimumVisibleSpan * 0.5f,
            0.0f,
            1.0f - minimumVisibleSpan);
        maximum = minimum + minimumVisibleSpan;
    }

    fitted.minimum = minimum;
    fitted.maximum = maximum;
    return ApplyRawLabToneGraphViewZoom(fitted, zoom);
}

RawLabToneGraphViewRange ApplyRawLabToneGraphViewZoom(
    const RawLabToneGraphViewRange& fittedRange,
    float zoom) {
    const float fittedMinimum = std::clamp(fittedRange.minimum, 0.0f, 1.0f);
    const float fittedMaximum = std::clamp(
        std::max(fittedMinimum, fittedRange.maximum),
        0.0f,
        1.0f);
    const float amount = std::clamp(zoom, 0.0f, 1.0f);
    return RawLabToneGraphViewRange{
        fittedMinimum * amount,
        1.0f - (1.0f - fittedMaximum) * amount
    };
}

RawLabGraphHistogram CropRawLabGraphHistogram(
    const RawLabGraphHistogram& histogram,
    const RawLabToneGraphViewRange& viewRange) {
    RawLabGraphHistogram cropped;
    cropped.valid = histogram.valid;
    if (!histogram.valid) {
        return cropped;
    }
    cropped.luma = CropHistogramBins(histogram.luma, viewRange);
    cropped.red = CropHistogramBins(histogram.red, viewRange);
    cropped.green = CropHistogramBins(histogram.green, viewRange);
    cropped.blue = CropHistogramBins(histogram.blue, viewRange);
    return cropped;
}

bool DrawRawLabRingDial(
    const char* id,
    float& value,
    float width,
    const char* tooltip,
    float doubleClickValue) {
    constexpr float dialHeight = 26.0f;
    ImGui::InvisibleButton(
        id,
        ImVec2(std::max(1.0f, width), dialHeight),
        ImGuiButtonFlags_MouseButtonLeft);
    const ImRect bounds(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const ImGuiID itemId = ImGui::GetItemID();
    bool changed = false;
    const auto setValue = [&](float requestedValue) {
        const float clamped = std::clamp(requestedValue, 0.0f, 1.0f);
        if (std::abs(value - clamped) > 0.0001f) {
            value = clamped;
            changed = true;
        }
    };
    if (ImGui::IsItemActivated()) {
        ImGui::GetStateStorage()->SetFloat(itemId, value);
    }
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        setValue(doubleClickValue);
    } else if (ImGui::IsItemActive()) {
        const float availableWidth = std::max(1.0f, bounds.GetWidth());
        const float dragStart = ImGui::GetStateStorage()->GetFloat(itemId, value);
        setValue(dragStart +
            ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x / availableWidth);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }

    const ImVec4 faded = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    constexpr float pillSpacing = 7.0f;
    // Translate the entire repeated ring pattern instead of using a slider
    // thumb. The dial is a direct visual echo of the horizontal drag.
    const float ringTravel = (std::clamp(value, 0.0f, 1.0f) - 1.0f) * 31.5f;
    const int pillExtent = static_cast<int>(std::ceil(
        bounds.GetWidth() / (pillSpacing * 2.0f))) + 6;
    drawList->PushClipRect(bounds.Min, bounds.Max, true);
    for (int index = -pillExtent; index <= pillExtent; ++index) {
        const float centerX = bounds.GetCenter().x +
            static_cast<float>(index) * pillSpacing + ringTravel;
        const float edgeDistance = std::abs(centerX - bounds.GetCenter().x) /
            std::max(1.0f, bounds.GetWidth() * 0.5f);
        const float centerWeight = std::clamp(1.0f - edgeDistance, 0.0f, 1.0f);
        const float alpha = 0.10f + 0.60f * centerWeight * centerWeight;
        const float height = 6.0f + 8.0f * centerWeight;
        ImVec4 pill = faded;
        pill.w = alpha;
        drawList->AddRectFilled(
            ImVec2(centerX - 0.8f, bounds.GetCenter().y - height * 0.5f),
            ImVec2(centerX + 0.8f, bounds.GetCenter().y + height * 0.5f),
            ImGui::GetColorU32(pill),
            0.8f);
    }
    drawList->PopClipRect();
    if (tooltip != nullptr && tooltip[0] != '\0' && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return changed;
}

RawLabGraphHistogram BuildRawLabZonesHistogram(
    const RawDevelopmentGraphScopeReadback& scope,
    float minimumEv,
    float maximumEv,
    float middleGrey) {
    RawLabGraphHistogram histogram;
    const std::size_t pixelCount =
        static_cast<std::size_t>(std::max(0, scope.width)) *
        static_cast<std::size_t>(std::max(0, scope.height));
    if (!scope.valid ||
        scope.stage != RawDevelopmentGraphScopeStage::LocalRangeInput ||
        scope.controlSignalDomain != "edge-aware-scene-ev" ||
        scope.width <= 0 || scope.height <= 0 ||
        scope.pixels.size() < pixelCount * 3u ||
        scope.controlSignal.size() != pixelCount) {
        return histogram;
    }

    const float evSpan = std::max(0.1f, maximumEv - minimumEv);
    const float safeMiddleGrey = std::max(0.000001f, middleGrey);
    auto addCoordinate = [](
                             std::array<float, kRawLabHistogramBinCount>& bins,
                             float coordinate) {
        if (!std::isfinite(coordinate)) {
            return;
        }
        coordinate = std::clamp(coordinate, 0.0f, 1.0f);
        const std::size_t bin = std::min<std::size_t>(
            kRawLabHistogramBinCount - 1,
            static_cast<std::size_t>(
                coordinate * static_cast<float>(kRawLabHistogramBinCount)));
        bins[bin] += 1.0f;
    };
    auto sceneValueCoordinate = [&](float value) {
        const float ev =
            std::log2(std::max(0.000001f, value) / safeMiddleGrey);
        return (ev - minimumEv) / evSpan;
    };

    for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const std::size_t base = pixelIndex * 3u;
        addCoordinate(
            histogram.luma,
            (scope.controlSignal[pixelIndex] - minimumEv) / evSpan);
        addCoordinate(histogram.red, sceneValueCoordinate(scope.pixels[base]));
        addCoordinate(histogram.green, sceneValueCoordinate(scope.pixels[base + 1]));
        addCoordinate(histogram.blue, sceneValueCoordinate(scope.pixels[base + 2]));
    }
    histogram.valid = true;
    return histogram;
}

std::array<float, kRawLabHistogramBinCount> BuildRawLabHistogramDensity(
    const std::array<float, kRawLabHistogramBinCount>& bins) {
    // The readback is intentionally discrete. Presenting each bin as an
    // individual filled rectangle makes that sampling visible as a picket
    // fence, especially in smooth photographic gradients. Filter in log
    // space so sparse shadow samples do not dominate the envelope.
    constexpr std::array<float, 7> kKernel = {
        1.0f, 6.0f, 15.0f, 20.0f, 15.0f, 6.0f, 1.0f,
    };
    std::array<float, kRawLabHistogramBinCount> density {};
    for (std::size_t index = 0; index < bins.size(); ++index) {
        float weightedValue = 0.0f;
        float totalWeight = 0.0f;
        for (int offset = -3; offset <= 3; ++offset) {
            const int neighbor = static_cast<int>(index) + offset;
            if (neighbor < 0 || neighbor >= static_cast<int>(bins.size())) {
                continue;
            }
            const float weight = kKernel[static_cast<std::size_t>(offset + 3)];
            weightedValue += weight * std::log1p(
                std::max(0.0f, bins[static_cast<std::size_t>(neighbor)]));
            totalWeight += weight;
        }
        density[index] = totalWeight > 0.0f
            ? weightedValue / totalWeight
            : 0.0f;
    }
    return density;
}

void DrawRawLabHistogramChannel(
    ImDrawList* drawList,
    const ImRect& rect,
    const std::array<float, kRawLabHistogramBinCount>& density,
    float normalization,
    const ImVec4& fill,
    const ImVec4& line) {
    if (normalization <= 0.0f || rect.GetWidth() <= 0.0f ||
        rect.GetHeight() <= 0.0f) {
        return;
    }
    std::array<ImVec2, kRawLabHistogramBinCount + 2> fillPoints {};
    fillPoints.front() = ImVec2(rect.Min.x, rect.Max.y);
    for (std::size_t index = 0; index < density.size(); ++index) {
        const float progress = static_cast<float>(index) /
            static_cast<float>(density.size() - 1);
        const float height = density[index] / normalization;
        const float top = rect.Max.y - std::clamp(height, 0.0f, 1.0f) * rect.GetHeight();
        fillPoints[index + 1] = ImVec2(
            rect.Min.x + progress * rect.GetWidth(),
            top);
    }
    fillPoints.back() = ImVec2(rect.Max.x, rect.Max.y);
    drawList->AddConcavePolyFilled(
        fillPoints.data(),
        static_cast<int>(fillPoints.size()),
        ImGui::GetColorU32(fill));
    drawList->AddPolyline(
        fillPoints.data() + 1,
        static_cast<int>(density.size()),
        ImGui::GetColorU32(line),
        ImDrawFlags_None,
        1.25f);
}

bool DrawRawLabHistogramBackdrop(
    ImDrawList* drawList,
    const ImRect& rect,
    const RawLabGraphHistogram& histogram,
    bool rgb,
    int emphasizedChannel = -1) {
    if (!histogram.valid) {
        const char* pending = "Scope pending";
        const ImVec2 labelSize = ImGui::CalcTextSize(pending);
        drawList->AddText(
            ImVec2(rect.Max.x - labelSize.x, rect.Min.y + 2.0f),
            ImGui::GetColorU32(ImVec4(
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled).x,
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled).y,
                ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled).z,
                0.55f)),
            pending);
        return false;
    }

    const auto redDensity = BuildRawLabHistogramDensity(histogram.red);
    const auto greenDensity = BuildRawLabHistogramDensity(histogram.green);
    const auto blueDensity = BuildRawLabHistogramDensity(histogram.blue);
    const auto lumaDensity = BuildRawLabHistogramDensity(histogram.luma);
    float maximum = 0.0f;
    auto includeMaximum = [&](const auto& density) {
        for (float value : density) {
            maximum = std::max(maximum, value);
        }
    };
    if (rgb) {
        includeMaximum(redDensity);
        includeMaximum(greenDensity);
        includeMaximum(blueDensity);
    } else {
        includeMaximum(lumaDensity);
    }
    const float normalization = maximum;
    if (rgb) {
        const float redScale = emphasizedChannel < 0 || emphasizedChannel == 0 ? 1.0f : 0.34f;
        const float greenScale = emphasizedChannel < 0 || emphasizedChannel == 1 ? 1.0f : 0.34f;
        const float blueScale = emphasizedChannel < 0 || emphasizedChannel == 2 ? 1.0f : 0.34f;
        DrawRawLabHistogramChannel(
            drawList, rect, redDensity, normalization,
            ImVec4(0.92f, 0.18f, 0.16f, 0.10f * redScale),
            ImVec4(0.96f, 0.28f, 0.24f, 0.34f * redScale));
        DrawRawLabHistogramChannel(
            drawList, rect, greenDensity, normalization,
            ImVec4(0.18f, 0.82f, 0.36f, 0.09f * greenScale),
            ImVec4(0.26f, 0.90f, 0.44f, 0.32f * greenScale));
        DrawRawLabHistogramChannel(
            drawList, rect, blueDensity, normalization,
            ImVec4(0.18f, 0.46f, 0.98f, 0.10f * blueScale),
            ImVec4(0.28f, 0.58f, 1.00f, 0.34f * blueScale));
    } else {
        DrawRawLabHistogramChannel(
            drawList, rect, lumaDensity, normalization,
            ImVec4(0.72f, 0.78f, 0.80f, 0.14f),
            ImVec4(0.82f, 0.88f, 0.90f, 0.34f));
    }
    return true;
}

ImRect RawLabLocalRangePlotRect(const ImRect& itemRect) {
    constexpr float kLeftAxisWidth = 24.0f;
    // The left gutter holds the vertical labels. Reserving the same amount on
    // the right keeps the plotted field centered in its graph surface.
    constexpr float kRightInset = kLeftAxisWidth;
    constexpr float kTopInset = 24.0f;
    constexpr float kBottomAxisHeight = 24.0f;
    return ImRect(
        ImVec2(itemRect.Min.x + kLeftAxisWidth, itemRect.Min.y + kTopInset),
        ImVec2(itemRect.Max.x - kRightInset, itemRect.Max.y - kBottomAxisHeight));
}

void DrawRawLabLocalRangeAxes(
    ImDrawList* drawList,
    const ImRect& itemRect,
    const ImRect& plotRect,
    float minimumEv,
    float maximumEv) {
    const ImVec4 disabled = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const ImU32 labelColor = ImGui::GetColorU32(
        ImVec4(disabled.x, disabled.y, disabled.z, 0.72f));
    auto addRightAligned = [&](const char* label, float right, float y) {
        const ImVec2 size = ImGui::CalcTextSize(label);
        drawList->AddText(ImVec2(right - size.x, y), labelColor, label);
    };

    const ImVec2 topSize = ImGui::CalcTextSize("+4");
    addRightAligned("+4", plotRect.Min.x - 4.0f, plotRect.Min.y - 1.0f);
    addRightAligned(
        "0",
        plotRect.Min.x - 4.0f,
        plotRect.GetCenter().y - topSize.y * 0.5f);
    addRightAligned(
        "-4",
        plotRect.Min.x - 4.0f,
        plotRect.Max.y - topSize.y + 1.0f);

    char minimumLabel[24] {};
    char maximumLabel[24] {};
    std::snprintf(minimumLabel, sizeof(minimumLabel), "%+.0f", minimumEv);
    std::snprintf(maximumLabel, sizeof(maximumLabel), "%+.0f EV", maximumEv);
    const float labelY = plotRect.Max.y + 3.0f;
    drawList->AddText(ImVec2(plotRect.Min.x, labelY), labelColor, minimumLabel);

    if (minimumEv < 0.0f && maximumEv > 0.0f) {
        const float zeroX = plotRect.Min.x +
            (-minimumEv / std::max(0.1f, maximumEv - minimumEv)) *
                plotRect.GetWidth();
        const ImVec2 zeroSize = ImGui::CalcTextSize("0");
        drawList->AddText(
            ImVec2(zeroX - zeroSize.x * 0.5f, labelY),
            labelColor,
            "0");
    }
    const ImVec2 maximumSize = ImGui::CalcTextSize(maximumLabel);
    drawList->AddText(
        ImVec2(plotRect.Max.x - maximumSize.x, labelY),
        labelColor,
        maximumLabel);

    (void)itemRect;
}

float CurvePointSegmentDistanceSquared(
    const ImVec2& point,
    const ImVec2& lineStart,
    const ImVec2& lineEnd) {
    const ImVec2 line(lineEnd.x - lineStart.x, lineEnd.y - lineStart.y);
    const float lengthSquared = line.x * line.x + line.y * line.y;
    const float projection = lengthSquared > 0.0001f
        ? std::clamp(
              ((point.x - lineStart.x) * line.x +
               (point.y - lineStart.y) * line.y) / lengthSquared,
              0.0f,
              1.0f)
        : 0.0f;
    const float nearestX = lineStart.x + line.x * projection;
    const float nearestY = lineStart.y + line.y * projection;
    const float dx = point.x - nearestX;
    const float dy = point.y - nearestY;
    return dx * dx + dy * dy;
}

ImVec2 CurveCubicPoint(
    const ImVec2& p0,
    const ImVec2& p1,
    const ImVec2& p2,
    const ImVec2& p3,
    float t) {
    const float inverse = 1.0f - t;
    return ImVec2(
        inverse * inverse * inverse * p0.x +
            3.0f * inverse * inverse * t * p1.x +
            3.0f * inverse * t * t * p2.x + t * t * t * p3.x,
        inverse * inverse * inverse * p0.y +
            3.0f * inverse * inverse * t * p1.y +
            3.0f * inverse * t * t * p2.y + t * t * t * p3.y);
}

void ClearCurveSegmentSelection(
    RawCurveGraphUiState& state,
    int& selectedPoint) {
    state.selectedSegment = -1;
    state.selectedSide = -1;
    state.chooserOpen = false;
    state.draggingPoint = -1;
    state.draggingSegment = -1;
    state.draggingSide = -1;
    selectedPoint = -1;
}

void DrawCurveTangentGuide(
    ImDrawList* drawList,
    const ImRect& rect,
    const std::vector<Stack::RawRecipe::RawBezierCurvePoint>& points,
    int segmentIndex,
    int side,
    ImU32 color) {
    if (segmentIndex < 0 ||
        segmentIndex + 1 >= static_cast<int>(points.size())) {
        return;
    }
    const int anchorIndex = side == 0 ? segmentIndex : segmentIndex + 1;
    const int neighborIndex = side == 0
        ? (anchorIndex > 0 ? anchorIndex - 1 : anchorIndex + 1)
        : (anchorIndex + 1 < static_cast<int>(points.size())
            ? anchorIndex + 1
            : anchorIndex - 1);
    const ImVec2 anchor = GraphToScreen(
        rect,
        points[static_cast<std::size_t>(anchorIndex)].x,
        points[static_cast<std::size_t>(anchorIndex)].y);
    const ImVec2 neighbor = GraphToScreen(
        rect,
        points[static_cast<std::size_t>(neighborIndex)].x,
        points[static_cast<std::size_t>(neighborIndex)].y);
    ImVec2 direction(neighbor.x - anchor.x, neighbor.y - anchor.y);
    const float length = std::sqrt(
        direction.x * direction.x + direction.y * direction.y);
    if (length <= 0.001f) {
        return;
    }
    direction.x /= length;
    direction.y /= length;
    drawList->PushClipRect(rect.Min, rect.Max, true);
    for (float offset = -140.0f; offset < 140.0f; offset += 12.0f) {
        const ImVec2 start(
            anchor.x + direction.x * offset,
            anchor.y + direction.y * offset);
        const ImVec2 end(
            anchor.x + direction.x * (offset + 5.0f),
            anchor.y + direction.y * (offset + 5.0f));
        drawList->AddLine(start, end, color, 1.0f);
    }
    drawList->PopClipRect();
}

bool DrawBezierCurveInteraction(
    const ImRect& rect,
    std::vector<Stack::RawRecipe::RawBezierCurvePoint>& points,
    RawCurveGraphUiState& state,
    int& selectedPoint,
    ImU32 normalCurveColor,
    ImU32 pointColor,
    int maximumPoints,
    const std::function<Stack::RawRecipe::RawBezierSegment(const std::vector<Stack::RawRecipe::RawBezierCurvePoint>&, std::size_t)>& segmentBuilder,
    bool preserveEndpointX,
    float* exposureEv) {
    constexpr float pointHitRadius = 24.0f;
    constexpr float handleHitRadius = 11.0f;
    constexpr float segmentHitRadius = 11.0f;
    constexpr float minimumPointSpacingPixels = 4.0f;
    constexpr int segmentSamples = 64;
    bool changed = false;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (points.size() < 2u) {
        return false;
    }

    const int segmentCount = static_cast<int>(points.size()) - 1;
    // -1 is the normal state while a graph point (rather than a segment) is
    // selected or being dragged. Clearing it every frame also cleared the
    // point drag immediately after mouse-down, before any movement arrived.
    if (state.selectedSegment < -1 || state.selectedSegment >= segmentCount) {
        ClearCurveSegmentSelection(state, selectedPoint);
    }
    if (selectedPoint >= static_cast<int>(points.size())) {
        selectedPoint = -1;
    }

    const auto toScreen = [&](const ImRect& bounds, float x, float y) {
        if (!segmentBuilder) return GraphToScreen(bounds, x, y);
        return ImVec2(bounds.Min.x + x * bounds.GetWidth(), bounds.Max.y - y * bounds.GetHeight());
    };
    auto screenSegment = [&](int index) {
        const Stack::RawRecipe::RawBezierSegment segment =
            (segmentBuilder ? segmentBuilder(points, static_cast<std::size_t>(index)) :
                Stack::RawRecipe::BuildRawBezierSegment(points, static_cast<std::size_t>(index)));
        return std::array<ImVec2, 4>{
            toScreen(rect, segment.left.x, segment.left.y),
            toScreen(rect, segment.leftHandleX, segment.leftHandleY),
            toScreen(rect, segment.rightHandleX, segment.rightHandleY),
            toScreen(rect, segment.right.x, segment.right.y)
        };
    };
    auto findPoint = [&](const ImVec2& mouse) {
        int result = -1;
        float bestDistance = pointHitRadius * pointHitRadius;
        for (int index = 0; index < static_cast<int>(points.size()); ++index) {
            const ImVec2 screen = toScreen(
                rect,
                points[static_cast<std::size_t>(index)].x,
                points[static_cast<std::size_t>(index)].y);
            const float dx = mouse.x - screen.x;
            const float dy = mouse.y - screen.y;
            const float distance = dx * dx + dy * dy;
            if (distance <= bestDistance) {
                bestDistance = distance;
                result = index;
            }
        }
        return result;
    };
    auto findSegment = [&](const ImVec2& mouse) {
        int result = -1;
        float bestDistance = segmentHitRadius * segmentHitRadius;
        for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
            const auto screen = screenSegment(segmentIndex);
            ImVec2 previous = screen[0];
            for (int sample = 1; sample <= segmentSamples; ++sample) {
                const ImVec2 current = CurveCubicPoint(
                    screen[0],
                    screen[1],
                    screen[2],
                    screen[3],
                    static_cast<float>(sample) /
                        static_cast<float>(segmentSamples));
                const float distance = CurvePointSegmentDistanceSquared(
                    mouse,
                    previous,
                    current);
                if (distance <= bestDistance) {
                    bestDistance = distance;
                    result = segmentIndex;
                }
                previous = current;
            }
        }
        return result;
    };

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool itemHovered = ImGui::IsItemHovered();
    const bool inside = rect.Contains(mouse) && itemHovered;
    const bool handleDragActive =
        state.draggingSegment >= 0 && state.draggingSide >= 0;
    const bool pointDragActive = state.draggingPoint >= 0;
    if ((handleDragActive || pointDragActive) &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }
    int hoveredPoint = itemHovered && !handleDragActive && !state.draggingExposure ? findPoint(mouse) : -1;
    int hoveredSegment =
        !state.draggingExposure && !handleDragActive && !pointDragActive && inside && hoveredPoint < 0
            ? findSegment(mouse)
            : -1;

    ImRect chooserRect;
    bool chooserContainsMouse = false;
    if (state.chooserOpen && state.selectedSegment >= 0) {
        const auto selectedScreen = screenSegment(state.selectedSegment);
        const ImVec2 center = CurveCubicPoint(
            selectedScreen[0],
            selectedScreen[1],
            selectedScreen[2],
            selectedScreen[3],
            0.5f);
        constexpr float chooserWidth = 102.0f;
        constexpr float chooserHeight = 25.0f;
        const float chooserX = std::clamp(
            center.x - chooserWidth * 0.5f,
            rect.Min.x + 2.0f,
            rect.Max.x - chooserWidth - 2.0f);
        const float preferredY = center.y - chooserHeight - 10.0f;
        const float chooserY = std::clamp(
            preferredY,
            rect.Min.y + 2.0f,
            rect.Max.y - chooserHeight - 2.0f);
        chooserRect = ImRect(
            ImVec2(chooserX, chooserY),
            ImVec2(chooserX + chooserWidth, chooserY + chooserHeight));
        chooserContainsMouse = chooserRect.Contains(mouse);
        if (chooserContainsMouse) {
            state.chooserHotSide =
                mouse.x < chooserRect.GetCenter().x ? 0 : 1;
        }
    }

    ImVec2 activeHandleScreen;
    bool activeHandleValid = false;
    if (state.selectedSegment >= 0 && state.selectedSide >= 0) {
        const auto selectedScreen = screenSegment(state.selectedSegment);
        activeHandleScreen = state.selectedSide == 0
            ? selectedScreen[1]
            : selectedScreen[2];
        activeHandleValid = true;
    }
    const bool handleHovered = activeHandleValid &&
        (mouse.x - activeHandleScreen.x) * (mouse.x - activeHandleScreen.x) +
            (mouse.y - activeHandleScreen.y) * (mouse.y - activeHandleScreen.y) <=
        handleHitRadius * handleHitRadius;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (!inside && hoveredPoint < 0 && !chooserContainsMouse) {
            ClearCurveSegmentSelection(state, selectedPoint);
            state.draggingPoint = -1;
        } else if (chooserContainsMouse) {
            state.selectedSide = state.chooserHotSide;
            state.chooserOpen = false;
            selectedPoint = state.selectedSide == 0
                ? state.selectedSegment
                : state.selectedSegment + 1;
        } else if (handleHovered) {
            state.draggingSegment = state.selectedSegment;
            state.draggingSide = state.selectedSide;
        } else if (hoveredPoint >= 0) {
            ClearCurveSegmentSelection(state, selectedPoint);
            selectedPoint = hoveredPoint;
            state.draggingPoint = hoveredPoint;
            state.dragMouseStart = mouse;
            state.dragPointStart = ImVec2(
                points[static_cast<std::size_t>(hoveredPoint)].x,
                points[static_cast<std::size_t>(hoveredPoint)].y);
        } else if (hoveredSegment >= 0) {
            state.selectedSegment = hoveredSegment;
            state.selectedSide = -1;
            state.chooserOpen = true;
            state.chooserHotSide = mouse.x <
                    (screenSegment(hoveredSegment)[0].x +
                     screenSegment(hoveredSegment)[3].x) * 0.5f
                ? 0
                : 1;
            selectedPoint = -1;
        } else if (inside) {
            ClearCurveSegmentSelection(state, selectedPoint);
            if (exposureEv && ImGui::IsItemActive()) {
                state.draggingExposure = true;
                state.exposureStart = *exposureEv;
                state.exposureMouseStartY = mouse.y;
            }
        }
    }

    if (state.draggingExposure && exposureEv) {
        const bool cancel = ImGui::GetIO().AppFocusLost ||
            (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false));
        if (cancel) {
            *exposureEv = state.exposureStart;
            state.draggingExposure = false;
            ImGui::ClearActiveID();
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsItemActive()) {
            if (ImGui::IsMousePosValid()) {
                *exposureEv = std::clamp(state.exposureStart +
                    (state.exposureMouseStartY - mouse.y) / 120.0f, -16.0f, 16.0f);
            }
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            char text[32];
            std::snprintf(text, sizeof(text), "%+.2f EV", *exposureEv);
            ImGui::GetForegroundDrawList()->AddText(ImVec2(mouse.x + 16, mouse.y + 16),
                ImGui::GetColorU32(ImGuiCol_Text), text);
        } else {
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::IsMousePosValid()) {
                *exposureEv = std::clamp(state.exposureStart +
                    (state.exposureMouseStartY - mouse.y) / 120.0f, -16.0f, 16.0f);
            } else {
                // Losing ownership while still held is cancellation, not a
                // chance to re-anchor on a point or a newly opened surface.
                *exposureEv = state.exposureStart;
            }
            state.draggingExposure = false;
        }
    }

    if (!state.draggingExposure && inside && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const int pointUnderMouse = findPoint(mouse);
        if (pointUnderMouse > 0 &&
            pointUnderMouse + 1 < static_cast<int>(points.size())) {
            points.erase(points.begin() + pointUnderMouse);
            if (pointUnderMouse > 0 &&
                pointUnderMouse < static_cast<int>(points.size())) {
                points[static_cast<std::size_t>(pointUnderMouse - 1)].outgoing = {};
                points[static_cast<std::size_t>(pointUnderMouse)].incoming = {};
            }
            state = {};
            selectedPoint = -1;
            changed = true;
        } else if (pointUnderMouse < 0 &&
                   points.size() < static_cast<std::size_t>(maximumPoints)) {
            const ImVec2 graphPoint = ScreenToGraph(rect, mouse);
            const float minimumSpacing = std::max(
                0.002f,
                minimumPointSpacingPixels / std::max(1.0f, rect.GetWidth()));
            const auto insertion = std::lower_bound(
                points.begin(),
                points.end(),
                graphPoint.x,
                [](const Stack::RawRecipe::RawBezierCurvePoint& point, float x) {
                    return point.x < x;
                });
            const int insertionIndex = static_cast<int>(
                std::distance(points.begin(), insertion));
            if ((segmentBuilder || (insertionIndex > 0 && insertionIndex < static_cast<int>(points.size()))) &&
                (insertionIndex == 0 || graphPoint.x >= points[static_cast<std::size_t>(insertionIndex - 1)].x + minimumSpacing) &&
                (insertionIndex == static_cast<int>(points.size()) || graphPoint.x <= points[static_cast<std::size_t>(insertionIndex)].x - minimumSpacing)) {
                Stack::RawRecipe::RawBezierCurvePoint added;
                added.x = graphPoint.x;
                added.y = graphPoint.y;
                if (insertionIndex > 0) points[static_cast<std::size_t>(insertionIndex - 1)].outgoing = {};
                if (insertionIndex < static_cast<int>(points.size())) points[static_cast<std::size_t>(insertionIndex)].incoming = {};
                points.insert(insertion, added);
                state = {};
                selectedPoint = insertionIndex;
                changed = true;
            }
        }
    }

    if (state.draggingPoint >= 0 &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.5f)) {
        const int pointIndex = state.draggingPoint;
        const ImVec2 travel(
            mouse.x - state.dragMouseStart.x,
            mouse.y - state.dragMouseStart.y);
        ImVec2 requested(
            state.dragPointStart.x +
                travel.x /
                    std::max(1.0f, rect.GetWidth()),
            state.dragPointStart.y -
                travel.y /
                    std::max(1.0f, rect.GetHeight()));
        const bool endpoint = pointIndex == 0 ||
            pointIndex + 1 == static_cast<int>(points.size());
        const float minimumSpacing = std::max(
            0.002f,
            minimumPointSpacingPixels / std::max(1.0f, rect.GetWidth()));
        const float minimumX = pointIndex == 0
            ? 0.0f
            : points[static_cast<std::size_t>(pointIndex - 1)].x + minimumSpacing;
        const float maximumX = pointIndex + 1 == static_cast<int>(points.size())
            ? 1.0f
            : points[static_cast<std::size_t>(pointIndex + 1)].x - minimumSpacing;
        requested.x = std::clamp(requested.x, minimumX, maximumX);
        requested.y = std::clamp(requested.y, 0.0f, 1.0f);
        if (endpoint && preserveEndpointX) {
            // Boundary anchors own the domain ends. Their output can be
            // edited, but their x coordinate stays on its graph edge.
            requested.x = state.dragPointStart.x;
        }
        auto& point = points[static_cast<std::size_t>(pointIndex)];
        if (std::abs(point.x - requested.x) > 0.0001f ||
            std::abs(point.y - requested.y) > 0.0001f) {
            point.x = requested.x;
            point.y = requested.y;
            changed = true;
        }
    }

    if (state.draggingSegment >= 0 && state.draggingSide >= 0 &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const int segmentIndex = state.draggingSegment;
        const ImVec2 requested = ScreenToGraph(rect, mouse);
        Stack::RawRecipe::RawBezierSegment segment =
            segmentBuilder ? segmentBuilder(points, static_cast<std::size_t>(segmentIndex)) :
                Stack::RawRecipe::BuildRawBezierSegment(points, static_cast<std::size_t>(segmentIndex));
        if (state.draggingSide == 0) {
            const float handleX = std::clamp(
                requested.x,
                segment.left.x,
                segment.rightHandleX);
            auto& handle = points[static_cast<std::size_t>(segmentIndex)].outgoing;
            const float offsetX = handleX - segment.left.x;
            const float offsetY =
                std::clamp(requested.y, 0.0f, 1.0f) - segment.left.y;
            if (!handle.manual || std::abs(handle.strength - 1.0f) > 0.0001f ||
                std::abs(handle.offsetX - offsetX) > 0.0001f ||
                std::abs(handle.offsetY - offsetY) > 0.0001f) {
                handle.manual = true;
                handle.strength = 1.0f;
                handle.offsetX = offsetX;
                handle.offsetY = offsetY;
                changed = true;
            }
        } else {
            const float handleX = std::clamp(
                requested.x,
                segment.leftHandleX,
                segment.right.x);
            auto& handle = points[static_cast<std::size_t>(segmentIndex + 1)].incoming;
            const float offsetX = handleX - segment.right.x;
            const float offsetY =
                std::clamp(requested.y, 0.0f, 1.0f) - segment.right.y;
            if (!handle.manual || std::abs(handle.strength - 1.0f) > 0.0001f ||
                std::abs(handle.offsetX - offsetX) > 0.0001f ||
                std::abs(handle.offsetY - offsetY) > 0.0001f) {
                handle.manual = true;
                handle.strength = 1.0f;
                handle.offsetX = offsetX;
                handle.offsetY = offsetY;
                changed = true;
            }
        }
    }

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        state.draggingPoint = -1;
        state.draggingSegment = -1;
        state.draggingSide = -1;
    }

    if (hoveredSegment >= 0) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        const float wheel = ImGui::GetIO().MouseWheel;
        if (std::abs(wheel) > 0.0001f) {
            const float increment = ImGui::GetIO().KeyShift ? 0.02f : 0.10f;
            auto adjust = [&](Stack::RawRecipe::RawBezierHandleState& handle) {
                handle.strength = std::clamp(
                    handle.strength + wheel * increment,
                    0.0f,
                    1.5f);
            };
            if (state.selectedSegment == hoveredSegment &&
                state.selectedSide == 0) {
                adjust(points[static_cast<std::size_t>(hoveredSegment)].outgoing);
            } else if (state.selectedSegment == hoveredSegment &&
                       state.selectedSide == 1) {
                adjust(points[static_cast<std::size_t>(hoveredSegment + 1)].incoming);
            } else {
                adjust(points[static_cast<std::size_t>(hoveredSegment)].outgoing);
                adjust(points[static_cast<std::size_t>(hoveredSegment + 1)].incoming);
            }
            changed = true;
        }
    }

    const ImU32 hoverColor = ImGui::GetColorU32(
        ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab));
    const ImU32 selectedColor = ImGui::GetColorU32(
        ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive));
    for (int segmentIndex = 0; segmentIndex + 1 < static_cast<int>(points.size()); ++segmentIndex) {
        const auto screen = screenSegment(segmentIndex);
        const bool selected = state.selectedSegment == segmentIndex;
        const bool hovered = hoveredSegment == segmentIndex;
        drawList->AddBezierCubic(
            screen[0],
            screen[1],
            screen[2],
            screen[3],
            selected ? selectedColor : (hovered ? hoverColor : normalCurveColor),
            selected ? 2.8f : (hovered ? 2.35f : 1.65f));
    }

    if (state.selectedSegment >= 0 && state.selectedSide >= 0) {
        const auto selectedScreen = screenSegment(state.selectedSegment);
        const ImVec2 anchor = state.selectedSide == 0
            ? selectedScreen[0]
            : selectedScreen[3];
        const ImVec2 handle = state.selectedSide == 0
            ? selectedScreen[1]
            : selectedScreen[2];
        const auto& handleState = state.selectedSide == 0
            ? points[static_cast<std::size_t>(state.selectedSegment)].outgoing
            : points[static_cast<std::size_t>(state.selectedSegment + 1)].incoming;
        if (handleState.strength > 1.0f) {
            DrawCurveTangentGuide(
                drawList,
                rect,
                points,
                state.selectedSegment,
                state.selectedSide,
                ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.45f));
        }
        drawList->AddLine(anchor, handle, selectedColor, 1.25f);
        drawList->AddCircleFilled(handle, handleHovered ? 5.5f : 4.5f, selectedColor, 20);
        drawList->AddCircle(anchor, 7.0f, selectedColor, 24, 2.0f);
    }

    for (int index = 0; index < static_cast<int>(points.size()); ++index) {
        const ImVec2 point = toScreen(
            rect,
            points[static_cast<std::size_t>(index)].x,
            points[static_cast<std::size_t>(index)].y);
        const bool activeAnchor = state.selectedSegment >= 0 &&
            state.selectedSide >= 0 &&
            index == (state.selectedSide == 0
                ? state.selectedSegment
                : state.selectedSegment + 1);
        drawList->AddCircleFilled(
            point,
            selectedPoint == index || activeAnchor ? 5.2f : 3.8f,
            activeAnchor ? selectedColor : pointColor,
            20);
    }

    if (state.chooserOpen && state.selectedSegment >= 0) {
        const ImU32 chooserBackground = ImGui::GetColorU32(
            ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
        const ImU32 chooserBorder = ImGui::GetColorU32(ImGuiCol_Border);
        drawList->AddRectFilled(chooserRect.Min, chooserRect.Max, chooserBackground, 4.0f);
        drawList->AddRect(chooserRect.Min, chooserRect.Max, chooserBorder, 4.0f);
        const float middleX = chooserRect.GetCenter().x;
        const ImRect leftRect(chooserRect.Min, ImVec2(middleX, chooserRect.Max.y));
        const ImRect rightRect(ImVec2(middleX, chooserRect.Min.y), chooserRect.Max);
        const ImRect hotRect = state.chooserHotSide == 0 ? leftRect : rightRect;
        drawList->AddRectFilled(
            hotRect.Min,
            hotRect.Max,
            ImGui::GetColorU32(ImGuiCol_HeaderHovered),
            3.0f);
        drawList->AddLine(
            ImVec2(middleX, chooserRect.Min.y + 3.0f),
            ImVec2(middleX, chooserRect.Max.y - 3.0f),
            chooserBorder);
        const ImVec2 leftText = ImGui::CalcTextSize("LEFT");
        const ImVec2 rightText = ImGui::CalcTextSize("RIGHT");
        drawList->AddText(
            ImVec2(leftRect.GetCenter().x - leftText.x * 0.5f,
                   leftRect.GetCenter().y - leftText.y * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text),
            "LEFT");
        drawList->AddText(
            ImVec2(rightRect.GetCenter().x - rightText.x * 0.5f,
                   rightRect.GetCenter().y - rightText.y * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text),
            "RIGHT");
    }
    return changed;
}

bool DrawFramelessToneCurveBezier(
    const char* id,
    Stack::RawRecipe::RawPointCurveComponent& component,
    RawCurveGraphUiState& interaction,
    int& selectedPoint,
    int activeCurve,
    const RawLabGraphHistogram& histogram,
    bool rgbHistogram,
    const ImVec2& size,
    const RawLabToneGraphViewRange& viewRange) {
    ImGui::InvisibleButton(
        id,
        size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    rect.Expand(-24.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (activeCurve > 0) {
        const ImVec4 channelTints[] = {
            ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
            ImVec4(0.96f, 0.18f, 0.14f, 0.050f),
            ImVec4(0.18f, 0.88f, 0.32f, 0.045f),
            ImVec4(0.16f, 0.42f, 1.00f, 0.055f)
        };
        const ImVec4 complementTints[] = {
            ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
            ImVec4(0.10f, 0.88f, 0.90f, 0.042f),
            ImVec4(0.92f, 0.16f, 0.82f, 0.038f),
            ImVec4(0.96f, 0.82f, 0.12f, 0.045f)
        };
        drawList->AddTriangleFilled(
            rect.Min,
            ImVec2(rect.Max.x, rect.Min.y),
            ImVec2(rect.Min.x, rect.Max.y),
            ImGui::GetColorU32(channelTints[activeCurve]));
        drawList->AddTriangleFilled(
            ImVec2(rect.Max.x, rect.Min.y),
            rect.Max,
            ImVec2(rect.Min.x, rect.Max.y),
            ImGui::GetColorU32(complementTints[activeCurve]));
    }
    DrawRawLabHistogramBackdrop(
        drawList,
        rect,
        histogram,
        rgbHistogram || activeCurve > 0,
        activeCurve > 0 ? activeCurve - 1 : -1);
    drawList->AddLine(
        ImVec2(rect.Min.x, rect.Max.y),
        ImVec2(rect.Max.x, rect.Min.y),
        ImGui::GetColorU32(ImVec4(0.78f, 0.82f, 0.84f, 0.22f)),
        1.0f);

    const ImVec4 curveColors[] = {
        ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive),
        ImVec4(1.00f, 0.34f, 0.30f, 1.0f),
        ImVec4(0.34f, 0.94f, 0.48f, 1.0f),
        ImVec4(0.38f, 0.62f, 1.00f, 1.0f)
    };
    const float viewMinimum = std::clamp(viewRange.minimum, 0.0f, 1.0f);
    const float viewMaximum = std::clamp(
        std::max(viewMinimum + 0.0001f, viewRange.maximum),
        0.0f,
        1.0f);
    const float viewSpan = std::max(0.0001f, viewMaximum - viewMinimum);
    const bool croppedView = viewMinimum > 0.0001f || viewMaximum < 0.9999f;
    std::vector<Stack::RawRecipe::RawBezierCurvePoint> points =
        Stack::RawRecipe::RawPointCurveBezierPoints(component);
    if (croppedView) {
        for (auto& point : points) {
            point.x = (point.x - viewMinimum) / viewSpan;
            point.y = (point.y - viewMinimum) / viewSpan;
            point.incoming.offsetX /= viewSpan;
            point.incoming.offsetY /= viewSpan;
            point.outgoing.offsetX /= viewSpan;
            point.outgoing.offsetY /= viewSpan;
        }
    }
    drawList->PushClipRect(rect.Min, rect.Max, true);
    const bool changed = croppedView
        ? DrawBezierCurveInteraction(
              rect,
              points,
              interaction,
              selectedPoint,
              ImGui::GetColorU32(curveColors[activeCurve]),
              ImGui::GetColorU32(ImGuiCol_Text),
              kRawLabCurveMaxPoints,
              [](const std::vector<Stack::RawRecipe::RawBezierCurvePoint>& displayed,
                 std::size_t index) {
                  return Stack::RawRecipe::BuildRawBezierSegment(displayed, index);
              },
              true)
        : DrawBezierCurveInteraction(
              rect,
              points,
              interaction,
              selectedPoint,
              ImGui::GetColorU32(curveColors[activeCurve]),
              ImGui::GetColorU32(ImGuiCol_Text),
              kRawLabCurveMaxPoints,
              {},
              true);
    drawList->PopClipRect();
    if (changed) {
        if (croppedView) {
            for (auto& point : points) {
                point.x = std::clamp(
                    viewMinimum + point.x * viewSpan,
                    0.0f,
                    1.0f);
                point.y = std::clamp(
                    viewMinimum + point.y * viewSpan,
                    0.0f,
                    1.0f);
                point.incoming.offsetX *= viewSpan;
                point.incoming.offsetY *= viewSpan;
                point.outgoing.offsetX *= viewSpan;
                point.outgoing.offsetY *= viewSpan;
            }
        }
        component.interpolation = "bezier-segments-v1";
        component.points.clear();
        component.points.reserve(points.size());
        for (const Stack::RawRecipe::RawBezierCurvePoint& point : points) {
            Stack::RawRecipe::RawPointCurveControlPoint stored;
            stored.x = point.x;
            stored.y = point.y;
            stored.shape = 1;
            stored.incoming = point.incoming;
            stored.outgoing = point.outgoing;
            component.points.push_back(stored);
        }
    }
    return changed;
}

bool DrawFramelessLocalRangeBezier(
    const char* id,
    Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    RawCurveGraphUiState& interaction,
    int& selectedPoint,
    std::string& selectedTargetZoneId,
    const RawLabGraphHistogram& histogram,
    bool rgbHistogram,
    const ImVec2& size,
    bool drawTargetZones,
    float* exposureEv,
    bool* curveChanged) {
    localRange = Stack::RawRecipe::SanitizeLocalRangeRecipe(std::move(localRange));
    ImGui::InvisibleButton(
        id,
        size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImRect itemRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const ImRect rect = RawLabLocalRangePlotRect(itemRect);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    DrawRawLabHistogramBackdrop(drawList, rect, histogram, rgbHistogram);
    DrawRawLabLocalRangeAxes(
        drawList,
        itemRect,
        rect,
        localRange.minEv,
        localRange.maxEv);
    const ImU32 baseColor = ImGui::GetColorU32(
        ImVec4(0.72f, 0.76f, 0.78f, 0.52f));
    const float zeroY = GraphToScreen(rect, 0.0f, 0.5f).y;
    drawList->AddLine(
        ImVec2(rect.Min.x, zeroY),
        ImVec2(rect.Max.x, zeroY),
        baseColor);

    const float evSpan = std::max(0.1f, localRange.maxEv - localRange.minEv);
    if (drawTargetZones) {
        for (const Stack::RawRecipe::RawLocalRangeTargetZone& zone :
             localRange.targetZones) {
            if (!zone.enabled) continue;
            const bool selected = zone.id == selectedTargetZoneId;
            const ImU32 fillColor = ImGui::GetColorU32(
                selected
                    ? ImVec4(0.10f, 0.78f, 0.72f, 0.16f)
                    : ImVec4(0.30f, 0.64f, 0.66f, 0.08f));
            const ImU32 lineColor = ImGui::GetColorU32(
                selected ? ImGuiCol_SliderGrabActive : ImGuiCol_TextDisabled);
            constexpr int lobeSamples = 48;
            ImVec2 previous;
            for (int sampleIndex = 0; sampleIndex < lobeSamples; ++sampleIndex) {
                const float u = static_cast<float>(sampleIndex) /
                    static_cast<float>(lobeSamples - 1);
                const float sampleEv = std::clamp(
                    zone.centerEv - zone.coreHalfWidthEv - zone.featherEv +
                        u * 2.0f * (zone.coreHalfWidthEv + zone.featherEv),
                    localRange.minEv,
                    localRange.maxEv);
                const float sampleDelta = zone.deltaEv *
                    Stack::RawRecipe::EvaluateLocalRangeTargetZoneTonalWeight(
                        zone,
                        sampleEv);
                const ImVec2 point = GraphToScreen(
                    rect,
                    (sampleEv - localRange.minEv) / evSpan,
                    (sampleDelta + 4.0f) / 8.0f);
                if (sampleIndex > 0) {
                    drawList->AddLine(
                        previous,
                        point,
                        lineColor,
                        selected ? 2.4f : 1.3f);
                    drawList->AddQuadFilled(
                        previous,
                        point,
                        ImVec2(point.x, zeroY),
                        ImVec2(previous.x, zeroY),
                        fillColor);
                }
                previous = point;
            }
        }
    }

    std::vector<Stack::RawRecipe::RawBezierCurvePoint> points =
        Stack::RawRecipe::RawLocalRangeBezierPoints(localRange);
    const float previousExposure = exposureEv ? *exposureEv : 0.0f;
    const bool changed = DrawBezierCurveInteraction(
        rect,
        points,
        interaction,
        selectedPoint,
        ImGui::GetColorU32(ImGuiCol_SliderGrabActive),
        ImGui::GetColorU32(ImGuiCol_Text),
        kRawLabCurveMaxPoints,
        {},
        true,
        exposureEv);
    if (curveChanged) *curveChanged = changed;
    if (changed) {
        localRange.points.clear();
        localRange.points.reserve(points.size());
        for (const Stack::RawRecipe::RawBezierCurvePoint& point : points) {
            Stack::RawRecipe::RawLocalRangePoint stored;
            stored.ev = localRange.minEv + point.x * evSpan;
            stored.deltaEv = -4.0f + point.y * 8.0f;
            stored.incoming = point.incoming;
            stored.outgoing = point.outgoing;
            localRange.points.push_back(stored);
        }
        localRange.enabled = true;
        localRange = Stack::RawRecipe::SanitizeLocalRangeRecipe(
            std::move(localRange));
    }
    return changed || (exposureEv && previousExposure != *exposureEv);
}


} // namespace Stack::Editor::RawLabInternal
