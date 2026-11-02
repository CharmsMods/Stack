#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawCalibrationEditor.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Utils/ImGuiExtras.h"
#include "Editor/GraphUI/GraphPlotStyle.h"
#include <imgui_internal.h>
#include <cmath>

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
    const bool panel=context.presentation==Stack::Editor::NodeEditorPresentation::Panel;
    const ImVec2 size(std::max(30.f,available.x), std::max(30.f,panel ? available.x : std::min(available.x*.85f,available.y-44.f)));
    bool changed = DrawCalibrationEditor(context.recipe.colorCalibration,state,size,hueDriven,saturationDriven,panel);
    const auto diagramMaximum=ImGui::GetItemRectMax();
    auto& calibration = context.recipe.colorCalibration;
    const bool editable = calibration.enabled && std::none_of(saturationDriven.begin(),saturationDriven.end(),[](bool v){return v;});
    const auto style = Stack::Editor::GraphUI::ResolveGraphPlotStyle();
    ImGui::TextUnformatted("Saturation"); ImGui::SameLine(0,style.controlGap);
    ImGui::BeginDisabled(!editable);
    ImGui::InvisibleButton("##CalibrationSaturation",{std::min(140.f*style.scale,ImGui::GetContentRegionAvail().x),style.controlHeight});
    if (ImGui::IsItemActivated()) {state.dialGesture=true;state.gestureStart=calibration;}
    const bool cancel = state.dialGesture && (!editable || ImGui::GetIO().AppFocusLost ||
        ImGui::IsKeyPressed(ImGuiKey_Escape,false) || ImGui::IsMouseDown(1));
    if(cancel) {
        calibration.primaries=state.gestureStart.primaries;changed=true;
        state.dialGesture=false;ImGui::ClearActiveID();
    }
    if(editable && state.dialGesture && ImGui::IsItemActive() && ImGui::IsMouseDragging(0,1.f)) {
        const float delta=ImGui::GetIO().MouseDelta.x*(ImGui::GetIO().KeyShift ? .08f : .8f)/style.scale;
        if(delta!=0.f) {
            for(auto& primary:calibration.primaries) primary.saturation+=delta;
            state.dialPhase=std::fmod(state.dialPhase+delta/100.f,100.f);
            changed=true;
        }
    }
    if(editable && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        for(auto& primary:calibration.primaries) {changed|=primary.saturation!=0.f;primary.saturation=0.f;}
        state.dialPhase=0.f;
    }
    if(!ImGui::IsMouseDown(0))state.dialGesture=false;
    Stack::Editor::GraphUI::DrawGraphDial(*ImGui::GetWindowDrawList(),ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),
        state.dialPhase,editable,style);
    ImGui::EndDisabled();
    const auto& recipe=context.recipe.colorCalibration;
    bool transformChanged=!state.transformChecked || state.checkedSpace!=context.recipe.technical.workingSpace ||
        state.checkedRecipe.enabled!=recipe.enabled;
    for(int i=0;i<3;++i) transformChanged|=state.checkedRecipe.primaries[i].hue!=recipe.primaries[i].hue ||
        state.checkedRecipe.primaries[i].saturation!=recipe.primaries[i].saturation;
    if(transformChanged) {
        state.checkedRecipe=recipe;state.checkedSpace=context.recipe.technical.workingSpace;
        state.transformChecked=true;state.transformError.clear();
        try { Stack::RawRecipe::BuildColorCalibrationTransform(recipe,state.checkedSpace); }
        catch(const std::exception& error) {state.transformError=error.what();}
    }
    if(!state.transformError.empty()) {
        // Keep the authored shape. The renderer reports its unavailable
        // result; the editor supplies a small, local explanation.
        const auto maximum = diagramMaximum;
        const ImVec2 marker(maximum.x-14.f,maximum.y-20.f);
        ImGui::GetWindowDrawList()->AddText(marker,IM_COL32(255,175,90,255),"!");
        if (ImGui::IsMouseHoveringRect(marker,maximum)) ImGui::SetTooltip("%s",state.transformError.c_str());
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
