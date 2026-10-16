#include "Editor/NodeEditors/CoreAdjustmentEditors.h"
#include "Editor/NodeEditors/RawWorkspaceEditContext.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/LayerRegistry.h"
#include "Utils/FloatingSlider.h"
#include "Utils/ImGuiExtras.h"

namespace Stack::Editor {
namespace {
const EditorNodeGraphDefinitions::LiveNodeDefinition* Definition(const RawWorkspaceEditContext& context) {
    return context.nodeDraft ? EditorNodeGraphDefinitions::FindLiveNodeDefinition(*context.nodeDraft) : nullptr;
}
bool DrawAdjustment(EditorModule&,RawWorkspaceEditContext& context) {
    const auto* definition=Definition(context);
    if (!definition || !context.nodeTarget) return false;
    auto& sliders=context.State<ImGuiExtras::FloatingSliderState>();
    ImGuiExtras::FloatingSliderScope scope(sliders);
    bool changed=false;
    for (const auto& parameter:definition->parameters) {
        if (parameter.logicalType!=NodeMath::LogicalValueType::Scalar) continue;
        if (context.IsParameterDriven(parameter.id)) {
            const auto value=context.uniformParameters.find(parameter.id);
            ImGui::TextUnformatted(parameter.label.c_str());
            if (value!=context.uniformParameters.end() && value->second.available)
                ImGui::TextDisabled("Driven: %.6g",value->second.value);
            else ImGui::TextWrapped("%s",value==context.uniformParameters.end()
                ? "Waiting for the connected uniform value." : value->second.reason.c_str());
            continue;
        }
        double value=context.nodeSettings.value(parameter.storageKey,parameter.defaultValue.get<double>());
        ImGui::PushID(parameter.id.c_str());
        if (ImGuiExtras::FloatingSliderDouble(parameter.label.c_str(),"##value",&value,
            parameter.minimum,parameter.maximum,"%.3f",ImGui::GetContentRegionAvail().x)) {
            context.nodeSettings[parameter.storageKey]=value; changed=true;
        }
        ImGui::PopID();
    }
    return changed;
}
bool ResetAdjustment(EditorModule&,RawWorkspaceEditContext& context) {
    const auto* definition=Definition(context);
    if (!definition) return false;
    bool changed=false;
    for (const auto& parameter:definition->parameters) {
        if (parameter.logicalType!=NodeMath::LogicalValueType::Scalar || context.IsParameterDriven(parameter.id)) continue;
        if (context.nodeSettings[parameter.storageKey]!=parameter.defaultValue) {
            context.nodeSettings[parameter.storageKey]=parameter.defaultValue; changed=true;
        }
    }
    return changed;
}
}
std::vector<NodeEditorDefinition> CoreAdjustmentEditors() {
    std::vector<NodeEditorDefinition> result;
    for (const auto type:{LayerType::Brightness,LayerType::Contrast,LayerType::Saturation,LayerType::Warmth,LayerType::Sharpen}) {
        const auto* definition=EditorNodeGraphDefinitions::FindLiveNodeDefinition(EditorNodeGraph::NodeKind::Layer,static_cast<int>(type));
        if (!definition) continue;
        NodeEditorDefinition editor;
        editor.definitionId=definition->identity.id;
        editor.preferredSize={400,type==LayerType::Sharpen ? 160.f : 80.f};
        editor.primary=DrawAdjustment;
        editor.reset=ResetAdjustment;
        result.push_back(std::move(editor));
    }
    return result;
}
}
