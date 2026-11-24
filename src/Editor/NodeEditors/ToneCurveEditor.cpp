#include "Editor/GraphUI/GraphPlotStyle.h"
#include "Editor/NodeEditors/ToneCurveEditor.h"
#include "Editor/NodeEditors/ToneCurveEditorState.h"
#include "Editor/NodeEditors/PhotographicPanelEditors.h"
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

void DrawPhotoChannels(ToneCurveEditorState& state,bool rgbOnly) {
    const auto style = GraphUI::ResolveGraphPlotStyle();
    const float side = ImGui::GetFrameHeight();
    const char* names[] = {"Luminance","Red","Green","Blue"};
    const int first=rgbOnly?1:0;
    const float available = ImGui::GetContentRegionAvail().x;
    const float gap = rgbOnly ? std::min(style.controlGap, std::max(0.f, available - 3.f) / 2.f) : style.controlGap;
    const float width = rgbOnly ? std::clamp((available - 2.f * gap) / 3.f, 1.f, side) : side;
    if (rgbOnly) {
        const float rowWidth = 3.f * width + 2.f * gap;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, (available - rowWidth) * .5f));
    }
    for (int i = first; i < 4; ++i) {
        if (i!=first) ImGui::SameLine(0, gap);
        ImGui::PushID("PhotoChannel");
        if (ImGui::InvisibleButton(names[i], {width, side}, ImGuiButtonFlags_EnableNav) && state.photoChannel != i) {
            state.EndInteraction();
            state.photoChannel = i;
            if(i>0)state.panelRgbChannel=i;
        }
        const auto pos = ImGui::GetItemRectMin();
        const ImVec2 center{pos.x + width * .5f, pos.y + side * .5f};
        const float dotSize = std::min(width, side);
        auto* draw = ImGui::GetWindowDrawList();
        const auto color = i ? style.channel[i-1] : IM_COL32_WHITE;
        draw->AddCircleFilled(center, dotSize * .20f, color);
        if (state.photoChannel == i || ImGui::IsItemHovered())
            draw->AddCircle(center, dotSize * .34f, style.curve, 0, state.photoChannel == i ? style.curveWidth : 1.f);
        if (ImGui::IsItemHovered() || ImGui::IsItemFocused()) ImGui::SetTooltip("%s curve", names[i]);
        ImGui::PopID();
    }
}

