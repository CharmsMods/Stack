#include "Editor/EditorModule.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Restormer/RestormerClient.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr float kRawLabCommandContentHeight = 38.0f;
constexpr float kRawLabFloatingChromeClearance = 48.0f;
constexpr float kRawLabOuterLeftInset = 18.0f;
constexpr float kRawLabCommandHeight =
    kRawLabCommandContentHeight + kRawLabFloatingChromeClearance;
constexpr float kRawLabSplitterSize = 12.0f;
constexpr float kRawLabToolRailDefaultWidth = 340.0f;
constexpr float kRawLabToolRailPreferredMinimumWidth = 280.0f;
constexpr float kRawLabToolRailMaximumWidth = 420.0f;
constexpr float kRawLabPreviewMinimumWidth = 480.0f;
constexpr float kRawLabPreviewMinimumHeight = 300.0f;
constexpr float kRawLabLowerShelfDefaultHeight = 180.0f;
constexpr float kRawLabLowerShelfMinimumHeight = 120.0f;
constexpr float kRawLabLowerShelfMaximumFraction = 0.35f;
constexpr float kRawLabFilmstripMinimumHeight = 96.0f;
constexpr float kRawLabFilmstripMaximumHeight = 280.0f;
constexpr int kRawLabDetachedOpenGraceFrames = 120;
constexpr int kRawLabCurveMaxPoints = 12;
constexpr std::size_t kRawLabHistogramBinCount = 128;
constexpr float kRawLabCurveFineDragScale = 0.18f;

int NormalizeRawLabRotationDegrees(int rotationDegrees) {
    int normalized = rotationDegrees % 360;
    if (normalized < 0) {
        normalized += 360;
    }
    return normalized;
}

void LabTooltip(const char* text, ImGuiHoveredFlags flags = 0) {
    if (text != nullptr && text[0] != '\0' && ImGui::IsItemHovered(flags)) {
        ImGui::SetTooltip("%s", text);
    }
}

void PushBareButtonColors(bool active = false) {
    const ImVec4 transparent(0.0f, 0.0f, 0.0f, 0.0f);
    const ImVec4 selected = active
        ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
        : transparent;
    ImGui::PushStyleColor(ImGuiCol_Button, selected);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
}

void PopBareButtonColors() {
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

bool BareTextButton(
    const char* label,
    bool active = false,
    bool enabled = true,
    const ImVec2& size = ImVec2(0.0f, 0.0f)) {
    PushBareButtonColors(active);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::Button(label, size);
    ImGui::EndDisabled();
    PopBareButtonColors();
    return pressed;
}

bool BareSliderFloat(
    const char* label,
    const char* id,
    float* value,
    float minimum,
    float maximum,
    const char* format,
    float width = 0.0f) {
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    if (width > 0.0f) {
        ImGui::SetNextItemWidth(width);
    } else {
        ImGui::SetNextItemWidth(-1.0f);
    }
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    const bool changed = ImGui::SliderFloat("##value", value, minimum, maximum, format);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    return changed;
}

bool BareSliderInt(
    const char* label,
    const char* id,
    int* value,
    int minimum,
    int maximum,
    const char* format,
    float width = 0.0f) {
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(width > 0.0f ? width : -1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgHovered,
        ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgActive,
        ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    const bool changed =
        ImGui::SliderInt("##value", value, minimum, maximum, format);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    ImGui::PopID();
    return changed;
}

ImVec2 CompactLabGraphSize(float reservedHeight, float minimumHeight, float maximumHeight) {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float width = std::clamp(std::min(320.0f, available.x), 160.0f, 320.0f);
    const float desiredHeight = std::clamp(width * 0.75f, minimumHeight, maximumHeight);
    const float usableHeight = std::max(minimumHeight, available.y - reservedHeight);
    const float height = std::min(desiredHeight, usableHeight);
    ImGui::SetCursorPosX(
        ImGui::GetCursorPosX() + std::max(0.0f, (available.x - width) * 0.5f));
    return ImVec2(width, height);
}

bool BareToolIslandButton(const char* label, bool active) {
    const ImVec4 transparent(0.0f, 0.0f, 0.0f, 0.0f);
    const ImVec4 defaultText = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImVec4 activeText = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive);
    ImGui::PushStyleColor(ImGuiCol_Button, transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, transparent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, transparent);
    ImGui::PushStyleColor(ImGuiCol_Text, active ? activeText : defaultText);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 5.0f));
    const bool pressed = ImGui::Button(label);
    const bool hovered = ImGui::IsItemHovered();
    const ImRect bounds(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    if (active || hovered) {
        const ImU32 color = ImGui::GetColorU32(
            active ? ImGuiCol_SliderGrabActive : ImGuiCol_TextDisabled);
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(bounds.Min.x + 5.0f, bounds.Max.y - 1.0f),
            ImVec2(bounds.Max.x - 5.0f, bounds.Max.y - 1.0f),
            color,
            active ? 2.0f : 1.0f);
    }
    return pressed;
}

float JsonFloat(const nlohmann::json& object, const char* key, float fallback) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return fallback;
    }
    const float value = it->get<float>();
    return std::isfinite(value) ? value : fallback;
}

int JsonInteger(const nlohmann::json& object, const char* key, int fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

bool JsonBoolean(const nlohmann::json& object, const char* key, bool fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

void EnsureFinishTone(nlohmann::json& finishTone) {
    if (!finishTone.is_object()) {
        finishTone = Stack::RawRecipe::DefaultFinishToneJson();
        return;
    }
    const nlohmann::json defaults = Stack::RawRecipe::DefaultFinishToneJson();
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!finishTone.contains(it.key())) {
            finishTone[it.key()] = it.value();
        }
    }
    finishTone = Stack::RawRecipe::SanitizeFinishTonePointCurveJson(
        std::move(finishTone));
}

void EnsureViewTransform(nlohmann::json& viewTransform) {
    if (!viewTransform.is_object()) {
        viewTransform = Stack::RawRecipe::DefaultViewTransformJson();
        return;
    }
    const nlohmann::json defaults = Stack::RawRecipe::DefaultViewTransformJson();
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        if (!viewTransform.contains(it.key())) {
            viewTransform[it.key()] = it.value();
        }
    }
}

Stack::RawRecipe::RawPointCurveChannel RawLabPointCurveChannel(int index) {
    return static_cast<Stack::RawRecipe::RawPointCurveChannel>(
        std::clamp(index, 0, 3));
}

ImVec2 FitLabImage(float width, float height, const ImVec2& bounds) {
    if (width <= 0.0f || height <= 0.0f || bounds.x <= 0.0f || bounds.y <= 0.0f) {
        return ImVec2(0.0f, 0.0f);
    }
    const float scale = std::min(bounds.x / width, bounds.y / height);
    return ImVec2(width * scale, height * scale);
}

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

void DrawFramelessGrid(ImDrawList* drawList, const ImRect& rect, int columns, int rows) {
    const ImU32 gridColor = ImGui::GetColorU32(ImVec4(
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled).x,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled).y,
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled).z,
        0.16f));
    for (int column = 1; column < columns; ++column) {
        const float x = rect.Min.x + rect.GetWidth() * (static_cast<float>(column) / columns);
        drawList->AddLine(ImVec2(x, rect.Min.y), ImVec2(x, rect.Max.y), gridColor);
    }
    for (int row = 1; row < rows; ++row) {
        const float y = rect.Min.y + rect.GetHeight() * (static_cast<float>(row) / rows);
        drawList->AddLine(ImVec2(rect.Min.x, y), ImVec2(rect.Max.x, y), gridColor);
    }
}

struct RawLabGraphHistogram {
    bool valid = false;
    std::array<float, kRawLabHistogramBinCount> luma {};
    std::array<float, kRawLabHistogramBinCount> red {};
    std::array<float, kRawLabHistogramBinCount> green {};
    std::array<float, kRawLabHistogramBinCount> blue {};
};

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

void DrawRawLabHistogramChannel(
    ImDrawList* drawList,
    const ImRect& rect,
    const std::array<float, kRawLabHistogramBinCount>& bins,
    float normalization,
    const ImVec4& fill,
    const ImVec4& line) {
    if (normalization <= 0.0f) {
        return;
    }
    std::array<ImVec2, kRawLabHistogramBinCount> outline {};
    const float binWidth = rect.GetWidth() / static_cast<float>(kRawLabHistogramBinCount);
    for (std::size_t index = 0; index < kRawLabHistogramBinCount; ++index) {
        const float height =
            std::log1p(std::max(0.0f, bins[index])) / normalization;
        const float left = rect.Min.x + static_cast<float>(index) * binWidth;
        const float right = left + binWidth + 0.5f;
        const float top = rect.Max.y - std::clamp(height, 0.0f, 1.0f) * rect.GetHeight();
        drawList->AddRectFilled(
            ImVec2(left, top),
            ImVec2(right, rect.Max.y),
            ImGui::GetColorU32(fill));
        outline[index] = ImVec2(left + binWidth * 0.5f, top);
    }
    drawList->AddPolyline(
        outline.data(),
        static_cast<int>(outline.size()),
        ImGui::GetColorU32(line),
        ImDrawFlags_None,
        1.0f);
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

    float maximum = 0.0f;
    auto includeMaximum = [&](const auto& bins) {
        for (float value : bins) {
            maximum = std::max(maximum, value);
        }
    };
    if (rgb) {
        includeMaximum(histogram.red);
        includeMaximum(histogram.green);
        includeMaximum(histogram.blue);
    } else {
        includeMaximum(histogram.luma);
    }
    const float normalization = std::log1p(maximum);
    if (rgb) {
        const float redScale = emphasizedChannel < 0 || emphasizedChannel == 0 ? 1.0f : 0.34f;
        const float greenScale = emphasizedChannel < 0 || emphasizedChannel == 1 ? 1.0f : 0.34f;
        const float blueScale = emphasizedChannel < 0 || emphasizedChannel == 2 ? 1.0f : 0.34f;
        DrawRawLabHistogramChannel(
            drawList, rect, histogram.red, normalization,
            ImVec4(0.92f, 0.18f, 0.16f, 0.10f * redScale),
            ImVec4(0.96f, 0.28f, 0.24f, 0.34f * redScale));
        DrawRawLabHistogramChannel(
            drawList, rect, histogram.green, normalization,
            ImVec4(0.18f, 0.82f, 0.36f, 0.09f * greenScale),
            ImVec4(0.26f, 0.90f, 0.44f, 0.32f * greenScale));
        DrawRawLabHistogramChannel(
            drawList, rect, histogram.blue, normalization,
            ImVec4(0.18f, 0.46f, 0.98f, 0.10f * blueScale),
            ImVec4(0.28f, 0.58f, 1.00f, 0.34f * blueScale));
    } else {
        DrawRawLabHistogramChannel(
            drawList, rect, histogram.luma, normalization,
            ImVec4(0.72f, 0.78f, 0.80f, 0.14f),
            ImVec4(0.82f, 0.88f, 0.90f, 0.34f));
    }
    return true;
}

ImRect RawLabLocalRangePlotRect(const ImRect& itemRect) {
    constexpr float kLeftAxisWidth = 24.0f;
    constexpr float kRightInset = 2.0f;
    constexpr float kTopInset = 4.0f;
    constexpr float kBottomAxisHeight = 18.0f;
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

bool DrawFramelessToneCurve(
    const char* id,
    Stack::RawRecipe::RawPointCurveComponent& component,
    int& selectedPoint,
    int activeCurve,
    const RawLabGraphHistogram& histogram,
    bool rgbHistogram,
    const ImVec2& size) {
    ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
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
        const ImVec2 topLeft(rect.Min.x, rect.Min.y);
        const ImVec2 topRight(rect.Max.x, rect.Min.y);
        const ImVec2 bottomLeft(rect.Min.x, rect.Max.y);
        const ImVec2 bottomRight(rect.Max.x, rect.Max.y);
        drawList->AddTriangleFilled(
            topLeft, topRight, bottomLeft,
            ImGui::GetColorU32(channelTints[activeCurve]));
        drawList->AddTriangleFilled(
            topRight, bottomRight, bottomLeft,
            ImGui::GetColorU32(complementTints[activeCurve]));
    }
    const int emphasizedHistogramChannel = activeCurve > 0 ? activeCurve - 1 : -1;
    DrawRawLabHistogramBackdrop(
        drawList,
        rect,
        histogram,
        rgbHistogram || activeCurve > 0,
        emphasizedHistogramChannel);
    DrawFramelessGrid(drawList, rect, 4, 4);
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
    const ImU32 curveColor = ImGui::GetColorU32(curveColors[activeCurve]);
    constexpr int curveSamples = 160;
    std::array<ImVec2, curveSamples> sampled {};
    for (int sample = 0; sample < curveSamples; ++sample) {
        const float x = static_cast<float>(sample) /
            static_cast<float>(curveSamples - 1);
        sampled[static_cast<std::size_t>(sample)] = GraphToScreen(
            rect,
            x,
            Stack::RawRecipe::EvaluateRawPointCurveComponent(component, x));
    }
    drawList->AddPolyline(
        sampled.data(),
        curveSamples,
        curveColor,
        ImDrawFlags_None,
        2.0f);
    for (int index = 0; index < static_cast<int>(component.points.size()); ++index) {
        const auto& point = component.points[static_cast<std::size_t>(index)];
        const ImVec2 screen = GraphToScreen(rect, point.x, point.y);
        if (index == selectedPoint) {
            drawList->AddCircleFilled(
                screen,
                6.5f,
                ImGui::GetColorU32(ImVec4(0.04f, 0.04f, 0.04f, 0.90f)));
        }
        drawList->AddCircleFilled(
            screen,
            index == selectedPoint ? 4.8f : 4.0f,
            index == selectedPoint ? curveColor : ImGui::GetColorU32(ImGuiCol_Text));
    }

    bool changed = false;
    static ImGuiID draggingGraph = 0;
    static int draggingPoint = -1;
    static ImVec2 draggingMouseStart(0.0f, 0.0f);
    static ImVec2 draggingPointStart(0.0f, 0.0f);
    static ImGuiID contextGraph = 0;
    static int contextPoint = -1;
    const ImGuiID graphId = ImGui::GetItemID();
    if (ImGui::IsItemHovered() &&
        rect.Contains(ImGui::GetIO().MousePos) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        float bestDistance = 15.0f * 15.0f;
        int bestPoint = -1;
        for (int index = 0; index < static_cast<int>(component.points.size()); ++index) {
            const auto& control = component.points[static_cast<std::size_t>(index)];
            const ImVec2 screen = GraphToScreen(rect, control.x, control.y);
            const float dx = mouse.x - screen.x;
            const float dy = mouse.y - screen.y;
            const float distance = dx * dx + dy * dy;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestPoint = index;
            }
        }
        if (bestPoint < 0 && component.points.size() < kRawLabCurveMaxPoints) {
            const ImVec2 point = ScreenToGraph(rect, mouse);
            component.points.push_back({ point.x, point.y, 1 });
            std::sort(component.points.begin(), component.points.end(), [](const auto& a, const auto& b) {
                return a.x < b.x;
            });
            bestPoint = 0;
            for (int index = 0; index < static_cast<int>(component.points.size()); ++index) {
                const auto& control = component.points[static_cast<std::size_t>(index)];
                if (std::abs(control.x - point.x) < 0.0001f &&
                    std::abs(control.y - point.y) < 0.0001f) {
                    bestPoint = index;
                    break;
                }
            }
            changed = true;
        }
        selectedPoint = bestPoint;
        draggingGraph = graphId;
        draggingPoint = bestPoint;
        draggingMouseStart = mouse;
        if (bestPoint >= 0) {
            const auto& control = component.points[static_cast<std::size_t>(bestPoint)];
            draggingPointStart = ImVec2(control.x, control.y);
        }
    }
    const bool graphPointerCaptured =
        draggingGraph == graphId &&
        draggingPoint >= 0 &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (graphPointerCaptured) {
        // This one interaction path owns every Curve presentation: Point/R/G/B,
        // Scene/Log, and Luma/RGB histogram modes. Keep the OS pointer hidden
        // for the entire press, including the short pre-drag selection phase.
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }
    if (draggingGraph == graphId &&
        draggingPoint >= 0 &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.5f)) {
        const ImVec2 mouseTravel(
            ImGui::GetIO().MousePos.x - draggingMouseStart.x,
            ImGui::GetIO().MousePos.y - draggingMouseStart.y);
        const ImVec2 point(
            std::clamp(
                draggingPointStart.x +
                    mouseTravel.x * kRawLabCurveFineDragScale /
                        std::max(1.0f, rect.GetWidth()),
                0.0f,
                1.0f),
            std::clamp(
                draggingPointStart.y -
                    mouseTravel.y * kRawLabCurveFineDragScale /
                        std::max(1.0f, rect.GetHeight()),
                0.0f,
                1.0f));
        const int lastIndex = static_cast<int>(component.points.size()) - 1;
        const float minimumX = draggingPoint == 0 ? 0.0f :
            component.points[static_cast<std::size_t>(draggingPoint - 1)].x + 0.002f;
        const float maximumX = draggingPoint == lastIndex ? 1.0f :
            component.points[static_cast<std::size_t>(draggingPoint + 1)].x - 0.002f;
        const float newX = draggingPoint == 0
            ? 0.0f
            : (draggingPoint == lastIndex ? 1.0f : std::clamp(point.x, minimumX, maximumX));
        auto& control = component.points[static_cast<std::size_t>(draggingPoint)];
        if (std::abs(control.x - newX) > 0.0001f ||
            std::abs(control.y - point.y) > 0.0001f) {
            control.x = newX;
            control.y = point.y;
            changed = true;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && draggingGraph == graphId) {
        draggingGraph = 0;
        draggingPoint = -1;
        draggingMouseStart = ImVec2(0.0f, 0.0f);
        draggingPointStart = ImVec2(0.0f, 0.0f);
    }
    if (ImGui::IsItemHovered() &&
        rect.Contains(ImGui::GetIO().MousePos) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        contextGraph = graphId;
        contextPoint = -1;
        for (int index = 1; index + 1 < static_cast<int>(component.points.size()); ++index) {
            const auto& control = component.points[static_cast<std::size_t>(index)];
            const ImVec2 screen = GraphToScreen(rect, control.x, control.y);
            const float dx = mouse.x - screen.x;
            const float dy = mouse.y - screen.y;
            if (dx * dx + dy * dy <= 15.0f * 15.0f) {
                contextPoint = index;
                break;
            }
        }
        ImGui::OpenPopup("##RawLabCurveContext");
    }
    if (ImGui::BeginPopup("##RawLabCurveContext")) {
        if (contextGraph == graphId && contextPoint > 0 &&
            contextPoint + 1 < static_cast<int>(component.points.size()) &&
            ImGui::MenuItem("Delete point")) {
            component.points.erase(component.points.begin() + contextPoint);
            selectedPoint = -1;
            contextPoint = -1;
            changed = true;
        }
        if (ImGui::MenuItem("Reset active curve")) {
            component = {};
            component.points = {
                { 0.0f, 0.0f, 1 },
                { 1.0f, 1.0f, 1 }
            };
            selectedPoint = -1;
            changed = true;
        }
        ImGui::EndPopup();
    }
    return changed;
}

