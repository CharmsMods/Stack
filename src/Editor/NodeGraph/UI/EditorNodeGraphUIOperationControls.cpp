#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include "Utils/ImGuiExtras.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"

bool EditorNodeGraphUI::RenderRawScalarControl(const char* owner, const char* label, float* value,
    float minimum, float maximum, const char* format) {
    ImGui::PushID(owner);
    ImGuiExtras::FloatingSliderScope floatingSliders(m_RawSliderState);
    const bool changed = ImGuiExtras::FloatingSliderFloat(label,"##scalar",value,minimum,maximum,
        format,ImGui::GetContentRegionAvail().x);
    ImGui::PopID();
    return changed;
}

// The canvas and image editing surface use the same controls and edit actions.
// Individual operation designs can replace this presentation without copying state.
void EditorNodeGraphUI::RenderOperationControls(EditorModule* editor, int nodeId) {
    using namespace Stack::Editor::NodeGraphUIVisuals;
    auto& graph = editor->GetNodeGraph();
    auto* node = graph.FindNode(nodeId);
    if (!node) return;
    m_GraphContext = editor->GetGraphEditorContext();
    const auto metrics = MetricsForNode(*node);
    const auto style = BuildGraphStyleTokens(editor);
    const NodeSurfaceSpec surface{};
    const float width = std::max(40.f,ImGui::GetContentRegionAvail().x-4.f);
    const auto slider = [&](const char* label,const char* id,float* value,float low,float high) {
        return ImGuiExtras::NodeSliderFloat(label,id,value,low,high,"%.3f",width);
    };
    std::function<void()> pendingGraphAction;
    NodeContentRenderContext context{editor,graph,*node,metrics,surface,style,ImGui::GetWindowDrawList(),
        ImVec2(width,width*.6f),width,width,width,width,1.f,1.f,metrics.itemGap,metrics.sectionGap,
        false,true,[]{},slider,[]{ ImGui::Spacing(); },pendingGraphAction};
    ImGui::PushID(node->instanceUuid.c_str());
    const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(*node);
    std::size_t controls = 0;
    if (definition) for (const auto& parameter:definition->parameters)
        if (parameter.id != "layer-settings") ++controls;
    std::optional<ImGuiExtras::FloatingSliderScope> floatingSliders;
    if (controls == 1 || node->kind == EditorNodeGraph::NodeKind::ConstantChannel ||
        node->kind == EditorNodeGraph::NodeKind::Value ||
        (node->kind == EditorNodeGraph::NodeKind::MaskGenerator &&
         node->maskKind == EditorNodeGraph::MaskGeneratorKind::Solid))
        floatingSliders.emplace(m_RawSliderState);
    bool simpleValue = false;
    bool valueChanged = false;
    if (node->kind == EditorNodeGraph::NodeKind::Value &&
        node->value.value.availability == Stack::NodeMath::ValueAvailability::Known) {
        if (auto* scalar = std::get_if<double>(&node->value.value.payload)) {
            simpleValue = true;
            valueChanged = ImGuiExtras::FloatingSliderDouble("Value","##value",scalar,
                std::min(-16.,*scalar),std::max(16.,*scalar),"%.6g",width);
        } else if (auto* integer = std::get_if<std::int64_t>(&node->value.value.payload)) {
            simpleValue = true;
            valueChanged = ImGuiExtras::FloatingSliderInteger("Value","##value",integer,
                std::min<std::int64_t>(-100,*integer),std::max<std::int64_t>(100,*integer),"%lld",width);
        }
    } else if (node->kind == EditorNodeGraph::NodeKind::ConstantChannel) {
        simpleValue = true;
        valueChanged = ImGuiExtras::FloatingSliderFloat("Value","##value",&node->constantChannelSettings.value,
            std::min(-16.f,node->constantChannelSettings.value),std::max(16.f,node->constantChannelSettings.value),"%.6g",width,false);
    }
    if (valueChanged) editor->MarkGraphEdited(node->id);
    if (simpleValue) { /* The value is edited directly in its authored node. */ }
    else if (node->kind == EditorNodeGraph::NodeKind::Lut) editor->RenderLutControls(*node,width,true);
    else if (node->kind == EditorNodeGraph::NodeKind::CustomMask) editor->RenderMaskTextureControls(*node,width);
    else if (!(RenderSourceNodeContent(context) || RenderMaskDataNodeContent(context) ||
        RenderUtilityNodeContent(context)))
        ImGui::TextWrapped("This operation is controlled by its graph connections.");
    ImGui::PopID();
    if (pendingGraphAction) {
        pendingGraphAction();
    }
}
