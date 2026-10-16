#include "Editor/EditorModule.h"
#include "Project/RawNodeEditing.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "NodeMath/FirstClassValue.h"
#include "Project/GraphImagePayload.h"
#include "NodeMath/ToneCurveParameters.h"
#include "Utils/HashUtils.h"

std::size_t EditorModule::CurrentUniformSourceIdentity() const {return UniformSourceIdentityForFrame(m_TimelineUi.currentFrame);}
std::size_t EditorModule::UniformSourceIdentityForFrame(int frame) const {
    // Producer identity belongs to captured data, independently of the visible
    // workspace, inspection endpoint or selected editing container.
    std::size_t identity=0;
    const auto sources=[&](const EditorNodeGraph::Graph& graph,const std::string& owner) {
        for (const auto& node:graph.GetNodes()) {
            using Kind=EditorNodeGraph::NodeKind;
            if (node.kind==Kind::Image && node.role==Stack::GraphModel::NodeRole::Ordinary) {
                Stack::Project::EnsureSharedImagePixels(node.image);
                StackHash::HashCombine(identity,StackHash::HashValue(owner+"/"+node.instanceUuid));
                StackHash::HashCombine(identity,node.image.pixelsFingerprint);
                StackHash::HashCombine(identity,node.image.importRequestId);
                StackHash::HashCombine(identity,node.image.width);StackHash::HashCombine(identity,node.image.height);
                StackHash::HashCombine(identity,node.image.channels);
            } else if (node.kind==Kind::RawSource || node.kind==Kind::RawDevelopment || node.kind==Kind::RawProjectFrame || node.kind==Kind::RawProjectSourceSet) {
                StackHash::HashCombine(identity,StackHash::HashValue(owner+"/"+node.instanceUuid));
                if (node.kind==Kind::RawSource && !node.rawSource.metadata.sourceContentSha256.empty()) {
                    StackHash::HashCombine(identity,StackHash::HashValue(node.rawSource.metadata.sourceContentSha256));
                    StackHash::HashCombine(identity,node.rawSource.metadata.sourceByteSize);
                } else StackHash::HashCombine(identity,GetNodeDirtyGeneration(node.id));
            }
        }
    };
    sources(m_Project->graph,Stack::Project::kProjectGraphId);
    for (const auto* layer:Stack::Project::RawLayersInOrder(m_Project->rawLayers.State())) sources(layer->graph,layer->id);
    if (IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive())
        StackHash::HashCombine(identity,GetActiveRawWorkspacePreviewSourceHash());
    StackHash::HashCombine(identity,StackHash::HashValue(frame));
    return identity ? identity : 1;
}

void EditorModule::PublishUniformParameterFeedback(const GraphExecutionStats& stats) {
    const auto document=GetProjectDocumentId();
    const auto source=CurrentUniformSourceIdentity();
    const auto current=[&](const auto& value) {
        return value.documentId==document && value.revision==m_Project->editRevision && value.sourceIdentity==source;
    };
    auto& accepted=m_Project->uniformFeedback;
    accepted.erase(std::remove_if(accepted.begin(),accepted.end(),[&](const auto& value) { return !current(value); }),accepted.end());
    for (const auto& value:stats.uniformParameters) {
        if (!current(value)) continue;
        const auto found=std::find_if(accepted.begin(),accepted.end(),[&](const auto& previous) {
            return previous.graphId==value.graphId && previous.nodeUuid==value.nodeUuid && previous.parameterId==value.parameterId;
        });
        if (found==accepted.end()) accepted.push_back(value); else *found=value;
    }
}

