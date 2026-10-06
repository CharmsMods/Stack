#include "Raw/HdrDisplayMapping.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include "Async/TaskSystem.h"
#include "Library/LibraryManager.h"
#include "Persistence/RawProjectEditPipeline.h"
#include "Persistence/ProjectIndex.h"
#include "Raw/RawGalleryFileActions.h"
#include "Raw/MultiFrameHdr/Contracts.h"
#include "Renderer/GLHelpers.h"
#include "Restormer/RestormerClient.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "Utils/RawGallerySelectionVisuals.h"

#include <GLFW/glfw3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

using namespace Stack::Editor::RawLabInternal;
using RawCurveGraphUiState = Stack::EditorModuleTypes::RawCurveGraphUiState;

namespace {
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
    viewTransform["contrastModel"] =
        Stack::RawRecipe::kViewContrastModelPivotedLogV2;
}

float EncodeViewGraphOutput(float linearValue, bool encodeSrgb) {
    const float value = std::clamp(linearValue, 0.0f, 1.0f);
    if (!encodeSrgb) {
        return value;
    }
    return value <= 0.0031308f
        ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

ImVec2 GraphToScreen(const ImRect& rect, float x, float y) {
    return ImVec2(
        rect.Min.x + std::clamp(x, 0.0f, 1.0f) * rect.GetWidth(),
        rect.Max.y - std::clamp(y, 0.0f, 1.0f) * rect.GetHeight());
}

bool DrawViewTransformGraph(nlohmann::json& view, const ImVec2& size, int& draggingHandle) {
    const float exposure = JsonFloat(view, "exposure", 0.0f);
    float blackEv = JsonFloat(view, "blackEv", -8.0f);
    float whiteEv = JsonFloat(view, "whiteEv", 4.0f);
    const float middleGrey = JsonFloat(view, "middleGrey", 0.18f);
    const float shoulder = JsonFloat(view, "shoulder", 0.45f);
    const float toe = JsonFloat(view, "toe", 0.18f);
    const float contrast = JsonFloat(view, "contrast", 1.0f);
    float contrastPivotEv = JsonFloat(view, "contrastPivotEv", 0.0f);
    const bool encodeSrgb = JsonBoolean(view, "encodeSrgbOutput", true);
    const float blackSceneEv = blackEv - exposure;
    const float whiteSceneEv = whiteEv - exposure;
    const float pivotSceneEv = contrastPivotEv - exposure;
    const float graphMinEv = std::min(
        -12.0f,
        std::min(blackSceneEv, pivotSceneEv) - 2.0f);
    const float graphMaxEv = std::max(
        8.0f,
        std::max(whiteSceneEv, pivotSceneEv) + 2.0f);
    const float graphSpan = std::max(1.0f, graphMaxEv - graphMinEv);

    ImGui::InvisibleButton("##RawLabViewGraph", size, ImGuiButtonFlags_MouseButtonLeft);
    const ImRect itemRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const ImRect rect(
        ImVec2(itemRect.Min.x + 27.0f, itemRect.Min.y + 6.0f),
        ImVec2(itemRect.Max.x - 5.0f, itemRect.Max.y - 19.0f));
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImVec4 disabled = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const ImU32 guideColor = ImGui::GetColorU32(
        ImVec4(disabled.x, disabled.y, disabled.z, 0.11f));
    const ImU32 zeroGuideColor = ImGui::GetColorU32(
        ImVec4(disabled.x, disabled.y, disabled.z, 0.24f));
    for (int row = 1; row < 4; ++row) {
        const float output = static_cast<float>(row) / 4.0f;
        const float y = GraphToScreen(rect, 0.0f, output).y;
        drawList->AddLine(ImVec2(rect.Min.x, y), ImVec2(rect.Max.x, y), guideColor);
    }
    const int firstGuideEv = static_cast<int>(std::ceil(graphMinEv / 4.0f)) * 4;
    for (int ev = firstGuideEv; static_cast<float>(ev) <= graphMaxEv; ev += 4) {
        const float u = (static_cast<float>(ev) - graphMinEv) / graphSpan;
        const float x = GraphToScreen(rect, u, 0.0f).x;
        drawList->AddLine(
            ImVec2(x, rect.Min.y),
            ImVec2(x, rect.Max.y),
            ev == 0 ? zeroGuideColor : guideColor);
        char label[16] {};
        std::snprintf(label, sizeof(label), ev > 0 ? "+%d" : "%d", ev);
        const ImVec2 labelSize = ImGui::CalcTextSize(label);
        drawList->AddText(
            ImVec2(x - labelSize.x * 0.5f, rect.Max.y + 3.0f),
            ImGui::GetColorU32(ImGuiCol_TextDisabled),
            label);
    }
    const char* outputLabel = encodeSrgb ? "sRGB" : "Linear";
    const ImVec2 outputLabelSize = ImGui::CalcTextSize(outputLabel);
    drawList->AddText(
        ImVec2(rect.Max.x - outputLabelSize.x, itemRect.Min.y),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        outputLabel);
    drawList->AddText(
        ImVec2(itemRect.Min.x, rect.Min.y - 2.0f),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        "100");
    drawList->AddText(
        ImVec2(itemRect.Min.x + 8.0f, rect.Max.y - ImGui::GetTextLineHeight()),
        ImGui::GetColorU32(ImGuiCol_TextDisabled),
        "0");

    const ImU32 curveColor = ImGui::GetColorU32(ImGuiCol_SliderGrabActive);
    constexpr int sampleCount = 192;
    std::array<ImVec2, sampleCount> curvePoints {};
    std::array<ImVec2, sampleCount> referencePoints {};
    for (int index = 0; index < sampleCount; ++index) {
        const float u = static_cast<float>(index) / static_cast<float>(sampleCount - 1);
        const float sceneEv = graphMinEv + u * graphSpan;
        const float input = middleGrey * std::exp2(sceneEv);
        const auto evaluate = [&](float evaluatedContrast) {
            return EncodeViewGraphOutput(
                (view.value("displayCurve", std::string("standard")) == Raw::HdrDisplay::Photographic
                    ? Raw::HdrDisplay::Evaluate(input,{exposure,blackEv,whiteEv,middleGrey,evaluatedContrast,contrastPivotEv})
                    : Stack::RawRecipe::EvaluateViewTransformDisplayLuma(
                    input,
                    exposure,
                    blackEv,
                    whiteEv,
                    middleGrey,
                    shoulder,
                    toe,
                    evaluatedContrast,
                    contrastPivotEv)),
                encodeSrgb);
        };
        curvePoints[static_cast<std::size_t>(index)] = GraphToScreen(
            rect,
            u,
            evaluate(contrast));
        referencePoints[static_cast<std::size_t>(index)] = GraphToScreen(
            rect,
            u,
            evaluate(1.0f));
    }
    if (std::abs(contrast - 1.0f) > 0.001f) {
        drawList->AddPolyline(
            referencePoints.data(),
            sampleCount,
            ImGui::GetColorU32(ImVec4(disabled.x, disabled.y, disabled.z, 0.30f)),
            ImDrawFlags_None,
            1.0f);
    }
    drawList->AddPolyline(
        curvePoints.data(),
        sampleCount,
        curveColor,
        ImDrawFlags_None,
        2.0f);

    const auto sceneEvToX = [&](float sceneEv) {
        const float u = std::clamp((sceneEv - graphMinEv) / graphSpan, 0.0f, 1.0f);
        return GraphToScreen(rect, u, 0.0f).x;
    };
    const float blackX = sceneEvToX(blackSceneEv);
    const float whiteX = sceneEvToX(whiteSceneEv);
    const float pivotX = sceneEvToX(pivotSceneEv);
    const ImU32 markerColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    drawList->AddLine(ImVec2(blackX, rect.Min.y), ImVec2(blackX, rect.Max.y), markerColor, 1.0f);
    drawList->AddLine(ImVec2(whiteX, rect.Min.y), ImVec2(whiteX, rect.Max.y), markerColor, 1.0f);
    drawList->AddTriangleFilled(
        ImVec2(blackX, rect.Max.y - 7.0f),
        ImVec2(blackX - 5.0f, rect.Max.y),
        ImVec2(blackX + 5.0f, rect.Max.y),
        markerColor);
    drawList->AddTriangleFilled(
        ImVec2(whiteX, rect.Min.y + 7.0f),
        ImVec2(whiteX - 5.0f, rect.Min.y),
        ImVec2(whiteX + 5.0f, rect.Min.y),
        markerColor);
    const float pivotInput = middleGrey * std::exp2(pivotSceneEv);
    const float pivotOutput = EncodeViewGraphOutput(
        (view.value("displayCurve", std::string("standard")) == Raw::HdrDisplay::Photographic
            ? Raw::HdrDisplay::Evaluate(pivotInput,{exposure,blackEv,whiteEv,middleGrey,contrast,contrastPivotEv})
            : Stack::RawRecipe::EvaluateViewTransformDisplayLuma(
            pivotInput,
            exposure,
            blackEv,
            whiteEv,
            middleGrey,
            shoulder,
            toe,
            contrast,
            contrastPivotEv)),
        encodeSrgb);
    const ImVec2 pivotPoint = GraphToScreen(
        rect,
        (pivotSceneEv - graphMinEv) / graphSpan,
        pivotOutput);
    drawList->AddLine(
        ImVec2(pivotX, rect.Min.y),
        ImVec2(pivotX, rect.Max.y),
        ImGui::GetColorU32(ImVec4(
            ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive).x,
            ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive).y,
            ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive).z,
            0.28f)));
    drawList->AddCircleFilled(pivotPoint, 4.0f, curveColor);

    bool changed = false;
    if (ImGui::IsItemHovered() &&
        rect.Contains(ImGui::GetIO().MousePos) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const float mouseX = ImGui::GetIO().MousePos.x;
        float bestDistance = std::abs(mouseX - blackX);
        draggingHandle = 1;
        if (std::abs(mouseX - whiteX) < bestDistance) {
            bestDistance = std::abs(mouseX - whiteX);
            draggingHandle = 2;
        }
        if (std::abs(mouseX - pivotX) < bestDistance) {
            draggingHandle = 3;
        }
    }
    if (draggingHandle != 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float u = std::clamp(
            (ImGui::GetIO().MousePos.x - rect.Min.x) /
                std::max(1.0f, rect.GetWidth()),
            0.0f,
            1.0f);
        const float sceneEv = graphMinEv + u * graphSpan;
        const float displayEv = sceneEv + exposure;
        if (draggingHandle == 1) {
            const float clamped = std::clamp(
                displayEv,
                -16.0f,
                std::min(0.0f, whiteEv - 0.1f));
            if (std::abs(clamped - blackEv) > 0.0001f) {
                blackEv = clamped;
                view["blackEv"] = blackEv;
                changed = true;
            }
        } else if (draggingHandle == 2) {
            const float clamped = std::clamp(
                displayEv,
                std::max(0.0f, blackEv + 0.1f),
                16.0f);
            if (std::abs(clamped - whiteEv) > 0.0001f) {
                whiteEv = clamped;
                view["whiteEv"] = whiteEv;
                changed = true;
            }
        } else {
            const float clamped = std::clamp(
                displayEv,
                std::max(-8.0f, blackEv + 0.1f),
                std::min(8.0f, whiteEv - 0.1f));
            if (std::abs(clamped - contrastPivotEv) > 0.0001f) {
                contrastPivotEv = clamped;
                view["contrastPivotEv"] = contrastPivotEv;
                changed = true;
            }
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        draggingHandle = 0;
    }
    return changed;
}


} // namespace