bool Draw(EditorModule& editor,RawWorkspaceEditContext& context,bool settings) {
    auto& parameters=context.recipe.finishTone.layerJson;
    auto& state=context.State<ToneCurveEditorState>();
    const auto input=NodeMath::DescribeImageOperationInput(context.inputDescriptor);
    bool changed=false;
    const bool panel=context.presentation==NodeEditorPresentation::Panel;
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
    if (!settings && (!panel || state.photoChannel!=0)) DrawPhotoChannels(state,panel);
    // Input analysis was prepared before the channel button. Wait for its
    // matching domain on the next frame before displaying or fitting it.
    if(previousChannel!=state.photoChannel)context.inputHistogram.valid=false;
    if (!settings && state.photoChannel == 0) {
        auto tone=RawRecipe::ReadSceneTone(parameters.at("luminanceTone"));tone.direct=true;
        ImVec2 size=ImGui::GetContentRegionAvail();size.y=std::min(size.y,250.f);
        const auto tonalDomain=ExposureTonalSliderDomain(-16.f,16.f);
        const bool toneChanged=DrawCurveCanvas("Luminance",tone.curve,tone.curveAmount,state.canvas[4],true,context.inputHistogram,size,64,nullptr,{},context.IsParameterDriven("luminance-amount"),
            nullptr,false,true,panel?nullptr:&tonalDomain,context.inputHistogramRevision,{true,false,parameters.value("domain",0)==1,-16,16,parameters.value("domain",0)==0});
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
        if(panel && activeChannel!=0) {
            ImGui::TextUnformatted("Curve values");
            if(ImGui::RadioButton("Linear",domain==0)) {parameters["domain"]=0;domain=0;changed=true;}
            ImGui::SameLine();
            if(ImGui::RadioButton("Exposure stops",domain==1)) {parameters["domain"]=1;domain=1;changed=true;}
        } else if (!panel && ImGui::Combo("Curve values",&domain,"Linear values\0Exposure stops (positive values)\0")) {parameters["domain"]=domain;changed=true;}
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Changes how channel curves affect the image. The graph initially fits the image's tonal range; zooming only changes the view.");
        if (domain==1 && (!panel || activeChannel!=0)) {
            float low=parameters.at("logMinEv"),high=parameters.at("logMaxEv"),grey=parameters.at("middleGrey");
            if (ImGui::InputFloat("Minimum EV",&low) && low<high) {parameters["logMinEv"]=low;changed=true;}
            if (ImGui::InputFloat("Maximum EV",&high) && high>low) {parameters["logMaxEv"]=high;changed=true;}
            if (ImGui::InputFloat("Middle grey",&grey) && grey>0) {parameters["middleGrey"]=grey;changed=true;}
        }
        if(panel) {
            auto& canvas=state.canvas[activeChannel==0?4:activeChannel];
            ImGui::Checkbox("Tonal controls",&canvas.showTonalSliders);
            if(canvas.showTonalSliders && activeChannel==0) {
                auto tone=RawRecipe::ReadSceneTone(parameters.at("luminanceTone"));
                if(DrawPanelTonalRollers(context,tone.curve,ExposureTonalSliderDomain(-16,16),64,tone.curveAmount.locked)) {
                    const auto serialized=RawRecipe::SerializeSceneTone(tone);
                    for(const auto& field:serialized.items())parameters["luminanceTone"][field.key()]=field.value();
                    changed=true;
                }
            } else if(canvas.showTonalSliders) {
                const auto channel=static_cast<RawRecipe::RawPointCurveChannel>(activeChannel);
                const auto key=RawRecipe::RawPointCurveChannelKey(channel);
                auto component=RawRecipe::PointCurveComponentFromFinishToneJson(parameters,channel);
                auto points=RawRecipe::RawPointCurveBezierPoints(component);
                const auto amount=RawRecipe::ReadCurveAmount(parameters.value("curveAmounts",nlohmann::json::object()).value(key,nlohmann::json::object()));
                const auto tonalDomain=domain==1 ? ExposureTonalSliderDomain(parameters.value("logMinEv",-10.f),parameters.value("logMaxEv",6.f)) : CurveTonalSliderDomain{};
                if(DrawPanelTonalRollers(context,points,tonalDomain,64,amount.locked)) {
                    component.points.clear();
                    for(const auto& point:points)component.points.push_back({point.x,point.y,1,point.incoming,point.outgoing,point.tonal});
                    RawRecipe::StorePointCurveComponentInFinishToneJson(parameters,channel,component);changed=true;
                }
            }
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
            nullptr,false,true,panel?nullptr:&tonalDomain,context.inputHistogramRevision,viewOptions)) {
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
bool ResetPanelToneCurves(RawWorkspaceEditContext& context,bool rgb) {
    auto& parameters=context.recipe.finishTone.layerJson;
    const auto defaults=NodeMath::DefaultToneCurveParameters();
    if(!rgb)parameters["luminanceTone"]=defaults.at("luminanceTone");
    else for(int i=1;i<=3;++i) {
        const auto channel=static_cast<RawRecipe::RawPointCurveChannel>(i);
        const auto component=RawRecipe::PointCurveComponentFromFinishToneJson(defaults,channel);
        RawRecipe::StorePointCurveComponentInFinishToneJson(parameters,channel,component);
        const auto key=RawRecipe::RawPointCurveChannelKey(channel);
        parameters["curveAmounts"][key]=defaults.at("curveAmounts").at(key);
    }
    context.State<ToneCurveEditorState>().EndInteraction();
    return true;
}
bool ResetToneCurve(EditorModule&,RawWorkspaceEditContext& context) {
    context.recipe.finishTone.layerJson=NodeMath::DefaultToneCurveParameters();
    auto& state=context.State<ToneCurveEditorState>();
    state.EndInteraction();
    state.photoChannel=0;
    return true;
}
}
