#include "Raw/ViewTransform.h"
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
    // Viewing an existing node must not opt it into a new output adjustment.
    if(!viewTransform.contains("sdrHighlightDetail")) viewTransform["sdrHighlightDetail"]=0.f;
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
    const float middleGrey = JsonFloat(view, "middleGrey", 0.18f);
    const float contrast = JsonFloat(view, "contrast", 1.0f);
    float contrastPivotEv = JsonFloat(view, "contrastPivotEv", 0.0f);
    const bool encodeSrgb = JsonBoolean(view, "encodeSrgbOutput", true);
    const float pivotSceneEv = contrastPivotEv - exposure;
    const float graphMinEv = std::min(
        -12.0f,
        pivotSceneEv - 2.0f);
    const float graphMaxEv = std::max(
        8.0f,
        pivotSceneEv + 2.0f);
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
    // Bounded value-keyed cache: unchanged drawing only places prepared samples.
    // Geometry, hover and dragging do not repeat curve preparation/evaluation.
    struct Samples {
        bool valid = false;
        std::array<float, 11> key {};
        std::array<float, sampleCount> actual {}, reference {};
        float pivot = 0;
    };
    static thread_local Samples samples;
    const std::array<float, 11> key {exposure,middleGrey,contrast,contrastPivotEv,
        float(encodeSrgb),graphMinEv,graphMaxEv,graphSpan,
        JsonFloat(view,"sdrBrightness",0.f),JsonFloat(view,"sdrHighlightDetail",0.f),JsonFloat(view,"sdrSaturation",1.f)};
    if (!samples.valid || samples.key != key) {
        Raw::ViewTransform::Parameters p;
        p.exposure=exposure;p.middleGrey=middleGrey;
        p.contrast=contrast;p.contrastPivotEv=contrastPivotEv;
        p.sdr={key[8],key[9],key[10]};
        const Raw::ViewTransform::PreparedTransform actual(p);
        p.contrast=1.f;
        const Raw::ViewTransform::PreparedTransform reference(p);
        for (int index=0;index<sampleCount;++index) {
            const float input=middleGrey*std::exp2(graphMinEv+graphSpan*index/(sampleCount-1));
            samples.actual[index]=EncodeViewGraphOutput(actual.Luma(input),encodeSrgb);
            samples.reference[index]=EncodeViewGraphOutput(reference.Luma(input),encodeSrgb);
        }
        samples.pivot=EncodeViewGraphOutput(actual.Luma(middleGrey*std::exp2(pivotSceneEv)),encodeSrgb);
        samples.key=key;samples.valid=true;
    }
    for (int index = 0; index < sampleCount; ++index) {
        const float u = static_cast<float>(index) / static_cast<float>(sampleCount - 1);
        curvePoints[static_cast<std::size_t>(index)] = GraphToScreen(
            rect,
            u,
            samples.actual[index]);
        referencePoints[static_cast<std::size_t>(index)] = GraphToScreen(
            rect,
            u,
            samples.reference[index]);
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
    const float pivotX = sceneEvToX(pivotSceneEv);
    const float pivotOutput = samples.pivot;
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
    const bool enabled = !(ImGui::GetCurrentContext()->LastItemData.ItemFlags & ImGuiItemFlags_Disabled);
    if (ImGui::IsItemActive() && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::ClearActiveID();
    // A hidden release or a fresh press in the outer margin cannot resume an old handle.
    if (!enabled || !ImGui::IsItemActive() || ImGui::IsItemActivated()) draggingHandle = 0;
    if (draggingHandle != 3) draggingHandle = 0;
    if (enabled && ImGui::IsItemActive() && ImGui::IsItemHovered() &&
        rect.Contains(ImGui::GetIO().MousePos) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        draggingHandle = 3;
    }
    if (draggingHandle != 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float u = std::clamp(
            (ImGui::GetIO().MousePos.x - rect.Min.x) /
                std::max(1.0f, rect.GetWidth()),
            0.0f,
            1.0f);
        const float sceneEv = graphMinEv + u * graphSpan;
        const float displayEv = sceneEv + exposure;
        const float clamped=std::clamp(displayEv,-8.f,8.f);
        if (std::abs(clamped-contrastPivotEv)>0.0001f) {
            view["contrastPivotEv"]=clamped;
            changed=true;
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
    if (FramelessTextButton(
                   enabled ? "Display Mapping On" : "Display Mapping Off",
                   enabled)) {
        enabled = !enabled;
        view["enabled"] = enabled;
        changed = true;
    }
    if (!enabled) {
        ImGui::TextWrapped(
            "Scene-linear output is active.");
    }
    ImGui::BeginDisabled(!enabled);
    ImGui::TextDisabled("Sigmoid display mapping");
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
    LabTooltip("Scene EV to display output. Drag the middle-grey pivot. Highlights approach white gradually.");
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
            "Pivot",
            "RawLabViewContrastPivot",
            &pivotEv,
            -8.f,
            8.f,
            "%+.2f EV",
            std::max(180.0f, ImGui::GetContentRegionAvail().x - 110.0f))) {
        view["contrastPivotEv"] = pivotEv;
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
    ImGui::Separator();
    bool proof=IsSdrOutputProof();
    if(ImGui::Checkbox("Preview SDR",&proof)) SetSdrOutputProof(proof);
    LabTooltip("Show the SDR export on this display. Your HDR edit is unchanged.");
    ImGui::BeginDisabled(!enabled);
    const float width=std::max(180.f,ImGui::GetContentRegionAvail().x-110.f);
    if(FramelessTextButton("Reset SDR appearance")) {
        view["sdrBrightness"]=0.f;view["sdrHighlightDetail"]=1.f;view["sdrSaturation"]=1.f;
        SetSdrOutputProof(true);changed=true;
    }
    float brightness=JsonFloat(view,"sdrBrightness",0.f);
    if(BareSliderFloat("SDR brightness","SdrBrightness",&brightness,-3.f,3.f,"%+.2f EV",width)) {
        view["sdrBrightness"]=brightness;changed=true;
    }
    float detail=JsonFloat(view,"sdrHighlightDetail",0.f);
    if(BareSliderFloat("Highlight separation","SdrDetail",&detail,0.f,1.f,"%.2f",width)) {
        view["sdrHighlightDetail"]=detail;changed=true;
    }
    float color=JsonFloat(view,"sdrSaturation",1.f);
    if(BareSliderFloat("SDR color","SdrColor",&color,0.f,2.f,"%.2f",width)) {
        view["sdrSaturation"]=color;changed=true;
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
