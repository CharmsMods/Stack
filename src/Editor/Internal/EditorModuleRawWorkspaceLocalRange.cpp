#include "Editor/EditorModule.h"

#include "Utils/ImGuiExtras.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr int kRawLocalRangeMaxPoints = 12;
constexpr float kRawLocalRangeHitRadius = 14.0f;
constexpr float kRawLocalRangeMinDeltaEv = -4.0f;
constexpr float kRawLocalRangeMaxDeltaEv = 4.0f;

void TooltipIfHovered(const char* text, ImGuiHoveredFlags flags = 0) {
    if (text != nullptr && text[0] != '\0' && ImGui::IsItemHovered(flags)) {
        ImGui::SetTooltip("%s", text);
    }
}

bool NearlyEqual(float a, float b, float epsilon = 0.001f) {
    return std::abs(a - b) <= epsilon;
}

bool RawLocalExposureLooksUntouched(const Stack::RawRecipe::RawLocalExposureRecipe& localExposure) {
    return !localExposure.enabled &&
        (NearlyEqual(localExposure.amount, 0.85f) ||
            NearlyEqual(localExposure.amount, 0.35f) ||
            NearlyEqual(localExposure.amount, 1.0f)) &&
        NearlyEqual(localExposure.shadowLiftEv, 0.0f) &&
        NearlyEqual(localExposure.highlightCompressionEv, 0.0f) &&
        NearlyEqual(localExposure.localBaselineEv, 0.0f) &&
        NearlyEqual(localExposure.noiseGuardBias, 0.0f) &&
        NearlyEqual(localExposure.highlightGuardBias, 0.0f) &&
        NearlyEqual(localExposure.shadowGuardBias, 0.0f) &&
        NearlyEqual(localExposure.smoothGradientProtection, 0.85f) &&
        NearlyEqual(localExposure.haloGuard, 0.90f);
}

float ProtectDetailFromLocalExposure(const Stack::RawRecipe::RawLocalExposureRecipe& localExposure) {
    const float noise = std::clamp(localExposure.noiseGuardBias * 0.5f + 0.5f, 0.0f, 1.0f);
    const float highlight = std::clamp(localExposure.highlightGuardBias * 0.5f + 0.5f, 0.0f, 1.0f);
    const float shadow = std::clamp(localExposure.shadowGuardBias * 0.5f + 0.5f, 0.0f, 1.0f);
    const float gradient = std::clamp(localExposure.smoothGradientProtection, 0.0f, 1.0f);
    const float edge = std::clamp(localExposure.haloGuard, 0.0f, 1.0f);
    return std::clamp((noise + highlight + shadow + gradient + edge) * 0.20f, 0.0f, 1.0f);
}

void ApplyProtectDetailToLocalExposure(
    Stack::RawRecipe::RawLocalExposureRecipe& localExposure,
    float protectDetail) {
    protectDetail = std::clamp(protectDetail, 0.0f, 1.0f);
    const float guardBias = protectDetail * 2.0f - 1.0f;
    localExposure.noiseGuardBias = guardBias;
    localExposure.highlightGuardBias = guardBias;
    localExposure.shadowGuardBias = guardBias;
    localExposure.smoothGradientProtection = std::clamp(0.25f + protectDetail * 0.75f, 0.0f, 1.0f);
    localExposure.haloGuard = std::clamp(0.35f + protectDetail * 0.65f, 0.0f, 1.0f);
}

void CopyLocalRangeTargetSampleToColorTarget(
    Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    float r,
    float g,
    float b) {
    localRange.colorMaskTargetR = std::clamp(r, 0.0f, 32.0f);
    localRange.colorMaskTargetG = std::clamp(g, 0.0f, 32.0f);
    localRange.colorMaskTargetB = std::clamp(b, 0.0f, 32.0f);
}

float DisplayMapLocalRangeTargetChannel(float value) {
    const float sceneLinear = std::max(0.0f, value);
    const float compressed = sceneLinear / (1.0f + sceneLinear);
    return std::pow(std::clamp(compressed, 0.0f, 1.0f), 1.0f / 2.2f);
}

ImVec4 DisplayMapLocalRangeTargetColor(float r, float g, float b) {
    return ImVec4(
        DisplayMapLocalRangeTargetChannel(r),
        DisplayMapLocalRangeTargetChannel(g),
        DisplayMapLocalRangeTargetChannel(b),
        1.0f);
}

void SameLineIfLocalRangeRowFits(float nextItemWidth, float spacing) {
    const float contentRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if (ImGui::GetItemRectMax().x + spacing + nextItemWidth <= contentRight) {
        ImGui::SameLine(0.0f, spacing);
    }
}

void RenderLocalRangeTargetSwatch(
    const char* id,
    float r,
    float g,
    float b,
    const char* sourceLabel) {
    const ImVec4 displayColor = DisplayMapLocalRangeTargetColor(r, g, b);
    ImGui::ColorButton(
        id,
        displayColor,
        ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoAlpha,
        ImVec2(22.0f, 16.0f));
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(sourceLabel != nullptr ? sourceLabel : "Display-mapped color target");
        ImGui::EndTooltip();
    }
}