Stack::NodeMath::UniformParameterFeedback EditorModule::GetUniformParameterState(
    const Stack::Project::NodeEditTarget& target, const std::string& parameterId) const {
    using namespace Stack;
    NodeMath::UniformParameterFeedback result;
    result.documentId=target.documentId; result.graphId=target.layerId;
    result.nodeUuid=target.nodeUuid; result.parameterId=parameterId;
    result.reason="Waiting for the connected uniform value.";
    if (target.documentId != GetProjectDocumentId()) return result;
    const auto* layer = Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),target.layerId);
    const auto* graph = target.layerId == Project::kProjectGraphId ? &m_Project->graph : layer ? &layer->graph : nullptr;
    result.revision = m_Project->editRevision;
    result.sourceIdentity=CurrentUniformSourceIdentity();
    if (!graph) return result;
    const EditorNodeGraph::Node* node=nullptr;
    for (const auto& candidate:graph->GetNodes()) if (candidate.instanceUuid==target.nodeUuid) { node=&candidate; break; }
    if (!node) return result;
    result.definitionHash=node->definitionHash;
    if (node->kind==EditorNodeGraph::NodeKind::RawOperation &&
        node->rawOperation.kind==RawRecipe::GraphOperationKind::ToneCurve &&
        !NodeMath::IsToneCurveParameterActive(node->rawOperation.parameters,parameterId)) {
        result.reason="This connected control is inactive in the current curve mode.";return result;
    }
    const auto* definition=EditorNodeGraphDefinitions::FindLiveNodeDefinition(*node);
    if (!definition) return result;
    const auto parameter=std::find_if(definition->parameters.begin(),definition->parameters.end(),
        [&](const auto& entry) { return entry.id==parameterId && entry.graphInputCapable; });
    if (parameter==definition->parameters.end()) return result;
    const auto* binding=graph->FindInputLink(node->id,EditorNodeGraph::ParameterInputSocketId(parameterId));
    const auto* source=binding ? graph->FindNode(binding->fromNodeId) : nullptr;
    if (!source) { result.reason="The uniform parameter has no connected producer."; return result; }
    if (source->kind==EditorNodeGraph::NodeKind::Value) {
        const auto& value=source->value.value;
        if (value.logicalType!=NodeMath::LogicalValueType::Scalar ||
            value.availability!=NodeMath::ValueAvailability::Known || !std::holds_alternative<double>(value.payload)) {
            result.reason="The connected Scalar Value is unavailable."; return result;
        }
        result.value=std::get<double>(value.payload);
        result.available=NodeMath::ValidateUniformParameter({parameter->id,parameter->label,parameter->storageKey,
            parameter->minimum,parameter->maximum,parameter->hasNumericDomain},result.value,result.reason);
        return result;
    }
    // Reductions may depend on image content. Only the current accepted worker
    // result can supply their effective value; never evaluate them in a widget.
    {
        for (const auto& feedback:m_Project->uniformFeedback)
            if (feedback.documentId==result.documentId && feedback.graphId==result.graphId &&
                feedback.nodeUuid==result.nodeUuid && feedback.parameterId==parameterId &&
                feedback.definitionHash==result.definitionHash && feedback.revision==result.revision &&
                feedback.sourceIdentity==result.sourceIdentity) return feedback;
    }
    return result;
}

void EditorModule::BindUniformParameterContext(RawWorkspaceEditContext& context, const EditorNodeGraph::Node& node) const {
    if (!context.nodeTarget) return;
    // The caller already decoded the authored operation. Only driven inputs
    // can require a different effective recipe.
    if (context.drivenParameters.empty()) return;
    auto effective=node.rawOperation;
    for (const auto& id:context.drivenParameters) {
        auto feedback=GetUniformParameterState(*context.nodeTarget,id);
        if (node.kind==EditorNodeGraph::NodeKind::RawOperation && feedback.available) {
            const auto* definition=EditorNodeGraphDefinitions::FindLiveNodeDefinition(node);
            for (const auto& parameter:definition->parameters) if (parameter.id==id)
                effective.parameters[nlohmann::json::json_pointer(parameter.storageKey)]=feedback.value;
        }
        context.uniformParameters.emplace(id,std::move(feedback));
    }
    if (node.kind==EditorNodeGraph::NodeKind::RawOperation) {
        auto recipe=Stack::RawRecipe::ReadGraphOperation(effective);
        recipe.source=context.recipe.source;
        recipe.technical=context.recipe.technical;
        recipe.cropRotation=context.recipe.cropRotation;
        context.recipe=std::move(recipe);
    }
}