bool DrawFramelessLocalRange(
    const char* id,
    Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    int& selectedPoint,
    std::string& selectedTargetZoneId,
    const RawLabGraphHistogram& histogram,
    bool rgbHistogram,
    const ImVec2& size,
    bool drawTargetZones) {
    localRange = Stack::RawRecipe::SanitizeLocalRangeRecipe(std::move(localRange));
    ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImRect itemRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const ImRect rect = RawLabLocalRangePlotRect(itemRect);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    DrawRawLabHistogramBackdrop(drawList, rect, histogram, rgbHistogram);
    DrawFramelessGrid(drawList, rect, 7, 4);
    DrawRawLabLocalRangeAxes(
        drawList,
        itemRect,
        rect,
        localRange.minEv,
        localRange.maxEv);
    const float evSpan = std::max(0.1f, localRange.maxEv - localRange.minEv);
    auto toNormalized = [&](const Stack::RawRecipe::RawLocalRangePoint& point) {
        return ImVec2(
            (point.ev - localRange.minEv) / evSpan,
            (point.deltaEv + 4.0f) / 8.0f);
    };
    const ImU32 curveColor = ImGui::GetColorU32(ImGuiCol_SliderGrabActive);
    const ImU32 baseColor = ImGui::GetColorU32(ImVec4(0.72f, 0.76f, 0.78f, 0.52f));
    const float zeroY = GraphToScreen(rect, 0.0f, 0.5f).y;
    drawList->AddLine(ImVec2(rect.Min.x, zeroY), ImVec2(rect.Max.x, zeroY), baseColor);
    if (drawTargetZones) {
        for (const Stack::RawRecipe::RawLocalRangeTargetZone& zone : localRange.targetZones) {
            if (!zone.enabled) {
                continue;
            }
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
                const float weight =
                    Stack::RawRecipe::EvaluateLocalRangeTargetZoneTonalWeight(
                        zone,
                        sampleEv);
                const float sampleDelta = zone.deltaEv * weight;
                const ImVec2 point = GraphToScreen(
                    rect,
                    (sampleEv - localRange.minEv) / evSpan,
                    (sampleDelta + 4.0f) / 8.0f);
                if (sampleIndex > 0) {
                    drawList->AddLine(previous, point, lineColor, selected ? 2.4f : 1.3f);
                    drawList->AddQuadFilled(
                        previous,
                        point,
                        ImVec2(point.x, zeroY),
                        ImVec2(previous.x, zeroY),
                        fillColor);
                }
                previous = point;
            }
            const ImVec2 handle = GraphToScreen(
                rect,
                (zone.centerEv - localRange.minEv) / evSpan,
                (zone.deltaEv + 4.0f) / 8.0f);
            drawList->AddCircleFilled(handle, selected ? 6.0f : 4.0f, lineColor);
        }
    }
    for (std::size_t index = 1; index < localRange.points.size(); ++index) {
        drawList->AddLine(
            GraphToScreen(rect, toNormalized(localRange.points[index - 1]).x, toNormalized(localRange.points[index - 1]).y),
            GraphToScreen(rect, toNormalized(localRange.points[index]).x, toNormalized(localRange.points[index]).y),
            curveColor,
            2.0f);
    }
    for (int index = 0; index < static_cast<int>(localRange.points.size()); ++index) {
        drawList->AddCircleFilled(
            GraphToScreen(rect, toNormalized(localRange.points[index]).x, toNormalized(localRange.points[index]).y),
            selectedPoint == index ? 6.0f : 4.0f,
            ImGui::GetColorU32(selectedPoint == index ? ImGuiCol_SliderGrabActive : ImGuiCol_Text));
    }

    bool changed = false;
    static ImGuiID draggingGraph = 0;
    static int draggingPoint = -1;
    static std::string draggingTargetZone;
    const ImGuiID graphId = ImGui::GetItemID();
    if (ImGui::IsItemHovered() &&
        rect.Contains(ImGui::GetIO().MousePos) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        float bestTargetDistance = 15.0f * 15.0f;
        std::string bestTargetZone;
        if (drawTargetZones) {
            for (const Stack::RawRecipe::RawLocalRangeTargetZone& zone : localRange.targetZones) {
                const ImVec2 screen = GraphToScreen(
                    rect,
                    (zone.centerEv - localRange.minEv) / evSpan,
                    (zone.deltaEv + 4.0f) / 8.0f);
                const float dx = mouse.x - screen.x;
                const float dy = mouse.y - screen.y;
                const float distance = dx * dx + dy * dy;
                if (distance < bestTargetDistance) {
                    bestTargetDistance = distance;
                    bestTargetZone = zone.id;
                }
            }
        }
        if (!bestTargetZone.empty()) {
            selectedTargetZoneId = bestTargetZone;
            selectedPoint = -1;
            draggingTargetZone = bestTargetZone;
            draggingGraph = graphId;
            draggingPoint = -1;
        } else {
        float bestDistance = 15.0f * 15.0f;
        int bestPoint = -1;
        for (int index = 0; index < static_cast<int>(localRange.points.size()); ++index) {
            const ImVec2 normalized = toNormalized(localRange.points[index]);
            const ImVec2 screen = GraphToScreen(rect, normalized.x, normalized.y);
            const float dx = mouse.x - screen.x;
            const float dy = mouse.y - screen.y;
            const float distance = dx * dx + dy * dy;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestPoint = index;
            }
        }
        if (bestPoint < 0 && localRange.points.size() < kRawLabCurveMaxPoints) {
            const ImVec2 normalized = ScreenToGraph(rect, mouse);
            Stack::RawRecipe::RawLocalRangePoint point;
            point.ev = localRange.minEv + normalized.x * evSpan;
            point.deltaEv = -4.0f + normalized.y * 8.0f;
            localRange.points.push_back(point);
            std::sort(localRange.points.begin(), localRange.points.end(), [](const auto& a, const auto& b) {
                return a.ev < b.ev;
            });
            for (int index = 0; index < static_cast<int>(localRange.points.size()); ++index) {
                if (std::abs(localRange.points[index].ev - point.ev) < 0.0001f &&
                    std::abs(localRange.points[index].deltaEv - point.deltaEv) < 0.0001f) {
                    bestPoint = index;
                    break;
                }
            }
            changed = true;
        }
        selectedPoint = bestPoint;
        draggingGraph = graphId;
        draggingPoint = bestPoint;
        draggingTargetZone.clear();
        }
    }
    if (draggingGraph == graphId &&
        !draggingTargetZone.empty() &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 normalized = ScreenToGraph(rect, ImGui::GetIO().MousePos);
        const auto zoneIt = std::find_if(
            localRange.targetZones.begin(),
            localRange.targetZones.end(),
            [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                return zone.id == draggingTargetZone;
            });
        if (zoneIt != localRange.targetZones.end()) {
            const float centerEv = std::clamp(
                localRange.minEv + normalized.x * evSpan,
                localRange.minEv,
                localRange.maxEv);
            const float deltaEv = std::clamp(-4.0f + normalized.y * 8.0f, -4.0f, 4.0f);
            if (std::abs(zoneIt->centerEv - centerEv) > 0.0001f ||
                std::abs(zoneIt->deltaEv - deltaEv) > 0.0001f) {
                zoneIt->centerEv = centerEv;
                zoneIt->deltaEv = deltaEv;
                changed = true;
            }
        }
    }
    if (draggingGraph == graphId && draggingPoint >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 normalized = ScreenToGraph(rect, ImGui::GetIO().MousePos);
        const int lastIndex = static_cast<int>(localRange.points.size()) - 1;
        const float minimumEv = draggingPoint == 0
            ? localRange.minEv
            : localRange.points[draggingPoint - 1].ev + 0.01f;
        const float maximumEv = draggingPoint == lastIndex
            ? localRange.maxEv
            : localRange.points[draggingPoint + 1].ev - 0.01f;
        const float requestedEv = localRange.minEv + normalized.x * evSpan;
        const float requestedDelta = -4.0f + normalized.y * 8.0f;
        auto& point = localRange.points[draggingPoint];
        const float newEv = std::clamp(requestedEv, minimumEv, maximumEv);
        if (std::abs(point.ev - newEv) > 0.0001f ||
            std::abs(point.deltaEv - requestedDelta) > 0.0001f) {
            point.ev = newEv;
            point.deltaEv = requestedDelta;
            changed = true;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && draggingGraph == graphId) {
        draggingGraph = 0;
        draggingPoint = -1;
        draggingTargetZone.clear();
    }
    if (ImGui::IsItemHovered() &&
        rect.Contains(ImGui::GetIO().MousePos) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        for (int index = 1; index + 1 < static_cast<int>(localRange.points.size()); ++index) {
            const ImVec2 normalized = toNormalized(localRange.points[index]);
            const ImVec2 screen = GraphToScreen(rect, normalized.x, normalized.y);
            const float dx = mouse.x - screen.x;
            const float dy = mouse.y - screen.y;
            if (dx * dx + dy * dy <= 15.0f * 15.0f) {
                localRange.points.erase(localRange.points.begin() + index);
                selectedPoint = -1;
                changed = true;
                break;
            }
        }
    }
    if (changed) {
        localRange.enabled = true;
        localRange = Stack::RawRecipe::SanitizeLocalRangeRecipe(std::move(localRange));
    }
    return changed;
}

bool DrawViewTransformGraph(nlohmann::json& view, const ImVec2& size) {
    const float exposure = JsonFloat(view, "exposure", 0.0f);
    float blackEv = JsonFloat(view, "blackEv", -8.0f);
    float whiteEv = JsonFloat(view, "whiteEv", 4.0f);
    const float middleGrey = JsonFloat(view, "middleGrey", 0.18f);
    const float shoulder = JsonFloat(view, "shoulder", 0.45f);
    const float toe = JsonFloat(view, "toe", 0.18f);
    const float contrast = JsonFloat(view, "contrast", 1.0f);
    const float graphMinEv = std::min(-12.0f, blackEv - 2.0f);
    const float graphMaxEv = std::max(8.0f, whiteEv + 2.0f);
    const float graphSpan = std::max(1.0f, graphMaxEv - graphMinEv);

    ImGui::InvisibleButton("##RawLabViewGraph", size, ImGuiButtonFlags_MouseButtonLeft);
    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    DrawFramelessGrid(drawList, rect, 6, 4);
    const ImU32 curveColor = ImGui::GetColorU32(ImGuiCol_SliderGrabActive);
    ImVec2 previous;
    constexpr int sampleCount = 192;
    for (int index = 0; index < sampleCount; ++index) {
        const float u = static_cast<float>(index) / static_cast<float>(sampleCount - 1);
        const float sceneEv = graphMinEv + u * graphSpan;
        const float input = middleGrey * std::exp2(sceneEv);
        const float output = Stack::RawRecipe::EvaluateViewTransformDisplayLuma(
            input,
            exposure,
            blackEv,
            whiteEv,
            middleGrey,
            shoulder,
            toe,
            contrast);
        const ImVec2 point = GraphToScreen(rect, u, output);
        if (index > 0) {
            drawList->AddLine(previous, point, curveColor, 2.0f);
        }
        previous = point;
    }

    const float blackU = std::clamp((blackEv - graphMinEv) / graphSpan, 0.0f, 1.0f);
    const float whiteU = std::clamp((whiteEv - graphMinEv) / graphSpan, 0.0f, 1.0f);
    const float blackX = GraphToScreen(rect, blackU, 0.0f).x;
    const float whiteX = GraphToScreen(rect, whiteU, 0.0f).x;
    const ImU32 markerColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    drawList->AddLine(ImVec2(blackX, rect.Min.y), ImVec2(blackX, rect.Max.y), markerColor, 1.5f);
    drawList->AddLine(ImVec2(whiteX, rect.Min.y), ImVec2(whiteX, rect.Max.y), markerColor, 1.5f);
    drawList->AddTriangleFilled(
        ImVec2(blackX, rect.Max.y - 8.0f),
        ImVec2(blackX - 6.0f, rect.Max.y),
        ImVec2(blackX + 6.0f, rect.Max.y),
        markerColor);
    drawList->AddTriangleFilled(
        ImVec2(whiteX, rect.Min.y + 8.0f),
        ImVec2(whiteX - 6.0f, rect.Min.y),
        ImVec2(whiteX + 6.0f, rect.Min.y),
        markerColor);

    bool changed = false;
    static int draggingHandle = 0;
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const float mouseX = ImGui::GetIO().MousePos.x;
        draggingHandle = std::abs(mouseX - blackX) <= std::abs(mouseX - whiteX) ? 1 : 2;
    }
    if (draggingHandle != 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float u = std::clamp(
            (ImGui::GetIO().MousePos.x - rect.Min.x) / std::max(1.0f, rect.GetWidth()),
            0.0f,
            1.0f);
        const float ev = graphMinEv + u * graphSpan;
        if (draggingHandle == 1) {
            const float clamped = std::min(ev, whiteEv - 0.1f);
            if (std::abs(clamped - blackEv) > 0.0001f) {
                blackEv = clamped;
                view["blackEv"] = blackEv;
                changed = true;
            }
        } else {
            const float clamped = std::max(ev, blackEv + 0.1f);
            if (std::abs(clamped - whiteEv) > 0.0001f) {
                whiteEv = clamped;
                view["whiteEv"] = whiteEv;
                changed = true;
            }
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        draggingHandle = 0;
    }
    return changed;
}

const char* LabToolName(EditorModule::RawLabTool tool) {
    switch (tool) {
        case EditorModule::RawLabTool::Denoise: return "CFA Denoise";
        case EditorModule::RawLabTool::RgbDenoise: return "RGB Denoise";
        case EditorModule::RawLabTool::Light: return "Exposure";
        case EditorModule::RawLabTool::Zones: return "Zones";
        case EditorModule::RawLabTool::Tone: return "Curve";
        case EditorModule::RawLabTool::View: return "View";
        case EditorModule::RawLabTool::MultiFrame: return "Multi-Frame";
        case EditorModule::RawLabTool::Inspect: return "Inspect";
        case EditorModule::RawLabTool::Setup: return "Setup";
    }
    return "Exposure";
}

const char* RgbDenoiseMethodLabel(
    Stack::RawRecipe::RawRgbDenoiseMethod method) {
    switch (method) {
        case Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1:
            return "Classical Multiscale";
        case Stack::RawRecipe::RawRgbDenoiseMethod::RestormerRealV1:
            return "Restormer Real Photo";
        case Stack::RawRecipe::RawRgbDenoiseMethod::RestormerGaussianBlindV1:
            return "Restormer Gaussian (Blind)";
    }
    return "Classical Multiscale";
}

const char* RgbDenoiseMappingLabel(
    Stack::RawRecipe::RawRgbDenoiseMapping mapping) {
    switch (mapping) {
        case Stack::RawRecipe::RawRgbDenoiseMapping::SceneLinearSafeV1:
            return "Scene-linear Safe";
        case Stack::RawRecipe::RawRgbDenoiseMapping::ProcessedRgbMatchV1:
            return "Processed RGB Match";
    }
    return "Scene-linear Safe";
}

bool DrawLabTileSet(
    const EditorRenderWorker::SharedTextureTileSet& tiles,
    const ImRect& rect) {
    if (!tiles.tiled || !tiles.complete || tiles.tiles.empty() ||
        tiles.fullWidth <= 0 || tiles.fullHeight <= 0) {
        return false;
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float drawWidth = std::max(1.0f, rect.GetWidth());
    const float drawHeight = std::max(1.0f, rect.GetHeight());
    drawList->PushClipRect(rect.Min, rect.Max, true);
    for (const EditorRenderWorker::SharedTextureTile& tile : tiles.tiles) {
        if (tile.texture == 0 || tile.width <= 0 || tile.height <= 0 ||
            tile.haloWidth <= 0 || tile.haloHeight <= 0) {
            continue;
        }
        const float tileMinX =
            rect.Min.x + (static_cast<float>(tile.x) / tiles.fullWidth) * drawWidth;
        const float tileMaxX =
            rect.Min.x + (static_cast<float>(tile.x + tile.width) / tiles.fullWidth) * drawWidth;
        const float tileMinY =
            rect.Max.y - (static_cast<float>(tile.y + tile.height) / tiles.fullHeight) * drawHeight;
        const float tileMaxY =
            rect.Max.y - (static_cast<float>(tile.y) / tiles.fullHeight) * drawHeight;
        const float localX = static_cast<float>(tile.x - tile.haloX);
        const float localY = static_cast<float>(tile.y - tile.haloY);
        const float u0 = (localX + 0.5f) / tile.haloWidth;
        const float u1 = (localX + tile.width - 0.5f) / tile.haloWidth;
        const float bottomV = (localY + 0.5f) / tile.haloHeight;
        const float topV = (localY + tile.height - 0.5f) / tile.haloHeight;
        drawList->AddImage(
            (ImTextureID)(intptr_t)tile.texture,
            ImVec2(tileMinX, tileMinY),
            ImVec2(tileMaxX, tileMaxY),
            ImVec2(u0, 1.0f - topV),
            ImVec2(u1, 1.0f - bottomV));
    }
    drawList->PopClipRect();
    return true;
}

} // namespace

bool EditorModule::BeginRawWorkspaceEditContext(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    RawWorkspaceEditContext& context) const {
    context = RawWorkspaceEditContext{};
    context.source = selectedSource;
    if (IsMultiFrameRawProjectActive() && m_ActiveRawProjectSnapshot) {
        const Stack::Project::MultiFrameSourceSet* sourceSet =
            Stack::Project::FindSourceSet(
                *m_ActiveRawProjectSnapshot,
                m_ActiveRawProjectSnapshot->activeSourceSetId);
        if (sourceSet == nullptr) {
            context.error = "The active multi-frame source set is unavailable.";
            return false;
        }
        const nlohmann::json storedRecipe = sourceSet->settings.value(
            "sharedPostMfdRecipe",
            nlohmann::json::object());
        context.recipe = storedRecipe.is_object() &&
                storedRecipe.contains("rawRecipeVersion")
            ? Stack::RawRecipe::DeserializeRecipe(storedRecipe)
            : Stack::RawRecipe::MakeDefaultRecipe(
                  "mfd://" + m_ActiveRawProjectSnapshot->projectId + "/" +
                      sourceSet->sourceSetId,
                  sourceSet->name + " developed result");
        context.recipe.technical.processingVersion =
            Raw::RawProcessingVersion::TruthfulV1;
        context.recipe.technical.mosaicDenoise.enabled = false;
        context.resolvedMode =
            Stack::RawWorkspace::RawProjectMode::RecipeBacked;
        context.multiFrameSourceSetId = sourceSet->sourceSetId;
        context.multiFrameResult = true;
        const Stack::Project::ProjectLifecyclePhase phase =
            m_ProjectSessionController.Phase();
        context.canEdit =
            phase != Stack::Project::ProjectLifecyclePhase::Loading &&
            phase != Stack::Project::ProjectLifecyclePhase::Importing &&
            phase != Stack::Project::ProjectLifecyclePhase::Saving &&
            phase != Stack::Project::ProjectLifecyclePhase::Conflict &&
            phase != Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery;
        if (!context.canEdit) {
            context.error =
                phase == Stack::Project::ProjectLifecyclePhase::Conflict
                ? "Resolve the project storage conflict before editing."
                : phase == Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery
                    ? "Save a repaired copy before editing this recovered project."
                    : "The multi-frame project is busy.";
        }
        return true;
    }
    if (selectedSource == nullptr) {
        context.error = "Select a RAW image to begin editing.";
        return false;
    }

    context.panelState = Stack::RawWorkspace::BuildRawPanelState(selectedSource);
    const bool selectedProjectActive =
        IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceSourceKey == selectedSource->relativePathKey;
    const bool selectedPreviewStageQueued =
        m_RawWorkspacePreviewStageQueued &&
        m_RawWorkspacePreviewStageSourceKey == selectedSource->relativePathKey;
    const bool selectedProjectLoading =
        selectedPreviewStageQueued ||
        (Async::IsBusy(m_RawWorkspaceProjectLoadTaskState) &&
         m_RawWorkspaceProjectLoadSourceKey == selectedSource->relativePathKey);
    const bool selectedProjectLoadFailed =
        m_RawWorkspaceProjectLoadTaskState == Async::TaskState::Failed &&
        m_RawWorkspaceProjectLoadSourceKey == selectedSource->relativePathKey;
    const bool selectedStoredProject =
        selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
        selectedSource->project.status == Stack::RawWorkspace::ProjectStatus::Embedded;

    bool resolved = false;
    if (selectedStoredProject && !selectedProjectActive) {
        context.recipe = BuildRawWorkspaceDefaultRecipe(*selectedSource);
        context.error = (selectedProjectLoading || selectedProjectLoadFailed)
            ? m_RawWorkspaceProjectLoadStatusText
            : "Double-click this image in Gallery to open its RAW project.";
    } else {
        resolved = ResolveRawWorkspaceRecipeForSource(
            *selectedSource,
            context.recipe,
            &context.resolvedMode,
            &context.error);
        if (selectedPreviewStageQueued && context.error.empty()) {
            context.error = m_RawWorkspaceProjectLoadStatusText.empty()
                ? "Preparing RAW preview..."
                : m_RawWorkspaceProjectLoadStatusText;
        }
    }
    context.canEdit =
        context.panelState.recipeControlsEditable &&
        resolved &&
        selectedProjectActive &&
        !selectedProjectLoading &&
        !selectedProjectLoadFailed;
    return resolved;
}

