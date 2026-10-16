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

bool Draw(EditorModule& editor,RawWorkspaceEditContext& context,bool settings) {
    auto& parameters=context.recipe.finishTone.layerJson;
    auto& state=context.State<ToneCurveEditorState>();
    const auto input=NodeMath::DescribeImageOperationInput(context.inputDescriptor);
    auto mode=parameters.value("curveMode",std::string("components"));
    bool changed=false;
    for (const auto& id:context.drivenParameters) {
        if (!NodeMath::IsToneCurveParameterActive(parameters,id)) continue;
        const auto feedback=context.uniformParameters.find(id);
        if (feedback!=context.uniformParameters.end() && feedback->second.available)
            ImGui::TextDisabled("%s driven: %.5g",id.c_str(),feedback->second.value);
        else ImGui::TextWrapped("%s",feedback==context.uniformParameters.end()
            ? "Waiting for the connected curve parameter." : feedback->second.reason.c_str());
    }
    if (settings) {
        for (const char* choice:{"components","luminance","contrast","advanced"}) {
            if (choice!=std::string("components")) ImGui::SameLine();
            const bool available=choice==std::string("components") || input.colorRelationships;
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(choice,mode==choice,ImGuiSelectableFlags_None,{90,0})) {
                parameters["curveMode"]=choice;mode=choice;changed=true;
            }
            ImGui::EndDisabled();
        }
        if (!input.colorRelationships) ImGui::TextWrapped("%s",input.colorUnavailableReason.c_str());
    }
    if (mode!="components" && !input.colorRelationships) {
        if (!settings) ImGui::TextWrapped("%s Choose Components explicitly to remap this input.",input.colorUnavailableReason.c_str());
        return changed;
    }
    if (mode=="advanced") {
        if (!state.advanced) state.advanced=std::make_shared<ToneCurveLayer>();
        if (state.appliedAdvanced!=parameters.at("advanced")) {
            ToneCurveLayer defaults;
            state.advanced->Deserialize(defaults.Serialize());
            state.advanced->Deserialize(parameters.at("advanced"));
            state.appliedAdvanced=parameters.at("advanced");
        }
        const auto before=state.advanced->Serialize();
        NodeSurfaceContext surface;
        surface.nodeId=context.presentationNodeId;
        surface.availableWidth=surface.safeContentWidth=ImGui::GetContentRegionAvail().x;
        surface.logicalAvailableWidth=surface.logicalSafeContentWidth=surface.safeContentWidth;
        surface.focused=true;
        state.advanced->RenderExpandedNodeSurface(&editor,surface);
        const auto after=state.advanced->Serialize();
        if (before!=after) {parameters["advanced"]=after;state.appliedAdvanced=after;changed=true;}
        return changed;
    }
    if (mode=="contrast") {
        auto tone=RawRecipe::ReadSceneTone(parameters.at("luminanceTone"));tone.direct=false;
        if (DrawSceneToneEditor(tone,true,state.selected,state.dragging,state.minimum,state.maximum,context.inputHistogram,
            [&](const auto& id){return context.IsParameterDriven(id);},settings ? RawLabControlSection::Settings : RawLabControlSection::Graph)) {
            const auto serialized=RawRecipe::SerializeSceneTone(tone);
            for (const auto& field:serialized.items())
                parameters["luminanceTone"][field.key()]=field.value();
            changed=true;
        }
        return changed;
    }
    if (mode=="luminance") {
        auto tone=RawRecipe::ReadSceneTone(parameters.at("luminanceTone"));tone.direct=true;
        if (!settings) {
            ImVec2 size=ImGui::GetContentRegionAvail();size.y=std::min(size.y,250.f);
            const auto tonalDomain=ExposureTonalSliderDomain(-16.f,16.f);
            if (DrawCurveCanvas("Luminance",tone.curve,tone.curveAmount,state.canvas[4],true,context.inputHistogram,size,64,nullptr,{},context.IsParameterDriven("luminance-amount"),
                nullptr,false,true,&tonalDomain,context.inputHistogramRevision)) {
                const auto serialized=RawRecipe::SerializeSceneTone(tone);
            for (const auto& field:serialized.items())
                parameters["luminanceTone"][field.key()]=field.value();
            changed=true;
            }
        }
        return changed;
    }
    if (settings) {
        int domain=parameters.value("domain",0);
        if (ImGui::Combo("Domain",&domain,"Linear\0Positive log\0")) {parameters["domain"]=domain;changed=true;}
        if (domain==1) {
            float low=parameters.at("logMinEv"),high=parameters.at("logMaxEv"),grey=parameters.at("middleGrey");
            if (ImGui::InputFloat("Minimum EV",&low) && low<high) {parameters["logMinEv"]=low;changed=true;}
            if (ImGui::InputFloat("Maximum EV",&high) && high>low) {parameters["logMaxEv"]=high;changed=true;}
            if (ImGui::InputFloat("Middle grey",&grey) && grey>0) {parameters["middleGrey"]=grey;changed=true;}
        }
        const char* labels[]{"Value","Red","Green","Blue"};
        for (int component=0;component<4;++component) {
            if (component && !NodeMath::HasImageComponent(input.components,
                static_cast<NodeMath::ImageComponent>(1u<<(component-1)))) continue;
            if (component) ImGui::SameLine();
            if (ImGui::Selectable(labels[component],state.component==component,ImGuiSelectableFlags_None,{70,0}))
                state.component=component;
        }
    }
    if (state.component && !NodeMath::HasImageComponent(input.components,
        static_cast<NodeMath::ImageComponent>(1u<<(state.component-1)))) state.component=0;
    const auto channel=static_cast<RawRecipe::RawPointCurveChannel>(state.component);
    const auto key=RawRecipe::RawPointCurveChannelKey(channel);
    auto component=RawRecipe::PointCurveComponentFromFinishToneJson(parameters,channel);
    auto points=RawRecipe::RawPointCurveBezierPoints(component);
    auto amount=RawRecipe::ReadCurveAmount(parameters.value("curveAmounts",nlohmann::json::object()).value(key,nlohmann::json::object()));
    if (!settings) {
        ImVec2 size=ImGui::GetContentRegionAvail();size.y=std::min(size.y,250.f);
        auto histogram=context.inputHistogram;
        if (state.component==1) histogram.luma=histogram.red;
        else if (state.component==2) histogram.luma=histogram.green;
        else if (state.component==3) histogram.luma=histogram.blue;
        if (histogram.valid && (!state.fittedValid[state.component] ||
            !context.inputHistogramRevision || state.fittedRevision[state.component]!=context.inputHistogramRevision)) {
            state.fitted[state.component]=BuildRawLabToneGraphViewRange(histogram,0,1);
            state.fittedRevision[state.component]=context.inputHistogramRevision;
            state.fittedValid[state.component]=true;
        } else if (!histogram.valid) {
            state.fitted[state.component]={};state.fittedValid[state.component]=false;
        }
        DrawRawLabRingDial("Input fit",state.zoom,100,"Fit the curve display to this node's input values. Authored points do not change.",1);
        const auto view=ApplyRawLabToneGraphViewZoom(state.fitted[state.component],state.zoom);
        const auto tonalDomain=parameters.value("domain",0)==1
            ? ExposureTonalSliderDomain(parameters.value("logMinEv",-10.f),parameters.value("logMaxEv",6.f))
            : CurveTonalSliderDomain{};
        if (DrawCurveCanvas("Curve",points,amount,state.canvas[state.component],true,histogram,size,64,nullptr,view,context.IsParameterDriven(std::string("amount-")+key),
            nullptr,false,true,&tonalDomain,context.inputHistogramRevision)) {
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
    context.recipe.finishTone.layerJson=NodeMath::DefaultToneCurveParameters();return true;
}
}
