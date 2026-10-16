#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawCalibrationEditor.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include <algorithm>
#include <exception>

using namespace Stack::Editor::RawLabInternal;

bool EditorModule::RenderRawWorkspaceLabCalibrationSurface(RawWorkspaceEditContext& context) {
    auto& state = context.nodeTarget ? context.State<CalibrationEditorState>() : m_RawWorkspaceLabUi.calibrationEditor;
    const auto source = GetActiveRawWorkspacePreviewIdentity() + ":" +
        context.rawAdjustmentLayerId + ":" + context.rawOperationUuid;
    if (state.source != source) { state = {}; state.source = source; }
    std::array<bool,3> hueDriven{}, saturationDriven{};
    for (int i=0;i<3;++i) {
        hueDriven[i] = context.IsParameterDriven("hue-" + std::to_string(i));
        saturationDriven[i] = context.IsParameterDriven("saturation-" + std::to_string(i));
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 size(std::max(30.f,available.x), std::max(30.f,std::min(available.x*.85f,available.y)));
    const bool changed = DrawCalibrationEditor(context.recipe.colorCalibration,state,size,hueDriven,saturationDriven);
    try {
        Stack::RawRecipe::BuildColorCalibrationTransform(context.recipe.colorCalibration,context.recipe.technical.workingSpace);
    } catch (const std::exception& error) {
        // Keep the authored shape. The renderer reports its unavailable
        // result; the editor supplies a small, local explanation.
        const auto maximum = ImGui::GetItemRectMax();
        const ImVec2 marker(maximum.x-14.f,maximum.y-20.f);
        ImGui::GetWindowDrawList()->AddText(marker,IM_COL32(255,175,90,255),"!");
        if (ImGui::IsMouseHoveringRect(marker,maximum)) ImGui::SetTooltip("%s",error.what());
    }
    return changed;
}

bool EditorModule::RenderRawWorkspaceLabCalibrationSettings(RawWorkspaceEditContext& context) {
    auto& recipe = context.recipe.colorCalibration;
    bool changed = false;
    if (BypassEyeButton("calibration-bypass",recipe.enabled,"Bypass Calibration","Enable Calibration")) {
        recipe.enabled = !recipe.enabled;
        changed = true;
    }
    ImGui::SameLine();
    if (BareTextButton("Reset")) {
        for (int i=0;i<3;++i) {
            if (!context.IsParameterDriven("hue-"+std::to_string(i))) recipe.primaries[i].hue = 0;
            if (!context.IsParameterDriven("saturation-"+std::to_string(i))) recipe.primaries[i].saturation = 0;
        }
        auto& state = context.nodeTarget ? context.State<CalibrationEditorState>() : m_RawWorkspaceLabUi.calibrationEditor;
        state.radius = .65;
        changed = true;
    }
    if (ImGui::CollapsingHeader("Values")) {
        const char* labels[] = {"Red","Green","Blue"};
        for (int i=0;i<3;++i) {
            ImGui::PushID(i);
            ImGui::TextUnformatted(labels[i]);
            ImGui::BeginDisabled(!recipe.enabled || context.IsParameterDriven("hue-"+std::to_string(i)));
            changed |= ImGui::InputFloat("Hue",&recipe.primaries[i].hue,0,0,"%.3f");
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!recipe.enabled || context.IsParameterDriven("saturation-"+std::to_string(i)));
            changed |= ImGui::InputFloat("Saturation",&recipe.primaries[i].saturation,0,0,"%.3f");
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }
    return changed;
}