void EditorModule::CommitRawWorkspaceEditContext(
    RawWorkspaceEditContext& context,
    bool changed,
    bool interactionActive) {
    if (changed && context.canEdit) {
        if (context.multiFrameResult) {
            ApplyMultiFramePostRecipeEdit(
                context.multiFrameSourceSetId,
                context.recipe,
                interactionActive);
        } else {
            ApplyRawWorkspaceRecipeEditForSelectedSource(
                context.recipe,
                interactionActive);
        }
    }
}

bool EditorModule::ApplyMultiFramePostRecipeEdit(
    const std::string& sourceSetId,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    bool interactionActive) {
    if (!IsMultiFrameRawProjectActive() ||
        !m_ActiveRawProjectSnapshot ||
        sourceSetId.empty()) {
        return false;
    }
    Stack::Project::RawProjectSnapshot snapshot =
        *m_ActiveRawProjectSnapshot;
    Stack::Project::MultiFrameSourceSet* sourceSet =
        Stack::Project::FindSourceSet(snapshot, sourceSetId);
    if (sourceSet == nullptr) {
        return false;
    }

    Stack::RawRecipe::RawDevelopmentRecipe storedRecipe = recipe;
    storedRecipe.technical.processingVersion =
        Raw::RawProcessingVersion::TruthfulV1;
    storedRecipe.technical.mosaicDenoise.enabled = false;
    const nlohmann::json serialized =
        Stack::RawRecipe::SerializeRecipe(storedRecipe);
    if (sourceSet->settings.value(
            "sharedPostMfdRecipe",
            nlohmann::json::object()) == serialized) {
        return true;
    }

    sourceSet->settings["sharedPostMfdRecipe"] = serialized;
    ++snapshot.postRecipeRevision;
    snapshot.dirtyRevision = m_ProjectSessionController.NoteEdit();
    m_ActiveRawProjectSnapshot =
        std::make_shared<Stack::Project::RawProjectSnapshot>(
            std::move(snapshot));
    m_Dirty = true;
    MarkRenderDirty();
    NoteRawWorkspaceRecipePreviewEdit(interactionActive);
    return true;
}