bool RenderLocalRangeTargetSummaryRow(
    bool hasTargetSample,
    float targetEv,
    float targetDeltaEv,
    bool hasColorPreview,
    float colorR,
    float colorG,
    float colorB,
    bool* useColorTarget) {
    bool changed = false;
    ImGui::PushID("RawLocalRangeTargetSummaryRow");
    (void)hasTargetSample;
    (void)targetEv;
    (void)targetDeltaEv;

    const float colorTargetWidth =
        ImGui::CalcTextSize("Color Target").x +
        ImGui::GetFrameHeight() +
        ImGui::CalcTextSize("On").x +
        28.0f;
    SameLineIfLocalRangeRowFits(colorTargetWidth, 10.0f);
    ImGui::AlignTextToFramePadding();
    if (useColorTarget != nullptr) {
        changed = ImGui::Checkbox("Color Target", useColorTarget);
    } else {
        ImGui::TextDisabled("Color Target");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Limit Local Range to the sampled color family without losing the stored sample.");
    }
    if (hasColorPreview) {
        ImGui::SameLine(0.0f, 6.0f);
        RenderLocalRangeTargetSwatch(
            "##ColorTargetSwatch",
            colorR,
            colorG,
            colorB,
            hasTargetSample ? "Last target sample" : "Recipe color target");
    }
    ImGui::PopID();
    return changed;
}

int LocalRangeOverlayModeToIndex(const std::string& mode) {
    if (mode == "affected-tones") {
        return 1;
    }
    if (mode == "delta-map") {
        return 2;
    }
    if (mode == "region-mask") {
        return 3;
    }
    return 0;
}

const char* LocalRangeOverlayModeFromIndex(int index) {
    switch (std::clamp(index, 0, 3)) {
        case 1: return "affected-tones";
        case 2: return "delta-map";
        case 3: return "region-mask";
        case 0:
        default:
            return "none";
    }
}

int LocalRangeRegionMaskModeToIndex(const std::string& mode) {
    if (mode == "radial-gradient") {
        return 1;
    }
    if (mode == "luminance-range") {
        return 2;
    }
    return 0;
}

const char* LocalRangeRegionMaskModeFromIndex(int index) {
    switch (std::clamp(index, 0, 2)) {
        case 1: return "radial-gradient";
        case 2: return "luminance-range";
        case 0:
        default:
            return "linear-gradient";
    }
}

Stack::RawRecipe::RawLocalRangeRecipe BuildLocalRangeUiRecipe(
    const Stack::RawRecipe::RawLocalRangeRecipe& localRange) {
    Stack::RawRecipe::RawLocalRangeRecipe recipe =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(localRange);
    const float minEv = recipe.minEv;
    const float maxEv = recipe.maxEv;
    if (recipe.points.empty()) {
        recipe.points = Stack::RawRecipe::DefaultLocalRangePoints(minEv, maxEv);
    }

    std::sort(recipe.points.begin(), recipe.points.end(), [](const auto& a, const auto& b) {
        return a.ev < b.ev;
    });
    if (recipe.points.empty() || recipe.points.front().ev > minEv + 0.001f) {
        recipe.points.insert(recipe.points.begin(), { minEv, 0.0f });
    } else {
        recipe.points.front().ev = minEv;
    }
    if (recipe.points.size() == 1 || recipe.points.back().ev < maxEv - 0.001f) {
        recipe.points.push_back({ maxEv, 0.0f });
    } else {
        recipe.points.back().ev = maxEv;
    }
    while (recipe.points.size() > static_cast<std::size_t>(kRawLocalRangeMaxPoints)) {
        recipe.points.erase(recipe.points.end() - 2);
    }
    return Stack::RawRecipe::SanitizeLocalRangeRecipe(recipe);
}