bool EditorModule::RenderRawWorkspaceLabViewSurface(RawWorkspaceEditContext& context) {
    bool changed = false;
    EnsureViewTransform(context.recipe.viewTransform.layerJson);
    nlohmann::json& view = context.recipe.viewTransform.layerJson;
    bool enabled = Stack::RawRecipe::IsViewTransformEnabled(context.recipe);
    bool controlsEnabled = enabled;
    if (context.multiFrameResult) {
        const Stack::Project::MultiFrameSourceSet* sourceSet =
            m_Project->snapshot
            ? Stack::Project::FindSourceSet(
                  *m_Project->snapshot,
                  context.multiFrameSourceSetId)
            : nullptr;
        bool internalPlacement = sourceSet == nullptr ||
            sourceSet->settings.value(
                "viewTransformPlacement",
                std::string("internal")) != "graph";
        if (sourceSet != nullptr && FramelessTextButton(
                internalPlacement
                    ? "Display Mapping On"
                    : "Display Mapping Off",
                internalPlacement)) {
            std::string placementError;
            if (!SetMultiFrameInternalViewTransformEnabled(
                    sourceSet->sourceSetId,
                    !internalPlacement,
                    &placementError)) {
                m_RawWorkspaceLabUi.multiFrameStatusText =
                    placementError.empty()
                    ? "Could not change View Transform placement."
                    : placementError;
            } else {
                m_RawWorkspaceLabUi.multiFrameStatusText = internalPlacement
                    ? "Scene-linear output is active; View Transform moved to the graph endpoint."
                    : "View Transform moved back into the RAW workflow with its settings preserved.";
            }
            return false;
        }
        ImGui::TextDisabled(
            internalPlacement
                ? "The HDR/denoise merge stays scene-linear. This is the one display transform, applied after the RAW edits."
                : "The merge and RAW edits stay scene-linear. One terminal graph View Transform owns display mapping.");
        controlsEnabled = enabled && internalPlacement;
    } else if (FramelessTextButton(
                   enabled ? "Display Mapping On" : "Display Mapping Off",
                   enabled)) {
        enabled = !enabled;
        view["enabled"] = enabled;
        changed = true;
        controlsEnabled = enabled;
    }
    if (!controlsEnabled) {
        ImGui::TextWrapped(
            "Scene-linear output is active. Add a View Transform node near the graph Output.");
    }
    ImGui::BeginDisabled(!controlsEnabled);
    int curve = view.value("displayCurve", std::string("standard")) == Raw::HdrDisplay::Photographic ? 1 : 0;
    if (ImGui::Combo("Display curve", &curve, "Standard\0Photographic HDR\0")) {
        view["displayCurve"] = curve ? Raw::HdrDisplay::Photographic : "standard";
        changed = true;
    }
    LabTooltip("Photographic HDR keeps middle grey anchored as the highlight range changes. It maps brightness without smoothing or sharpening the image.");
    ImGui::BeginDisabled(!m_RawWorkspaceViewTransformInputStats.valid ||
        m_RawWorkspaceAnalysisPending || m_RawWorkspaceAnalysisRequested);
    if (FramelessTextButton("Fit HDR highlight range", false)) {
        view["displayCurve"] = Raw::HdrDisplay::Photographic;
        view["whiteEv"] = Raw::HdrDisplay::FitWhite(
            m_RawWorkspaceViewTransformInputStats.p999Luma,
            JsonFloat(view,"exposure",0.f),JsonFloat(view,"middleGrey",.18f),
            JsonFloat(view,"contrastPivotEv",0.f));
        changed = true;
    }
    ImGui::EndDisabled();
    LabTooltip("Fits the highlight range to the current image. Your exposure, black point, contrast and color edits stay as set. Small bright lights can still reach display white.");
    const ImVec2 graphSize = CompactLabGraphSize(
        118.0f,
        150.0f,
        230.0f);
    if (DrawViewTransformGraph(
            view,
            graphSize,
            m_ProjectInteractionUi.viewTransformDraggingHandle)) {
        changed = true;
    }
    LabTooltip(
        "Scene EV to display output. Drag the black, pivot, or white marker. The faint curve is the neutral-contrast reference.");
    float contrast = JsonFloat(view, "contrast", 1.0f);
    if (BareSliderFloat(
            "Contrast",
            "RawLabViewContrast",
            &contrast,
            0.25f,
            2.5f,
            "%.2f",
            std::max(180.0f, ImGui::GetContentRegionAvail().x - 110.0f))) {
        view["contrast"] = contrast;
        changed = true;
    }
    float pivotEv = JsonFloat(view, "contrastPivotEv", 0.0f);
    if (BareSliderFloat(
            view.value("displayCurve", std::string("standard")) == Raw::HdrDisplay::Photographic ? "Grey point" : "Pivot",
            "RawLabViewContrastPivot",
            &pivotEv,
            std::max(-8.0f, JsonFloat(view, "blackEv", -8.0f) + 0.1f),
            std::min(8.0f, JsonFloat(view, "whiteEv", 4.0f) - 0.1f),
            "%+.2f EV",
            std::max(180.0f, ImGui::GetContentRegionAvail().x - 110.0f))) {
        view["contrastPivotEv"] = pivotEv;
        changed = true;
    }
    if(view.value("displayCurve", std::string("standard")) == Raw::HdrDisplay::Photographic)
        LabTooltip("Scene brightness mapped to middle grey. Contrast pivots around this point.");
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

bool EditorModule::RenderRawWorkspaceLabViewSecondaryControls(
    RawWorkspaceEditContext& context) {
    bool changed = false;
    EnsureViewTransform(context.recipe.viewTransform.layerJson);
    nlohmann::json& view = context.recipe.viewTransform.layerJson;
    bool enabled = Stack::RawRecipe::IsViewTransformEnabled(context.recipe);
    if (context.multiFrameResult && m_Project->snapshot) {
        const Stack::Project::MultiFrameSourceSet* sourceSet =
            Stack::Project::FindSourceSet(
                *m_Project->snapshot,
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
    if (BareSliderFloat(
            "Display Exposure",
            "RawLabViewExposure",
            &exposure,
            -8.0f,
            8.0f,
            "%+.2f")) {
        view["exposure"] = exposure;
        changed = true;
    }
    float blackEv = JsonFloat(view, "blackEv", -8.0f);
    if (BareSliderFloat(
            "Black EV",
            "RawLabViewBlack",
            &blackEv,
            -16.0f,
            2.0f,
            "%.2f")) {
        view["blackEv"] = blackEv;
        changed = true;
    }
    float whiteEv = JsonFloat(view, "whiteEv", 4.0f);
    if (BareSliderFloat(
            "White EV",
            "RawLabViewWhite",
            &whiteEv,
            -2.0f,
            16.0f,
            "%.2f")) {
        view["whiteEv"] = whiteEv;
        changed = true;
    }
    float middleGrey = JsonFloat(view, "middleGrey", 0.18f);
    if (BareSliderFloat(
            "Middle Grey",
            "RawLabViewGrey",
            &middleGrey,
            0.01f,
            0.50f,
            "%.3f")) {
        view["middleGrey"] = middleGrey;
        changed = true;
    }
    const bool photographic = view.value("displayCurve", std::string("standard")) == Raw::HdrDisplay::Photographic;
    ImGui::BeginDisabled(photographic);
    float toe = JsonFloat(view, "toe", 0.18f);
    if (BareSliderFloat(
            "Toe",
            "RawLabViewToe",
            &toe,
            0.0f,
            1.0f,
            "%.2f")) {
        view["toe"] = toe;
        changed = true;
    }
    float shoulder = JsonFloat(view, "shoulder", 0.45f);
    if (BareSliderFloat(
            "Shoulder",
            "RawLabViewShoulder",
            &shoulder,
            0.001f,
            2.0f,
            "%.2f")) {
        view["shoulder"] = shoulder;
        changed = true;
    }
    ImGui::EndDisabled();
    if (photographic) ImGui::TextDisabled("Photographic HDR uses the black, white and pivot markers for its curve.");
    bool preserveHue = JsonBoolean(view, "preserveHue", true);
    if (ImGui::Checkbox("Preserve Hue", &preserveHue)) {
        view["preserveHue"] = preserveHue;
        changed = true;
    }
    bool falseColor = JsonBoolean(view, "debugFalseColor", false);
    if (ImGui::Checkbox("False Color", &falseColor)) {
        view["debugFalseColor"] = falseColor;
        changed = true;
    }
    bool encodeOutput = JsonBoolean(view, "encodeSrgbOutput", true);
    if (ImGui::Checkbox("Encode sRGB Output", &encodeOutput)) {
        view["encodeSrgbOutput"] = encodeOutput;
        changed = true;
    }
    ImGui::EndDisabled();
    return changed;
}