bool EditorModule::RenderRawWorkspaceLabCommandStrip(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    const Stack::RawWorkspace::RawPanelState& panelState,
    RawWorkspaceEditContext& context) {
    const bool multiFrameProject =
        IsMultiFrameRawProjectActive() &&
        m_ActiveRawProjectSnapshot &&
        m_ActiveRawProjectStore;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
    const float rowY = ImGui::GetCursorPosY();
    const bool openFolder = BareTextButton("Open");
    LabTooltip("Open a RAW folder.");
    if (openFolder) {
        ImGui::PopStyleVar();
        OpenRawWorkspaceFolderDialog();
        return true;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton("Rescan", false, !m_RawWorkspace.workspaceRoot.empty())) {
        RescanRawWorkspace();
    }
    LabTooltip("Rescan the current RAW folder.", ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton("Clear", false, !m_RawWorkspace.workspaceRoot.empty())) {
        m_RawWorkspaceLabUi.clearConfirmationRequested = true;
        ImGui::OpenPopup("Clear RAW Workspace?");
    }
    LabTooltip("Clear the current workspace after confirmation.", ImGuiHoveredFlags_AllowWhenDisabled);

    bool orientationChanged = false;
    ImGui::SameLine(0.0f, 10.0f);
    if (BareTextButton("Rotate Left", false, context.canEdit)) {
        context.recipe.cropRotation.rotationDegrees =
            NormalizeRawLabRotationDegrees(
                context.recipe.cropRotation.rotationDegrees - 90);
        orientationChanged = true;
    }
    LabTooltip(
        "Rotate the developed image 90 degrees counter-clockwise.",
        ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton("Rotate Right", false, context.canEdit)) {
        context.recipe.cropRotation.rotationDegrees =
            NormalizeRawLabRotationDegrees(
                context.recipe.cropRotation.rotationDegrees + 90);
        orientationChanged = true;
    }
    LabTooltip(
        "Rotate the developed image 90 degrees clockwise.",
        ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton(
            "Flip H",
            context.recipe.cropRotation.flipHorizontally,
            context.canEdit)) {
        context.recipe.cropRotation.flipHorizontally =
            !context.recipe.cropRotation.flipHorizontally;
        orientationChanged = true;
    }
    LabTooltip(
        "Mirror the developed image across its vertical axis.",
        ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton(
            "Flip V",
            context.recipe.cropRotation.flipVertically,
            context.canEdit)) {
        context.recipe.cropRotation.flipVertically =
            !context.recipe.cropRotation.flipVertically;
        orientationChanged = true;
    }
    LabTooltip(
        "Mirror the developed image across its horizontal axis.",
        ImGuiHoveredFlags_AllowWhenDisabled);
    if (orientationChanged) {
        CommitRawWorkspaceEditContext(context, true, false);
    }

    const float leftEnd =
        ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 12.0f;
    const float contentMaximumX = ImGui::GetWindowContentRegionMax().x;
    const bool selectedProjectActive =
        multiFrameProject ||
        (selectedSource != nullptr &&
         IsRawWorkspaceProjectActive() &&
         m_ActiveRawWorkspaceSourceKey == selectedSource->relativePathKey);
    const float rightWidth = 126.0f;
    const float rightStart = std::max(leftEnd + 80.0f, contentMaximumX - rightWidth);
    std::string identity;
    std::string status;
    if (multiFrameProject) {
        const Stack::Project::MultiFrameSourceSet* activeSet =
            Stack::Project::FindSourceSet(
                *m_ActiveRawProjectSnapshot,
                m_ActiveRawProjectSnapshot->activeSourceSetId);
        identity = m_ActiveRawProjectSnapshot->projectName;
        status = identity;
        if (activeSet != nullptr) {
            status += "  |  " + activeSet->name + "  |  " +
                std::to_string(activeSet->frames.size()) + " frames";
            const bool adoptedCurrent =
                m_MfdAdoptedRawResult &&
                m_MfdAdoptedRawResult->projectId ==
                    m_ActiveRawProjectSnapshot->projectId &&
                m_MfdAdoptedRawResult->sourceSetId == activeSet->sourceSetId &&
                m_MfdAdoptedRawResult->inputRevision ==
                    m_ActiveRawProjectSnapshot->mfdInputRevision;
            status += IsMfdExperimentalProcessingBusy()
                ? "  |  Processing"
                : adoptedCurrent ? "  |  Result ready" : "  |  Not processed";
        }
    } else {
        identity = selectedSource != nullptr
            ? selectedSource->fileName
            : (m_RawWorkspace.workspaceRoot.empty()
                   ? "No RAW folder"
                   : "No image selected");
        status = selectedSource != nullptr && !panelState.statusText.empty()
            ? identity + "  |  " + panelState.statusText
            : identity;
    }
    std::string visibleStatus = status.size() > 96 ? status.substr(0, 93) + "..." : status;
    const float statusWidth = ImGui::CalcTextSize(visibleStatus.c_str()).x;
    const float centerStart = std::clamp(
        (contentMaximumX - statusWidth) * 0.5f,
        leftEnd,
        std::max(leftEnd, rightStart - statusWidth - 12.0f));
    ImGui::SetCursorPos(ImVec2(centerStart, rowY + 5.0f));
    ImGui::TextDisabled(
        "%s",
        visibleStatus.c_str());
    ImGui::SetCursorPos(ImVec2(rightStart, rowY));
    if (BareTextButton(
            "...",
            false,
            multiFrameProject || selectedSource != nullptr)) {
        ImGui::OpenPopup("RawLabProjectMenu");
    }
    LabTooltip("Project actions.", ImGuiHoveredFlags_AllowWhenDisabled);
    if (ImGui::BeginPopup("RawLabProjectMenu")) {
        if (multiFrameProject) {
            const Stack::Project::ProjectLifecyclePhase phase =
                m_ProjectSessionController.Phase();
            const bool inPlaceAllowed =
                phase != Stack::Project::ProjectLifecyclePhase::Conflict &&
                phase != Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery;
            if (phase == Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery &&
                ImGui::MenuItem("Save Repaired Copy as Bundle...")) {
                const std::string folder = FileDialogs::OpenFolderDialog(
                    "Choose Directory for Repaired Project Bundle");
                if (!folder.empty()) {
                    std::string error;
                    const std::filesystem::path destination =
                        std::filesystem::path(folder) /
                        m_ActiveRawProjectSnapshot->projectName;
                    if (!SaveActiveMultiFrameRawProjectAs(
                            destination,
                            Stack::Project::ProjectStorageKind::DirectoryBundle,
                            &error)) {
                        m_RawWorkspaceLabUi.multiFrameStatusText = error;
                    }
                }
            }
            if (ImGui::MenuItem(
                    "Optimize Portable Project",
                    nullptr,
                    false,
                    inPlaceAllowed &&
                        m_ActiveRawProjectStore->StorageKind() ==
                            Stack::Project::ProjectStorageKind::PortableFile)) {
                std::string error;
                if (!OptimizeActiveMultiFrameRawProject(&error)) {
                    m_RawWorkspaceLabUi.multiFrameStatusText = error;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close Project")) {
                m_ShowRawWorkspaceCloseProjectPopup = true;
            }
        } else {
            if (selectedProjectActive) {
                if (ImGui::MenuItem("Close Project")) {
                    m_ShowRawWorkspaceCloseProjectPopup = true;
                }
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine(0.0f, 2.0f);
    const bool galleryOpen = m_RawWorkspaceLabUi.galleryHost != RawGalleryHost::Closed;
    if (BareTextButton("Gallery", galleryOpen, !m_RawWorkspace.workspaceRoot.empty())) {
        if (galleryOpen) {
            if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
                CloseRawWorkspaceLabNativeGallery();
            } else {
                m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Closed;
            }
        } else {
            m_RawWorkspaceLabUi.galleryHost = m_RawWorkspaceLabUi.lastGalleryHost;
            if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
                OpenRawWorkspaceLabNativeGallery();
            }
        }
    }
    LabTooltip("Open or close the remembered Gallery host.", ImGuiHoveredFlags_AllowWhenDisabled);
    ImGui::PopStyleVar();

    static bool suppressConflictPrompt = false;
    const Stack::Project::ProjectLifecyclePhase projectPhase =
        m_ProjectSessionController.Phase();
    if (!multiFrameProject ||
        projectPhase != Stack::Project::ProjectLifecyclePhase::Conflict) {
        suppressConflictPrompt = false;
    } else if (!suppressConflictPrompt) {
        ImGui::OpenPopup("Project Changed Outside Stack");
    }
    if (multiFrameProject && ImGui::BeginPopupModal(
            "Project Changed Outside Stack",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "The project storage revision changed outside this session. "
            "Autosave and in-place save are blocked.");
        if (ImGui::Button("Reload")) {
            LibraryManager::Get().RequestLoadProjectFromPath(
                m_ActiveRawWorkspaceProjectPath,
                this);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Copy")) {
            const std::string destination =
                FileDialogs::SaveProjectFileDialog(
                    "Save Conflicted Project Copy",
                    (m_ActiveRawProjectSnapshot->projectName + "-copy.stack")
                        .c_str());
            if (!destination.empty()) {
                std::string error;
                if (SaveActiveMultiFrameRawProjectAs(
                        destination,
                        Stack::Project::ProjectStorageKind::PortableFile,
                        &error)) {
                    ImGui::CloseCurrentPopup();
                } else {
                    m_RawWorkspaceLabUi.multiFrameStatusText = error;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            suppressConflictPrompt = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (m_RawWorkspaceLabUi.clearConfirmationRequested) {
        ImGui::OpenPopup("Clear RAW Workspace?");
        m_RawWorkspaceLabUi.clearConfirmationRequested = false;
    }
    bool workspaceInvalidated = false;
    if (ImGui::BeginPopupModal(
            "Clear RAW Workspace?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted("Clear the current RAW folder and selection?");
        ImGui::TextDisabled("Saved projects remain on disk.");
        if (ImGui::Button("Clear", ImVec2(96.0f, 0.0f))) {
            ClearRawWorkspaceForUser();
            m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Closed;
            workspaceInvalidated = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return workspaceInvalidated;
}

void EditorModule::RenderRawWorkspacePreviewCanvas(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    bool drawImageFrame) {
    if (IsMultiFrameRawProjectActive() && m_ActiveRawProjectSnapshot) {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 imageBounds(
            std::max(120.0f, available.x),
            std::max(120.0f, available.y));
        const ImVec2 framebufferScale =
            ImGui::GetIO().DisplayFramebufferScale;
        const float previewPixelScale = std::max(
            1.0f,
            std::max(framebufferScale.x, framebufferScale.y));
        const float desiredPreviewPixels =
            std::max(imageBounds.x, imageBounds.y) *
            previewPixelScale * 1.15f;
        m_RawWorkspaceInteractivePreviewMaxDimension = std::clamp(
            static_cast<int>(
                std::ceil(desiredPreviewPixels / 64.0f)) * 64,
            768,
            2048);

        const unsigned int texture = m_Pipeline.GetOutputTexture();
        const int textureWidth = m_Pipeline.GetCanvasWidth();
        const int textureHeight = m_Pipeline.GetCanvasHeight();
        const bool adoptedCurrent =
            m_MfdAdoptedRawResult &&
            m_MfdAdoptedRawResult->projectId ==
                m_ActiveRawProjectSnapshot->projectId &&
            m_MfdAdoptedRawResult->sourceSetId ==
                m_ActiveRawProjectSnapshot->activeSourceSetId &&
            m_MfdAdoptedRawResult->inputRevision ==
                m_ActiveRawProjectSnapshot->mfdInputRevision;
        const bool canDraw =
            adoptedCurrent &&
            texture != 0 &&
            textureWidth > 1 &&
            textureHeight > 1 &&
            IsViewportTextureSafeForDrawing(texture);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (canDraw) {
            const ImVec2 imageSize = m_RawWorkspaceLabUi.previewActualPixels
                ? ImVec2(
                      std::max(
                          1.0f,
                          static_cast<float>(textureWidth) /
                              previewPixelScale),
                      std::max(
                          1.0f,
                          static_cast<float>(textureHeight) /
                              previewPixelScale))
                : FitLabImage(
                      static_cast<float>(textureWidth),
                      static_cast<float>(textureHeight),
                      imageBounds);
            const ImVec2 imageMinimum(
                start.x + (imageBounds.x - imageSize.x) * 0.5f,
                start.y + (imageBounds.y - imageSize.y) * 0.48f);
            const ImRect imageRect(
                imageMinimum,
                ImVec2(
                    imageMinimum.x + imageSize.x,
                    imageMinimum.y + imageSize.y));
            drawList->AddImage(
                (ImTextureID)(intptr_t)texture,
                imageRect.Min,
                imageRect.Max,
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 0.0f));
            if (drawImageFrame) {
                drawList->AddRect(
                    imageRect.Min,
                    imageRect.Max,
                    ImGui::GetColorU32(ImGuiCol_Border));
            }
            drawList->AddText(
                ImVec2(start.x + 8.0f, start.y + 6.0f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                "MFD result");
        } else {
            const ImVec2 placeholderSize = FitLabImage(
                4.0f,
                3.0f,
                imageBounds);
            const ImRect placeholder(
                ImVec2(
                    start.x + (imageBounds.x - placeholderSize.x) * 0.5f,
                    start.y + (imageBounds.y - placeholderSize.y) * 0.48f),
                ImVec2(
                    start.x + (imageBounds.x + placeholderSize.x) * 0.5f,
                    start.y +
                        (imageBounds.y - placeholderSize.y) * 0.48f +
                        placeholderSize.y));
            drawList->AddRectFilled(
                placeholder.Min,
                placeholder.Max,
                ImGui::GetColorU32(
                    ImVec4(0.5f, 0.5f, 0.5f, 0.08f)));
            const char* label =
                "Process the burst to create the MFD result";
            const ImVec2 labelSize = ImGui::CalcTextSize(label);
            drawList->AddText(
                ImVec2(
                    placeholder.GetCenter().x - labelSize.x * 0.5f,
                    placeholder.GetCenter().y - labelSize.y * 0.5f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                label);
        }
        ImGui::Dummy(imageBounds);
        return;
    }
    if (selectedSource == nullptr) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const char* label = "Select an image from Gallery";
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        ImGui::SetCursorPos(ImVec2(
            std::max(0.0f, (available.x - textSize.x) * 0.5f),
            std::max(0.0f, (available.y - textSize.y) * 0.5f)));
        ImGui::TextDisabled("%s", label);
        return;
    }

    const bool selectedProjectActive =
        IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceSourceKey == selectedSource->relativePathKey;
    const bool selectedPreviewStageQueued =
        m_RawWorkspacePreviewStageQueued &&
        m_RawWorkspacePreviewStageSourceKey == selectedSource->relativePathKey;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 imageBounds(
        std::max(120.0f, available.x),
        std::max(120.0f, available.y));
    const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    const float previewPixelScale =
        std::max(1.0f, std::max(framebufferScale.x, framebufferScale.y));
    const float desiredPreviewPixels =
        std::max(imageBounds.x, imageBounds.y) * previewPixelScale * 1.15f;
    m_RawWorkspaceInteractivePreviewMaxDimension = std::clamp(
        static_cast<int>(std::ceil(desiredPreviewPixels / 64.0f)) * 64,
        768,
        2048);
    const ImRect bounds(
        start,
        ImVec2(start.x + imageBounds.x, start.y + imageBounds.y));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool currentRawPreview =
        selectedProjectActive &&
        m_ViewportOutputRawWorkspaceSourceKey == selectedSource->relativePathKey;

    auto imageRectFor = [&](float width, float height) {
        const ImVec2 imageSize = m_RawWorkspaceLabUi.previewActualPixels
            ? ImVec2(
                std::max(1.0f, width / previewPixelScale),
                std::max(1.0f, height / previewPixelScale))
            : FitLabImage(width, height, imageBounds);
        const ImVec2 imageMinimum(
            bounds.Min.x + (imageBounds.x - imageSize.x) * 0.5f,
            bounds.Min.y + (imageBounds.y - imageSize.y) * 0.48f);
        return ImRect(
            imageMinimum,
            ImVec2(imageMinimum.x + imageSize.x, imageMinimum.y + imageSize.y));
    };
    auto drawLocalOverlay = [&](const ImRect& imageRect) {
        if (!HasRawWorkspaceLocalRangeOverlayForSource(selectedSource->relativePathKey)) {
            return;
        }
        const float uInset = m_RawWorkspaceLocalRangeOverlayWidth > 1
            ? 0.5f / m_RawWorkspaceLocalRangeOverlayWidth
            : 0.0f;
        const float vInset = m_RawWorkspaceLocalRangeOverlayHeight > 1
            ? 0.5f / m_RawWorkspaceLocalRangeOverlayHeight
            : 0.0f;
        drawList->AddImage(
            (ImTextureID)(intptr_t)m_RawWorkspaceLocalRangeOverlayTexture,
            imageRect.Min,
            imageRect.Max,
            ImVec2(uInset, 1.0f - vInset),
            ImVec2(1.0f - uInset, vInset));
    };
    auto finishImage = [&](const ImRect& imageRect) {
        drawLocalOverlay(imageRect);
        if (drawImageFrame) {
            drawList->AddRect(imageRect.Min, imageRect.Max, ImGui::GetColorU32(ImGuiCol_Border));
        }
        HandleRawWorkspaceLocalRangeTargetInteraction(
            *selectedSource,
            imageRect.Min,
            imageRect.Max,
            selectedProjectActive,
            currentRawPreview);
    };

    bool drewPreview = false;
    if (currentRawPreview &&
        m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::Tiled &&
        HasViewportOutputTiles()) {
        const auto& tiles = GetViewportOutputTiles();
        const ImRect imageRect = imageRectFor(
            static_cast<float>(tiles.fullWidth),
            static_cast<float>(tiles.fullHeight));
        drewPreview = DrawLabTileSet(tiles, imageRect);
        if (drewPreview) {
            finishImage(imageRect);
        }
    }
    if (!drewPreview &&
        currentRawPreview &&
        m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::SingleTexture &&
        IsViewportTextureSafeForDrawing(m_RawWorkspacePresentationTexture.texture) &&
        m_RawWorkspacePresentationTexture.width > 0 &&
        m_RawWorkspacePresentationTexture.height > 0) {
        const ImRect imageRect = imageRectFor(
            static_cast<float>(m_RawWorkspacePresentationTexture.width),
            static_cast<float>(m_RawWorkspacePresentationTexture.height));
        const float uInset = m_RawWorkspacePresentationTexture.width > 1
            ? 0.5f / m_RawWorkspacePresentationTexture.width
            : 0.0f;
        const float vInset = m_RawWorkspacePresentationTexture.height > 1
            ? 0.5f / m_RawWorkspacePresentationTexture.height
            : 0.0f;
        drawList->AddImage(
            (ImTextureID)(intptr_t)m_RawWorkspacePresentationTexture.texture,
            imageRect.Min,
            imageRect.Max,
            ImVec2(uInset, 1.0f - vInset),
            ImVec2(1.0f - uInset, vInset));
        finishImage(imageRect);
        drewPreview = true;
    }
    if (!drewPreview && !selectedProjectActive && !selectedPreviewStageQueued) {
        int textureWidth = 0;
        int textureHeight = 0;
        const unsigned int texture =
            GetRawWorkspaceThumbnailTexture(*selectedSource, &textureWidth, &textureHeight);
        if (texture != 0 && textureWidth > 0 && textureHeight > 0) {
            const ImRect imageRect = imageRectFor(
                static_cast<float>(textureWidth),
                static_cast<float>(textureHeight));
            drawList->AddImage(
                (ImTextureID)(intptr_t)texture,
                imageRect.Min,
                imageRect.Max,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
            if (drawImageFrame) {
                drawList->AddRect(imageRect.Min, imageRect.Max, ImGui::GetColorU32(ImGuiCol_Border));
            }
            drewPreview = true;
        }
    }
    if (!drewPreview) {
        const ImRect placeholder = imageRectFor(4.0f, 3.0f);
        const ImU32 placeholderColor = ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.12f));
        drawList->AddRectFilled(placeholder.Min, placeholder.Max, placeholderColor);
        const char* label = selectedPreviewStageQueued ? "Preparing RAW preview..." : "RAW";
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        drawList->AddText(
            ImVec2(
                placeholder.GetCenter().x - labelSize.x * 0.5f,
                placeholder.GetCenter().y - labelSize.y * 0.5f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            label);
    }
    ImGui::Dummy(imageBounds);
}

void EditorModule::RenderRawWorkspaceLabPreview(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    RawWorkspaceEditContext& context) {
    const bool selectedProjectActive =
        selectedSource != nullptr &&
        IsRawWorkspaceProjectActive() &&
        m_ActiveRawWorkspaceSourceKey == selectedSource->relativePathKey;
    const bool localRangeActive =
        selectedProjectActive &&
        Stack::RawRecipe::IsLocalRangeEnabled(context.recipe.localRange);
    const bool maskAvailable =
        selectedProjectActive &&
        (context.recipe.localRange.regionMaskEnabled ||
         context.recipe.localRange.colorMaskEnabled ||
         !context.recipe.localRange.targetZones.empty());

    const bool showExposureHud =
        context.canEdit &&
        m_RawWorkspaceLabUi.activeTool != RawLabTool::Light;
    const float hudHeight = showExposureHud ? 30.0f : 0.0f;
    const ImVec2 previewMinimum = ImGui::GetCursorScreenPos();
    const ImVec2 previewAvailable = ImGui::GetContentRegionAvail();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 4.0f));
    ImGui::BeginChild(
        "RawLabPreviewCanvas",
        ImVec2(0.0f, std::max(100.0f, previewAvailable.y - hudHeight)),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    RenderRawWorkspacePreviewCanvas(selectedSource, false);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if (showExposureHud) {
        float exposure = context.recipe.preToneExposureEv;
        const float sliderWidth = std::min(480.0f, std::max(180.0f, ImGui::GetContentRegionAvail().x - 190.0f));
        ImGui::SetCursorPosX(std::max(0.0f, (ImGui::GetContentRegionAvail().x - sliderWidth - 115.0f) * 0.5f));
        if (BareSliderFloat(
                "RAW Exposure",
                "RawLabExposureHud",
                &exposure,
                -8.0f,
                8.0f,
                "%+.2f EV",
                sliderWidth)) {
            context.recipe.preToneExposureEv = exposure;
            CommitRawWorkspaceEditContext(context, true, ImGui::IsAnyItemActive());
        }
    }

    const float previewButtonWidth = ImGui::CalcTextSize("Preview ...").x + 18.0f;
    const float actualPixelsButtonWidth = ImGui::CalcTextSize("100%").x + 18.0f;
    const float lowerButtonWidth = ImGui::CalcTextSize("Lower").x + 18.0f;
    const float overlayWidth =
        previewButtonWidth + actualPixelsButtonWidth + lowerButtonWidth + 4.0f;
    ImGui::SetNextWindowPos(ImVec2(
        previewMinimum.x + std::max(0.0f, previewAvailable.x - overlayWidth - 8.0f),
        previewMinimum.y + 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::Begin(
            "##RawLabPreviewOverlayControls",
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoBackground)) {
        if (BareTextButton("Preview ...")) {
            ImGui::OpenPopup("RawLabPreviewModes");
        }
        LabTooltip("Choose the preview overlay or open Highlight Risk diagnostics.");
        ImGui::SameLine(0.0f, 2.0f);
        if (BareToolIslandButton(
                "100%", m_RawWorkspaceLabUi.previewActualPixels)) {
            m_RawWorkspaceLabUi.previewActualPixels =
                !m_RawWorkspaceLabUi.previewActualPixels;
        }
        LabTooltip(m_RawWorkspaceLabUi.previewActualPixels
            ? "Return to Fit view."
            : "Show one preview texture pixel per display pixel for denoise inspection.");
        ImGui::SameLine(0.0f, 2.0f);
        if (BareToolIslandButton("Lower", m_RawWorkspaceLabUi.lowerShelfOpen)) {
            m_RawWorkspaceLabUi.lowerShelfOpen = !m_RawWorkspaceLabUi.lowerShelfOpen;
            SaveRawWorkspaceAppState();
        }
        LabTooltip(m_RawWorkspaceLabUi.lowerShelfOpen
            ? "Close the lower grading placeholder."
            : "Open the lower grading placeholder.");

        auto setPreviewMode = [&](const char* mode) {
            if (m_RawWorkspaceLocalRangeOverlayMode != mode) {
                m_RawWorkspaceLocalRangeOverlayMode = mode;
                ClearRawWorkspaceLocalRangeOverlayState();
                MarkRenderRefreshDirty();
            }
        };
        if (ImGui::BeginPopup("RawLabPreviewModes")) {
            if (ImGui::MenuItem(
                    "Final",
                    nullptr,
                    m_RawWorkspaceLocalRangeOverlayMode == "none")) {
                setPreviewMode("none");
            }
            ImGui::MenuItem("Compare", nullptr, false, false);
            if (ImGui::MenuItem(
                    "Affected",
                    nullptr,
                    m_RawWorkspaceLocalRangeOverlayMode == "affected-tones",
                    localRangeActive)) {
                setPreviewMode("affected-tones");
            }
            if (ImGui::MenuItem(
                    "Delta",
                    nullptr,
                    m_RawWorkspaceLocalRangeOverlayMode == "delta-map",
                    localRangeActive)) {
                setPreviewMode("delta-map");
            }
            if (ImGui::MenuItem(
                    "Mask",
                    nullptr,
                    m_RawWorkspaceLocalRangeOverlayMode == "region-mask",
                    maskAvailable)) {
                setPreviewMode("region-mask");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Highlight Risk")) {
                m_RawWorkspaceLayoutUi.diagnosticsOpenRequested = true;
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

bool EditorModule::RenderRawWorkspaceLabDenoiseSurface(
    RawWorkspaceEditContext& context) {
    bool changed = false;
    Raw::RawMosaicDenoiseSettings& denoise =
        context.recipe.technical.mosaicDenoise;

    ImGui::TextDisabled("Pre-demosaic sensor cleanup");
    ImGui::Spacing();
    bool enabled = denoise.enabled;
    if (ImGui::Checkbox("Denoise", &enabled)) {
        denoise.enabled = enabled;
        changed = true;
    }
    LabTooltip(
        "Filters same-color sensor samples before Stack reconstructs RGB pixels.");

    ImGui::Spacing();
    ImGui::TextDisabled("Noise model");
    const bool dngMode =
        denoise.mode == Raw::RawMosaicDenoiseMode::DngNoiseProfile;
    if (BareTextButton("DNG Profile", dngMode) && !dngMode) {
        denoise.mode = Raw::RawMosaicDenoiseMode::DngNoiseProfile;
        changed = true;
    }
    LabTooltip(
        "Uses the camera's DNG shot/read NoiseProfile when valid; otherwise "
        "uses the fixed-threshold fallback.");
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton("Fixed", !dngMode) && dngMode) {
        denoise.mode = Raw::RawMosaicDenoiseMode::LegacyFixedThreshold;
        changed = true;
    }
    LabTooltip("Uses Stack's historical fixed-threshold same-CFA filter.");

    if (denoise.mode == Raw::RawMosaicDenoiseMode::DngNoiseProfile) {
        if (context.recipe.technical.processingVersion !=
            Raw::RawProcessingVersion::TruthfulV1) {
            ImGui::TextWrapped(
                "This recipe is not Truthful V1, so the fixed fallback is active.");
        } else {
            ImGui::TextWrapped(
                "DNG-aware filtering is used when the source supplies a valid "
                "NoiseProfile; otherwise Stack preserves the fixed fallback.");
        }
    } else {
        ImGui::TextWrapped(
            "Fixed mode is useful as the compatibility comparison.");
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!denoise.enabled);
    float greenStrength = denoise.lumaStrength;
    if (BareSliderFloat(
            "Green",
            "RawLabDenoiseGreen",
            &greenStrength,
            0.0f,
            1.0f,
            "%.2f",
            std::max(170.0f, ImGui::GetContentRegionAvail().x - 90.0f))) {
        denoise.lumaStrength = greenStrength;
        changed = true;
    }
    LabTooltip("Denoise strength for green CFA samples.");
    float redBlueStrength = denoise.chromaStrength;
    if (BareSliderFloat(
            "Red / Blue",
            "RawLabDenoiseRedBlue",
            &redBlueStrength,
            0.0f,
            1.0f,
            "%.2f",
            std::max(150.0f, ImGui::GetContentRegionAvail().x - 120.0f))) {
        denoise.chromaStrength = redBlueStrength;
        changed = true;
    }
    LabTooltip("Denoise strength for red and blue CFA samples.");
    float edgeProtection = denoise.edgeProtection;
    if (BareSliderFloat(
            "Protect Edges",
            "RawLabDenoiseEdges",
            &edgeProtection,
            0.0f,
            1.0f,
            "%.2f",
            std::max(150.0f, ImGui::GetContentRegionAvail().x - 130.0f))) {
        denoise.edgeProtection = edgeProtection;
        changed = true;
    }
    LabTooltip("Higher values reject neighboring samples more aggressively.");
    bool hotPixels = denoise.hotPixelSuppression;
    if (ImGui::Checkbox("Suppress Hot Pixels", &hotPixels)) {
        denoise.hotPixelSuppression = hotPixels;
        changed = true;
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::TextDisabled(
        "Judge this at 100%% zoom; settled output remains full resolution.");
    return changed;
}

bool EditorModule::RenderRawWorkspaceLabRgbDenoiseSurface(
    RawWorkspaceEditContext& context) {
    bool changed = false;
    Stack::RawRecipe::RawRgbDenoiseRecipe& denoise = context.recipe.rgbDenoise;

    ImGui::TextDisabled("Post-demosaic scene-linear cleanup");
    ImGui::Spacing();
    bool enabled = denoise.enabled;
    if (ImGui::Checkbox("Denoise", &enabled)) {
        denoise.enabled = enabled;
        changed = true;
    }
    LabTooltip(
        "Reduces remaining color speckles and luminance grain after demosaic, "
        "before RAW Exposure and the later tone stages.");

    ImGui::Spacing();
    ImGui::BeginDisabled(!denoise.enabled);
    ImGui::TextDisabled("Method");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    if (ImGui::BeginCombo(
            "##RawLabRgbDenoiseMethod",
            RgbDenoiseMethodLabel(denoise.method))) {
        constexpr Stack::RawRecipe::RawRgbDenoiseMethod methods[] = {
            Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1,
            Stack::RawRecipe::RawRgbDenoiseMethod::RestormerRealV1,
            Stack::RawRecipe::RawRgbDenoiseMethod::RestormerGaussianBlindV1
        };
        for (const auto method : methods) {
            const bool selected = denoise.method == method;
            if (ImGui::Selectable(
                    RgbDenoiseMethodLabel(method), selected) && !selected) {
                denoise.method = method;
                if (method ==
                    Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1) {
                    denoise.packageVersion.clear();
                    denoise.modelSha256.clear();
                    m_RawWorkspaceRestormerPackageStatusText.clear();
                } else {
                    denoise.packageId =
                        Stack::RawRecipe::kRestormerDenoisePackageId;
                    denoise.adapterVersion =
                        Stack::RawRecipe::kRestormerDenoiseAdapterVersion;
                    const Stack::Restormer::ValidationResult package =
                        Stack::Restormer::Client::Instance().Validate(
                            denoise, true);
                    if (package.ok) {
                        denoise.packageVersion =
                            package.manifest.packageVersion;
                        denoise.modelSha256 =
                            package.selectedModel.sha256;
                        m_RawWorkspaceRestormerPackageStatusText.clear();
                    } else {
                        denoise.packageVersion.clear();
                        denoise.modelSha256.clear();
                        m_RawWorkspaceRestormerPackageStatusText =
                            package.error;
                    }
                }
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    LabTooltip(
        "Real Photo is intended for genuine camera noise. Gaussian (Blind) "
        "is the synthetic-noise comparison model.");

    const bool aiMethod =
        denoise.method !=
        Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1;
    if (aiMethod) {
        ImGui::Spacing();
        ImGui::TextDisabled("Model Mapping");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        if (ImGui::BeginCombo(
                "##RawLabRgbDenoiseMapping",
                RgbDenoiseMappingLabel(denoise.mapping))) {
            constexpr Stack::RawRecipe::RawRgbDenoiseMapping mappings[] = {
                Stack::RawRecipe::RawRgbDenoiseMapping::SceneLinearSafeV1,
                Stack::RawRecipe::RawRgbDenoiseMapping::ProcessedRgbMatchV1
            };
            for (const auto mapping : mappings) {
                const bool selected = denoise.mapping == mapping;
                if (ImGui::Selectable(
                        RgbDenoiseMappingLabel(mapping), selected) &&
                    !selected) {
                    denoise.mapping = mapping;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleVar();
        LabTooltip(
            "Scene-linear Safe removes model drift and bounds the residual. "
            "Processed RGB Match keeps more of the model's native response.");
        if (denoise.packageVersion.empty() || denoise.modelSha256.empty()) {
            ImGui::TextWrapped(
                "The optional Restormer package has not been pinned yet. "
                "Rendering and export remain blocked until an approved local "
                "package is installed, or you select Classical Multiscale.");
            if (BareTextButton("Check Package")) {
                const Stack::Restormer::ValidationResult package =
                    Stack::Restormer::Client::Instance().Validate(
                        denoise, true);
                if (package.ok) {
                    denoise.packageVersion =
                        package.manifest.packageVersion;
                    denoise.modelSha256 =
                        package.selectedModel.sha256;
                    denoise.adapterVersion =
                        package.manifest.adapterVersion;
                    m_RawWorkspaceRestormerPackageStatusText.clear();
                    changed = true;
                } else {
                    m_RawWorkspaceRestormerPackageStatusText =
                        package.error;
                }
            }
            LabTooltip(
                "Validates every package artifact and pins this recipe to the "
                "installed package version and selected model hash.");
            if (!m_RawWorkspaceRestormerPackageStatusText.empty()) {
                ImGui::TextWrapped(
                    "%s",
                    m_RawWorkspaceRestormerPackageStatusText.c_str());
            }
        } else {
            ImGui::TextDisabled(
                "Package %s | model %.12s...",
                denoise.packageVersion.c_str(),
                denoise.modelSha256.c_str());
            if (BareTextButton("Recheck Package")) {
                const Stack::Restormer::ValidationResult package =
                    Stack::Restormer::Client::Instance().Validate(
                        denoise, false);
                m_RawWorkspaceRestormerPackageStatusText =
                    package.ok
                        ? "Package check passed."
                        : package.error;
            }
            LabTooltip(
                "Revalidates the pinned package manifest, model hash, helper, "
                "runtime, licenses, and notices.");
            if (!m_RawWorkspaceRestormerPackageStatusText.empty()) {
                ImGui::TextWrapped(
                    "%s",
                    m_RawWorkspaceRestormerPackageStatusText.c_str());
            }
        }
        const std::string& denoiseStatus =
            m_Pipeline.GetRawRgbDenoiseStatus();
        if (!denoiseStatus.empty()) {
            ImGui::TextDisabled("%s", denoiseStatus.c_str());
        }
        if (!m_RawWorkspaceStaleRenderStatusText.empty()) {
            ImGui::TextWrapped(
                "Preview stale: %s",
                m_RawWorkspaceStaleRenderStatusText.c_str());
        }
    }

    ImGui::Spacing();
    float colorNoise = denoise.colorNoise;
    if (BareSliderFloat(
            "Color Noise",
            "RawLabRgbDenoiseColor",
            &colorNoise,
            0.0f,
            1.0f,
            "%.2f",
            std::max(150.0f, ImGui::GetContentRegionAvail().x - 120.0f))) {
        denoise.colorNoise = colorNoise;
        changed = true;
    }
    LabTooltip("Reduces colored speckles and broad chroma blotches.");

    float luminanceNoise = denoise.luminanceNoise;
    if (BareSliderFloat(
            "Luminance Noise",
            "RawLabRgbDenoiseLuminance",
            &luminanceNoise,
            0.0f,
            1.0f,
            "%.2f",
            std::max(140.0f, ImGui::GetContentRegionAvail().x - 145.0f))) {
        denoise.luminanceNoise = luminanceNoise;
        changed = true;
    }
    LabTooltip("Reduces brightness grain while retaining a natural texture.");

    float detailProtection = denoise.detailProtection;
    if (BareSliderFloat(
            "Detail Protection",
            "RawLabRgbDenoiseDetail",
            &detailProtection,
            0.0f,
            1.0f,
            "%.2f",
            std::max(140.0f, ImGui::GetContentRegionAvail().x - 145.0f))) {
        denoise.detailProtection = detailProtection;
        changed = true;
    }
    LabTooltip("Higher values preserve edges and fine structure more strongly.");
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (aiMethod) {
        ImGui::TextDisabled(
            "AI runs outside Stack.exe. Judge the settled result at 100%% zoom.");
    } else {
        ImGui::TextDisabled(
            "Classical Multiscale V1. Judge color and fine detail at 100%% zoom.");
    }
    return changed;
}

bool EditorModule::RenderRawWorkspaceLabLightSurface(RawWorkspaceEditContext& context) {
    bool changed = false;
    ImGui::TextDisabled("Scene-linear capture placement");
    ImGui::Spacing();
    float exposure = context.recipe.preToneExposureEv;
    if (BareSliderFloat(
            "RAW Exposure",
            "RawLabLightExposure",
            &exposure,
            -8.0f,
            8.0f,
            "%+.2f EV",
            std::max(200.0f, ImGui::GetContentRegionAvail().x - 150.0f))) {
        context.recipe.preToneExposureEv = exposure;
        changed = true;
    }
    LabTooltip("Places scene-linear capture exposure before Zones, Finish Tone, and View.");
    const float zeroPosition = std::clamp((-(-8.0f)) / 16.0f, 0.0f, 1.0f);
    const ImVec2 lineMinimum = ImGui::GetCursorScreenPos();
    const float lineWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x - 8.0f);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddLine(
        ImVec2(lineMinimum.x, lineMinimum.y + 5.0f),
        ImVec2(lineMinimum.x + lineWidth, lineMinimum.y + 5.0f),
        ImGui::GetColorU32(ImGuiCol_TextDisabled));
    drawList->AddLine(
        ImVec2(lineMinimum.x + zeroPosition * lineWidth, lineMinimum.y),
        ImVec2(lineMinimum.x + zeroPosition * lineWidth, lineMinimum.y + 10.0f),
        ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::Dummy(ImVec2(lineWidth, 16.0f));
    ImGui::TextDisabled("The scene histogram and sensor headroom display are reserved for a later pass.");
    return changed;
}

bool EditorModule::RenderRawWorkspaceLabZonesSurface(RawWorkspaceEditContext& context) {
    bool changed = false;
    context.recipe.localRange =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(std::move(context.recipe.localRange));
    Stack::RawRecipe::RawLocalRangeRecipe& localRange = context.recipe.localRange;

    auto stopTargeting = [&]() {
        if (!m_RawWorkspaceLocalRangeTargetMode) {
            return;
        }
        const std::string selectedZoneId = m_RawWorkspaceLocalRangeTargetZoneId;
        m_RawWorkspaceLocalRangeOverlayMode =
            m_RawWorkspaceLocalRangeTargetPreviousOverlayMode;
        ClearRawWorkspaceLocalRangeOverlayState();
        ClearRawWorkspaceLocalRangeTargetState(false);
        m_RawWorkspaceLocalRangeTargetZoneId = selectedZoneId;
        MarkRenderRefreshDirty();
    };
    auto startTargeting = [&](bool createNewTarget) {
        if (!m_RawWorkspaceLocalRangeTargetMode) {
            m_RawWorkspaceLocalRangeTargetPreviousOverlayMode =
                m_RawWorkspaceLocalRangeOverlayMode;
        }
        m_RawWorkspaceLocalRangeTargetMode = true;
        m_RawWorkspaceLocalRangeOverlayMode = "target-outline";
        m_RawWorkspaceLocalRangeTargetCreateZone = createNewTarget;
        if (createNewTarget) {
            m_RawWorkspaceLocalRangeTargetZoneId.clear();
        }
        ClearRawWorkspaceLocalRangeOverlayState();
        MarkRenderRefreshDirty();
    };
    auto setPreviewMode = [&](const char* mode) {
        stopTargeting();
        if (m_RawWorkspaceLocalRangeOverlayMode != mode) {
            m_RawWorkspaceLocalRangeOverlayMode = mode;
            ClearRawWorkspaceLocalRangeOverlayState();
            MarkRenderRefreshDirty();
        }
    };

    bool enabled = context.recipe.localRange.enabled;
    if (ImGui::Checkbox("Enable Zones", &enabled)) {
        context.recipe.localRange.enabled = enabled;
        changed = true;
    }
    ImGui::Spacing();

    const bool targetedView = m_RawWorkspaceLabUi.zonesTargetedView;
    if (BareToolIslandButton("Overall Tones", !targetedView) && targetedView) {
        stopTargeting();
        m_RawWorkspaceLabUi.zonesTargetedView = false;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (BareToolIslandButton("Targeted Areas", targetedView) && !targetedView) {
        m_RawWorkspaceLabUi.zonesTargetedView = true;
        m_RawWorkspaceLabUi.selectedZonePoint = -1;
    }
    ImGui::Spacing();

    if (!m_RawWorkspaceLabUi.zonesTargetedView) {
        auto applyPreset = [&](const char* label, Stack::RawRecipe::RawLocalRangePreset preset) {
            if (BareTextButton(label)) {
                context.recipe.localRange =
                    Stack::RawRecipe::ApplyLocalRangePreset(context.recipe.localRange, preset);
                changed = true;
            }
        };
        applyPreset("Open Shadows", Stack::RawRecipe::RawLocalRangePreset::OpenShadows);
        ImGui::SameLine(0.0f, 2.0f);
        applyPreset("Hold Highlights", Stack::RawRecipe::RawLocalRangePreset::HoldHighlights);
        ImGui::SameLine(0.0f, 2.0f);
        applyPreset("Compress", Stack::RawRecipe::RawLocalRangePreset::CompressRange);

        ImGui::Spacing();
        ImGui::TextDisabled("Histogram");
        ImGui::SameLine(0.0f, 6.0f);
        if (BareTextButton("Tones", !m_RawWorkspaceLabUi.zonesRgbScope)) {
            m_RawWorkspaceLabUi.zonesRgbScope = false;
        }
        LabTooltip(
            "The brightness distribution after edge-aware smoothing. This is the "
            "same tonal map used by Overall Tones.");
        ImGui::SameLine(0.0f, 2.0f);
        if (BareTextButton("RGB", m_RawWorkspaceLabUi.zonesRgbScope)) {
            m_RawWorkspaceLabUi.zonesRgbScope = true;
        }
        LabTooltip(
            "Overlay the red, green, and blue tonal distributions. This is an RGB "
            "histogram aligned to the graph, not a spatial video parade.");

        const float minimumEv = context.recipe.localRange.minEv;
        const float maximumEv = context.recipe.localRange.maxEv;
        const float middleGrey = std::max(0.000001f, context.recipe.localRange.middleGrey);
        const RawLabGraphHistogram histogram = BuildRawLabZonesHistogram(
            m_RawWorkspaceGraphScopeReadback,
            minimumEv,
            maximumEv,
            middleGrey);

        const ImVec2 graphSize = CompactLabGraphSize(46.0f, 150.0f, 240.0f);
        if (DrawFramelessLocalRange(
                "##RawLabZonesOverallGraph",
                context.recipe.localRange,
                m_RawWorkspaceLabUi.selectedZonePoint,
                m_RawWorkspaceLocalRangeTargetZoneId,
                histogram,
                m_RawWorkspaceLabUi.zonesRgbScope,
                graphSize,
                false)) {
            changed = true;
        }
        LabTooltip("Left-click to add or drag a point. Right-click an interior point to remove it.");
    } else {
        if (BareTextButton("+ Add Target")) {
            startTargeting(true);
        }
        LabTooltip("Create an independent exposure target with the next image drag.");
        ImGui::SameLine(0.0f, 2.0f);
        const bool hasSelectedTarget = std::any_of(
            localRange.targetZones.begin(),
            localRange.targetZones.end(),
            [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                return zone.id == m_RawWorkspaceLocalRangeTargetZoneId;
            });
        if (BareTextButton(
                m_RawWorkspaceLocalRangeTargetMode ? "Stop Editing" : "Edit on Image",
                m_RawWorkspaceLocalRangeTargetMode,
                m_RawWorkspaceLocalRangeTargetMode || hasSelectedTarget)) {
            if (m_RawWorkspaceLocalRangeTargetMode) {
                stopTargeting();
            } else {
                startTargeting(false);
            }
        }
        LabTooltip(
            "Drag vertically for exposure. Wheel changes tonal reach; Shift-wheel changes "
            "feather; Ctrl-wheel changes color reach. Shift-click adds an area and Alt-click "
            "removes one.",
            ImGuiHoveredFlags_AllowWhenDisabled);

        ImGui::Spacing();
        ImGui::TextDisabled("Preview");
        const bool localRangeActive = Stack::RawRecipe::IsLocalRangeEnabled(localRange);
        const bool maskAvailable =
            localRange.regionMaskEnabled ||
            localRange.colorMaskEnabled ||
            !localRange.targetZones.empty();
        auto previewButton = [&](const char* label, const char* mode, bool available) {
            if (BareTextButton(
                    label,
                    m_RawWorkspaceLocalRangeOverlayMode == mode,
                    available)) {
                setPreviewMode(mode);
            }
        };
        previewButton("Final", "none", true);
        ImGui::SameLine(0.0f, 2.0f);
        previewButton("Affected", "affected-tones", localRangeActive);
        ImGui::SameLine(0.0f, 2.0f);
        previewButton("Delta", "delta-map", localRangeActive);
        ImGui::SameLine(0.0f, 2.0f);
        previewButton("Mask", "region-mask", maskAvailable);

        ImGui::Spacing();
        ImGui::TextDisabled("Targets");
        if (localRange.targetZones.empty()) {
            m_RawWorkspaceLocalRangeTargetZoneId.clear();
            ImGui::TextWrapped(
                "No targets yet. Choose + Add Target, then drag vertically over the image.");
        } else {
            const auto selectedIt = std::find_if(
                localRange.targetZones.begin(),
                localRange.targetZones.end(),
                [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                    return zone.id == m_RawWorkspaceLocalRangeTargetZoneId;
                });
            if (selectedIt == localRange.targetZones.end() &&
                !m_RawWorkspaceLocalRangeTargetCreateZone) {
                m_RawWorkspaceLocalRangeTargetZoneId = localRange.targetZones.front().id;
            }

            for (std::size_t index = 0; index < localRange.targetZones.size(); ++index) {
                const Stack::RawRecipe::RawLocalRangeTargetZone& zone =
                    localRange.targetZones[index];
                char valueText[32];
                std::snprintf(valueText, sizeof(valueText), "%+.2f EV", zone.deltaEv);
                std::string rowLabel = zone.name.empty()
                    ? "Target " + std::to_string(index + 1)
                    : zone.name;
                rowLabel += "    ";
                rowLabel += valueText;
                if (!zone.enabled) {
                    rowLabel += "  (off)";
                }
                ImGui::PushID(zone.id.c_str());
                if (BareTextButton(
                        rowLabel.c_str(),
                        zone.id == m_RawWorkspaceLocalRangeTargetZoneId,
                        true,
                        ImVec2(-1.0f, 0.0f))) {
                    m_RawWorkspaceLocalRangeTargetZoneId = zone.id;
                    m_RawWorkspaceLocalRangeTargetCreateZone = false;
                    m_RawWorkspaceLabUi.selectedZonePoint = -1;
                }
                ImGui::PopID();
            }

            auto zoneIt = std::find_if(
                localRange.targetZones.begin(),
                localRange.targetZones.end(),
                [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                    return zone.id == m_RawWorkspaceLocalRangeTargetZoneId;
                });
            if (zoneIt != localRange.targetZones.end()) {
                ImGui::Separator();
                ImGui::TextDisabled("Selected Target");
                ImGui::PushID(zoneIt->id.c_str());

                char nameBuffer[65] = {};
                std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", zoneIt->name.c_str());
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Name");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::InputText("##name", nameBuffer, sizeof(nameBuffer))) {
                    zoneIt->name = nameBuffer;
                    changed = true;
                }

                bool zoneEnabled = zoneIt->enabled;
                if (ImGui::Checkbox("Enabled", &zoneEnabled)) {
                    zoneIt->enabled = zoneEnabled;
                    changed = true;
                }

                float deltaEv = zoneIt->deltaEv;
                if (BareSliderFloat(
                        "Exposure",
                        "RawLabTargetExposure",
                        &deltaEv,
                        -4.0f,
                        4.0f,
                        "%+.2f EV")) {
                    zoneIt->deltaEv = deltaEv;
                    changed = true;
                }
                float centerEv = zoneIt->centerEv;
                if (BareSliderFloat(
                        "Brightness",
                        "RawLabTargetCenter",
                        &centerEv,
                        localRange.minEv,
                        localRange.maxEv,
                        "%+.2f EV")) {
                    zoneIt->centerEv = centerEv;
                    changed = true;
                }
                float reach = zoneIt->coreHalfWidthEv;
                if (BareSliderFloat(
                        "Reach",
                        "RawLabTargetReach",
                        &reach,
                        0.05f,
                        4.0f,
                        "%.2f EV")) {
                    zoneIt->coreHalfWidthEv = reach;
                    changed = true;
                }
                float feather = zoneIt->featherEv;
                if (BareSliderFloat(
                        "Feather",
                        "RawLabTargetFeather",
                        &feather,
                        0.02f,
                        4.0f,
                        "%.2f EV")) {
                    zoneIt->featherEv = feather;
                    changed = true;
                }

                ImGui::TextDisabled("Affect");
                const bool selectedAreas =
                    zoneIt->scope == Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas;
                if (BareTextButton("Selected Areas", selectedAreas) && !selectedAreas) {
                    zoneIt->scope = Stack::RawRecipe::RawLocalRangeTargetScope::SelectedAreas;
                    changed = true;
                }
                ImGui::SameLine(0.0f, 2.0f);
                if (BareTextButton("All Matches", !selectedAreas) && selectedAreas) {
                    zoneIt->scope = Stack::RawRecipe::RawLocalRangeTargetScope::AllMatches;
                    changed = true;
                }

                bool colorEnabled = zoneIt->colorEnabled;
                if (ImGui::Checkbox("Color Match", &colorEnabled)) {
                    zoneIt->colorEnabled = colorEnabled;
                    changed = true;
                }
                LabTooltip("Also require pixels to resemble the color sampled when the target was created.");
                if (zoneIt->colorEnabled) {
                    float colorReach = zoneIt->colorRadius;
                    if (BareSliderFloat(
                            "Color Reach",
                            "RawLabTargetColorReach",
                            &colorReach,
                            0.002f,
                            0.25f,
                            "%.3f")) {
                        zoneIt->colorRadius = colorReach;
                        changed = true;
                    }
                    float colorFeather = zoneIt->colorFeather;
                    if (BareSliderFloat(
                            "Color Feather",
                            "RawLabTargetColorFeather",
                            &colorFeather,
                            0.002f,
                            0.25f,
                            "%.3f")) {
                        zoneIt->colorFeather = colorFeather;
                        changed = true;
                    }
                }

                ImGui::TextDisabled("Combine Targets");
                auto combineButton = [&](const char* label, Stack::RawRecipe::RawLocalRangeZoneCombineMode mode) {
                    if (BareTextButton(label, localRange.targetZoneCombineMode == mode) &&
                        localRange.targetZoneCombineMode != mode) {
                        localRange.targetZoneCombineMode = mode;
                        changed = true;
                    }
                };
                combineButton("Add", Stack::RawRecipe::RawLocalRangeZoneCombineMode::Add);
                ImGui::SameLine(0.0f, 2.0f);
                combineButton("Strongest", Stack::RawRecipe::RawLocalRangeZoneCombineMode::Strongest);
                ImGui::SameLine(0.0f, 2.0f);
                combineButton("Blend", Stack::RawRecipe::RawLocalRangeZoneCombineMode::Blend);

                ImGui::Spacing();
                if (BareTextButton("Reset Exposure")) {
                    zoneIt->deltaEv = 0.0f;
                    changed = true;
                }
                ImGui::SameLine(0.0f, 2.0f);
                if (BareTextButton("Delete Target")) {
                    const std::size_t erasedIndex = static_cast<std::size_t>(
                        std::distance(localRange.targetZones.begin(), zoneIt));
                    localRange.targetZones.erase(zoneIt);
                    if (localRange.targetZones.empty()) {
                        stopTargeting();
                        m_RawWorkspaceLocalRangeTargetZoneId.clear();
                    } else {
                        const std::size_t nextIndex = std::min(
                            erasedIndex,
                            localRange.targetZones.size() - 1);
                        m_RawWorkspaceLocalRangeTargetZoneId =
                            localRange.targetZones[nextIndex].id;
                    }
                    changed = true;
                }

                ImGui::PopID();
            }
        }
    }

    ImGui::Spacing();
    float strength = localRange.strength;
    if (BareSliderFloat(
            "Strength",
            "RawLabZonesStrength",
            &strength,
            0.0f,
            1.0f,
            "%.2f",
            std::max(180.0f, ImGui::GetContentRegionAvail().x - 110.0f))) {
        localRange.strength = strength;
        changed = true;
    }
    return changed;
}

bool EditorModule::RenderRawWorkspaceLabToneSurface(RawWorkspaceEditContext& context) {
    bool changed = false;
    EnsureFinishTone(context.recipe.finishTone.layerJson);
    nlohmann::json& finishTone = context.recipe.finishTone.layerJson;
    int domain = std::clamp(JsonInteger(finishTone, "domain", 1), 0, 1);
    int activeCurve = std::clamp(m_RawWorkspaceLabUi.activePointCurve, 0, 3);
    const char* curveLabels[] = { "Point", "R", "G", "B" };
    const auto curveSet = Stack::RawRecipe::PointCurveSetFromFinishToneJson(finishTone);
    for (int index = 0; index < 4; ++index) {
        if (BareTextButton(curveLabels[index], activeCurve == index)) {
            activeCurve = index;
            m_RawWorkspaceLabUi.activePointCurve = index;
            m_RawWorkspaceLabUi.selectedTonePoint = -1;
            SaveRawWorkspaceAppState();
        }
        if (!Stack::RawRecipe::IsIdentityRawPointCurveComponent(
                curveSet.curves[static_cast<std::size_t>(index)])) {
            const ImRect selectorRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(selectorRect.GetCenter().x, selectorRect.Max.y - 2.0f),
                1.8f,
                ImGui::GetColorU32(ImGuiCol_SliderGrabActive));
        }
        if (index + 1 < 4) {
            ImGui::SameLine(0.0f, 2.0f);
        }
    }
    ImGui::SameLine(0.0f, 10.0f);
    if (BareTextButton("Scene", domain == 0)) {
        finishTone["domain"] = 0;
        domain = 0;
        changed = true;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton("Log", domain == 1)) {
        finishTone["domain"] = 1;
        domain = 1;
        changed = true;
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Histogram");
    ImGui::SameLine(0.0f, 6.0f);
    if (BareTextButton("Luma", !m_RawWorkspaceLabUi.toneRgbScope)) {
        m_RawWorkspaceLabUi.toneRgbScope = false;
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton("RGB", m_RawWorkspaceLabUi.toneRgbScope)) {
        m_RawWorkspaceLabUi.toneRgbScope = true;
    }
    LabTooltip(
        "Overlay the red, green, and blue tonal distributions. This is an RGB "
        "histogram aligned to the curve, not a spatial video parade.");

    if (finishTone.contains("legacyLuma") &&
        finishTone["legacyLuma"].is_object() &&
        finishTone["legacyLuma"].value("enabled", false)) {
        ImGui::Spacing();
        ImGui::TextDisabled("Legacy Y curve active");
        ImGui::SameLine();
        if (BareTextButton("Reset legacy Y")) {
            finishTone["legacyLuma"]["enabled"] = false;
            changed = true;
        }
        LabTooltip(
            "This compatibility stage preserves the previous Y-mode rendering. "
            "Reset it to remove the legacy luminance operation.");
    }

    const float minimumEv = JsonFloat(finishTone, "logMinEv", -10.0f);
    const float maximumEv = JsonFloat(finishTone, "logMaxEv", 6.0f);
    const float middleGrey = std::max(
        0.000001f,
        JsonFloat(finishTone, "middleGrey", 0.18f));
    const float evSpan = std::max(0.1f, maximumEv - minimumEv);
    const RawLabGraphHistogram histogram = BuildRawLabGraphHistogram(
        m_RawWorkspaceGraphScopeReadback,
        RawDevelopmentGraphScopeStage::FinishToneInput,
        context.recipe.technical.workingSpace,
        [&](float value) {
            if (domain == 0) {
                return value;
            }
            const float ev = std::log2(std::max(0.000001f, value) / middleGrey);
            return (ev - minimumEv) / evSpan;
        });

    Stack::RawRecipe::RawPointCurveComponent component =
        Stack::RawRecipe::PointCurveComponentFromFinishToneJson(
            finishTone,
            RawLabPointCurveChannel(activeCurve));
    const ImVec2 graphSize = CompactLabGraphSize(54.0f, 170.0f, 240.0f);
    bool componentChanged = DrawFramelessToneCurve(
            "##RawLabFinishToneGraph",
            component,
            m_RawWorkspaceLabUi.selectedTonePoint,
            activeCurve,
            histogram,
            m_RawWorkspaceLabUi.toneRgbScope,
            graphSize);
    LabTooltip(
        "Left-click to add or drag a point. Right-click for point removal or "
        "active-curve reset.");

    const int selected = m_RawWorkspaceLabUi.selectedTonePoint;
    if (selected >= 0 && selected < static_cast<int>(component.points.size())) {
        auto& point = component.points[static_cast<std::size_t>(selected)];
        int input = std::clamp(static_cast<int>(std::lround(point.x * 255.0f)), 0, 255);
        int output = std::clamp(static_cast<int>(std::lround(point.y * 255.0f)), 0, 255);
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Input");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(64.0f);
        const bool endpoint = selected == 0 ||
            selected + 1 == static_cast<int>(component.points.size());
        ImGui::BeginDisabled(endpoint);
        if (ImGui::InputInt("##RawLabCurveInput", &input, 0, 0)) {
            const float minimum = component.points[static_cast<std::size_t>(selected - 1)].x + 0.002f;
            const float maximum = component.points[static_cast<std::size_t>(selected + 1)].x - 0.002f;
            point.x = std::clamp(static_cast<float>(std::clamp(input, 0, 255)) / 255.0f, minimum, maximum);
            componentChanged = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::TextUnformatted("Output");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(64.0f);
        if (ImGui::InputInt("##RawLabCurveOutput", &output, 0, 0)) {
            point.y = static_cast<float>(std::clamp(output, 0, 255)) / 255.0f;
            componentChanged = true;
        }
        if (domain == 1) {
            const float inputEv = minimumEv + point.x * evSpan;
            const float outputEv = minimumEv + point.y * evSpan;
            ImGui::TextDisabled("%+.2f EV  ->  %+.2f EV", inputEv, outputEv);
        }
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("Select a point for numeric Input / Output.");
    }

    if (componentChanged) {
        Stack::RawRecipe::StorePointCurveComponentInFinishToneJson(
            finishTone,
            RawLabPointCurveChannel(activeCurve),
            component);
        changed = true;
    }
    return changed;
}

bool EditorModule::RenderRawWorkspaceLabViewSurface(RawWorkspaceEditContext& context) {
    bool changed = false;
    EnsureViewTransform(context.recipe.viewTransform.layerJson);
    nlohmann::json& view = context.recipe.viewTransform.layerJson;
    bool enabled = Stack::RawRecipe::IsViewTransformEnabled(context.recipe);
    bool controlsEnabled = enabled;
    if (context.multiFrameResult) {
        const Stack::Project::MultiFrameSourceSet* sourceSet =
            m_ActiveRawProjectSnapshot
            ? Stack::Project::FindSourceSet(
                  *m_ActiveRawProjectSnapshot,
                  context.multiFrameSourceSetId)
            : nullptr;
        bool internalPlacement = sourceSet == nullptr ||
            sourceSet->settings.value(
                "viewTransformPlacement",
                std::string("internal")) != "graph";
        ImGui::BeginDisabled();
        ImGui::Checkbox(
            "Apply View Transform in MFD RAW Development",
            &internalPlacement);
        ImGui::EndDisabled();
        ImGui::TextDisabled(
            "View placement is managed from Multi-Frame advanced controls so the graph keeps exactly one display transform.");
        controlsEnabled = enabled && internalPlacement;
    } else if (ImGui::Checkbox(
                   "Apply View Transform in RAW Development",
                   &enabled)) {
        view["enabled"] = enabled;
        changed = true;
    }
    if (!controlsEnabled) {
        ImGui::TextWrapped(
            "Scene-linear output is active. Add a View Transform node near the graph Output.");
    }
    ImGui::BeginDisabled(!controlsEnabled);
    const ImVec2 graphSize = CompactLabGraphSize(82.0f, 150.0f, 230.0f);
    if (DrawViewTransformGraph(
            view,
            graphSize)) {
        changed = true;
    }
    LabTooltip("The curve is evaluated with the same CPU formula as the existing View Transform shader. Drag the black or white marker.");
    float contrast = JsonFloat(view, "contrast", 1.0f);
    if (BareSliderFloat(
            "Contrast",
            "RawLabViewContrast",
            &contrast,
            0.05f,
            3.0f,
            "%.2f",
            std::max(180.0f, ImGui::GetContentRegionAvail().x - 110.0f))) {
        view["contrast"] = contrast;
        changed = true;
    }
    float saturation = JsonFloat(view, "saturation", 1.0f);
    if (BareSliderFloat(
            "Saturation",
            "RawLabViewSaturation",
            &saturation,
            0.0f,
            2.0f,
            "%.2f",
            std::max(180.0f, ImGui::GetContentRegionAvail().x - 110.0f))) {
        view["saturation"] = saturation;
        changed = true;
    }
    ImGui::EndDisabled();
    return changed;
}

void EditorModule::RenderRawWorkspaceLabToolIsland() {
    const RawLabTool tools[] = {
        RawLabTool::Denoise,
        RawLabTool::RgbDenoise,
        RawLabTool::Light,
        RawLabTool::Zones,
        RawLabTool::Tone,
        RawLabTool::View,
        RawLabTool::MultiFrame
    };
    constexpr int toolCount =
        static_cast<int>(sizeof(tools) / sizeof(tools[0]));
    const float contentRight = ImGui::GetWindowContentRegionMax().x;
    for (int index = 0; index < toolCount; ++index) {
        const RawLabTool tool = tools[index];
        const float buttonWidth =
            ImGui::CalcTextSize(LabToolName(tool)).x + 14.0f;
        if (index > 0 &&
            ImGui::GetCursorPosX() + buttonWidth > contentRight) {
            ImGui::NewLine();
        }
        const bool active = m_RawWorkspaceLabUi.activeTool == tool;
        const bool toolEnabled =
            !IsMultiFrameRawProjectActive() ||
            tool != RawLabTool::Denoise;
        ImGui::BeginDisabled(!toolEnabled);
        const bool toolPressed =
            BareToolIslandButton(LabToolName(tool), active);
        ImGui::EndDisabled();
        if (toolPressed && toolEnabled && !active) {
            const RawLabTool previousTool = m_RawWorkspaceLabUi.activeTool;
            if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones &&
                m_RawWorkspaceLocalRangeTargetMode) {
                m_RawWorkspaceLocalRangeOverlayMode =
                    m_RawWorkspaceLocalRangeTargetPreviousOverlayMode;
                ClearRawWorkspaceLocalRangeOverlayState();
                ClearRawWorkspaceLocalRangeTargetState(false);
                if (!m_RawWorkspaceLocalRangeOverlayMode.empty() &&
                    m_RawWorkspaceLocalRangeOverlayMode != "none") {
                    // Restoring a non-final overlay still requires graph
                    // execution even when the new tool's scope input is
                    // available from the settled readback cache.
                    MarkRenderRefreshDirty();
                }
            }
            m_RawWorkspaceLabUi.activeTool = tool;
            m_RawWorkspaceLabUi.secondarySheetOpen = false;
            if (tool == RawLabTool::Zones || tool == RawLabTool::Tone) {
                if (!RestoreRawWorkspaceGraphScopeReadbackForActiveLabTool()) {
                    MarkRenderRefreshDirty();
                }
            } else if (previousTool == RawLabTool::Zones ||
                       previousTool == RawLabTool::Tone) {
                m_RawWorkspaceGraphScopeReadback = {};
            }
            SaveRawWorkspaceAppState();
        }
        LabTooltip(
            toolEnabled
                ? LabToolName(tool)
                : "The MFD result has already been denoised in the CFA domain. Use Multi-Frame for burst processing or RGB Denoise after development.",
            toolEnabled ? 0 : ImGuiHoveredFlags_AllowWhenDisabled);
        if (index + 1 < toolCount) {
            ImGui::SameLine(0.0f, 2.0f);
        }
    }
}

bool EditorModule::RenderRawWorkspaceLabLeftRail(RawWorkspaceEditContext& context) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
    ImGui::BeginChild(
        "RawLabToolIsland",
        ImVec2(0.0f, 68.0f),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    RenderRawWorkspaceLabToolIsland();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 8.0f));
    const ImGuiWindowFlags activeToolSurfaceFlags =
        (m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones &&
             m_RawWorkspaceLabUi.zonesTargetedView) ||
                m_RawWorkspaceLabUi.activeTool == RawLabTool::MultiFrame
        ? ImGuiWindowFlags_None
        : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild(
        "RawLabActiveToolSurface",
        ImVec2(0.0f, 0.0f),
        false,
        activeToolSurfaceFlags);
    ImGui::TextUnformatted(LabToolName(m_RawWorkspaceLabUi.activeTool));
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(),
        ImGui::GetWindowContentRegionMax().x - 78.0f));
    const bool multiFrameTool =
        m_RawWorkspaceLabUi.activeTool == RawLabTool::MultiFrame;
    if (!multiFrameTool &&
        BareTextButton("Reset", false, context.canEdit)) {
        switch (m_RawWorkspaceLabUi.activeTool) {
            case RawLabTool::Denoise:
                context.recipe.technical.mosaicDenoise =
                    Raw::RawMosaicDenoiseSettings {};
                break;
            case RawLabTool::RgbDenoise:
                context.recipe.rgbDenoise =
                    Stack::RawRecipe::RawRgbDenoiseRecipe {};
                break;
            case RawLabTool::Light:
                context.recipe.preToneExposureEv = 0.0f;
                break;
            case RawLabTool::Zones:
                if (m_RawWorkspaceLabUi.zonesTargetedView) {
                    const auto zoneIt = std::find_if(
                        context.recipe.localRange.targetZones.begin(),
                        context.recipe.localRange.targetZones.end(),
                        [&](const Stack::RawRecipe::RawLocalRangeTargetZone& zone) {
                            return zone.id == m_RawWorkspaceLocalRangeTargetZoneId;
                        });
                    if (zoneIt != context.recipe.localRange.targetZones.end()) {
                        zoneIt->deltaEv = 0.0f;
                    }
                } else {
                    context.recipe.localRange =
                        Stack::RawRecipe::ApplyLocalRangePreset(
                            context.recipe.localRange,
                            Stack::RawRecipe::RawLocalRangePreset::Reset);
                }
                break;
            case RawLabTool::Tone: {
                EnsureFinishTone(context.recipe.finishTone.layerJson);
                Stack::RawRecipe::RawPointCurveSet resetSet;
                for (auto& component : resetSet.curves) {
                    component.points = {
                        { 0.0f, 0.0f, 1 },
                        { 1.0f, 1.0f, 1 }
                    };
                }
                resetSet.legacyLumaEnabled = false;
                Stack::RawRecipe::StorePointCurveSetInFinishToneJson(
                    context.recipe.finishTone.layerJson,
                    resetSet);
                m_RawWorkspaceLabUi.selectedTonePoint = -1;
                break;
            }
            case RawLabTool::View:
            {
                const bool enabled =
                    Stack::RawRecipe::IsViewTransformEnabled(context.recipe);
                context.recipe.viewTransform.layerJson =
                    Stack::RawRecipe::DefaultViewTransformJson();
                context.recipe.viewTransform.layerJson["enabled"] = enabled;
                break;
            }
            default:
                break;
        }
        CommitRawWorkspaceEditContext(context, true, false);
    }
    if (!multiFrameTool) {
        ImGui::SameLine(0.0f, 2.0f);
    }
    bool secondarySheetOpenedThisFrame = false;
    if (BareTextButton(
            "...",
            m_RawWorkspaceLabUi.secondarySheetOpen,
            context.canEdit || multiFrameTool)) {
        const bool wasOpen = m_RawWorkspaceLabUi.secondarySheetOpen;
        m_RawWorkspaceLabUi.secondarySheetOpen = !m_RawWorkspaceLabUi.secondarySheetOpen;
        secondarySheetOpenedThisFrame = !wasOpen;
    }
    LabTooltip("Open the active tool's secondary controls.");
    ImGui::Spacing();

    bool changed = false;
    if (m_RawWorkspaceLabUi.activeTool == RawLabTool::MultiFrame) {
        RenderMultiFrameRawLabTool();
        if (m_RawLabMultiFrameAdvancedOpenedThisFrame) {
            secondarySheetOpenedThisFrame = true;
            m_RawLabMultiFrameAdvancedOpenedThisFrame = false;
        }
    } else if (context.source == nullptr && !context.multiFrameResult) {
        ImGui::TextDisabled("%s", context.error.empty()
            ? "Choose an image from Gallery to edit."
            : context.error.c_str());
    } else {
        ImGui::BeginDisabled(!context.canEdit);
        switch (m_RawWorkspaceLabUi.activeTool) {
            case RawLabTool::Denoise:
                changed = RenderRawWorkspaceLabDenoiseSurface(context);
                break;
            case RawLabTool::RgbDenoise:
                changed = RenderRawWorkspaceLabRgbDenoiseSurface(context);
                break;
            case RawLabTool::Light:
                changed = RenderRawWorkspaceLabLightSurface(context);
                break;
            case RawLabTool::Zones:
                changed = RenderRawWorkspaceLabZonesSurface(context);
                break;
            case RawLabTool::Tone:
                changed = RenderRawWorkspaceLabToneSurface(context);
                break;
            case RawLabTool::View:
                changed = RenderRawWorkspaceLabViewSurface(context);
                break;
            default:
                ImGui::TextDisabled("This tool is reserved for a later RAW Lab pass.");
                break;
        }
        ImGui::EndDisabled();
        if (!context.error.empty() && !context.canEdit) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", context.error.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();

    CommitRawWorkspaceEditContext(context, changed, ImGui::IsAnyItemActive());
    return secondarySheetOpenedThisFrame;
}

void EditorModule::RenderRawWorkspaceLabSecondarySheet(
    RawWorkspaceEditContext& context,
    const ImVec2& railMinimum,
    const ImVec2& railSize,
    bool openedThisFrame) {
    const bool multiFrameTool =
        m_RawWorkspaceLabUi.activeTool == RawLabTool::MultiFrame;
    if (!m_RawWorkspaceLabUi.secondarySheetOpen ||
        (!context.canEdit && !multiFrameTool)) {
        return;
    }

    const float sheetWidth = multiFrameTool
        ? std::clamp(railSize.x * 1.5f, 460.0f, 620.0f)
        : std::clamp(railSize.x, 300.0f, 380.0f);
    const ImVec2 sheetPosition(
        railMinimum.x + railSize.x + kRawLabSplitterSize,
        railMinimum.y);
    ImGui::SetNextWindowPos(sheetPosition);
    ImGui::SetNextWindowSize(ImVec2(sheetWidth, std::max(240.0f, railSize.y)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_ChildBg));
    bool sheetOpen = true;
    bool sheetHovered = false;
    if (ImGui::Begin(
            "RAW Lab Secondary Controls##RawLabSecondarySheet",
            &sheetOpen,
            ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoSavedSettings)) {
        sheetHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        ImGui::TextUnformatted(LabToolName(m_RawWorkspaceLabUi.activeTool));
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(
            ImGui::GetCursorPosX(),
            ImGui::GetWindowContentRegionMax().x - 48.0f));
        if (BareTextButton("Close")) {
            sheetOpen = false;
        }
        ImGui::Spacing();
        bool advancedChanged = false;
        switch (m_RawWorkspaceLabUi.activeTool) {
            case RawLabTool::MultiFrame:
                RenderMultiFrameRawLabAdvanced();
                break;
            case RawLabTool::Denoise: {
                Raw::RawMosaicDenoiseSettings& denoise =
                    context.recipe.technical.mosaicDenoise;
                ImGui::BeginDisabled(!denoise.enabled);
                int radius = denoise.radius;
                if (BareSliderInt(
                        "Radius",
                        "RawLabDenoiseRadius",
                        &radius,
                        1,
                        4,
                        "%d CFA steps")) {
                    denoise.radius = radius;
                    advancedChanged = true;
                }
                int iterations = denoise.iterations;
                if (BareSliderInt(
                        "Iterations",
                        "RawLabDenoiseIterations",
                        &iterations,
                        1,
                        2,
                        "%d")) {
                    denoise.iterations = iterations;
                    advancedChanged = true;
                }
                float hotPixelThreshold = denoise.hotPixelThreshold;
                if (BareSliderFloat(
                        "Hot Pixel Threshold",
                        "RawLabDenoiseHotPixelThreshold",
                        &hotPixelThreshold,
                        0.005f,
                        0.5f,
                        "%.3f")) {
                    denoise.hotPixelThreshold = hotPixelThreshold;
                    advancedChanged = true;
                }
                ImGui::EndDisabled();
                ImGui::Spacing();
                ImGui::TextWrapped(
                    "Experimental pre-demosaic controls. Two iterations currently repeat "
                    "the center evaluation against the original neighborhood; "
                    "they are not two separately materialized denoise passes.");
                break;
            }
            case RawLabTool::RgbDenoise:
                ImGui::TextWrapped(
                    "Classical Multiscale V1 uses three edge-guided scales in "
                    "scene-linear working RGB. Public AI methods can later share "
                    "this stage without changing the recipe's position in the RAW pipeline.");
                ImGui::Spacing();
                ImGui::TextDisabled(
                    "Pipeline: demosaic and white balance, RGB denoise, RAW Exposure.");
                break;
            case RawLabTool::Light:
                ImGui::TextWrapped(
                    "White balance, demosaic, baseline exposure, processing version, "
                    "and output assumptions remain preserved but intentionally hidden in this prototype.");
                break;
            case RawLabTool::Zones: {
                float smoothness = context.recipe.localRange.smoothness;
                if (BareSliderFloat("Smoothness", "RawLabZoneSmoothness", &smoothness, 0.0f, 1.0f, "%.2f")) {
                    context.recipe.localRange.smoothness = smoothness;
                    advancedChanged = true;
                }
                float edgeProtection = context.recipe.localRange.edgeProtection;
                if (BareSliderFloat("Edge Protection", "RawLabZoneEdge", &edgeProtection, 0.0f, 1.0f, "%.2f")) {
                    context.recipe.localRange.edgeProtection = edgeProtection;
                    advancedChanged = true;
                }
                float detailProtection = context.recipe.localRange.detailProtection;
                if (BareSliderFloat("Detail Protection", "RawLabZoneDetail", &detailProtection, 0.0f, 1.0f, "%.2f")) {
                    context.recipe.localRange.detailProtection = detailProtection;
                    advancedChanged = true;
                }
                bool colorTarget = context.recipe.localRange.colorMaskEnabled;
                if (ImGui::Checkbox("Use Color Target", &colorTarget)) {
                    context.recipe.localRange.colorMaskEnabled = colorTarget;
                    advancedChanged = true;
                }
                bool regionMask = context.recipe.localRange.regionMaskEnabled;
                if (ImGui::Checkbox("Use Region Mask", &regionMask)) {
                    context.recipe.localRange.regionMaskEnabled = regionMask;
                    advancedChanged = true;
                }
                break;
            }
            case RawLabTool::Tone: {
                EnsureFinishTone(context.recipe.finishTone.layerJson);
                float minimumEv = JsonFloat(context.recipe.finishTone.layerJson, "logMinEv", -10.0f);
                float maximumEv = JsonFloat(context.recipe.finishTone.layerJson, "logMaxEv", 6.0f);
                if (BareSliderFloat("Graph Black EV", "RawLabToneMin", &minimumEv, -20.0f, 0.0f, "%.2f")) {
                    context.recipe.finishTone.layerJson["logMinEv"] = minimumEv;
                    advancedChanged = true;
                }
                if (BareSliderFloat("Graph White EV", "RawLabToneMax", &maximumEv, 0.0f, 20.0f, "%.2f")) {
                    context.recipe.finishTone.layerJson["logMaxEv"] = maximumEv;
                    advancedChanged = true;
                }
                if (maximumEv <= minimumEv + 0.1f) {
                    context.recipe.finishTone.layerJson["logMaxEv"] = minimumEv + 0.1f;
                    advancedChanged = true;
                }
                break;
            }
            case RawLabTool::View: {
                EnsureViewTransform(context.recipe.viewTransform.layerJson);
                nlohmann::json& view = context.recipe.viewTransform.layerJson;
                bool enabled =
                    Stack::RawRecipe::IsViewTransformEnabled(context.recipe);
                if (context.multiFrameResult &&
                    m_ActiveRawProjectSnapshot) {
                    const Stack::Project::MultiFrameSourceSet* sourceSet =
                        Stack::Project::FindSourceSet(
                            *m_ActiveRawProjectSnapshot,
                            context.multiFrameSourceSetId);
                    enabled = enabled &&
                        (sourceSet == nullptr ||
                         sourceSet->settings.value(
                             "viewTransformPlacement",
                             std::string("internal")) != "graph");
                }
                if (!enabled) {
                    ImGui::TextWrapped(
                        "Built-in View Transform is off; these settings are preserved but inactive.");
                }
                ImGui::BeginDisabled(!enabled);
                float exposure = JsonFloat(view, "exposure", 0.0f);
                if (BareSliderFloat("Display Exposure", "RawLabViewExposure", &exposure, -8.0f, 8.0f, "%+.2f")) {
                    view["exposure"] = exposure;
                    advancedChanged = true;
                }
                float blackEv = JsonFloat(view, "blackEv", -8.0f);
                if (BareSliderFloat("Black EV", "RawLabViewBlack", &blackEv, -16.0f, 2.0f, "%.2f")) {
                    view["blackEv"] = blackEv;
                    advancedChanged = true;
                }
                float whiteEv = JsonFloat(view, "whiteEv", 4.0f);
                if (BareSliderFloat("White EV", "RawLabViewWhite", &whiteEv, -2.0f, 16.0f, "%.2f")) {
                    view["whiteEv"] = whiteEv;
                    advancedChanged = true;
                }
                float middleGrey = JsonFloat(view, "middleGrey", 0.18f);
                if (BareSliderFloat("Middle Grey", "RawLabViewGrey", &middleGrey, 0.01f, 0.50f, "%.3f")) {
                    view["middleGrey"] = middleGrey;
                    advancedChanged = true;
                }
                float toe = JsonFloat(view, "toe", 0.18f);
                if (BareSliderFloat("Toe", "RawLabViewToe", &toe, 0.0f, 1.0f, "%.2f")) {
                    view["toe"] = toe;
                    advancedChanged = true;
                }
                float shoulder = JsonFloat(view, "shoulder", 0.45f);
                if (BareSliderFloat("Shoulder", "RawLabViewShoulder", &shoulder, 0.001f, 2.0f, "%.2f")) {
                    view["shoulder"] = shoulder;
                    advancedChanged = true;
                }
                bool preserveHue = JsonBoolean(view, "preserveHue", true);
                if (ImGui::Checkbox("Preserve Hue", &preserveHue)) {
                    view["preserveHue"] = preserveHue;
                    advancedChanged = true;
                }
                bool falseColor = JsonBoolean(view, "debugFalseColor", false);
                if (ImGui::Checkbox("False Color", &falseColor)) {
                    view["debugFalseColor"] = falseColor;
                    advancedChanged = true;
                }
                bool encodeOutput = JsonBoolean(view, "encodeSrgbOutput", true);
                if (ImGui::Checkbox("Encode sRGB Output", &encodeOutput)) {
                    view["encodeSrgbOutput"] = encodeOutput;
                    advancedChanged = true;
                }
                ImGui::EndDisabled();
                break;
            }
            default:
                break;
        }
        CommitRawWorkspaceEditContext(context, advancedChanged, ImGui::IsAnyItemActive());
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            sheetOpen = false;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    if (!openedThisFrame &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !sheetHovered) {
        sheetOpen = false;
    }
    if (!sheetOpen) {
        m_RawWorkspaceLabUi.secondarySheetOpen = false;
    }
}

void EditorModule::RenderRawWorkspaceLabGalleryHeader(bool nativeWindow) {
    auto switchHost = [&](RawGalleryHost host) {
        if (host == RawGalleryHost::NativeWindow) {
            OpenRawWorkspaceLabNativeGallery();
            return;
        }
        if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
            CloseRawWorkspaceLabNativeGallery();
        }
        m_RawWorkspaceLabUi.galleryHost = host;
        if (host != RawGalleryHost::Closed) {
            m_RawWorkspaceLabUi.lastGalleryHost = host;
        }
        SaveRawWorkspaceAppState();
    };

    if (BareTextButton(
            "Filmstrip",
            m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Filmstrip)) {
        switchHost(RawGalleryHost::Filmstrip);
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton(
            "Full Workspace",
            m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Workspace)) {
        switchHost(RawGalleryHost::Workspace);
    }
    ImGui::SameLine(0.0f, 2.0f);
    if (BareTextButton("Pop-out", nativeWindow)) {
        switchHost(RawGalleryHost::NativeWindow);
    }
    ImGui::SameLine(0.0f, 8.0f);
    if (nativeWindow || m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Workspace) {
        if (BareTextButton(
                "Grid",
                m_RawWorkspaceGalleryDisplayMode == Stack::RawWorkspace::GalleryDisplayMode::Grid)) {
            m_RawWorkspaceGalleryDisplayMode = Stack::RawWorkspace::GalleryDisplayMode::Grid;
            SaveRawWorkspaceAppState();
        }
        ImGui::SameLine(0.0f, 2.0f);
        if (BareTextButton(
                "List",
                m_RawWorkspaceGalleryDisplayMode == Stack::RawWorkspace::GalleryDisplayMode::List)) {
            m_RawWorkspaceGalleryDisplayMode = Stack::RawWorkspace::GalleryDisplayMode::List;
            SaveRawWorkspaceAppState();
        }
        ImGui::SameLine(0.0f, 8.0f);
    }
    if (BareTextButton("Close")) {
        switchHost(RawGalleryHost::Closed);
    }

    const std::size_t selectedCount =
        m_RawWorkspace.selectedSourceKeys.size();
    if (selectedCount > 0u) {
        ImGui::SameLine(0.0f, 12.0f);
        ImGui::TextDisabled(
            "%llu selected",
            static_cast<unsigned long long>(selectedCount));
    }
    const bool replacementBusy =
        IsDeferredLoadedProjectApplyActive() ||
        IsRawWorkspaceProjectLoadBusy() ||
        IsMfdExperimentalProcessingBusy();
    if (selectedCount >= 2u) {
        ImGui::SameLine(0.0f, 8.0f);
        if (BareTextButton(
                "Create New MFD",
                false,
                !replacementBusy)) {
            RequestCreateMfdProjectFromGallerySelection();
        }
        LabTooltip(
            replacementBusy
                ? "Finish the current load or processing run before creating a new project."
                : "Create a separate MFD project from this selection. The current project is not modified.",
            ImGuiHoveredFlags_AllowWhenDisabled);
    }
    if (selectedCount > 0u &&
        IsMultiFrameRawProjectActive() &&
        m_ActiveRawProjectSnapshot) {
        const Stack::Project::MultiFrameSourceSet* activeSet =
            Stack::Project::FindSourceSet(
                *m_ActiveRawProjectSnapshot,
                m_ActiveRawProjectSnapshot->activeSourceSetId);
        const Stack::Project::ProjectLifecyclePhase phase =
            m_ProjectSessionController.Phase();
        const bool canAdd = activeSet != nullptr &&
            phase != Stack::Project::ProjectLifecyclePhase::Conflict &&
            phase != Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery &&
            !IsMfdExperimentalProcessingBusy();
        ImGui::SameLine(0.0f, 8.0f);
        if (BareTextButton("Add to Current Burst", false, canAdd)) {
            std::vector<std::filesystem::path> paths;
            paths.reserve(m_RawWorkspace.selectedSourceKeys.size());
            for (const std::string& key :
                 m_RawWorkspace.selectedSourceKeys) {
                if (const Stack::RawWorkspace::SourceRecord* source =
                        FindRawWorkspaceSourceByKey(key)) {
                    paths.push_back(source->absolutePath);
                }
            }
            std::string error;
            const bool added = !paths.empty() &&
                AddFramesToMultiFrameSourceSet(
                    activeSet->sourceSetId,
                    paths,
                    &error);
            m_RawWorkspaceLabUi.multiFrameStatusText = added
                ? "Selected Gallery frames embedded and verified."
                : error.empty() ? "No selected RAW frames were available."
                                : error;
        }
        LabTooltip(
            canAdd
                ? "Embed the selected Gallery RAW files into the currently open MFD burst."
                : "Resolve the project state or finish processing before adding frames.",
            ImGuiHoveredFlags_AllowWhenDisabled);
    }
}

void EditorModule::RenderRawWorkspaceLabGalleryContent(bool compactFilmstrip) {
    const Stack::RawWorkspace::GalleryPresentation& presentation =
        GetRawWorkspaceGalleryPresentation();
    if (presentation.totalSources <= 0) {
        ImGui::TextDisabled("No RAW images in this workspace.");
        return;
    }

    auto drawThumbnail = [&](const Stack::RawWorkspace::SourceRecord& source,
                             const Stack::RawWorkspace::GallerySourceView& view,
                             const ImVec2& tileSize,
                             float imageHeight,
                             bool showDetails) {
        ImGui::PushID(source.relativePathKey.c_str());
        const ImVec2 tileMinimum = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##tile", tileSize);
        const ImRect tileRect(
            tileMinimum,
            ImVec2(tileMinimum.x + tileSize.x, tileMinimum.y + tileSize.y));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImRect imageArea(
            tileRect.Min,
            ImVec2(tileRect.Max.x, std::min(tileRect.Max.y, tileRect.Min.y + imageHeight)));
        int textureWidth = 0;
        int textureHeight = 0;
        const unsigned int texture =
            GetRawWorkspaceThumbnailTexture(source, &textureWidth, &textureHeight);
        if (texture != 0 && textureWidth > 0 && textureHeight > 0) {
            const ImVec2 fitted = FitLabImage(
                static_cast<float>(textureWidth),
                static_cast<float>(textureHeight),
                imageArea.GetSize());
            const ImVec2 imageMinimum(
                imageArea.Min.x + (imageArea.GetWidth() - fitted.x) * 0.5f,
                imageArea.Min.y + (imageArea.GetHeight() - fitted.y) * 0.5f);
            drawList->AddImage(
                (ImTextureID)(intptr_t)texture,
                imageMinimum,
                ImVec2(imageMinimum.x + fitted.x, imageMinimum.y + fitted.y),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
        } else {
            drawList->AddRectFilled(
                imageArea.Min,
                imageArea.Max,
                ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, 0.10f)));
            const char* rawLabel = "RAW";
            const ImVec2 rawSize = ImGui::CalcTextSize(rawLabel);
            drawList->AddText(
                ImVec2(
                    imageArea.GetCenter().x - rawSize.x * 0.5f,
                    imageArea.GetCenter().y - rawSize.y * 0.5f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                rawLabel);
        }
        const bool selected = view.multiSelected;
        const bool focused = view.selected ||
            source.relativePathKey == m_RawWorkspace.selectedSourceKey;
        if (selected) {
            drawList->AddLine(
                ImVec2(tileRect.Min.x, tileRect.Max.y - 2.0f),
                ImVec2(tileRect.Max.x, tileRect.Max.y - 2.0f),
                ImGui::GetColorU32(ImGuiCol_SliderGrabActive),
                3.0f);
            drawList->AddCircleFilled(
                ImVec2(tileRect.Min.x + 9.0f, tileRect.Min.y + 9.0f),
                4.0f,
                ImGui::GetColorU32(ImGuiCol_SliderGrabActive));
        } else if (focused) {
            drawList->AddLine(
                ImVec2(tileRect.Min.x, tileRect.Max.y - 1.0f),
                ImVec2(tileRect.Max.x, tileRect.Max.y - 1.0f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                1.0f);
        }
        const float textY = imageArea.Max.y + 5.0f;
        const std::string filename = source.fileName.size() > 24
            ? source.fileName.substr(0, 21) + "..."
            : source.fileName;
        drawList->AddText(
            ImVec2(tileRect.Min.x + 2.0f, textY),
            ImGui::GetColorU32(ImGuiCol_Text),
            filename.c_str());
        if (showDetails) {
            const char* projectStatus =
                Stack::RawWorkspace::ProjectStatusLabel(view.projectStatus);
            drawList->AddText(
                ImVec2(tileRect.Min.x + 2.0f, textY + ImGui::GetTextLineHeight() + 2.0f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled),
                projectStatus);
        }
        if (ImGui::IsItemClicked()) {
            SelectRawWorkspaceSourceForGallery(
                source.relativePathKey,
                ImGui::GetIO().KeyCtrl,
                ImGui::GetIO().KeyShift,
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
        }
        LabTooltip(source.relativePathKey.c_str());
        ImGui::PopID();
    };

    if (!compactFilmstrip &&
        m_RawWorkspaceGalleryDisplayMode == Stack::RawWorkspace::GalleryDisplayMode::List) {
        for (const auto& group : presentation.groups) {
            ImGui::TextDisabled("%s", group.label.c_str());
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(group.sources.size()));
            while (clipper.Step()) {
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                    const auto& view = group.sources[static_cast<std::size_t>(index)];
                    const Stack::RawWorkspace::SourceRecord* source =
                        FindRawWorkspaceSourceByKey(view.relativePathKey);
                    if (source == nullptr) {
                        continue;
                    }
                    ImGui::PushID(source->relativePathKey.c_str());
                    const bool selected = view.multiSelected;
                    std::string row = source->fileName;
                    row += "    Project ";
                    row += Stack::RawWorkspace::ProjectStatusLabel(view.projectStatus);
                    if (ImGui::Selectable(row.c_str(), selected)) {
                        SelectRawWorkspaceSourceForGallery(
                            source->relativePathKey,
                            ImGui::GetIO().KeyCtrl,
                            ImGui::GetIO().KeyShift,
                            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
                    }
                    LabTooltip(source->relativePathKey.c_str());
                    ImGui::PopID();
                }
            }
            ImGui::Spacing();
        }
        return;
    }

    const float compactAvailableHeight = compactFilmstrip
        ? std::max(56.0f, ImGui::GetContentRegionAvail().y - 4.0f)
        : 0.0f;
    const float tileWidth = compactFilmstrip ? 124.0f : 158.0f;
    const float tileHeight = compactFilmstrip
        ? std::clamp(compactAvailableHeight, 56.0f, 104.0f)
        : 148.0f;
    const float imageHeight = compactFilmstrip
        ? std::max(34.0f, tileHeight - 27.0f)
        : 108.0f;
    const float gap = compactFilmstrip ? 10.0f : 16.0f;
    if (compactFilmstrip) {
        bool first = true;
        for (const auto& group : presentation.groups) {
            for (const auto& view : group.sources) {
                const Stack::RawWorkspace::SourceRecord* source =
                    FindRawWorkspaceSourceByKey(view.relativePathKey);
                if (source == nullptr) {
                    continue;
                }
                if (!first) {
                    ImGui::SameLine(0.0f, gap);
                }
                first = false;
                drawThumbnail(*source, view, ImVec2(tileWidth, tileHeight), imageHeight, false);
            }
        }
        return;
    }

    const float availableWidth = std::max(tileWidth, ImGui::GetContentRegionAvail().x);
    const int columns = std::max(1, static_cast<int>((availableWidth + gap) / (tileWidth + gap)));
    for (const auto& group : presentation.groups) {
        ImGui::TextDisabled("%s", group.label.c_str());
        for (int index = 0; index < static_cast<int>(group.sources.size()); ++index) {
            const auto& view = group.sources[static_cast<std::size_t>(index)];
            const Stack::RawWorkspace::SourceRecord* source =
                FindRawWorkspaceSourceByKey(view.relativePathKey);
            if (source == nullptr) {
                continue;
            }
            if (index % columns != 0) {
                ImGui::SameLine(0.0f, gap);
            }
            drawThumbnail(*source, view, ImVec2(tileWidth, tileHeight), imageHeight, true);
        }
        ImGui::Spacing();
    }
}

void EditorModule::OpenRawWorkspaceLabNativeGallery() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr) {
        return;
    }
    m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::NativeWindow;
    m_RawWorkspaceLabUi.lastGalleryHost = RawGalleryHost::NativeWindow;
    m_RawWorkspaceLabNativeGalleryMonitorPos = viewport->WorkPos;
    m_RawWorkspaceLabNativeGalleryMonitorSize = viewport->WorkSize;
    m_RawWorkspaceLabNativeGalleryRequestFocus = true;
    m_RawWorkspaceLabNativeGalleryPlacementInitialized = false;
    m_RawWorkspaceLabNativeGalleryShown = false;
    m_RawWorkspaceLabNativeGalleryFirstPresented = false;
    m_RawWorkspaceLabNativeGalleryLayoutDetached = false;
    m_RawWorkspaceLabNativeGalleryPlatformWaitFrames = 0;
    m_RawWorkspaceLabNativeGalleryFocusAttempts = 0;
    m_RawWorkspaceLabNativeGalleryViewportId = 0;
    m_RawWorkspaceLabNativeGalleryStyledWindow = nullptr;
    m_RawWorkspaceLabNativeGalleryStyledSurfaceColor = 0;
    m_RawWorkspaceLabNativeGalleryStyledTextColor = 0;
    SaveRawWorkspaceAppState();
}

void EditorModule::CloseRawWorkspaceLabNativeGallery() {
    if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
        m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Closed;
    }
    m_RawWorkspaceLabNativeGalleryRequestFocus = false;
    m_RawWorkspaceLabNativeGalleryPlacementInitialized = false;
    m_RawWorkspaceLabNativeGalleryShown = false;
    m_RawWorkspaceLabNativeGalleryFirstPresented = false;
    m_RawWorkspaceLabNativeGalleryLayoutDetached = false;
    m_RawWorkspaceLabNativeGalleryPlatformWaitFrames = 0;
    m_RawWorkspaceLabNativeGalleryFocusAttempts = 0;
    m_RawWorkspaceLabNativeGalleryViewportId = 0;
    m_RawWorkspaceLabNativeGalleryStyledWindow = nullptr;
    m_RawWorkspaceLabNativeGalleryStyledSurfaceColor = 0;
    m_RawWorkspaceLabNativeGalleryStyledTextColor = 0;
}

void EditorModule::RenderRawWorkspaceLabNativeGalleryWindow() {
    if (m_RawWorkspaceLabUi.galleryHost != RawGalleryHost::NativeWindow) {
        return;
    }
    if (m_RawWorkspaceLabNativeGalleryMonitorSize.x <= 1.0f ||
        m_RawWorkspaceLabNativeGalleryMonitorSize.y <= 1.0f) {
        CloseRawWorkspaceLabNativeGallery();
        return;
    }

    const bool initialPlacement = !m_RawWorkspaceLabNativeGalleryPlacementInitialized;
    if (initialPlacement) {
        const float availableWidth =
            std::max(420.0f, m_RawWorkspaceLabNativeGalleryMonitorSize.x - 96.0f);
        const float availableHeight =
            std::max(320.0f, m_RawWorkspaceLabNativeGalleryMonitorSize.y - 96.0f);
        m_RawWorkspaceLabNativeGalleryWindowSize = ImVec2(
            std::clamp(m_RawWorkspaceLabNativeGalleryMonitorSize.x * 0.58f, 640.0f, availableWidth),
            std::clamp(m_RawWorkspaceLabNativeGalleryMonitorSize.y * 0.70f, 480.0f, availableHeight));
        m_RawWorkspaceLabNativeGalleryWindowPos = ImVec2(
            m_RawWorkspaceLabNativeGalleryMonitorPos.x +
                (m_RawWorkspaceLabNativeGalleryMonitorSize.x -
                 m_RawWorkspaceLabNativeGalleryWindowSize.x) *
                    0.5f,
            m_RawWorkspaceLabNativeGalleryMonitorPos.y +
                (m_RawWorkspaceLabNativeGalleryMonitorSize.y -
                 m_RawWorkspaceLabNativeGalleryWindowSize.y) *
                    0.5f);
    }

    ImGuiWindowClass windowClass;
    windowClass.ClassId = ImHashStr("RawLabNativeGalleryWindow");
    windowClass.DockingAllowUnclassed = false;
    windowClass.ParentViewportId = 0;
    windowClass.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
    windowClass.ViewportFlagsOverrideClear =
        ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoTaskBarIcon;
    ImGui::SetNextWindowClass(&windowClass);
    if (initialPlacement) {
        ImGui::SetNextWindowPos(m_RawWorkspaceLabNativeGalleryWindowPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(m_RawWorkspaceLabNativeGalleryWindowSize, ImGuiCond_Always);
    }

    m_RawWorkspaceLabNativeGallerySurfaceColor = GetWorkspaceBaseColor();
    m_RawWorkspaceLabNativeGallerySurfaceColor.w = 1.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    bool keepOpen = true;
    const bool visible = ImGui::Begin(
        "RAW Gallery",
        &keepOpen,
        ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse);
    ImGuiViewport* galleryViewport = ImGui::GetWindowViewport();
    const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const bool ownsDedicatedViewport =
        galleryViewport != nullptr &&
        mainViewport != nullptr &&
        galleryViewport->ID != mainViewport->ID;
    m_RawWorkspaceLabNativeGalleryViewportId =
        ownsDedicatedViewport ? galleryViewport->ID : 0;
    GLFWwindow* platformWindow = ownsDedicatedViewport
        ? static_cast<GLFWwindow*>(galleryViewport->PlatformHandle)
        : nullptr;
    m_RawWorkspaceLabNativeGalleryWindowPos = ImGui::GetWindowPos();
    m_RawWorkspaceLabNativeGalleryWindowSize = ImGui::GetWindowSize();
    const bool escape =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    bool closeAfterEnd = !keepOpen || escape;
    if (!visible || !ownsDedicatedViewport || platformWindow == nullptr) {
        ++m_RawWorkspaceLabNativeGalleryPlatformWaitFrames;
        if (m_RawWorkspaceLabNativeGalleryPlatformWaitFrames > kRawLabDetachedOpenGraceFrames) {
            closeAfterEnd = true;
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
        if (closeAfterEnd) {
            CloseRawWorkspaceLabNativeGallery();
        }
        return;
    }

    m_RawWorkspaceLabNativeGalleryPlatformWaitFrames = 0;
    m_RawWorkspaceLabNativeGalleryPlacementInitialized = true;
    RenderRawWorkspaceLabGalleryHeader(true);
    ImGui::Spacing();
    ImGui::BeginChild("RawLabNativeGalleryContent", ImVec2(0.0f, 0.0f), false);
    RenderRawWorkspaceLabGalleryContent(false);
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar(3);
    if (closeAfterEnd) {
        CloseRawWorkspaceLabNativeGallery();
    }
}

void EditorModule::RenderRawWorkspaceDetachedWindows() {
    if (!m_RawWorkspaceRootTabActive || m_RawWorkspaceLockedByEditorProject) {
        if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::NativeWindow) {
            CloseRawWorkspaceLabNativeGallery();
        } else {
            m_RawWorkspaceLabUi.galleryHost = RawGalleryHost::Closed;
        }
        m_RawWorkspaceGalleryWindowOpen = false;
        return;
    }
    RenderRawWorkspaceLabNativeGalleryWindow();
}

bool EditorModule::QueryRawWorkspaceLabNativeGalleryWindow(
    DetachedNativeWindowRequest& request) const {
    request = DetachedNativeWindowRequest{};
    request.kind = DetachedSurfaceKind::RawGallery;
    if (!m_RawWorkspaceRootTabActive ||
        m_RawWorkspaceLockedByEditorProject ||
        m_RawWorkspaceLabUi.galleryHost != RawGalleryHost::NativeWindow) {
        return false;
    }
    request.viewportId = m_RawWorkspaceLabNativeGalleryViewportId;
    request.surfaceColor = m_RawWorkspaceLabNativeGallerySurfaceColor;
    request.surfaceColor.w = 1.0f;
    request.surfaceColorU32 = ImGui::ColorConvertFloat4ToU32(request.surfaceColor);
    request.textColorU32 =
        ImGui::ColorConvertFloat4ToU32(ImGui::GetStyleColorVec4(ImGuiCol_Text));
    request.nativeShown = m_RawWorkspaceLabNativeGalleryShown;
    request.firstPresented = m_RawWorkspaceLabNativeGalleryFirstPresented;
    request.layoutDetached = m_RawWorkspaceLabNativeGalleryLayoutDetached;
    request.focusAttempt = m_RawWorkspaceLabNativeGalleryFocusAttempts;
    request.waitFrames = m_RawWorkspaceLabNativeGalleryPlatformWaitFrames;
    if (m_RawWorkspaceLabNativeGalleryViewportId == 0) {
        return true;
    }
    const ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    for (int index = 0; index < platformIo.Viewports.Size; ++index) {
        const ImGuiViewport* viewport = platformIo.Viewports[index];
        if (viewport == nullptr || viewport->ID != m_RawWorkspaceLabNativeGalleryViewportId) {
            continue;
        }
        request.window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
        request.hasPlatformWindow = request.window != nullptr;
        if (request.hasPlatformWindow) {
            request.applyTheme =
                request.window != m_RawWorkspaceLabNativeGalleryStyledWindow ||
                request.surfaceColorU32 != m_RawWorkspaceLabNativeGalleryStyledSurfaceColor ||
                request.textColorU32 != m_RawWorkspaceLabNativeGalleryStyledTextColor;
            request.requestFocus =
                m_RawWorkspaceLabNativeGalleryRequestFocus &&
                m_RawWorkspaceLabNativeGalleryFocusAttempts < 4;
        }
        return true;
    }
    return true;
}

void EditorModule::CompleteRawWorkspaceLabNativeGalleryWindowRequest(
    const DetachedNativeWindowRequest& request,
    bool themeApplied,
    bool focused) {
    if (m_RawWorkspaceLabUi.galleryHost != RawGalleryHost::NativeWindow ||
        !request.hasPlatformWindow ||
        request.window == nullptr) {
        return;
    }
    m_RawWorkspaceLabNativeGalleryPlatformWaitFrames = 0;
    m_RawWorkspaceLabNativeGalleryPlacementInitialized = true;
    if (themeApplied) {
        m_RawWorkspaceLabNativeGalleryStyledWindow = request.window;
        m_RawWorkspaceLabNativeGalleryStyledSurfaceColor = request.surfaceColorU32;
        m_RawWorkspaceLabNativeGalleryStyledTextColor = request.textColorU32;
    }
    if (request.requestFocus) {
        ++m_RawWorkspaceLabNativeGalleryFocusAttempts;
        if (focused || m_RawWorkspaceLabNativeGalleryFocusAttempts >= 4) {
            m_RawWorkspaceLabNativeGalleryRequestFocus = false;
        }
    }
}

void EditorModule::MarkRawWorkspaceLabNativeGalleryWindowShown(
    const DetachedNativeWindowRequest& request,
    bool focused) {
    if (m_RawWorkspaceLabUi.galleryHost != RawGalleryHost::NativeWindow ||
        !request.hasPlatformWindow ||
        request.window == nullptr) {
        return;
    }
    m_RawWorkspaceLabNativeGalleryShown = true;
    if (request.requestFocus && focused) {
        m_RawWorkspaceLabNativeGalleryRequestFocus = false;
    }
}

void EditorModule::MarkRawWorkspaceLabNativeGalleryPlatformPresented(GLFWwindow* window) {
    if (m_RawWorkspaceLabUi.galleryHost != RawGalleryHost::NativeWindow || window == nullptr) {
        return;
    }
    if (m_RawWorkspaceLabNativeGalleryStyledWindow != nullptr &&
        window != m_RawWorkspaceLabNativeGalleryStyledWindow) {
        return;
    }
    m_RawWorkspaceLabNativeGalleryFirstPresented = true;
    m_RawWorkspaceLabNativeGalleryLayoutDetached = true;
}

void EditorModule::RenderRawWorkspaceLabUI() {
    LoadResourceTextures();
    EnsureRawWorkspaceLoaded();
    PumpNonRenderingWork(2.5);
    PumpRawWorkspaceThumbnailTextureUploads();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild(
        "RawWorkspaceLabRoot",
        ImVec2(0.0f, 0.0f),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const bool multiFrameProject = IsMultiFrameRawProjectActive() &&
        m_ActiveRawProjectSnapshot;
    if (multiFrameProject) {
        if (m_RawLabPresentedMultiFrameProjectId !=
            m_ActiveRawProjectSnapshot->projectId) {
            m_RawLabPresentedMultiFrameProjectId =
                m_ActiveRawProjectSnapshot->projectId;
            m_RawWorkspaceLabUi.activeTool = RawLabTool::MultiFrame;
            m_RawWorkspaceLabUi.secondarySheetOpen = false;
        }
    } else {
        m_RawLabPresentedMultiFrameProjectId.clear();
    }

    const RawWorkspaceScanSnapshot scanSnapshot = GetRawWorkspaceScanSnapshot();
    const RawWorkspaceThumbnailSnapshot thumbnailSnapshot = GetRawWorkspaceThumbnailSnapshot();
    const Stack::RawWorkspace::SourceRecord* selectedSource =
        FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    const Stack::RawWorkspace::RawPanelState panelState =
        Stack::RawWorkspace::BuildRawPanelState(selectedSource);
    RawWorkspaceEditContext context;
    BeginRawWorkspaceEditContext(selectedSource, context);

    ImGui::SetCursorPosX(kRawLabOuterLeftInset);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 3.0f));
    ImGui::BeginChild(
        "RawLabCommandStrip",
        ImVec2(0.0f, kRawLabCommandHeight),
        false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + kRawLabFloatingChromeClearance);
    const bool workspaceInvalidated =
        RenderRawWorkspaceLabCommandStrip(selectedSource, panelState, context);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    if (workspaceInvalidated) {
        // Open/Clear may replace m_RawWorkspace.sources. The selected source
        // and edit context above are references into that storage and must not
        // be used again during this frame.
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        return;
    }
    ImGui::SetCursorPosX(kRawLabOuterLeftInset);

    if (m_RawWorkspace.workspaceRoot.empty() && !multiFrameProject) {
        const float contentStartX = ImGui::GetCursorPosX();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(
            contentStartX + std::max(0.0f, (available.x - 180.0f) * 0.5f),
            kRawLabCommandHeight + std::max(0.0f, (available.y - 34.0f) * 0.42f)));
        if (BareTextButton("Open RAW Folder", false, true, ImVec2(180.0f, 34.0f))) {
            OpenRawWorkspaceFolderDialog();
        }
        ImGui::SetCursorPosX(
            contentStartX + std::max(0.0f, (available.x - 220.0f) * 0.5f));
        if (BareTextButton(
                "Create New MFD Project",
                false,
                true,
                ImVec2(220.0f, 34.0f))) {
            m_RawWorkspaceLabUi.multiFrameStatusText.clear();
            m_OpenMultiFrameCreationPopup = true;
        }
        RenderMultiFrameRawLabCreationPopup();
    } else if (m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Workspace &&
               !m_RawWorkspace.workspaceRoot.empty()) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
        ImGui::BeginChild(
            "RawLabWorkspaceGallery",
            ImVec2(0.0f, 0.0f),
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        RenderRawWorkspaceLabGalleryHeader(false);
        ImGui::Spacing();
        ImGui::BeginChild("RawLabWorkspaceGalleryScroll", ImVec2(0.0f, 0.0f), false);
        RenderRawWorkspaceLabGalleryContent(false);
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::PopStyleVar();
    } else {
        const float totalHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y);
        const bool showFilmstrip =
            m_RawWorkspaceLabUi.galleryHost == RawGalleryHost::Filmstrip;
        const float filmstripHeight = showFilmstrip
            ? std::clamp(
                  m_RawWorkspaceLabUi.filmstripHeight,
                  kRawLabFilmstripMinimumHeight,
                  std::min(kRawLabFilmstripMaximumHeight, std::max(kRawLabFilmstripMinimumHeight, totalHeight * 0.30f)))
            : 0.0f;
        const float editingHeight = std::max(1.0f, totalHeight - filmstripHeight);
        const float editingWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float maximumRailWidth = std::max(
            240.0f,
            std::min(
                kRawLabToolRailMaximumWidth,
                editingWidth - kRawLabPreviewMinimumWidth - kRawLabSplitterSize));
        const float minimumRailWidth = std::min(
            kRawLabToolRailPreferredMinimumWidth,
            maximumRailWidth);
        m_RawWorkspaceLabUi.toolRailWidth = std::clamp(
            std::isfinite(m_RawWorkspaceLabUi.toolRailWidth)
                ? m_RawWorkspaceLabUi.toolRailWidth
                : kRawLabToolRailDefaultWidth,
            minimumRailWidth,
            maximumRailWidth);
        const float railWidth = m_RawWorkspaceLabUi.toolRailWidth;
        const ImVec2 railMinimum = ImGui::GetCursorScreenPos();
        const ImVec2 railSize(railWidth, editingHeight);

        ImGui::BeginChild(
            "RawLabLeftRail",
            railSize,
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const bool secondarySheetOpenedThisFrame =
            RenderRawWorkspaceLabLeftRail(context);
        ImGui::EndChild();

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::InvisibleButton(
            "##RawLabToolRailWidthSplitter",
            ImVec2(kRawLabSplitterSize, editingHeight));
        const bool railSplitterHovered = ImGui::IsItemHovered();
        const bool railSplitterActive = ImGui::IsItemActive();
        if (railSplitterHovered || railSplitterActive) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            const float x = (minimum.x + maximum.x) * 0.5f;
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(x, minimum.y),
                ImVec2(x, maximum.y),
                ImGui::GetColorU32(
                    railSplitterActive ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab),
                2.0f);
        }
        if (railSplitterActive) {
            const float previous = m_RawWorkspaceLabUi.toolRailWidth;
            m_RawWorkspaceLabUi.toolRailWidth = std::clamp(
                previous + ImGui::GetIO().MouseDelta.x,
                minimumRailWidth,
                maximumRailWidth);
            if (std::abs(previous - m_RawWorkspaceLabUi.toolRailWidth) > 0.01f) {
                SaveRawWorkspaceAppState();
            }
        }

        ImGui::SameLine(0.0f, 0.0f);
        const float rightColumnWidth = std::max(
            1.0f,
            editingWidth - railWidth - kRawLabSplitterSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild(
            "RawLabRightColumn",
            ImVec2(rightColumnWidth, editingHeight),
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();

        const float maximumLowerShelf = std::max(
            80.0f,
            std::min(
                editingHeight * kRawLabLowerShelfMaximumFraction,
                editingHeight - kRawLabPreviewMinimumHeight - kRawLabSplitterSize));
        const float minimumLowerShelf = std::min(
            kRawLabLowerShelfMinimumHeight,
            maximumLowerShelf);
        m_RawWorkspaceLabUi.lowerShelfHeight = std::clamp(
            std::isfinite(m_RawWorkspaceLabUi.lowerShelfHeight)
                ? m_RawWorkspaceLabUi.lowerShelfHeight
                : kRawLabLowerShelfDefaultHeight,
            minimumLowerShelf,
            maximumLowerShelf);
        const bool showLowerShelf =
            m_RawWorkspaceLabUi.lowerShelfOpen &&
            editingHeight >= kRawLabPreviewMinimumHeight + 80.0f + kRawLabSplitterSize;
        const float previewHeight = showLowerShelf
            ? editingHeight - m_RawWorkspaceLabUi.lowerShelfHeight - kRawLabSplitterSize
            : editingHeight;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
        ImGui::BeginChild(
            "RawLabPreviewRegion",
            ImVec2(0.0f, previewHeight),
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        RenderRawWorkspaceLabPreview(selectedSource, context);
        ImGui::EndChild();
        ImGui::PopStyleVar();

        if (showLowerShelf) {
            ImGui::InvisibleButton(
                "##RawLabLowerShelfHeightSplitter",
                ImVec2(ImGui::GetContentRegionAvail().x, kRawLabSplitterSize));
            const bool shelfSplitterHovered = ImGui::IsItemHovered();
            const bool shelfSplitterActive = ImGui::IsItemActive();
            if (shelfSplitterHovered || shelfSplitterActive) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                const ImVec2 minimum = ImGui::GetItemRectMin();
                const ImVec2 maximum = ImGui::GetItemRectMax();
                const float y = (minimum.y + maximum.y) * 0.5f;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(minimum.x, y),
                    ImVec2(maximum.x, y),
                    ImGui::GetColorU32(
                        shelfSplitterActive ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab),
                    2.0f);
            }
            if (shelfSplitterActive) {
                const float previous = m_RawWorkspaceLabUi.lowerShelfHeight;
                m_RawWorkspaceLabUi.lowerShelfHeight = std::clamp(
                    previous - ImGui::GetIO().MouseDelta.y,
                    minimumLowerShelf,
                    maximumLowerShelf);
                if (std::abs(previous - m_RawWorkspaceLabUi.lowerShelfHeight) > 0.01f) {
                    SaveRawWorkspaceAppState();
                }
            }

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
            ImGui::BeginChild(
                "RawLabLowerShelf",
                ImVec2(0.0f, m_RawWorkspaceLabUi.lowerShelfHeight),
                false,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            const char* placeholder = "Future grading surface";
            const ImVec2 placeholderSize = ImGui::CalcTextSize(placeholder);
            const ImVec2 available = ImGui::GetContentRegionAvail();
            ImGui::SetCursorPos(ImVec2(
                std::max(0.0f, (available.x - placeholderSize.x) * 0.5f),
                std::max(0.0f, (available.y - placeholderSize.y) * 0.5f)));
            ImGui::TextDisabled("%s", placeholder);
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();

        if (showFilmstrip) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
            ImGui::BeginChild(
                "RawLabFilmstrip",
                ImVec2(0.0f, filmstripHeight),
                false,
                ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::InvisibleButton(
                "##RawLabFilmstripHeightSplitter",
                ImVec2(ImGui::GetContentRegionAvail().x, 8.0f));
            const bool filmstripSplitterHovered = ImGui::IsItemHovered();
            const bool filmstripSplitterActive = ImGui::IsItemActive();
            if (filmstripSplitterHovered || filmstripSplitterActive) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                const ImVec2 minimum = ImGui::GetItemRectMin();
                const ImVec2 maximum = ImGui::GetItemRectMax();
                const float y = (minimum.y + maximum.y) * 0.5f;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(minimum.x, y),
                    ImVec2(maximum.x, y),
                    ImGui::GetColorU32(
                        filmstripSplitterActive ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab),
                    2.0f);
            }
            if (filmstripSplitterActive) {
                const float previous = m_RawWorkspaceLabUi.filmstripHeight;
                m_RawWorkspaceLabUi.filmstripHeight = std::clamp(
                    previous - ImGui::GetIO().MouseDelta.y,
                    kRawLabFilmstripMinimumHeight,
                    kRawLabFilmstripMaximumHeight);
                if (std::abs(previous - m_RawWorkspaceLabUi.filmstripHeight) > 0.01f) {
                    SaveRawWorkspaceAppState();
                }
            }
            RenderRawWorkspaceLabGalleryHeader(false);
            ImGui::BeginChild(
                "RawLabFilmstripScroll",
                ImVec2(0.0f, 0.0f),
                false,
                ImGuiWindowFlags_HorizontalScrollbar);
            RenderRawWorkspaceLabGalleryContent(true);
            ImGui::EndChild();
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        RenderRawWorkspaceLabSecondarySheet(
            context,
            railMinimum,
            railSize,
            secondarySheetOpenedThisFrame);
    }

    RenderRawWorkspaceLifecyclePopups();
    const bool projectLoadBusy = IsRawWorkspaceProjectLoadBusy();
    const bool projectSaveBusy = IsRawWorkspaceProjectSaveBusy();
    if (Async::IsBusy(scanSnapshot.state)) {
        ImGuiExtras::RenderBusyOverlay(
            scanSnapshot.statusText.empty() ? "Scanning RAW Workspace..." : scanSnapshot.statusText.c_str());
    } else if (projectLoadBusy) {
        const std::string status = GetRawWorkspaceProjectLoadStatusText();
        ImGuiExtras::RenderBusyOverlay(status.empty() ? "Loading RAW project..." : status.c_str());
    } else if (projectSaveBusy) {
        const std::string status = GetRawWorkspaceProjectSaveStatusText();
        ImGuiExtras::RenderBusyOverlay(status.empty() ? "Saving RAW project..." : status.c_str());
    }
    (void)thumbnailSnapshot;
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}
