#include "Renderer/Internal/RenderPipelineGraphExecutionRuntime.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

namespace Stack::Renderer::GraphExecution {
bool GraphExecutionRuntime::ApplyUniformInputs(const RenderGraphNode& authored, RenderGraphNode& evaluated) {
    bool ready = true;
    for (const auto& parameter : authored.uniformParameters) {
        const auto* binding = findInputLink(authored.nodeId,EditorNodeGraph::ParameterInputSocketId(parameter.id));
        if (!binding) continue;
        NodeMath::UniformParameterFeedback feedback;
        feedback.documentId=authored.authoredDocumentId;
        feedback.graphId=authored.authoredGraphId;
        feedback.nodeUuid=authored.authoredNodeUuid;
        feedback.definitionHash=authored.definitionHash;
        feedback.parameterId=parameter.id;
        feedback.revision=authored.authoredRevision;
        feedback.sourceIdentity=authored.uniformSourceIdentity;
        const bool known = evalScalar(binding->fromNodeId,binding->fromSocketId,feedback.value);
        if (!known) feedback.reason = parameter.label + ": " +
            (pipeline.m_LastGraphExecutionStats.lastReductionFailure.empty()
                ? "The connected uniform value is unavailable." : pipeline.m_LastGraphExecutionStats.lastReductionFailure);
        auto& payload = authored.kind == RenderGraphNodeKind::RawOperation
            ? evaluated.rawOperation.parameters : evaluated.layerJson;
        feedback.available = known && NodeMath::WriteUniformParameter(payload,parameter,feedback.value,feedback.reason);
        if (!feedback.available) {
            ready=false;
            pipeline.m_LastGraphExecutionStats.lastSpecializedFailureNodeId=authored.nodeId;
            pipeline.m_LastGraphExecutionStats.lastSpecializedFailure=feedback.reason;
        }
        pipeline.m_LastGraphExecutionStats.uniformParameters.push_back(std::move(feedback));
    }
    return ready;
}
}
