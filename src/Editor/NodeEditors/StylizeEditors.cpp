#include "Editor/NodeEditors/StylizeEditors.h"
#include "Editor/NodeEditors/RawWorkspaceEditContext.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Graph/LayerOperationParameters.h"
#include "NodeMath/StylizeInput.h"
#include "Utils/FloatingSlider.h"
#include "Utils/ImGuiExtras.h"
#include <functional>

namespace Stack::Editor {
namespace {
bool Number(RawWorkspaceEditContext& context,const char* key,const char* label,
    float low,float high,const char* format="%.2f") {
    const auto* definition=EditorNodeGraphDefinitions::FindLiveNodeDefinition(*context.nodeDraft);
    for (const auto& parameter:definition->parameters) if (parameter.storageKey==key && context.IsParameterDriven(parameter.id)) {
        const auto feedback=context.uniformParameters.find(parameter.id);
        ImGui::TextUnformatted(label);
        if (feedback!=context.uniformParameters.end() && feedback->second.available)
            ImGui::TextDisabled("Driven: %.4g",feedback->second.value);
        else ImGui::TextWrapped("%s",feedback==context.uniformParameters.end()
            ? "Waiting for the connected value." : feedback->second.reason.c_str());
        return false;
    }
    float value=context.nodeSettings.at(key).get<float>();
    ImGui::PushID(key);
    const bool changed=ImGuiExtras::FloatingSliderFloat(label,"##value",&value,low,high,format,ImGui::GetContentRegionAvail().x);
    ImGui::PopID();
    if (changed) context.nodeSettings[key]=value;
    return changed;
}
bool Choice(RawWorkspaceEditContext& context,const char* key,const char* label,
    std::initializer_list<const char*> choices,const std::function<bool(int)>& available={}) {
    ImGui::TextUnformatted(label);
    int current=context.nodeSettings.at(key).get<int>(),index=0;
    bool changed=false;
    ImGui::PushID(key);
    for (const auto* choice:choices) {
        const bool enabled=!available || available(index);
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Selectable(choice,current==index)) {context.nodeSettings[key]=index;changed=true;}
        ImGui::EndDisabled();++index;
    }
    ImGui::PopID();return changed;
}
bool Toggle(RawWorkspaceEditContext& context,const char* key,const char* label,bool available=true) {
    bool value=context.nodeSettings.at(key).get<bool>();
    ImGui::BeginDisabled(!available);
    const bool changed=ImGui::Checkbox(label,&value);
    ImGui::EndDisabled();
    if (changed) context.nodeSettings[key]=value;
    return changed;
}
bool Draw(EditorModule&,RawWorkspaceEditContext& context,bool settings) {
    if (!context.nodeDraft || !context.nodeTarget) return false;
    auto& sliders=context.State<ImGuiExtras::FloatingSliderState>();
    ImGuiExtras::FloatingSliderScope scope(sliders);
    const auto type=context.nodeSettings.at("type").get<std::string>();
    const auto input=NodeMath::DescribeImageOperationInput(context.inputDescriptor);
    std::string reason;
    if (!NodeMath::ValidateStylizeInput(type,context.nodeSettings,input,reason)) ImGui::TextWrapped("%s",reason.c_str());
    bool changed=false;
    if (type=="CellShading") {
        if (!settings) {
            int levels=context.nodeSettings.at("levels");
            if (ImGui::SliderInt("Bands",&levels,2,12)) {context.nodeSettings["levels"]=levels;changed=true;}
            changed|=Number(context,"bias","Band position",-1,1);
            changed|=Number(context,"gamma","Band spacing",.1f,3);
            changed|=Toggle(context,"showEdges","Ink outlines");
            if (context.nodeSettings.at("showEdges").get<bool>()) {
                changed|=Number(context,"edgeStrength","Ink amount",0,200,"%.0f%%");
                changed|=Number(context,"edgeThickness","Outline width",.5f,5,"%.1f px");
            }
        } else {
            changed|=Choice(context,"quantMode","Shading basis",{"Luminance","Each component","HSV value"},
                [&](int mode){return mode==1 || input.colorRelationships;});
            changed|=Choice(context,"bandMap","Band mapping",{"Nearest bands","Ease band values","Even bins"});
            changed|=Choice(context,"edgeMethod","Outline detector",{"Sobel","Roberts cross","Laplacian"});
            ImGui::BeginDisabled(!input.colorRelationships || context.nodeSettings.at("quantMode").get<int>()!=0);
            changed|=Number(context,"colorPreserve","Retain color",0,100,"%.0f%%");
            ImGui::EndDisabled();
        }
    } else if (type=="PaletteReconstructor") {
        if (!settings) {
            changed|=Number(context,"blend","Palette amount",0,100,"%.0f%%");
            auto& palette=context.nodeSettings.at("palette");
            ImGui::TextUnformatted("Palette colors in the input RGB space");
            for (std::size_t i=0;i<palette.size();++i) {
                ImGui::PushID(static_cast<int>(i));
                float color[]{palette[i][0].get<float>(),palette[i][1].get<float>(),palette[i][2].get<float>()};
                if (ImGui::ColorEdit3("##color",color)) {palette[i]={color[0],color[1],color[2]};changed=true;}
                ImGui::PopID();
            }
            if (palette.size()<16 && ImGui::Button("Add color")) {palette.push_back({.5f,.5f,.5f});changed=true;}
            ImGui::SameLine();
            if (palette.size()>2 && ImGui::Button("Remove last color")) {palette.erase(palette.size()-1);changed=true;}
            context.nodeSettings["paletteCount"]=palette.size();
        } else {
            changed|=Number(context,"smoothing","Input smoothing",0,100,"%.0f");
            changed|=Choice(context,"smoothingType","Smoothing kernel",{"Box","Gaussian"});
            ImGui::TextWrapped("Palette matching uses RGB distance in the declared input space. Convert the input in Graph when another space is needed.");
        }
    } else if (type=="Halftoning") {
        if (!settings) {
            changed|=Number(context,"size","Screen cell size",1,20,"%.1f source px");
            changed|=Number(context,"intensity","Ink intensity",0,1,"%.2f");
            changed|=Number(context,"sharpness","Screen sharpness",0,1,"%.2f");
            changed|=Toggle(context,"invert","Invert screen");
        } else {
            changed|=Choice(context,"pattern","Screen shape",{"Circles","Lines","Cross","Diamond"});
            changed|=Choice(context,"colorMode","Screen separation",{"Luminance / Value","Each component","CMY","CMYK"},
                [&](int mode){return mode==1 || (mode==0 && (input.scalar || input.colorRelationships)) || (mode>=2 && input.fullRgb);});
            changed|=Toggle(context,"gray","Grayscale output",input.fullRgb);
        }
    }
    return changed;
}
bool Primary(EditorModule& editor,RawWorkspaceEditContext& context) {return Draw(editor,context,false);}
bool Settings(EditorModule& editor,RawWorkspaceEditContext& context) {return Draw(editor,context,true);}
bool Reset(EditorModule&,RawWorkspaceEditContext& context) {
    context.nodeSettings=GraphModel::DefaultLayerParameters(context.nodeDraft->layerType);return true;
}
}
std::vector<NodeEditorDefinition> StylizeEditors() {
    std::vector<NodeEditorDefinition> result;
    for (const auto type:{LayerType::CellShading,LayerType::PaletteReconstructor,LayerType::Halftoning}) {
        const auto* operation=EditorNodeGraphDefinitions::FindLiveNodeDefinition(EditorNodeGraph::NodeKind::Layer,static_cast<int>(type));
        if (!operation) continue;
        NodeEditorDefinition editor;editor.definitionId=operation->identity.id;
        editor.preferredSize={400,300};editor.primary=Primary;editor.settings=Settings;editor.reset=Reset;
        result.push_back(std::move(editor));
    }
    return result;
}
}