bool DrawLocalRangeWidget(
    Stack::RawRecipe::RawLocalRangeRecipe& localRange,
    const ImVec2& size,
    int* outSelectedPoint,
    const float* sampledSceneEv = nullptr) {
    static int selectedPoint = -1;
    static int draggingPoint = -1;
    static int contextPoint = -1;

    bool changed = false;
    localRange = BuildLocalRangeUiRecipe(localRange);
    std::vector<Stack::RawRecipe::RawLocalRangePoint>& points = localRange.points;
    if (selectedPoint >= static_cast<int>(points.size())) {
        selectedPoint = -1;
    }
    if (draggingPoint >= static_cast<int>(points.size())) {
        draggingPoint = -1;
    }

    ImGui::InvisibleButton("##RawLocalRangeGraph", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 bg = ImGui::GetColorU32(ImGuiCol_FrameBg);
    const ImU32 border = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 grid = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.28f);
    const ImU32 axis = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.55f);
    const ImU32 curve = ImGui::GetColorU32(ImGuiCol_CheckMark);
    drawList->AddRectFilled(min, max, bg, 5.0f);
    drawList->AddRect(min, max, border, 5.0f);

    const float labelPad = std::max(22.0f, ImGui::GetFontSize() + 6.0f);
    const ImRect plot(
        ImVec2(min.x + 34.0f, min.y + 14.0f),
        ImVec2(max.x - 10.0f, max.y - labelPad));
    drawList->AddRect(plot.Min, plot.Max, ImGui::GetColorU32(ImGuiCol_Border, 0.55f), 3.0f);

    const float evRange = std::max(0.1f, localRange.maxEv - localRange.minEv);
    const float deltaRange = kRawLocalRangeMaxDeltaEv - kRawLocalRangeMinDeltaEv;
    auto toScreen = [&](const Stack::RawRecipe::RawLocalRangePoint& point) {
        const float xNorm = std::clamp((point.ev - localRange.minEv) / evRange, 0.0f, 1.0f);
        const float yNorm = std::clamp((point.deltaEv - kRawLocalRangeMinDeltaEv) / deltaRange, 0.0f, 1.0f);
        return ImVec2(
            plot.Min.x + plot.GetWidth() * xNorm,
            plot.Max.y - plot.GetHeight() * yNorm);
    };
    auto fromScreen = [&](const ImVec2& screen) {
        const float xNorm = std::clamp((screen.x - plot.Min.x) / std::max(1.0f, plot.GetWidth()), 0.0f, 1.0f);
        const float yNorm = std::clamp((plot.Max.y - screen.y) / std::max(1.0f, plot.GetHeight()), 0.0f, 1.0f);
        return Stack::RawRecipe::RawLocalRangePoint{
            localRange.minEv + evRange * xNorm,
            kRawLocalRangeMinDeltaEv + deltaRange * yNorm
        };
    };
    auto isEndpoint = [&](int index) {
        return index == 0 || index == static_cast<int>(points.size()) - 1;
    };

    for (int i = 1; i < 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        const float x = plot.Min.x + plot.GetWidth() * t;
        const float y = plot.Min.y + plot.GetHeight() * t;
        drawList->AddLine(ImVec2(x, plot.Min.y), ImVec2(x, plot.Max.y), grid, 1.0f);
        drawList->AddLine(ImVec2(plot.Min.x, y), ImVec2(plot.Max.x, y), grid, 1.0f);
    }
    const float zeroY = toScreen({ localRange.minEv, 0.0f }).y;
    drawList->AddLine(ImVec2(plot.Min.x, zeroY), ImVec2(plot.Max.x, zeroY), axis, 1.25f);
    if (sampledSceneEv && std::isfinite(*sampledSceneEv)) {
        const float sampleXNorm = std::clamp((*sampledSceneEv - localRange.minEv) / evRange, 0.0f, 1.0f);
        const float sampleX = plot.Min.x + plot.GetWidth() * sampleXNorm;
        const ImU32 markerColor = ImGui::GetColorU32(ImGuiCol_Text, 0.85f);
        drawList->AddLine(
            ImVec2(sampleX, plot.Min.y),
            ImVec2(sampleX, plot.Max.y),
            markerColor,
            1.5f);
        drawList->AddCircleFilled(ImVec2(sampleX, zeroY), 4.0f, markerColor, 16);
    }

    auto addSmallText = [&](const ImVec2& pos, const char* text) {
        drawList->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
    };
    char labelBuffer[64] = {};
    snprintf(labelBuffer, sizeof(labelBuffer), "%+.0f", kRawLocalRangeMaxDeltaEv);
    addSmallText(ImVec2(min.x + 6.0f, plot.Min.y - 4.0f), labelBuffer);
    snprintf(labelBuffer, sizeof(labelBuffer), "%+.0f", 0.0f);
    addSmallText(ImVec2(min.x + 6.0f, zeroY - ImGui::GetFontSize() * 0.5f), labelBuffer);
    snprintf(labelBuffer, sizeof(labelBuffer), "%+.0f", kRawLocalRangeMinDeltaEv);
    addSmallText(ImVec2(min.x + 6.0f, plot.Max.y - ImGui::GetFontSize() + 2.0f), labelBuffer);
    snprintf(labelBuffer, sizeof(labelBuffer), "%.0f EV", localRange.minEv);
    addSmallText(ImVec2(plot.Min.x, plot.Max.y + 4.0f), labelBuffer);
    addSmallText(ImVec2(plot.Min.x + plot.GetWidth() * 0.5f - 18.0f, plot.Max.y + 4.0f), "Scene EV");
    snprintf(labelBuffer, sizeof(labelBuffer), "%+.0f EV", localRange.maxEv);
    const ImVec2 rightLabelSize = ImGui::CalcTextSize(labelBuffer);
    addSmallText(ImVec2(plot.Max.x - rightLabelSize.x, plot.Max.y + 4.0f), labelBuffer);

    auto findPointNearMouse = [&]() {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int bestIndex = -1;
        float bestDist2 = kRawLocalRangeHitRadius * kRawLocalRangeHitRadius;
        for (int i = 0; i < static_cast<int>(points.size()); ++i) {
            const ImVec2 point = toScreen(points[static_cast<std::size_t>(i)]);
            const float dx = point.x - mouse.x;
            const float dy = point.y - mouse.y;
            const float dist2 = dx * dx + dy * dy;
            if (dist2 <= bestDist2) {
                bestDist2 = dist2;
                bestIndex = i;
            }
        }
        return bestIndex;
    };
    auto sortAndFind = [&](float ev, float deltaEv) {
        localRange = BuildLocalRangeUiRecipe(localRange);
        int bestIndex = -1;
        float bestDist = 100.0f;
        for (int i = 0; i < static_cast<int>(localRange.points.size()); ++i) {
            const float dist =
                std::abs(localRange.points[static_cast<std::size_t>(i)].ev - ev) +
                std::abs(localRange.points[static_cast<std::size_t>(i)].deltaEv - deltaEv);
            if (dist < bestDist) {
                bestDist = dist;
                bestIndex = i;
            }
        }
        return bestIndex;
    };

    const int hoveredPoint = findPointNearMouse();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hoveredPoint >= 0) {
            selectedPoint = hoveredPoint;
            draggingPoint = hoveredPoint;
        } else if (points.size() < static_cast<std::size_t>(kRawLocalRangeMaxPoints)) {
            const Stack::RawRecipe::RawLocalRangePoint point = fromScreen(ImGui::GetIO().MousePos);
            points.push_back(point);
            localRange.enabled = true;
            selectedPoint = sortAndFind(point.ev, point.deltaEv);
            draggingPoint = selectedPoint;
            changed = true;
        }
    }
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
        draggingPoint >= 0 &&
        draggingPoint < static_cast<int>(points.size())) {
        Stack::RawRecipe::RawLocalRangePoint point = fromScreen(ImGui::GetIO().MousePos);
        if (isEndpoint(draggingPoint)) {
            point.ev = draggingPoint == 0 ? localRange.minEv : localRange.maxEv;
        } else {
            const float left = points[static_cast<std::size_t>(draggingPoint - 1)].ev + 0.001f;
            const float right = points[static_cast<std::size_t>(draggingPoint + 1)].ev - 0.001f;
            point.ev = std::clamp(point.ev, left, right);
        }
        point.deltaEv = std::clamp(point.deltaEv, kRawLocalRangeMinDeltaEv, kRawLocalRangeMaxDeltaEv);
        points[static_cast<std::size_t>(draggingPoint)] = point;
        selectedPoint = draggingPoint;
        localRange.enabled = true;
        changed = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        draggingPoint = -1;
    }
    if ((hovered || active) &&
        selectedPoint > 0 &&
        selectedPoint < static_cast<int>(points.size()) - 1 &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
        points.erase(points.begin() + selectedPoint);
        selectedPoint = -1;
        draggingPoint = -1;
        localRange.enabled = true;
        changed = true;
    }
    if (hovered && hoveredPoint > 0 &&
        hoveredPoint < static_cast<int>(points.size()) - 1 &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        selectedPoint = hoveredPoint;
        contextPoint = hoveredPoint;
        ImGui::OpenPopup("RawLocalRangePointMenu");
    }
    if (ImGui::BeginPopup("RawLocalRangePointMenu")) {
        if (contextPoint > 0 && contextPoint < static_cast<int>(points.size()) - 1) {
            if (ImGui::MenuItem("Delete Point")) {
                points.erase(points.begin() + contextPoint);
                selectedPoint = -1;
                draggingPoint = -1;
                contextPoint = -1;
                localRange.enabled = true;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }

    if (changed) {
        localRange = BuildLocalRangeUiRecipe(localRange);
        if (draggingPoint >= static_cast<int>(localRange.points.size())) {
            draggingPoint = -1;
        }
    }

    for (std::size_t i = 1; i < points.size(); ++i) {
        drawList->AddLine(toScreen(points[i - 1]), toScreen(points[i]), curve, 2.0f);
    }
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        const ImVec2 point = toScreen(points[static_cast<std::size_t>(i)]);
        const bool selected = i == selectedPoint;
        const bool endpoint = isEndpoint(i);
        const ImU32 pointColor = selected
            ? ImGui::GetColorU32(ImGuiCol_Text)
            : (endpoint ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : curve);
        drawList->AddCircleFilled(point, selected ? 6.0f : 4.5f, pointColor, 18);
        drawList->AddCircle(point, selected ? 7.5f : 5.8f, IM_COL32(0, 0, 0, 145), 18, 1.0f);
    }
    if (outSelectedPoint) {
        *outSelectedPoint =
            selectedPoint >= 0 && selectedPoint < static_cast<int>(points.size())
                ? selectedPoint
                : -1;
    }
    return changed;
}

} // namespace

