#include "Editor/GraphUI/GraphPlotStyle.h"
#include "Editor/NodeEditors/ToneCurveEditor.h"
#include "Editor/NodeEditors/ToneCurveEditorState.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeEditors/RawWorkspaceEditContext.h"
#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/Internal/RawLab/CurveTonalSliders.h"
#include "Editor/Internal/RawLab/SceneToneEditor.h"
#include "Editor/Layers/ToneLayers.h"
#include "NodeMath/ToneCurveParameters.h"
#include "NodeMath/ImageOperationInput.h"
#include "Raw/Curves/CurveAuthoring.h"

namespace Stack::Editor {
namespace {
using namespace RawLabInternal;

void DrawPhotoChannels(ToneCurveEditorState& state) {
    const auto style = GraphUI::ResolveGraphPlotStyle();
    const float side = ImGui::GetFrameHeight();
    const char* names[] = {"Luminance","Red","Green","Blue"};
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine(0, style.controlGap);
        ImGui::PushID("PhotoChannel");
        if (ImGui::InvisibleButton(names[i], {side, side}, ImGuiButtonFlags_EnableNav) && state.photoChannel != i) {
            state.EndInteraction();
            state.photoChannel = i;
        }
        const auto pos = ImGui::GetItemRectMin();
        const ImVec2 center{pos.x + side * .5f, pos.y + side * .5f};
        auto* draw = ImGui::GetWindowDrawList();
        const auto color = i ? style.channel[i-1] : IM_COL32_WHITE;
        draw->AddCircleFilled(center, side * .20f, color);
        if (state.photoChannel == i || ImGui::IsItemHovered())
            draw->AddCircle(center, side * .34f, style.curve, 0, state.photoChannel == i ? style.curveWidth : 1.f);
        if (ImGui::IsItemHovered() || ImGui::IsItemFocused()) ImGui::SetTooltip("%s curve", names[i]);
        ImGui::PopID();
    }
}

bool Draw(EditorModule& editor,RawWorkspaceEditContext& context,bool settings) {
    auto& parameters=context.recipe.finishTone.layerJson;
    auto& state=context.State<ToneCurveEditorState>();
    const auto input=NodeMath::DescribeImageOperationInput(context.inputDescriptor);
    bool changed=false;
    for (const auto& id:context.drivenParameters) {
        if (!NodeMath::IsToneCurveParameterActive(parameters,id)) continue;
        const auto feedback=context.uniformParameters.find(id);
        if (feedback!=context.uniformParameters.end() && feedback->second.available)
            ImGui::TextDisabled("%s driven: %.5g",id.c_str(),feedback->second.value);
        else ImGui::TextWrapped("%s",feedback==context.uniformParameters.end()
            ? "Waiting for the connected curve parameter." : feedback->second.reason.c_str());
    }
    if (!input.colorRelationships) {
        ImGui::TextWrapped("%s", input.colorUnavailableReason.c_str());
        return changed;
    }
    const int previousChannel=state.photoChannel;
    if (!settings) DrawPhotoChannels(state);
    // Input analysis was prepared before the channel button. Wait for its
    // matching domain on the next frame before displaying or fitting it.
    if(previousChannel!=state.photoChannel)context.inputHistogram.valid=false;
    if (!settings && state.photoChannel == 0) {
        auto tone=RawRecipe::ReadSceneTone(parameters.at("luminanceTone"));tone.direct=true;
        ImVec2 size=ImGui::GetContentRegionAvail();size.y=std::min(size.y,250.f);
        const auto tonalDomain=ExposureTonalSliderDomain(-16.f,16.f);
        const bool toneChanged=DrawCurveCanvas("Luminance",tone.curve,tone.curveAmount,state.canvas[4],true,context.inputHistogram,size,64,nullptr,{},context.IsParameterDriven("luminance-amount"),
            nullptr,false,true,&tonalDomain,context.inputHistogramRevision,{true,false,parameters.value("domain",0)==1,-16,16,parameters.value("domain",0)==0});
        if (toneChanged) {
            const auto serialized=RawRecipe::SerializeSceneTone(tone);
            for (const auto& field:serialized.items())
                parameters["luminanceTone"][field.key()]=field.value();
            changed=true;
        }
        return changed;
    }
    int& activeChannel = state.photoChannel;
    if (settings) {
        int domain=parameters.value("domain",0);
        if (ImGui::Combo("Curve values",&domain,"Linear values\0Exposure stops (positive values)\0")) {parameters["domain"]=domain;changed=true;}
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Changes how channel curves affect the image. The graph initially fits the image's tonal range; zooming only changes the view.");
        if (domain==1) {
            float low=parameters.at("logMinEv"),high=parameters.at("logMaxEv"),grey=parameters.at("middleGrey");
            if (ImGui::InputFloat("Minimum EV",&low) && low<high) {parameters["logMinEv"]=low;changed=true;}
            if (ImGui::InputFloat("Maximum EV",&high) && high>low) {parameters["logMaxEv"]=high;changed=true;}
            if (ImGui::InputFloat("Middle grey",&grey) && grey>0) {parameters["middleGrey"]=grey;changed=true;}
        }
        return changed;
    }
    if (activeChannel && !NodeMath::HasImageComponent(input.components,
        static_cast<NodeMath::ImageComponent>(1u<<(activeChannel-1)))) activeChannel=0;
    const auto channel=static_cast<RawRecipe::RawPointCurveChannel>(activeChannel);
    const auto key=RawRecipe::RawPointCurveChannelKey(channel);
    auto component=RawRecipe::PointCurveComponentFromFinishToneJson(parameters,channel);
    auto points=RawRecipe::RawPointCurveBezierPoints(component);
    auto amount=RawRecipe::ReadCurveAmount(parameters.value("curveAmounts",nlohmann::json::object()).value(key,nlohmann::json::object()));
    if (!settings) {
        ImVec2 size=ImGui::GetContentRegionAvail();size.y=std::min(size.y,250.f);
        auto histogram=context.inputHistogram;
        if (activeChannel==1) histogram.luma=histogram.red;
        else if (activeChannel==2) histogram.luma=histogram.green;
        else if (activeChannel==3) histogram.luma=histogram.blue;
        const bool exposureDomain=parameters.value("domain",0)==1;
        const auto tonalDomain=exposureDomain
            ? ExposureTonalSliderDomain(parameters.value("logMinEv",-10.f),parameters.value("logMaxEv",6.f))
            : CurveTonalSliderDomain{};
        const CurveCanvasViewOptions viewOptions{true,!exposureDomain,exposureDomain,
            exposureDomain?parameters.value("logMinEv",-10.f):0.f,
            exposureDomain?parameters.value("logMaxEv",6.f):1.f};
        if (DrawCurveCanvas("Curve",points,amount,state.canvas[activeChannel],true,histogram,size,64,nullptr,{},context.IsParameterDriven(std::string("amount-")+key),
            nullptr,false,true,&tonalDomain,context.inputHistogramRevision,viewOptions)) {
            component.points.clear();
            for (const auto& point:points) component.points.push_back({point.x,point.y,1,point.incoming,point.outgoing,point.tonal});
            RawRecipe::StorePointCurveComponentInFinishToneJson(parameters,channel,component);
            parameters["curveAmounts"][key]=RawRecipe::CurveAmountJson(amount);changed=true;
        }
    }
    return changed;
}
}
bool DrawToneCurve(EditorModule& editor,RawWorkspaceEditContext& context) {return Draw(editor,context,false);}
bool DrawToneCurveSettings(EditorModule& editor,RawWorkspaceEditContext& context) {return Draw(editor,context,true);}
bool ResetToneCurve(EditorModule&,RawWorkspaceEditContext& context) {
    context.recipe.finishTone.layerJson=NodeMath::DefaultToneCurveParameters();
    auto& state=context.State<ToneCurveEditorState>();
    state.EndInteraction();
    state.photoChannel=0;
    return true;
}
}