bool EditorModule::RenderRawWorkspaceLocalRangeControls(
    const Stack::RawWorkspace::SourceRecord* selectedSource,
    Stack::RawRecipe::RawDevelopmentRecipe& editedRecipe,
    float controlWidth) {
    if (selectedSource == nullptr ||
        !ImGui::CollapsingHeader("Local Range", ImGuiTreeNodeFlags_DefaultOpen)) {
        return false;
    }

    bool changed = false;
    editedRecipe.localRange = BuildLocalRangeUiRecipe(editedRecipe.localRange);
    bool enabled = editedRecipe.localRange.enabled;
    if (ImGuiExtras::NodeCheckbox("Enable Local Range", "##RawLocalRangeEnabled", &enabled, controlWidth)) {
        editedRecipe.localRange.enabled = enabled;
        changed = true;
    }
    TooltipIfHovered("Apply the scene-EV graph before Finish Tone and View Transform.");

    const float actionGap = 6.0f;
    const float actionButtonWidth = std::max(68.0f, (controlWidth - actionGap) * 0.5f);
    if (ImGuiExtras::RichFullWidthButton(
            m_RawWorkspaceLocalRangeTargetMode ? "Stop Target" : "Target",
            actionButtonWidth,
            0.0f)) {
        if (m_RawWorkspaceLocalRangeTargetMode) {
            m_RawWorkspaceLocalRangeTargetMode = false;
            m_RawWorkspaceLocalRangeTargetDragging = false;
            m_RawWorkspaceLocalRangeTargetSamplePending = false;
            m_RawWorkspaceLocalRangeTargetApplyWhenSampled = false;
        } else {
            m_RawWorkspaceLocalRangeTargetMode = true;
        }
    }
    TooltipIfHovered("Click the preview and drag up/down to add or edit a Local Range point. Target samples scene EV and color together; Color Target uses the color only when enabled.");
    ImGui::SameLine(0.0f, actionGap);
    if (ImGuiExtras::RichFullWidthButton("Reset", actionButtonWidth, 0.0f)) {
        editedRecipe.localRange = BuildLocalRangeUiRecipe(
            Stack::RawRecipe::ApplyLocalRangePreset(
                editedRecipe.localRange,
                Stack::RawRecipe::RawLocalRangePreset::Reset));
        changed = true;
    }
    TooltipIfHovered("Reset Local Range graph points and section controls.");

    const float presetGap = 6.0f;
    const float presetButtonWidth = std::max(46.0f, (controlWidth - presetGap * 2.0f) / 3.0f);
    auto applyLocalRangePreset = [&](Stack::RawRecipe::RawLocalRangePreset preset) {
        editedRecipe.localRange = BuildLocalRangeUiRecipe(
            Stack::RawRecipe::ApplyLocalRangePreset(editedRecipe.localRange, preset));
        changed = true;
    };
    if (ImGuiExtras::RichFullWidthButton("Open Shadows", presetButtonWidth, 0.0f)) {
        applyLocalRangePreset(Stack::RawRecipe::RawLocalRangePreset::OpenShadows);
    }
    ImGui::SameLine(0.0f, presetGap);
    if (ImGuiExtras::RichFullWidthButton("Hold Highlights", presetButtonWidth, 0.0f)) {
        applyLocalRangePreset(Stack::RawRecipe::RawLocalRangePreset::HoldHighlights);
    }
    ImGui::SameLine(0.0f, presetGap);
    if (ImGuiExtras::RichFullWidthButton("Compress", presetButtonWidth, 0.0f)) {
        applyLocalRangePreset(Stack::RawRecipe::RawLocalRangePreset::CompressRange);
    }
    TooltipIfHovered("Compresses large scene-EV differences into a gentler local range.");

    int selectedLocalRangePoint = -1;
    float sampledSceneEv = 0.0f;
    const float* sampledSceneEvMarker = nullptr;
    const bool hasTargetSample =
        HasRawWorkspaceLocalRangeTargetSampleForSource(selectedSource->relativePathKey);
    if (hasTargetSample) {
        sampledSceneEv = m_RawWorkspaceLocalRangeTargetSceneEv;
        sampledSceneEvMarker = &sampledSceneEv;
    }
    if (DrawLocalRangeWidget(
            editedRecipe.localRange,
            ImVec2(controlWidth, std::clamp(controlWidth * 0.48f, 190.0f, 230.0f)),
            &selectedLocalRangePoint,
            sampledSceneEvMarker)) {
        changed = true;
    }
    if (hasTargetSample || editedRecipe.localRange.colorMaskEnabled) {
        const bool colorPreviewUsesSample = hasTargetSample;
        const float colorR = colorPreviewUsesSample
            ? m_RawWorkspaceLocalRangeTargetSceneR
            : editedRecipe.localRange.colorMaskTargetR;
        const float colorG = colorPreviewUsesSample
            ? m_RawWorkspaceLocalRangeTargetSceneG
            : editedRecipe.localRange.colorMaskTargetG;
        const float colorB = colorPreviewUsesSample
            ? m_RawWorkspaceLocalRangeTargetSceneB
            : editedRecipe.localRange.colorMaskTargetB;
        bool useColorTarget = editedRecipe.localRange.colorMaskEnabled;
        if (RenderLocalRangeTargetSummaryRow(
                hasTargetSample,
                m_RawWorkspaceLocalRangeTargetSceneEv,
                m_RawWorkspaceLocalRangeTargetDeltaEv,
                true,
                colorR,
                colorG,
                colorB,
                &useColorTarget)) {
            editedRecipe.localRange.colorMaskEnabled = useColorTarget;
            if (useColorTarget && hasTargetSample) {
                CopyLocalRangeTargetSampleToColorTarget(
                    editedRecipe.localRange,
                    m_RawWorkspaceLocalRangeTargetSceneR,
                    m_RawWorkspaceLocalRangeTargetSceneG,
                    m_RawWorkspaceLocalRangeTargetSceneB);
            }
            changed = true;
        }
    }
    const char* overlayLabels[] = { "Off", "Affected", "Delta", "Mask" };
    int overlayIndex = LocalRangeOverlayModeToIndex(m_RawWorkspaceLocalRangeOverlayMode);
    if (ImGuiExtras::NodeCombo("Overlay", "##RawLocalRangeOverlayMode", &overlayIndex, overlayLabels, IM_ARRAYSIZE(overlayLabels), controlWidth)) {
        m_RawWorkspaceLocalRangeOverlayMode = LocalRangeOverlayModeFromIndex(overlayIndex);
        ClearRawWorkspaceLocalRangeOverlayState();
        MarkRenderRefreshDirty();
    }
    TooltipIfHovered("Preview-only view of Local Range affected tones, delta, or mask after region and color targeting.");

    float strength = editedRecipe.localRange.strength;
    if (ImGuiExtras::NodeSliderFloat("Strength", "##RawLocalRangeStrength", &strength, 0.0f, 1.0f, "%.2f", controlWidth)) {
        editedRecipe.localRange.strength = strength;
        changed = true;
    }

    if (ImGui::TreeNodeEx("Advanced##RawLocalRangeAdvanced")) {
        float smoothness = editedRecipe.localRange.smoothness;
        if (ImGuiExtras::NodeSliderFloat("Smoothness", "##RawLocalRangeSmoothness", &smoothness, 0.0f, 1.0f, "%.2f", controlWidth)) {
            editedRecipe.localRange.smoothness = smoothness;
            changed = true;
        }
        TooltipIfHovered("Broadens the tone-zone map so Local Range follows regions instead of fine texture.");
        float edgeProtection = editedRecipe.localRange.edgeProtection;
        if (ImGuiExtras::NodeSliderFloat("Edge Protection", "##RawLocalRangeEdgeProtection", &edgeProtection, 0.0f, 1.0f, "%.2f", controlWidth)) {
            editedRecipe.localRange.edgeProtection = edgeProtection;
            changed = true;
        }
        TooltipIfHovered("Reduces Local Range bleed across strong brightness edges.");
        float detailProtection = editedRecipe.localRange.detailProtection;
        if (ImGuiExtras::NodeSliderFloat("Detail Protection", "##RawLocalRangeDetailProtection", &detailProtection, 0.0f, 1.0f, "%.2f", controlWidth)) {
            editedRecipe.localRange.detailProtection = detailProtection;
            changed = true;
        }
        TooltipIfHovered("Keeps small texture from becoming separate exposure zones.");
        ImGui::TreePop();
    }

    const bool compactColorTargetVisible = hasTargetSample || editedRecipe.localRange.colorMaskEnabled;
    const ImGuiTreeNodeFlags colorMaskFlags =
        editedRecipe.localRange.colorMaskEnabled ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    if (ImGui::TreeNodeEx("Color Target##RawLocalRangeColorMask", colorMaskFlags)) {
        if (!compactColorTargetVisible) {
            bool colorMaskEnabled = editedRecipe.localRange.colorMaskEnabled;
            if (ImGuiExtras::NodeCheckbox("Use Color Target", "##RawLocalRangeColorMaskEnabled", &colorMaskEnabled, controlWidth)) {
                editedRecipe.localRange.colorMaskEnabled = colorMaskEnabled;
                if (colorMaskEnabled && hasTargetSample) {
                    CopyLocalRangeTargetSampleToColorTarget(
                        editedRecipe.localRange,
                        m_RawWorkspaceLocalRangeTargetSceneR,
                        m_RawWorkspaceLocalRangeTargetSceneG,
                        m_RawWorkspaceLocalRangeTargetSceneB);
                }
                changed = true;
            }
            TooltipIfHovered("Multiplies the Local Range result by color similarity in the pre-finish scene-linear image. Useful when the same luminance appears in different material colors.");
        }

        ImGui::BeginDisabled(!hasTargetSample);
        if (ImGuiExtras::RichFullWidthButton("Use Sample", controlWidth, 0.0f)) {
            editedRecipe.localRange.colorMaskEnabled = true;
            CopyLocalRangeTargetSampleToColorTarget(
                editedRecipe.localRange,
                m_RawWorkspaceLocalRangeTargetSceneR,
                m_RawWorkspaceLocalRangeTargetSceneG,
                m_RawWorkspaceLocalRangeTargetSceneB);
            m_RawWorkspaceLocalRangeOverlayMode = "region-mask";
            ClearRawWorkspaceLocalRangeOverlayState();
            MarkRenderRefreshDirty();
            changed = true;
        }
        ImGui::EndDisabled();
        TooltipIfHovered(
            hasTargetSample
                ? "Copies the current target sample into Color Target."
                : "Use Target first, then copy the sampled color.",
            hasTargetSample ? 0 : ImGuiHoveredFlags_AllowWhenDisabled);

        ImGui::BeginDisabled(!editedRecipe.localRange.colorMaskEnabled);
        float targetColor[3] = {
            editedRecipe.localRange.colorMaskTargetR,
            editedRecipe.localRange.colorMaskTargetG,
            editedRecipe.localRange.colorMaskTargetB
        };
        if (ImGuiExtras::NodeColorEdit3(
                "Target Color",
                "##RawLocalRangeColorMaskTarget",
                targetColor,
                ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_HDR,
                controlWidth)) {
            editedRecipe.localRange.colorMaskTargetR = std::clamp(targetColor[0], 0.0f, 32.0f);
            editedRecipe.localRange.colorMaskTargetG = std::clamp(targetColor[1], 0.0f, 32.0f);
            editedRecipe.localRange.colorMaskTargetB = std::clamp(targetColor[2], 0.0f, 32.0f);
            changed = true;
        }
        TooltipIfHovered("Scene-linear target color sampled before Local Range and before View Transform.");

        float hueWidth = editedRecipe.localRange.colorMaskHueWidth;
        if (ImGuiExtras::NodeSliderFloat("Hue Width", "##RawLocalRangeColorMaskHueWidth", &hueWidth, 0.02f, 1.20f, "%.2f", controlWidth)) {
            editedRecipe.localRange.colorMaskHueWidth = hueWidth;
            changed = true;
        }
        TooltipIfHovered("Width of the color-direction match. Lower values isolate a tighter color family; higher values include nearby hues.");

        float colorFeather = editedRecipe.localRange.colorMaskFeather;
        if (ImGuiExtras::NodeSliderFloat("Color Feather", "##RawLocalRangeColorMaskFeather", &colorFeather, 0.0f, 1.0f, "%.2f", controlWidth)) {
            editedRecipe.localRange.colorMaskFeather = colorFeather;
            changed = true;
        }

        float neutralGuard = editedRecipe.localRange.colorMaskMinChroma;
        if (ImGuiExtras::NodeSliderFloat("Neutral Guard", "##RawLocalRangeColorMaskMinChroma", &neutralGuard, 0.0f, 0.60f, "%.2f", controlWidth)) {
            editedRecipe.localRange.colorMaskMinChroma = neutralGuard;
            changed = true;
        }
        TooltipIfHovered("For colored targets, this suppresses neutral or low-saturation pixels such as white clouds and grey glare.");

        if (ImGuiExtras::RichFullWidthButton("Show Mask", controlWidth, 0.0f)) {
            m_RawWorkspaceLocalRangeOverlayMode = "region-mask";
            ClearRawWorkspaceLocalRangeOverlayState();
            MarkRenderRefreshDirty();
        }
        TooltipIfHovered("Show the Color Target mask in the preview.");
        ImGui::EndDisabled();
        ImGui::TreePop();
    }

    const ImGuiTreeNodeFlags regionMaskFlags =
        editedRecipe.localRange.regionMaskEnabled ? ImGuiTreeNodeFlags_DefaultOpen : 0;
    if (ImGui::TreeNodeEx("Region Mask##RawLocalRangeRegionMask", regionMaskFlags)) {
        bool regionMaskEnabled = editedRecipe.localRange.regionMaskEnabled;
        if (ImGuiExtras::NodeCheckbox("Enable Region Mask", "##RawLocalRangeRegionMaskEnabled", &regionMaskEnabled, controlWidth)) {
            editedRecipe.localRange.regionMaskEnabled = regionMaskEnabled;
            changed = true;
        }
        TooltipIfHovered("Constrain Local Range to a manual image region without changing the tone-zone graph.");

        if (ImGuiExtras::RichFullWidthButton("Show Mask", controlWidth, 0.0f)) {
            m_RawWorkspaceLocalRangeOverlayMode = "region-mask";
            ClearRawWorkspaceLocalRangeOverlayState();
            MarkRenderRefreshDirty();
        }
        TooltipIfHovered("Show the Region Mask in the preview.");

        ImGui::BeginDisabled(!editedRecipe.localRange.regionMaskEnabled);
        const char* regionMaskLabels[] = { "Linear Gradient", "Radial Gradient", "Luminance Range" };
        int regionMaskIndex = LocalRangeRegionMaskModeToIndex(editedRecipe.localRange.regionMaskMode);
        if (ImGuiExtras::NodeCombo("Mask Type", "##RawLocalRangeRegionMaskMode", &regionMaskIndex, regionMaskLabels, IM_ARRAYSIZE(regionMaskLabels), controlWidth)) {
            editedRecipe.localRange.regionMaskMode = LocalRangeRegionMaskModeFromIndex(regionMaskIndex);
            changed = true;
        }

        bool regionMaskInvert = editedRecipe.localRange.regionMaskInvert;
        if (ImGuiExtras::NodeCheckbox("Invert Mask", "##RawLocalRangeRegionMaskInvert", &regionMaskInvert, controlWidth)) {
            editedRecipe.localRange.regionMaskInvert = regionMaskInvert;
            changed = true;
        }

        if (editedRecipe.localRange.regionMaskMode == "linear-gradient" ||
            editedRecipe.localRange.regionMaskMode == "radial-gradient") {
            float centerX = editedRecipe.localRange.regionMaskCenterX;
            if (ImGuiExtras::NodeSliderFloat("Center X", "##RawLocalRangeRegionMaskCenterX", &centerX, 0.0f, 1.0f, "%.2f", controlWidth)) {
                editedRecipe.localRange.regionMaskCenterX = centerX;
                changed = true;
            }
            float centerY = editedRecipe.localRange.regionMaskCenterY;
            if (ImGuiExtras::NodeSliderFloat("Center Y", "##RawLocalRangeRegionMaskCenterY", &centerY, 0.0f, 1.0f, "%.2f", controlWidth)) {
                editedRecipe.localRange.regionMaskCenterY = centerY;
                changed = true;
            }
        }

        if (editedRecipe.localRange.regionMaskMode == "linear-gradient") {
            float angle = editedRecipe.localRange.regionMaskAngleDegrees;
            if (ImGuiExtras::NodeSliderFloat("Angle", "##RawLocalRangeRegionMaskAngle", &angle, -180.0f, 180.0f, "%.0f deg", controlWidth)) {
                editedRecipe.localRange.regionMaskAngleDegrees = angle;
                changed = true;
            }
            float width = editedRecipe.localRange.regionMaskSize;
            if (ImGuiExtras::NodeSliderFloat("Width", "##RawLocalRangeRegionMaskWidth", &width, 0.02f, 1.5f, "%.2f", controlWidth)) {
                editedRecipe.localRange.regionMaskSize = width;
                changed = true;
            }
            TooltipIfHovered("Controls how broad the gradient transition is across the image.");
            float feather = editedRecipe.localRange.regionMaskFeather;
            if (ImGuiExtras::NodeSliderFloat("Feather", "##RawLocalRangeRegionMaskLinearFeather", &feather, 0.0f, 1.0f, "%.2f", controlWidth)) {
                editedRecipe.localRange.regionMaskFeather = feather;
                changed = true;
            }
        } else if (editedRecipe.localRange.regionMaskMode == "radial-gradient") {
            float radius = editedRecipe.localRange.regionMaskSize;
            if (ImGuiExtras::NodeSliderFloat("Radius", "##RawLocalRangeRegionMaskRadius", &radius, 0.02f, 1.5f, "%.2f", controlWidth)) {
                editedRecipe.localRange.regionMaskSize = radius;
                changed = true;
            }
            float feather = editedRecipe.localRange.regionMaskFeather;
            if (ImGuiExtras::NodeSliderFloat("Feather", "##RawLocalRangeRegionMaskRadialFeather", &feather, 0.0f, 1.0f, "%.2f", controlWidth)) {
                editedRecipe.localRange.regionMaskFeather = feather;
                changed = true;
            }
        } else if (editedRecipe.localRange.regionMaskMode == "luminance-range") {
            float lowEv = editedRecipe.localRange.regionMaskLowEv;
            if (ImGuiExtras::NodeSliderFloat("Low EV", "##RawLocalRangeRegionMaskLowEv", &lowEv, -16.0f, 16.0f, "%+.1f EV", controlWidth)) {
                editedRecipe.localRange.regionMaskLowEv = std::min(lowEv, editedRecipe.localRange.regionMaskHighEv - 0.1f);
                changed = true;
            }
            float highEv = editedRecipe.localRange.regionMaskHighEv;
            if (ImGuiExtras::NodeSliderFloat("High EV", "##RawLocalRangeRegionMaskHighEv", &highEv, -16.0f, 16.0f, "%+.1f EV", controlWidth)) {
                editedRecipe.localRange.regionMaskHighEv = std::max(highEv, editedRecipe.localRange.regionMaskLowEv + 0.1f);
                changed = true;
            }
            float feather = editedRecipe.localRange.regionMaskFeather;
            if (ImGuiExtras::NodeSliderFloat("EV Feather", "##RawLocalRangeRegionMaskEvFeather", &feather, 0.0f, 1.0f, "%.2f", controlWidth)) {
                editedRecipe.localRange.regionMaskFeather = feather;
                changed = true;
            }
            TooltipIfHovered("Softens the selected scene-EV range; 1.00 is a broad four-stop feather.");
        }
        ImGui::EndDisabled();
        ImGui::TreePop();
    }

    const bool showLegacyLocalExposure =
        Stack::RawRecipe::IsLocalExposureEnabled(editedRecipe) ||
        !RawLocalExposureLooksUntouched(editedRecipe.localExposure);
    if (showLegacyLocalExposure &&
        ImGui::TreeNodeEx("Legacy Local Exposure##RawLocalExposureCompatibility")) {
        if (Stack::RawRecipe::IsLocalExposureEnabled(editedRecipe) &&
            ImGuiExtras::RichFullWidthButton("Convert To Local Range", controlWidth, 0.0f)) {
            editedRecipe.localRange = BuildLocalRangeUiRecipe(
                Stack::RawRecipe::LocalRangeRecipeFromLocalExposure(
                    editedRecipe.localExposure,
                    editedRecipe.localRange));
            editedRecipe.localExposure = Stack::RawRecipe::RawLocalExposureRecipe{};
            changed = true;
        }
        TooltipIfHovered("Converts old Local Exposure settings into Local Range graph points and disables the legacy block.");

        bool legacyEnabled = editedRecipe.localExposure.enabled;
        if (ImGuiExtras::NodeCheckbox("Enable Legacy Local Exposure", "##RawLegacyLocalExposureEnabled", &legacyEnabled, controlWidth)) {
            editedRecipe.localExposure.enabled = legacyEnabled;
            changed = true;
        }
        TooltipIfHovered("Compatibility controls for projects that already use the older Local Exposure block.");

        ImGui::BeginDisabled(!editedRecipe.localExposure.enabled);
        float amount = std::clamp(editedRecipe.localExposure.amount, 0.0f, 1.0f);
        if (ImGuiExtras::NodeSliderFloat("Overall Strength", "##RawLocalExposureStrength", &amount, 0.0f, 1.0f, "%.2f", controlWidth)) {
            editedRecipe.localExposure.amount = amount;
            changed = true;
        }

        float openShadows = std::clamp(editedRecipe.localExposure.shadowLiftEv, 0.0f, 4.0f);
        if (ImGuiExtras::NodeSliderFloat("Open Shadows", "##RawLocalOpenShadows", &openShadows, 0.0f, 4.0f, "%.2f EV", controlWidth)) {
            editedRecipe.localExposure.shadowLiftEv = openShadows;
            changed = true;
        }

        float recoverHighlights = std::clamp(-editedRecipe.localExposure.highlightCompressionEv, 0.0f, 4.0f);
        if (ImGuiExtras::NodeSliderFloat("Recover Highlights", "##RawLocalRecoverHighlights", &recoverHighlights, 0.0f, 4.0f, "%.2f EV", controlWidth)) {
            editedRecipe.localExposure.highlightCompressionEv = -recoverHighlights;
            changed = true;
        }

        float baseline = editedRecipe.localExposure.localBaselineEv;
        if (ImGuiExtras::NodeSliderFloat("Balance", "##RawLocalBalance", &baseline, -1.25f, 1.25f, "%+.2f EV", controlWidth)) {
            editedRecipe.localExposure.localBaselineEv = baseline;
            changed = true;
        }

        float protectDetail = ProtectDetailFromLocalExposure(editedRecipe.localExposure);
        if (ImGuiExtras::NodeSliderFloat("Protect Detail", "##RawLocalProtectDetail", &protectDetail, 0.0f, 1.0f, "%.2f", controlWidth)) {
            ApplyProtectDetailToLocalExposure(editedRecipe.localExposure, protectDetail);
            changed = true;
        }

        if (ImGui::TreeNodeEx("Advanced##RawLocalExposureAdvanced")) {
            float shadowLift = std::clamp(editedRecipe.localExposure.shadowLiftEv, 0.0f, 4.0f);
            if (ImGuiExtras::NodeSliderFloat("Shadow Lift EV", "##RawLocalShadowLiftEv", &shadowLift, 0.0f, 4.0f, "%.2f EV", controlWidth)) {
                editedRecipe.localExposure.shadowLiftEv = shadowLift;
                changed = true;
            }

            float highlightCompression = std::clamp(-editedRecipe.localExposure.highlightCompressionEv, 0.0f, 4.0f);
            if (ImGuiExtras::NodeSliderFloat("Highlight Recovery EV", "##RawLocalHighlightRecoveryEv", &highlightCompression, 0.0f, 4.0f, "%.2f EV", controlWidth)) {
                editedRecipe.localExposure.highlightCompressionEv = -highlightCompression;
                changed = true;
            }

            float noiseGuard = editedRecipe.localExposure.noiseGuardBias;
            if (ImGuiExtras::NodeSliderFloat("Noise Guard", "##RawLocalNoiseGuard", &noiseGuard, -1.0f, 1.0f, "%+.2f", controlWidth)) {
                editedRecipe.localExposure.noiseGuardBias = noiseGuard;
                changed = true;
            }

            float highlightGuard = editedRecipe.localExposure.highlightGuardBias;
            if (ImGuiExtras::NodeSliderFloat("Highlight Guard", "##RawLocalHighlightGuard", &highlightGuard, -1.0f, 1.0f, "%+.2f", controlWidth)) {
                editedRecipe.localExposure.highlightGuardBias = highlightGuard;
                changed = true;
            }

            float shadowGuard = editedRecipe.localExposure.shadowGuardBias;
            if (ImGuiExtras::NodeSliderFloat("Shadow Guard", "##RawLocalShadowGuard", &shadowGuard, -1.0f, 1.0f, "%+.2f", controlWidth)) {
                editedRecipe.localExposure.shadowGuardBias = shadowGuard;
                changed = true;
            }

            float smoothGradientProtection = editedRecipe.localExposure.smoothGradientProtection;
            if (ImGuiExtras::NodeSliderFloat("Gradient Protection", "##RawLocalGradientProtection", &smoothGradientProtection, 0.0f, 1.0f, "%.2f", controlWidth)) {
                editedRecipe.localExposure.smoothGradientProtection = smoothGradientProtection;
                changed = true;
            }

            float haloGuard = editedRecipe.localExposure.haloGuard;
            if (ImGuiExtras::NodeSliderFloat("Edge Protection", "##RawLocalEdgeProtection", &haloGuard, 0.0f, 1.0f, "%.2f", controlWidth)) {
                editedRecipe.localExposure.haloGuard = haloGuard;
                changed = true;
            }
            ImGui::TreePop();
        }
        ImGui::EndDisabled();
        ImGui::TreePop();
    }

    return changed;
}
